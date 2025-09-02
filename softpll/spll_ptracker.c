/*
 * This work is part of the White Rabbit project
 *
 * Copyright (C) 2010 - 2013 CERN (www.cern.ch)
 * Author: Tomasz Wlostowski <tomasz.wlostowski@cern.ch>
 *
 * Released according to the GNU GPL, version 2 or any later version.
 */

/* spll_ptracker.c - implementation of phase trackers. */

#include "softpll_ng.h"

static int tag_ref = -1;

extern int reverse_spll;


void ptracker_init(struct spll_ptracker_state *s, int id, int num_avgs)
{
	s->id = id;
	s->ready = 0;
	s->n_avg = num_avgs;
	s->acc = 0;
	s->avg_count = 0;
	s->enabled = 0;
	s->dbg_channel = -1;
	s->offset = 0;
}

void ptracker_start(struct spll_ptracker_state *s)
{
	s->preserve_sign = 0;
	s->enabled = 1;
	s->ready = 0;
	s->acc = 0;
	s->avg_count = 0;

	spll_enable_tagger(s->id, 1);
	spll_enable_tagger(MAIN_CHANNEL, 1);
}

#undef HPLL_N
#define HPLL_N 22

/* The phase of a ref_clk period (16ns) ~= 1_310_720 */
#define PHASE_MAX ((16000 / 200) << 14)

void ptrackers_update(struct spll_ptracker_state *ptrackers, int tag,
		      int source)
{
	/* Adjustment for wrap-arounds.  */
	static const int adj_tab[16] = {
		/* psign */
		/* 0   - 1/4   */  0, 0, 0, -(1<<HPLL_N),
		/* 1/4 - 1/2  */   0, 0, 0, 0,
		/* 1/2 - 3/4  */   0, 0, 0, 0,
		/* 3/4 - 1   */    (1<<HPLL_N), 0, 0, 0};

	if(source == MAIN_CHANNEL)
	{
		tag_ref = tag;
		return;
	}


	register struct spll_ptracker_state *s = ptrackers + source;

	if(!s->enabled)
		return;

	register int delta;
	if (!reverse_spll)
		delta = (tag_ref - tag) & ((1 << HPLL_N) - 1);
	else
		delta = (tag - tag_ref) & ((1 << HPLL_N) - 1);

	register int index = delta >> (HPLL_N - 2);

	if( s->dbg_channel >= 0 )
	{
		spll_debug( SPLL_DBG_SRC_AUX(s->dbg_channel), SPLL_DBG_SIGNAL_ERR, tag_ref-tag, 1);
	}

	if (s->avg_count == 0) {
		/* hack: two since PTRACK_WRAP_LO/HI are in 1/4 and 3/4 of the scale,
		   we can use the two MSBs of delta and a trivial LUT instead, removing 2 branches */
		s->preserve_sign = index << 2;
		s->acc = delta;
		s->avg_count ++;
	} else {

		/* same hack again, using another lookup table to adjust for wraparound */
		s->acc += delta + adj_tab[ index + s->preserve_sign ];
		s->avg_count ++;

		if (s->avg_count == s->n_avg) {
			int avg = s->acc / s->n_avg;
			int phase = avg + s->offset;
			/* Keep the phase within 1 ref_clk period, as it is
			   used as fine grain offset for RX timestamp */
			if (phase >= PHASE_MAX) {
				/* Two possibilities */
				if (s->offset > -PHASE_MAX
				    && avg >= (1 << HPLL_N) - PHASE_MAX) {
					/* The phase rollover to the max */
					phase -= (1 << HPLL_N);
					s->offset -= (1 << HPLL_N);
				}
				else {
					/* The phase slowly increase and become
					   greather than PHASE_MAX */
					phase -= PHASE_MAX;
					s->offset -= PHASE_MAX;
				}
			}
			else if (phase < 0) {
				/* Again, two possibilities */
				if (s->offset < -(1 << HPLL_N) + PHASE_MAX
				    && avg < PHASE_MAX) {
					/* The phase rollover to the min */
					phase += (1 << HPLL_N);
					s->offset += (1 << HPLL_N);
				}
				else {
					/* The phase slowly decreased and
					   became < 0 */
					phase += PHASE_MAX;
					s->offset += PHASE_MAX;
				}
			}
			else
				s->ready = 1;
			s->phase_val = phase;
			s->acc = 0;
			s->avg_count = 0;
		}
	}
}
