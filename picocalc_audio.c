/*
 *  picocalc_audio - algorithmic sound engine for the ClockworkPi PicoCalc
 *  =====================================================================
 *
 *  See picocalc_audio.h for the API, the hardware notes and the reasons
 *  this exists.  Implementation notes:
 *
 *  Output stage.  The PWM slice shared by GP26/GP27 is set so that one
 *  PWM period == one audio sample (wrap = sysclk / PCAUDIO_SAMPLE_RATE),
 *  making the duty cycle a ~12-bit DAC value and the wrap interrupt the
 *  sample clock.  Each interrupt renders exactly one stereo sample and
 *  writes both channels with a single pwm_set_both_levels().
 *
 *  All maths is fixed-point integer - no <math.h>, no FPU dependency, so
 *  this runs the same on RP2040 and RP2350.  Two scalings are used:
 *      Q12 (4096 = 1.0) for filter coefficients
 *      Q16 (65536 = 1.0) for envelope levels
 *  and audio samples are plain int32_t with full scale at +/-32767.
 *  Every multiply below is arranged so the intermediate stays inside a
 *  signed 32-bit range; the filter state is clamped to +/-SVF_CLAMP both
 *  to keep a resonant filter from running away and to keep those
 *  intermediates bounded.
 *
 *  Sine without a table.  osc_sine() uses the well-known two-stage
 *  parabolic approximation (y = 4x(1-|x|), then refined by
 *  0.775y + 0.225y|y|).  It is within about 0.2% of a true sine - far
 *  better than needed for an explosion thump or an LFO - and costs no
 *  table space and no libm.
 *
 *  Noise.  A 32-bit xorshift LFSR, two shifts and three XORs per sample.
 *  This is the raw material for wind, explosions, gunfire and the
 *  unvoiced parts of speech; it is the one thing the square-wave PIO
 *  driver in picocalc-text-starter fundamentally cannot produce.
 *
 *  Filter.  A Chamberlin state-variable filter, which gives low-pass,
 *  band-pass and high-pass from the same two state words and has a
 *  genuinely resonant peak - that peak is what turns flat white noise
 *  into whistling wind, and what makes three of them in parallel sound
 *  like a vowel.  Coefficient f ~= 2*pi*fc/fs is accurate while
 *  fc << fs, hence CUTOFF_MAX below.
 *
 *  Rates.  Sample-rate work (oscillator, filter, envelope, mix) runs in
 *  the interrupt.  Slower work (cutoff sweeps, LFO) runs once every
 *  CTRL_DIV samples, which is inaudible for those parameters and keeps
 *  the per-sample cost down.
 *
 *  Concurrency.  When the engine runs on core 1 (see pcaudio_init), core
 *  0 writes voices while core 1 reads them.  A voice is filled in
 *  completely and only then published by setting `active` after a memory
 *  barrier, so the interrupt never sees a half-built voice.  The one
 *  window that remains is voice *stealing*: the interrupt can be part way
 *  through the stolen voice as it is overwritten, which costs at most a
 *  single bad sample.  That is acceptable here; a project that cannot
 *  tolerate it should take a hardware spinlock around both sides.
 *
 *  Build note: compile -O2/-O3 (a Release build).  At -O0 the small
 *  static helpers are not inlined into the RAM-resident interrupt and
 *  end up executing from flash, which still works but adds timing
 *  jitter.
 *
 *  Author: Thomas Dzubin.
 */

#include <string.h>

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/clocks.h"
#include "hardware/irq.h"
#include "hardware/pwm.h"
#include "hardware/sync.h"

#include "picocalc_audio.h"

/* ===================================================================== */
/*  Fixed-point scalings and limits                                       */
/* ===================================================================== */

#define Q            (12)              /* filter coefficient fraction    */
#define Q_ONE        (1 << Q)          /* 4096 == 1.0                    */
#define ENV_Q        (16)              /* envelope fraction              */
#define ENV_ONE      (1 << ENV_Q)      /* 65536 == 1.0                   */

#define SAMPLE_MAX   (32767)
#define SVF_CLAMP    (1 << 17)         /* filter state ceiling: 4x full
                                        * scale of ring, and keeps every
                                        * intermediate inside int32      */
#define CTRL_DIV     (16)              /* samples per control-rate tick  */

#define CUTOFF_MIN   (30)
#define CUTOFF_MAX   (PCAUDIO_SAMPLE_RATE / 6)   /* SVF accuracy limit   */
#define F_COEF_MAX   ((Q_ONE * 9) / 10)          /* 0.9, stability limit */

/* 2*pi in Q12, used to turn a cutoff in Hz into the SVF's f coefficient */
#define TWO_PI_Q12   (25736)

/* envelope level below which a note is considered finished (~ -60 dB) */
#define ENV_EPSILON  (64)

