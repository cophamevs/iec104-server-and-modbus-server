#include "logger.h"
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <string.h>

// Global log level
static LogLevel g_log_level = LOG_LEVEL_INFO;

/**
 * Get log level name
 */
static const char* log_level_name(LogLevel level) {
    switch (level) {
        case LOG_LEVEL_DEBUG: return "DEBUG";
        case LOG_LEVEL_INFO:  return "INFO";
        case LOG_LEVEL_WARN:  return "WARN";
        case LOG_LEVEL_ERROR: return "ERROR";
        default:              return "UNKNOWN";
    }
}

/**
 * Get current timestamp string
 */
static void get_timestamp(char* buffer, size_t size) {
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    strftime(buffer, size, "%Y-%m-%d %H:%M:%S", tm_info);
}

/**
 * Initialize logger
 */
void logger_init(LogLevel level) {
    g_log_level = level;
}

/**
 * Set log level
 */
void logger_set_level(LogLevel level) {
    g_log_level = level;
}

/**
 * Get log level
 */
LogLevel logger_get_level(void) {
    return g_log_level;
}

/**
 * Log a message with format
 */
void log_message(LogLevel level, const char* format, ...) {
    if (level < g_log_level) {
        return;
    }

    char timestamp[64];
    get_timestamp(timestamp, sizeof(timestamp));

    // Print timestamp and level
    FILE* output = (level >= LOG_LEVEL_ERROR) ? stderr : stdout;
    fprintf(output, "{\"timestamp\":\"%s\",\"level\":\"%s\",\"message\":\"",
            timestamp, log_level_name(level));

    // Print formatted message
    va_list args;
    va_start(args, format);
    vfprintf(output, format, args);
    va_end(args);

    fprintf(output, "\"}\n");
    fflush(output);
}

/**
 * Log a JSON key-value pair
 */
void log_json(LogLevel level, const char* key, const char* value) {
    if (level < g_log_level) {
        return;
    }

    char timestamp[64];
    get_timestamp(timestamp, sizeof(timestamp));

    FILE* output = (level >= LOG_LEVEL_ERROR) ? stderr : stdout;
    fprintf(output, "{\"timestamp\":\"%s\",\"level\":\"%s\",\"%s\":\"%s\"}\n",
            timestamp, log_level_name(level), key, value);
    fflush(output);
}

/**
 * Log a JSON object with multiple key-value pairs
 */
void log_json_obj(LogLevel level, int count, ...) {
    if (level < g_log_level) {
        return;
    }

    char timestamp[64];
    get_timestamp(timestamp, sizeof(timestamp));

    FILE* output = (level >= LOG_LEVEL_ERROR) ? stderr : stdout;
    fprintf(output, "{\"timestamp\":\"%s\",\"level\":\"%s\"",
            timestamp, log_level_name(level));

    va_list args;
    va_start(args, count);
    
    for (int i = 0; i < count; i++) {
        const char* key = va_arg(args, const char*);
        const char* value = va_arg(args, const char*);
        fprintf(output, ",\"%s\":\"%s\"", key, value);
    }
    
    va_end(args);

    fprintf(output, "}\n");
    fflush(output);
}
