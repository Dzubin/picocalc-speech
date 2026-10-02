/*
 *  picocalc_speech - vocal-tract sounds: vowels, words, singing
 *  ============================================================
 *
 *  Built on the formant synthesiser in picocalc_audio.c/.h, which does
 *  the actual work: a glottal pulse (or noise, for unvoiced sounds)
 *  through three parallel resonant band-passes at the formant
 *  frequencies, with everything gliding between targets under the
 *  control of a phoneme sequencer.  This file holds the vocabulary -
 *  the vowel table and the word tables - plus convenience wrappers.
 *
 *  Split out from picocalc_audio so a project can take the engine plus
 *  just the sound effects in picocalc_sound_effects.c without carrying
 *  the words, or vice versa.  Call pcaudio_init() before any of this.
 *
 *  Expect a Speak & Spell / SAM sort of voice.  That is inherent to a
 *  pulse-plus-three-formants model rather than a shortcoming of the
 *  tuning: it is the same technique those chips used.
 *
 *  Author: Thomas Dzubin.
 */

#pragma once

#include <stdint.h>

#include "picocalc_audio.h"

/* ===================================================================== */
/*  Monophthong vowels                                                    */
/* ===================================================================== */

/*
 *  The ten steady-state American-English vowels, roughly front-to-back /
 *  high-to-low the way a vowel chart is usually drawn.  Names follow the
 *  common ARPABET-style mnemonics (as used by CMUdict / TIMIT and most
 *  speech-synthesis code) rather than IPA symbols, since they're plain
 *  ASCII and every one of them is a real English word's vowel sound.
 *  Formant numbers are in VOWEL_FORMANTS in picocalc_speech.c - see the
 *  comment there for where they come from and how to read them when
 *  writing a new word.
 */
typedef enum {
    PCAUDIO_VOWEL_IY = 0,   /* beet, see    */
    PCAUDIO_VOWEL_IH,       /* bit, sit     */
    PCAUDIO_VOWEL_EH,       /* bet, bed     */
    PCAUDIO_VOWEL_AE,       /* bat, cat     */
    PCAUDIO_VOWEL_AA,       /* father, hot  */
    PCAUDIO_VOWEL_AO,       /* bought, law  */
    PCAUDIO_VOWEL_UH,       /* book, put    */
    PCAUDIO_VOWEL_UW,       /* boot, food   */
    PCAUDIO_VOWEL_AH,       /* cup, other (stressed "uh"; also a workable
                             * stand-in for the unstressed schwa /e/ in
                             * "sofa", "about" - a formant synth this
                             * simple can't tell the difference anyway)  */
    PCAUDIO_VOWEL_ER,       /* bird, her - see the r-colouring note below */
    PCAUDIO_VOWEL_COUNT
} pcaudio_vowel_t;

/*
 *  A sung vowel: a pulse train at f0_hz through three parallel resonant
 *  band-passes at that vowel's formants.  f0_hz 0 defaults to 110 Hz.
 */
int  pcaudio_vowel(pcaudio_vowel_t vowel, uint16_t f0_hz, uint16_t ms);

/* ===================================================================== */
/*  Diphthongs                                                            */
/* ===================================================================== */

/*
 *  A diphthong is not a sound of its own - it's a glide from one
 *  monophthong's target to another's, exactly the mechanism
 *  pcaudio_phoneme_t already provides.  So there is no separate
 *  diphthong table: write two entries, the onset vowel held briefly and
 *  the offset vowel glided into and held to taste (hello_seq's "oh" and
 *  goodbye_seq's "bye" below are both worked examples of this).  The
 *  five English diphthongs, as (onset vowel -> offset vowel) pairs using
 *  the PCAUDIO_VOWEL_* names above:
 *
 *      EY  (bait, day)   AE -> IH   (starts a little higher than AE in
 *                                    real speech; AE is close enough)
 *      AY  (bite, my)    AA -> IH   see goodbye_seq's "bye"
 *      OY  (boy, toy)    AO -> IH
 *      AW  (bout, how)   AA -> UH
 *      OW  (boat, go)    AO -> UH   see hello_seq's "oh"
 */

/* ===================================================================== */
/*  Consonant place-of-articulation loci                                  */
/* ===================================================================== */

