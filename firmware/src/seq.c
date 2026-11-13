/* SPDX-License-Identifier: GPL-3.0-only */
/* Drumtonic's step sequencer (seq.h). Needs drum.c's drum_hit_isr. */
#include "seq.h"

#define PAIR_PER_BPM 51952u                     /* an eighth (two steps) as 2^32, per block of 16 frames, per BPM:
                                                 * 2^32 * 16 * 2 / (60 * 44100) */
#define PCT 42949673u                           /* 1 % of 2^32 */

static seq_song_t seq_song;
static volatile struct {
    uint8_t run, ext;                           /* main: playing; following a MIDI clock */
    uint8_t pat, next;                          /* ISR: the pattern playing / main: the one asked for */
    uint8_t step, frac;                         /* ISR: the step playing, how far into it (0..255) */
    uint8_t bpm, swing;                         /* main: 40..250; 50..75 (% of an eighth for the odd step) */
    uint8_t mute, solo;                         /* main: bit = channel */
    uint8_t restart;                            /* main sets, ISR clears: start at step 1 */
    uint32_t clk_in;                            /* main: MIDI clocks received */
    uint32_t clk_out;                           /* ISR: MIDI clocks due (internal clock) */
} seq = {.bpm = 120, .swing = 50};
static volatile uint8_t seq_skip[DRUM_NCH];     /* main: step + 1 not to play once (a live hit recorded ahead) */
static volatile uint16_t seq_ev[32];            /* ISR -> main: the hits played, ch << 8 | vel */
static volatile uint32_t seq_ev_w;
static uint32_t seq_ev_r, seq_clk_sent;         /* (main) */
static uint32_t seq_pp, seq_clk_seen, seq_ticks; /* (ISR) the eighth's phase; external clocks counted */
static uint8_t seq_half, seq_clkph;

/* ---- the ISR side */
static void seq_fire(uint32_t st)
{
    uint32_t ch, pat = seq.pat, mute = seq.mute, solo = seq.solo;
    seq.step = (uint8_t)st;
    for (ch = 0; ch < DRUM_NCH; ch++) {
        uint32_t v = seq_song.s[pat][ch][st], vel;
        if (seq_skip[ch] == st + 1u) { seq_skip[ch] = 0; continue; }
        if (!v || (mute >> ch & 1u) || (solo && !(solo >> ch & 1u))) continue;
        vel = v == STEP_ACC ? SEQ_VEL_ACC : SEQ_VEL;
        drum_hit_isr(ch, vel, 0);
        seq_ev[seq_ev_w % 32u] = (uint16_t)(ch << 8 | vel);
        seq_ev_w++;
    }
}

static void seq_next(void)
{
    uint32_t st = seq.step + 1u;
    if (st >= SEQ_NSTEP) {                      /* a bar ends: the asked-for pattern takes over */
        st = 0;
        seq.pat = seq.next;
    }
    seq_fire(st);
}

static void seq_block(void)                     /* DRUM_BLOCK_HOOK: every 16 frames */
{
    uint32_t old, thr, k;
    if (!seq.run) return;
    if (seq.restart) {
        seq.restart = 0;
        seq_pp = 0;
        seq_half = 0;
        seq_clkph = 0;
        seq_ticks = 0;
        seq_clk_seen = seq.clk_in;
        seq.pat = seq.next;
        seq.clk_out++;                          /* the first clock is the downbeat */
        seq_fire(0);
        return;
    }
    if (seq.ext) {                              /* 6 clocks a step */
        while (seq_clk_seen != seq.clk_in) {
            seq_clk_seen++;
            if (++seq_ticks % 6u == 0u) seq_next();
        }
        seq.frac = (uint8_t)(seq_ticks % 6u * 42u);
        return;
    }
    thr = (uint32_t)seq.swing * PCT;            /* where the odd step falls in the eighth */
    old = seq_pp;
    seq_pp += (uint32_t)seq.bpm * PAIR_PER_BPM;
    k = (uint32_t)(((uint64_t)seq_pp * 12u) >> 32);  /* MIDI clocks: 24 a quarter, 12 an eighth, straight */
    if (k != seq_clkph) {
        seq.clk_out += (k + 12u - seq_clkph) % 12u;
        seq_clkph = (uint8_t)k;
    }
    if (seq_pp < old) { seq_half = 0; seq_next(); }
    else if (!seq_half && seq_pp >= thr) { seq_half = 1; seq_next(); }
    seq.frac = (uint8_t)(seq_half ? ((seq_pp - thr) >> 16) * 255u / ((0u - thr) >> 16)
                                  : (seq_pp >> 16) * 255u / ((thr >> 16) | 1u));
}

/* ---- the main loop side */
static void seq_play(void)
{
    seq.restart = 1;
    seq.run = 1;
}
static void seq_stop(void) { seq.run = 0; }

static void seq_pattern(uint32_t p)
{
    seq.next = (uint8_t)(p % SEQ_NPAT);
    if (!seq.run) seq.pat = seq.next;
}

static void seq_rec(uint32_t ch, uint32_t vel)
{
    uint32_t st = seq.step, pat = seq.pat;
    ch %= DRUM_NCH;
    if (seq.frac >= 128u) {                     /* nearer the next step: that one, and it must not sound again */
        st = (st + 1u) % SEQ_NSTEP;
        seq_skip[ch] = (uint8_t)(st + 1u);
    }
    seq_song.s[pat][ch][st] = vel > 110u ? STEP_ACC : STEP_ON;
}

static int32_t seq_out_take(void)
{
    if (seq_ev_r == seq_ev_w) return -1;
    if (seq_ev_w - seq_ev_r > 32u) seq_ev_r = seq_ev_w - 32u;   /* (fell behind: the newest 32) */
    return seq_ev[seq_ev_r++ % 32u];
}

static uint32_t seq_clocks_due(void)
{
    uint32_t n = seq.clk_out - seq_clk_sent;
    seq_clk_sent = seq.clk_out;
    return n;
}
