/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vlad Kuznetsov */
/* The sound (audio ISR): the latch and four voices of the SAW drone engine (docs/SPEC.md, milestone 2), with the
 * swell and release of the sound's ENV values and the master volume. The fuzz, the cabinet, DARK, BREATH, the FM
 * engine and the cassette come in the next milestones.
 *
 * The latch: a key (or a MIDI note-on) whose note is not sounding starts it; the same note again releases it (the
 * release time); a note-off does nothing. At most NV notes: a fifth takes a voice (a free one, else the oldest
 * releasing, else the oldest held). A taken voice that still sounds fades out in STEAL_RAMP (10 ms, V_STEAL), then
 * starts the new note from silence with its swell: no click, no note appearing at full level. A note that is still
 * releasing and is latched again swells back from where it is. SCL releases every note (silence_req),
 * app_audio_stop silences at once (stop_req).
 *
 * A voice: LAYERS (1..3) band-limited saws (polyBLEP) and a sine sub one or two octaves down.
 *   detune   BEATS sets the spread between the outermost layers, 1..40 cents; SPREAD CURVE bends the knob from
 *            linear (0) to cubic (100: more of its travel for slow beating). Two layers sit at +-1/2 of it; three at
 *            0, +1/2 and -0.43 (unequal, so the two beat rates against the middle layer differ and the pattern
 *            never repeats exactly). The factors are shared by every voice and computed once per block.
 *   level    each layer at 1/sqrt(LAYERS) (uncorrelated saws: the loudness stays put); the layer and sub levels glide
 *            to their targets a step per block (~45 ms end to end), so LAYERS and SUB LEVEL turn without clicks.
 *   polyBLEP the saw's wrap is smoothed over the sample before and after it with the two-segment polynomial residual;
 *            x = t / dt comes from a reciprocal of the increment (rdt = 2^47 / inc) made at the note's start, so
 *            the sample loop has no divide; the 64-bit multiply runs only on the two samples around a wrap.
 *   sub      a sine from two parabolas with one correction step (|error| < 0.1 %, harmonics below -55 dB).
 *
 * Gains are Q24 (1 << 24 = full); ramps are a per-sample step computed once per note event (the divides are in
 * noinline functions); samples Q15, summed in int32. */
#define NV 4u
#define NL 3u                    /* layers per voice at most */
#define NOTE_KEY0 53u            /* note key 0 is F3 at octave 0 (hal/fm1_input.h fm1_in.notes) */
#define VAMP 6000                /* one voice's level, Q15: a single layer's saw peak, the sub's peak at SUB 100 */
#define G_FULL (1 << 24)
#define MIN_RAMP 256             /* samples (~6 ms): the shortest swell or release, no clicks at 0 s */
#define STEAL_RAMP 441           /* samples (10 ms): a stolen voice fades out in this, from full */
#define LVL_STEP 64              /* layer / sub level glide per block, Q15 units of VAMP-scaled level (~45 ms) */

enum { V_OFF, V_ON, V_REL, V_STEAL };   /* V_ON: rising to full or held; V_REL: falling to silence;
                                         * V_STEAL: falling fast, then the note in `next` starts */
enum { SRC_KEY = 1, SRC_MIDI = 2 };

/* phase increments of C4 .. B4 (2^32 f / FS); other octaves shift */
static const uint32_t OCT4_INC[12] = {25480119u, 26995246u, 28600467u, 30301139u, 32102938u, 34011878u,
                                      36034330u, 38177043u, 40447168u, 42852281u, 45400411u, 48100060u};
/* 1 / sqrt(layers), Q15 */
static const int32_t LAYER_GAIN[NL + 1u] = {0, 32768, 23170, 18919};
/* each layer's share of the spread, Q15, per layer count (docs/SPEC.md Sound) */
static const int32_t LAYER_OFF[NL + 1u][NL] = {{0, 0, 0}, {0, 0, 0}, {16384, -16384, 0}, {0, 16384, -14090}};

typedef struct {
    uint32_t ph[NL], sub;        /* phases: the layers' saws, the sub's sine */
    uint32_t inc, rdt, age;      /* the note's increment, 2^47 / inc (polyBLEP), when it started (stealing) */
    int32_t gain, step;          /* Q24; step per sample, + while rising, - while falling */
    int32_t lvl[NL];             /* each layer's level now, Q15 (glides to snd.lvl_to) */
    int32_t sub_lvl;             /* the sub's level now, Q15 */
    uint8_t note, next, state, src;   /* note: the pitch sounding; next: the note waiting in V_STEAL */
} voice_t;

