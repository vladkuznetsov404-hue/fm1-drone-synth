/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vlad Kuznetsov */
/* The cassette, milestones 6 and 7 of docs/SPEC.md: one loop of the instrument's own output (after the chain, before
 * the master volume), 8-bit at 22 050 Hz (every second output sample, two averaged: a crude anti-alias that suits
 * a cassette), 10 s in the pool. Played back mixed with the live sound; overdub adds the live sound onto the tape
 * while the old material fades by DECAY each pass. SPEED (1/2 .. 1, varispeed), DIRECTION, the BRAKE (PLAY: the
 * tape slows to a stop, its pitch falling), WEAR (wow ~0.5 Hz, flutter ~8 Hz, hiss, and highs lost on every pass).
 * The tape lives in RAM only.
 *
 * The audio ISR owns the tape: its clock is the sample count, so the loop is sample-exact at any frame rate. The
 * main loop only posts the buttons (tape_rec, tape_play, tape_erase: a small ring, taken at the start of a block)
 * and shows what the ISR publishes (tape.st, and the length and position, turned into ms by tape_frame).
 *   REC     EMPTY: record (the first pass sets the length; at 10 s it closes by itself)
 *           REC: close the loop and play   PLAY: overdub on   DUB: overdub off   STOP: play with overdub
 *   REC 1 s held: erase (EMPTY)
 *   PLAY    REC: close the loop and play   PLAY / DUB: the brake, then STOP   STOP: spin up and play   EMPTY: nothing
 *
 * Levels: a cell is the output (Q15) x 5 / 512, so the chain's ~12 000 peak uses ~117 of the 127 steps; playback
 * undoes it (x 0.4 after the interpolation's x 256). Writes saturate softly above 96. Per output sample the read head
 * moves speed / 2 cells (Q16 fraction) and reads two cells linearly interpolated; a cell the head leaves while
 * overdubbing gets the average of the live samples that fell on it (1..8 of them; below 1/4 speed, while braking,
 * nothing is written). */
#define TAPE_RATE 22050u
#define TAPE_N 220500u           /* 10 s of cells, in the pool */
#define TAPE_MAX_MS 10000u
#define TAPE_MIN 2205u           /* a loop is at least 0.1 s */
#define REC_HOLD_MS 1000u
#define SPIN_BLOCKS 413          /* PLAY from STOP: up to speed in ~0.3 s */
#define WOW_INC 1558278u         /* ~0.5 Hz per block: 2^32 * 0.5 * CTL / FS */
#define FLUT_INC 24932000u       /* ~8 Hz */

enum { TP_EMPTY, TP_REC, TP_PLAY, TP_DUB, TP_STOP };
static const char *const TP_NAME[5] = {"EMPTY", "REC", "PLAY", "DUB", "STOP"};
enum { TE_REC = 1, TE_PLAY, TE_ERASE };

static int8_t tape_cells[TAPE_N] __attribute__((section(".pool")));

/* 1..8 live samples on one cell -> their average: x RECIP >> 15 */
static const int16_t TAPE_RECIP[9] = {0, 32767, 16384, 10923, 8192, 6554, 5461, 4681, 4096};

static struct {
    volatile uint8_t st;         /* ISR -> screen, LEDs: TP_* */
    volatile uint8_t ev[4];      /* main loop -> ISR: the buttons, a ring */
    volatile uint8_t ev_w, ev_r;
    volatile uint32_t n_pub, ip_pub;   /* ISR -> main loop: the length and the position, cells */
    uint32_t len, pos;           /* main loop: the same in ms (tape_frame), for the screen */
    /* the ISR's own */
    uint32_t n;                  /* loop length, cells (0: empty; while recording: recorded so far) */
    uint32_t ip, frac;           /* the head: the cell, and the way to the next one (Q16) */
    int32_t spd, ramp;           /* speed (Q16, 65 536 = 1x), its change per block (brake, spin-up) */
    uint8_t braking, spinning;
    int32_t acc, cnt;            /* the live samples on the cell under the head */
    int32_t loss;                /* WEAR's high loss on writes: a one-pole across cells (Q7 cells x 128) */
    uint32_t wow, flut, rnd;     /* WEAR: the LFOs' phases, the hiss's xorshift */
} tape = {.rnd = 0x1234567u};

/* ---- the main loop's side ---- */
static void tape_post(uint8_t e)
{
    uint8_t w = tape.ev_w;
    if ((uint8_t)(w - tape.ev_r) >= 4u)          /* (full: four presses within a block: the newest is dropped) */
        return;
    tape.ev[w & 3u] = e;
    RING_PUBLISH();
    tape.ev_w = (uint8_t)(w + 1u);
}

