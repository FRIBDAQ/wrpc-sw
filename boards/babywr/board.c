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
#include "dev/bb_spi.h"
#include "dev/spi_flash.h"
#include "dev/bb_i2c.h"
#include "dev/i2c_eeprom.h"
#include "dev/syscon.h"
#include "dev/endpoint.h"
#include "storage.h"
#include "softpll_ng.h"
#include "hw/sit5359_regs.h"

struct babywr_board board;

static spll_gain_schedule_t spll_main_ocxo_gain_sched;

#define SIT5359_PIN_SCL 0
#define SIT5359_PIN_SDA 1

// I2C_ADDR (A0=0) => 1100010 (WR:0xC4, RD:0xC5 or 0x62+r/w)
// I2C_ADDR (A0=1) => 1101010 (WR:0xD4, RD:0xD5 or 0x6A+r/w)
#define SIT5359_I2C_ADDR_A0_0 0x62
#define SIT5359_I2C_ADDR_A0_1 0x6A

#define DAC_HALF_SCALE (1<<(BOARD_SPLL_DAC_BITS - 1))
#define DAC_FULL_SCALE (1<<(BOARD_SPLL_DAC_BITS))

static void sit5359_gpio_out(const struct gpio_pin *pin, int value)
{
    struct wr_sit5359_interface_device* dev = ( struct wr_sit5359_interface_device* ) pin->device->priv;

    uint32_t mask = (pin->pin == SIT5359_PIN_SCL ? SIT5359_GPCR_SCL : SIT5359_GPCR_SDA );
    uint32_t reg = (value ? SIT5359_REG_GPSR : SIT5359_REG_GPCR );

    writel( mask, dev->base_addr + reg );
}

static void sit5359_gpio_set_dir(const struct gpio_pin *pin, int dir)
{
    sit5359_gpio_out(pin, !dir);
}

static int sit5359_gpio_in(const struct gpio_pin *pin)
{
    struct wr_sit5359_interface_device* dev = ( struct wr_sit5359_interface_device* ) pin->device->priv;

    uint32_t gpsr = readl( dev->base_addr + SIT5359_REG_GPSR );

    if ( pin->pin == SIT5359_PIN_SCL )
        return (gpsr & SIT5359_GPSR_SCL ? 1 : 0);
    else
        return (gpsr & SIT5359_GPSR_SDA ? 1 : 0);
}

static void wr_sit5359_interface_init( struct wr_sit5359_interface_device *dev, uint32_t base_addr, uint8_t i2c_addr )
{
    int attempts = 0;

    dev->base_addr = (void *) base_addr;
    dev->gpio_i2c.priv = (void *) dev;
    dev->gpio_i2c.read_pin = sit5359_gpio_in;
    dev->gpio_i2c.set_dir = sit5359_gpio_set_dir;
    dev->gpio_i2c.set_out = sit5359_gpio_out;
    dev->i2c_addr = i2c_addr;
    dev->pin_scl.device = &dev->gpio_i2c;
    dev->pin_scl.pin = SIT5359_PIN_SCL;
    dev->pin_sda.device = &dev->gpio_i2c;
    dev->pin_sda.pin = SIT5359_PIN_SDA;
    bb_i2c_create( &dev->master, &dev->pin_scl, &dev->pin_sda );
    bb_i2c_init(&dev->master);

    // Avoid failing initialization due to any pending I2C transfer from the past.
    // Probe the SiTime oscillators to align their I2C state machine.
    // Wait for a proper answer...
    while (!bb_i2c_devprobe(&dev->master, i2c_addr)) {
        attempts++;
        if (attempts > 10) {
            board_dbg("SiT5359 device not found.\n");
            break;
        }
    }
}

uint16_t temp_poll(void)
{    
    uint32_t* sysmon_ptr = (uint32_t*)BASE_SYSMON;
    uint32_t temp_32 = *sysmon_ptr;
    static const uint16_t multiply = 509;
    static const uint16_t subtract = 280;
    static const uint16_t shift = 10;
    static const uint32_t mask = 0x0000FFC0;

    uint32_t temp_16 = temp_32 & mask; // Keep the low order byte
    uint32_t adc_code = temp_16 >> 6;  // Keep the 10 MSB of that byte
    uint32_t intermediate_0 = adc_code * multiply;
    intermediate_0 += 1 << (shift - 1);
    uint16_t intermediate_1 = (uint16_t)(intermediate_0 >> shift);
    uint16_t temp = intermediate_1 - subtract;
    
    return temp;
}

static void sit5359_read( struct wr_sit5359_interface_device *dev, uint8_t addr, uint8_t *data, int count )
{
    int i;

    bb_i2c_start( &dev->master );
    bb_i2c_put_byte( &dev->master, dev->i2c_addr << 1 );
    bb_i2c_put_byte( &dev->master, addr );
    bb_i2c_repeat_start( &dev->master );
    bb_i2c_put_byte( &dev->master, (dev->i2c_addr << 1) | 1 );

    for(i = 0; i < count; i ++)
        bb_i2c_get_byte( &dev->master, &data[i], i == (count - 1) ? 1 : 0 );

    bb_i2c_stop( &dev->master );
}

