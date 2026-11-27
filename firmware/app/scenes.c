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

/* ---- the tape echo ---- */
#define SCD_Y 62                 /* the tape's upper run */
#define SCD_Y2 108               /* the lower run */
#define SCD_LX 40                /* the reels' centres */
#define SCD_RX 210
#define SCD_R 23
#define SCD_REC 72               /* the record head */

static void sc_delay(uint32_t y)
{
    int32_t d = dl.d_pub ? dl.d_pub : dly_ms10(set.g[G_DT]) * DL_Q, ms10 = d / DL_Q, cells = d >> 16, v = dl.v_pub;
    int32_t sp = 8 + (ms10 - 600) * 36 / 8900, x, k, off, i, dt, tick, spoke, heads = set.g[G_DHEADS];
    uint32_t w = dl.w_pub;
    char b[24];
    sp = sp < 6 ? 6 : sp > 46 ? 46 : sp;
    dt = (int32_t)(fm1_ms - sc.t);
    dt = dt < 0 || dt > 200 || !sc.t ? 70 : dt;
    sc.t = fm1_ms;
    sc.ph += (uint32_t)(sp * 256 * dt * 10 / ms10);   /* the tape runs D in ms10 / 10 ms past a head spacing */
    tick = (int32_t)(sc.ph >> 8) % 12;
    spoke = (int32_t)(sc.ph >> 8) / 4 % 36;
    cv_begin(240, 124, T_BG);
    /* the reels and the tape round them */
    for (k = 0; k < 2; k++) {
        int32_t cx = k ? SCD_RX : SCD_LX, s;
        sc_ring(cx, 85, SCD_R, T_DIM);
        sc_disc(cx, 85, 7, T_RAISE);
        for (s = 0; s < 3; s++) {
            int32_t a = (spoke + 12 * s) % 36;
            cv_line_t(cx + SPOKE_V[a][0] * 7 / 32, 85 + SPOKE_V[a][1] * 7 / 32, cx + SPOKE_V[a][0] * 19 / 32,
                      85 + SPOKE_V[a][1] * 19 / 32, T_DIM, 2);
        }
        sc_disc(cx, 85, 2, T_MID);
    }
    cv_rect(SCD_LX, SCD_Y, SCD_RX - SCD_LX, 3, T_DIM);                /* the runs */
    cv_rect(SCD_LX, SCD_Y2, SCD_RX - SCD_LX, 3, T_DIM);
    for (x = SCD_LX + tick; x < SCD_RX; x += 12)                      /* the upper run moves right, the lower left */
        cv_rect(x, SCD_Y, 1, 3, T_MID);
    for (x = SCD_RX - tick; x > SCD_LX; x -= 12)
        cv_rect(x, SCD_Y2, 1, 3, T_MID);
    /* the sound on the tape: the newest cell at the record head, the oldest at head 3 */
    for (x = SCD_REC; x <= SCD_REC + 3 * sp; x++) {
        int32_t span = cells / sp, m = 0, amp;
        off = (x - SCD_REC) * cells / sp;
        span = span > 16 ? 16 : span < 1 ? 1 : span;
        for (i = 0; i < span; i++) {
            int32_t c = dl_cells[(w - 1u - (uint32_t)(off + i)) & DL_MASK];
            c = c < 0 ? -c : c;
            m = c > m ? c : m;
        }
        amp = sc_bits(m) - 5;                    /* (a log scale from 32 up: 1 px, 12 000: 9 px) */
        if (amp > 0)
            cv_rect(x, SCD_Y + 1 - amp, 1, 2 * amp, T_THEME);
    }
    /* the heads: down on the tape when they play, lifted when they do not */
    for (k = 0; k < 3; k++) {
        int32_t hx = SCD_REC + (k + 1) * sp, on = (heads >> k) & 1, hy = on ? 40 : 31;
        char n[2] = {(char)('1' + k), 0};
        cv_rrect(hx - 6, hy, 12, 18, 2, on ? T_THEME : T_RAISE, T_BG);
        cv_text_in(hx - 6, hy + 3, 12, &AF_S, n, on ? T_INK : T_DIM, on ? T_THEME : T_RAISE);
        if (on)
            cv_rect(hx - 1, hy + 18, 3, 3, T_THEME);
    }
    cv_rrect(SCD_REC - 7, 38, 14, 20, 2, T_REC, T_BG);                /* the record head */
    cv_text_in(SCD_REC - 7, 42, 14, &AF_S, "R", T_INK, T_REC);
    /* the numbers: the delay, and the bend while the motor runs */
    cv_text(8, 0, &AF_S, "TAPE ECHO", T_MID);
    num(b, ms10 / 10);
    str_cat(b, " MS", sizeof b);
    cv_text_r(232, 0, &AF_M, b, T_TEXT, T_BG);
    if (v > 5243 || v < -5243) {                 /* head 1's pitch: D shortening by 0.04 cell a sample = +16 % */
        int32_t pct = -v / 5243;
        b[0] = pct > 0 ? '+' : '-';
        num(b + 1, pct < 0 ? -pct : pct);
        str_cat(b, " %", sizeof b);
        cv_text_r(232, 17, &AF_S, b, T_ACCENT, T_BG);
        cv_text(8, 17, &AF_S, pct > 0 ? "SPINNING UP" : "SLOWING", T_ACCENT);
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

static void sc_amp(uint32_t y)
{
    int32_t i, k, lev = sc_level(), cab = cur.p[P_CAB], drive = cur.p[P_DRIVE], y0, y1, shake = lev >> 6;
    static const char *const LAB[4] = {"DRIVE", "TONE", "CAB", "LEVEL"};
    int32_t at[4];
    at[0] = drive * 32 / 100;
    at[1] = cur.p[P_TONE] * 32 / 100;
    at[2] = cab * 16;
    at[3] = cur.p[P_LEVEL] * 32 / 100;
    cv_begin(240, 124, T_BG);
    cv_rrect(14, 2, 212, 120, 6, T_SURF, T_BG);                       /* the cabinet */
    cv_rrect(16, 4, 208, 34, 4, T_BG, T_SURF);                        /* the panel */
    for (k = 0; k < 4; k++) {
        int32_t cx = 66 + k * 44;
        cv_text_in(cx - 22, 5, 44, &AF_S, LAB[k], T_MID, T_BG);
        sc_dial(cx, 27, 8, at[k], T_DIM, T_RAISE, k == 0 && drive > 60 ? T_ACCENT : T_THEME);
    }
    sc_disc(32, 21, 5, drive > 5 ? T_REC : T_DIM);                    /* the pilot lamp */
    /* what comes out, as it is: squarer as the fuzz is turned up */
    cv_rrect(18, 40, 204, 24, 3, T_BG, T_SURF);
    if (scope.count) {
        int32_t m = 64;
        for (i = 0; i < SCOPE_N; i++) {
            int32_t a = scope.y[i] < 0 ? -scope.y[i] : scope.y[i];
            m = a > m ? a : m;
        }
        y0 = 52 - scope.y[0] * 10 / m;
        for (i = 1; i < SCOPE_N; i++) {
            y1 = 52 - scope.y[i] * 10 / m;
            cv_line(18 + (int32_t)(i - 1u) * 204 / (SCOPE_N - 1), y0, 18 + (int32_t)i * 204 / (SCOPE_N - 1), y1, T_THEME);
            y0 = y1;
        }
    } else
        cv_rect(20, 52, 200, 1, T_DIM);
    /* the grille cloth, then four speakers */
    if (cab)
        for (y1 = 66; y1 < 119; y1 += cab == 2 ? 2 : 3)
            cv_rect(18, y1, 204, 1, cab == 2 ? T_LINE : T_TINT);
    for (k = 0; k < 4; k++) {
        int32_t cx = 43 + k * 51 + (lev > 40 ? (k & 1 ? shake : -shake) : 0), cy = 93;
        if (cab) {
            sc_disc(cx, cy, 22, T_BG);
            sc_ring(cx, cy, 22, T_MID);
            sc_disc(cx, cy, 16, cab == 2 ? T_TINT : T_RAISE);
            sc_ring(cx, cy, 16, T_DIM);
        } else {                                                      /* no cabinet: the bare cones */
            sc_ring(cx, cy, 22, T_TEXT);
            sc_ring(cx, cy, 15, T_MID);
        }
        sc_disc(cx, cy, 4 + lev / 64, drive > 60 ? T_ACCENT : T_TEXT);   /* the dust cap: out with the level */
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
