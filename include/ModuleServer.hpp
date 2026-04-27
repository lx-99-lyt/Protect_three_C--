#pragma once
#include "CarData.hpp"
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <string>
#include <cstring>
#include <iostream>
#include <cerrno>

inline volatile bool g_keep_running = true;

class ModuleServer {
public:
    // 构造函数，初始化时传入路径和名字
    ModuleServer(const std::string& path, const std::string& name) : m_path(path), m_name(name){}

    // 析构函数； RAII 机制， 对象销毁时自动关闭fd, 绝不泄露
    virtual ~ModuleServer() {
        if (m_server_fd != -1) close(m_server_fd);
        if (m_epoll_fd != -1) close(m_epoll_fd);
        unlink(m_path.c_str());
        std::cout << m_name << " 资源已安全释放。" << std::endl;
    }

    // 启动服务器
    void start() {
        // 创建socket
        m_server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (m_server_fd < 0) {
            perror("socket");
            return;
        }

        // 绑定路径
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, m_path.c_str(), sizeof(addr.sun_path) - 1);
        unlink(m_path.c_str()); // 确保路径不存在
        if (bind(m_server_fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
            perror("bind");
            close(m_server_fd);
            m_server_fd = -1;
            return;
        }

        // 监听
        if (listen(m_server_fd, 5) < 0) {
            perror("listen");
            close(m_server_fd);
            m_server_fd = -1;
            unlink(m_path.c_str());
            return;
        }
        
        // 配置Epoll
        m_epoll_fd = epoll_create1(0);
        if (m_epoll_fd < 0) {
            perror("epoll_create1");
            close(m_server_fd);
            m_server_fd = -1;
            unlink(m_path.c_str());
            return;
        }

        epoll_event ev{};
        ev.events = EPOLLIN;
        ev.data.fd = m_server_fd;
        if (epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, m_server_fd, &ev) < 0) {
            perror("epoll_ctl");
            close(m_epoll_fd);
            close(m_server_fd);
            m_epoll_fd = -1;
            m_server_fd = -1;
            unlink(m_path.c_str());
            return;
        }

        std::cout << m_name << " 服务已启动，监听路径: " << m_path << "\n";

        // 事件循环
        runLoop();
    }

protected:
    // 纯虚函数，老父亲不实现， 逼着子类必须实现具体业务逻辑
    // 当收到消息时， 老父亲会把收到的req传给函数，要求子类填好resp
    virtual void processCommand(const Car::Msg&req, Car::Msg& resp) = 0;

private:
    static bool readFull(int fd, void* buf, size_t size) {
        char* p = static_cast<char*>(buf);
        size_t total = 0;
        while (total < size) {
            ssize_t n = read(fd, p + total, size - total);
            if (n == 0) {
                return false;
            }
            if (n < 0) {
                if (errno == EINTR) {
                    continue;
                }
                return false;
            }
            total += static_cast<size_t>(n);
        }
        return true;
    }

    static bool writeFull(int fd, const void* buf, size_t size) {
        const char* p = static_cast<const char*>(buf);
        size_t total = 0;
        while (total < size) {
            ssize_t n = send(fd, p + total, size - total, 0);
            if (n <= 0) {
                if (n < 0 && errno == EINTR) {
                    continue;
                }
                return false;
            }
            total += static_cast<size_t>(n);
        }
        return true;
    }

    void runLoop() {
        epoll_event events[10];
        while (g_keep_running) {
            int nfds = epoll_wait(m_epoll_fd, events, 10, 1000);
            if (nfds < 0) {
                if (errno == EINTR) {
                    continue;
                }
                perror("epoll_wait");
                break;
            }
            for (int i = 0; i < nfds; ++i) {
                int fd = events[i].data.fd;
                if (fd == m_server_fd) {
                    // 处理新连接
                    int client_fd = accept(m_server_fd, nullptr, nullptr);
                    if (client_fd < 0) {
                        continue;
                    }
                    epoll_event ev{EPOLLIN, {.fd = client_fd}};
                    if (epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, client_fd, &ev) < 0) {
                        close(client_fd);
                    }
                }
                else {
                    // 处理客户端发来的消息
                    Car::Msg req{}, resp{};
                    if (!readFull(fd, &req, sizeof(req))) {
                        close(fd);
                        epoll_ctl(m_epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
                        continue;
                    }

                    // 准备相应头的基本信息
                    resp.msg_type = Car::MsgType::RESPONSE;
                    resp.mod_id = req.mod_id;
                    resp.result = 0; // 默认成功，子类可以修改这个值表示不同的结果

                    // [核心] 调用子类实现的业务逻辑
                    processCommand(req, resp);

                    // 发送响应
                    if (!writeFull(fd, &resp, sizeof(resp))) {
                        close(fd);
                        epoll_ctl(m_epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
                    }
                }
            }
        }
    }
    std::string m_path, m_name;
    int m_server_fd{-1}, m_epoll_fd{-1};
};