/* phase increment per Hz - lets the speech sequencer retune the glottal
 * pulse with a multiply instead of a 64-bit divide inside the interrupt */
#define PHASE_PER_HZ ((uint32_t)((1ull << 32) / PCAUDIO_SAMPLE_RATE))

enum { ENV_IDLE = 0, ENV_ATTACK, ENV_DECAY, ENV_SUSTAIN, ENV_RELEASE };

/* The whole single-interrupt-drives-stereo design depends on both output
 * pins living on one PWM slice (a slice covers an even/odd GPIO pair).
 * GP26/GP27 do; check it here rather than discovering it at runtime.   */
_Static_assert(PCAUDIO_LEFT_PIN / 2 == PCAUDIO_RIGHT_PIN / 2,
               "PCAUDIO_LEFT_PIN and PCAUDIO_RIGHT_PIN must share a PWM slice");

/* ===================================================================== */
/*  Voice state                                                           */
/* ===================================================================== */

typedef struct {
    int32_t low, band;
} svf_t;

typedef struct {
    volatile bool active;       /* published last - see the header note  */
    uint16_t serial;            /* bumped per start, validates handles   */

    /* source */
    uint8_t  source;
    uint32_t phase, phase_inc;

    /* pitch sweep, tracked in Q8 Hz like the cutoff sweep */
    int32_t  cur_freq, freq_target, freq_step;

    /* filter */
    uint8_t  filter;
    bool     formants;          /* run f2/f3 in parallel (vowels)        */
    svf_t    s1, s2, s3;
    int32_t  coef1, coef2, coef3;   /* Q12                               */
    int32_t  q_coef;                /* Q12 damping (lower = resonant)    */

    /* cutoff sweep, tracked in Q8 Hz so slow sweeps stay smooth */
    int32_t  cutoff_cur, cutoff_target, cutoff_step;

    /* LFO */
    uint32_t lfo_phase, lfo_inc;
    uint16_t lfo_cutoff;
    uint8_t  lfo_level;

    /* envelope */
    uint8_t  stage;
    int32_t  env;               /* Q16                                   */
    int32_t  attack_inc;        /* Q16 per sample                        */
    int32_t  decay_coef;        /* Q16 multiplier per sample             */
    int32_t  release_coef;      /* Q16 multiplier per sample             */
    int32_t  sustain_level;     /* Q16                                   */
    int32_t  hold_samples;      /* countdown; < 0 means hold forever     */
    int32_t  delay_samples;     /* silent countdown before the voice
                                 * starts at all (patch delay_ms)        */

    /* output */
    uint8_t  level;             /* patch level, 0..255                   */
    uint8_t  eff_level;         /* level after LFO dip, 0..255           */
    int32_t  gain_l, gain_r;    /* pan, 0..255                           */

    /* speech sequencer (pcaudio_speak); everything here is stepped by
     * speech_control() instead of voice_control()                       */
    bool     speech;
    const pcaudio_phoneme_t *seq;
    uint8_t  seq_len, seq_index;
    int32_t  seg_blocks;        /* control blocks left in this segment    */
    int32_t  glide_blocks;      /* of those, how many still glide; after
                                 * this hits 0 the targets are held       */
    int32_t  cur_f1, cur_f2, cur_f3;   /* Q8 Hz, glided each block       */
    int32_t  d_f1,  d_f2,  d_f3;       /* Q8 Hz per block                */
    int32_t  cur_f0, d_f0;             /* Q8 Hz                          */
    int32_t  cur_amp, d_amp;           /* Q8 of 0..255                   */
    int32_t  f0_scale;                 /* Q8, 256 = sing at the table's
                                        * own pitch (see pcaudio_speak_at)*/
} voice_t;

static voice_t  voices[PCAUDIO_VOICES];
static uint16_t next_serial = 1;

static bool     initialised = false;
static uint     audio_slice;
static uint16_t pwm_wrap;
static uint8_t  master_volume = 200;
static uint32_t ctrl_countdown = 1;

/* ===================================================================== */
/*  Time helpers                                                          */
/* ===================================================================== */

/* samples for a duration, at least 1 so nothing divides by zero */
static inline int32_t ms_to_samples(uint32_t ms)
{
    int32_t n = (int32_t)((ms * (uint32_t)PCAUDIO_SAMPLE_RATE) / 1000u);
    return n < 1 ? 1 : n;
}

/* the same in control-rate blocks, for anything stepped by *_control() */
static inline int32_t ms_to_blocks(uint32_t ms)
{
    int32_t n = ms_to_samples(ms) / CTRL_DIV;
    return n < 1 ? 1 : n;
}

/* ===================================================================== */
/*  Sources                                                               */
/* ===================================================================== */

/* 32-bit xorshift.  Shared by every noise voice, which is what we want:
 * consecutive voices draw different values and so decorrelate, giving a
 * genuinely stereo hiss rather than the same noise in both ears.        */
