#include "ModuleServer.hpp"

// ===================空调模块====================
#ifdef BUILD_Car_Air
class AirModule : public ModuleServer {
    Car::AirState m_state{};
public:
    AirModule() : ModuleServer(Car::SOCK_AIR, "Car_Air") {}
protected:
    void processCommand(const Car::Msg& req, Car::Msg& resp) override {
        resp.mod_id = Car::ModuleID::AIR;
        resp.result = 0; // 默认成功
        if (req.cmd_type == Car::CmdType::GET_ALL) {
            resp.val_type = Car::ValType::STR_U8;
            std::memcpy(resp.value.arr_u8, &m_state, sizeof(m_state));
        }
        else if(req.cmd_type == Car::CmdType::WRITE) {
            if (req.item_id == 1) m_state.ac_switch = req.value.u8;
            else if (req.item_id == 2) m_state.fan_speed = req.value.u8;
            else if (req.item_id == 3) m_state.temp_set = req.value.i32;
            else if (req.item_id == 4) m_state.inner_cycle = req.value.u8;
            resp.value = req.value;
        }
        else if(req.cmd_type == Car::CmdType::READ) {
            if (req.item_id == 1) {
                resp.val_type = Car::ValType::U8;
                resp.value.u8 = m_state.ac_switch;
            }
            else if (req.item_id == 2) {
                resp.val_type = Car::ValType::U8;
                resp.value.u8 = m_state.fan_speed;
            }
            else if (req.item_id == 3) {
                resp.val_type = Car::ValType::I32;
                resp.value.i32 = m_state.temp_set;
            }
            else if (req.item_id == 4) {
                resp.val_type = Car::ValType::U8;
                resp.value.u8 = m_state.inner_cycle;
            }
        }
    }
};
int main() {AirModule().start(); return 0;}
#endif

// ===================车门模块====================
// 类似AirModule， 只需要改构造函数传入不同的路径和名字， 以及实现processCommand函数处理车门相关的命令即可
#ifdef BUILD_Car_Door
class DoorModule : public ModuleServer {
    Car::DoorState m_state{};
public:
    DoorModule() : ModuleServer(Car::SOCK_DOOR, "Car_Door") {}
protected:
    void processCommand(const Car::Msg& req, Car::Msg& resp) override {
        resp.mod_id = Car::ModuleID::DOOR;
        resp.result = 0; // 默认成功
        if (req.cmd_type == Car::CmdType::GET_ALL) {
            resp.val_type = Car::ValType::STR_U8;
            std::memcpy(resp.value.arr_u8, &m_state, sizeof(m_state));
        }
        else if(req.cmd_type == Car::CmdType::WRITE) {
            if (req.item_id == 1) m_state.front_left = req.value.u8;
            else if (req.item_id == 2) m_state.front_right = req.value.u8;
            else if (req.item_id == 3) m_state.back_left = req.value.u8;
            else if (req.item_id == 4) m_state.back_right = req.value.u8;
            else if (req.item_id == 5) m_state.trunk = req.value.u8;
            else if (req.item_id == 6) m_state.lock_status = req.value.u8;
            resp.value = req.value;
        }
        else if(req.cmd_type == Car::CmdType::READ) {
            if (req.item_id == 1) {
                resp.val_type = Car::ValType::U8;
                resp.value.u8 = m_state.front_left;
            }
            else if (req.item_id == 2) {
                resp.val_type = Car::ValType::U8;
                resp.value.u8 = m_state.front_right;
            }
            else if (req.item_id == 3) {
                resp.val_type = Car::ValType::U8;
                resp.value.u8 = m_state.back_left;
            }
            else if (req.item_id == 4) {
                resp.val_type = Car::ValType::U8;
                resp.value.u8 = m_state.back_right;
            }
            else if (req.item_id == 5) {
                resp.val_type = Car::ValType::U8;
                resp.value.u8 = m_state.trunk;
            }
            else if (req.item_id == 6) {
                resp.val_type = Car::ValType::U8;
                resp.value.u8 = m_state.lock_status;
            }
        }
    }
};
int main() {DoorModule().start(); return 0;}
#endif

