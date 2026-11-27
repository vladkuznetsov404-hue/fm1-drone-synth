/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vlad Kuznetsov */
/* The surface (main loop): the controls table of docs/SPEC.md, the pages, the SAVE dialog, the LEDs.
 *
 * Every page is the same frame: a header (page, engine, octave), the sound line (slot, name, * when edited; a
 * short message in its place for 1.5 s), four rows for KNOB 1..4 (label, value, a bar), and the held notes and the
 * tape. HOME puts the held notes large and the tape with its bar above four compact macro rows. A value that does
 * not reach the sound yet carries a dim SOON next to its label (ui_live); a milestone that wires one removes it.
 *
 * Lazy drawing: each area has a signature of what it shows (area_sig) and is drawn only when that changes; a page
 * change or app_redraw clears the screen and draws everything. */
enum { PG_HOME, PG_EDIT, PG_FX, PG_ENV, PG_LFO, PG_TAPE, PG_TAPE2, PG_GLO, NPG };   /* SELECT's order */
enum { A_HEAD, A_SOUND, A_R0, A_R1, A_R2, A_R3, A_NOTES, A_TAPE, NA };

#define C_BLANK (-1)             /* a row with nothing on it */
#define C_VER (-2)               /* GLO's version line */
#define C_G 100                  /* cell codes: 0..NP-1 a sound value, C_G + G_x a setting */
#define TOAST_MS 1500u

static const int16_t CELLS[NPG][4] = {
    [PG_HOME] = {P_MASSA, P_BEATS, P_DARK, P_BREATH},
    [PG_EDIT] = {P_LAYERS, P_SUB, P_SUBOCT, P_CURVE},          /* SAW; FM: CELLS_FM */
    [PG_FX] = {P_DRIVE, P_TONE, P_CAB, P_LEVEL},
    [PG_ENV] = {P_ATTACK, P_RELEASE, C_BLANK, C_BLANK},
    [PG_LFO] = {P_RATE, P_TOPITCH, P_TOCUT, P_TOLEVEL},
    [PG_TAPE] = {C_G + G_SPEED, C_G + G_DIR, C_G + G_DECAY, C_G + G_WEAR},
    [PG_TAPE2] = {C_G + G_BRAKE, C_BLANK, C_BLANK, C_BLANK},
    [PG_GLO] = {C_G + G_CH, C_G + G_TRS, C_G + G_VOL, C_VER},
};
static const int16_t CELLS_FM[4] = {P_RATIO, P_FB, P_INDEX, P_SUB};
static const char *const PG_TITLE[NPG] = {"HOME", "EDIT", "FX", "ENV", "LFO", "TAPE", "TAPE 2", "GLO"};

static struct {
    uint8_t page, dlg, dlg_slot, full;
    int8_t saved;                /* the last SAVE: 1 saved, -1 failed, 0 none yet */
    uint32_t last_buttons, rec_down, toast_until;
    uint8_t rec_held, rec_used;
    const char *toast;
    uint32_t drawn[NA], dlg_drawn;
} ui;

static int ui_live(int32_t code)                 /* does this value reach the sound yet (no SOON tag) */
{
    return code == P_ATTACK || code == P_RELEASE || code == C_G + G_CH || code == C_G + G_TRS ||
           code == C_G + G_VOL || code == P_BEATS || code == P_LAYERS || code == P_SUB || code == P_SUBOCT ||
           code == P_CURVE ||                   /* (milestone 2: the SAW engine) */
           code == P_MASSA || code == P_DARK || code == P_DRIVE || code == P_TONE || code == P_CAB ||
           code == P_LEVEL ||                   /* (milestone 3: the fuzz, the cabinet, DARK) */
           code == P_BREATH || code == P_RATE || code == P_TOPITCH || code == P_TOCUT ||
           code == P_TOLEVEL ||                 /* (milestone 4: BREATH) */
           code == C_G + G_SPEED || code == C_G + G_DIR || code == C_G + G_DECAY || code == C_G + G_WEAR ||
           code == C_G + G_BRAKE ||             /* (milestones 6, 7: the cassette) */
           code == P_RATIO || code == P_FB || code == P_INDEX;   /* (milestone 5: FM) */
}

