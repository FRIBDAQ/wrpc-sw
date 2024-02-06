/*
 * This work is part of the White Rabbit project
 *
 * Copyright (C) 2022 Nikhef (www.Nikhef.nl)
 * Author: Peter Jansweijer <peterj@nikhef.nl> based on work
 * from Tomasz Wlostowski <tomasz.wlostowski@cern.ch>
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "board.h"
#include "wrc.h"
#include "wrc-debug.h"
#include "wrpc.h"
#include "dev/bb_spi.h"
#include "dev/spi_flash.h"
#include "dev/bb_i2c.h"
#include "dev/i2c_eeprom.h"
#include "dev/syscon.h"
#include "dev/endpoint.h"
#include "dev/pps_gen.h"
#include "storage.h"
#include "softpll_ng.h"
#include <wrpc.h>
#include "lib/snmp.h"

static spll_gain_schedule_t spll_main_ocxo_gain_sched;

struct spec7_board board;

static struct gpio_pin pin_pll_cs_n_o        = { &board.gpio_aux, 0 };
static struct gpio_pin pin_pll_mosi_o        = { &board.gpio_aux, 1 };
static struct gpio_pin pin_pll_miso_i        = { &board.gpio_aux, 2 };
static struct gpio_pin pin_pll_sck_o         = { &board.gpio_aux, 3 };
static struct gpio_pin pin_pll_reset_n_o     = { &board.gpio_aux, 4 };
//static struct gpio_pin pin_pll_lock_i        = { &board.gpio_aux, 5 };
//static struct gpio_pin pin_pll_status_i      = { &board.gpio_aux, 6 };
static struct gpio_pin pin_pll_sync_o        = { &board.gpio_aux, 7 };
static struct gpio_pin pin_pll_wr_mode0_o    = { &board.gpio_aux, 8 };
static struct gpio_pin pin_pll_wr_mode1_o    = { &board.gpio_aux, 9 };
static struct gpio_pin pin_pll_clk_sel       = { &board.gpio_aux, 10 };
static struct gpio_pin pin_eeprom_scl        = { &board.gpio_aux, 11 };
static struct gpio_pin pin_eeprom_sda        = { &board.gpio_aux, 12 };
static struct gpio_pin pin_pll_even_odd_n_i  = { &board.gpio_aux, 13 };
static struct gpio_pin pin_pll_sync_done_i   = { &board.gpio_aux, 14 };
static struct gpio_pin pin_aux_scl           = { &board.gpio_aux, 15 }; // la23_p, fmc d23, j1002-10
static struct gpio_pin pin_aux_sda           = { &board.gpio_aux, 16 }; // la06_p, fmc c10, j1002-11

#include "configs/ltc6950_defs.h" 
static struct ltc695x_config ltc6950_base_config =
#include "configs/ltc6950_base_config.h" 
static struct ltc695x_config ltc6950_ext_10mhz_config =
#include "configs/ltc6950_ext_10mhz_config.h" 

#define PLL_EVEN_ODD_TIMEOUT_MS 10000
#define PLL_SYNC_TIMEOUT_MS 4000

timeout_t pll_even_odd_timeout;
timeout_t pll_sync_timeout;

//#define CONFIG_HPSEC_GM
#undef CONFIG_HPSEC_GM

//volatile struct softpll_state softpll;

static void spec7_spll_setup(void)
{

int implement_two_stages = 1; // implement 2-stage ocxo lock later

/* configure a suitable PI gain schedule for the SoftPLL: */
    spll_gain_schedule_t* gs=  &spll_main_ocxo_gain_sched;

/* we start with the default values (Bandwidth 100 Hz) */
    gs->stages[0].kp = -4000;
    gs->stages[0].ki = -100;
    gs->stages[0].lock_samples = 10000;
    gs->stages[0].shift = 12;