/*
 *  Stops (/p b/, /t d/, /k g/) and nasals (/m/, /n/, /ng/) are made at
 *  the same three places in the mouth, and share the same F2 "locus" -
 *  the point their neighbouring vowel's F2 transition points toward (or
 *  away from).  That locus, not the very brief and quiet burst/murmur
 *  itself, is the main thing a listener uses to tell them apart - see
 *  the long comment above goodbye_seq in picocalc_speech.c for a full
 *  worked example of building a stop from closure + burst + transition.
 *
 *  Real /t d s z/ bursts/frications are far higher-frequency than their
 *  locus (see the fricative note below); using the locus value for the
 *  brief burst entry too, as the three example words already do, is a
 *  workable simplification rather than a measured burst spectrum.
 */
#define PCAUDIO_LOCUS_LABIAL_F2    (850)   /* p b m - lips              */
#define PCAUDIO_LOCUS_ALVEOLAR_F2  (1700)  /* t d n - tongue tip/ridge  */
#define PCAUDIO_LOCUS_VELAR_F2     (1900)  /* k g ng - tongue back      */
#define PCAUDIO_LOCUS_VELAR_F3     (2350)  /* ditto - the "velar pinch":
                                             * F2 and F3 sitting close
                                             * together is the main cue
                                             * that separates k/g from
                                             * the other two places      */

/* ===================================================================== */
/*  Nasals                                                                */
/* ===================================================================== */

/*
 *  /m n ng/ are voiced continuants like vowels (no closure/burst split
 *  needed - just hold a target the way hello_seq's /l/ does), but with a
 *  very low F1 from the added nasal cavity and a level dipped well below
 *  a vowel's, the same way hello_seq's /l/ is dipped.  That low F1 is
 *  the main perceptual cue that a nasal is a nasal.  Each one's F2 (and,
 *  for /ng/, F3) is its stop cousin's locus above.
 */
#define PCAUDIO_F_NASAL_F1  (250)
#define PCAUDIO_F_NASAL_F3  (2200)  /* /m/ and /n/ - no strong measured
                                     * value; this works well by ear.
                                     * /ng/ uses PCAUDIO_LOCUS_VELAR_F3
                                     * instead, like its stop cousin.    */

/* ===================================================================== */
/*  Fricatives                                                            */
/* ===================================================================== */

/*
 *  Hiss (or, voiced, hiss-plus-buzz) shaped by the same three resonant
 *  bands as everything else here, just driven by noise (f0 = 0) instead
 *  of a glottal pulse.  A voiced fricative (/v dh z zh/) adds a quiet,
 *  low-F1 voice bar under the noise - build it the same way a voiced
 *  stop's closure gets one (see the stop-consonant comment in
 *  picocalc_speech.c) rather than by changing these numbers.
 *
 *  This engine's resonant filters top out at CUTOFF_MAX in
 *  picocalc_audio.c (~3675 Hz at the default 22050 Hz sample rate) -
 *  well below where /s/ and /z/ actually peak (4-8 kHz).  PCAUDIO_F_S_F3
 *  below is already pinned to that ceiling; there is no way to make
 *  sibilants brighter than this on this hardware, so lean on `resonance`
 *  and `level` rather than chasing a higher frequency.
 */
#define PCAUDIO_F_F_F1   (250)   /* f v (labiodental) - weak and diffuse,
                                  * no strong peak; a low level sells it
                                  * more than exact placement does       */
#define PCAUDIO_F_F_F2   (1400)
#define PCAUDIO_F_F_F3   (2200)

#define PCAUDIO_F_TH_F1  (300)   /* th, as in "thin" or "this" - similar
                                  * to f/v: weak, diffuse, slightly higher */
#define PCAUDIO_F_TH_F2  (1600)
#define PCAUDIO_F_TH_F3  (2300)

#define PCAUDIO_F_S_F1   (300)   /* s z - the brightest, most energetic
                                  * fricative; push toward the ceiling   */
#define PCAUDIO_F_S_F2   (3400)
#define PCAUDIO_F_S_F3   (3675)  /* the engine's CUTOFF_MAX              */

#define PCAUDIO_F_SH_F1  (300)   /* sh, as in "she"; zh as in "measure" -
                                  * lower and more hushed than s/z       */
#define PCAUDIO_F_SH_F2  (2200)
#define PCAUDIO_F_SH_F3  (3000)

