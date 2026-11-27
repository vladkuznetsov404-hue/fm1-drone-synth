/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vlad Kuznetsov */
/* The surface (main loop): the controls table of docs/SPEC.md, the pages, the SAVE dialog, the LEDs.
 *
 * Every page but HOME is a picture that moves with the sound and the knobs (scenes.c, scenes2.c, scenes3.c) with the
 * four knobs as a strip of dials under it (the amp has its own knobs in the picture), a header (page, body, engine,
 * octave; a short message in its place for 1.5 s) and a foot (the held notes, the tape): EDIT, FX, ENV, LFO and LFO 2
 * (the LFO button again), SCL, ANOMALY 1..4, SLICE 1..2, GLO. No page lists values with a bar under each. HOME is the
 * body (the black hole or the quasar) over the sound line, the tape's line and the four macro dials.
 * SCL: a tap opens the scale page, held 0.8 s it silences every note. SEQ held: the keys are the cassette's pads.
 * A value that does not reach the sound yet carries a dim SOON next to its label (ui_live): none now.
 *
 * Lazy drawing: each area has a signature of what it shows (area_sig) and is drawn only when that changes; a page
 * change or app_redraw clears the screen and draws everything. */
enum { PG_HOME, PG_EDIT, PG_FX, PG_ENV, PG_LFO, PG_SCL, PG_DELAY, PG_DELAY2, PG_TAPE, PG_TAPE2, PG_GLO,
       PG_LFO2, NPG };
enum { A_HEAD, A_SOUND, A_NOTES, A_TAPE, A_SCENE, A_DIALS, NA };

#define C_BLANK (-1)             /* a row with nothing on it */
#define C_VER (-2)               /* GLO's version line */
#define C_G 100                  /* cell codes: 0..NP-1 a sound value, C_G + G_x a setting */
#define TOAST_MS 1500u

static const int16_t CELLS[NPG][4] = {
    [PG_HOME] = {P_MASSA, P_BEATS, P_DARK, P_BREATH},
    [PG_EDIT] = {P_LAYERS, P_SUB, P_SUBOCT, P_CURVE},          /* SAW; FM: CELLS_FM */
    [PG_FX] = {P_DRIVE, P_TONE, P_CAB, P_LEVEL},
    [PG_ENV] = {P_ATTACK, P_RELEASE, C_G + G_SLIDE, C_BLANK},
    [PG_LFO] = {P_RATE, P_LSHAPE, P_LDEST, P_LAMT},
    [PG_SCL] = {C_G + G_SCALE, C_G + G_ROOT, C_G + G_CHORD, C_G + G_LOW},
    [PG_DELAY] = {C_G + G_DT, C_G + G_DFB, C_G + G_DMIX, C_G + G_AMASS},
    [PG_DELAY2] = {C_G + G_DWOB, C_G + G_DDULL, C_G + G_DGLIDE, C_G + G_DDRV},
    [PG_TAPE] = {C_G + G_SPEED, C_G + G_PITCH, C_G + G_DECAY, C_G + G_WEAR},
    [PG_TAPE2] = {C_G + G_BRAKE, C_G + G_DIR, C_G + G_LOCK, C_G + G_DROP},
    [PG_GLO] = {C_G + G_CH, C_G + G_TRS, C_G + G_VOL, C_BLANK},
    [PG_LFO2] = {P_BREATH, P_TOPITCH, P_TOCUT, P_TOLEVEL},
};
static const int16_t CELLS_FM[4] = {P_RATIO, P_FB, P_INDEX, P_SUB};
static const int16_t CELLS_SHOE_HOME[4] = {P_MASSA, P_SPIN, P_REVERB, P_DARK};   /* the quasar's four: mass, spin, bloom, dark */
static const int16_t CELLS_SHOE_FX[4] = {P_DRIVE, P_CHORUS, P_REVERB, P_LEVEL};   /* SHOEGAZE's FX */
static const char *const PG_TITLE[NPG] = {"HOME", "EDIT", "FX", "ENV", "LFO", "SCL", "ANOMALY", "ANOMALY 2",
                                          "SLICE", "SLICE 2", "GLO", "LFO 2"};
