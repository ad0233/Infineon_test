#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "single_parse.h"

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            printf("FAIL: %s\n", msg); \
            return 1; \
        } else { \
            printf("PASS: %s\n", msg); \
        } \
    } while(0)

// 辅助函数：创建测试用的解析器
static single_parse_handle_t test_parser_create(size_t max_size, uint8_t* recv_buf, uint8_t* output_buf, struct single_parse_t* parser_struct) {
    if (single_parse_init(parser_struct, recv_buf, output_buf, max_size) == 0) {
        return parser_struct;
    }
    return NULL;
}

// 测试基本 pack/unpack 循环
static int test_basic_pack_unpack(void) {
    printf("\n=== Test: Basic pack/unpack ===\n");
    
    uint8_t data[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A};
    size_t data_len = sizeof(data);
    
    // 打包
    uint8_t frame[1024];
    int packed_len = single_parse_pack(data, data_len, frame, sizeof(frame));
    TEST_ASSERT(packed_len > 0, "pack should succeed");
    
    // 解包（使用外部缓冲区）
    uint8_t recv_buf[1024];
    uint8_t output_buf[512];
    struct single_parse_t parser_struct;
    single_parse_handle_t parser = &parser_struct;
    TEST_ASSERT(single_parse_init(parser, recv_buf, output_buf, 1024) == 0, "parser initialization should succeed");
    
    size_t out_len = 0;
    const uint8_t* out_buf = NULL;
    
    for (int i = 0; i < packed_len; i++) {
        out_buf = single_parse_unpack(parser, frame[i], &out_len);
        if (out_len > 0) {
            break;
        }
    }
    
    TEST_ASSERT(out_len == data_len, "unpacked length should match");
    TEST_ASSERT(out_buf != NULL, "unpacked buffer should not be NULL");
    TEST_ASSERT(memcmp(out_buf, data, data_len) == 0, "unpacked data should match original");
    
    single_parse_deinit(parser);
    return 0;
}

// 测试特殊字节转义
static int test_escape_special_bytes(void) {
    printf("\n=== Test: Escape special bytes ===\n");
    
    // 包含所有特殊字节的数据
    uint8_t data[] = {0xDA, 0xFA, 0x0A, 0x01, 0x02, 0xDA, 0x0A};
    size_t data_len = sizeof(data);
    
    uint8_t frame[1024];
    int packed_len = single_parse_pack(data, data_len, frame, sizeof(frame));
    TEST_ASSERT(packed_len > 0, "pack should succeed");
    
    // 验证帧中包含转义字符
    int escape_count = 0;
    for (int i = 0; i < packed_len; i++) {
        if (frame[i] == 0xFA) {
            escape_count++;
        }
    }
    TEST_ASSERT(escape_count >= 3, "frame should contain escape characters");
    
    // 解包验证
    uint8_t recv_buf2[1024];
    uint8_t output_buf2[512];
    struct single_parse_t parser_struct2;
    single_parse_handle_t parser = test_parser_create(1024, recv_buf2, output_buf2, &parser_struct2);
    TEST_ASSERT(parser != NULL, "parser creation should succeed");
    
    size_t out_len = 0;
    const uint8_t* out_buf = NULL;
    
    for (int i = 0; i < packed_len; i++) {
        out_buf = single_parse_unpack(parser, frame[i], &out_len);
        if (out_len > 0) {
            break;
        }
    }
    
    TEST_ASSERT(out_len == data_len, "unpacked length should match");
    TEST_ASSERT(memcmp(out_buf, data, data_len) == 0, "unpacked data should match original");
    
    single_parse_deinit(parser);
    return 0;
}

