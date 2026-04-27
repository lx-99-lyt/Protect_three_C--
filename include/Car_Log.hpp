#pragma once
#include <string>
#include <mutex>
#include <fstream>

enum class LogLevel { DEBUG, INFO, WARN, ERROR };

class Logger {
public:
    static Logger& getInstance();
    void init(const std::string& path, LogLevel level);
    void log(LogLevel level, const char* file, int line, const char* format, ...);
private:
    Logger() = default;
    ~Logger();
    void rotateIfNeeded();

    std::mutex m_mutex;
    std::ofstream m_file;
    std::string m_path;
    LogLevel m_level = LogLevel::INFO;
};

#define LOG_DEBUG(...) Logger::getInstance().log(LogLevel::DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_INFO(...) Logger::getInstance().log(LogLevel::INFO, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_WARN(...) Logger::getInstance().log(LogLevel::WARN, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_ERROR(...) Logger::getInstance().log(LogLevel::ERROR, __FILE__, __LINE__, __VA_ARGS__)