static uint32_t rng_state = 0x13579BDFu;

static inline int32_t noise_sample(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return (int32_t)(rng_state >> 16) - 32768;
}

/* parabolic sine approximation; phase is a full 32-bit turn, result is
 * +/-32767 (see the file header)                                        */
static inline int32_t osc_sine(uint32_t phase)
{
    int32_t x = (int32_t)(phase >> 16) - 32768;      /* -32768..32767    */
    int32_t ax = x < 0 ? -x : x;
    int32_t y = (x * (32768 - ax)) >> 13;            /* rough parabola   */
    int32_t ay = y < 0 ? -y : y;
    y = (y * (25395 + ((ay * 7373) >> 15))) >> 15;   /* 0.775y+0.225y|y| */
    if (y >  SAMPLE_MAX) y =  SAMPLE_MAX;
    if (y < -SAMPLE_MAX) y = -SAMPLE_MAX;
    return y;
}

static inline int32_t source_sample(voice_t *v)
{
    switch (v->source) {
    case PCAUDIO_SRC_NOISE:
        return noise_sample();
    case PCAUDIO_SRC_SINE:
        return osc_sine(v->phase);
    case PCAUDIO_SRC_SQUARE:
        return (v->phase & 0x80000000u) ? SAMPLE_MAX : -SAMPLE_MAX;
    case PCAUDIO_SRC_SAW:
        return (int32_t)(v->phase >> 16) - 32768;
    case PCAUDIO_SRC_TRIANGLE: {
        int32_t s = (int32_t)(v->phase >> 16);
        return s < 32768 ? (s * 2 - 32768) : (98303 - s * 2);
    }
    case PCAUDIO_SRC_PULSE:
        /* 1/8 duty, chosen so the mean is exactly zero: a decent stand-in
         * for a glottal pulse when driving formant filters             */
        return ((v->phase >> 16) < 8192) ? 28000 : -4000;
    default:
        return 0;
    }
}

/* ===================================================================== */
/*  Filter                                                                */
/* ===================================================================== */

static inline int32_t cutoff_to_coef(int32_t hz)
{
    int32_t f;
    if (hz < CUTOFF_MIN) hz = CUTOFF_MIN;
    if (hz > CUTOFF_MAX) hz = CUTOFF_MAX;
    f = (hz * TWO_PI_Q12) / PCAUDIO_SAMPLE_RATE;
    if (f > F_COEF_MAX) f = F_COEF_MAX;
    return f;
}

/* one Chamberlin SVF step; returns the requested response */
static inline int32_t svf_step(svf_t *s, int32_t in, int32_t f, int32_t q,
                               uint8_t mode)
{
    int32_t high = in - s->low - ((q * s->band) >> Q);
    if (high >  SVF_CLAMP) high =  SVF_CLAMP;
    if (high < -SVF_CLAMP) high = -SVF_CLAMP;

    s->band += (f * high) >> Q;
    if (s->band >  SVF_CLAMP) s->band =  SVF_CLAMP;
    if (s->band < -SVF_CLAMP) s->band = -SVF_CLAMP;

    s->low += (f * s->band) >> Q;
    if (s->low >  SVF_CLAMP) s->low =  SVF_CLAMP;
    if (s->low < -SVF_CLAMP) s->low = -SVF_CLAMP;

    switch (mode) {
    case PCAUDIO_FILT_LP: return s->low;
    case PCAUDIO_FILT_BP: return s->band;
    case PCAUDIO_FILT_HP: return high;
    default:              return in;
    }
}

/* ===================================================================== */
/*  Control rate: cutoff sweeps and the LFO                               */
/* ===================================================================== */

static void __not_in_flash_func(voice_control)(voice_t *v)
{
    int32_t fc;

    if (v->cutoff_step) {
        v->cutoff_cur += v->cutoff_step;
        if ((v->cutoff_step > 0 && v->cutoff_cur >= v->cutoff_target) ||
            (v->cutoff_step < 0 && v->cutoff_cur <= v->cutoff_target)) {
            v->cutoff_cur  = v->cutoff_target;
            v->cutoff_step = 0;
        }
    }

    if (v->freq_step) {
        int32_t hz;
        v->cur_freq += v->freq_step;
        if ((v->freq_step > 0 && v->cur_freq >= v->freq_target) ||
            (v->freq_step < 0 && v->cur_freq <= v->freq_target)) {
            v->cur_freq  = v->freq_target;
            v->freq_step = 0;
        }
        hz = v->cur_freq >> 8;
        if (hz < 1)
            hz = 1;                  /* never stall the phase at 0 Hz    */
        v->phase_inc = (uint32_t)hz * PHASE_PER_HZ;
    }

    fc = v->cutoff_cur >> 8;

    if (v->lfo_inc) {
        int32_t l, gain;
        v->lfo_phase += v->lfo_inc;
        l = osc_sine(v->lfo_phase);                  /* +/-32767         */
        fc += ((int32_t)v->lfo_cutoff * l) >> 15;
        /* dip the level as the LFO swings negative - wind gusts rather
         * than droning at a constant loudness                          */
        gain = 255 - (((int32_t)v->lfo_level * (32767 - l)) >> 16);
        v->eff_level = (uint8_t)(((int32_t)v->level * gain) >> 8);
    } else {
        v->eff_level = v->level;
    }

    v->coef1 = cutoff_to_coef(fc);
}

