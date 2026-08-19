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
#include "cmds.h"
#include "lpdc.h"

#include "hw/ep_mdio_regs.h"

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
    RX_WAIT_SEQ,
    /* Master role only: wait until the RX clock is syntonized (remote slave
       locked to us) before sweeping - detected as a stationary ptracker
       phase.  The mpll never locks on a master, so RX_WAIT_MPLL would wait
       forever and the sweep would never calibrate ptrackers[0].offset; but
       that offset de-quantizes EVERY rx timestamp (lib/net.c raw_phase),
       including t4 of the delay measurement, so without it crtt scatters by
       up to one 16ns clock period per relink -> PPS offset scatters ~8ns. */
    RX_WAIT_SYNTON,
    /* Comma detected and at correct alignment */
    RX_WAIT_MPLL,
    RX_SWEEP_WAIT,
    RX_READY
};

/* States of the sweep FSM */
#define SWEEP_WAIT_LOCK 0
#define SWEEP_WAIT_MEASURE 1
#define SWEEP_DONE 2
#define SWEEP_ERROR 3

/* Number of clock edge to sample for a sweep step */
#define NBR_SWEEP_SAMPLES 10240

/* Master-role syntonization detector (RX_WAIT_SYNTON): the ptracker phase
   must move less than TOL_PS between two samples CHECK_MS apart, COUNT times
   in a row.  A free-running remote slave drifts ~us/500ms (aliasing below TOL
   three consecutive times is ~1e-5); a locked one wanders a few ps. */
#define RXPI_SYNTON_CHECK_MS 500
#define RXPI_SYNTON_TOL_PS 200
#define RXPI_SYNTON_COUNT 3
/* One refclk period in ps: ptracker phase wraps here. */
#define RXPI_REF_PERIOD_PS 16000

struct sweep_state {
    unsigned char state;
    unsigned char verbose;

    /* Phase of the last 0, -1 for not set. */
    int phase_0;

    unsigned ps_res;
    unsigned abs_phase;
    int delta;
};

struct rx_state {
    enum rx_fsm_state state;
    timeout_t timeout;
    unsigned reset_iter;
    struct sweep_state sweep;
    /* Syntonization detector (master role) */
    int32_t synt_phase;
    unsigned char synt_cnt;
};

static void rxpi_sweep_init(struct sweep_state *state)
{
    regs->ps_ctrl = RXPI_GTHE4_MAP_PS_CTRL_RST;
    regs->ps_count = NBR_SWEEP_SAMPLES;
    state->phase_0 = -1;
    state->verbose = 0;
    state->state = SWEEP_WAIT_LOCK;
}

