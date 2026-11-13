/* SPDX-License-Identifier: GPL-3.0-only */
/* Drumtonic: a drum machine for the M-Vave SMK-37 (Pro, Elite, MKE-P37) and the M-Vave FM-1. 8 synthesized channels
 * (the FM-1: 10, with MT and HT; drum.c) and a step sequencer, on Dinghy's platform; dinghy.c includes this file.
 *
 * What both boards share is here: the sound, the kit, the text, the hits by hand, MIDI in and the transport, the
 * splash. Each board has its own app (app_smk.c, app_fm1.c): its sequencer, its settings, its screen, its controls
 * and its main loop. The controls: docs/DRUMTONIC.md.
 *
 *   MIDI   in: notes 36-43 play channels 1-8 (the FM-1: 36-45, 1-10), clock, start, stop (external clock);
 *          out: the hits on channel 10, clock, start, stop (internal clock) */
#define APP_NAME "DRUMTONIC"
#define STORE_MAGIC 0x3733544Eu                 /* "NT37": settings and blobs (store.h; the name before Drumtonic: kept,
                                                 * so what a unit saved still loads) */
#if BOARD_SMK37
#define CV_MAX (240u * 112u)                    /* the parameter panel in one canvas */
#else
#define CV_MAX (240u * 148u)                    /* the screen's middle band in one canvas (ui_fm1.c) */
#endif
#if BOARD_SMK37
#define SMK_MASTER_FROM_APP 1                   /* K1 is a parameter knob; the volume is fader 4 (hal/smk_input.h) */
#endif
static void app_idle(void);
#define LCD_IDLE app_idle
#include "platform.c"

static void seq_block(void);
#define DRUM_BLOCK_HOOK seq_block               /* the sequencer's clock, in the audio ISR */
#if !BOARD_SMK37
#define DRUM_NCH 10u                            /* BD SD CP CH OH LT MT HT RS CY: the factory kits' 8 and two toms */
#define DRUM_SENDS 1                            /* the reverb's and the delay's buses (fx.c) */
#define DRUM_CPU2 1                             /* half the channels and the reverb on the second core (platform.c) */
#endif
#include "drum.c"
#include "kits.c"
#if BOARD_SMK37
#include "seq.c"
#else
static void mot_step(uint32_t pat, uint32_t st);
static uint32_t mot_cond(uint32_t pat, uint32_t ch, uint32_t st);
static void mot_tick(void);
static void fx_render(int32_t *out, uint32_t n);
#define SEQ_STEP_HOOK(pat, st) mot_step(pat, st)   /* the lanes, the conditions: motion.c */
#define SEQ_COND(pat, ch, st) mot_cond(pat, ch, st)
static uint32_t mot_cond_at(uint32_t pat, uint32_t ch, uint32_t st);
static int32_t mot_micro(uint32_t pat, uint32_t ch, uint32_t st);
static void mot_lock_ch(uint32_t pat, uint32_t st, uint32_t ch);
static uint32_t mot_ratch(uint32_t pat, uint32_t ch, uint32_t st);
#define SEQ_RATCH(pat, ch, st) mot_ratch(pat, ch, st)
#define SEQ_COND_AT(pat, ch, st) mot_cond_at(pat, ch, st)   /* (micro timing: motion.c) */
#define SEQ_MICRO(pat, ch, st) mot_micro(pat, ch, st)
#define SEQ_LOCK_CH(pat, st, ch) mot_lock_ch(pat, st, ch)
#include "seq_fm1.c"                            /* (32 patterns up to 64 steps: app_fm1.c) */
#endif

/* ---- the sound */
static volatile uint32_t cpu_us_max;            /* the longest render since the screen last showed it */
static void app_audio(int32_t *out, uint32_t n)
{
#ifndef DINGHY_HOST
    uint32_t t0 = fm1_ticks(), us;
#endif
#if !BOARD_SMK37
    seq_at_render();                            /* (the time line: this render's first frame, now) */
    mot_tick();                                 /* the lanes' glides, the LFOs, the kit's changes (motion.c) */
#endif
    drum_render(out, n);
#if !BOARD_SMK37
    fx_render(out, n);                          /* the reverb, the delay, the master (fx.c) */
#endif
#ifndef DINGHY_HOST
    us = (fm1_ticks() - t0) / FM1_TICKS_PER_US;
    if (us > cpu_us_max) cpu_us_max = us;
#endif
}

/* ---- the kit playing, and what every board keeps */
static drum_kit_t kit;                          /* the kit playing, as edited */
static uint8_t kit_edited, rec;

static const char *const PARAM_NAME[DRUM_NP] = {
    "WAVE", "PITCH", "MOD", "AMT", "RATE", "ATK", "DEC", "VEL",
    "FILT", "FREQ", "Q", "ENV", "ATK", "DEC", "STER", "VEL",
    "MIX", "DIST", "EQ F", "EQ G", "LEVEL", "PAN", "CHOKE", "CLICK"};
