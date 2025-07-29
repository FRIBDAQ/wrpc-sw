/*
 * This work is part of the White Rabbit project
 *
 * Released according to the GNU GPL, version 2 or any later version.
 */
#ifndef __WRC_H__
#define __WRC_H__

/*
 * This header includes all generic prototypes that were missing
 * in earlier implementations. For example, the monitor is only
 * one function and doesn't deserve an header of its own.
 * Also, this brings in very common and needed headers
 */
#include <inttypes.h>
#include "dev/syscon.h"
#include "pp-printf.h"
#include "util.h"
#include "trace.h"
#include "wrc-event.h"
#include "wrc-task.h"
#include "wrc-debug.h"

#define sprintf pp_sprintf

/* Allow "if" at C language level, to avoid ifdef */
#ifdef CONFIG_TARGET_WR_SWITCH
#  define is_wr_switch 1
#  define is_wr_node 0
#else
#  define is_wr_switch 0
#  define is_wr_node 1
#endif

#ifdef CONFIG_WR_NODE_SIM
#  define IS_WR_NODE_SIM 1
#else
#  define IS_WR_NODE_SIM 0
#endif

#ifdef CONFIG_IP
#define HAS_IP 1
#else
#define HAS_IP 0
#endif

#ifdef CONFIG_ABSCAL
#define HAS_ABSCAL 1
#else
#define HAS_ABSCAL 0
#endif

#ifdef CONFIG_ETHERBONE
#define HAS_EB 1
#else
#define HAS_EB 0
#endif

#ifdef CONFIG_VLAN
#define HAS_VLANS 1
#else
#define HAS_VLANS 0
#endif


#ifdef CONFIG_CMD_LL
#define HAS_LL 1
#else
#define HAS_LL 0
#endif

#define SCB_VER_SET_A(x) (x<<24)
#define SCB_VER_SET_B(x) (x<<16)
#define SCB_VER_SET_C(x) (x<<8)
#define SCB_VER_SET_D(x) (x)
#define SCB_VER_A(x) ((x>>24) & 0xff)
#define SCB_VER_B(x) ((x>>16) & 0xff)
#define SCB_VER_C(x) ((x>>8) & 0xff)
#define SCB_VER_D(x) ((x) & 0xff)

int wrc_mon_gui(void);
void redraw_gui(void);
int wrc_log_stats(void);
void shell_init(void);
int wrc_wr_diags(void);

/* Default width (in 8ns/16ns units) of the pulses on the PPS output */
#define PPS_WIDTH (10 * 1000 * 1000 / NS_PER_CLOCK) /* 10ms */

/* Init functions and defaults for the wrs build */
int ad9516_init(int scb_ver, int lj_periph_type, int ljd_present);
int ljd_ad9516_init(void);
void rts_init(void);
int rtipc_init(void);
void rts_update(void);
void rtipc_action(void);

int wrc_is_timing_up(void);


#endif /* __WRC_H__ */
