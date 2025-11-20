#ifndef DATA_MANAGER_H
#define DATA_MANAGER_H

#include "data_types.h"
#include "../../lib60870/lib60870-C/src/inc/api/cs104_slave.h"
#include <pthread.h>

/**
 * Dynamic IOA configuration
 * Stores the list of IOA addresses configured for a data type
 */
typedef struct {
    int *ioa_list;      // Array of IOA addresses
    int count;          // Number of IOAs
} DynamicIOAConfig;

/**
 * Data type context - encapsulates all data for one type
 *
 * This structure consolidates everything related to a single IEC104 data type:
 * - Configuration (which IOAs are configured)
 * - Runtime data (current values)
 * - Offline update tracking
 * - Thread safety (mutex)
 * - Type metadata
 *
 * Benefits:
 * - Encapsulation: All related data is together
 * - Type safety: Each context knows its type
 * - Thread safety: Each has its own mutex
 * - Easy to extend: Add new fields here without changing 9 places
 */
typedef struct {
    TypeID type_id;                     // IEC104 TypeID
    const DataTypeInfo* type_info;      // Pointer to type metadata
    DynamicIOAConfig config;            // IOA configuration
    DataValue* data_array;              // Current data values
    uint64_t* last_offline_update;      // Timestamps for offline updates
    pthread_mutex_t mutex;              // Thread safety
} DataTypeContext;

/**
 * Global contexts for all data types
 * One context for each of the 10 supported data types
 */
extern DataTypeContext g_data_contexts[10];

/**
 * Initialize all data contexts
 * Call this once at startup before using any data functions
 */
void init_data_contexts(void);

/**
 * Cleanup all data contexts
 * Call this at shutdown to free all allocated memory
 */
void cleanup_data_contexts(void);

/**
 * Generic update function - REPLACES 9 FUNCTIONS!
 *
 * This single function handles updates for ALL data types.
 * It replaces:
 * - update_m_sp_tb_1_data()
 * - update_m_dp_tb_1_data()
 * - update_m_me_td_1_data()
 * - update_m_it_tb_1_data()
 * - update_m_sp_na_1_data()
 * - update_m_dp_na_1_data()
 * - update_m_me_na_1_data()
 * - update_m_me_nb_1_data()
 * - update_m_me_nc_1_data()
 *
 * @param ctx The data type context to update
 * @param slave The CS104 slave instance
 * @param ioa The IOA address to update
 * @param new_value The new value to set
 * @return true if value changed and should be sent, false otherwise
 */
bool update_data(DataTypeContext* ctx, CS104_Slave slave,
                 int ioa, const DataValue* new_value);

/**
 * Find IOA index in configuration
 *
 * @param config The IOA configuration to search
 * @param ioa The IOA address to find
 * @return Index in ioa_list array, or -1 if not found
 */
int find_ioa_index(const DynamicIOAConfig* config, int ioa);

/**
 * Get context by type ID
 *
 * @param type_id The IEC104 TypeID
 * @return Pointer to context or NULL if not found
 */
DataTypeContext* get_data_context(TypeID type_id);

/**
 * Check if a client is connected
 *
 * @param slave The CS104 slave instance
 * @return true if at least one client is connected
 */
bool is_client_connected(CS104_Slave slave);

/**
 * Reset all process data
 * 
 * Resets all data values to 0/false.
 * Used for C_RP_NA_1 (Reset Process Command).
 */
void reset_all_data(void);

#endif // DATA_MANAGER_H
