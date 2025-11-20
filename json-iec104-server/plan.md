
## Tổng quan
File `json-iec104-server/json-iec104-server.c` hiện tại có **2,313 dòng code** trong 1 file duy nhất với nhiều vấn đề nghiêm trọng về maintainability. Kế hoạch này sẽ hệ thống lại code thành cấu trúc module hóa, giảm code duplication và cải thiện chất lượng tổng thể.

---

## 1. CÁC VẤN ĐỀ CHÍNH

### 🔴 Vấn đề 1: Code Duplication Cực Cao (~40%)

#### 1.1. 9 Update Functions giống hệt nhau
**Các hàm bị trùng lặp:**
- `update_m_sp_tb_1_data()` - dòng 1185-1212
- `update_m_dp_tb_1_data()` - dòng 1214-1242
- `update_m_me_td_1_data()` - dòng 1244-1271
- `update_m_it_tb_1_data()` - dòng 1273-1300
- `update_m_sp_na_1_data()` - dòng 1302-1330
- `update_m_dp_na_1_data()` - dòng 1332-1359
- `update_m_me_na_1_data()` - dòng 1361-1389
- `update_m_me_nb_1_data()` - dòng 1391-1419
- `update_m_me_nc_1_data()` - dòng 1421-1451

**Pattern lặp lại (90% giống nhau):**
```c
// Bước 1: Kiểm tra address range
// Bước 2: Lock mutex
// Bước 3: So sánh giá trị cũ/mới
// Bước 4: Set changed flag
// Bước 5: Unlock mutex
// Bước 6: Kiểm tra IsClientConnected hoặc allow_offline_update
// Bước 7: Return changed status
```

**Tác động:** ~250 dòng code trùng lặp

#### 1.2. 14 Configuration Parsing Blocks giống nhau
**Trong hàm `parse_config_from_json()` (dòng 1477-1886):**
- 14 blocks parse config cho 14 data types
- Mỗi block ~30-40 dòng
- Pattern: Get JSON array → Allocate memory → Parse items → Initialize

**Tác động:** ~400 dòng code trùng lặp

#### 1.3. 9 Interrogation Handler Blocks giống nhau
**Trong hàm `interrogationHandler()` (dòng 545-785):**
- 9 blocks gửi data cho mỗi type
- Pattern: Lock → Calculate chunks → Loop → Create ASDU → Send → Unlock

**Tác động:** ~200 dòng code trùng lặp

#### 1.4. 7 Process Data Update Blocks giống nhau
**Trong hàm `process_data_update()` (dòng 2106-2283):**
- 7 case statements với logic tương tự
- Pattern: Find IOA → Update → Create ASDU → Send

**Tác động:** ~150 dòng code trùng lặp

### 🔴 Vấn đề 2: Functions Quá Dài và Phức Tạp

| Function | Lines | Cyclomatic Complexity | Issue |
|----------|-------|----------------------|-------|
| `parse_config_from_json()` | 490 | ~30 | Quá dài, khó đọc |
| `asduHandler()` | 290 | ~25 | Switch case phức tạp |
| `interrogationHandler()` | 240 | ~20 | Nhiều nested loops |
| `process_data_update()` | 177 | ~15 | Switch case lặp lại |

**Best practice:** Functions nên < 50-100 dòng, cyclomatic complexity < 10

### 🔴 Vấn đề 3: Thiếu Module Hóa Hoàn Toàn

**File hiện tại chứa TẤT CẢ logic:**
```
json-iec104-server.c (2313 lines)
├── Configuration management (parsing, validation)
├── Data structures (9 data types)
├── Protocol handlers (interrogation, commands, clock sync)
├── Thread management (3 threads)
├── JSON processing
├── Memory management
├── Main application logic
└── Logging & utilities
```

**Nên tách thành:**
```
src/
├── config/          (Configuration management)
├── data/            (Data structures & management)
├── protocol/        (Protocol handlers)
├── threads/         (Thread implementations)
└── utils/           (Utilities, logging, helpers)
```

### 🔴 Vấn đề 4: Quá Nhiều Biến Global (>50 biến)

**Global variables hiện tại:**
- 14 `DynamicIOAConfig` structures
- 9 data arrays (M_SP_TB_1_data, M_DP_TB_1_data, ...)
- 9 last_offline_update arrays
- 9 pthread_mutex_t
- 1 selectTableMutex
- Các config variables (offline_udt_time, deadband, ASDU, command_mode...)

**Vấn đề:**
- Không có encapsulation
- Thread-safety phụ thuộc vào developer nhớ lock đúng mutex
- Khó test và debug
- Tight coupling giữa các components