/* ===================================================================== */
/*  Speech sequencer                                                      */
/*                                                                        */
/*  Runs at control rate in place of voice_control() for voices started   */
/*  by pcaudio_speak().  Each block it nudges the three formants, the     */
/*  pitch and the level a little further toward the current phoneme's     */
/*  targets, and moves on to the next phoneme when the time is up.  The   */
/*  gliding is what turns a list of vowels into something the ear hears   */
/*  as a word; see the pcaudio_phoneme_t comment in the header.           */
/* ===================================================================== */

static void __not_in_flash_func(speech_load_segment)(voice_t *v)
{
    const pcaudio_phoneme_t *p = &v->seq[v->seq_index++];
    int32_t blocks = ms_to_blocks(p->ms);
    int32_t glide;

    v->seg_blocks = blocks;

    if (p->glide_ms == 0) {
        /* jump, then hold for the whole segment */
        v->glide_blocks = 0;
        v->cur_f1  = (int32_t)p->f1    << 8;
        v->cur_f2  = (int32_t)p->f2    << 8;
        v->cur_f3  = (int32_t)p->f3    << 8;
        v->cur_amp = (int32_t)p->level << 8;
        v->d_f1 = v->d_f2 = v->d_f3 = v->d_amp = 0;
    } else {
        glide = ms_to_blocks(p->glide_ms);
        if (glide > blocks)
            glide = blocks;             /* glide the whole segment       */
        v->glide_blocks = glide;
        v->d_f1  = (((int32_t)p->f1    << 8) - v->cur_f1)  / glide;
        v->d_f2  = (((int32_t)p->f2    << 8) - v->cur_f2)  / glide;
        v->d_f3  = (((int32_t)p->f3    << 8) - v->cur_f3)  / glide;
        v->d_amp = (((int32_t)p->level << 8) - v->cur_amp) / glide;
    }

    /* Never glide in or out of silence: a voiced->unvoiced switch would
     * otherwise swoop the pitch down through 0 Hz.  Jump instead.      */
    if (p->f0 == 0 || v->cur_f0 == 0 || v->glide_blocks == 0) {
        v->cur_f0 = (int32_t)p->f0 << 8;
        v->d_f0   = 0;
    } else {
        v->d_f0   = (((int32_t)p->f0 << 8) - v->cur_f0) / v->glide_blocks;
    }
}

static void __not_in_flash_func(speech_control)(voice_t *v)
{
    if (v->seg_blocks <= 0) {
        if (v->seq_index >= v->seq_len) {
            v->stage = ENV_RELEASE;      /* word finished */
            return;
        }
        speech_load_segment(v);
    }
    v->seg_blocks--;

    /* move only while gliding; afterwards the targets are held steady,
     * which is what gives a consonant a recognisable identity instead of
     * smearing it into its neighbours                                  */
    if (v->glide_blocks > 0) {
        v->glide_blocks--;
        v->cur_f1  += v->d_f1;
        v->cur_f2  += v->d_f2;
        v->cur_f3  += v->d_f3;
        v->cur_f0  += v->d_f0;
        v->cur_amp += v->d_amp;
    }

    v->coef1 = cutoff_to_coef(v->cur_f1 >> 8);
    v->coef2 = cutoff_to_coef(v->cur_f2 >> 8);
    v->coef3 = cutoff_to_coef(v->cur_f3 >> 8);

    if (v->cur_f0 > 0) {
        /* f0_scale transposes the whole word; the formants above are
         * deliberately left alone, since a vocal tract's resonances do
         * not move with the note being sung                            */
        int32_t hz = ((v->cur_f0 >> 8) * v->f0_scale) >> 8;
        if (hz < 1)
            hz = 1;
        v->source    = PCAUDIO_SRC_PULSE;    /* voiced: glottal pulses  */
        v->phase_inc = (uint32_t)hz * PHASE_PER_HZ;
    } else {
        v->source    = PCAUDIO_SRC_NOISE;    /* unvoiced: /h/, whisper  */
    }

    v->eff_level = (uint8_t)(v->cur_amp < 0 ? 0 :
                             (v->cur_amp > (255 << 8) ? 255 : (v->cur_amp >> 8)));
}

