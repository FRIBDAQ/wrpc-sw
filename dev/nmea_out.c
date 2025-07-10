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
#include "dev/nmea_out.h"

#define SUART_CALC_BAUD_NMEA(baudrate, clkrate) \
    ( ((( (unsigned long long)baudrate * 8ULL) << (16 - 7)) + \
      (clkrate >> 8)) / (clkrate >> 7) )

void nmea_out_init(struct nmea_master *dev, uint32_t baudrate, uint32_t invert){
  nmea_out_set_baud(dev, baudrate);
  nmea_out_set_invert(dev, invert);
}

int nmea_out_set_baud(struct nmea_master *dev, uint32_t baudrate)
{
  int baud = 0;
  int clk_freq = (dev->SR & NMEA_MASTER_SR_CLK_FREQ_MASK) >> NMEA_MASTER_SR_CLK_FREQ_SHIFT;
  int cr = dev->CR;
  baud = SUART_CALC_BAUD_NMEA(baudrate, clk_freq);
  cr &= ~NMEA_MASTER_CR_BAUD_DIV_MASK;
  cr |= (baud << NMEA_MASTER_CR_BAUD_DIV_SHIFT);
  dev->CR = cr;
  return 0;
}

void nmea_out_set_invert(struct nmea_master *dev, int invert)
{
  int cr = dev->CR;
  if(invert){
    cr |= NMEA_MASTER_CR_INVERT;
  }else{
    cr &= ~NMEA_MASTER_CR_INVERT;
  }
  dev->CR = cr;
}

int nmea_out_get_invert(struct nmea_master *dev)
{
  return (dev->CR & NMEA_MASTER_CR_INVERT) ? 1 : 0;
}

void nmea_out_get_status(struct nmea_master *dev, int *valid, int *tip)
{
  *valid = (dev->SR & NMEA_MASTER_SR_VALID) ? 1 : 0;
  *tip   = (dev->SR & NMEA_MASTER_SR_TIP) ? 1 : 0;
}

void nmea_out_get_tod(struct nmea_master *dev, int *hour, int *min, int *sec)
{

  *hour = (dev->TOD & NMEA_MASTER_TOD_HOUR_MASK)   >> NMEA_MASTER_TOD_HOUR_SHIFT;
  *min  = (dev->TOD & NMEA_MASTER_TOD_MINUTE_MASK) >> NMEA_MASTER_TOD_MINUTE_SHIFT;
  *sec  = (dev->TOD & NMEA_MASTER_TOD_SECOND_MASK) >> NMEA_MASTER_TOD_SECOND_SHIFT;
}

void nmea_out_get_date(struct nmea_master *dev, int *day, int *month, int *year)
{

  *day   = (dev->DATE & NMEA_MASTER_DATE_DAY_MASK)   >> NMEA_MASTER_DATE_DAY_SHIFT;
  *month = (dev->DATE & NMEA_MASTER_DATE_MONTH_MASK) >> NMEA_MASTER_DATE_MONTH_SHIFT;
  *year  = (dev->DATE & NMEA_MASTER_DATE_YEAR_MASK)  >> NMEA_MASTER_DATE_YEAR_SHIFT;
}
