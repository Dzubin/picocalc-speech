/*
 *  Speech & Sound Demo
 *  ===================
 *
 *  Standalone PicoCalc demo for the audio engine in picocalc_audio.c/.h
 *  (PWM slice as a ~12-bit DAC, one PWM period per sample, noise +
 *  resonant filter + envelope per voice) and its two optional preset
 *  modules: picocalc_sound_effects.c/.h (explosion / gunshot / wind /
 *  surf / raindrops) and picocalc_speech.c/.h (vowels, words, singing -
 *  a Speak & Spell / SAM sort of voice, built on three parallel formant
 *  band-passes gliding under a phoneme sequencer).  Expect that voice
 *  quality - it's inherent to the pulse-plus-three-formants model, not a
 *  tuning shortfall.
 *
 *  Split off from a shapes-drawing demo that used to bolt the same keys
 *  onto its own screen so the shape-drawing code and its colour picker
 *  could be tried without carrying this along, or vice versa.  This
 *  program does no drawing beyond a static list of controls and a
 *  one-line "last played" status - see draw_help_screen() / show_status().
 *
 *  pcaudio_init(true) puts its sample interrupt on core 1, out of reach
 *  of the interrupt masking lcd_blit() does on the drawing core - kept
 *  even though this program's screen barely changes, for consistency
 *  with the shapes demo this one was split from.
 *
 *  Built on the drivers from "picocalc-text-starter" by Blair Leduc
 *  (drivers/lcd.c, drivers/southbridge.c): lcd_init(), lcd_solid_rectangle(),
 *  lcd_putstr() for drawing and sb_init() / sb_read_keyboard() for raw
 *  keyboard-FIFO polling, the same pattern picocalc-music-maker uses.  We
 *  do NOT call picocalc_init(), which would start a background poll
 *  draining the same FIFO.  (picocalc_audio.c drives the speakers itself
 *  and does not use the starter's drivers/audio.c at all.)
 *
 *  Controls:
 *      E      - explosion        )
 *      G      - gunshot          )  picocalc_sound_effects presets;
 *      W      - wind on/off      )  letters because the PicoCalc's
 *      Z      - surf on/off      )  function keys stop at F5
 *      X      - raindrops        )
 *      V      - sung vowel       )
 *      H      - says "hello"     )  picocalc_speech
 *      J      - says "goodbye"   )
 *      K      - says "PicoCalc"  )
 *      S      - sings "hello" an octave up (pitch scaling demo)
 *      1      - says the full greeting sentence (pcaudio_word_greeting -
 *               several words and pauses spliced into one sequence)
 *      4      - sine wave      )
 *      5      - triangle wave  )  all at TONE_HZ (500 Hz) via
 *      6      - square wave    )  pcaudio_waveform() - hear the engine's
 *      7      - sawtooth wave  )  raw oscillator shapes on their own
 *      8      - pulse wave     )
 *      ESC    - stop all sound and reset to the ready-for-input state
 *               (redraws the help screen, same as at startup)
 *      Q      - stop all sound and leave for the PicoCalc UF2 Loader menu
 *      ~ (SHIFT + backtick) - reboot into BOOTSEL (USB drive) mode
 *
 *  Author: Thomas Dzubin.
 */

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/bootrom.h"   /* reset_usb_boot() - BOOTSEL reboot ('~') */
#include "hardware/watchdog.h" /* watchdog_hw, watchdog_reboot() - exit to the UF2 Loader */

#include "loader_const.h"   /* the UF2 Loader hand-off values           */

#include "lcd.h"            /* WIDTH, HEIGHT, RGB(), lcd_*, GLYPH_HEIGHT,
                             * ROWS, MAX_ROW                            */
#include "southbridge.h"    /* sb_init(), sb_read_keyboard()           */
#include "keyboard.h"       /* KEY_STATE_*, KEY_ESC                     */

#include "picocalc_audio.h"         /* sound engine: pcaudio_init(),
                                     * pcaudio_source_t for the 4-8 keys */
#include "picocalc_sound_effects.h" /* E / G / W / Z / X / 4 / 5 / 6 / 7 /
                                     * 8 demo keys                      */
#include "picocalc_speech.h"        /* V / H / J / K / S / 1 demo keys  */

#define VERSION "V0.46"     /* matches CHANGELOG.md's heading for this round */

#define COL_BG   RGB(  0,   0,   0)
#define COL_TEXT RGB(235, 235, 235)

#define TONE_HZ  (500)      /* shared frequency for the 4/5/6/7/8 waveform
                             * demo keys, so the only thing that changes
                             * between them is the oscillator shape       */
#define TONE_MS  (700)

#define TCOLS         (40)             /* 320 / 8px-wide glyphs, built-in
                                         * 8x10 font (see put_row)          */

/* The physical PicoCalc's bezel/screen makes MAX_ROW (the very bottom
 * text row) hard to see, so it's off-limits for anything but a possible
 * future debug line - the status line lives one row above it instead.    */
#define STATUS_ROW    (MAX_ROW - 1)

