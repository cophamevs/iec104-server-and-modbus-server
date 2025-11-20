#include "input_handler.h"
#include "../client/client_manager.h"
#include "../data/data_manager.h"
#include "../data/data_types.h"
#include "../protocol/interrogation.h"
#include "../utils/logger.h"
#include "../../cJSON/cJSON.h"
#include "hal_time.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>

// External globals
extern int ASDU;

// Module state
static CS104_Slave slave = NULL;
static bool initialized = false;

// Helper to convert double input to DataValue
static void convert_input_to_value(TypeID type, double input_val, int qualifier, DataValue* out_val) {
    out_val->type = get_data_type_info(type)->value_type;
    out_val->has_quality = get_data_type_info(type)->has_quality;
    out_val->has_timestamp = get_data_type_info(type)->has_time_tag;
    out_val->quality = qualifier;
    CP56Time2a_createFromMsTimestamp(out_val->timestamp, Hal_getTimeInMs());

    switch (out_val->type) {
        case DATA_VALUE_TYPE_BOOL:
            out_val->value.bool_val = (bool)input_val;
            break;
        case DATA_VALUE_TYPE_DOUBLE_POINT:
            out_val->value.dp_val = (DoublePointValue)input_val;
            break;
        case DATA_VALUE_TYPE_INT16:
            out_val->value.int16_val = (int16_t)input_val;
            break;
        case DATA_VALUE_TYPE_UINT32:
            out_val->value.uint32_val = (uint32_t)input_val;
            break;
        case DATA_VALUE_TYPE_FLOAT:
            out_val->value.float_val = (float)input_val;
            break;
    }
}

void input_handler_init(CS104_Slave slave_instance) {
    slave = slave_instance;
    initialized = true;
    LOG_DEBUG("Input handler initialized");
}

void input_handler_cleanup(void) {
    slave = NULL;
    initialized = false;
    LOG_DEBUG("Input handler cleaned up");
}

