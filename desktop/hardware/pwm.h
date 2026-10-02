/*
 * hardware/pwm.h - Windows stand-in for the Pico SDK PWM API, just the calls
 * picocalc_audio.c uses. The "PWM slice" is emulated in shim_pwm.c: its wrap
 * interrupt is driven by SDL's audio callback, once per output sample, and the
 * two duty-cycle levels written each time become the left/right sample.
 *
 * Author: Thomas Dzubin
 */
#ifndef SHIM_HARDWARE_PWM_H
#define SHIM_HARDWARE_PWM_H

#include "pico/stdlib.h"
#include "shim_pwm_config.h"

typedef struct {
    uint16_t wrap;
} pwm_config;

#define PWM_DEFAULT_IRQ_NUM() (SHIM_PWM_IRQ_NUM)

uint       pwm_gpio_to_slice_num(uint gpio);
pwm_config pwm_get_default_config(void);
void       pwm_init(uint slice, pwm_config *config, bool start);
void       pwm_set_both_levels(uint slice, uint16_t level_a, uint16_t level_b);
void       pwm_clear_irq(uint slice);
void       pwm_set_irq_enabled(uint slice, bool enabled);
void       pwm_set_enabled(uint slice, bool enabled);

static inline void pwm_config_set_clkdiv_int(pwm_config *c, uint div) { (void)c; (void)div; }
static inline void pwm_config_set_wrap(pwm_config *c, uint16_t wrap)  { c->wrap = wrap; }

#endif /* SHIM_HARDWARE_PWM_H */
