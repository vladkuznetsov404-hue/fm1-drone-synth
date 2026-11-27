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
    memset(&fx, 0, sizeof fx);
    memset(fx.last, 0xFF, sizeof fx.last);       /* (-1: never updated, as at power-on) */
    fx.lmod = 32768;
    memset(&br, 0, sizeof br);
    memset(br.last, 0xFF, sizeof br.last);
    memset(&tape, 0, sizeof tape);
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
                    if (22 + text_w(&AF_S, q->label) + 6 + text_w(&AF_S, "SOON") + 8 + text_w(&AF_M, t) > 232) {
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
    check("FX, KNOB 1: fuzz drive +5, the sound marked edited", cur.p[P_DRIVE] == SP[P_DRIVE].def + 5 && snd_dirty());
    turn(EN_K3, 9);
    check("FX, KNOB 3: cabinet clamped at its last value", cur.p[P_CAB] == 2);
    press(B_GLO);
    turn(EN_K1, 3);
    check("GLO, KNOB 1: MIDI channel 3", set.g[G_CH] == 3);
    turn(EN_K1, -3);

    press(B_ARP);
    check("ARP: says SOON", toast_on() && str_eq(ui.toast, "ARP: SOON"));

    /* the cassette's transport */
    press(B_PLAY);
    check("PLAY on an empty tape: nothing", tape.st == TP_EMPTY);
    press(B_REC);
    check("REC: recording, its LED lit", tape.st == TP_REC && host_led[panel.btn[B_REC]]);
    for (erases = 0; erases < 125u; erases++)    /* 2 s */
        host_frame();
    press(B_REC);
    check("REC again: the loop closed (~2 s) and playing", tape.st == TP_PLAY && tape.len > 1900u && tape.len < 2200u);
    press(B_REC);
    check("REC while playing: overdub, REC and PLAY lit", tape.st == TP_DUB && host_led[panel.btn[B_REC]] &&
          host_led[panel.btn[B_PLAY]]);
    host_png("build/shots/dub.png");
    press(B_REC);
    check("REC again: overdub off", tape.st == TP_PLAY);
    press(B_PLAY);
    check("PLAY: stopped, the position kept", tape.st == TP_STOP && tape.pos < tape.len);
    press(B_PLAY);
    check("PLAY again: playing", tape.st == TP_PLAY);
    host_press(B_REC, 1);
    for (erases = 0; erases < 70u; erases++)     /* 1.1 s */
        host_frame();
    host_press(B_REC, 0);
    host_frame();
    check("REC held 1 s: the tape erased", tape.st == TP_EMPTY && tape.len == 0u && str_eq(ui.toast, "TAPE ERASED"));
    press(B_REC);
    for (erases = 0; erases < 700u; erases++)    /* 11 s */
        host_frame();
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
    turn(EN_PRESET, 4);                          /* (loads sound 5: the drive back at its default) */
    turn(EN_K1, 7);
    press(B_SAVE);
    press(B_SAVE);
    power_cycle();
    check("power-on: sound 5 with its drive, octave -1", set.g[G_SLOT] == 4 && cur.p[P_DRIVE] == SP[P_DRIVE].def + 7 &&
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
              bank.s[1].p[P_TONE] == 11 && bank.s[1].p[P_RELEASE] == SP[P_RELEASE].def && set.g[G_VOL] == 44 &&
              set.g[G_OCT] == -2 && !memcmp(bank.s[2].name, "INIT 03     ", NAME_LEN));
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
    puts(failed ? "APP TEST FAILED" : "app test passed");
    return failed ? 1 : 0;
}
