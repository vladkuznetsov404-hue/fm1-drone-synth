/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vlad Kuznetsov */
/* The pictures (main loop): what a page shows in place of rows of numbers. Each is drawn into the 240 x 124 canvas and
 * redrawn every ~70 ms (the tape echo) or when the drone's picture changes (the amp), so the screen moves with the
 * sound and with the knobs:
 *   the tape echo   a loop of tape round two reels, the record head and three playback heads on it. TIME moves the heads
 *                   along the tape (their spacing is the delay), the tape runs faster the shorter it is, and what is on
 *                   the tape is the sound itself: the line's cells, read where the picture puts them.
 *   the amp         a combo with its four knobs, the scope of what comes out (the fuzz squares it as DRIVE goes up) and
 *                   four speakers, the cones moving with the level; CAB changes what is drawn (open cones, a grille,
 *                   a darker grille).
 * A dial (ring, pointer) is the one knob picture the pages' strip of four uses. Integer maths only (the tables below). */

static const int8_t DIAL_V[33][2] = {   /* a pointer, -135 .. +135 degrees in 33 steps: (dx, dy) x 32, 0 deg = up */
    {-23, 23}, {-26, 19}, {-28, 15}, {-30, 11}, {-31, 6}, {-32, 2}, {-32, -3}, {-31, -8}, {-30, -12}, {-27, -16},
    {-25, -20}, {-21, -24}, {-18, -27}, {-14, -29}, {-9, -31}, {-5, -32}, {0, -32}, {5, -32}, {9, -31}, {14, -29},
    {18, -27}, {21, -24}, {25, -20}, {27, -16}, {30, -12}, {31, -8}, {32, -3}, {32, 2}, {31, 6}, {30, 11}, {28, 15},
    {26, 19}, {23, 23}};
static const int8_t SPOKE_V[36][2] = {  /* a full turn in 10 degree steps: (dx, dy) x 32 */
    {32, 0}, {32, 6}, {30, 11}, {28, 16}, {25, 21}, {21, 25}, {16, 28}, {11, 30}, {6, 32}, {0, 32}, {-6, 32},
    {-11, 30}, {-16, 28}, {-21, 25}, {-25, 21}, {-28, 16}, {-30, 11}, {-32, 6}, {-32, 0}, {-32, -6}, {-30, -11},
    {-28, -16}, {-25, -21}, {-21, -25}, {-16, -28}, {-11, -30}, {-6, -32}, {0, -32}, {6, -32}, {11, -30}, {16, -28},
    {21, -25}, {25, -21}, {28, -16}, {30, -11}, {32, -6}};

static struct {
    uint32_t t;                  /* when the last picture of the tape was drawn (fm1_ms) */
    uint32_t ph;                 /* the tape's travel, Q8 px */
} sc;

static int32_t isq(int32_t n)    /* floor(sqrt(n)), n >= 0 and small */
{
    int32_t x = 0;
    while ((x + 1) * (x + 1) <= n)
        x++;
    return x;
}

static void sc_disc(int32_t cx, int32_t cy, int32_t r, uint16_t c)
{
    int32_t y;
    for (y = -r; y <= r; y++) {
        int32_t x = isq(r * r - y * y);
        cv_rect(cx - x, cy + y, 2 * x + 1, 1, c);
    }
}

static void sc_ring(int32_t cx, int32_t cy, int32_t r, uint16_t c)   /* a 1 px circle */
{
    int32_t y;
    for (y = -r; y <= r; y++) {
        int32_t xo = isq(r * r - y * y), xi = y > -r + 1 && y < r - 1 ? isq((r - 1) * (r - 1) - y * y) : -1;
        if (xi < 0) {
            cv_rect(cx - xo, cy + y, 2 * xo + 1, 1, c);
        } else {
            cv_rect(cx - xo, cy + y, xo - xi, 1, c);
            cv_rect(cx + xi + 1, cy + y, xo - xi, 1, c);
        }
    }
}

