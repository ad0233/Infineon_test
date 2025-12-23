#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void single_parse_lib_init(void);

/**
 * @brief Single parse instance structure
 * @note This structure must be allocated by the caller
 */
struct single_parse_t {
    int recv_flag;
    int ctrl_flag;
    uint8_t* rec_buffer;      // Provided by caller
    uint8_t* output_buffer;   // Provided by caller
    size_t max_size;
    size_t offset;
    uint8_t last_byte;
    size_t error_count;
};

/**
 * @brief Handle to single parse instance
 */
typedef struct single_parse_t* single_parse_handle_t;

/**
 * @brief Initialize a single parse instance
 * @param parser Parser structure provided by caller (must be allocated externally)
 * @param rec_buffer Receive buffer provided by caller (size: max_size)
 * @param output_buffer Output buffer provided by caller (size: max_size / 2)
 * @param max_size Maximum frame size for parsing
 * @return 0 on success, -1 on failure
 * @note All buffers and structure must remain valid until single_parse_deinit is called
 */
int single_parse_init(single_parse_handle_t parser, uint8_t* rec_buffer, uint8_t* output_buffer, size_t max_size);

/**
 * @brief Deinitialize a single parse instance
 * @param parser Parser handle
 * @note This only resets the parser state, does not free any memory
 */
void single_parse_deinit(single_parse_handle_t parser);

/**
 * @brief Unpack a single byte (incremental parsing)
 * @param parser Parser handle
 * @param byte Input byte to parse
 * @param out_len Pointer to store output length (0 if no complete frame)
 * @return Pointer to decoded data if frame complete, NULL otherwise
 * @note The returned pointer is valid until the next call to single_parse_unpack
 */
const uint8_t* single_parse_unpack(single_parse_handle_t parser, uint8_t byte, size_t* out_len);

/**
 * @brief Pack data into a frame
 * @param data Input data to encode
 * @param data_len Length of input data
 * @param out_buf Output buffer (provided by caller)
 * @param out_buf_len Size of output buffer
 * @return Actual encoded length on success, -1 on failure
 */
int single_parse_pack(const uint8_t* data, size_t data_len, uint8_t* out_buf, size_t out_buf_len);

#ifdef __cplusplus
}
#endif

