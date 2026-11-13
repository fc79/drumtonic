/* SPDX-License-Identifier: GPL-3.0-only */
/* What a pattern holds besides its steps, on the FM-1:
 *
 *   lanes   160 for all the patterns. A lane is one value per step for one parameter of one channel in one pattern:
 *           a knob's recorded motion, its p-locks, (P_COND) the steps' conditions, (P_MICRO) their micro timing or
 *           (P_RATCH) their ratchets.
 *           MOT_NONE: the knob's own value (no condition: always; no micro timing: on the step).
 *   LFOs    8 a pattern, each moving one parameter of one channel around the value it has.
 *
 * Both are saved with the patterns (app_fm1.c). A condition: 1..100 a chance in %, COND_CYC + k the cycle k (1:2, 2:2,
 * 1:3 .. 8:8), COND_FILL / COND_NFILL (plays only while SEL is held / only while it is not); MOT_NONE: always. */
#pragma once
#include <stdint.h>
#include "drum.h"

#define NLANE 160u
#define NLFO 8u
#define MOT_NONE 0xFFu
#define MOT_GAP 4u                              /* recording: a pause up to this many steps is filled in */
enum {                                          /* a lane's or an LFO's parameter: the patch's 0..DRUM_NP-1, then these */
    P_REV = DRUM_NP, P_DLY,                     /* the channel's sends */
    P_COND,                                     /* (lanes only) the steps' conditions */
    P_MICRO,                                    /* (lanes only) the steps' micro timing: MICRO0 + 24ths of a step */
    P_RATCH,                                    /* (lanes only) the steps' ratchets: 2..4 hits in the step */
    P_NTARGET
};
#define COND_CYC 101u                           /* 101..135: the cycles a:b, b = 2..8, a = 1..b */
#define COND_NCYC 35u
#define COND_FILL (COND_CYC + COND_NCYC)
#define COND_NFILL (COND_FILL + 1u)
#define MICRO0 128u                             /* P_MICRO: MICRO0 - 23 .. MICRO0 + 23 = 23/24 of a step early .. late (as
                                                 * Elektron's 1/384 of a bar: 1/24 of a 16th) */
#define MICRO_MAX 23
static int32_t micro_of(uint32_t v)             /* a P_MICRO value in 24ths; below 16, a first build's fifths (5 +- 4) */
{
    if (v == MOT_NONE) return 0;
    if (v < 16u) return ((int32_t)v - 5) * 24 / 5;
    return (int32_t)v - (int32_t)MICRO0;
}

typedef struct {
    uint8_t used, gen, pat, ch, param, rsv[3];  /* used is set last; gen tells the ISR a lane was reused */
    uint8_t val[64];                            /* (SEQ_NSTEP) */
} lane_t;                                       /* 72 B */
typedef struct {
    uint8_t used, ch, param, rate;              /* rate: LFO_NRATE in time, or in Hz (sync 0) */
    uint8_t shape, sync;                        /* LFO_TRI .. LFO_SH; 1 in time */
    int8_t depth;                               /* -100..+100 % (100 % = +-64) */
    uint8_t rsv;
} lfo_t;                                        /* 8 B */
enum { LFO_TRI, LFO_SAW, LFO_SQR, LFO_SH, LFO_NSHAPE };
#define LFO_NRATE 10u