### 🔴 Vấn đề 5: Error Handling Không Nhất Quán

**Các vấn đề:**
1. Return values không nhất quán (bool, void, int)
2. Error messages không có structure (in ra stdout)
3. Không có error codes
4. Memory leak risk khi allocation fail ở giữa
5. Race conditions trong một số update functions

---

## 2. CHI TIẾT TỪNG BƯỚC THỰC HIỆN

### Phase 1: Tạo Generic Data Type System ⭐

**Mục tiêu:** Thay thế 9 structs riêng lẻ bằng 1 generic system

#### Bước 1.1: Tạo file `src/data/data_types.h`

```c
#ifndef DATA_TYPES_H
#define DATA_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include "iec60870_common.h"

// Generic data value types
typedef enum {
    DATA_VALUE_TYPE_BOOL,           // Single point
    DATA_VALUE_TYPE_DOUBLE_POINT,   // Double point
    DATA_VALUE_TYPE_INT16,          // Scaled value
    DATA_VALUE_TYPE_UINT32,         // Integrated totals
    DATA_VALUE_TYPE_FLOAT           // Short floating point
} DataValueType;

// Generic data value container
typedef struct {
    DataValueType type;
    union {
        bool bool_val;                    // For M_SP_*
        DoublePointValue dp_val;          // For M_DP_*
        int16_t int16_val;                // For M_ME_NB_1
        uint32_t uint32_val;              // For M_IT_*
        float float_val;                  // For M_ME_NC_1, M_ME_TD_1
    } value;
    QualityDescriptor quality;
    bool has_quality;
    CP56Time2a timestamp;
    bool has_timestamp;
} DataValue;

// Data type information
typedef struct {
    TypeID type_id;                 // IEC104 TypeID (M_SP_TB_1, etc.)
    const char* name;               // String name
    DataValueType value_type;       // Value type
    bool has_time_tag;              // Has CP56Time2a?
    bool has_quality;               // Has QualityDescriptor?
    size_t io_size;                 // Size for ASDU calculation
} DataTypeInfo;

// Lookup table for all data types
extern const DataTypeInfo DATA_TYPE_TABLE[];
extern const int DATA_TYPE_COUNT;

// Helper functions
const DataTypeInfo* get_data_type_info(TypeID type_id);
const DataTypeInfo* get_data_type_info_by_name(const char* name);
TypeID parse_type_id_from_string(const char* str);

#endif // DATA_TYPES_H
```

#### Bước 1.2: Implement `src/data/data_types.c`

```c
#include "data_types.h"
#include <string.h>

const DataTypeInfo DATA_TYPE_TABLE[] = {
    // Type ID         Name           Value Type                Has Time  Has Quality  IO Size
    {M_SP_TB_1,   "M_SP_TB_1",   DATA_VALUE_TYPE_BOOL,          true,     true,        8},
    {M_DP_TB_1,   "M_DP_TB_1",   DATA_VALUE_TYPE_DOUBLE_POINT,  true,     true,        8},
    {M_ME_TD_1,   "M_ME_TD_1",   DATA_VALUE_TYPE_FLOAT,         true,     true,        10},
    {M_IT_TB_1,   "M_IT_TB_1",   DATA_VALUE_TYPE_UINT32,        true,     false,       12},
    {M_SP_NA_1,   "M_SP_NA_1",   DATA_VALUE_TYPE_BOOL,          false,    true,        4},
    {M_DP_NA_1,   "M_DP_NA_1",   DATA_VALUE_TYPE_DOUBLE_POINT,  false,    true,        4},
    {M_ME_NA_1,   "M_ME_NA_1",   DATA_VALUE_TYPE_FLOAT,         false,    true,        6},
    {M_ME_NB_1,   "M_ME_NB_1",   DATA_VALUE_TYPE_INT16,         false,    true,        5},
    {M_ME_NC_1,   "M_ME_NC_1",   DATA_VALUE_TYPE_FLOAT,         false,    true,        6},
    {M_ME_ND_1,   "M_ME_ND_1",   DATA_VALUE_TYPE_FLOAT,         false,    false,       5}
};

const int DATA_TYPE_COUNT = sizeof(DATA_TYPE_TABLE) / sizeof(DATA_TYPE_TABLE[0]);

const DataTypeInfo* get_data_type_info(TypeID type_id) {
    for (int i = 0; i < DATA_TYPE_COUNT; i++) {
        if (DATA_TYPE_TABLE[i].type_id == type_id) {
            return &DATA_TYPE_TABLE[i];
        }
    }
    return NULL;
}

const DataTypeInfo* get_data_type_info_by_name(const char* name) {
    for (int i = 0; i < DATA_TYPE_COUNT; i++) {
        if (strcmp(DATA_TYPE_TABLE[i].name, name) == 0) {
            return &DATA_TYPE_TABLE[i];
        }
    }
    return NULL;
}

TypeID parse_type_id_from_string(const char* str) {
    const DataTypeInfo* info = get_data_type_info_by_name(str);
    return info ? info->type_id : 0;
}
```

