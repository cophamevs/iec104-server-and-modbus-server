#include "data_manager.h"
#include "../utils/logger.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <math.h>

/**
 * Global contexts - one for each data type
 * Initialized by init_data_contexts()
 */
DataTypeContext g_data_contexts[10];

/**
 * External global variables from the main program
 * These will be refactored into a ServerConfig struct in a later phase
 */
extern uint32_t offline_udt_time;
extern float deadband_M_ME_NC_1_percent;

/**
 * Initialize all data contexts
 *
 * This sets up the context array with default values.
 * Memory allocation for data arrays happens during config parsing.
 */
void init_data_contexts(void) {
    for (int i = 0; i < DATA_TYPE_COUNT; i++) {
        g_data_contexts[i].type_id = DATA_TYPE_TABLE[i].type_id;
        g_data_contexts[i].type_info = &DATA_TYPE_TABLE[i];
        g_data_contexts[i].config.ioa_list = NULL;
        g_data_contexts[i].config.count = 0;
        g_data_contexts[i].data_array = NULL;
        g_data_contexts[i].last_offline_update = NULL;
        pthread_mutex_init(&g_data_contexts[i].mutex, NULL);
    }
}

/**
 * Cleanup all data contexts
 *
 * Frees all allocated memory and destroys mutexes.
 * Call this at program shutdown.
 */
void cleanup_data_contexts(void) {
    for (int i = 0; i < DATA_TYPE_COUNT; i++) {
        DataTypeContext* ctx = &g_data_contexts[i];

        // Free allocated memory
        if (ctx->config.ioa_list) {
            free(ctx->config.ioa_list);
            ctx->config.ioa_list = NULL;
        }

        if (ctx->data_array) {
            free(ctx->data_array);
            ctx->data_array = NULL;
        }

        if (ctx->last_offline_update) {
            free(ctx->last_offline_update);
            ctx->last_offline_update = NULL;
        }

        // Destroy mutex
        pthread_mutex_destroy(&ctx->mutex);
    }
}

/**
 * Find IOA index in configuration
 *
 * Linear search through the IOA list.
 * This is efficient for small lists (typically < 100 IOAs per type).
 */
int find_ioa_index(const DynamicIOAConfig* config, int ioa) {
    for (int i = 0; i < config->count; i++) {
        if (config->ioa_list[i] == ioa) {
            return i;
        }
    }
    return -1;
}

/**
 * Get context by type ID
 *
 * Look up the context for a given TypeID.
 */
DataTypeContext* get_data_context(TypeID type_id) {
    for (int i = 0; i < DATA_TYPE_COUNT; i++) {
        if (g_data_contexts[i].type_id == type_id) {
            return &g_data_contexts[i];
        }
    }
    return NULL;
}

/**
 * Check if a client is connected
 *
 * Uses the CS104 slave API to check open connections.
 * Returns true if at least one client is connected.
 */
bool is_client_connected(CS104_Slave slave) {
    if (slave == NULL) {
        return false;
    }
    return CS104_Slave_getOpenConnections(slave) > 0;
}

/**
 * Compare two data values
 *
 * Returns true if values are equal (within tolerance for floats).
 * Handles all 5 value types: bool, double point, int16, uint32, float.
 *
 * For float types, applies deadband if configured.
 */
static bool values_equal(const DataValue* v1, const DataValue* v2,
                        const DataTypeInfo* type_info) {
    // Type must match
    if (v1->type != v2->type) {
        return false;
    }

    switch (v1->type) {
        case DATA_VALUE_TYPE_BOOL:
            return v1->value.bool_val == v2->value.bool_val;

        case DATA_VALUE_TYPE_DOUBLE_POINT:
            return v1->value.dp_val == v2->value.dp_val;

        case DATA_VALUE_TYPE_INT16:
            return v1->value.int16_val == v2->value.int16_val;

        case DATA_VALUE_TYPE_UINT32:
            return v1->value.uint32_val == v2->value.uint32_val;

        case DATA_VALUE_TYPE_FLOAT: {
            // Calculate difference
            float diff = fabsf(v1->value.float_val - v2->value.float_val);

            // Apply deadband for M_ME_NC_1 if configured
            if (type_info->type_id == M_ME_NC_1 && deadband_M_ME_NC_1_percent > 0.0f) {
                float threshold = fabsf(v1->value.float_val) * (deadband_M_ME_NC_1_percent / 100.0f);
                return diff <= threshold;
            }

            // For other float types, use small epsilon
            return diff < 0.0001f;
        }

        default:
            return false;
    }
}

