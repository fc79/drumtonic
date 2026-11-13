/* SPDX-License-Identifier: GPL-3.0-only */
/* Drumtonic's SMK-37 panel (drumtonic.c includes it for BOARD_SMK37).
 *
 *   pads       STEP mode (SEQ lit): the 16 steps of the channel shown (top row 1-8, bottom 9-16): off, on, accent
 *              PLAY mode: the bottom row plays channels 1-8, the top row picks pattern A-H (at the bar's end)
 *   K1..K8     the 8 knobs of the page shown (OSC, NOISE, MIX) for the channel shown
 *   keys       the lowest 8 play and pick channels 1-8; the others play the channel shown, chromatic (C4 = as set)
 *   < >        the channel          KNOB BANK   the page
 *   FADER BANK the pads' sensitivity (5 curves, gen/pad_curves.h: 1 as read, the default .. 5 light hits loudest)
 *   faders     tempo, swing, the channel's level, the volume (each acts once it moves) */
#include "pad_curves.h"

enum { PAGE_OSC, PAGE_NOISE, PAGE_MIX, NPAGE };
#define NKNOB 8u
#define PANEL_NBTN 18u
#define PANEL_NSENS PAD_NCURVE
#define PANEL_PASTE_HINT "COPY ONE FIRST (FX)"
static const char *const PAGE_NAME[NPAGE] = {"OSC", "NOISE", "MIX"};
static const uint8_t PAGE_PARAM[NPAGE][NKNOB] = {   /* the knobs as the patch: page p, knob k = parameter 8 p + k */
    {P_WAVE, P_FREQ, P_PMODE, P_PAMT, P_PRATE, P_OATK, P_ODEC, P_OVEL},
    {P_NFILT, P_NFREQ, P_NQ, P_NENV, P_NATK, P_NDEC, P_NSTEREO, P_NVEL},
    {P_MIX, P_DIST, P_EQF, P_EQG, P_LEVEL, P_PAN, P_CHOKE, P_PHASE}};

static uint8_t fader_seen[4];
static uint32_t keys_prev;

static void panel_init(void)
{
    uint32_t k;
    smk_pad_events = 1;                         /* the pads as events (smk_pad_take) */
    for (k = 0; k < 4u; k++) fader_seen[k] = fm1_in.fader[k];
}

static uint32_t pad_vel(uint32_t v)             /* the sensitivity: one of 5 curves, from as read to light hits loud */
{
    return PAD_CURVE[set.sens][v > 127u ? 127u : v ? v : 1u];
}

static void key(uint32_t k, uint32_t vel)       /* keybed key k (0 = the lowest C) pressed */
{
    if (k < DRUM_NCH) { set.ch = (uint8_t)k; hit(k, vel, 0, 0); }
    else hit(set.ch, vel, (int32_t)k - 12, 0);  /* C4 (key 12) plays the channel as set */
}

/* the pads' colours: brightness 0..255 is very bright, so a quarter at most */
static void panel_leds(void)
{
    uint32_t k;
    for (k = 0; k < 16u; k++) {
        const uint8_t *c;
        uint32_t r = 0, g = 0, b = 0, d = 0;
        if (set.mode == MODE_STEP) {
            uint32_t v = seq_song.s[seq.pat][set.ch][k];
            c = CH_RGB[set.ch];
            d = v == STEP_ACC ? 3u : v ? 5u : 0u;
            if (d) { r = c[0] >> d; g = c[1] >> d; b = c[2] >> d; }
            if (seq.run && seq.step == k) { r = v ? 90 : 18; g = v ? 90 : 18; b = v ? 90 : 18; }
        } else if (k >= 8u) {                    /* bottom row: the channels, bright for 80 ms after a hit */
            uint32_t ch = k - 8u;
            c = CH_RGB[ch];
            d = fm1_ms - hit_ms[ch] < 80u ? 2u : (seq.mute >> ch & 1u) ? 9u : 6u;
            r = c[0] >> d; g = c[1] >> d; b = c[2] >> d;
        } else {                                /* top row: the patterns */
            if (k == seq.pat) r = g = b = 60;
            else if (k == seq.next && fm1_ms % 400u < 200u) r = g = b = 60;   /* (waiting for the bar's end) */
            else if (pat_used(k)) r = g = b = 8;
        }
        padled_set(k, (uint8_t)r, (uint8_t)g, (uint8_t)b);
    }
    padled_flush();
}

static void panel_input(void)
{
    uint32_t keys = fm1_in.notes, k;
    int32_t e;

    /* the keys: the 27 in the middle as a bitmap, the outer ones as events */
    for (k = 0; k < 27u; k++)
        if ((keys & ~keys_prev) >> k & 1u) key(SMK_KEY_F3 + k, fm1_in.key_vel[SMK_KEY_F3 + k]);
    keys_prev = keys;
    while ((e = smk_extkey_take()) >= 0)
        if (e & 0x7F) key((uint32_t)e >> 8, (uint32_t)e & 0x7Fu);

    /* the pads */
    while ((e = smk_pad_take()) >= 0) {
        uint32_t pad = ((uint32_t)e >> 8) & 15u, vel = (uint32_t)e & 0x7Fu;
        if (!vel) continue;
        if (set.mode == MODE_STEP) step_tap(pad);
        else if (pad >= 8u) { set.ch = (uint8_t)(pad - 8u); hit(pad - 8u, pad_vel(vel), 0, 0); }
        else pattern_pick(pad);
    }

    /* the knobs */
    for (k = 0; k < NKNOB; k++)
        if ((e = fm1_enc_take(k)) != 0) knob(k, e);

    /* the faders: tempo, swing, the channel's level, the volume; each acts only once it has moved */
    for (k = 0; k < 4u; k++) {
        uint32_t f = fm1_in.fader[k];
        int32_t d = (int32_t)f - (int32_t)fader_seen[k];
        if (d < 2 && d > -2) continue;
        fader_seen[k] = (uint8_t)f;
        if (k == 0u) { set.bpm = (uint8_t)(60u + f * 140u / 127u); seq.bpm = set.bpm; }
        else if (k == 1u) { set.swing = (uint8_t)(50u + f * 25u / 127u); seq.swing = set.swing; }
        else if (k == 2u) { kit.pat[set.ch].p[P_LEVEL] = (uint8_t)f; drum_set_patch(set.ch, &kit.pat[set.ch]); kit_edited = 1; }
#ifndef DINGHY_HOST
        else smk_master = (int32_t)(f * 1023u / 127u);
#endif
        touched();
    }
}

static void panel_buttons(uint32_t b)           /* the SMK's four more: < > KNOB BANK FADER BANK */
{
    if (b >> BTN_LEFT & 1u) set.ch = (uint8_t)((set.ch + DRUM_NCH - 1u) % DRUM_NCH);
    if (b >> BTN_RIGHT & 1u) set.ch = (uint8_t)((set.ch + 1u) % DRUM_NCH);
    if (b >> BTN_KNOB_BANK & 1u) set.page = (uint8_t)((set.page + 1u) % NPAGE);
    if (b >> BTN_FADER_BANK & 1u) {
        char s[24];
        set.sens = (uint8_t)((set.sens + 1u) % PANEL_NSENS);
        catn(cat(catn(cat(s, "PAD SENSITIVITY "), set.sens + 1), "/"), PANEL_NSENS);
        say(s);
    }
}