static struct {
    voice_t v[NV];
    uint32_t last_keys, clock;
    int32_t det[NL];             /* per block: each layer's frequency offset, Q24 (f = 1 + det / 2^24) */
    int32_t lvl_to[NL], sub_to;  /* per block: the levels the layers and the sub glide to, Q15 */
    volatile uint8_t stop_req;   /* main loop -> ISR: everything silent now, every note forgotten */
    volatile uint8_t silence_req;   /* main loop -> ISR (SCL): every note released */
    volatile uint8_t held[NV];   /* ISR -> screen and LEDs: the latched notes per voice (0 none; releasing: 0) */
    volatile uint8_t last_midi;  /* ISR -> screen: the last MIDI note-on taken (0: none yet) */
} snd;

static uint32_t note_inc(uint32_t n)             /* MIDI note -> phase increment */
{
    uint32_t oct = n / 12u, inc = OCT4_INC[n % 12u];
    return oct >= 5u ? inc << (oct - 5u) : inc >> (5u - oct);
}

static int32_t ramp_step(int32_t tenths)         /* a ramp of tenths of a second over the whole range, per sample */
{
    int32_t len = tenths * (FS / 10);
    return G_FULL / (len < MIN_RAMP ? MIN_RAMP : len);
}

/* BEATS and SPREAD CURVE -> the spread between the outermost layers, cents in Q8 (256 .. 40 * 256) */
static int32_t spread_q8(int32_t beats, int32_t curve)
{
    int32_t b = beats * 1311 >> 2;               /* 0..100 -> 0..32775, Q15 */
    int32_t b3 = (b * b >> 15) * b >> 15, m;
    b = b > 32768 ? 32768 : b;
    b3 = b3 > 32768 ? 32768 : b3;
    m = b + ((b3 - b) * (curve * 327) >> 15);    /* linear -> cubic; 327 / 32768 just under 1 / 100, so the blend
                                                  * never passes the cubic and the spread never dips below 1 c */
    return 256 + (39 * 256 * m >> 15);
}

static __attribute__((noinline)) void snd_block_params(void)   /* once per block: detune factors, level targets */
{
    int32_t nl = cur.p[P_LAYERS], d = spread_q8(cur.p[P_BEATS], cur.p[P_CURVE]), l, lamp;
    nl = nl < 1 ? 1 : nl > (int32_t)NL ? (int32_t)NL : nl;
    lamp = VAMP * LAYER_GAIN[nl] >> 15;
    for (l = 0; l < (int32_t)NL; l++) {
        int32_t c = d * LAYER_OFF[nl][l] >> 15;  /* cents, Q8 */
        int32_t a = c * 9690 >> 8;               /* c * ln 2 / 1200, Q24 */
        int32_t a8 = a >> 8;
        snd.det[l] = a + (a8 * a8 >> 9);         /* 2^(c/1200) - 1 ~ a + a^2 / 2, Q24 */
        snd.lvl_to[l] = l < nl ? lamp : 0;
    }
    snd.sub_to = cur.p[P_SUB] * (VAMP / 100);
}

static void voice_begin(voice_t *v, uint32_t note)   /* a new note from silence */
{
    uint32_t l;
    v->note = (uint8_t)note;
    v->inc = note_inc(note);
    v->rdt = (0xFFFFFFFFu / ((v->inc >> 8) | 1u)) << 7;   /* ~2^47 / inc: inc >> 8 >= 3110 (MIDI 0), 12 bits+ */
    for (l = 0; l < NL; l++) {
        v->ph[l] = l * 0x55555555u;               /* 0, 1/3, 2/3: no coherent peak at the start */
        v->lvl[l] = snd.lvl_to[l];
    }
    v->sub = 0;
    v->sub_lvl = snd.sub_to;
    v->gain = 0;
    v->state = V_ON;
    v->step = ramp_step(cur.p[P_ATTACK]);
}

