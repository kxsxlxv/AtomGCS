#include "Logger.h"

#include <iostream>
#include <chrono>
#include <ctime>
#include <cstdio>
#include <algorithm>

// Кроссплатформенные заголовки для поддержки цветного вывода
#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
#endif
#include <filesystem>

namespace gcs {

// ═══════════════════════════════════════════════════════════════════
// ANSI-коды цветов
// ═══════════════════════════════════════════════════════════════════

namespace ansi {
    constexpr const char* reset   = "\033[0m";
    constexpr const char* gray    = "\033[90m";    // Trace
    constexpr const char* cyan    = "\033[36m";    // Debug
    constexpr const char* green   = "\033[32m";    // Info
    constexpr const char* yellow  = "\033[33m";    // Warning
    constexpr const char* red     = "\033[31m";    // Error
    constexpr const char* boldRed = "\033[1;31m";  // Critical
    constexpr const char* dim     = "\033[2m";     // Приглушённый (для file:line)
} // namespace ansi

// ═══════════════════════════════════════════════════════════════════
// Singleton
// ═══════════════════════════════════════════════════════════════════

Logger& Logger::instance() {
    static Logger inst;
    return inst;
}

Logger::~Logger() {
    shutdown();
}

// ═══════════════════════════════════════════════════════════════════
// Инициализация / завершение
// ═══════════════════════════════════════════════════════════════════

void Logger::initialize(LogLevel minLevel, bool toFile,
                         const std::string& logDir) {
    std::lock_guard<std::mutex> lock(mutex_);

    minLevel_.store(minLevel);

    enableAnsiColors();

    // Инициализация файлового вывода
    if (toFile) {
        try {
            // Создаём директорию для логов если её нет
            std::filesystem::create_directories(logDir);

            // Формируем имя файла: gcs_2024-01-15.log
            auto now = std::chrono::system_clock::now();
            auto timeT = std::chrono::system_clock::to_time_t(now);
            std::tm tm{};
#ifdef _WIN32
            localtime_s(&tm, &timeT);
#else
            localtime_r(&timeT, &tm);
#endif
            char dateBuf[16];
            std::strftime(dateBuf, sizeof(dateBuf), "%Y-%m-%d", &tm);

            std::string filePath = logDir + "/gcs_"
                                   + std::string(dateBuf) + ".log";

            logFile_.open(filePath, std::ios::app);
            if (logFile_.is_open()) {
                fileEnabled_ = true;
                logFile_ << "\n"
                         << "════════════════════════════════════════\n"
                         << " gcs — сессия " << getFullTimestamp() << "\n"
                         << "════════════════════════════════════════\n"
                         << std::endl;
            } else {
                std::cerr << "[Logger] Не удалось открыть лог-файл: "
                          << filePath << std::endl;
            }
        } catch (const std::exception& e) {
            std::cerr << "[Logger] Ошибка создания директории логов: "
                      << e.what() << std::endl;
        }
    }

    initialized_ = true;
}

void Logger::shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (fileEnabled_ && logFile_.is_open()) {
        logFile_ << "\n[" << getFullTimestamp()
                 << "] Сессия завершена\n" << std::endl;
        logFile_.close();
        fileEnabled_ = false;
    }

    initialized_ = false;
}

// ═══════════════════════════════════════════════════════════════════
// Основной метод записи
// ═══════════════════════════════════════════════════════════════════

void Logger::log(LogLevel level, const std::string& message,
                  const char* file, int line) {
    // Быстрая проверка уровня без захвата мьютекса
    if (static_cast<int>(level) < static_cast<int>(minLevel_.load())) {
        return;
    }

    std::string timestamp = getTimestamp();

    std::lock_guard<std::mutex> lock(mutex_);

    // 1. Вывод в консоль
    writeToConsole(level, timestamp, message, file, line);

    // 2. Запись в файл
    if (fileEnabled_) {
        writeToFile(level, timestamp, message, file, line);
    }

    // 3. Добавление в кольцевой буфер (для UI)
    if (entries_.size() >= maxEntries_) {
        entries_.pop_front();
    }
    entries_.push_back({level, timestamp, message});
}

// ═══════════════════════════════════════════════════════════════════
// Вывод в консоль с цветами
// ═══════════════════════════════════════════════════════════════════

void Logger::writeToConsole(LogLevel level, const std::string& timestamp,
                             const std::string& message,
                             const char* file, int line) {
    // Метки уровней (выровнены до 5 символов)
    static const char* levelLabels[] = {
        "TRACE", "DEBUG", "INFO ", "WARN ", "ERROR", "CRIT "
    };

    int idx = static_cast<int>(level);
    if (idx < 0 || idx > 5) idx = 2; // Info по умолчанию

    const char* label = levelLabels[idx];
    const char* color = colorsEnabled_ ? getColorCode(level) : "";
    const char* reset = colorsEnabled_ ? ansi::reset : "";
    const char* dim   = colorsEnabled_ ? ansi::dim : "";

    // Основная строка: [HH:MM:SS.mmm] [LEVEL] Сообщение
    std::fprintf(stderr, "%s[%s]%s %s[%s]%s %s",
                 dim, timestamp.c_str(), reset,
                 color, label, reset,
                 message.c_str());

    // Информация о файле/строке (только для Trace и Debug)
    if (file && (level == LogLevel::Trace || level == LogLevel::Debug)) {
        const char* shortFile = extractFilename(file);
        std::fprintf(stderr, " %s(%s:%d)%s",
                     dim, shortFile, line, reset);
    }

    std::fprintf(stderr, "\n");
}