/* what a page says in its foot while no note is latched (the foot has room) */
static const char *const PG_HINT[NPG] = {0, "ALGO: SAW / FM", "KNOBS = THE AMP", "SWELL: SLOW IS HEAVY", "LFO AGAIN: BREATH",
                                         "ENV > SLIDE: CHORD GLIDE", "ARP: NEXT PAGE",
                                         "ARP: BACK TO THE FIRST", "ALGO: NUDGE THE TIME", "SEQ TAP: OTHER PAGE",
                                         "SELECT: THE BODY", "LFO: BACK TO THE WAVE"};
static const uint8_t HEAD_ORDER[7] = {1, 2, 4, 3, 5, 6, 7};   /* HEADS as the knob steps through it: one head, two, three */

static void app_palette(void);

static int page_scene(uint32_t p) { return p != PG_HOME; }    /* a picture: every page but HOME (which has its own layout) */
static int page_strip(uint32_t p) { return page_scene(p) && !(p == PG_FX && set.g[G_MODE] != MODE_SHOE); }   /* ... and a strip of dials (not FX of the hole: the amp's own) */
static int page_delay(uint32_t p) { return p >= PG_DELAY && p <= PG_DELAY2; }

static struct {
    uint8_t page, dlg, dlg_slot, full;
    int8_t saved;                /* the last SAVE: 1 saved, -1 failed, 0 none yet */
    uint32_t last_buttons, rec_down, toast_until;
    uint8_t rec_held, rec_used;
    uint32_t scl_down, seq_pads_prev;   /* SCL's press time; the pads at the last frame */
    uint8_t scl_used, seq_down, seq_used;
    const char *toast;
    uint32_t scope_t;            /* when the last picture of the drone was shown (fm1_ms) */
    uint32_t drawn[NA], dlg_drawn;
} ui;

static int ui_live(int32_t code) { (void)code; return 1; }   /* every value reaches the sound now */

static int32_t cell(uint32_t page, uint32_t i)
{
    if (page == PG_FX && set.g[G_MODE] == MODE_SHOE)
        return CELLS_SHOE_FX[i];
    if (page == PG_HOME && set.g[G_MODE] == MODE_SHOE)
        return CELLS_SHOE_HOME[i];
    return page == PG_EDIT && cur.engine == ENG_FM ? CELLS_FM[i] : CELLS[page][i];
}

static const par_t *cell_par(int32_t code)
{
    return code >= C_G ? &GP[code - C_G] : code >= 0 ? &SP[code] : 0;
}

static int32_t cell_get(int32_t code) { return code >= C_G ? set.g[code - C_G] : code >= 0 ? cur.p[code] : 0; }

static void cell_add(int32_t code, int32_t steps)
{
    const par_t *p = cell_par(code);
    int16_t *v = code >= C_G ? &set.g[code - C_G] : &cur.p[code];
    if (code == C_G + G_DHEADS) {                /* the masks in the order one, two, three heads */
        int32_t k = 0;
        while (k < 6 && HEAD_ORDER[k] != *v)
            k++;
        *v = HEAD_ORDER[clamp16(k + steps, 0, 6)];
        return;
    }
    if (p)
        *v = clamp16(*v + steps, p->lo, p->hi);
}

static void toast(const char *s)
{
    ui.toast = s;
    ui.toast_until = fm1_ms + TOAST_MS;
}

static int toast_on(void) { return ui.toast && (int32_t)(ui.toast_until - fm1_ms) > 0; }

static uint32_t h_add(uint32_t h, uint32_t v) { return (h ^ v) * 16777619u; }

static uint32_t held_sig(void)
{
    uint32_t i, h = 0;
    for (i = 0; i < NV; i++)
        h = h * 131u + snd.held[i];
    return h;
}

