/*
 *  picocalc_sound_effects - ready-made non-vocal sounds
 *  ====================================================
 *
 *  Wind, explosions, gunfire and plain tones, built on the synthesis
 *  engine in picocalc_audio.c/.h.  This file holds nothing but presets:
 *  every one of them is an ordinary pcaudio_patch_t that any caller
 *  could have filled in, so treat the numbers as a starting point and
 *  tune them by ear rather than as anything fixed.
 *
 *  Split out from picocalc_audio so a project can take the engine plus
 *  just these, without the vocal-tract code in picocalc_speech.c (or
 *  vice versa).  Call pcaudio_init() before any of them.
 *
 *  Author: Thomas Dzubin.
 */

#pragma once

#include <stdint.h>

#include "picocalc_audio.h"

/*
 *  Three layers split by frequency: a brief bright crack, a low-pitched
 *  noise body, and a pitched-down saw sub underneath.  Returns the
 *  crack's voice handle.
 */
int  pcaudio_explosion(void);

/*
 *  Crack, report and muzzle blast.  The blast - a fast downward pitch
 *  sweep - is what separates this from a firecracker, which is
 *  essentially all crack and no low end.
 */
int  pcaudio_gunshot(void);

/*
 *  Sustained stereo wind: two resonant band-passed noise voices panned
 *  apart, wobbling at deliberately mismatched LFO rates so they never
 *  line up and it never settles into a static hiss.  Runs until
 *  pcaudio_wind_stop().
 *
 *  (pcaudio_stop_all() also silences it, but leaves this module's saved
 *  handles stale.  That is harmless - handles are serial-validated, so
 *  stopping a voice that has already gone is a no-op - and the next
 *  pcaudio_wind_start() clears them anyway.)
 */
void pcaudio_wind_start(void);
void pcaudio_wind_stop(void);

/*
 *  Sustained stereo surf.  Built like the wind but shaped as a broadband
 *  roar rather than a whistle, on a much slower swell - see the notes on
 *  the preset for why those two differences are what separate "sea" from
 *  "wind" to the ear.  Runs until pcaudio_surf_stop().
 */
void pcaudio_surf_start(void);
void pcaudio_surf_stop(void);

/*
 *  A short scatter of falling water drops - a one-shot, not a loop.
 *  Each call plays a slightly different pattern so repeats don't sound
 *  mechanical.  Returns the handle of the first drop.
 */
int  pcaudio_raindrops(void);

/* A plain filtered tone `ms` long - handy for UI beeps.  Equivalent to
 * pcaudio_waveform(PCAUDIO_SRC_SINE, hz, ms) below.                     */
int  pcaudio_tone(uint16_t hz, uint16_t ms);

/*
 *  The same plain tone as pcaudio_tone(), but letting the caller pick
 *  which raw oscillator shape plays it - see pcaudio_source_t in
 *  picocalc_audio.h.  Meant for hearing what the engine's waveforms
 *  actually sound like on their own, unfiltered, at a fixed pitch:
 *
 *      PCAUDIO_SRC_SINE      pure tone - osc_sine()'s parabolic
 *                            approximation, within ~0.2% of a true sine
 *      PCAUDIO_SRC_TRIANGLE  softer than a square, brighter than a sine
 *                            - odd harmonics falling off as 1/n^2
 *      PCAUDIO_SRC_SQUARE    hollow, reedy - odd harmonics only, falling
 *                            off as 1/n (buzzier than the triangle)
 *      PCAUDIO_SRC_SAW       brightest/buzziest - every harmonic, falling
 *                            off as 1/n
 *      PCAUDIO_SRC_PULSE     the narrow glottal-pulse stand-in speech
 *                            uses (picocalc_speech.c) - thin and nasal
 *                            at a fixed pitch instead of gliding formants
 *
 *  PCAUDIO_SRC_NOISE isn't included here on purpose: `freq` is ignored
 *  for it (see the pcaudio_source_t comment in picocalc_audio.h), so it
 *  has no fixed pitch to compare against the others - it's already
 *  demonstrated by pcaudio_explosion()/wind/surf/raindrops above.
 */
int  pcaudio_waveform(pcaudio_source_t source, uint16_t hz, uint16_t ms);
