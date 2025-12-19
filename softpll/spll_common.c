/*
 * This work is part of the White Rabbit project
 *
 * Copyright (C) 2010 - 2014 CERN (www.cern.ch)
 * Author: Tomasz Wlostowski <tomasz.wlostowski@cern.ch>
 * Author: Grzegorz Daniluk <grzegorz.daniluk@cern.ch>
 *
 * Released according to the GNU GPL, version 2 or any later version.
 */

/* spll_common.c - common data structures and functions used by the SoftPLL */

#include <string.h>
#include <wrc.h>
#include "softpll_ng.h"

int pi_update(spll_pi_t *pi, int x)
{
	int64_t i_new;
	int y;
	pi->x = x;
	i_new = pi->integrator + (int64_t) pi->ki * x;

	int64_t y_preround = (i_new + (int64_t) x * pi->kp) + ( 1 << (pi->shift - 1) );

	y = ( y_preround >> pi->shift) + pi->bias;

	/* clamping (output has to be in <y_min, y_max>) and
	   anti-windup: stop the integrator if the output is already
	   out of range and the output is going further away from
	   y_min/y_max. */
	if (y < pi->y_min) {
		y = pi->y_min;
		if ((pi->anti_windup && (i_new > pi->integrator))
		    || !pi->anti_windup)
			pi->integrator = i_new;
	} else if (y > pi->y_max) {
		y = pi->y_max;
		if ((pi->anti_windup && (i_new < pi->integrator))
		    || !pi->anti_windup)
			pi->integrator = i_new;
	} else			/* No antiwindup/clamping? */
		pi->integrator = i_new;

	pi->y = y;
	return y;
}

void pi_init(spll_pi_t *pi)
{
	pi->integrator = 0;
	pi->y = pi->bias;
}

/* Lock detector state machine. Takes an error sample (y) and checks
   if it's withing an acceptable range (i.e. <-ld.threshold,
   ld.threshold>. If it has been inside the range for
   (ld.lock_samples) cyckes, the FSM assumes the PLL is locked.
   
   Return value:
   0: PLL not locked
   1: PLL locked
   -1: PLL just got out of lock
 */
int ld_update(spll_lock_det_t *ld, int y)
{
	ld->lock_changed = 0;

	if (abs(y) <= ld->threshold) {
		if (ld->lock_cnt < ld->lock_samples)
			ld->lock_cnt++;

		if (ld->lock_cnt == ld->lock_samples) {
			ld->lock_changed = !ld->locked;
			ld->locked = 1;
			return 1;
		}
	} else {
		if (ld->lock_cnt > ld->delock_samples)
			ld->lock_cnt--;

		if (ld->lock_cnt == ld->delock_samples && ld->locked) {
			ld->lock_cnt = 0;
			ld->lock_changed = ld->locked;
			ld->locked = 0;
			return -1;
		}
	}
	return ld->locked;
}

void ld_init(spll_lock_det_t *ld)
{
	ld->locked = 0;
	ld->lock_cnt = 0;
	ld->lock_changed = 0;
}

/* Enables/disables DDMTD tag generation on a given (channel). 

Channels (0 ... splL_n_chan_ref - 1) are the reference channels
	(e.g. transceivers' RX clocks or a local reference)

Channels (spll_n_chan_ref ... spll_n_chan_out + spll_n_chan_ref-1) are the output
	channels (local voltage controlled oscillators). One output
	(usually the first one) is always used to drive the oscillator
	which produces the reference clock for the transceiver. Other
	outputs can be used to discipline external oscillators
	(e.g. on FMCs).
*/

void spll_enable_tagger(int channel, int enable)
{
	if (channel >= spll_n_chan_ref) {	/* Output channel? */
		if (enable)
			SPLL->OCER |= 1 << (channel - spll_n_chan_ref);
		else
			SPLL->OCER &= ~(1 << (channel - spll_n_chan_ref));
	} else {		/* Reference channel */
		if (enable)
			SPLL->RCER |= 1 << channel;
		else
			SPLL->RCER &= ~(1 << channel);
	}

	pll_verbose("%s: ch %d, OCER 0x%x, RCER 0x%x\n", __FUNCTION__, channel, SPLL->OCER, SPLL->RCER);
}

#if 0

static struct spll_debug_queue_state
{
	int undersample_ratio;
	uint8_t undersample_count[SPLL_DBG_MAX_SOURCES];
	uint8_t undersample_pass[SPLL_DBG_MAX_SOURCES];
	int coalesce_threshold;
} dbg_state;


void spll_debug_queue_configure(int undersample, int coalsesce_threshold)
{
	int dummy, i;
	while (!(SPLL->DFR_HOST_CSR & SPLL_DFR_HOST_CSR_EMPTY))
		{
			dummy = SPLL->DFR_HOST_R0;
			(void) dummy;
		}

	dbg_state.undersample_ratio = undersample;
	dbg_state.coalesce_threshold = coalsesce_threshold * undersample;

	for(i=0;i<SPLL_DBG_MAX_SOURCES;i++)
	{
		dbg_state.undersample_count[i] = 0;
		dbg_state.undersample_pass[i] = 0;
	}
}

int spll_get_debug_queue_samples(uint32_t *buf, int *count)
{
	int n_ents = 0;
	int latch_full = SPLL->DFR_HOST_CSR & SPLL_DFR_HOST_CSR_FULL;
	int latch_count = SPLL_DFR_HOST_CSR_USEDW_R(SPLL->DFR_HOST_CSR);
	struct spll_debug_queue_state *st = &dbg_state;

	/* Return now if not enough samples.  */
	if( !latch_full && latch_count < dbg_state.coalesce_threshold )
	{
		*count = 0;
		return 0;
	}

	while(1)
	{
		if ( SPLL->DFR_HOST_CSR & SPLL_DFR_HOST_CSR_EMPTY )
			break;

		if( n_ents == *count )
			break;

		volatile uint32_t v = SPLL->DFR_HOST_R0;
		int signal = SPLL_DBG_EXTRACT_SIGNAL( v );
		int src = SPLL_DBG_EXTRACT_SOURCE( v );

		if(st->undersample_pass[src] || signal == SPLL_DBG_SIGNAL_EVENT )
		{
			*buf++ = v;
			n_ents ++;
		}

		if( v & 0x80000000 ) // last entry in the record
		{
			st->undersample_count[src]++;
			if (st->undersample_count[src] >= st->undersample_ratio)
			{
				st->undersample_count[src] = 0;
				st->undersample_pass[src] = 1;
			} else {
				st->undersample_pass[src] = 0;
			}
		}

	}

	*count = n_ents;

	if( latch_full )
		return -ENOSPC;

	return 0;
}
#endif

void spll_debug(int src, int what, int value, int last)
{
	uint32_t w = (last ? 0x80000000 : 0) | (value & 0xffffff) | (src << 28) | (what << 24);
	SPLL->DFR_SPLL = w;
}
