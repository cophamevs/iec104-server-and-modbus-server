#include "../src/data/data_types.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/**
 * Test Suite for data_types module
 *
 * These tests verify that our generic data type system works correctly.
 * All tests should pass before proceeding to Phase 2.
 */

void test_data_type_count() {
    printf("Testing DATA_TYPE_COUNT...\n");
    assert(DATA_TYPE_COUNT == 10);
    printf("  ✓ DATA_TYPE_COUNT is 10\n");
}

void test_lookup_by_type_id() {
    printf("\nTesting get_data_type_info()...\n");

    // Test M_SP_TB_1
    const DataTypeInfo* info = get_data_type_info(M_SP_TB_1);
    assert(info != NULL);
    assert(info->type_id == M_SP_TB_1);
    assert(strcmp(info->name, "M_SP_TB_1") == 0);
    assert(info->value_type == DATA_VALUE_TYPE_BOOL);
    assert(info->has_time_tag == true);
    assert(info->has_quality == true);
    assert(info->io_size == 8);
    printf("  ✓ M_SP_TB_1 lookup correct\n");

    // Test M_ME_NC_1
    info = get_data_type_info(M_ME_NC_1);
    assert(info != NULL);
    assert(info->type_id == M_ME_NC_1);
    assert(strcmp(info->name, "M_ME_NC_1") == 0);
    assert(info->value_type == DATA_VALUE_TYPE_FLOAT);
    assert(info->has_time_tag == false);
    assert(info->has_quality == true);
    assert(info->io_size == 6);
    printf("  ✓ M_ME_NC_1 lookup correct\n");

    // Test M_IT_TB_1
    info = get_data_type_info(M_IT_TB_1);
    assert(info != NULL);
    assert(info->type_id == M_IT_TB_1);
    assert(info->has_quality == false);  // IT doesn't have quality
    printf("  ✓ M_IT_TB_1 lookup correct\n");

    // Test invalid type
    info = get_data_type_info(999);
    assert(info == NULL);
    printf("  ✓ Invalid TypeID returns NULL\n");
}

void test_lookup_by_name() {
    printf("\nTesting get_data_type_info_by_name()...\n");

    // Test valid names
    const DataTypeInfo* info = get_data_type_info_by_name("M_SP_TB_1");
    assert(info != NULL);
    assert(info->type_id == M_SP_TB_1);
    printf("  ✓ 'M_SP_TB_1' lookup correct\n");

    info = get_data_type_info_by_name("M_ME_ND_1");
    assert(info != NULL);
    assert(info->type_id == M_ME_ND_1);
    assert(info->has_quality == false);  // ND doesn't have quality
    printf("  ✓ 'M_ME_ND_1' lookup correct\n");

    // Test invalid names
    info = get_data_type_info_by_name("INVALID_TYPE");
    assert(info == NULL);
    printf("  ✓ Invalid name returns NULL\n");

    info = get_data_type_info_by_name(NULL);
    assert(info == NULL);
    printf("  ✓ NULL name returns NULL\n");
}

void test_parse_type_id_from_string() {
    printf("\nTesting parse_type_id_from_string()...\n");

    // Test valid strings
    TypeID type = parse_type_id_from_string("M_SP_TB_1");
    assert(type == M_SP_TB_1);
    printf("  ✓ 'M_SP_TB_1' parses correctly\n");

    type = parse_type_id_from_string("M_ME_NC_1");
    assert(type == M_ME_NC_1);
    printf("  ✓ 'M_ME_NC_1' parses correctly\n");

    // Test invalid string
    type = parse_type_id_from_string("INVALID");
    assert(type == 0);
    printf("  ✓ Invalid string returns 0\n");
}

void test_type_id_to_string() {
    printf("\nTesting type_id_to_string()...\n");

    // Test valid TypeIDs
    const char* name = type_id_to_string(M_SP_TB_1);
    assert(name != NULL);
    assert(strcmp(name, "M_SP_TB_1") == 0);
    printf("  ✓ M_SP_TB_1 converts to 'M_SP_TB_1'\n");

    name = type_id_to_string(M_DP_TB_1);
    assert(strcmp(name, "M_DP_TB_1") == 0);
    printf("  ✓ M_DP_TB_1 converts correctly\n");

    // Test invalid TypeID
    name = type_id_to_string(999);
    assert(strcmp(name, "UNKNOWN") == 0);
    printf("  ✓ Invalid TypeID returns 'UNKNOWN'\n");
}

