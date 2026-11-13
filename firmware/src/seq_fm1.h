/* SPDX-License-Identifier: GPL-3.0-only */
/* Drumtonic's step sequencer on the FM-1: 32 patterns of 10 channels, up to 64 steps each, with the pattern's length,
 * rate and swing. A step is a bit: on, and accented. The clock
 * runs in the audio ISR (DRUM_BLOCK_HOOK -> seq_block), as on the SMK-37 (seq.c); with an external MIDI clock a step
 * is 6, 4, 3 or 8 clocks (1/16, 1/16T, 1/32, 1/8T), with no swing.
 *
 *   seq_play(), seq_stop()          main loop: start from step 1 of the current pattern, stop
 *   seq_pattern(p)                  main loop: the next pattern; while playing it takes over when this one ends
 *   seq_chain(list, n)              main loop: play these patterns in turn (n < 2: no chain)
 *   seq_rec(ch, vel)                main loop: a live hit into the playing pattern, on the nearest step
 *   seq_out_take()                  main loop: the next hit the sequencer played (ch << 8 | vel), -1 = none
 *   seq_clocks_due()                main loop: MIDI clocks to send since the last call (internal clock)
 *   seq_get(), seq_set()            a step: 0 off, STEP_ON, STEP_ACC
 *
 * seq_song is written by the main loop (whole words) and read by the ISR; seq's fields have one writer each.
 * SEQ_STEP_HOOK(pat, step) runs before a step's hits, SEQ_COND(pat, ch, step) says whether a hit plays (motion.c). */
#pragma once
#include <stdint.h>
#include "drum.h"

#define SEQ_NPAT 32u
#define SEQ_NSTEP 64u
#define SEQ_NCHAIN 16u
#define SEQ_VEL 96u                             /* a step's velocity, and an accented one's */
#define SEQ_VEL_ACC 127u
enum { STEP_OFF, STEP_ON, STEP_ACC };
enum { RATE_16, RATE_16T, RATE_32, RATE_8T, SEQ_NRATE };

typedef struct { uint32_t hit[2], acc[2]; } seq_track_t;     /* bit s of word s / 32: step s */
typedef struct {
    seq_track_t t[DRUM_NCH];
    uint8_t len, rate, swing, kit;              /* the longest part's steps; RATE_*; 50..75 (% of an eighth for the odd step);
                                                 * its kit + 1, 0 none (app_fm1.c; until 2026-11-12 a spare byte, 0) */
    uint8_t tlen[DRUM_NCH], rsv2[2];            /* each part's 1..64 steps: they loop on their own, a pattern ends with len */
} seq_pat_t;                                    /* 176 B */
typedef struct { seq_pat_t p[SEQ_NPAT]; } seq_song_t;

static void seq_block(void);
static void seq_play(void);
static void seq_stop(void);
static void seq_pattern(uint32_t p);
static void seq_chain(const uint8_t *list, uint32_t n);
static void seq_rec(uint32_t ch, uint32_t vel);
static int32_t seq_out_take(void);
static uint32_t seq_clocks_due(void);
static uint32_t seq_get(uint32_t pat, uint32_t ch, uint32_t st);
static void seq_set(uint32_t pat, uint32_t ch, uint32_t st, uint32_t v);