static uint32_t scene_sig(uint32_t h)            /* what the page's picture shows */
{
    uint32_t i;
    switch (ui.page) {
    case PG_HOME:                                /* the hole breathes: a new picture every ~80 ms */
        h = h_add(h, fm1_ms / 80u);
        h = h_add(h, scope.count);
        for (i = 0; i < 4u; i++)
            h = h_add(h, (uint32_t)cur.p[cell(PG_HOME, i)]);
        h = h_add(h, held_sig());
        h = h_add(h, (uint32_t)set.g[G_MODE]);
        break;
    case PG_FX:
        h = h_add(h, fm1_ms / 90u);
        h = h_add(h, scope.count);
        for (i = 0; i < 4u; i++)
            h = h_add(h, (uint32_t)cur.p[cell(PG_FX, i)]);
        h = h_add(h, (uint32_t)set.g[G_MODE]);
        if (set.g[G_MODE] == MODE_SHOE)          /* (the corona turns with SPIN) */
            h = h_add(h, (uint32_t)cur.p[P_SPIN] << 8 | spin_pub >> 8);
        break;
    case PG_ENV:                                 /* a dot runs the path: a new picture every ~160 ms */
        h = h_add(h, fm1_ms / 160u);
        h = h_add(h, (uint32_t)(cur.p[P_ATTACK] << 16 | cur.p[P_RELEASE]));
        h = h_add(h, (uint32_t)set.g[G_SLIDE]);
        break;
    case PG_LFO:
        h = h_add(h, fm1_ms / 80u);
        for (i = P_RATE; i <= P_TOLEVEL; i++)
            h = h_add(h, (uint32_t)cur.p[i]);
        for (i = P_LSHAPE; i <= P_LAMT; i++)
            h = h_add(h, (uint32_t)cur.p[i]);
        break;
    case PG_LFO2:
        h = h_add(h, fm1_ms / 120u);
        for (i = P_BREATH; i <= P_BREATH; i++)
            h = h_add(h, (uint32_t)cur.p[i]);
        for (i = P_RATE; i <= P_TOLEVEL; i++)
            h = h_add(h, (uint32_t)cur.p[i]);
        break;
    case PG_EDIT:                                /* the layers drift: a new picture every ~90 ms */
        h = h_add(h, fm1_ms / 90u);
        for (i = P_LAYERS; i <= P_INDEX; i++)
            h = h_add(h, (uint32_t)cur.p[i]);
        h = h_add(h, cur.engine);
        h = h_add(h, (uint32_t)cur.p[P_MASSA]);
        break;
    case PG_GLO:                                 /* static */
        h = h_add(h, (uint32_t)(set.g[G_CH] << 16 | set.g[G_TRS] << 8));
        h = h_add(h, (uint32_t)set.g[G_VOL]);
        break;
    case PG_SCL:                                 /* static: only when a value or a note changes */
        for (i = 0; i < 4u; i++)
            h = h_add(h, (uint32_t)set.g[CELLS[PG_SCL][i] - C_G]);
        h = h_add(h, held_sig());
        h = h_add(h, (uint32_t)set.g[G_OCT]);
        break;
    case PG_DELAY: case PG_DELAY2:               /* the tape moves: a new picture every ~70 ms */
        h = h_add(h, fm1_ms / 70u);
        for (i = G_DT; i <= G_DDULL; i++)
            h = h_add(h, (uint32_t)set.g[i]);
        for (i = G_DGLIDE; i <= G_DDRV; i++)
            h = h_add(h, (uint32_t)set.g[i]);
        h = h_add(h, (uint32_t)set.g[G_AMASS]);
        break;
    case PG_TAPE:
        h = h_add(h, fm1_ms / 70u);
        h = h_add(h, tape.st);
        h = h_add(h, tape.pads);
        for (i = 0; i < 4u; i++)
            h = h_add(h, (uint32_t)set.g[CELLS[PG_TAPE][i] - C_G]);
        break;
    case PG_TAPE2:
        h = h_add(h, tape.pads);
        h = h_add(h, dl.freeze);
        break;
    }
    return h;
}

