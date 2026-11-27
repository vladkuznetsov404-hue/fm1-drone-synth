/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 M-Vave-OP1 contributors
 * Copyright (C) 2026 Vlad Kuznetsov */
/* Host test of MASSA (firmware/app/, docs/SPEC.md) on the host platform (tests/host_platform.h): every page drawn
 * (lazy, redrawn after the platform drew over it, the layout lint at every value of every row), the controls table
 * (pages, octave, latch, SCL, PRESETS, ALGORITHM, knobs, the cassette's transport, ARP, the SAVE dialog), the LEDs,
 * the two records in flash (save, power cycle, another app's, older and shorter, damaged, no flash) and the sound
 * (pitch, latch on and off, swell and release, no clicks, exact silence, stealing, MIDI channel and TRS, a lost
 * stream, app_audio_stop, volume). Screens in build/shots/. Prove each new check can fail (break the code once).
 *   cc -O1 -Ibuild/gen -Ifirmware/src -Ifirmware/app -Itests -o build/host/app_test tests/app_test.c -lm
 *   build/host/app_test */
#include "host_platform.h"
#include <math.h>

static int failed;
static void check(const char *what, int ok)
{
    printf("%-64s %s\n", what, ok ? "ok" : "FAIL");
    failed += !ok;
}

#define SECS 4u
static int32_t buf[2u * FS * SECS];
#define BLOCKS(n) ((n) / CTL * CTL)

static uint32_t ink_rows(uint32_t y0, uint32_t y1)   /* rows in y0..y1 with anything but the background */
{
    uint32_t x, y, n = 0;
    for (y = y0; y < y1; y++)
        for (x = 0; x < 240u; x++)
            if (host_fb[y * 240u + x] != T_BG) {
                n++;
                break;
            }
    return n;
}

static void press(uint32_t label)                /* a button down for a frame, then up for a frame */
{
    host_press(label, 1);
    host_frame();
    host_press(label, 0);
    host_frame();
}

static int16_t def_of(uint32_t slot, uint32_t code)   /* a slot's value on a fresh unit (INIT or its factory sound) */
{
    snd_t d;
    snd_default(&d, slot);
    return d.p[code];
}

static void run_frames(uint32_t k)               /* k main-loop frames (16 ms) with the audio running beside them */
{
    while (k--) {
        host_frame();
        host_render(buf, 704);                   /* (16 ms, whole blocks) */
    }
}

static void press_t(uint32_t label)              /* a press, then a block of audio: the cassette's ISR takes it */
{
    press(label);
    host_render(buf, 64);
    host_frame();
}

static void turn(uint32_t role, int32_t steps)
{
    host_turn(role, steps);
    host_frame();
}

static void tap_key(uint32_t k)                  /* a note key down and up, the ISR seeing both */
{
    fm1_in.notes |= 1u << k;
    host_render(buf, CTL);
    fm1_in.notes &= ~(1u << k);
    host_render(buf, CTL);
}

static uint32_t n_held(void)
{
    uint32_t i, n = 0;
    for (i = 0; i < NV; i++)
        n += snd.held[i] != 0;
    return n;
}

static int is_held(uint32_t note)
{
    uint32_t i;
    for (i = 0; i < NV; i++)
        if (snd.held[i] == note)
            return 1;
    return 0;
}

static void reset_state(void)                    /* the app's RAM as at power-on (the flash kept) */
{
    memset(&ui, 0, sizeof ui);
    memset(&snd, 0, sizeof snd);
    memset(snd.fm_last, 0xFF, sizeof snd.fm_last);
    memset(&scope, 0, sizeof scope);
    memset(&fx, 0, sizeof fx);
    memset(fx.last, 0xFF, sizeof fx.last);       /* (-1: never updated, as at power-on) */
    fx.lmod = 32768;
    memset(&br, 0, sizeof br);
    memset(br.last, 0xFF, sizeof br.last);
    memset(&tape, 0, sizeof tape);
    tape.rnd = 0x1234567u;
    memset(&dl, 0, sizeof dl);
    memset(dl_cells, 0, sizeof dl_cells);
    memset(&cur, 0, sizeof cur);
    memset(&bank, 0, sizeof bank);
    memset(&set, 0, sizeof set);
    fm1_in.notes = fm1_in.buttons = 0;
}

static void power_cycle(void)
{
    reset_state();
    host_boot();
    host_frame();
}

/* ---------------------------------------------------------------- screen --- */
static void screen(void)
{
    static const struct { uint32_t btn, page; const char *shot; } PAGES[] = {
        {B_EDIT, PG_EDIT, "build/shots/edit_saw.png"}, {B_FX, PG_FX, "build/shots/fx.png"},
        {B_ENV, PG_ENV, "build/shots/env.png"},        {B_LFO, PG_LFO, "build/shots/lfo.png"},
        {B_ARP, PG_DELAY, "build/shots/delay.png"},    {B_ARP, PG_DELAY2, "build/shots/delay2.png"},
        {B_SEQ, PG_TAPE, "build/shots/tape.png"},      {B_SEQ, PG_TAPE2, "build/shots/tape2.png"},
        {B_GLO, PG_GLO, "build/shots/glo.png"},        {B_HOME, PG_HOME, "build/shots/home_again.png"},
    };
    uint32_t i, p, r, overlap = 0;
    host_nor_erase();
    reset_state();
    host_boot();
    check("boot: the name drawn large before the first frame", ink_rows(90, 125) > 20u);
    {   /* the large face is sparse (tools/gen_aa_font.py L_CHARS): a character it lacks draws as a space */
        const char *c = APP_NAME;
        int all = 1;
        for (; *c; c++)
            all &= glyph_at(&AF_L, (uint8_t)*c) >= 0;
        check("boot: every character of APP_NAME is in the large font", all);
    }
    host_png("build/shots/boot.png");
    check("boot: the MASSA palette (black background)", T_BG == 0 && T_THEME != T_TEXT);
    host_frame();
    check("HOME: header, sound line, notes, tape and four macro rows drawn",
          ink_rows(0, 24) > 8u && ink_rows(24, 48) > 8u && ink_rows(50, 100) > 2u && ink_rows(102, 126) > 8u &&
          ink_rows(128, 240) > 60u && ui.page == PG_HOME);
    host_png("build/shots/home.png");
    host_frame();
    check("lazy: nothing changed, nothing sent to the panel", host_blits == 0u);
    turn(EN_K1, 3);
    check("KNOB 1 on HOME: MASSA +3, only its areas sent", cur.p[P_MASSA] == SP[P_MASSA].def + 3 && host_blits > 0u &&
          host_blits <= 3u);

    for (i = 0; i < sizeof PAGES / sizeof PAGES[0]; i++) {
        press(PAGES[i].btn);
        host_png(PAGES[i].shot);
        check(PAGES[i].shot + 12, ui.page == PAGES[i].page && ink_rows(0, 24) > 8u && ink_rows(52, 212) > 40u);
    }
    turn(EN_SELECT, -1);
    check("SELECT left from HOME: wraps to GLO", ui.page == PG_GLO);
    turn(EN_SELECT, 2);
    check("SELECT right twice: EDIT", ui.page == PG_EDIT);
    turn(EN_ALGO, 1);
    host_png("build/shots/edit_fm.png");
    check("ALGORITHM right: FM, EDIT shows the FM values", cur.engine == ENG_FM && cell(PG_EDIT, 0) == P_RATIO);
    turn(EN_ALGO, -1);
    check("ALGORITHM left: SAW", cur.engine == ENG_SAW && cell(PG_EDIT, 0) == P_LAYERS);

    memset(host_fb, 0, sizeof host_fb);          /* the platform drew over the screen (UBOOT countdown) */
    app_redraw = 1;
    host_frame();
    check("app_redraw: the whole page drawn again", ink_rows(0, 24) > 8u && ink_rows(52, 212) > 40u && !app_redraw);

    /* the layout lint: every value of every row of every page (both engines), the widest notes and messages */
    host_clipped = 0;
    snd.held[0] = 1; snd.held[1] = 3; snd.held[2] = 6; snd.held[3] = 8;   /* C#-1 D#-1 F#-1 G#-1 */
    snd.last_midi = 1;
    for (r = 0; r < 2u; r++) {
        cur.engine = (uint8_t)r;
        for (p = 0; p < NPG; p++) {
            ui_page(p);
            host_frame();
            for (i = 0; i < 4u; i++) {
                int32_t c = cell(p, i), v;
                const par_t *q = cell_par(c);
                if (!q)
                    continue;
                for (v = q->lo; v <= q->hi; v++) {
                    char t[24];
                    if (c >= C_G) set.g[c - C_G] = (int16_t)v; else cur.p[c] = (int16_t)v;
                    host_frame();
                    par_text(t, sizeof t, v, q->fmt);    /* label, SOON and value side by side, 8 px apart */
                    if (page_scene(p)) {                 /* a dial's cell: label and value each inside 58 px */
                        if (text_w(&AF_S, dial_label(c)) > 58 || text_w(&AF_S, t) > 58) {
                            overlap++;
                            printf("  (dial too wide: %s %s)\n", dial_label(c), t);
                        }
                    } else if (22 + text_w(&AF_S, q->label) + 6 + text_w(&AF_S, "SOON") + 8 + text_w(&AF_M, t) > 232) {
                        overlap++;
                        printf("  (overlap: %s %s)\n", q->label, t);
                    }
                }
            }
        }
    }
    memcpy(cur.name, "WWWWWWWWWWWW", NAME_LEN);
    set.g[G_SLOT] = 15;
    set.g[G_OCT] = -3;
    tape.st = TP_DUB;
    tape.len = 1000;
    ui_page(PG_HOME);
    host_frame();
    host_png("build/shots/widest_home.png");
    ui_page(PG_FX);
    host_frame();
    host_png("build/shots/widest_fx.png");
    toast("SAVE FAILED");
    host_frame();
    toast("TAPE ERASED");
    host_frame();
    ui.dlg = 1;
    ui.dlg_slot = 15;
    memcpy(bank.s[15].name, "WWWWWWWWWWWW", NAME_LEN);
    ui.full = 1;
    host_frame();
    host_png("build/shots/widest_save.png");
    check("layout: nothing cut or outside its canvas, at every value", host_clipped == 0u);
    check("layout: no row's label runs into its value, at every value", overlap == 0u);
    check("layout: HOME's tape line: state, bar and SOON 8 px apart",
          40 + text_w(&AF_M, "EMPTY") + 8 <= TB_X && TB_X + TB_W + 8 <= 232 - text_w(&AF_S, "SOON"));
    if (host_clipped)
        printf("  (%u, the last: %s)\n", host_clipped, host_clip_what);
    power_cycle();
}

