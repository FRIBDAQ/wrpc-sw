/*
 * This work is part of the White Rabbit project
 *
 * Copyright (C) 2023 Nikhef (www.Nikhef.nl)
 * Author: Peter Jansweijer <peterj@nikhef.nl> based on work
 * from Tomasz Wlostowski <tomasz.wlostowski@cern.ch>
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

/*
    LPDC PHY Calibration code.
    Specific for Artix UltraScale+ devices using wr_gthe4_phy_family7_lp
    (based on CPLL)
*/

#include <string.h>
#include "board.h"
#include "dev/syscon.h"
#include "dev/endpoint.h"
#include "softpll_ng.h"
#include "storage.h"
#include "util.h"
#include "wrc-debug.h"
#include "wrc-task.h"
#include "lpdc.h"

#include "hw/ep_mdio_regs.h"
#include "hw/lpdc_mdio_regs.h"

/* TX Target phase is measured at tx_out_clk of the PHY.
   Clk_ref_62m5 and tx_out_clk are phase locked but have an offset.
   Add a safe offset such that the TxData and TxK (clk_ref_62m5 domain)
   are safely clocked into the PHY (tx_out_clk domain).
   set tx_out_clk 4 ns before clk_ref_62m5 so there is 12 ns setup time.
*/
#define LPDC_COARSE_PHASE_MIN_PS 11750   /* ps */
#define LPDC_COARSE_PHASE_MAX_PS 12250   /* ps */
#define LPDC_FINE_PHASE_TOLLERANCE_PS 50 /* ps */
#define LPDC_MAX_ATTEMPS_TX_SETUP_STATE_RESET_PCS 2000
#define LPDC_MAX_ATTEMPS_RX_SETUP_STATE_RESET_PCS 100

// number of raw DDMTD phase samples used to measure the RX/TX clock phases
#define LPDC_NUM_PTRACKER_SAMPLES 10 

#define LPDC_TARGET_COMMA_POS 0

#define LPDC_MDIO_CTRL_DMTD_SOURCE_TXOUTCLK (1 << LPDC_MDIO_CTRL_DMTD_CLK_SEL_SHIFT)
#define LPDC_MDIO_CTRL_DMTD_SOURCE_RXRECCLK (0 << LPDC_MDIO_CTRL_DMTD_CLK_SEL_SHIFT)

#define TX_SETUP_STATE_START 0
#define TX_SETUP_STATE_RESET_PCS 1
#define TX_SETUP_STATE_WAIT_TX_PLL_LOCK 2
#define TX_SETUP_STATE_MEASURE_PHASE 3
#define TX_SETUP_DONE 4
#define TX_SETUP_VALIDATE 5
#define TX_SETUP_STATE_DISABLED 6
#define TX_SETUP_STATE_WAIT_SPLL_LOCK 7
#define TX_SETUP_STATE_WAIT_TX_CLK_STABILIZE 8

#define RX_SETUP_STATE_INIT 0
#define RX_SETUP_STATE_RESET_PCS 1
#define RX_SETUP_STATE_WAIT_LOCK 2
#define RX_SETUP_STATE_MEASURE_PHASE 3
#define RX_SETUP_DONE 4
#define RX_SETUP_VALIDATE 5
#define RX_SETUP_STATE_DISABLED 6

#define FSM_DEBUG_REFRESH_PERIOD_MS 1000
#define FSM_PHY_LOCK_TIMEOUT_MS 1000
#define FSM_SPLL_LOCK_TIMEOUT_MS 10000
#define FSM_DMTD_TIMEOUT_MS 100
#define FSM_EARLY_LINK_UP_TIMEOUT_MS 100
#define FSM_STABILIZE_TIMEOUT_MS 100

struct wrc_port_tx_setup_state
{
    int state;
    int cnt;
    int attempts;
    int cal_saved_phase;
    int cal_saved_phase_valid;
    int cal_file_updated;
    int measured_phase;
    int expected_phase;
    int tollerance;
    int update_cnt;
    int expected_phase_valid;
    timeout_t phy_lock_timeout;
    timeout_t spll_lock_timeout;
    timeout_t dmtd_timeout;
};

struct wrc_port_rx_setup_state
{
    int cpos_stat[20];
    int state;
    int attempts;
    int prev_link_up;
    timeout_t link_timeout;
    timeout_t stabilize_timeout;
};

struct wrc_lpdc_state
{
    struct wrc_port_tx_setup_state tx_state;
    struct wrc_port_rx_setup_state rx_state;
    struct wr_endpoint_device *endpoint;
};

