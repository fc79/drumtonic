/* SPDX-License-Identifier: GPL-3.0-only */
/* PERF_TEST=1, a development build (never a release; its version ends in -PERF): where the audio's time goes.
 *
 * Each render (256 frames, 5.8 ms) is cut into laps: PF_LAP(k) gives part k the time since the last lap, so the
 * parts on core 0 add up to the render. Core 1 stamps its own job (PF_C1_BEGIN / PF_C1_END) and core 0 takes that
 * figure only after cpu2_wait has returned (core 1 writes it before it says done). Once a second (172 renders) the
 * ISR publishes, in microseconds, each part's mean and its value in the longest render of that second, into the
 * buffer the screen is not reading (pf_seq says which). SETTINGS > CPU, SEL: the page (ui_fm1.c).
 *
 * Without PERF_TEST every macro is empty, and the tree builds the release byte for byte. On the host the clock
 * reads 0: the page shows zeros (PERF_DEMO fills it with made-up figures, for a picture of the page). */
#pragma once

#if PERF_TEST
enum {
    PF_SEQ, PF_HITS, PF_V0, PF_VW, PF_V1, PF_BLIM,      /* page 1: the sequencer and motion, hits, voices, the block limiter */
    PF_FXIN, PF_DLY, PF_RVW, PF_RV1, PF_MAST, PF_LIM,   /* page 2: the effects */
    PF_OTHER, PF_TOT, PF_N
};
#ifdef DINGHY_HOST
#define PF_NOW() 0u
#ifndef FM1_TICKS_PER_US
#define FM1_TICKS_PER_US 24u
#endif
#else
#define PF_NOW() fm1_ticks()                    /* TIMER4, 24 MHz (an inline load): core 0 and core 1 read the same */
#endif
#define PF_WIN 172u                             /* renders a window: a second */
static uint32_t pf_t0, pf_t, pf_cur[PF_N], pf_sum[PF_N], pf_pk[PF_N], pf_n;   /* (the ISR's) */
static volatile uint32_t pf_c1_ticks;           /* core 1: its last job (written by core 1 only) */
static volatile uint32_t pf_pub[2][2][PF_N];    /* [buffer][mean, peak][part], microseconds */
static volatile uint32_t pf_seq;                /* windows published: the screen reads pf_pub[pf_seq & 1] */

static inline void pf_start(void)
{
    uint32_t k;
    pf_t0 = pf_t = PF_NOW();
    for (k = 0; k < PF_N; k++) pf_cur[k] = 0;
}
static inline uint32_t pf_lap(uint32_t k)
{
    uint32_t t = PF_NOW();
    pf_cur[k] += t - pf_t;
    pf_t = t;
    return 0;
}
static void pf_end(void)
{
    uint32_t k;
    pf_lap(PF_OTHER);
    pf_cur[PF_TOT] = pf_t - pf_t0;
    for (k = 0; k < PF_N; k++) pf_sum[k] += pf_cur[k];
    if (pf_cur[PF_TOT] >= pf_pk[PF_TOT])
        for (k = 0; k < PF_N; k++) pf_pk[k] = pf_cur[k];
    if (++pf_n >= PF_WIN) {
        volatile uint32_t (*b)[PF_N] = pf_pub[(pf_seq + 1u) & 1u];
        for (k = 0; k < PF_N; k++) {
            b[0][k] = pf_sum[k] / (pf_n * FM1_TICKS_PER_US);
            b[1][k] = pf_pk[k] / FM1_TICKS_PER_US;
            pf_sum[k] = pf_pk[k] = 0;
        }
        pf_n = 0;
        pf_seq++;
    }
}
#ifdef PERF_DEMO                                /* (host: made-up figures, for a picture of the page) */
static void pf_demo(void)
{
    static const uint32_t M[PF_N] = {45, 12, 1180, 140, 1150, 60, 70, 900, 1950, 2650, 110, 260, 2, 4520};
    static const uint32_t P[PF_N] = {130, 64, 1720, 410, 1690, 61, 71, 960, 2140, 2790, 112, 262, 3, 4870};
    uint32_t k;
    for (k = 0; k < PF_N; k++) { pf_pub[pf_seq & 1u][0][k] = M[k]; pf_pub[pf_seq & 1u][1][k] = P[k]; }
}
#endif
#define PF_START() pf_start()
#define PF_LAP(k) ((void)pf_lap(k))
#define PF_END() pf_end()
static int pf_w;
#define PF_WAIT(x, before, during) (pf_lap(before), pf_w = (x), pf_lap(during), pf_w)   /* (x: cpu2_wait(), 0 or -1) */
#define PF_C1(k) (pf_cur[k] += pf_c1_ticks)
#define PF_C1_BEGIN() uint32_t pf_c1_0 = PF_NOW()
#define PF_C1_END() (pf_c1_ticks = PF_NOW() - pf_c1_0)
#else
#define PF_START() ((void)0)
#define PF_LAP(k) ((void)0)
#define PF_END() ((void)0)
#define PF_WAIT(x, before, during) (x)
#define PF_C1(k) ((void)0)
#define PF_C1_BEGIN() ((void)0)
#define PF_C1_END() ((void)0)
#endif
