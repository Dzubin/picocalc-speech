/*
 * shim_pwm.c - emulates the slice of the Pico SDK that picocalc_audio.c (the
 * Speech project's sound engine) drives: a PWM slice with a wrap interrupt,
 * the interrupt controller, the system clock, interrupt masking and core 1.
 *
 * The real engine works like this: one PWM period is one audio sample, the
 * wrap interrupt fires once per sample, the handler mixes all voices and
 * writes the stereo result with pwm_set_both_levels(). Here SDL's audio
 * callback plays the part of the PWM hardware - it calls the registered
 * handler once per output frame and turns the two levels it wrote into the
 * left and right sample. The engine itself runs unmodified.
 *
 * Author: Thomas Dzubin
 */
#include "shim.h"
#include "hardware/pwm.h"
#include "hardware/irq.h"
#include "hardware/clocks.h"
#include "hardware/sync.h"
#include "pico/multicore.h"

static SDL_mutex      *irq_lock;
static irq_handler_t   handler;
static bool            line_enabled;      /* irq_set_enabled()      */
static bool            slice_irq_enabled; /* pwm_set_irq_enabled()  */
static bool            running;           /* pwm_set_enabled()      */
static uint16_t        wrap = 1;
static uint16_t        level_a, level_b;
static SDL_AudioDeviceID device;

static void lock_init(void)
{
    if (!irq_lock)
        irq_lock = SDL_CreateMutex();
}

/* ---- interrupt masking / core 1 -------------------------------------- */

uint32_t save_and_disable_interrupts(void)
{
    lock_init();
    SDL_LockMutex(irq_lock);
    return 0;
}

void restore_interrupts(uint32_t status)
{
    (void)status;
    SDL_UnlockMutex(irq_lock);
}

void __wfi(void)
{
    SDL_Delay(1);
}

static int SDLCALL core1_thread(void *entry)
{
    ((void (*)(void))entry)();
    return 0;
}

void multicore_launch_core1(void (*entry)(void))
{
    SDL_Thread *t = SDL_CreateThread(core1_thread, "core1", (void *)entry);

    if (t)
        SDL_DetachThread(t);
}

/* ---- clocks / interrupt controller ----------------------------------- */

uint32_t clock_get_hz(enum clock_index clk)
{
    (void)clk;
    return SHIM_SYS_CLOCK_HZ;
}

void irq_set_exclusive_handler(uint num, irq_handler_t h)
{
    (void)num;
    lock_init();
    SDL_LockMutex(irq_lock);
    handler = h;
    SDL_UnlockMutex(irq_lock);
}

void irq_set_priority(uint num, uint8_t priority)  { (void)num; (void)priority; }

void irq_set_enabled(uint num, bool enabled)
{
    (void)num;
    line_enabled = enabled;
}

/* ---- the PWM slice ---------------------------------------------------- */

uint pwm_gpio_to_slice_num(uint gpio)   { return (gpio >> 1) & 7u; }
pwm_config pwm_get_default_config(void) { pwm_config c = { 0xFFFF }; return c; }

void pwm_init(uint slice, pwm_config *config, bool start)
{
    (void)slice;
    lock_init();
    wrap = config->wrap ? config->wrap : 1;
    running = start;
}

void pwm_set_both_levels(uint slice, uint16_t a, uint16_t b)
{
    (void)slice;
    level_a = a;
    level_b = b;
}

void pwm_clear_irq(uint slice)                     { (void)slice; }
void pwm_set_irq_enabled(uint slice, bool enabled) { (void)slice; slice_irq_enabled = enabled; }

/* duty level (0..wrap) -> signed 16-bit sample, at the configured loudness */
static int16_t level_to_sample(uint16_t level)
{
    int32_t s = (int32_t)(((uint32_t)level << 16) / wrap) - 32768;

    if (s > 32767)
        s = 32767;
    if (s < -32767)
        s = -32767;
    return (int16_t)(s * SHIM_PWM_GAIN_PERCENT / 100);
}

static void SDLCALL audio_fill(void *user, Uint8 *stream, int len)
{
    int16_t *out = (int16_t *)stream;
    int frames = len / (int)(2 * sizeof(int16_t));

    (void)user;
    for (int i = 0; i < frames; i++) {
        SDL_LockMutex(irq_lock);
        if (running && line_enabled && slice_irq_enabled && handler)
            handler();
        out[2 * i]     = running ? level_to_sample(level_a) : 0;
        out[2 * i + 1] = running ? level_to_sample(level_b) : 0;
        SDL_UnlockMutex(irq_lock);
    }
}

void pwm_set_enabled(uint slice, bool enabled)
{
    SDL_AudioSpec want;

    (void)slice;
    running = enabled;
    if (!enabled || device)
        return;

    /* One PWM period per sample: the sample rate is sysclk / (wrap + 1). */
    shim_init();
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
        return;
    SDL_zero(want);
    want.freq = (int)(SHIM_SYS_CLOCK_HZ / ((uint32_t)wrap + 1u));
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = SHIM_PWM_AUDIO_SAMPLES;
    want.callback = audio_fill;
    device = SDL_OpenAudioDevice(NULL, 0, &want, NULL, 0);
    if (device)
        SDL_PauseAudioDevice(device, 0);
}
