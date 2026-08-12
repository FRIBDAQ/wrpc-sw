/*
 * This work is part of the White Rabbit project
 *
 * Released according to the GNU GPL, version 2 or any later version.
 *
 * CTS board (KR260 + APV5104 mezzanine), Cortex-R5 firmware.
 * Derived from boards/generic.
 *
 * SFP0 management I2C is owned by the R5 and read through the generic WR
 * path (dev/sfp.c -> sfp_match(), called on link-up), bit-banged over the
 * WR-core SYSCON GPIO (sfp1_scl/sfp1_sda), brought out on P2_HDIO3/4.
 *
 * Persistent storage is two 25AA02E48 SPI EEPROMs (256 B each) on SOM
 * connector CN1, bank 45.  They are driven by a PL-native SPI master in the
 * gateware (top/cts/spi_master.vhd), which the R5 controls through mpsoc_map
 * registers at 0x8000_2000 (SPI_CS / SPI_TX / SPI_RX).  The two chips are
 * concatenated into one flat 480 B SDBFS device (only the low 240 B of each
 * is used; the top page holds the factory EUI-48).  The WR node MAC is taken
 * from chip0's EUI-48 at 0xFA.
 */
#include <string.h>

#include "board.h"
#include "wrc-debug.h"
#include "pp-printf.h"
#include "dev/endpoint.h"
#include "storage.h"
#include "board-decl.h"

/* Run a register-based read/write self-test at boot (prints over console). */
#define CTS_SPI_SELFTEST 1

/* ---- PL SPI master registers (mpsoc_map, AXI window @ 0x8000_0000) ---- */
#define SPI_EE_BASE   0x80002000u
#define SPI_EE_CS     (*(volatile uint32_t *)(SPI_EE_BASE + 0x00))  /* rw */
#define SPI_EE_TX     (*(volatile uint32_t *)(SPI_EE_BASE + 0x04))  /* wo: starts xfer */
#define SPI_EE_RX     (*(volatile uint32_t *)(SPI_EE_BASE + 0x08))  /* ro: [7:0]=rx [8]=busy */

#define SPI_RX_BUSY   (1u << 8)
/* SPI_CS bits are the raw CSN pin levels (active low): 1 = deasserted. */
#define SPI_CS_DESEL  0x3u                 /* both chips deselected */

/* ---- 25AA02E48 SPI EEPROM ------------------------------------------- */
#define EE_OP_READ   0x03
#define EE_OP_WRITE  0x02
#define EE_OP_WREN   0x06
#define EE_OP_RDSR   0x05
#define EE_OP_WRSR   0x01

#define EE_SR_BP     0x0c      /* BP1:BP0 block-protect bits */

#define EE_PAGE      16        /* write page size (bytes) */
#define EE_USABLE    240       /* 0x00..0xEF; top page holds the EUI-48 */
#define EE_MAC_OFFS  0xFA      /* factory EUI-48 location (6 bytes) */
#define EE_TOTAL     (2 * EE_USABLE)   /* flat SDBFS device size (480 B) */

/* Assert one chip's CS (active low), the other stays high. */
static void spi_select(int chip)
{
	SPI_EE_CS = (chip == 0) ? (SPI_CS_DESEL & ~0x1u)    /* CSN0 low  */
				: (SPI_CS_DESEL & ~0x2u);   /* CSN1 low  */
}

static void spi_deselect(void)
{
	SPI_EE_CS = SPI_CS_DESEL;
}

/* Shift out one byte and return the byte shifted in.  CS is managed by the
 * caller and held across the bytes of a transaction. */
static uint8_t spi_xfer(uint8_t tx)
{
	SPI_EE_TX = tx;			/* the write starts the transfer */
	while (SPI_EE_RX & SPI_RX_BUSY)
		;
	return (uint8_t)(SPI_EE_RX & 0xff);
}

static void ee_read(int chip, uint32_t addr, uint8_t *buf, int n)
{
	int i;

	spi_select(chip);
	spi_xfer(EE_OP_READ);
	spi_xfer(addr & 0xff);
	for (i = 0; i < n; i++)
		buf[i] = spi_xfer(0x00);
	spi_deselect();
}

