/*
 * This work is part of the White Rabbit project at FRIB
 *
 * Copyright (C) 2024 FRIB (frib.msu.edu)
 * Author: Genie Jhang <changj@frib.msu.edu>
 *
 * Released according to the GNU GPL, version 2 or any later version.
 */
#include "board.h"
#include "dev/bb_i2c.h"
#include "dev/i2c_eeprom.h"
#include "dev/syscon.h"
#include "dev/endpoint.h"
#include "storage.h"
#include "wrc-debug.h"

static struct i2c_bus i2c_wrc_eeprom;
static struct i2c_eeprom_device wrc_eeprom_dev;

int wrc_board_early_init()
{
	/* EEPROM support */
	bb_i2c_create(&i2c_wrc_eeprom, &pin_sysc_fmc_scl, &pin_sysc_fmc_sda);
	bb_i2c_init(&i2c_wrc_eeprom);

	i2c_eeprom_create( &wrc_eeprom_dev, &i2c_wrc_eeprom, CFG_EEPROM_ADR, 2);
	storage_i2ceeprom_create( &wrc_storage_dev, &wrc_eeprom_dev);

	/*
	 * Mount SDBFS filesystem from storage.
	 */
	storage_mount( &wrc_storage_dev );

	return 0;
}

int wrc_board_init()
{
	/*
	 * MAC address assignment
	 */
	board_dbg("Manually assigned MAC address used.\n");
	uint8_t mac_addr[6] = {0x02, 0x51, 0x79, 0x08, 0x74, 0x97};

	ep_set_mac_addr(&wrc_endpoint_dev, mac_addr);
	ep_pfilter_init_default(&wrc_endpoint_dev);

	return 0;
}

int wrc_board_create_tasks()
{
    return 0;
}
