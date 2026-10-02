# Changelog

All notable changes to PicoCalc Speech.
Format loosely follows [Keep a Changelog](https://keepachangelog.com/);
the version here matches the `VERSION` define in `main.c`.

## [Unreleased]

### Added
- Build outputs are named without the `picocalc-` prefix, since the chip or system
  at the end of the name already says what they are for: `speech-RP2040.uf2`, `speech-RP2350.uf2`, `speech-Windows.exe` and `speech-Linux`.
- Leaving: `Q` now stops all sound and leaves for the PicoCalc UF2 Loader menu
  (it asks the loader for its menu through the watchdog scratch registers,
  constants in the new `loader_const.h`; with no loader installed the program
  just restarts, and on a PC it closes). The help screen lists it; `ESC` keeps
  its job and `~` still goes to BOOTSEL.
- Vendored `drivers/fat32.c`: `get_next_free_cluster()` now advances its search
  hint past the cluster it hands out (a local fix, not in upstream), so a long
  sequential write to the SD card no longer re-scans every cluster already given
  out and looks hung.
- Desktop build: the build logic that was repeated in `desktop/CMakeLists.txt` now
  lives in `desktop/shim_desktop.cmake` (that file now only names the program and
  its sources), and the shim files were refreshed from a shared copy. The numeric
  keypad now works in the desktop build.
- Windows build: `desktop/` holds a small SDL2 shim that re-implements the
  picocalc-text-starter LCD/keyboard drivers and emulates the PWM sample
  interrupt, so the same `main.c` and sound engine run on a PC at the normal
  22050 Hz; SDL2 is linked statically, giving one self-contained
  `speech-Windows.exe`. Build with
  `cmake -S desktop -B build-windows -G Ninja` then `ninja -C build-windows`
  from an MSYS2 UCRT64 shell.
- Linux build: the `desktop/` build (formerly `windows/`) now also builds on
  Linux and produces `speech-Linux`. SDL2 is linked dynamically there and the
  build copies `libSDL2-2.0.so.0` next to the executable (found through an
  `$ORIGIN` run-path); needs the SDL2 development package (e.g. `libsdl2-dev`).
- The firmware build now names every output with the chip (the `.uf2`, `.elf`, `.bin` and `.hex` all end in `-RP2040` / `-RP2350`) and copies the `.uf2` to the top-level folder as
  `speech-RP2040.uf2` / `speech-RP2350.uf2`, so the two
  chips' builds sit side by side.

## [V0.46] - 2026-09-13

### Removed
- The "Daisy Bell" sung chorus (`pcaudio_word_daisy_bell`,
  `pcaudio_sing_daisy_bell()`, the `3` key) — despite retuning its tempo
  and pitch last round, it still didn't sound right and wasn't worth
  further tuning.
- The HAL 9000 sentence (`pcaudio_word_hal9000`, `pcaudio_say_hal9000()`,
  the `2` key), removed at the same time.
- `main.c`'s `1` (greeting sentence) and `4`-`8` (waveform demo) keys are
  unaffected; `2` and `3` are simply unused now rather than renumbered.

## [V0.45] - 2026-09-13

### Added
- `pcaudio_waveform(source, hz, ms)` in `picocalc_sound_effects.c/.h` — a
  generalised version of the existing `pcaudio_tone()` (now just
  `pcaudio_waveform(PCAUDIO_SRC_SINE, ...)`) that plays any of the
  engine's oscillator shapes as a plain, unfiltered tone. `PCAUDIO_SRC_SINE`
  and `PCAUDIO_SRC_TRIANGLE` were already implemented in
  `picocalc_audio.c` but had never been wired to anything audible.
- `main.c`: keys `4`-`8` each play `TONE_HZ` (500 Hz) on a different
  waveform — sine, triangle, square, sawtooth, pulse — so the shapes can
  be compared directly at a fixed pitch. `PCAUDIO_SRC_NOISE` isn't
  included (no fixed pitch to compare - already demoed by `E`/`G`/`W`/
  `Z`/`X`). Added to the help screen and the header's Controls list.

## [V0.44] - 2026-09-12

### Changed
- `picocalc_speech.c`: retuned `daisy_bell_seq` after testing on real
  hardware — every duration is now an additional 1.5x longer (on top of
  the per-line slowdown already baked in), every pitch is one octave
  lower, and the silent gaps between lines are now two distinct lengths:
  a shorter one at a comma ("marriage," "carriage,") and a longer one at
  a full stop ("do!" "you!"), instead of one ungraded pause length.

## [V0.43] - 2026-09-12

### Added
- `pcaudio_word_daisy_bell` in `picocalc_speech.c`: the chorus of "Daisy
  Bell" (Harry Dacre, 1892 - public domain), sung the way HAL 9000 does
  in its own deactivation scene - normal tempo/pitch at first, then each
  of the six lines is transposed down and slowed further, ending on a
  long, fading final note. Consonants are simplified to single quick
  rows (rather than the full closure+burst treatment elsewhere) since a
  legato sung line leans on the vowels for pitch.
- `main.c`: the `3` key sings it (`pcaudio_sing_daisy_bell()`), added to
  the help screen and the header's Controls list.

## [V0.42] - 2026-09-12

### Added
- `pcaudio_word_hal9000` in `picocalc_speech.c`: "Good afternoon,
  gentlemen. I am a HAL 9000 computer. I became operational at the HAL
  plant in Urbana, Illinois on the 12th of January 1992." (152 phoneme
  rows) - "HAL" is spoken as one word ("Hal", rhyming with "pal"), the
  way it's actually said in the film, not spelled out letter by letter.
