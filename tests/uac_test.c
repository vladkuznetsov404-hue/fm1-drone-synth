/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Copyright (C) 2026 M-Vave-OP1 contributors */
/* Host test of the USB audio ring of src/usb.c the way audio.c drives it: uac_render_start() before each half buffer,
 * uac_tap() after each 32-frame block, uac_packet() once a millisecond (what uac_service queues). The render is a
 * counter in Q15 (left = n, right = -n, clipped at 16 bit), so every frame the host receives must be the one
 * before it plus one: a lost, repeated or reordered frame shows at once. 44.1 kHz only (the app's build). */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
#define HALF_FRAMES 128
#define CTL 32
#define FELUCCA_OTA 1
#define FELUCCA_CDC 0
#define FELUCCA_UAC 1
#define FELUCCA_UAC_48K 0
#define FELUCCA_USB_LAYOUT 0
#define FELUCCA_CDC_DEFAULT 0
#define FELUCCA_DISK 0
static void fm1_delay_ms(uint32_t ms) { (void)ms; }
#pragma GCC diagnostic ignored "-Wint-to-pointer-cast"
#include "../firmware/src/usb.c"
static uint32_t ota_now_ms(void) { return 0; }
static void ota_idle(void) {}

static int fails;
static void check(const char *what, int ok)
{
    printf("%-72s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok)
        fails++;
}

static int32_t counter;                                 /* the render: frame n -> L n % 60000 - 30000, R -L (Q15) */
static int follows(int32_t a, int32_t b) { return b == a + 1 || (a == 29999 && b == -30000); }
static void render_half(int ramp)
{
    int32_t blk[2 * CTL];
    uint32_t b, i;
    uac_render_start();
    for (b = 0; b < HALF_FRAMES; b += CTL) {
        for (i = 0; i < CTL; i++) {
            counter++;
            blk[2 * i] = ramp ? counter % 60000 - 30000 : 0;      /* a saw inside 16 bit: the step is always +1 */
            blk[2 * i + 1] = ramp ? -(counter % 60000 - 30000) : 0;
        }
        uac_tap(blk, CTL);
    }
}

static int32_t got_l[200000];
static uint32_t got_n;
static void take_packet(void)
{
    uint32_t d[UA_MAXF], n = uac_packet(d), i;
    for (i = 0; i < n && got_n < 200000u; i++)
        got_l[got_n++] = (int16_t)(d[i] & 0xFFFFu);
}

/* seconds of sound: renders every 128 frames, packets every ms, the render `late_us` after the packet grid */
static void run(double seconds, double late_us)
{
    double t = 0, next_r = late_us, next_p = 0, half_us = HALF_FRAMES * 1e6 / 44100.0, end = seconds * 1e6;
    while (t < end) {
        if (next_p <= next_r) {
            t = next_p;
            take_packet();
            next_p += 1000.0;
        } else {
            t = next_r;
            render_half(1);
            next_r += half_us;
        }
    }
}

int main(void)
{
    uint32_t i, nz = 0, bad = 0, steps = 0;
    printf("-- USB audio ring (uac_render_start, uac_tap, uac_packet)\n");

    usb.up = 1;
    usb.config = 1;
    uac.alt = 1;                                        /* the host selected the streaming setting ... */
    uac.flowing = 0;
    render_half(1);
    check("nothing is fed before the host reads a packet (flowing 0)", ua_w == 0 && !uac.go);
    uac.flowing = 1;                                    /* ... and reads */
    uac.fill_lo = 0xFFFFFFFFu;                          /* (uac_ep4_reset does this on a real stream start) */
    uac.fill_hi = 0;
    counter = 0;
    got_n = 0;
    run(3.0, 700.0);
    check("primed, then a stream", uac.go && uac.pkts > 2900u);
    check("no underrun, no overrun, no missed packet over 3 s", !uac.underruns && !uac.overruns && !uac.missed);
    for (i = 0; i < got_n && got_l[i] == 0; i++)
        ;
    nz = i;
    check("the prime is silence, then the render starts (a short run of zeros)", nz > 0 && nz < 2u * UA_PRIME);
    for (i = nz + 1; i < got_n; i++, steps++)
        if (!follows(got_l[i - 1], got_l[i]))
            bad++;
    check("every frame the host gets is the one before + 1 (no loss, repeat or reorder)", bad == 0 && steps > 100000u);
    {   /* the long-run rate: frames delivered match frames rendered within the ring's fill */
        uint32_t made = (uint32_t)counter, fill = ua_w - ua_r;
        check("frames in = frames out + the ring's fill", (uint32_t)uac.frames - UA_PRIME <= made &&
                                                          made - (uint32_t)(uac.frames - UA_PRIME) <= fill + 2u * HALF_FRAMES);
    }
    check("the ring's fill at render starts stayed near its band (a start may be a little late)",
          uac.fill_lo + 8u >= UA_LO && uac.fill_hi <= UA_HI);

    {   /* clipping: a Q15 block past 16 bit saturates, it does not wrap */
        int32_t blk[2 * CTL];
        uint32_t d[UA_MAXF], w0 = ua_w, k;
        for (k = 0; k < CTL; k++) {
            blk[2 * k] = 40000;
            blk[2 * k + 1] = -40000;
        }
        uac_render_start();
        uac_tap(blk, CTL);
        check("a block is written whole to the ring", ua_w - w0 == CTL);
        (void)d;
        check("clip +", (int16_t)(ua_ring[w0 & (UA_N - 1u)] & 0xFFFFu) == 32767);
        check("clip -", (int16_t)(ua_ring[w0 & (UA_N - 1u)] >> 16) == -32768);
    }

    /* the host stops reading (alt 0 back, or it stalls): the render stops feeding, the ring does not fill up */
    uac.alt = 0;
    {
        uint32_t w0;
        render_half(1);                                 /* sees alt 0 */
        w0 = ua_w;
        for (i = 0; i < 20; i++)
            render_half(1);
        check("alt 0: the render feeds nothing", ua_w == w0 && !uac.go);
    }
    /* back to reading: primed again, silence first, then the stream, still continuous */
    uac.alt = 1;
    uac.flowing = 1;
    got_n = 0;
    counter = 1000;
    uac.underruns = uac.overruns = 0;
    run(1.0, 300.0);
    for (i = 0; i < got_n && got_l[i] == 0; i++)
        ;
    nz = i;
    bad = 0;
    for (i = nz + 1; i < got_n; i++)
        if (!follows(got_l[i - 1], got_l[i]))
            bad++;
    check("restart: re-primed, then continuous again", nz > 0 && nz < 2u * UA_PRIME && bad == 0 && !uac.underruns);

    /* nobody reads at all (the ring would overrun): blocks are dropped, never written past the reader */
    uac.flowing = 1;
    {
        uint32_t k;
        for (k = 0; k < 40; k++)
            render_half(1);                             /* no packets taken */
        check("a reader that stops: the ring never holds more than it can", (uint32_t)(ua_w - ua_r) <= UA_N);
        check("... and the dropped blocks are counted", uac.overruns > 0);
    }
    printf("%s\n", fails ? "usb audio: FAILED" : "usb audio: all ok");
    return fails ? 1 : 0;
}