#### Bước 1.3: Tạo test file `tests/test_data_types.c`

```c
#include "data_types.h"
#include <assert.h>
#include <stdio.h>

void test_lookup() {
    const DataTypeInfo* info = get_data_type_info(M_SP_TB_1);
    assert(info != NULL);
    assert(info->type_id == M_SP_TB_1);
    assert(strcmp(info->name, "M_SP_TB_1") == 0);
    assert(info->has_time_tag == true);
    printf("✓ test_lookup passed\n");
}

void test_parse_type_id() {
    TypeID type = parse_type_id_from_string("M_ME_NC_1");
    assert(type == M_ME_NC_1);
    printf("✓ test_parse_type_id passed\n");
}

int main() {
    test_lookup();
    test_parse_type_id();
    printf("All data_types tests passed!\n");
    return 0;
}
```

---

### Phase 2: Refactor Update Functions (Gộp 9 → 1) ⭐⭐

**Mục tiêu:** Gộp 9 update functions thành 1 generic function

#### Bước 2.1: Tạo `src/data/data_manager.h`

```c
#ifndef DATA_MANAGER_H
#define DATA_MANAGER_H

#include "data_types.h"
#include "cs104_slave.h"
#include <pthread.h>

// Dynamic IOA configuration (same as before)
typedef struct {
    int *ioa_list;
    int count;
} DynamicIOAConfig;

// Data type context - encapsulates all data for one type
typedef struct {
    TypeID type_id;
    const DataTypeInfo* type_info;
    DynamicIOAConfig config;
    DataValue* data_array;
    uint64_t* last_offline_update;
    pthread_mutex_t mutex;
} DataTypeContext;

// Global contexts for all data types
extern DataTypeContext g_data_contexts[DATA_TYPE_COUNT];

// Initialize data contexts
void init_data_contexts(void);

// Cleanup data contexts
void cleanup_data_contexts(void);

// Generic update function (replaces 9 functions!)
bool update_data(DataTypeContext* ctx, CS104_Slave slave,
                 int ioa, const DataValue* new_value);

// Find IOA index in config
int find_ioa_index(const DynamicIOAConfig* config, int ioa);

// Helper: Get context by type ID
DataTypeContext* get_data_context(TypeID type_id);

#endif // DATA_MANAGER_H
```

#### Bước 2.2: Implement `src/data/data_manager.c`

```c
#include "data_manager.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

// Global contexts
DataTypeContext g_data_contexts[DATA_TYPE_COUNT];

// External variables from main (will be refactored later)
extern uint32_t offline_udt_time;
extern float deadband_M_ME_NC_1_percent;

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

void cleanup_data_contexts(void) {
    for (int i = 0; i < DATA_TYPE_COUNT; i++) {
        DataTypeContext* ctx = &g_data_contexts[i];
        free(ctx->config.ioa_list);
        free(ctx->data_array);
        free(ctx->last_offline_update);
        pthread_mutex_destroy(&ctx->mutex);
    }
}

int find_ioa_index(const DynamicIOAConfig* config, int ioa) {
    for (int i = 0; i < config->count; i++) {
        if (config->ioa_list[i] == ioa) {
            return i;
        }
    }
    return -1;
}

DataTypeContext* get_data_context(TypeID type_id) {
    for (int i = 0; i < DATA_TYPE_COUNT; i++) {
        if (g_data_contexts[i].type_id == type_id) {
            return &g_data_contexts[i];
        }
    }
    return NULL;
}

// Helper: Compare values based on type
static bool values_equal(const DataValue* v1, const DataValue* v2,
                        const DataTypeInfo* type_info) {
    if (v1->type != v2->type) return false;

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
            // Apply deadband for float types
            float diff = fabs(v1->value.float_val - v2->value.float_val);
            if (type_info->type_id == M_ME_NC_1 && deadband_M_ME_NC_1_percent > 0) {
                float threshold = fabs(v1->value.float_val) * (deadband_M_ME_NC_1_percent / 100.0f);
                return diff <= threshold;
            }
            return diff < 0.0001f;  // Small epsilon
        }
        default:
            return false;
    }
}

// Helper: Check if offline update allowed
static bool allow_offline_update(uint64_t last_update_time) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    uint64_t current = (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
    return (current - last_update_time) >= (offline_udt_time * 1000);
}

// Generic update function - REPLACES 9 FUNCTIONS!
bool update_data(DataTypeContext* ctx, CS104_Slave slave,
                 int ioa, const DataValue* new_value) {
    bool rc = false;

    // Find IOA index
    int idx = find_ioa_index(&ctx->config, ioa);
    if (idx < 0) {
        printf("{\"error\":\"IOA %d not configured for type %s\"}\n",
               ioa, ctx->type_info->name);
        return false;
    }

    // Lock mutex
    pthread_mutex_lock(&ctx->mutex);

    // Compare old and new values
    bool changed = !values_equal(&ctx->data_array[idx], new_value, ctx->type_info);

    if (changed) {
        // Update data
        ctx->data_array[idx] = *new_value;
    }

    pthread_mutex_unlock(&ctx->mutex);

    // Check if we should send update
    if (changed) {
        if (CS104_Slave_getNumberOfQueueEntries(slave, NULL) > 0) {
            // Client connected
            rc = true;
        } else if (ctx->last_offline_update != NULL) {
            // Check offline update timing
            if (allow_offline_update(ctx->last_offline_update[idx])) {
                rc = true;
                struct timespec ts;
                clock_gettime(CLOCK_REALTIME, &ts);
                ctx->last_offline_update[idx] = (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
            }
        }
    }

    return rc;
}
```