static inline void mdio_lpdc_write(struct wrc_lpdc_state *lpdc, int location, uint16_t value)
{
    ep_pcs_write(lpdc->endpoint, location + EP_MDIO_PHY_SPECIFIC_REGS, value);
}

static inline uint16_t mdio_lpdc_read(struct wrc_lpdc_state *lpdc, int location)
{
    return ep_pcs_read(lpdc->endpoint, location + EP_MDIO_PHY_SPECIFIC_REGS);
}

static void mdio_lpdc_set_bits( struct wrc_lpdc_state *lpdc, uint16_t reg, uint16_t mask )
{
    uint16_t rdbk = mdio_lpdc_read( lpdc, reg );
    rdbk |= mask;
    mdio_lpdc_write( lpdc, reg, rdbk );
}

static void mdio_lpdc_clear_bits( struct wrc_lpdc_state *lpdc, uint16_t reg, uint16_t mask )
{
    uint16_t rdbk = mdio_lpdc_read( lpdc, reg );
    rdbk &= ~mask;
    mdio_lpdc_write( lpdc, reg, rdbk );
}


static void tx_fsm_init(struct wrc_port_tx_setup_state *fsm)
{
    fsm->attempts = 0;
    fsm->state = TX_SETUP_STATE_START;
    fsm->expected_phase = 0;
    fsm->expected_phase_valid = 0;
    fsm->tollerance = 300;
    fsm->update_cnt = 0;
    fsm->cal_saved_phase_valid = 0;
    fsm->cal_saved_phase = 0;
    fsm->cal_file_updated = 0;
    fsm->cnt = 0;

    /* FIXME: is cal_saved_phase unsigned? uint32_t? declare it so
     * at the wrc_port_tx_setup_state structure */
    if( !storage_get_calibration_parameter( CAL_PARAM_PHY_TARGET_TX_PHASE, (uint32_t *)&fsm->cal_saved_phase )
	&& fsm->cal_saved_phase != -1)
    {
        phy_dbg("[lpdc] TX target phase from calibration data: %d ps\n", fsm->cal_saved_phase);
        fsm->cal_saved_phase_valid = 1;
    }
}

