#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "../src/protocol/interrogation.h"
#include "../src/data/data_manager.h"
#include "../src/data/data_types.h"

// Mock global variables - alParameters is a pointer type
struct sCS101_AppLayerParameters alParams_struct;
CS101_AppLayerParameters alParameters = &alParams_struct;
int ASDU = 1;

// Additional globals required by data_manager.c
uint32_t offline_udt_time = 5000;
float deadband_M_ME_NC_1_percent = 1.0f;

// Mock connection structure for testing
typedef struct {
    int asdu_count;
    int io_count;
    bool act_con_sent;
    bool act_term_sent;
    bool negative_con;
} MockConnection;

static MockConnection mock_conn;

// Mock IMasterConnection_sendASDU - must return bool
bool IMasterConnection_sendASDU(IMasterConnection connection, CS101_ASDU asdu) {
    (void)connection;
    mock_conn.asdu_count++;
    
    // Count IOs in this ASDU
    int num_ios = CS101_ASDU_getNumberOfElements(asdu);
    mock_conn.io_count += num_ios;
    
    printf("  Mock: Sent ASDU with %d IOs (total: %d ASDUs, %d IOs)\n",
           num_ios, mock_conn.asdu_count, mock_conn.io_count);
    return true;
}

// Mock IMasterConnection_sendACT_CON - must return bool
bool IMasterConnection_sendACT_CON(IMasterConnection connection, CS101_ASDU asdu, bool negative) {
    (void)connection;
    (void)asdu;
    mock_conn.act_con_sent = true;
    mock_conn.negative_con = negative;
    printf("  Mock: Sent ACT-CON (negative=%d)\n", negative);
    return true;
}

// Mock IMasterConnection_sendACT_TERM - must return bool
bool IMasterConnection_sendACT_TERM(IMasterConnection connection, CS101_ASDU asdu) {
    (void)connection;
    (void)asdu;
    mock_conn.act_term_sent = true;
    printf("  Mock: Sent ACT-TERM\n");
    return true;
}

void reset_mock_connection() {
    memset(&mock_conn, 0, sizeof(mock_conn));
}

void test_interrogation_empty_config() {
    printf("\nTesting interrogation with empty configuration...\n");
    
    reset_mock_connection();
    init_data_contexts();
    
    // Create mock ASDU
    CS101_ASDU asdu = CS101_ASDU_create(alParameters, false, CS101_COT_INTERROGATED_BY_STATION,
                                        0, 1, false, false);
    
    bool result = interrogationHandler(NULL, (IMasterConnection)&mock_conn, asdu, 20);
    
    assert(result == true);
    assert(mock_conn.act_con_sent == true);
    assert(mock_conn.act_term_sent == true);
    assert(mock_conn.negative_con == false);
    assert(mock_conn.asdu_count == 0); // No data configured
    
    CS101_ASDU_destroy(asdu);
    cleanup_data_contexts();
    
    printf("  ✓ Empty config handled correctly\n");
}

void test_interrogation_with_data() {
    printf("\nTesting interrogation with configured data...\n");
    
    reset_mock_connection();
    init_data_contexts();
    
    // Configure some data
    DataTypeContext* ctx1 = get_data_context(M_SP_TB_1);
    ctx1->config.ioa_list = (int*)malloc(3 * sizeof(int));
    ctx1->config.ioa_list[0] = 100;
    ctx1->config.ioa_list[1] = 101;
    ctx1->config.ioa_list[2] = 102;
    ctx1->config.count = 3;
    ctx1->data_array = (DataValue*)calloc(3, sizeof(DataValue));
    for (int i = 0; i < 3; i++) {
        ctx1->data_array[i].type = DATA_VALUE_TYPE_BOOL;
        ctx1->data_array[i].value.bool_val = (i % 2 == 0);
        ctx1->data_array[i].quality = IEC60870_QUALITY_GOOD;
        ctx1->data_array[i].has_quality = true;
        ctx1->data_array[i].has_timestamp = true;
    }
    
    DataTypeContext* ctx2 = get_data_context(M_ME_NC_1);
    ctx2->config.ioa_list = (int*)malloc(2 * sizeof(int));
    ctx2->config.ioa_list[0] = 200;
    ctx2->config.ioa_list[1] = 201;
    ctx2->config.count = 2;
    ctx2->data_array = (DataValue*)calloc(2, sizeof(DataValue));
    for (int i = 0; i < 2; i++) {
        ctx2->data_array[i].type = DATA_VALUE_TYPE_FLOAT;
        ctx2->data_array[i].value.float_val = 123.45f + i;
        ctx2->data_array[i].quality = IEC60870_QUALITY_GOOD;
        ctx2->data_array[i].has_quality = true;
    }
    
    CS101_ASDU asdu = CS101_ASDU_create(alParameters, false, CS101_COT_INTERROGATED_BY_STATION,
                                        0, 1, false, false);
    
    bool result = interrogationHandler(NULL, (IMasterConnection)&mock_conn, asdu, 20);
    
    assert(result == true);
    assert(mock_conn.act_con_sent == true);
    assert(mock_conn.act_term_sent == true);
    assert(mock_conn.negative_con == false);
    assert(mock_conn.asdu_count == 2); // 2 types with data
    assert(mock_conn.io_count == 5);   // 3 + 2 IOs
    
    CS101_ASDU_destroy(asdu);
    cleanup_data_contexts();
    
    printf("  ✓ Interrogation with data works correctly\n");
}

