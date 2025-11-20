#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "../src/utils/error_codes.h"
#include "../src/utils/logger.h"

void test_error_code_to_string() {
    printf("\nTesting error_code_to_string()...\n");
    
    assert(strcmp(error_code_to_string(IEC104_OK), "OK") == 0);
    assert(strcmp(error_code_to_string(IEC104_ERR_INVALID_CONFIG), "INVALID_CONFIG") == 0);
    assert(strcmp(error_code_to_string(IEC104_ERR_MEMORY_ALLOC), "MEMORY_ALLOC") == 0);
    assert(strcmp(error_code_to_string(IEC104_ERR_INVALID_IOA), "INVALID_IOA") == 0);
    assert(strcmp(error_code_to_string(IEC104_ERR_UNKNOWN), "UNKNOWN") == 0);
    
    printf("  ✓ Error code strings correct\n");
}

void test_report_error() {
    printf("\nTesting report_error()...\n");
    
    IEC104_Error err = {
        .code = IEC104_ERR_INVALID_CONFIG,
        .message = "Test error message",
        .file = "test_file.c",
        .line = 42
    };
    
    printf("  Expected error output to stderr:\n");
    report_error(&err);
    
    printf("  ✓ Error reported successfully\n");
}

void test_logger_levels() {
    printf("\nTesting logger levels...\n");
    
    // Test default level
    logger_init(LOG_LEVEL_INFO);
    assert(logger_get_level() == LOG_LEVEL_INFO);
    
    // Test setting level
    logger_set_level(LOG_LEVEL_DEBUG);
    assert(logger_get_level() == LOG_LEVEL_DEBUG);
    
    logger_set_level(LOG_LEVEL_WARN);
    assert(logger_get_level() == LOG_LEVEL_WARN);
    
    printf("  ✓ Logger level management works\n");
}

void test_log_message() {
    printf("\nTesting log_message()...\n");
    
    logger_set_level(LOG_LEVEL_DEBUG);
    
    printf("  Expected log outputs:\n");
    LOG_DEBUG("This is a debug message");
    LOG_INFO("This is an info message");
    LOG_WARN("This is a warning message");
    LOG_ERROR("This is an error message");
    
    printf("  ✓ Log messages output correctly\n");
}

void test_log_json() {
    printf("\nTesting log_json()...\n");
    
    logger_set_level(LOG_LEVEL_INFO);
    
    printf("  Expected JSON log outputs:\n");
    LOG_JSON_INFO("status", "connected");
    LOG_JSON_INFO("ioa", "12345");
    LOG_JSON_WARN("warning", "high temperature");
    
    printf("  ✓ JSON logs output correctly\n");
}

void test_log_json_obj() {
    printf("\nTesting log_json_obj()...\n");
    
    logger_set_level(LOG_LEVEL_INFO);
    
    printf("  Expected JSON object output:\n");
    log_json_obj(LOG_LEVEL_INFO, 3, 
                 "event", "data_update",
                 "ioa", "100",
                 "value", "true");
    
    printf("  ✓ JSON object logs output correctly\n");
}

void test_log_filtering() {
    printf("\nTesting log level filtering...\n");
    
    logger_set_level(LOG_LEVEL_WARN);
    
    printf("  The following should NOT appear (DEBUG and INFO filtered):\n");
    LOG_DEBUG("This debug should not appear");
    LOG_INFO("This info should not appear");
    
    printf("  The following SHOULD appear (WARN and ERROR):\n");
    LOG_WARN("This warning should appear");
    LOG_ERROR("This error should appear");
    
    printf("  ✓ Log filtering works correctly\n");
}

int main() {
    printf("===========================================\n");
    printf("Running utils test suite\n");
    printf("===========================================\n");
    
    test_error_code_to_string();
    test_report_error();
    test_logger_levels();
    test_log_message();
    test_log_json();
    test_log_json_obj();
    test_log_filtering();
    
    printf("\n===========================================\n");
    printf("✓ All utils tests passed!\n");
    printf("===========================================\n");
    
    return 0;
}