static int tx_fsm_update(struct wrc_lpdc_state *lpdc)
{
    struct wrc_port_tx_setup_state *fsm = &lpdc->tx_state;

    switch (fsm->state)
    {
        case TX_SETUP_STATE_START:
        {
            spll_init( SPLL_MODE_FREE_RUNNING_MASTER, 0, 0 );
            spll_set_ptracker_average_samples( 0, LPDC_NUM_PTRACKER_SAMPLES );
            tmo_init(&fsm->spll_lock_timeout, FSM_SPLL_LOCK_TIMEOUT_MS);
            ep_sfp_enable( lpdc->endpoint, 0 );
            fsm->state = TX_SETUP_STATE_WAIT_SPLL_LOCK;
            break;
        }

        case TX_SETUP_STATE_WAIT_SPLL_LOCK:
        {
            if( tmo_expired( &fsm->spll_lock_timeout ) )
            {
                phy_dbg("[lpdc] can't lock the SoftPLL. This is necessary for PHY calibration to continue. Retrying...\n");
                tmo_restart( &fsm->spll_lock_timeout );
                fsm->state = TX_SETUP_STATE_START;
                break;
            }
           
            if( spll_check_lock( 0 ) )
            {
                spll_enable_ptracker(0, 0);

                mdio_lpdc_set_bits( lpdc, LPDC_MDIO_CTRL, LPDC_MDIO_CTRL_RX_SW_RESET | LPDC_MDIO_CTRL_DMTD_SOURCE_TXOUTCLK );
                fsm->state = TX_SETUP_STATE_RESET_PCS;

                phy_dbg("[lpdc] SPLL locked\n");
            }

            break;
        }

        case TX_SETUP_STATE_RESET_PCS:
        {
            //phy_dbg("[lpdc] TX reset PCS\n");

            spll_enable_ptracker(0, 0);

            // No CPLL reset needed. CPLL reset is already included in tx_sw_reset => gtwiz_reset_all_in.
            // Reset TX path: tx_sw_reset => gtwiz_reset_all_in
            mdio_lpdc_set_bits( lpdc, LPDC_MDIO_CTRL, LPDC_MDIO_CTRL_TX_SW_RESET );
            usleep(2);

            // Un-reset TX path
            mdio_lpdc_clear_bits( lpdc, LPDC_MDIO_CTRL, LPDC_MDIO_CTRL_TX_SW_RESET );

            // GTHE4 tx_sw_reset to tx_rst_done ~ 2.5 ms
            //usleep(100);

            tmo_init( &fsm->phy_lock_timeout, FSM_PHY_LOCK_TIMEOUT_MS );
            fsm->state = TX_SETUP_STATE_WAIT_TX_PLL_LOCK;
      
            break;
        }

    case TX_SETUP_STATE_WAIT_TX_PLL_LOCK:
    {
        uint32_t stat = mdio_lpdc_read(lpdc, LPDC_MDIO_STAT);
        if (stat & LPDC_MDIO_STAT_TX_RST_DONE)
        {
            fsm->attempts++;
            fsm->state = TX_SETUP_STATE_WAIT_TX_CLK_STABILIZE;
            tmo_init(&fsm->phy_lock_timeout, 10 );
        }
        else if( tmo_expired(&fsm->phy_lock_timeout) )
        {
            fsm->state = TX_SETUP_STATE_RESET_PCS;
            phy_dbg("PHY PLL lock timeout, retrying...[LPDC_STAT=0x%04x]\n", stat );
        }
        break;
    }

    case TX_SETUP_STATE_WAIT_TX_CLK_STABILIZE:
    {
        if( !tmo_expired( &fsm->phy_lock_timeout ) )
            return 0;

        spll_set_ptracker_average_samples( 0, LPDC_NUM_PTRACKER_SAMPLES );
        spll_enable_ptracker(0, 1);
        tmo_init( &fsm->dmtd_timeout, FSM_DMTD_TIMEOUT_MS );
        fsm->state = TX_SETUP_STATE_MEASURE_PHASE;
        break;
    }

    case TX_SETUP_STATE_MEASURE_PHASE:
    {
        int32_t phase;
        int enabled; //, p2;
        int rv = spll_read_ptracker(0, &phase, &enabled);

 
        if (!rv)
        {
            if( tmo_expired( &fsm->dmtd_timeout ) )
            {
                phy_dbg("[lpdc] TX Phase measurement timeout, retrying...\n");
                fsm->state = TX_SETUP_STATE_RESET_PCS;
            }
            return 0;
        }

        if (!fsm->expected_phase_valid)
        {
            if ( fsm->cal_saved_phase_valid )
            {
                if ( within_range( fsm->cal_saved_phase, LPDC_COARSE_PHASE_MIN_PS, LPDC_COARSE_PHASE_MAX_PS, 16000 ) )
                {
                    fsm->expected_phase = fsm->cal_saved_phase;
                    fsm->tollerance = LPDC_FINE_PHASE_TOLLERANCE_PS;
                    phy_dbg("[lpdc] Using the previous phase setpoint = %d ps as the target with tollerance = %d ps\n",
                             fsm->cal_saved_phase,
                             fsm->tollerance);
                } else {
                    fsm->expected_phase = (LPDC_COARSE_PHASE_MAX_PS + LPDC_COARSE_PHASE_MIN_PS) / 2;
                    fsm->tollerance = (LPDC_COARSE_PHASE_MAX_PS - LPDC_COARSE_PHASE_MIN_PS) / 2;
                    fsm->cal_saved_phase_valid = 0;
                    phy_dbg("[lpdc] Previous phase setpoint (%d ps) out of range. Old calibration algorithm? Restarting from scratch.\n", fsm->cal_saved_phase);
                }
            }
            else // find a sane default
            {
                fsm->expected_phase = (LPDC_COARSE_PHASE_MAX_PS + LPDC_COARSE_PHASE_MIN_PS) / 2;
                fsm->tollerance = (LPDC_COARSE_PHASE_MAX_PS - LPDC_COARSE_PHASE_MIN_PS) / 2;
                phy_dbg("[lpdc] No LPDC TX Calibration data found. Restarting from scratch.\n" );
            }
            fsm->expected_phase_valid = 1;
        }

        int phase_min = fsm->expected_phase - fsm->tollerance;
        int phase_max = fsm->expected_phase + fsm->tollerance;
        phy_dbg("[lpdc] TX phase = %d ps\n", phase);

        if (within_range(phase, phase_min, phase_max, 16000))
        {
            fsm->measured_phase = phase;
            //phy_dbg("[lpdc] Fix phase = %d ps\n", fsm->measured_phase );
            fsm->state = TX_SETUP_VALIDATE;
        }
        else
        {
            if (fsm->attempts >= LPDC_MAX_ATTEMPS_TX_SETUP_STATE_RESET_PCS)
            {
                phy_dbg("[lpdc] No proper TX phase found at %d ps after %d attempts. Clear stored PHY_TARGET_TX_PHASE.\n", fsm->cal_saved_phase, fsm->attempts);
                phy_dbg("[lpdc] Old calibration due to new gateware?\n");
                //storage_remove_calibration_parameter(CAL_PARAM_PHY_TARGET_TX_PHASE);
                //storage_save_calibration();
                fsm->attempts = 0;
                fsm->cal_saved_phase_valid = 0;
                fsm->expected_phase_valid = 0;
            }
            else
               fsm->state = TX_SETUP_STATE_RESET_PCS;
        }

        break;
    }

    case TX_SETUP_VALIDATE:
    {
        phy_dbg("[lpdc] TX calibration complete (phase %d ps, after %d attempts)\n", fsm->measured_phase, fsm->attempts);
        spll_enable_ptracker(0, 0);

        // enable the PCS+SFP on the port
        mdio_lpdc_write( lpdc, LPDC_MDIO_CTRL, LPDC_MDIO_CTRL_TX_ENABLE | LPDC_MDIO_CTRL_DMTD_SOURCE_RXRECCLK );
        ep_sfp_enable( lpdc->endpoint, 1 );

        if( !fsm->cal_saved_phase_valid )
        {
            phy_dbg("[lpdc] Saving established target phase as calibration parameter: %d ps\n", fsm->measured_phase);
            storage_set_calibration_parameter_and_save( CAL_PARAM_PHY_TARGET_TX_PHASE, fsm->measured_phase);
        }

        fsm->state = TX_SETUP_DONE;
        break;
    }

    case TX_SETUP_DONE:
    {
        return 1;
        break;
    }
    }

    return 0;
}


