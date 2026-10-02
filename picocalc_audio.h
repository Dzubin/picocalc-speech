/*
 *  picocalc_audio - algorithmic sound engine for the ClockworkPi PicoCalc
 *  =====================================================================
 *
 *  A self-contained, reusable synthesis engine: drop picocalc_audio.c /
 *  picocalc_audio.h into any Pico SDK project, add the .c to your
 *  add_executable() and link pico_stdlib, pico_multicore, hardware_pwm
 *  and hardware_clocks.  It has no other dependencies - in particular it
 *  does NOT need picocalc-text-starter, <math.h>, pico_float or any
 *  recorded audio data.  Every sound is generated from an algorithm, one
 *  sample at a time.
 *
 *  ------------------------------------------------------------------
 *  Why this exists / how it differs from the starter's audio driver
 *  ------------------------------------------------------------------
 *  picocalc-text-starter's drivers/audio.c + audio.pio drive the speakers
 *  with a PIO loop that toggles the pin at a given frequency: a square
 *  wave at a fixed 50% duty, 100..2000 Hz, and nothing else.  A square
 *  wave is periodic by construction, so that design can never produce
 *  noise - and noise is the raw material for wind, explosions, gunfire
 *  and the unvoiced parts of speech.
 *
 *  This engine instead uses the RP2040/RP2350 *hardware* PWM block as a
 *  ~12-bit DAC: one PWM period = one audio sample.  An interrupt fires
 *  once per period, software computes the next sample, and the duty
 *  cycle carries it.  That's the same approach PicoMite (the firmware
 *  the PicoCalc ships with) uses on this exact hardware, which is how it
 *  manages white noise, periodic noise and 44.1 kHz WAV/MP3 playback
 *  through these same two pins.
 *
 *  No .pio file is needed (the PWM peripheral does the work in hardware)
 *  and no .S/.asm is needed (the DSP is fixed-point integer C; at 22 kHz
 *  with 8 voices it costs only a few percent of one core).
 *
 *  ------------------------------------------------------------------
 *  The hardware
 *  ------------------------------------------------------------------
 *  GP26 is the left channel, GP27 the right; each goes through an RC
 *  low-pass filter on the mainboard into the amplifier, the two speakers
 *  and the 3.5 mm jack.  Both pins live on the *same* PWM slice (GP26 is
 *  channel A, GP27 is channel B).  That is a real limitation if you are
 *  making square waves by varying the PWM *frequency* - you can only
 *  have one frequency - but it is irrelevant here and actually
 *  convenient: one slice gives one sample-rate interrupt driving two
 *  independent duty registers, i.e. free stereo.
 *
 *  Sample resolution is sysclk / sample_rate.  On a Pico 2 (RP2350) at
 *  150 MHz and PCAUDIO_SAMPLE_RATE 22050 that is a wrap of 6802, so
 *  about 12.7 bits per sample.
 *
 *  ------------------------------------------------------------------
 *  Run it on core 1 (recommended on the PicoCalc)
 *  ------------------------------------------------------------------
 *  picocalc-text-starter's lcd_blit() wraps every SPI transfer in
 *  save_and_disable_interrupts().  A core-0 audio interrupt would be
 *  stalled for the whole of every blit and you would hear it click and
 *  stutter whenever the screen redraws.  save_and_disable_interrupts()
 *  only masks interrupts on the core that calls it, so passing true to
 *  pcaudio_init() - which services the audio interrupt on core 1 -
 *  removes the problem entirely.  Pass false only if your project needs
 *  core 1 for something else and you can live with drawing glitches.
 *
 *  ------------------------------------------------------------------
 *  Making sounds
 *  ------------------------------------------------------------------
 *  Every voice is the classic subtractive-synthesis chain:
 *
 *      source  ->  resonant filter  ->  ADSR envelope  ->  pan  ->  mix
 *     (noise or   (low/band/high     (attack, decay,
 *      an osc)     pass, sweepable    sustain, release)
 *                  cutoff, LFO)
 *
 *  which is enough for all of:
 *      wind       band-passed noise, cutoff wobbled by a slow LFO
 *      explosion  noise low-passed with a fast downward cutoff sweep
 *      gunfire    a very short, very fast-attack noise burst
 *      voice      a pulse "glottal" source through parallel band-passes
 *                 at the formant frequencies (set formant2 / formant3)
 *
 *  Use pcaudio_play() with your own pcaudio_patch_t, or take one of the
 *  two preset modules built on this engine and tune their numbers:
 *
 *      picocalc_sound_effects.c/.h   wind, explosion, gunshot, tones
 *      picocalc_speech.c/.h          vowels, words, singing
 *
 *  Both are optional - a project can compile this engine with one, the
 *  other, both or neither, which is why they are separate files.
 *
 *  Everything is prefixed pcaudio_ so it can coexist with
 *  picocalc-text-starter's drivers/audio.c (which owns the plain
 *  audio_init() / audio_play_sound() names) if a project links both.
 *  Do not run both at once, though - they would fight over the pins.
 *
 *  Author: Thomas Dzubin.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

/* ===================================================================== */
/*  Build-time configuration                                              */
/* ===================================================================== */