static uint32_t area_sig(uint32_t a)
{
    uint32_t h = h_add(2166136261u, a << 8 | ui.page), i;
    switch (a) {
    case A_HEAD:
        h = h_add(h, cur.engine);
        h = h_add(h, (uint32_t)set.g[G_OCT]);
        h = h_add(h, (uint32_t)set.g[G_MODE]);
        if (page_scene(ui.page)) {               /* (no sound line here: its message and * go in the header) */
            h = h_add(h, (uint32_t)snd_dirty());
            if (toast_on())
                h = h_add(h, (uint32_t)(uintptr_t)ui.toast);
        }
        break;
    case A_SOUND:
        if (page_scene(ui.page))
            return h;
        if (toast_on())
            return h_add(h, (uint32_t)(uintptr_t)ui.toast);
        for (i = 0; i < NAME_LEN; i++)
            h = h_add(h, (uint8_t)cur.name[i]);
        h = h_add(h, (uint32_t)set.g[G_SLOT]);
        h = h_add(h, (uint32_t)snd_dirty());
        h = h_add(h, snd.last_midi);
        break;
    case A_NOTES:
        if (ui.page == PG_HOME)                  /* (the notes are in the picture) */
            return h;
        h = h_add(h, held_sig());
        h = h_add(h, tape.st);
        break;
    case A_TAPE:
        if (ui.page != PG_HOME)
            return h;
        h = h_add(h, tape.st);
        h = h_add(h, tape_pct());
        break;
    case A_SCENE:
        if (page_scene(ui.page) || ui.page == PG_HOME)
            h = scene_sig(h);
        break;
    case A_DIALS:
        if (ui.page == PG_FX && set.g[G_MODE] != MODE_SHOE)
            return scene_sig(h);                 /* the speakers (or the scope) under the amp's knobs */
        if (page_strip(ui.page) || ui.page == PG_HOME)
            for (i = 0; i < 4u; i++) {
                int32_t c = cell(ui.page, i);
                h = h_add(h, (uint32_t)c);
                h = h_add(h, (uint32_t)cell_get(c));
            }
        break;
    }
    return h;
}

/* ------------------------------------------------------------- drawing --- */
static void draw_head(uint32_t y)
{
    char b[16];
    int32_t x;
    cv_begin(240, 24, T_BG);
    if (page_scene(ui.page) && toast_on()) {
        cv_text_in(0, 4, 240, &AF_M, ui.toast, T_ACCENT, T_BG);
        cv_rect(0, 23, 240, 1, T_LINE);
        cv_blit(0, y);
        return;
    }
    str_cpy(b, PG_TITLE[ui.page], sizeof b);
    if (ui.page == PG_EDIT)
        str_cat(b, cur.engine == ENG_FM ? " FM" : " SAW", sizeof b);
    if (page_scene(ui.page) && snd_dirty())
        str_cat(b, " *", sizeof b);
    if (ui.page == PG_GLO) {                     /* (the version has no row of its own any more) */
        str_cat(b, " ", sizeof b);
        str_cat(b, APP_VERSION, sizeof b);
    }
    x = cv_text(8, 4, &AF_M, b, T_ACCENT);
    cv_text(8 + text_w(&AF_M, b) + 8, 6, &AF_S, MODE_TAG[set.g[G_MODE]], T_MID);
    str_cpy(b, "OCT ", sizeof b);
    par_text(b + 4, sizeof b - 4u, set.g[G_OCT], F_OCT);
    x = cv_text_r(232, 6, &AF_S, b, T_MID, T_BG);
    (void)x;
    cv_text_r(232 - text_w(&AF_S, b) - 10, 6, &AF_S, cur.engine == ENG_FM ? "FM" : "SAW", T_THEME, T_BG);
    cv_rect(0, 23, 240, 1, T_LINE);
    cv_blit(0, y);
}

static void draw_sound(uint32_t y)
{
    char b[24];
    int32_t x;
    cv_begin(240, 24, T_BG);
    if (toast_on()) {
        cv_text_in(0, 4, 240, &AF_M, ui.toast, T_ACCENT, T_BG);
    } else {
        num(b, set.g[G_SLOT] + 1);
        if (set.g[G_SLOT] < 9) {
            b[1] = b[0];
            b[0] = '0';
            b[2] = 0;
        }
        cv_text(8, 4, &AF_M, b, T_THEME);
        memcpy(b, cur.name, NAME_LEN);
        b[NAME_LEN] = 0;
        for (x = NAME_LEN - 1; x >= 0 && b[x] == ' '; x--)
            b[x] = 0;
        x = cv_text(36, 4, &AF_M, b, T_TEXT);
        if (snd_dirty())
            cv_text(36 + text_w(&AF_M, b) + 4, 4, &AF_M, "*", T_ACCENT);
        if (snd.last_midi) {
            str_cpy(b, "MIDI ", sizeof b);
            note_name(b + 5, snd.last_midi);
            cv_text_r(232, 6, &AF_S, b, T_MID, T_BG);
        }
    }
    cv_blit(0, y);
}