static void sit5359_i2c_write( struct wr_sit5359_interface_device *dev, uint8_t addr, uint8_t *data, int count )
{
    int i;

    bb_i2c_start( &dev->master );
    bb_i2c_put_byte( &dev->master, dev->i2c_addr << 1 );
    bb_i2c_put_byte( &dev->master, addr );

    for(i = 0; i < count; i ++)
        bb_i2c_put_byte( &dev->master, data[i] );

    bb_i2c_stop( &dev->master );
}

static int sit5359_dev_init( struct wr_sit5359_interface_device *dev )
{
    // Enable SPLL and Osc Output Enable (PartNo option "I": hardware OE via pin 1)
    // I2C bus freqency = 1/(4*(30+1)*16ns) = 504 KHz
    writel( SIT5359_CR_SPLL_EN | SIT5359_CR_OSC_OE | SIT5359_CR_CLK_DIV_W(30) | SIT5359_CR_I2C_ADDR_W ( ( dev->i2c_addr << 1 ) ), dev->base_addr + SIT5359_REG_CR );

    return 0;
}

static int conv_twos_compl (int val) {
    if (val < DAC_HALF_SCALE)
        return (DAC_FULL_SCALE - (~(val - DAC_HALF_SCALE) + 1));
    else
        return(val - DAC_HALF_SCALE);
}

static int __attribute__((unused)) regs2dac (uint8_t *regs)
{
    int dac_twos = (((regs[1] & 0xff) << (SIT5359_DFC_BITS-26)) | // SiT5359 Reg 0x00 7:0  => DFC-LSW[7:0]
                    ((regs[0] & 0xff) << (SIT5359_DFC_BITS-18)) | // SiT5359 Reg 0x00 15:8 => DFC-LSW[15:8]
                    ((regs[3] & 0xff) << (SIT5359_DFC_BITS-10)) | // SiT5359 Reg 0x01 7:0  => DFC-MSW[7:0]
                    ((regs[2] & 0x03) << (SIT5359_DFC_BITS-2 ))); // SiT5359 Reg 0x01 15:8 => DFC-MSW[9:8]
    dac_twos = dac_twos >> (SIT5359_DFC_BITS - BOARD_SPLL_DAC_BITS);
    return conv_twos_compl(dac_twos);
}

static void dac2regs (uint32_t dac, uint8_t * regs)
{
    int dac_twos = conv_twos_compl(dac);
    const char SITIME_OE = 0x04;            // bit 2 of register regs[2] = bit 10 of SiTime address 0x01

    dac_twos = dac_twos << (SIT5359_DFC_BITS - BOARD_SPLL_DAC_BITS);
    regs[1] =  (dac_twos >> (SIT5359_DFC_BITS-26)) & 0xff; // SiT5359 Reg 0x00 7:0  => DFC-LSW[7:0]
    regs[0] =  (dac_twos >> (SIT5359_DFC_BITS-18)) & 0xff; // SiT5359 Reg 0x00 15:8 => DFC-LSW[15:8]
    regs[3] =  (dac_twos >> (SIT5359_DFC_BITS-10)) & 0xff; // SiT5359 Reg 0x01 7:0  => DFC-MSW[7:0]
    regs[2] = ((dac_twos >> (SIT5359_DFC_BITS-2 )) & 0x3)  // SiT5359 Reg 0x01 15:8 => DFC-MSW[9:8]
              | SITIME_OE;                                 // (PartNo option "I": hardware OE via pin 1)
    regs[5] = 0x01;                                        // SiT5359 Reg 0x02 7:0  => Pull Range 10 ppm
    regs[4] = 0x00;                                        // SiT5359 Reg 0x02 15:8 => not used
}

void read_sitime (void)
{
    uint8_t regs[6];

    // read back all SiT5359_refclk registers (6 bytes)
    sit5359_read(&board.sit5359_refclk, 0x00, regs, 6 );
    board_dbg("SiT5359 RefClk Regs: %02x:%02x:%02x:%02x:%02x:%02x DAC: %d\n",regs[0],regs[1],regs[2],regs[3],regs[4],regs[5],regs2dac(regs));
    // read back all SiT5359_dmtd registers (6 bytes)
    sit5359_read(&board.sit5359_dmtd, 0x00, regs, 6 );
    board_dbg("SiT5359 DMTD Regs:   %02x:%02x:%02x:%02x:%02x:%02x DAC: %d\n",regs[0],regs[1],regs[2],regs[3],regs[4],regs[5],regs2dac(regs));
}

void write_sitime (int dev, int val)
{
    uint8_t regs[6];

    dac2regs(val, regs);

    if (dev == 0)
        sit5359_i2c_write(&board.sit5359_refclk, 0x00, regs, 6 );
    else
        sit5359_i2c_write(&board.sit5359_dmtd, 0x00, regs, 6 );
}