**Kết quả:**
- ✅ Gộp 9 functions (250 dòng) → 1 function (~80 dòng)
- ✅ Giảm ~170 dòng code
- ✅ Logic nhất quán, dễ maintain

---

### Phase 3: Refactor Configuration Parsing ⭐⭐⭐

**Mục tiêu:** Gộp 14 blocks parsing thành 1 generic function

#### Bước 3.1: Tạo `src/config/config_parser.h`

```c
#ifndef CONFIG_PARSER_H
#define CONFIG_PARSER_H

#include <stdbool.h>
#include "cJSON/cJSON.h"
#include "../data/data_manager.h"

// Parse configuration from JSON string
bool parse_config_from_json(const char* json_string);

// Parse configuration from file
bool init_config_from_file(const char* filename);

// Parse global settings
bool parse_global_settings(cJSON* json);

// Parse data type configuration (generic)
bool parse_data_type_config(cJSON* json, const char* config_key,
                            DataTypeContext* ctx);

#endif // CONFIG_PARSER_H
```

#### Bước 3.2: Implement `src/config/config_parser.c`

```c
#include "config_parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// External global variables (will be refactored to ServerConfig struct later)
extern uint32_t offline_udt_time;
extern float deadband_M_ME_NC_1_percent;
extern int ASDU;
extern char command_mode[64];
// ... other globals

bool parse_global_settings(cJSON* json) {
    cJSON* item;

    // Parse offline_udt_time
    item = cJSON_GetObjectItemCaseSensitive(json, "offline_udt_time");
    if (cJSON_IsNumber(item)) {
        offline_udt_time = item->valueint;
    }

    // Parse deadband
    item = cJSON_GetObjectItemCaseSensitive(json, "deadband_M_ME_NC_1_percent");
    if (cJSON_IsNumber(item)) {
        deadband_M_ME_NC_1_percent = (float)item->valuedouble;
    }

    // Parse ASDU
    item = cJSON_GetObjectItemCaseSensitive(json, "asdu");
    if (cJSON_IsNumber(item)) {
        ASDU = item->valueint;
    }

    // Parse command_mode
    item = cJSON_GetObjectItemCaseSensitive(json, "command_mode");
    if (cJSON_IsString(item)) {
        strncpy(command_mode, item->valuestring, sizeof(command_mode) - 1);
    }

    // ... parse other settings

    return true;
}

// Generic parsing for any data type config
bool parse_data_type_config(cJSON* json, const char* config_key,
                            DataTypeContext* ctx) {
    cJSON* config_array = cJSON_GetObjectItemCaseSensitive(json, config_key);

    // If not present, just return true (optional config)
    if (!cJSON_IsArray(config_array)) {
        return true;
    }

    int count = cJSON_GetArraySize(config_array);
    if (count == 0) {
        return true;
    }

    // Allocate IOA list
    ctx->config.ioa_list = (int*)malloc(count * sizeof(int));
    if (!ctx->config.ioa_list) {
        printf("{\"error\":\"Failed to allocate memory for %s config\"}\n", config_key);
        return false;
    }

    // Parse IOA addresses
    for (int i = 0; i < count; i++) {
        cJSON* item = cJSON_GetArrayItem(config_array, i);
        if (cJSON_IsNumber(item)) {
            ctx->config.ioa_list[i] = item->valueint;
        } else {
            printf("{\"error\":\"Invalid IOA at index %d in %s\"}\n", i, config_key);
            free(ctx->config.ioa_list);
            return false;
        }
    }
    ctx->config.count = count;

    // Allocate data array
    ctx->data_array = (DataValue*)calloc(count, sizeof(DataValue));
    if (!ctx->data_array) {
        printf("{\"error\":\"Failed to allocate data array for %s\"}\n", config_key);
        free(ctx->config.ioa_list);
        return false;
    }

    // Initialize data values with defaults
    for (int i = 0; i < count; i++) {
        ctx->data_array[i].type = ctx->type_info->value_type;
        ctx->data_array[i].has_quality = ctx->type_info->has_quality;
        ctx->data_array[i].has_timestamp = ctx->type_info->has_time_tag;

        // Set default quality
        if (ctx->type_info->has_quality) {
            ctx->data_array[i].quality = QUALITY_GOOD;
        }
    }

    // Allocate offline update tracking if has timestamp
    if (ctx->type_info->has_time_tag) {
        ctx->last_offline_update = (uint64_t*)calloc(count, sizeof(uint64_t));
        if (!ctx->last_offline_update) {
            printf("{\"error\":\"Failed to allocate offline tracking for %s\"}\n", config_key);
            free(ctx->config.ioa_list);
            free(ctx->data_array);
            return false;
        }
    }

    return true;
}

bool parse_config_from_json(const char* json_string) {
    cJSON* json = cJSON_Parse(json_string);
    if (!json) {
        printf("{\"error\":\"Failed to parse JSON configuration\"}\n");
        return false;
    }

    // Parse global settings
    if (!parse_global_settings(json)) {
        cJSON_Delete(json);
        return false;
    }

    // Parse all data type configurations using generic function
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

    for (int i = 0; i < sizeof(config_keys) / sizeof(config_keys[0]); i++) {
        DataTypeContext* ctx = get_data_context(config_keys[i].type_id);
        if (!ctx) {
            printf("{\"error\":\"Failed to get context for type %s\"}\n", config_keys[i].key);
            cJSON_Delete(json);
            return false;
        }

        if (!parse_data_type_config(json, config_keys[i].key, ctx)) {
            cJSON_Delete(json);
            return false;
        }
    }

    cJSON_Delete(json);
    return true;
}

bool init_config_from_file(const char* filename) {
    FILE* file = fopen(filename, "r");
    if (!file) {
        printf("{\"error\":\"Failed to open config file: %s\"}\n", filename);
        return false;
    }

    // Get file size
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    // Read file content
    char* content = (char*)malloc(file_size + 1);
    if (!content) {
        printf("{\"error\":\"Failed to allocate memory for config file\"}\n");
        fclose(file);
        return false;
    }

    size_t read_size = fread(content, 1, file_size, file);
    content[read_size] = '\0';
    fclose(file);

    // Parse JSON
    bool result = parse_config_from_json(content);
    free(content);

    return result;
}
```

