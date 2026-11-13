/* SPDX-License-Identifier: GPL-3.0-only */
/* Drumtonic's drum engine: DRUM_NCH synthesized drum channels (the SMK-37 8, the FM-1 10) (an oscillator and a filtered noise each), a master limiter.
 * Integers only (docs/RULES.md); tables in firmware/gen/drum_tables.h (tools/gen_drum_tables.py).
 *
 *   drum_init()                     once, before the audio starts
 *   drum_set_patch(ch, p)           main loop: a channel's 24 knob values (0..127) -> its sound
 *   drum_trigger(ch, vel, semi)     main loop: hit channel ch, velocity 1..127, semi semitones from its pitch
 *   drum_render(out, n)             audio ISR: adds n stereo frames (Q15) to out; n a multiple of DRUM_BLK
 *
 * A patch is what the knobs show (the SMK-37: three pages of 8; the FM-1: four of 6, ui_fm1.c). The engine keeps its own copy, compiled to
 * coefficients; a trigger is a counter the ISR follows, so the main loop and the ISR never wait on each other. */
#pragma once
#include <stdint.h>

#ifndef DRUM_NCH
#define DRUM_NCH 8u                             /* the SMK-37's 8 (pads); the FM-1 has 10 (drumtonic.c) */
#endif
#define DRUM_BLK 16u                            /* frames per control step: envelopes, pitch, filters */

enum {
    /* OSC */   P_WAVE, P_FREQ, P_PMODE, P_PAMT, P_PRATE, P_OATK, P_ODEC, P_OVEL,
    /* NOISE */ P_NFILT, P_NFREQ, P_NQ, P_NENV, P_NATK, P_NDEC, P_NSTEREO, P_NVEL,
    /* MIX */   P_MIX, P_DIST, P_EQF, P_EQG, P_LEVEL, P_PAN, P_CHOKE, P_PHASE,
    DRUM_NP
};
/* the modes the knobs choose (value * 3 / 128) */
enum { WAVE_SINE, WAVE_TRI, WAVE_SAW };
enum { PMOD_DECAY, PMOD_SINE, PMOD_NOISE };
enum { NF_LP, NF_BP, NF_HP };
enum { NENV_EXP, NENV_LIN, NENV_CLAP };
#define NENV_V_EXP 21u                          /* P_NENV: up to here exponential, from NENV_V_LIN linear, between them a blend */
#define NENV_V_LIN 64u                          /* (then, past LIN's zone, the clap: saved sounds' 0, 64, 127 play as ever) */
#define NENV_V_CLAP 106u

typedef struct { uint8_t p[DRUM_NP]; } drum_patch_t;
typedef struct { char name[8]; char ch[8][4]; drum_patch_t pat[8]; } drum_kit8_t;   /* the factory kits (kits.c) */
#if DRUM_NCH == 8u
typedef drum_kit8_t drum_kit_t;
#else
typedef struct { char name[8]; char ch[DRUM_NCH][4]; drum_patch_t pat[DRUM_NCH]; } drum_kit_t;   /* (app_fm1.c kit_from8) */
#endif

static void drum_init(void);
static void drum_set_patch(uint32_t ch, const drum_patch_t *p);
static void drum_trigger(uint32_t ch, uint32_t vel, int32_t semi);
static void drum_render(int32_t *out, uint32_t n);