// ═══════════════════════════════════════════════════════════════════
// Запись в файл (без цветовых кодов)
// ═══════════════════════════════════════════════════════════════════

void Logger::writeToFile(LogLevel level, const std::string& timestamp,
                          const std::string& message,
                          const char* file, int line) {
    if (!logFile_.is_open()) return;

    static const char* levelLabels[] = {
        "TRACE", "DEBUG", "INFO ", "WARN ", "ERROR", "CRIT "
    };

    int idx = static_cast<int>(level);
    if (idx < 0 || idx > 5) idx = 2;

    logFile_ << "[" << timestamp << "] "
             << "[" << levelLabels[idx] << "] "
             << message;

    if (file) {
        logFile_ << " (" << extractFilename(file) << ":" << line << ")";
    }

    logFile_ << "\n";

    // Сбрасываем буфер для важных сообщений
    if (level >= LogLevel::Warning) {
        logFile_.flush();
    }
}

// ═══════════════════════════════════════════════════════════════════
// Кольцевой буфер для UI
// ═══════════════════════════════════════════════════════════════════

std::vector<Logger::LogEntry> Logger::getEntries() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return {entries_.begin(), entries_.end()};
}

size_t Logger::getEntryCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return entries_.size();
}

void Logger::clearEntries() {
    std::lock_guard<std::mutex> lock(mutex_);
    entries_.clear();
}

// ═══════════════════════════════════════════════════════════════════
// Настройки runtime
// ═══════════════════════════════════════════════════════════════════

void Logger::setMinLevel(LogLevel level) {
    minLevel_.store(level);
}

LogLevel Logger::getMinLevel() const {
    return minLevel_.load();
}

// ═══════════════════════════════════════════════════════════════════
// Утилиты
// ═══════════════════════════════════════════════════════════════════

LogLevel Logger::levelFromString(const std::string& str) {
    std::string lower = str;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    if (lower == "trace")    return LogLevel::Trace;
    if (lower == "debug")    return LogLevel::Debug;
    if (lower == "info")     return LogLevel::Info;
    if (lower == "warn" || lower == "warning") return LogLevel::Warning;
    if (lower == "error")    return LogLevel::Error;
    if (lower == "critical" || lower == "crit" || lower == "fatal")
        return LogLevel::Critical;

    return LogLevel::Info; // По умолчанию
}

const char* Logger::levelToString(LogLevel level) {
    switch (level) {
        case LogLevel::Trace:    return "TRACE";
        case LogLevel::Debug:    return "DEBUG";
        case LogLevel::Info:     return "INFO";
        case LogLevel::Warning:  return "WARN";
        case LogLevel::Error:    return "ERROR";
        case LogLevel::Critical: return "CRIT";
    }
    return "UNKNOWN";
}

std::string Logger::getTimestamp() {
    auto now      = std::chrono::system_clock::now();
    auto timeT    = std::chrono::system_clock::to_time_t(now);
    auto millisec = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now.time_since_epoch()) % 1000;

    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &timeT);
#else
    localtime_r(&timeT, &tm);
#endif

    char buf[16];
    std::strftime(buf, sizeof(buf), "%H:%M:%S", &tm);

    char result[24];
    std::snprintf(result, sizeof(result), "%s.%03d",
                  buf, static_cast<int>(millisec.count()));
    return result;
}

std::string Logger::getFullTimestamp() {
    auto now      = std::chrono::system_clock::now();
    auto timeT    = std::chrono::system_clock::to_time_t(now);
    auto millisec = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now.time_since_epoch()) % 1000;

    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &timeT);
#else
    localtime_r(&timeT, &tm);
#endif

    char buf[24];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);

    char result[32];
    std::snprintf(result, sizeof(result), "%s.%03d",
                  buf, static_cast<int>(millisec.count()));
    return result;
}

const char* Logger::extractFilename(const char* path) {
    if (!path) return "unknown";

    const char* name = path;
    while (*path) {
        if (*path == '/' || *path == '\\') {
            name = path + 1;
        }
        path++;
    }
    return name;
}

const char* Logger::getColorCode(LogLevel level) {
    switch (level) {
        case LogLevel::Trace:    return ansi::gray;
        case LogLevel::Debug:    return ansi::cyan;
        case LogLevel::Info:     return ansi::green;
        case LogLevel::Warning:  return ansi::yellow;
        case LogLevel::Error:    return ansi::red;
        case LogLevel::Critical: return ansi::boldRed;
    }
    return ansi::reset;
}

void Logger::enableAnsiColors() {
#ifdef _WIN32
    // Windows 10/11: включаем обработку VT100 escape-последовательностей
    HANDLE hErr = GetStdHandle(STD_ERROR_HANDLE);
    if (hErr != INVALID_HANDLE_VALUE) {
        DWORD mode = 0;
        if (GetConsoleMode(hErr, &mode)) {
            mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
            if (SetConsoleMode(hErr, mode)) {
                colorsEnabled_ = true;
            }
        }
    }
    // Устанавливаем UTF-8 кодировку для корректного вывода русского текста
    SetConsoleOutputCP(CP_UTF8);
#else
    // Linux/macOS: ANSI-коды поддерживаются по умолчанию
    // Проверяем что stderr подключён к терминалу
    colorsEnabled_ = isatty(fileno(stderr));
#endif
}

} // namespace gcs