**Kết quả:**
- ✅ Gộp 14 blocks (400 dòng) → 1 function (~60 dòng)
- ✅ Giảm hàm parse_config_from_json từ 490 → ~120 dòng
- ✅ Giảm ~370 dòng code

---

### Phase 4: Refactor Protocol Handlers ⭐⭐

**Mục tiêu:** Generic interrogation và command handlers

#### Bước 4.1: Tạo `src/protocol/interrogation.h`

```c
#ifndef INTERROGATION_H
#define INTERROGATION_H

#include "iec60870_slave.h"

// Interrogation handler
bool interrogationHandler(void* parameter, IMasterConnection connection,
                         CS101_ASDU asdu, uint8_t qoi);

#endif // INTERROGATION_H
```

#### Bước 4.2: Implement `src/protocol/interrogation.c`

```c
#include "interrogation.h"
#include "../data/data_manager.h"
#include <stdio.h>

// External globals
extern CS101_AppLayerParameters appLayerParameters;
extern int ASDU;

// Helper: Calculate max IOAs per ASDU
static int calcMaxIOAs(int maxASDUSize, int ioSize) {
    return (maxASDUSize - 16) / ioSize;
}

// Helper: Create InformationObject based on type
static InformationObject create_io_for_type(const DataTypeContext* ctx,
                                            int ioa, int data_idx) {
    const DataValue* data = &ctx->data_array[data_idx];

    switch (ctx->type_id) {
        case M_SP_TB_1:
            return (InformationObject)SinglePointWithCP56Time2a_create(
                NULL, ioa, data->value.bool_val, data->quality, &data->timestamp);

        case M_DP_TB_1:
            return (InformationObject)DoublePointWithCP56Time2a_create(
                NULL, ioa, data->value.dp_val, data->quality, &data->timestamp);

        case M_ME_TD_1:
            return (InformationObject)MeasuredValueNormalizedWithCP56Time2a_create(
                NULL, ioa, data->value.float_val, data->quality, &data->timestamp);

        case M_ME_NC_1:
            return (InformationObject)MeasuredValueShortWithoutTime_create(
                NULL, ioa, data->value.float_val, data->quality);

        // ... other cases

        default:
            return NULL;
    }
}

// Generic function to send interrogation for one data type
static bool send_interrogation_for_type(IMasterConnection connection,
                                       DataTypeContext* ctx) {
    pthread_mutex_lock(&ctx->mutex);

    if (ctx->config.count > 0) {
        const int maxIOAs = calcMaxIOAs(
            appLayerParameters->maxSizeOfASDU,
            ctx->type_info->io_size
        );

        // Send data in chunks
        for (int i = 0; i < ctx->config.count; i += maxIOAs) {
            CS101_ASDU newAsdu = CS101_ASDU_create(
                appLayerParameters, false,
                CS101_COT_INTERROGATED_BY_STATION,
                ASDU, ASDU, false, false
            );

            int end = (i + maxIOAs < ctx->config.count) ?
                      (i + maxIOAs) : ctx->config.count;

            for (int j = i; j < end; j++) {
                InformationObject io = create_io_for_type(ctx,
                    ctx->config.ioa_list[j], j);

                if (io) {
                    CS101_ASDU_addInformationObject(newAsdu, io);
                    InformationObject_destroy(io);
                }
            }

            IMasterConnection_sendASDU(connection, newAsdu);
            CS101_ASDU_destroy(newAsdu);
        }
    }

    pthread_mutex_unlock(&ctx->mutex);
    return true;
}

bool interrogationHandler(void* parameter, IMasterConnection connection,
                         CS101_ASDU asdu, uint8_t qoi) {
    if (qoi == 20) {  // Station interrogation
        IMasterConnection_sendACT_CON(connection, asdu, false);

        // Iterate through all data types
        for (int i = 0; i < DATA_TYPE_COUNT; i++) {
            send_interrogation_for_type(connection, &g_data_contexts[i]);
        }

        IMasterConnection_sendACT_TERM(connection, asdu);

        printf("{\"status\":\"Interrogation completed\"}\n");
    } else {
        IMasterConnection_sendACT_CON(connection, asdu, true);
    }

    return true;
}
```