static uint8_t ee_rdsr(int chip)
{
	uint8_t r;

	spi_select(chip);
	spi_xfer(EE_OP_RDSR);
	r = spi_xfer(0x00);
	spi_deselect();
	return r;
}

/* Write the status register (WREN + WRSR + WIP poll). */
static void ee_write_sr(int chip, uint8_t val)
{
	unsigned int timeout = 2000000;

	spi_select(chip);
	spi_xfer(EE_OP_WREN);
	spi_deselect();

	spi_select(chip);
	spi_xfer(EE_OP_WRSR);
	spi_xfer(val);
	spi_deselect();

	while ((ee_rdsr(chip) & 0x01) && --timeout)	/* WRSR write cycle */
		;
}

/* Clear block protection (BP1:BP0) so the whole array is writable.  Some
 * 25AA02E48 ship with BP0 set (protects 0xC0-0xFF), which blocks writes to the
 * upper part of our storage region.  Retry + verify, since a single WRSR did
 * not always take on some units.  The 6-lead SOT-23 has no WP pin, so WRSR is
 * always permitted; we never write the EUI-48 (0xFA), so it stays safe. */
static int ee_clear_bp(int chip)
{
	int tries;

	for (tries = 0; tries < 3; tries++) {
		if ((ee_rdsr(chip) & EE_SR_BP) == 0)
			return 0;
		ee_write_sr(chip, 0x00);
	}
	return ((ee_rdsr(chip) & EE_SR_BP) == 0) ? 0 : -1;
}

/* Write within a single 16-byte page (caller guarantees no page crossing). */
static void ee_write_page(int chip, uint32_t addr, const uint8_t *buf, int n)
{
	unsigned int timeout = 2000000;
	int i;

	spi_select(chip);
	spi_xfer(EE_OP_WREN);
	spi_deselect();

	spi_select(chip);
	spi_xfer(EE_OP_WRITE);
	spi_xfer(addr & 0xff);
	for (i = 0; i < n; i++)
		spi_xfer(buf[i]);
	spi_deselect();

	while ((ee_rdsr(chip) & 0x01) && --timeout)	/* WIP */
		;
	if (!timeout)
		pp_printf("cts spi-ee: WIP timeout @0x%02x\n", (unsigned)addr);
}

static void ee_write(int chip, uint32_t addr, const uint8_t *buf, int n)
{
	while (n > 0) {
		int pg = EE_PAGE - (addr & (EE_PAGE - 1));	/* to page end */

		if (pg > n)
			pg = n;
		ee_write_page(chip, addr, buf, pg);
		addr += pg;
		buf += pg;
		n -= pg;
	}
}

/* ---- flat SDBFS device over the two chips (0..239 chip0, 240..479 chip1) */
static void cts_route(uint32_t off, int *chip, uint32_t *chip_addr, int *room)
{
	if (off < EE_USABLE) {
		*chip = 0;
		*chip_addr = off;
		*room = EE_USABLE - off;
	} else {
		*chip = 1;
		*chip_addr = off - EE_USABLE;
		*room = EE_TOTAL - off;
	}
}

static int cts_storage_read(struct storage_device *dev, int offset,
			    void *buf, int count)
{
	uint8_t *p = buf;
	int done = 0;

	while (count > 0 && (uint32_t)offset < EE_TOTAL) {
		int chip, room, chunk;
		uint32_t ca;

		cts_route(offset, &chip, &ca, &room);
		chunk = count < room ? count : room;
		ee_read(chip, ca, p, chunk);
		offset += chunk;
		p += chunk;
		count -= chunk;
		done += chunk;
	}
	return done;
}

static int cts_storage_write(struct storage_device *dev, int offset,
			     void *buf, int count)
{
	uint8_t *p = buf;
	int done = 0;

	while (count > 0 && (uint32_t)offset < EE_TOTAL) {
		int chip, room, chunk;
		uint32_t ca;

		cts_route(offset, &chip, &ca, &room);
		chunk = count < room ? count : room;
		ee_write(chip, ca, p, chunk);
		offset += chunk;
		p += chunk;
		count -= chunk;
		done += chunk;
	}
	return done;
}