static int32_t cell(uint32_t page, uint32_t i)
{
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

static uint32_t area_sig(uint32_t a)
{
    uint32_t h = h_add(2166136261u, a << 8 | ui.page), i;
    switch (a) {
    case A_HEAD:
        h = h_add(h, cur.engine);
        h = h_add(h, (uint32_t)set.g[G_OCT]);
        break;
    case A_SOUND:
        if (toast_on())
            return h_add(h, (uint32_t)(uintptr_t)ui.toast);
        for (i = 0; i < NAME_LEN; i++)
            h = h_add(h, (uint8_t)cur.name[i]);
        h = h_add(h, (uint32_t)set.g[G_SLOT]);
        h = h_add(h, (uint32_t)snd_dirty());
        h = h_add(h, snd.last_midi);
        break;
    case A_R0: case A_R1: case A_R2: case A_R3: {
        int32_t c = cell(ui.page, a - A_R0);
        h = h_add(h, (uint32_t)c);
        h = h_add(h, (uint32_t)cell_get(c));
        break;
    }
    case A_NOTES:
        h = h_add(h, held_sig());
        if (ui.page != PG_HOME)
            h = h_add(h, tape.st);
        break;
    case A_TAPE:
        h = h_add(h, tape.st);
        h = h_add(h, tape_pct());
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
    str_cpy(b, PG_TITLE[ui.page], sizeof b);
    if (ui.page == PG_EDIT)
        str_cat(b, cur.engine == ENG_FM ? " FM" : " SAW", sizeof b);
    cv_text(8, 4, &AF_M, b, T_ACCENT);
    str_cpy(b, "OCT ", sizeof b);
    par_text(b + 4, sizeof b - 4u, set.g[G_OCT], F_OCT);
    x = cv_text_r(232, 6, &AF_S, b, T_MID, T_BG);
    (void)x;
    cv_text_r(232 - text_w(&AF_S, b) - 12, 6, &AF_S, cur.engine == ENG_FM ? "FM" : "SAW", T_THEME, T_BG);
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

static void draw_row(uint32_t y, uint32_t h, int32_t code, uint32_t knob)
{
    char lab[4], val[24];
    const par_t *p = cell_par(code);
    int32_t vy = h > 30u ? 6 : 2, x;
    cv_begin(240, h, T_BG);
    lab[0] = (char)('1' + knob);
    lab[1] = 0;
    cv_text(8, vy + 1, &AF_S, lab, T_DIM);
    if (code == C_VER) {
        cv_text(22, vy + 1, &AF_S, "VERSION", T_MID);
        cv_text_r(232, vy, &AF_M, APP_VERSION, T_TEXT, T_BG);
    } else if (!p) {
        cv_text(22, vy + 1, &AF_S, "-", T_DIM);
    } else {
        int32_t v = cell_get(code), w = 224 * (v - p->lo) / (p->hi - p->lo);
        x = cv_text(22, vy + 1, &AF_S, p->label, T_MID);
        (void)x;
        if (!ui_live(code))
            cv_text(22 + text_w(&AF_S, p->label) + 6, vy + 1, &AF_S, "SOON", T_DIM);
        par_text(val, sizeof val, v, p->fmt);
        cv_text_r(232, vy, &AF_M, val, T_TEXT, T_BG);
        cv_rect(8, (int32_t)h - 9, 224, 4, T_TINT);
        if (w > 0)
            cv_rect(8, (int32_t)h - 9, w, 4, T_THEME);
    }
    cv_blit(0, y);
}

static void held_notes(uint8_t *n)               /* the latched notes, in voice order, 0 = none */
{
    uint32_t i;
    for (i = 0; i < NV; i++)
        n[i] = snd.held[i];
}

static void draw_notes_big(uint32_t y)           /* HOME: the held notes, large */
{
    uint8_t n[NV];
    char b[8];
    uint32_t i;
    held_notes(n);
    cv_begin(240, 50, T_BG);
    for (i = 0; i < NV; i++) {
        const aafont_t *f = &AF_L;
        if (!n[i]) {
            cv_text_in((int32_t)i * 60, 12, 60, &AF_L, "-", T_DIM, T_BG);
            continue;
        }
        note_name(b, n[i]);
        if (text_w(f, b) > 58)
            f = &AF_M;
        cv_text_in((int32_t)i * 60, f == &AF_L ? 12 : 18, 60, f, b, T_THEME, T_BG);
    }
    cv_blit(0, y);
}

static uint16_t tape_col(void)
{
    return tape.st == TP_REC || tape.st == TP_DUB ? T_REC : tape.st == TP_PLAY ? T_THEME : T_MID;
}

#define TB_X 122                 /* HOME's tape line: TAPE, the state (EMPTY the widest, from x 40), the bar */
#define TB_W 64

static void draw_tape(uint32_t y)                /* HOME: the cassette's state and position */
{
    uint32_t w = tape_pct() * TB_W / 100u;
    cv_begin(240, 24, T_BG);
    cv_text(8, 5, &AF_S, "TAPE", T_MID);
    cv_text(40, 4, &AF_M, TP_NAME[tape.st], tape_col());
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
    cv_text(8, 7, &AF_S, b, T_TEXT);
    cv_text_r(232, 7, &AF_S, TP_NAME[tape.st], tape_col(), T_BG);
    cv_text_r(232 - text_w(&AF_S, TP_NAME[tape.st]) - 6, 7, &AF_S, "TAPE", T_MID, T_BG);
    cv_blit(0, y);
}

static void draw_area(uint32_t a)
{
    int home = ui.page == PG_HOME;
    switch (a) {
    case A_HEAD: draw_head(0); break;
    case A_SOUND: draw_sound(24); break;
    case A_R0: case A_R1: case A_R2: case A_R3:
        if (home)
            draw_row(128u + (a - A_R0) * 28u, 28, cell(ui.page, a - A_R0), a - A_R0);
        else
            draw_row(52u + (a - A_R0) * 40u, 40, cell(ui.page, a - A_R0), a - A_R0);
        break;
    case A_NOTES:
        if (home)
            draw_notes_big(50);
        else
            draw_foot(214);
        break;
    case A_TAPE:
        if (home)
            draw_tape(102);
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
    static const uint8_t PAGE_BTN[NPG] = {B_HOME, B_EDIT, B_FX, B_ENV, B_LFO, B_SEQ, B_SEQ, B_GLO};
    uint32_t i, on[NB] = {0}, keys = 0;
    int32_t base = (int32_t)NOTE_KEY0 + 12 * set.g[G_OCT];
    on[PAGE_BTN[ui.page]] = !ui.dlg;
    on[B_SAVE] = ui.dlg;
    on[B_REC] = tape.st == TP_REC || tape.st == TP_DUB;
    on[B_PLAY] = tape.st == TP_PLAY || tape.st == TP_DUB;
    on[B_OCTDN] = set.g[G_OCT] < 0;
    on[B_OCTUP] = set.g[G_OCT] > 0;
    on[B_SCL] = panel_held(B_SCL);
    for (i = 0; i < NB; i++)
        fm1_led_key(panel.btn[i], (int)on[i]);
    for (i = 0; i < NV; i++) {
        int32_t k = (int32_t)snd.held[i] - base;
        if (snd.held[i] && k >= 0 && k < 27)
            keys |= 1u << k;
    }
    for (i = 0; i < 27u; i++)
        fm1_led_key(14u + i, (int)((keys >> i) & 1u));
}

static void ui_frame(void)
{
    uint32_t b = fm1_in.buttons, pr = b & ~ui.last_buttons, i;
    int32_t sel = panel_enc(EN_SELECT), alg = panel_enc(EN_ALGO), pre = panel_enc(EN_PRESET), k[4];
#define PRESSED(x) (pr & panel_mask(x))
    ui.last_buttons = b;
    for (i = 0; i < 4u; i++)
        k[i] = panel_enc(EN_K1 + i);
    tape_frame();

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
    if (PRESSED(B_LFO)) ui_page(PG_LFO);
    if (PRESSED(B_GLO)) ui_page(PG_GLO);
    if (PRESSED(B_SEQ)) ui_page(ui.page == PG_TAPE ? PG_TAPE2 : PG_TAPE);
    if (sel)
        ui_page((uint32_t)(((int32_t)ui.page + sel % NPG + NPG) % NPG));
    if (PRESSED(B_OCTDN)) set.g[G_OCT] = clamp16(set.g[G_OCT] - 1, GP[G_OCT].lo, GP[G_OCT].hi);
    if (PRESSED(B_OCTUP)) set.g[G_OCT] = clamp16(set.g[G_OCT] + 1, GP[G_OCT].lo, GP[G_OCT].hi);
    if (PRESSED(B_SCL)) {
        snd.silence_req = 1;
        toast("SILENCE");
    }
    if (PRESSED(B_ARP)) toast("ARP: SOON");
    if (alg)
        cur.engine = alg > 0 ? ENG_FM : ENG_SAW;
    if (pre) {
        int16_t s = clamp16(set.g[G_SLOT] + pre, 0, NSND - 1);
        set.g[G_SLOT] = s;
        cur = bank.s[s];                         /* the held notes keep sounding with the new sound */
    }
    for (i = 0; i < 4u; i++)
        if (k[i])
            cell_add(cell(ui.page, i), k[i]);

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
    ui_leds();
    ui_draw();
}
