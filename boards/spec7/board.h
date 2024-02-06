/*
 * This work is part of the White Rabbit project
 *
 * Released according to the GNU GPL, version 2 or any later version.
 */
#ifndef __BOARD_SPEC7_H
#define __BOARD_SPEC7_H

#include "dev/gpio.h"
#include "dev/ltc695x.h"
#include "dev/pca9554.h"
#include "lib/snmp.h"
/*
 * This is meant to be automatically included by the Makefile,
 * when wrpc-sw is build for wrc (node) -- as opposed to wrs (switch)
 */

/* SPEC7 WB bus behind wr-cores Aux WB bus */
#define BASE_GPIO            (BASE_AUXWB + 0x000)

/* Board-specific parameters */
#define TICS_PER_SECOND 1000

/* WR Core system/CPU clock frequency in Hz (clk_sys) */
#define CPU_CLOCK 62500000ULL

/* WR Reference clock period (picoseconds) and frequency (Hz) */
/* GENERIC_PHY_16BIT */
#define NS_PER_CLOCK 16
#define REF_CLOCK_PERIOD_PS 16000
#define REF_CLOCK_FREQ_HZ 62500000

/* Accomodate 20 bit MAX5719A */
#define BOARD_SPLL_DAC_BITS 20
#define BOARD_SPLL_DIV_BITS 4

/* Maximum number of simultaneously created sockets */
#define NET_MAX_SOCKETS 12

/* Socket buffer size, determines the max. RX packet size */
#define NET_MAX_SKBUF_SIZE 512

/* spll parameter that are board-specific */
// SPEC7 has GENERIC_PHY_16BIT
#define BOARD_DIVIDE_DMTD_CLOCKS      0

/* Number of reference channels (RX clocks) */
#define BOARD_MAX_CHAN_REF            1
/* Number of external pll that can be disciplined */
#define BOARD_MAX_CHAN_AUX            2
/* Should be the same as reference channels */
#define BOARD_MAX_PTRACKERS           1

/* Events are not used on this platform */
#define BOARD_USE_EVENTS 0

/* Use one uart at 115200 baud. May add extra uart. */
#define BOARD_CONSOLE_DEVICES 1
#define CONSOLE_UART_BAUDRATE 115200

/* Maximum number of files in the sdb filesystem.
   Need at least 4: ., sfp database, init script and calibration
   MAC address could also be written on sdbfs. */
#define SDBFS_REC 5

/* Specific to this board (see board.c) */
/* I2C address of the storage eeprom */
#define FMC_EEPROM_ADR 0x50

/* I2C address of the Unique ID EEPROM and Unique ID address */
#define UID_EEPROM_ADR 0x51
#define UID_OFFSET 0xfa
/* macro for extended SNMP support on the SPEC7 board */
#define CONFIG_SNMP_BOARD_SPECIFIC
/* I2C address of the I2C multiplexer */
#define PCA9554_ADR 0x23

// Timing main board LEDs and other IO on I2C GPIO
#define TIM_MAIN_BOARD_LED_0         WBGEN2_GEN_MASK(0, 1)
#define TIM_MAIN_BOARD_LED_1         WBGEN2_GEN_MASK(1, 1)
#define TIM_MAIN_BOARD_LED_2         WBGEN2_GEN_MASK(2, 1)
#define TIM_MAIN_BOARD_LED_3         WBGEN2_GEN_MASK(3, 1)
#define TIM_MAIN_BOARD_SEL_GROUP_0   WBGEN2_GEN_MASK(4, 1)
#define TIM_MAIN_BOARD_SEL_GROUP_1   WBGEN2_GEN_MASK(5, 1)
#define TIM_MAIN_BOARD_SEL_IRIG_B    WBGEN2_GEN_MASK(6, 1)

#define SDBFS_REC 5

// PLL WR_MODE options:
#define PLL_WR_MODE_MASTER 1
#define PLL_WR_MODE_SLAVE 2
#define PLL_WR_MODE_GM 3

struct spec7_board
{
    struct gpio_device gpio_aux;
    struct spi_bus spi_ltc6950;
    struct ltc695x_device ltc6950_pll;
    struct pca9554_gpio_device gpio_tim_main_board;
    int pll_wr_mode;
};

extern struct spec7_board board;

void gpio_control_init(void);
int gpio_control_poll(void);
void board_pre_pll_lock(int pll_wr_mode);
int  spec7_init(void);

extern int phy_calibration_poll(void);
extern void phy_calibration_init(void);
extern void phy_calibration_disable(void);
extern int phy_calibration_done(void);

void sdb_find_devices(void);
void sdb_print_devices(void);

#if defined(CONFIG_SNMP) && defined(SNMP_SET)
int set_select_group(uint8_t *buf, struct snmp_oid *obj);
int get_select_group(uint8_t *buf, struct snmp_oid *obj);
/* wrpcSelGroup entries */
static const uint8_t oid_wrpcSelGroup0[] =           {1,0};
static const uint8_t oid_wrpcSelGroup1[] =           {2,0};
/* oid_wprcBoardSpecific*/
static const uint8_t oid_wrpcBoardSpecificGroup[] =    {0x2B,6,1,4,1,96,101,1,13};
/* wrpcBoardSpecificGroup array */
extern const struct snmp_oid oid_array_wrpcBoardSpecificGroup[];
#endif

#endif /* __BOARD_SPEC7_H */