static void rx_fsm_init(struct wrc_port_rx_setup_state* fsm)
{
    fsm->attempts = 0;
    fsm->state = RX_SETUP_STATE_INIT;
    fsm->prev_link_up = 0;
    memset(fsm->cpos_stat, 0, sizeof(fsm->cpos_stat ));
}

static int rx_fsm_update(struct wrc_lpdc_state *lpdc)
{
    struct wrc_port_rx_setup_state* fsm = &lpdc->rx_state;
    struct wrc_port_tx_setup_state* fsm_tx = &lpdc->tx_state;

    uint16_t lpc_stat = mdio_lpdc_read(lpdc, LPDC_MDIO_STAT);
    int early_link_up =  lpc_stat & LPDC_MDIO_STAT_LINK_UP;

    if( fsm_tx->state != TX_SETUP_DONE )
    {
        fsm->state = RX_SETUP_STATE_INIT;
        return 0;
    }

        switch( fsm->state )
    {
        case RX_SETUP_STATE_INIT:
        {
            mdio_lpdc_set_bits( lpdc, LPDC_MDIO_CTRL, LPDC_TARGET_COMMA_POS );

            if (early_link_up) {
                if ( fsm_tx->state == TX_SETUP_DONE )
                {
                    phy_dbg("[lpdc] RX calibration started.\n");
    
                    fsm->state = RX_SETUP_STATE_RESET_PCS;
                }
            }

            fsm->attempts = 0;

            break;
        }

        case RX_SETUP_STATE_RESET_PCS:
        {
            if (early_link_up)
            {

                fsm->state = RX_SETUP_STATE_WAIT_LOCK;

                // Reset RX path
                mdio_lpdc_set_bits( lpdc, LPDC_MDIO_CTRL, LPDC_MDIO_CTRL_RX_SW_RESET );
                usleep(1);
                // Un-Reset RX path
                mdio_lpdc_clear_bits( lpdc, LPDC_MDIO_CTRL, LPDC_MDIO_CTRL_RX_SW_RESET );

                usleep(10000);
                fsm->attempts++;

                tmo_init(&fsm->link_timeout, FSM_EARLY_LINK_UP_TIMEOUT_MS);
            }

            break;
        }

        case RX_SETUP_STATE_WAIT_LOCK:
        {
            int rx_aligned = lpc_stat & LPDC_MDIO_STAT_LINK_ALIGNED;

            if ( tmo_expired(&fsm->link_timeout) && !early_link_up) {
                fsm->state = RX_SETUP_STATE_INIT;
            }
            else 
            {
		if ( !early_link_up )
		    return 0;

                if( rx_aligned )
                {
                    fsm->state = RX_SETUP_VALIDATE;
                    tmo_init( &fsm->stabilize_timeout, FSM_STABILIZE_TIMEOUT_MS );
                } else {
                    // In rare occasions the comma can't be found at the proper tap, even after
                    // multiple PCS resets. In such a case a full GTHE4 reset is needed.
                    if (fsm-> attempts % LPDC_MAX_ATTEMPS_RX_SETUP_STATE_RESET_PCS == 0) {
                        phy_dbg("[lpdc] No RX calibration yet... Restarting TX calibration from scratch.\n");
                        fsm->state = RX_SETUP_STATE_RESET_PCS;
                        tx_fsm_init(&lpdc->tx_state);
                    } else {
                        fsm->state = RX_SETUP_STATE_RESET_PCS;
                    }
                }
            }
            break;
        }

        case RX_SETUP_VALIDATE:
        {
            if( !tmo_expired( &fsm->stabilize_timeout ))
                return 0;

            int rx_aligned = lpc_stat & LPDC_MDIO_STAT_LINK_ALIGNED;
            int rx_comma_pos = (lpc_stat & LPDC_MDIO_STAT_COMMA_CURRENT_POS_MASK) > LPDC_MDIO_STAT_COMMA_CURRENT_POS_SHIFT;
            int rx_comma_valid = lpc_stat & LPDC_MDIO_STAT_COMMA_POS_VALID;

            if ( early_link_up && rx_aligned && rx_comma_valid && (rx_comma_pos == LPDC_TARGET_COMMA_POS) )
            {
                mdio_lpdc_set_bits( lpdc, LPDC_MDIO_CTRL, LPDC_MDIO_CTRL_RX_ENABLE );
                ep_pcs_write(lpdc->endpoint, EP_MDIO_MCR, EP_MDIO_MCR_SPEED1000 | EP_MDIO_MCR_FULLDPLX | EP_MDIO_MCR_ANENABLE | EP_MDIO_MCR_ANRESTART);
                phy_dbg("[lpdc] RX calibration complete (after %d attempts) comma @ %d taps.\n", fsm->attempts, rx_comma_pos );
                spll_enable_ptracker( 0, 0 );
                spll_set_ptracker_average_samples( 0, PTRACKER_AVERAGE_SAMPLES );

                   fsm->state = RX_SETUP_DONE;

            } else {
                //phy_dbg("[lpdc] Stabilize link... Do nothing but wait\n");
                //fsm->state = RX_SETUP_STATE_RESET_PCS;
            }

            break;
        }

        case RX_SETUP_DONE:
        {

            int link_up = ep_link_up(&wrc_endpoint_dev, NULL);

            if( !early_link_up /*|| ( ( fsm->prev_link_up && !link_up ) )*/ )
            {
                phy_dbg("port went down, need RX recalibration.\n");
                fsm->state = RX_SETUP_STATE_INIT;
                fsm->prev_link_up = link_up;
                return 0;
            }

            fsm->prev_link_up = link_up;
            return 1;
            break;
        }

        default:
        break;
    }

    return 0;
}