/* ===================================================================== */
/*  Envelope                                                              */
/* ===================================================================== */

static inline void envelope_step(voice_t *v)
{
    switch (v->stage) {
    case ENV_ATTACK:
        v->env += v->attack_inc;
        if (v->env >= ENV_ONE) {
            v->env   = ENV_ONE;
            v->stage = ENV_DECAY;
        }
        break;

    case ENV_DECAY:
        /* 64-bit intermediate is REQUIRED: env and the coefficient are
         * both close to ENV_ONE, and 65536 * 65534 needs 33 bits.  Done
         * in 32 bits this overflows to a small negative number and the
         * test below then kills the voice on its very first decay
         * sample - which silences every percussive sound completely.
         * (Cortex-M0+/M33 both do 32x32->64 in one instruction.)      */
        v->env = v->sustain_level +
                 (int32_t)((((int64_t)(v->env - v->sustain_level)) *
                            v->decay_coef) >> ENV_Q);
        if (v->env <= v->sustain_level + ENV_EPSILON) {
            if (v->sustain_level <= ENV_EPSILON) {
                v->env   = 0;
                v->stage = ENV_IDLE;      /* purely percussive: done     */
            } else {
                v->env   = v->sustain_level;
                v->stage = ENV_SUSTAIN;
            }
        }
        break;

    case ENV_SUSTAIN:
        break;

    case ENV_RELEASE:
        /* same 33-bit product as the decay case above - without the
         * 64-bit intermediate a release ends instantly instead of
         * fading (which is why the wind used to stop dead)            */
        v->env = (int32_t)(((int64_t)v->env * v->release_coef) >> ENV_Q);
        if (v->env < ENV_EPSILON) {
            v->env   = 0;
            v->stage = ENV_IDLE;
        }
        break;

    default:
        break;
    }

    if (v->hold_samples > 0) {
        if (--v->hold_samples == 0 && v->stage != ENV_IDLE)
            v->stage = ENV_RELEASE;
    }
}

/* ===================================================================== */
/*  The sample interrupt                                                  */
/* ===================================================================== */

static void __not_in_flash_func(pcaudio_isr)(void)
{
    int32_t acc_l = 0, acc_r = 0;
    bool    do_control;
    int     i;

    pwm_clear_irq(audio_slice);

    do_control = (--ctrl_countdown == 0);
    if (do_control)
        ctrl_countdown = CTRL_DIV;

    for (i = 0; i < PCAUDIO_VOICES; i++) {
        voice_t *v = &voices[i];
        int32_t  s;

        if (!v->active)
            continue;

        /* still waiting to start: stay silent and keep the envelope and
         * every sweep frozen, so the voice begins exactly as written    */
        if (v->delay_samples > 0) {
            v->delay_samples--;
            continue;
        }

        if (do_control) {
            if (v->speech) speech_control(v);
            else           voice_control(v);
        }

        envelope_step(v);
        if (v->stage == ENV_IDLE) {
            v->active = false;
            continue;
        }

        v->phase += v->phase_inc;
        s = source_sample(v);

        if (v->filter != PCAUDIO_FILT_OFF) {
            if (v->formants) {
                /* three parallel band-passes at F1/F2/F3, weighted so the
                 * lowest formant dominates as it does in a real voice  */
                int32_t b1 = svf_step(&v->s1, s, v->coef1, v->q_coef,
                                      PCAUDIO_FILT_BP);
                int32_t b2 = svf_step(&v->s2, s, v->coef2, v->q_coef,
                                      PCAUDIO_FILT_BP);
                int32_t b3 = svf_step(&v->s3, s, v->coef3, v->q_coef,
                                      PCAUDIO_FILT_BP);
                s = b1 + (b2 >> 1) + (b3 >> 2);
            } else {
                s = svf_step(&v->s1, s, v->coef1, v->q_coef, v->filter);
            }
        }

        /* keep the envelope/level/pan multiplies inside int32 */
        if (s >  65535) s =  65535;
        if (s < -65535) s = -65535;

        s = (s * (v->env >> 8)) >> 8;       /* envelope                  */
        s = (s * v->eff_level) >> 8;        /* patch level (+ LFO dip)   */

        acc_l += (s * v->gain_l) >> 7;
        acc_r += (s * v->gain_r) >> 7;
    }

    /* master first, THEN limit: that way turning the master down really
     * does buy headroom for sounds that stack up, instead of only
     * quietening something that has already been clipped               */
    acc_l = (acc_l * master_volume) >> 8;
    acc_r = (acc_r * master_volume) >> 8;

    if (acc_l >  SAMPLE_MAX) acc_l =  SAMPLE_MAX;
    if (acc_l < -SAMPLE_MAX) acc_l = -SAMPLE_MAX;
    if (acc_r >  SAMPLE_MAX) acc_r =  SAMPLE_MAX;
    if (acc_r < -SAMPLE_MAX) acc_r = -SAMPLE_MAX;

    /* +/-32767 -> 0..wrap duty cycle */
    pwm_set_both_levels(audio_slice,
                        (uint16_t)(((acc_l + 32768) * pwm_wrap) >> 16),
                        (uint16_t)(((acc_r + 32768) * pwm_wrap) >> 16));
}

