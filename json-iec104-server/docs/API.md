# API Documentation - IEC 60870-5-104 Server Modules

**Version:** 1.0  
**Date:** 2025-11-20  
**Status:** Complete

---

## Table of Contents

1. [Data Types Module](#data-types-module)
2. [Data Manager Module](#data-manager-module)
3. [Config Parser Module](#config-parser-module)
4. [Interrogation Module](#interrogation-module)
5. [Error Codes Module](#error-codes-module)
6. [Logger Module](#logger-module)

---

## Data Types Module

**Files:** `src/data/data_types.h`, `src/data/data_types.c`

### Overview

Provides a generic data type system for handling all 10 IEC 60870-5-104 data types through a unified interface.

### Data Structures

#### `DataValueType` (enum)

Generic value types for IEC104 data.

```c
typedef enum {
    DATA_VALUE_TYPE_BOOL,           // Single point
    DATA_VALUE_TYPE_DOUBLE_POINT,   // Double point
    DATA_VALUE_TYPE_INT16,          // Scaled value
    DATA_VALUE_TYPE_UINT32,         // Integrated totals
    DATA_VALUE_TYPE_FLOAT           // Short floating point
} DataValueType;
```

#### `DataValue` (struct)

Generic container for any IEC104 data value.

```c
typedef struct {
    DataValueType type;
    union {
        bool bool_val;
        DoublePointValue dp_val;
        int16_t int16_val;
        uint32_t uint32_val;
        float float_val;
    } value;
    QualityDescriptor quality;
    bool has_quality;
    CP56Time2a timestamp;
    bool has_timestamp;
} DataValue;
```

#### `DataTypeInfo` (struct)

Metadata for each IEC104 data type.

```c
typedef struct {
    TypeID type_id;                 // IEC104 TypeID
    const char* name;               // String name
    DataValueType value_type;       // Value type
    bool has_time_tag;              // Has CP56Time2a?
    bool has_quality;               // Has QualityDescriptor?
    size_t io_size;                 // Size for ASDU calculation
} DataTypeInfo;
```

### Functions

#### `get_data_type_info()`

Get data type information by TypeID.

```c
const DataTypeInfo* get_data_type_info(TypeID type_id);
```

**Parameters:**
- `type_id` - IEC104 TypeID (e.g., M_SP_TB_1)

**Returns:**
- Pointer to `DataTypeInfo` if found
- `NULL` if type not found

**Example:**
```c
const DataTypeInfo* info = get_data_type_info(M_SP_TB_1);
if (info) {
    printf("Type: %s, Size: %zu\n", info->name, info->io_size);
}
```

#### `get_data_type_info_by_name()`

Get data type information by string name.

```c
const DataTypeInfo* get_data_type_info_by_name(const char* name);
```

**Parameters:**
- `name` - Type name string (e.g., "M_SP_TB_1")

**Returns:**
- Pointer to `DataTypeInfo` if found
- `NULL` if not found or name is NULL

**Example:**
```c
const DataTypeInfo* info = get_data_type_info_by_name("M_ME_NC_1");
```

#### `parse_type_id_from_string()`

Parse TypeID from string.

```c
TypeID parse_type_id_from_string(const char* str);
```

**Parameters:**
- `str` - Type name string

**Returns:**
- TypeID if found
- 0 if not found

**Example:**
```c
TypeID type = parse_type_id_from_string("M_SP_TB_1");
```

#### `type_id_to_string()`

Convert TypeID to string name.

```c
const char* type_id_to_string(TypeID type_id);
```

**Parameters:**
- `type_id` - IEC104 TypeID

**Returns:**
- String name if found
- "UNKNOWN" if not found

**Example:**
```c
const char* name = type_id_to_string(M_SP_TB_1);  // Returns "M_SP_TB_1"
```

### Supported Data Types

| TypeID | Name | Value Type | Time Tag | Quality | IO Size |
|--------|------|------------|----------|---------|---------|
| M_SP_TB_1 | Single point with time | BOOL | Yes | Yes | 8 |
| M_DP_TB_1 | Double point with time | DOUBLE_POINT | Yes | Yes | 8 |
| M_ME_TD_1 | Normalized measured with time | FLOAT | Yes | Yes | 10 |
| M_IT_TB_1 | Integrated totals with time | UINT32 | Yes | No | 12 |
| M_SP_NA_1 | Single point | BOOL | No | Yes | 4 |
| M_DP_NA_1 | Double point | DOUBLE_POINT | No | Yes | 4 |
| M_ME_NA_1 | Normalized measured | FLOAT | No | Yes | 6 |
| M_ME_NB_1 | Scaled measured | INT16 | No | Yes | 5 |
| M_ME_NC_1 | Short floating point | FLOAT | No | Yes | 6 |
| M_ME_ND_1 | Normalized without quality | FLOAT | No | No | 5 |

---

## Data Manager Module

**Files:** `src/data/data_manager.h`, `src/data/data_manager.c`

### Overview

Manages data storage and updates for all configured data types with thread-safe operations.

### Data Structures

#### `DynamicIOAConfig` (struct)

Configuration for IOA addresses.

```c
typedef struct {
    int* ioa_list;      // Array of IOA addresses
    int count;          // Number of IOAs
} DynamicIOAConfig;
```

#### `DataTypeContext` (struct)

Complete context for one data type.

```c
typedef struct {
    TypeID type_id;
    const DataTypeInfo* type_info;
    DynamicIOAConfig config;
    DataValue* data_array;
    uint64_t* last_offline_update;
    pthread_mutex_t mutex;
} DataTypeContext;
```

### Functions

#### `init_data_contexts()`

Initialize all data contexts.

```c
void init_data_contexts(void);
```

**Description:**
- Initializes all 10 data type contexts
- Sets up mutexes
- Must be called before using any data functions

**Example:**
```c
init_data_contexts();
```

#### `cleanup_data_contexts()`

Cleanup all data contexts.

```c
void cleanup_data_contexts(void);
```

**Description:**
- Frees all allocated memory
- Destroys mutexes
- Call at program shutdown

**Example:**
```c
cleanup_data_contexts();
```

#### `update_data()`

Generic update function for any data type.

```c
bool update_data(DataTypeContext* ctx, CS104_Slave slave,
                 int ioa, const DataValue* new_value);
```

**Parameters:**
- `ctx` - Data type context
- `slave` - IEC104 slave instance
- `ioa` - Information Object Address
- `new_value` - New data value

**Returns:**
- `true` if update should be sent to client
- `false` if no update needed or error

**Description:**
- Thread-safe with mutex locking
- Compares old and new values
- Applies deadband for float types
- Checks offline update timing
- Determines if update should be sent

**Example:**
```c
DataTypeContext* ctx = get_data_context(M_SP_TB_1);
DataValue new_value = {
    .type = DATA_VALUE_TYPE_BOOL,
    .value.bool_val = true,
    .quality = IEC60870_QUALITY_GOOD,
    .has_quality = true
};

if (update_data(ctx, slave, 100, &new_value)) {
    // Send update to client
}
```

#### `get_data_context()`

Get context for a specific data type.

```c
DataTypeContext* get_data_context(TypeID type_id);
```

**Parameters:**
- `type_id` - IEC104 TypeID

**Returns:**
- Pointer to context if found
- `NULL` if not found

**Example:**
```c
DataTypeContext* ctx = get_data_context(M_ME_NC_1);
```

#### `find_ioa_index()`

Find index of IOA in configuration.

```c
int find_ioa_index(const DynamicIOAConfig* config, int ioa);
```

**Parameters:**
- `config` - IOA configuration
- `ioa` - IOA to find

**Returns:**
- Index if found (>= 0)
- -1 if not found

**Example:**
```c
int idx = find_ioa_index(&ctx->config, 100);
if (idx >= 0) {
    // IOA found at index idx
}
```

---

## Config Parser Module

**Files:** `src/config/config_parser.h`, `src/config/config_parser.c`

### Overview

Parses JSON configuration files and initializes data contexts.

### Functions

#### `parse_config_from_json()`

Parse configuration from JSON string.

```c
bool parse_config_from_json(const char* json_string);
```

**Parameters:**
- `json_string` - JSON configuration string

**Returns:**
- `true` on success
- `false` on error

**Example:**
```c
const char* json = "{\"asdu\":1,\"M_SP_TB_1_config\":[100,101,102]}";
if (parse_config_from_json(json)) {
    // Configuration loaded successfully
}
```

#### `init_config_from_file()`

Parse configuration from file.

```c
bool init_config_from_file(const char* filename);
```

**Parameters:**
- `filename` - Path to JSON config file

**Returns:**
- `true` on success
- `false` on error

**Example:**
```c
if (init_config_from_file("config.json")) {
    // Configuration loaded successfully
}
```

#### `parse_global_settings()`

Parse global settings from JSON.

```c
bool parse_global_settings(cJSON* json);
```

**Parameters:**
- `json` - cJSON object

**Returns:**
- `true` on success
- `false` on error

**Global Settings:**
- `offline_udt_time` - Offline update time (ms)
- `deadband_M_ME_NC_1_percent` - Deadband percentage
- `asdu` - ASDU address
- `command_mode` - Command mode (direct/select)
- `port` - TCP port
- `local_ip` - Local IP address

#### `parse_data_type_config()`

Generic parser for any data type configuration.

```c
bool parse_data_type_config(cJSON* json, const char* config_key,
                            DataTypeContext* ctx);
```

**Parameters:**
- `json` - cJSON object
- `config_key` - Configuration key (e.g., "M_SP_TB_1_config")
- `ctx` - Data type context to populate

**Returns:**
- `true` on success
- `false` on error

**Example:**
```c
DataTypeContext* ctx = get_data_context(M_SP_TB_1);
parse_data_type_config(json, "M_SP_TB_1_config", ctx);
```

### Configuration Format

```json
{
  "offline_udt_time": 5000,
  "deadband_M_ME_NC_1_percent": 1.5,
  "asdu": 1,
  "command_mode": "direct",
  "port": 2404,
  "local_ip": "0.0.0.0",
  "M_SP_TB_1_config": [100, 101, 102],
  "M_ME_NC_1_config": [200, 201, 202]
}
```

---

## Interrogation Module

**Files:** `src/protocol/interrogation.h`, `src/protocol/interrogation.c`

### Overview

Handles IEC104 station interrogation (QOI=20) with automatic ASDU chunking.

### Functions

#### `interrogationHandler()`

Main interrogation handler.

```c
bool interrogationHandler(void* parameter, IMasterConnection connection,
                         CS101_ASDU asdu, uint8_t qoi);
```

**Parameters:**
- `parameter` - User parameter (unused)
- `connection` - Master connection
- `asdu` - Interrogation ASDU
- `qoi` - Qualifier of Interrogation

**Returns:**
- `true` if handled successfully
- `false` if QOI not supported

**Description:**
- Supports QOI=20 (station interrogation)
- Sends ACT-CON confirmation
- Iterates through all data types
- Sends ACT-TERM termination

**Example:**
```c
CS104_Slave_setInterrogationHandler(slave, interrogationHandler, NULL);
```

#### `send_interrogation_for_type()`

Send interrogation data for one data type.

```c
bool send_interrogation_for_type(IMasterConnection connection,
                                DataTypeContext* ctx,
                                int asdu_addr);
```

**Parameters:**
- `connection` - Master connection
- `ctx` - Data type context
- `asdu_addr` - ASDU address

**Returns:**
- `true` on success
- `false` on error

**Description:**
- Automatically chunks data into multiple ASDUs if needed
- Thread-safe with mutex locking
- Creates appropriate InformationObjects for each type

---

## Error Codes Module

**Files:** `src/utils/error_codes.h`, `src/utils/error_codes.c`

### Overview

Structured error code system with JSON logging.

### Error Codes

```c
typedef enum {
    IEC104_OK = 0,
    IEC104_ERR_INVALID_CONFIG = 1,
    IEC104_ERR_MEMORY_ALLOC = 2,
    IEC104_ERR_INVALID_IOA = 3,
    IEC104_ERR_IOA_NOT_CONFIGURED = 4,
    IEC104_ERR_MUTEX_LOCK = 5,
    IEC104_ERR_INVALID_JSON = 6,
    IEC104_ERR_FILE_NOT_FOUND = 7,
    IEC104_ERR_CLIENT_NOT_CONNECTED = 8,
    IEC104_ERR_INVALID_TYPE = 9,
    IEC104_ERR_INVALID_VALUE = 10,
    IEC104_ERR_THREAD_CREATE = 11,
    IEC104_ERR_SOCKET = 12,
    IEC104_ERR_TIMEOUT = 13,
    IEC104_ERR_UNKNOWN = 99
} IEC104_ErrorCode;
```

### Functions

#### `error_code_to_string()`

Convert error code to string.

```c
const char* error_code_to_string(IEC104_ErrorCode code);
```

#### `report_error()`

Report error with structured logging.

```c
void report_error(const IEC104_Error* error);
```

### Macros

```c
RETURN_ERROR(code, msg)       // Return with error reporting
RETURN_NULL_ERROR(code, msg)  // Return NULL with error reporting
```

**Example:**
```c
if (!config) {
    RETURN_ERROR(IEC104_ERR_INVALID_CONFIG, "Config file missing");
}
```

---

## Logger Module

**Files:** `src/utils/logger.h`, `src/utils/logger.c`

### Overview

Structured JSON logging with configurable log levels.

### Log Levels

```c
typedef enum {
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO = 1,
    LOG_LEVEL_WARN = 2,
    LOG_LEVEL_ERROR = 3
} LogLevel;
```

### Functions

#### `logger_init()`

Initialize logger with log level.

```c
void logger_init(LogLevel level);
```

#### `logger_set_level()`

Set current log level.

```c
void logger_set_level(LogLevel level);
```

#### `log_message()`

Log a formatted message.

```c
void log_message(LogLevel level, const char* format, ...);
```

### Macros

```c
LOG_DEBUG(...)  // Debug messages
LOG_INFO(...)   // Info messages
LOG_WARN(...)   // Warning messages
LOG_ERROR(...)  // Error messages
```

**Example:**
```c
logger_init(LOG_LEVEL_INFO);

LOG_INFO("Server started on port %d", port);
LOG_WARN("High temperature: %.2f", temp);
LOG_ERROR("Connection failed: %s", error);
```

### Output Format

```json
{
  "timestamp": "2025-11-20 19:03:30",
  "level": "INFO",
  "message": "Server started on port 2404"
}
```

---

## Thread Safety

All modules are thread-safe:

- **Data Manager:** Uses mutexes for data access
- **Config Parser:** Called during initialization only
- **Interrogation:** Uses mutexes via data manager
- **Logger:** Thread-safe output

---

## Error Handling

All functions follow consistent error handling:

1. Validate input parameters
2. Log errors with `LOG_ERROR()`
3. Return appropriate error code/value
4. Clean up resources on error

---

**Last Updated:** 2025-11-20 19:03  
**Version:** 1.0  
**Status:** Complete
