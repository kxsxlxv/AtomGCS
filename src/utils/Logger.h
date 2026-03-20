#pragma once
// ═══════════════════════════════════════════════════════════════════
// Logger — потокобезопасный логгер с цветным выводом в консоль,
// записью в файл и кольцевым буфером для UI (ImGui LogPanel).
// Использование:
//   LOG_INFO("Простое сообщение");
//   LOG_WARN("Значение x = ", x, ", y = ", y);
//   LOG_ERROR("Код ошибки: ", errorCode);
// ═══════════════════════════════════════════════════════════════════

#include <string>
#include <vector>
#include <deque>
#include <mutex>
#include <sstream>
#include <fstream>
#include <atomic>

namespace gcs {

// ── Уровни логирования ──
enum class LogLevel {
    Trace    = 0,   // Детальная трассировка
    Debug    = 1,   // Отладочные сообщения
    Info     = 2,   // Информационные сообщения
    Warning  = 3,   // Предупреждения
    Error    = 4,   // Ошибки
    Critical = 5    // Критические ошибки
};

class Logger {
public:
    /// Запись лога для кольцевого буфера (доступна из UI)
    struct LogEntry {
        LogLevel    level;
        std::string timestamp;  // "HH:MM:SS.mmm"
        std::string message;
    };

    /// Получить единственный экземпляр
    static Logger& instance();

    // ── Инициализация ──

    /// Инициализировать логгер.
    /// @param minLevel  — минимальный уровень для вывода
    /// @param toFile    — писать ли в файл
    /// @param logDir    — директория для лог-файлов
    void initialize(LogLevel minLevel = LogLevel::Info,
                    bool toFile = false,
                    const std::string& logDir = "logs");

    /// Завершить работу логгера (закрыть файл)
    void shutdown();

    // ── Основной метод записи ──

    /// Записать сообщение в лог.
    /// @param level   — уровень сообщения
    /// @param message — текст
    /// @param file    — имя файла (__FILE__), nullptr если не нужно
    /// @param line    — номер строки (__LINE__)
    void log(LogLevel level, const std::string& message,
             const char* file = nullptr, int line = 0);

    /// Шаблонный метод с поддержкой нескольких аргументов (fold expression).
    /// LOG_INFO("x = ", 42, ", name = ", name);
    template<typename... Args>
    void logFmt(LogLevel level, const char* file, int line, Args&&... args) {
        // Быстрая проверка уровня без захвата мьютекса
        if (static_cast<int>(level) < static_cast<int>(minLevel_.load())) {
            return;
        }
        std::ostringstream oss;
        (oss << ... << std::forward<Args>(args)); // C++17 fold expression
        log(level, oss.str(), file, line);
    }

    // ── Кольцевой буфер для UI ──

    /// Получить все записи из буфера (копия)
    std::vector<LogEntry> getEntries() const;

    /// Текущее количество записей в буфере
    size_t getEntryCount() const;

    /// Очистить буфер
    void clearEntries();

    // ── Утилиты ──

    /// Преобразовать строку в уровень логирования ("info" → LogLevel::Info)
    static LogLevel levelFromString(const std::string& str);

    /// Преобразовать уровень в строку (LogLevel::Info → "INFO")
    static const char* levelToString(LogLevel level);

    /// Установить минимальный уровень (runtime)
    void setMinLevel(LogLevel level);

    /// Получить текущий минимальный уровень
    LogLevel getMinLevel() const;

private:
    Logger() = default;
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    /// Вывод в консоль с цветами
    void writeToConsole(LogLevel level, const std::string& timestamp,
                        const std::string& message,
                        const char* file, int line);

    /// Запись в файл (без цветов)
    void writeToFile(LogLevel level, const std::string& timestamp,
                     const std::string& message,
                     const char* file, int line);

    /// Получить текущую метку времени: "HH:MM:SS.mmm"
    static std::string getTimestamp();

    /// Получить полную метку с датой: "YYYY-MM-DD HH:MM:SS.mmm"
    static std::string getFullTimestamp();

    /// Извлечь имя файла из полного пути
    static const char* extractFilename(const char* path);

    /// Включить ANSI-коды цвета в консоли (Windows)
    void enableAnsiColors();

    /// Получить ANSI-код цвета для уровня
    static const char* getColorCode(LogLevel level);

    // ── Данные ──

    mutable std::mutex mutex_;
    std::atomic<LogLevel> minLevel_{LogLevel::Info};

    // Кольцевой буфер
    std::deque<LogEntry> entries_;
    size_t maxEntries_ = 2000;

    // Файловый вывод
    std::ofstream logFile_;
    bool fileEnabled_   = false;

    // Состояние
    bool initialized_   = false;
    bool colorsEnabled_  = false;
};

} // namespace gcs

// ═══════════════════════════════════════════════════════════════════
// Макросы для удобного логирования
// Поддерживают произвольное число аргументов через запятую:
//   LOG_INFO("Порт: ", port, " адрес: ", addr);
// ═══════════════════════════════════════════════════════════════════

#define LOG_TRACE(...) \
    ::gcs::Logger::instance().logFmt( \
        ::gcs::LogLevel::Trace, __FILE__, __LINE__, __VA_ARGS__)

#define LOG_DEBUG(...) \
    ::gcs::Logger::instance().logFmt( \
        ::gcs::LogLevel::Debug, __FILE__, __LINE__, __VA_ARGS__)

#define LOG_INFO(...) \
    ::gcs::Logger::instance().logFmt( \
        ::gcs::LogLevel::Info, __FILE__, __LINE__, __VA_ARGS__)

#define LOG_WARN(...) \
    ::gcs::Logger::instance().logFmt( \
        ::gcs::LogLevel::Warning, __FILE__, __LINE__, __VA_ARGS__)

#define LOG_ERROR(...) \
    ::gcs::Logger::instance().logFmt( \
        ::gcs::LogLevel::Error, __FILE__, __LINE__, __VA_ARGS__)

#define LOG_CRITICAL(...) \
    ::gcs::Logger::instance().logFmt( \
        ::gcs::LogLevel::Critical, __FILE__, __LINE__, __VA_ARGS__)