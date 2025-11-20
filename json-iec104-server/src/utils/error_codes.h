#ifndef ERROR_CODES_H
#define ERROR_CODES_H

/**
 * Error codes for IEC 60870-5-104 server
 * Provides structured error handling across the application
 */
typedef enum {
    IEC104_OK = 0,                          // Success
    IEC104_ERR_INVALID_CONFIG = 1,          // Invalid configuration
    IEC104_ERR_MEMORY_ALLOC = 2,            // Memory allocation failed
    IEC104_ERR_INVALID_IOA = 3,             // Invalid IOA address
    IEC104_ERR_IOA_NOT_CONFIGURED = 4,      // IOA not in configuration
    IEC104_ERR_MUTEX_LOCK = 5,              // Mutex lock/unlock failed
    IEC104_ERR_INVALID_JSON = 6,            // JSON parsing error
    IEC104_ERR_FILE_NOT_FOUND = 7,          // Configuration file not found
    IEC104_ERR_CLIENT_NOT_CONNECTED = 8,    // No client connected
    IEC104_ERR_INVALID_TYPE = 9,            // Invalid data type
    IEC104_ERR_INVALID_VALUE = 10,          // Invalid value
    IEC104_ERR_THREAD_CREATE = 11,          // Thread creation failed
    IEC104_ERR_SOCKET = 12,                 // Socket error
    IEC104_ERR_TIMEOUT = 13,                // Operation timeout
    IEC104_ERR_UNKNOWN = 99                 // Unknown error
} IEC104_ErrorCode;

/**
 * Error information structure
 */
typedef struct {
    IEC104_ErrorCode code;      // Error code
    const char* message;        // Error message
    const char* file;           // Source file where error occurred
    int line;                   // Line number where error occurred
} IEC104_Error;

/**
 * Get string representation of error code
 * @param code The error code
 * @return String description of the error
 */
const char* error_code_to_string(IEC104_ErrorCode code);

/**
 * Report an error (logs to stderr with structured format)
 * @param error Pointer to error structure
 */
void report_error(const IEC104_Error* error);

/**
 * Macro to return with error reporting
 * Usage: RETURN_ERROR(IEC104_ERR_INVALID_CONFIG, "Config file missing");
 */
#define RETURN_ERROR(code, msg) \
    do { \
        IEC104_Error err = {code, msg, __FILE__, __LINE__}; \
        report_error(&err); \
        return code; \
    } while(0)

/**
 * Macro to return pointer with error reporting
 * Usage: RETURN_NULL_ERROR(IEC104_ERR_MEMORY_ALLOC, "Failed to allocate");
 */
#define RETURN_NULL_ERROR(code, msg) \
    do { \
        IEC104_Error err = {code, msg, __FILE__, __LINE__}; \
        report_error(&err); \
        return NULL; \
    } while(0)

#endif // ERROR_CODES_H
