#include "error_codes.h"
#include <stdio.h>
#include <time.h>

/**
 * Error code to string mapping
 */
const char* error_code_to_string(IEC104_ErrorCode code) {
    switch (code) {
        case IEC104_OK:
            return "OK";
        case IEC104_ERR_INVALID_CONFIG:
            return "INVALID_CONFIG";
        case IEC104_ERR_MEMORY_ALLOC:
            return "MEMORY_ALLOC";
        case IEC104_ERR_INVALID_IOA:
            return "INVALID_IOA";
        case IEC104_ERR_IOA_NOT_CONFIGURED:
            return "IOA_NOT_CONFIGURED";
        case IEC104_ERR_MUTEX_LOCK:
            return "MUTEX_LOCK";
        case IEC104_ERR_INVALID_JSON:
            return "INVALID_JSON";
        case IEC104_ERR_FILE_NOT_FOUND:
            return "FILE_NOT_FOUND";
        case IEC104_ERR_CLIENT_NOT_CONNECTED:
            return "CLIENT_NOT_CONNECTED";
        case IEC104_ERR_INVALID_TYPE:
            return "INVALID_TYPE";
        case IEC104_ERR_INVALID_VALUE:
            return "INVALID_VALUE";
        case IEC104_ERR_THREAD_CREATE:
            return "THREAD_CREATE";
        case IEC104_ERR_SOCKET:
            return "SOCKET";
        case IEC104_ERR_TIMEOUT:
            return "TIMEOUT";
        case IEC104_ERR_UNKNOWN:
        default:
            return "UNKNOWN";
    }
}

/**
 * Report error with structured JSON format
 */
void report_error(const IEC104_Error* error) {
    if (!error) {
        return;
    }

    // Get current timestamp
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);

    // Print structured error in JSON format to stderr
    fprintf(stderr, 
            "{\"timestamp\":\"%s\",\"level\":\"ERROR\",\"code\":%d,\"error\":\"%s\","
            "\"message\":\"%s\",\"file\":\"%s\",\"line\":%d}\n",
            timestamp,
            error->code,
            error_code_to_string(error->code),
            error->message ? error->message : "",
            error->file ? error->file : "unknown",
            error->line);
    
    fflush(stderr);
}
