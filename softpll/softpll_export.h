/*
 * This work is part of the White Rabbit project
 *
 * Copyright (C) 2010 - 2013 CERN (www.cern.ch)
 * Author: Tomasz Wlostowski <tomasz.wlostowski@cern.ch>
 *
 * Released according to the GNU GPL, version 2 or any later version.
 */

/* softpll_export.h: public SoftPLL stats API header */

#ifndef __SOFTPLL_EXPORT_H
#define __SOFTPLL_EXPORT_H

#include <stdint.h>


/* SoftPLL operating modes, for mode parameter of spll_init(). */

/* Disabled mode: SoftPLL inactive.
   Previously it was 4, but this was not very intuitive */
#define SPLL_MODE_DISABLED 0

/* Grand Master - lock to 10 MHz external reference */
#define SPLL_MODE_GRAND_MASTER 1

/* Free running master - 125 MHz refrence free running, DDMTD locked to it */
#define SPLL_MODE_FREE_RUNNING_MASTER 2

/* Slave mode - 125 MHz reference locked to one of the input clocks */
#define SPLL_MODE_SLAVE 3

#define SEQ_DISABLED 7
#define SEQ_CLEAR_DACS 9
#define SEQ_WAIT_CLEAR_DACS 10
#define SEQ_WAIT_EXT 2
#define SEQ_WAIT_HELPER 4
#define SEQ_WAIT_MAIN 6
#define SEQ_READY 8

#define AUX_DISABLED 1
#define AUX_LOCK_PLL 2
#define AUX_ALIGN_PHASE 3
#define AUX_SLAVE_READY 4
#define AUX_WAIT_MONITOR_LOCK 5
#define AUX_MONITOR_READY 6

#define ALIGN_STATE_EXT_OFF 0
#define ALIGN_STATE_WAIT_CLKIN 9
#define ALIGN_STATE_WAIT_PLOCK 10
#define ALIGN_STATE_START 1
#define ALIGN_STATE_START_MAIN 8
#define ALIGN_STATE_INIT_CSYNC 2
#define ALIGN_STATE_WAIT_CSYNC 3
#define ALIGN_STATE_START_ALIGNMENT 7
#define ALIGN_STATE_WAIT_SAMPLE 4
#define ALIGN_STATE_COMPENSATE_DELAY 5
#define ALIGN_STATE_LOCKED 6

#define SPLL_STATS_VER 7
#define SPLL_STATS_MAGIC 0x5b1157a7
#define SPLL_LOOP_HELPER -1
#define SPLL_LOOP_MAIN 0

/*
 * Low Jitter capabilities corner
 */
#define PERIPH_WRS_STD_NO_LJ    0
#define PERIPH_WRS_STD_WITH_LJD 1
#define PERIPH_WRS_FL_SYNCTECH  2
#define PERIPH_WRS_LJ_SAFRAN    3
#define PERIPH_END              4

/* OSC_FREQ allows to recognize frequency of oscillator on LJD and
 * also the fact that LJ functionality is intgrated */
#define OSC_FREQ_10M        0x0
#define OSC_FREQ_20M        0x1
#define OSC_FREQ_25M        0x2
#define OSC_FREQ_50M        0x3
#define OSC_FREQ_100M       0x4
#define OSC_FREQ_WRS_LJ_INT 0x7

/* When Low Jitter functionlity integrated, periph_ID allows to detect
 * the switch type */
#define PERIPH_ID_WRS_FL_SYNCTECHv1_0 0x7
#define PERIPH_ID_WRS_FL_SYNCTECHv1_5 0x6
#define PERIPH_ID_WRS_LJ_SAFRAN       0x5

struct spll_build_id {
	char commit_id[32];
	char build_date[16];
	char build_time[16];
	char build_by[32];
};

/* info reported through .stat section */
/* due to endiannes problem strings has to be 4 bytes alligned */
struct spll_stats {
	int magic;	/* 0x5b1157a7 = SPLLSTAT ?;)*/
	int ver;	/* version of the structure */
	int sequence;	/* sequence number, so the host can retry */
	int mode;
	int irq_cnt;
	int seq_state;
	int align_state;
	int H_lock;
	int M_lock;
	int H_y, M_y;
	int del_cnt;
	int start_cnt;
	struct spll_build_id build_id;
	int ext_pps_latency_ps;
	int main_pll_kp;
	int main_pll_ki;
	int helper_pll_kp;
	int helper_pll_ki;
	int ljd_present;
	int lj_periph_id;
	int lj_osc_freq_type;
	int lj_wrs_type;
};

extern struct spll_stats *stats;
extern const struct spll_build_id build_id;

#endif /* __SOFTPLL_EXPORT_H */