static void babywr_spll_setup(void)
{

int implement_two_stages = 0; // implement 2-stage ocxo lock later

/* configure a suitable PI gain schedule for the SoftPLL: */
    spll_gain_schedule_t* gs=  &spll_main_ocxo_gain_sched;

/* we start with the default SiT5359 values (Bandwidth ~20 Hz) */
    gs->stages[0].kp = -1800;  // use 1400 when X1 = 125 MHz
    gs->stages[0].ki = -25;
    gs->stages[0].lock_samples = 10000;
    gs->stages[0].shift = 12;

/* once it's locked, the loop bandwidth is switched to low bandwidth  to filter out WR link added phase noise */
    gs->stages[1].kp = -3000;
    gs->stages[1].ki = -5;
    gs->stages[1].lock_samples = 10000;
    gs->stages[1].shift = 16;

    if ( implement_two_stages ) {
        gs->n_stages = 2;   // 2 stages: OCXO
        board_dbg("Oscillator gain schedule: Two stage OCXO setup\n");
        spll_set_gain_schedule( gs );
    } else {
        gs->n_stages = 1;   // 1 stage: SiTime 5359
        board_dbg("Oscillator gain schedule: 1st stage SiTime setup\n");
        spll_set_gain_schedule( gs );
    }
}

static struct gpio_pin pin_eeprom_scl        = { &board.gpio_aux, 0 };
static struct gpio_pin pin_eeprom_sda        = { &board.gpio_aux, 1 };
static struct gpio_pin pin_aux_scl           = { &board.gpio_aux, 2 };
static struct gpio_pin pin_aux_sda           = { &board.gpio_aux, 3 };
//static struct gpio_pin pin_spare0            = { &board.gpio_aux, 4 };
//static struct gpio_pin pin_spare1            = { &board.gpio_aux, 5 };

struct i2c_bus            i2c_wrc_eeprom;
struct i2c_bus            dev_i2c_aux;
struct i2c_eeprom_device  wrc_eeprom_dev;
struct i2c_eeprom_device  wrc_uid_dev;

int wrc_board_early_init()
{
    /* most of the I/Os of the slow peripherals (i2c, spi) are bitbanged. First, let's
       initialize the GPIO controller they're connected to */
    wb_gpio_create( &board.gpio_aux, BASE_GPIO );
    
    /* create and initialize eeprom I2C bus */
    bb_i2c_create(&i2c_wrc_eeprom,
         &pin_eeprom_scl,
         &pin_eeprom_sda );
    bb_i2c_init(&i2c_wrc_eeprom);

    /* create and initialize auxiliary I2C bus */
    bb_i2c_create(&dev_i2c_aux,
         &pin_aux_scl,
         &pin_aux_sda );
    bb_i2c_init(&dev_i2c_aux);

    i2c_eeprom_create(&wrc_eeprom_dev, &i2c_wrc_eeprom, FMC_EEPROM_ADR, 2);
    storage_i2ceeprom_create( &wrc_storage_dev, &wrc_eeprom_dev );

    /*
     * Mount SDBFS filesystem from storage.
     */
    storage_mount( &wrc_storage_dev );
    
    /* create and initialize UID eeprom I2C bus */
    i2c_eeprom_create(&wrc_uid_dev, &i2c_wrc_eeprom, UID_EEPROM_ADR, 1);

    wr_sit5359_interface_init( &board.sit5359_refclk, BASE_SIT5359_REFCLK, SIT5359_I2C_ADDR_A0_1 );
    wr_sit5359_interface_init( &board.sit5359_dmtd, BASE_SIT5359_DMTD, SIT5359_I2C_ADDR_A0_0 );

    /* Setup the SoftPLL for the OCXO we have */
    babywr_spll_setup();

    return 0;
}

int wrc_board_init()
{
    uint8_t regs[6];

    // set I2C bus speed and OSC Output enable
    sit5359_dev_init(&board.sit5359_refclk);
    sit5359_dev_init(&board.sit5359_dmtd);

    // initialize registers
    dac2regs(DAC_HALF_SCALE, regs);
    sit5359_i2c_write(&board.sit5359_refclk, 0x00, regs, 6 );
    sit5359_i2c_write(&board.sit5359_dmtd, 0x00, regs, 6 );

    // read back the SiT5359 oscillators
    read_sitime();

    uint8_t mac_addr[6];
    /*
     * Read MAC addr from Unique-ID, IC D12, 24AA025E48
     */

    i2c_eeprom_read(&wrc_uid_dev, UID_OFFSET , mac_addr, sizeof(mac_addr));
    board_dbg("MAC addr: %x:%x:%x:%x:%x:%x\n",mac_addr[0],mac_addr[1],mac_addr[2],mac_addr[3],mac_addr[4],mac_addr[5]);

    ep_set_mac_addr(&wrc_endpoint_dev, mac_addr);
    ep_pfilter_init_default(&wrc_endpoint_dev);

    return 0;
}