/* once it's locked, the loop bandwidth is switched to 15 Hz to filter out WR link added phase noise */
    gs->stages[1].kp = -600;
    gs->stages[1].ki = -2;
    gs->stages[1].lock_samples = 10000;
    gs->stages[1].shift = 12;

    if ( implement_two_stages ) {
        gs->n_stages = 2;   // 2 stages: OCXO
        board_dbg("Oscillator gain schedule: Two stage OCXO setup\n");
        spll_set_gain_schedule( gs );
    } else {
        gs->n_stages = 1;   // 1 stage: Crystek
        board_dbg("Oscillator gain schedule: 1st stage Crystek setup\n");
        spll_set_gain_schedule( gs );
    }
}

// ======================================
// GPIO Control functions
// ======================================

int gpio_control_poll()
{
    static int prev_servo_state = 0;
    static int prev_link_state = 0;
    int link_state = 0;

    uint8_t io_stat;
    uint64_t sec;
    uint32_t nsec;

    extern struct pp_instance ppi_static;
    struct pp_instance *ppi = &ppi_static;
//    struct wr_servo_state *s =
//			&((struct wr_data *)ppi->ext_data)->servo_state;
    int curr_servo_state = ppi->servo->state;

    if (prev_servo_state != WRH_TRACK_PHASE && curr_servo_state == WRH_TRACK_PHASE) {
        shw_pps_gen_get_time(&sec, &nsec);
        board_dbg("TRACK_PHASE: '%s'\n",format_time(sec, TIME_FORMAT_LEGACY));
        io_stat = pca9554_read_reg(&board.gpio_tim_main_board, PCA9554_REG_IN);
        pca9554_write_reg(&board.gpio_tim_main_board, PCA9554_REG_OUT, io_stat | TIM_MAIN_BOARD_LED_0);
    }
    if (prev_servo_state == WRH_TRACK_PHASE && curr_servo_state != WRH_TRACK_PHASE) {
        shw_pps_gen_get_time(&sec, &nsec);
        board_dbg("LOST TRACK_PHASE: '%s'\n",format_time(sec, TIME_FORMAT_LEGACY));
        io_stat = pca9554_read_reg(&board.gpio_tim_main_board, PCA9554_REG_IN);
        pca9554_write_reg(&board.gpio_tim_main_board, PCA9554_REG_OUT, io_stat & ~TIM_MAIN_BOARD_LED_0);
    }

    link_state = ep_link_up( &wrc_endpoint_dev, NULL);
    if (!prev_link_state && link_state) {
        shw_pps_gen_get_time(&sec, &nsec);
        board_dbg("Link up: '%s'\n",format_time(sec, TIME_FORMAT_LEGACY));
        io_stat = pca9554_read_reg(&board.gpio_tim_main_board, PCA9554_REG_IN);
        pca9554_write_reg(&board.gpio_tim_main_board, PCA9554_REG_OUT, io_stat | TIM_MAIN_BOARD_LED_1);
	} else if (prev_link_state && !link_state) {
        shw_pps_gen_get_time(&sec, &nsec);
        board_dbg("Link down: '%s'\n",format_time(sec, TIME_FORMAT_LEGACY));
        io_stat = pca9554_read_reg(&board.gpio_tim_main_board, PCA9554_REG_IN);
        pca9554_write_reg(&board.gpio_tim_main_board, PCA9554_REG_OUT, io_stat & ~TIM_MAIN_BOARD_LED_1);
	}

    prev_servo_state = curr_servo_state;
    prev_link_state = link_state;

    return 0;
}

void gpio_control_init()
{
    int i;
    uint8_t io_stat;
    board_dbg("Initializing GPIO control...\n");
    pca9554_write_reg(&board.gpio_tim_main_board, PCA9554_REG_CONFIG, 0x00);  // Configure all IO as output
    pca9554_write_reg(&board.gpio_tim_main_board, PCA9554_REG_OUT, 0x00);     // LEDs, SEL_GROUP_0/1 and SEL_IRIG_B all '0'

    for( i = 0 ; i < 5; i++ )
        {
        io_stat = pca9554_read_reg(&board.gpio_tim_main_board, PCA9554_REG_OUT);
        pca9554_write_reg(&board.gpio_tim_main_board, PCA9554_REG_OUT, io_stat | TIM_MAIN_BOARD_LED_3);
        timer_delay_ms(100);
        pca9554_write_reg(&board.gpio_tim_main_board, PCA9554_REG_OUT, io_stat & ~TIM_MAIN_BOARD_LED_3);
        timer_delay_ms(100);
    }
}