/* a knob: a ring, a raised face and a pointer; at is 0 .. 32 (the pointer's step) */
static void sc_dial(int32_t cx, int32_t cy, int32_t r, int32_t at, uint16_t ring, uint16_t face, uint16_t ptr)
{
    at = at < 0 ? 0 : at > 32 ? 32 : at;
    sc_disc(cx, cy, r, ring);
    sc_disc(cx, cy, r - 1, face);
    cv_line_t(cx, cy, cx + DIAL_V[at][0] * (r - 2) / 32, cy + DIAL_V[at][1] * (r - 2) / 32, ptr, 2);
}

static const int8_t C120[120] = {
    127, 127, 126, 125, 124, 123, 121, 119, 116, 113, 110, 107, 103, 99, 94, 90, 85, 80, 75, 69,
    64, 58, 52, 46, 39, 33, 26, 20, 13, 7, 0, -7, -13, -20, -26, -33, -39, -46, -52, -58,
    -63, -69, -75, -80, -85, -90, -94, -99, -103, -107, -110, -113, -116, -119, -121, -123, -124, -125, -126, -127,
    -127, -127, -126, -125, -124, -123, -121, -119, -116, -113, -110, -107, -103, -99, -94, -90, -85, -80, -75, -69,
    -64, -58, -52, -46, -39, -33, -26, -20, -13, -7, 0, 7, 13, 20, 26, 33, 39, 46, 52, 58,
    64, 69, 75, 80, 85, 90, 94, 99, 103, 107, 110, 113, 116, 119, 121, 123, 124, 125, 126, 127,
};
static int32_t cs(int32_t i) { return C120[(i % 120 + 120) % 120]; }          /* cos of i x 3 degrees, x 127 */
static int32_t sn(int32_t i) { return C120[((i + 90) % 120 + 120) % 120]; }   /* sin */

static int32_t sc_bits(int32_t v)    /* the length of |v| in bits: a level on a log scale */
{
    int32_t n = 0;
    v = v < 0 ? -v : v;
    while (v) {
        n++;
        v >>= 1;
    }
    return n;
}

#define SC_HEAD(n, sp, bq) ((n) * (sp) * (256 + ((n) - 1) * (bq)) / 256)   /* head n's distance from the source: MASS stretches the gaps between the echoes */

/* ---- the anomaly (the tape echo): a source, its echoes as lenses along a ray, the sound on the ray, a bent sheet ---- */
#define SCD_Y 52                 /* the ray */
#define SCD_REC 56               /* the source (the record head) */

