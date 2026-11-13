/* SPDX-License-Identifier: GPL-3.0-only */
/* Drumtonic's step sequencer: 8 patterns (A..H) of 8 channels x 16 steps, each step off, on or accented.
 * The clock runs in the audio ISR (DRUM_BLOCK_HOOK -> seq_block), so a hit lands within 16 frames (0.36 ms) of its
 * time; swing delays the odd steps. With an external MIDI clock, 6 clocks are a step (no swing).
 *
 *   seq_play(), seq_stop()          main loop: start from step 1 of the current pattern, stop
 *   seq_pattern(p)                  main loop: the next pattern; while playing it takes over at the end of a bar
 *   seq_rec(ch, vel)                main loop: a live hit into the playing pattern, on the nearest step
 *   seq_out_take()                  main loop: the next hit the sequencer played (ch << 8 | vel), -1 = none
 *   seq_clocks_due()                main loop: MIDI clocks to send since the last call (internal clock)
 *
 * seq_song is written by the main loop (one byte a step) and read by the ISR; seq's fields have one writer each. */
#pragma once
#include <stdint.h>
#include "drum.h"

#define SEQ_NPAT 8u
#define SEQ_NSTEP 16u
#define SEQ_VEL 96u                             /* a step's velocity, and an accented one's */
#define SEQ_VEL_ACC 127u
enum { STEP_OFF, STEP_ON, STEP_ACC };

typedef struct { uint8_t s[SEQ_NPAT][DRUM_NCH][SEQ_NSTEP]; } seq_song_t;

static void seq_block(void);
static void seq_play(void);
static void seq_stop(void);
static void seq_pattern(uint32_t p);
static void seq_rec(uint32_t ch, uint32_t vel);
static int32_t seq_out_take(void);
static uint32_t seq_clocks_due(void);