static __attribute__((noinline)) void snd_toggle(uint32_t note, uint32_t src)   /* one latch press */
{
    uint32_t i, pick = NV, oldest = 0xFFFFFFFFu;
    voice_t *v;
    if (note > 127u)
        return;
    for (i = 0; i < NV; i++) {                   /* sounding: release it */
        v = &snd.v[i];
        if ((v->state == V_ON && v->note == note) || (v->state == V_STEAL && v->next == note)) {
            if (v->state == V_ON)                /* (a steal cancelled: the old note fades on as it was) */
                v->step = -ramp_step(cur.p[P_RELEASE]);
            v->state = V_REL;
            snd.held[i] = 0;
            return;
        }
    }
    for (i = 0; i < NV && pick == NV; i++)       /* a free voice, */
        if (snd.v[i].state == V_OFF)
            pick = i;
    for (i = 0; i < NV && pick == NV; i++)       /* else the same note still releasing (it swells back), */
        if (snd.v[i].state == V_REL && snd.v[i].note == note)
            pick = i;
    if (pick == NV)                              /* else the oldest releasing, */
        for (i = 0; i < NV; i++)
            if (snd.v[i].state == V_REL && snd.v[i].age < oldest) {
                oldest = snd.v[i].age;
                pick = i;
            }
    if (pick == NV)                              /* else the oldest held */
        for (i = 0; i < NV; i++)
            if (snd.v[i].age < oldest) {
                oldest = snd.v[i].age;
                pick = i;
            }
    v = &snd.v[pick];
    v->src = (uint8_t)src;
    v->age = ++snd.clock;
    snd.held[pick] = (uint8_t)note;
    if (v->state == V_OFF) {
        voice_begin(v, note);
    } else if (v->note == note) {                /* releasing, same note: swell back from here */
        v->state = V_ON;
        v->step = ramp_step(cur.p[P_ATTACK]);
    } else {                                     /* another note sounding: fade it, then start (snd_steal_end) */
        v->next = (uint8_t)note;
        v->state = V_STEAL;
        v->step = -(G_FULL / STEAL_RAMP);
    }
}

static __attribute__((noinline)) void snd_steal_end(voice_t *v) { voice_begin(v, v->next); }

static __attribute__((noinline)) void snd_keys(uint32_t down)   /* keys that went down since the last block */
{
    int32_t base = (int32_t)NOTE_KEY0 + 12 * set.g[G_OCT];
    while (down) {
        uint32_t k = (uint32_t)__builtin_ctz(down);
        down &= down - 1u;
        if (base + (int32_t)k >= 0)
            snd_toggle((uint32_t)(base + (int32_t)k), SRC_KEY);
    }
}

static __attribute__((noinline)) void snd_midi(uint32_t pkt, uint32_t source)   /* one event */
{
    uint32_t st = (pkt >> 8) & 0xF0u, ch = (pkt >> 8) & 0x0Fu, d1 = (pkt >> 16) & 0x7Fu, d2 = (pkt >> 24) & 0x7Fu;
    if (pkt == MIDI_LOST)                        /* lost events: with a latch only toggles are lost (note-offs do
                                                  * nothing), so the drones go on; SCL silences them */
        return;
    if (source == 2u && !set.g[G_TRS])
        return;
    if (set.g[G_CH] && ch != (uint32_t)set.g[G_CH] - 1u)
        return;
    if (st == 0x90u && d2) {                     /* note-on latches like a key; note-off (or velocity 0): nothing */
        snd.last_midi = (uint8_t)d1;
        snd_toggle(d1, SRC_MIDI);
    }
}

static __attribute__((noinline)) void snd_requests(void)
{
    uint32_t i;
    if (snd.stop_req) {
        snd.stop_req = 0;
        snd.silence_req = 0;
        for (i = 0; i < NV; i++) {
            snd.v[i].state = V_OFF;
            snd.v[i].gain = 0;
            snd.held[i] = 0;
        }
    }
    if (snd.silence_req) {
        snd.silence_req = 0;
        for (i = 0; i < NV; i++)
            if (snd.v[i].state == V_ON || snd.v[i].state == V_STEAL) {
                if (snd.v[i].state == V_ON)
                    snd.v[i].step = -ramp_step(cur.p[P_RELEASE]);
                snd.v[i].state = V_REL;
                snd.held[i] = 0;
            }
    }
}

static inline int32_t glide(int32_t now, int32_t to)
{
    return now < to ? (now + LVL_STEP < to ? now + LVL_STEP : to) : (now - LVL_STEP > to ? now - LVL_STEP : to);
}