/* /h/ has no place of its own: it's just noise shaped by whatever vowel
 * comes next (see the /h/ note above hello_seq in picocalc_speech.c),
 * so reuse that vowel's own F1/F2/F3 with f0 = 0 - no constants needed. */

/* ===================================================================== */
/*  Liquids                                                               */
/* ===================================================================== */

#define PCAUDIO_F_L_F1   (360)   /* /l/ - see hello_seq's /l/ for a full
                                  * worked example: fast glide in, a real
                                  * hold, and a dipped level.  F2 drops a
                                  * long way while F3 stays high.        */
#define PCAUDIO_F_L_F2   (1000)
#define PCAUDIO_F_L_F3   (2700)

#define PCAUDIO_F_R_F1   (300)   /* /r/ - not used by any word here yet.
                                  * Its whole identity is F3 dropping
                                  * close to F2 ("r-colouring"), the same
                                  * cue PCAUDIO_VOWEL_ER holds through an
                                  * entire vowel instead of a quick
                                  * consonant glide.                     */
#define PCAUDIO_F_R_F2   (1300)
#define PCAUDIO_F_R_F3   (1600)

/* ===================================================================== */
/*  Glides (semivowels)                                                   */
/* ===================================================================== */

/*
 *  /w/ and /y/ are vowel-like targets that are never held on their own -
 *  give them a short ms with glide_ms == ms (glide the whole segment, no
 *  steady state), the same trick a diphthong's offglide uses.
 */
#define PCAUDIO_F_W_F1   (300)   /* /w/ - starts near /u/, rounded       */
#define PCAUDIO_F_W_F2   (610)
#define PCAUDIO_F_W_F3   (2200)

#define PCAUDIO_F_Y_F1   (270)   /* /y/ (as in "yes") - starts near /i/  */
#define PCAUDIO_F_Y_F2   (2200)
#define PCAUDIO_F_Y_F3   (3000)

/* ===================================================================== */
/*  Words                                                                 */
/* ===================================================================== */

/*
 *  A word is just a phoneme table and its length, bundled so it can be
 *  passed around (and so it can be sung at an arbitrary pitch).  Write
 *  your own with a static const pcaudio_phoneme_t[] and one of these
 *  pointing at it; see picocalc_speech.c for four worked examples - three
 *  single words plus a full sentence (pcaudio_word_greeting) built by
 *  splicing several words and pauses into one sequence - and for how
 *  stop consonants are built.  Everything above is the building blocks
 *  (vowels, diphthongs, stop/nasal loci, fricatives, liquids, glides) to
 *  reach for instead of guessing new numbers.
 */
typedef struct {
    const pcaudio_phoneme_t *seq;
    uint8_t count;
} pcaudio_word_t;

extern const pcaudio_word_t pcaudio_word_hello;
extern const pcaudio_word_t pcaudio_word_goodbye;
extern const pcaudio_word_t pcaudio_word_picocalc;
extern const pcaudio_word_t pcaudio_word_greeting;

/*
 *  Speak `word` in its natural voice - the pitch contour written into
 *  its table, which is what makes it sound spoken rather than sung.
 */
int  pcaudio_say(const pcaudio_word_t *word);

/*
 *  Sing `word` with its first voiced sound placed on f0_hz.  The rest
 *  of the table's pitch contour is transposed by the same ratio, so the
 *  word keeps its natural inflection while sitting on the note you
 *  asked for; call it repeatedly with different pitches to build a
 *  melody.  f0_hz = 0 is identical to pcaudio_say().
 *
 *  Useful pitches, if you want to sing something recognisable:
 *
 *      A2 110   C3 131   D3 147   E3 165   F3 175   G3 196
 *      A3 220   C4 262   D4 294   E4 330   F4 349   G4 392
 *
 *  The tables here sit around 110 Hz, so 110 sounds essentially like
 *  normal speech and 220 is an octave up.  Roughly 70-400 Hz is the
 *  usable range - see the note on pcaudio_speak_at() for why vowels
 *  lose definition above that.
 */
int  pcaudio_sing(const pcaudio_word_t *word, uint16_t f0_hz);

/* convenience wrappers, each equivalent to pcaudio_say() on the
 * matching pcaudio_word_* above                                       */
int  pcaudio_say_hello(void);
int  pcaudio_say_goodbye(void);
int  pcaudio_say_picocalc(void);
int  pcaudio_say_greeting(void);
