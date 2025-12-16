#ifndef MY_DOWNLOAD_H
#define MY_DOWNLOAD_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*download_chunk_cb_t)(uint8_t *data, int len, void *user_ctx);
typedef void (*download_progress_cb_t)(int downloaded, int total, void *user_ctx);

typedef struct {
    const char *url;
    int timeout_ms;
    download_chunk_cb_t on_chunk;
    download_progress_cb_t on_progress;
    void *user_ctx;
} my_download_config_t;

typedef struct my_download_impl* my_download_handle_t;

int my_download_init(my_download_handle_t *self_out);
int my_download_begin(my_download_handle_t self, const my_download_config_t *cfg);
int my_download_flush(my_download_handle_t self);
int my_download_abort(my_download_handle_t self);
int my_download_destroy(my_download_handle_t self);

#ifdef __cplusplus
}
#endif

#endif
