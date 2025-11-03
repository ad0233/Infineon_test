#include "fsm_main.h"

#include "esp_log.h"

#include "my_nvs.h"

#define TAG "fsm_main"

uint8_t fm_has_wifi_config(void) {
    const struct device_config *cfg = my_nvs_get_config();
    if (cfg->wifi_enable) {
        return 0;
    }
    return 1;
}

void fsm_main_do_init(void *arg) {
    ESP_LOGI(TAG, "init");
}