static void sc_delay(uint32_t y)
{
    int32_t d = dl.d_pub ? dl.d_pub : dly_ms10(set.g[G_DT]) * DL_Q, ms10 = d / DL_Q, cells = d >> 16, v = dl.v_pub;
    int32_t sp = 16 + (ms10 - 600) * 39 / 8900, x, k, off, i, dt, tms = dly_ms10(set.g[G_DT]), tsp, mass = set.g[G_AMASS], bq = mass * 38 / 100;
    int32_t bend, well, r0, x0 = 0, y0 = 0, row;
    uint32_t w = dl.w_pub;
    char b[24];
    sp = sp < 16 ? 16 : sp > 55 ? 55 : sp;
    tsp = 16 + (tms - 600) * 39 / 8900;
    tsp = tsp < 16 ? 16 : tsp > 55 ? 55 : tsp;
    dt = (int32_t)(fm1_ms - sc.t);
    dt = dt < 0 || dt > 200 || !sc.t ? 70 : dt;
    sc.t = fm1_ms;
    sc.ph += (uint32_t)(sp * 256 * dt * 10 / ms10);   /* the ripples run out faster the shorter the delay is */
    bend = -v / 5243;                                 /* head 1's pitch change in %: the sheet is dragged by it */
    bend = bend > 60 ? 60 : bend < -60 ? -60 : bend;
    well = 6 + set.g[G_DMIX] * 12 / 100 + set.g[G_DFB] * 8 / 100 + mass * 10 / 100;   /* (MASS: the heavier it is, the deeper the well) */
    cv_begin(240, 124, T_BG);
    for (k = 0; k < 3; k++) {                         /* ripples spreading from the source */
        r0 = 12 + (int32_t)((sc.ph >> 7) + (uint32_t)k * 15u) % 45;
        sc_ring(SCD_REC, SCD_Y, r0, r0 < 24 ? T_LINE : r0 < 36 ? T_TINT : T_TINT);
    }
    cv_rect(SCD_REC, SCD_Y, 232 - SCD_REC, 1, T_LINE);                /* the ray */
    for (x = SCD_REC; x <= SCD_REC + SC_HEAD(3, sp, bq) && x < 232; x++) {   /* the sound on it: the newest at the source (a head's far side: MASS stretches the gaps) */
        int32_t span = cells / sp, m = 0, amp, c;
        off = (x - SCD_REC) * cells / sp * 256 / (256 + (x - SCD_REC) * bq / (2 * sp));
        span = span > 16 ? 16 : span < 1 ? 1 : span;
        for (i = 0; i < span; i++) {
            c = dl_cells[(w - 1u - (uint32_t)(off + i)) & DL_MASK];
            c = c < 0 ? -c : c;
            m = c > m ? c : m;
        }
        amp = sc_bits(m) - 8;                         /* (a log scale from 256 up: 1 px, 12 000: 6 px) */
        if (amp > 0)
            cv_rect(x, SCD_Y - amp, 1, 2 * amp + 1, T_THEME);
    }
    for (k = 0; k < 3 && (tsp - sp > 1 || sp - tsp > 1); k++)         /* where TIME is sending the echoes: ghosts */
        sc_ring(SCD_REC + SC_HEAD(k + 1, tsp, bq), SCD_Y, 9, T_DIM);
    for (k = 0; k < 3; k++) {                                         /* the echoes: a lens each, dark when that head is off */
        int32_t hx = SCD_REC + SC_HEAD(k + 1, sp, bq) + sn((int32_t)(fm1_ms / 45u) + k * 30) * set.g[G_DWOB] * (k + 1) / 2540, on = 1;   /* (ORBIT: the lenses sway) */
        hx = hx > 224 ? 224 : hx;
        char n[2] = {(char)('1' + k), 0};
        sc_disc(hx, SCD_Y, 11 - k, on ? T_THEME : T_RAISE);
        sc_disc(hx, SCD_Y, 8 - k, T_BG);
        if (on)
            sc_ring(hx, SCD_Y, 5 - k, T_ACCENT);
        cv_text_in(hx - 8, 28, 16, &AF_S, n, on ? T_TEXT : T_DIM, T_BG);
    }
    for (k = 0; k < (int32_t)DL_NG; k++) {                            /* MASS's grains: bits of the echo torn off, falling into the well */
        int32_t gw = dl.gp_w[k], gx;
        if (!gw)
            continue;
        gx = SCD_REC + (int32_t)dl.gp_gap[k] * sp / (cells > 1 ? cells : 1);
        gx = gx > 226 ? 226 : gx;
        cv_rect(gx, 66 + k * 2, 4 + (gw < 0 ? -gw : gw) / 24, 2, gw < 0 ? T_ACCENT : T_THEME);
    }
    sc_disc(SCD_REC, SCD_Y, 12, T_TINT);                              /* the source */
    sc_disc(SCD_REC, SCD_Y, 7, T_ACCENT);
    cv_rect(SCD_REC, 17, sp, 1, T_ACCENT);                            /* the dimension: the source to echo 1 is TIME */
    cv_rect(SCD_REC, 14, 1, 7, T_ACCENT);
    cv_rect(SCD_REC + sp, 14, 1, 7, T_ACCENT);
    cv_text(SCD_REC + sp + 6, 12, &AF_S, "= TIME", T_ACCENT);
    for (row = 0; row < 4; row++) {                                   /* the sheet: a well under the source, dragged by the bend */
        int32_t base = 78 + row * 9;
        for (x = 8; x <= 232; x += 8) {
            int32_t dx = x - SCD_REC, sag = well * 128 / (128 + dx * dx / 24), rip = cs((x + (int32_t)(sc.ph >> 9)) / 2) * bend / 256;
            int32_t yy = base + sag * (4 - row) / 4 + rip;
            if (x > 8)
                cv_line_t(x0, y0, x, yy, row == 0 ? T_MID : T_DIM, 1);
            x0 = x;
            y0 = yy;
        }
    }
    num(b, ms10 / 10);
    str_cat(b, " MS", sizeof b);
    cv_text_r(232, 0, &AF_M, b, T_TEXT, T_BG);
    if (tms - ms10 > 30 || ms10 - tms > 30) {                         /* the motor is on its way */
        str_cpy(b, "GOING TO ", sizeof b);               /* (the bottom row: below the sheet) */
        num(b + 9, tms / 10);
        str_cat(b, " MS", sizeof b);
        cv_text_r(232, 112, &AF_S, b, T_DIM, T_BG);
    }
    if (v > 5243 || v < -5243) {                 /* head 1's pitch: D shortening by 0.04 cell a sample = +16 % */
        int32_t pct = -v / 5243;                         /* (the heads read at 3 x at most, backwards or forwards) */
        pct = pct > 200 ? 200 : pct < -400 ? -400 : pct;
        str_cpy(b, pct > 0 ? "SPINNING UP " : "SLOWING ", sizeof b);
        i = (int32_t)str_len(b);
        b[i] = pct > 0 ? '+' : '-';
        num(b + i + 1, pct < 0 ? -pct : pct);
        str_cat(b, " %", sizeof b);
        cv_text(8, 112, &AF_S, b, T_ACCENT);
    }
    cv_blit(0, y);
}

