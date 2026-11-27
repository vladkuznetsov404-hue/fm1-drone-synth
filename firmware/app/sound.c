/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vlad Kuznetsov */
/* The sound (audio ISR): the latch and four voices of two drone engines. SAW (milestone 2 of docs/SPEC.md): per note
 * 1..3 band-limited saws detuned by BEATS (SPREAD CURVE shapes the knob). FM (milestone 5): per note 1..2 detuned
 * two-operator pairs, the carrier with self-feedback (FEEDBACK: a sine grows into a dirty saw, the DX7's operator-1
 * trick) and a modulator at RATIO x the note (INDEX). Both with a sine sub one or two octaves down, with
 * the swell and release of the sound's ENV values and the master volume, into the chain of fx.c (the fuzz, the
 * cabinet, DARK), with BREATH's slow pitch drift (breath.c), and the cassette (tape.c). The FM engine comes in
 * milestone 5; the latch and its rules are final here.
 *
 * The latch: a key (or a MIDI note-on) whose note is not sounding starts it; the same note again releases it (the
 * release time); a note-off does nothing. At most NV notes: a fifth takes the oldest voice, which keeps its phase
 * and its level and only changes pitch (no click; the swell then runs from where the level is). SCL releases every
 * note (silence_req), app_audio_stop silences at once (stop_req).
 *
 * SCL (the scale page): the keys are not only chromatic. A scale (G_SCALE) and its root (G_ROOT) turn key k into the
 * k-th degree going up from the root (seven keys an octave for the seven-note scales: MINOR, PHRYGIAN, LOCRIAN,
 * HARM MINOR; five for PENT MINOR, six for WHOLE); CHROMATIC keeps the old layout (key 0 = F3 whatever the root).
 * A key press is a chord (G_CHORD: POWER, SUS4, DOOM ...) over its note, with the root doubled an octave or two down
 * (G_LOW): at most NV notes, the root first. The latch toggles the whole chord: pressed while its root sounds, every
 * note of the chord is released; otherwise every note that is not sounding already starts. MIDI notes stay single.
 * While SEQ is held (pad_mode) the keys are the cassette's pads (tape.c) and make no notes.
 *
 * Gains are Q24 (1 << 24 = full); ramps are a per-sample step computed once per note event (the one divide, in a
 * noinline function). The tuning of the layers (their increments and the reciprocals the saws' polyBLEP needs: the
 * divides) is worked out in snd_tune, a noinline function, only when a note starts or a tuning value changes (a
 * signature checked once per block). The hot loop per layer is a phase add, a shift and, in the one sample after
 * the saw's wrap and the one before it (rarely: 2 of every 44 100 / f samples), the polyBLEP's two multiplies.
 *
 * The saws are band-limited with polyBLEP (a 2-sample polynomial step at the wrap): the naive saw's aliasing is
 * small at drone pitches but grows up the keyboard and the fuzz of milestone 3 would make it louder. The sub is a
 * parabolic sine (one multiply), a little warmer than a true sine. */
#define NV 4u
#define NOTE_KEY0 53u            /* note key 0 is F3 at octave 0 (hal/fm1_input.h fm1_in.notes) */
#define VAMP 7000                /* one voice's peak with one layer and no sub, Q15: four at full sum to 28 000 */
#define NL 3u                    /* layers per note at most */
#define BEATS_CENTS 40           /* BEATS 100: the outer layers 40 cents off (spec: ~1 c .. ~40 c) */
/* the saws' level per layer for 1, 2, 3 layers: detuned saws rarely line up, so their sum is louder than one saw by
 * about the square root of their number; these keep 2 and 3 layers near the loudness of one */
static const int32_t LAYER_AMP[NL + 1u] = {0, VAMP, 4400, 3400};
#define FM_NL 2u                 /* FM pairs per note at most (each costs two sines a sample) */
/* one FM pair's peak for 1, 2 pairs, Q15: a sine sounds quieter than a saw at the same peak (8000 against 7000),
 * two pairs scaled like two saws (x 4400 / 7000) */
static const int32_t FM_LAMP[FM_NL + 1u] = {0, 8000, 5029};
#define XF_STEP 475              /* the engines' crossfade per block, Q15: ~69 blocks, ~50 ms */
#define FB_MAX 1200000000u       /* FEEDBACK 100 (with MASSA): ~1.75 rad of the carrier's own output */
#define IX_MAX 2733000000u       /* INDEX 100: 4 rad of modulation (as phase, 2^32 = 2 pi) */

