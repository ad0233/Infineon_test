#ifndef XENSIV_BGT60TRXX_CONF_H
#define XENSIV_BGT60TRXX_CONF_H

/* Auto-generated from official dev-board register dump.
 * Config: BGT60TR13C | 59-63 GHz | BW=4 GHz | 128samp/chirp |
 *         64chirp/frame | 3RX | ADC=1MHz | IF=43dB | 10Hz frame rate
 */
#define XENSIV_BGT60TRXX_CONF_DEVICE                  (XENSIV_DEVICE_BGT60TR13C)
#define XENSIV_BGT60TRXX_CONF_START_FREQ_HZ           (59000000000)
#define XENSIV_BGT60TRXX_CONF_END_FREQ_HZ             (63000000000)
#define XENSIV_BGT60TRXX_CONF_NUM_SAMPLES_PER_CHIRP   (128)
#define XENSIV_BGT60TRXX_CONF_NUM_CHIRPS_PER_FRAME    (64)
#define XENSIV_BGT60TRXX_CONF_NUM_RX_ANTENNAS         (3)
#define XENSIV_BGT60TRXX_CONF_NUM_TX_ANTENNAS         (1)
#define XENSIV_BGT60TRXX_CONF_SAMPLE_RATE             (1000000)
#define XENSIV_BGT60TRXX_CONF_CHIRP_REPETITION_TIME_S (5.0e-04)
#define XENSIV_BGT60TRXX_CONF_FRAME_REPETITION_TIME_S (0.1)
#define XENSIV_BGT60TRXX_CONF_NUM_REGS                (38)

#if defined(XENSIV_BGT60TRXX_CONF_IMPL)
const uint32_t register_list[] = {
    0x011e8270UL,  /* MAIN      0x00  data=0x1e8270 */
    0x03140210UL,  /* ADC0      0x01  data=0x140210  ADC_DIV=80 -> 1MHz */
    0x09e967fdUL,  /* PACR1     0x04  data=0xe967fd */
    0x0b0805b4UL,  /* PACR2     0x05  data=0x0805b4 */
    0x0d1027ffUL,  /* SFCTL     0x06  data=0x1027ff  FIFO_CREF=0x7ff */
    0x0f010f00UL,  /* SADC_CTRL 0x07  data=0x010f00 */
    0x11000000UL,  /* CSP_I_0   0x08  data=0x000000 */
    0x13000000UL,  /* CSP_I_1   0x09  data=0x000000 */
    0x15000000UL,  /* CSP_I_2   0x0A  data=0x000000 */
    0x17000be0UL,  /* CSCI      0x0B  data=0x000be0 */
    0x19000000UL,  /* CSP_D_0   0x0C  data=0x000000 */
    0x1b000000UL,  /* CSP_D_1   0x0D  data=0x000000 */
    0x1d000000UL,  /* CSP_D_2   0x0E  data=0x000000 */
    0x1f000b60UL,  /* CSCDS     0x0F  data=0x000b60 */
    0x2113fc51UL,  /* CS1_U_0   0x10  data=0x13fc51 */
    0x237ff41fUL,  /* CS1_U_1   0x11  data=0x7ff41f */
    0x25705ef7UL,  /* CS1_U_2   0x12  data=0x705ef7  VGA_GAIN=5 (IF 43dB) */
    0x2d000490UL,  /* CS1       0x16  data=0x000490 */
    0x3b000480UL,  /* CS2       0x1D  data=0x000480 */
    0x49000480UL,  /* CS3       0x24  data=0x000480 */
    0x57000480UL,  /* CS4       0x2B  data=0x000480 */
    0x5911be0eUL,  /* CCR0      0x2C  data=0x11be0e */
    0x5b714c0aUL,  /* CCR1      0x2D  data=0x714c0a */
    0x5d03f000UL,  /* CCR2      0x2E  data=0x03f000 */
    0x5f787e1eUL,  /* CCR3      0x2F  data=0x787e1e */
    0x61c191caUL,  /* PLL1_0    0x30  data=0xc191ca  FSU 59GHz start */
    0x63000271UL,  /* PLL1_1    0x31  data=0x000271  RSU */
    0x65000532UL,  /* PLL1_2    0x32  data=0x000532  RTU */
    0x67000080UL,  /* PLL1_3    0x33  data=0x000080  APU */
    0x69000000UL,  /* PLL1_4    0x34  data=0x000000 */
    0x6b000000UL,  /* PLL1_5    0x35  data=0x000000 */
    0x6d000000UL,  /* PLL1_6    0x36  data=0x000000 */
    0x6f261b10UL,  /* PLL1_7    0x37  data=0x261b10 */
    0x7f000100UL,  /* PLL2_7    0x3F  data=0x000100 */
    0x8f000100UL,  /* PLL3_7    0x47  data=0x000100 */
    0x9f000100UL,  /* PLL4_7    0x4F  data=0x000100 */
    0xad000000UL,  /* RFT1      0x56  data=0x000000 */
    0xb7000000UL,  /* SDFT0     0x5B  data=0x000000 */
};
#endif /* XENSIV_BGT60TRXX_CONF_IMPL */

#endif /* XENSIV_BGT60TRXX_CONF_H */