/* -------------------------------------------------------------- controls --- */
static void controls(void)
{
    uint32_t erases;
    host_nor_erase();
    power_cycle();
    check("defaults: octave -2, sound 1, HOME", set.g[G_OCT] == -2 && set.g[G_SLOT] == 0 && ui.page == PG_HOME);

    press(B_OCTDN);
    check("OCT-: octave -3", set.g[G_OCT] == -3);
    press(B_OCTDN);
    check("OCT- at -3: stays", set.g[G_OCT] == -3);
    press(B_OCTUP); press(B_OCTUP); press(B_OCTUP); press(B_OCTUP); press(B_OCTUP);
    check("OCT+ to the top: +1, its LED lit, OCT- dark", set.g[G_OCT] == 1 && host_led[panel.btn[B_OCTUP]] &&
          !host_led[panel.btn[B_OCTDN]]);
    press(B_OCTDN); press(B_OCTDN); press(B_OCTDN);
    check("back to -2: OCT- lit", set.g[G_OCT] == -2 && host_led[panel.btn[B_OCTDN]]);
    check("HOME: its button lit", host_led[panel.btn[B_HOME]] && !host_led[panel.btn[B_FX]]);

    tap_key(0);                                  /* F1 at octave -2 */
    host_frame();
    check("key 0 tapped: F1 (29) latched after the key is up", is_held(29) && n_held() == 1u);
    check("latched: the key's LED lit", host_led[14] && !host_led[15]);
    host_png("build/shots/latched.png");
    tap_key(0);
    host_frame();
    check("key 0 again: released", !is_held(29) && !host_led[14]);
    tap_key(0); tap_key(4); tap_key(7); tap_key(12);
    check("four keys: four notes", n_held() == 4u);
    tap_key(16);
    check("a fifth: the oldest (F1) stolen", n_held() == 4u && !is_held(29) && is_held(29 + 16));
    host_frame();
    host_png("build/shots/four.png");
    press(B_SCL);
    host_render(buf, CTL);
    check("SCL: every note released, SILENCE shown", n_held() == 0u && toast_on() && str_eq(ui.toast, "SILENCE"));

    turn(EN_PRESET, 2);
    check("PRESETS +2: sound 3 loaded", set.g[G_SLOT] == 2 && !memcmp(&cur, &bank.s[2], sizeof cur));
    turn(EN_PRESET, -10);
    check("PRESETS down past 1: stays at 1", set.g[G_SLOT] == 0);
    tap_key(0);
    turn(EN_PRESET, 1);
    host_render(buf, CTL);
    check("PRESETS with a note held: the note goes on", is_held(29));
    press(B_SCL);

    press(B_FX);
    turn(EN_K1, 5);
    check("FX, KNOB 1: fuzz drive +5, the sound marked edited",
          cur.p[P_DRIVE] == bank.s[set.g[G_SLOT]].p[P_DRIVE] + 5 && snd_dirty());
    turn(EN_K3, 9);
    check("FX, KNOB 3: cabinet clamped at its last value", cur.p[P_CAB] == 2);
    press(B_GLO);
    turn(EN_K1, 3);
    check("GLO, KNOB 1: MIDI channel 3", set.g[G_CH] == 3);
    turn(EN_K1, -3);

    press(B_ARP);
    check("ARP: the DELAY page, ARP lit", ui.page == PG_DELAY && host_led[panel.btn[B_ARP]]);
    press(B_ARP);
    check("ARP again: DELAY 2 (WOBBLE, DULL)", ui.page == PG_DELAY2 && cell(PG_DELAY2, 0) == C_G + G_DWOB);
    press(B_ARP);
    turn(EN_K1, 5);
    turn(EN_K2, -5);
    turn(EN_K3, 7);
    check("DELAY, KNOBS 1 2 3: TIME, REPEATS, ECHO move", set.g[G_DT] == GP[G_DT].def + 5 &&
          set.g[G_DFB] == GP[G_DFB].def - 5 && set.g[G_DMIX] == GP[G_DMIX].def + 7);
    {
        int32_t h0 = set.g[G_DHEADS];
        turn(EN_K4, 1);
        check("DELAY, KNOB 4: HEADS steps 1, 2, 3, 12, 13, 23, 123 (from 12: 13)", h0 == 3 && set.g[G_DHEADS] == 5);
        turn(EN_K4, 9);
        check("HEADS: clamped at all three, and back to one", set.g[G_DHEADS] == 7);
        turn(EN_K4, -9);
        check("HEADS: one head at the low end", set.g[G_DHEADS] == 1);
        set.g[G_DHEADS] = (int16_t)h0;
    }
    press(B_HOME);

    /* the cassette's transport (its clock is the audio: frames and audio run together here) */
    press_t(B_PLAY);
    check("PLAY on an empty tape: nothing", tape.st == TP_EMPTY);
    press_t(B_REC);
    check("REC: recording, its LED lit", tape.st == TP_REC && host_led[panel.btn[B_REC]]);
    run_frames(125);                             /* 2 s */
    press_t(B_REC);
    check("REC again: the loop closed (~2 s) and playing", tape.st == TP_PLAY && tape.len > 1900u && tape.len < 2200u);
    press_t(B_REC);
    check("REC while playing: overdub, REC and PLAY lit", tape.st == TP_DUB && host_led[panel.btn[B_REC]] &&
          host_led[panel.btn[B_PLAY]]);
    host_png("build/shots/dub.png");
    press_t(B_REC);
    check("REC again: overdub off", tape.st == TP_PLAY);
    press_t(B_PLAY);
    check("PLAY: stopped (braking), the position kept", tape.st == TP_STOP && tape.pos < tape.len);
    press_t(B_PLAY);
    check("PLAY again: playing", tape.st == TP_PLAY);
    host_press(B_REC, 1);
    run_frames(70);                              /* 1.1 s */
    host_press(B_REC, 0);
    host_frame();
    host_render(buf, 64);
    host_frame();
    check("REC held 1 s: the tape erased", tape.st == TP_EMPTY && tape.len == 0u && str_eq(ui.toast, "TAPE ERASED"));
    press_t(B_REC);
    run_frames(700);                             /* 11 s */
    check("a first pass to the end: closes itself at 10 s", tape.st == TP_PLAY && tape.len == TAPE_MAX_MS);
    press(B_SEQ);
    check("SEQ: the TAPE page, SEQ lit", ui.page == PG_TAPE && host_led[panel.btn[B_SEQ]]);
    press(B_SEQ);
    check("SEQ on TAPE: TAPE 2 (BRAKE)", ui.page == PG_TAPE2 && cell(PG_TAPE2, 0) == C_G + G_BRAKE);

    /* the SAVE dialog */
    erases = host_erases;
    press(B_SAVE);
    host_png("build/shots/save.png");
    check("SAVE: the dialog, SAVE lit", ui.dlg && host_led[panel.btn[B_SAVE]]);
    press(B_OCTDN);
    check("OCT- in the dialog: cancelled, nothing written, octave kept", !ui.dlg && host_erases == erases &&
          set.g[G_OCT] == -2);
    press(B_SAVE);
    turn(EN_PRESET, 3);
    check("PRESETS in the dialog: picks slot 5 (2 + 3), nothing loaded", ui.dlg && ui.dlg_slot == 4 && set.g[G_SLOT] == 1);
    turn(EN_K1, 4);
    check("knobs in the dialog: ignored", set.g[G_BRAKE] == GP[G_BRAKE].def);
    press(B_SAVE);
    check("SAVE again: saved into slot 5, SAVED shown", !ui.dlg && ui.saved == 1 && host_erases > erases &&
          set.g[G_SLOT] == 4 && !memcmp(&bank.s[4], &cur, sizeof cur) && !snd_dirty());
    press(B_SAVE);
    press(B_OCTUP);
    check("OCT+ in the dialog: saves too", ui.saved == 1 && !ui.dlg);
}

/* ---------------------------------------------------------------- flash --- */
static void persistence(void)
{
    host_nor_erase();
    power_cycle();
    check("fresh unit: defaults (INIT 01, octave -2, volume 80)", !memcmp(cur.name, "INIT 01     ", NAME_LEN) &&
          set.g[G_OCT] == -2 && set.g[G_VOL] == 80 && cur.p[P_DRIVE] == SP[P_DRIVE].def);
    press(B_FX);
    turn(EN_K1, 7);
    press(B_OCTUP);
    power_cycle();
    check("not saved: power-on back at the defaults", cur.p[P_DRIVE] == SP[P_DRIVE].def && set.g[G_OCT] == -2);
    press(B_FX);
    turn(EN_K1, 7);
    press(B_OCTUP);
    turn(EN_PRESET, 4);                          /* (loads sound 5, DARK SUB: its own drive) */
    turn(EN_K1, 7);
    press(B_SAVE);
    press(B_SAVE);
    power_cycle();
    check("power-on: sound 5 with its drive, octave -1", set.g[G_SLOT] == 4 && cur.p[P_DRIVE] == def_of(4, P_DRIVE) + 7 &&
          set.g[G_OCT] == -1 && bank.s[0].p[P_DRIVE] == SP[P_DRIVE].def);
    {   /* the unit ran another firmware since: its records in the same slots read as empty */
        static uint8_t other[1600];
        memset(other, 0x33, sizeof other);
        host_nor_erase();
        store_save(SLOT_SET, 0x12345678u, other, 68);
        store_save(SLOT_BANK, 0x12345678u, other, sizeof other);
        power_cycle();
        check("another app's records: ignored (defaults)", set.g[G_OCT] == -2 && cur.p[P_DRIVE] == SP[P_DRIVE].def &&
              !memcmp(cur.name, "INIT 01     ", NAME_LEN));
    }
    {   /* ours, damaged: out of range values, a bad name, a bad engine; the rest kept */
        bank.s[0].p[P_DRIVE] = 999;
        bank.s[0].p[P_TONE] = 77;
        bank.s[0].p[P_LAYERS] = 0;
        bank.s[0].engine = 9;
        bank.s[0].name[3] = 1;
        bank.s[0].np = 200;
        set.g[G_OCT] = 7;
        set.g[G_VOL] = 33;
        set.g[G_CH] = -1;
        store_save(SLOT_BANK, APP_ID, &bank, sizeof bank);
        store_save(SLOT_SET, APP_ID, &set, sizeof set);
        power_cycle();
        check("damaged values: each defaulted, the valid ones kept",
              cur.p[P_DRIVE] == SP[P_DRIVE].def && cur.p[P_TONE] == 77 && cur.p[P_LAYERS] == SP[P_LAYERS].def &&
              cur.engine == ENG_SAW && !memcmp(cur.name, "INIT 01     ", NAME_LEN) && cur.np == NP &&
              set.g[G_OCT] == -2 && set.g[G_VOL] == 33 && set.g[G_CH] == 0);
    }
    {   /* an older, shorter record: fewer values, fewer sounds; what it lacks at the defaults */
        bank.s[1].p[P_TONE] = 11;
        bank.s[1].p[P_RELEASE] = 150;
        bank.s[1].np = P_RELEASE;                /* this firmware had the values before RELEASE only */
        set.ng = G_OCT;                          /* settings: CH, TRS, VOL only */
        set.g[G_VOL] = 44;
        set.g[G_OCT] = 1;
        store_save(SLOT_BANK, APP_ID, &bank, 2u * sizeof(snd_t));
        store_save(SLOT_SET, APP_ID, &set, 4u + 2u * G_OCT);
        power_cycle();
        check("older record: its values read, the newer ones at defaults",
              bank.s[1].p[P_TONE] == 11 && bank.s[1].p[P_RELEASE] == def_of(1, P_RELEASE) && set.g[G_VOL] == 44 &&
              set.g[G_OCT] == -2 && !memcmp(bank.s[2].name, "SUNN WALL   ", NAME_LEN));
    }
    flash_ok = 0;
    press(B_SAVE);
    press(B_SAVE);
    host_png("build/shots/save_failed.png");
    check("no flash: SAVE fails visibly", ui.saved == -1 && toast_on() && str_eq(ui.toast, "SAVE FAILED"));
    flash_ok = 1;
    host_nor_erase();
    power_cycle();
}

/* ---------------------------------------------------------------- sound --- */
static double pitch(const int32_t *b, uint32_t frames)   /* rising zero crossings per second, left channel */
{
    uint32_t i, n = 0, first = 0, last = 0;
    for (i = 1; i < frames; i++)
        if (b[2u * (i - 1u)] < 0 && b[2u * i] >= 0) {
            if (!n)
                first = i;
            last = i;
            n++;
        }
    return n > 1u ? (double)(n - 1u) * FS / (double)(last - first) : 0.0;
}

static int32_t peak(const int32_t *b, uint32_t frames)
{
    int32_t m = 0;
    uint32_t i;
    for (i = 0; i < 2u * frames; i++)
        m = abs(b[i]) > m ? abs(b[i]) : m;
    return m;
}