static void held_notes(uint8_t *n)               /* the latched notes, in voice order, 0 = none */
{
    uint32_t i;
    for (i = 0; i < NV; i++)
        n[i] = snd.held[i];
}

#define TB_X 124                 /* HOME's tape line: SLICE, the state (EMPTY the widest, from x 46), the bar */
#define TB_W 62

static void draw_tape(uint32_t y)                /* HOME: the cassette's state and position */
{
    uint32_t w = tape_pct() * TB_W / 100u;
    cv_begin(240, 24, T_BG);
    cv_text(8, 5, &AF_S, "SLICE", T_MID);
    cv_text(46, 4, &AF_M, TP_NAME[tape.st], tape_col());
    cv_rect(TB_X, 10, TB_W, 4, T_TINT);
    if (w)
        cv_rect(TB_X, 10, (int32_t)w, 4, tape_col());
    cv_rect(0, 23, 240, 1, T_LINE);
    cv_blit(0, y);
}

static void draw_foot(uint32_t y)                /* the other pages: held notes and the tape's state */
{
    uint8_t n[NV];
    char b[32], t[8];
    uint32_t i;
    held_notes(n);
    b[0] = 0;
    for (i = 0; i < NV; i++) {
        if (i)
            str_cat(b, " ", sizeof b);
        if (n[i]) {
            note_name(t, n[i]);
            str_cat(b, t, sizeof b);
        } else
            str_cat(b, "-", sizeof b);
    }
    cv_begin(240, 26, T_BG);
    cv_rect(0, 0, 240, 1, T_LINE);
    if (!(n[0] | n[1] | n[2] | n[3]) && PG_HINT[ui.page])
        cv_text(8, 7, &AF_S, PG_HINT[ui.page], T_DIM);
    else
        cv_text(8, 7, &AF_S, b, T_TEXT);
    cv_text_r(232, 7, &AF_S, TP_NAME[tape.st], tape_col(), T_BG);
    cv_text_r(232 - text_w(&AF_S, TP_NAME[tape.st]) - 6, 7, &AF_S, "SLICE", T_MID, T_BG);
    cv_blit(0, y);
}

static int32_t dial_at(int32_t code)             /* the knob's pointer: 0 .. 32 across its range */
{
    const par_t *p = cell_par(code);
    int32_t v = cell_get(code), k = 0;
    if (!p)
        return 0;
    if (code == C_G + G_DHEADS) {
        while (k < 6 && HEAD_ORDER[k] != v)
            k++;
        return k * 32 / 6;
    }
    return (v - p->lo) * 32 / (p->hi - p->lo);
}

static const char *dial_label(int32_t code)      /* the strip's cell is 60 px: FUZZ DRIVE and FUZZ TONE are DRIVE and TONE */
{
    return code == C_G + G_CH ? "MIDI CH" : code == C_G + G_TRS ? "TRS IN" : code == P_TOCUT ? "CUTOFF" : code == P_TOPITCH ? "PITCH" :
           code == P_TOLEVEL ? "LEVEL" : code == P_CURVE ? "CURVE" : code == P_SUB ? "SUB" : code == P_SUBOCT ? "SUB OCT" :
           code == P_DRIVE ? "DRIVE" : code == P_TONE ? "TONE" : code == C_G + G_DDRV ? "DRIVE" :
           code == C_G + G_DGLIDE ? "GLIDE" :
           code == C_G + G_DIR ? "DIR" : code == C_G + G_LOW ? "LOW" :
           cell_par(code)->label;
}

static void draw_dials(uint32_t y)               /* a scene page's four knobs (and HOME's macros): label, dial, value */
{
    char val[24];
    uint32_t i;
    cv_begin(240, 58, T_BG);
    for (i = 0; i < 4u; i++) {
        int32_t c = cell(ui.page, i);
        const par_t *p = cell_par(c);
        if (!p)
            continue;
        par_text(val, sizeof val, cell_get(c), p->fmt);
        sc_dials_cell((int32_t)i * 60, dial_label(c), val, dial_at(c), 1);
    }
    cv_blit(0, y);
}