#define PCAUDIO_LEFT_PIN     (26)   /* PicoCalc left  speaker            */
#define PCAUDIO_RIGHT_PIN    (27)   /* PicoCalc right speaker (same PWM
                                      * slice as the left - see above)    */

/* 22050 Hz keeps ~12.7 bits of duty resolution at 150 MHz and leaves
 * plenty of CPU headroom.  44100 halves the resolution (~11.7 bits) and
 * doubles the interrupt rate; both work.                                */
#define PCAUDIO_SAMPLE_RATE  (22050)

/*
 *  Simultaneous sounds.  Needs headroom above the biggest single effect:
 *  pcaudio_raindrops() alone starts 8, and a sustained wind or surf is
 *  holding 2 more the whole time, so 8 total would have the rain steal
 *  the weather mid-gust.  Idle voices cost only one test per sample, so
 *  raising this is cheap - about 0.5% of one core per 4 extra voices at
 *  22 kHz - and the memory is a few hundred bytes each.
 */
#define PCAUDIO_VOICES       (12)

/* ===================================================================== */
/*  Patches                                                               */
/* ===================================================================== */

typedef enum {
    PCAUDIO_SRC_NOISE = 0,  /* white noise - the basis of wind/bangs     */
    PCAUDIO_SRC_SINE,
    PCAUDIO_SRC_SQUARE,
    PCAUDIO_SRC_SAW,
    PCAUDIO_SRC_TRIANGLE,
    PCAUDIO_SRC_PULSE,      /* narrow zero-mean pulse - glottal source
                             * for voice/formant work                    */
} pcaudio_source_t;

/*
 *  Getting bass out of the PicoCalc.  Its speakers are small and move
 *  very little air below about 150 Hz, so a pure PCAUDIO_SRC_SINE down
 *  there is nearly inaudible no matter how loud you make it.  Use a SAW
 *  (or SQUARE) instead: a 60 Hz saw puts energy at 120, 180, 240 Hz and
 *  up, all of which the speakers reproduce happily, and the ear
 *  reconstructs the 60 Hz fundamental that was never really there.  Run
 *  it through a low-pass around 600-800 Hz to keep the first few
 *  harmonics and drop the buzz, and it will sound considerably deeper
 *  than the sine did.  This is the standard trick for small speakers.
 */

typedef enum {
    PCAUDIO_FILT_OFF = 0,   /* source goes straight through              */
    PCAUDIO_FILT_LP,        /* low pass  - explosions, body              */
    PCAUDIO_FILT_BP,        /* band pass - wind, formants, gunfire crack */
    PCAUDIO_FILT_HP,        /* high pass - hiss, air                     */
} pcaudio_filter_t;

/*
 *  One sound, described declaratively.  Zero-initialise and set only
 *  what you need (a zeroed patch is white noise, no filter, instant
 *  attack, sustained forever at silence - so always set at least
 *  `level` and an envelope).
 */