void test_all_types_present() {
    printf("\nTesting all expected types are present...\n");

    // Test all 10 types we expect
    TypeID expected_types[] = {
        M_SP_TB_1, M_DP_TB_1, M_ME_TD_1, M_IT_TB_1,
        M_SP_NA_1, M_DP_NA_1, M_ME_NA_1, M_ME_NB_1,
        M_ME_NC_1, M_ME_ND_1
    };

    for (int i = 0; i < 10; i++) {
        const DataTypeInfo* info = get_data_type_info(expected_types[i]);
        assert(info != NULL);
    }
    printf("  ✓ All 10 expected types are present\n");
}

void test_value_types() {
    printf("\nTesting value type assignments...\n");

    // Test BOOL types
    const DataTypeInfo* info = get_data_type_info(M_SP_TB_1);
    assert(info->value_type == DATA_VALUE_TYPE_BOOL);
    info = get_data_type_info(M_SP_NA_1);
    assert(info->value_type == DATA_VALUE_TYPE_BOOL);
    printf("  ✓ Single point types use BOOL\n");

    // Test DOUBLE_POINT types
    info = get_data_type_info(M_DP_TB_1);
    assert(info->value_type == DATA_VALUE_TYPE_DOUBLE_POINT);
    info = get_data_type_info(M_DP_NA_1);
    assert(info->value_type == DATA_VALUE_TYPE_DOUBLE_POINT);
    printf("  ✓ Double point types use DOUBLE_POINT\n");

    // Test FLOAT types
    info = get_data_type_info(M_ME_TD_1);
    assert(info->value_type == DATA_VALUE_TYPE_FLOAT);
    info = get_data_type_info(M_ME_NC_1);
    assert(info->value_type == DATA_VALUE_TYPE_FLOAT);
    printf("  ✓ Measured value types use FLOAT\n");

    // Test INT16 type
    info = get_data_type_info(M_ME_NB_1);
    assert(info->value_type == DATA_VALUE_TYPE_INT16);
    printf("  ✓ Scaled value type uses INT16\n");

    // Test UINT32 type
    info = get_data_type_info(M_IT_TB_1);
    assert(info->value_type == DATA_VALUE_TYPE_UINT32);
    printf("  ✓ Integrated totals type uses UINT32\n");
}

void test_time_tag_flags() {
    printf("\nTesting time tag flags...\n");

    // Types with time tags (TB suffix)
    const DataTypeInfo* info = get_data_type_info(M_SP_TB_1);
    assert(info->has_time_tag == true);
    info = get_data_type_info(M_DP_TB_1);
    assert(info->has_time_tag == true);
    info = get_data_type_info(M_ME_TD_1);
    assert(info->has_time_tag == true);
    info = get_data_type_info(M_IT_TB_1);
    assert(info->has_time_tag == true);
    printf("  ✓ Types with time tags marked correctly\n");

    // Types without time tags (NA suffix)
    info = get_data_type_info(M_SP_NA_1);
    assert(info->has_time_tag == false);
    info = get_data_type_info(M_ME_NC_1);
    assert(info->has_time_tag == false);
    printf("  ✓ Types without time tags marked correctly\n");
}

void test_quality_flags() {
    printf("\nTesting quality flags...\n");

    // Most types have quality
    const DataTypeInfo* info = get_data_type_info(M_SP_TB_1);
    assert(info->has_quality == true);
    info = get_data_type_info(M_ME_NC_1);
    assert(info->has_quality == true);
    printf("  ✓ Types with quality marked correctly\n");

    // IT and ND types don't have quality
    info = get_data_type_info(M_IT_TB_1);
    assert(info->has_quality == false);
    info = get_data_type_info(M_ME_ND_1);
    assert(info->has_quality == false);
    printf("  ✓ IT and ND types without quality marked correctly\n");
}

int main() {
    printf("===========================================\n");
    printf("Running data_types test suite\n");
    printf("===========================================\n\n");

    test_data_type_count();
    test_lookup_by_type_id();
    test_lookup_by_name();
    test_parse_type_id_from_string();
    test_type_id_to_string();
    test_all_types_present();
    test_value_types();
    test_time_tag_flags();
    test_quality_flags();

    printf("\n===========================================\n");
    printf("✓ All data_types tests passed!\n");
    printf("===========================================\n");

    return 0;
}
