#ifndef DATA_TYPES_H
#define DATA_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include "../../lib60870/lib60870-C/src/inc/api/iec60870_common.h"
#include "../../lib60870/lib60870-C/src/inc/api/iec60870_slave.h"

/**
 * Generic data value types for IEC 60870-5-104
 * This enum represents the actual value types stored in our system
 */
typedef enum {
    DATA_VALUE_TYPE_BOOL,           // Single point (M_SP_*)
    DATA_VALUE_TYPE_DOUBLE_POINT,   // Double point (M_DP_*)
    DATA_VALUE_TYPE_INT16,          // Scaled value (M_ME_NB_1)
    DATA_VALUE_TYPE_UINT32,         // Integrated totals (M_IT_*)
    DATA_VALUE_TYPE_FLOAT           // Normalized/Short floating point (M_ME_NA_1, M_ME_NC_1, M_ME_TD_1, M_ME_ND_1)
} DataValueType;

/**
 * Generic data value container
 * This structure can hold any IEC104 data type value
 */
typedef struct {
    DataValueType type;
    union {
        bool bool_val;                    // For M_SP_* types
        DoublePointValue dp_val;          // For M_DP_* types
        int16_t int16_val;                // For M_ME_NB_1
        uint32_t uint32_val;              // For M_IT_* types
        float float_val;                  // For M_ME_NA_1, M_ME_NC_1, M_ME_TD_1, M_ME_ND_1
    } value;
    QualityDescriptor quality;
    bool has_quality;
    CP56Time2a timestamp;
    bool has_timestamp;
} DataValue;

/**
 * Metadata for each IEC104 data type
 */
typedef struct {
    TypeID type_id;              // IEC104 type identifier
    const char* name;            // Human-readable name
    DataValueType value_type;    // Internal value representation
    bool has_time_tag;           // Whether this type includes timestamp
    bool has_quality;            // Whether this type includes quality descriptor
    int io_size;                 // Size in bytes (IOA + data)
    TypeID offline_equivalent;   // Type to use when queueing offline (0 = no offline support)
} DataTypeInfo;

/**
 * Lookup table for all supported data types
 * This table contains metadata for all 10 IEC104 data types we support
 */
extern const DataTypeInfo DATA_TYPE_TABLE[];
extern const int DATA_TYPE_COUNT;

/**
 * Get data type info by TypeID
 * @param type_id The IEC104 TypeID (e.g., M_SP_TB_1)
 * @return Pointer to DataTypeInfo or NULL if not found
 */
const DataTypeInfo* get_data_type_info(TypeID type_id);

/**
 * Get data type info by string name
 * @param name The string name (e.g., "M_SP_TB_1")
 * @return Pointer to DataTypeInfo or NULL if not found
 */
const DataTypeInfo* get_data_type_info_by_name(const char* name);

/**
 * Parse TypeID from string
 * @param str The string representation (e.g., "M_SP_TB_1")
 * @return TypeID value or 0 if not found
 */
TypeID parse_type_id_from_string(const char* str);

/**
 * Get string name from TypeID
 * @param type_id The IEC104 TypeID
 * @return String name or "UNKNOWN" if not found
 */
const char* type_id_to_string(TypeID type_id);

/**
 * Parse quality descriptor from string
 * @param str The string representation (e.g., "IEC60870_QUALITY_GOOD")
 * @return Quality descriptor value or IEC60870_QUALITY_GOOD as default
 */
QualityDescriptor parse_qualifier_from_string(const char* str);

#endif // DATA_TYPES_H
