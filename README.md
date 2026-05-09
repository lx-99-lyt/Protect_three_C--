# 智能车载引擎 — 项目文档

## 目录

1. [项目简介](#1-项目简介)
2. [系统架构](#2-系统架构)
3. [目录结构](#3-目录结构)
4. [快速开始](#4-快速开始)
5. [模块说明](#5-模块说明)
6. [IPC 通信协议](#6-ipc-通信协议)
7. [car_ctl 命令行工具使用手册](#7-car_ctl-命令行工具使用手册)
8. [配置文件说明](#8-配置文件说明)
9. [日志系统](#9-日志系统)
10. [为项目新增一个模块](#10-为项目新增一个模块)

---

## 1. 项目简介

这是一个用 C++ 编写的车载中控后台系统，模拟真实车机的多进程架构。

整个系统由若干个**独立进程**组成，每个进程负责一个独立的车载功能模块（车门、空调、状态仪表、故障灯）。进程之间通过 **Unix Domain Socket** 进行 IPC 通信，一个中央主控进程负责统筹调度、状态持久化和安全规则执行。

**设计核心思路：**

- 各功能模块互相隔离，一个模块崩溃不影响其他模块
- 所有对模块的读写都通过统一的消息协议进行，没有共享内存竞争
- C++ RAII 机制确保所有 fd 资源在进程退出时自动释放，不泄露
- 状态持久化采用"写临时文件 → fsync → rename"的原子写法，断电不丢数据

---

## 2. 系统架构

```
┌──────────────────────────────────────────────────────┐
│                    car_ctl（命令行工具）               │
│            用于手动查询和写入任意模块字段               │
└────────────────────┬─────────────────────────────────┘
                     │ Unix Domain Socket
                     ▼
┌──────────────────────────────────────────────────────┐
│                  car_main（主控进程）                 │
│  - 启动时从 car_info.ini 恢复状态到各子模块             │
│  - 每秒检查自动落锁规则                                │
│  - 每 10 秒聚合所有模块状态并写盘                       │
│  - 监听 SIGINT/SIGTERM 优雅退出                        │
└──────┬───────────┬────────────┬──────────────┬────────┘
       │           │            │              │
       │ UDS       │ UDS        │ UDS          │ UDS
       ▼           ▼            ▼              ▼
  car_door    car_status    car_air       car_fault
  车门进程    状态仪表进程   空调进程       故障灯进程
```

每个子模块进程是一个独立的 Unix Domain Socket 服务端，基于 epoll 驱动事件循环，可以同时服务多个客户端连接。

---

## 3. 目录结构

```
.
├── CarData.hpp         # 核心数据定义：枚举、结构体、IPC 消息格式、Socket 路径常量
├── ModuleServer.hpp    # 子模块服务端基类（epoll + RAII + 纯虚接口）
├── CarModules.cpp      # 四个子模块的具体实现（通过编译宏区分）
├── main.cpp            # 主控进程：状态恢复、自动落锁、定期落盘
├── car_ctl.cpp         # 命令行调试工具
├── ConfigManager.hpp   # 配置管理器接口（单例）
├── ConfigManager.cpp   # 配置管理器实现：INI 读写、原子落盘
├── Car_Log.hpp         # 日志系统接口
├── Car_Log.cpp         # 日志系统实现：分级、自动轮转
└── Makefile            # 构建脚本
```

---

## 4. 快速开始

### 4.1 编译

需要支持 C++17 的 GCC（Linux 环境）。

```bash
mkdir build
cd build/
cmake ..
make
cd bin/
```

编译成功后，当前目录下会出现以下可执行文件：

| 文件         | 说明           |
|------------|--------------|
| `car_main`   | 主控进程         |
| `car_ctl`    | 命令行调试工具      |
| `car_door`   | 车门子模块进程      |
| `car_status` | 状态仪表子模块进程    |
| `car_air`    | 空调子模块进程      |
| `car_fault`  | 故障灯子模块进程     |

清理编译产物：

```bash
make clean
```

### 4.2 启动系统

**必须按顺序启动**，因为主控进程启动时会主动连接各子模块恢复状态，子模块需要先就绪。

推荐用六个独立终端窗口分别运行：

```bash
# 终端 1：启动车门进程
./car_door

# 终端 2：启动状态仪表进程
./car_status

# 终端 3：启动空调进程
./car_air

# 终端 4：启动故障灯进程
./car_fault

# 终端 5：启动主控进程（子模块全部就绪后再启动）
./car_main

# 终端 6：用命令行工具查询 / 写入
./car_ctl air get_all
```

### 4.3 停止系统

在每个进程的终端窗口按 `Ctrl+C`，各进程会捕获 SIGINT 信号并优雅退出，自动释放 socket 文件和 fd 资源。

主控进程退出前会自动执行一次最终的状态落盘，确保数据不丢失。

---

## 5. 模块说明

### 5.1 CarData.hpp — 数据契约层

整个系统的"公共语言"，所有进程都包含这个头文件。定义了：

**Socket 路径常量**

| 常量           | 路径                    | 对应模块   |
|--------------|-----------------------|--------|
| `SOCK_DOOR`   | `/tmp/car_door.sock`   | 车门进程   |
| `SOCK_STATUS` | `/tmp/car_status.sock` | 状态仪表进程 |
| `SOCK_AIR`    | `/tmp/car_air.sock`    | 空调进程   |
| `SOCK_FAULT`  | `/tmp/car_fault.sock`  | 故障灯进程  |

**状态结构体**（均以 `#pragma pack(1)` 禁止填充字节）

| 结构体          | 字段                                                              |
|-------------|-------------------------------------------------------------------|
| `DoorState`   | `front_left` `front_right` `back_left` `back_right` `trunk` `lock_status` |
| `StatusState` | `speed` `rpm` `water_temp` `oil_temp` `fuel` `battery_voltage` `gear` `hand_brake` |
| `AirState`    | `ac_switch` `fan_speed` `temp_set` `inner_cycle`                  |
| `FaultState`  | `fault_count` `fault_codes[10]` `wring_light`                     |

`gear` 字段取值来自 `Car::Gear` 枚举：`P=0` `R=1` `N=2` `D=3`

**编译期安全检查**：文件末尾有四条 `static_assert`，确保每个状态结构体都能装进 `Msg.value` union。如果以后扩充字段导致尺寸溢出，编译时就会报错。

---

### 5.2 ModuleServer.hpp — 子模块服务端基类

所有子模块进程的基础设施，不需要修改。提供：

- Unix Domain Socket 的 bind / listen / accept 全流程
- 基于 epoll 的事件驱动循环，支持多客户端并发
- RAII 析构：对象销毁时自动 `close(fd)` 并 `unlink` socket 文件
- `readFull` / `writeFull` 处理 TCP 粘包，保证每次完整收发一个 `Msg`
- 信号处理：`setupModuleSignalHandlers()` 注册 SIGINT/SIGTERM，`g_keep_running` 置 0 后事件循环退出

子类只需要继承 `ModuleServer` 并实现一个纯虚函数：

```cpp
virtual void processCommand(const Car::Msg& req, Car::Msg& resp) = 0;
```

基类负责网络收发，子类只管业务逻辑，两者完全解耦。

---

### 5.3 CarModules.cpp — 四个子模块实现

用 `#ifdef` 宏在同一个文件里放了四个独立的模块实现，编译时用 `-D` 宏激活其中一个。

每个模块的结构完全相同：持有一份对应的状态结构体 `m_state`，在 `processCommand` 里响应三种命令：

| 命令       | 行为                                  |
|----------|-------------------------------------|
| `GET_ALL` | 把整个 `m_state` 通过 `memcpy` 塞进响应的 `arr_u8` 返回 |
| `WRITE`   | 根据 `item_id` 更新 `m_state` 对应字段        |
| `READ`    | 根据 `item_id` 返回 `m_state` 对应字段的当前值    |

---

### 5.4 main.cpp — 主控进程

负责三件事：

**1. 启动时恢复状态**

读取 `car_info.ini`，把上次保存的状态逐字段写回各子模块进程。写入带重试机制（最多重试 10 次，每次间隔 200ms），容忍子模块启动较慢的情况。

**2. 自动落锁规则（每秒检查）**

当车速超过 20 km/h 且门锁处于未锁状态时，自动向车门模块发送锁门指令，并记录日志。

**3. 定期落盘（每 10 秒）**

向四个子模块各发一次 `GET_ALL` 查询，把最新状态聚合后写入 `car_info.ini`。写入采用原子写法，不会因为中途掉电产生损坏的配置文件。

---

### 5.5 ConfigManager — 配置管理器

单例模式，线程安全（`std::mutex` 保护）。

`save()` 的写盘流程：

```
1. 序列化到内存字符串
2. 写入 car_info.ini.tmp
3. fsync（刷到磁盘）
4. rename(tmp → car_info.ini)   ← 原子操作，要么成功要么保留旧文件
5. fsync 目录（确保目录项也落盘）
```

---

### 5.6 Logger — 日志系统

分四个级别：`DEBUG` `INFO` `WARN` `ERROR`，低于初始化时设定级别的日志会被过滤。

每条日志格式：
```
[2025-01-01 12:00:00] [INFO] [main.cpp:42] 消息内容
```

日志同时输出到终端和文件。文件超过 1MB 后自动轮转，最多保留 6 个历史文件（`.old.1` 到 `.old.6`）。

使用宏调用，自动填充文件名和行号：

```cpp
LOG_DEBUG("调试信息");
LOG_INFO("车速 %.1f km/h", speed);
LOG_WARN("配置文件未找到: %s", path);
LOG_ERROR("Socket 绑定失败");
```

---

## 6. IPC 通信协议

所有进程间通信都使用同一个固定长度的 `Car::Msg` 结构体，通过 Unix Domain Socket 传输。固定长度设计避免了粘包处理的复杂性。

### Msg 结构体字段

```
┌──────────┬──────────┬──────────┬──────────┬──────────┐
│ mod_id   │ msg_type │ cmd_type │ item_id  │ val_type │
│ (1 byte) │ (1 byte) │ (1 byte) │ (1 byte) │ (1 byte) │
└──────────┴──────────┴──────────┴──────────┴──────────┘
┌─────────────────────────────────────────────────────────────┐
│ value (union, 64 bytes)                                     │
│   u8       → uint8_t                                        │
│   i32      → int32_t                                        │
│   f32      → float                                          │
│   str      → char[64]                                       │
│   arr_u8   → uint8_t[32]   （用于 GET_ALL 返回整个状态结构体）  │
│   arr_u16  → uint16_t[32]  （用于故障码数组）                 │
│   arr_i32  → int32_t[32]                                    │
└─────────────────────────────────────────────────────────────┘
┌──────────┐
│ result   │
│ (4 byte) │  响应时使用，0 = 成功，非 0 = 错误码
└──────────┘
```

### 通信流程

```
客户端                              服务端（子模块进程）
  │                                      │
  │──── connect() ──────────────────────▶│
  │                                      │
  │──── send(Msg, sizeof(Msg)) ─────────▶│  readFull()
  │                                      │  processCommand()
  │◀─── recv(Msg, sizeof(Msg)) ──────────│  writeFull()
  │                                      │
  │──── close() ────────────────────────▶│
```

每次连接只传输一对请求/响应，完成后客户端主动关闭连接。

### item_id 字段对照表

**door 模块**

| item_id | 字段名         | 类型  | 说明           |
|---------|-------------|-----|--------------|
| 1       | front_left  | U8  | 前左门（0=关，1=开） |
| 2       | front_right | U8  | 前右门          |
| 3       | back_left   | U8  | 后左门          |
| 4       | back_right  | U8  | 后右门          |
| 5       | trunk       | U8  | 后备箱          |
| 6       | lock_status | U8  | 门锁（0=未锁，1=锁定）|

**status 模块**

| item_id | 字段名              | 类型  | 单位   |
|---------|-----------------|-----|------|
| 1       | speed           | F32 | km/h |
| 2       | rpm             | I32 | rpm  |
| 3       | water_temp      | F32 | °C   |
| 4       | oil_temp        | F32 | °C   |
| 5       | fuel            | F32 | %    |
| 6       | battery_voltage | F32 | V    |
| 7       | gear            | U8  | P/R/N/D = 0/1/2/3 |
| 8       | hand_brake      | U8  | 0=放下，1=拉起 |

**air 模块**

| item_id | 字段名          | 类型  | 说明              |
|---------|-------------|-----|-----------------|
| 1       | ac_switch   | U8  | 0=关，1=开         |
| 2       | fan_speed   | U8  | 风速档位（0-7）      |
| 3       | temp_set    | I32 | 设定温度（°C）        |
| 4       | inner_cycle | U8  | 0=外循环，1=内循环    |

**fault 模块**

| item_id | 字段名          | 类型     | 说明              |
|---------|-------------|--------|-----------------|
| 1       | fault_count | U8     | 当前故障数量          |
| 2       | fault_codes | STR_U16| 故障码数组（共 10 个槽位） |
| 3       | wring_light | U8     | 警告灯（0=灭，1=亮）    |

---

## 7. car_ctl 命令行工具使用手册

`car_ctl` 是一个独立的命令行调试工具，可以在系统运行时随时查询或修改任意模块的任意字段，无需重启任何进程。

### 语法

```bash
./car_ctl <模块> <命令> [参数...]
```

模块名：`door` `status` `air` `fault`

### 命令：get_all

查询模块的全部字段，格式化输出。

```bash
./car_ctl <模块> get_all
```

示例：

```bash
./car_ctl door get_all
```

输出：
```
============================
[ 车门状态 ]:
 前左门：关
 前右门：关
 后左门：关
 后右门：关
 后备箱：关
 锁状态：未锁
============================
```

```bash
./car_ctl status get_all
```

输出：
```
============================
[ 车辆状态 ]:
 速度：0 km/h
 转速：0 rpm
 水温：30 °C
 油温：40 °C
 油量：50 %
 电压：12 V
 档位：P (0)
 手刹：拉起
============================
```

### 命令：read

读取某个具体字段的当前值。

```bash
./car_ctl <模块> read <字段名>
```

示例：

```bash
./car_ctl air read temp_set      # 读取空调设定温度
./car_ctl status read speed      # 读取当前车速
./car_ctl door read lock_status  # 读取门锁状态
./car_ctl status read gear       # 读取当前档位（显示 P/R/N/D）
```

### 命令：write

修改某个字段的值，立即生效。

```bash
./car_ctl <模块> write <字段名> <值>
```

示例：

```bash
# 空调操作
./car_ctl air write ac_switch 1       # 开启空调
./car_ctl air write temp_set 26       # 设定温度 26°C
./car_ctl air write fan_speed 3       # 风速调到 3 档
./car_ctl air write inner_cycle 1     # 切换为内循环

# 车门操作
./car_ctl door write lock_status 1    # 锁门
./car_ctl door write front_left 1     # 开前左门

# 车辆状态（模拟测试用）
./car_ctl status write speed 80.0     # 模拟车速 80 km/h
./car_ctl status write gear D         # 切换到 D 档（支持 P/R/N/D 字母）
./car_ctl status write gear 3         # 同上，也支持数字
./car_ctl status write hand_brake 0   # 释放手刹
./car_ctl status write fuel 30.5      # 设置油量 30.5%

# 故障码操作
./car_ctl fault write wring_light 1   # 点亮警告灯
./car_ctl fault write fault_count 2   # 设置故障数量
```

### 常见错误提示

| 错误信息                    | 原因                      |
|-------------------------|-------------------------|
| `连接 /tmp/xxx.sock 失败`    | 对应子模块进程未启动               |
| `错误：未知模块 'xxx' 不存在`     | 模块名拼写错误                  |
| `错误：未知字段 'xxx' 不存在`     | 字段名拼写错误，参考 item_id 对照表   |
| `错误：value 格式非法`          | 填了非数字内容（gear 字段除外）       |
| `通信失败`                   | 子模块进程在通信中途崩溃或超时（3 秒）    |

---

## 8. 配置文件说明

配置文件路径：`./car_info.ini`（与可执行文件同目录）

系统启动时自动加载；若文件不存在，则用默认值启动并生成初始配置文件。

文件格式为标准 INI，每 10 秒由主控进程自动覆盖更新。手动编辑后需要重启主控进程才能生效。

```ini
# Car configuration file
# This file is automatically generated, do not edit manually

[door]
door_front_left = 0
door_front_right = 0
door_back_left = 0
door_back_right = 0
door_trunk = 0
lock_status = 0

[status]
speed = 0
rpm = 0
water_temp = 30
oil_temp = 40
fuel = 50
battery_voltage = 12
gear = P
gear_code = 0
hand_brake = 1

[air]
ac_switch = 0
fan_speed = 0
temp_set = 20
inner_cycle = 0

[fault]
fault_count = 0
wring_light = 0
fault_code_0 = 0
...
fault_code_9 = 0
```

**默认初始值说明：**

| 字段               | 默认值  | 含义      |
|------------------|------|---------|
| battery_voltage  | 12.0 | 正常车辆蓄电池电压 |
| fuel             | 50.0 | 半箱油     |
| gear             | P    | 停车档     |
| hand_brake       | 1    | 手刹拉起    |
| oil_temp         | 40.0 | 冷车油温    |
| water_temp       | 30.0 | 冷车水温    |
| temp_set         | 20   | 空调默认温度  |

---

## 9. 日志系统

**主控进程日志**：`./car_ctl.log`（与可执行文件同目录）

**子模块进程日志**：输出到各自的终端 stdout，不单独写文件。

日志分级过滤规则（从低到高）：`DEBUG` < `INFO` < `WARN` < `ERROR`

主控进程以 `INFO` 级别初始化，DEBUG 级别日志默认不输出。如需调试，修改 `main.cpp` 里的初始化参数：

```cpp
// main.cpp
Logger::getInstance().init("car_ctl.log", LogLevel::DEBUG);  // 改为 DEBUG 可看到所有日志
```

**日志轮转规则**：单个日志文件超过 1MB 后自动轮转，历史文件命名为 `.old.1` 到 `.old.6`，超出后最老的文件被删除。

---

## 10. 为项目新增一个模块

以新增"车灯模块"为例，说明完整步骤。

**第一步**：在 `CarData.hpp` 添加 Socket 路径常量、模块 ID 和状态结构体：

```cpp
constexpr const char* SOCK_LIGHT = "/tmp/car_light.sock";

enum class ModuleID : uint8_t { DOOR = 1, STATUS, AIR, FAULT, LIGHT };  // 追加 LIGHT

struct LightState { uint8_t headlight, tail_light, fog_light, turn_left, turn_right; };

// 别忘了追加 static_assert
static_assert(sizeof(LightState) <= sizeof(Msg::value), "LightState too large for Msg union");
```

**第二步**：在 `CarModules.cpp` 末尾新增模块实现：

```cpp
#ifdef BUILD_Car_Light
class LightModule : public ModuleServer {
    Car::LightState m_state{};
public:
    LightModule() : ModuleServer(Car::SOCK_LIGHT, "Car_Light") {}
protected:
    void processCommand(const Car::Msg& req, Car::Msg& resp) override {
        resp.mod_id = Car::ModuleID::LIGHT;
        resp.result = 0;
        if (req.cmd_type == Car::CmdType::GET_ALL) {
            resp.val_type = Car::ValType::STR_U8;
            std::memcpy(resp.value.arr_u8, &m_state, sizeof(m_state));
        }
        else if (req.cmd_type == Car::CmdType::WRITE) {
            if (req.item_id == 1) m_state.headlight  = req.value.u8;
            else if (req.item_id == 2) m_state.tail_light  = req.value.u8;
            else if (req.item_id == 3) m_state.fog_light   = req.value.u8;
            else if (req.item_id == 4) m_state.turn_left   = req.value.u8;
            else if (req.item_id == 5) m_state.turn_right  = req.value.u8;
            resp.value = req.value;
        }
        else if (req.cmd_type == Car::CmdType::READ) {
            resp.val_type = Car::ValType::U8;
            if      (req.item_id == 1) resp.value.u8 = m_state.headlight;
            else if (req.item_id == 2) resp.value.u8 = m_state.tail_light;
            else if (req.item_id == 3) resp.value.u8 = m_state.fog_light;
            else if (req.item_id == 4) resp.value.u8 = m_state.turn_left;
            else if (req.item_id == 5) resp.value.u8 = m_state.turn_right;
        }
    }
};
int main() { setupModuleSignalHandlers(); LightModule().start(); return 0; }
#endif
```

**第三步**：在 `Makefile` 添加编译目标：

```makefile
TARGETS := car_main car_ctl car_door car_status car_air car_fault car_light

car_light: CarModules.cpp
    $(CXX) $(CXXFLAGS) -DBUILD_Car_Light -o $@ $^
```

**第四步**：在 `car_ctl.cpp` 的路由表和字段字典里补充新模块的条目，之后就可以用 `./car_ctl light get_all` 等命令操作了。

**第五步**：在 `main.cpp` 的 `restoreStateToModules` 和 `syncAndSaveConfig` 里补充对新模块的写入和查询调用。

完成。新模块和现有系统完全解耦，不影响任何已有功能。