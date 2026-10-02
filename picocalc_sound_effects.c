/*
 *  picocalc_sound_effects - ready-made non-vocal sounds
 *  ====================================================
 *
 *  See picocalc_sound_effects.h.  Everything here is presets over the
 *  engine in picocalc_audio.c - no new DSP.
 *
 *  Three things learned tuning these on real PicoCalc hardware, worth
 *  knowing before changing the numbers:
 *
 *    - A resonant filter peak sweeping downward is the textbook sci-fi
 *      phaser/laser effect.  It is tempting as a way to add low-end
 *      weight, and it will make an explosion sound like a weapon.  All
 *      the noise layers here deliberately keep resonance low (~25).
 *    - Bright broadband noise with a long decay is a crash cymbal, not
 *      a detonation.  Any high-frequency layer has to be brief; the
 *      long layers have to be low.
 *    - The speakers move very little air below ~150 Hz, so the sub
 *      layers use a SAW rather than a sine: its harmonics survive and
 *      the ear reconstructs the fundamental that the speaker never
 *      produced.  Headphones, however, reproduce that sub perfectly -
 *      which is why the sub layers are kept short.  A tail that sounds
 *      right on the speakers can drone badly on headphones.
 *
 *  Author: Thomas Dzubin.
 */

#include "picocalc_sound_effects.h"

/* handles of the two sustained wind voices, -1 when not blowing */
static int wind_voice[2] = { -1, -1 };

/* likewise for the surf */
static int surf_voice[2] = { -1, -1 };

int pcaudio_explosion(void)
{
    /* 1. the crack: the initial transient only, brief and modest */
    pcaudio_patch_t crack = {
        .source     = PCAUDIO_SRC_NOISE,
        .filter     = PCAUDIO_FILT_LP,
        .cutoff     = 3000,
        .cutoff_end = 800,
        .sweep_ms   = 120,
        .resonance  = 25,
        .attack_ms  = 2,
        .decay_ms   = 150,
        .sustain    = 0,
        .release_ms = 60,
        .level      = 140,
    };

    /* 2. the body: the main event, and low-pitched from the start
     *    rather than sweeping down into it from 4 kHz             */
    pcaudio_patch_t body = {
        .source     = PCAUDIO_SRC_NOISE,
        .filter     = PCAUDIO_FILT_LP,
        .cutoff     = 600,
        .cutoff_end = 200,
        .sweep_ms   = 400,
        .resonance  = 25,
        .attack_ms  = 5,
        .decay_ms   = 870,
        .sustain    = 0,
        .release_ms = 130,
        .level      = 210,
    };

    /* 3. the sub: kept to half the body's length on purpose.  The
     *    speakers barely reproduce it, so on the PicoCalc itself a long
     *    sub is inaudible either way - but on headphones it is fully
     *    audible and a long one drags the tail out badly.          */
    pcaudio_patch_t sub = {
        .source     = PCAUDIO_SRC_SAW,
        .freq       = 150,
        .freq_end   = 35,
        .pitch_ms   = 200,
        .filter     = PCAUDIO_FILT_LP,
        .cutoff     = 550,
        .resonance  = 25,
        .attack_ms  = 3,
        .decay_ms   = 450,
        .sustain    = 0,
        .release_ms = 65,
        .level      = 225,
    };

    pcaudio_play(&sub);
    pcaudio_play(&body);
    return pcaudio_play(&crack);
}