static void tape_rec(void) { tape_post(TE_REC); }
static void tape_play(void) { tape_post(TE_PLAY); }
static void tape_erase(void) { tape_post(TE_ERASE); }

static void tape_frame(void)    /* every frame: the ISR's cells -> ms for the screen */
{
    uint32_t n = tape.n_pub, ip = tape.ip_pub;
    tape.len = tape.st == TP_REC || tape.st == TP_EMPTY ? 0u : n * 1000u / TAPE_RATE;
    tape.pos = (tape.st == TP_REC ? n : ip) * 1000u / TAPE_RATE;
}

static uint32_t tape_pct(void)  /* the screen's bar: 0..100 of the loop (REC: of the longest loop) */
{
    uint32_t l = tape.st == TP_REC ? TAPE_MAX_MS : tape.len;
    return l ? (tape.pos < l ? tape.pos : l) * 100u / l : 0u;
}

/* ---- the ISR's side ---- */
static int32_t tape_base(void)   /* SPEED's speed, Q16: x 655.36 without a divide (100 -> 65 536, 50 -> 32 768) */
{
    return set.g[G_SPEED] * 167773 >> 8;
}

static __attribute__((noinline)) void tape_close(void)   /* REC -> PLAY: the first pass sets the length */
{
    if (tape.n < TAPE_MIN) {                     /* a very short press: pad with silence */
        memset(tape_cells + tape.n, 0, TAPE_MIN - tape.n);
        tape.n = TAPE_MIN;
    }
    tape.ip = 0;
    tape.frac = 0;
    tape.spd = tape_base();
    tape.braking = tape.spinning = 0;
    tape.cnt = tape.acc = 0;
    tape.st = TP_PLAY;
}

static __attribute__((noinline)) void tape_spin(uint8_t st)   /* STOP -> PLAY / DUB: spin up */
{
    tape.braking = 0;
    tape.spinning = 1;
    tape.ramp = tape_base() / SPIN_BLOCKS;
    tape.ramp = tape.ramp ? tape.ramp : 1;
    tape.cnt = tape.acc = 0;
    tape.st = st;
}

static __attribute__((noinline)) void tape_brake(void)   /* PLAY / DUB -> STOP: slow down over BRAKE */
{
    int32_t blocks = set.g[G_BRAKE] * (FS / 10) / CTL;
    tape.spinning = 0;
    tape.braking = 1;
    tape.ramp = tape.spd / (blocks > 0 ? blocks : 1);
    tape.ramp = tape.ramp ? tape.ramp : 1;
    tape.st = TP_STOP;
}

static __attribute__((noinline)) void tape_event(uint8_t e)
{
    switch (e) {
    case TE_REC:
        switch (tape.st) {
        case TP_EMPTY: tape.n = 0; tape.cnt = tape.acc = 0; tape.loss = 0; tape.st = TP_REC; break;
        case TP_REC: tape_close(); break;
        case TP_PLAY: tape.st = TP_DUB; break;
        case TP_DUB: tape.st = TP_PLAY; break;
        case TP_STOP: tape_spin(TP_DUB); break;
        }
        break;
    case TE_PLAY:
        switch (tape.st) {
        case TP_REC: tape_close(); break;
        case TP_PLAY:
        case TP_DUB: tape_brake(); break;
        case TP_STOP: tape_spin(TP_PLAY); break;
        default: break;
        }
        break;
    case TE_ERASE:
        tape.st = TP_EMPTY;
        tape.n = tape.ip = tape.frac = 0;
        tape.braking = tape.spinning = 0;
        tape.spd = 0;
        break;
    }
    tape.n_pub = tape.n;
}

static int32_t tape_sat(int32_t c)  /* a cell's value, saturating softly above 96 */
{
    int32_t a = c < 0 ? -c : c;
    if (a > 96)
        a = 96 + ((a - 96) >> 1);
    a = a > 127 ? 127 : a;
    return c < 0 ? -a : a;
}

static void tape_write(uint32_t cell, int32_t keep_q8, int32_t loss_a)   /* the head leaves a cell (REC, DUB) */
{
    int32_t in, v;
    if (tape.cnt < 1 || tape.cnt > 8) {          /* (too slow: braking) */
        tape.cnt = tape.acc = 0;
        return;
    }
    in = (tape.acc * TAPE_RECIP[tape.cnt] >> 15) * 5 >> 9;   /* the average, as cells (Q15 x 5 / 512) */
    tape.cnt = tape.acc = 0;
    v = (tape_cells[cell] * keep_q8 >> 8) + in;
    tape.loss += (v * 128 - tape.loss) * loss_a >> 14;   /* WEAR: the highs lost on every pass */
    tape_cells[cell] = (int8_t)tape_sat(tape.loss >> 7);
}

