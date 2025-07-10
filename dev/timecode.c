/*
 * This work is part of the White Rabbit project
 *
 * Copyright (C) 2024 CERN (www.cern.ch)
 * Author: Harvey Leicester <harvey.leicester@cern.ch>
 *
 * Released according to the GNU GPL, version 2 or any later version.
 */

#include <dev/syscon.h>
#include <wrc.h>
#include "dev/timecode.h"
#include "wrpc.h"
#include "dev/pps_gen.h"
#include "board.h"

#define TIMECODE ((volatile struct timecode *)(BASE_TIMECODE))

/* TODO
 * handle leap second events
 */

static int timecode_next_sec(int *time)
{

  static uint64_t prev_time = 0;
  uint64_t curr_time = 0;
  shw_pps_gen_get_time(&curr_time, NULL);
  if(curr_time != prev_time){
    prev_time = curr_time;
    *time = curr_time+1;
    return 1;
  }
  return 0;
}

void timecode_get_tc(struct wr_timecode_fields *tc)
{

  tc->sec       = (TIMECODE->CURR.UTC_MDHMS & TIMECODE_CURR_UTC_MDHMS_SECOND_MASK) >> TIMECODE_CURR_UTC_MDHMS_SECOND_SHIFT;
  tc->min       = (TIMECODE->CURR.UTC_MDHMS & TIMECODE_CURR_UTC_MDHMS_MINUTE_MASK) >> TIMECODE_CURR_UTC_MDHMS_MINUTE_SHIFT;
  tc->hour      = (TIMECODE->CURR.UTC_MDHMS & TIMECODE_CURR_UTC_MDHMS_UTC_HR_MASK) >> TIMECODE_CURR_UTC_MDHMS_UTC_HR_SHIFT;
  tc->sbs       = (TIMECODE->CURR.UTC_SBS   & TIMECODE_CURR_UTC_SBS_UTC_SBS_MASK) >> TIMECODE_CURR_UTC_SBS_UTC_SBS_SHIFT;
  tc->day       = (TIMECODE->CURR.UTC_MDHMS & TIMECODE_CURR_UTC_MDHMS_UTC_DAY_MASK) >> TIMECODE_CURR_UTC_MDHMS_UTC_DAY_SHIFT;
  tc->month     = (TIMECODE->CURR.UTC_MDHMS & TIMECODE_CURR_UTC_MDHMS_UTC_MON_MASK) >> TIMECODE_CURR_UTC_MDHMS_UTC_MON_SHIFT;
  tc->year      = (TIMECODE->CURR.UTC_Y & TIMECODE_CURR_UTC_Y_UTC_YEAR_MASK) >> TIMECODE_CURR_UTC_Y_UTC_YEAR_SHIFT;
  tc->diy       = (TIMECODE->CURR.UTC_Y & TIMECODE_CURR_UTC_Y_UTC_DIY_MASK) >> TIMECODE_CURR_UTC_Y_UTC_DIY_SHIFT;
  tc->ls_val    = (TIMECODE->CURR.LEAP_SEC & TIMECODE_CURR_LEAP_SEC_LS_VALUE_MASK) >> TIMECODE_CURR_LEAP_SEC_LS_VALUE_SHIFT;
  tc->ls_flag59 = (TIMECODE->CURR.LEAP_SEC & TIMECODE_CURR_LEAP_SEC_LS_FLAG59) ? 1 : 0;
  tc->ls_flag61 = (TIMECODE->CURR.LEAP_SEC & TIMECODE_CURR_LEAP_SEC_LS_FLAG61) ? 1 : 0;
  tc->ls_valid  = (TIMECODE->CURR.LEAP_SEC & TIMECODE_CURR_LEAP_SEC_LS_VALID) ? 1 : 0;
  tc->utc_valid = (TIMECODE->SR & TIMECODE_SR_VALID) ? 1 : 0;
}

int timecode_get_sel(void)
{

  uint32_t sel = (TIMECODE->CR & TIMECODE_CR_SERDES_IP_SEL_MASK) >> TIMECODE_CR_SERDES_IP_SEL_SHIFT;
  if(sel == TIMECODE_SEL_CLK)
    return TIMECODE_SEL_CLK;
  if(sel == TIMECODE_SEL_NMEA)
    return TIMECODE_SEL_NMEA;
  if(sel == TIMECODE_SEL_IRIG)
    return TIMECODE_SEL_IRIG;

  return 0;
}

