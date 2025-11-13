#include "ui.h"
#include "fastlz.h"
#include <stdio.h>
#include <stdlib.h>

uint8_t* _ui_load_binary(char* fname, const uint32_t size)
{
    lv_fs_file_t f;
    lv_fs_res_t res;
    uint32_t read_num;
    uint8_t* buf;
    
    res = lv_fs_open(&f, fname, LV_FS_MODE_RD);
    if (res != LV_FS_RES_OK) {
        return NULL;
    }
    
    buf = (uint8_t*)malloc(sizeof(uint8_t) * size);
    if (buf == NULL) {
        lv_fs_close(&f);
        return NULL;
    }
    
    res = lv_fs_read(&f, buf, size, &read_num);
    if (res != LV_FS_RES_OK || read_num != size)
    {
        free(buf);
        lv_fs_close(&f);
        return NULL;
    }
    
    lv_fs_close(&f);
    return buf;
}


uint8_t* _ui_load_compressed_binary(char* fname, const uint32_t compsize, const uint32_t size )
{
    lv_fs_file_t f;
    lv_fs_res_t res;
    uint32_t read_num = 0;

    // Open compressed binary
    res = lv_fs_open(&f, fname, LV_FS_MODE_RD);
    if (res != LV_FS_RES_OK) {
        return NULL;
    }

    // Read compressed data
    uint8_t* cbuf = (uint8_t*)malloc(sizeof(uint8_t) * compsize);
    if (cbuf == NULL) {
        lv_fs_close(&f);
        return NULL;
    }

    res = lv_fs_read(&f, cbuf, compsize, &read_num);
    if (res != LV_FS_RES_OK || read_num != compsize) {
        free(cbuf);
        lv_fs_close(&f);
        return NULL;
    }

    // Allocate output buffer and decompress
    uint8_t* out = (uint8_t*)malloc(sizeof(uint8_t) * size);
    if (out == NULL) {
        free(cbuf);
        lv_fs_close(&f);
        return NULL;
    }

    int decomp_len = fastlz_decompress(cbuf, (int)compsize, out, (int)size);
    free(cbuf);
    lv_fs_close(&f);

    if (decomp_len != (int)size) {
        free(out);
        return NULL;
    }

    return out;
}
