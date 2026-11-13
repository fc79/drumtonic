/* SPDX-License-Identifier: GPL-3.0-only */
/* The drum engine on a computer (firmware/src/drum.c, unchanged): each kit's channels and a demo groove as WAV,
 * some checks, and the render's cost.
 *
 *   tests/run_drums.sh                  -> build/drums/<kit>-<n>-<ch>.wav, <kit>-demo.wav; exit 1 on a failed check
 *
 * Checks: every channel sounds, stays under full scale, and falls silent (its voice ends) after its decay; DIST's
 * makeup keeps the library's sounds as loud on average at every DIST as without it. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include "drum.c"
#include "kits.c"
#include "sound_lib.h"                         /* the FM-1's library: each sound on channel 1 */

#define BLOCK 256u                              /* as the chip's audio ISR */
static int32_t buf[2u * BLOCK];
static int fails;

static void wav_write(const char *path, const int16_t *s, uint32_t frames)
{
    FILE *f = fopen(path, "wb");
    uint32_t bytes = frames * 4u, v;
    if (!f) { perror(path); exit(2); }
    fwrite("RIFF", 1, 4, f); v = 36u + bytes; fwrite(&v, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f); v = 16; fwrite(&v, 4, 1, f);
    { uint16_t fmt[2] = {1, 2}; fwrite(fmt, 2, 2, f); }
    v = DRUM_FS; fwrite(&v, 4, 1, f); v = DRUM_FS * 4u; fwrite(&v, 4, 1, f);
    { uint16_t al[2] = {4, 16}; fwrite(al, 2, 2, f); }
    fwrite("data", 1, 4, f); fwrite(&bytes, 4, 1, f);
    fwrite(s, 4, frames, f);
    fclose(f);
}

static void load_kit(const drum_kit_t *k)
{
    uint32_t c;
    for (c = 0; c < DRUM_NCH; c++)
        drum_set_patch(c, &k->pat[c]);
}

static void silence(void)                       /* let every voice end */
{
    uint32_t i, on;
    do {
        memset(buf, 0, sizeof buf);
        drum_render(buf, BLOCK);
        for (on = 0, i = 0; i < DRUM_NCH; i++) on |= drum_v[i].on;
    } while (on);
}

/* render ms milliseconds into out (stereo int16), hits[t] at the frames given; returns the peak */
static int32_t render(int16_t *out, uint32_t frames, int32_t *peak_out)
{
    uint32_t f, i;
    int32_t peak = 0;
    for (f = 0; f < frames; f += BLOCK) {
        memset(buf, 0, sizeof buf);
        drum_render(buf, BLOCK);
        for (i = 0; i < 2u * BLOCK && f * 2u + i < frames * 2u; i++) {
            int32_t v = buf[i];
            if (v > peak) peak = v;
            if (-v > peak) peak = -v;
            out[f * 2u + i] = (int16_t)(v > 32767 ? 32767 : v < -32767 ? -32767 : v);
        }
    }
    if (peak_out) *peak_out = peak;
    return peak;
}

static void one_channel(const drum_kit_t *k, uint32_t kn, uint32_t c)
{
    enum { SEC = 3 };
    static int16_t s[2u * DRUM_FS * SEC];
    uint32_t half = DRUM_FS * SEC / 2u, ends;
    int32_t p1, p2;
    char path[96];
    silence();
    drum_trigger(c, 120, 0);                    /* a hard hit, then a soft one */
    render(s, half, &p1);
    ends = !drum_v[c].on;
    drum_trigger(c, 40, 0);
    render(s + 2u * half, half, &p2);
    snprintf(path, sizeof path, "build/drums/%s-%u-%s.wav", k->name, (unsigned)c + 1u, k->ch[c]);
    wav_write(path, s, 2u * half);
    printf("  %-8s %u %-3s peak %5d / %5d (hard / soft)%s\n", k->name, (unsigned)c + 1u, k->ch[c], (int)p1, (int)p2,
           ends ? "" : "  (still sounding after 1.5 s)");
    if (p1 < 1000 || p1 > 32767 || p2 > p1) { printf("    FAIL: hit level\n"); fails++; }   /* (distorted: soft = hard) */
    (void)kn;
}

/* a 4-bar groove at 120 BPM, 16th steps: rows are the channels, '-' nothing, 'x' a hit, 'X' an accent */
static void demo(const drum_kit_t *k)
{
    static const char *const ROW[DRUM_NCH] = {
        "X---x-----x-x---", "----X-------X--x", "------------X---", "x-x-x-x-x-x-x---",
        "--------------X-", "-----------x--x-", "---x------x-----", "X---------------"};
    enum { BARS = 4, STEP = DRUM_FS / 8u };      /* 120 BPM: a 16th is 1/8 s */
    static int16_t s[2u * STEP * 16u * BARS + 2u * DRUM_FS];
    uint32_t bar, st, c, total = STEP * 16u * BARS + DRUM_FS;
    int32_t peak = 0, p;
    char path[96];
    silence();
    for (bar = 0; bar < BARS; bar++)
        for (st = 0; st < 16u; st++) {
            for (c = 0; c < DRUM_NCH; c++) {
                char h = ROW[c][st];
                if (c == 7u && bar % 2u) h = '-';   /* the cymbal every other bar */
                if (h == 'x' || h == 'X') drum_trigger(c, h == 'X' ? 120 : 80, 0);
            }
            render(s + 2u * (bar * 16u + st) * STEP, STEP, &p);
            if (p > peak) peak = p;
        }
    render(s + 2u * STEP * 16u * BARS, DRUM_FS, &p);
    snprintf(path, sizeof path, "build/drums/%s-demo.wav", k->name);
    wav_write(path, s, total);
    printf("  %-8s demo peak %d, limiter gain now %d / 32768\n", k->name, (int)(peak > p ? peak : p), (int)drum_lim);
}