// ======================================

void board_pre_pll_lock(int wrc_ptp_mode)
{
    int pll_wr_mode;
    int mode_hpsec_gm = 0;

    // Set clock multiplexers (U63, U64) depending on wrc_ptp_mode
    switch(wrc_ptp_mode) {
        case WRC_MODE_GM || WRC_MODE_ABSCAL:
            // Default reference design locks local VCXO to external 10 MHz
            pll_wr_mode = PLL_WR_MODE_SLAVE;
#if defined(CONFIG_HPSEC_GM)
            // When HPSEC is used in GM mode then HPSEC locks to external
            // 10 MHz via LTC6950 and WRC_MODE *must* be free running master.
            // Note: WRC_MODE_GM tries to align the local VCXO with the
            // external 10 MHz but in HPSEC_GM case the VCXO is not used.
            pp_printf("ERROR: HPSEC_GM must use WRC_MODE_MASTER.\n");
#endif
            break;
        case WRC_MODE_MASTER:
            pll_wr_mode = PLL_WR_MODE_MASTER;
#if defined(CONFIG_HPSEC_GM)
            // When HPSEC is used in GM mode then HPSEC locks to external
            // 10 MHz via LTC6950 and WRC_MODE *must* be free running master.
            pll_wr_mode = PLL_WR_MODE_GM;
            mode_hpsec_gm = 1;
#endif
            break;
        default:
            pll_wr_mode = PLL_WR_MODE_SLAVE;
    }

    gen_gpio_out( &pin_pll_wr_mode0_o, (pll_wr_mode & 0x1) ? 1 : 0);
    gen_gpio_out( &pin_pll_wr_mode1_o, (pll_wr_mode & 0x2) ? 1 : 0);
    board_dbg("wr_mode: %d\n", pll_wr_mode);

    // ltc6950 initialization depending on wrc_ptp_mode
    board_dbg("Initialize ltc6950.\n");
    if ((wrc_ptp_mode == WRC_MODE_MASTER) | (mode_hpsec_gm == 1)) {
        // 10 MHZ from TCXO (WRC_MODE_MASTER) on outputs 0, 1, 2
        // or (for HPSEC in WRC_MODE GM) External 10 MHZ In (Bulls-Eye B03/B04) => 125 MHz
        ltc695x_configure(&board.ltc6950_pll, &ltc6950_ext_10mhz_config);
        while ((ltc695x_read(&board.ltc6950_pll,  0x00) & LTC6950_LOCK) == 0);
        board_dbg("ltc6950 locked.\n");
#if defined(CONFIG_HPSEC_GM)
        pll_sync();
#endif
    } else {
        // Forward 125 MHz VCXO_REFCLK at CLK input to outputs 0, 1, 2
        ltc695x_configure(&board.ltc6950_pll, &ltc6950_base_config);
    }

    board_dbg("Select ltc6950 output as clk_sys source.\n");
    /* ltc6950 now initialized so switch clk_sys from free running clk_dmtd to ltc6950 output */
    gen_gpio_out( &pin_pll_clk_sel, 1);
    timer_delay_ms(10);
}