/* A sine for the FM operators: the parabola 4 u (1 - |u|) and one correction step, y += 0.225 (y |y| - y), within
 * ~0.1 % of a sine (the sub's plain parabola has 3 % third harmonic: fine for a sub, too much for FM). Q15. */
static inline __attribute__((always_inline)) int32_t fm_sine(uint32_t ph)
{
    int32_t u = (int16_t)(ph >> 16), y = u * (32768 - (u < 0 ? -u : u)) >> 13;
    y = y > 32767 ? 32767 : y;
    return y + (((y * (y < 0 ? -y : y) >> 15) - y) * 7373 >> 15);
}
#define G_FULL (1 << 24)
#define MIN_RAMP 256             /* samples (~6 ms): the shortest swell or release, no clicks at 0 s */

enum { V_OFF, V_ON, V_REL };     /* V_ON: rising to full or held there; V_REL: falling to silence */
enum { SRC_KEY = 1, SRC_MIDI = 2 };

/* phase increments of C4 .. B4 (2^32 f / FS); other octaves shift */
static const uint32_t OCT4_INC[12] = {25480119u, 26995246u, 28600467u, 30301139u, 32102938u, 34011878u,
                                      36034330u, 38177043u, 40447168u, 42852281u, 45400411u, 48100060u};

typedef struct {
    uint32_t ph[NL], inc[NL];    /* the layers' phases and increments (2^32 = one cycle) */
    uint32_t rinv[NL];           /* 2^31 / (inc >> 8): turns a phase near the wrap into the polyBLEP's x (Q15) */
    uint32_t sph, sinc;          /* the sub */
    uint32_t mph[FM_NL];         /* FM: the modulators' phases (the carriers use ph[]) */
    int32_t c1[FM_NL], c2[FM_NL];   /* FM: the carriers' last two outputs (their feedback is the average) */
    uint32_t base, tgt, age;     /* base: the note's increment, gliding to tgt; age: when it started (snd.clock), the oldest is stolen */
    int32_t gain, step;          /* Q24; step per sample, + while rising, - while falling */
    uint8_t note, state, src;
} voice_t;

static struct {
    voice_t v[NV];
    uint32_t last_keys, clock;
    volatile uint8_t stop_req;   /* main loop -> ISR: everything silent now, every note forgotten */
    volatile uint8_t silence_req;   /* main loop -> ISR (SCL held): every note released */
    volatile uint8_t pad_mode;   /* main loop -> ISR (SEQ held): the keys are pads, not notes */
    volatile uint8_t held[NV];   /* ISR -> screen and LEDs: the latched notes per voice (0 none; releasing: 0) */
    volatile uint8_t last_midi;  /* ISR -> screen: the last MIDI note-on taken (0: none yet) */
    uint32_t tune_sig;           /* the tuning values snd_tune last used (BEATS, CURVE, LAYERS, SUB OCTAVE) */
    int32_t off[NL];             /* the layers' detune, Q24 (an increment changes by inc * off >> 24) */
    int32_t lamp;                /* a layer's level, Q15 (the sub's: fx.samp, with MASSA's lift) */
    uint8_t layers, subsh;       /* layers sounding; the sub's octave shift */
    int16_t croot;               /* the key's note whose chord is latched (-1: none: the keys are single notes) */
    uint8_t chsig;               /* CHORD and LOW ROOT as last seen: a change re-voices the sounding chord */
    int32_t xf;                  /* the crossfade: 0 SAW .. 32 768 FM, Q15 */
    uint32_t fbk, ix, ratio;     /* FM: the feedback and the index as phase per unit (>> 15 applied), the ratio */
    int16_t fm_last[3];          /* the values fm_update last used: FEEDBACK, INDEX, MASSA (-1 never) */
} snd = {.fm_last = {-1, -1, -1}, .croot = -1, .chsig = 0xFFu};