static void draw_area(uint32_t a)
{
    int home = ui.page == PG_HOME;
    switch (a) {
    case A_HEAD: draw_head(0); break;
    case A_SOUND:
        if (!page_scene(ui.page))
            draw_sound(24);
        break;
    case A_SCENE:
        switch (ui.page) {
        case PG_HOME: sc_hole(48); break;
        case PG_FX:
            if (set.g[G_MODE] == MODE_SHOE)
                sc_corona(26);
            else
                sc_amp(26);
            break;
        case PG_ENV: sc_env(26); break;
        case PG_LFO: sc_lfo(26); break;
        case PG_LFO2: sc_lfo2(26); break;
        case PG_EDIT: sc_edit(26); break;
        case PG_GLO: sc_glo(26); break;
        case PG_SCL: sc_scl(26); break;
        case PG_DELAY: case PG_DELAY2: sc_delay(26); break;
        case PG_TAPE: sc_cassette(26); break;
        case PG_TAPE2: sc_pads(26); break;
        }
        break;
    case A_DIALS:
        if (ui.page == PG_FX && set.g[G_MODE] != MODE_SHOE) {
            sc_amp_low(150);
        } else if (home)
            draw_dials(178);
        else if (page_strip(ui.page))
            draw_dials(152);
        break;
    case A_NOTES:
        if (!home)
            draw_foot(214);
        break;
    case A_TAPE:
        if (home)
            draw_tape(152);
        break;
    }
}

static uint32_t dlg_sig(void) { return h_add(0x5A5Eu, ui.dlg_slot); }

static void draw_dialog(void)
{
    char b[32], n[NAME_LEN + 1];
    int32_t i;
    lcd_fill(0, 0, 240, 240, T_BG);
    cv_begin(240, 40, T_BG);
    cv_text_in(0, 4, 240, &AF_L, "SAVE", T_ACCENT, T_BG);
    cv_blit(0, 50);
    cv_begin(240, 44, T_BG);
    str_cpy(b, "INTO SLOT ", sizeof b);
    num(b + 10, ui.dlg_slot + 1);
    cv_text_in(0, 2, 240, &AF_M, b, T_TEXT, T_BG);
    memcpy(n, bank.s[ui.dlg_slot].name, NAME_LEN);
    n[NAME_LEN] = 0;
    for (i = NAME_LEN - 1; i >= 0 && n[i] == ' '; i--)
        n[i] = 0;
    str_cpy(b, "NOW: ", sizeof b);
    str_cat(b, n, sizeof b);
    cv_text_in(0, 24, 240, &AF_S, b, T_MID, T_BG);
    cv_blit(0, 100);
    cv_begin(240, 44, T_BG);
    cv_text_in(0, 2, 240, &AF_S, "SAVE / OCT+  YES     OCT-  NO", T_TEXT, T_BG);
    cv_text_in(0, 24, 240, &AF_S, "PRESETS: ANOTHER SLOT", T_MID, T_BG);
    cv_blit(0, 160);
}

static void ui_draw(void)
{
    uint32_t a, s;
    if (app_redraw) {
        app_redraw = 0;
        ui.full = 1;
    }
    if (ui.dlg) {
        s = dlg_sig();
        if (ui.full || s != ui.dlg_drawn) {
            ui.full = 0;
            ui.dlg_drawn = s;
            draw_dialog();
        }
        return;
    }
    if (ui.full) {
        ui.full = 0;
        lcd_fill(0, 0, 240, 240, T_BG);
        for (a = 0; a < NA; a++)
            ui.drawn[a] = ~area_sig(a);
    }
    for (a = 0; a < NA; a++) {
        s = area_sig(a);
        if (s != ui.drawn[a]) {
            ui.drawn[a] = s;
            draw_area(a);
        }
    }
}

/* --------------------------------------------------------------- input --- */
static void ui_page(uint32_t p)
{
    if (p != ui.page) {
        ui.page = (uint8_t)p;
        ui.full = 1;
    }
}

