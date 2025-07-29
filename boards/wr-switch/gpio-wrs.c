/*
 * This work is part of the White Rabbit project
 *
 * Copyright (C) 2013-2019 CERN (www.cern.ch)
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
#include "dev/gpio.h"
#include "gpio-wrs.h"

#define GPIO_SYS_CLK_SEL	0
#define GPIO_PLL_RESET_N	1
#define GPIO_PERIPH_RESET_N	3
#define GPIO_LJD_BOARD_DETECT	4
#define GPIO_LJD_OSC_FREQ_0	5
#define GPIO_LJD_OSC_FREQ_1	6
#define GPIO_LJD_OSC_FREQ_2	7
#define GPIO_LJD_PERIPH_ID_0	8
#define GPIO_LJD_PERIPH_ID_1	9
#define GPIO_LJD_PERIPH_ID_2	10

static struct gpio_device wrs_gpio;

const struct gpio_pin gpio_pin_sys_clk_sel = { &wrs_gpio, GPIO_SYS_CLK_SEL };
const struct gpio_pin gpio_pin_pll_reset_n = { &wrs_gpio, GPIO_PLL_RESET_N };
const struct gpio_pin gpio_pin_periph_reset_n = { &wrs_gpio, GPIO_PERIPH_RESET_N };
const struct gpio_pin gpio_pin_ljd_board_detect = { &wrs_gpio, GPIO_LJD_BOARD_DETECT };
const struct gpio_pin gpio_pin_ljd_osc_freq_0 = { &wrs_gpio, GPIO_LJD_OSC_FREQ_0 };
const struct gpio_pin gpio_pin_ljd_osc_freq_1 = { &wrs_gpio, GPIO_LJD_OSC_FREQ_1 };
const struct gpio_pin gpio_pin_ljd_osc_freq_2 = { &wrs_gpio, GPIO_LJD_OSC_FREQ_2 };
const struct gpio_pin gpio_pin_ljd_periph_id_0 = { &wrs_gpio, GPIO_LJD_PERIPH_ID_0 };
const struct gpio_pin gpio_pin_ljd_periph_id_1 = { &wrs_gpio, GPIO_LJD_PERIPH_ID_1 };
const struct gpio_pin gpio_pin_ljd_periph_id_2 = { &wrs_gpio, GPIO_LJD_PERIPH_ID_2 };

void wrs_gpio_init(void)
{
    wb_gpio_create( &wrs_gpio, BASE_GPIO );
}
