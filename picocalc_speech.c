/*
 *  picocalc_speech - vocal-tract sounds: vowels, words, singing
 *  ============================================================
 *
 *  See picocalc_speech.h.  This file is vocabulary only - the formant
 *  synthesis itself lives in picocalc_audio.c.
 *
 *  Author: Thomas Dzubin.
 */

#include <stddef.h>     /* NULL */

#include "picocalc_speech.h"

/* ===================================================================== */
/*  Vowels                                                                */
/* ===================================================================== */

/*
 *  Classic measured male-voice formant centres for the ten steady-state
 *  American-English vowels (Peterson & Barney 1952; corroborated by
 *  Hillenbrand et al. 1995's larger modern corpus, which lands on
 *  essentially the same numbers).  F1/F2 carry most of the vowel
 *  identity; F3 mainly adds "human".  PCAUDIO_VOWEL_ER is the odd one
 *  out - its very low F3, close to F2, is "r-colouring" rather than a
 *  third independent resonance, and is the same cue PCAUDIO_F_R_* in
 *  picocalc_speech.h uses for the consonant /r/.
 *
 *  These five (IY, EH, AA, AO, UW) are the same numbers this table
 *  always had, just renamed from single letters (I/E/A/O/U) to the
 *  ARPABET-style mnemonics used everywhere else now; AO in particular
 *  was labelled "O - boat" before, which was the right formants (the
 *  classic /ɔ/ triple) under the wrong name - "boat" is the diphthong
 *  OW (AO gliding to UH), not a steady-state vowel on its own, see the
 *  diphthong note in picocalc_speech.h.
 */
static const uint16_t VOWEL_FORMANTS[PCAUDIO_VOWEL_COUNT][3] = {
    { 270, 2290, 3010 },   /* IY - beet   */
    { 390, 1990, 2550 },   /* IH - bit    */
    { 530, 1840, 2480 },   /* EH - bet    */
    { 660, 1720, 2410 },   /* AE - bat    */
    { 730, 1090, 2440 },   /* AA - father */
    { 570,  840, 2410 },   /* AO - bought */
    { 440, 1020, 2240 },   /* UH - book   */
    { 300,  870, 2240 },   /* UW - boot   */
    { 640, 1190, 2390 },   /* AH - cup    */
    { 490, 1350, 1690 },   /* ER - bird   */
};

int pcaudio_vowel(pcaudio_vowel_t vowel, uint16_t f0_hz, uint16_t ms)
{
    pcaudio_patch_t v = {
        .source     = PCAUDIO_SRC_PULSE,   /* stand-in glottal pulse     */
        .freq       = f0_hz ? f0_hz : 110,
        .filter     = PCAUDIO_FILT_BP,
        .resonance  = 200,                 /* sharp formant peaks        */
        .attack_ms  = 30,
        .decay_ms   = 60,
        .sustain    = 230,
        .hold_ms    = ms,
        .release_ms = 80,
        .level      = 200,
    };

    if (vowel >= PCAUDIO_VOWEL_COUNT)
        vowel = PCAUDIO_VOWEL_AA;

    v.cutoff   = VOWEL_FORMANTS[vowel][0];
    v.formant2 = VOWEL_FORMANTS[vowel][1];
    v.formant3 = VOWEL_FORMANTS[vowel][2];

    return pcaudio_play(&v);
}

/* ===================================================================== */
/*  Words                                                                 */
/* ===================================================================== */

/*
 *  "hello" = /h/ /E/ /l/ /oU/.
 *
 *  Formant values are the usual measured male averages; the timings and
 *  the falling pitch are what make it a spoken word rather than a list
 *  of vowels.  Points worth knowing if you write your own words:
 *
 *    - /h/ is just the *following* vowel with the voice turned off
 *      (f0 = 0, so the source becomes noise).  That is genuinely what
 *      an /h/ is - aspiration shaped by wherever the mouth already is.
 *    - /l/ needs a fast glide *and* a hold (glide_ms much shorter than
 *      ms).  Gliding all the way through it means the formants only
 *      sweep past the /l/ target on their way to the /o/, so it never
 *      sounds like an /l/, just a smear between the two vowels.  Its
 *      level is dipped too: laterals really are quieter than vowels,
 *      and that dip is a large part of what the ear uses to place the
 *      consonant.
 *    - /l/'s cue is F2 dropping a long way while F3 stays high.
 *    - The "o" is a diphthong, so it is two entries: the tongue starts
 *      at /o/ and slides toward /U/, F2 rising as it goes.
 *    - f0 falls 122 -> 88 Hz across the word.  A flat pitch is the
 *      single biggest giveaway of a machine; the fall is what makes it
 *      land as a statement.
 *    - The last entry tapers level to 0 so the word ends rather than
 *      being cut off.
 */