/* ---- the amp ---- */
static int32_t sc_level(void)    /* the drone's level 0 .. 256 from the last picture of its wave */
{
    int32_t m = 0, a;
    uint32_t i;
    for (i = 0; i < SCOPE_N; i++) {
        a = scope.y[i] < 0 ? -scope.y[i] : scope.y[i];
        m = a > m ? a : m;
    }
    m = m * 256 / 900;
    return m > 256 ? 256 : m;
}

static int32_t sc_scope_max2(void)               /* the scope's peak (at least 64) */
{
    int32_t m = 64, a;
    uint32_t i;
    for (i = 0; i < SCOPE_N; i++) {
        a = scope.y[i] < 0 ? -scope.y[i] : scope.y[i];
        m = a > m ? a : m;
    }
    return m;
}

/* The combo, in two canvases: the top (the panel with its four knobs and their values, two valves, the scope of what
 * comes out) and the bottom (the four speakers behind the cloth). The knobs are the amp's own: there is no second
 * strip of dials. DRIVE lights the valves and, high up, makes flames and sparks; the level moves the cones and the
 * dust caps; CAB changes the speakers (bare cones, a grille, a darker grille). */
static void sc_amp(uint32_t y)                   /* 240 x 124 */
{
    int32_t i, k, lev = sc_level(), cab = cur.p[P_CAB], drive = cur.p[P_DRIVE], y0, y1, m, t = (int32_t)(fm1_ms / 60u);
    static const char *const LAB[4] = {"DRIVE", "TONE", "CAB", "LEVEL"};
    static const uint8_t CODE[4] = {P_DRIVE, P_TONE, P_CAB, P_LEVEL};
    char val[16];
    int32_t at[4];
    at[0] = drive * 32 / 100;
    at[1] = cur.p[P_TONE] * 32 / 100;
    at[2] = cab * 16;
    at[3] = cur.p[P_LEVEL] * 32 / 100;
    cv_begin(240, 124, T_BG);
    cv_rrect(4, 2, 232, 58, 6, T_SURF, T_BG);                         /* the panel */
    for (k = 0; k < 4; k++) {
        int32_t cx = 33 + k * 58;
        cv_text_in(cx - 28, 3, 56, &AF_S, LAB[k], T_MID, T_SURF);
        sc_dial(cx, 30, 11, at[k], T_DIM, T_RAISE, k == 0 && drive > 60 ? T_ACCENT : T_THEME);
        par_text(val, sizeof val, cur.p[CODE[k]], SP[CODE[k]].fmt);
        cv_text_in(cx - 28, 44, 56, &AF_S, val, T_TEXT, T_SURF);
    }
    sc_disc(11, 9, 3, drive > 5 ? T_REC : T_DIM);                     /* the pilot lamp */
    for (k = 0; k < 2; k++) {                                         /* two valves: glow and flames with DRIVE */
        int32_t x0 = k ? 206 : 8, gh = 4 + drive * 36 / 100 + lev / 16, fl = (t + k * 3) % 4;
        cv_rrect(x0, 66, 26, 54, 8, T_RAISE, T_BG);
        cv_rrect(x0 + 2, 68, 22, 50, 7, T_BG, T_RAISE);
        cv_rect(x0 + 8, 116 - gh, 10, gh, drive > 40 ? T_ACCENT : T_THEME);
        cv_rect(x0 + 6, 78, 1, 36, T_DIM);
        cv_rect(x0 + 19, 78, 1, 36, T_DIM);
        if (drive > 70)                                               /* flames over the valves */
            for (i = 0; i < 3; i++)
                cv_rect(x0 + 6 + i * 6, 64 - (2 + fl + i * (drive - 60) / 12) % 9, 3, 2 + (fl + i) % 4 + (drive - 60) / 10, T_ACCENT);
        if (drive > 50 && lev > 100)                                  /* sparks */
            for (i = 0; i < 3; i++)
                cv_rect(x0 + (t * 7 + i * 11 + k * 5) % 24, 62 + (t * 3 + i * 5) % 7, 1, 1, T_TEXT);
    }
    cv_rrect(40, 64, 160, 58, 3, T_BG, T_SURF);                       /* what comes out, as it is: squarer with DRIVE */
    cv_rect(42, 93, 156, 1, T_LINE);
    if (scope.count) {
        m = sc_scope_max2();
        y0 = 93 - scope.y[0] * 25 / m;
        for (i = 1; i < (int32_t)SCOPE_N; i++) {
            y1 = 93 - scope.y[i] * 25 / m;
            cv_line(42 + (i - 1) * 156 / ((int32_t)SCOPE_N - 1), y0, 42 + i * 156 / ((int32_t)SCOPE_N - 1), y1, T_THEME);
            y0 = y1;
        }
    }
    cv_blit(0, y);
}