static void ui_leds(void)
{
    static const uint8_t PAGE_BTN[NPG] = {B_HOME, B_EDIT, B_FX, B_ENV, B_LFO, B_SCL, B_ARP, B_ARP, B_SEQ, B_SEQ, B_GLO, B_LFO};
    uint32_t i, on[NB] = {0}, keys = 0;
    on[PAGE_BTN[ui.page]] = !ui.dlg;
    on[B_SAVE] = ui.dlg;
    on[B_REC] = tape.st == TP_REC || tape.st == TP_DUB;
    on[B_PLAY] = tape.st == TP_PLAY || tape.st == TP_DUB;
    on[B_OCTDN] = set.g[G_OCT] < 0;
    on[B_OCTUP] = set.g[G_OCT] > 0;
    on[B_SEQ] = on[B_SEQ] || ui.seq_down;       /* (held: the keys are pads) */
    for (i = 0; i < NB; i++)
        fm1_led_key(panel.btn[i], (int)on[i]);
    for (i = 0; i < NV; i++) {
        int32_t k = snd.held[i] ? note_key(snd.held[i]) : -1;
        if (k >= 0)
            keys |= 1u << k;
    }
    if (ui.seq_down)                             /* pads: the keys that are on show, like the pad legend */
        keys = (tape.pads & 0x3FFFu) | (dl.freeze ? 1u << PD_ECHO : 0u);
    for (i = 0; i < 27u; i++)
        fm1_led_key(14u + i, (int)((keys >> i) & 1u));
}