static inline int32_t sine_q15(uint32_t ph)      /* sin(2 pi ph / 2^32), Q15 (+-32768) */
{
    int32_t x = (int32_t)ph >> 16, ax = x < 0 ? -x : x;   /* -1 .. 1 = -pi .. pi */
    int32_t y = x * (32768 - ax) >> 13, ay;      /* 4 x (1 - |x|): the parabola */
    ay = y < 0 ? -y : y;
    return y + ((((y * ay) >> 15) - y) * 7373 >> 15);   /* + 0.225 (y |y| - y) */
}

static void voice_render(voice_t *v, int32_t *out, uint32_t n)
{
    int32_t tmp[CTL];
    uint32_t i, l, ph, inc, rdt, sph, sinc;
    int32_t g = v->gain, st = v->step, top = v->state == V_ON ? G_FULL : 0, lv, sl;
    for (i = 0; i < n; i++)
        tmp[i] = 0;
    for (l = 0; l < NL; l++) {                   /* the layers: band-limited saws */
        v->lvl[l] = glide(v->lvl[l], snd.lvl_to[l]);
        lv = v->lvl[l];
        if (!lv)
            continue;
        ph = v->ph[l];
        inc = v->inc + (uint32_t)((int64_t)v->inc * snd.det[l] >> 24);
        rdt = v->rdt - (uint32_t)((int64_t)v->rdt * snd.det[l] >> 24);   /* 1 / (1 + d) ~ 1 - d */
        for (i = 0; i < n; i++) {
            int32_t s;
            uint32_t x;
            ph += inc;
            s = (int32_t)(ph >> 16) - 32768;
            if (ph < inc) {                      /* just past the wrap: x = t / dt */
                x = (uint32_t)((uint64_t)ph * rdt >> 32);
                x = x > 32768u ? 32768u : x;
                s -= (int32_t)(2u * x) - (int32_t)(x * x >> 15) - 32768;
            } else if (0u - ph < inc) {          /* just before it: x = (1 - t) / dt */
                x = (uint32_t)((uint64_t)(0u - ph) * rdt >> 32);
                x = 32768u - (x > 32768u ? 32768u : x);
                s -= (int32_t)(x * x >> 15);
            }
            tmp[i] += s * lv >> 15;
        }
        v->ph[l] = ph;
    }
    v->sub_lvl = glide(v->sub_lvl, snd.sub_to);
    sl = v->sub_lvl;
    sph = v->sub;
    sinc = v->inc >> (cur.p[P_SUBOCT] == 2 ? 2 : 1);
    for (i = 0; i < n; i++) {                    /* the sub, the envelope, into the mix */
        int32_t s;
        sph += sinc;
        s = tmp[i] + (sine_q15(sph) * sl >> 15);
        g += st;
        g = st > 0 ? (g > top ? top : g) : (g < 0 ? 0 : g);
        out[2u * i] += s * (g >> 9) >> 15;
    }
    v->sub = sph;
    v->gain = g;
}

static void snd_render(int32_t *out, uint32_t n)
{
    uint32_t keys = fm1_in.notes, pkt, src, i, j;
    int32_t vol = set.g[G_VOL] * 82 >> 5;        /* 0..100 -> 0..256 (Q8) */
    for (i = 0; i < n; i++)
        out[2u * i] = 0;
    snd_block_params();                          /* (first: a note started below takes this block's levels) */
    if (keys & ~snd.last_keys)
        snd_keys(keys & ~snd.last_keys);
    snd.last_keys = keys;
    while (midi_in_take(&pkt, &src))
        snd_midi(pkt, src);
    if (snd.stop_req | snd.silence_req)          /* after the queue: what came before a stop is forgotten too */
        snd_requests();
    for (j = 0; j < NV; j++) {
        voice_t *v = &snd.v[j];
        if (v->state == V_OFF)
            continue;
        voice_render(v, out, n);
        if (v->gain == 0 && v->state == V_REL)
            v->state = V_OFF;
        else if (v->gain == 0 && v->state == V_STEAL)
            snd_steal_end(v);
    }
    for (i = 0; i < n; i++) {
        int32_t s = out[2u * i] * vol >> 8;
        s = s > 32767 ? 32767 : s < -32767 ? -32767 : s;
        out[2u * i] = s;
        out[2u * i + 1u] = s;
    }
}
