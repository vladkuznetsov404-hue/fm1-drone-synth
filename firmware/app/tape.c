/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vlad Kuznetsov */
/* The cassette, skeleton: the transport's states and the loop's length and position, kept by the main loop in
 * milliseconds so the screen and the LEDs show the whole workflow. The tape audio itself (the 8-bit ~22 kHz loop
 * buffer in the pool, overdub with decay, speed, reverse, the brake, wear) is milestones 6 and 7 of docs/SPEC.md and
 * will move the clock into the ISR (sample counts); the states and the REC / PLAY rules here stay.
 *   REC     EMPTY: record (the first pass sets the length; at TAPE_MAX_MS it closes by itself)
 *           REC: close the loop and play   PLAY: overdub on   DUB: overdub off   STOP: play with overdub
 *   REC 1 s held: erase (EMPTY)
 *   PLAY    REC: close the loop and play   PLAY / DUB: stop (the brake, later)   STOP: play   EMPTY: nothing */
#define TAPE_MAX_MS 10000u       /* ~10 s: 220 KB of 8-bit samples at 22 kHz (docs/SPEC.md The cassette) */
#define REC_HOLD_MS 1000u

enum { TP_EMPTY, TP_REC, TP_PLAY, TP_DUB, TP_STOP };
static const char *const TP_NAME[5] = {"EMPTY", "REC", "PLAY", "DUB", "STOP"};

static struct {
    uint8_t st;
    uint32_t t0;                 /* fm1_ms when the loop's position was 0 (REC: when recording began) */
    uint32_t len;                /* loop length, ms (0: empty) */
    uint32_t pos;                /* position when stopped, ms */
} tape;

static uint32_t tape_pos(void)  /* ms into the loop (REC: recorded so far) */
{
    uint32_t t = fm1_ms - tape.t0;
    switch (tape.st) {
    case TP_REC: return t < TAPE_MAX_MS ? t : TAPE_MAX_MS;
    case TP_PLAY:
    case TP_DUB: return tape.len ? t % tape.len : 0;
    case TP_STOP: return tape.pos;
    default: return 0;
    }
}

static uint32_t tape_pct(void)  /* the screen's bar: 0..100 of the loop (REC: of the longest loop) */
{
    uint32_t p = tape_pos(), l = tape.st == TP_REC ? TAPE_MAX_MS : tape.len;
    return l ? p * 100u / l : 0u;
}

static void tape_close(void)    /* REC -> PLAY: the first pass sets the length */
{
    uint32_t t = fm1_ms - tape.t0;
    tape.len = t < 100u ? 100u : t > TAPE_MAX_MS ? TAPE_MAX_MS : t;
    tape.t0 = fm1_ms;
    tape.st = TP_PLAY;
}

static void tape_rec(void)
{
    switch (tape.st) {
    case TP_EMPTY: tape.st = TP_REC; tape.t0 = fm1_ms; break;
    case TP_REC: tape_close(); break;
    case TP_PLAY: tape.st = TP_DUB; break;
    case TP_DUB: tape.st = TP_PLAY; break;
    case TP_STOP: tape.t0 = fm1_ms - tape.pos; tape.st = TP_DUB; break;
    }
}

static void tape_play(void)
{
    switch (tape.st) {
    case TP_REC: tape_close(); break;
    case TP_PLAY:
    case TP_DUB: tape.pos = tape_pos(); tape.st = TP_STOP; break;
    case TP_STOP: tape.t0 = fm1_ms - tape.pos; tape.st = TP_PLAY; break;
    default: break;
    }
}

static void tape_erase(void) { memset(&tape, 0, sizeof tape); }

static void tape_frame(void)    /* every frame: a first pass that reaches the end closes by itself */
{
    if (tape.st == TP_REC && fm1_ms - tape.t0 >= TAPE_MAX_MS)
        tape_close();
}