void test_interrogation_unsupported_qoi() {
    printf("\nTesting interrogation with unsupported QOI...\n");
    
    reset_mock_connection();
    init_data_contexts();
    
    CS101_ASDU asdu = CS101_ASDU_create(alParameters, false, CS101_COT_INTERROGATED_BY_STATION,
                                        0, 1, false, false);
    
    bool result = interrogationHandler(NULL, (IMasterConnection)&mock_conn, asdu, 99);
    
    assert(result == false);
    assert(mock_conn.act_con_sent == true);
    assert(mock_conn.negative_con == true); // Negative confirmation
    assert(mock_conn.act_term_sent == false);
    
    CS101_ASDU_destroy(asdu);
    cleanup_data_contexts();
    
    printf("  ✓ Unsupported QOI rejected correctly\n");
}

void test_send_interrogation_for_type() {
    printf("\nTesting send_interrogation_for_type()...\n");
    
    reset_mock_connection();
    init_data_contexts();
    
    DataTypeContext* ctx = get_data_context(M_DP_TB_1);
    ctx->config.ioa_list = (int*)malloc(5 * sizeof(int));
    for (int i = 0; i < 5; i++) {
        ctx->config.ioa_list[i] = 300 + i;
    }
    ctx->config.count = 5;
    ctx->data_array = (DataValue*)calloc(5, sizeof(DataValue));
    for (int i = 0; i < 5; i++) {
        ctx->data_array[i].type = DATA_VALUE_TYPE_DOUBLE_POINT;
        ctx->data_array[i].value.dp_val = IEC60870_DOUBLE_POINT_ON;
        ctx->data_array[i].quality = IEC60870_QUALITY_GOOD;
        ctx->data_array[i].has_quality = true;
        ctx->data_array[i].has_timestamp = true;
    }
    
    bool result = send_interrogation_for_type((IMasterConnection)&mock_conn, ctx, 1);
    
    assert(result == true);
    assert(mock_conn.asdu_count == 1);
    assert(mock_conn.io_count == 5);
    
    cleanup_data_contexts();
    
    printf("  ✓ send_interrogation_for_type works correctly\n");
}

void test_send_interrogation_chunking() {
    printf("\nTesting ASDU chunking with large dataset...\n");
    
    reset_mock_connection();
    init_data_contexts();
    
    // Create a large dataset that will require multiple ASDUs
    DataTypeContext* ctx = get_data_context(M_SP_NA_1);
    int large_count = 100;
    ctx->config.ioa_list = (int*)malloc(large_count * sizeof(int));
    for (int i = 0; i < large_count; i++) {
        ctx->config.ioa_list[i] = 1000 + i;
    }
    ctx->config.count = large_count;
    ctx->data_array = (DataValue*)calloc(large_count, sizeof(DataValue));
    for (int i = 0; i < large_count; i++) {
        ctx->data_array[i].type = DATA_VALUE_TYPE_BOOL;
        ctx->data_array[i].value.bool_val = true;
        ctx->data_array[i].quality = IEC60870_QUALITY_GOOD;
        ctx->data_array[i].has_quality = true;
    }
    
    bool result = send_interrogation_for_type((IMasterConnection)&mock_conn, ctx, 1);
    
    assert(result == true);
    assert(mock_conn.asdu_count > 1); // Should be chunked into multiple ASDUs
    assert(mock_conn.io_count == large_count);
    
    printf("  ✓ Large dataset chunked into %d ASDUs\n", mock_conn.asdu_count);
    
    cleanup_data_contexts();
}

void test_send_interrogation_null_params() {
    printf("\nTesting send_interrogation_for_type() with NULL params...\n");
    
    init_data_contexts();
    
    DataTypeContext* ctx = get_data_context(M_SP_TB_1);
    
    bool result1 = send_interrogation_for_type(NULL, ctx, 1);
    assert(result1 == false);
    
    bool result2 = send_interrogation_for_type((IMasterConnection)&mock_conn, NULL, 1);
    assert(result2 == false);
    
    cleanup_data_contexts();
    
    printf("  ✓ NULL parameters handled correctly\n");
}

int main() {
    printf("===========================================\n");
    printf("Running interrogation test suite\n");
    printf("===========================================\n");
    
    // Initialize app layer parameters (pointer to struct)
    alParameters->maxSizeOfASDU = 249;
    alParameters->sizeOfCOT = 2;
    alParameters->sizeOfCA = 2;
    alParameters->sizeOfIOA = 3;
    alParameters->originatorAddress = 0;
    
    test_interrogation_empty_config();
    test_interrogation_with_data();
    test_interrogation_unsupported_qoi();
    test_send_interrogation_for_type();
    test_send_interrogation_chunking();
    test_send_interrogation_null_params();
    
    printf("\n===========================================\n");
    printf("✓ All interrogation tests passed!\n");
    printf("===========================================\n");
    
    return 0;
}
