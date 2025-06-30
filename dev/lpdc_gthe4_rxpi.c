/*
 * This work is part of the White Rabbit project
 *
 * Copyright (C) 2025 CERN
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

#include <string.h>
#include <board.h>
#include "dev/syscon.h"
#include "dev/endpoint.h"
#include <softpll_ng.h>
#include "storage.h"
#include "util.h"
#include "wrc-debug.h"
#include "wrc-task.h"

#include <hw/rxpi_gthe4_map.h>

static volatile struct rxpi_gthe4_map *regs =
  (volatile struct rxpi_gthe4_map *)BASE_AUXWB;

enum rx_fsm_state {
    /* Out of reset, wait for commas */
    RX_RESET,
    /* Comma detected, wait for alignment */
    RX_COMMA,
    /* Comma detected and at correct alignment */
    RX_ALIGNED
};

struct rx_state {
    enum rx_fsm_state state;
};

static struct rx_state rx_state;

int phy_calibration_poll(void)
{
    unsigned status;

    status = regs->status;
    switch (rx_state.state) {
    case RX_RESET:
	if (status & (1 << 11)) {
	    phy_dbg("comma detected: %08x\n", status);
	    rx_state.state = RX_COMMA;
	}
	break;

    case RX_COMMA:
	if (status & (1 << 10)) {
	    phy_dbg("comma aligned: %08x slide: %08x\n",
		    status, regs->bitslide);
	    rx_state.state = RX_ALIGNED;
	    regs->ctrl = RXPI_GTHE4_MAP_CTRL_RDY;
	}
	break;

    case RX_ALIGNED:
	if (!(status & (1 << 10))) {
	    phy_dbg("not comma aligned: %08x\n", status);
	    rx_state.state = RX_RESET;
	    regs->ctrl &= ~RXPI_GTHE4_MAP_CTRL_RDY;
	}
	break;
    }
    
    return 1;
}

void phy_calibration_init(void)
{
    pp_printf("rxpi magic: %08x (@%08x)\n",
	      (unsigned)regs->id, (unsigned)&regs->id);
    rx_state.state = RX_RESET;
}

void phy_calibration_disable(void)
{
}
