/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 M-Vave-OP1 contributors
 * Copyright (C) 2026 Vlad Kuznetsov */
/* MASSA: a latching drone synthesizer (docs/SPEC.md). This file holds the five hooks of the contract (app.h) and
 * includes the parts in dependency order:
 *   data.c   the values of a sound and of the settings, their text, the two records in flash
 *   fx.c     the chain after the voices (audio ISR): the fuzz, the cabinet, DARK, MASSA's drive
 *   breath.c BREATH: three slow LFOs on the pitch, the cutoff and the level (audio ISR, per block)
 *   tape.c   the cassette: its audio and transport (audio ISR), the buttons and the screen's values (main loop)
 *   delay.c  the tape echo over the master: heads on a loop of tape, TIME as a motor, wobble, saturation (audio ISR)
 *   sound.c  the audio ISR: the latch, the voices, MIDI in
 *   scenes.c the pictures: the tape echo's tape and heads, the combo amp, the dials (main loop)
 *   ui.c     the pages, the SAVE dialog, the controls, the LEDs */
#ifndef APP_NAME
#define APP_NAME "MASSA"
#endif
#ifndef APP_VERSION
#define APP_VERSION "v0.1"       /* build.py --release X.Y */
#endif
#define APP_ID 0x56D5141Fu       /* this app's records in the store (store.c): every app its own (setup/new_project.py
                                  * drew one), so another firmware's data in the same flash reads as empty */

#include "data.c"
#include "fx.c"
#include "breath.c"
#include "shoe.c"
#include "tape.c"
#include "delay.c"
#include "sound.c"
#include "scenes.c"
#include "scenes2.c"
#include "scenes3.c"
#include "ui.c"

static void app_palette(void)                    /* the mode's palette (tools/gen_ui_palettes.py), by name */
{
    uint32_t i;
    const char *name = MODE_PAL[set.g[G_MODE]];
    for (i = 0; i < NPALETTES; i++)
        if (str_eq(UI_PALETTES[i].name, name)) {
            palette_set(i);
            return;
        }
}

static void app_init(void)
{
    app_palette();
    data_load();
    ui.full = 1;
    lcd_fill(0, 0, 240, 240, T_BG);
    draw_text_line(0, 90, 240, &AF_L, APP_NAME, T_ACCENT, T_BG, 1);   /* the boot screen, until the first frame */
    draw_text_line(0, 126, 240, &AF_S, APP_VERSION, T_MID, T_BG, 1);
}

static void app_frame(void) { ui_frame(); }

static void app_audio(int32_t *out, uint32_t n) { snd_render(out, n); }

static void app_audio_stop(void) { snd.stop_req = 1; }

static const char *app_fault_name(void) { return APP_NAME " CRASH"; }