static const pcaudio_phoneme_t hello_seq[] = {
    /*  ms  glide    F1    F2    F3    f0  level                   */
    {  70,    0,   530, 1840, 2480,   0,  90 }, /* /h/ aspiration  */
    { 120,   40,   530, 1840, 2480, 122, 230 }, /* /E/ "eh"        */
    { 115,   45,   360, 1000, 2700, 116, 150 }, /* /l/ in fast,
                                                 * then held       */
    { 175,   65,   570,  840, 2410, 105, 230 }, /* /o/ of "oh"     */
    { 130,  130,   440, 1020, 2240,  92, 185 }, /* /U/ glide       */
    {  75,   75,   430, 1030, 2240,  88,   0 }, /* fade out        */
};

/*
 *  Stop consonants (/p/ /b/ /t/ /d/ /k/ /g/) - needed by both words
 *  below, and built quite differently from the vowels in "hello".
 *
 *  A stop is a *gap* followed by a *bang*, in three parts:
 *
 *    1. Closure.  The mouth is shut, so there is nothing to hear: an
 *       entry with level 0.  Voiced stops (/b/ /d/ /g/) keep the vocal
 *       folds going behind the closure, so instead of pure silence they
 *       get a low-level buzz at a very low F1 - the "voice bar".
 *    2. Burst.  A very short unvoiced entry (f0 = 0, so noise), jumped
 *       into with glide_ms 0 because a burst is instantaneous by nature.
 *    3. Transition.  The following vowel glides away from wherever the
 *       burst put the formants.
 *
 *  Step 3 is what actually tells a listener *which* stop it was: each
 *  place of articulation has a characteristic F2 starting point (its
 *  "locus") that the vowel then moves away from.
 *
 *      labial   /p/ /b/    F2 low, around 800-900 Hz
 *      alveolar /t/ /d/    F2 around 1700-1800 Hz
 *      velar    /k/ /g/    F2 and F3 close together near 1900/2350,
 *                          the so-called "velar pinch"
 *
 *  So the burst entries below mostly differ in F2/F3, not in level or
 *  duration, and the consonant is heard in the glide that follows.
 */

/*
 *  /g/ /U/ /d/ /b/ /aI/, with the pitch lifting onto the stressed
 *  "bye" and then falling away.
 *
 *  The /d/ gets its own closure AND its own alveolar burst.  An earlier
 *  version merged the /d/ and /b/ into a single closure and released
 *  only the /b/ - which is what unhurried speech really does, but it
 *  means there is no /d/ present at all to hear, and it just sounds
 *  muddled.  It also gets a transition segment before the closure that
 *  carries F2 up from the /U/ toward the alveolar locus, because that
 *  rising F2 is the actual cue for /d/ - far more so than the burst
 *  itself, which is nearly inaudible at a closure's low level.
 */
static const pcaudio_phoneme_t goodbye_seq[] = {
    /*  ms  glide    F1    F2    F3    f0  level                    */
    {  45,    0,   250,  900, 2200, 115,  40 }, /* /g/ voice bar    */
    {  25,    0,   400, 1900, 2400,   0, 150 }, /* /g/ velar burst  */
    { 175,   55,   440, 1020, 2240, 122, 230 }, /* /U/ "oo"         */
    {  45,   45,   400, 1550, 2350, 118, 190 }, /* F2 up toward the
                                                 * alveolar locus:
                                                 * the /d/ cue      */
    {  40,    0,   300, 1650, 2400, 112,  45 }, /* /d/ closure      */
    {  22,    0,   400, 1800, 2500,   0, 150 }, /* /d/ burst        */
    {  40,    0,   280,  900, 2150, 110,  45 }, /* /b/ closure      */
    {  25,    0,   350,  800, 2100,   0, 145 }, /* /b/ labial burst */
    { 150,   60,   730, 1090, 2440, 128, 235 }, /* /a/ of "bye"     */
    { 160,  160,   400, 1900, 2600, 105, 200 }, /* glide to /I/     */
    {  80,   80,   380, 1950, 2600,  85,   0 }, /* fade             */
};