int timecode_is_enabled(uint32_t ip)
{

  uint32_t sr = TIMECODE->SR;
  switch(ip){
    case TIMECODE_SEL_CLK:
      return (sr & TIMECODE_SR_AUXCLK_ENABLED ? 1 : 0);
    case TIMECODE_SEL_NMEA:
      return (sr & TIMECODE_SR_NMEA_ENABLED ? 1 : 0);
    case TIMECODE_SEL_IRIG:
      return (sr & TIMECODE_SR_IRIG_ENABLED ? 1 : 0);
    default:
      break;
  }
  return 0;
}

int timecode_sel(uint32_t ip)
{

  switch(ip){
  case TIMECODE_SEL_CLK:
    if(timecode_is_enabled(TIMECODE_SEL_CLK)){
      TIMECODE->CR = (TIMECODE->CR & ~TIMECODE_CR_SERDES_IP_SEL_MASK) | (TIMECODE_SEL_CLK << TIMECODE_CR_SERDES_IP_SEL_SHIFT);
      return 1;
    }
    break;
  case TIMECODE_SEL_NMEA:
    if(timecode_is_enabled(TIMECODE_SEL_NMEA)){
      TIMECODE->CR = (TIMECODE->CR & ~TIMECODE_CR_SERDES_IP_SEL_MASK) | (TIMECODE_SEL_NMEA << TIMECODE_CR_SERDES_IP_SEL_SHIFT);
      return 1;
    }
    break;
  case TIMECODE_SEL_IRIG:
    if(timecode_is_enabled(TIMECODE_SEL_IRIG)){
      TIMECODE->CR = (TIMECODE->CR & ~TIMECODE_CR_SERDES_IP_SEL_MASK) | (TIMECODE_SEL_IRIG << TIMECODE_CR_SERDES_IP_SEL_SHIFT);
      return 1;
    }
    break;
  default:
    break;
  }
  return 0;
}

int timecode_update(void)
{
  int time, year, month, day, hour, min, sec, diy, sbs;
  int ls_ptp, ls_sys;

  if(timecode_next_sec(&time)){

    TIMECODE->CR &= ~TIMECODE_CR_VALID;
    format_time_int(time, &year, &month, &day, &hour, &min, &sec, &sbs, &diy);
    wrc_ptp_get_leapsec(&ls_ptp, &ls_sys);

    TIMECODE->NEXT.UTC_MDHMS = (((month << TIMECODE_NEXT_UTC_MDHMS_UTC_MON_SHIFT) & TIMECODE_NEXT_UTC_MDHMS_UTC_MON_MASK) | \
                                ((day   << TIMECODE_NEXT_UTC_MDHMS_UTC_DAY_SHIFT) & TIMECODE_NEXT_UTC_MDHMS_UTC_DAY_MASK) | \
                                ((hour  << TIMECODE_NEXT_UTC_MDHMS_UTC_HR_SHIFT)  & TIMECODE_NEXT_UTC_MDHMS_UTC_HR_MASK)  | \
                                ((min   << TIMECODE_NEXT_UTC_MDHMS_MINUTE_SHIFT)  & TIMECODE_NEXT_UTC_MDHMS_MINUTE_MASK)  | \
                                ((sec   << TIMECODE_NEXT_UTC_MDHMS_SECOND_SHIFT)  & TIMECODE_NEXT_UTC_MDHMS_SECOND_MASK));
    TIMECODE->NEXT.UTC_Y = (((diy << TIMECODE_NEXT_UTC_Y_UTC_DIY_SHIFT) & TIMECODE_NEXT_UTC_Y_UTC_DIY_MASK) | \
                             ((year << TIMECODE_NEXT_UTC_Y_UTC_YEAR_SHIFT) & TIMECODE_NEXT_UTC_Y_UTC_YEAR_MASK));

    TIMECODE->NEXT.UTC_SBS = sbs;

    //TODO: update to handle leap second event
    TIMECODE->NEXT.LEAP_SEC = (ls_ptp << TIMECODE_CURR_LEAP_SEC_LS_VALUE_SHIFT);
    TIMECODE->NEXT.LEAP_SEC |= TIMECODE_CURR_LEAP_SEC_LS_VALID;

    TIMECODE->CR |= TIMECODE_CR_VALID;
  }

  return 0;
}