// 测试 CRC 校验
static int test_crc_validation(void) {
    printf("\n=== Test: CRC validation ===\n");
    
    uint8_t data[] = {0x11, 0x22, 0x33, 0x44, 0x55};
    size_t data_len = sizeof(data);
    
    uint8_t frame[1024];
    int packed_len = single_parse_pack(data, data_len, frame, sizeof(frame));
    TEST_ASSERT(packed_len > 0, "pack should succeed");
    
    // 修改帧中的一个字节（破坏 CRC）
    frame[packed_len / 2] ^= 0x01;
    
    // 解包
    uint8_t recv_buf3[1024];
    uint8_t output_buf3[512];
    struct single_parse_t parser_struct3;
    single_parse_handle_t parser = test_parser_create(1024, recv_buf3, output_buf3, &parser_struct3);
    size_t out_len = 0;
    const uint8_t* out_buf = NULL;
    
    for (int i = 0; i < packed_len; i++) {
        out_buf = single_parse_unpack(parser, frame[i], &out_len);
        if (out_len > 0) {
            break;
        }
    }
    
    // CRC16 校验失败，应该返回 NULL
    TEST_ASSERT(out_len == 0, "corrupted frame should fail CRC check");
    TEST_ASSERT(out_buf == NULL, "corrupted frame should return NULL");
    
    single_parse_deinit(parser);
    return 0;
}

// 测试边界情况：空数据
static int test_empty_data(void) {
    printf("\n=== Test: Empty data ===\n");
    
    uint8_t frame[1024];
    int packed_len = single_parse_pack(NULL, 0, frame, sizeof(frame));
    TEST_ASSERT(packed_len == -1, "pack with NULL data should fail");
    
    packed_len = single_parse_pack((uint8_t[]){0x01}, 0, frame, sizeof(frame));
    TEST_ASSERT(packed_len == -1, "pack with zero length should fail");
    
    return 0;
}

// 测试边界情况：大数据
static int test_large_data(void) {
    printf("\n=== Test: Large data ===\n");
    
    size_t large_size = 10000;
    uint8_t* large_data = (uint8_t*)malloc(large_size);
    for (size_t i = 0; i < large_size; i++) {
        large_data[i] = (uint8_t)(i & 0xFF);
    }
    
    uint8_t* frame = (uint8_t*)malloc(large_size * 2);
    int packed_len = single_parse_pack(large_data, large_size, frame, large_size * 2);
    TEST_ASSERT(packed_len > 0, "pack large data should succeed");
    
    uint8_t* recv_buf4 = (uint8_t*)malloc(large_size * 2);
    uint8_t* output_buf4 = (uint8_t*)malloc(large_size);
    struct single_parse_t parser_struct4;
    single_parse_handle_t parser = test_parser_create(large_size * 2, recv_buf4, output_buf4, &parser_struct4);
    size_t out_len = 0;
    const uint8_t* out_buf = NULL;
    
    for (int i = 0; i < packed_len; i++) {
        out_buf = single_parse_unpack(parser, frame[i], &out_len);
        if (out_len > 0) {
            break;
        }
    }
    
    TEST_ASSERT(out_len == large_size, "unpacked large data length should match");
    TEST_ASSERT(memcmp(out_buf, large_data, large_size) == 0, "unpacked large data should match");
    
    single_parse_deinit(parser);
    free(large_data);
    free(frame);
    free(recv_buf4);
    free(output_buf4);
    return 0;
}

// 测试错误帧处理
static int test_invalid_frame(void) {
    printf("\n=== Test: Invalid frame ===\n");
    
    uint8_t recv_buf5[1024];
    uint8_t output_buf5[512];
    struct single_parse_t parser_struct5;
    single_parse_handle_t parser = test_parser_create(1024, recv_buf5, output_buf5, &parser_struct5);
    size_t out_len = 0;
    
    // 发送不完整的帧
    uint8_t invalid_frame[] = {0xDA, 0xDA, 0x01, 0x02};
    for (size_t i = 0; i < sizeof(invalid_frame); i++) {
        const uint8_t* out_buf = single_parse_unpack(parser, invalid_frame[i], &out_len);
        TEST_ASSERT(out_len == 0, "incomplete frame should not produce output");
        TEST_ASSERT(out_buf == NULL, "incomplete frame should return NULL");
    }
    
    single_parse_deinit(parser);
    return 0;
}