bool input_handler_process_line(const char* line) {
    if (!initialized || !slave) {
        LOG_ERROR("Input handler not initialized");
        return true;
    }

    cJSON* json = cJSON_Parse(line);
    if (!json) {
        LOG_ERROR("Invalid JSON input");
        return true;
    }

    // Check for commands: {"cmd":"stop"} or {"cmd":"get_connected_clients"}
    cJSON* cmd_item = cJSON_GetObjectItem(json, "cmd");
    if (cmd_item && cJSON_IsString(cmd_item)) {
        if (strcmp(cmd_item->valuestring, "stop") == 0) {
            LOG_INFO("Shutdown command received");
            cJSON_Delete(json);
            return false; // Signal shutdown
        }
        else if (strcmp(cmd_item->valuestring, "get_connected_clients") == 0) {
            char* json_str = client_manager_get_clients_json();
            if (json_str) {
                printf("%s\n", json_str);
                fflush(stdout);
                free(json_str);
            }
            cJSON_Delete(json);
            return true;
        }
        else if (strcmp(cmd_item->valuestring, "get_queue_count") == 0) {
            int queue_count = 0;
            if (slave && CS104_Slave_isRunning(slave)) {
                queue_count = CS104_Slave_getNumberOfQueueEntries(slave, NULL);
            }
            printf("{\"queue_count\":%d}\n", queue_count);
            fflush(stdout);
            cJSON_Delete(json);
            return true;
        }
    }

    // Parse data update: {"type":"M_SP_TB_1", "address":100, "value":1, "qualifier":0}
    cJSON* type_item = cJSON_GetObjectItem(json, "type");
    cJSON* value_item = cJSON_GetObjectItem(json, "value");

    if (type_item && value_item) {
        // Handle string type (e.g. "M_SP_TB_1") or number
        TypeID type_id = 0;
        if (cJSON_IsString(type_item)) {
            const DataTypeInfo* info = get_data_type_info_by_name(type_item->valuestring);
            if (info) type_id = info->type_id;
        } else if (cJSON_IsNumber(type_item)) {
            type_id = (TypeID)type_item->valueint;
        }

        cJSON* addr_item = cJSON_GetObjectItem(json, "address");
        cJSON* qual_item = cJSON_GetObjectItem(json, "qualifier");

        if (type_id > 0 && addr_item && cJSON_IsNumber(addr_item)) {
            int ioa = addr_item->valueint;

            // Parse qualifier - support both string and number
            int qual = 0; // Default to QUALITY_GOOD
            if (qual_item) {
                if (cJSON_IsString(qual_item)) {
                    qual = parse_qualifier_from_string(qual_item->valuestring);
                } else if (cJSON_IsNumber(qual_item)) {
                    qual = qual_item->valueint;
                }
            }



            DataTypeContext* ctx = get_data_context(type_id);
            if (ctx) {
                DataValue val;
                convert_input_to_value(type_id, value_item->valuedouble, qual, &val);

                // Get type info for offline handling
                const DataTypeInfo* type_info = get_data_type_info(type_id);
                
                // Generic offline handling for ALL non-timestamped types
                bool offline_enqueue = false;
                
                if (type_info && !type_info->has_time_tag && !is_client_connected(slave) && 
                    type_info->offline_equivalent != 0) {
                    // This type supports offline queueing
                    int idx = find_ioa_index(&ctx->config, ioa);
                    if (idx >= 0) {
                        // Global offline tracking for all non-timestamped types
                        // Each type gets its own tracking array
                        static struct {
                            TypeID type_id;
                            uint64_t* timestamps;
                            int count;
                        } offline_tracking[10] = {{0}};  // Support up to 10 types
                        
                        // Find or create tracking for this type
                        int tracking_idx = -1;
                        for (int i = 0; i < 10; i++) {
                            if (offline_tracking[i].type_id == type_id) {
                                tracking_idx = i;
                                break;
                            }
                            if (offline_tracking[i].type_id == 0) {
                                // Initialize new tracking
                                offline_tracking[i].type_id = type_id;
                                offline_tracking[i].count = ctx->config.count;
                                offline_tracking[i].timestamps = (uint64_t*)calloc(ctx->config.count, sizeof(uint64_t));
                                tracking_idx = i;
                                break;
                            }
                        }
                        
                        if (tracking_idx >= 0) {
                            pthread_mutex_lock(&ctx->mutex);
                            
                            // Get old value based on type
                            bool deadband_passed = false;
                            switch (type_info->value_type) {
                                case DATA_VALUE_TYPE_FLOAT: {
                                    float old_val = ctx->data_array[idx].value.float_val;
                                    float new_val = val.value.float_val;
                                    float diff = fabsf(old_val - new_val);
                                    
                                    // Apply deadband (use M_ME_NC_1 deadband for all float types for now)
                                    extern float deadband_M_ME_NC_1_percent;
                                    float percent = 0.0f;
                                    if (fabsf(old_val) > 0.0001f) {
                                        percent = (diff / fabsf(old_val)) * 100.0f;
                                    } else {
                                        percent = (diff > 0.0001f) ? 100.0f : 0.0f;
                                    }
                                    deadband_passed = (percent >= deadband_M_ME_NC_1_percent && diff > 0.0001f);
                                    break;
                                }
                                case DATA_VALUE_TYPE_BOOL:
                                case DATA_VALUE_TYPE_DOUBLE_POINT:
                                case DATA_VALUE_TYPE_INT16:
                                case DATA_VALUE_TYPE_UINT32:
                                    // For non-float types, any change passes deadband
                                    deadband_passed = true;
                                    break;
                            }
                            
                            if (deadband_passed) {
                                // Check offline timing
                                extern uint32_t offline_udt_time;
                                struct timespec ts;
                                clock_gettime(CLOCK_REALTIME, &ts);
                                uint64_t current = (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
                                
                                if ((current - offline_tracking[tracking_idx].timestamps[idx]) >= (offline_udt_time * 1000)) {
                                    offline_tracking[tracking_idx].timestamps[idx] = current;
                                    offline_enqueue = true;
                                }
                            }
                            
                            pthread_mutex_unlock(&ctx->mutex);
                        }
                    }
                }

                // Update the data
                bool should_send_immediately = update_data(ctx, slave, ioa, &val);
                
                // Determine if we should enqueue
                bool should_enqueue = should_send_immediately || offline_enqueue;

                if (should_enqueue) {
                    CS101_AppLayerParameters alParams = CS104_Slave_getAppLayerParameters(slave);
                    CS101_ASDU newAsdu = CS101_ASDU_create(
                        alParams, false, CS101_COT_SPONTANEOUS,
                        0, ASDU, false, false  // OA=0, CA=ASDU
                    );

                    if (newAsdu) {
                        InformationObject io = NULL;
                        
                        // Use generic offline IO creator for non-timestamped types when offline
                        if (offline_enqueue) {
                            io = create_offline_io_for_type(type_id, ioa, &val);
                        } else {
                            // Normal case: use the original type
                            io = create_io_for_type(type_id, ioa, &val);
                        }
                        
                        if (io) {
                            CS101_ASDU_addInformationObject(newAsdu, io);
                            InformationObject_destroy(io);
                            CS104_Slave_enqueueASDU(slave, newAsdu);
                        }
                        CS101_ASDU_destroy(newAsdu);
                    }
                }
            } else {
                LOG_WARN("Unknown data type ID: %d", type_id);
            }
        }
    }

    cJSON_Delete(json);
    return true;
}