static __attribute__((noinline)) void fm_update(void)   /* FEEDBACK (+ half of MASSA) and INDEX -> phase amounts */
{
    int32_t fb = cur.p[P_FB] + cur.p[P_MASSA] / 2;
    fb = fb > 100 ? 100 : fb;
    snd.fm_last[0] = cur.p[P_FB];
    snd.fm_last[1] = cur.p[P_INDEX];
    snd.fm_last[2] = cur.p[P_MASSA];
    snd.fbk = (FB_MAX >> 15) * (uint32_t)fb / 100u;
    snd.ix = (IX_MAX >> 15) * (uint32_t)cur.p[P_INDEX] / 100u;
}

/* HOME's picture of the drone: on request (the main loop, ~10 times a second) the ISR waits for a rising zero
 * crossing of the output (or 4 096 samples: silence) and keeps every 4th sample of the next 960 (~22 ms: a period of
 * F1, two of F2), as Q15 >> 4. The main loop draws it scaled to its own peak when ready is set and asks again later. */
#define SCOPE_N 240u
static struct {
    volatile uint8_t req, ready; /* main loop -> ISR: capture; ISR -> main loop: done */
    uint8_t armed, k;
    uint16_t wait, n;
    int32_t last;
    uint32_t count;              /* captures drawn (the screen's signature) */
    int16_t y[SCOPE_N];
} scope;

static __attribute__((noinline)) void scope_take(const int32_t *out, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n && !scope.ready; i++) {
        int32_t s = out[2u * i];
        if (!scope.armed) {
            if ((scope.last < 0 && s >= 0) || ++scope.wait >= 4096u) {
                scope.armed = 1;
                scope.n = scope.k = 0;
                scope.wait = 0;
            }
            scope.last = s;
            continue;
        }
        if (++scope.k == 4u) {
            scope.k = 0;
            scope.y[scope.n] = (int16_t)(s >> 4);
            if (++scope.n == SCOPE_N) {
                scope.armed = 0;
                scope.req = 0;
                scope.ready = 1;
            }
        }
    }
}

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

static uint32_t tune_sig(void)
{
    return (uint32_t)cur.p[P_BEATS] | (uint32_t)cur.p[P_CURVE] << 8 | (uint32_t)cur.p[P_LAYERS] << 16 |
           (uint32_t)cur.p[P_SUBOCT] << 20 | (uint32_t)cur.p[P_SUB] << 24;
}

/* The detune of the layers from BEATS and SPREAD CURVE. b = BEATS / 100; the knob's shape is b at CURVE 0 (even
 * steps) and b^3 at CURVE 100 (fine steps at the low end, where the slow beating lives), mixed in between; the outer
 * layers sit at +-BEATS_CENTS times that. cents -> a ratio offset: 2^(c/1200) - 1 ~ c * ln 2 / 1200 (at 40 cents
 * 0.27 cents off the true ratio, inaudible here), so off = c * 0.000577623 in Q24 = c * 9690.9. Layers: one at the
 * note; two at +-half the spread (no centre: the beating is between them); three at the note and +-the spread. */
static __attribute__((noinline)) void snd_spread(void)
{
    int32_t b = cur.p[P_BEATS] * 65536 / 100;   /* Q16 */
    int32_t cube = (int32_t)((int64_t)b * b >> 16) * (int64_t)b >> 16;
    int32_t shape = b + (int32_t)((int64_t)(cube - b) * cur.p[P_CURVE] / 100);
    int32_t d = (int32_t)((int64_t)shape * BEATS_CENTS * 9691 >> 16);   /* Q24 ratio offset at full spread */
    uint32_t n = (uint32_t)cur.p[P_LAYERS];
    n = n < 1u ? 1u : n > NL ? NL : n;
    snd.layers = (uint8_t)n;
    snd.off[0] = n == 2u ? d / 2 : 0;
    snd.off[1] = n == 2u ? -d / 2 : d;
    snd.off[2] = -d;
    snd.lamp = LAYER_AMP[n];
    snd.subsh = cur.p[P_SUBOCT] >= 2 ? 2u : 1u;
}

static __attribute__((noinline)) void snd_tune(voice_t *v)   /* the layers and the sub from v->base */
{
    uint32_t l;
    for (l = 0; l < NL; l++) {
        uint32_t inc = (uint32_t)((int64_t)v->base + ((int64_t)v->base * snd.off[l] >> 24));
        uint32_t q = inc >> 8;
        v->inc[l] = inc;
        v->rinv[l] = q ? 0x80000000u / q : 0x80000000u;
    }
    v->sinc = v->base >> snd.subsh;
}

