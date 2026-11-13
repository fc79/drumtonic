/* SPDX-License-Identifier: GPL-3.0-only */
/* The FM-1's effects and master, as the knobs see them (0..max each, kept with the sound: app_fm1.c snd_x): the reverb
 * and the delay every channel sends to, then the master. FX's pages are REVERB, DELAY, DELAY 2, MASTER, COMP
 * (ui_fm1.c). The order is the sound's stored layout: RETURNs, COMP and MAKEUP stay in it, unused. */
#pragma once
#include <stdint.h>

enum {
    FX_RV_DECAY, FX_RV_TONE, FX_RV_HPF, FX_RV_LEVEL,         /* REVERB */
    FX_DL_TIME, FX_DL_FDBK, FX_DL_TONE, FX_DL_TAPE,          /* DELAY (TAPE 0: DIGI, a clean 12-bit delay) */
    FX_DL_HPF, FX_DL_LEVEL, FX_DL_PING,                      /* DELAY 2 */
    FX_M_DRIVE, FX_M_COMP, FX_M_SCHP, FX_M_FILTER,           /* MASTER (COMP: one knob; FILTER 64 = off; SCHP: the compressor's detector, in the old PUMP's place) */
    FX_M_THRESH, FX_M_RATIO, FX_M_ATTACK, FX_M_RELEASE,      /* MORE: the compressor */
    FX_M_MAKEUP, FX_M_MIX, FX_M_RESO, FX_M_LIMIT,            /* MORE 2 */
    FX_NP
};
typedef struct { const char *name; uint8_t max, def; const char *const *names; } fx_param_t;
static const char *const FX_DIV_N[13] = {"1/32", "1/16T", "1/16", "1/8T", "1/16.", "1/8", "1/4T", "1/8.", "1/4",
                                         "1/2T", "1/4.", "1/2", "1/2."};
static const char *const FX_RATIO_N[10] = {"1:1", "1.5:1", "2:1", "3:1", "4:1", "6:1", "8:1", "12:1", "20:1", "INF"};
static const char *const FX_ONOFF_N[2] = {"OFF", "ON"};
static const char *const FX_SCHP_N[4] = {"OFF", "60 Hz", "90 Hz", "150 Hz"};
static const char *const FX_LIMIT_N[3] = {"OFF", "LIM", "CLIP"};   /* (unused: the limiter is always on, no knob; kept for the stored layout) */
static const fx_param_t FX_P[FX_NP] = {
    [FX_RV_DECAY] = {"DECAY", 127, 73, 0},   [FX_RV_TONE] = {"TONE", 127, 57, 0},
    [FX_RV_HPF] = {"LOW CUT", 127, 62, 0},   [FX_RV_LEVEL] = {"RETURN", 127, 85, 0},
    [FX_DL_TIME] = {"TIME", 12, 7, FX_DIV_N}, [FX_DL_FDBK] = {"FDBK", 127, 52, 0},
    [FX_DL_TONE] = {"TONE", 127, 51, 0},     [FX_DL_TAPE] = {"TAPE", 127, 40, 0},
    [FX_DL_HPF] = {"LOW CUT", 127, 62, 0},   [FX_DL_LEVEL] = {"RETURN", 127, 85, 0},
    [FX_DL_PING] = {"PING", 1, 0, FX_ONOFF_N},
    [FX_M_DRIVE] = {"DRIVE", 127, 0, 0},     [FX_M_COMP] = {"COMP", 127, 0, 0},
    [FX_M_SCHP] = {"SC HPF", 3, 0, FX_SCHP_N},     [FX_M_FILTER] = {"FILTER", 127, 64, 0},
    [FX_M_THRESH] = {"THRESH", 127, 95, 0},  [FX_M_RATIO] = {"RATIO", 9, 0, FX_RATIO_N},
    [FX_M_ATTACK] = {"ATTACK", 127, 60, 0},  [FX_M_RELEASE] = {"RELEASE", 127, 60, 0},
    [FX_M_MAKEUP] = {"MAKEUP", 127, 0, 0},   [FX_M_MIX] = {"MIX", 127, 127, 0},
    [FX_M_RESO] = {"RESO", 127, 20, 0},      [FX_M_LIMIT] = {"LIMIT", 2, 1, FX_LIMIT_N},
};