static const uint8_t CH_RGB[DRUM_NCH][3] = {     /* each channel's colour, on the screen (and the SMK's pads) */
    {255, 40, 20}, {255, 140, 0}, {240, 220, 0}, {40, 220, 60}, {0, 200, 220}, {40, 90, 255},
#if DRUM_NCH > 8u
    {80, 140, 255}, {130, 185, 255},            /* MT, HT: the toms, LT's blue lighter and lighter */
#endif
    {170, 60, 255}, {255, 60, 170}};

static void kit_apply(void)
{
    uint32_t c;
    for (c = 0; c < DRUM_NCH; c++)
        drum_set_patch(c, &kit.pat[c]);
}
/* ---- text */
static char *cat(char *p, const char *s) { while (*s) *p++ = *s++; *p = 0; return p; }
static char *catn(char *p, int32_t v)
{
    char b[12];
    uint32_t n = 0, u = (uint32_t)(v < 0 ? -v : v);
    do { b[n++] = (char)('0' + u % 10u); u /= 10u; } while (u);
    if (v < 0) *p++ = '-';
    while (n) *p++ = b[--n];
    *p = 0;
    return p;
}

static char msg[32];
static uint32_t msg_until;
static void say(const char *s)                  /* the state band's message: cut to fit (a longer one ran over into
                                                 * key_role, behind it in RAM, until 2026-11-10: a black key became a white) */
{
    uint32_t i;
    for (i = 0; i + 1u < sizeof msg && s[i]; i++) msg[i] = s[i];
    msg[i] = 0;
    msg_until = fm1_ms + 1500u;
}

/* ---- playing */
static uint32_t hit_ms[DRUM_NCH];
static void hit(uint32_t ch, uint32_t vel, int32_t semi, int midi_in)   /* a hit by hand: a key, a pad, MIDI */
{
    drum_trigger(ch, vel, semi);
    hit_ms[ch] = fm1_ms;
    if (!midi_in && !semi) {                    /* out as the sequencer's hits (not what came in: no loop) */
        midi_send(0x99, 36u + ch, vel);
        midi_send(0x89, 36u + ch, 0);
    }
    if (rec && seq.run && !semi) seq_rec(ch, vel);   /* (chromatic notes are not recorded: a step has no pitch) */
}

#define C_GREY RGB(90, 90, 90)
#define C_TXT RGB(210, 210, 210)
static uint16_t ch_colour(uint32_t c) { return RGB(CH_RGB[c][0], CH_RGB[c][1], CH_RGB[c][2]); }

#define SPLASH_MS 1500u                         /* the name at power-on, until then or a button */
#ifndef FELUCCA_ID
#define FELUCCA_ID "HOST"                       /* (platform.c gives it to the device build only) */
#endif
static void draw_splash(void)
{
    uint32_t c;
    lcd_fill(0, 0, 240, 240, C_BLACK);
    for (c = 0; c < DRUM_NCH; c++)
        lcd_fill(42u + c * 20u, 90, 16, 16, ch_colour(c));
    draw_text_box(0, 118, 240, &FONT_S, APP_NAME, RGB(255, 220, 80), 1);
    draw_text_box(0, 140, 240, &FONT_S, APP_VER "  " FELUCCA_ID, C_GREY, 1);
}

static void transport(int play)
{
    if (play) { seq_play(); if (!seq.ext) midi_send(0xFA, 0, 0); }
    else { seq_stop(); if (!seq.ext) midi_send(0xFC, 0, 0); }
}

/* MIDI in: from the main loop, and while the screen draws (idle_ok) */
static uint32_t idle_ok;
static void midi_in(void)
{
    uint8_t st, d1, d2;
    while (midi_get(&st, &d1, &d2)) {           /* (USB, and the FM-1's TRS jack) */
        uint32_t ty = st & 0xF0u;
        if (st == 0xF8u) { if (seq.ext) seq.clk_in++; continue; }
        if (st == 0xFAu) { if (seq.ext) seq_play(); continue; }
        if (st == 0xFBu) { if (seq.ext) seq.run = 1; continue; }
        if (st == 0xFCu) { if (seq.ext) seq_stop(); continue; }
        if (ty == 0x90u && d2 && d1 >= 36u && d1 < 36u + DRUM_NCH)
            hit(d1 - 36u, d2, 0, 1);
    }
}
#if !BOARD_SMK37
static void midi_out_due(void);
#endif
static void app_idle(void)
{
    if (!idle_ok) return;
    midi_in();
#if !BOARD_SMK37
    midi_out_due();
#endif
}

static uint8_t save_req;
static int quiet(void)                          /* nothing sounds: a flash erase (~50 ms a sector) would cut it */
{
    uint32_t i, busy = 0;
    for (i = 0; i < DRUM_NCH; i++) busy |= drum_v[i].on;
    return !busy;
}

#if BOARD_SMK37
#include "app_smk.c"
#else
#include "app_fm1.c"
#endif
