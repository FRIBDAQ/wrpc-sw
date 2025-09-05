/*
 * This work is part of the White Rabbit project
 *
 * Copyright (C) 2010 - 2013 CERN (www.cern.ch)
 * Author: Tomasz Wlostowski <tomasz.wlostowski@cern.ch>
 *
 * Released according to the GNU GPL, version 2 or any later version.
 */

/* spll_ptracker.h - data structures & prototypes for phase trackers. */

#ifndef __SPLL_PTRACKER_H
#define __SPLL_PTRACKER_H

/* NOTE: Please increment WRPC_SHMEM_VERSION if you change this structure */
struct spll_ptracker_state {
	unsigned char enabled;
	/* True if the result is set */
	unsigned char ready;
	unsigned char dbg_channel;
	/* Id, not really used */
	unsigned char id;
	/* Number of samples to average */
	unsigned n_avg;
	/* Current state. */
	int acc, avg_count, preserve_sign;
	/* Result (phase in dmtd unit) */
	int phase_val;
	int offset;
	int sign_offset;
};

void ptracker_init(struct spll_ptracker_state *s, int id, int num_avgs);
void ptracker_start(struct spll_ptracker_state *s);
void ptrackers_update(struct spll_ptracker_state *ptrackers, int tag, int source);

#endif // __SPLL_PTRACKER_H
