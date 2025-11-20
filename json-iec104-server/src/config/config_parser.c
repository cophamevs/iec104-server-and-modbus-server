#include "config_parser.h"
#include "../utils/logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// External global variables (these will be refactored to ServerConfig struct in later phases)
extern uint32_t offline_udt_time;
extern float deadband_M_ME_NC_1_percent;
extern int ASDU;
extern char command_mode[64];
extern int tcpPort;
extern char local_ip[64];

/**
 * Parse global settings from JSON configuration
 */
bool parse_global_settings(cJSON* json) {
    if (!json) {
        LOG_ERROR("NULL JSON object in parse_global_settings");
        return false;
    }

    cJSON* item;

    // Parse offline_udt_time
    item = cJSON_GetObjectItemCaseSensitive(json, "offline_udt_time");
    if (cJSON_IsNumber(item)) {
        offline_udt_time = (uint32_t)item->valueint;
        LOG_DEBUG("Config: offline_udt_time=%u", offline_udt_time);
    }

    // Parse deadband
    item = cJSON_GetObjectItemCaseSensitive(json, "deadband_M_ME_NC_1_percent");
    if (cJSON_IsNumber(item)) {
        deadband_M_ME_NC_1_percent = (float)item->valuedouble;
        LOG_DEBUG("Config: deadband_M_ME_NC_1_percent=%.2f", deadband_M_ME_NC_1_percent);
    }

    // Parse ASDU (Common Address)
    item = cJSON_GetObjectItemCaseSensitive(json, "ASDU");
    if (cJSON_IsNumber(item)) {
        ASDU = item->valueint;
        LOG_INFO("Config: ASDU (Common Address)=%d", ASDU);
    }

    // Parse command_mode
    item = cJSON_GetObjectItemCaseSensitive(json, "command_mode");
    if (cJSON_IsString(item) && item->valuestring) {
        strncpy(command_mode, item->valuestring, sizeof(command_mode) - 1);
        command_mode[sizeof(command_mode) - 1] = '\0';
        LOG_DEBUG("Config: command_mode=%s", command_mode);
    }

    // Parse TCP port
    item = cJSON_GetObjectItemCaseSensitive(json, "port");
    if (cJSON_IsNumber(item)) {
        tcpPort = item->valueint;
        LOG_DEBUG("Config: port=%d", tcpPort);
    }

    // Parse local IP
    item = cJSON_GetObjectItemCaseSensitive(json, "local_ip");
    if (cJSON_IsString(item) && item->valuestring) {
        strncpy(local_ip, item->valuestring, sizeof(local_ip) - 1);
        local_ip[sizeof(local_ip) - 1] = '\0';
        LOG_DEBUG("Config: local_ip=%s", local_ip);
    }

    return true;
}

/**
 * Generic parsing for any data type configuration
 * This single function replaces 14 duplicate parsing blocks (~400 lines of code)
 */
