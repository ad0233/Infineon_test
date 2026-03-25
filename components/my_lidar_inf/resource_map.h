#include "driver/gpio.h"

/* sensor SPI interface */
#define PIN_XENSIV_BGT60TRXX_SPI_SCLK       GPIO_NUM_0 //
#define PIN_XENSIV_BGT60TRXX_SPI_MOSI       GPIO_NUM_1 //
#define PIN_XENSIV_BGT60TRXX_SPI_MISO       GPIO_NUM_3 //
#define PIN_XENSIV_BGT60TRXX_SPI_CSN        GPIO_NUM_5 //
/* sensor interrupt output pin */
#define PIN_XENSIV_BGT60TRXX_IRQ            GPIO_NUM_2 //
/* sensor HW reset pin */
#define PIN_XENSIV_BGT60TRXX_RSTN           GPIO_NUM_13
/* enable 1V8 LDO on radar wingboard*/
#define PIN_XENSIV_BGT60TRXX_LDO_EN         GPIO_NUM_14
#define PIN_LED_RED                         GPIO_NUM_15
#define PIN_LED_GREEN                       GPIO_NUM_16
#define PIN_LED_BLUE                        GPIO_NUM_17