typedef struct {
    uint8_t  source;        /* pcaudio_source_t                          */
    uint16_t freq;          /* Hz; pitch of tonal sources, ignored by
                             * PCAUDIO_SRC_NOISE                         */
    uint16_t freq_end;      /* Hz, pitch swept toward over pitch_ms;
                             * 0 = hold `freq`.  A pitch dropping fast is
                             * the signature of an impact - it is what a
                             * muzzle blast or a big thud actually does,
                             * and it reads as far heavier than any fixed
                             * tone.                                     */
    uint16_t pitch_ms;

    uint8_t  filter;        /* pcaudio_filter_t                          */
    uint16_t cutoff;        /* Hz, filter corner/centre at note start    */
    uint16_t cutoff_end;    /* Hz, swept toward over sweep_ms; 0 = none  */
    uint16_t sweep_ms;
    uint8_t  resonance;     /* 0 = gentle .. 255 = sharply resonant      */

    uint16_t formant2;      /* extra parallel band-pass centres in Hz,   */
    uint16_t formant3;      /* 0 = unused.  Set both for vowel sounds.   */

    uint16_t lfo_mhz;       /* LFO rate in milli-Hz (1000 = 1.0 Hz);
                             * 0 = no LFO.  Gives wind its life.         */
    uint16_t lfo_cutoff;    /* +/- Hz the LFO swings the cutoff by       */
    uint8_t  lfo_level;     /* 0..255, how deeply the LFO dips the level */

    uint16_t attack_ms;
    uint16_t decay_ms;
    uint8_t  sustain;       /* 0..255 as a fraction of peak; 0 makes the
                             * sound purely percussive (dies after decay)*/
    uint16_t hold_ms;       /* how long to hold before auto-releasing;
                             * 0 = hold until pcaudio_release()          */
    uint16_t release_ms;

    uint8_t  level;         /* 0..255 overall loudness                   */
    int8_t   pan;           /* -127 left .. 0 centre .. 127 right        */

    uint16_t delay_ms;      /* hold the voice silent this long before it
                             * starts.  Lets one call schedule several
                             * voices at different times - scattered
                             * raindrops, a stuttered echo, debris after
                             * an impact - without the caller having to
                             * run a timer.  The voice occupies its slot
                             * while waiting.                            */
} pcaudio_patch_t;

/* ===================================================================== */
/*  Engine                                                                */
/* ===================================================================== */

/*
 *  Claim GP26/GP27, set the PWM slice up as a sample-rate DAC and start
 *  the audio interrupt.  Pass on_core1 = true on the PicoCalc so screen
 *  drawing can't stall the audio (see the note at the top).  Safe to
 *  call twice; the second call does nothing.
 *
 *  CALL THIS *AFTER* ANY SYSTEM CLOCK CHANGES.  It reads clk_sys once,
 *  here, and derives the PWM wrap from it (wrap = sysclk / sample rate)
 *  so that the sample rate comes out the same on any clock - which is
 *  what makes pitch and timing identical on a 125 MHz RP2040 and a
 *  150 MHz RP2350 without touching a thing.  But that wrap is fixed at
 *  init: call set_sys_clock_khz() afterwards and the real sample rate
 *  shifts by the same ratio, so everything plays at the wrong speed and
 *  pitch (going 150 -> 250 MHz after init would run all audio ~67%
 *  fast).  Set the clock first, then initialise.
 *
 *  The clock does still affect two things even when ordered correctly,
 *  both in your favour as it goes up: the PWM period is the DAC's
 *  full-scale count, so resolution is ~12.5 bits at 125 MHz, ~12.7 at
 *  150 MHz and ~13.5 if overclocked to 250 MHz; and the cycles
 *  available per sample (sysclk / sample rate) set how many voices can
 *  be mixed before the interrupt runs out of time.
 */
void pcaudio_init(bool on_core1);

/* Silence everything and stop the interrupt (the pins are left low). */
void pcaudio_shutdown(void);

/* 0 = silent, 255 = full scale.  Defaults to 200. */
void pcaudio_set_master_volume(uint8_t volume);

uint32_t pcaudio_get_sample_rate(void);

/*
 *  Start `patch` on a free voice and return a handle for it, or -1 if
 *  every voice is busy with something louder.  The patch is copied, so
 *  it may live on the stack.
 */
