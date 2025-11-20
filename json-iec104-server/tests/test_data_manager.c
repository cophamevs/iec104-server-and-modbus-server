#include "../src/data/data_manager.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

// Mock global variables
uint32_t offline_udt_time = 10;
float deadband_M_ME_NC_1_percent = 5.0f;

void test_init_cleanup() {
    printf("\nTesting init and cleanup...\n");
    init_data_contexts();

    for (int i = 0; i < DATA_TYPE_COUNT; i++) {
        assert(g_data_contexts[i].type_info != NULL);
    }
    printf("  ✓ Contexts initialized\n");

    cleanup_data_contexts();
    printf("  ✓ Cleanup completed\n");
}

void test_get_context() {
    printf("\nTesting get_data_context()...\n");
    init_data_contexts();

    DataTypeContext* ctx = get_data_context(M_SP_TB_1);
    assert(ctx != NULL);
    assert(ctx->type_id == M_SP_TB_1);
    printf("  ✓ Context lookup works\n");

    cleanup_data_contexts();
}

void test_find_ioa() {
    printf("\nTesting find_ioa_index()...\n");

    int* ioas = (int*)malloc(3 * sizeof(int));
    ioas[0] = 100;
    ioas[1] = 101;
    ioas[2] = 102;

    DynamicIOAConfig config;
    config.ioa_list = ioas;
    config.count = 3;

    assert(find_ioa_index(&config, 100) == 0);
    assert(find_ioa_index(&config, 102) == 2);
    assert(find_ioa_index(&config, 999) == -1);
    printf("  ✓ IOA lookup works\n");

    free(ioas);
}

void test_update_bool() {
    printf("\nTesting update_data() with BOOL...\n");
    init_data_contexts();

    DataTypeContext* ctx = get_data_context(M_SP_TB_1);

    // Setup
    ctx->config.ioa_list = (int*)malloc(1 * sizeof(int));
    ctx->config.ioa_list[0] = 100;
    ctx->config.count = 1;
    ctx->data_array = (DataValue*)calloc(1, sizeof(DataValue));
    ctx->data_array[0].type = DATA_VALUE_TYPE_BOOL;
    ctx->data_array[0].value.bool_val = false;

    // Update
    DataValue new_val;
    new_val.type = DATA_VALUE_TYPE_BOOL;
    new_val.value.bool_val = true;
    new_val.has_quality = true;
    new_val.quality = IEC60870_QUALITY_GOOD;

    update_data(ctx, NULL, 100, &new_val);
    assert(ctx->data_array[0].value.bool_val == true);
    printf("  ✓ Bool update works\n");

    cleanup_data_contexts();
}

void test_update_float() {
    printf("\nTesting update_data() with FLOAT...\n");
    init_data_contexts();

    DataTypeContext* ctx = get_data_context(M_ME_NC_1);

    // Setup
    ctx->config.ioa_list = (int*)malloc(1 * sizeof(int));
    ctx->config.ioa_list[0] = 200;
    ctx->config.count = 1;
    ctx->data_array = (DataValue*)calloc(1, sizeof(DataValue));
    ctx->data_array[0].type = DATA_VALUE_TYPE_FLOAT;
    ctx->data_array[0].value.float_val = 100.0f;

    // Update
    DataValue new_val;
    new_val.type = DATA_VALUE_TYPE_FLOAT;
    new_val.value.float_val = 150.0f;
    new_val.has_quality = true;
    new_val.quality = IEC60870_QUALITY_GOOD;

    update_data(ctx, NULL, 200, &new_val);
    assert(fabsf(ctx->data_array[0].value.float_val - 150.0f) < 0.01f);
    printf("  ✓ Float update works\n");

    cleanup_data_contexts();
}

void test_invalid_ioa() {
    printf("\nTesting invalid IOA...\n");
    init_data_contexts();

    DataTypeContext* ctx = get_data_context(M_SP_TB_1);
    ctx->config.ioa_list = (int*)malloc(1 * sizeof(int));
    ctx->config.ioa_list[0] = 100;
    ctx->config.count = 1;
    ctx->data_array = (DataValue*)calloc(1, sizeof(DataValue));

    DataValue new_val;
    new_val.type = DATA_VALUE_TYPE_BOOL;
    new_val.value.bool_val = true;

    bool result = update_data(ctx, NULL, 999, &new_val);
    assert(result == false);
    printf("  ✓ Invalid IOA rejected\n");

    cleanup_data_contexts();
}

void test_null_params() {
    printf("\nTesting NULL parameters...\n");

    DataValue val;
    val.type = DATA_VALUE_TYPE_BOOL;

    assert(update_data(NULL, NULL, 100, &val) == false);
    assert(update_data((DataTypeContext*)1, NULL, 100, NULL) == false);
    printf("  ✓ NULL parameters handled\n");
}

int main() {
    printf("===========================================\n");
    printf("Running data_manager test suite\n");
    printf("===========================================\n");

    test_init_cleanup();
    test_get_context();
    test_find_ioa();
    test_update_bool();
    test_update_float();
    test_invalid_ioa();
    test_null_params();

    printf("\n===========================================\n");
    printf("✓ All data_manager tests passed!\n");
    printf("===========================================\n");

    return 0;
}