/* ===================================================================== */
/*  Engine setup                                                          */
/* ===================================================================== */

static void audio_irq_attach(void)
{
    irq_set_exclusive_handler(PWM_DEFAULT_IRQ_NUM(), pcaudio_isr);
    irq_set_priority(PWM_DEFAULT_IRQ_NUM(), 0);   /* sample clock: highest */
    irq_set_enabled(PWM_DEFAULT_IRQ_NUM(), true);
    pwm_set_irq_enabled(audio_slice, true);
}

/* core 1 only ever attaches the interrupt and then idles - the interrupt
 * itself does all the work, on this core, out of reach of core 0's
 * save_and_disable_interrupts() (see the header)                        */
static void core1_audio_entry(void)
{
    audio_irq_attach();
    while (true)
        __wfi();
}

void pcaudio_init(bool on_core1)
{
    pwm_config cfg;
    uint32_t   sysclk;

    if (initialised)
        return;

    memset(voices, 0, sizeof voices);
    for (int i = 0; i < PCAUDIO_VOICES; i++)
        voices[i].active = false;

    /* one slice covers both pins - that is what lets a single interrupt
     * drive true stereo (see the header and the _Static_assert above)  */
    audio_slice = pwm_gpio_to_slice_num(PCAUDIO_LEFT_PIN);

    gpio_set_function(PCAUDIO_LEFT_PIN,  GPIO_FUNC_PWM);
    gpio_set_function(PCAUDIO_RIGHT_PIN, GPIO_FUNC_PWM);

    /* One PWM period per audio sample: the wrap value is both the sample
     * clock divider and the DAC's full-scale count.  Reading clk_sys
     * here is what keeps the sample rate (and so all pitch and timing)
     * identical across different system clocks - but it is also read
     * ONLY here, so changing the clock after init silently shifts the
     * real sample rate.  See the warning on pcaudio_init() in the
     * header.                                                          */
    sysclk   = clock_get_hz(clk_sys);
    pwm_wrap = (uint16_t)((sysclk / PCAUDIO_SAMPLE_RATE) - 1);

    cfg = pwm_get_default_config();
    pwm_config_set_clkdiv_int(&cfg, 1);
    pwm_config_set_wrap(&cfg, pwm_wrap);
    pwm_init(audio_slice, &cfg, false);

    /* park at mid-rail so enabling the output doesn't thump */
    pwm_set_both_levels(audio_slice, pwm_wrap / 2, pwm_wrap / 2);
    pwm_clear_irq(audio_slice);

    initialised = true;

    if (on_core1)
        multicore_launch_core1(core1_audio_entry);
    else
        audio_irq_attach();

    pwm_set_enabled(audio_slice, true);
}

void pcaudio_shutdown(void)
{
    if (!initialised)
        return;

    pcaudio_stop_all();
    pwm_set_irq_enabled(audio_slice, false);
    irq_set_enabled(PWM_DEFAULT_IRQ_NUM(), false);
    pwm_set_enabled(audio_slice, false);

    gpio_set_function(PCAUDIO_LEFT_PIN,  GPIO_FUNC_SIO);
    gpio_set_function(PCAUDIO_RIGHT_PIN, GPIO_FUNC_SIO);
    gpio_set_dir(PCAUDIO_LEFT_PIN,  GPIO_OUT);
    gpio_set_dir(PCAUDIO_RIGHT_PIN, GPIO_OUT);
    gpio_put(PCAUDIO_LEFT_PIN,  0);
    gpio_put(PCAUDIO_RIGHT_PIN, 0);

    initialised = false;
}

void pcaudio_set_master_volume(uint8_t volume)
{
    master_volume = volume;
}

uint32_t pcaudio_get_sample_rate(void)
{
    return PCAUDIO_SAMPLE_RATE;
}

/* ===================================================================== */
/*  Voice handles                                                         */
/* ===================================================================== */

static voice_t *voice_from_handle(int handle)
{
    int      index;
    uint16_t serial;

    if (handle < 0)
        return NULL;

    index  = handle & 0xFF;
    serial = (uint16_t)((handle >> 8) & 0xFFFF);
    if (index >= PCAUDIO_VOICES)
        return NULL;
    if (!voices[index].active || voices[index].serial != serial)
        return NULL;

    return &voices[index];
}

