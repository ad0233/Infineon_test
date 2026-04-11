#include "my_rgb.h"
#include "led_strip.h"
#include "esp_log.h"

#define MY_RGB_GPIO     (20)
#define MY_RGB_NUM_LEDS (1)

static const char *TAG = "my_rgb";
static led_strip_handle_t s_strip = NULL;

int my_rgb_init(void)
{
    led_strip_config_t strip_cfg = {
        .strip_gpio_num = MY_RGB_GPIO,
        .max_leds = MY_RGB_NUM_LEDS,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags.invert_out = false,
    };

    led_strip_rmt_config_t rmt_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .flags.with_dma = false,
    };

    esp_err_t ret = led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &s_strip);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "LED strip init failed: %s", esp_err_to_name(ret));
        return -1;
    }

    led_strip_clear(s_strip);
    ESP_LOGI(TAG, "WS2812 on GPIO%d ready", MY_RGB_GPIO);
    return 0;
}

void my_rgb_set(uint8_t r, uint8_t g, uint8_t b, uint8_t brightness)
{
    if (s_strip == NULL) { return; }

    if (brightness == 0)
    {
        led_strip_clear(s_strip);
        return;
    }

    if (brightness > 100) { brightness = 100; }

    uint8_t ro = (uint8_t)((uint16_t)r * brightness / 100);
    uint8_t go = (uint8_t)((uint16_t)g * brightness / 100);
    uint8_t bo = (uint8_t)((uint16_t)b * brightness / 100);

    led_strip_set_pixel(s_strip, 0, ro, go, bo);
    led_strip_refresh(s_strip);
}
