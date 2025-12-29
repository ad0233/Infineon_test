/*
 * SPDX-FileCopyrightText: 2022-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
/* Pre Encrypted HTTPS OTA example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include "my_ota.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_encrypted_img.h"
#include "esp_https_ota.h"
#include "esp_crt_bundle.h"

static const char *TAG = "pre_encrypted_ota_example";

// 进度回调函数指针
static ota_progress_callback_t s_progress_cb = NULL;
static void *s_progress_user_ctx = NULL;
static int s_ota_total_size = 0;

extern const char rsa_private_pem_start[] asm("_binary_rsa_priv_key_pem_start");
extern const char rsa_private_pem_end[]   asm("_binary_rsa_priv_key_pem_end");

__attribute__((unused)) static esp_err_t validate_image_header(esp_app_desc_t *new_app_info)
{
    if (new_app_info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_app_desc_t running_app_info;
    if (esp_ota_get_partition_description(running, &running_app_info) == ESP_OK) {
        ESP_LOGI(TAG, "Running firmware version: %s %ld", running_app_info.version, running->size);
    }

// #ifndef CONFIG_EXAMPLE_SKIP_VERSION_CHECK
//     if (memcmp(new_app_info->version, running_app_info.version, sizeof(new_app_info->version)) == 0) {
//         ESP_LOGW(TAG, "Current running version is the same as a new. We will not continue the update.");
//         return ESP_FAIL;
//     }
// #endif
    return ESP_OK;
}

static esp_err_t _decrypt_cb(decrypt_cb_arg_t *args, void *user_ctx)
{
    if (args == NULL || user_ctx == NULL) {
        ESP_LOGE(TAG, "_decrypt_cb: Invalid argument");
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err;
    pre_enc_decrypt_arg_t pargs = {};
    pargs.data_in = args->data_in;
    pargs.data_in_len = args->data_in_len;
    err = esp_encrypted_img_decrypt_data((esp_decrypt_handle_t *)user_ctx, &pargs);
    if (err != ESP_OK && err != ESP_ERR_NOT_FINISHED) {
        ESP_LOGE(TAG, "Decrypt callback failed %d", err);
        free(pargs.data_out);
        return err;
    }

    static bool is_image_verified = false;
    if (pargs.data_out_len > 0) {
        args->data_out = pargs.data_out;
        args->data_out_len = pargs.data_out_len;
        if (!is_image_verified) {
            is_image_verified = true;
            const int app_desc_offset = sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t);
            // It is unlikely to not have App Descriptor available in first iteration of decrypt callback.
            assert(args->data_out_len >= app_desc_offset + sizeof(esp_app_desc_t));
            esp_app_desc_t *app_info = (esp_app_desc_t *) &args->data_out[app_desc_offset];
            err = validate_image_header(app_info);
            if (err != ESP_OK) {
                free(pargs.data_out);
            }
            return err;
        }
    } else {
        args->data_out_len = 0;
    }

    return ESP_OK;
}


void my_ota_register_progress_callback(ota_progress_callback_t progress_cb, void *user_ctx)
{
    s_progress_cb = progress_cb;
    s_progress_user_ctx = user_ctx;
}

static uint32_t start_time = 0;
static esp_https_ota_handle_t https_ota_handle = NULL;
static esp_decrypt_handle_t decrypt_handle;
int my_ota_begin_v1(const char *url, int ota_size) {
    if (url == NULL) {
        ESP_LOGE(TAG, "OTA URL is NULL");
        return-1;
    }
    if(https_ota_handle != NULL) {
        ESP_LOGE(TAG, "https_ota_handle is not NULL");
        return-1;
    }

    s_ota_total_size = ota_size;

    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 90 * 1000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .keep_alive_enable = true,
        .buffer_size = 1024 * 64,
    };
    esp_decrypt_cfg_t cfg = {0};
    cfg.rsa_priv_key = rsa_private_pem_start;
    cfg.rsa_priv_key_len = rsa_private_pem_end - rsa_private_pem_start;
    decrypt_handle = esp_encrypted_img_decrypt_start(&cfg);
    if (!decrypt_handle) {
        ESP_LOGE(TAG, "OTA upgrade failed");
        return-1;
    }

    esp_https_ota_config_t ota_config = {
        .http_config = &config,
        .decrypt_cb = _decrypt_cb,
        .decrypt_user_ctx = (void *)decrypt_handle,
        .enc_img_header_size = esp_encrypted_img_get_header_size(),
    };

    esp_err_t err = esp_https_ota_begin(&ota_config, &https_ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ESP HTTPS OTA Begin failed %d", err);
        return-1;
    }
    start_time = esp_timer_get_time();

    return 0;
}

int my_ota_flush_v1() {
    if(https_ota_handle == NULL) {
        return-1;
    }
    if (!decrypt_handle) {
        ESP_LOGE(TAG, "decrypt_handle is NULL");
        return-1;
    }
    esp_err_t err = esp_https_ota_perform(https_ota_handle);
    if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
        if (!esp_https_ota_is_complete_data_received(https_ota_handle)) {
            // the OTA image was not completely received and user can customise the response to this situation.
            ESP_LOGE(TAG, "Complete data was not received.");
            return -1;
        } else {
            err = esp_encrypted_img_decrypt_end(decrypt_handle);
            if (err != ESP_OK) {
                return err;
            }
            esp_err_t ota_finish_err = esp_https_ota_finish(https_ota_handle);
            if ((err == ESP_OK) && (ota_finish_err == ESP_OK)) {
                ESP_LOGI(TAG, "ESP_HTTPS_OTA upgrade successful. Rebooting ...");
                vTaskDelay(1000 / portTICK_PERIOD_MS);
                esp_restart();
            } else {
                if (ota_finish_err == ESP_ERR_OTA_VALIDATE_FAILED) {
                    ESP_LOGE(TAG, "Image validation failed, image is corrupted");
                }
                ESP_LOGE(TAG, "ESP_HTTPS_OTA upgrade failed 0x%x", ota_finish_err);
                return err;
            }
        }
        return 0;
    }
    // 调用进度回调
    if (s_progress_cb != NULL) {
        int bytes_read = esp_https_ota_get_image_len_read(https_ota_handle);
        s_progress_cb(bytes_read, s_ota_total_size, s_progress_user_ctx);
    }
    // 每 5 秒打印一次日志
    if (esp_timer_get_time() - start_time > 5000) {
        ESP_LOGI(TAG, "Image bytes read: %d %d", esp_https_ota_get_image_len_read(https_ota_handle), s_ota_total_size);
        start_time = esp_timer_get_time();
    }
    return 0;
}