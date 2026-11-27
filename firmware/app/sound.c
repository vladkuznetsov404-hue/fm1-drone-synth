/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vlad Kuznetsov */
/* The sound (audio ISR), skeleton: the latch and four voices of one plain saw each, with the swell and release of
 * the sound's ENV values and the master volume. The detuned layers, the sub, the fuzz, the cabinet, DARK, BREATH,
 * the FM engine and the cassette come in the milestones of docs/SPEC.md; the latch and its rules are final here.
 *
 * The latch: a key (or a MIDI note-on) whose note is not sounding starts it; the same note again releases it (the
 * release time); a note-off does nothing. At most NV notes: a fifth takes the oldest voice, which keeps its phase
 * and its level and only changes pitch (no click; the swell then runs from where the level is). SCL releases every
 * note (silence_req), app_audio_stop silences at once (stop_req).
 *
 * Gains are Q24 (1 << 24 = full); ramps are a per-sample step computed once per note event (the one divide, in a
 * noinline function); the hot loop is a phase add, a multiply and an add per voice. */
#define NV 4u
#define NOTE_KEY0 53u            /* note key 0 is F3 at octave 0 (hal/fm1_input.h fm1_in.notes) */
#define VAMP 7000                /* one voice's saw peak, Q15: four at full sum to 28 000 */
#define G_FULL (1 << 24)
#define MIN_RAMP 256             /* samples (~6 ms): the shortest swell or release, no clicks at 0 s */

enum { V_OFF, V_ON, V_REL };     /* V_ON: rising to full or held there; V_REL: falling to silence */
enum { SRC_KEY = 1, SRC_MIDI = 2 };

/* phase increments of C4 .. B4 (2^32 f / FS); other octaves shift */
static const uint32_t OCT4_INC[12] = {25480119u, 26995246u, 28600467u, 30301139u, 32102938u, 34011878u,
                                      36034330u, 38177043u, 40447168u, 42852281u, 45400411u, 48100060u};

typedef struct {
    uint32_t phase, inc, age;    /* age: when it started (snd.clock), the oldest is stolen */
    int32_t gain, step;          /* Q24; step per sample, + while rising, - while falling */
    uint8_t note, state, src;
} voice_t;

static struct {
    voice_t v[NV];
    uint32_t last_keys, clock;
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

static __attribute__((noinline)) void snd_toggle(uint32_t note, uint32_t src)   /* one latch press */
{
    uint32_t i, pick = NV, oldest = 0xFFFFFFFFu;
    voice_t *v;
    if (note > 127u)
        return;
    for (i = 0; i < NV; i++)                     /* sounding: release it */
        if (snd.v[i].state == V_ON && snd.v[i].note == note) {
            snd.v[i].state = V_REL;
            snd.v[i].step = -ramp_step(cur.p[P_RELEASE]);
            snd.held[i] = 0;
            return;
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
    if (pick == NV)                              /* else the oldest held (all are V_ON here) */
        for (i = 0; i < NV; i++)
            if (snd.v[i].age < oldest) {
                oldest = snd.v[i].age;
                pick = i;
            }
    v = &snd.v[pick];
    if (v->state == V_OFF)
        v->gain = 0;                             /* (phase and gain carry on in a stolen voice: no click) */
    v->note = (uint8_t)note;
    v->inc = note_inc(note);
    v->src = (uint8_t)src;
    v->state = V_ON;
    v->step = ramp_step(cur.p[P_ATTACK]);
    v->age = ++snd.clock;
    snd.held[pick] = (uint8_t)note;
}

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
            if (snd.v[i].state == V_ON) {
                snd.v[i].state = V_REL;
                snd.v[i].step = -ramp_step(cur.p[P_RELEASE]);
                snd.held[i] = 0;
            }
    }
}

static void snd_render(int32_t *out, uint32_t n)
{
    uint32_t keys = fm1_in.notes, pkt, src, i, j;
    int32_t vol = set.g[G_VOL] * 82 >> 5;        /* 0..100 -> 0..256 (Q8) */
    for (i = 0; i < n; i++)
        out[2u * i] = 0;
    if (keys & ~snd.last_keys)
        snd_keys(keys & ~snd.last_keys);
    snd.last_keys = keys;
    while (midi_in_take(&pkt, &src))
        snd_midi(pkt, src);
    if (snd.stop_req | snd.silence_req)          /* after the queue: what came before a stop is forgotten too */
        snd_requests();
    for (j = 0; j < NV; j++) {
        voice_t *v = &snd.v[j];
        uint32_t ph = v->phase, inc = v->inc;
        int32_t g = v->gain, st = v->step, top = v->state == V_ON ? G_FULL : 0;
        if (v->state == V_OFF)
            continue;
        for (i = 0; i < n; i++) {
            g += st;
            g = st > 0 ? (g > top ? top : g) : (g < 0 ? 0 : g);
            ph += inc;
            out[2u * i] += (((int32_t)(ph >> 16) - 32768) * VAMP >> 15) * (g >> 9) >> 15;
        }
        v->phase = ph;
        v->gain = g;
        if (v->state == V_REL && g == 0)
            v->state = V_OFF;
    }
    for (i = 0; i < n; i++) {
        int32_t s = out[2u * i] * vol >> 8;
        s = s > 32767 ? 32767 : s < -32767 ? -32767 : s;
        out[2u * i] = s;
        out[2u * i + 1u] = s;
    }
}
