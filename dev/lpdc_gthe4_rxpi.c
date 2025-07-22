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
    /* Start a reset. */
    RX_RESET,
    /* Wait end of reset */
    RX_WAIT_RESET,
    /* Out of reset, wait for commas */
    RX_WAIT_COMMA,
    /* Comma detected, wait for alignment */
    RX_WAIT_ALIGN,
    /* Comma detected and at correct alignment */
    RX_WAIT_FREQ_LOCK,
    RX_SWEEP_WAIT,
    RX_SWEEP_MEASURE,
    RX_READY
};

struct rx_state {
    enum rx_fsm_state state;
    timeout_t timeout;
    unsigned ps_res;
    unsigned prev_val;
};

static struct rx_state rx_state;

int phy_calibration_poll(void)
{
    unsigned status;

    status = regs->status;
    switch (rx_state.state) {
    case RX_RESET:
	phy_dbg("reset rx\n");
	regs->reset |= RXPI_GTHE4_MAP_RESET_GTH_RX_PMA_RST;
	regs->ctrl &= ~RXPI_GTHE4_MAP_CTRL_RDY;
	tmo_init(&rx_state.timeout, 200);
	rx_state.state = RX_WAIT_RESET;
	break;
    case RX_WAIT_RESET:
	if (tmo_expired(&rx_state.timeout)) {
	    regs->reset &= ~RXPI_GTHE4_MAP_RESET_GTH_RX_PMA_RST;
	    rx_state.state = RX_WAIT_COMMA;
	}
	break;
    case RX_WAIT_COMMA:
	if (status & (1 << 11)) {
	    phy_dbg("comma detected: %08x\n", status);
	    rx_state.state = RX_WAIT_ALIGN;
	}
	break;

    case RX_WAIT_ALIGN:
	if (status & (1 << 10)) {
	    unsigned bitslide = regs->bitslide;
	    phy_dbg("comma aligned: %08x slide: %08x\n",
		    status, bitslide);
	    if (bitslide & 1)
		rx_state.state = RX_RESET;
	    else {
		rx_state.state = RX_WAIT_FREQ_LOCK;
		softpll.mpll.rxpi_ready = 0;
		regs->ctrl = RXPI_GTHE4_MAP_CTRL_RDY;
	    }
	}
	break;

    case RX_WAIT_FREQ_LOCK:
	if (!(status & (1 << 10))) {
	    phy_dbg("not comma aligned: %08x\n", status);
	    rx_state.state = RX_RESET;
	    regs->ctrl &= ~RXPI_GTHE4_MAP_CTRL_RDY;
	}
	if (softpll.mpll.phase_ld.locked) {
	    phy_dbg("phase locked, start sweep!\n");
	    regs->ps_ctrl = RXPI_GTHE4_MAP_PS_CTRL_RST;
	    regs->ps_count = 10240; /* # of val to sample */
	    rx_state.state = RX_SWEEP_WAIT;
	    rx_state.prev_val = RXPI_GTHE4_MAP_PS_RES_VAL_MASK;
	}
	break;

    case RX_SWEEP_WAIT:
	/* Clear reset, set incdec */
	regs->ps_ctrl = RXPI_GTHE4_MAP_PS_CTRL_INCDEC;
	if (regs->ps_stat & RXPI_GTHE4_MAP_PS_STAT_LOCKED) {
	    rx_state.ps_res = regs->ps_res;
	    rx_state.state = RX_SWEEP_MEASURE;
	}
	break;
    case RX_SWEEP_MEASURE: {
	unsigned res = regs->ps_res;
	if ((res & RXPI_GTHE4_MAP_PS_RES_GEN_MASK)
	    != (rx_state.ps_res & RXPI_GTHE4_MAP_PS_RES_GEN_MASK)) {
	    unsigned phase = regs->ps_stat & RXPI_GTHE4_MAP_PS_STAT_PHASE_MASK;
	    unsigned val = res & RXPI_GTHE4_MAP_PS_RES_VAL_MASK;

	    if (0)
		phy_dbg("phase measure (%u.%02u=%ups): %u (res=%08x)\n",
			phase / 56, phase % 56, phase * 800 / 56, val, res);
	    if (val > 0 && rx_state.prev_val == 0) {
		unsigned tag_ref = softpll.mpll.tag_ref_d;
		int delta = (phase / (56 / 4)) - (tag_ref >> 14);
		softpll.mpll.rxpi_ready = 1;
		softpll.ptrackers[0].offset = delta << 14;
		
		phy_dbg("phase measure (%u.%02u=%ups): %u (res=%08x)\n",
			phase / 56, phase % 56, phase * 800 / 56, val, res);

		phy_dbg("rising edge, tag=%u (%uui.%04x), delta ui=%d (ui=200ps)\n",
			tag_ref, tag_ref >> 14, (tag_ref << 2) & 0xffff,
			delta);
		softpll.mpll.phase_shift_current = tag_ref + (delta << 14);
		softpll.mpll.phase_shift_target = softpll.mpll.phase_shift_current;
		rx_state.state = RX_READY;
	    }
	    else if (phase == 20 * 56) {
		phy_dbg("rx edge not found\n");
		rx_state.state = RX_READY;
	    }
	    else {
		regs->ps_ctrl = RXPI_GTHE4_MAP_PS_CTRL_SHIFT
		    | RXPI_GTHE4_MAP_PS_CTRL_INCDEC;
		rx_state.prev_val = val;
		rx_state.state = RX_SWEEP_WAIT;
	    }
	}
	break;
    }
    case RX_READY:
	if (!softpll.mpll.phase_ld.locked) {
	    softpll.mpll.rxpi_ready = 0;
	    rx_state.state = RX_WAIT_FREQ_LOCK;
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
