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

enum class LogLevel {
    Debug,
    Info,
    Warning,
    Error
};

class Logger {
public:
    static Logger& Instance() {
        static Logger instance;
        return instance;
    }

    void Enable(bool enable) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_enabled = enable;
    }

    void SetOutputPath(const char* path) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (path) {
            m_outputPath = path;
            m_file.open(path, std::ios::out | std::ios::app);
        } else {
            m_file.close();
            m_outputPath.clear();
        }
    }

    bool IsEnabled() const {
        return m_enabled;
    }

    void Log(LogLevel level, const char* format, ...) {
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
        } else {
            OutputDebugStringA(message.c_str());
        }
    }

private:
    Logger() = default;
    ~Logger() {
        if (m_file.is_open()) {
            m_file.close();
        }
    }

    const char* GetLevelString(LogLevel level) {
        switch (level) {
            case LogLevel::Debug:   return "DEBUG";
            case LogLevel::Info:    return "INFO";
            case LogLevel::Warning: return "WARN";
            case LogLevel::Error:   return "ERROR";
            default:                return "UNKNOWN";
        }
    }

    std::mutex m_mutex;
    bool m_enabled = false;
    std::string m_outputPath;
    std::ofstream m_file;
};

// Convenience macros
#define LOG_DEBUG(fmt, ...) Logger::Instance().Log(LogLevel::Debug, fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)  Logger::Instance().Log(LogLevel::Info, fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)  Logger::Instance().Log(LogLevel::Warning, fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) Logger::Instance().Log(LogLevel::Error, fmt, ##__VA_ARGS__)

} // namespace blurwindow
