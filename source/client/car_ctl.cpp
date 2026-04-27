#include "CarData.hpp"
#include <iostream>
#include <string>
#include <unordered_map>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cstring>
#include <iomanip>
#include <cctype>

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

static const char* gearToText(uint8_t gear) {
    switch (static_cast<Car::Gear>(gear)) {
        case Car::Gear::P: return "P";
        case Car::Gear::R: return "R";
        case Car::Gear::N: return "N";
        case Car::Gear::D: return "D";
        default: return "UNKNOWN";
    }
}

static bool parseGearInput(const std::string& s, uint8_t& out) {
    if (s.size() == 1) {
        char c = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
        if (c == 'P') { out = static_cast<uint8_t>(Car::Gear::P); return true; }
        if (c == 'R') { out = static_cast<uint8_t>(Car::Gear::R); return true; }
        if (c == 'N') { out = static_cast<uint8_t>(Car::Gear::N); return true; }
        if (c == 'D') { out = static_cast<uint8_t>(Car::Gear::D); return true; }
    }

    try {
        int v = std::stoi(s);
        if (v < 0 || v > 3) {
            return false;
        }
        out = static_cast<uint8_t>(v);
        return true;
    }
    catch (const std::exception&) {
        return false;
    }
}

// 发送指令的通用封装
int sendCmd(const std::string& path, Car::Msg& cmd, Car::Msg& resp) {
    // 实现发送指令的逻辑
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    // 设置超时，防止服务端挂掉导致客户端卡死
    timeval tv{3, 0}; // 3秒超时
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

    if (connect(fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "连接 " << path << " 失败！\n";
        close(fd);
        return -1;
    }

    if (!sendAll(fd, &cmd, sizeof(cmd)) || !recvAll(fd, &resp, sizeof(resp))) {
        close(fd);
        return -1;
    }
    close(fd);
    return 0;
}

// 格式化打印 get_all 命令的响应结果
void printGetAll(Car::ModuleID mod_ID, const Car::Msg& resp) {
    if (resp.result != 0) {
        std::cerr << "操作失败，错误码: " << resp.result << "\n";
        return;
    }

    std::cout << "============================\n";
    switch (mod_ID) {
        case Car::ModuleID::DOOR: {
            Car::DoorState d;
            std::memcpy(&d, resp.value.arr_u8, sizeof(d));
            std::cout << "[ 车门状态 ]:\n"
                      << " 前左门：" << (d.front_left ? "开" : "关") << "\n"
                      << " 前右门：" << (d.front_right ? "开" : "关") << "\n"
                      << " 后左门：" << (d.back_left ? "开" : "关") << "\n"
                      << " 后右门：" << (d.back_right ? "开" : "关") << "\n"
                      << " 后备箱：" << (d.trunk ? "开" : "关") << "\n"
                      << " 锁状态：" << (d.lock_status ? "锁定" : "未锁") << "\n";
            break;
        }
        case Car::ModuleID::STATUS: {
            Car::StatusState s;
            std::memcpy(&s, resp.value.arr_u8, sizeof(s));
            std::cout << "[ 车辆状态 ]:\n"
                      << " 速度：" << s.speed << " km/h\n"
                      << " 转速：" << s.rpm << " rpm\n"
                      << " 水温：" << s.water_temp << " °C\n"
                      << " 油温：" << s.oil_temp << " °C\n"
                      << " 油量：" << s.fuel << " %\n"
                      << " 电压：" << s.battery_voltage << " V\n"
                      << " 档位：" << gearToText(s.gear) << " (" << (int)s.gear << ")\n"
                      << " 手刹：" << (s.hand_brake ? "拉起" : "放下") << "\n";
            break;
        }
        case Car::ModuleID::AIR: {
            Car::AirState a;
            std::memcpy(&a, resp.value.arr_u8, sizeof(a));
            std::cout << "[ 空调状态 ]:\n"
                      << " AC开关：" << (a.ac_switch ? "开" : "关") << "\n"
                      << " 风速：" << (int)a.fan_speed << "\n"
                      << " 设定温度：" << a.temp_set << " °C\n"
                      << " 内外循环：" << (a.inner_cycle ? "内循环" : "外循环") << "\n";
            break;
        }
        case Car::ModuleID::FAULT: {
            Car::FaultState f;
            std::memcpy(&f, resp.value.arr_u8, sizeof(f));
            std::cout << "[ 故障状态 ]:\n"
                      << " 故障数：" << (int)f.fault_count << "\n"
                      << " 故障码：";
            for (int i = 0; i < Car::MAX_FAULT_CODE; ++i) {
                if (f.fault_codes[i] != 0) // 只打印非0的故障码
                    std::cout << std::hex << std::setw(4) << std::setfill('0') << f.fault_codes[i] << " ";
            }
            std::cout << "\n"
                      << " 警告灯：" << (f.wring_light ? "亮" : "灭") << "\n";
            break;
        }
    }
    std::cout << "============================\n";
}

int main(int argc, char const *argv[])
{
    if (argc < 3) {
        std::cout << "Usage: \n"
                  << "  " << argv[0] << " <模块> get_all\n"
                  << "  " << argv[0] << " <模块> read <item_id>\n"
                  << "  " << argv[0] << " <模块> write <item_id> <value>\n"
                  << "示例：" << argv[0] << " air write temp_set 24\n";
        return 1;
    }

    std::string modStr = argv[1];
    std::string action = argv[2];

    // 1、模块路由表
    std::unordered_map<std::string, std::pair<Car::ModuleID, std::string>> routes = {
        {"door", {Car::ModuleID::DOOR, Car::SOCK_DOOR}},
        {"status", {Car::ModuleID::STATUS, Car::SOCK_STATUS}},
        {"air", {Car::ModuleID::AIR, Car::SOCK_AIR}},
        {"fault", {Car::ModuleID::FAULT, Car::SOCK_FAULT}}
    };
    
    if (routes.find(modStr) == routes.end()) {
        std::cerr << " 错误：未知模块 '" << modStr << " ' 不存在！\n";
        return 1;
    }

    auto [modID, sockPath] = routes[modStr];
    Car::Msg req{}, resp{};
    req.msg_type = Car::MsgType::CMD;
    req.mod_id = modID;

    // 字段字典 （映射 字段名 —> {ItemID, 数据类型}）
    struct ItemMeta { uint8_t id; Car::ValType type; };
    std::unordered_map<std::string, std::unordered_map<std::string, ItemMeta>> dict = {
        {"door", {
            {"front_left", {1, Car::ValType::U8}},
            {"front_right", {2, Car::ValType::U8}},
            {"back_left", {3, Car::ValType::U8}},
            {"back_right", {4, Car::ValType::U8}},
            {"trunk", {5, Car::ValType::U8}},
            {"lock_status", {6, Car::ValType::U8}}
        }},
        {"status", {
            {"speed", {1, Car::ValType::F32}},
            {"rpm", {2, Car::ValType::I32}},
            {"water_temp", {3, Car::ValType::F32}},
            {"oil_temp", {4, Car::ValType::F32}},
            {"fuel", {5, Car::ValType::F32}},
            {"battery_voltage", {6, Car::ValType::F32}},
            {"gear", {7, Car::ValType::U8}},
            {"hand_brake", {8, Car::ValType::U8}}
        }},
        {"air", {
            {"ac_switch", {1, Car::ValType::U8}},
            {"fan_speed", {2, Car::ValType::U8}},
            {"temp_set", {3, Car::ValType::I32}},
            {"inner_cycle", {4, Car::ValType::U8}}
        }},
        {"fault", {
            {"fault_count", {1, Car::ValType::U8}},
            {"fault_codes", {2, Car::ValType::STR_U16}}, // 数组类型特殊处理
            {"wring_light", {3, Car::ValType::U8}}
        }}
    };

    // 处理get_all命令
    if (action == "get_all") {
        req.cmd_type = Car::CmdType::GET_ALL;
        if (sendCmd(sockPath, req, resp) == 0) {
            printGetAll(modID, resp);
        }
    }

    // 处理read命令
    else if (action == "read") {
        if (argc < 4) {
            std::cerr << " 错误：缺少item_id参数！\n";
            return 1;
        }
        std::string itemStr = argv[3];
        if (dict[modStr].find(itemStr) == dict[modStr].end()) {
            std::cerr << " 错误：未知字段 '" << itemStr << "' 不存在！\n";
            return 1;
        }
        auto [itemID, valType] = dict[modStr][itemStr];
        req.cmd_type = Car::CmdType::READ;
        req.item_id = itemID;
        req.val_type = valType;

        if (sendCmd(sockPath, req, resp) == 0) {
            if (resp.result != 0) {
                std::cerr << "操作失败，错误码: " << resp.result << "\n";
                return 1;
            }

            // 终极化简：管它是什么字段，直接根据返回的数据类型拆盲盒！
            std::cout << "当前值为： ";
            switch (resp.val_type) {
                case Car::ValType::U8:
                    if (modStr == "status" && itemStr == "gear") {
                        std::cout << gearToText(resp.value.u8) << " (" << (int)resp.value.u8 << ")\n";
                    }
                    else {
                        // uint8_t 默认会被当成字符打印，所以要转成 int
                        std::cout << (int)resp.value.u8 << "\n";
                    }
                    break;

                case Car::ValType::I32:
                    std::cout << resp.value.i32 << "\n";
                    break;

                case Car::ValType::F32:
                    std::cout << resp.value.f32 << "\n";
                    break;

                case Car::ValType::STR_U16:
                    // 数组类型特殊处理
                    for (int i = 0; i < Car::MAX_FAULT_CODE; ++i) {
                        std::cout << resp.value.arr_u16[i] << " ";
                    }
                    std::cout << "\n";
                    break;

                default:
                    std::cout << "[不支持打印的类型]\n";
                    break;
            }
        }
        else {
            std::cerr << "通信失败！\n";
            return 1;
        }
    }

    // 处理write命令
    else if (action == "write") {
        if (argc != 5) {
            std::cerr << " 错误：write 命令参数数量不正确，应为 <item_id> <value>！\n";
            return 1;
        }

        std::string item = argv[3];
        std::string valStr = argv[4];

        if (dict[modStr].find(item) == dict[modStr].end()) {
            std::cerr << " 错误：未知字段 '" << item << "' 不存在！\n";
            return 1;
        }

        ItemMeta meta = dict[modStr][item];
        req.cmd_type = Car::CmdType::WRITE;
        req.item_id = meta.id;
        req.val_type = meta.type;

        // 根据类型转换字符串
        try {
            if (modStr == "status" && item == "gear") {
                if (!parseGearInput(valStr, req.value.u8)) {
                    std::cerr << " 错误：gear 仅支持 P/R/N/D 或 0/1/2/3！\n";
                    return 1;
                }
            }
            else if (meta.type == Car::ValType::U8) {
                req.value.u8 = static_cast<uint8_t>(std::stoi(valStr));
            }
            else if (meta.type == Car::ValType::I32) {
                req.value.i32 = std::stoi(valStr);
            }
            else if (meta.type == Car::ValType::F32) {
                req.value.f32 = std::stof(valStr);
            }
            else {
                std::cerr << " 错误：不支持的类型！\n";
                return 1;
            }
        }
        catch (const std::exception&) {
            std::cerr << " 错误：value 格式非法，请输入有效数字！\n";
            return 1;
        }

        if (sendCmd(sockPath, req, resp) == 0) {
            if (resp.result == 0) {
                std::cout << "写入成功！\n";
            }
            else {
                std::cerr << "写入失败，错误码: " << resp.result << "\n";
            }
        }
        else {
            std::cerr << "通信失败！\n";
            return 1;
        }
    }
    else {
        std::cerr << " 错误：未知命令 action，仅支持 get_all/read/write。\n";
        return 1;
    }
    return 0;
}
