/*
 * This work is part of the White Rabbit project
 *
 * Released according to the GNU GPL, version 2 or any later version.
 *
 * CTS board (KR260 + APV5104 mezzanine), Cortex-R5 firmware.
 * Derived from boards/generic.
 *
 * SFP0 management I2C is owned by the R5 and read through the generic WR
 * path (dev/sfp.c -> sfp_match(), called on link-up).  It is bit-banged
 * over the WR-core SYSCON GPIO -- SCL = pin_sysc_sfp1_scl, SDA =
 * pin_sysc_sfp1_sda, presence = pin_sysc_sfp1_det (see dev/syscon.c) --
 * which the gateware brings out on the wrpc sfp_scl_b/sfp_sda_b pads.
 * On this board those pads are wired to P2_HDIO3_SDA / P2_HDIO4_SCL,
 * soldered straight to the SFP cage: the APV5104 branch buffer is
 * bypassed, so no "SFP I2C ENA0" enable has to be asserted here.
 *
 * The Si5344 trunk clock stays on PS I2C1 (MIO24/25) under Linux; that
 * bus is unrelated to the SFP management I2C the R5 now drives.
 *
 * Persistent storage (SFP database, init script, calibration, MAC) is an
 * external I2C EEPROM (24AA256, 32 KB) on the WR-core FMC-EEPROM I2C,
 * which the gateware brings out on P2_HDIO1_SDA / P2_HDIO2_SCL.  It is
 * handled by the generic I2C storage path (EEPROM_STORAGE=1,
 * FMC_EEPROM_ADR=0x50; see boards/generic/generic-storage.c).
 */
#include "board.h"
#include "wrc-debug.h"
#include "dev/syscon.h"
#include "dev/endpoint.h"
#include "dev/bb_i2c.h"
#include "dev/24aa256.h"
#include "storage.h"
#include "board-decl.h"

/* 24AA256 EUI-48 node address (read from EEPROM offset 0x7F7A, see
 * dev/24aa256.c), on the same FMC-EEPROM I2C bus as sdbfs storage. */
static struct i2c_bus         cts_fmc_i2c;
static struct m24aa256_device cts_mac_eeprom;

static int cts_get_mac(uint8_t *mac)
{
	bb_i2c_create(&cts_fmc_i2c, &pin_sysc_fmc_scl, &pin_sysc_fmc_sda);
	bb_i2c_init(&cts_fmc_i2c);

	if (m24aa256_init(&cts_mac_eeprom, &cts_fmc_i2c, FMC_EEPROM_ADR) < 0)
		return -1;
	if (m24aa256_read_mac(&cts_mac_eeprom, mac) < 0)
		return -1;

	/* reject a blank / missing device */
	if ((mac[0] == 0xff && mac[1] == 0xff && mac[2] == 0xff) ||
	    (mac[0] == 0x00 && mac[1] == 0x00 && mac[2] == 0x00))
		return -1;

	return 0;
}

/* --- board hooks ----------------------------------------------------- */
int wrc_board_early_init(void)
{
	generic_board_storage_init();

	return 0;
}

int wrc_board_init(void)
{
	uint8_t mac_addr[6];

	if (cts_get_mac(mac_addr) < 0) {
		board_dbg("CTS: no MAC in EEPROM, using fallback address\n");
		mac_addr[0] = 0x22;
		mac_addr[1] = 0x33;
		mac_addr[2] = 0x44;
		mac_addr[3] = 0x55;
		mac_addr[4] = 0x66;
		mac_addr[5] = 0x77;
	}
	ep_set_mac_addr(&wrc_endpoint_dev, mac_addr);
	ep_pfilter_init_default(&wrc_endpoint_dev);

	return 0;
}