**Kết quả:**
- ✅ Gộp 9 blocks (200 dòng) → 1 loop (~50 dòng)
- ✅ Giảm hàm interrogationHandler từ 240 → ~80 dòng
- ✅ Giảm ~160 dòng code

---

### Phase 5: Tách Module Files 📁

**Mục tiêu:** Tách code thành các files có tổ chức

#### Bước 5.1: Tạo cấu trúc thư mục

```bash
mkdir -p src/config
mkdir -p src/data
mkdir -p src/protocol
mkdir -p src/threads
mkdir -p src/utils
mkdir -p tests
mkdir -p include
```

#### Bước 5.2: Di chuyển code vào modules

**Files cần tạo:**

1. **Config module:**
   - `src/config/config_parser.c/h` ✅ (đã tạo ở Phase 3)
   - `src/config/config_types.h` - Data structures cho config
   - `src/config/server_config.c/h` - Encapsulate tất cả global config

2. **Data module:**
   - `src/data/data_types.c/h` ✅ (đã tạo ở Phase 1)
   - `src/data/data_manager.c/h` ✅ (đã tạo ở Phase 2)

3. **Protocol module:**
   - `src/protocol/interrogation.c/h` ✅ (đã tạo ở Phase 4)
   - `src/protocol/commands.c/h` - Command handlers (C_SC, C_DC, C_SE...)
   - `src/protocol/clock_sync.c/h` - Clock sync handler
   - `src/protocol/reset_process.c/h` - Reset handler
   - `src/protocol/connection.c/h` - Connection management

4. **Threads module:**
   - `src/threads/periodic_sender.c/h` - Periodic data thread
   - `src/threads/integrated_totals.c/h` - IT thread
   - `src/threads/queue_monitor.c/h` - Queue logging

5. **Utils module:**
   - `src/utils/time_utils.c/h` - Time conversion utilities
   - `src/utils/logger.c/h` - Structured logging
   - `src/utils/error_codes.c/h` - Error code definitions

6. **Main:**
   - `src/main.c` - Entry point, main loop

#### Bước 5.3: Tạo Makefile