static void ui_frame(void)
{
    uint32_t b = fm1_in.buttons, pr = b & ~ui.last_buttons, rl = ~b & ui.last_buttons, i;
    uint8_t mode0 = (uint8_t)set.g[G_MODE];
    int32_t sel = panel_enc(EN_SELECT), alg = panel_enc(EN_ALGO), pre = panel_enc(EN_PRESET), k[4];
#define PRESSED(x) (pr & panel_mask(x))
#define RELEASED(x) (rl & panel_mask(x))
    ui.last_buttons = b;
    for (i = 0; i < 4u; i++)
        k[i] = panel_enc(EN_K1 + i);
    dly_mass_apply();
    tape_frame();
    if (scope.ready) {                           /* a picture taken: show it, ask for the next in ~100 ms */
        scope.count++;
        scope.ready = 0;
        ui.scope_t = fm1_ms;
    } else if ((ui.page == PG_HOME || ui.page == PG_FX) && !scope.req && fm1_ms - ui.scope_t >= 100u)
        scope.req = 1;
    if (ui.seq_down) {                           /* SEQ held: the keys are the cassette's pads, the first 15 */
        uint32_t pads = fm1_in.notes & 0x7FFFu, rise = pads & ~ui.seq_pads_prev;
        if (rise)
            ui.seq_used = 1;
        if (rise & (1u << PD_ECHO))              /* the echo's freeze: a latch */
            dl.freeze = (uint8_t)!dl.freeze;
        ui.seq_pads_prev = pads;
        tape_pads(pads);
    }

    if (ui.dlg) {                                /* the SAVE dialog takes SAVE / OCT+, OCT-, PRESETS */
        if (pre)
            ui.dlg_slot = (uint8_t)clamp16(ui.dlg_slot + pre, 0, NSND - 1);
        if (PRESSED(B_SAVE) || PRESSED(B_OCTUP)) {
            ui.saved = data_save(ui.dlg_slot) ? -1 : 1;
            toast(ui.saved > 0 ? "SAVED" : "SAVE FAILED");
            ui.dlg = 0;
            ui.full = 1;
        } else if (PRESSED(B_OCTDN)) {
            ui.dlg = 0;
            ui.full = 1;
        }
        ui_leds();
        ui_draw();
        return;
    }

    if (PRESSED(B_HOME)) ui_page(PG_HOME);
    if (PRESSED(B_EDIT)) ui_page(PG_EDIT);
    if (PRESSED(B_FX)) ui_page(PG_FX);
    if (PRESSED(B_ENV)) ui_page(PG_ENV);
    if (PRESSED(B_LFO)) ui_page(ui.page == PG_LFO ? PG_LFO2 : PG_LFO);   /* again: BREATH's three */
    if (PRESSED(B_GLO)) ui_page(PG_GLO);
    if (PRESSED(B_SEQ)) {                        /* held: pads; let go with no pad played: the cassette's pages */
        ui.seq_down = 1;
        ui.seq_used = 0;
        ui.seq_pads_prev = fm1_in.notes & 0x7FFFu;
        snd.pad_mode = 1;
    }
    if (RELEASED(B_SEQ) && ui.seq_down) {
        ui.seq_down = 0;
        snd.pad_mode = 0;
        tape_pads(0);
        if (!ui.seq_used)
            ui_page(ui.page == PG_TAPE ? PG_TAPE2 : PG_TAPE);
    }
    if (PRESSED(B_ARP)) ui_page(page_delay(ui.page) ? (ui.page == PG_DELAY2 ? PG_DELAY : ui.page + 1u) : PG_DELAY);
    if (sel)                                     /* SELECT turns the body (the engine): every page has its own button */
        set.g[G_MODE] = (int16_t)(((int32_t)set.g[G_MODE] + sel % (int32_t)NMODE + (int32_t)NMODE) % (int32_t)NMODE);
    if (PRESSED(B_OCTDN)) set.g[G_OCT] = clamp16(set.g[G_OCT] - 1, GP[G_OCT].lo, GP[G_OCT].hi);
    if (PRESSED(B_OCTUP)) set.g[G_OCT] = clamp16(set.g[G_OCT] + 1, GP[G_OCT].lo, GP[G_OCT].hi);
    if (PRESSED(B_SCL)) {                        /* a tap: the scale page; held 0.8 s: every note off */
        ui.scl_down = fm1_ms;
        ui.scl_used = 0;
    }
    if (panel_held(B_SCL) && !ui.scl_used && fm1_ms - ui.scl_down >= 800u) {
        ui.scl_used = 1;
        snd.silence_req = 1;
        toast("SILENCE");
    }
    if (RELEASED(B_SCL) && !ui.scl_used)
        ui_page(PG_SCL);
    if (alg) {
        if (ui.page == PG_TAPE || ui.page == PG_TAPE2) {   /* the tape's jog: a nudge of the speed (a push on the reel) */
            int32_t n = alg > 3 ? 3 : alg < -3 ? -3 : alg;
            for (; n > 0; n--)
                tape_nudge(1);
            for (; n < 0; n++)
                tape_nudge(0);
        } else
            cur.engine = alg > 0 ? ENG_FM : ENG_SAW;
    }
    if (pre) {
        int16_t s = clamp16(set.g[G_SLOT] + pre, 0, NSND - 1);
        set.g[G_SLOT] = s;
        cur = bank.s[s];                         /* the held notes keep sounding with the new sound */
    }
    for (i = 0; i < 4u; i++)
        if (k[i])
            cell_add(cell(ui.page, i), k[i]);
    if ((uint8_t)set.g[G_MODE] != mode0) {       /* another body: its own sounds, colours and picture; each keeps its edits */
        workm[mode0] = cur;
        workslot[mode0] = (uint8_t)set.g[G_SLOT];
        cur = workslot[set.g[G_MODE]] == (uint8_t)set.g[G_SLOT] ? workm[set.g[G_MODE]] : bank.s[set.g[G_SLOT]];
        app_palette();
        toast(MODE_NAME[set.g[G_MODE]]);
        ui.full = 1;
    }

    if (PRESSED(B_REC)) {                        /* acts at once; held 1 s it erases (whatever the press did) */
        tape_rec();
        ui.rec_down = fm1_ms;
        ui.rec_used = 0;
    }
    if (panel_held(B_REC) && !ui.rec_used && fm1_ms - ui.rec_down >= REC_HOLD_MS) {
        tape_erase();
        ui.rec_used = 1;
        toast("TAPE ERASED");
    }
    if (PRESSED(B_PLAY)) tape_play();
    if (PRESSED(B_SAVE)) {
        ui.dlg = 1;
        ui.dlg_slot = (uint8_t)set.g[G_SLOT];
        ui.full = 1;
    }
#undef PRESSED
#undef RELEASED
    ui_leds();
    ui_draw();
}
