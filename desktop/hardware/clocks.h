/*
 * hardware/clocks.h - Windows stand-in: only clock_get_hz(clk_sys), which
 * reports SHIM_SYS_CLOCK_HZ.
 *
 * Author: Thomas Dzubin
 */
#ifndef SHIM_HARDWARE_CLOCKS_H
#define SHIM_HARDWARE_CLOCKS_H

#include "pico/stdlib.h"

enum clock_index { clk_gpout0, clk_gpout1, clk_gpout2, clk_gpout3,
                   clk_ref, clk_sys, clk_peri, clk_usb, clk_adc };

uint32_t clock_get_hz(enum clock_index clk);

#endif /* SHIM_HARDWARE_CLOCKS_H */
