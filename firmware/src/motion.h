/* SPDX-License-Identifier: GPL-3.0-only */
/* What a pattern holds besides its steps, on the FM-1:
 *
 *   lanes   120 for all the patterns (176 until 2026-11-23, with the marks in them). A lane is one value per step for
 *           one parameter of one channel in one pattern: a knob's recorded motion or its p-locks (params below
 *           P_COND), or a ratchet's RAMP (P_RAMP, since 2026-11-24). MOT_NONE: the knob's own value (P_RAMP: FLAT,
 *           each hit at the step's velocity).
 *   marks   NMARK for all the patterns: a step's condition (P_COND), micro timing (P_MICRO) and ratchet (P_RATCH),
 *           one word a step that has any (motion.c; until 2026-11-23 each a lane of 64 steps; until 2026-11-24 its
 *           RAMP too). mot_get / mot_set take them as the lanes did: MOT_NONE no condition (always), no micro timing
 *           (on the step), no ratchet (one hit).
 *   LFOs    8 a pattern, each moving one parameter of one channel around the value it has.
 *
 * All are saved with the patterns (app_fm1.c). A condition: 1..100 a chance in %, COND_CYC + k the cycle k (1:2, 2:2,
 * 1:3 .. 8:8), COND_FILL / COND_NFILL (plays only while SEL is held / only while it is not); MOT_NONE: always. */
#pragma once
#include <stdint.h>
#include "drum.h"

#define NLANE 120u
#define NMARK 896u                              /* marks in use at most: 7/8 of the table's MARK_NSLOT (short
                                                 * searches) */
#define MARK_NSLOT 1024u
#define NLFO 8u
#define MOT_NONE 0xFFu
#define MOT_GAP 4u                              /* recording: a pause up to this many steps is filled in */
enum {                                          /* a lane's or an LFO's parameter: the patch's 0..DRUM_NP-1, then these */
    P_REV = DRUM_NP, P_DLY,                     /* the channel's sends */
    P_COND,                                     /* (marks only) the steps' conditions */
    P_MICRO,                                    /* (marks only) the steps' micro timing: MICRO0 + 24ths of a step */
    P_RATCH,                                    /* (marks only) the steps' ratchets: 2..8 hits in the step (1..4 until
                                                 * 2026-11-23) */
    P_RAMP,                                     /* a ratchet's velocities, RAMP0 + -100..+100 (none: FLAT); only on a step
                                                 * of 2 hits or more. A lane, not a mark (2026-11-24: 201 values do not fit
                                                 * a mark's word), the only one past P_COND: never in mot_ov, never glided,
                                                 * never a lock (motion.c) */
    P_NTARGET
};
#define RATCH_MAX 8u
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
static uint32_t ratch_of(uint32_t v) { return v >= 2u && v <= RATCH_MAX ? v : 1u; }   /* a P_RATCH value: hits in the
                                                 * step */
#define RAMP_MAX 100                            /* P_RAMP (2026-11-24, the user: without steps): DOWN 100 % .. FLAT ..
                                                 * UP 100 %, -100 .. +100. DOWN n %: the first hit at the step's velocity
                                                 * v, the last at v - (v - 1) n / 100 (100 %: 1), a straight line between;
                                                 * UP n %: the reverse (seq_fm1.c seq_ramp_vel) */
#define RAMP0 100u                              /* a P_RAMP lane's value: RAMP0 + the RAMP, 0..200 (FLAT: MOT_NONE) */
static int32_t ramp_of(uint32_t v) { return v <= 2u * RAMP_MAX ? (int32_t)v - RAMP_MAX : 0; }   /* a P_RAMP value:
                                                 * -100 .. +100 (MOT_NONE, or past 200: FLAT, 0) */
static uint32_t ramp_val(int32_t r)             /* a RAMP as a P_RAMP lane holds it */
{
    r = r < -RAMP_MAX ? -RAMP_MAX : r > RAMP_MAX ? RAMP_MAX : r;
    return r ? (uint32_t)((int32_t)RAMP0 + r) : MOT_NONE;
}
static int32_t ramp_from6(uint32_t code)        /* an SNG6 record's RAMP (its marks': FLAT 0, DOWN 1, UP 2, DOWN+ 3, UP+ 4;
                                                 * DOWN went to half the velocity, DOWN+ to 1) */
{
    static const int8_t R[5] = {0, -50, 50, -100, 100};
    return code < 5u ? R[code] : 0;
}
/* The conditions' line, as the knob (ALGORITHM, a step held) goes along it, one place a click: 0 none (100 %), +1..+19
 * the chances 95..5 %, -1 FILL, -2 NOT FILL (near: live), -3..-37 the cycles 1:2 .. 8:8 (ui_fm1.c until 2026-11-23; a
 * mark keeps its condition as its place, motion.c) */
#define COND_POS_MIN (-37)
#define COND_POS_MAX 19
static uint32_t cond_pos_val(int32_t pos)       /* a place's condition (MOT_NONE: none) */
{
    if (!pos) return MOT_NONE;
    if (pos > 0) return 100u - 5u * (uint32_t)(pos > COND_POS_MAX ? COND_POS_MAX : pos);
    if (pos >= -2) return pos == -1 ? COND_FILL : COND_NFILL;
    return COND_CYC + (uint32_t)(-(pos < COND_POS_MIN ? COND_POS_MIN : pos) - 3);
}
static int32_t cond_val_pos(uint32_t c)         /* a condition's place; one off the line (none is made: a chance not a
                                                 * multiple of 5, 0 %, past NOT FILL) the nearest (a chance under 5 %:
                                                 * 5 %; past NOT FILL: none) */
{
    if (c == MOT_NONE || c == 100u || c > COND_NFILL) return 0;
    if (c < 100u) { int32_t p = (int32_t)(100u - c + 2u) / 5; return p > COND_POS_MAX ? COND_POS_MAX : p; }
    if (c >= COND_FILL) return c == COND_FILL ? -1 : -2;
    return -(int32_t)(c - COND_CYC) - 3;
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