/* free voice if there is one, else the quietest thing currently playing */
static int allocate_voice(void)
{
    int i, quietest = 0;
    int32_t quietest_env = INT32_MAX;

    for (i = 0; i < PCAUDIO_VOICES; i++)
        if (!voices[i].active)
            return i;

    for (i = 0; i < PCAUDIO_VOICES; i++) {
        int32_t e = voices[i].env * voices[i].eff_level;
        if (e < quietest_env) {
            quietest_env = e;
            quietest     = i;
        }
    }
    return quietest;
}

/* ===================================================================== */
/*  Starting and stopping sounds                                          */
/* ===================================================================== */

/* per-sample multiplier that decays roughly exponentially over `ms`;
 * (1 - 1/n) is the first-order form of exp(-1/n) and is accurate enough
 * here while costing no libm                                            */
static int32_t decay_multiplier(uint32_t ms)
{
    int32_t n = ms_to_samples(ms);
    /* round rather than truncate: at these time constants ENV_ONE/n is a
     * small number (3-ish for a one second decay), so truncating it
     * stretched every long decay by tens of percent                    */
    int32_t c = ENV_ONE - ((ENV_ONE + n / 2) / n);
    if (c < 0)             c = 0;
    if (c > ENV_ONE - 1)   c = ENV_ONE - 1;
    return c;
}

/*
 *  Shared by pcaudio_play() and pcaudio_speak().  When `seq` is non-NULL
 *  the voice is put under the speech sequencer instead of the ordinary
 *  cutoff-sweep/LFO control path.  Either way the voice is filled in
 *  completely and only published (active = true) at the very end, so the
 *  interrupt can never see it half-built.
 */
static int start_voice(const pcaudio_patch_t *p,
                       const pcaudio_phoneme_t *seq, uint8_t count,
                       int32_t f0_scale)
{
    voice_t *v;
    int      index;
    uint16_t serial;

    if (!initialised || p == NULL)
        return -1;

    index = allocate_voice();
    v     = &voices[index];

    /* take the slot out of the mix before rewriting it (see the header's
     * note on voice stealing)                                          */
    v->active = false;
    __dmb();

    memset(v, 0, sizeof *v);

    v->source = p->source;
    if (p->freq)
        v->phase_inc = (uint32_t)(((uint64_t)p->freq << 32) /
                                  PCAUDIO_SAMPLE_RATE);

    v->cur_freq = (int32_t)p->freq << 8;
    if (p->freq && p->freq_end && p->pitch_ms) {
        int32_t blocks = ms_to_blocks(p->pitch_ms);
        v->freq_target = (int32_t)p->freq_end << 8;
        v->freq_step   = (v->freq_target - v->cur_freq) / blocks;
        if (v->freq_step == 0)
            v->freq_step = (v->freq_target > v->cur_freq) ? 1 : -1;
    }

    v->filter = p->filter;
    v->q_coef = 2048 - ((int32_t)p->resonance * 7);   /* 2048..263       */
    if (v->q_coef < 150)
        v->q_coef = 150;

    v->cutoff_cur = (int32_t)(p->cutoff ? p->cutoff : CUTOFF_MAX) << 8;
    if (p->cutoff_end && p->sweep_ms) {
        int32_t blocks = ms_to_samples(p->sweep_ms) / CTRL_DIV;
        v->cutoff_target = (int32_t)p->cutoff_end << 8;
        v->cutoff_step   = (v->cutoff_target - v->cutoff_cur) /
                           (blocks < 1 ? 1 : blocks);
        if (v->cutoff_step == 0)
            v->cutoff_step = (v->cutoff_target > v->cutoff_cur) ? 1 : -1;
    }
    v->coef1 = cutoff_to_coef(v->cutoff_cur >> 8);

    if (p->formant2 && p->formant3) {
        v->formants = true;
        v->coef2 = cutoff_to_coef(p->formant2);
        v->coef3 = cutoff_to_coef(p->formant3);
    }

    if (p->lfo_mhz) {
        uint32_t ctrl_rate = PCAUDIO_SAMPLE_RATE / CTRL_DIV;
        v->lfo_inc = (uint32_t)(((uint64_t)p->lfo_mhz << 32) /
                                (1000ull * ctrl_rate));
        v->lfo_cutoff = p->lfo_cutoff;
        v->lfo_level  = p->lfo_level;
    }

    v->sustain_level = ((int32_t)p->sustain * ENV_ONE) / 255;
    v->decay_coef    = decay_multiplier(p->decay_ms ? p->decay_ms : 1);
    v->release_coef  = decay_multiplier(p->release_ms ? p->release_ms : 1);
    v->hold_samples  = p->hold_ms ? ms_to_samples(p->hold_ms) : -1;
    v->delay_samples = p->delay_ms ? ms_to_samples(p->delay_ms) : 0;

    if (p->attack_ms) {
        v->stage      = ENV_ATTACK;
        v->env        = 0;
        v->attack_inc = ENV_ONE / ms_to_samples(p->attack_ms);
    } else {
        v->stage = ENV_DECAY;
        v->env   = ENV_ONE;
    }

    v->level     = p->level;
    v->eff_level = p->level;
    /* pan by attenuating the far side only, so centre is unity (128 ->
     * >>7 == x1) and hard panning never boosts a channel above it     */
    v->gain_l    = (p->pan <= 0) ? 128 : (128 - p->pan);
    v->gain_r    = (p->pan >= 0) ? 128 : (128 + p->pan);

    if (seq && count) {
        /* start the vocal tract already in the first phoneme's shape, so
         * the word opens on that sound instead of gliding up to it from
         * nothing; speech_control() takes over from the next block on  */
        v->speech    = true;
        v->seq       = seq;
        v->seq_len   = count;
        v->seq_index = 0;
        v->seg_blocks = 0;
        v->cur_f1    = (int32_t)seq[0].f1 << 8;
        v->cur_f2    = (int32_t)seq[0].f2 << 8;
        v->cur_f3    = (int32_t)seq[0].f3 << 8;
        v->cur_f0    = (int32_t)seq[0].f0 << 8;
        v->cur_amp   = (int32_t)seq[0].level << 8;
        v->coef1     = cutoff_to_coef(seq[0].f1);
        v->coef2     = cutoff_to_coef(seq[0].f2);
        v->coef3     = cutoff_to_coef(seq[0].f3);
        v->formants  = true;
        v->eff_level = seq[0].level;
        v->f0_scale  = f0_scale;
        if (seq[0].f0) {
            int32_t hz = ((int32_t)seq[0].f0 * f0_scale) >> 8;
            if (hz < 1)
                hz = 1;
            v->source    = PCAUDIO_SRC_PULSE;
            v->phase_inc = (uint32_t)hz * PHASE_PER_HZ;
        } else {
            v->source    = PCAUDIO_SRC_NOISE;
        }
    }

    serial    = next_serial++;
    if (next_serial == 0)
        next_serial = 1;
    v->serial = serial;

    __dmb();                 /* everything above is visible before... */
    v->active = true;        /* ...the interrupt starts using it      */

    return (int)((serial << 8) | (uint32_t)index);
}

