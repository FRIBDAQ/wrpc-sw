/*
 * This work is part of the White Rabbit project
 *
 * Released according to the GNU GPL, version 2 or any later version.
 */
#ifndef __BOARD_CONFIG_CTS_H
#define __BOARD_CONFIG_CTS_H
/*
 * CTS board: KR260 (Zynq UltraScale+) carrying the APV5104 mezzanine,
 * WR node firmware running on the Cortex-R5.
 *
 * Derived from boards/generic (16-bit PHY).  The WR core sits behind the
 * PS M_AXI window (see include/board.h, DEV_BASE = 0x80003000).
 *
 * Added on this board and handled in board.c:
 *   - SFP0 management I2C, bit-banged over the WR-core SYSCON GPIO
 *     (dev_i2c_sfp1: sfp1_scl/sfp1_sda), brought out on the wrpc
 *     sfp_scl_b/sfp_sda_b pads and soldered to P2_HDIO3_SDA/P2_HDIO4_SCL.
 *   - 25AA02E48 SPI EEPROM (EUI-48 MAC) on the WR-core bit-bang SPI.
 */

/* Support etherbone: define the address of the etherbone core. */
#define BASE_ETHERBONE_CFG	BASE_AUXWB

/* clk_sys is 62.5 MHz -> 1000 tics per second (g_cntr_period). */
#define TICS_PER_SECOND 1000

/* WR Core system/CPU clock frequency in Hz (clk_sys) */
#define CPU_CLOCK 62500000ULL

/* 16-bit PHY datapath: PCS clocked at 62.5 MHz */
#define NS_PER_CLOCK 16
#define REF_CLOCK_PERIOD_PS 16000
#define REF_CLOCK_FREQ_HZ 62500000

/* Maximum number of simultaneously created sockets */
#define NET_MAX_SOCKETS 12

/* Socket buffer size, determines the max. RX packet size */
#define NET_MAX_SKBUF_SIZE 512

/* spll parameters (16-bit PHY: DMTD clock not divided) */
#define BOARD_DIVIDE_DMTD_CLOCKS 0
#define BOARD_MAX_CHAN_REF 1
#define BOARD_MAX_CHAN_AUX 2
#define BOARD_MAX_PTRACKERS 1
#define BOARD_USE_EVENTS 0

/* One UART at 115200 baud */
#define BOARD_EXTRA_CONSOLES 0
#define CONSOLE_UART_BAUDRATE 115200

/* Use the board-local sdbfs image (boards/cts/sdbfs/) instead of the
 * generic tools/sdbfs one. */

/* Maximum number of records in the sdb filesystem (standard WR set:
 * interconnect + mac-address + wr-init + sfp-database + calibration). */
#define SDBFS_REC 5

/* Storage: external I2C EEPROM (24AA256, 32 KB) on the WR-core FMC-EEPROM
 * I2C, which the gateware brings out on P2_HDIO1_SDA / P2_HDIO2_SCL.
 * EEPROM_STORAGE=1 selects the generic I2C storage path (2-byte address,
 * see boards/generic/generic-storage.c -> generic_board_i2c_storage).
 * I2C address 0x54 (24AA256 A2/A1/A0 = 1/0/0 on this board). */
#define FMC_EEPROM_ADR 0x54
#define EEPROM_STORAGE 1

/* ---------------------------------------------------------------------
 * CTS-specific hardware wiring
 * ------------------------------------------------------------------- */

/* SFP0 management I2C is driven by the R5 over the WR-core SYSCON GPIO
 * (SCL = pin_sysc_sfp1_scl, SDA = pin_sysc_sfp1_sda, det = pin_sysc_sfp1_det;
 * see dev/syscon.c).  The gateware routes the sfp_scl_b/sfp_sda_b pads to
 * P2_HDIO3_SDA/P2_HDIO4_SCL, soldered directly to the SFP cage, so the
 * APV5104 branch buffer-enable ("SFP I2C ENA0") does not need asserting.
 * Reading is done by the generic path -- see boards/cts/board.c. */

#endif /* __BOARD_CONFIG_CTS_H */
