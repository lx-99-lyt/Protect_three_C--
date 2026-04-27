#pragma once
#include <cstdint>
#include <string_view>

// 用namespace隔离代码，避免命名冲突
namespace Car{

// 定义socket路径
constexpr const char* SOCK_DOOR = "/tmp/car_door.sock";
constexpr const char* SOCK_STATUS = "/tmp/car_status.sock";
constexpr const char* SOCK_AIR = "/tmp/car_air.sock";
constexpr const char* SOCK_FAULT  = "/tmp/car_fault.sock";

constexpr const char* LOG_FILE_PATH = "./car_log.txt";
constexpr const char* INI_FILE_PATH = "./car_info.ini";
constexpr int MAX_FAULT_CODE = 10;
constexpr int DEVICE_NAME_LEN = 32;

enum class ModuleID : uint8_t { DOOR = 1, STATUS, AIR, FAULT };
enum class MsgType : uint8_t { CMD = 1, RESPONSE = 2 };
enum class CmdType : uint8_t { READ = 1, WRITE = 2, GET_ALL = 3 };
enum class ValType : uint8_t { U8, I32, F32, STR, STR_U8, STR_U16, STR_I32, STR_F32} ;
enum class Gear : uint8_t { P = 0, R = 1, N = 2, D = 3 };

// 设置内存对齐为1字节，确保结构体在网络传输时没有填充字节
#pragma pack(push, 1)
struct DoorState { uint8_t front_left, front_right, back_left, back_right, trunk, lock_status; };
struct StatusState { float speed; int rpm; float water_temp, oil_temp, fuel, battery_voltage; uint8_t gear, hand_brake; };
struct AirState { uint8_t ac_switch, fan_speed; int temp_set; uint8_t inner_cycle; };
struct FaultState { uint8_t fault_count; uint16_t fault_codes[MAX_FAULT_CODE]; uint8_t wring_light; };

// IPC消息结构体
struct Msg {
    ModuleID mod_id;
    MsgType msg_type;
    CmdType cmd_type;
    uint8_t item_id;
    ValType val_type;
    union {
        uint8_t u8;
        int32_t i32;
        float f32;
        char str[64];
        uint8_t arr_u8[32];
        uint16_t arr_u16[32];
        int32_t arr_i32[32];
    } value;
    int result; // 用于响应消息，表示操作结果
};

#pragma pack(pop) // 恢复默认内存对齐

}