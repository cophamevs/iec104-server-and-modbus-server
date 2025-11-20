#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "../src/config/config_parser.h"
#include "../src/data/data_manager.h"
#include "../src/data/data_types.h"

// Mock global variables that config_parser expects
uint32_t offline_udt_time = 0;
float deadband_M_ME_NC_1_percent = 0.0f;
int ASDU = 1;
char command_mode[64] = "";
int tcpPort = 2404;
char local_ip[64] = "0.0.0.0";

void test_parse_global_settings() {
    printf("\nTesting parse_global_settings()...\n");
    
    const char* json_str = "{"
        "\"offline_udt_time\": 5000,"
        "\"deadband_M_ME_NC_1_percent\": 1.5,"
        "\"asdu\": 47,"
        "\"command_mode\": \"direct\","
        "\"port\": 2404,"
        "\"local_ip\": \"192.168.1.100\""
    "}";
    
    cJSON* json = cJSON_Parse(json_str);
    assert(json != NULL);
    
    bool result = parse_global_settings(json);
    assert(result == true);
    
    // Verify parsed values
    assert(offline_udt_time == 5000);
    assert(deadband_M_ME_NC_1_percent == 1.5f);
    assert(ASDU == 47);
    assert(strcmp(command_mode, "direct") == 0);
    assert(tcpPort == 2404);
    assert(strcmp(local_ip, "192.168.1.100") == 0);
    
    cJSON_Delete(json);
    printf("  ✓ Global settings parsed correctly\n");
}

void test_parse_data_type_config() {
    printf("\nTesting parse_data_type_config()...\n");
    
    // Initialize data contexts
    init_data_contexts();
    
    const char* json_str = "{"
        "\"M_SP_TB_1_config\": [100, 101, 102]"
    "}";
    
    cJSON* json = cJSON_Parse(json_str);
    assert(json != NULL);
    
    DataTypeContext* ctx = get_data_context(M_SP_TB_1);
    assert(ctx != NULL);
    
    bool result = parse_data_type_config(json, "M_SP_TB_1_config", ctx);
    assert(result == true);
    
    // Verify configuration
    assert(ctx->config.count == 3);
    assert(ctx->config.ioa_list[0] == 100);
    assert(ctx->config.ioa_list[1] == 101);
    assert(ctx->config.ioa_list[2] == 102);
    
    // Verify data array initialized
    assert(ctx->data_array != NULL);
    assert(ctx->data_array[0].type == DATA_VALUE_TYPE_BOOL);
    assert(ctx->data_array[0].has_quality == true);
    assert(ctx->data_array[0].has_timestamp == true);
    
    // Verify offline tracking allocated (M_SP_TB_1 has timestamp)
    assert(ctx->last_offline_update != NULL);
    
    cJSON_Delete(json);
    cleanup_data_contexts();
    printf("  ✓ Data type config parsed correctly\n");
}

void test_parse_multiple_types() {
    printf("\nTesting parse_config_from_json() with multiple types...\n");
    
    // Initialize data contexts
    init_data_contexts();
    
    const char* json_str = "{"
        "\"offline_udt_time\": 3000,"
        "\"asdu\": 1,"
        "\"M_SP_TB_1_config\": [100, 101],"
        "\"M_DP_TB_1_config\": [200, 201, 202],"
        "\"M_ME_NC_1_config\": [300]"
    "}";
    
    bool result = parse_config_from_json(json_str);
    assert(result == true);
    
    // Verify M_SP_TB_1
    DataTypeContext* ctx1 = get_data_context(M_SP_TB_1);
    assert(ctx1->config.count == 2);
    assert(ctx1->config.ioa_list[0] == 100);
    
    // Verify M_DP_TB_1
    DataTypeContext* ctx2 = get_data_context(M_DP_TB_1);
    assert(ctx2->config.count == 3);
    assert(ctx2->config.ioa_list[1] == 201);
    
    // Verify M_ME_NC_1
    DataTypeContext* ctx3 = get_data_context(M_ME_NC_1);
    assert(ctx3->config.count == 1);
    assert(ctx3->config.ioa_list[0] == 300);
    
    cleanup_data_contexts();
    printf("  ✓ Multiple types parsed correctly\n");
}

void test_parse_empty_config() {
    printf("\nTesting parse with empty/missing configs...\n");
    
    init_data_contexts();
    
    const char* json_str = "{"
        "\"offline_udt_time\": 1000,"
        "\"asdu\": 1"
    "}";
    
    bool result = parse_config_from_json(json_str);
    assert(result == true);
    
    // Verify all contexts have zero count (no configs)
    for (int i = 0; i < DATA_TYPE_COUNT; i++) {
        assert(g_data_contexts[i].config.count == 0);
    }
    
    cleanup_data_contexts();
    printf("  ✓ Empty configs handled correctly\n");
}

void test_parse_invalid_json() {
    printf("\nTesting parse with invalid JSON...\n");
    
    init_data_contexts();
    
    const char* invalid_json = "{invalid json}";
    bool result = parse_config_from_json(invalid_json);
    assert(result == false);
    
    cleanup_data_contexts();
    printf("  ✓ Invalid JSON rejected correctly\n");
}

void test_parse_invalid_ioa() {
    printf("\nTesting parse with invalid IOA type...\n");
    
    init_data_contexts();
    
    const char* json_str = "{"
        "\"M_SP_TB_1_config\": [100, \"invalid\", 102]"
    "}";
    
    bool result = parse_config_from_json(json_str);
    assert(result == false);
    
    cleanup_data_contexts();
    printf("  ✓ Invalid IOA type rejected correctly\n");
}

void test_init_config_from_file() {
    printf("\nTesting init_config_from_file()...\n");
    
    // Create a temporary config file
    const char* test_file = "/tmp/test_config.json";
    FILE* f = fopen(test_file, "w");
    assert(f != NULL);
    
    const char* config_content = "{"
        "\"offline_udt_time\": 2000,"
        "\"asdu\": 99,"
        "\"M_SP_TB_1_config\": [500, 501]"
    "}";
    
    fwrite(config_content, 1, strlen(config_content), f);
    fclose(f);
    
    init_data_contexts();
    
    bool result = init_config_from_file(test_file);
    assert(result == true);
    
    // Verify parsed values
    assert(ASDU == 99);
    assert(offline_udt_time == 2000);
    
    DataTypeContext* ctx = get_data_context(M_SP_TB_1);
    assert(ctx->config.count == 2);
    assert(ctx->config.ioa_list[0] == 500);
    
    cleanup_data_contexts();
    
    // Clean up test file
    remove(test_file);
    
    printf("  ✓ Config file parsed correctly\n");
}

void test_init_config_from_nonexistent_file() {
    printf("\nTesting init_config_from_file() with non-existent file...\n");
    
    init_data_contexts();
    
    bool result = init_config_from_file("/nonexistent/path/config.json");
    assert(result == false);
    
    cleanup_data_contexts();
    printf("  ✓ Non-existent file handled correctly\n");
}

int main() {
    printf("===========================================\n");
    printf("Running config_parser test suite\n");
    printf("===========================================\n");
    
    test_parse_global_settings();
    test_parse_data_type_config();
    test_parse_multiple_types();
    test_parse_empty_config();
    test_parse_invalid_json();
    test_parse_invalid_ioa();
    test_init_config_from_file();
    test_init_config_from_nonexistent_file();
    
    printf("\n===========================================\n");
    printf("✓ All config_parser tests passed!\n");
    printf("===========================================\n");
    
    return 0;
}