// ===================状态模块====================
// 类似AirModule， 只需要改构造函数传入不同的路径和名字， 以及实现processCommand函数处理状态相关的命令即可
#ifdef BUILD_Car_Status
class StatusModule : public ModuleServer {
    Car::StatusState m_state{};
public:
    StatusModule() : ModuleServer(Car::SOCK_STATUS, "Car_Status") {}
protected:
    void processCommand(const Car::Msg& req, Car::Msg& resp) override {
        resp.mod_id = Car::ModuleID::STATUS;
        resp.result = 0; // 默认成功
        if (req.cmd_type == Car::CmdType::GET_ALL) {
            resp.val_type = Car::ValType::STR_U8;
            std::memcpy(resp.value.arr_u8, &m_state, sizeof(m_state));
        }
        else if(req.cmd_type == Car::CmdType::WRITE) {
            if (req.item_id == 1) m_state.speed = req.value.f32;
            else if (req.item_id == 2) m_state.rpm = req.value.i32;
            else if (req.item_id == 3) m_state.water_temp = req.value.f32;
            else if (req.item_id == 4) m_state.oil_temp = req.value.f32;
            else if (req.item_id == 5) m_state.fuel = req.value.f32;
            else if (req.item_id == 6) m_state.battery_voltage = req.value.f32;
            else if (req.item_id == 7) m_state.gear = req.value.u8;
            else if (req.item_id == 8) m_state.hand_brake = req.value.u8;
            resp.value = req.value;
        }
        else if(req.cmd_type == Car::CmdType::READ) {
            if (req.item_id == 1) {
                resp.val_type = Car::ValType::F32;
                resp.value.f32 = m_state.speed;
            }
            else if (req.item_id == 2) {
                resp.val_type = Car::ValType::I32;
                resp.value.i32 = m_state.rpm;
            }
            else if (req.item_id == 3) {
                resp.val_type = Car::ValType::F32;
                resp.value.f32 = m_state.water_temp;
            }
            else if (req.item_id == 4) {
                resp.val_type = Car::ValType::F32;
                resp.value.f32 = m_state.oil_temp;
            }
            else if (req.item_id == 5) {
                resp.val_type = Car::ValType::F32;
                resp.value.f32 = m_state.fuel;
            }
            else if (req.item_id == 6) {
                resp.val_type = Car::ValType::F32;
                resp.value.f32 = m_state.battery_voltage;
            }
            else if (req.item_id == 7) {
                resp.val_type = Car::ValType::U8;
                resp.value.u8 = m_state.gear;
            }
            else if (req.item_id == 8) {
                resp.val_type = Car::ValType::U8;
                resp.value.u8 = m_state.hand_brake;
            }
        }
    }
};
int main() {StatusModule().start(); return 0;}
#endif

// ===================故障模块====================
// 类似AirModule， 只需要改构造函数传入不同的路径和名字， 以及实现processCommand函数处理故障相关的命令即可
#ifdef BUILD_Car_Fault
class FaultModule : public ModuleServer {
    Car::FaultState m_state{};
public:
    FaultModule() : ModuleServer(Car::SOCK_FAULT, "Car_Fault") {}
protected:
    void processCommand(const Car::Msg& req, Car::Msg& resp) override {
        resp.mod_id = Car::ModuleID::FAULT;
        resp.result = 0; // 默认成功
        if (req.cmd_type == Car::CmdType::GET_ALL) {
            resp.val_type = Car::ValType::STR_U8;
            std::memcpy(resp.value.arr_u8, &m_state, sizeof(m_state));
        }
        else if(req.cmd_type == Car::CmdType::WRITE) {
            if (req.item_id == 1) m_state.fault_count = req.value.u8;
            else if (req.item_id == 2) std::memcpy(m_state.fault_codes, req.value.arr_u16, sizeof(m_state.fault_codes));
            else if (req.item_id == 3) m_state.wring_light = req.value.u8;
            resp.value = req.value;
        }
        else if(req.cmd_type == Car::CmdType::READ) {
            if (req.item_id == 1) {
                resp.val_type = Car::ValType::U8;
                resp.value.u8 = m_state.fault_count;
            }
            else if (req.item_id == 2) {
                resp.val_type = Car::ValType::STR_U16;
                std::memcpy(resp.value.arr_u16, m_state.fault_codes, sizeof(m_state.fault_codes));
            }
            else if (req.item_id == 3) {
                resp.val_type = Car::ValType::U8;
                resp.value.u8 = m_state.wring_light;
            }
        }
    }
};
int main() {FaultModule().start(); return 0;}
#endif