static __attribute__((noinline)) void snd_retune(void)   /* a tuning value changed: every voice */
{
    uint32_t i;
    snd.tune_sig = tune_sig();
    snd_spread();
    for (i = 0; i < NV; i++)
        snd_tune(&snd.v[i]);
}

/* The scales: the degrees in semitones above the root, and how many there are; the chords: the intervals above the
 * pressed key's note. SCALE 0 is the chromatic layout. */
static const uint8_t SC_LEN[7] = {12, 7, 7, 7, 7, 5, 6};
static const uint8_t SC_DEG[7][12] = {
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}, {0, 2, 3, 5, 7, 8, 10}, {0, 1, 3, 5, 7, 8, 10}, {0, 1, 3, 5, 6, 8, 10},
    {0, 2, 3, 5, 7, 8, 11},                 {0, 3, 5, 7, 10},       {0, 2, 4, 6, 8, 10}};
static const uint8_t CH_LEN[8] = {1, 2, 3, 3, 3, 2, 4, 3};
static const uint8_t CH_IV[8][4] = {{0}, {0, 7}, {0, 7, 12}, {0, 5, 7}, {0, 3, 7}, {0, 6}, {0, 7, 12, 19}, {0, 12, 24}};

static int32_t scale_start(void)                 /* the root's note at or just above the keyboard's first note */
{
    int32_t base = (int32_t)NOTE_KEY0 + 12 * set.g[G_OCT];
    return base + (set.g[G_ROOT] - base % 12 + 12) % 12;
}

static int32_t key_note(uint32_t k)              /* key 0..26 -> the MIDI note it plays */
{
    uint32_t sc = (uint32_t)set.g[G_SCALE], len;
    if (sc == 0u || sc > 6u)
        return (int32_t)NOTE_KEY0 + 12 * set.g[G_OCT] + (int32_t)k;
    len = SC_LEN[sc];
    return scale_start() + 12 * (int32_t)(k / len) + SC_DEG[sc][k % len];
}

static int32_t note_key(int32_t note)            /* the inverse: the key that plays this note, -1 if none (an off-scale MIDI note) */
{
    uint32_t sc = (uint32_t)set.g[G_SCALE], len, i;
    int32_t n, k = -1;
    if (sc == 0u || sc > 6u)
        k = note - ((int32_t)NOTE_KEY0 + 12 * set.g[G_OCT]);
    else {
        len = SC_LEN[sc];
        n = note - scale_start();
        if (n >= 0)
            for (i = 0; i < len; i++)
                if (SC_DEG[sc][i] == n % 12)
                    k = (n / 12) * (int32_t)len + (int32_t)i;
    }
    return k >= 0 && k < 27 ? k : -1;
}

/* the notes of a key press: the root, its low doubling, the chord's others; at most NV */
static uint32_t chord_notes(int32_t root, int32_t *out)
{
    uint32_t n = 0, i, ch = (uint32_t)set.g[G_CHORD];
    ch = ch > 7u ? 0u : ch;
    out[n++] = root;
    if (set.g[G_LOW] == 1 && root - 12 >= 0)     /* LOW: the root one or two octaves down (one of them) */
        out[n++] = root - 12;
    if (set.g[G_LOW] == 2 && root - 24 >= 0)
        out[n++] = root - 24;
    for (i = 1; i < CH_LEN[ch] && n < NV; i++)
        out[n++] = root + CH_IV[ch][i];
    return n;
}

static __attribute__((noinline)) void snd_stop_voice(uint32_t i)   /* release one note */
{
    snd.v[i].state = V_REL;
    snd.v[i].step = -ramp_step(cur.p[P_RELEASE]);
    snd.held[i] = 0;
}

static __attribute__((noinline)) void snd_start(uint32_t note, uint32_t src);

static __attribute__((noinline)) void snd_toggle(uint32_t note, uint32_t src)   /* one latch press */
{
    uint32_t i;
    if (note > 127u)
        return;
    for (i = 0; i < NV; i++)                     /* sounding: release it */
        if (snd.v[i].state == V_ON && snd.v[i].note == note) {
            snd_stop_voice(i);
            return;
        }
    snd_start(note, src);
}

