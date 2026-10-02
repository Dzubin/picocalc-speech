/*
 * hardware/irq.h - Windows stand-in: interrupt registration for the emulated
 * PWM wrap interrupt (see shim_pwm.c).
 *
 * Author: Thomas Dzubin
 */
#ifndef SHIM_HARDWARE_IRQ_H
#define SHIM_HARDWARE_IRQ_H

#include "pico/stdlib.h"

typedef void (*irq_handler_t)(void);

void irq_set_exclusive_handler(uint num, irq_handler_t handler);
void irq_set_priority(uint num, uint8_t priority);
void irq_set_enabled(uint num, bool enabled);

#endif /* SHIM_HARDWARE_IRQ_H */
