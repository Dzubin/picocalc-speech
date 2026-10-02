/*
 * shim_pwm_config.h - tunables for the PWM / interrupt / multicore emulation
 * that lets picocalc_audio.c (the Speech project's sound engine) run on
 * Windows. Kept out of the .c so the .c holds only code.
 *
 * Author: Thomas Dzubin
 */
#ifndef SHIM_PWM_CONFIG_H
#define SHIM_PWM_CONFIG_H

/* What clock_get_hz(clk_sys) reports: the SDK default for the RP2040. The
 * engine derives its sample rate from this, so it plays at the intended
 * 22050 Hz exactly as on the Pico. */
#define SHIM_SYS_CLOCK_HZ       150000000u

/* Loudness of the emulated PWM output on the PC (100 = full scale). */
#define SHIM_PWM_GAIN_PERCENT   50

/* Frames per SDL audio callback. */
#define SHIM_PWM_AUDIO_SAMPLES  512

/* IRQ number handed out for the PWM wrap interrupt (any value; it is only
 * ever passed back to the irq_* functions). */
#define SHIM_PWM_IRQ_NUM        4

#endif /* SHIM_PWM_CONFIG_H */
