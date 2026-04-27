#pragma once
#include "CarData.hpp"
#include <mutex>

class ConfigManager {
public:
    struct FullCarData {
        Car::DoorState door{};
        Car::StatusState status{};
        Car::AirState air{};
        Car::FaultState fault{};
    };

    static ConfigManager& getInstance();
    void initDefaults();
    bool load();
    bool save();
    
    FullCarData getData();
    void setData(const FullCarData& data);

private:
    ConfigManager() = default;
    FullCarData m_data;
    std::mutex m_mutex;
};