static void rxpi_sweep_fsm(struct sweep_state *state)
{
    switch (state->state) {
    case SWEEP_WAIT_LOCK:
	/* Clear reset, set incdec */
	regs->ps_ctrl = RXPI_GTHE4_MAP_PS_CTRL_INCDEC;
	if (regs->ps_stat & RXPI_GTHE4_MAP_PS_STAT_LOCKED) {
	    /* Skip the current measure */
	    state->ps_res = regs->ps_res;
	    state->state = SWEEP_WAIT_MEASURE;
	}
	break;
    case SWEEP_WAIT_MEASURE: {
	unsigned res = regs->ps_res;
	if ((res & RXPI_GTHE4_MAP_PS_RES_GEN_MASK)
	    == (state->ps_res & RXPI_GTHE4_MAP_PS_RES_GEN_MASK)) {
	    /* Same generation: no new measure */
	    break;
	}

	/* Got a new value (different generation). */
	unsigned phase = regs->ps_stat & RXPI_GTHE4_MAP_PS_STAT_PHASE_MASK;
	unsigned val = res & RXPI_GTHE4_MAP_PS_RES_VAL_MASK;

	if (state->verbose)
	    phy_dbg("phase measure (%u.%02u=%ups): %u (res=%08x)\n",
		    phase / 56, phase % 56, phase * 800 / 56, val, res);
	if (val == 0)
	    state->phase_0 = phase;
	if (val >= NBR_SWEEP_SAMPLES && state->phase_0 >= 0) {
	    /* Found phase 1 and already got phase 0,
	       Keep 22b like in spll_ptracker */
	    unsigned tag_ref = softpll.mpll.tag_ref & ((1 << 22) - 1);
	    unsigned phase_1 = phase;

	    /* Get the middle between before and after the rising edge */
	    phase = (state->phase_0 + phase_1) >> 1;

	    /* Phase shift clock vco is running at 1250Mhz, so the
	       period is 800ps.
	       A shift is 800ps/56
	       (56 is the number of available phase shifts per period) */
	    phy_dbg("sweep-result ph0:%u.%02u ph1:%u.%02u ph:%u.%02u phps:%ups\n",
		    state->phase_0 / 56, state->phase_0 % 56,
		    phase_1 / 56, phase_1 % 56,
		    phase / 56, phase % 56,
		    phase * 800 / 56);


	    /* To compute the absolute phase, we need to combine the phase
	       of the sweep (coarse) with the phase of rxpi (fine).
	       Round the sweep phase. */
	    unsigned sub_tag = tag_ref & ((1 << 14) - 1);

	    /* Traces.
	       Extract the remainder of 200ps for phase shift and tag
	       (There is 14 rxpi phase bits per 200ps period). */
	    int ph_ps = (phase * 800 / 56) % 200;
	    int tag_ps = ((tag_ref & ((1 << 14) - 1)) * 200) >> 14;
	    phy_dbg("ph_ps:%d tag_ps:%d diff_ps:%d sub_tag:%04x\n",
		    ph_ps, tag_ps, ph_ps - tag_ps, sub_tag);

	    /* Due to routing or transceiver layout, rxpi and sweep phases
	       are not equal.
	       Assume rxpi is always in advance wrt sweep.  If it isn't, there
	       was a rollover which needs to be fixed.
	       If the assumption is not correct, then it is always late.  We
	       are just adding a delay, which will be fixed by calibration.
	       FIXME: the only problem is the 'always'.  Can the skew sign
	       change with PVT ?  */
	    if (ph_ps >= tag_ps)
		phase += 56 / 4;

	    /* The phase must always be within the cycle.
	       Adjust when we scanned slightly beyond the cycle */
	    if (phase >= 20 * 56)
		phase -= 20 * 56;

	    /* Phase shift to tag:
	       ((phase / 56) * 800 / 200) << 14
	       = (phase << 14) * 4 / 56
	       = (phase << 14) / 14 */
	    unsigned abs_phase = ((phase / (56 / 4)) << 14) | sub_tag;

	    int delta = tag_ref - abs_phase;

	    /* Tag is (200ps/128) * (1<<15) * 256 = 200ps * (1<<14) */
	    phy_dbg("rising edge, tag:%u tagui:%uui.%04x abs_ph:%u aphui:%uui.%04x aphps:%ups delta:%d deltaui:%d (ui=200ps)\n",
		    tag_ref, tag_ref >> 14, (tag_ref << 2) & 0xffff,
		    abs_phase, abs_phase >> 14, (abs_phase << 2) & 0xffff,
		    abs_phase * 25 >> 11,
		    delta, delta >> 14);

	    state->abs_phase = abs_phase;
	    state->delta = delta;
	    state->state = SWEEP_DONE;
	}
	else if (phase == 21 * 56) {
	    /* Go slightly beyond a full cycle (20 * 56) in order to detect
	       rising edges close to the clock rising edge */
	    phy_dbg("rx edge not found\n");
	    state->state = SWEEP_ERROR;
	}
	else {
	    /* Rising edge still not detected, continue phase shifting */
	    regs->ps_ctrl = RXPI_GTHE4_MAP_PS_CTRL_SHIFT
		| RXPI_GTHE4_MAP_PS_CTRL_INCDEC;
	    state->state = SWEEP_WAIT_LOCK;
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
    if (rx_state.state >= RX_WAIT_SEQ
	&& !(status & RXPI_GTHE4_MAP_STATUS_PHY_READY)) {
	spll_debug(SPLL_DBG_SRC_RAW, SPLL_DBG_SIGNAL_EVENT,
		   SPLL_DBG_EVT_PHY_DOWN, 1);
	phy_dbg("link down\n");
	rx_state.state = RX_RESET;
	regs->ctrl &= ~RXPI_GTHE4_MAP_CTRL_RDY;
    }

    switch (rx_state.state) {
    case RX_RESET: {
	struct wr_endpoint_device* dev = &wrc_endpoint_dev;
	if ((ep_pcs_read(dev, EP_MDIO_MCR) & EP_MDIO_MCR_PDOWN) == 0) {
	    phy_dbg("reset rx\n");
	    regs->reset |= RXPI_GTHE4_MAP_RESET_GTH_RX_PMA_RST;
	    regs->ctrl &= ~RXPI_GTHE4_MAP_CTRL_RDY;
	    tmo_init(&rx_state.timeout, 20 + rx_state.reset_iter);
	    if (rx_state.reset_iter < 30) rx_state.reset_iter++;
	    rx_state.state = RX_WAIT_RESET;
	}
	/* Main pll is not locked. */
	softpll.mpll.rxpi_ready = 0;
	/* Do not run mpll while establishing the link */
	softpll.mpll.link_up = 0;
	break;
    }
    case RX_WAIT_RESET:
	if (tmo_expired(&rx_state.timeout)) {
	    regs->reset &= ~RXPI_GTHE4_MAP_RESET_GTH_RX_PMA_RST;
	    /* Window for the gateware comma-steering FSM (PCS rxslide) to
	       walk the comma to the fixed target tap 0 and assert PHY_READY
	       (worst case ~5 ms incl. buffbypass + CDR relock; 50 ms is
	       ample).  If it never asserts (stuck acquisition, dead peer
	       TX), re-throw with another RX PMA reset as a safety net. */
	    tmo_init(&rx_state.timeout, 50);
	    rx_state.state = RX_WAIT_COMMA;
	}
	break;
    case RX_WAIT_COMMA:
	if (!(status & RXPI_GTHE4_MAP_STATUS_PHY_READY)) {
	    if (tmo_expired(&rx_state.timeout))
		rx_state.state = RX_RESET;   /* re-throw: try another tap */
	}
	else {
	    unsigned bitslide = regs->bitslide;
	    spll_debug(SPLL_DBG_SRC_RAW, SPLL_DBG_SIGNAL_EVENT,
		       SPLL_DBG_EVT_PHY_READY, 1);
	    phy_dbg("comma-aligned:%08x slide:%u\n", status, bitslide);
	    /* bitslide[4:0] is now the REAL PCS slide count (0..19; the UI
	       latency term compensated via the endpoint/PPSi bitslide path).
	       Any value is acceptable - the comma always ends at tap 0.  The
	       old GTX-era "reject odd bitslide" re-throw is GONE: it would
	       reject odd slide counts forever. */
	    {
		struct wr_endpoint_device* dev = &wrc_endpoint_dev;
		unsigned mcr;

		rx_state.state = RX_WAIT_SEQ;
		softpll.mpll.rxpi_ready = 0;
		regs->ctrl = RXPI_GTHE4_MAP_CTRL_RDY;
		rx_state.reset_iter = 0;
		softpll.mpll.link_up = 1;

		/* Restart auto-negotiation now that the PHY has aligned the
		   comma to tap 0.  ep_reset_phy kicked AN once at boot, long
		   before the LPDC comma-steering reached tap 0, so it never
		   completed and the Ethernet link stayed down (SoftPLL locks
		   off the direct tag regardless).  See
		   doc/lpdc-rxpi-phase-matching.md wall #5. */
		mcr = EP_MDIO_MCR_SPEED1000 | EP_MDIO_MCR_FULLDPLX;
		if (dev->flags & EP_DEV_AUTONEG_ENABLED)
		    mcr |= EP_MDIO_MCR_ANENABLE | EP_MDIO_MCR_ANRESTART;
		ep_pcs_write(dev, EP_MDIO_MCR, mcr);
	    }
	}
	break;

    case RX_WAIT_SEQ:
	if (softpll.mode == SPLL_MODE_SLAVE) {
	    if (softpll.seq_state == SEQ_WAIT_MAIN)
		rx_state.state = RX_WAIT_MPLL;
	}
	else if (softpll.seq_state == SEQ_READY) {
	    /* Master: never reaches SEQ_WAIT_MAIN and the mpll never locks,
	       so wait for RX syntonization instead (see RX_WAIT_SYNTON). */
	    if (spll_read_ptracker(0, &rx_state.synt_phase, NULL)) {
		rx_state.synt_cnt = 0;
		tmo_init(&rx_state.timeout, RXPI_SYNTON_CHECK_MS);
		rx_state.state = RX_WAIT_SYNTON;
	    }
	}
	break;

    case RX_WAIT_SYNTON:
	if (tmo_expired(&rx_state.timeout)) {
	    int32_t ph, d;

	    if (!spll_read_ptracker(0, &ph, NULL))
		break;
	    d = ph - rx_state.synt_phase;
	    if (d > RXPI_REF_PERIOD_PS / 2)
		d -= RXPI_REF_PERIOD_PS;
	    if (d < -RXPI_REF_PERIOD_PS / 2)
		d += RXPI_REF_PERIOD_PS;
	    if (d < 0)
		d = -d;
	    rx_state.synt_phase = ph;
	    tmo_init(&rx_state.timeout, RXPI_SYNTON_CHECK_MS);
	    if (d >= RXPI_SYNTON_TOL_PS) {
		rx_state.synt_cnt = 0;
	    }
	    else if (++rx_state.synt_cnt >= RXPI_SYNTON_COUNT) {
		phy_dbg("rx syntonized, start sweep (master)!\n");
		spll_debug(SPLL_DBG_SRC_RAW, SPLL_DBG_SIGNAL_EVENT,
			   SPLL_DBG_EVT_SWEEP_START, 1);
		rxpi_sweep_init(&rx_state.sweep);
		rx_state.state = RX_SWEEP_WAIT;
	    }
	}
	break;

    case RX_WAIT_MPLL:
	if (softpll.mpll.phase_ld.locked) {
	    phy_dbg("phase locked, start sweep!\n");
	    spll_debug(SPLL_DBG_SRC_RAW, SPLL_DBG_SIGNAL_EVENT,
		       SPLL_DBG_EVT_SWEEP_START, 1);
	    rxpi_sweep_init(&rx_state.sweep);
	    rx_state.state = RX_SWEEP_WAIT;
	}
	break;

    case RX_SWEEP_WAIT:
	rxpi_sweep_fsm(&rx_state.sweep);
	if (rx_state.sweep.state == SWEEP_DONE) {
	    spll_debug(SPLL_DBG_SRC_RAW, SPLL_DBG_SIGNAL_EVENT,
		       SPLL_DBG_EVT_SWEEP_DONE, 1);
	    pp_printf("rxpi: set ptrackers[0] offset: 0x%x\n",
		      rx_state.sweep.delta);
	    if (softpll.mode == SPLL_MODE_SLAVE) {
		/* Disable the mpll for atomic changes */
		softpll.mpll.enabled = 0;
		softpll.mpll.phase_shift_current = rx_state.sweep.abs_phase;
		softpll.mpll.phase_shift_target = 0;
		softpll.ptrackers[0].offset = -rx_state.sweep.delta;
		softpll.ptrackers[0].preserve_sign = 1 << 2;
		softpll.ptrackers[0].sign_offset = 0;

		softpll.mpll.rxpi_ready = 1;
		softpll.mpll.enabled = 1;
	    }
	    else {
		/* Master: ONLY calibrate the ptracker zero (used to
		   de-quantize the rx timestamps, esp. t4).  Never touch the
		   mpll - the master's oscillator is the reference and must
		   stay free-running. */
		softpll.ptrackers[0].offset = -rx_state.sweep.delta;
		softpll.ptrackers[0].preserve_sign = 1 << 2;
		softpll.ptrackers[0].sign_offset = 0;
	    }
	    spll_debug(SPLL_DBG_SRC_RAW, SPLL_DBG_SIGNAL_EVENT,
		       SPLL_DBG_EVT_SWEEP_DONE, 1);
	    rx_state.state = RX_READY;
	}
	else if (rx_state.sweep.state == SWEEP_ERROR) {
	    rx_state.state = RX_READY;
	}
	break;
    case RX_READY:
	/* The relock check is slave-only: on a master the mpll is disabled
	   and never "locked", and the calibration stays valid until the next
	   PHY down/reset (handled at the top of this function). */
	if (softpll.mode == SPLL_MODE_SLAVE && !softpll.mpll.phase_ld.locked) {
	    phy_dbg("pll unlocked\n");
	    softpll.mpll.enabled = 0;
	    ld_init((spll_lock_det_t *)&softpll.mpll.phase_ld);
	    ld_init((spll_lock_det_t *)&softpll.mpll.freq_ld);
	    softpll.mpll.rxpi_ready = 0;
	    softpll.mpll.enabled = 1;
	    rx_state.state = RX_WAIT_SEQ;
	}
	break;
    }

    return 1;
}

void phy_calibration_init(void)
{
    rx_state.reset_iter = 0;
    rx_state.state = RX_RESET;
    regs->rxpi_nsamp = 0x7fff;
    regs->rxpi_shift = 0;
}


static const char * const rxpi_cmds[] =
{
	 [0] = "sweep",
};

int cmd_rxpi(const char *args[])
{
	int icmd;

	icmd = sub_cmd(rxpi_cmds, ARRAY_SIZE(rxpi_cmds), args);

	switch (icmd) {
	case 0: {
	    struct sweep_state state;
	    rxpi_sweep_init(&state);
	    if (args[1] && atoi(args[1]))
		state.verbose = 1;
	    while (state.state < SWEEP_DONE)
		rxpi_sweep_fsm(&state);
	    return 0;
	}
	default:
	    return -1;
	}
}