static void sc_amp_low(uint32_t y)               /* 240 x 64: the four speakers */
{
    int32_t k, y1, lev = sc_level(), cab = cur.p[P_CAB], drive = cur.p[P_DRIVE], shake = lev >> 6;
    cv_begin(240, 64, T_BG);
    if (cab)                                                          /* the grille cloth */
        for (y1 = 0; y1 < 64; y1 += cab == 2 ? 2 : 3)
            cv_rect(0, y1, 240, 1, cab == 2 ? T_LINE : T_TINT);
    for (k = 0; k < 4; k++) {
        int32_t cx = 30 + k * 60 + (lev > 40 ? (k & 1 ? shake : -shake) : 0), cy = 32, push = lev / 64;
        if (cab) {
            sc_disc(cx, cy, 27, T_BG);
            sc_ring(cx, cy, 27, T_MID);
            sc_disc(cx, cy, 20 + push, cab == 2 ? T_TINT : T_RAISE);
            sc_ring(cx, cy, 20 + push, T_DIM);
        } else {                                                      /* no cabinet: the bare cones */
            sc_ring(cx, cy, 27, T_TEXT);
            sc_ring(cx, cy, 19 + push, T_MID);
            sc_ring(cx, cy, 12 + push, T_DIM);
        }
        sc_disc(cx, cy, 5 + lev / 48, drive > 60 ? T_ACCENT : T_TEXT);   /* the dust cap: out with the level */
    }
    cv_blit(0, y);
}

/* ---- a strip of four dials, a page's four knobs ---- */
static void sc_dials_cell(int32_t x0, const char *lab, const char *val, int32_t at, int32_t ring)
{
    cv_text_in(x0, 1, 60, &AF_S, lab, T_MID, T_BG);
    if (ring)
        sc_dial(x0 + 30, 28, 11, at, T_DIM, T_RAISE, T_THEME);
    cv_text_in(x0, 44, 60, &AF_S, val, T_TEXT, T_BG);
}
