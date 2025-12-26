#include "Logger.h"
#include <windows.h>
#include <string>
#include <fstream>
#include <mutex>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <cstdio>
#include <cstdarg>

namespace blurwindow {

Logger& Logger::Instance() {
    static Logger instance;
    return instance;
}

void Logger::Enable(bool enable) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_enabled = enable;
}

void Logger::SetOutputPath(const char* path) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (path) {
        m_outputPath = path;
        m_file.open(path, std::ios::out | std::ios::app);
    } else {
        m_file.close();
        m_outputPath.clear();
    }
}

void Logger::SetCallback(void (*callback)(const char*)) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_callback = callback;
}

bool Logger::IsEnabled() const {
    return m_enabled;
}

void Logger::Log(LogLevel level, const char* format, ...) {
    if (!m_enabled) return;

    std::lock_guard<std::mutex> lock(m_mutex);

    // Format timestamp
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf;
    localtime_s(&tm_buf, &time);

    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");
    oss << " [" << GetLevelString(level) << "] ";

    // Format message
    char buffer[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    oss << buffer << "\n";
    std::string message = oss.str();

    // Output
    if (m_file.is_open()) {
        m_file << message;
        m_file.flush();
    } else if (m_callback) {
        m_callback(message.c_str());
    } else {
        OutputDebugStringA(message.c_str());
    }
}

Logger::Logger() = default;
Logger::~Logger() {
    if (m_file.is_open()) {
        m_file.close();
    }
}

const char* Logger::GetLevelString(LogLevel level) {
    switch (level) {
        case LogLevel::Debug:   return "DEBUG";
        case LogLevel::Info:    return "INFO";
        case LogLevel::Warning: return "WARN";
        case LogLevel::Error:   return "ERROR";
        default:                return "UNKNOWN";
    }
}

} // namespace blurwindow
