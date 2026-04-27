cmake_minimum_required(VERSION 3.17)

project(Car_Device_System VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(-Wall -Wextra -Wpedantic -O2)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

include_directories(${CMAKE_SOURCE_DIR}/include)

# 中控主程序
add_executable(car_main
	source/main.cpp
	source/Car_Log.cpp
	source/ConfigManager.cpp
)

# 客户端工具
add_executable(car_ctl
	source/client/car_ctl.cpp
)

# 当前仓库中可直接构建的服务端模块
add_executable(Car_Air
	source/server/CarModules.cpp
)
target_compile_definitions(Car_Air PRIVATE BUILD_Car_Air)

add_executable(Car_Door
	source/server/CarModules.cpp
)
target_compile_definitions(Car_Door PRIVATE BUILD_Car_Door)

add_executable(Car_Status
	source/server/CarModules.cpp
)
target_compile_definitions(Car_Status PRIVATE BUILD_Car_Status)

add_executable(Car_Fault
	source/server/CarModules.cpp
)
target_compile_definitions(Car_Fault PRIVATE BUILD_Car_Fault)