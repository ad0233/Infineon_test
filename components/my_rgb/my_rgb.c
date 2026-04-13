#include "my_rgb.h"
#include "led_strip.h"
#include "esp_log.h"
#include "driver/usb_serial_jtag.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>

/* 同时监听 UART0 (getchar) + USB-JTAG，哪个有数据读哪个 */
#define LISTEN_UART 1
#define LISTEN_USBJTAG 1

#define MY_RGB_GPIO     (20)
#define MY_RGB_NUM_LEDS (1)

static const char *TAG = "my_rgb";
static led_strip_handle_t s_strip = NULL;

static uint8_t s_r = 255, s_g = 255, s_b = 255;
static uint8_t s_brightness = 100;
static bool s_enabled = false;

static void rgb_apply(void)
{
    if (s_strip == NULL) { return; }

    if (!s_enabled)
    {
        led_strip_clear(s_strip);
        return;
    }

    uint8_t ro = (uint8_t)((uint16_t)s_r * s_brightness / 100);
    uint8_t go = (uint8_t)((uint16_t)s_g * s_brightness / 100);
    uint8_t bo = (uint8_t)((uint16_t)s_b * s_brightness / 100);

    led_strip_set_pixel(s_strip, 0, ro, go, bo);
    led_strip_refresh(s_strip);
}

/* 解析 "rgb R G B BR" 格式 */
static void parse_and_apply(const char *line)
{
    int r = 0, g = 0, b = 0, br = 0;
    if (sscanf(line, "rgb %d %d %d %d", &r, &g, &b, &br) == 4)
    {
        if (r < 0) r = 0; else if (r > 255) r = 255;
        if (g < 0) g = 0; else if (g > 255) g = 255;
        if (b < 0) b = 0; else if (b > 255) b = 255;
        if (br < 0) br = 0; else if (br > 100) br = 100;
        my_rgb_set_color((uint8_t)r, (uint8_t)g, (uint8_t)b, (uint8_t)br);
    }
}

/* 独立任务：从 USB-JTAG 读取 rgb 命令（不依赖 CLI） */
static void rgb_cmd_task(void *arg)
{
    (void)arg;

#if LISTEN_USBJTAG
    usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    esp_err_t ret = usb_serial_jtag_driver_install(&cfg);
    ESP_LOGI(TAG, "USB-JTAG cmd listener install: %s", esp_err_to_name(ret));
#endif

#if LISTEN_UART
    setvbuf(stdin, NULL, _IONBF, 0);
#endif

    static char buf[64];
    size_t pos = 0;

    ESP_LOGI(TAG, "rgb_cmd_task started");

    for (;;)
    {
        uint8_t byte = 0;
        int n = 0;

#if LISTEN_USBJTAG
        n = usb_serial_jtag_read_bytes(&byte, 1, 0);
#endif

#if LISTEN_UART
        if (n <= 0)
        {
            int c = getchar();
            if (c != EOF) { byte = (uint8_t)c; n = 1; }
        }
#endif

        if (n <= 0)
        {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        if (byte == '\r' || byte == '\n')
        {
            if (pos > 0)
            {
                buf[pos] = '\0';
                ESP_LOGI(TAG, "line: [%s]", buf);
                parse_and_apply(buf);
                pos = 0;
            }
        }
        else if (pos < sizeof(buf) - 1)
        {
            buf[pos++] = (char)byte;
        }
        else
        {
            pos = 0;
        }
    }
}

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

    /* 启动命令监听任务（独立于 CLI） */
    xTaskCreate(rgb_cmd_task, "rgb_cmd", 3072, NULL, 2, NULL);
    return 0;
}

void my_rgb_set_color(uint8_t r, uint8_t g, uint8_t b, uint8_t brightness)
{
    if (brightness > 100) { brightness = 100; }
    s_r = r;
    s_g = g;
    s_b = b;
    s_brightness = brightness;
    rgb_apply();
}

void my_rgb_enable(bool on)
{
    s_enabled = on;
    rgb_apply();
}