```makefile
CC = gcc
CFLAGS = -Wall -Wextra -I./include -I./src -pthread
LDFLAGS = -lm -lpthread -l60870

SRC_DIRS = src src/config src/data src/protocol src/threads src/utils
SOURCES = $(foreach dir,$(SRC_DIRS),$(wildcard $(dir)/*.c))
OBJECTS = $(SOURCES:.c=.o)

TARGET = json-iec104-server

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $(TARGET) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET)

test:
	$(MAKE) -C tests

.PHONY: all clean test
```

---

### Phase 6: Error Handling & Logging 🔧

#### Bước 6.1: Tạo `src/utils/error_codes.h`

```c
#ifndef ERROR_CODES_H
#define ERROR_CODES_H

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
} IEC104_ErrorCode;

typedef struct {
    IEC104_ErrorCode code;
    const char* message;
    const char* file;
    int line;
} IEC104_Error;

void report_error(const IEC104_Error* error);
const char* error_code_to_string(IEC104_ErrorCode code);

#define RETURN_ERROR(code, msg) \
    do { \
        IEC104_Error err = {code, msg, __FILE__, __LINE__}; \
        report_error(&err); \
        return code; \
    } while(0)

#endif // ERROR_CODES_H
```

#### Bước 6.2: Tạo `src/utils/logger.h`

```c
#ifndef LOGGER_H
#define LOGGER_H

#include <stdbool.h>

typedef enum {
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR
} LogLevel;

void log_message(LogLevel level, const char* format, ...);
void log_json(LogLevel level, const char* key, const char* value);
void set_log_level(LogLevel level);

#define LOG_DEBUG(...) log_message(LOG_LEVEL_DEBUG, __VA_ARGS__)
#define LOG_INFO(...)  log_message(LOG_LEVEL_INFO, __VA_ARGS__)
#define LOG_WARN(...)  log_message(LOG_LEVEL_WARN, __VA_ARGS__)
#define LOG_ERROR(...) log_message(LOG_LEVEL_ERROR, __VA_ARGS__)

#endif // LOGGER_H
```

---

### Phase 7: Testing & Documentation 🧪

#### Bước 7.1: Viết Unit Tests

```bash
tests/
├── test_data_types.c
├── test_data_manager.c
├── test_config_parser.c
├── test_interrogation.c
└── test_runner.c
```

#### Bước 7.2: End-to-End Testing

- Test với real IEC104 client
- Test interrogation
- Test commands
- Test periodic sending
- Memory leak testing với valgrind

#### Bước 7.3: Documentation

- Update README.md
- Tạo API documentation
- Code comments
- Architecture diagram

---

### Phase 8: Missing Functionalities (Commands, Periodic, Clock Sync) 🚀

#### Bước 8.1: Command Handler (`src/protocol/command_handler.h/c`)

Implement xử lý commands từ client (C_SC_NA_1, C_SE_NC_1):

```c
#ifndef COMMAND_HANDLER_H
#define COMMAND_HANDLER_H

#include "cs104_slave.h"

// Generic ASDU handler for commands
bool asduHandler(void* parameter, IMasterConnection connection, CS101_ASDU asdu);

// Specific command handlers
bool handle_single_command(IMasterConnection connection, CS101_ASDU asdu);
bool handle_set_point_command(IMasterConnection connection, CS101_ASDU asdu);

#endif // COMMAND_HANDLER_H
```

#### Bước 8.2: Periodic Updates (`src/threads/periodic_sender.h/c`)

Implement thread gửi data định kỳ:

```c
#ifndef PERIODIC_SENDER_H
#define PERIODIC_SENDER_H

#include "cs104_slave.h"

void* periodic_sender_thread(void* arg);
void start_periodic_sender(CS104_Slave slave);
void stop_periodic_sender(void);

#endif // PERIODIC_SENDER_H
```

#### Bước 8.3: Clock Synchronization (`src/protocol/clock_sync.h/c`)

Implement xử lý đồng bộ thời gian (C_CS_NA_1):

```c
#ifndef CLOCK_SYNC_H
#define CLOCK_SYNC_H

#include "cs104_slave.h"

bool clockSyncHandler(void* parameter, IMasterConnection connection, CS101_ASDU asdu, CP56Time2a newTime);

#endif // CLOCK_SYNC_H
```

---

## 3. DANH SÁCH VIỆC CẦN LÀM

### ✅ Phase 1: Generic Data Type System
- [ ] Tạo `src/data/data_types.h`
- [ ] Implement `src/data/data_types.c`
- [ ] Viết `tests/test_data_types.c`
- [ ] Compile và test