int pcaudio_play(const pcaudio_patch_t *p)
{
    return start_voice(p, NULL, 0, 256);
}

void pcaudio_release(int handle)
{
    voice_t *v = voice_from_handle(handle);
    if (v && v->stage != ENV_IDLE)
        v->stage = ENV_RELEASE;
}

void pcaudio_kill(int handle)
{
    voice_t *v = voice_from_handle(handle);
    if (v) {
        v->active = false;
        v->stage  = ENV_IDLE;
    }
}

bool pcaudio_is_playing(int handle)
{
    return voice_from_handle(handle) != NULL;
}

void pcaudio_stop_all(void)
{
    for (int i = 0; i < PCAUDIO_VOICES; i++) {
        voices[i].active = false;
        voices[i].stage  = ENV_IDLE;
    }
}

/* ===================================================================== */
/*  Speech                                                                */
/* ===================================================================== */

int pcaudio_speak_at(const pcaudio_phoneme_t *seq, uint8_t count,
                     uint16_t f0_hz)
{
    /* The patch supplies only the parts the sequencer does not drive:
     * the filter type and resonance, the pan, and an envelope that
     * simply stays open (sustain 255, no hold) until speech_control()
     * reaches the end of the word and releases it.  Everything else -
     * source, pitch, all three formants, level - is under the
     * sequencer's control from the first block onward.               */
    pcaudio_patch_t voice = {
        .filter     = PCAUDIO_FILT_BP,
        .resonance  = 205,          /* sharp formant peaks             */
        .attack_ms  = 8,
        .decay_ms   = 1,
        .sustain    = 255,
        .hold_ms    = 0,            /* sequencer decides when it ends  */
        .release_ms = 60,
        .level      = 255,          /* per-phoneme level does the work */
    };
    int32_t scale = 256;            /* Q8; 256 = the table's own pitch */

    if (seq == NULL || count == 0)
        return -1;

    if (f0_hz) {
        /* Transpose so the word's first *voiced* phoneme lands on
         * f0_hz.  Anchoring on the first voiced one rather than seq[0]
         * matters because words often open on an unvoiced sound (an
         * /h/, a stop closure) whose f0 is 0 by definition.          */
        int i;
        for (i = 0; i < count; i++) {
            if (seq[i].f0) {
                scale = ((int32_t)f0_hz << 8) / seq[i].f0;
                break;
            }
        }
    }

    return start_voice(&voice, seq, count, scale);
}

int pcaudio_speak(const pcaudio_phoneme_t *seq, uint8_t count)
{
    return pcaudio_speak_at(seq, count, 0);
}
