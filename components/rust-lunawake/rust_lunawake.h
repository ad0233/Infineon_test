#pragma once
#include <stdint.h>
#include <stddef.h>
#include <assert.h>

// ESP32 是 32 位架构，约定类型大小为 4 字节
#define RUST_FFI_PTR_SIZE 4

// 静态检查：确保 C 的类型大小符合约定
_Static_assert(sizeof(size_t) == RUST_FFI_PTR_SIZE, "size_t must be 4 bytes");
_Static_assert(sizeof(intptr_t) == RUST_FFI_PTR_SIZE, "intptr_t must be 4 bytes");
_Static_assert(sizeof(void*) == RUST_FFI_PTR_SIZE, "pointer must be 4 bytes");

#ifdef __cplusplus
extern "C" {
#endif

void rust_lib_init();

/**
 * @brief Opaque handle to single parse instance
 */
typedef struct single_parse_t* single_parse_handle_t;

/**
 * @brief Create a new single parse instance
 * @param max_size Maximum frame size for parsing
 * @return Parser handle, NULL on failure
 */
single_parse_handle_t rust_single_parse_new(size_t max_size);

/**
 * @brief Free a single parse instance
 * @param parser Parser handle returned from single_parse_new
 */
void rust_single_parse_free(single_parse_handle_t parser);

/**
 * @brief Unpack a single byte (incremental parsing)
 * @param parser Parser handle
 * @param byte Input byte to parse
 * @param out_len Pointer to store output length (0 if no complete frame)
 * @return Pointer to decoded data if frame complete, NULL otherwise
 * @note The returned pointer is valid until the next call to single_parse_unpack
 */
const uint8_t* rust_single_parse_unpack(single_parse_handle_t parser, uint8_t byte, size_t* out_len);

/**
 * @brief Pack data into a frame
 * @param data Input data to encode
 * @param data_len Length of input data
 * @param out_buf Output buffer (provided by caller)
 * @param out_buf_len Size of output buffer
 * @return Actual encoded length on success, -1 on failure
 */
int rust_single_parse_pack(const uint8_t* data, size_t data_len, uint8_t* out_buf, size_t out_buf_len);

#ifdef __cplusplus
}
#endif