bool parse_data_type_config(cJSON* json, const char* config_key, DataTypeContext* ctx) {
    if (!json || !config_key || !ctx) {
        LOG_ERROR("Invalid parameters to parse_data_type_config");
        return false;
    }

    cJSON* config_array = cJSON_GetObjectItemCaseSensitive(json, config_key);

    // If not present, just return true (optional config)
    if (!cJSON_IsArray(config_array)) {
        LOG_DEBUG("No configuration found for %s (optional)", config_key);
        return true;
    }

    int count = cJSON_GetArraySize(config_array);
    if (count == 0) {
        LOG_DEBUG("Empty configuration for %s", config_key);
        return true;
    }

    LOG_INFO("Parsing %s with %d IOAs", config_key, count);

    // Allocate IOA list
    ctx->config.ioa_list = (int*)malloc(count * sizeof(int));
    if (!ctx->config.ioa_list) {
        LOG_ERROR("Failed to allocate memory for %s IOA list", config_key);
        return false;
    }

    // Parse IOA addresses
    for (int i = 0; i < count; i++) {
        cJSON* item = cJSON_GetArrayItem(config_array, i);
        if (cJSON_IsNumber(item)) {
            ctx->config.ioa_list[i] = item->valueint;
        } else {
            LOG_ERROR("Invalid IOA at index %d in %s", i, config_key);
            free(ctx->config.ioa_list);
            ctx->config.ioa_list = NULL;
            return false;
        }
    }
    ctx->config.count = count;

    // Allocate data array
    ctx->data_array = (DataValue*)calloc(count, sizeof(DataValue));
    if (!ctx->data_array) {
        LOG_ERROR("Failed to allocate data array for %s", config_key);
        free(ctx->config.ioa_list);
        ctx->config.ioa_list = NULL;
        return false;
    }

    // Initialize data values with defaults based on type info
    for (int i = 0; i < count; i++) {
        ctx->data_array[i].type = ctx->type_info->value_type;
        ctx->data_array[i].has_quality = ctx->type_info->has_quality;
        ctx->data_array[i].has_timestamp = ctx->type_info->has_time_tag;

        // Set default quality to INVALID until first real data arrives
        if (ctx->type_info->has_quality) {
            ctx->data_array[i].quality = IEC60870_QUALITY_INVALID;
        }

        // Initialize values to zero/false
        switch (ctx->data_array[i].type) {
            case DATA_VALUE_TYPE_BOOL:
                ctx->data_array[i].value.bool_val = false;
                break;
            case DATA_VALUE_TYPE_DOUBLE_POINT:
                ctx->data_array[i].value.dp_val = IEC60870_DOUBLE_POINT_INDETERMINATE;
                break;
            case DATA_VALUE_TYPE_INT16:
                ctx->data_array[i].value.int16_val = 0;
                break;
            case DATA_VALUE_TYPE_UINT32:
                ctx->data_array[i].value.uint32_val = 0;
                break;
            case DATA_VALUE_TYPE_FLOAT:
                ctx->data_array[i].value.float_val = 0.0f;
                break;
        }
    }

    // Allocate offline update tracking if has timestamp
    if (ctx->type_info->has_time_tag) {
        ctx->last_offline_update = (uint64_t*)calloc(count, sizeof(uint64_t));
        if (!ctx->last_offline_update) {
            LOG_ERROR("Failed to allocate offline tracking for %s", config_key);
            free(ctx->config.ioa_list);
            free(ctx->data_array);
            ctx->config.ioa_list = NULL;
            ctx->data_array = NULL;
            return false;
        }
    }

    LOG_INFO("Configured %s with %d IOAs", config_key, count);
    return true;
}

#include "../threads/periodic_sender.h"

/**
 * Parse periodic configuration
 */
static void parse_periodic_config(cJSON* json) {
    cJSON* periodic = cJSON_GetObjectItemCaseSensitive(json, "periodic");
    if (!cJSON_IsObject(periodic)) return;

    // Parse M_ME_NC_1 periodic config
    cJSON* me_nc_1 = cJSON_GetObjectItemCaseSensitive(periodic, "M_ME_NC_1");
    if (cJSON_IsObject(me_nc_1)) {
        cJSON* enabled = cJSON_GetObjectItemCaseSensitive(me_nc_1, "enabled");
        cJSON* period = cJSON_GetObjectItemCaseSensitive(me_nc_1, "period_ms");
        
        if (cJSON_IsBool(enabled)) {
            g_periodic_M_ME_NC_1.enabled = cJSON_IsTrue(enabled);
        }
        if (cJSON_IsNumber(period)) {
            g_periodic_M_ME_NC_1.period_ms = period->valueint;
        }
        LOG_INFO("Periodic M_ME_NC_1: enabled=%d, period=%d ms", 
                 g_periodic_M_ME_NC_1.enabled, g_periodic_M_ME_NC_1.period_ms);
    }

    // Parse M_SP_TB_1 periodic config
    cJSON* sp_tb_1 = cJSON_GetObjectItemCaseSensitive(periodic, "M_SP_TB_1");
    if (cJSON_IsObject(sp_tb_1)) {
        cJSON* enabled = cJSON_GetObjectItemCaseSensitive(sp_tb_1, "enabled");
        cJSON* period = cJSON_GetObjectItemCaseSensitive(sp_tb_1, "period_ms");
        
        if (cJSON_IsBool(enabled)) {
            g_periodic_M_SP_TB_1.enabled = cJSON_IsTrue(enabled);
        }
        if (cJSON_IsNumber(period)) {
            g_periodic_M_SP_TB_1.period_ms = period->valueint;
        }
        LOG_INFO("Periodic M_SP_TB_1: enabled=%d, period=%d ms", 
                 g_periodic_M_SP_TB_1.enabled, g_periodic_M_SP_TB_1.period_ms);
    }
}