int pll_sync(void)
{
    // Used in HPSEC Grand Master mode where external 10MHz generates 125MHz.
    // 125MHz is not an integer multiple of 10MHz so it has two lock modes: even/odd.
    // The generated 125MHz must be even/odd alligned with the external 10MHz/1PPS.
    
    tmo_init(&pll_even_odd_timeout, PLL_EVEN_ODD_TIMEOUT_MS);
    while (gen_gpio_in( &pin_pll_even_odd_n_i ) == 0) {
        // Reset the PLL (RES6950 clears itself)
        board_dbg("Reset ltc6950...\n");
        ltc695x_write( &board.ltc6950_pll, 0x03, 4);
        timer_delay_ms(1);
        ltc695x_configure(&board.ltc6950_pll, &ltc6950_ext_10mhz_config);
        while ((ltc695x_read(&board.ltc6950_pll,  0x00) & LTC6950_LOCK) == 0);
        timer_delay_ms(1000);  // wait for next PPS
        if ( tmo_expired(&pll_even_odd_timeout)) {
            pp_printf("TIMEOUT: External 10MHz/1PPS lock to \"even\" 125MHz clock cycle.\n");
            return 0;
        }
    }
    board_dbg("HPSEC_GM mode: External 10MHz/1PPS lock achieved on \"even\" 125MHz clock cycle\n");

    // Trigger a clk_ref_125m to clk_ref_62m5 divider synchronisation
    gen_gpio_out( &pin_pll_sync_o, 1);
    gen_gpio_out( &pin_pll_sync_o, 0);

    tmo_init(&pll_sync_timeout, PLL_SYNC_TIMEOUT_MS);
    // Wait for sync sequence done
    while (gen_gpio_in( &pin_pll_sync_done_i ) == 0) {
        if ( tmo_expired(&pll_sync_timeout)) {
            pp_printf("TIMEOUT: clk_ref_125m to clk_ref_62m5 divider synchronization.\n");
            return 0;
        }
    }
    board_dbg("HPSEC_GM mode: clk_ref_125m to clk_ref_62m5 divider synchronization done\n");

    phy_calibration_init();
    while (!phy_calibration_done()) {
        phy_calibration_poll();
    }

    return 1;
}

#if defined(CONFIG_SNMP) && defined(SNMP_SET)
/* Functions and struct used by the SNMP protocol to control the timing output */
const struct snmp_oid oid_array_wrpcBoardSpecificGroup[] = {
	OID_FIELD_VAR(   oid_wrpcSelGroup0,  get_select_group, set_select_group, ASN_INTEGER,   &(board.gpio_tim_main_board)),
	OID_FIELD_VAR(   oid_wrpcSelGroup1,  get_select_group, set_select_group, ASN_INTEGER,   &(board.gpio_tim_main_board)),
	{ 0, }
};

int set_select_group(uint8_t *buf, struct snmp_oid *obj){
    static const int sel_group_offset = 3;
	uint8_t io_stat;
	uint8_t len = buf[1];
	uint8_t *oid_data = buf + 2;
	uint8_t sel_group = *(buf - 2) + sel_group_offset;  // add offset to get the appropriate value from board.h
	uint8_t sel_group_reg = WBGEN2_GEN_MASK(sel_group, 1);
	uint8_t asn_incoming = buf[0];
	uint8_t asn_expected = obj->asn;
	uint32_t tmp_u32;

	if (asn_incoming != asn_expected) { /* wrong data type */
		snmp_verbose("%s: wrong asn 0x%02x, expected 0x%02x\n",
			     __func__, asn_incoming, asn_expected);
		return -SNMP_ERR_BADVALUE;
	}
	
	io_stat = pca9554_read_reg(obj->p, PCA9554_REG_OUT);

	memcpy(&tmp_u32, oid_data, len);
	tmp_u32 = ntohl(tmp_u32);
	/* move data when shorter than 4 bytes */
	tmp_u32 = tmp_u32 >> ((4 - len) * 8);

	if(tmp_u32){		
		pca9554_write_reg(obj->p, PCA9554_REG_OUT, io_stat | sel_group_reg);
	}
	else{
		pca9554_write_reg(obj->p, PCA9554_REG_OUT, io_stat & ~sel_group_reg);
	}

	return len + 2;
}

int get_select_group(uint8_t *buf, struct snmp_oid *obj){
    static const int sel_group_offset = 3;
	uint8_t *oid_data = buf + 2;
	uint8_t *len = &buf[1];
	uint32_t on = htonl(1);
	uint32_t off = htonl(0);
	uint8_t sel_group = *(buf - 2) + sel_group_offset; // add offset to get the appropriate value from board.h
	uint8_t reg_stat = pca9554_read_reg(obj->p, PCA9554_REG_OUT);
	uint8_t sel_group_reg = WBGEN2_GEN_MASK(sel_group, 1);
	uint8_t sel_group_status = reg_stat & sel_group_reg;

	*len = sizeof(uint32_t);
	buf[0] = obj->asn;
	if(sel_group_status){
		memcpy((char*)oid_data, &on, *len);
	}
	else{
		memcpy((char*)oid_data, &off, *len);
	}

	return *len + 2;
}

