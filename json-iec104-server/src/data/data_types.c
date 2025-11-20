#include "data_types.h"
#include <string.h>
#include <stdio.h>

/**
 * Lookup table containing metadata for all supported IEC104 data types
 *
 * This table is the heart of our generic data type system.
 * Each entry describes one IEC104 data type with all its characteristics.
 *
 * Benefits of this approach:
 * - Single source of truth for data type properties
 * - Easy to add new data types (just add one entry here)
 * - Enables generic functions that work with any data type
 * - Eliminates code duplication across different data types
 */
const DataTypeInfo DATA_TYPE_TABLE[] = {
    // TypeID      Name           Value Type                  Has Time  Has Quality  IO Size  Offline Type
    {M_SP_TB_1,   "M_SP_TB_1",   DATA_VALUE_TYPE_BOOL,          true,     true,        11,     M_SP_TB_1},   // Already has timestamp
    {M_DP_TB_1,   "M_DP_TB_1",   DATA_VALUE_TYPE_DOUBLE_POINT,  true,     true,        11,     M_DP_TB_1},   // Already has timestamp
    {M_ME_TD_1,   "M_ME_TD_1",   DATA_VALUE_TYPE_FLOAT,         true,     true,        13,     M_ME_TD_1},   // Already has timestamp
    {M_IT_TB_1,   "M_IT_TB_1",   DATA_VALUE_TYPE_UINT32,        true,     false,       15,     M_IT_TB_1},   // Already has timestamp
    {M_SP_NA_1,   "M_SP_NA_1",   DATA_VALUE_TYPE_BOOL,          false,    true,        4,      M_SP_TB_1},   // Offline: add timestamp
    {M_DP_NA_1,   "M_DP_NA_1",   DATA_VALUE_TYPE_DOUBLE_POINT,  false,    true,        4,      M_DP_TB_1},   // Offline: add timestamp
    {M_ME_NA_1,   "M_ME_NA_1",   DATA_VALUE_TYPE_FLOAT,         false,    true,        6,      M_ME_TD_1},   // Offline: add timestamp
    {M_ME_NB_1,   "M_ME_NB_1",   DATA_VALUE_TYPE_INT16,         false,    true,        6,      M_ME_TB_1},   // Offline: add timestamp (scaled)
    {M_ME_NC_1,   "M_ME_NC_1",   DATA_VALUE_TYPE_FLOAT,         false,    true,        8,      M_ME_TF_1},   // Offline: add timestamp (short float)
    {M_ME_ND_1,   "M_ME_ND_1",   DATA_VALUE_TYPE_FLOAT,         false,    false,       5,      0}            // No offline (no quality)
};

const int DATA_TYPE_COUNT = sizeof(DATA_TYPE_TABLE) / sizeof(DATA_TYPE_TABLE[0]);

/**
 * Get data type info by TypeID
 *
 * This is a simple linear search through the lookup table.
 * Given the small number of types (10), this is efficient enough.
 * If we had many more types, we could use a hash table instead.
 */
const DataTypeInfo* get_data_type_info(TypeID type_id) {
    for (int i = 0; i < DATA_TYPE_COUNT; i++) {
        if (DATA_TYPE_TABLE[i].type_id == type_id) {
            return &DATA_TYPE_TABLE[i];
        }
    }
    return NULL;
}

/**
 * Get data type info by string name
 *
 * Useful for parsing configuration files where types are specified as strings
 * Example: "M_SP_TB_1" -> DataTypeInfo for M_SP_TB_1
 */
const DataTypeInfo* get_data_type_info_by_name(const char* name) {
    if (name == NULL) {
        return NULL;
    }

    for (int i = 0; i < DATA_TYPE_COUNT; i++) {
        if (strcmp(DATA_TYPE_TABLE[i].name, name) == 0) {
            return &DATA_TYPE_TABLE[i];
        }
    }
    return NULL;
}

/**
 * Parse TypeID from string
 *
 * Convenience function for getting just the TypeID from a string
 * Returns 0 if the type is not found
 */
TypeID parse_type_id_from_string(const char* str) {
    const DataTypeInfo* info = get_data_type_info_by_name(str);
    return info ? info->type_id : 0;
}

/**
 * Get string name from TypeID
 *
 * Useful for logging and error messages
 */
const char* type_id_to_string(TypeID type_id) {
    const DataTypeInfo* info = get_data_type_info(type_id);
    return info ? info->name : "UNKNOWN";
}

/**
 * Parse quality descriptor from string
 *
 * Supports standard IEC60870 quality descriptor names.
 * This allows JSON input to specify quality as string like:
 * {"qualifier": "IEC60870_QUALITY_INVALID"}
 * instead of numeric value.
 */
QualityDescriptor parse_qualifier_from_string(const char* str) {
    if (str == NULL) {
        return IEC60870_QUALITY_GOOD;
    }

    if (strcmp(str, "IEC60870_QUALITY_GOOD") == 0) {
        return IEC60870_QUALITY_GOOD;
    }
    if (strcmp(str, "IEC60870_QUALITY_INVALID") == 0) {
        return IEC60870_QUALITY_INVALID;
    }
    if (strcmp(str, "IEC60870_QUALITY_OVERFLOW") == 0) {
        return IEC60870_QUALITY_OVERFLOW;
    }
    if (strcmp(str, "IEC60870_QUALITY_RESERVED") == 0) {
        return IEC60870_QUALITY_RESERVED;
    }
    if (strcmp(str, "IEC60870_QUALITY_ELAPSED_TIME_INVALID") == 0) {
        return IEC60870_QUALITY_ELAPSED_TIME_INVALID;
    }
    if (strcmp(str, "IEC60870_QUALITY_BLOCKED") == 0) {
        return IEC60870_QUALITY_BLOCKED;
    }
    if (strcmp(str, "IEC60870_QUALITY_SUBSTITUTED") == 0) {
        return IEC60870_QUALITY_SUBSTITUTED;
    }
    if (strcmp(str, "IEC60870_QUALITY_NON_TOPICAL") == 0) {
        return IEC60870_QUALITY_NON_TOPICAL;
    }

    // Default to GOOD if unknown
    return IEC60870_QUALITY_GOOD;
}
