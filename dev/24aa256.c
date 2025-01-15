/*
 * This work is part of the White Rabbit project at FRIB
 *
 * Copyright (C) 2024 FRIB (frib.msu.edu)
 * Author: Genie Jhang <changj@frib.msu.edu>
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

#include <stdint.h>

#include "dev/i2c.h"
#include "dev/24aa256.h"
#include "pp-printf.h"

#include <errno.h>

int m24aa256_init(struct m24aa256_device *dev, struct i2c_bus *i2c, uint8_t addr)
{
    dev->bus = i2c;
    dev->addr = addr;

    return bb_i2c_devprobe(i2c, addr);
}

int m24aa256_read_mac(struct m24aa256_device *dev, uint8_t *mac)
{
    int i;

    bb_i2c_start(dev->bus);

    if (bb_i2c_put_byte (dev->bus, dev->addr << 1) < 0) {
        bb_i2c_stop( dev->bus );

        return -ENODEV;
    }

    // EUI-48 Node Address is 0x7F7A
    bb_i2c_put_byte(dev -> bus, 0x7F);
    bb_i2c_put_byte(dev -> bus, 0x7A);
    bb_i2c_repeat_start(dev -> bus);
    bb_i2c_put_byte(dev -> bus, (dev -> addr << 1) | 1);
    for (i = 0; i < 6; i++) {
        bb_i2c_get_byte(dev -> bus, &mac[i], i == 5 ? 1 : 0);
    }
    bb_i2c_stop(dev -> bus);

    return 0;
}