static double level_at(const int32_t *b, uint32_t frames, double hz)   /* Goertzel: the amplitude at hz, left */
{
    double w = 2.0 * M_PI * hz / FS, c = 2.0 * cos(w), s1 = 0, s2 = 0, s0;
    uint32_t i;
    for (i = 0; i < frames; i++) {
        s0 = b[2u * i] + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return 2.0 * sqrt(s1 * s1 + s2 * s2 - c * s1 * s2) / frames;
}

static double band_above(const int32_t *b, uint32_t frames, double f0, double f1, double step)   /* peak level */
{
    double m = 0, f;
    for (f = f0; f < f1; f += step) {
        double l = level_at(b, frames, f);
        m = l > m ? l : m;
    }
    return m;
}

static int same_lr(const int32_t *b, uint32_t frames)
{
    uint32_t i;
    for (i = 0; i < frames; i++)
        if (b[2u * i] != b[2u * i + 1u])
            return 0;
    return 1;
}

static void sound(void)
{
    const uint32_t S = BLOCKS(FS);
    int32_t p1, p2;
    host_nor_erase();
    power_cycle();
    host_render(buf, 1024);
    check("silence with nothing latched", peak(buf, 1024) == 0);
    fx.bypass = 1;                               /* the voices alone: the chain has its own checks (fx_chain) */
    cur.p[P_BREATH] = 0;                         /* still, no LFOs: their own checks (breath) */
    set.g[G_OCT] = 0;                            /* key 0 = F3, 174.61 Hz */
    cur.p[P_LAYERS] = 1;                         /* the latch, ramps and MIDI checks on one plain saw, no sub */
    cur.p[P_SUB] = 0;

    cur.p[P_ATTACK] = 0;                         /* the shortest swell: ~6 ms, still no click */
    fm1_in.notes = 1u;
    host_render(buf, S);
    check("key F3: 174.6 Hz (+-0.5)", fabs(pitch(buf, S) - 174.61) < 0.5);
    check("key F3: the start ramps (the first 32 frames below 1/8 of the peak)", peak(buf, 32) < peak(buf, S) / 8);
    check("key F3: peak within the voice's level, mono", peak(buf, S) <= VAMP && peak(buf, S) > VAMP * 3 / 4 &&
          same_lr(buf, S));
    fm1_in.notes = 0;
    host_render(buf, S);
    check("key up: the note drones on (latch)", peak(buf + 2u * (S - 1024u), 1024) > VAMP / 2);
    cur.p[P_RELEASE] = 0;
    tap_key(0);
    host_render(buf, 1024);
    check("key F3 again: silent within ~6 ms, exactly", peak(buf + 2u * 512u, 512) == 0 && n_held() == 0u);

    cur.p[P_ATTACK] = 20;                        /* a 2 s swell */
    tap_key(0);
    host_render(buf, S);
    p1 = peak(buf, 4096);
    p2 = peak(buf + 2u * (S - 4096u), 4096);
    check("swell 2.0 s: rising over the first second", p1 < VAMP / 8 && p2 > VAMP / 3 && p2 < VAMP * 3 / 4);
    cur.p[P_RELEASE] = 40;                       /* a 4 s release */
    tap_key(0);
    host_render(buf, S);
    check("release 4.0 s: still sounding after 1 s, falling", peak(buf + 2u * (S - 1024u), 1024) > 300 &&
          peak(buf + 2u * (S - 1024u), 1024) < p2);
    host_render(buf, S); host_render(buf, S); host_render(buf, S);
    check("release 4.0 s: exact silence after it", peak(buf + 2u * (S - 1024u), 1024) == 0 && snd.v[0].state == V_OFF);

    cur.p[P_ATTACK] = 0;
    tap_key(0); tap_key(3); tap_key(7); tap_key(10);
    host_render(buf, S);
    check("four notes: summed, never past full scale", peak(buf, S) > VAMP && peak(buf, S) <= 32767);
    tap_key(12);
    host_render(buf, 64);
    check("a fifth note steals, still four", n_held() == 4u && is_held(65));
    check("the steal keeps the stolen voice's level (no restart from silence)", peak(buf, 64) > VAMP);
    cur.p[P_RELEASE] = 0;
    snd.silence_req = 1;
    host_render(buf, 2048);
    check("SCL: all released (exact silence after the 6 ms release)", peak(buf + 2u * 1024u, 1024) == 0);

    host_midi(0x90, 69, 100);                    /* A4 over USB-MIDI */
    host_render(buf, S);
    check("MIDI A4: 440 Hz (+-1), shown as the last note", fabs(pitch(buf, S) - 440.0) < 1.0 && snd.last_midi == 69u);
    host_midi(0x80, 69, 0);
    host_midi(0x90, 69, 0);                      /* (velocity 0 = an off) */
    host_render(buf, 2048);
    check("note-off and velocity 0: nothing (latch)", peak(buf + 2u * 1024u, 1024) > VAMP / 2);
    host_midi(0x90, 69, 90);
    host_render(buf, 2048);
    check("the same note-on again: released", peak(buf + 2u * 1024u, 1024) == 0);
    set.g[G_CH] = 2;
    host_midi(0x90, 60, 100);
    host_render(buf, 2048);
    check("channel 2 set: a channel 1 note ignored", peak(buf, 2048) == 0);
    host_midi(0x91, 60, 100);
    host_render(buf, 2048);
    check("channel 2 set: a channel 2 note plays", peak(buf + 2u * 1024u, 1024) > VAMP / 2);
    host_midi(0x91, 60, 100);
    set.g[G_CH] = 0;
    set.g[G_TRS] = 0;
    midi_enqueue(0x09u | 0x90u << 8 | 62u << 16 | 100u << 24, 2u);
    host_render(buf, 2048);
    check("TRS MIDI IN off: a TRS note ignored", n_held() == 0u);
    set.g[G_TRS] = 1;
    midi_enqueue(0x09u | 0x90u << 8 | 62u << 16 | 100u << 24, 2u);
    host_render(buf, 2048);
    check("TRS MIDI IN on: a TRS note plays", is_held(62));
    midi_in_overflow = 1;                        /* a broken stream: events lost */
    host_render(buf, 2048);
    check("MIDI ring overflow: the drone goes on, the ring recovers", is_held(62) && !midi_in_overflow &&
          peak(buf + 2u * 1024u, 1024) > VAMP / 2);
    set.g[G_VOL] = 0;
    host_render(buf, 1024);
    check("volume 0: silent", peak(buf, 1024) == 0);
    set.g[G_VOL] = 80;
    tap_key(0);
    host_midi(0x90, 64, 100);
    app_audio_stop();
    host_render(buf, 1024);
    check("app_audio_stop: silent at once, every note forgotten", peak(buf, 1024) == 0 && n_held() == 0u);
}

/* The SAW drone engine (milestone 2): the layers' detune from BEATS and SPREAD CURVE, the sub and its octave, the
 * polyBLEP, the levels. Measured on one F3 (174.61 Hz) over a second (1 Hz resolution, Goertzel). */
static void engine(void)
{
    const uint32_t S = BLOCKS(FS);
    const double F = 174.614, UP40 = F * pow(2.0, 40.0 / 1200.0), DN40 = F / pow(2.0, 40.0 / 1200.0);
    double c, u, d, a1, a2;
    int32_t pk;
    host_nor_erase();
    power_cycle();
    fx.bypass = 1;
    cur.p[P_BREATH] = 0;
    set.g[G_OCT] = 0;
    cur.p[P_ATTACK] = 0;
    cur.p[P_SUB] = 0;
    cur.p[P_LAYERS] = 3;
    cur.p[P_BEATS] = 100;
    cur.p[P_CURVE] = 0;
    tap_key(0);
    host_render(buf, S);                         /* settle */
    host_render(buf, S);
    c = level_at(buf, S, F); u = level_at(buf, S, UP40); d = level_at(buf, S, DN40);
    check("3 layers, BEATS 100: the note and +-40 cents, about equal", c > 1500 && u > c * 0.7 && d > c * 0.7 &&
          u < c * 1.4 && d < c * 1.4);
    cur.p[P_LAYERS] = 2;
    host_render(buf, S); host_render(buf, S);
    c = level_at(buf, S, F);
    u = level_at(buf, S, F * pow(2.0, 20.0 / 1200.0));
    d = level_at(buf, S, F / pow(2.0, 20.0 / 1200.0));
    check("2 layers, BEATS 100: +-20 cents, nothing at the note itself", u > 1500 && d > 1500 && c < u / 4);
    cur.p[P_LAYERS] = 3;
    cur.p[P_BEATS] = 0;
    host_render(buf, S); host_render(buf, S);
    c = level_at(buf, S, F); u = level_at(buf, S, UP40);
    check("BEATS 0: the layers in unison (all at the note, none at +40 c)", c > 1500 && u < c / 10);
    cur.p[P_BEATS] = 50;
    cur.p[P_CURVE] = 0;                          /* even steps: half the knob, half the spread (20 c) */
    host_render(buf, S); host_render(buf, S);
    a1 = level_at(buf, S, F * pow(2.0, 20.0 / 1200.0));
    cur.p[P_CURVE] = 100;                        /* cubic: half the knob, an eighth of the spread (5 c) */
    host_render(buf, S); host_render(buf, S);
    a2 = level_at(buf, S, F * pow(2.0, 20.0 / 1200.0));
    check("SPREAD CURVE 0 -> 100 at BEATS 50: the spread falls from 20 c to 5 c", a1 > 1500 && a2 < a1 / 3);

    cur.p[P_BEATS] = 0;
    cur.p[P_LAYERS] = 1;
    cur.p[P_SUB] = 0;
    cur.p[P_MASSA] = 0;                          /* (MASSA lifts the sub: checked in fx_chain) */
    host_render(buf, S); host_render(buf, S);
    a1 = level_at(buf, S, F / 2.0);
    cur.p[P_SUB] = 100;
    cur.p[P_SUBOCT] = 1;
    host_render(buf, S); host_render(buf, S);
    a2 = level_at(buf, S, F / 2.0);
    check("SUB LEVEL 0 -> 100, one octave: a strong sine an octave down", a1 < 50 && a2 > SUB_AMP * 0.8 &&
          a2 < SUB_AMP * 1.15 && level_at(buf, S, F / 4.0) < 50);
    check("the sub is a sine (its 3rd harmonic small)", level_at(buf, S, 3.0 * F / 2.0) < a2 / 10);
    cur.p[P_SUBOCT] = 2;
    host_render(buf, S); host_render(buf, S);
    check("SUB OCTAVE 2: two octaves down", level_at(buf, S, F / 4.0) > SUB_AMP * 0.8 && level_at(buf, S, F / 2.0) < 50);
    cur.p[P_SUB] = 0;

    /* polyBLEP: a high note's aliases. C8 (4186 Hz, over MIDI): its 6th harmonic (25 116 Hz) folds to 18 984 Hz; a
     * naive saw has it at 1/6 of the fundamental's level, the BLEP at ~1/20 (a 2-sample BLEP is weakest this high;
     * at drone pitches it does far better). The check sits between the two: a naive saw fails it. */
    tap_key(0);
    host_render(buf, S); host_render(buf, S); host_render(buf, S);
    host_midi(0x90, 108, 100);
    host_render(buf, S); host_render(buf, S);
    a1 = level_at(buf, S, 4186.01);
    a2 = level_at(buf, S, 44100.0 - 6.0 * 4186.01);
    check("polyBLEP: C8's folded 6th harmonic well below a naive saw's 1/6 (< 1/12)", a1 > 3000 && a2 < a1 / 12);
    host_midi(0x90, 108, 100);
    host_render(buf, S); host_render(buf, S);

    cur.p[P_LAYERS] = 3;                         /* the loudest sound: 3 layers at full spread and the sub */
    cur.p[P_BEATS] = 100;
    cur.p[P_SUB] = 100;
    cur.p[P_SUBOCT] = 1;
    tap_key(0);
    host_render(buf, S); host_render(buf, S);
    pk = peak(buf, S);
    check("one note, 3 layers and the sub: louder than one saw, well below full scale", pk > VAMP && pk < 20000);
    tap_key(3); tap_key(7); tap_key(10);
    host_render(buf, S); host_render(buf, S);
    check("four such notes: summed, never past full scale", peak(buf, S) > pk && peak(buf, S) <= 32767);
    cur.p[P_BEATS] = 20;                         /* a tuning change while droning: no gap */
    host_render(buf, 64);
    check("BEATS turned while droning: the sound goes on", peak(buf, 64) > VAMP / 2);
    app_audio_stop();
    host_render(buf, 64);
}

static double rms(const int32_t *b, uint32_t frames)
{
    double a = 0;
    uint32_t i;
    for (i = 0; i < frames; i++)
        a += (double)b[2u * i] * b[2u * i];
    return sqrt(a / frames);
}

static double highs(const int32_t *b, uint32_t frames)   /* the loudest harmonic of F2 between 5 and 8 kHz */
{
    double m = 0, f;
    for (f = 87.307 * 58; f < 8000.0; f += 87.307) {
        double l = level_at(b, frames, f);
        m = l > m ? l : m;
    }
    return m;
}

/* The chain after the voices (milestone 3): the fuzz (drive, MASSA, tone), the level, the cabinet, DARK, MASSA's
 * sub, the silence after the release. On one F2 (87.3 Hz), one layer (clean harmonics to measure), a second at a
 * time. (The fuzz's asymmetry is not checked by its peaks: a driven wave is near square, and the DC block after the
 * clip makes its two sides equal again; it shows as even harmonics at moderate drive.) */
static void fx_chain(void)
{
    const uint32_t S = BLOCKS(FS);
    double r0, r1, h0, h1, h2;
    int32_t p0, p1;
    host_nor_erase();
    power_cycle();
    set.g[G_OCT] = -1;                           /* key 0 = F2 */
    cur.p[P_BREATH] = 0;
    cur.p[P_ATTACK] = 0;
    cur.p[P_RELEASE] = 0;
    cur.p[P_DRIVE] = 0;
    cur.p[P_MASSA] = 0;
    cur.p[P_DARK] = 0;
    cur.p[P_CAB] = 0;
    cur.p[P_TONE] = 100;
    cur.p[P_LAYERS] = 1;
    cur.p[P_SUB] = 0;
    tap_key(0);
    host_render(buf, S); host_render(buf, S);
    p0 = peak(buf, S);
    r0 = rms(buf, S) / p0;
    check("fuzz at 0 dB: a light saturation, below the level's full output", p0 > 3000 && p0 <= FX_OUT + 200);
    cur.p[P_DRIVE] = 100;
    cur.p[P_MASSA] = 100;
    host_render(buf, S); host_render(buf, S);
    p1 = peak(buf, S);
    r1 = rms(buf, S) / p1;
    check("DRIVE + MASSA 100: squashed (RMS / peak up by a third), the peak held by the clip", r1 > r0 * 1.33 &&
          p1 <= FX_OUT + 200);
    h0 = highs(buf, S);
    cur.p[P_TONE] = 0;
    host_render(buf, S); host_render(buf, S);
    check("FUZZ TONE 100 -> 0: the highs at least 6 dB down", highs(buf, S) < h0 / 2);
    cur.p[P_TONE] = 100;
    cur.p[P_CAB] = 1;
    host_render(buf, S); host_render(buf, S);
    h1 = highs(buf, S);
    cur.p[P_CAB] = 2;
    host_render(buf, S); host_render(buf, S);
    h2 = highs(buf, S);
    check("CABINET off -> 4x12 -> dark: the fizz above 5 kHz 6 dB down, then 10 dB more", h1 < h0 / 2 &&
          h2 < h1 / 3.16);
    cur.p[P_CAB] = 0;
    cur.p[P_DARK] = 100;
    host_render(buf, S); host_render(buf, S);
    check("DARK 0 -> 100: the highs 30 dB down, the fundamental kept", highs(buf, S) < h0 / 30 &&
          level_at(buf, S, 87.307) > 300);
    cur.p[P_DARK] = 0;
    cur.p[P_LEVEL] = 50;
    host_render(buf, S); host_render(buf, S);
    p0 = peak(buf, S);
    cur.p[P_LEVEL] = 100;
    host_render(buf, S); host_render(buf, S);
    check("LEVEL 50 -> 100: twice as loud (+-5 %)", fabs((double)peak(buf, S) / p0 - 2.0) < 0.1);
    cur.p[P_LEVEL] = 0;
    host_render(buf, S);
    check("LEVEL 0: silent", peak(buf + 2u * 1024u, S - 1024u) == 0);
    cur.p[P_LEVEL] = 80;

    fx.bypass = 1;                               /* MASSA lifts the sub (on the voices) */
    cur.p[P_SUB] = 100;
    cur.p[P_MASSA] = 0;
    host_render(buf, S); host_render(buf, S);
    h0 = level_at(buf, S, 87.307 / 2.0);
    cur.p[P_MASSA] = 100;
    host_render(buf, S); host_render(buf, S);
    check("MASSA 0 -> 100: the sub 60 % louder", fabs(level_at(buf, S, 87.307 / 2.0) / h0 - 1.6) < 0.08);
    fx.bypass = 0;

    cur.p[P_CAB] = 1;                            /* the loudest settings: the hump and a full chord */
    cur.p[P_LEVEL] = 100;
    tap_key(4); tap_key(7); tap_key(12);
    host_render(buf, S); host_render(buf, S);
    check("full chord, LEVEL 100, 4x12: below full scale (no hard clip at the end)", peak(buf, S) < 32767);
    snd.silence_req = 1;                         /* release (0 s) */
    host_render(buf, BLOCKS(FX_IDLE) + 512u);
    host_render(buf, 2048);
    check("released: exact silence once the chain went idle (no hum, no limit cycle)", peak(buf, 2048) == 0 &&
          fx.idle >= FX_IDLE);
    tap_key(0);
    host_render(buf, 256);
    check("a new note after the idle: sounds at once", peak(buf, 256) > 1000);
    app_audio_stop();
    host_render(buf, 64);
}

/* BREATH (milestone 4): still at 0; at 100 the level, the pitch and the cutoff move, each by its TO value. Measured
 * in quarter seconds over 4 s at RATE 100 (1.2 Hz with BREATH's nudge, the others x 1.375 and x 0.75). */
static void spread_q(const int32_t *b, uint32_t frames, double (*f)(const int32_t *, uint32_t), double *lo,
                     double *hi)
{
    uint32_t q = BLOCKS(FS / 4), i;
    *lo = 1e30;
    *hi = 0;
    for (i = 0; i + q <= frames; i += q) {
        double v = f(b + 2u * i, q);
        *lo = v < *lo ? v : *lo;
        *hi = v > *hi ? v : *hi;
    }
}

static double pitch_q(const int32_t *b, uint32_t frames) { return pitch(b, frames); }



static double h10_q(const int32_t *b, uint32_t frames) { return level_at(b, frames, 1746.14); }   /* F3's 10th */

static void breath(void)
{
    const uint32_t S4 = BLOCKS(4u * FS);
    double lo, hi;
    host_nor_erase();
    power_cycle();
    set.g[G_OCT] = 0;
    cur.p[P_ATTACK] = 0;
    cur.p[P_LAYERS] = 1;
    cur.p[P_SUB] = 0;
    cur.p[P_BREATH] = 0;
    cur.p[P_RATE] = 100;
    cur.p[P_TOPITCH] = 0;
    cur.p[P_TOCUT] = 0;
    cur.p[P_TOLEVEL] = 100;
    tap_key(0);
    host_render(buf, BLOCKS(FS));
    host_render(buf, S4);
    spread_q(buf, S4, rms, &lo, &hi);
    check("BREATH 0: the level still (quarter seconds within 2 %)", hi / lo < 1.02);
    cur.p[P_BREATH] = 100;
    host_render(buf, S4);
    spread_q(buf, S4, rms, &lo, &hi);
    check("BREATH 100, TO LEVEL 100: the level breathes (> 1.5 dB between quarter seconds)", hi / lo > 1.19);
    cur.p[P_TOLEVEL] = 0;
    host_render(buf, S4);
    spread_q(buf, S4, rms, &lo, &hi);
    check("TO LEVEL 0: the level still again", hi / lo < 1.05);

    fx.bypass = 1;                               /* the pitch on the voices alone: zero crossings */
    cur.p[P_TOPITCH] = 100;
    host_render(buf, S4);
    spread_q(buf, S4, pitch_q, &lo, &hi);
    check("TO PITCH 100: the pitch drifts (> 5 cents between quarter seconds, < 17)", hi / lo > 1.0029 &&
          hi / lo < 1.0099);
    cur.p[P_TOPITCH] = 0;
    host_render(buf, S4);
    spread_q(buf, S4, pitch_q, &lo, &hi);
    check("TO PITCH 0: the pitch still (< 0.5 cent)", hi / lo < 1.0003);
    fx.bypass = 0;

    cur.p[P_DARK] = 50;
    cur.p[P_TOCUT] = 100;
    host_render(buf, S4);
    {                                            /* the cutoff (~1.3 kHz at DARK 50) seen on F3's 10th harmonic */
        double l2, h2;
        spread_q(buf, S4, h10_q, &lo, &hi);
        cur.p[P_TOCUT] = 0;
        host_render(buf, S4);
        spread_q(buf, S4, h10_q, &l2, &h2);
        check("TO CUTOFF 100: the 10th harmonic swells and fades (> 12 dB), TO CUTOFF 0: much less (< 3.5 dB)",
              hi / lo > 4.0 && h2 / l2 < 1.5);  /* (still: ~1.25, the fuzz's aliases beating with it) */
    }
    app_audio_stop();
    host_render(buf, 64);
}

/* The FM engine (milestone 5): a pure sine at INDEX and FEEDBACK 0, sidebands from INDEX at RATIO's spacing, the
 * feedback's saw, MASSA's share of it, two pairs at most, the crossfade from SAW without a gap. Voices alone, F3. */
static void fm(void)
{
    const uint32_t S = BLOCKS(FS);
    const double F = 174.614;
    double f1, f2, f3;
    uint32_t i, gap;
    host_nor_erase();
    power_cycle();
    fx.bypass = 1;
    cur.p[P_BREATH] = 0;
    cur.p[P_ATTACK] = 0;
    cur.p[P_SUB] = 0;
    cur.p[P_MASSA] = 0;
    cur.p[P_LAYERS] = 1;
    cur.p[P_FB] = 0;
    cur.p[P_INDEX] = 0;
    cur.p[P_RATIO] = 1;
    set.g[G_OCT] = 0;
    cur.engine = ENG_FM;
    tap_key(0);
    host_render(buf, S); host_render(buf, S);
    f1 = level_at(buf, S, F); f2 = level_at(buf, S, 2.0 * F); f3 = level_at(buf, S, 3.0 * F);
    check("FM, INDEX and FEEDBACK 0: a pure sine at the note (2nd, 3rd < 1 %)", f1 > 6000 && f2 < f1 / 100 &&
          f3 < f1 / 100 && fabs(pitch(buf, S) / F - 1.0) < 0.003);
    cur.p[P_INDEX] = 50;
    host_render(buf, S); host_render(buf, S);
    f1 = level_at(buf, S, F); f2 = level_at(buf, S, 2.0 * F);
    check("INDEX 50, RATIO 1: harmonics appear (2nd > 1/5 of the 1st)", f2 > f1 / 5);
    cur.p[P_RATIO] = 2;
    host_render(buf, S); host_render(buf, S);
    f1 = level_at(buf, S, F); f2 = level_at(buf, S, 2.0 * F); f3 = level_at(buf, S, 3.0 * F);
    check("RATIO 2: odd sidebands (3rd strong), the even ones not (2nd < 1/20 of the 3rd)", f3 > f1 / 5 &&
          f2 < f3 / 20);
    cur.p[P_RATIO] = 1;
    cur.p[P_INDEX] = 0;
    cur.p[P_FB] = 100;
    host_render(buf, S); host_render(buf, S);
    f1 = level_at(buf, S, F); f2 = level_at(buf, S, 2.0 * F); f3 = level_at(buf, S, 3.0 * F);
    check("FEEDBACK 100: the sine grows into a saw (2nd > 1/4, 3rd > 1/8 of the 1st)", f2 > f1 / 4 && f3 > f1 / 8);
    cur.p[P_FB] = 0;
    cur.p[P_MASSA] = 100;
    host_render(buf, S); host_render(buf, S);
    f1 = level_at(buf, S, F); f2 = level_at(buf, S, 2.0 * F);
    check("MASSA 100 adds feedback (2nd > 1/8 of the 1st at FEEDBACK 0)", f2 > f1 / 8);
    cur.p[P_MASSA] = 0;
    cur.p[P_LAYERS] = 3;                         /* FM: two pairs at most, detuned like the saws (+-40 c at 100) */
    cur.p[P_BEATS] = 100;
    host_render(buf, S); host_render(buf, S);
    check("FM, 3 layers: two pairs (at +-40 c), none at the note", level_at(buf, S, F * pow(2.0, 40.0 / 1200.0)) > 1500 &&
          level_at(buf, S, F / pow(2.0, 40.0 / 1200.0)) < 300);
    cur.p[P_LAYERS] = 1;
    cur.p[P_BEATS] = 0;
    cur.engine = ENG_SAW;                        /* the crossfade: no gap, then the saw's harmonics */
    host_render(buf, BLOCKS(FS / 10));           /* 0.1 s: the whole crossfade, in windows of 256 */
    for (i = gap = 0; i + 256u <= BLOCKS(FS / 10); i += 256u)
        gap |= peak(buf + 2u * i, 256) < 2000;
    host_render(buf, S);
    check("ALGORITHM FM -> SAW: no gap through the crossfade, then a saw (2nd ~1/2)", !gap && snd.xf == 0 &&
          level_at(buf, S, 2.0 * F) > level_at(buf, S, F) / 3);
    cur.engine = ENG_FM;
    host_render(buf, 1024);
    check("SAW -> FM: still crossfading after 23 ms (not a switch)", snd.xf > 8000 && snd.xf < 24000);
    host_render(buf, 3072);
    check("SAW -> FM: the crossfade is ~50 ms (done within 4 096 samples)", snd.xf == 32768);
    app_audio_stop();
    host_render(buf, 64);
    fx.bypass = 0;
}

/* HOME's picture of the drone (milestone 9): a wave drawn behind the notes ~10 times a second while on HOME; none
 * asked for on the other pages. */
static uint32_t wave_px(void)                    /* the wave's colour in the notes area, off its centre row */
{
    uint32_t x, y, n = 0;
    for (y = 52; y < 98; y++)
        for (x = 0; x < 240u; x++)
            n += y != 75u && host_fb[y * 240u + x] == T_TINT;
    return n;
}

static void scope_shot(void)
{
    uint32_t c0, ink0;
    host_nor_erase();
    power_cycle();
    set.g[G_SLOT] = 1;
    cur = bank.s[1];                             /* MONOLITH */
    cur.p[P_ATTACK] = 0;
    ui_page(PG_HOME);
    run_frames(2);
    ink0 = wave_px();                            /* silence: a flat line on the centre row only */
    tap_key(0); tap_key(7);
    c0 = scope.count;
    run_frames(60);                              /* ~1 s */
    check("HOME: pictures of the drone, ~10 a second", scope.count - c0 >= 7u && scope.count - c0 <= 12u);
    check("HOME: silence a flat line; a drone a wave across the notes area", ink0 < 10u && wave_px() > 200u);
    host_png("build/shots/home_drone.png");
    press(B_GLO);
    c0 = scope.count;
    run_frames(30);
    check("other pages: no pictures asked for", scope.count - c0 <= 1u);
    press(B_FX);
    c0 = scope.count;
    run_frames(60);
    check("FX: pictures for the amp, ~10 a second", scope.count - c0 >= 7u && scope.count - c0 <= 12u);
    host_png("build/shots/fx_drone.png");
    cur.p[P_DRIVE] = 100;
    cur.p[P_MASSA] = 100;
    run_frames(60);
    host_png("build/shots/fx_drone_hot.png");
    set.g[G_DMIX] = 70;
    set.g[G_DFB] = 60;
    set.g[G_DHEADS] = 7;
    press(B_ARP);
    run_frames(240);                             /* ~4 s: the echoes fill the tape */
    host_png("build/shots/delay_drone.png");
    set.g[G_DGLIDE] = 90;
    set.g[G_DT] = 5;
    run_frames(45);
    host_png("build/shots/delay_glide.png");
    check("DELAY: while TIME glides the picture shows the bend", dl.v_pub != 0);
    set.g[G_DMIX] = 0;
    app_audio_stop();
    host_render(buf, 64);
}

/* The factory sounds (milestone 8): sixteen names, every value in its range, each one droning (a chord through the
 * whole chain) without silence and within full scale. */
static void factory(void)
{
    uint32_t i, j, names = 1, ranges = 1, sounds = 1;
    host_nor_erase();
    power_cycle();
    for (i = 0; i < NSND; i++) {
        for (j = 0; j < i; j++)
            names &= memcmp(bank.s[i].name, bank.s[j].name, NAME_LEN) != 0;
        for (j = 0; j < NP; j++)
            ranges &= bank.s[i].p[j] >= SP[j].lo && bank.s[i].p[j] <= SP[j].hi;
        ranges &= bank.s[i].engine <= ENG_FM;
    }
    check("factory: 16 different names", names);
    check("factory: every value of every sound in its range", ranges);
    check("factory: slot 1 INIT, slot 2 MONOLITH, slot 6 FM", !memcmp(bank.s[0].name, "INIT 01     ", NAME_LEN) &&
          !memcmp(bank.s[1].name, "MONOLITH    ", NAME_LEN) && bank.s[5].engine == ENG_FM);
    set.g[G_OCT] = -2;
    for (i = 0; i < NSND; i++) {
        int32_t pk;
        set.g[G_SLOT] = (int16_t)i;
        cur = bank.s[i];
        cur.p[P_ATTACK] = 0;
        tap_key(0); tap_key(7); tap_key(12);
        host_render(buf, BLOCKS(FS));
        host_render(buf, BLOCKS(FS));
        pk = peak(buf, BLOCKS(FS));
        sounds &= pk > 1500 && pk <= 32767;
        if (pk <= 1500)
            printf("    factory sound %u too quiet: peak %d\n", i + 1u, pk);
        app_audio_stop();
        host_render(buf, 64);
    }
    check("factory: every sound drones (a chord: audible, within full scale)", sounds);
}

/* The cassette's audio (milestones 6, 7). The voices alone (fx.bypass: a clean saw to measure), F3 (174.61 Hz). */
static void tape_rec_s(uint32_t frames)          /* REC, frames of audio, REC: a loop of that length, playing */
{
    press_t(B_REC);
    host_render(buf, frames);
    press_t(B_REC);
}

static void tape_audio(void)
{
    const uint32_t S = BLOCKS(FS), L = 44096u;   /* a 1 s loop (22 048 cells: 44 096 output samples at 1x) */
    double p;
    uint32_t i, same;
    host_nor_erase();
    power_cycle();
    fx.bypass = 1;
    cur.p[P_BREATH] = 0;
    cur.p[P_LAYERS] = 1;
    cur.p[P_SUB] = 0;
    cur.p[P_ATTACK] = 0;
    cur.p[P_RELEASE] = 0;
    set.g[G_OCT] = 0;
    set.g[G_WEAR] = 0;
    set.g[G_DECAY] = 0;
    tap_key(0);
    host_render(buf, 1024);
    tape_rec_s(L - 64u);                         /* (press_t renders 64 more) */
    check("1 s recorded: playing, the length 1.0 s", tape.st == TP_PLAY && tape.len >= 990u && tape.len <= 1010u);
    tap_key(0);                                  /* the live note off: from here only the tape sounds */
    host_render(buf, 2048);
    host_render(buf, 2u * L);
    p = pitch(buf, 2u * L);
    check("playback: the recorded note (174.6 Hz +-1 %)", fabs(p / 174.61 - 1.0) < 0.01 && peak(buf, 2u * L) > 4000 &&
          peak(buf, 2u * L) < 8500);
    for (i = same = 0; i < tape.n * 2u; i++)
        same += buf[2u * i] == buf[2u * (i + tape.n * 2u)];
    check("WEAR 0: the loop repeats sample for sample", same == tape.n * 2u);
    set.g[G_SPEED] = 50;
    host_render(buf, S);
    host_render(buf, S);
    check("SPEED 1/2: an octave down (87.3 Hz +-1 %)", fabs(pitch(buf, S) / 87.307 - 1.0) < 0.01);
    set.g[G_SPEED] = 75;
    host_render(buf, S);
    check("SPEED 75: varispeed (131 Hz +-1 %)", fabs(pitch(buf, S) / (174.61 * 0.75) - 1.0) < 0.01);
    set.g[G_SPEED] = 100;
    press_t(B_HOME);

    set.g[G_DECAY] = 50;                         /* overdub with nothing live: each pass keeps half */
    host_render(buf, L);
    p = rms(buf, L);
    press_t(B_REC);
    check("REC while playing: DUB", tape.st == TP_DUB);
    host_render(buf, L);                         /* one pass written at 1/2 */
    host_render(buf, L);                         /* the next pass plays it */
    check("DECAY 50: one silent overdub pass halves the tape (RMS 0.5 +-0.05)", fabs(rms(buf, L) / p - 0.5) < 0.05);
    set.g[G_DECAY] = 0;                          /* (the pass just played was still written at 1/2) */
    host_render(buf, L);
    p = rms(buf, L);
    host_render(buf, L);
    check("DECAY 0: a silent overdub pass keeps the tape (+-3 %)", fabs(rms(buf, L) / p - 1.0) < 0.03);
    tap_key(4);                                  /* A3 (220 Hz) added live while dubbing */
    host_render(buf, L);
    tap_key(4);
    host_render(buf, 2048);
    host_render(buf, L);
    check("overdub: the tape now holds the old note (faded twice) and the new one", level_at(buf, L, 174.61) > 300 &&
          level_at(buf, L, 220.0) > 1000);
    press_t(B_REC);
    check("REC: overdub off, playing", tape.st == TP_PLAY);

    set.g[G_BRAKE] = 10;                         /* a 1 s brake */
    host_render(buf, 4096);
    p = pitch(buf, 4096);
    press_t(B_PLAY);
    check("PLAY: STOP shown at once, the brake begins", tape.st == TP_STOP && tape.braking);
    host_render(buf, BLOCKS(FS / 2));            /* (the last 0.1 s of the first half: speed ~0.5) */
    check("half way through the brake: the pitch lower (below 0.65)", pitch(buf + 2u * (BLOCKS(FS / 2) - 4096u), 4096) <
          p * 0.65);
    host_render(buf, S);
    check("after the brake: silent, stopped", peak(buf + 2u * (S - 8192u), 8192) == 0 && tape.spd == 0);
    press_t(B_PLAY);
    host_render(buf, BLOCKS(FS / 2));            /* (spun up after 0.3 s) */
    check("PLAY: spins up and plays again at speed (A3 on the tape at 220 Hz again)", tape.st == TP_PLAY &&
          !tape.spinning && tape.spd == 65536 && level_at(buf + 2u * (BLOCKS(FS / 2) - 8192u), 8192, 220.0) > 1000);

    set.g[G_DIR] = 1;                            /* reverse: a swell recorded plays as a fade */
    host_render(buf, 64);
    {
        double a1, a2;
        host_nor_erase();
        press_t(B_PLAY);                          /* (stop, erase, record a 1 s swell from silence) */
        host_press(B_REC, 1);
        run_frames(70);
        host_press(B_REC, 0);
        host_frame();
        host_render(buf, 64);
        set.g[G_DIR] = 0;
        cur.p[P_ATTACK] = 10;                    /* 1 s swell */
        tap_key(0);
        tape_rec_s(L - 64u);
        tap_key(0);
        host_render(buf, 2048);
        while (tape.ip > 64u)                    /* to the loop's start */
            host_render(buf, CTL);
        host_render(buf, L);
        a1 = rms(buf + 2u * 2048u, 8192) / rms(buf + 2u * (L - 10240u), 8192);
        set.g[G_DIR] = 1;
        host_render(buf, L);
        a2 = rms(buf + 2u * 2048u, 8192) / rms(buf + 2u * (L - 10240u), 8192);
        check("forward: the swell rises; DIRECTION reverse: it falls", a1 < 0.6 && a2 > 1.6);
        set.g[G_DIR] = 0;
        host_press(B_REC, 1);                    /* a steady loop again for WEAR */
        run_frames(70);
        host_press(B_REC, 0);
        host_frame();
        host_render(buf, 64);
        cur.p[P_ATTACK] = 0;
        tap_key(0);
        host_render(buf, 1024);
        tape_rec_s(L - 64u);
        tap_key(0);
        host_render(buf, 2048);
    }

    {                                            /* wow and flutter: the head after each loop's worth of samples */
        uint32_t w = 2u * tape.n, k, ip0, drift = 0, d;
        while (tape.ip > 64u)
            host_render(buf, CTL);
        set.g[G_WEAR] = 0;
        ip0 = tape.ip;
        for (k = 0; k < 4u; k++) {
            host_render(buf, w);
            d = tape.ip > ip0 ? tape.ip - ip0 : ip0 - tape.ip;
            drift = d > drift ? d : drift;
        }
        check("WEAR 0: the head back at the same cell after every loop (exact speed)", drift == 0u);
        set.g[G_WEAR] = 100;
        for (k = drift = 0; k < 4u; k++) {
            host_render(buf, w);
            d = tape.ip > ip0 ? tape.ip - ip0 : ip0 - tape.ip;
            d = d > tape.n / 2u ? tape.n - d : d;
            drift = d > drift ? d : drift;
        }
        check("WEAR 100: the speed wavers (the head > 20 cells off after a loop: wow ~+-0.6 %)", drift > 20u);
        set.g[G_WEAR] = 0;
    }

    host_press(B_REC, 1);                        /* erase: silence again */
    run_frames(70);
    host_press(B_REC, 0);
    host_frame();
    host_render(buf, 2048);
    check("erased: the tape silent", tape.st == TP_EMPTY && peak(buf + 2u * 1024u, 1024) == 0);
    press_t(B_REC);                              /* a silent loop: only the hiss of WEAR */
    host_render(buf, 8192);
    press_t(B_REC);
    host_render(buf, S);
    check("a silent tape at WEAR 0: exact silence", peak(buf, S) == 0);
    set.g[G_WEAR] = 100;
    host_render(buf, S);
    check("a silent tape at WEAR 100: a faint hiss (0 < peak < 300)", peak(buf, S) > 0 && peak(buf, S) < 300);
    set.g[G_WEAR] = GP[G_WEAR].def;
    fx.bypass = 0;
    app_audio_stop();
    host_render(buf, 64);
}

/* ------------------------------------------------------------------ delay --- */
#define DN 400000u
static int32_t dl_in[DN + 64u];
static int32_t dl_out[2u * (DN + 64u)];          /* left: the echo alone (out - in), right: the whole output */

static void dly_setup(int t, int fb, int mix, int heads, int wob, int dull)
{
    memset(&dl, 0, sizeof dl);
    memset(dl_cells, 0, sizeof dl_cells);
    set_default();
    set.g[G_DT] = (int16_t)t;
    set.g[G_DFB] = (int16_t)fb;
    set.g[G_DMIX] = (int16_t)mix;
    set.g[G_DHEADS] = (int16_t)heads;
    set.g[G_DWOB] = (int16_t)wob;
    set.g[G_DDULL] = (int16_t)dull;
    set.g[G_DGLIDE] = 100;                       /* (the slowest motor: the bend tests below were made for it) */
}

static void dly_run(const int32_t *in, uint32_t frames, int32_t *o)   /* whole blocks through dly_process */
{
    int32_t blk[2u * CTL];
    uint32_t i, j;
    for (i = 0; i + CTL <= frames; i += CTL) {
        for (j = 0; j < CTL; j++) {
            blk[2u * j] = in[i + j];
            blk[2u * j + 1u] = 0;
        }
        dly_process(blk, CTL);
        for (j = 0; j < CTL; j++) {
            o[2u * (i + j)] = blk[2u * j] - in[i + j];
            o[2u * (i + j) + 1u] = blk[2u * j];
        }
    }
}

static uint32_t argmax_abs(const int32_t *o, uint32_t from, uint32_t to)   /* the echo's loudest sample */
{
    uint32_t i, at = from;
    int32_t m = -1;
    for (i = from; i < to; i++)
        if (abs(o[2u * i]) > m) {
            m = abs(o[2u * i]);
            at = i;
        }
    return at;
}

static void burst(uint32_t frames)               /* a click of 64 samples at 12 000 at the start */
{
    memset(dl_in, 0, (frames + 64u) * sizeof dl_in[0]);
    memset(dl_out, 0, 2u * (frames + 64u) * sizeof dl_out[0]);
    memset(dl_in, 0, 64u * sizeof dl_in[0]);
    {
        uint32_t i;
        for (i = 0; i < 64u; i++)
            dl_in[i] = 12000;
    }
}

static void sine_in(uint32_t from, uint32_t frames, double hz, double amp)   /* phase-continuous from sample 0 */
{
    uint32_t i;
    for (i = from; i < from + frames; i++)
        dl_in[i] = (int32_t)(amp * sin(2.0 * M_PI * hz * i / FS));
}

static void delay_audio(void)
{
    uint32_t d, i, at[3];
    int32_t pk;
    double r;
    reset_state();
    host_boot();
    check("TIME 0 .. 100: 60 ms .. 950 ms, rising", dly_ms10(0) == 600 && dly_ms10(100) == 9500 &&
          dly_ms10(50) > dly_ms10(49));
    check("the farthest head at the longest TIME (+ the wobble) is inside the line",
          3u * (uint32_t)(((int64_t)dly_ms10(100) * DL_Q >> 16) + 20) < DL_N - 64u);

    /* the timing of the heads */
    d = 4u * (uint32_t)((int64_t)dly_ms10(45) * DL_Q >> 16);          /* TIME 45, in output samples */
    dly_setup(45, 0, 100, 1, 0, 0);
    burst(40000);
    dly_run(dl_in, 40000, dl_out);
    at[0] = argmax_abs(dl_out, 2000, 40000);
    pk = abs(dl_out[2u * at[0]]);
    check("head 1: one echo, D after the click (the click's edge, 0 .. 90 samples on)", at[0] + 8u >= d && at[0] <= d + 90u);
    check("head 1: the echo is the click's size (the line is not attenuating it)", pk > 9000 && pk < 12500);
    check("head 1 alone: nothing between and nothing after (below 2 %)",
          abs(dl_out[2u * (d / 2u)]) < pk / 50 && abs(dl_out[2u * (2u * d)]) < pk / 50 &&
          abs(dl_out[2u * (3u * d + 32u)]) < pk / 50);
    dly_setup(45, 0, 100, 7, 0, 0);
    burst(40000);
    dly_run(dl_in, 40000, dl_out);
    at[0] = argmax_abs(dl_out, 2000, d + 2000u);
    at[1] = argmax_abs(dl_out, d + 2000u, 2u * d + 2000u);
    at[2] = argmax_abs(dl_out, 2u * d + 2000u, 36000);
    check("heads 1 2 3: echoes at D, 2 D, 3 D (each 0 .. 90 samples after)",
          at[0] + 8u >= d && at[0] <= d + 90u && at[1] + 8u >= 2u * d && at[1] <= 2u * d + 90u &&
          at[2] + 8u >= 3u * d && at[2] <= 3u * d + 90u);
    r = (double)abs(dl_out[2u * at[2]]) / (double)abs(dl_out[2u * at[0]]);
    check("three heads: equal echoes (+-10 %), each at 0.46 of one head's", r > 0.9 && r < 1.1 &&
          abs(dl_out[2u * at[0]]) > 4000 && abs(dl_out[2u * at[0]]) < 5800);
    dly_setup(45, 0, 100, 2, 0, 0);
    burst(40000);
    dly_run(dl_in, 40000, dl_out);
    at[0] = argmax_abs(dl_out, 2000, 40000);
    check("HEADS 2 only: the echo at 2 D", at[0] + 8u >= 2u * d && at[0] <= 2u * d + 90u &&
          abs(dl_out[2u * (d + 32u)]) < 300);

    /* MIX 0 is the dry signal itself */
    dly_setup(45, 60, 0, 7, 50, 40);
    {
        uint32_t k;
        uint32_t seed = 7;
        for (k = 0; k < 160000u; k++) {
            seed = seed * 1664525u + 1013904223u;
            dl_in[k] = (int32_t)(seed >> 16) % 8000 - 4000;
        }
    }
    dly_run(dl_in, 160000, dl_out);
    {
        int32_t m = 0;
        for (i = 0; i < 160000u; i++)
            m = abs(dl_out[2u * i]) > m ? abs(dl_out[2u * i]) : m;
        check("ECHO 0: the signal passes untouched (exact), the line forgotten after 3 s (no cost)", m == 0 && !dl.dirty);
    }

    /* the repeats */
    dly_setup(45, 50, 100, 1, 0, 0);
    burst(60000);
    dly_run(dl_in, 60000, dl_out);
    at[0] = argmax_abs(dl_out, d, d + 400u);
    at[1] = argmax_abs(dl_out, 2u * d, 2u * d + 400u);
    r = (double)abs(dl_out[2u * at[1]]) / (double)abs(dl_out[2u * at[0]]);
    check("REPEATS 50: the second echo is about half the first (0.35 .. 0.65)", r > 0.35 && r < 0.65);

    /* REPEATS 100: it plays itself, held by the saturation */
    dly_setup(10, 100, 100, 1, 0, 40);
    memset(dl_in, 0, (DN + 64u) * sizeof dl_in[0]);
    sine_in(0, 13000, 220.0, 8000.0);
    dly_run(dl_in, 352000, dl_out);
    pk = peak(dl_out + 2u * 308000u, 40000);
    check("REPEATS 100: the loop sustains (a second after the input, RMS > 4 000)",
          rms(dl_out + 2u * 308000u, 40000) > 4000.0);
    check("REPEATS 100: held by the tape's saturation (a peak ~21 000, never past 26 000)",
          pk > 12000 && pk < 26000);

    /* and a decaying loop ends in exact silence */
    dly_setup(10, 40, 100, 3, 20, 40);
    memset(dl_in, 0, (DN + 64u) * sizeof dl_in[0]);
    sine_in(0, 13000, 220.0, 8000.0);
    dly_run(dl_in, 396000, dl_out);
    check("REPEATS 40: the echoes die away (the last second is exactly silent)",
          peak(dl_out + 2u * 356000u, 40000) == 0 && !dl.dirty);

    /* TIME as a motor: the echoes bend */
    dly_setup(100, 0, 100, 1, 0, 0);
    memset(dl_in, 0, (DN + 64u) * sizeof dl_in[0]);
    sine_in(0, 280000, 440.0, 6000.0);
    dly_run(dl_in, 88200, dl_out);
    r = pitch(dl_out + 2u * 66150u, 22050);
    check("TIME steady: the echo's pitch is the input's (440 Hz +-4)", r > 436.0 && r < 444.0);
    set.g[G_DT] = 0;
    dly_run(dl_in + 88200u, 132300, dl_out + 2u * 88200u);
    r = pitch(dl_out + 2u * 154350u, 22050);
    check("TIME 100 -> 0: the heads close in, head 1 reads faster (about +16 %: 490 .. 525 Hz)", r > 490.0 && r < 525.0);
    set.g[G_DT] = 100;
    dly_run(dl_in + 220500u, 59600, dl_out + 2u * 220500u);
    r = pitch(dl_out + 2u * 257000u, 22000);
    check("TIME 0 -> 100: the heads part, head 1 reads slower (below 400 Hz)", r > 330.0 && r < 400.0);
    check("D follows TIME at a limited speed (not there yet after 1.5 s)", dl.d > dly_ms10(0) * DL_Q * 3);

    dly_setup(100, 0, 100, 4, 0, 0);
    memset(dl_in, 0, (DN + 64u) * sizeof dl_in[0]);
    sine_in(0, 330000, 440.0, 6000.0);
    dly_run(dl_in, 154350, dl_out);
    r = pitch(dl_out + 2u * 132300u, 22050);
    check("head 3 at TIME 100: steady 440 Hz (+-4)", r > 436.0 && r < 444.0);
    set.g[G_DT] = 0;
    dly_run(dl_in + 154350u, 110250, dl_out + 2u * 154350u);
    r = pitch(dl_out + 2u * 220500u, 22050);
    check("head 3 bends three times as far as head 1 (+48 %: 610 .. 690 Hz)", r > 610.0 && r < 690.0);

    /* WOBBLE */
    {
        double lo = 1e9, hi = 0, mean = 0;
        uint32_t w;
        dly_setup(20, 0, 100, 1, 100, 0);
        memset(dl_in, 0, (DN + 64u) * sizeof dl_in[0]);
        sine_in(0, 140000, 4000.0, 6000.0);
        dly_run(dl_in, 140000, dl_out);
        for (w = 0; w < 8u; w++) {
            double f = pitch(dl_out + 2u * (30000u + w * 11025u), 11025);
            lo = f < lo ? f : lo;
            hi = f > hi ? f : hi;
            mean += f / 8.0;
        }
        check("WOBBLE 100: the echo's pitch wavers (a spread over 0.4 %)", (hi - lo) / mean > 0.004);
        dly_setup(20, 0, 100, 1, 0, 0);
        lo = 1e9;
        hi = 0;
        dly_run(dl_in, 140000, dl_out);
        for (w = 0; w < 8u; w++) {
            double f = pitch(dl_out + 2u * (30000u + w * 11025u), 11025);
            lo = f < lo ? f : lo;
            hi = f > hi ? f : hi;
        }
        check("WOBBLE 0: the pitch is still (a spread under 0.15 %)", (hi - lo) / (hi + lo) * 2.0 < 0.0015);
    }

    /* silence costs nothing and changes nothing */
    dly_setup(45, 60, 100, 7, 50, 40);
    {
        int32_t blk[2u * CTL];
        memset(blk, 0, sizeof blk);
        {
            uint32_t w0 = dl.w;
            dly_process(blk, CTL);
            check("an empty line and a quiet input: the block untouched, the tape still running",
                  peak(blk, CTL) == 0 && dl.w == w0 + CTL / 4u && !dl.dirty);
        }
    }

    /* GLIDE: the motor's speed. 0: across the whole range in ~0.15 s (a fast turn is followed at once); 100: ~6 s */
    {
        int32_t tgt;
        dly_setup(100, 0, 100, 1, 0, 0);
        memset(dl_in, 0, (DN + 64u) * sizeof dl_in[0]);
        sine_in(0, 88200, 440.0, 6000.0);
        set.g[G_DGLIDE] = 0;
        dly_run(dl_in, 22050, dl_out);
        set.g[G_DT] = 0;
        tgt = dly_ms10(0) * DL_Q;
        dly_run(dl_in + 22050u, 13230, dl_out + 2u * 22050u);                  /* 0.3 s */
        check("GLIDE 0: TIME 100 -> 0 across the whole range is done in 0.3 s", abs(dl.d - tgt) < DL_Q * 40);
        dly_setup(100, 0, 100, 1, 0, 0);
        set.g[G_DGLIDE] = 50;
        dly_run(dl_in, 22050, dl_out);
        set.g[G_DT] = 0;
        dly_run(dl_in + 22050u, 13230, dl_out + 2u * 22050u);
        check("GLIDE 50: the same turn is half way after 0.3 s, and done in ~2 s",
              dl.d - tgt > DL_Q * 2000 && dl.d - tgt < DL_Q * 8000);
        dly_run(dl_in + 35280u, 52920, dl_out + 2u * 35280u);                  /* +1.2 s */
        check("GLIDE 50: ... and there after 1.5 s in all", abs(dl.d - tgt) < DL_Q * 60);
        dly_setup(100, 0, 100, 1, 0, 0);
        set.g[G_DGLIDE] = 0;
        dly_run(dl_in, 22050, dl_out);
        set.g[G_DT] = 0;
        dly_run(dl_in + 22050u, 1280, dl_out + 2u * 22050u);                   /* a fast turn: 30 ms later it has gone */
        check("GLIDE 0: 30 ms into a full turn the tape is already moving fast (not the old 6 s crawl)",
              abs(dl.v_pub) > 500000);
        pk = 0;
        for (d = 0; d < 1280u; d++)
            pk = abs(dl_out[2u * (22050u + d)]) > pk ? abs(dl_out[2u * (22050u + d)]) : pk;
        check("GLIDE 0: the throw stays bounded", pk < 40000);
    }

    /* TAPE DRIVE: the saturation takes the crest off a loud sine */
    {
        double c0, c1;
        uint32_t k;
        int32_t blk[2u * CTL];
        for (k = 0; k < 2u; k++) {
            dly_setup(10, 0, 100, 1, 0, 0);
            set.g[G_DDRV] = k ? 100 : 0;
            memset(dl_in, 0, (DN + 64u) * sizeof dl_in[0]);
            sine_in(0, 88200, 330.0, 20000.0);
            dly_run(dl_in, 88200, dl_out);
            pk = 0;
            for (d = 66150u; d < 88200u; d++)    /* (the left channel: the echo alone) */
                pk = abs(dl_out[2u * d]) > pk ? abs(dl_out[2u * d]) : pk;
            if (k)
                c1 = pk / rms(dl_out + 2u * 66150u, 22050);
            else
                c0 = pk / rms(dl_out + 2u * 66150u, 22050);
        }
        (void)blk;
        check("TAPE DRIVE 100 flattens a loud sine (crest 0.08 lower), 0 leaves it (about 1.41)",
              c1 < c0 - 0.08 && c0 > 1.39);
    }

    /* FREEZE: the loop holds what it has */
    {
        double a, b;
        dly_setup(10, 0, 100, 1, 0, 0);
        memset(dl_in, 0, (DN + 64u) * sizeof dl_in[0]);
        sine_in(0, 22050, 220.0, 8000.0);
        dly_run(dl_in, 22050, dl_out);
        dl.freeze = 1;
        dly_run(dl_in + 22050u, 220500, dl_out + 2u * 22050u);                 /* 5 s of silence in */
        a = rms(dl_out + 2u * 66150u, 22050);
        b = rms(dl_out + 2u * 220500u, 22050);
        check("FREEZE: the held loop still sounds after 5 s, at about its level (REPEATS 0)", a > 800.0 && b > 0.5 * a);
        dl.freeze = 0;
        dly_run(dl_in + 242550u, 66150, dl_out + 2u * 242550u);
        check("FREEZE off: REPEATS 0 lets it end (a second later only the one-pole's last bits are left)",
              peak(dl_out + 2u * (242550u + 44100u), 22050) < 20);
    }

    /* GRAINS */
    {
        uint32_t k, live = 0;
        double r2, e1;
        dly_setup(45, 0, 0, 1, 0, 0);
        set.g[G_GMIX] = 100;
        set.g[G_GSIZE] = 50;
        set.g[G_GDENS] = 50;
        set.g[G_GSCAT] = 0;
        set.g[G_GREV] = 0;
        set.g[G_GPITCH] = 0;
        memset(dl_in, 0, (DN + 64u) * sizeof dl_in[0]);
        sine_in(0, 44100, 440.0, 8000.0);
        dly_run(dl_in, 132300, dl_out);
        r2 = pitch(dl_out + 2u * 20000u, 20000);
        e1 = rms(dl_out + 2u * 20000u, 20000);
        check("GRAINS (0 st, forward): a 440 Hz sine comes back as 440 Hz (+-6)", r2 > 434.0 && r2 < 446.0);
        check("GRAINS: audible, and not louder than a few input levels", e1 > 800.0 && e1 < 30000.0);
        check("GRAINS: they play on after the input ends (0.04 .. 0.14 s after: still sounding)",
              rms(dl_out + 2u * 46000u, 4000) > 300.0);
        for (k = 0; k < 6u; k++)
            live += (dl.gmask >> k) & 1u;
        check("GRAINS: at most six live", live <= DL_NG);
        set.g[G_GMIX] = 0;
        dly_run(dl_in + 132300u, 264600 - 132300u, dl_out + 2u * 132300u);
        check("GRAINS off and a quiet input: after the line's length everything is cleared", !dl.dirty && !dl.gmask);

        dly_setup(45, 0, 0, 1, 0, 0);                          /* the same, backwards */
        set.g[G_GMIX] = 100;
        set.g[G_GSIZE] = 20;
        set.g[G_GDENS] = 50;
        set.g[G_GSCAT] = 40;
        set.g[G_GREV] = 100;
        sine_in(0, 44100, 440.0, 8000.0);
        dly_run(dl_in, 88200, dl_out);
        r2 = pitch(dl_out + 2u * 20000u, 20000);
        check("GRAINS REVERSE 100: a sine reversed is the same sine (440 Hz +-8)", r2 > 432.0 && r2 < 448.0);

        dly_setup(45, 0, 0, 1, 0, 0);                          /* +12 st only: GRAIN PITCH 25 is {0, +12} */
        set.g[G_GMIX] = 100;
        set.g[G_GSIZE] = 20;
        set.g[G_GDENS] = 100;
        set.g[G_GSCAT] = 0;
        set.g[G_GREV] = 0;
        set.g[G_GPITCH] = 25;
        sine_in(0, 44100, 440.0, 8000.0);
        dly_run(dl_in, 88200, dl_out);
        e1 = 0;
        for (k = 0; k < 10u; k++) {                            /* the pitch of short stretches: 440 and 880 both turn up */
            r2 = pitch(dl_out + 2u * (12000u + 3000u * k), 2800);
            e1 += r2 > 800.0 && r2 < 960.0;
        }
        check("GRAIN PITCH 25: some grains are an octave up (880 Hz turns up in the output)", e1 >= 1.0);
        set.g[G_GFB] = 100;
        set.g[G_GMIX] = 100;
        set.g[G_GPITCH] = 100;
        set.g[G_GREV] = 50;
        dly_run(dl_in, 396000, dl_out);
        pk = peak(dl_out, 396000);
        check("GRAIN FEED 100 with everything on: bounded (< 80 000)", pk < 80000);
    }

    /* anything thrown at it: noise, every knob jumping, all heads, REPEATS 100 (run under ASan / UBSan too) */
    {
        uint32_t seed = 99, k, bl;
        int32_t blk[2u * CTL], worst = 0;
        dly_setup(50, 100, 100, 7, 100, 0);
        for (bl = 0; bl < 40000u; bl++) {
            if (bl % 300u == 0u) {
                seed = seed * 1664525u + 1013904223u;
                set.g[G_DT] = (int16_t)(seed >> 24) % 101;
                set.g[G_DFB] = (int16_t)((seed >> 16 & 255u) % 101u);
                set.g[G_DHEADS] = (int16_t)(1 + (seed >> 8 & 255u) % 7u);
                set.g[G_DWOB] = (int16_t)((seed >> 4 & 255u) % 101u);
                set.g[G_DDULL] = (int16_t)((seed >> 12 & 255u) % 101u);
                set.g[G_DGLIDE] = (int16_t)((seed >> 20 & 255u) % 101u);
                set.g[G_DDRV] = (int16_t)((seed >> 6 & 255u) % 101u);
                set.g[G_GMIX] = (int16_t)((seed >> 3 & 255u) % 101u);
                set.g[G_GSIZE] = (int16_t)(1 + (seed >> 10 & 255u) % 100u);
                set.g[G_GDENS] = (int16_t)(1 + (seed >> 14 & 255u) % 100u);
                set.g[G_GPITCH] = (int16_t)((seed >> 18 & 255u) % 101u);
                set.g[G_GSCAT] = (int16_t)((seed >> 22 & 255u) % 101u);
                set.g[G_GREV] = (int16_t)((seed >> 2 & 255u) % 101u);
                set.g[G_GFB] = (int16_t)((seed >> 9 & 255u) % 101u);
                dl.freeze = (uint8_t)(seed >> 5 & 1u);
            }
            for (k = 0; k < CTL; k++) {
                seed = seed * 1664525u + 1013904223u;
                blk[2u * k] = (int32_t)(seed >> 16) % 20000 - 10000;
                blk[2u * k + 1u] = 0;
            }
            dly_process(blk, CTL);
            for (k = 0; k < CTL; k++)
                worst = abs(blk[2u * k]) > worst ? abs(blk[2u * k]) : worst;
        }
        check("TIME / REPEATS / HEADS / WOBBLE thrown about over noise: the output stays bounded (< 80 000)",
              worst < 80000);
    }
    reset_state();
}


/* Pitch apart from speed, the pads, the artifacts (the cassette's second round). The same 1 s loop of F3. */
static uint32_t tape_adv(uint32_t frames)        /* render, and how many cells the head moved (mod the loop) */
{
    uint32_t a = tape.ip_pub, b;
    host_render(buf, frames);
    b = tape.ip_pub;
    return b >= a ? b - a : b + tape.n - a;
}

static void tape_pads_audio(void)
{
    const uint32_t S = BLOCKS(FS), L = 44096u;
    uint32_t i, k, lo, hi, cnt, sip;
    double p, base;
    host_nor_erase();
    power_cycle();
    fx.bypass = 1;
    cur.p[P_BREATH] = 0;
    cur.p[P_LAYERS] = 1;
    cur.p[P_SUB] = 0;
    cur.p[P_ATTACK] = 0;
    cur.p[P_RELEASE] = 0;
    set.g[G_OCT] = 0;
    set.g[G_WEAR] = 0;
    set.g[G_DECAY] = 0;
    tap_key(0);
    host_render(buf, 1024);
    tape_rec_s(L - 64u);
    tap_key(0);
    host_render(buf, 2048);
    host_render(buf, L);
    base = pitch(buf, L);
    check("pads: the loop sounds at F3 first (174.6 Hz +-1 %)", fabs(base / 174.61 - 1.0) < 0.01);

    set.g[G_PITCH] = 12;
    host_render(buf, S);
    host_render(buf, 22050);
    p = pitch(buf, 22050);
    check("PITCH +12: an octave up (349.2 Hz +-3 %), the time as it was", fabs(p / 349.22 - 1.0) < 0.03 && tape.gon);
    i = tape_adv(22050);
    check("PITCH +12: the head still moves at 1x (11 025 cells in 0.5 s, +-3 %)", fabs(i / 11025.0 - 1.0) < 0.03);
    set.g[G_PITCH] = -12;
    host_render(buf, S);
    host_render(buf, 22050);
    p = pitch(buf, 22050);
    check("PITCH -12: an octave down (87.3 Hz +-4 %)", fabs(p / 87.307 - 1.0) < 0.04);
    set.g[G_PITCH] = 7;
    host_render(buf, S);
    host_render(buf, 22050);
    p = pitch(buf, 22050);
    check("PITCH +7: a fifth up (261.6 Hz +-3 %)", fabs(p / 261.63 - 1.0) < 0.03);
    set.g[G_PITCH] = 0;
    host_render(buf, S);
    for (i = 0; i < 6u; i++)
        host_render(buf, 2048);                  /* the grains hand over to the plain read */
    check("PITCH 0 again: the grains are off, the plain read is back", !tape.gon);
    p = pitch(buf, 2048u * 4u);
    host_render(buf, S);
    check("... at F3 (174.6 Hz +-1 %)", fabs(pitch(buf, S) / 174.61 - 1.0) < 0.01);
    (void)p;

    set.g[G_SPEED] = 50;
    set.g[G_LOCK] = 1;
    host_render(buf, S);
    host_render(buf, 22050);
    p = pitch(buf, 22050);
    check("SPEED 50 with LOCK: the pitch stays (174.6 Hz +-3 %)", fabs(p / 174.61 - 1.0) < 0.03);
    i = tape_adv(22050);
    check("SPEED 50 with LOCK: the head moves at half (5 512 cells in 0.5 s, +-3 %)", fabs(i / 5512.5 - 1.0) < 0.03);
    set.g[G_LOCK] = 0;
    host_render(buf, S);
    host_render(buf, 22050);
    p = pitch(buf, 22050);
    check("SPEED 50 without LOCK: the pitch falls with it (87.3 Hz +-2 %)", fabs(p / 87.307 - 1.0) < 0.02);
    set.g[G_SPEED] = 100;
    host_render(buf, S);
    host_render(buf, S);

    tape_pads(1u << PD_HALF);
    host_render(buf, S);
    host_render(buf, 22050);
    check("pad HALF: half speed, an octave down (87.3 Hz +-2 %)", fabs(pitch(buf, 22050) / 87.307 - 1.0) < 0.02);
    tape_pads(1u << PD_DOUBLE);
    host_render(buf, S);
    host_render(buf, 22050);
    check("pad DOUBLE: an octave up (349.2 Hz +-2 %)", fabs(pitch(buf, 22050) / 349.22 - 1.0) < 0.02);
    tape_pads(1u << PD_STOP);
    host_render(buf, S);
    host_render(buf, 4096);
    check("pad STOP: the tape stops (silence)", peak(buf, 4096) < 30);
    tape_pads(0);
    host_render(buf, S);
    host_render(buf, 8192);
    check("pad STOP released: it spins up again and plays", peak(buf, 8192) > 2000);
    tape_pads(1u << PD_REV);
    host_render(buf, S);
    host_render(buf, 22050);
    check("pad REV: the same pitch, backwards (174.6 Hz +-2 %)", fabs(pitch(buf, 22050) / 174.61 - 1.0) < 0.02);
    tape_pads(0);
    host_render(buf, S);

    tape_pads(1u << PD_FREEZE);
    host_render(buf, 2048);
    lo = tape.ip_pub;
    host_render(buf, 22050);
    hi = tape.ip_pub;
    check("pad FREEZE: the head stays put, the sound goes on", (hi == lo || hi - lo < 4u) && peak(buf, 22050) > 1000);
    tape_pads(0);
    host_render(buf, S);
    for (i = 0; i < 6u; i++)
        host_render(buf, 2048);

    tape_pads(1u << PD_RESTART);
    host_render(buf, 64);
    tape_pads(0);
    check("pad RESTART: the head is at the start", tape.ip_pub < 400u);
    lo = tape.ip_pub;
    tape_pads(1u << PD_SKIP);
    host_render(buf, 64);
    tape_pads(0);
    hi = tape.ip_pub;
    check("pad SKIP: the head jumps an eighth ahead", hi > lo + tape.n / 8u - 200u && hi < lo + tape.n / 8u + 400u);

    host_render(buf, 64);
    tape_pads(1u << PD_ST8);
    host_render(buf, 64);
    sip = tape.ip_pub;
    lo = hi = sip;
    cnt = 0;
    for (k = 0; k < 80u; k++) {                  /* a second of 1/8 stutter: the head stays within an eighth of the loop */
        uint32_t d;
        host_render(buf, 512);
        d = (tape.ip_pub + tape.n - sip) % tape.n;
        cnt += d > tape.n / 8u + 400u && d < tape.n / 2u;
    }
    check("pad STUTTER 1/8: the head never leaves the eighth it was pressed in", cnt == 0u);
    tape_pads(0);
    host_render(buf, S);

    tape_nudge(1);
    host_render(buf, 512);
    check("nudge: the tape is faster for a moment", tape.spd_pub > 65536 + 3000);
    host_render(buf, S);
    host_render(buf, S);
    check("nudge: ... and back to the speed", tape.spd_pub == 65536);

    /* the artifacts: windows of 256 samples (one period of F3) well below the loop's level */
    set.g[G_DROP] = 0;
    {
        uint32_t d0 = 0, d1 = 0, d2 = 0, b;
        double ref;
        host_render(buf, S);
        host_render(buf, S);
        ref = rms(buf, S);
        for (b = 0; b < 344u; b++) {
            host_render(buf, 256);
            d0 += rms(buf, 256) < 0.8 * ref;
        }
        set.g[G_DROP] = 100;
        for (b = 0; b < 344u; b++) {
            host_render(buf, 256);
            d1 += rms(buf, 256) < 0.8 * ref;
        }
        set.g[G_DROP] = 0;
        tape_pads(1u << PD_CHEW);
        host_render(buf, S);
        for (b = 0; b < 344u; b++) {
            host_render(buf, 256);
            d2 += rms(buf, 256) < 0.8 * ref;
        }
        tape_pads(0);
                check("DROPOUT 0, WEAR 0: no dropouts (the loop's own seam: a window or two)", d0 <= 3u);
        check("DROPOUT 100: the level dips now and then (some windows, not most)", d1 > d0 + 15u && d1 < 250u);
        check("pad CHEW: dips often too", d2 > d0 + 15u && d2 < 344u);
    }
    app_audio_stop();
    host_render(buf, 64);
}


/* SHOEGAZE's chorus and reverb (shoe.c), alone: a block loop over a mono input */
static void shoe_run(const int32_t *in, uint32_t frames, int32_t *o)
{
    int32_t blk[2u * CTL];
    uint32_t i, j;
    for (i = 0; i + CTL <= frames; i += CTL) {
        for (j = 0; j < CTL; j++) {
            blk[2u * j] = in[i + j];
            blk[2u * j + 1u] = 0;
        }
        shoe_process(blk, CTL);
        for (j = 0; j < CTL; j++)
            o[2u * (i + j)] = blk[2u * j];
    }
}

static void shoe_audio(void)
{
    uint32_t i, same;
    double e0, e1, e2, e3;
    reset_state();
    host_boot();
    shoe_clear();
    set.g[G_MODE] = MODE_SHOE;
    fx.bypass = 0;
    memset(dl_in, 0, (DN + 64u) * sizeof dl_in[0]);
    sine_in(0, 44100, 440.0, 8000.0);
    cur.p[P_CHORUS] = 0;
    cur.p[P_REVERB] = 0;
    shoe_run(dl_in, 44100, dl_out);
    for (i = same = 0; i < 44096u; i++)
        same += dl_out[2u * i] == dl_in[i];
    check("SHOEGAZE with CHORUS 0 and REVERB 0: the sound untouched", same == 44096u);

    cur.p[P_CHORUS] = 100;
    shoe_run(dl_in, 44100, dl_out);
    e0 = rms(dl_out + 2u * 22050u, 22050);
    for (i = same = 0; i < 44096u; i++)
        same += dl_out[2u * i] == dl_in[i];
    check("CHORUS 100: the sound is changed, a comb moving across the tone (RMS 0.5 .. 1.0 of the input's)",
          same < 4000u && e0 / (8000.0 / 1.4142) > 0.5 && e0 / (8000.0 / 1.4142) < 1.0);
    {                                            /* the taps swing: the level beats over time (a comb moving across the tone) */
        double lo = 1e9, hi = 0, r;
        uint32_t w;
        for (w = 0; w < 40u; w++) {
            r = rms(dl_out + 2u * (4400u + w * 1000u), 1000);
            lo = r < lo ? r : lo;
            hi = r > hi ? r : hi;
        }
        check("CHORUS 100: the output's level moves (the delay swings)", hi / lo > 1.05);
    }

    shoe_clear();
    cur.p[P_CHORUS] = 0;
    cur.p[P_REVERB] = 100;
    memset(dl_in, 0, (DN + 64u) * sizeof dl_in[0]);
    for (i = 0; i < 64u; i++)
        dl_in[i] = 8000;
    shoe_run(dl_in, 396000, dl_out);
    e0 = rms(dl_out + 2u * 13000u, 4000);        /* 0.3 s on */
    e1 = rms(dl_out + 2u * 44100u, 4000);        /* 1 s */
    e2 = rms(dl_out + 2u * 132300u, 4000);       /* 3 s */
    check("REVERB 100: a tail after a click (0.3 s: audible; 1 s: still; falling)", e0 > 200.0 && e1 > 30.0 && e1 < e0);
    check("REVERB 100: the tail falls over seconds (3 s below 1 s)", e2 < e1);
    check("REVERB 100: the click itself is passed (dry first)", dl_out[2u * 10u] >= 7000);
    {
        uint32_t quiet_at = 0;
        for (i = 0; i < 396000u; i++)
            if (dl_out[2u * i] != 0)
                quiet_at = i;
        check("REVERB: the tail ends in exact silence and the state is cleared (before 8 s)",
              quiet_at < 8u * 44100u && !sh.live && peak(dl_out + 2u * 380000u, 16000) == 0);
    }
    cur.p[P_REVERB] = 20;
    shoe_clear();
    shoe_run(dl_in, 88200, dl_out);
    e3 = rms(dl_out + 2u * 44100u, 4000);
    check("REVERB 20: a shorter, quieter tail than 100 (1 s: lower)", e3 < e1);
    {
        static const double F[5] = {55.0, 82.4, 110.0, 164.8, 440.0};
        int32_t worst = 0;
        uint32_t f;
        cur.p[P_REVERB] = 100;
        for (f = 0; f < 5u; f++) {
            shoe_clear();
            memset(dl_in, 0, (DN + 64u) * sizeof dl_in[0]);
            sine_in(0, 88200, F[f], 11000.0);
            shoe_run(dl_in, 132300, dl_out);
            worst = peak(dl_out, 132300) > worst ? peak(dl_out, 132300) : worst;
        }
        printf("    reverb worst peak %d\n", worst);
        check("REVERB 100 with a full-level drone (11 000, five pitches): bounded (< 32 000)", worst < 32000);
    }
    set.g[G_MODE] = MODE_HARSH;
    shoe_run(dl_in, 4096, dl_out);
    check("back to HARSH: the stages' state is dropped, the sound passes", !sh.on && !sh.live);
    cur.p[P_REVERB] = 0;

    /* a tail survives the voices' silence (the whole chain: a note, released) */
    reset_state();
    host_boot();
    set.g[G_MODE] = MODE_SHOE;
    cur.p[P_REVERB] = 100;
    cur.p[P_CHORUS] = 0;
    cur.p[P_ATTACK] = 0;
    cur.p[P_RELEASE] = 0;
    tap_key(0);
    host_render(buf, BLOCKS(FS));
    tap_key(0);
    host_render(buf, BLOCKS(FS) / 2);
    host_render(buf, BLOCKS(FS) / 2);
    check("SHOEGAZE through the whole chain: the reverb rings after the note is off (0.5 s later)",
          rms(buf, BLOCKS(FS) / 2) > 50.0);
    app_audio_stop();
    host_render(buf, 64);
    set.g[G_MODE] = MODE_HARSH;
    cur.p[P_REVERB] = 0;
    shoe_clear();
}


/* SCL: the scales and the chords on the keys */
static int has_note(uint32_t note)
{
    uint32_t i;
    for (i = 0; i < NV; i++)
        if (snd.held[i] == note)
            return 1;
    return 0;
}

static void scl_audio(void)
{
    uint32_t k, ok;
    static const uint8_t MIN_A[8] = {57, 59, 60, 62, 64, 65, 67, 69};   /* A natural minor from A3 */
    reset_state();
    host_boot();
    set.g[G_OCT] = 0;
    check("CHROMATIC (the default): key 0 is F3 = 53, key 13 is 66, whatever the root",
          key_note(0) == 53 && key_note(13) == 66 && (set.g[G_ROOT] = 4, key_note(0) == 53));
    set.g[G_ROOT] = 9;
    set.g[G_SCALE] = 1;
    for (k = ok = 0; k < 8u; k++)
        ok += key_note(k) == MIN_A[k];
    check("MINOR with the root A: the keys climb A B C D E F G A", ok == 8u);
    check("... and every key maps back (note_key inverts key_note), the notes off the scale map to nothing",
          note_key(key_note(0)) == 0 && note_key(key_note(12)) == 12 && note_key(key_note(26)) == 26 &&
          note_key(58) == -1);
    set.g[G_SCALE] = 5;
    check("PENT MINOR on A: A C D E G A (five keys an octave)",
          key_note(0) == 57 && key_note(1) == 60 && key_note(2) == 62 && key_note(3) == 64 && key_note(4) == 67 &&
          key_note(5) == 69);
    set.g[G_SCALE] = 0;
    ok = 1;
    for (k = 0; k < 27u; k++)
        ok &= note_key(key_note(k)) == (int32_t)k;
    check("every scale: note_key inverts key_note on all 27 keys", ok);
    for (k = 1; k < 7u; k++) {
        uint32_t m;
        set.g[G_SCALE] = (int16_t)k;
        for (m = 0; m < 27u; m++)
            ok &= note_key(key_note(m)) == (int32_t)m && key_note(m) < 128;
    }
    check("... (SCALE 1..6 too)", ok);
    set.g[G_SCALE] = 1;
    set.g[G_ROOT] = 9;

    set.g[G_CHORD] = 1;                          /* POWER: the fifth over the key */
    tap_key(0);
    check("POWER: one key latches A3 and E4", n_held() == 2 && has_note(57) && has_note(64));
    tap_key(0);
    host_render(buf, BLOCKS(FS));
    check("POWER: the key again releases both", n_held() == 0);
    set.g[G_CHORD] = 6;                          /* DOOM: 0, 7, 12, 19 */
    set.g[G_LOW] = 1;
    tap_key(0);
    check("DOOM with LOW -1: root, the root an octave down, the fifth and the octave (four voices at most)",
          n_held() == 4 && has_note(57) && has_note(45) && has_note(64) && has_note(69));
    tap_key(0);
    host_render(buf, BLOCKS(FS));
    check("... and one key releases all four", n_held() == 0);
    set.g[G_CHORD] = 0;
    set.g[G_LOW] = 2;
    tap_key(0);
    check("SINGLE with LOW -2: the root, and the root two octaves under it", n_held() == 2 && has_note(57) && has_note(33));
    tap_key(0);
    host_render(buf, BLOCKS(FS));
    set.g[G_LOW] = 0;
    set.g[G_CHORD] = 3;                          /* SUS4 */
    tap_key(0);
    tap_key(2);                                  /* a second chord: a note of it (E4's fifth) ... */
    check("SUS4 twice: four voices are never exceeded", n_held() <= NV);
    app_audio_stop();
    host_render(buf, 64);

    /* pads: the keys are not notes while SEQ is held */
    reset_state();
    host_boot();
    snd.pad_mode = 1;
    tap_key(0);
    check("pad mode: a key makes no note", n_held() == 0);
    fm1_in.notes |= 1u;
    host_render(buf, CTL);
    snd.pad_mode = 0;
    host_render(buf, CTL);
    fm1_in.notes = 0;
    host_render(buf, CTL);
    check("... and a key held while SEQ is let go makes none either", n_held() == 0);
    tap_key(0);
    check("pad mode off: the key plays again", n_held() == 1);
    app_audio_stop();
    host_render(buf, 64);
}

int main(void)
{
    if (system("mkdir -p build/shots"))
        return 1;
    screen();
    controls();
    persistence();
    sound();
    engine();
    fx_chain();
    breath();
    fm();
    factory();
    scope_shot();
    tape_audio();
    tape_pads_audio();
    shoe_audio();
    scl_audio();
    delay_audio();
    puts(failed ? "APP TEST FAILED" : "app test passed");
    return failed ? 1 : 0;
}
