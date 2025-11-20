#ifndef LOGGER_H
#define LOGGER_H

#include <stdbool.h>

/**
 * Log levels for structured logging
 */
typedef enum {
    LOG_LEVEL_DEBUG = 0,    // Detailed debug information
    LOG_LEVEL_INFO = 1,     // Informational messages
    LOG_LEVEL_WARN = 2,     // Warning messages
    LOG_LEVEL_ERROR = 3     // Error messages
} LogLevel;

/**
 * Initialize logger with specified log level
 * @param level Minimum log level to output
 */
void logger_init(LogLevel level);

/**
 * Set current log level
 * @param level New log level
 */
void logger_set_level(LogLevel level);

/**
 * Get current log level
 * @return Current log level
 */
LogLevel logger_get_level(void);

/**
 * Log a message with specified level
 * @param level Log level
 * @param format Printf-style format string
 * @param ... Variable arguments
 */
void log_message(LogLevel level, const char* format, ...);

/**
 * Log a JSON key-value pair
 * @param level Log level
 * @param key JSON key
 * @param value JSON value (will be quoted)
 */
void log_json(LogLevel level, const char* key, const char* value);

/**
 * Log a JSON object with multiple key-value pairs
 * @param level Log level
 * @param count Number of key-value pairs
 * @param ... Alternating key-value pairs (all const char*)
 */
void log_json_obj(LogLevel level, int count, ...);

/**
 * Convenience macros for logging
 */
#define LOG_DEBUG(...) log_message(LOG_LEVEL_DEBUG, __VA_ARGS__)
#define LOG_INFO(...)  log_message(LOG_LEVEL_INFO, __VA_ARGS__)
#define LOG_WARN(...)  log_message(LOG_LEVEL_WARN, __VA_ARGS__)
#define LOG_ERROR(...) log_message(LOG_LEVEL_ERROR, __VA_ARGS__)

/**
 * Convenience macros for JSON logging
 */
#define LOG_JSON_DEBUG(key, value) log_json(LOG_LEVEL_DEBUG, key, value)
#define LOG_JSON_INFO(key, value)  log_json(LOG_LEVEL_INFO, key, value)
#define LOG_JSON_WARN(key, value)  log_json(LOG_LEVEL_WARN, key, value)
#define LOG_JSON_ERROR(key, value) log_json(LOG_LEVEL_ERROR, key, value)

#endif // LOGGER_H