int pcaudio_gunshot(void)
{
    /*
     *  The crack is HIGH-pass, not band-pass.  A narrow resonant band-
     *  pass keeps only the sliver of noise inside its band and throws
     *  the rest away, which made an early version of this preset so
     *  quiet it was inaudible.  A high-pass passes everything above the
     *  corner instead, so the crack stays broadband and loud - which is
     *  also what a real gunshot is.
     */
    pcaudio_patch_t crack = {
        .source     = PCAUDIO_SRC_NOISE,
        .filter     = PCAUDIO_FILT_HP,
        .cutoff     = 900,
        .cutoff_end = 400,
        .sweep_ms   = 60,
        .resonance  = 20,       /* wide, not a narrow ringing peak      */
        .attack_ms  = 0,
        .decay_ms   = 90,
        .sustain    = 0,
        .release_ms = 30,
        .level      = 165,      /* a transient, not the whole event     */
    };
    /* the report rolling away underneath it */
    pcaudio_patch_t body = {
        .source     = PCAUDIO_SRC_NOISE,
        .filter     = PCAUDIO_FILT_LP,
        .cutoff     = 1600,
        .cutoff_end = 150,
        .sweep_ms   = 220,
        .resonance  = 120,
        .attack_ms  = 0,
        .decay_ms   = 450,
        .sustain    = 0,
        .release_ms = 90,
        .level      = 200,
    };
    /*  The muzzle blast, and the main thing that separates a gunshot
     *  from a firecracker.  Halved in length for the same headphone
     *  reason as the explosion's sub above.                        */
    pcaudio_patch_t blast = {
        .source     = PCAUDIO_SRC_SAW,
        .freq       = 190,
        .freq_end   = 50,
        .pitch_ms   = 65,
        .filter     = PCAUDIO_FILT_LP,
        .cutoff     = 800,
        .resonance  = 40,
        .attack_ms  = 0,
        .decay_ms   = 130,
        .sustain    = 0,
        .release_ms = 40,
        .level      = 215,
    };

    pcaudio_play(&blast);
    pcaudio_play(&body);
    return pcaudio_play(&crack);
}

void pcaudio_wind_start(void)
{
    /* Two resonant band-passed noise voices, panned apart and wobbling
     * at slightly different rates so they never quite line up - that
     * mismatch is what stops it sounding like a static hiss.          */
    pcaudio_patch_t gust = {
        .source     = PCAUDIO_SRC_NOISE,
        .filter     = PCAUDIO_FILT_BP,
        .cutoff     = 520,
        .resonance  = 150,
        .lfo_mhz    = 270,          /* 0.27 Hz */
        .lfo_cutoff = 380,
        .lfo_level  = 120,
        .attack_ms  = 900,
        .decay_ms   = 1,
        .sustain    = 255,          /* hold until pcaudio_wind_stop()    */
        .hold_ms    = 0,
        .release_ms = 900,
        .level      = 120,
        .pan        = -90,
    };

    pcaudio_wind_stop();

    wind_voice[0] = pcaudio_play(&gust);

    gust.cutoff     = 700;
    gust.lfo_mhz    = 410;          /* deliberately not a multiple of the
                                     * first, so the two drift apart    */
    gust.lfo_cutoff = 300;
    gust.pan        =  90;
    wind_voice[1]   = pcaudio_play(&gust);
}

void pcaudio_wind_stop(void)
{
    for (int i = 0; i < 2; i++) {
        if (wind_voice[i] >= 0) {
            pcaudio_release(wind_voice[i]);
            wind_voice[i] = -1;
        }
    }
}

void pcaudio_surf_start(void)
{
    /*
     *  Surf is the same raw material as the wind - band-limited noise
     *  swelling under an LFO - but three deliberate differences are what
     *  make the ear hear sea rather than air:
     *
     *    - Low resonance (30 rather than the wind's 150).  A sharp
     *      resonant peak is a *whistle*, which is what wind does as air
     *      squeezes past edges.  Water has no such peak; it is a
     *      broadband roar, so the filter is left wide.
     *    - Low-pass rather than band-pass, and opened much further, so
     *      the foam hiss on top of the wave survives.
     *    - A far slower, far deeper swell.  0.16 Hz is one wave about
     *      every six seconds, and lfo_level 200 nearly silences it
     *      between waves - surf comes in sets, where wind merely gusts.
     *
     *  The cutoff swings with the swell as well as the level, because a
     *  breaking wave really is brightest at its peak: the hiss rises out
     *  of the rumble and falls back into it.
     */
    pcaudio_patch_t wave = {
        .source     = PCAUDIO_SRC_NOISE,
        .filter     = PCAUDIO_FILT_LP,
        .cutoff     = 1025,
        .resonance  = 30,
        .lfo_mhz    = 160,          /* 0.16 Hz - a wave every ~6 s      */
        .lfo_cutoff = 625,          /* 400 Hz in the trough, 1650 at the
                                     * crest - the crest was originally
                                     * 2200, too bright/hissy; cutoff and
                                     * lfo_cutoff are set so the crest
                                     * alone comes down 25% (2200 -> 1650)
                                     * while the trough stays at 400      */
        .lfo_level  = 200,          /* deep: nearly silent between waves */
        .attack_ms  = 1200,
        .decay_ms   = 1,
        .sustain    = 255,
        .hold_ms    = 0,            /* until pcaudio_surf_stop()        */
        .release_ms = 1200,
        .level      = 135,
        .pan        = -80,
    };

    pcaudio_surf_stop();

    surf_voice[0] = pcaudio_play(&wave);

    /* the second wave train runs at a rate that is not a multiple of the
     * first, so the two drift in and out of step over about 22 seconds
     * and the sea never repeats itself audibly.  Same crest-only 25%
     * reduction as above: crest was 1700, now 1275; trough stays at 300 */
    wave.cutoff     = 788;
    wave.lfo_mhz    = 205;
    wave.lfo_cutoff = 488;
    wave.pan        =  80;
    surf_voice[1]   = pcaudio_play(&wave);
}