/**
 * Parse configuration from JSON string
 */
bool parse_config_from_json(const char* json_string) {
    if (!json_string) {
        LOG_ERROR("NULL JSON string");
        return false;
    }

    cJSON* json = cJSON_Parse(json_string);
    if (!json) {
        const char* error_ptr = cJSON_GetErrorPtr();
        if (error_ptr) {
            LOG_ERROR("Failed to parse JSON: %s", error_ptr);
        } else {
            LOG_ERROR("Failed to parse JSON configuration");
        }
        return false;
    }

    // Parse global settings
    if (!parse_global_settings(json)) {
        cJSON_Delete(json);
        return false;
    }

    // Parse periodic settings
    parse_periodic_config(json);

    // Parse all data type configurations using generic function
    // This replaces 14 duplicate blocks with a simple loop!
    struct {
        const char* key;
        TypeID type_id;
    } config_keys[] = {
        {"M_SP_TB_1_config", M_SP_TB_1},
        {"M_DP_TB_1_config", M_DP_TB_1},
        {"M_ME_TD_1_config", M_ME_TD_1},
        {"M_IT_TB_1_config", M_IT_TB_1},
        {"M_SP_NA_1_config", M_SP_NA_1},
        {"M_DP_NA_1_config", M_DP_NA_1},
        {"M_ME_NA_1_config", M_ME_NA_1},
        {"M_ME_NB_1_config", M_ME_NB_1},
        {"M_ME_NC_1_config", M_ME_NC_1},
        {"M_ME_ND_1_config", M_ME_ND_1}
    };

    for (size_t i = 0; i < sizeof(config_keys) / sizeof(config_keys[0]); i++) {
        DataTypeContext* ctx = get_data_context(config_keys[i].type_id);
        if (!ctx) {
            LOG_ERROR("Failed to get context for type %s", config_keys[i].key);
            cJSON_Delete(json);
            return false;
        }

        if (!parse_data_type_config(json, config_keys[i].key, ctx)) {
            LOG_ERROR("Failed to parse config for %s", config_keys[i].key);
            cJSON_Delete(json);
            return false;
        }
    }

    cJSON_Delete(json);
    LOG_INFO("Configuration parsed successfully");
    return true;
}

/**
 * Parse configuration from file
 */
bool init_config_from_file(const char* filename) {
    if (!filename) {
        LOG_ERROR("NULL filename");
        return false;
    }

    FILE* file = fopen(filename, "r");
    if (!file) {
        LOG_ERROR("Failed to open config file: %s", filename);
        return false;
    }

    // Get file size
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (file_size <= 0) {
        LOG_ERROR("Invalid file size for %s", filename);
        fclose(file);
        return false;
    }

    // Read file content
    char* content = (char*)malloc(file_size + 1);
    if (!content) {
        LOG_ERROR("Failed to allocate memory for config file");
        fclose(file);
        return false;
    }

    size_t read_size = fread(content, 1, file_size, file);
    content[read_size] = '\0';
    fclose(file);

    LOG_DEBUG("Read %zu bytes from %s", read_size, filename);

    // Parse JSON
    bool result = parse_config_from_json(content);
    free(content);

    return result;
}