/*
 *  /p i:/ /k oU/ /k ae l k/ - "PEE-co-calc", stress on the first
 *  syllable.  Four stops, three of them velar, so most of the work is
 *  in the velar-pinch bursts and the transitions out of them.  /p/ and
 *  /k/ are voiceless, so their closures are true silence (level 0)
 *  rather than the voice bar /g/ and /b/ get above.  The final /k/ is
 *  released, which is what stops the word sounding like "PicoCal".
 *
 *  The first vowel is a long /i:/ as in "speed", not the /aI/ of "pie"
 *  - so it is one steady monophthong rather than the two-part glide a
 *  diphthong needs.  Its very low F1 (270) with a very high F2 (2290)
 *  is the widest F1/F2 split of any vowel, which is what makes /i:/
 *  instantly recognisable.
 */
static const pcaudio_phoneme_t picocalc_seq[] = {
    /*  ms  glide    F1    F2    F3    f0  level                    */
    {  50,    0,   400,  800, 2100,   0,   0 }, /* /p/ closure      */
    {  25,    0,   400,  900, 2200,   0, 160 }, /* /p/ burst        */
    { 190,   50,   270, 2290, 3010, 128, 230 }, /* /i:/ "Pee"       */
    {  45,    0,   350, 1800, 2400,   0,   0 }, /* /k/ closure      */
    {  25,    0,   450, 1900, 2350,   0, 150 }, /* /k/ velar burst  */
    { 140,   60,   570,  840, 2410, 112, 225 }, /* /oU/ of "co"     */
    {  45,    0,   400, 1000, 2300,   0,   0 }, /* /k/ closure      */
    {  25,    0,   450, 1900, 2350,   0, 150 }, /* /k/ velar burst  */
    { 120,   50,   660, 1720, 2410, 118, 230 }, /* /ae/ of "Calc"   */
    { 105,   45,   360, 1000, 2700, 108, 155 }, /* /l/ in fast,held */
    {  40,    0,   400, 1500, 2400,   0,   0 }, /* final /k/ closure*/
    {  35,    0,   450, 1900, 2350,   0, 130 }, /* final /k/ burst  */
    {  50,   50,   450, 1900, 2350,   0,   0 }, /* fade             */
};

/*
 *  "Hello, I am a PicoCalc.  I can talk.  Can you understand me?" - a
 *  full sentence rather than a single word, built from the same pieces
 *  as the three words above plus a few new ones.  Nothing stops a
 *  pcaudio_phoneme_t[] from spanning more than one lexical word: it is
 *  just a list of vocal-tract targets to glide through in order, and a
 *  sentence is exactly that, with a short silent gap at each comma/
 *  period instead of a continuous glide.  Four things worth knowing:
 *
 *    - The "Hello," and "...PicoCalc." chunks are hello_seq and
 *      picocalc_seq's own rows, copied in unchanged - both already end
 *      faded to level 0 with f0 0, which is exactly what a clean silence
 *      gap needs (see the next point), so they drop in as-is.
 *    - A gap between sentences/clauses is a dedicated silent entry
 *      (level 0, f0 0) rather than just relying on the previous word's
 *      own fade-out: the sequencer only *jumps* to a fresh onset (rather
 *      than gliding the pitch up from wherever it last was) when the
 *      *previous* entry's f0 was already 0 - see speech_load_segment()'s
 *      comment in picocalc_audio.c.  Skipping the explicit gap would
 *      still work by ear most of the time, but the gap makes the silence
 *      reliably audible and the reset reliably clean.
 *    - Word-internal stress is durations and levels, not new machinery:
 *      "understand"'s unstressed AH/ER get short ms and a lower level
 *      than its stressed AE ("-STAND-"), exactly the vowel-reduction
 *      pattern real English uses.  Its final /d/ (before the /m/ of
 *      "me") skips the release-burst row entirely - connected speech
 *      really does drop or barely voice a stop before a following nasal
 *      (the same elision goodbye_seq's comment discusses for /d/+/b/).
 *    - The question mark is a rising f0 across "me" (118 -> 165 -> 200
 *      in its three rows) where every earlier vowel in this file falls -
 *      a rising pitch is the standard English cue for a yes/no question,
 *      the mirror image of the falling statement pitch hello_seq's own
 *      comment explains.
 */
