#include "CarData.hpp"
#include "Car_Log.hpp"
#include "ConfigManager.hpp"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <chrono>
#include <thread>
#include <csignal>
#include <cstring>
#include <iostream>

static bool sendAll(int fd, const void* buf, size_t size) {
    const char* p = static_cast<const char*>(buf);
    size_t total = 0;
    while (total < size) {
        ssize_t n = send(fd, p + total, size - total, 0);
        if (n <= 0) {
            return false;
        }
        total += static_cast<size_t>(n);
    }
    return true;
}

static bool recvAll(int fd, void* buf, size_t size) {
    char* p = static_cast<char*>(buf);
    size_t total = 0;
    while (total < size) {
        ssize_t n = recv(fd, p + total, size - total, 0);
        if (n <= 0) {
            return false;
        }
        total += static_cast<size_t>(n);
    }
    return true;
}

volatile bool g_running = true;
constexpr float AUTO_LOCK_SPEED_KMH = 20.0f;

bool sendRequest(const char* sock_path, Car::Msg& req, Car::Msg& resp);

static bool writeU8ToModule(const char* sock_path, Car::ModuleID mod_id, uint8_t item_id, uint8_t value) {
    Car::Msg req{}, resp{};
    req.msg_type = Car::MsgType::CMD;
    req.cmd_type = Car::CmdType::WRITE;
    req.mod_id = mod_id;
    req.item_id = item_id;
    req.val_type = Car::ValType::U8;
    req.value.u8 = value;
    return sendRequest(sock_path, req, resp) && resp.result == 0;
}

static bool writeI32ToModule(const char* sock_path, Car::ModuleID mod_id, uint8_t item_id, int32_t value) {
    Car::Msg req{}, resp{};
    req.msg_type = Car::MsgType::CMD;
    req.cmd_type = Car::CmdType::WRITE;
    req.mod_id = mod_id;
    req.item_id = item_id;
    req.val_type = Car::ValType::I32;
    req.value.i32 = value;
    return sendRequest(sock_path, req, resp) && resp.result == 0;
}

static bool writeF32ToModule(const char* sock_path, Car::ModuleID mod_id, uint8_t item_id, float value) {
    Car::Msg req{}, resp{};
    req.msg_type = Car::MsgType::CMD;
    req.cmd_type = Car::CmdType::WRITE;
    req.mod_id = mod_id;
    req.item_id = item_id;
    req.val_type = Car::ValType::F32;
    req.value.f32 = value;
    return sendRequest(sock_path, req, resp) && resp.result == 0;
}

static bool writeFaultCodesToModule(const uint16_t* codes) {
    Car::Msg req{}, resp{};
    req.msg_type = Car::MsgType::CMD;
    req.cmd_type = Car::CmdType::WRITE;
    req.mod_id = Car::ModuleID::FAULT;
    req.item_id = 2;
    req.val_type = Car::ValType::STR_U16;
    std::memcpy(req.value.arr_u16, codes, sizeof(req.value.arr_u16));
    return sendRequest(Car::SOCK_FAULT, req, resp) && resp.result == 0;
}

// 向指定模块发送请求并接收响应
bool sendRequest(const char* sock_path, Car::Msg& req, Car::Msg& resp) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return false;

    struct timeval tv = {1, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, sock_path, sizeof(addr.sun_path) - 1);

    bool ok = false;
    if (connect(fd, (sockaddr*)&addr, sizeof(addr)) == 0) {
        if (sendAll(fd, &req, sizeof(req)) && recvAll(fd, &resp, sizeof(resp))) {
            ok = true;
        }
    }

    close(fd);
    return ok;
}

// 模板函数：向指定的子模块发起查询并提取最新状态
template <typename T>
void fetchStateFromModule(const char* sock_path, Car::ModuleID mod_id, T* state_ptr) {
    Car::Msg cmd{}, resp{};
    cmd.msg_type = Car::MsgType::CMD;
    cmd.cmd_type = Car::CmdType::GET_ALL;
    cmd.mod_id = mod_id;

    if (sendRequest(sock_path, cmd, resp) && resp.result == 0) {
        if (resp.val_type == Car::ValType::STR_U8) {
            std::memcpy(state_ptr, resp.value.arr_u8, sizeof(T));
        }
    }
}

// 自动落锁规则：当车速达到阈值且当前未锁时，自动写入门锁状态为锁定
void applyAutoLockRule() {
    Car::StatusState status{};
    Car::DoorState door{};

    fetchStateFromModule(Car::SOCK_STATUS, Car::ModuleID::STATUS, &status);
    fetchStateFromModule(Car::SOCK_DOOR, Car::ModuleID::DOOR, &door);

    if (status.speed < AUTO_LOCK_SPEED_KMH || door.lock_status != 0) {
        return;
    }

    Car::Msg req{}, resp{};
    req.msg_type = Car::MsgType::CMD;
    req.mod_id = Car::ModuleID::DOOR;
    req.cmd_type = Car::CmdType::WRITE;
    req.item_id = 6; // lock_status
    req.val_type = Car::ValType::U8;
    req.value.u8 = 1;

    if (sendRequest(Car::SOCK_DOOR, req, resp) && resp.result == 0) {
        LOG_INFO("Auto lock applied at speed %.1f km/h", status.speed);
    }
}