/* A note that moves to another (a chord changing, a note stolen) glides there instead of jumping: its increment follows
 * the target with a one-pole, the time constant SLIDE (tenths of a second; 0 = at once), a step a block. The phases
 * carry on, so there is no click; the layers' tuning is worked out again each block while it moves. */
static __attribute__((noinline)) void snd_glide(uint32_t n)
{
    uint32_t i, slide = (uint32_t)set.g[G_SLIDE];
    uint64_t k = slide ? (uint64_t)n * 3804u / slide : 1u << 24;   /* Q24: n / (SLIDE x 4410 samples) */
    for (i = 0; i < NV; i++) {
        voice_t *v = &snd.v[i];
        int64_t d;
        if (v->state == V_OFF || v->tgt == v->base)
            continue;
        d = (int64_t)v->tgt - (int64_t)v->base;
        if (k >= (1u << 24) || (d < 0 ? -d : d) < (int64_t)(v->base >> 12))
            v->base = v->tgt;
        else
            v->base = (uint32_t)((int64_t)v->base + (d * (int64_t)k >> 24));
        snd_tune(v);
    }
}

static __attribute__((noinline)) void snd_morph(int32_t root, uint32_t src)   /* this root's chord takes over the sounding one */
{
    int32_t raw[NV], notes[NV], nn = 0, m = 0, i, j, r;
    uint32_t idx[NV], nr = chord_notes(root, raw);
    for (i = 0; i < (int32_t)nr; i++)            /* the new notes, in range, lowest first */
        if (raw[i] >= 0 && raw[i] <= 127) {
            for (j = nn++; j > 0 && notes[j - 1] > raw[i]; j--)
                notes[j] = notes[j - 1];
            notes[j] = raw[i];
        }
    for (i = 0; i < (int32_t)NV; i++)            /* the sounding voices, lowest note first */
        if (snd.v[i].state == V_ON) {
            for (j = m++; j > 0 && snd.v[idx[j - 1]].note > snd.v[i].note; j--)
                idx[j] = idx[j - 1];
            idx[j] = (uint32_t)i;
        }
    for (r = 0; r < m && r < nn; r++) {          /* the lowest to the lowest: each glides to its note */
        voice_t *v = &snd.v[idx[r]];
        if (v->note != (uint8_t)notes[r]) {
            v->note = (uint8_t)notes[r];
            v->tgt = note_inc((uint32_t)notes[r]);
            snd.held[idx[r]] = (uint8_t)notes[r];
        }
    }
    for (r = nn; r < m; r++)                     /* a smaller chord: the highest go */
        snd_stop_voice(idx[r]);
    for (r = m; r < nn; r++)                     /* a bigger one: the new notes swell in */
        snd_start((uint32_t)notes[r], src);
}

static __attribute__((noinline)) uint32_t snd_any_on(void)
{
    uint32_t j, on = 0;
    for (j = 0; j < NV; j++)
        on |= snd.v[j].state == V_ON;
    return on;
}

static __attribute__((noinline)) void snd_chord_watch(void)   /* CHORD or LOW ROOT turned while a chord sounds: it changes */
{
    uint8_t sig = (uint8_t)(set.g[G_CHORD] | set.g[G_LOW] << 4);
    if (sig == snd.chsig)
        return;
    snd.chsig = sig;
    if (snd.croot >= 0 && snd_any_on())
        snd_morph(snd.croot, SRC_KEY);
}

static __attribute__((noinline)) void snd_press(int32_t root, uint32_t src)   /* a key: its chord, latched as a whole */
{
    int32_t notes[NV];
    uint32_t n = chord_notes(root, notes), i, j, on = 0;
    if (root < 0 || root > 127)
        return;
    if (set.g[G_CHORD] > 0 || set.g[G_LOW] > 0) {   /* chords: the same key again ends it, another key takes the sound over */
        if (snd.croot == root && snd_any_on()) {
            for (j = 0; j < NV; j++)
                if (snd.v[j].state == V_ON)
                    snd_stop_voice(j);
            snd.croot = -1;
        } else if (snd_any_on()) {
            snd_morph(root, src);
            snd.croot = (int16_t)root;
        } else {
            for (i = 0; i < n; i++)
                if (notes[i] >= 0 && notes[i] <= 127)
                    snd_start((uint32_t)notes[i], src);
            snd.croot = (int16_t)root;
        }
        return;
    }
    snd.croot = -1;
    for (j = 0; j < NV; j++)
        on |= snd.v[j].state == V_ON && snd.v[j].note == root;
    for (i = 0; i < n; i++) {
        if (notes[i] < 0 || notes[i] > 127)
            continue;
        if (on) {                                /* the root sounds: the whole chord goes */
            for (j = 0; j < NV; j++)
                if (snd.v[j].state == V_ON && snd.v[j].note == (uint8_t)notes[i])
                    snd_stop_voice(j);
        } else {                                 /* else what is not sounding starts */
            for (j = 0; j < NV; j++)
                if (snd.v[j].state == V_ON && snd.v[j].note == (uint8_t)notes[i])
                    break;
            if (j == NV)
                snd_start((uint32_t)notes[i], src);
        }
    }
}