static struct wrc_lpdc_state lpdc;

int phy_calibration_poll(void)
{
    tx_fsm_update(&lpdc);
    rx_fsm_update(&lpdc);
    return 1;
}

void phy_calibration_init(void)
{
    // fixme: do we want more than one in the WRC? maybe soon...
    lpdc.endpoint = &wrc_endpoint_dev;

    phy_dbg("[lpdc] Initializing PHY calibrator...\n");
    ep_pcs_write(lpdc.endpoint, EP_MDIO_MCR, EP_MDIO_MCR_PDOWN);	/* reset the PHY */
    timer_delay_ms(200);
    ep_pcs_write(lpdc.endpoint, EP_MDIO_MCR, EP_MDIO_MCR_RESET);	/* reset the PHY */
    ep_pcs_write(lpdc.endpoint, EP_MDIO_MCR, 0);	                /* reset the PHY */

    mdio_lpdc_write( &lpdc, LPDC_MDIO_CTRL, 0 );
    mdio_lpdc_write( &lpdc, LPDC_MDIO_CTRL2, 0 );

    tx_fsm_init(&lpdc.tx_state);
    rx_fsm_init(&lpdc.rx_state);
}

void phy_calibration_disable(void)
{
    lpdc.tx_state.state = TX_SETUP_STATE_DISABLED;
    lpdc.rx_state.state = RX_SETUP_STATE_DISABLED;
}
