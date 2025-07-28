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
#include "shell.h"

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
    RX_READY
};

#define SWEEP_WAIT_LOCK 0
#define SWEEP_WAIT_MEASURE 1
#define SWEEP_DONE 2
#define SWEEP_ERROR 3

struct sweep_state {
    unsigned state;
    unsigned ps_res;
    unsigned prev_val;
    unsigned abs_phase;
    int delta;
};

struct rx_state {
    enum rx_fsm_state state;
    timeout_t timeout;
    struct sweep_state sweep;
};

static void rxpi_sweep_init(struct sweep_state *state)
{
    regs->ps_ctrl = RXPI_GTHE4_MAP_PS_CTRL_RST;
    regs->ps_count = 10240; /* # of val to sample */
    state->prev_val = RXPI_GTHE4_MAP_PS_RES_VAL_MASK;
    state->state = SWEEP_WAIT_LOCK;
}

static void rxpi_sweep_fsm(struct sweep_state *state)
{
    switch (state->state) {
    case SWEEP_WAIT_LOCK:
	/* Clear reset, set incdec */
	regs->ps_ctrl = RXPI_GTHE4_MAP_PS_CTRL_INCDEC;
	if (regs->ps_stat & RXPI_GTHE4_MAP_PS_STAT_LOCKED) {
	    state->ps_res = regs->ps_res;
	    state->state = SWEEP_WAIT_MEASURE;
	}
	break;
    case SWEEP_WAIT_MEASURE: {
	unsigned res = regs->ps_res;
	if ((res & RXPI_GTHE4_MAP_PS_RES_GEN_MASK)
	    != (state->ps_res & RXPI_GTHE4_MAP_PS_RES_GEN_MASK)) {
	    /* Got a new value (different generation). */
	    unsigned phase = regs->ps_stat & RXPI_GTHE4_MAP_PS_STAT_PHASE_MASK;
	    unsigned val = res & RXPI_GTHE4_MAP_PS_RES_VAL_MASK;

	    if (0)
		phy_dbg("phase measure (%u.%02u=%ups): %u (res=%08x)\n",
			phase / 56, phase % 56, phase * 800 / 56, val, res);
	    if (val > 0 && state->prev_val == 0) {
		unsigned tag_ref = softpll.mpll.tag_ref;

		/* Phase shift clock vco is running at 1250Mhz, so the
		   period is 800ps.
		   A shift is 800ps/56 */
		phy_dbg("phase measure ph:%u.%02u phps:%ups val:%u res:%08x\n",
			phase / 56, phase % 56, phase * 800 / 56, val, res);

		/* Phase shift to tag:
		   ((phase / 56) * 800 / 200) << 14
		   = (phase << 14) * 4 / 56
		   = (phase << 14) / 14 */
		unsigned abs_phase =
		    ((phase / (56 / 4)) << 14) | (tag_ref & ((1 << 14) - 1));

		int delta = tag_ref - abs_phase;

		/* Tag is (200ps/128) * (1<<15) * 256 = 200ps * (1<<14) */
		phy_dbg("rising edge, tag:%u tagui:%uui.%04x abs_ph:%u phui:%uui.%04x phps:%ups delta:%u deltaui:%uui.%04x (ui=200ps)\n",
			tag_ref, tag_ref >> 14, (tag_ref << 2) & 0xffff,
			abs_phase, abs_phase >> 14, (abs_phase << 2) & 0xffff,
			abs_phase * 25 >> 11,
			delta, delta >> 14, (delta << 2) & 0xffff);

		state->abs_phase = abs_phase;
		state->delta = delta;
		state->state = SWEEP_DONE;
	    }
	    else if (phase == 20 * 56) {
		phy_dbg("rx edge not found\n");
		state->state = SWEEP_ERROR;
	    }
	    else {
		regs->ps_ctrl = RXPI_GTHE4_MAP_PS_CTRL_SHIFT
		    | RXPI_GTHE4_MAP_PS_CTRL_INCDEC;
		state->prev_val = val;
		state->state = SWEEP_WAIT_LOCK;
	    }
	}
    }
    default:
	break;
    }
}

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
	softpll.mpll.rxpi_ready = 0;
	break;
    case RX_WAIT_RESET:
	if (tmo_expired(&rx_state.timeout)) {
	    regs->reset &= ~RXPI_GTHE4_MAP_RESET_GTH_RX_PMA_RST;
	    rx_state.state = RX_WAIT_COMMA;
	}
	break;
    case RX_WAIT_COMMA:
	if (status & (1 << 16)) {
	    phy_dbg("comma detected: %08x\n", status);
	    rx_state.state = RX_WAIT_ALIGN;
	}
	break;

    case RX_WAIT_ALIGN:
	if (status & (1 << 10)) {
	    unsigned bitslide = regs->bitslide;
	    phy_dbg("comma-aligned:%08x slide:%u\n", status, bitslide);
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
	if (!(status & (1 << 16))) {
	    phy_dbg("not comma aligned: %08x\n", status);
	    rx_state.state = RX_RESET;
	    regs->ctrl &= ~RXPI_GTHE4_MAP_CTRL_RDY;
	}
	if (softpll.mpll.phase_ld.locked) {
	    phy_dbg("phase locked, start sweep!\n");
	    rxpi_sweep_init(&rx_state.sweep);
	    rx_state.state = RX_SWEEP_WAIT;
	}
	break;

    case RX_SWEEP_WAIT:
	rxpi_sweep_fsm(&rx_state.sweep);
	if (rx_state.sweep.state == SWEEP_DONE) {
	    softpll.mpll.phase_shift_current = rx_state.sweep.abs_phase;
	    softpll.mpll.phase_shift_target = rx_state.sweep.abs_phase;
	    softpll.ptrackers[0].offset = -rx_state.sweep.delta;
	    softpll.mpll.rxpi_ready = 1;
	    rx_state.state = RX_READY;
	}
	else if (rx_state.sweep.state == SWEEP_ERROR) {
	    rx_state.state = RX_READY;
	}
	break;
    case RX_READY:
	if (!(status & (1 << 16))) {
	    phy_dbg("link down\n");
	    rx_state.state = RX_RESET;
	}
	else if (!softpll.mpll.phase_ld.locked) {
	    phy_dbg("pll unlocked\n");
	    softpll.mpll.enabled = 0;
	    ld_init((spll_lock_det_t *)&softpll.mpll.phase_ld);
	    ld_init((spll_lock_det_t *)&softpll.mpll.freq_ld);
	    softpll.mpll.rxpi_ready = 0;
	    softpll.mpll.enabled = 1;
	    rx_state.state = RX_WAIT_FREQ_LOCK;
	}
	break;
    }
    
    return 1;
}

void phy_calibration_init(void)
{
    rx_state.state = RX_RESET;
}


static const char * const rxpi_cmds[] =
{
	 [0] = "sweep",
};

static int cmd_rxpi(const char *args[])
{
	int icmd;

	icmd = sub_cmd(rxpi_cmds, ARRAY_SIZE(rxpi_cmds), args);

	switch (icmd) {
	case 0: {
	    struct sweep_state state;
	    rxpi_sweep_init(&state);
	    while (state.state < SWEEP_DONE)
		rxpi_sweep_fsm(&state);
	    return 0;
	}
	default:
	    return -1;
	}
}

DEFINE_WRC_COMMAND(rxpi) = {
	.name = "rxpi",
	.exec = cmd_rxpi,
};