// 测试实际帧数据
static int test_real_frame(void) {
    printf("\n=== Test: Real frame data ===\n");
    
    // 实际的帧数据（来自用户提供的十六进制字符串）
    const uint8_t real_frame[] = {
        0xDA, 0xDA, 0x01, 0x7B, 0xFA, 0x0A, 0x20, 0x20, 0x22, 0x63, 0x6D, 0x64, 0x22, 0x3A, 0x20, 0x22,
        0x77, 0x69, 0x66, 0x69, 0x5F, 0x63, 0x6F, 0x6E, 0x6E, 0x65, 0x63, 0x74, 0x22, 0x2C, 0xFA, 0x0A,
        0x20, 0x20, 0x22, 0x70, 0x61, 0x72, 0x61, 0x6D, 0x73, 0x22, 0x3A, 0x20, 0x7B, 0xFA, 0x0A, 0x20,
        0x20, 0x20, 0x20, 0x22, 0x73, 0x73, 0x69, 0x64, 0x22, 0x3A, 0x20, 0x22, 0x6C, 0x75, 0x6E, 0x61,
        0x77, 0x61, 0x6B, 0x65, 0x22, 0x2C, 0xFA, 0x0A, 0x20, 0x20, 0x20, 0x20, 0x22, 0x70, 0x61, 0x73,
        0x73, 0x77, 0x6F, 0x72, 0x64, 0x22, 0x3A, 0x20, 0x22, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
        0x38, 0x22, 0xFA, 0x0A, 0x20, 0x20, 0x7D, 0xFA, 0x0A, 0x7D, 0x52, 0x13, 0x0A, 0x0A
    };
    
    // 期望的解码数据前几个字节
    const uint8_t expected_start[] = {0x01, 0x7B, 0x0A};
    
    uint8_t recv_buf6[1024];
    uint8_t output_buf6[512];
    struct single_parse_t parser_struct6;
    single_parse_handle_t parser = test_parser_create(1024, recv_buf6, output_buf6, &parser_struct6);
    TEST_ASSERT(parser != NULL, "parser creation should succeed");
    
    size_t out_len = 0;
    const uint8_t* out_buf = NULL;
    
    // 逐字节解析
    for (size_t i = 0; i < sizeof(real_frame); i++) {
        out_buf = single_parse_unpack(parser, real_frame[i], &out_len);
        if (out_len > 0) {
            break;
        }
    }
    
    TEST_ASSERT(out_len > 0, "real frame should be parsed");
    TEST_ASSERT(out_buf != NULL, "real frame should return valid buffer");
    TEST_ASSERT(out_len >= sizeof(expected_start), "unpacked length should be at least expected start length");
    TEST_ASSERT(memcmp(out_buf, expected_start, sizeof(expected_start)) == 0, "unpacked data should match expected start");
    
    // 验证数据是有效的 JSON（至少包含 "cmd"）
    int found_cmd = 0;
    for (size_t i = 0; i < out_len - 3; i++) {
        if (out_buf[i] == '"' && out_buf[i+1] == 'c' && out_buf[i+2] == 'm' && out_buf[i+3] == 'd') {
            found_cmd = 1;
            break;
        }
    }
    TEST_ASSERT(found_cmd, "unpacked data should contain 'cmd' string");
    
    single_parse_deinit(parser);
    return 0;
}

int main(void) {
    printf("=== Single Parse Test Suite ===\n");
    
    int failed = 0;
    
    failed += test_basic_pack_unpack();
    failed += test_escape_special_bytes();
    failed += test_crc_validation();
    failed += test_empty_data();
    failed += test_large_data();
    failed += test_invalid_frame();
    failed += test_real_frame();
    
    printf("\n=== Test Summary ===\n");
    if (failed == 0) {
        printf("All tests PASSED!\n");
        return 0;
    } else {
        printf("%d test(s) FAILED!\n", failed);
        return 1;
    }
}