int  pcaudio_play(const pcaudio_patch_t *patch);

/* Move a voice into its release phase (for patches with hold_ms 0). */
void pcaudio_release(int voice);

/* Cut a voice off immediately - may click; prefer pcaudio_release(). */
void pcaudio_kill(int voice);

/* True while `voice` is still making sound. */
bool pcaudio_is_playing(int voice);

void pcaudio_stop_all(void);

/* ===================================================================== */
/*  Speech                                                                */
/* ===================================================================== */

/*
 *  One target position of the vocal tract.  A word is an array of these.
 *
 *  The synthesiser *glides* smoothly from wherever it currently is to
 *  each entry's targets across that entry's `ms`, rather than jumping.
 *  That gliding is the whole trick: holding still on a series of vowels
 *  sounds like a robot reciting letters, whereas moving between them the
 *  way a real mouth does is what the ear hears as connected speech.  The
 *  consonants here are largely *implied* by those movements, which is
 *  how formant synthesis has always worked.
 *
 *  ms         total length of this segment.
 *  glide_ms   how much of that is spent gliding in from the previous
 *             segment; the rest is spent *holding* the target.  This
 *             matters more than it sounds: a segment that glides for its
 *             whole duration is only ever passed through on the way to
 *             the next one, which is what makes consonants smear into
 *             their neighbours.  Give a consonant a short glide and a
 *             real hold and it snaps into focus.  0 = jump instantly;
 *             >= ms = glide the whole way (no steady state).
 *  f1/f2/f3   formant frequencies in Hz.  F1 and F2 carry nearly all the
 *             vowel identity; F3 mostly makes it sound human.
 *  f0         voice pitch in Hz, or 0 for an unvoiced sound - then the
 *             glottal pulse is replaced by noise, which is exactly what
 *             makes /h/, /s/, /f/ and whispering.  Pitch is never glided
 *             into or out of 0, so voiced/unvoiced switches cleanly.
 *  level      0..255 loudness of this segment.  Dipping it on consonants
 *             (they really are quieter than vowels) is a large part of
 *             what makes word boundaries audible.  Taper to 0 at the end
 *             of a word so it doesn't cut off abruptly.
 */
typedef struct {
    uint16_t ms;
    uint16_t glide_ms;
    uint16_t f1, f2, f3;
    uint16_t f0;
    uint8_t  level;
} pcaudio_phoneme_t;

/*
 *  Speak an arbitrary sequence at the pitch written into the table.
 *  `seq` is NOT copied - it must stay valid until the word finishes, so
 *  use a static/const array.  Returns a voice handle; the voice releases
 *  itself when the sequence ends.
 */
int  pcaudio_speak(const pcaudio_phoneme_t *seq, uint8_t count);

/*
 *  The same, transposed: the word's first voiced phoneme is put on
 *  f0_hz and every other pitch in the table is scaled by the same ratio.
 *  f0_hz = 0 means "leave the table's pitches alone", so this is exactly
 *  pcaudio_speak() - that is the default and the natural speaking voice.
 *
 *  Scaling by ratio rather than flattening to a constant is deliberate:
 *  musical intervals are ratios, so the word keeps its intonation shape
 *  as it moves, and a sequence of calls at different f0_hz gives a
 *  singable melody rather than a monotone.  Write a table with a
 *  constant f0 if you want a dead-flat sung note instead.
 *
 *  Note that the formants are deliberately NOT scaled with the pitch.
 *  A vocal tract's resonances are set by its shape, not by the note
 *  being sung, which is why a vowel stays recognisable across a
 *  singer's whole range - and why scaling them here would make high
 *  notes sound like a sped-up tape instead of a higher voice.
 *
 *  Practical range is roughly 70-400 Hz.  Push much above that and the
 *  pitch starts to approach F1 (270 Hz for /i:/, 730 for /a/), leaving
 *  too few harmonics below the formants to define the vowel - real
 *  sopranos have the same problem with intelligibility up high.
 */
int  pcaudio_speak_at(const pcaudio_phoneme_t *seq, uint8_t count,
                      uint16_t f0_hz);