static __attribute__((noinline)) void snd_start(uint32_t note, uint32_t src)   /* a note starts: a free voice, or the oldest */
{
    uint32_t i, pick = NV, oldest = 0xFFFFFFFFu;
    voice_t *v;
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
    if (v->state == V_OFF) {                     /* (phase and gain carry on in a stolen voice: no click) */
        v->gain = 0;
        v->ph[0] = 0;                            /* the layers a third of a cycle apart: no phasey start */
        v->ph[1] = 0x55555555u;
        v->ph[2] = 0xAAAAAAAAu;
        v->sph = 0;
    }
    if (v->state == V_OFF) {
        v->mph[0] = v->mph[1] = 0;
        v->c1[0] = v->c1[1] = v->c2[0] = v->c2[1] = 0;
    }
    v->note = (uint8_t)note;
    v->tgt = note_inc(note);
    if (v->state == V_OFF || !set.g[G_SLIDE])    /* (a stolen voice glides to its new note) */
        v->base = v->tgt;
    snd_tune(v);
    v->src = (uint8_t)src;
    v->state = V_ON;
    v->step = ramp_step(cur.p[P_ATTACK]);
    v->age = ++snd.clock;
    snd.held[pick] = (uint8_t)note;
}

static __attribute__((noinline)) void snd_keys(uint32_t down)   /* keys that went down since the last block */
{
    while (down) {
        uint32_t k = (uint32_t)__builtin_ctz(down);
        down &= down - 1u;
        snd_press(key_note(k), SRC_KEY);
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
        snd.croot = -1;
        for (i = 0; i < NV; i++) {
            snd.v[i].state = V_OFF;
            snd.v[i].gain = 0;
            snd.held[i] = 0;
        }
    }
    if (snd.silence_req) {
        snd.silence_req = 0;
        snd.croot = -1;
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
    uint32_t keys = snd.pad_mode ? 0u : fm1_in.notes, pkt, src, i, j;
    int32_t vol = set.g[G_VOL] * 82 >> 5;        /* 0..100 -> 0..256 (Q8) */
    for (i = 0; i < n; i++)
        out[2u * i] = 0;
    if (keys & ~snd.last_keys)
        snd_keys(keys & ~snd.last_keys);
    snd.last_keys = fm1_in.notes;                /* (the real keys: one pressed as a pad makes no note when SEQ is let go) */
    while (midi_in_take(&pkt, &src))
        snd_midi(pkt, src);
    snd_chord_watch();
    snd_glide(n);
    if (snd.stop_req | snd.silence_req)          /* after the queue: what came before a stop is forgotten too */
        snd_requests();
    if (tune_sig() != snd.tune_sig)              /* (also the first block: the signature starts at 0, never valid) */
        snd_retune();
    fx_check();
    breath_block();                              /* the LFOs: br.pm, fx.dark_a, fx.lmod (breath.c) */
    if (snd.fm_last[0] != cur.p[P_FB] || snd.fm_last[1] != cur.p[P_INDEX] || snd.fm_last[2] != cur.p[P_MASSA])
        fm_update();
    snd.ratio = (uint32_t)(cur.p[P_RATIO] < 1 ? 1 : cur.p[P_RATIO] > 8 ? 8 : cur.p[P_RATIO]);
    {                                            /* the crossfade towards the engine chosen */
        int32_t t = cur.engine == ENG_FM ? 32768 : 0;
        snd.xf = snd.xf < t ? (snd.xf + XF_STEP > t ? t : snd.xf + XF_STEP) :
                 snd.xf > t ? (snd.xf - XF_STEP < t ? t : snd.xf - XF_STEP) : t;
    }
    for (j = 0; j < NV; j++) {
        voice_t *v = &snd.v[j];
        uint32_t nl = snd.layers, sph = v->sph, sinc = v->sinc, l, linc[NL], ratio = snd.ratio, ix = snd.ix,
                 fbk = snd.fbk, fnl = nl < FM_NL ? nl : FM_NL;
        int32_t xf = snd.xf, famp = FM_LAMP[fnl];
        int32_t g = v->gain, st = v->step, top = v->state == V_ON ? G_FULL : 0, lamp = snd.lamp, samp = fx.samp;
        if (v->state == V_OFF)
            continue;
        for (l = 0; l < nl; l++)                 /* BREATH's pitch: inc * pm >> 24, in 32 bits (inc >> 12: ~0.02 %) */
            linc[l] = v->inc[l] + (uint32_t)((int32_t)(v->inc[l] >> 12) * (br.pm >> 4) >> 8);
        sinc += (uint32_t)((int32_t)(sinc >> 12) * (br.pm >> 4) >> 8);
        for (i = 0; i < n; i++) {
            int32_t acc = 0, acc_s = 0, u, y;
            g += st;
            g = st > 0 ? (g > top ? top : g) : (g < 0 ? 0 : g);
            if (xf) {                            /* FM: two sines a pair (the carriers use the saws' phases) */
                int32_t fm = 0;
                for (l = 0; l < fnl; l++) {
                    uint32_t cph = xf == 32768 ? (v->ph[l] += linc[l]) : v->ph[l] + linc[l];   /* (mid-fade the
                                                  * saws' loop below moves the phase: once) */
                    int32_t m, c;
                    v->mph[l] += linc[l] * ratio;
                    m = fm_sine(v->mph[l]);
                    c = fm_sine(cph + (uint32_t)m * ix + (uint32_t)((v->c1[l] + v->c2[l]) >> 1) * fbk);
                    v->c2[l] = v->c1[l];
                    v->c1[l] = c;
                    fm += c;
                }
                fm = fm * famp >> 15;
                if (xf == 32768) {
                    acc = fm;
                    goto sub;
                }
                acc = fm * xf >> 15;
            }
            for (l = 0; l < nl; l++) {
                uint32_t p = v->ph[l] += linc[l];
                int32_t s = (int32_t)(p >> 16) - 32768, x;
                if (p < linc[l]) {               /* just after the wrap: t / dt in Q15 */
                    x = (int32_t)(((p >> 8) * v->rinv[l]) >> 16);
                    s -= 2 * x - (x * x >> 15) - 32768;
                } else if (p > ~linc[l]) {       /* just before it: (1 - t) / dt */
                    x = (int32_t)((((0u - p) >> 8) * v->rinv[l]) >> 16);
                    s -= (x * x >> 15) - 2 * x + 32768;
                }
                acc_s += s;
            }
            acc += (acc_s * lamp >> 15) * (32768 - xf) >> 15;
        sub:
            sph += sinc;                         /* the sub: 4 u (1 - |u|) over the half cycles, u = phase */
            u = (int16_t)(sph >> 16);
            y = u * (32768 - (u < 0 ? -u : u)) >> 13;
            y = y > 32767 ? 32767 : y;
            acc += y * samp >> 15;
            out[2u * i] += acc * (g >> 9) >> 15;
        }
        v->sph = sph;
        v->gain = g;
        if (v->state == V_REL && g == 0)
            v->state = V_OFF;
    }
    for (j = 0; j < NV && snd.v[j].state == V_OFF; j++)
        ;
    fx_process(out, n, j < NV);                  /* the fuzz, the cabinet, DARK (fx.c) */
    shoe_process(out, n);                        /* SHOEGAZE: the chorus and the reverb (shoe.c) */
    tape_process(out, n);                        /* the cassette: records the chain, plays back over it (tape.c) */
    dly_process(out, n);                         /* the tape echo over all of it (delay.c) */
    for (i = 0; i < n; i++) {
        int32_t s = out[2u * i] * vol >> 8;
        s = s > 32767 ? 32767 : s < -32767 ? -32767 : s;
        out[2u * i] = s;
        out[2u * i + 1u] = s;
    }
    if (scope.req && !scope.ready)
        scope_take(out, n);
}
