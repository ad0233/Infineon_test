#include "my_ui_assets.h"

#include <stdlib.h>
#include <stdint.h>

#include "esp_heap_caps.h"

extern int fastlz_decompress(const void *input, int length, void *output, int maxout);

uint8_t* _my_ui_load_compressed_binary(const uint8_t *bin, const uint32_t compsize, const uint32_t size)
{
    if (bin == NULL || compsize == 0 || size == 0) {
        return NULL;
    }

    uint8_t *out = (uint8_t *)heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (out == NULL) {
        return NULL;
    }

    int decomp_len = fastlz_decompress(bin, (int)compsize, out, (int)size);
    if (decomp_len != (int)size) {
        free(out);
        return NULL;
    }

    return out;
}