- `main.c`: the `2` key speaks it (`pcaudio_say_hal9000()`), added to the
  help screen and the header's Controls list.

## [V0.41] - 2026-09-12

### Changed
- `main.c`: every demo key now stops whatever sound is currently playing
  before starting its own (`stop_all_sounds()`), so sounds are always
  independent and never overlap — pressing `W` then `G` stops the wind
  the instant the gunshot starts, instead of mixing the two. `W`/`Z`
  still toggle off when pressed while that same sound is the one
  playing; any other key silences them first.

## [V0.4] - 2026-09-12

### Added
- `main.c`: `ESC` stops all sound (`pcaudio_stop_all()`) and resets to
  the ready-for-input state (`wind_on`/`surf_on` cleared, help screen and
  "NOTHING YET" status redrawn) — added to the help screen and the
  header's Controls list.

### Fixed
- `picocalc_sound_effects.c`: the surf preset's crest was too bright
  (2200 Hz / 1700 Hz for the two wave trains) — brought both down 25%
  (to 1650 Hz / 1275 Hz) by adjusting `cutoff`/`lfo_cutoff` so only the
  peak of the swell drops; the trough (400 Hz / 300 Hz) is unchanged.

## [V0.3] - 2026-09-12

### Added
- `pcaudio_word_greeting` in `picocalc_speech.c`: a full sentence -
  "Hello, I am a PicoCalc. I can talk. Can you understand me?" - built by
  splicing `hello_seq` and `picocalc_seq` together with new words ("I",
  "am", "a", "can", "talk", "you", "understand", "me") and silent gaps at
  each comma/period, demonstrating that a `pcaudio_phoneme_t[]` can span
  an arbitrary sentence, not just one lexical word. The final "me?" rises
  in pitch to sell the question mark, the mirror image of every other
  word's falling statement pitch.
- `main.c`: the `1` key speaks it (`pcaudio_say_greeting()`), added to the
  help screen and the header's Controls list.

## [V0.2] - 2026-09-12

### Added
- A much bigger formant/phoneme reference in `picocalc_speech.h`, so new
  English words are a lookup instead of a guess:
  - The vowel set grew from 5 to the full 10 steady-state American-English
    monophthongs (added IH, AE, UH, AH, ER), renamed from single letters
    (A/E/I/O/U) to standard ARPABET-style mnemonics (AA/AE/AH/AO/EH/ER/
    IH/IY/UH/UW) for consistency across all 10.
  - A diphthong section documenting the 5 English diphthongs (EY, AY, OY,
    AW, OW) as onset/offset vowel pairs — no new table needed, since a
    diphthong is just a glide between two of the existing vowel targets.
  - Named `PCAUDIO_LOCUS_*` constants for the labial/alveolar/velar
    places of articulation shared by the stops and nasals.
  - Formant reference constants for the three nasals (`/m n ng/`), four
    fricative places (`/f v/`, `/th dh/`, `/s z/`, `/sh zh/`), both
    liquids (`/l/`, and `/r/` — new, not used by any word yet), and both
    glides (`/w/`, `/y/`).
- A note on the fricative reference explaining this engine's ~3675 Hz
  filter ceiling (`CUTOFF_MAX` in `picocalc_audio.c`) and why sibilants
  can't be pushed brighter than that on this hardware.

### Changed
- `pcaudio_vowel_t`'s enum values renamed (A→AA, E→EH, I→IY, O→AO, U→UW)
  to fit the new naming scheme; `PCAUDIO_VOWEL_A`'s old "boat" comment was
  corrected to "bought" — the formants were always right, the label
  wasn't (see the comment above `VOWEL_FORMANTS` in `picocalc_speech.c`).
  `main.c`'s `V` key updated to match (still plays the same "ah" sound).

## [V0.1] - 2026-09-12

### Added
- New project: the audio engine
  (`picocalc_audio.c/.h`), the sound-effects presets
  (`picocalc_sound_effects.c/.h` — explosion, gunshot, wind, surf,
  raindrops) and the speech module (`picocalc_speech.c/.h` — vowels,
  words, singing), reusing the shared engine both build on.
- `main.c`: a static help screen listing the E/G/W/Z/X sound
  effect keys and the V/H/J/K/S speech keys, plus a "LAST: ..." status
  line that updates on every key that plays something.

### Removed
- All shape-drawing and colour-picker code carried over from the
  starting point (the six shapes, rotation math, polygon fill, the
  rainbow hue strip and colour-selection arrow, and the UP/DOWN/F1/F4/F5/
  LEFT/RIGHT shape controls) — this project does no drawing beyond the
  help screen and status line.

### Changed
- Renamed the top-level program file from `picocalc-speech.c` to
  `main.c` — it was easy to confuse with `picocalc_speech.c` (the
  underlying vowels/words/singing module), so the file that owns
  `main()` and wires everything else together now has an unambiguous
  name.
