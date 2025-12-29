#include "my_download.h"

#include <stdlib.h>
#include <string.h>
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_crt_bundle.h"

static const char *TAG = "MY_DOWNLOAD";

struct my_download_impl {
    esp_http_client_handle_t client;
    my_download_config_t cfg;
    int downloaded;
    int total;
    uint8_t buf[8192];
};

static esp_err_t http_event_cb(esp_http_client_event_t *evt)
{
    my_download_handle_t self = (my_download_handle_t)evt->user_data;
    if (!self) return ESP_OK;

    if (evt->event_id == HTTP_EVENT_ON_HEADER) {
        if (evt->header_key &&
            strcmp(evt->header_key, "Content-Length") == 0) {

            self->total = atoi(evt->header_value);
            ESP_LOGI(TAG, "Total size = %d", self->total);
        }
    }
    return ESP_OK;
}

int my_download_init(my_download_handle_t *self_out)
{
    if (!self_out) return -1;

    struct my_download_impl *self = calloc(1, sizeof(*self));
    if (!self) return -1;

    self->client = NULL;
    self->total = -1;

    *self_out = self;
    return 0;
}

int my_download_begin(my_download_handle_t self, const my_download_config_t *cfg)
{
    if (!self || !cfg || !cfg->url) return -1;

    self->cfg = *cfg;
    self->downloaded = 0;
    self->total = -1;

    esp_http_client_config_t config = {
        .url = cfg->url,
        .timeout_ms = cfg->timeout_ms ? cfg->timeout_ms : 90 * 1000,
        .event_handler = http_event_cb,
        .user_data = self,                 
        .crt_bundle_attach = esp_crt_bundle_attach,
        .keep_alive_enable = true,
        .buffer_size = 16 * 1024,
    };

    self->client = esp_http_client_init(&config);
    if (!self->client) return -1;

    if (esp_http_client_open(self->client, 0) != ESP_OK) {
        return -1;
    }

    return 0;
}

int my_download_flush(my_download_handle_t self)
{
    if (!self || !self->client) return -1;

    int len = esp_http_client_read(
        self->client,
        (char *)self->buf,
        sizeof(self->buf)
    );

    if (len < 0) return -1;

    if (len == 0) {
        esp_http_client_close(self->client);
        esp_http_client_cleanup(self->client);
        self->client = NULL;
        return 1;   // 完成
    }

    if (self->cfg.on_chunk)
        self->cfg.on_chunk(self->buf, len, self->cfg.user_ctx);

    self->downloaded += len;

    if (self->cfg.on_progress && self->total > 0)
        self->cfg.on_progress(self->downloaded, self->total, self->cfg.user_ctx);

    return 0;
}

int my_download_abort(my_download_handle_t self)
{
    if (!self) return -1;

    if (self->client) {
        esp_http_client_close(self->client);
        esp_http_client_cleanup(self->client);
        self->client = NULL;
    }
    return 0;
}

int my_download_destroy(my_download_handle_t self)
{
    if (!self) return -1;

    my_download_abort(self);
    free(self);
    return 0;
}
