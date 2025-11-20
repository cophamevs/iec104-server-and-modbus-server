#ifndef CONFIG_PARSER_H
#define CONFIG_PARSER_H

#include <stdbool.h>
#include "../../cJSON/cJSON.h"
#include "../data/data_manager.h"

/**
 * Parse configuration from JSON string
 * @param json_string The JSON configuration string
 * @return true on success, false on error
 */
bool parse_config_from_json(const char* json_string);

/**
 * Parse configuration from file
 * @param filename Path to the JSON configuration file
 * @return true on success, false on error
 */
bool init_config_from_file(const char* filename);

/**
 * Parse global settings from JSON object
 * @param json The root JSON object
 * @return true on success, false on error
 */
bool parse_global_settings(cJSON* json);

/**
 * Parse data type configuration (generic function)
 * This replaces 14 duplicate parsing blocks with a single generic implementation
 * @param json The root JSON object
 * @param config_key The configuration key (e.g., "M_SP_TB_1_config")
 * @param ctx The data type context to populate
 * @return true on success, false on error
 */
bool parse_data_type_config(cJSON* json, const char* config_key, DataTypeContext* ctx);

#endif // CONFIG_PARSER_H