// 聚合最新的设备状态并落盘保存
void syncAndSaveConfig() {
    auto& config = ConfigManager::getInstance();
    auto data = config.getData(); // 获取当前主程序数据副本

    // 发送 Epoll 查询，把分布在6个独立进程中的数据汇聚起来
    fetchStateFromModule(Car::SOCK_DOOR,   Car::ModuleID::DOOR,   &data.door);
    fetchStateFromModule(Car::SOCK_STATUS, Car::ModuleID::STATUS, &data.status);
    fetchStateFromModule(Car::SOCK_AIR,    Car::ModuleID::AIR,    &data.air);
    fetchStateFromModule(Car::SOCK_FAULT,  Car::ModuleID::FAULT,  &data.fault);

    // 把拉取到的最新数据写回单例，并触发完整的写文件操作
    config.setData(data);
    config.save();
}

void restoreStateToModules(const ConfigManager::FullCarData& data) {
    int failed = 0;

    failed += !writeU8ToModule(Car::SOCK_DOOR, Car::ModuleID::DOOR, 1, data.door.front_left);
    failed += !writeU8ToModule(Car::SOCK_DOOR, Car::ModuleID::DOOR, 2, data.door.front_right);
    failed += !writeU8ToModule(Car::SOCK_DOOR, Car::ModuleID::DOOR, 3, data.door.back_left);
    failed += !writeU8ToModule(Car::SOCK_DOOR, Car::ModuleID::DOOR, 4, data.door.back_right);
    failed += !writeU8ToModule(Car::SOCK_DOOR, Car::ModuleID::DOOR, 5, data.door.trunk);
    failed += !writeU8ToModule(Car::SOCK_DOOR, Car::ModuleID::DOOR, 6, data.door.lock_status);

    failed += !writeF32ToModule(Car::SOCK_STATUS, Car::ModuleID::STATUS, 1, data.status.speed);
    failed += !writeI32ToModule(Car::SOCK_STATUS, Car::ModuleID::STATUS, 2, data.status.rpm);
    failed += !writeF32ToModule(Car::SOCK_STATUS, Car::ModuleID::STATUS, 3, data.status.water_temp);
    failed += !writeF32ToModule(Car::SOCK_STATUS, Car::ModuleID::STATUS, 4, data.status.oil_temp);
    failed += !writeF32ToModule(Car::SOCK_STATUS, Car::ModuleID::STATUS, 5, data.status.fuel);
    failed += !writeF32ToModule(Car::SOCK_STATUS, Car::ModuleID::STATUS, 6, data.status.battery_voltage);
    failed += !writeU8ToModule(Car::SOCK_STATUS, Car::ModuleID::STATUS, 7, data.status.gear);
    failed += !writeU8ToModule(Car::SOCK_STATUS, Car::ModuleID::STATUS, 8, data.status.hand_brake);

    failed += !writeU8ToModule(Car::SOCK_AIR, Car::ModuleID::AIR, 1, data.air.ac_switch);
    failed += !writeU8ToModule(Car::SOCK_AIR, Car::ModuleID::AIR, 2, data.air.fan_speed);
    failed += !writeI32ToModule(Car::SOCK_AIR, Car::ModuleID::AIR, 3, data.air.temp_set);
    failed += !writeU8ToModule(Car::SOCK_AIR, Car::ModuleID::AIR, 4, data.air.inner_cycle);

    failed += !writeU8ToModule(Car::SOCK_FAULT, Car::ModuleID::FAULT, 1, data.fault.fault_count);
    failed += !writeFaultCodesToModule(data.fault.fault_codes);
    failed += !writeU8ToModule(Car::SOCK_FAULT, Car::ModuleID::FAULT, 3, data.fault.wring_light);

    if (failed == 0) {
        LOG_INFO("Restored persisted state to all module servers.");
    } else {
        LOG_WARN("Persisted state restore partially failed (%d writes failed).", failed);
    }
}

int main()
{
    Logger::getInstance().init("car_ctl.log", LogLevel::INFO);

    signal(SIGINT, [](int){ g_running = false; });
    signal(SIGTERM, [](int){ g_running = false; });

    auto& config = ConfigManager::getInstance();
    config.initDefaults();
    if (!config.load()) {
        LOG_WARN("Using default config values and writing initial config file.");
        config.save();
    }

    restoreStateToModules(config.getData());

    LOG_INFO("Central Controller Started. Now monitoring live modules...");

    auto last_save = std::chrono::steady_clock::now();
    auto last_auto_lock_check = std::chrono::steady_clock::now();

    while (g_running) {
        auto now = std::chrono::steady_clock::now();

        if (now - last_auto_lock_check >= std::chrono::seconds(1)) {
            applyAutoLockRule();
            last_auto_lock_check = now;
        }

        // 测试时改为10秒，正式环境可以改回1分钟
        if (now - last_save >= std::chrono::seconds(10)) {
            LOG_INFO("Saving current state...");
            syncAndSaveConfig();
            last_save = now;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100)); // 休息 100 毫秒，避免占用过多 CPU
    }

    syncAndSaveConfig();
    LOG_INFO("Central controller shut down safely.");
    return 0;
}