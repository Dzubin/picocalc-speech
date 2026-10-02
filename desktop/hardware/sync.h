/*
 * hardware/sync.h - Windows stand-in. "Disabling interrupts" locks a mutex
 * that the emulated sample interrupt also takes (see shim_pwm.c); the
 * barrier and wait-for-interrupt calls map onto the obvious PC equivalents.
 *
 * Author: Thomas Dzubin
 */
#ifndef SHIM_HARDWARE_SYNC_H
#define SHIM_HARDWARE_SYNC_H

#include "pico/stdlib.h"

uint32_t save_and_disable_interrupts(void);
void     restore_interrupts(uint32_t status);
void     __wfi(void);

#define __dmb() __sync_synchronize()

#endif /* SHIM_HARDWARE_SYNC_H */