/**
 * Check if offline update is allowed
 *
 * Returns true if enough time has passed since last offline update.
 * This prevents flooding the queue with updates when no client is connected.
 * 
 * IMPORTANT: This function UPDATES the timestamp if enough time has passed.
 * This matches the reference implementation behavior.
 */
static bool allow_offline_update(uint64_t* last_update_time_ptr) {
    if (last_update_time_ptr == NULL) {
        return true;
    }
    
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    uint64_t current = (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;

    if ((current - *last_update_time_ptr) >= (offline_udt_time * 1000)) {
        *last_update_time_ptr = current;  // Update timestamp
        return true;
    }
    return false;
}

/**
 * Generic update function - THE HEART OF PHASE 2
 *
 * This single function replaces 9 nearly-identical update functions.
 * It handles all data types through the DataTypeContext abstraction.
 *
 * Process:
 * 1. Find IOA in configuration
 * 2. Lock mutex for thread safety
 * 3. Compare old and new values
 * 4. Update if changed
 * 5. Unlock mutex
 * 6. Check if update should be sent (client connected or offline timing)
 *
 * Benefits:
 * - Eliminates ~250 lines of duplicate code
 * - Consistent behavior across all data types
 * - Single place to fix bugs or add features
 * - Easier to test and maintain
 */
bool update_data(DataTypeContext* ctx, CS104_Slave slave,
                 int ioa, const DataValue* new_value) {
    bool rc = false;

    // Validate input
    if (ctx == NULL || new_value == NULL) {
        LOG_ERROR("Invalid parameters to update_data: ctx=%p, new_value=%p", 
                  (void*)ctx, (void*)new_value);
        return false;
    }

    // Find IOA in configuration
    int idx = find_ioa_index(&ctx->config, ioa);
    if (idx < 0) {
        LOG_ERROR("IOA %d not configured for type %s", ioa, ctx->type_info->name);
        return false;
    }

    // Lock mutex for thread safety
    pthread_mutex_lock(&ctx->mutex);

    // Compare old and new values
    bool changed = !values_equal(&ctx->data_array[idx], new_value, ctx->type_info);

    if (changed) {
        // Update data
        ctx->data_array[idx] = *new_value;

        // Update timestamp if type has timestamps
        if (ctx->type_info->has_time_tag && new_value->has_timestamp) {
            ctx->data_array[idx].timestamp = new_value->timestamp;
        }

        // Update quality if type has quality
        if (ctx->type_info->has_quality && new_value->has_quality) {
            ctx->data_array[idx].quality = new_value->quality;
        }
    }

    // Unlock mutex
    pthread_mutex_unlock(&ctx->mutex);

    // Determine if update should be sent
    // Logic matches reference: send if (client_connected OR offline_update_allowed)
    if (changed) {
        // Check if client is connected first
        bool should_send = is_client_connected(slave);
        
        // If not connected, check offline update timing (if tracking is enabled)
        if (!should_send && ctx->last_offline_update != NULL) {
            should_send = allow_offline_update(&ctx->last_offline_update[idx]);
        }
        
        rc = should_send;
    }

    return rc;
}

/**
 * Reset all process data
 * 
 * Iterates through all data contexts and resets values to 0.
 * Thread-safe implementation using mutexes.
 */
void reset_all_data(void) {
    for (int i = 0; i < DATA_TYPE_COUNT; i++) {
        DataTypeContext* ctx = &g_data_contexts[i];
        
        pthread_mutex_lock(&ctx->mutex);
        
        if (ctx->data_array && ctx->config.count > 0) {
            // Reset data array based on type
            // Since DataValue is a union and we want to zero it out, 
            // memset is the safest and most efficient way.
            // This sets all value types (bool, int, float, etc.) to 0.
            memset(ctx->data_array, 0, sizeof(DataValue) * ctx->config.count);
            
            // Note: We deliberately don't reset timestamps or quality here
            // as per standard practice, or we could reset them too.
            // The reference implementation uses memset on the whole structure,
            // so we should probably do the same to match it exactly.
            // However, our DataValue structure might be different.
            // Let's just zero out the whole array of DataValues.
        }
        
        pthread_mutex_unlock(&ctx->mutex);
    }
    
    LOG_INFO("All process data reset to 0");
}