static inline uint16_t key_event(void)      { return sb_read_keyboard(); }
static inline uint8_t  ev_state(uint16_t e) { return (e >> 8) & 0xFF; }
static inline uint8_t  ev_code(uint16_t e)  { return e & 0xFF; }

/* Every demo key below calls this before starting its own sound, so
 * sounds are always independent and never overlap: press W then G and
 * the wind stops the instant the gunshot starts, rather than the two
 * mixing.  pcaudio_stop_all() only silences the voices - it doesn't
 * know about wind_on/surf_on, so those are reset here too, or a later W
 * / Z press would see one still "on" and stop it instead of starting a
 * fresh sound.                                                          */
static void stop_all_sounds(bool *wind_on, bool *surf_on)
{
    pcaudio_stop_all();
    *wind_on = false;
    *surf_on = false;
}

static void clear_row(int row)
{
    lcd_solid_rectangle(COL_BG, 0, (uint16_t)(row * GLYPH_HEIGHT), WIDTH, GLYPH_HEIGHT);
}

/* clear a text row and print a string centred on it */
static void put_row(int row, const char *str, uint16_t fg)
{
    clear_row(row);
    int len = (int)strlen(str);
    int col = (TCOLS - len) / 2;
    if (col < 0) col = 0;
    lcd_set_foreground(fg);
    lcd_putstr((uint8_t)col, (uint8_t)row, str);
}

/* shows what was last triggered, e.g. "LAST: SAYS \"HELLO\"" - updated on
 * every key below that actually plays a sound                            */
static void show_status(const char *what)
{
    char buf[40];
    snprintf(buf, sizeof buf, "LAST: %s", what);
    put_row(STATUS_ROW, buf, COL_TEXT);
}

/* one-time static list of controls - see the header comment for the same
 * text in comment form                                                   */
static void draw_help_screen(void)
{
    lcd_solid_rectangle(COL_BG, 0, 0, WIDTH, HEIGHT);

    static const char *const lines[] = {
        "SPEECH & SOUND DEMO",
        "",
        "E - EXPLOSION",
        "G - GUNSHOT",
        "W - WIND ON/OFF",
        "Z - SURF ON/OFF",
        "X - RAINDROPS",
        "V - SUNG VOWEL",
        "H - SAYS \"HELLO\"",
        "J - SAYS \"GOODBYE\"",
        "K - SAYS \"PICOCALC\"",
        "S - SINGS \"HELLO\" AN OCTAVE UP",
        "1 - SAYS THE GREETING SENTENCE",
        "4 - SINE WAVE   5 - TRIANGLE WAVE",
        "6 - SQUARE WAVE 7 - SAWTOOTH WAVE",
        "8 - PULSE WAVE",
        "ESC - STOP ALL SOUND",
        "Q - BACK TO THE UF2 LOADER",
    };
    int first_row = 3;
    for (size_t i = 0; i < sizeof lines / sizeof lines[0]; i++)
        put_row(first_row + (int)i, lines[i], COL_TEXT);

    show_status("NOTHING YET");
}

/* Leaves the program for the PicoCalc UF2 Loader's menu: stops every sound,
 * shows a short message, asks the loader for its menu (see
 * LOADER_COMMAND_MAGIC in loader_const.h) and reboots with the watchdog. If
 * the program was flashed straight to the chip with no loader, nothing reads
 * the request and it simply restarts. The Windows / Linux build uses a
 * stand-in watchdog that closes the program.                              */
static void exit_to_loader(void)
{
    pcaudio_stop_all();
    put_row(STATUS_ROW, "BACK TO THE LOADER...", COL_TEXT);
    sleep_ms(LOADER_EXIT_MSG_MS);

    watchdog_hw->scratch[LOADER_SCRATCH_MODE] = LOADER_BOOT_MODE_SD;
    watchdog_hw->scratch[LOADER_SCRATCH_ARGUMENT] = 0;
    watchdog_hw->scratch[LOADER_SCRATCH_MAGIC] = LOADER_COMMAND_MAGIC;
    watchdog_reboot(0, 0, LOADER_REBOOT_DELAY_MS);

    for (;;)
        tight_loop_contents();      /* the reboot comes in a few milliseconds */
}