/* EEPROM has no erase; emulate by writing 0xFF so SDBFS sees blank space. */
static int cts_storage_erase(struct storage_device *dev, int offset, int count)
{
	uint8_t ff[EE_PAGE];
	int done = 0;

	memset(ff, 0xff, sizeof(ff));
	while (count > 0) {
		int chunk = count < EE_PAGE ? count : EE_PAGE;

		if (cts_storage_write(dev, offset, ff, chunk) != chunk)
			break;
		offset += chunk;
		count -= chunk;
		done += chunk;
	}
	return done;
}

static const struct storage_rwops cts_ee_rwops = {
	.read  = cts_storage_read,
	.write = cts_storage_write,
	.erase = cts_storage_erase,
};

static const int32_t cts_ee_entry_points[] = { 0x000000, -1 };

#if CTS_SPI_SELFTEST
static void cts_spi_test_chip(const char *name, int chip)
{
	static const uint8_t pat[2] = { 0x5a, 0xa5 };
	uint8_t eui[6], sr, rb, orig;
	int i, ok = 1;

	sr = ee_rdsr(chip);
	ee_read(chip, EE_MAC_OFFS, eui, 6);
	pp_printf("%s: SR=%02x EUI-48=%02x:%02x:%02x:%02x:%02x:%02x\n", name,
		  sr, eui[0], eui[1], eui[2], eui[3], eui[4], eui[5]);

	ee_read(chip, 0xF0, &orig, 1);		/* scratch, outside SDBFS+EUI */
	for (i = 0; i < 2; i++) {
		ee_write(chip, 0xF0, &pat[i], 1);
		ee_read(chip, 0xF0, &rb, 1);
		if (rb != pat[i]) {
			pp_printf("%s: write FAIL (wrote %02x read %02x)\n",
				  name, pat[i], rb);
			ok = 0;
			break;
		}
	}
	ee_write(chip, 0xF0, &orig, 1);		/* restore */
	if (ok)
		pp_printf("%s: read/write OK\n", name);
}
#endif

static void cts_storage_init(void)
{
	spi_deselect();

	/* Clear block protection so the full storage region is writable. */
	if (ee_clear_bp(0) < 0)
		pp_printf("cts spi-ee: chip0 BP clear failed (SR=%02x)\n",
			  ee_rdsr(0));
	if (ee_clear_bp(1) < 0)
		pp_printf("cts spi-ee: chip1 BP clear failed (SR=%02x)\n",
			  ee_rdsr(1));

#if CTS_SPI_SELFTEST
	pp_printf("CTS SPI-EEPROM self-test (PL master, two 25AA02E48):\n");
	cts_spi_test_chip("  chip0", 0);
	cts_spi_test_chip("  chip1", 1);
#endif

	wrc_storage_dev.name         = "spi-eeprom-25aa02x2";
	wrc_storage_dev.priv         = NULL;
	wrc_storage_dev.block_size   = EE_PAGE;
	wrc_storage_dev.size         = EE_TOTAL;
	wrc_storage_dev.cfg_entry    = 0;
	wrc_storage_dev.entry_points = cts_ee_entry_points;
	wrc_storage_dev.rwops        = &cts_ee_rwops;

	storage_mount(&wrc_storage_dev);
}

/* MAC is chip0's factory EUI-48 (25AA02E48, at 0xFA). */
static int cts_get_mac(uint8_t *mac)
{
	ee_read(0, EE_MAC_OFFS, mac, 6);

	if ((mac[0] == 0xff && mac[1] == 0xff && mac[2] == 0xff) ||
	    (mac[0] == 0x00 && mac[1] == 0x00 && mac[2] == 0x00))
		return -1;

	return 0;
}

/* --- board hooks ----------------------------------------------------- */
int wrc_board_early_init(void)
{
	cts_storage_init();

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
