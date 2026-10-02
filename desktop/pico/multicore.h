/*
 * pico/multicore.h - Windows stand-in: "core 1" is a thread.
 *
 * Author: Thomas Dzubin
 */
#ifndef SHIM_PICO_MULTICORE_H
#define SHIM_PICO_MULTICORE_H

#include "pico/stdlib.h"

void multicore_launch_core1(void (*entry)(void));

#endif /* SHIM_PICO_MULTICORE_H */