int main(void)
{
    stdio_init_all();

    sb_init();                /* keyboard / south-bridge I2C (no bg poll) */
    lcd_init();                /* ST7365P LCD                              */
    lcd_enable_cursor(false);  /* no text cursor on this screen            */

    /* true = service the audio interrupt on core 1.  lcd_blit() masks
     * interrupts on its own core for the whole of every transfer, so a
     * core-0 audio interrupt would click and stutter every time the
     * screen redrew; core 1 is out of its reach.  See picocalc_audio.h. */
    pcaudio_init(true);

    bool wind_on = false;      /* W toggles the sustained wind             */
    bool surf_on = false;      /* Z toggles the sustained surf             */

    draw_help_screen();

    for (;;) {
        uint16_t e = key_event();
        if (!e) {
            sleep_ms(2);
            continue;
        }

        uint8_t code = ev_code(e);
        uint8_t st   = ev_state(e);

        if (st != KEY_STATE_PRESSED)
            continue;

        if (code == '~') {                      /* BOOTSEL reboot */
            reset_usb_boot(0, 0);
            while (1)
                tight_loop_contents();
        }

        if (code == 'q' || code == 'Q')         /* back to the UF2 Loader */
            exit_to_loader();

        if (code == KEY_ESC) {
            /* Redrawing the help screen is what puts the status line
             * back to "NOTHING YET", i.e. what "back to the key entry
             * part" means here - this program has no other screen/state
             * to return to.                                             */
            stop_all_sounds(&wind_on, &surf_on);
            draw_help_screen();
            continue;
        }

        /* ---- picocalc_sound_effects / picocalc_speech demo keys ------
         * Letters rather than F6..F9: keyboard.h defines codes up to
         * KEY_F10, but the PicoCalc's physical top row stops at F5, so
         * those codes never arrive.  Every branch stops whatever sound
         * was already playing before starting its own (see
         * stop_all_sounds() above) - the one exception is pressing W or
         * Z while that same toggle is already the thing playing, which
         * turns it back off instead of restarting it.                  */
        if (code == 'e' || code == 'E') {
            stop_all_sounds(&wind_on, &surf_on);
            pcaudio_explosion();
            show_status("EXPLOSION");
        } else if (code == 'g' || code == 'G') {
            stop_all_sounds(&wind_on, &surf_on);
            pcaudio_gunshot();
            show_status("GUNSHOT");
        } else if (code == 'w' || code == 'W') {
            if (wind_on) {
                pcaudio_wind_stop();
                wind_on = false;
                show_status("WIND OFF");
            } else {
                stop_all_sounds(&wind_on, &surf_on);
                pcaudio_wind_start();
                wind_on = true;
                show_status("WIND ON");
            }
        } else if (code == 'v' || code == 'V') {
            stop_all_sounds(&wind_on, &surf_on);
            pcaudio_vowel(PCAUDIO_VOWEL_AA, 110, 600);
            show_status("SUNG VOWEL");
        } else if (code == 'h' || code == 'H') {
            stop_all_sounds(&wind_on, &surf_on);
            pcaudio_say_hello();
            show_status("SAYS \"HELLO\"");
        } else if (code == 'j' || code == 'J') {
            stop_all_sounds(&wind_on, &surf_on);
            pcaudio_say_goodbye();
            show_status("SAYS \"GOODBYE\"");
        } else if (code == 'k' || code == 'K') {
            stop_all_sounds(&wind_on, &surf_on);
            pcaudio_say_picocalc();
            show_status("SAYS \"PICOCALC\"");
        } else if (code == 's' || code == 'S') {
            /* same word, sung an octave above its speaking pitch - the
             * formants stay put, only f0 moves (see pcaudio_sing)      */
            stop_all_sounds(&wind_on, &surf_on);
            pcaudio_sing(&pcaudio_word_hello, 220);
            show_status("SINGS \"HELLO\"");
        } else if (code == 'z' || code == 'Z') {
            if (surf_on) {
                pcaudio_surf_stop();
                surf_on = false;
                show_status("SURF OFF");
            } else {
                stop_all_sounds(&wind_on, &surf_on);
                pcaudio_surf_start();
                surf_on = true;
                show_status("SURF ON");
            }
        } else if (code == 'x' || code == 'X') {
            stop_all_sounds(&wind_on, &surf_on);
            pcaudio_raindrops();
            show_status("RAINDROPS");
        } else if (code == '1') {
            stop_all_sounds(&wind_on, &surf_on);
            pcaudio_say_greeting();
            show_status("GREETING SENTENCE");
        } else if (code == '4') {
            stop_all_sounds(&wind_on, &surf_on);
            pcaudio_waveform(PCAUDIO_SRC_SINE, TONE_HZ, TONE_MS);
            show_status("SINE WAVE");
        } else if (code == '5') {
            stop_all_sounds(&wind_on, &surf_on);
            pcaudio_waveform(PCAUDIO_SRC_TRIANGLE, TONE_HZ, TONE_MS);
            show_status("TRIANGLE WAVE");
        } else if (code == '6') {
            stop_all_sounds(&wind_on, &surf_on);
            pcaudio_waveform(PCAUDIO_SRC_SQUARE, TONE_HZ, TONE_MS);
            show_status("SQUARE WAVE");
        } else if (code == '7') {
            stop_all_sounds(&wind_on, &surf_on);
            pcaudio_waveform(PCAUDIO_SRC_SAW, TONE_HZ, TONE_MS);
            show_status("SAWTOOTH WAVE");
        } else if (code == '8') {
            stop_all_sounds(&wind_on, &surf_on);
            pcaudio_waveform(PCAUDIO_SRC_PULSE, TONE_HZ, TONE_MS);
            show_status("PULSE WAVE");
        }
    }
}
