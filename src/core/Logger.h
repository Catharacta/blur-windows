#pragma once

#include <windows.h>
#include <mutex>
#include <string>
#include <fstream>

namespace blurwindow {

enum class LogLevel {
    Debug,
    Info,
    Warning,
    Error
};

class Logger {
public:
    static Logger& Instance();
    void Enable(bool enable);
    void SetOutputPath(const char* path);
    void SetCallback(void (*callback)(const char*));
    bool IsEnabled() const;
    void Log(LogLevel level, const char* format, ...);

private:
    Logger();
    ~Logger();
    const char* GetLevelString(LogLevel level);

    std::mutex m_mutex;
    bool m_enabled = false;
    std::string m_outputPath;
    std::ofstream m_file;
    void (*m_callback)(const char*) = nullptr;
};

// Convenience macros
#define LOG_DEBUG(fmt, ...) Logger::Instance().Log(LogLevel::Debug, fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)  Logger::Instance().Log(LogLevel::Info, fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)  Logger::Instance().Log(LogLevel::Warning, fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) Logger::Instance().Log(LogLevel::Error, fmt, ##__VA_ARGS__)

} // namespace blurwindow