static const pcaudio_phoneme_t greeting_seq[] = {
    /*  ms  glide    F1    F2    F3    f0  level                        */

    /* --- "Hello," - hello_seq's own 6 rows, unchanged ------------ */
    {  70,    0,   530, 1840, 2480,   0,  90 }, /* /h/ aspiration        */
    { 120,   40,   530, 1840, 2480, 122, 230 }, /* /E/ "eh"              */
    { 115,   45,   360, 1000, 2700, 116, 150 }, /* /l/ in fast, held     */
    { 175,   65,   570,  840, 2410, 105, 230 }, /* /o/ of "oh"           */
    { 130,  130,   440, 1020, 2240,  92, 185 }, /* /U/ glide             */
    {  75,   75,   430, 1030, 2240,  88,   0 }, /* fade - into the gap   */
    {  90,    0,   430, 1030, 2240,   0,   0 }, /* comma pause (silent)  */

    /* --- "I am a" ---------------------------------------------- */
    { 140,   50,   730, 1090, 2440, 128, 225 }, /* AY onset (AA) - "I"   */
    { 130,  130,   390, 1990, 2550, 116, 195 }, /* AY offglide (IH)      */
    { 110,   45,   660, 1720, 2410, 112, 220 }, /* AE - "am"             */
    {  90,   40,   250,  850, 2200, 106, 130 }, /* /m/ nasal, labial     */
    {  55,   30,   640, 1190, 2390,  98,  90 }, /* AH - reduced "a"      */

    /* --- "PicoCalc." - picocalc_seq's own 13 rows, unchanged ----- */
    {  50,    0,   400,  800, 2100,   0,   0 }, /* /p/ closure           */
    {  25,    0,   400,  900, 2200,   0, 160 }, /* /p/ burst             */
    { 190,   50,   270, 2290, 3010, 128, 230 }, /* /i:/ "Pee"            */
    {  45,    0,   350, 1800, 2400,   0,   0 }, /* /k/ closure           */
    {  25,    0,   450, 1900, 2350,   0, 150 }, /* /k/ velar burst       */
    { 140,   60,   570,  840, 2410, 112, 225 }, /* /oU/ of "co"          */
    {  45,    0,   400, 1000, 2300,   0,   0 }, /* /k/ closure           */
    {  25,    0,   450, 1900, 2350,   0, 150 }, /* /k/ velar burst       */
    { 120,   50,   660, 1720, 2410, 118, 230 }, /* /ae/ of "Calc"        */
    { 105,   45,   360, 1000, 2700, 108, 155 }, /* /l/ in fast, held     */
    {  40,    0,   400, 1500, 2400,   0,   0 }, /* final /k/ closure     */
    {  35,    0,   450, 1900, 2350,   0, 130 }, /* final /k/ burst       */
    {  50,   50,   450, 1900, 2350,   0,   0 }, /* fade - into the gap   */
    { 150,    0,   450, 1900, 2350,   0,   0 }, /* period pause (silent) */

    /* --- "I can talk." ------------------------------------------ */
    { 140,   45,   730, 1090, 2440, 124, 225 }, /* AY onset (AA) - "I"   */
    { 120,  120,   390, 1990, 2550, 112, 195 }, /* AY offglide (IH)      */
    {  40,    0,   400, 1500, 2400,   0,   0 }, /* /k/ closure - "can"   */
    {  25,    0,   450, 1900, 2350,   0, 150 }, /* /k/ velar burst       */
    { 110,   45,   660, 1720, 2410, 118, 225 }, /* AE - "can"            */
    {  90,   40,   250, 1700, 2200, 110, 135 }, /* /n/ nasal, alveolar   */
    {  45,    0,   350, 1500, 2400,   0,   0 }, /* /t/ closure - "talk"  */
    {  20,    0,   400, 1800, 2500,   0, 155 }, /* /t/ alveolar burst    */
    { 130,   50,   570,  840, 2410, 108, 225 }, /* AO - "talk"           */
    {  40,    0,   400, 1000, 2300,   0,   0 }, /* /k/ closure           */
    {  30,    0,   450, 1900, 2350,   0, 140 }, /* /k/ velar burst,
                                                 * released - clearly
                                                 * ends the word, unlike
                                                 * the elided /d/ later  */
    {  55,   55,   450, 1900, 2350,   0,   0 }, /* fade - into the gap   */
    { 150,    0,   450, 1900, 2350,   0,   0 }, /* period pause (silent) */

    /* --- "Can you understand me?" -------------------------------- */
    {  40,    0,   400, 1500, 2400,   0,   0 }, /* /k/ closure - "can"   */
    {  25,    0,   450, 1900, 2350,   0, 150 }, /* /k/ velar burst       */
    { 105,   45,   660, 1720, 2410, 122, 220 }, /* AE - "can"            */
    {  85,   40,   250, 1700, 2200, 114, 130 }, /* /n/ nasal, alveolar   */
    {  55,   55,   270, 2200, 3000, 112, 130 }, /* /y/ glide - "you"     */
    { 120,   50,   300,  870, 2240, 108, 210 }, /* UW - "you"            */
    {  90,   35,   640, 1700, 2390, 108, 175 }, /* AH - "un-" (secondary
                                                 * stress: shorter and
                                                 * quieter than a
                                                 * primary-stress vowel) */
    {  55,   30,   250, 1700, 2200, 104, 120 }, /* /n/ nasal - "un-"     */
    {  35,    0,   280, 1500, 2350, 100,  40 }, /* /d/ closure, voice bar
                                                 * - "-der"              */
    {  18,    0,   400, 1800, 2500,   0, 130 }, /* /d/ alveolar burst    */
    {  70,   35,   490, 1350, 1690,  98, 150 }, /* ER - "-der" (unstressed;
                                                 * low F3 close to F2 is
                                                 * the r-colouring cue)  */
    {  70,   20,   300, 3400, 3675,   0, 130 }, /* /s/ - "st"; pushed to
                                                 * this engine's filter
                                                 * ceiling, see the
                                                 * fricative note in
                                                 * picocalc_speech.h     */
    {  40,    0,   350, 1500, 2400,   0,   0 }, /* /t/ closure, true
                                                 * silence - voiceless   */
    {  20,    0,   400, 1800, 2500,   0, 145 }, /* /t/ alveolar burst    */
    { 150,   50,   660, 1720, 2410, 120, 230 }, /* AE - "-STAND-" (primary
                                                 * stress: the loudest,
                                                 * longest vowel here)   */
    {  85,   35,   250, 1700, 2200, 112, 145 }, /* /n/ nasal - "stan-"   */
    {  45,    0,   280, 1500, 2350, 108,  35 }, /* /d/ closure, voice bar
                                                 * - final "-nd", left
                                                 * unreleased into the
                                                 * /m/ that follows, the
                                                 * way connected speech
                                                 * really elides it (the
                                                 * same trick as the
                                                 * /d/+/b/ note above)   */
    {  75,   40,   250,  850, 2200, 118, 145 }, /* /m/ nasal, labial -
                                                 * "me"; pitch already
                                                 * climbing here         */
    { 220,   90,   270, 2290, 3010, 165, 235 }, /* IY - "me?" - the
                                                 * question mark: f0
                                                 * keeps climbing across
                                                 * the whole vowel, the
                                                 * mirror image of every
                                                 * falling statement
                                                 * pitch above           */
    {  90,   90,   280, 2300, 3050, 200,   0 }, /* fade, pitch still
                                                 * rising as it dies away
                                                 * - the end of the
                                                 * question inflection   */
};

#define WORD(tbl) { (tbl), (uint8_t)(sizeof (tbl) / sizeof (tbl)[0]) }

const pcaudio_word_t pcaudio_word_hello    = WORD(hello_seq);
const pcaudio_word_t pcaudio_word_goodbye  = WORD(goodbye_seq);
const pcaudio_word_t pcaudio_word_picocalc = WORD(picocalc_seq);
const pcaudio_word_t pcaudio_word_greeting = WORD(greeting_seq);

int pcaudio_say(const pcaudio_word_t *word)
{
    if (word == NULL)
        return -1;
    return pcaudio_speak(word->seq, word->count);
}

int pcaudio_sing(const pcaudio_word_t *word, uint16_t f0_hz)
{
    if (word == NULL)
        return -1;
    return pcaudio_speak_at(word->seq, word->count, f0_hz);
}

int pcaudio_say_hello(void)    { return pcaudio_say(&pcaudio_word_hello);    }
int pcaudio_say_goodbye(void)  { return pcaudio_say(&pcaudio_word_goodbye);  }
int pcaudio_say_picocalc(void) { return pcaudio_say(&pcaudio_word_picocalc); }
int pcaudio_say_greeting(void) { return pcaudio_say(&pcaudio_word_greeting); }