### ✅ Phase 2: Refactor Update Functions
- [ ] Tạo `src/data/data_manager.h`
- [ ] Implement `src/data/data_manager.c` với `update_data()` generic
- [ ] Replace 9 old update functions trong file gốc
- [ ] Viết `tests/test_data_manager.c`
- [ ] Compile và test

### ✅ Phase 3: Refactor Configuration Parsing
- [ ] Tạo `src/config/config_parser.h`
- [ ] Implement `src/config/config_parser.c`
- [ ] Replace old `parse_config_from_json()` function
- [ ] Viết `tests/test_config_parser.c`
- [ ] Compile và test

### ✅ Phase 4: Refactor Protocol Handlers
- [ ] Tạo `src/protocol/interrogation.h/c`
- [ ] Replace old interrogationHandler
- [ ] Tạo `src/protocol/commands.h/c`
- [ ] Tạo `src/protocol/clock_sync.h/c`
- [ ] Tạo `src/protocol/connection.h/c`
- [ ] Compile và test

### ✅ Phase 5: Module Separation
- [ ] Tạo cấu trúc thư mục
- [ ] Tạo `src/threads/periodic_sender.h/c`
- [ ] Tạo `src/threads/integrated_totals.h/c`
- [ ] Tạo `src/utils/time_utils.h/c`
- [ ] Tạo `src/utils/logger.h/c`
- [ ] Tạo `src/utils/error_codes.h/c`
- [ ] Tạo `src/main.c` mới
- [ ] Tạo Makefile
- [ ] Compile toàn bộ project

### ✅ Phase 6: Error Handling
- [ ] Implement error codes system
- [ ] Implement logger system
- [ ] Update tất cả functions để dùng error codes
- [ ] Replace printf với structured logging

### ✅ Phase 7: Testing & Cleanup
- [ ] Viết unit tests cho tất cả modules
- [ ] End-to-end testing
- [ ] Memory leak testing
- [ ] Performance testing
- [ ] Update documentation
- [ ] Code review
- [ ] Remove old monolithic file

### Phase 8: Missing Functionalities
- [ ] Tạo `src/protocol/command_handler.h/c`
- [ ] Implement `asduHandler` cho C_SC_NA_1, C_SE_NC_1
- [ ] Tạo `src/threads/periodic_sender.h/c`
- [ ] Implement periodic update thread
- [ ] Tạo `src/protocol/clock_sync.h/c`
- [ ] Implement `clockSyncHandler`
- [ ] Update `src/main.c` để tích hợp các modules mới
- [ ] Viết tests cho các modules mới

---

## 4. BẮT ĐẦU CODE TỪNG PHẦN

### 🚀 Bắt đầu với Phase 1

**Tôi đã chuẩn bị code mẫu cho Phase 1 ở trên. Bạn có muốn:**

1. **Tạo ngay 3 files cho Phase 1:**
   - `src/data/data_types.h`
   - `src/data/data_types.c`
   - `tests/test_data_types.c`

2. **Xem lại và sửa đổi code trước khi tạo**

3. **Bắt đầu từ phase khác**

**Bạn muốn làm gì tiếp theo?**

---

## 5. METRICS & KẾT QUẢ MONG ĐỢI

### Trước Refactoring:
- 📄 **Total Lines**: 2,313
- 📄 **Files**: 1
- 🔴 **Code Duplication**: ~40%
- 📏 **Max Function Length**: 490 dòng
- 🔢 **Cyclomatic Complexity**: 30
- 🌐 **Global Variables**: >50
- ✅ **Test Coverage**: 0%

### Sau Refactoring:
- 📄 **Total Lines**: ~1,200 (-48%)
- 📄 **Files**: ~20-25
- 🟢 **Code Duplication**: <5% (-87.5%)
- 📏 **Max Function Length**: <100 dòng (-80%)
- 🔢 **Cyclomatic Complexity**: <10 (-67%)
- 🌐 **Global Variables**: <10 (-80%)
- ✅ **Test Coverage**: >80%

### Maintainability Improvements:
- ✅ Dễ tìm bugs
- ✅ Dễ thêm features mới
- ✅ Dễ test
- ✅ Dễ onboard developers mới
- ✅ Code quality cao hơn
- ✅ Technical debt giảm đáng kể

---

## Tổng kết

File hiện tại là một **monolithic implementation** với nhiều vấn đề về code quality. Kế hoạch refactoring này sẽ chuyển đổi thành một **well-structured, modular codebase** với:

- Generic programming để giảm duplication
- Clear separation of concerns
- Proper error handling
- Comprehensive testing
- Professional code organization

**Thời gian ước tính:** 10-14 tuần (nếu làm part-time)

**Bạn sẵn sàng bắt đầu chưa? 🚀**
