/*
 * Onewire generic interface
 * Alessandro Rubini, 2013 GNU GPL2 or later
 */
#include "wrc.h"
#include "shell.h"
#include "dev/w1.h"
#include "cmds.h"

#define BLEN 32

#ifdef CONFIG_W1_EEPROM
/* A shell command, for testing write: "w1w <offset> <byte> [<byte> ...]" */
int cmd_w1w(const char *args[])
{
	struct w1_dev *w1_dev = w1_find_eeprom_device(&wrpc_w1_bus);
	int offset, i, blen;
	unsigned char buf[BLEN];

	if (!args[0] || !args[1] || w1_dev == NULL)
		return -1;
	offset = atoi(args[0]);
	for (i = 1, blen = 0; args[i] && blen < BLEN; i++, blen++) {
		buf[blen] = atoi(args[i]);
		pp_printf("offset %4i (0x%03x): %3i (0x%02x)\n",
			  offset + blen, offset + blen, buf[blen], buf[blen]);
	}
	i = w1_write_eeprom(w1_dev, offset, buf, blen);
	pp_printf("write(0x%x, %i): result = %i\n", offset, blen, i);
	return i == blen ? 0 : -1;
}

/* A shell command, for testing read: "w1r <offset> <len> */
int cmd_w1r(const char *args[])
{
	struct w1_dev *w1_dev = w1_find_eeprom_device(&wrpc_w1_bus);
	int offset, i, blen;
	unsigned char buf[BLEN];

	if (!args[0] || !args[1] || w1_dev == NULL)
		return -1;
	offset = atoi(args[0]);
	blen = atoi(args[1]);
	if (blen > BLEN)
		blen = BLEN;
	i = w1_read_eeprom(w1_dev, offset, buf, blen);
	pp_printf("read(0x%x, %i): result = %i\n", offset, blen, i);
	if (i <= 0 || i > blen) return -1;
	for (blen = 0; blen < i; blen++) {
		pp_printf("offset %4i (0x%03x): %3i (0x%02x)\n",
			  offset + blen, offset + blen, buf[blen], buf[blen]);
	}
	return i == blen ? 0 : -1;
}
#endif /* CONFIG_W1_EEPROM */

#ifdef CONFIG_W1_TEMP
/* A shell command, for checking */
int cmd_w1(const char *args[])
{
	int i;
	struct w1_dev *d;
	int32_t temp;

	for (i = 0; i < W1_MAX_DEVICES; i++) {
		d = wrpc_w1_bus.devs + i;
		if (d->rom) {
			pp_printf("device %i: %08x%08x\n", i,
				  (int)(d->rom >> 32), (int)d->rom);
		temp = w1_read_temp(d, 0);
		pp_printf("temp: %d.%04d\n", (int) (temp >> 16),
			  (int)((temp & 0xffff) * 10 * 1000 >> 16));
		}
	}
	return 0;
}
#endif /* CONFIG_W1_TEMP */