/* out[2i]: the chain's output in, plus the tape out (mono, Q15). Once per block: the buttons, the speed. */
static void tape_process(int32_t *out, uint32_t n)
{
    uint32_t i;
    int32_t wear = set.g[G_WEAR], keep, loss_a, step, hiss;
    int dub, rev;
    while (tape.ev_r != tape.ev_w) {
        tape_event(tape.ev[tape.ev_r & 3u]);
        tape.ev_r = (uint8_t)(tape.ev_r + 1u);
    }
    if (tape.st == TP_EMPTY)
        return;
    keep = (100 - set.g[G_DECAY]) * 656 >> 8;   /* the old material kept per pass, Q8 (x 2.5625: 0 -> 256) */
    loss_a = 16384 - wear * 98;                  /* WEAR 100: a cell-rate one-pole at ~1.8 kHz on every write */
    if (tape.st == TP_REC) {                     /* the first pass: forward at 1x, the live sound monitored only */
        for (i = 0; i < n; i++) {
            tape.acc += out[2u * i];
            if (++tape.cnt == 2) {
                tape_write(tape.n, 0, loss_a);
                if (++tape.n >= TAPE_N) {        /* the longest loop: it closes by itself */
                    tape_close();
                    break;
                }
            }
        }
        tape.n_pub = tape.n;
        return;
    }
    if (tape.braking) {                          /* the speed: brake, spin-up, wow and flutter (per block) */
        tape.spd -= tape.ramp;
        if (tape.spd <= 0) {
            tape.spd = 0;
            tape.braking = 0;
        }
    } else if (tape.st == TP_STOP) {             /* braked to a stop */
        tape.spd = 0;
    } else if (tape.spinning) {
        int32_t b = tape_base();
        tape.spd += tape.ramp;
        if (tape.spd >= b) {
            tape.spd = b;
            tape.spinning = 0;
        }
    } else
        tape.spd = tape_base();
    if (!tape.spd) {                             /* stopped: no tape */
        tape.ip_pub = tape.ip;
        return;
    }
    tape.wow += WOW_INC;
    tape.flut += FLUT_INC;
    {                                            /* WEAR 100: wow +-0.61 % (200 / 32 768), flutter +-0.15 % */
        int32_t m = (br_sine(tape.wow) * (wear * 2) >> 15) + (br_sine(tape.flut) * wear >> 16);
        step = (tape.spd + (tape.spd * m >> 15)) >> 1;   /* cells per output sample, Q16 (the tape: half rate) */
    }
    dub = tape.st == TP_DUB;
    rev = set.g[G_DIR] != 0;
    hiss = wear * 2;                             /* WEAR 100: ~-44 dB of hiss */
    for (i = 0; i < n; i++) {
        uint32_t nx = rev ? (tape.ip ? tape.ip - 1u : tape.n - 1u) : (tape.ip + 1u < tape.n ? tape.ip + 1u : 0u);
        int32_t c0 = tape_cells[tape.ip], c1 = tape_cells[nx];
        int32_t s = c0 * 256 + ((c1 - c0) * (int32_t)(tape.frac >> 8));
        s = s * 13107 >> 15;                     /* cells x 256 -> Q15 (x 512 / 5 / 256) */
        if (hiss) {
            tape.rnd ^= tape.rnd << 13;
            tape.rnd ^= tape.rnd >> 17;
            tape.rnd ^= tape.rnd << 5;
            s += ((int32_t)(tape.rnd >> 16) - 32768) * hiss >> 15;
        }
        if (dub) {
            tape.acc += out[2u * i];
            tape.cnt++;
        }
        out[2u * i] += s;
        tape.frac += (uint32_t)step;
        while (tape.frac >= 65536u) {            /* (at most once: step < 1 cell) */
            tape.frac -= 65536u;
            if (dub)
                tape_write(tape.ip, keep, loss_a);
            tape.ip = nx;
            nx = rev ? (tape.ip ? tape.ip - 1u : tape.n - 1u) : (tape.ip + 1u < tape.n ? tape.ip + 1u : 0u);
        }
    }
    tape.ip_pub = tape.ip;
}