#endif

int spec7_init()
{
    /* most of the I/Os of the slow peripherals (i2c, spi) are bitbanged. First, let's
       initialize the GPIO controller they're connected to */
    wb_gpio_create( &board.gpio_aux, BASE_GPIO );

    // Use free running dmtd clock for bootstrapping
    gen_gpio_out( &pin_pll_clk_sel, 0);
    board_dbg("Use free running dmtd clock for bootstrapping.\n");

    // PLL reset (although not connected at top level) de-asserted
    gen_gpio_out( &pin_pll_reset_n_o, 1);

    // Do not (yet) sync the clk_ref_125m to clk_ref_62m5 divider
    gen_gpio_out( &pin_pll_sync_o, 0);

    /* initialize the SPI bus for the SPEC7 PLL (LTC6950 U66) */
    bb_spi_create( &board.spi_ltc6950,
        &pin_pll_cs_n_o,
        &pin_pll_mosi_o,
        &pin_pll_miso_i,
        &pin_pll_sck_o,
        100 );

    /* Setup the SoftPLL for the OCXO we have */
    spec7_spll_setup();
    ltc695x_init(&board.ltc6950_pll, &board.spi_ltc6950);

    // Reset the PLL (RES6950 clears itself)
    ltc695x_write( &board.ltc6950_pll, 0x03, 4);
    timer_delay_ms(1);

    int id = ltc695x_read( &board.ltc6950_pll, 0x16 );
    if( id != 0x65 )
    {
        board_dbg("detect LTC6950: ID %x should be %x\n", id, 0x65 );
    } else {
        board_pre_pll_lock(WRC_MODE_SLAVE);
    }

    return 0;
}

struct i2c_bus            dev_i2c_eeprom;
struct i2c_bus            dev_i2c_aux;
struct i2c_eeprom_device  wrc_eeprom_dev;
struct i2c_eeprom_device  wrc_uid_dev;

int wrc_board_early_init()
{
    /* Initialize SPEC7 clocking */
    spec7_init();

    /* create and initialize eeprom I2C bus */
    bb_i2c_create(&dev_i2c_eeprom,
         &pin_eeprom_scl,
         &pin_eeprom_sda );
    bb_i2c_init(&dev_i2c_eeprom);

    /* create and initialize auxiliary I2C bus */
    bb_i2c_create(&dev_i2c_aux,
         &pin_aux_scl,
         &pin_aux_sda );
    bb_i2c_init(&dev_i2c_aux);

    i2c_eeprom_create(&wrc_eeprom_dev, &dev_i2c_eeprom, 0x50, 2);
    storage_i2ceeprom_create( &wrc_storage_dev, &wrc_eeprom_dev );

    /*
     * Mount SDBFS filesystem from storage.
     */
    storage_mount( &wrc_storage_dev );

    /* create and initialize UID eeprom I2C bus */
    i2c_eeprom_create(&wrc_uid_dev, &dev_i2c_eeprom, UID_EEPROM_ADR, 1);

    /* Initialize I2C bus multiplexer */
    pca9554_gpio_init( &board.gpio_tim_main_board, &dev_i2c_aux, PCA9554_ADR );

    return 0;
}

int wrc_board_init()
{
	uint8_t mac_addr[6];
	/* Read MAC addr from Unique-ID, IC D12, 24AA025E48 */
	i2c_eeprom_read(&wrc_uid_dev, UID_OFFSET , mac_addr, sizeof(mac_addr));
	board_dbg("MAC addr: %x:%x:%x:%x:%x:%x\n",mac_addr[0],mac_addr[1],mac_addr[2],mac_addr[3],mac_addr[4],mac_addr[5]);
	ep_set_mac_addr(&wrc_endpoint_dev, mac_addr);
	ep_pfilter_init_default(&wrc_endpoint_dev);

    return 0;
}

int wrc_board_create_tasks()
{
    wrc_task_create( "phy-cal", phy_calibration_init, phy_calibration_poll );
    wrc_task_create( "pgpio_control", gpio_control_init, gpio_control_poll );

    return 0;
}
