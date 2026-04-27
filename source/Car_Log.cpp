#include "Car_Log.hpp"
#include <cstdarg>
#include <filesystem>
#include <iostream>
#include <chrono>
#include <iomanip>

namespace fs = std::filesystem;

Logger& Logger::getInstance() {
    static Logger instance;
    return instance;
}

Logger::~Logger() {
    if (m_file.is_open()) {
        m_file.close();
    }
}

void Logger::init(const std::string& path, LogLevel level) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_path = path;
    m_level = level;
    if (m_file.is_open()) {
        m_file.close();
    }
    m_file.open(m_path, std::ios::app);
}

void Logger::rotateIfNeeded() {
    constexpr uintmax_t MAX_SIZE = 1024 * 1024;
    if (!m_file.is_open() || fs::file_size(m_path) < MAX_SIZE) {
        return;
    }

    m_file.close();
    for (int i = 5; i >= 1; --i) {
        auto old_p = m_path + "." + std::to_string(i);
        auto new_p = m_path + "." + std::to_string(i + 1);
        if (fs::exists(old_p)) {
            fs::rename(old_p, new_p);
        }
    }
    fs::rename(m_path, m_path + ".old.1");
    m_file.open(m_path, std::ios::app);
}

void Logger::log(LogLevel level, const char* file, int line, const char* format, ...)
{
    if (level < m_level) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    rotateIfNeeded();

    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);

    const char* level_strs[] = {"DEBUG", "INFO", "WARN", "ERROR"};
    std::string filename = fs::path(file).filename().string();

    char buffer[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    auto print = [&](std::ostream& os) {
        os << "[" << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S") << "] "
           << "[" << level_strs[static_cast<int>(level)] << "] "
           << "[" << filename << ":" << line << "] "
           << buffer << "\n";
    };
    print(std::cout);
    if (m_file.is_open()) {
        print(m_file);
        m_file.flush();
    }
}