static void choke(void)                         /* CLASSIC: the open hat (5, group 1) stops when the closed one (4) hits */
{
    static int16_t s[2u * DRUM_FS];
    load_kit(&DRUM_KITS[0]);
    silence();
    drum_trigger(4, 110, 0);
    render(s, DRUM_FS / 10u, NULL);
    if (!drum_v[4].on) { printf("  FAIL: the open hat ended by itself in 100 ms\n"); fails++; return; }
    drum_trigger(3, 110, 0);
    render(s, DRUM_FS * 30u / 1000u, NULL);
    printf("  choke: open hat %s 30 ms after the closed one\n", drum_v[4].on ? "STILL ON" : "silent");
    if (drum_v[4].on) fails++;
}

static void cost(void)                         /* all 8 channels sounding, hit every 1/8 s, for 20 s */
{
    struct timespec a, b;
    uint32_t blocks = 0, t, c;
    double us;
    load_kit(&DRUM_KITS[0]);
    clock_gettime(CLOCK_MONOTONIC, &a);
    for (t = 0; t < 20u * DRUM_FS; t += BLOCK, blocks++) {
        if (t % (DRUM_FS / 8u) < BLOCK)
            for (c = 0; c < DRUM_NCH; c++) drum_trigger(c, 100, 0);
        memset(buf, 0, sizeof buf);
        drum_render(buf, BLOCK);
    }
    clock_gettime(CLOCK_MONOTONIC, &b);
    us = ((double)(b.tv_sec - a.tv_sec) * 1e6 + (double)(b.tv_nsec - a.tv_nsec) / 1e3) / blocks;
    printf("cost: %.1f us per %u-frame block on this computer, 8 channels busy (the chip's budget: 2900 us)\n",
           us, BLOCK);
}

static double hit_db(drum_patch_t p, uint32_t d)   /* a hard hit's first 300 ms on channel 1, its RMS in dB */
{
    uint32_t f, i, frames = DRUM_FS * 3u / 10u;
    double e = 0;
    p.p[P_DIST] = (uint8_t)d;
    drum_set_patch(0, &p);
    silence();
    drum_lim = 32768;
    drum_trigger(0, 120, 0);
    for (f = 0; f < frames; f += BLOCK) {
        memset(buf, 0, sizeof buf);
        drum_render(buf, BLOCK);
        for (i = 0; i < 2u * BLOCK; i++) e += (double)buf[i] * buf[i];
    }
    return 10.0 * log10(e / frames + 1.0);
}

static void dist_makeup_check(void)             /* DIST changes the sound, not its volume: on average within 1 dB, each
                                                 * sound within 8 (until 2026-11-12: up to 19 dB louder) */
{
    static const uint32_t D[] = {8, 32, 64, 127};
    uint32_t s, k;
    for (k = 0; k < 4u; k++) {
        double sum = 0, mn = 99, mx = -99;
        for (s = 0; s < SND_LIB_N; s++) {
            double r = hit_db(SND_LIB[s].p, D[k]) - hit_db(SND_LIB[s].p, 0);
            sum += r; if (r < mn) mn = r; if (r > mx) mx = r;
        }
        printf("  DIST %3u: the library %+.1f dB on average (%+.1f .. %+.1f)\n", (unsigned)D[k], sum / SND_LIB_N, mn, mx);
        if (fabs(sum / SND_LIB_N) > 1.0 || mn < -8.0 || mx > 8.0) { printf("    FAIL: DIST's makeup\n"); fails++; }
    }
    load_kit(&DRUM_KITS[0]);
}

static void library(void)                       /* the FM-1's library: each sound, a hard hit and a soft one, on channel 1 */
{
    enum { SEC = 3 };
    static int16_t s[2u * DRUM_FS * SEC];
    uint32_t i, half = DRUM_FS * SEC / 2u;
    int32_t p1, p2;
    char path[96];
    for (i = 0; i < SND_LIB_N; i++) {
        silence();
        drum_set_patch(0, &SND_LIB[i].p);
        drum_trigger(0, 120, 0);
        render(s, half, &p1);
        drum_trigger(0, 40, 0);
        render(s + 2u * half, half, &p2);
        snprintf(path, sizeof path, "build/drums/lib-%02u-%s-%s.wav", (unsigned)i + 1u, SND_LIB[i].name, SND_LIB[i].label);
        wav_write(path, s, 2u * half);
        printf("  lib %2u %-3s %-11s peak %5d / %5d (hard / soft)\n", (unsigned)i + 1u, SND_LIB[i].name, SND_LIB[i].label,
               (int)p1, (int)p2);
        if (p1 < 1000 || p1 > 32767 || p2 > p1) { printf("    FAIL: hit level\n"); fails++; }
    }
}

int main(void)
{
    uint32_t k, c;
    drum_init();
    for (k = 0; k < DRUM_NKITS; k++) {
        load_kit(&DRUM_KITS[k]);
        for (c = 0; c < DRUM_NCH; c++)
            one_channel(&DRUM_KITS[k], k, c);
        demo(&DRUM_KITS[k]);
    }
    choke();
    library();
    dist_makeup_check();
    cost();
    printf(fails ? "FAILED: %d checks\n" : "all checks passed\n", fails);
    return fails ? 1 : 0;
}