void pcaudio_surf_stop(void)
{
    for (int i = 0; i < 2; i++) {
        if (surf_voice[i] >= 0) {
            pcaudio_release(surf_voice[i]);
            surf_voice[i] = -1;
        }
    }
}

/*
 *  Raindrops.
 *
 *  A falling drop is a short blip whose pitch sweeps *upward* - the
 *  cavity it makes in the water shrinks as it closes, so its resonance
 *  rises.  That rise is the whole cue; a flat blip sounds like a
 *  woodblock instead.
 *
 *  Rain is many of those scattered in time, so each drop is one voice
 *  with its own delay_ms.  The offsets below are deliberately irregular:
 *  evenly spaced drops sound like a dripping tap or a metronome rather
 *  than rain.  Higher, quieter drops read as further away, which is what
 *  gives the scatter some depth.
 */
static const struct {
    uint16_t delay_ms;
    uint16_t freq, freq_end;
    uint8_t  level;
    int8_t   pan;
} DROPS[] = {
    {   0,  850, 1450, 150, -70 },
    {  90, 1250, 2000, 110,  55 },
    { 165,  700, 1150, 165,  20 },
    { 275, 1500, 2350,  85, -35 },
    { 340,  950, 1600, 140,  85 },
    { 465, 1100, 1800, 120, -90 },
    { 580,  780, 1300, 155,  40 },
    { 690, 1350, 2150,  95, -15 },
};

int pcaudio_raindrops(void)
{
    /* rotate the starting point so two presses are never identical -
     * real rain does not repeat, and a fixed pattern is noticeable    */
    static uint8_t phase = 0;
    const int count = (int)(sizeof DROPS / sizeof DROPS[0]);
    int first = -1;

    for (int i = 0; i < count; i++) {
        const int d = (i + phase) % count;
        pcaudio_patch_t drop = {
            .source     = PCAUDIO_SRC_SINE,
            .freq       = DROPS[d].freq,
            .freq_end   = DROPS[d].freq_end,
            .pitch_ms   = 30,       /* fast rise: the drop's "plink"    */
            .filter     = PCAUDIO_FILT_OFF,
            .attack_ms  = 1,
            .decay_ms   = 55,
            .sustain    = 0,
            .release_ms = 25,
            .level      = DROPS[d].level,
            .pan        = DROPS[d].pan,
            .delay_ms   = DROPS[i].delay_ms,   /* keep the rhythm, vary
                                                * which drop lands on it */
        };
        int h = pcaudio_play(&drop);
        if (first < 0)
            first = h;
    }

    phase = (uint8_t)((phase + 3) % count);
    return first;
}

int pcaudio_waveform(pcaudio_source_t source, uint16_t hz, uint16_t ms)
{
    pcaudio_patch_t tone = {
        .source     = source,
        .freq       = hz,
        .filter     = PCAUDIO_FILT_OFF,
        .attack_ms  = 4,
        .decay_ms   = 30,
        .sustain    = 220,
        .hold_ms    = ms,
        .release_ms = 40,
        .level      = 180,
    };
    return pcaudio_play(&tone);
}

int pcaudio_tone(uint16_t hz, uint16_t ms)
{
    return pcaudio_waveform(PCAUDIO_SRC_SINE, hz, ms);
}
