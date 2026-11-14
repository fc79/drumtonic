/* SPDX-License-Identifier: GPL-3.0-only */
/* Drumtonic's FM-1 panel (app_fm1.c includes it): the interface for one drum machine of 10 channels. Its six rules:
 *
 *   1  three bands: the 4 knobs on top (level with the real ones), the page in the middle, the state at the bottom
 *   2  the black keys: 1-10 mute the channel on LIVE (HOME), pick it on every other page;
 *      11 held + white keys = accents. With REC on, the channels are white keys 1-10 (they play and
 *      record)
 *   3  the three left encoders, on every page: SELECT the channel (1-10, then ALL), PRESETS (on SEQ) the part's length
 *      (longer copies the steps; back to where it started, the original), ALGORITHM moves the channel's steps (ALL:
 *      every channel's); with a step held, ALGORITHM is its condition (clockwise 95..5 %, anticlockwise FILL, NOT
 *      FILL, then the cycles 1:2 .. 8:8; 100 % in the middle). In a list: SELECT the row, ALGORITHM the value
 *   4  a button opens one page; again, the next bank of knobs (the dots under the knobs). The open page's button is
 *      lit, the others that open something glow
 *   5  hold a step and turn: on SOUND a knob locks that step's value (a p-lock), ALGORITHM sets its condition
 *   6  combinations of three kinds only: GLO (held: copy; + a key: paste there; + REC: clear; + a knob: its motion and
 *      LFO go; + a white key on MIX: save the kit), HOME + (a white key: a pattern from any page; a black key: solo;
 *      REC: the pattern as last saved), holding (a step; LFO + a knob puts an LFO on it; black 11 + whites). SEL
 *      held: FILL, as long as it is held
 *
 *   HOME   LIVE: white keys = patterns (OCT-/OCT+: 1-16, 17-32; hold one and tap others: a chain, in that order);
 *          TEMPO, SWING, the master's RESO and FILTER. Again: SETTINGS
 *   ENV    MIX: the channel chosen's level, reverb and delay sends, pan; white keys 1-8 = the kits (load; GLO + key: save)
 *   SEQ    STEPS: the 10 x 16 grid (OCT-/OCT+: steps 17-64); RATE, MICRO: with a step held, its micro timing (knob 2,
 *          or OCT-/OCT+ held then), 1/24 of a step a click as Elektron's, 23/24 early .. 23/24 late, stopping at 0 on
 *          its way across; a mark at the cell's bottom
 *   EDIT   SOUND: OSC, MOD, AMP, NOISE, N.ENV, OUT, CHANNEL (level, pan, the sends)
 *   LFO    the pattern's 8 LFOs; SELECT one, its RATE, SHAPE, DEPTH, SYNC
 *   FX     REVERB, DELAY, DELAY 2, MASTER, COMP
 *   SAVE   everything;  PLAY, REC on the press;  ARP: nothing (no 303 here)
 * A button held alone for 0.9 s shows what it does on this page. */
#include "saira_font.h"

static const uint8_t WHITE[16] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};
static const uint8_t BLACK[11] = {1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25};
#define BLACK_ACC 10u                           /* black 11 */
#define UI_NKEY 27u                             /* F3 .. G5 */
#define PANEL_NBTN 14u
static uint8_t key_role[UI_NKEY];               /* 0x10 | white index, 0x20 | black index */

/* ---- graphics B "Blocks": colours, boxes, text */
#define B_BG RGB(0, 0, 0)
#define B_BOX RGB(23, 24, 26)
#define B_ACT RGB(36, 38, 42)
#define B_TRACK RGB(46, 48, 52)
#define B_EMPTY RGB(48, 50, 55)                 /* (1.5:1 on the background, the beats 2.3:1: seen in the half-dark; every */
#define B_BEAT RGB(74, 77, 82)                  /* channel's colour still 3:1 on a beat, WCAG 1.4.11) */
#define B_OUT RGB(18, 19, 20)
#define B_USED RGB(74, 77, 82)
#define B_HEAD RGB(104, 107, 112)               /* the playhead on an empty step: above a beat (1.6:1), and darker than any
                                                 * channel's colour, in grey too */
#define B_TXT RGB(244, 244, 242)
#define B_SEC RGB(142, 145, 150)
#define B_REC RGB(255, 46, 62)
/* the three bands (FM1-INTERFACCIA.md): the knobs 0..73 (cells 58 x 64, the bank's dots at 68), the page 74..207 (its
 * content in the top 136 rows), the state band 216..239; SETTINGS and the library: one list over 0..215, in two strips */
#define TOP_H 74
#define MID_Y 74
#define MID_H 142
#define MID_C 136                               /* (the page's content: the top 136 rows of it) */
#define BOT_Y 216
#define BOT_H 24                                /* (32 until 2026-11-13, its text high with 11 rows empty under it; then
                                                 * 26, the last row of SEQ against it) */
#define BOT_TY 1                                /* (the state band's text: its capitals, 17 px, centred) */
#define SR 14                                   /* STEPS: a row's pitch, a cell's height (13 and 10 until 2026-11-13) */
#define SCH 12                                  /* (the user's choice of 11 and 12: 3 px over the state band) */
#define LIST_STRIP (BOT_Y / 2)
/* Text in a box: FONT_LB's capitals are 11 px from 2 under its y, FONT_SB's 12 px from 2 under (measured): centred in a
 * box of height h at y, LB at y + (h - 11) / 2 - 2, SB at y + (h - 12) / 2 - 2 (2026-11-13: several were 2-8 px high) */

#define B_LFO RGB(184, 255, 61)
#define B_LOCK RGB(255, 176, 32)
#define B_INK RGB(0, 0, 0)
static const uint8_t UI_CH[DRUM_NCH][3] = {     /* each channel's colour here (CH_RGB's hues, light enough for 4.5:1) */
    {255, 107, 74}, {255, 163, 71}, {255, 214, 61}, {140, 224, 74}, {61, 224, 208}, {124, 156, 255},
    {120, 186, 255}, {165, 212, 255},           /* MT, HT: LT's blue, lighter */
    {180, 140, 255}, {255, 111, 181}};
static uint16_t chc(uint32_t c) { return c < DRUM_NCH ? RGB(UI_CH[c][0], UI_CH[c][1], UI_CH[c][2]) : B_TXT; }

static void box(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c)   /* corners rounded (3 px) */
{
    static const uint8_t IN[3] = {2, 1, 0};
    int32_t r;
    for (r = 0; r < h; r++) {
        int32_t k = r < 3 ? IN[r] : r >= h - 3 ? IN[h - 1 - r] : 0;
        cv_rect(x + k, y + r, w - 2 * k, 1, c);
    }
}
static int32_t tx(int32_t x, int32_t y, const felucca_font_t *f, const char *s, uint16_t c, uint16_t bg)   /* blended
                                                 * from bg to c */
{
    uint16_t ramp[16];
    int32_t a, r0 = bg >> 11, g0 = bg >> 5 & 63, b0 = bg & 31, r1 = c >> 11, g1 = c >> 5 & 63, b1 = c & 31;
    for (a = 0; a < 16; a++)
        ramp[a] = (uint16_t)(((r0 + (r1 - r0) * a / 15) << 11) | ((g0 + (g1 - g0) * a / 15) << 5) | (b0 + (b1 - b0) * a / 15));
    for (; *s; s++) {
        uint32_t gi = glyph(f, (uint8_t)*s), gx, gy, w = f->bw[gi], bpr = (w + 1u) / 2u;
        const uint8_t *gd = f->data + f->off[gi];
        for (gy = 0; gy < f->h; gy++)
            for (gx = 0; gx < w; gx++) {
                uint32_t v = gd[gy * bpr + gx / 2u];
                v = (gx & 1u) ? (v & 15u) : (v >> 4);
                if (v) cv_pset(x - f->pad + (int32_t)gx, y + (int32_t)gy, ramp[v]);
            }
        x += f->adv[gi];
    }
    return x;
}
static void txr(int32_t xr, int32_t y, const felucca_font_t *f, const char *s, uint16_t c, uint16_t bg)   /* right-aligned */
{
    tx(xr - text_w(f, s), y, f, s, c, bg);
}
static void txc(int32_t xc, int32_t y, const felucca_font_t *f, const char *s, uint16_t c, uint16_t bg)   /* centred */
{
    tx(xc - text_w(f, s) / 2, y, f, s, c, bg);
}

/* ---- the state */
enum { V_LIVE, V_SET, V_MIX, V_STEPS, V_SOUND, V_LFO, V_FX, NVIEW };
enum { CB_NONE, CB_PAT, CB_TRACK, CB_STEP, CB_SOUND, CB_LFO };
enum { A_NONE, A_PASTE_PAT, A_PASTE_TRACK, A_CLEAR_PAT, A_CLEAR_TRACK, A_KIT_SAVE, A_FACTORY, A_LIB_SAVE };
static struct {
    uint8_t view, bank[NVIEW], spage, pbank, row, lfo_sel;
    uint32_t btn, btn_used, down_t[PANEL_NBTN], turn_t, keys;
    int8_t held;                                /* the step held, -1 none */
    uint8_t held_created, held_used, acc_hold, help, fill;
    uint8_t nr_on, nr_rate;                     /* note repeat (SEQ, ARP): on; its rate, NR_DIV's */
    uint8_t br_mode;                            /* beat repeat (HOME): ARP's tap turns it on, whites 1-4 then repeat */
    int8_t br_k;                                /* (repeating: its length, BR_LEN's, -1: not) */
    uint8_t br_w;                               /* (the white key holding it) */
    uint16_t nr_held, nr_new;                   /* (note repeat: the keys repeating at the last loop; pressed this loop) */
    uint32_t held_t;
    int8_t touched;
    uint32_t touch_until;
    uint8_t ask, ask_a, ask_b;
    char ask_q[32];
    uint8_t chain[SEQ_NCHAIN], chain_n;
    int8_t chain_key;
    uint8_t clip_kind, clip_pat, clip_ch, clip_step, clip_lfo;
    uint8_t lib_row;
    float gr;                                   /* the COMP page's meter: dB of reduction, held and falling 20 dB/s */
    uint32_t gr_t;                            /* the library's row: SND_LIB's, then the user's (LIB_USER) */
    uint32_t enc_t[7], enc_rate[7], djf_t, micro_t;   /* (micro_t: MICRO stopped at 0 then, 0: not) */
    uint32_t eat_b, eat_k;                      /* the buttons and keys that closed the opening: do nothing until released */
    uint32_t splash, splash_t, cpu;   /* splash: when the opening began (0: over); splash_t: its last frame */
    uint32_t h_top, h_mid, h_bot, h_led, h_midm, mid_t;   /* h_midm, mid_t: the middle band with its meters, when */
    uint32_t big_until;                         /* the big bubble (big()): until then, its two lines, grey: the first */
    char big_l1[8], big_l2[8], copied[24];      /* copied: what GLO held copied, for its guide (0: nothing this hold) */
    uint8_t big_grey;
    uint32_t guide_seen;                        /* the buttons whose guide showed in this press */
} ui = {.held = -1, .chain_key = -1, .touched = -1};

static void big(const char *a, const char *b, uint32_t grey);
static void big_n(const char *a, const char *pre, int32_t n);
static int32_t guide_btn(void);
static uint32_t cur_pat(void) { return seq.pat; }   /* the pattern edited: the one playing */
static uint32_t one_ch(void) { return set.ch < DRUM_NCH; }
static uint32_t len_ch(void) { return one_ch() ? set.ch : DRUM_NCH; }   /* whose length PRESETS moves */
static uint32_t len_now(uint32_t pat) { return one_ch() ? track_len(pat, set.ch) : pat_len(pat); }

/* ---- the knobs: what each page's banks hold */
enum { R_NONE, R_PARAM, R_LEVEL, R_FX, R_TEMPO, R_SWING, R_RATE, R_LFO, R_MICRO, R_NREP, R_QUANT };
typedef struct { uint8_t kind, a; } pref_t;
typedef struct { const char *name; pref_t k[4]; } bank_t;
#define PP(p) {R_PARAM, p}
#define FXP(p) {R_FX, p}
#define NOK {R_NONE, 0}
static const bank_t BANK_LIVE[1] = {{"LIVE", {{R_TEMPO, 0}, {R_SWING, 0}, FXP(FX_M_RESO), FXP(FX_M_FILTER)}}};
static const bank_t BANK_SET[1] = {{"SETTINGS", {NOK, NOK, NOK, NOK}}};
static const bank_t BANK_MIX[1] = {{"MIX", {PP(P_LEVEL), PP(P_REV), PP(P_DLY), PP(P_PAN)}}};   /* the channel chosen */
static const bank_t BANK_STEPS[1] = {{"STEPS", {{R_RATE, 0}, {R_MICRO, 0}, {R_NREP, 0}, {R_QUANT, 0}}}};   /* (MICRO: the
                                                 * step held's; REPEAT: note repeat's rate, ARP; QUANT: the part's) */
#define NSOUND 8u
#define B_LIB 7u                                /* SOUND's last bank: the library */
static const bank_t BANK_SOUND[NSOUND] = {
    {"OSC", {PP(P_WAVE), PP(P_FREQ), PP(P_PHASE), PP(P_MIX)}},
    {"MOD", {PP(P_PMODE), PP(P_PAMT), PP(P_PRATE), NOK}},
    {"AMP", {PP(P_OATK), PP(P_ODEC), PP(P_OVEL), NOK}},
    {"NOISE", {PP(P_NFILT), PP(P_NFREQ), PP(P_NQ), PP(P_NSTEREO)}},
    {"N.ENV", {PP(P_NENV), PP(P_NATK), PP(P_NDEC), PP(P_NVEL)}},
    {"OUT", {PP(P_EQF), PP(P_EQG), PP(P_CHOKE), PP(P_DIST)}},
    {"CHANNEL", {PP(P_LEVEL), PP(P_PAN), PP(P_REV), PP(P_DLY)}},
    {"LIBRARY", {NOK, NOK, NOK, NOK}}};          /* (a list: lib_*) */
static const bank_t BANK_LFO[1] = {{"LFO", {{R_LFO, 0}, {R_LFO, 1}, {R_LFO, 2}, {R_LFO, 3}}}};
#define B_COMP 4u                               /* FX's COMP page: its meter, MIX on ALGORITHM */
static const bank_t BANK_FX[5] = {
    {"REVERB", {FXP(FX_RV_DECAY), FXP(FX_RV_TONE), FXP(FX_RV_HPF), NOK}},   /* (no RETURN: unity, fx.c) */
    {"DELAY", {FXP(FX_DL_TIME), FXP(FX_DL_FDBK), FXP(FX_DL_TONE), FXP(FX_DL_TAPE)}},
    {"DELAY 2", {FXP(FX_DL_HPF), FXP(FX_DL_PING), NOK, NOK}},   /* (no RETURN: fixed, fx.c) */
    {"MASTER", {FXP(FX_M_DRIVE), FXP(FX_M_FILTER), FXP(FX_M_RESO), NOK}},
    {"COMP", {FXP(FX_M_THRESH), FXP(FX_M_RATIO), FXP(FX_M_ATTACK), FXP(FX_M_RELEASE)}}};
static const struct { const bank_t *b; uint8_t n; const char *name; } VIEWS[NVIEW] = {
    {BANK_LIVE, 1, "LIVE"}, {BANK_SET, 1, "SETTINGS"}, {BANK_MIX, 1, "MIX"}, {BANK_STEPS, 1, "STEPS"},
    {BANK_SOUND, NSOUND, "SOUND"}, {BANK_LFO, 1, "LFO"}, {BANK_FX, 5, "FX"}};
static const bank_t *bank_now(void) { return &VIEWS[ui.view].b[ui.bank[ui.view] % VIEWS[ui.view].n]; }

static uint32_t is_mode(uint32_t p) { return p == P_WAVE || p == P_PMODE || p == P_NFILT || p == P_NENV; }
static uint32_t is_switch(const pref_t *r)
{
    return (r->kind == R_PARAM && (is_mode(r->a) || r->a == P_NSTEREO || r->a == P_CHOKE)) ||
           (r->kind == R_FX && FX_P[r->a].max < 16u) || r->kind == R_RATE || r->kind == R_NREP || (r->kind == R_LFO && r->a != 2u);
}
/* note repeat's rates (SEQ, ARP): hits a half note (seq_fm1.c's seq_nr_div) */
#define NR_N 8u
static const uint8_t NR_DIV[NR_N] = {2, 3, 4, 6, 8, 12, 16, 24};
static const char NR_NAME[NR_N][6] = {"1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/32T"};
static void nr_set(int32_t r)
{
    ui.nr_rate = (uint8_t)(r < 0 ? 0 : r >= (int32_t)NR_N ? NR_N - 1u : (uint32_t)r);
    seq_nr_div = NR_DIV[ui.nr_rate];
}
static uint32_t knob_ch(const pref_t *r) { return r->kind == R_LEVEL ? r->a : set.ch; }
static uint32_t pref_param(const pref_t *r) { return r->kind == R_LEVEL ? P_LEVEL : r->a; }

static void micro_held_set(int32_t m)           /* the step held's micro timing to m 24ths (0: on the step) */
{
    uint32_t pat = cur_pat();
    m = m > MICRO_MAX ? MICRO_MAX : m < -MICRO_MAX ? -MICRO_MAX : m;
    if (mot_set(pat, set.ch, P_MICRO, (uint32_t)ui.held, m ? (uint32_t)((int32_t)MICRO0 + m) : MOT_NONE)) say("MOTION FULL");
    ui.held_used = 1;
}

static uint32_t quant_get(uint32_t pat)         /* QUANT: the channel's; ALL: the highest */
{
    uint32_t c, q = 0;
    if (one_ch()) return mot_quant[pat % SEQ_NPAT][set.ch];
    for (c = 0; c < DRUM_NCH; c++) if (mot_quant[pat % SEQ_NPAT][c] > q) q = mot_quant[pat % SEQ_NPAT][c];
    return q;
}
static void quant_set(uint32_t pat, int32_t v)  /* QUANT to v %: the channel's; ALL: every channel's */
{
    uint32_t c;
    v = v < 0 ? 0 : v > 100 ? 100 : v;
    for (c = 0; c < DRUM_NCH; c++) if (!one_ch() || c == set.ch) mot_quant[pat % SEQ_NPAT][c] = (uint8_t)v;
}
static int32_t pref_get(const pref_t *r)
{
    uint32_t ch = knob_ch(r) % DRUM_NCH, pat = cur_pat();
    const lfo_t *l = &lfos[pat][ui.lfo_sel % NLFO];
    switch (r->kind) {
    case R_PARAM: case R_LEVEL: {
        uint32_t prm = pref_param(r), v = MOT_NONE;
        if (ui.held >= 0 && ui.view == V_SOUND) v = mot_get(pat, ch, prm, (uint32_t)ui.held);
        return (int32_t)(v != MOT_NONE ? v : mot_base(ch, prm));
    }
    case R_FX: return kitx[X_FX + r->a];
    case R_TEMPO: return set.bpm;
    case R_SWING: return seq_song.p[pat].swing;
    case R_RATE: return seq_song.p[pat].rate;
    case R_LFO: return !l->used ? 0 : r->a == 0 ? l->rate : r->a == 1 ? l->shape : r->a == 2 ? l->depth : l->sync;
    case R_MICRO: return ui.held >= 0 && one_ch() ? micro_of(mot_get(pat, ch, P_MICRO, (uint32_t)ui.held)) : 0;
    case R_NREP: return ui.nr_rate;
    case R_QUANT: return (int32_t)quant_get(pat);
    }
    return 0;
}
static uint32_t pref_bar(const pref_t *r)       /* 0..127 for the bar under the value */
{
    int32_t v = pref_get(r);
    switch (r->kind) {
    case R_FX: return (uint32_t)v * 127u / FX_P[r->a].max;
    case R_TEMPO: return (uint32_t)(v - 40) * 127u / 210u;
    case R_SWING: return (uint32_t)(v - 50) * 127u / 25u;
    case R_RATE: return (uint32_t)v * 127u / 3u;
    case R_NREP: return (uint32_t)v * 127u / (NR_N - 1u);
    case R_LFO: return r->a == 0 ? (uint32_t)v * 127u / 9u : r->a == 1 ? (uint32_t)v * 127u / 3u : r->a == 2 ? (uint32_t)(v + 100) * 127u / 200u : v ? 127u : 0u;
    case R_MICRO: return (uint32_t)(v + MICRO_MAX) * 127u / (2u * MICRO_MAX);
    case R_QUANT: return (uint32_t)v * 127u / 100u;
    }
    return (uint32_t)v;
}
static const char *pref_name(const pref_t *r)
{
    static const char *const LFON[4] = {"RATE", "SHAPE", "DEPTH", "SYNC"};
    switch (r->kind) {
    case R_PARAM: return r->a == P_REV ? "REV" : r->a == P_DLY ? "DLY" : r->a == P_FREQ ? "PITCH" : r->a == P_PAMT ? "AMT"
                       : r->a == P_PMODE ? "MOD" : PARAM_NAME[r->a];
    case R_LEVEL: return kit.ch[r->a];
    case R_FX: return FX_P[r->a].name;
    case R_TEMPO: return "TEMPO";
    case R_SWING: return "SWING";
    case R_RATE: return "RATE";
    case R_LFO: return LFON[r->a & 3u];
    case R_MICRO: return "MICRO";
    case R_NREP: return "REPEAT";
    case R_QUANT: return "QUANT";
    }
    return "";
}
static void param_text(char *s, uint32_t p, uint32_t v)
{
    static const char *const WAVE[3] = {"SIN", "TRI", "SAW"}, *const PMOD[3] = {"DEC", "LFO", "RND"},
                      *const FILT[3] = {"LP", "BP", "HP"}, *const ENV[3] = {"EXP", "LIN", "CLAP"};
    switch (p) {
    case P_WAVE: cat(s, WAVE[knob_mode(v)]); break;
    case P_PMODE: cat(s, PMOD[knob_mode(v)]); break;
    case P_NFILT: cat(s, FILT[knob_mode(v)]); break;
    case P_NENV:
        if (v > NENV_V_EXP && v < NENV_V_LIN) cat(catn(s, (int32_t)((v - NENV_V_EXP) * 100u / (NENV_V_LIN - NENV_V_EXP))), "%");
        else cat(s, ENV[knob_mode(v)]);
        break;
    case P_NSTEREO: cat(s, v >= 64u ? "ON" : "OFF"); break;
    case P_CHOKE: if (v >> 5) catn(cat(s, "G"), (int32_t)(v >> 5)); else cat(s, "-"); break;
    case P_PAN: if (v == 64u) cat(s, "C"); else catn(cat(s, v < 64u ? "L" : "R"), (int32_t)(v < 64u ? 64u - v : v - 64u)); break;
    case P_PAMT: case P_EQG: catn(s, (int32_t)v - 64); break;
    default: catn(s, (int32_t)v); break;
    }
}
static void pref_text(char *s, const pref_t *r)
{
    static const char *const RATE_N[SEQ_NRATE] = {"1/16", "1/16T", "1/32", "1/8T"};
    int32_t v = pref_get(r);
    const lfo_t *l = &lfos[cur_pat()][ui.lfo_sel % NLFO];
    *s = 0;
    switch (r->kind) {
    case R_PARAM: case R_LEVEL: param_text(s, pref_param(r), (uint32_t)v); break;
    case R_FX:
        if (r->a == FX_M_FILTER) { if (v < 64) catn(cat(s, "L"), 64 - v); else if (v > 64) catn(cat(s, "H"), v - 64); else cat(s, "OFF"); }   /* (short: one size of figures, LP / HP 1-63) */
        else if (r->a == FX_M_THRESH) catn(s, (v * 48 + 63) / 127 - 48);   /* dB, -48..0 (fx.c c_thr) */
        else if (r->a == FX_M_ATTACK || r->a == FX_M_RELEASE) {   /* the times fx.c sets: 0.1 .. 100 ms, 10 .. 1500 ms */
            uint32_t t = (uint32_t)((r->a == FX_M_ATTACK ? 1.0f * fx_exp2(fx_pot((uint32_t)v) * 9.966f) : 100.0f * fx_exp2(fx_pot((uint32_t)v) * 7.229f)) + 0.5f);   /* (tenths of a ms) */
            if (t < 100u) cat(catn(cat(catn(s, (int32_t)(t / 10u)), "."), (int32_t)(t % 10u)), "ms");
            else if (t < 10000u) cat(catn(s, (int32_t)((t + 5u) / 10u)), "ms");
            else cat(catn(cat(catn(s, (int32_t)((t + 500u) / 10000u)), "."), (int32_t)((t + 500u) / 1000u % 10u)), "s");
        }
        else if (FX_P[r->a].names) cat(s, FX_P[r->a].names[v]);
        else catn(s, v);
        break;
    case R_TEMPO: catn(s, v); break;
    case R_SWING: cat(catn(s, v), "%"); break;
    case R_RATE: cat(s, RATE_N[v % SEQ_NRATE]); break;
    case R_NREP: cat(s, NR_NAME[(uint32_t)v % NR_N]); break;
    case R_QUANT: cat(catn(s, v), "%"); break;
    case R_LFO:
        if (!l->used) { cat(s, "-"); break; }
        if (r->a == 0) cat(s, (l->sync ? LFO_SYNC_N : LFO_HZ_N)[v % LFO_NRATE]);
        else if (r->a == 1) cat(s, LFO_SHAPE_N[v & 3]);
        else if (r->a == 2) cat(catn(cat(s, v > 0 ? "+" : ""), v), "%");
        else cat(s, v ? "SYNC" : "FREE");
        break;
    case R_MICRO:                               /* the step held's, in 24ths (short: the big figures; the state band
                                                 * spells it, "-2/24"); no step held: - */
        if (ui.held < 0 || !one_ch()) cat(s, "-");
        else catn(cat(s, v > 0 ? "+" : ""), v);
        break;
    }
}

/* ---- turning */
static uint32_t knob_span(const pref_t *r)      /* the values a knob goes through (0: no acceleration) */
{
    switch (r->kind) {
    case R_PARAM: return is_switch(r) ? 0u : 128u;
    case R_LEVEL: return 128u;
    case R_FX: return is_switch(r) ? 0u : FX_P[r->a].max + 1u;
    case R_LFO: return r->a == 2u ? 128u : 0u;  /* (its depth) */
    case R_QUANT: return 101u;
    default: return 0u;                         /* (TEMPO: a BPM a click, always; SWING, RATE, MICRO, REPEAT: few) */
    }
}
#define ACC_SLOW 12u                            /* clicks a second: up to it one step a click */
#define ACC_GAP 150u                            /* ms: a longer pause, a new turn (one step a click again) */
static int32_t accel(uint32_t e, int32_t n, const pref_t *r)   /* the 4 knobs: one click, one step; spun, up to x8
                                                 * (a quick turn crosses 0..127 in about one turn). The speed is the
                                                 * clicks over the time since the last ones, so a long main loop (a redraw)
                                                 * does not hide it (until 2026-11-12: clicks under 60 ms apart, rarely) */
{
    uint32_t now = fm1_ms, dt = now - ui.enc_t[e], an = (uint32_t)(n < 0 ? -n : n), span = r ? knob_span(r) : 0u, v, m;
    ui.enc_t[e] = now;
    if (dt > ACC_GAP) { ui.enc_rate[e] = 0; return n; }
    v = an * 1000u / (dt ? dt : 1u);            /* clicks a second, now */
    if (v > 200u) v = 200u;
    ui.enc_rate[e] = (ui.enc_rate[e] + v) / 2u; /* (smoothed) */
    if (span < 32u || ui.enc_rate[e] <= ACC_SLOW) return n;
    m = 1u + (ui.enc_rate[e] - ACC_SLOW) / 4u;  /* 16 a second x2, 24 x4, 40 x8 */
    if (m > span / 16u) m = span / 16u;
    return n * (int32_t)m;
}
static int32_t step_param(uint32_t p, int32_t v, int32_t e)   /* a patch value turned e clicks */
{
    if (p == P_NENV) {                          /* EXP, every % to LIN, then one click to CLAP */
        int32_t lo = NENV_V_EXP, hi = NENV_V_LIN;
        if (knob_mode((uint32_t)v) == NENV_CLAP) return e < 0 ? hi : NENV_V_CLAP;
        if (v >= hi && e > 0) return NENV_V_CLAP;
        v = (v < lo ? lo : v > hi ? hi : v) + e;
        return v < lo ? lo : v > hi ? hi : v;
    }
    if (is_mode(p)) v = (int32_t)knob_mode((uint32_t)v) * 43 + 21 + e * 43;
    else if (p == P_NSTEREO) v = e > 0 ? 127 : 0;
    else if (p == P_CHOKE) v = (v >> 5) * 32 + e * 32;
    else v += e;
    return v < 0 ? 0 : v > 127 ? 127 : v;
}
static void pref_set(const pref_t *r, int32_t e)
{
    uint32_t pat = cur_pat();
    int32_t v;
    lfo_t *l = &lfos[pat][ui.lfo_sel % NLFO];
    switch (r->kind) {
    case R_PARAM: case R_LEVEL: {
        uint32_t ch = knob_ch(r), prm = pref_param(r);
        if (ch >= DRUM_NCH) { say("CHOOSE A CHANNEL"); return; }
        if (ui.held >= 0 && ui.view == V_SOUND) {   /* a p-lock on the step held */
            uint32_t cur = mot_get(pat, ch, prm, (uint32_t)ui.held);
            v = step_param(prm, (int32_t)(cur != MOT_NONE ? cur : mot_base(ch, prm)), e);
            if (mot_set(pat, ch, prm, (uint32_t)ui.held, (uint32_t)v)) say("MOTION FULL");
            mot_preview(pat, ch, (uint32_t)ui.held);
            ui.held_used = 1;
            return;
        }
        v = step_param(prm, (int32_t)mot_base(ch, prm), e);
        if (prm < DRUM_NP) kit.pat[ch].p[prm] = (uint8_t)v;
        else kitx[X_SEND + ch * 2u + (prm - P_REV)] = (uint8_t)v;
        kit_edited = 1;
        mot_kit_changed(ch);
        if (mot_touch(ch, prm) < 0) say("MOTION FULL");
        return;
    }
    case R_FX:
        v = kitx[X_FX + r->a];
        if (r->a == FX_M_FILTER && v != 64 && ((v < 64) != (v + e < 64) || v + e == 64) && fm1_ms - ui.djf_t > 350u) {
            v = 64;                             /* across: it stops at OFF; a pause, then on to the other side */
            ui.djf_t = fm1_ms;
        } else if (r->a == FX_M_FILTER && v == 64 && fm1_ms - ui.djf_t < 350u) {
            ui.djf_t = fm1_ms;
            return;
        } else {
            v += e;
        }
        v = v < 0 ? 0 : v > FX_P[r->a].max ? FX_P[r->a].max : v;
        kitx[X_FX + r->a] = (uint8_t)v;
        if (r->a == FX_M_COMP) fx_comp_knob((uint32_t)v);   /* (one knob: the details follow) */
        kit_edited = 1;
        fx_apply();
        return;
    case R_TEMPO:
        v = set.bpm + e;
        set.bpm = (uint8_t)(v < 40 ? 40 : v > 250 ? 250 : v);
        seq.bpm = set.bpm;
        fx_apply();                             /* (the delay's time follows) */
        return;
    case R_SWING:
        v = seq_song.p[pat].swing + e;
        seq_song.p[pat].swing = (uint8_t)(v < 50 ? 50 : v > 75 ? 75 : v);
        return;
    case R_NREP: nr_set((int32_t)ui.nr_rate + (e > 0 ? 1 : -1)); return;
    case R_QUANT: quant_set(pat, (int32_t)quant_get(pat) + e); return;
    case R_RATE:
        v = (int32_t)seq_song.p[pat].rate + (e > 0 ? 1 : -1);
        seq_song.p[pat].rate = (uint8_t)(v < 0 ? 0 : v >= (int32_t)SEQ_NRATE ? SEQ_NRATE - 1u : (uint32_t)v);
        return;
    case R_MICRO:                               /* the step held's micro timing; it stops on the step (0) on its way
                                                 * across, a pause then on to the other side (as the master's FILTER) */
        if (ui.held < 0 || !one_ch()) { say("HOLD A STEP, TURN MICRO"); return; }
        v = pref_get(r);
        if (v != 0 && ((v < 0) != (v + e < 0) || v + e == 0)) {
            v = 0;                              /* (stopped at 0: turning on, within 0.35 s, stays there) */
            ui.micro_t = fm1_ms | 1u;
        } else if (v == 0 && ui.micro_t && fm1_ms - ui.micro_t < 350u) {
            ui.micro_t = fm1_ms | 1u;
            return;
        } else {
            ui.micro_t = 0;
            v += e;
        }
        micro_held_set(v);
        return;
    case R_LFO:
        if (!l->used) { say("HOLD LFO, TURN A KNOB"); return; }
        if (r->a == 0) { v = l->rate + (e > 0 ? 1 : -1); l->rate = (uint8_t)(v < 0 ? 0 : v >= (int32_t)LFO_NRATE ? LFO_NRATE - 1u : (uint32_t)v); }
        else if (r->a == 1) { v = l->shape + (e > 0 ? 1 : -1); l->shape = (uint8_t)(v < 0 ? 0 : v >= LFO_NSHAPE ? LFO_NSHAPE - 1 : v); }
        else if (r->a == 2) { v = l->depth + e; l->depth = (int8_t)(v < -100 ? -100 : v > 100 ? 100 : v); }
        else l->sync = e < 0;
        return;
    }
}

static void lfo_name(char *s, uint32_t pat, uint32_t k)   /* "BD PITCH" */
{
    const lfo_t *l = &lfos[pat][k];
    pref_t r = {R_PARAM, l->param};
    cat(cat(cat(s, kit.ch[l->ch % DRUM_NCH]), " "), pref_name(&r));
}

/* ---- note repeat (SEQ): ARP's tap turns it on; a black key held repeats its channel at the rate (knob 3, REPEAT; ARP
 * held + white 1-8 or a knob); with REC, each hit on the step nearest (one hit a step: until 2026-11-13 two or more on a
 * step made its ratchet, which a recording no longer does; a ratchet is set by hand, a step held + ARP) */
/* ---- beat repeat (HOME): a tap on ARP turns it on (on until the next tap on HOME, other pages too); on, whites 1-4
 * repeat the slice in progress (1/4 .. 1/32) while held, whites 5-16 do nothing (HOME + a white: still a pattern).
 * Stopped: PLAY FIRST. Leaving HOME or stopping ends a repeat; it changes nothing in the pattern (seq_fm1.c
 * seq_br_block). (Until 2026-11-11: ARP held + a white or a knob) */
#define BR_N 4u
static const uint16_t BR_LEN[BR_N] = {8192, 4096, 2048, 1024};   /* (in 4096ths of an eighth) */
static const char BR_NAME[BR_N][5] = {"1/4", "1/8", "1/16", "1/32"};
static void br_start(uint32_t k, uint32_t w)
{
    if (!seq.run) { big("PLAY", "FIRST", 1); return; }
    ui.br_k = (int8_t)k;
    ui.br_w = (uint8_t)w;
    seq_br_len = BR_LEN[k];
    RING_PUBLISH();
    seq_br_req++;
    big("REPEAT", BR_NAME[k], 0);
}
static void br_end(void)
{
    if (ui.br_k < 0) return;
    ui.br_k = -1;
    if (!memcmp(ui.big_l1, "REPEAT", 7)) ui.big_until = 0;   /* (its bubble goes with it; another one stays) */
    seq_br_len = 0;
    RING_PUBLISH();
    seq_br_req++;
}
static void br_keys(void)                       /* each loop: its key still held (let go, another of whites 1-4 still
                                                 * held: that one), on HOME, playing; the bubble */
{
    uint32_t k;
    if (ui.br_k < 0) return;
    if (ui.view != V_LIVE || !seq.run || !ui.br_mode) { br_end(); return; }
    if (!(ui.keys >> WHITE[ui.br_w % 16u] & 1u)) {
        for (k = 0; k < BR_N && !(ui.keys >> WHITE[k] & 1u); k++) {}
        if (k < BR_N) br_start(k, k); else br_end();
        return;
    }
    if (seq_br_done == seq_br_req && !seq_br_on) { seq_br_req++; return; }   /* (the ISR ended it, a START from a
                                                 * DAW: the slice in progress from there) */
    if ((int32_t)(fm1_ms - ui.big_until) >= 0 && (int32_t)(fm1_ms - ui.touch_until) >= 0) big("REPEAT", BR_NAME[ui.br_k], 0);
}

static void nr_pick(int32_t r)                  /* (a white key, ARP + a knob: the big bubble says it) */
{
    nr_set(r);
    big("RATE", NR_NAME[ui.nr_rate], 0);
}
static void rec_micro(uint32_t pat, uint32_t ch, uint32_t st, int32_t m)   /* a hit recorded, m 24ths from its step:
                                                 * REC FREE keeps it as the step's micro timing (0: on the step), over
                                                 * what it had; GRID leaves the step's as it was */
{
    if (!set.rec_free) return;
    m = m > MICRO_MAX ? MICRO_MAX : m < -MICRO_MAX ? -MICRO_MAX : m;
    if (mot_set(pat, ch, P_MICRO, st, m ? (uint32_t)((int32_t)MICRO0 + m) : MOT_NONE)) say("MOTION FULL");
}
static void nr_take(void)                       /* the repeats played: REC records them */
{
    int32_t e;
    while ((e = seq_nr_take()) >= 0) {
        uint32_t ch = (uint32_t)e & 15u, st = (uint32_t)e >> 4 & 63u, pat = (uint32_t)e >> 10 & 31u;
        if (ch >= DRUM_NCH) continue;
        hit_ms[ch] = fm1_ms;                    /* (its key's light, its meter) */
        if (!rec || !seq.run) continue;
        if (seq_get(pat, ch, st) == STEP_OFF) seq_set(pat, ch, st, STEP_ON);   /* (the next step's skip: the ISR's) */
        rec_micro(pat, ch, st, (int32_t)((uint32_t)e >> 16 & 31u) - 16);
    }
}
static void nr_keys(void)                       /* each loop: the channels repeating, the black keys held (not while a
                                                 * question waits); a key held when it is turned on: its first hit now */
{
    uint32_t k, m = 0;
    if (ui.view != V_STEPS) ui.nr_on = 0;       /* (off SEQ: off) */
    for (k = 0; ui.nr_on && !ui.ask && k < DRUM_NCH; k++) if (ui.keys >> BLACK[k] & 1u) m |= 1u << k;
    for (k = 0; k < DRUM_NCH; k++)
        if ((m & ~(uint32_t)ui.nr_held & ~(uint32_t)ui.nr_new) >> k & 1u) seq_nr_req[k]++;
    ui.nr_held = (uint16_t)m;
    ui.nr_new = 0;
    seq_nr_rec = (uint8_t)(rec && seq.run);
    seq_nr_mask = (uint16_t)m;
    nr_take();
}

static void knob_turn(uint32_t k, int32_t n)
{
    const pref_t *r = &bank_now()->k[k & 3u];
    uint32_t pat = cur_pat();
    char s[32];
    if (ui.btn >> BTN_ARP & 1u) ui.btn_used |= 1u << BTN_ARP;   /* (a knob with ARP held: ARP's tap does nothing) */
    if (ui.view == V_STEPS && (ui.btn >> BTN_ARP & 1u) && ui.held < 0 && !ui.ask) {   /* ARP + a knob: note repeat's
                                                 * rate (not after a ratchet: the step's knobs) */
        ui.btn_used |= 1u << BTN_ARP;
        ui.turn_t = fm1_ms;
        nr_pick((int32_t)ui.nr_rate + (n > 0 ? 1 : -1));
        return;
    }
    if (ui.ask || r->kind == R_NONE) return;
    ui.turn_t = fm1_ms;
    ui.touched = (int8_t)k;
    ui.touch_until = fm1_ms + 1000u;            /* (the knob lit, and its bubble, until 1 s after the last click) */
    ui.big_until = 0;
    if (ui.btn >> BTN_GLO & 1u) {               /* GLO + a knob: its motion (on the step held: its lock) and LFO go */
        uint32_t ch = knob_ch(r), prm = pref_param(r);
        ui.btn_used |= 1u << BTN_GLO;
        undo_begin("CLEAR");
        if (r->kind == R_LFO) { lfo_clear(pat, ui.lfo_sel); big_n("CLEAR", "LFO ", ui.lfo_sel + 1); return; }
        if (r->kind == R_MICRO) { if (ui.held >= 0 && one_ch()) { micro_held_set(0); big("CLEAR", "MICRO", 0); } return; }
        if (r->kind == R_QUANT) { quant_set(pat, 0); big("CLEAR", "QUANT", 0); return; }
        if ((r->kind != R_PARAM && r->kind != R_LEVEL) || ch >= DRUM_NCH) return;
        if (ui.held >= 0) { mot_set(pat, ch, prm, (uint32_t)ui.held, MOT_NONE); ui.held_used = 1; big("CLEAR", "LOCK", 0); return; }
        if (mot_find(pat, ch, prm) >= 0) mot_free((uint32_t)mot_find(pat, ch, prm));
        if (lfo_find(pat, ch, prm) >= 0) lfo_clear(pat, (uint32_t)lfo_find(pat, ch, prm));
        big("CLEAR", pref_name(r), 0);
        return;
    }
    if (ui.btn >> BTN_LFO & 1u) {               /* LFO + a knob: an LFO on it, once a hold */
        int32_t i;
        if (ui.btn_used >> BTN_LFO & 1u) return;
        ui.btn_used |= 1u << BTN_LFO;
        if ((r->kind != R_PARAM && r->kind != R_LEVEL) || knob_ch(r) >= DRUM_NCH) { say("NO LFO HERE"); return; }
        if (lfo_find(pat, knob_ch(r), pref_param(r)) >= 0) {   /* it has one: its page */
            ui.lfo_sel = (uint8_t)lfo_find(pat, knob_ch(r), pref_param(r));
            ui.view = V_LFO;
            return;
        }
        i = lfo_assign(pat, knob_ch(r), pref_param(r));
        if (i == -1) { say("LFO FULL"); return; }
        if (i < 0) { say("NO LFO HERE"); return; }
        ui.lfo_sel = (uint8_t)i;
        lfo_name(cat(catn(cat(s, "LFO "), i + 1), ": "), pat, (uint32_t)i);
        say(s);
        return;
    }
    pref_set(r, accel(2u + k, n, r));
}

/* ---- the encoders on the left */
static void cond_text(char *s, uint32_t c)
{
    uint32_t k, b;
    if (c == MOT_NONE || c == 100u) { cat(s, "100%"); return; }
    if (c == COND_FILL) { cat(s, "FILL"); return; }
    if (c == COND_NFILL) { cat(s, "NOT FILL"); return; }
    if (c >= COND_CYC) {
        k = c - COND_CYC;
        for (b = 2; k >= b; b++) k -= b;
        catn(cat(catn(s, (int32_t)k + 1), ":"), (int32_t)b);
        return;
    }
    cat(catn(s, (int32_t)c), "%");
}
static char *micro_text(char *s, uint32_t v)    /* a step's micro timing: "+5/24", "-1/24"; none: nothing */
{
    int32_t m = micro_of(v);
    if (!m) return s;
    return cat(catn(cat(s, m > 0 ? "+" : "-"), m > 0 ? m : -m), "/24");
}
static uint32_t cond_pos_val(int32_t pos)       /* the condition line, one place a read: 0 = 100 %, +1..+19 = 95..5 %,
                                                 * -1 FILL, -2 NOT FILL (near: live), -3..-37 the cycles 1:2 .. 8:8 */
{
    if (!pos) return MOT_NONE;
    if (pos > 0) return 100u - 5u * (uint32_t)pos;
    if (pos >= -2) return pos == -1 ? COND_FILL : COND_NFILL;
    return COND_CYC + (uint32_t)(-pos - 3);
}
static int32_t cond_val_pos(uint32_t c)
{
    if (c == MOT_NONE || c == 100u) return 0;
    if (c < 100u) return (int32_t)(100u - c) / 5;
    if (c >= COND_FILL) return c == COND_FILL ? -1 : -2;
    return -(int32_t)(c - COND_CYC) - 3;
}

static void sel_channel(int32_t n)
{
    int32_t c = ((int32_t)set.ch + n) % (int32_t)(DRUM_NCH + 1u);   /* round: BD back to ALL, ALL on to BD (until
                                                 * 2026-11-13 it stopped at both ends) */
    if (c < 0) c += (int32_t)(DRUM_NCH + 1u);
    if ((uint32_t)c == set.ch) return;
    set.ch = (uint8_t)c;
    ui.held = -1;
    if (c < (int32_t)DRUM_NCH && !seq.run) hit((uint32_t)c, 100, 0, 0);   /* a channel chosen is heard, when stopped */
}

enum { ROW_CLOCK, ROW_REC, ROW_CLEAR, ROW_FACTORY, ROW_CPU, ROW_VER, NROW };   /* (CPU MODE until 2026-11-12: 2 x 320 MHz always) */
static void list_value(int32_t d)               /* SETTINGS: ALGORITHM on the row */
{
    if (ui.row == ROW_CLOCK) {
        if (seq.run) transport(0);
        set.ext = !set.ext;
        seq.ext = set.ext;
    }
    if (ui.row == ROW_REC) set.rec_free = !set.rec_free;   /* GRID / FREE: a hit by hand with its micro timing */
    (void)d;
}

static void ask(uint32_t act, uint32_t a, uint32_t b, const char *q)
{
    ui.ask = (uint8_t)act; ui.ask_a = (uint8_t)a; ui.ask_b = (uint8_t)b;
    cat(ui.ask_q, q);
}
static void ask_yes(void);

/* ---- the sound library: EDIT's last bank, a list. SELECT a row (ALGORITHM: 8 at a time), heard at once on the channel
 * chosen, in the pattern too (motion.c mot_prev) without changing it; SEL loads it there (with its name); GLO + SEL
 * saves the channel's sound into a user row (from a factory one: the first free); GLO + REC deletes a user row */
#define LIB_ROWS (SND_LIB_N + LIB_USER)
static uint32_t lib_on(void) { return ui.view == V_SOUND && ui.bank[V_SOUND] % NSOUND == B_LIB && one_ch(); }
static void big(const char *a, const char *b, uint32_t grey)   /* what a gesture did: two words, big, for 1 s (GLO, HOME) */
{
    uint32_t i;
    for (i = 0; i < 7u && a[i]; i++) ui.big_l1[i] = a[i];
    ui.big_l1[i] = 0;
    for (i = 0; i < 7u && b[i]; i++) ui.big_l2[i] = b[i];
    ui.big_l2[i] = 0;
    ui.big_grey = (uint8_t)grey;
    ui.touch_until = 0;
    ui.big_until = fm1_ms + 1000u;
    if (ui.view == V_SET || lib_on()) {         /* (SETTINGS, the library: the list fills the screen; the state band says it) */
        char s[24];
        cat(cat(cat(s, ui.big_l1), " "), ui.big_l2);
        say(s);
        ui.big_until = 0;
    }
}
static void big_n(const char *a, const char *pre, int32_t n)   /* "PASTE" / "P5", "STEP 5" */
{
    char s[12];
    catn(cat(s, pre), n);
    big(a, s, 0);
}
static const lib_snd_t *lib_user_at(uint32_t r)  /* a user row's sound, 0: a factory row, or empty */
{
    uint32_t u = r - SND_LIB_N;
    return r >= SND_LIB_N && u < LIB_USER && (lib_user.used >> u & 1u) ? &lib_user.s[u] : 0;
}
static const drum_patch_t *lib_patch(uint32_t r, const char **name)
{
    const lib_snd_t *u = lib_user_at(r);
    if (r < SND_LIB_N) { *name = SND_LIB[r].name; return &SND_LIB[r].p; }
    if (u) { *name = u->name; return &u->p; }
    return 0;
}
static void lib_end(void)                       /* the channel's own sound again */
{
    uint32_t ch = mot_prev_ch;
    if (!mot_prev) return;
    mot_prev = 0;
    mot_kit_changed(ch);
}
static void lib_preview(void)
{
    const char *nm;
    const drum_patch_t *p = lib_patch(ui.lib_row, &nm);
    if (!p) { lib_end(); return; }
    if (mot_prev != p || mot_prev_ch != set.ch) {
        lib_end();
        mot_prev_ch = (uint8_t)set.ch;
        mot_prev = p;
        mot_kit_changed(set.ch);
    }
    if (!seq.run) hit(set.ch, 100, 0, 0);
}
static void lib_load_row(void)                  /* SEL */
{
    const char *nm;
    const drum_patch_t *p = lib_patch(ui.lib_row, &nm);
    char s[32];
    if (!p) { say("EMPTY ROW"); return; }
    undo_begin("SOUND");
    lib_end();
    kit.pat[set.ch] = *p;
    memset(kit.ch[set.ch], 0, sizeof kit.ch[set.ch]);
    { uint32_t i; for (i = 0; i < 3u && nm[i]; i++) kit.ch[set.ch][i] = nm[i]; }
    kit_edited = 1;
    mot_kit_changed(set.ch);
    if (!seq.run) hit(set.ch, 100, 0, 0);
    cat(catn(cat(cat(s, nm), " LOADED ON "), (int32_t)set.ch + 1), "");
    say(s);
}
static void lib_store(uint32_t u)               /* the channel's own sound into user row u, saved at once */
{
    lib_end();                                  /* (what is heard: the channel's own, the one saved) */
    lib_user.s[u].p = kit.pat[set.ch];
    memcpy(lib_user.s[u].name, kit.ch[set.ch], sizeof lib_user.s[u].name);
    lib_user.used |= (uint64_t)1 << u;
    ui.lib_row = (uint8_t)(SND_LIB_N + u);
    lib_save_req = 1;
    say("SAVING...");
}
static void lib_save(void)                      /* GLO + SEL */
{
    uint32_t u;
    if (ui.lib_row >= SND_LIB_N) {
        u = ui.lib_row - SND_LIB_N;
        if (lib_user.used >> u & 1u) { ask(A_LIB_SAVE, u, 0, "REPLACE THE SOUND?"); return; }
    } else {
        for (u = 0; u < LIB_USER && (lib_user.used >> u & 1u); u++) {}
        if (u >= LIB_USER) { say("LIBRARY FULL"); return; }
    }
    lib_store(u);
}
static void lib_delete(void)                    /* GLO + REC */
{
    char s[24];
    if (!lib_user_at(ui.lib_row)) { say(ui.lib_row < SND_LIB_N ? "FACTORY SOUND: STAYS" : "EMPTY ROW"); return; }
    lib_end();
    lib_user.used &= ~((uint64_t)1 << (ui.lib_row - SND_LIB_N));
    lib_save_req = 1;
    cat(catn(cat(s, "USER "), ui.lib_row - SND_LIB_N + 1), " DELETED");
    say(s);
}
static void ui_lib_saved(int rc)
{
    char s[24];
    if (rc) { say("SAVE FAILED"); return; }
    if (lib_user_at(ui.lib_row)) { cat(catn(cat(s, "SAVED: USER "), ui.lib_row - SND_LIB_N + 1), ""); say(s); }
}

static void enc_left(uint32_t e, int32_t n)
{
    uint32_t pat = cur_pat();
    if (ui.ask) return;
    ui.turn_t = fm1_ms;
    if (lib_on() && e != ENC_PRESETS) {         /* the library: SELECT a row, ALGORITHM 8 rows */
        int32_t row = (int32_t)ui.lib_row + (e == ENC_ALGORITHM ? 8 * n : n);
        ui.lib_row = (uint8_t)(row < 0 ? 0 : row >= (int32_t)LIB_ROWS ? (int32_t)LIB_ROWS - 1 : row);
        lib_preview();
        return;
    }
    if (e == ENC_SELECT) {
        if (ui.view == V_SET) { int32_t r = (int32_t)ui.row + n; ui.row = (uint8_t)(r < 0 ? 0 : r >= NROW ? NROW - 1 : r); }
        else if (ui.view == V_LFO) { int32_t r = (int32_t)ui.lfo_sel + n; ui.lfo_sel = (uint8_t)(r < 0 ? 0 : r >= (int32_t)NLFO ? NLFO - 1u : (uint32_t)r); }
        else sel_channel(n);
        return;
    }
    if (ui.view == V_SET) { if (e == ENC_ALGORITHM) list_value(n); return; }
    if (e == ENC_PRESETS) {
        int32_t i;
        if (ui.view == V_FX && ui.bank[V_FX] % 5u == B_COMP) {   /* COMP: the detector's high-pass */
            int32_t v = (int32_t)kitx[X_FX + FX_M_SCHP] + (n > 0 ? 1 : -1);
            kitx[X_FX + FX_M_SCHP] = (uint8_t)(v < 0 ? 0 : v > 3 ? 3 : v);
            kit_edited = 1;
            fx_apply();
            return;
        }
        if (ui.view != V_STEPS) { say("LENGTH: ON SEQ"); return; }
        for (i = 0; i < (n < 0 ? -n : n); i++) part_set_len(pat, len_ch(), n);
        if (ui.spage * 16u >= pat_len(pat)) ui.spage = (uint8_t)((pat_len(pat) - 1u) / 16u);
        {
            char s[24];
            uint32_t o = orig_len(pat, len_ch());
            cat(catn(cat(s, "LEN "), (int32_t)len_now(pat)), o && o == len_now(pat) ? ": ORIGINAL" : "");
            say(s);
        }
        return;
    }
    if (e == ENC_ALGORITHM && ui.view == V_FX && ui.bank[V_FX] % 5u == B_COMP) {   /* COMP: MIX, dry / wet */
        int32_t v = (int32_t)kitx[X_FX + FX_M_MIX] + 2 * n;
        kitx[X_FX + FX_M_MIX] = (uint8_t)(v < 0 ? 0 : v > 127 ? 127 : v);
        kit_edited = 1;
        fx_apply();
        return;
    }
    if (e == ENC_ALGORITHM) {
        if (ui.held >= 0 && one_ch() && (ui.view == V_STEPS || ui.view == V_SOUND)) {   /* the step's condition */
            int32_t pos = cond_val_pos(mot_get(pat, set.ch, P_COND, (uint32_t)ui.held)) + (n > 0 ? 1 : -1);
            char s[24];
            pos = pos > 19 ? 19 : pos < -37 ? -37 : pos;
            if (mot_set(pat, set.ch, P_COND, (uint32_t)ui.held, cond_pos_val(pos))) say("MOTION FULL");
            cond_text(cat(catn(cat(s, "STEP "), ui.held + 1), ": "), cond_pos_val(pos));
            say(s);
            ui.held_used = 1;
            return;
        }
        if (ui.view == V_STEPS) {               /* the steps move on SEQ only (elsewhere it would move them unseen) */
            int32_t i;
            for (i = 0; i < (n < 0 ? -n : n); i++) track_rotate(pat, set.ch, n);
        }
    }
}

/* ---- copy, paste, clear (GLO) */
static void copy_now(void)
{
    char s[32];
    uint32_t pat = cur_pat();
    ui.clip_pat = (uint8_t)pat; ui.clip_ch = set.ch;
    if (ui.view == V_LFO) {
        ui.clip_kind = CB_LFO; ui.clip_lfo = ui.lfo_sel;
        catn(cat(s, "COPIED LFO "), ui.lfo_sel + 1);
    } else if ((ui.view == V_STEPS || ui.view == V_SOUND) && one_ch() && ui.held >= 0) {
        ui.clip_kind = CB_STEP; ui.clip_step = (uint8_t)ui.held;
        ui.held_used = 1;
        catn(cat(s, "COPIED STEP "), ui.held + 1);
    } else if (ui.view == V_STEPS && one_ch()) {
        ui.clip_kind = CB_TRACK;
        cat(cat(s, "COPIED "), kit.ch[set.ch]);
    } else if (ui.view == V_SOUND && one_ch()) {
        ui.clip_kind = CB_SOUND;
        cat(cat(cat(s, "COPIED "), kit.ch[set.ch]), " SOUND");
    } else {
        ui.clip_kind = CB_PAT;
        catn(cat(s, "COPIED P"), (int32_t)pat + 1);
    }
    memcpy(ui.copied, s, sizeof ui.copied);    /* (the guide says it, while GLO is held) */
}
static void paste_white(uint32_t w)
{
    uint32_t pat = cur_pat();
    undo_begin("PASTE");
    if (ui.view == V_MIX) {                     /* GLO + a kit's key: the sound saved there */
        if (w >= NKIT) return;
        if (kits_used >> w & 1u && w != kit_cur) { ask(A_KIT_SAVE, w, 0, "REPLACE KIT?"); return; }
        if (kit_store(w)) big("SAVE", "FAILED", 1);
        else big_n("SAVED", "KIT ", (int32_t)w + 1);
        return;
    }
    if (ui.clip_kind == CB_PAT) {
        uint32_t to = ui.pbank * 16u + w;
        if (to == ui.clip_pat) return;
        if (pat_used(to)) { ask(A_PASTE_PAT, ui.clip_pat, to, "PASTE OVER NOTES?"); return; }
        if (track_copy(ui.clip_pat, DRUM_NCH, to, 0)) big("MOTION", "FULL", 1);
        else big_n("PASTE", "P", (int32_t)to + 1);
        return;
    }
    if (ui.clip_kind == CB_STEP && (ui.view == V_STEPS || ui.view == V_SOUND) && one_ch()) {
        uint32_t st = ui.spage * 16u + w;
        seq_set(pat, set.ch, st, seq_get(ui.clip_pat, ui.clip_ch, ui.clip_step));
        mot_copy_step(ui.clip_pat, ui.clip_ch, ui.clip_step, pat, st);
        big_n("PASTE", "STEP ", (int32_t)st + 1);
        return;
    }
    if (ui.clip_kind == CB_LFO && ui.view == V_LFO && w < NLFO) {
        lfos[pat][w] = lfos[ui.clip_pat][ui.clip_lfo];
        ui.lfo_sel = (uint8_t)w;
        big_n("PASTE", "LFO ", (int32_t)w + 1);
        return;
    }
    big("NOTHING", "TO PASTE", 1);
}
static void paste_black(uint32_t c)
{
    uint32_t pat = cur_pat();
    undo_begin("PASTE");
    if (ui.clip_kind == CB_TRACK) {
        if (pat == ui.clip_pat && c == ui.clip_ch) return;
        if (ch_used(pat, c)) { ask(A_PASTE_TRACK, c, 0, "PASTE OVER NOTES?"); return; }
        if (track_copy(ui.clip_pat, ui.clip_ch, pat, c)) big("MOTION", "FULL", 1);
        else big("PASTE", kit.ch[c], 0);
        return;
    }
    if (ui.clip_kind == CB_SOUND) {             /* the channel's sound, its name and its sends */
        kit.pat[c] = kit.pat[ui.clip_ch];
        memcpy(kit.ch[c], kit.ch[ui.clip_ch], sizeof kit.ch[c]);   /* (a sound keeps its name: LT pasted on CY is LT) */
        kitx[X_SEND + c * 2u] = kitx[X_SEND + ui.clip_ch * 2u];
        kitx[X_SEND + c * 2u + 1u] = kitx[X_SEND + ui.clip_ch * 2u + 1u];
        kit_edited = 1;
        mot_kit_changed(c);
        big("PASTE", kit.ch[c], 0);
        return;
    }
    big("NOTHING", "TO PASTE", 1);
}
static void clear_now(void)                     /* GLO + REC */
{
    uint32_t pat = cur_pat();
    undo_begin("CLEAR");
    if (lib_on()) { lib_delete(); return; }
    if (ui.view == V_LFO) { lfo_clear(pat, ui.lfo_sel); big_n("CLEAR", "LFO ", ui.lfo_sel + 1); return; }
    if ((ui.view == V_STEPS || ui.view == V_SOUND) && one_ch() && ui.held >= 0) {
        if (ui.view == V_STEPS) seq_set(pat, set.ch, (uint32_t)ui.held, STEP_OFF);
        mot_clear_step(pat, set.ch, (uint32_t)ui.held);
        ui.held_used = 1;
        big_n("CLEAR", "STEP ", ui.held + 1);
        return;
    }
    if (ui.view == V_SOUND && one_ch()) { mot_clear(pat, set.ch); lfo_clear_ch(pat, set.ch); big("CLEAR", "MOTION", 0); return; }
    if (ui.view == V_STEPS && one_ch()) {
        if (ch_used(pat, set.ch)) ask(A_CLEAR_TRACK, set.ch, 0, "CLEAR THE CHANNEL?");
        return;
    }
    if (pat_used(pat)) ask(A_CLEAR_PAT, pat, 0, "CLEAR THE PATTERN?");
    else { track_clear(pat, DRUM_NCH); big("CLEAR", "PATTERN", 0); }
}
static void ask_yes(void)
{
    uint32_t pat = cur_pat(), a = ui.ask_a, b = ui.ask_b, k;
    uint8_t act = ui.ask;
    ui.ask = 0;
    undo_begin(act == A_PASTE_PAT || act == A_PASTE_TRACK ? "PASTE" : act == A_CLEAR_PAT || act == A_CLEAR_TRACK ? "CLEAR" : "EDIT");
    switch (act) {
    case A_PASTE_PAT: if (track_copy(a, DRUM_NCH, b, 0)) big("MOTION", "FULL", 1); else big_n("PASTE", "P", (int32_t)b + 1); break;
    case A_PASTE_TRACK: if (track_copy(ui.clip_pat, ui.clip_ch, pat, a)) big("MOTION", "FULL", 1); else big("PASTE", kit.ch[a], 0); break;
    case A_CLEAR_PAT: track_clear(a, DRUM_NCH); big("CLEAR", "PATTERN", 0); break;
    case A_CLEAR_TRACK: track_clear(pat, a); big("CLEAR", kit.ch[a], 0); break;
    case A_KIT_SAVE: if (kit_store(a)) big("SAVE", "FAILED", 1); else big_n("SAVED", "KIT ", (int32_t)a + 1); break;
    case A_LIB_SAVE: lib_store(a); break;
    case A_FACTORY:
        kit_stage_pat = 0xFFu;                  /* (a staged copy of one: not taken now) */
        for (k = 0; k < DRUM_NKITS; k++) { kit_from8(&kits[k].k, &DRUM_KITS[k]); snd_x_defaults(kits[k].x); }
        kits_used |= (uint8_t)((1u << DRUM_NKITS) - 1u);
        kw_on &= (uint8_t)~((1u << DRUM_NKITS) - 1u);   /* (their edits go; the one playing keeps its own, a *) */
        say(snd_save() ? "SAVE FAILED" : "FACTORY KITS BACK");
        break;
    }
}

/* ---- keys */
static void pattern_key(uint32_t w)
{
    uint32_t p = ui.pbank * 16u + w;
    if (ui.chain_key >= 0) {                    /* holding the first: the chain, in the order tapped */
        if (ui.chain_n < SEQ_NCHAIN) ui.chain[ui.chain_n++] = (uint8_t)p;
        return;
    }
    ui.chain_key = (int8_t)w;
    ui.chain[0] = (uint8_t)p;
    ui.chain_n = 1;
    seq_pattern(p);
    set.pat = (uint8_t)p;
}
static void white_down(uint32_t w)
{
    uint32_t pat = cur_pat(), st = ui.spage * 16u + w;
    if (ui.ask) return;
    if (ui.btn >> BTN_ARP & 1u) {
        if (ui.view == V_STEPS && ui.held < 0) {   /* ARP held + white 1-8: note repeat's rate (not after a ratchet) */
            ui.btn_used |= 1u << BTN_ARP;
            if (w < NR_N) nr_pick((int32_t)w);
            return;
        }
        if (ui.view == V_LIVE) { ui.btn_used |= 1u << BTN_ARP; return; }   /* (HOME: nothing; ARP's tap neither) */
    }
    if (ui.view == V_LIVE && ui.br_mode && !(ui.btn >> BTN_HOME & 1u)) {   /* beat repeat on: white 1-4, while held */
        if (w < BR_N) br_start(w, w);
        return;
    }
    if (rec && ui.view != V_LIVE) {             /* REC: the channels on whites 1-10 (not on HOME: patterns there) */
        if (w < DRUM_NCH) hit(w, 100, 0, 0);
        return;
    }
    if (ui.btn >> BTN_HOME & 1u) {              /* HOME + a white key: that pattern, from anywhere */
        ui.btn_used |= 1u << BTN_HOME;
        seq_pattern(ui.pbank * 16u + w);
        set.pat = (uint8_t)(ui.pbank * 16u + w);
        big_n("PATTERN", "P", (int32_t)(ui.pbank * 16u + w) + 1);
        return;
    }
    if (ui.btn >> BTN_GLO & 1u) { ui.btn_used |= 1u << BTN_GLO; paste_white(w); return; }
    switch (ui.view) {
    case V_LIVE: pattern_key(w); break;
    case V_MIX:
        if (w < NKIT) {
            char s[16];
            if (!(kits_used >> w & 1u)) { say("EMPTY KIT: GLO + KEY SAVES"); break; }
            undo_begin("KIT");
            cat(catn(cat(s, "KIT "), (int32_t)w + 1), w == kit_cur ? " REVERTED" : " LOADED");   /* (the one playing:
                                                 * back to its saved one) */
            kit_load(w);
            say(s);
        }
        break;
    case V_STEPS: case V_SOUND:
        if (!one_ch()) { say("CHOOSE A CHANNEL"); break; }
        if (st >= track_len(pat, set.ch)) { say("PAST THE LENGTH (PRESETS)"); break; }
        if (ui.acc_hold) {                      /* black 11 held: the accent */
            seq_set(pat, set.ch, st, seq_get(pat, set.ch, st) == STEP_ACC ? STEP_ON : STEP_ACC);
            break;
        }
        if (ui.held >= 0) break;
        ui.held = (int8_t)st;
        ui.held_t = fm1_ms;
        ui.held_used = 0;
        ui.held_created = seq_get(pat, set.ch, st) == STEP_OFF;
        if (ui.held_created) seq_set(pat, set.ch, st, STEP_ON);
        mot_preview(pat, set.ch, st);
        break;
    }
}
static void white_up(uint32_t w)
{
    uint32_t pat = cur_pat(), st = ui.spage * 16u + w;
    if (ui.view == V_LIVE && ui.chain_key == (int8_t)w) {
        if (ui.chain_n >= 2u) {
            char s[32], *p = cat(s, "CHAIN");
            uint32_t i;
            for (i = 0; i < ui.chain_n && i < 6u; i++) p = catn(cat(p, " "), ui.chain[i] + 1);
            seq_chain(ui.chain, ui.chain_n);
            say(s);
        }
        ui.chain_key = -1;
        ui.chain_n = 0;
    }
    if (ui.held >= 0 && (uint32_t)ui.held == st) {
        if (!ui.held_created && !ui.held_used && fm1_ms - ui.held_t < 300u) {   /* a tap: off */
            seq_set(pat, set.ch, st, STEP_OFF);
            mot_clear_step(pat, set.ch, st);
        }
        ui.held = -1;
        mot_preview_end();
    }
}
static void black_down(uint32_t b)
{
    if (ui.ask) return;
    if (b == BLACK_ACC) { ui.acc_hold = 1; return; }
    if (b >= DRUM_NCH) return;
    if (ui.nr_on) {                             /* note repeat (SEQ): every black key repeats its channel (HOME and GLO
                                                 * held: no solo, no paste); its first hit now, then while held */
        ui.btn_used |= (ui.btn & (1u << BTN_HOME | 1u << BTN_GLO));
        if (b != set.ch) { set.ch = (uint8_t)b; ui.held = -1; }
        ui.nr_new |= (uint16_t)(1u << b);
        seq_nr_req[b]++;
        return;
    }
    if (ui.btn >> BTN_GLO & 1u) { ui.btn_used |= 1u << BTN_GLO; paste_black(b); return; }
    if (ui.btn >> BTN_HOME & 1u) {              /* HOME + a black key: solo */
        ui.btn_used |= 1u << BTN_HOME;
        seq.solo ^= (uint16_t)(1u << b);
        big("SOLO", seq.solo >> b & 1u ? kit.ch[b] : "OFF", 0);
        return;
    }
    if (ui.view != V_LIVE) {                    /* off LIVE: the channel, heard when stopped; playing, only with REC
                                                 * (then recorded); on LIVE (HOME): its mute */
        if (b != set.ch) { set.ch = (uint8_t)b; ui.held = -1; }
        if (!seq.run || rec) hit(b, 100, 0, 0);
        return;
    }
    seq.mute ^= (uint16_t)(1u << b);
}
static void black_up(uint32_t b)
{
    if (b == BLACK_ACC) ui.acc_hold = 0;
}

/* ---- buttons */
static void open_view(uint32_t v)
{
    if (ui.view == v) ui.bank[v] = (uint8_t)((ui.bank[v] + 1u) % VIEWS[v].n);
    else ui.view = (uint8_t)v;
    ui.held = -1;
    mot_preview_end();
}
static uint32_t undo_combo(uint32_t i)          /* OCT- / OCT+ with HOME held: undo / redo */
{
    return (i == BTN_OCT_DOWN || i == BTN_OCT_UP) && (ui.btn >> BTN_HOME & 1u) && !ui.ask;
}
static void undo_key(int32_t d)
{
    char l[UNDO_LBL];
    if (rec) rec = 0;                           /* (a REC pass ends: what it recorded is the change undone) */
    ui.held = -1;
    if (undo_go(d, l)) big("NOTHING", d < 0 ? "TO UNDO" : "TO REDO", 1);
    else big(d < 0 ? "UNDO" : "REDO", l, 0);
}
static void btn_down(uint32_t i)
{
    ui.btn |= 1u << i;
    ui.btn_used &= ~(1u << i);
    ui.down_t[i] = fm1_ms;
    ui.help = 0;
    ui.guide_seen &= ~(1u << i);
    if (i == BTN_GLO) ui.copied[0] = 0;
    if (undo_combo(i)) { ui.btn_used |= 1u << i | 1u << BTN_HOME; undo_key(i == BTN_OCT_UP ? 1 : -1); return; }
    if (i == BTN_ARP && ui.held >= 0 && (ui.view == V_STEPS || ui.view == V_SOUND) && one_ch() && !ui.ask) {
        uint32_t pat = cur_pat(), n = ratch_of(mot_get(pat, set.ch, P_RATCH, (uint32_t)ui.held)) % 4u + 1u;
        ui.btn_used |= 1u << i;                 /* a step held + ARP: its ratchet, 1 2 3 4 1 .. hits in the step */
        ui.held_used = 1;
        if (mot_set(pat, set.ch, P_RATCH, (uint32_t)ui.held, n > 1u ? n : MOT_NONE)) big("MOTION", "FULL", 1);
        else big_n("RATCHET", "", (int32_t)n);
        return;
    }
    if (i == BTN_PLAY) {
        if (ui.ask) return;
        ui.btn_used |= 1u << i;
        if (seq.ext) say("EXT CLOCK: THE DAW PLAYS");
        else transport(!seq.run);
        return;
    }
    if (i == BTN_REC) {
        ui.btn_used |= 1u << i;
        if (ui.ask) return;
        if (ui.btn >> BTN_HOME & 1u) {          /* HOME + REC: the pattern as last saved */
            int rc;
            undo_begin("REVERT");
            rc = pat_revert(cur_pat());
            ui.btn_used |= 1u << BTN_HOME;
            if (rc == -1) big("NOTHING", "SAVED", 1);
            else if (rc == -2) big("MOTION", "FULL", 1);
            else big_n("REVERT", "P", (int32_t)cur_pat() + 1);
            return;
        }
        if (ui.btn >> BTN_GLO & 1u) { ui.btn_used |= 1u << BTN_GLO; clear_now(); return; }
        rec = !rec;
        return;
    }
    if (i == BTN_SEL) {
        if (ui.ask) { ui.btn_used |= 1u << i; ask_yes(); return; }
        if (lib_on()) {
            ui.btn_used |= 1u << i;
            if (ui.btn >> BTN_GLO & 1u) { ui.btn_used |= 1u << BTN_GLO; lib_save(); }
            else lib_load_row();
            return;
        }
        if (ui.view == V_SET) {
            ui.btn_used |= 1u << i;
            if (ui.row == ROW_CLEAR) { if (pat_used(cur_pat())) ask(A_CLEAR_PAT, cur_pat(), 0, "CLEAR THE PATTERN?"); }
            else if (ui.row == ROW_FACTORY) ask(A_FACTORY, 0, 0, "FACTORY KITS IN 1-3?");
            else list_value(1);
            return;
        }
        ui.fill = mot_fill = 1;                 /* FILL while held */
    }
}
static void btn_up(uint32_t i)
{
    uint32_t used = ui.btn_used >> i & 1u, held = fm1_ms - ui.down_t[i];
    ui.btn &= ~(1u << i);
    if (i == BTN_SEL) ui.fill = mot_fill = 0;
    if (ui.help == i + 1u) { ui.help = 0; return; }
    if (used) return;
    if (ui.guide_seen >> i & 1u) return;        /* (its guide was read: letting go does nothing else) */
    if (ui.ask) {
        if (i == BTN_HOME) { ui.ask = 0; say("NO"); }
        return;
    }
    switch (i) {
    case BTN_HOME:
        ui.view = ui.view == V_LIVE ? V_SET : V_LIVE; ui.held = -1;
        break;
    case BTN_ENV: open_view(V_MIX); break;
    case BTN_SEQ: open_view(V_STEPS); break;
    case BTN_EDIT: open_view(V_SOUND); break;
    case BTN_LFO: open_view(V_LFO); break;
    case BTN_FX: open_view(V_FX); break;
    case BTN_GLO: if (held < 250u) say("HOLD GLO: COPY"); break;
    case BTN_SAVE:
        save_req = 1;
        say("SAVING...");
        break;
    case BTN_ARP:
        if (ui.view == V_STEPS) { ui.nr_on = !ui.nr_on; big("REPEAT", ui.nr_on ? NR_NAME[ui.nr_rate] : "OFF", !ui.nr_on); }
        else if (ui.view == V_SOUND && one_ch()) say("HOLD A STEP + ARP: RATCHET");
        else if (ui.view == V_LIVE) { ui.br_mode = !ui.br_mode; br_end(); big("REPEAT", ui.br_mode ? "ON" : "OFF", !ui.br_mode); }
        else say("ARP: NOTHING HERE");
        break;
    case BTN_OCT_DOWN: case BTN_OCT_UP: {
        int32_t d = i == BTN_OCT_UP ? 1 : -1;
        if (ui.view == V_LIVE) ui.pbank = (uint8_t)(d > 0 ? 1 : 0);
        else if (ui.view == V_SOUND || ui.view == V_FX) {   /* EDIT, FX: the bank before / after (steps' page: on SEQ) */
            uint32_t nb = VIEWS[ui.view].n;
            ui.bank[ui.view] = (uint8_t)((ui.bank[ui.view] + nb + (uint32_t)d) % nb);
            ui.held = -1;
            mot_preview_end();
        } else if (ui.view == V_STEPS && ui.held >= 0 && one_ch()) {   /* a step held: its micro timing, 1/24 of a step */
            uint32_t pat = cur_pat();
            int32_t m = micro_of(mot_get(pat, set.ch, P_MICRO, (uint32_t)ui.held)) + d;
            char s[24], *p;
            micro_held_set(m);
            m = micro_of(mot_get(pat, set.ch, P_MICRO, (uint32_t)ui.held));
            p = cat(catn(cat(s, "STEP "), ui.held + 1), ": ");
            if (m) micro_text(p, (uint32_t)((int32_t)MICRO0 + m)); else cat(p, "ON THE STEP");
            say(s);
        } else if (ui.view == V_STEPS) {
            int32_t pg = (int32_t)ui.spage + d, last = (int32_t)(pat_len(cur_pat()) - 1u) / 16;
            ui.spage = (uint8_t)(pg < 0 ? 0 : pg > last ? last : pg);
            ui.held = -1;
        }
        break;
    }
    }
}
static void ui_saved(int rc) { say(rc ? "SAVE FAILED" : "SAVED"); }

/* ---- the help cards: a button held alone for 0.9 s */
/* ---- input, once a loop */
/* ---- each pattern's kit (app_fm1.c): which has which, the switch the ISR made, the one to make, the next to stage */
static void kit_say(void) { char s[12]; catn(cat(s, "KIT "), (int32_t)kit_cur + 1); say(s); }
static void kit_changed(void)                   /* the kit changed by itself: said; not an undo step; no question on it */
{
    undo_rebase();
    if (ui.ask == A_KIT_SAVE) ui.ask = 0;       /* ("REPLACE KIT?" was about the kit before) */
    kit_say();
}
static void kit_follow(void)                    /* each loop, before the input */
{
    uint32_t p, c, up, want, pat;
    if (kit_take_over()) kit_changed();         /* the ISR swapped the next pattern's kit in as it started */
    for (p = 0; p < SEQ_NPAT; p++) {            /* steps and no kit: the one playing; no steps: none */
        seq_pat_t *q = &seq_song.p[p];
        uint32_t any = 0;
        for (c = 0; c < DRUM_NCH && !any; c++) any = q->t[c].hit[0] | q->t[c].hit[1];
        if (!any) q->kit = 0;
        else if (!q->kit || q->kit > NKIT) q->kit = (uint8_t)(kit_cur + 1u);
    }
    pat = seq.pat;                              /* the pattern playing's kit plays: stopped, one chosen; playing, one the
                                                 * ISR did not take; a kit changed by a paste, an undo, a revert */
    want = seq_song.p[pat % SEQ_NPAT].kit;
    if (want && want - 1u != kit_cur) {
        kit_stage_pat = 0xFFu;                  /* (then the ISR swaps no more: checked again) */
        __asm__ volatile("" ::: "memory");
        if (kit_swaps != kit_swaps_seen) return;   /* (it just did: next time) */
        kit_switch(want - 1u);
        kit_changed();
    }
    up = seq.next;                              /* the pattern after this one: staged for the ISR */
    if (seq.chain_n >= 2u && seq.next == pat) up = seq_chain_l[(seq.chain_at + 1u) % seq.chain_n];
    want = seq.run && up != pat ? seq_song.p[up % SEQ_NPAT].kit : 0u;
    if (kit_swaps != kit_swaps_seen) return;    /* (a swap not taken over yet: the stage is the ISR's) */
    if (!want || want - 1u == kit_cur) { kit_stage_pat = 0xFFu; return; }
    if (kit_stage_pat == up && kit_stage_k == want - 1u && !memcmp(&kit_stage, kit_src(want - 1u), sizeof kit_stage)) return;
    kit_stage_pat = 0xFFu;
    __asm__ volatile("" ::: "memory");
    kit_stage = *kit_src(want - 1u);
    kit_stage_k = (uint8_t)(want - 1u);
    __asm__ volatile("" ::: "memory");
    kit_stage_pat = (uint8_t)up;
}

static void ui_input(void)
{
    uint32_t rel, b = fm1_input_edges(&rel), keys = fm1_in.notes, kp, kr, i, n1 = 0, first = 0, ev;
    int32_t n;
    kit_follow();                               /* (each pattern's kit: before the input, so kit_cur is right) */
    kp = fm1_input_note_edges() | (keys & ~ui.keys);
    kr = (ui.keys & ~keys) | (kp & ~keys);      /* (a tap quicker than a loop: down and up now) */
    ui.keys = keys;
    if (ui.splash && (b || kp)) {               /* the opening, until a key or a button: that press only closes it */
        ui.splash = 0; lcd_fill(0, 0, 240, 240, B_BG); ui.h_top = ui.h_mid = ui.h_bot = ~0u;
        ui.eat_b |= b; ui.eat_k |= kp;
    }
    b &= ~ui.eat_b; kp &= ~ui.eat_k;
    i = rel & ui.eat_b; rel &= ~ui.eat_b; ui.eat_b &= ~i;
    i = kr & ui.eat_k; kr &= ~ui.eat_k; ui.eat_k &= ~i;
    ev = b | rel | kp | kr;                     /* (undo.c: each event goes on a gesture, or starts one, before it is handled) */
    for (i = 0; i < PANEL_NBTN; i++)
        if (b >> i & 1u) {
            if (!undo_combo(i)) undo_event(i == BTN_REC ? UK_REC : UK_BTN + i, 0, i == BTN_REC ? "REC" : (i == BTN_OCT_DOWN || i == BTN_OCT_UP) ? "MICRO" : "EDIT");
            btn_down(i);
        }
    for (i = 0; i < UI_NKEY; i++) {
        if ((kp | kr) >> i & 1u) undo_event(UK_KEYS, 2000u, "STEPS");
        if (kp >> i & 1u) { if (key_role[i] & 0x10u) white_down(key_role[i] & 15u); else black_down(key_role[i] & 15u); }
        if (kr >> i & 1u) { if (key_role[i] & 0x10u) white_up(key_role[i] & 15u); else black_up(key_role[i] & 15u); }
    }
    for (i = 0; i < 7u; i++)
        if ((n = fm1_enc_take(i)) != 0) {
            ev = 1;
            if (i == ENC_SELECT || i == ENC_ALGORITHM || i == ENC_PRESETS) {
                undo_event(UK_ENC + i, 1000u, i == ENC_PRESETS ? (ui.view == V_FX ? "SC HPF" : "LENGTH") :
                           i == ENC_ALGORITHM ? (ui.view == V_FX ? "MIX" : ui.held >= 0 ? "COND" : "SHIFT") : "EDIT");
                enc_left(i, n);
            } else {
                const pref_t *r = &bank_now()->k[(i - ENC_KNOB1) & 3u];
                undo_event(UK_KNOB + i, 1000u, r->kind == R_NONE ? "EDIT" : pref_name(r));
                knob_turn(i - ENC_KNOB1, n);
            }
        }
    for (i = 0; i < PANEL_NBTN; i++)
        if (rel >> i & 1u) {
            if (!(ui.btn_used >> i & 1u)) undo_event(i == BTN_REC ? UK_REC : UK_BTN + i, UNDO_PRESS, (i == BTN_OCT_DOWN || i == BTN_OCT_UP) ? "MICRO" : "EDIT");
            btn_up(i);
        }
    /* GLO alone for 250 ms: copy (any button alone that long: its guide, guide_btn) */
    for (i = 0; i < PANEL_NBTN; i++) if (ui.btn >> i & 1u) { n1++; first = i; }
    if (n1 == 1u && !(ui.btn_used >> first & 1u) && (int32_t)(ui.turn_t - ui.down_t[first]) <= 0 && !ui.keys && !ui.ask) {
        uint32_t t = fm1_ms - ui.down_t[first];
        if (first == BTN_GLO && t >= 250u) { ui.btn_used |= 1u << BTN_GLO; copy_now(); }
    }
    if (mot_prev && (!lib_on() || mot_prev_ch != set.ch)) lib_end();   /* the library left, another channel: its own sound */
    undo_check(ev);                             /* (also: GLO held 250 ms copies, nothing changed) */
    nr_keys();
    br_keys();
    {
        int32_t g = guide_btn();
        if (g >= 0) ui.guide_seen |= 1u << g;   /* (shown: letting go does nothing else) */
    }
}

/* ---- the screen */
static uint32_t hsh(uint32_t h, uint32_t v) { return (h ^ v) * 16777619u; }
/* SETTINGS and the library: no knobs there, so the list takes the top and middle bands (y 0..207), in two strips */
static char *cat_v(char *p, uint32_t mv, uint32_t two)   /* 1260: "1.26"; 3200, one figure: "3.2" */
{
    uint32_t c = mv / 10u % 100u;
    p = cat(catn(p, (int32_t)(mv / 1000u)), ".");
    if (!two) return catn(p, (int32_t)(c / 10u));
    if (c < 10u) p = cat(p, "0");
    return catn(p, (int32_t)c);
}
static uint32_t list_view(void) { return ui.view == V_SET || lib_on(); }   /* SETTINGS, the library: no knobs */
static int32_t guide_btn(void)                  /* a button held alone for 250 ms, no knob turned since: its guide over the
                                                 * page; -1 none (SEL, PLAY, REC: none, they act as they are held / pressed) */
{
    uint32_t b;
    if (ui.ask || ui.keys || fm1_ms < ui.big_until || fm1_ms < ui.touch_until) return -1;
    for (b = 0; b < PANEL_NBTN; b++)
        if (ui.btn == 1u << b) {
            if (b == BTN_SEL || b == BTN_PLAY || b == BTN_REC) return -1;
            if (fm1_ms - ui.down_t[b] < 250u || (int32_t)(ui.turn_t - ui.down_t[b]) > 0) return -1;
            return (int32_t)b;
        }
    return -1;
}
typedef struct { const char *k, *a; } grow_t;
static uint32_t guide_rows(uint32_t b, grow_t *r)   /* with the button held, here: the key, what it does; most used first */
{
    static char rec_a[12];
    uint32_t n = 0;
    switch (b) {                                /* TAP: what a press does; the rest, what goes with it */
    case BTN_HOME:
        r[n++] = (grow_t){"WHITE", "PATTERN"}; r[n++] = (grow_t){"BLACK", "SOLO"};
        r[n++] = (grow_t){"OCT-/+", "UNDO"}; r[n++] = (grow_t){"REC", "SAVED"};
        return n;
    case BTN_SEQ:
        r[n++] = (grow_t){"TAP", "STEPS"}; r[n++] = (grow_t){"BLACK", "CHANNEL"};
        r[n++] = (grow_t){"WHITE", "STEP ON/OFF"}; r[n++] = (grow_t){"OCT-/+", "STEPS 17-64"};
        return n;
    case BTN_EDIT:
        r[n++] = (grow_t){"TAP", "SOUND"}; r[n++] = (grow_t){"AGAIN", "NEXT BANK"};
        r[n++] = (grow_t){"BLACK", "CHANNEL"}; r[n++] = (grow_t){"STEP", "+ KNOB: LOCK"};
        return n;
    case BTN_ENV:
        r[n++] = (grow_t){"TAP", "MIX"}; r[n++] = (grow_t){"BLACK", "CHANNEL"};
        r[n++] = (grow_t){"WHITE", "LOAD A KIT"}; r[n++] = (grow_t){"KNOBS", "LEVEL, SENDS"};
        return n;
    case BTN_LFO:
        r[n++] = (grow_t){"TAP", "8 LFOS"}; r[n++] = (grow_t){"KNOB", "ADD AN LFO"}; r[n++] = (grow_t){"SELECT", "PICK AN LFO"};
        return n;
    case BTN_FX:
        r[n++] = (grow_t){"TAP", "EFFECTS"}; r[n++] = (grow_t){"AGAIN", "NEXT BANK"};
        r[n++] = (grow_t){"OCT-/+", "BANK -/+"}; r[n++] = (grow_t){"SENDS", "EDIT, MIX"};
        return n;
    case BTN_SAVE: r[n++] = (grow_t){"TAP", "SAVE ALL"}; r[n++] = (grow_t){"LIT", "UNSAVED"}; return n;
    case BTN_ARP:
        if (ui.view == V_STEPS) {
            r[n++] = (grow_t){"TAP", ui.nr_on ? "REPEAT OFF" : "NOTE REPEAT"}; r[n++] = (grow_t){"WHITE 1-8", "RATE"};
            r[n++] = (grow_t){"KNOB", "RATE"}; r[n++] = (grow_t){"STEP+ARP", "RATCHET"};
            return n;
        }
        if (ui.view == V_SOUND && one_ch()) { r[n++] = (grow_t){"STEP+ARP", "RATCHET"}; return n; }
        if (ui.view == V_LIVE) { r[n++] = (grow_t){"TAP", ui.br_mode ? "REPEAT OFF" : "BEAT REPEAT"}; r[n++] = (grow_t){"WHITE 1-4", "REPEAT"}; return n; }
        r[n++] = (grow_t){"TAP", "NOTHING"}; return n;
    case BTN_OCT_DOWN: case BTN_OCT_UP: {
        uint32_t up = b == BTN_OCT_UP;
        r[n++] = (grow_t){"SEQ", up ? "STEPS +16" : "STEPS -16"}; r[n++] = (grow_t){"LIVE", up ? "P17-32" : "P1-16"};
        r[n++] = (grow_t){"EDIT, FX", up ? "BANK +" : "BANK -"}; r[n++] = (grow_t){"HOME", up ? "REDO" : "UNDO"};
        return n;
    }
    }
    if (lib_on()) { r[n++] = (grow_t){"SEL", "SAVE"}; r[n++] = (grow_t){"REC", "DELETE"}; return n; }
    if (ui.view == V_LFO) {
        r[n++] = (grow_t){"WHITE", "PASTE"}; r[n++] = (grow_t){"REC", "CLEAR"}; r[n++] = (grow_t){"KNOB", "LFO OFF"};
    } else if ((ui.view == V_STEPS || ui.view == V_SOUND) && one_ch() && ui.held >= 0) {
        r[n++] = (grow_t){"WHITE", "PASTE"}; r[n++] = (grow_t){"REC", "CLEAR"}; r[n++] = (grow_t){"KNOB", "ITS LOCK"};
    } else if (ui.view == V_STEPS && one_ch()) {
        cat(cat(rec_a, "CLEAR "), kit.ch[set.ch]);
        r[n++] = (grow_t){"BLACK", "PASTE"}; r[n++] = (grow_t){"REC", rec_a}; r[n++] = (grow_t){"KNOB", "ITS MOTION"};
    } else if (ui.view == V_SOUND && one_ch()) {
        r[n++] = (grow_t){"BLACK", "PASTE"}; r[n++] = (grow_t){"REC", "ALL MOTION"}; r[n++] = (grow_t){"KNOB", "ITS MOTION"};
    } else {
        catn(cat(rec_a, "CLEAR P"), (int32_t)cur_pat() + 1);
        r[n++] = (grow_t){"WHITE", ui.view == V_MIX ? "SAVE KIT" : "PASTE"}; r[n++] = (grow_t){"REC", rec_a};
    }
    return n;
}
static void draw_guide(uint32_t b)              /* the bubble's frame; a row a gesture, 24 px: the key (grey), what it does
                                                 * (white); GLO: first what it copied (amber); 4 rows at most */
{
    grow_t r[6];
    const felucca_font_t *f = &FONT_ST;
    uint32_t n = 0, k, max = 4u, lh = 30u;
    int32_t kw = 0, y;
    if (b == BTN_GLO && ui.copied[0]) r[n++] = (grow_t){"COPY", ui.copied + 7};
    n += guide_rows(b, r + n);
    if (n > max) n = max;
    for (k = 0; k < n; k++) { int32_t w = text_w(f, r[k].k); if (w > kw) kw = w; }
    box(2, 0, 236, MID_C, B_TXT);
    box(4, 2, 232, MID_C - 4, B_ACT);
    y = 2 + (int32_t)(124u - n * lh) / 2;       /* (the font's own space above the capitals: 2) */
    for (k = 0; k < n; k++, y += (int32_t)lh) {
        uint16_t c = b == BTN_GLO && ui.copied[0] && !k ? B_LOCK : B_TXT;
        const felucca_font_t *fa = 12 + kw + 14 + text_w(f, r[k].a) <= 230 ? f : &FONT_SB;
        tx(12, y, f, r[k].k, k == 0 && c == B_LOCK ? B_LOCK : B_SEC, B_ACT);
        tx(12 + kw + 14, y + (fa == f ? 0 : 6), fa, r[k].a, c, B_ACT);
    }
}
static uint32_t list_full(void) { return list_view() && !ui.help && !ui.ask && guide_btn() < 0; }   /* the list over both bands */
static void draw_set(void)
{
    static const char *const RN[NROW] = {"CLOCK", "REC", "CLEAR PATTERN", "FACTORY KITS", "CPU", "VERSION"};
    uint32_t k, half;
    char s[24];
    for (half = 0; half < 2u; half++) {
        cv_begin(240, LIST_STRIP, B_BG);
        cv_oy = -(int32_t)(half * LIST_STRIP);
        for (k = 0; k < NROW; k++) {
            int32_t y = 2 + (int32_t)k * 29;
            uint16_t bg = k == ui.row ? B_ACT : B_BOX;
            box(2, y, 236, 27, bg);
            tx(12, y + 5, &FONT_SB, RN[k], k == ui.row ? B_TXT : B_SEC, bg);
            s[0] = 0;
            if (k == ROW_CLOCK) cat(s, set.ext ? "EXT" : "INT");
            else if (k == ROW_REC) cat(s, set.rec_free ? "FREE" : "GRID");
            else if (k == ROW_CPU) {            /* the load, and what runs now: cores x MHz */
                char *p = cat(catn(s, (int32_t)ui.cpu), "%  ");
                p = cat(catn(p, (int32_t)cpu_cores()), "x");
                catn(p, (int32_t)cpu_mhz());
            }
            else if (k == ROW_VER) cat(s, APP_VER);
            else cat(s, ">");
            txr(228, y + 5, &FONT_SB, s, k == ui.row ? B_TXT : B_SEC, bg);
        }
        {                                       /* the supplies as set at power-on: to read, not a row to select */
            const uint32_t *v = cpu_power();
            int32_t y = 2 + (int32_t)NROW * 29;
            char *p = cat_v(s, v[0], 1u);
            p = cat_v(cat(p, "  "), v[1], 1u);
            p = cat(p, v[2] ? " DC  " : " LDO  ");
            cat(cat_v(p, v[3], 0u), "V");
            box(2, y, 236, 27, B_BOX);
            tx(12, y + 5, &FONT_SB, "POWER", B_SEC, B_BOX);
            txr(228, y + 5, &FONT_SB, s, B_SEC, B_BOX);
        }
        cv_oy = 0;
        cv_blit(0, half * LIST_STRIP);
    }
}
static uint16_t bar_col(const pref_t *r)        /* a knob's bar: its channel's colour, an LFO's, else white */
{
    return r->kind == R_LEVEL ? chc(r->a) : r->kind == R_PARAM ? chc(set.ch) : r->kind == R_LFO ? B_LFO : B_TXT;
}
static const pref_t *bubble_knob(void)          /* the knob turned less than 1 s ago, for the bubble; 0: none */
{
    const pref_t *r;
    if (fm1_ms >= ui.touch_until || ui.touched < 0 || ui.help || ui.ask || list_view()) return 0;
    r = &bank_now()->k[ui.touched & 3];
    return r->kind == R_NONE ? 0 : r;
}
static const felucca_font_t *huge_f(char c) { return c >= '@' ? &FONT_HUGEL : &FONT_HUGE; }   /* (88 px in two halves) */
static int32_t huge_w(const char *s)
{
    int32_t w = 0;
    for (; *s; s++) { char one[2] = {*s, 0}; w += text_w(huge_f(*s), one); }
    return w;
}
static uint32_t big_bubble(void) { return fm1_ms < ui.big_until && !ui.help && !ui.ask && !list_view(); }
static void draw_big_bubble(void)               /* big(): two words, big (as the knob's bubble) */
{
    box(2, 0, 236, MID_C, B_TXT);
    box(4, 2, 232, MID_C - 4, B_ACT);
    txc(120, 9, &FONT_BIG, ui.big_l1, ui.big_grey ? B_SEC : B_TXT, B_ACT);
    txc(120, 67, &FONT_BIG, ui.big_l2, B_TXT, B_ACT);
}
static void draw_bubble(const pref_t *r)        /* over the page: the knob's name and its value, as big as it fits, its bar */
{
    char s[16];
    int32_t w, x;
    box(2, 0, 236, MID_C, B_TXT);
    box(4, 2, 232, MID_C - 4, B_ACT);
    txc(120, 7, &FONT_LB, pref_name(r), B_SEC, B_ACT);
    pref_text(s, r);
    if (ui.view == V_FX && ui.bank[V_FX] % 5u == B_COMP) {   /* COMP: the value smaller, and the reduction as you turn */
        int32_t g10 = (int32_t)(ui.gr * 10.0f + 0.5f), gw = (int32_t)(ui.gr * 200.0f / 24.0f);   /* (the meter's 24 dB) */
        txc(120, 23, &FONT_BIG, s, B_TXT, B_ACT);
        cv_rect(20, 78, 200, 6, B_TRACK);
        w = (int32_t)(pref_bar(r) * 200u / 127u);
        cv_rect(20, 78, w < 4 ? 4 : w, 6, bar_col(r));
        tx(20, 87, &FONT_LB, "GAIN REDUCTION", B_SEC, B_ACT);
        cat(catn(cat(catn(cat(s, g10 ? "-" : ""), g10 / 10), "."), g10 % 10), " dB");
        txr(220, 87, &FONT_LB, s, B_TXT, B_ACT);
        cv_rect(20, 110, 200, 12, B_TRACK);
        if (gw > 0) cv_rect(20, 110, gw > 200 ? 200 : gw, 12, B_LOCK);
        return;
    }
    if ((w = huge_w(s)) <= 224) {               /* 88 px, a glyph at a time from its half; its digits midway between the
                                                 * name and the bar, 16 px from each (until 2026-11-11 at 16: a slash or a T
                                                 * touched the name, 22 px left under it) */
        const char *p;
        for (x = 120 - w / 2, p = s; *p; p++) { char one[2] = {*p, 0}; x = tx(x, 30, huge_f(*p), one, B_TXT, B_ACT); }
    } else {
        txc(120, 43, &FONT_BIG, s, B_TXT, B_ACT);
    }
    cv_rect(20, 110, 200, 6, B_TRACK);
    w = (int32_t)(pref_bar(r) * 200u / 127u);
    cv_rect(20, 110, w < 4 ? 4 : w, 6, bar_col(r));
}
#define SP_X 40                                 /* STEPS: which 16 steps are shown, over the grid's columns */
#define SP_W 199
static void steps_pages(uint32_t pat)           /* (a pattern longer than 16 steps) a segment for each 16 steps: the ones
                                                 * shown white, the playhead's grey (playing), the others dark; the user's
                                                 * choice of two, 2026-11-13 (the other: the whole pattern, a window) */
{
    uint32_t len = pat_len(pat), n = (len + 15u) / 16u, i, ph = seq.step / 16u;
    for (i = 0; i < n; i++) {
        int32_t x0 = SP_X + (int32_t)(SP_W * i / n), x1 = SP_X + (int32_t)(SP_W * (i + 1u) / n) - 3;
        cv_rect(x0, 67, x1 - x0, 4, i == ui.spage ? B_TXT : seq.run && i == ph ? B_SEC : B_EMPTY);
    }
}
static void draw_top(void)                      /* the 4 knobs, then the bank's dots */
{
    const bank_t *bk = bank_now();
    uint32_t k, pat = cur_pat(), nb = VIEWS[ui.view].n, touched_on = fm1_ms < ui.touch_until;
    if (list_full()) return;                    /* (draw_set / draw_lib have it) */
    cv_begin(240, TOP_H, B_BG);                 /* (down to the middle band: clears what SETTINGS left there) */
    if (list_view()) { cv_blit(0, 0); return; }   /* (its question or help card: no knobs above) */
    for (k = 0; k < 4u; k++) {
        const pref_t *r = &bk->k[k];
        int32_t x = 2 + (int32_t)k * 60;
        uint16_t bg = touched_on && ui.touched == (int8_t)k ? B_ACT : B_BOX;
        char s[16];
        box(x, 2, 58, 64, bg);
        if (r->kind == R_NONE) continue;
        tx(x + 4, 2, &FONT_LB, pref_name(r), B_SEC, bg);
        pref_text(s, r);
        if (text_w(&FONT_V, s) > 53) tx(x + 4, 28, &FONT_V20, s, B_TXT, bg);   /* (too wide for 30 px: 20, low) */
        else tx(x + 4, 20, &FONT_V, s, B_TXT, bg);
        cv_rect(x + 4, 58, 50, 3, B_TRACK);
        cv_rect(x + 4, 58, (int32_t)(pref_bar(r) * 50u / 127u) < 2 ? 2 : (int32_t)(pref_bar(r) * 50u / 127u), 3, bar_col(r));
        if ((r->kind == R_PARAM || r->kind == R_LEVEL) && knob_ch(r) < DRUM_NCH) {   /* the marks: lock, LFO, motion */
            uint32_t ch = knob_ch(r), prm = pref_param(r);
            if (ui.held >= 0 && ui.view == V_SOUND && mot_get(pat, ch, prm, (uint32_t)ui.held) != MOT_NONE) cv_rect(x + 49, 6, 4, 4, B_LOCK);
            else if (lfo_find(pat, ch, prm) >= 0) cv_rect(x + 49, 6, 4, 4, B_LFO);
            else if (mot_has_motion(pat, ch, prm)) cv_rect(x + 49, 6, 4, 4, B_TXT);
        }
    }
    if (nb > 1u)
        for (k = 0; k < nb; k++) cv_rect(120 - (int32_t)(nb - 1u) * 5 + (int32_t)k * 10 - 2, 68, 4, 3, k == ui.bank[ui.view] % nb ? B_TXT : B_USED);
    if (ui.view == V_STEPS && pat_len(pat) > 16u) steps_pages(pat);
    cv_blit(0, 0);
}
static uint32_t top_hash(void)
{
    const bank_t *bk = bank_now();
    uint32_t h = hsh(ui.view * 16u + ui.bank[ui.view], set.ch), k, pat = cur_pat();
    h = hsh(h, fm1_ms < ui.touch_until ? (uint32_t)ui.touched + 1u : 0u);
    h = hsh(h, (uint32_t)ui.held + 1u);
    h = hsh(h, ui.lfo_sel * 2u + list_full());
    h = hsh(h, (uint32_t)guide_btn() + 1u);
    if (ui.view == V_STEPS)                     /* (which 16 steps: steps_pages) */
        h = hsh(hsh(h, pat_len(pat) * 64u + ui.spage), seq.run ? seq.step / 16u + 1u : 0u);
    for (k = 0; k < 4u; k++) {
        const pref_t *r = &bk->k[k];
        h = hsh(h, (uint32_t)pref_get(r) + (uint32_t)r->kind * 1000u);
        if ((r->kind == R_PARAM || r->kind == R_LEVEL) && knob_ch(r) < DRUM_NCH)
            h = hsh(h, (uint32_t)(lfo_find(pat, knob_ch(r), pref_param(r)) + 2) * 3u + mot_has_motion(pat, knob_ch(r), pref_param(r)));
    }
    return h;
}

static uint32_t meter(uint32_t c)               /* 0..54: a channel's hit, fading */
{
    uint32_t dt = fm1_ms - hit_ms[c];
    return (seq.mute >> c & 1u) ? 0u : dt < 300u ? 54u - dt * 54u / 300u : 0u;
}
static void draw_mid_steps(void)
{
    uint32_t c, s, pat = cur_pat(), base = ui.spage * 16u;
    for (c = 0; c < DRUM_NCH; c++) {
        int32_t y = (int32_t)c * SR;              /* (10 rows of SR in the middle band, a gap under them) */
        uint32_t sel = set.ch == c;
        uint16_t bg = sel ? B_ACT : B_BG;
        if (sel) box(0, y, 240, SR, B_ACT);
        tx(4, y + 1 + (SCH - 11) / 2 - 2, &FONT_LB, kit.ch[c], sel ? chc(c) : (seq.mute >> c & 1u) ? B_REC : B_SEC, bg);   /* (its
                                                 * capitals, 11 px from 2 under y, level with the cells) */
        for (s = 0; s < 16u; s++) {
            uint32_t st = base + s, tl = track_len(pat, c), v = st < tl ? seq_get(pat, c, st) : 0u;   /* (past its length: dark) */
            int32_t x = 40 + (int32_t)s * 25 / 2;
            uint16_t col = v == STEP_ACC ? B_TXT : v ? chc(c) : st >= tl ? B_OUT : s % 4u == 0u ? B_BEAT : B_EMPTY;
            if (seq.run && st == seq_pos[c] && !v && st < tl) col = B_HEAD;   /* (the playhead on an empty step) */
            box(x, y + 1, 11, SCH, col);
            if (v) {                            /* the marks, black (7..18:1 on every channel's colour and on an accent;
                                                 * white was 1.3..2.6:1): a lock or a condition, the top right half; micro
                                                 * timing, a bar at the bottom, left early, right late, and the step
                                                 * itself moved that way, 1..3 px */
                int32_t q, m = mot_micro(pat, c, st);   /* (as played: QUANT applied) */
                int32_t mx = m ? m * 2 / MICRO_MAX + (m > 0 ? 1 : -1) : 0;
                if (mx) {                       /* (redrawn moved: its place back to the empty step's) */
                    box(x, y + 1, 11, SCH, B_BG);
                    x += mx;
                    box(x, y + 1, 11, SCH, col);
                    cv_rect(x + (m < 0 ? 0 : 4), y + SCH - 2, 7, 3, B_BG);
                }
                if (mot_lock_mark(pat, c, st))
                    for (q = 0; q < 7; q++) cv_rect(x + 4 + q, y + 1 + q, 7 - q, 1, B_BG);
                if (mot_get(pat, c, P_RATCH, st) != MOT_NONE)   /* a ratchet: a black dot in the middle (how many: the
                                                 * bottom band with the step held; slices 1 px were not seen on the unit) */
                    cv_rect(x + 4, y + 1 + (SCH - 3) / 2, 3, 3, B_BG);
            }
            if (sel && ui.held == (int8_t)st) {
                cv_rect(x - 1, y, 13, 1, B_TXT); cv_rect(x - 1, y + SCH + 1, 13, 1, B_TXT);
                cv_rect(x - 1, y, 1, SCH + 2, B_TXT); cv_rect(x + 11, y, 1, SCH + 2, B_TXT);
            }
        }
    }
}
static void draw_lib(void)                      /* the library's list over both bands: the channel, 9 rows around the one chosen */
{
    uint32_t k, half, top = ui.lib_row < 4u ? 0u : ui.lib_row - 4u;
    char s[24];
    if (top + 9u > LIB_ROWS) top = LIB_ROWS - 9u;
    for (half = 0; half < 2u; half++) {         /* two strips */
        cv_begin(240, LIST_STRIP, B_BG);
        cv_oy = -(int32_t)(half * LIST_STRIP);
        cat(cat(catn(s, (int32_t)set.ch + 1), " "), kit.ch[set.ch]);
        tx(8, 0, &FONT_SB, s, chc(set.ch), B_BG);
        catn(cat(catn(s, (int32_t)ui.lib_row + 1), " / "), (int32_t)LIB_ROWS);
        txr(232, 2, &FONT_LB, s, B_SEC, B_BG);
        for (k = top; k < top + 9u; k++) {
            int32_t y = 30 + (int32_t)(k - top) * 20;   /* (9 rows: down to 210; the text centred, y + 2) */
            uint32_t on = k == ui.lib_row;
            uint16_t bg = on ? B_ACT : B_BG;
            const lib_snd_t *u = lib_user_at(k);
            if (on) box(0, y, 240, 20, B_ACT);
            if (k < SND_LIB_N) {
                tx(8, y + 2, &FONT_LB, SND_LIB[k].name, on ? B_TXT : B_SEC, bg);
                tx(44, y + 2, &FONT_LB, SND_LIB[k].label, on ? B_TXT : B_SEC, bg);
            } else {
                tx(8, y + 2, &FONT_LB, u ? u->name : "-", u && on ? B_TXT : B_SEC, bg);   /* (text: B_SEC at least, 4.5:1) */
                cat(catn(cat(s, "USER "), (int32_t)(k - SND_LIB_N) + 1), u ? "" : "  EMPTY");
                tx(44, y + 2, &FONT_LB, s, u ? B_LFO : B_SEC, bg);
            }
        }
        cv_oy = 0;
        cv_blit(0, half * LIST_STRIP);
    }
}
static uint32_t chain_making(void) { return ui.view == V_LIVE && ui.chain_key >= 0 && ui.chain_n >= 2u; }
static void draw_mid(uint32_t meters_only)      /* meters_only: send just the meters' rows (LIVE 70.., MIX ..109) */
{
    uint32_t c, k, pat = cur_pat();
    char s[40];
    if (list_full()) { if (ui.view == V_SET) draw_set(); else draw_lib(); return; }
    cv_begin(240, MID_H, B_BG);
    if ((c = (uint32_t)guide_btn()) != ~0u) {   /* a button held: its guide */
        draw_guide(c);
    } else if (ui.ask) {
        box(6, 24, 228, 88, B_ACT);
        txc(120, 38, &FONT_SB, ui.ask_q, B_TXT, B_ACT);
        txc(120, 74, &FONT_LB, "SEL YES   HOME NO", B_SEC, B_ACT);
    } else switch (ui.view) {
    case V_LIVE:                                /* the pattern once, big: playing, next (blinking), the chain */
#define LIVE_MY 55                              /* (the meters' top: under the next pattern's bar) */
#define LIVE_CY 46                              /* (the chain being made: over the meters) */
#define LIVE_BX 6                               /* where the pattern is: its bar, the screen's width */
#define LIVE_BW 228
        catn(cat(s, "P"), (int32_t)seq.pat + 1);
        tx(6, -4, &FONT_P, s, B_TXT, B_BG);
        if (seq.next != seq.pat) {              /* the next one, grey, steady (2026-11-12: it blinked) */
            catn(cat(s, ">P"), (int32_t)seq.next + 1);
            tx(76, -4, &FONT_P, s, B_SEC, B_BG);
        }
        if (seq.run && !chain_making()) {       /* where the pattern is (its longest part), the screen's width, cut every
                                                 * 4 steps: grey, white with a next pattern waiting (it changes at the
                                                 * end); stopped, none (until 2026-11-13: only with a next one waiting,
                                                 * under it) */
            uint32_t len = pat_len(seq.pat) ? pat_len(seq.pat) : 16u, st = seq.step + 1u < len ? seq.step + 1u : len, q;
            uint16_t col = seq.next != seq.pat ? B_TXT : B_SEC;
            box(LIVE_BX, 47, LIVE_BW, 4, B_BOX);
            cv_rect(LIVE_BX, 47, (int32_t)((uint32_t)LIVE_BW * st / len), 4, col);
            for (q = 4; q < len; q += 4)        /* (cut every 4 steps, wider every 16: the user's choice of two, 2026-11-13) */
                cv_rect(LIVE_BX + (int32_t)((uint32_t)LIVE_BW * q / len), 47, q % 16u ? 1 : 2, 4, B_BG);
        }
        if (chain_making()) {                   /* (the second key held on: how the chain is growing, below) */
            cat(catn(cat(s, "CHAIN "), (int32_t)ui.chain_n), "/16");
            txr(236, 8, &FONT_SB, s, B_TXT, B_BG);
        } else if (seq.chain_n < 2u) {          /* the pattern's kit, 38 px, white (read standing; * edits not saved);
                                                 * a next pattern with another kit: that one, grey, instead (2026-11-12);
                                                 * with a chain: the chain there, the state band says the kit */
            uint32_t nk = seq_song.p[seq.next % SEQ_NPAT].kit;
            if (seq.next != seq.pat && nk && nk - 1u != kit_cur) {   /* (the next one's instead, steady) */
                catn(cat(s, ">KIT "), (int32_t)nk);
                txr(236, 4, &FONT_V, s, B_SEC, B_BG);
            } else {
                char *q = catn(cat(s, "KIT "), (int32_t)kit_cur + 1);
                if (kit_differs(kit_cur)) cat(q, "*");
                txr(236, 4, &FONT_V, s, B_TXT, B_BG);
            }
        } else {                                /* the chain from the one playing: 3 of it, that one lit */
            tx(170, -2, &FONT_LB, "CHAIN", B_SEC, B_BG);
            for (k = 0; k < 3u && k < seq.chain_n; k++) {
                uint32_t i = (seq.chain_at + k) % seq.chain_n, on = k == 0u;
                int32_t x = 170 + (int32_t)k * 23;
                box(x, 18, 20, 20, on ? B_TXT : B_BOX);
                catn(s, (int32_t)seq_chain_l[i] + 1);
                txc(x + 10, 17, &FONT_LB, s, on ? B_INK : B_SEC, on ? B_TXT : B_BOX);
            }
        }
        if (chain_making()) {                   /* over the meters: up to 16, in the order tapped, the last one lit */
            for (k = 0; k < SEQ_NCHAIN; k++) {
                int32_t x = 4 + (int32_t)(k % 8u) * 29, y = LIVE_CY + (int32_t)(k / 8u) * 41;
                uint32_t on = k < ui.chain_n, last = k + 1u == ui.chain_n;
                uint16_t bg = last ? B_TXT : on ? B_ACT : B_BOX;
                box(x, y, 27, 39, bg);
                if (on) { catn(s, (int32_t)ui.chain[k] + 1); txc(x + 13, y + 4, &FONT_ST, s, last ? B_INK : B_TXT, bg); }
            }
            break;
        }
        for (c = 0; c < DRUM_NCH; c++) {
            int32_t x = 5 + (int32_t)c * 23;          /* (10 columns of 23) */
            uint32_t m = meter(c) * (124u - LIVE_MY - 22u) / 54u, muted = seq.mute >> c & 1u;
            uint16_t bg = c == set.ch ? B_ACT : B_BOX;
            box(x, LIVE_MY, 21, MID_C - LIVE_MY, bg);
            if (m) cv_rect(x + 3, MID_C - 4 - (int32_t)m, 15, (int32_t)m, chc(c));
            if (muted) { cv_rect(x + 3, MID_C - 7, 15, 3, B_REC); txc(x + 10, 97, &FONT_LB, "M", B_REC, bg); }   /* (the name stays) */
            txc(x + 10, LIVE_MY, &FONT_LB, kit.ch[c], muted ? B_REC : c == set.ch ? chc(c) : B_SEC, bg);
        }
        break;
    case V_MIX:
        for (c = 0; c < DRUM_NCH; c++) {
            int32_t x = 5 + (int32_t)c * 23, lv = kit.pat[c].p[P_LEVEL] * 76 / 127;   /* (10 columns of 23) */
            uint32_t cur = c == set.ch, m = meter(c);   /* (the knobs: the channel chosen: framed in its colour, */
            uint16_t bg = cur ? B_ACT : B_BOX;          /* its name dark on it) */
            if (cur) box(x - 2, -2, 25, 106, chc(c));
            box(x, 0, 21, 102, bg);
            if (cur) box(x, 0, 21, 18, chc(c));
            cv_rect(x + 3, 98 - lv, 6, lv, (seq.mute >> c & 1u) ? B_TRACK : chc(c));
            if (m) cv_rect(x + 11, 98 - (int32_t)m * 76 / 54, 7, (int32_t)m * 76 / 54, B_TXT);
            txc(x + 10, 0, &FONT_LB, kit.ch[c], cur ? B_INK : (seq.mute >> c & 1u) ? B_REC : B_SEC, cur ? chc(c) : bg);
        }
        tx(6, 113, &FONT_LB, "KIT", B_SEC, B_BG);
        for (k = 0; k < NKIT; k++) box(40 + (int32_t)k * 24, 110, 20, 20, k == kit_cur ? B_TXT : kits_used >> k & 1u ? B_USED : B_EMPTY);
        break;
    case V_STEPS:
        draw_mid_steps();
        break;
    case V_SOUND:
        if (one_ch()) {
            cat(cat(catn(s, (int32_t)set.ch + 1), " "), kit.ch[set.ch]);
            tx(8, -4, &FONT_P, s, chc(set.ch), B_BG);
        } else {
            tx(8, -4, &FONT_P, "ALL", B_TXT, B_BG);
        }
        for (k = 0; k < NSOUND; k++) {
            int32_t x = 8 + (int32_t)(k % 4u) * 57, y = 60 + (int32_t)(k / 4u) * 38;
            uint32_t on = k == ui.bank[V_SOUND] % NSOUND;
            box(x, y, 53, 32, on ? B_ACT : B_BOX);
            txc(x + 26, y + 8, on ? &FONT_SB : &FONT_LB, BANK_SOUND[k].name, on ? B_TXT : B_SEC, on ? B_ACT : B_BOX);
        }
        break;
    case V_LFO:
        for (k = 0; k < NLFO; k++) {
            const lfo_t *l = &lfos[pat][k];
            int32_t y = (int32_t)k * 17;              /* (8 rows of 17; the text centred, y + 1) */
            uint16_t bg = k == ui.lfo_sel ? B_ACT : B_BG;
            if (k == ui.lfo_sel) box(0, y, 240, 17, B_ACT);
            catn(s, (int32_t)k + 1);
            tx(6, y + 1, &FONT_LB, s, l->used ? B_LFO : B_SEC, bg);
            if (!l->used) { tx(24, y + 1, &FONT_LB, k == ui.lfo_sel ? "EMPTY: HOLD LFO, TURN A KNOB" : "-", B_SEC, bg); continue; }
            s[0] = 0; lfo_name(s, pat, k);
            tx(24, y + 1, &FONT_LB, s, B_TXT, bg);
            catn(cat(cat(cat(cat(cat(s, (l->sync ? LFO_SYNC_N : LFO_HZ_N)[l->rate % LFO_NRATE]), " "), LFO_SHAPE_N[l->shape & 3u]), " "),
                     l->depth > 0 ? "+" : ""), l->depth);
            txr(234, y + 1, &FONT_LB, s, B_SEC, bg);
        }
        break;
    case V_FX:
        if (ui.bank[V_FX] % 5u == B_COMP) {     /* the reduction now, then MIX (ALGORITHM) */
            int32_t gw = (int32_t)(ui.gr * 224.0f / 24.0f), mix = kitx[X_FX + FX_M_MIX];
            tx(8, 4, &FONT_LB, "GAIN REDUCTION", B_SEC, B_BG);
            {
                int32_t g10 = (int32_t)(ui.gr * 10.0f + 0.5f);
                cat(catn(cat(catn(cat(s, g10 ? "-" : ""), g10 / 10), "."), g10 % 10), " dB");
            }
            txr(232, 0, &FONT_V20, s, B_TXT, B_BG);
            box(8, 30, 224, 16, B_BOX);
            if (gw > 0) box(8, 30, gw > 224 ? 224 : gw, 16, B_LOCK);
            for (k = 1; k < 4u; k++) cv_rect(8 + (int32_t)k * 56, 47, 1, 4, B_SEC);   /* 6, 12, 18 dB */
            tx(8, 54, &FONT_LB, "DRY/WET (ALGORITHM)", B_SEC, B_BG);
            cat(catn(s, mix * 100 / 127), "%");
            txr(232, 50, &FONT_V20, s, B_TXT, B_BG);
            box(8, 78, 224, 16, B_BOX);
            if (mix) box(8, 78, mix * 224 / 127, 16, B_TXT);
            tx(8, 104, &FONT_LB, "SC HPF (PRESETS)", B_SEC, B_BG);
            txr(232, 100, &FONT_V20, FX_SCHP_N[kitx[X_FX + FX_M_SCHP] & 3u], kitx[X_FX + FX_M_SCHP] ? B_TXT : B_SEC, B_BG);
            break;
        }
        tx(8, -4, &FONT_P, bank_now()->name, B_TXT, B_BG);   /* the bank, then all five (the sends: MIX, EDIT) */
        for (k = 0; k < 5u; k++) {
            int32_t x = 2 + (int32_t)(k % 3u) * 79, y = 60 + (int32_t)(k / 3u) * 38;
            uint32_t on = k == ui.bank[V_FX] % 5u;
            box(x, y, 77, 34, on ? B_ACT : B_BOX);
            txc(x + 38, y + 9, on ? &FONT_SB : &FONT_LB, BANK_FX[k].name, on ? B_TXT : B_SEC, on ? B_ACT : B_BOX);
        }
        break;
    }
    if (big_bubble()) { draw_big_bubble(); meters_only = 0; }
    else {   /* (asked once: the 1 s can end between two calls, and the second would answer 0: the crashes of November 2026) */
        const pref_t *bk = bubble_knob();
        if (bk) { draw_bubble(bk); meters_only = 0; }
    }
    if (meters_only && !ui.help && !ui.ask && ui.view == V_LIVE) cv_blit_from(0, MID_Y, LIVE_MY);
    else if (meters_only && !ui.help && !ui.ask && ui.view == V_MIX) lcd_blit(0, MID_Y, 240, 98, cv_px);
    else if (meters_only && !ui.help && !ui.ask && ui.view == V_FX) lcd_blit(0, MID_Y, 240, 52, cv_px);   /* (COMP's meter) */
    else cv_blit(0, MID_Y);
}
static uint32_t mid_hash(uint32_t meters)      /* meters: the hits' meters too (LIVE, MIX), apart: ui_draw paces them */
{
    uint32_t h = hsh(ui.view * 64u + ui.help * 4u + ui.ask, ui.bank[ui.view]), c, k, pat = cur_pat();
    h = hsh(hsh(h, (uint32_t)guide_btn() + 1u), (uint32_t)ui.copied[8] * 256u + (uint32_t)ui.copied[11] + (uint32_t)(ui.held + 1) * 65536u);
    h = hsh(h, set.ch); h = hsh(h, pat); h = hsh(h, seq.mute); h = hsh(h, kit_cur); h = hsh(h, kit_differs(kit_cur)); h = hsh(h, seq_song.p[seq.next % SEQ_NPAT].kit);
    if (big_bubble()) h = hsh(hsh(h, 77u), (uint32_t)ui.big_l1[0] * 256u + (uint32_t)ui.big_l2[1] + ui.big_until);
    else {
        const pref_t *bk = bubble_knob();       /* (once: see above) */
        if (bk) h = hsh(hsh(h, (uint32_t)ui.touched + 1000u), (uint32_t)pref_get(bk) * 131u + pref_bar(bk));
    }
    switch (ui.view) {
    case V_FX:
        h = hsh(hsh(h, kitx[X_FX + FX_M_MIX]), kitx[X_FX + FX_M_SCHP]);
        if (meters) h = hsh(h, (uint32_t)(ui.gr * 2.0f));
        break;
    case V_LIVE:
        h = hsh(h, seq.next * 64u + (seq.run ? seq.step + 1u : 0u));   /* (where the pattern is: its bar) */
        h = hsh(h, seq.chain_n * 32u + seq.chain_at);
        h = hsh(h, chain_making() ? ui.chain_n : 0u);
        for (k = 0; chain_making() && k < ui.chain_n; k++) h = hsh(h, ui.chain[k]);
        if (meters) for (c = 0; c < DRUM_NCH; c++) h = hsh(h, meter(c) / 6u);
        break;
    case V_SET: h = hsh(h, ui.row * 16u + set.ext * 4u + set.rec_free * 2u); h = hsh(h, ui.cpu * 4u + cpu_cores()); break;
    case V_SOUND:
        h = hsh(h, ui.lib_row); h = hsh(h, (uint32_t)lib_user.used); h = hsh(h, (uint32_t)(lib_user.used >> 32));
        h = hsh(h, (uint32_t)kit.ch[set.ch % DRUM_NCH][0] << 8 | kit.ch[set.ch % DRUM_NCH][1]);
        break;
    case V_MIX:
        for (c = 0; c < DRUM_NCH; c++) h = hsh(h, kit.pat[c].p[P_LEVEL] * 64u + (meters ? meter(c) / 6u : 0u));
        h = hsh(h, kits_used);
        break;
    case V_STEPS:
        { uint32_t c; for (c = 0; c < DRUM_NCH; c++) h = hsh(h, track_len(pat, c)); }
        h = hsh(h, ui.spage * 256u + pat_len(pat)); h = hsh(h, seq.run ? seq.step + 1u : 0u); h = hsh(h, (uint32_t)ui.held + 1u);
        for (c = 0; c < DRUM_NCH; c++) {
            const seq_track_t *t = &seq_song.p[pat].t[c];
            h = hsh(hsh(hsh(hsh(h, t->hit[0]), t->hit[1]), t->acc[0]), t->acc[1]);
        }
        for (k = 0; k < NLANE; k++) if (lanes[k].used && lanes[k].pat == pat) {
            h = hsh(h, k * 7u + lanes[k].ch);
            if (lanes[k].param == P_RATCH || lanes[k].param == P_MICRO)   /* (its marks; the cells moved) */
                for (c = 0; c < SEQ_NSTEP; c++) h = hsh(h, lanes[k].val[c]);
        }
        for (c = 0; c < DRUM_NCH; c++) h = hsh(h, mot_quant[pat][c]);
        break;
    case V_LFO: for (k = 0; k < NLFO; k++) { const lfo_t *l = &lfos[pat][k]; h = hsh(hsh(h, l->ch), l->used | l->param << 4 | l->rate << 10 | l->shape << 14 | l->sync << 16 | (uint32_t)(uint8_t)l->depth << 20); } h = hsh(h, ui.lfo_sel); break;
    }
    return h;
}
static void play_icon(int32_t x, int32_t y, uint16_t c)   /* a triangle, 8 x 15 */
{
    int32_t r;
    for (r = 0; r < 15; r++) cv_rect(x, y + r, (r < 8 ? r : 14 - r) + 1, 1, c);
}
static uint8_t bot_drop, bot_ok;                /* (the band's right side: what it left out, no overlap; host tests) */
static void draw_bot(void)                      /* the state band */
{
    uint32_t pat = cur_pat(), o = orig_len(pat, len_ch());
    uint16_t band = rec ? B_REC : chc(set.ch);
    char l[40], r[32], *p;
    int32_t x;
    l[0] = r[0] = 0;
    if (fm1_ms < msg_until) { band = B_TXT; cat(l, msg); }
    else if (ui.held >= 0) {                    /* the step held: its condition, its micro timing */
        p = cat(catn(cat(l, "STEP "), ui.held + 1), "  ");
        cond_text(p, mot_get(pat, set.ch, P_COND, (uint32_t)ui.held));
        if (mot_get(pat, set.ch, P_MICRO, (uint32_t)ui.held) != MOT_NONE)
            { while (*p) p++; micro_text(cat(p, "  "), mot_get(pat, set.ch, P_MICRO, (uint32_t)ui.held)); }
        if (mot_get(pat, set.ch, P_RATCH, (uint32_t)ui.held) != MOT_NONE)
            { while (*p) p++; catn(cat(p, "  X"), (int32_t)ratch_of(mot_get(pat, set.ch, P_RATCH, (uint32_t)ui.held))); }
    }
    else if (ui.fill) cat(l, rec ? "REC FILL" : "FILL");
    else if (one_ch()) cat(cat(catn(l, (int32_t)set.ch + 1), " "), kit.ch[set.ch]);
    else cat(l, "ALL");
    cv_begin(240, BOT_H, B_BG);
    box(0, 0, 240, BOT_H, band);
    x = tx(6, BOT_TY, &FONT_ST, l, B_INK, band);
    if (fm1_ms >= msg_until && ui.held < 0 && !ui.fill) {
        const char *pg = ui.view == V_SOUND || ui.view == V_FX || ui.view == V_MIX ? bank_now()->name : VIEWS[ui.view].name;
        char rp[16];
        if (ui.nr_on) { cat(cat(rp, "RPT "), NR_NAME[ui.nr_rate]); pg = rp; }
        if (ui.view == V_LIVE && ui.br_mode) pg = "REPEAT";
        x = tx(x + 10, BOT_TY, &FONT_ST, pg, B_INK, band);
    }
    if (fm1_ms >= msg_until) {
        uint32_t drop;
        const felucca_font_t *f = &FONT_ST;
        for (drop = 0; drop < 3u; drop++) {     /* (no room even at 18 px: LEN unchanged goes, then EXT, then LEN changed) */
            p = r; *p = 0;
            if (ui.view == V_STEPS && o && o != len_now(pat)) { if (drop < 2u) p = cat(catn(cat(catn(cat(p, "LEN "), (int32_t)len_now(pat)), " / "), (int32_t)o), "  "); }
            else if (ui.view == V_STEPS && one_ch() && !ui.spage) { if (drop < 1u) p = cat(catn(cat(p, "LEN "), (int32_t)len_now(pat)), "  "); }
            else if ((ui.view == V_STEPS || ui.view == V_SOUND) && ui.spage) p = cat(catn(cat(catn(p, ui.spage * 16 + 1), "-"), ui.spage * 16 + 16), "  ");
            if (ui.view != V_LIVE) {
                p = catn(cat(p, "P"), (int32_t)seq.pat + 1);
                if (seq.run && seq.next != seq.pat) p = catn(cat(p, ">"), (int32_t)seq.next + 1);
            } else p = cat(p, seq.run ? "PLAY" : "STOP");
            if (set.ext && drop < 1u + (o && o != len_now(pat) ? 0u : 1u)) cat(p, p == r ? "EXT" : " EXT");
            f = x + 24 + text_w(&FONT_ST, r) <= 234 ? &FONT_ST : &FONT_SB;
            if (x + 24 + text_w(&FONT_SB, r) <= 234) break;
        }
        {
            int32_t w = text_w(f, r);
            bot_drop = (uint8_t)drop; bot_ok = x + 10 + (seq.run && ui.view != V_LIVE ? 14 : 0) + w <= 234;
            txr(234, (f == &FONT_ST ? 0 : 3) + BOT_TY, f, r, B_INK, band);
            if (seq.run && ui.view != V_LIVE) play_icon(w ? 222 - w : 224, (BOT_H - 15) / 2, B_INK);   /* (15 px: centred) */
        }
    }
    cv_blit(0, BOT_Y);
}
static uint32_t bot_hash(void)
{
    uint32_t pat = cur_pat(), h = hsh(rec * 2u + ui.fill, set.ch);
    h = hsh(h, fm1_ms < msg_until ? msg_until : 0u);
    h = hsh(h, (uint32_t)ui.held + 1u + ui.nr_on * 256u + ui.nr_rate * 1024u + ui.br_mode * 65536u);
    if (ui.held >= 0) h = hsh(hsh(hsh(h, mot_get(pat, set.ch, P_COND, (uint32_t)ui.held)), mot_get(pat, set.ch, P_MICRO, (uint32_t)ui.held)),
                              mot_get(pat, set.ch, P_RATCH, (uint32_t)ui.held));
    h = hsh(h, ui.view * 16u + ui.bank[ui.view]); h = hsh(h, seq.pat * 64u + seq.next); h = hsh(h, seq.run * 2u + set.ext);
    h = hsh(h, len_now(pat) * 128u + orig_len(pat, len_ch())); h = hsh(h, ui.spage);
    return h;
}
/* ---- the opening (mockup 1e, "it scrolls away"): pattern 1's steps under a playhead at a step every 62 ms, the
 * accents flashing as it passes; from 1.1 s the grid scrolls up and away, a red line grows along the bottom and the
 * logo rises to the middle; it stays until a key or a button */
static uint32_t ease_q10(int32_t x)             /* 1 - (1 - x)^3, x and the result 0..1024 */
{
    uint32_t r;
    if (x <= 0) return 0;
    if (x >= 1024) return 1024;
    r = 1024u - (uint32_t)x;
    return 1024u - r * r / 1024u * r / 1024u;
}
static uint16_t mix565(uint16_t c, uint16_t bg, uint32_t a)   /* a 0..256: bg .. c */
{
    int32_t r0 = bg >> 11, g0 = bg >> 5 & 63, b0 = bg & 31, r1 = c >> 11, g1 = c >> 5 & 63, b1 = c & 31, k = (int32_t)a;
    return (uint16_t)(((r0 + (r1 - r0) * k / 256) << 11) | ((g0 + (g1 - g0) * k / 256) << 5) | (b0 + (b1 - b0) * k / 256));
}
#define OPEN_BG   RGB(0, 0, 0)                  /* the opening's ground: black, from its first frame */
#define LOGO_INK  RGB(245, 242, 250)
#define LOGO_STEP RGB(255, 95, 69)
static void open_logo(int32_t top, uint32_t a, uint16_t bg)     /* 29 x 16 cells of 6 px, a pitch of 7 */
{
    static const uint32_t ROW[16] = {           /* bit x: column x */
        0x04513CFu, 0x06D1451u, 0x0551451u, 0x05513D1u, 0x0451151u, 0x0451251u, 0x044E44Fu, 0x0000000u,
        0x0000000u, 0xE7D139Fu, 0x11113444u, 0x1115444u, 0x1119444u, 0x1111444u, 0x11111444u, 0xE7D1384u};
    uint16_t ink = mix565(LOGO_INK, bg, a);
    uint32_t x, y;
    for (y = 0; y < 16u; y++)
        for (x = 0; x < 29u; x++)
            if (ROW[y] >> x & 1u) cv_rect(19 + (int32_t)x * 7, top + (int32_t)y * 7, 6, 6, ink);
    cv_rect(19 + 24 * 7, top, 34, 48, mix565(LOGO_STEP, bg, a));   /* the lit step, in a letter's place */
}
static void open_frame(uint32_t t)
{
    static const char *const DEMO[DRUM_NCH] = {   /* (pattern 1 empty: the demo groove) */
        "X---x-----x-x---", "----X-------X---", "------------x---", "x-x-x-x-x-x-x---",
        "--------------X-", "----------------", "----------------", "----------------", "---x------x-----", "X---------------"};
    uint32_t up = ease_q10((int32_t)(t - 1100u) * 2253 / 1000) * 170u / 1024u, a, c, s, half, demo = !pat_used(0);
    int32_t name_y = 250 - (int32_t)(up * 62u / 100u);
    uint16_t bg;
    char pw[40];
    a = t < 1100u ? 0u : (t - 1100u) * 3u * 256u / 1000u;
    if (a > 256u) a = 256u;
    {
        const uint32_t *v = cpu_power();
        char *p = cat_v(cat(pw, "CORE "), v[0], 1u);
        p = cat_v(cat(p, "  VDC14 "), v[1], 1u);
        p = cat(p, v[2] ? " DC  IO " : " LDO  IO ");
        cat_v(p, v[3], 0u);
    }
    bg = OPEN_BG;
    for (half = 0; half < 2u; half++) {         /* two strips of 120 rows */
        cv_begin(240, 120, bg);
        cv_oy = -(int32_t)(half * 120u);
        for (c = 0; c < DRUM_NCH; c++)
            for (s = 0; s < 16u; s++) {
                uint32_t v = demo ? (DEMO[c][s] == 'X' ? STEP_ACC : DEMO[c][s] == 'x' ? STEP_ON : STEP_OFF) : seq_get(0, c, s);
                uint32_t past = t * 16u >= s * 1000u, here = t * 16u / 1000u == s;
                uint16_t col = s % 4u == 0u ? B_BEAT : B_EMPTY;
                if (v && past) col = v == STEP_ACC ? B_TXT : chc(c);
                if (here) col = v ? B_TXT : B_HEAD;
                if (v == STEP_ACC && past && t * 16u < s * 1000u + 1500u) col = B_TXT;
                box(24 + (int32_t)(s * 124u / 10u), 40 + (int32_t)c * 13 - (int32_t)up, 10, 11, col);
            }
        cv_rect(0, BOT_Y, (int32_t)(ease_q10((int32_t)(t - 1100u) * 2253 / 1000) * 240u / 1024u), 3, chc(0));
        if (a) {
            open_logo(name_y - 97, a, bg);
            txc(120, name_y + 23, &FONT_LB, APP_VER "   " FELUCCA_ID, mix565(B_SEC, bg, a), bg);
            txc(120, name_y + 42, &FONT_LB, pw, mix565(B_SEC, bg, a), bg);
        }
        cv_oy = 0;
        cv_blit(0, half * 120u);
    }
}

static void ui_draw(void)
{
    uint32_t h;
    if (ui.splash) {
        uint32_t t = fm1_ms - ui.splash;
        if (t < 1900u || !ui.splash_t) {        /* a frame every 25 ms while it moves, then it holds */
            if (!ui.splash_t || fm1_ms - ui.splash_t >= 25u) { ui.splash_t = fm1_ms | 1u; open_frame(t > 1900u ? 1900u : t); }
        }
        return;
    }
    if ((h = top_hash()) != ui.h_top) { ui.h_top = h; draw_top(); }
    if (ui.view == V_FX && ui.bank[V_FX] % 5u == B_COMP) {   /* the meter: the ISR's peak, held, falling 20 dB/s */
        float p = -fx_gr;
        fx_gr = 0.0f;
        ui.gr -= (float)(fm1_ms - ui.gr_t) * 0.02f;
        if (ui.gr < 0.0f) ui.gr = 0.0f;
        if (p > ui.gr) ui.gr = p;
    }
    ui.gr_t = fm1_ms;
    h = mid_hash(0);                            /* a change: at once; the meters alone: 25 frames a second at most */
    if (h != ui.h_mid || (mid_hash(1) != ui.h_midm && fm1_ms - ui.mid_t >= 40u)) {
        draw_mid(h == ui.h_mid);
        ui.h_mid = h; ui.h_midm = mid_hash(1); ui.mid_t = fm1_ms;
    }
    if ((h = bot_hash()) != ui.h_bot) { ui.h_bot = h; draw_bot(); }
}

/* ---- the LEDs */
/* the LEDs wanted this loop, then only the changes written: the scan (in the timer ISR) never sees one switched
 * off and on again mid-loop, a blink or a dropout */
static uint64_t led_on, led_glow, led_on_was, led_glow_was;
static uint8_t leds_shown;
static void want(uint32_t id, int on) { if (on) led_on |= (uint64_t)1 << id; else led_on &= ~((uint64_t)1 << id); }
static void want_glow(uint32_t id, int on) { if (on) led_glow |= (uint64_t)1 << id; else led_glow &= ~((uint64_t)1 << id); }
static void leds_write(void)
{
    uint32_t id;
    for (id = 0; id < 14u + UI_NKEY; id++) {
        uint64_t m = (uint64_t)1 << id;
        if (!leds_shown || ((led_on ^ led_on_was) & m)) fm1_led_key(id, (led_on & m) != 0);
        if (!leds_shown || ((led_glow ^ led_glow_was) & m)) fm1_led_key_glow(id, (led_glow & m) != 0);
    }
    led_on_was = led_on; led_glow_was = led_glow; leds_shown = 1;
}
static void led(uint32_t key, int full, int dim)
{
    want(KEY_LED(key), full);
    want_glow(KEY_LED(key), dim && !full);
}
static void ui_leds(void)
{
    uint32_t k, pat = cur_pat(), blink = (fm1_ms >> 7) & 1u, len = len_now(pat), st, home = ui.btn >> BTN_HOME & 1u;
    static const uint8_t VBTN[NVIEW] = {BTN_HOME, BTN_HOME, BTN_ENV, BTN_SEQ, BTN_EDIT, BTN_LFO, BTN_FX};
    static const uint8_t OPENS[] = {BTN_HOME, BTN_ENV, BTN_SEQ, BTN_EDIT, BTN_LFO, BTN_FX, BTN_GLO, BTN_SAVE, BTN_SEL,
                                    BTN_PLAY, BTN_REC};   /* (PLAY, REC: dim when off, to find them) */
    for (k = 0; k < PANEL_NBTN; k++) { want(k, 0); want_glow(k, 0); }
    for (k = 0; k < sizeof OPENS; k++) want_glow(OPENS[k], 1);
    want(VBTN[ui.view], 1);
    want(BTN_PLAY, seq.run && (seq.step % 4u == 0u || seq.ext));
    want(BTN_REC, rec);
    want(BTN_SEL, ui.fill || ui.ask || ui.view == V_SET || lib_on());
    want(BTN_GLO, ui.btn >> BTN_GLO & 1u);
    want(BTN_SAVE, kits_dirty() ? (fm1_ms >> 8) & 1u : kit_edited || memcmp(set_saved, &set, sizeof set) != 0);   /* (lit:
                                                 * something SAVE would write; blinking: a kit's edits, any kit) */
    if (ui.view == V_STEPS || (ui.view == V_SOUND && one_ch()) || ui.view == V_LIVE) want_glow(BTN_ARP, 1);   /* ARP: dim where it does
                                                 * something, lit: note repeat on, a beat repeat */
    want(BTN_ARP, ui.nr_on || (ui.view == V_LIVE && ui.br_mode) || (ui.btn >> BTN_ARP & 1u));
    if (ui.view == V_LIVE || ui.view == V_SOUND || ui.view == V_FX) { want_glow(BTN_OCT_DOWN, 1); want_glow(BTN_OCT_UP, 1); }
    want(BTN_OCT_UP, (ui.view == V_LIVE && ui.pbank) ||   /* STEPS: OCT+ lit with steps after the 16 shown, OCT- with steps
                                                 * before, off otherwise (until 2026-11-13: both dim always) */
                     (ui.view == V_STEPS && (ui.spage + 1u) * 16u < pat_len(cur_pat())));
    if (ui.view == V_STEPS) want(BTN_OCT_DOWN, ui.spage > 0u);
    if (home && !ui.ask) {                      /* HOME held: lit; OCT- / OCT+ dim if there is something to undo / redo */
        want(BTN_HOME, 1);
        want(BTN_OCT_DOWN, 0); want_glow(BTN_OCT_DOWN, undo.back >= 0);
        want(BTN_OCT_UP, 0); want_glow(BTN_OCT_UP, undo.fwd >= 0);
    }
    for (k = 0; k < 16u; k++) {                 /* the white keys */
        int full = 0, dim = 0;
        if (ui.view == V_STEPS && (ui.btn >> BTN_ARP & 1u)) { dim = k < NR_N; full = k == ui.nr_rate; }   /* ARP held: the rates */
        else if (ui.view == V_LIVE && !home && ui.br_mode) { dim = k < BR_N; full = (int8_t)k == ui.br_k; }   /* (beat repeat: its lengths) */
        else if (rec && !home && ui.view != V_LIVE) { dim = k < DRUM_NCH; full = k < DRUM_NCH && fm1_ms - hit_ms[k] < 80u; }
        else if (ui.view == V_LIVE || home) {   /* (HOME held, on any page: the patterns its white keys pick) */
            uint32_t p = ui.pbank * 16u + k, i, in_chain = 0;   /* playing: full; next: full, blinking fast; in the
                                                 * chain: dim, blinking slow; with notes: dim; empty: off */
            for (i = 0; seq.chain_n >= 2u && i < seq.chain_n; i++) in_chain |= seq_chain_l[i] == p;
            if (p == seq.pat) full = 1;
            else if (p == seq.next) { full = (int)blink; dim = !blink; }
            else if (in_chain) dim = (int)(fm1_ms >> 9 & 1u);
            else dim = pat_used(p) != 0;
        } else if (ui.view == V_MIX) { full = k == kit_cur; dim = k < NKIT && (kits_used >> k & 1u); }
        else if ((ui.view == V_STEPS || ui.view == V_SOUND) && one_ch()) {
            st = ui.spage * 16u + k;
            if (st < len) {
                uint32_t v = seq_get(pat, set.ch, st);
                full = ui.acc_hold ? v == STEP_ACC : v != STEP_OFF;
                dim = st % 4u == 0u;
                if (seq.run && seq_pos[set.ch] == st) full = !full;
                if (ui.held == (int8_t)st) full = 1;
            }
        } else if (ui.view == V_LFO) { full = k == ui.lfo_sel; dim = k < NLFO && lfos[pat][k].used; }
        if (ui.keys >> WHITE[k] & 1u) full = 1;
        led(WHITE[k], full, dim);
    }
    for (k = 0; k < 11u; k++) {                 /* the black keys: 1-10 the channels, 11 the accents */
        int full = 0, dim = 0;
        if (k < DRUM_NCH) {
            uint32_t silent = (seq.mute >> k & 1u) || (seq.solo && !(seq.solo >> k & 1u));
            full = !silent && fm1_ms - hit_ms[k] < 80u;   /* on every page as on HOME: bright as it plays, dim */
            dim = !silent;                                /* when silent, dark when muted; the one chosen blinks, stopped; */
            if (k == set.ch && !silent) {                 /* playing, on the pages its key chooses (not LIVE): lit, off as it plays */
                if (!seq.run) full |= (int)blink;
                else if (ui.view != V_LIVE) full = !full;
            }
        } else if (k == BLACK_ACC) { full = ui.acc_hold; dim = 1; }
        led(BLACK[k], full, dim);
    }
    leds_write();
}

static void ui_init(void)
{
    uint32_t k;
    for (k = 0; k < 16u; k++) key_role[WHITE[k]] = (uint8_t)(0x10u | k);
    for (k = 0; k < 11u; k++) key_role[BLACK[k]] = (uint8_t)(0x20u | k);
    ui.view = V_LIVE;
    ui.nr_rate = 0;                             /* (note repeat at 1/4: seq_nr_div's 2) */
    ui.br_k = -1;
    ui.h_top = ui.h_mid = ui.h_bot = ~0u;
}

/* ---- the crash screen (platform.c fm1_fault): what it was, each value named, and the time left to photograph it */
static const char *crash_dbg_name(uint32_t dbg)
{
    static const char N[][15] = {"WATCHDOG", "PRP RD MMU", "PRP WR LIMIT", "PRP WR MMU", "AXI RD INVALID", "AXI WR INVALID", "C1 RD MMU", "C0 RD MMU",
                                      "C1 WR MMU", "C0 WR MMU", "C1 PC LIMIT", "C1 WR LIMIT", "C0 PC LIMIT", "C0 WR LIMIT", "DMC", "DBG",
                                      "C1 FETCH BUS", "C1 RD BUS", "C1 WR BUS", "C0 FETCH BUS", "C0 RD BUS", "C0 WR BUS", "RESERVED", "RESERVED"};
    uint32_t i;
    for (i = 0; i < 24u; i++) if (dbg >> i & 1u) return N[i];
    return "DBG";
}
static char *hex8(char *p, uint32_t v)
{
    uint32_t k;
    for (k = 0; k < 8u; k++) *p++ = "0123456789ABCDEF"[(v >> (28u - 4u * k)) & 15u];
    *p = 0;
    return p;
}
static const char *crash_cause(uint32_t emu, uint32_t dbg)   /* the AC79 SDK's names (cpu/wl82/debug.c), the likely ones first */
{
    static const struct { uint8_t bit; char name[15]; } E[] = {{1, "ILLEGAL INSTR"}, {0, "MISALIGN"}, {3, "STACK OVERFLOW"}, {2, "DIV BY 0"}, {8, "WATCHPOINT"}};
    uint32_t i;
    for (i = 0; i < sizeof E / sizeof E[0]; i++) if (emu >> E[i].bit & 1u) return E[i].name;
    if (dbg) return crash_dbg_name(dbg);
    if (emu >> 16 & 31u) return "FPU";
    return "UNKNOWN";
}
static void crash_row(int32_t y, const char *k, const char *v)
{
    tx(8, y, &FONT_LB, k, B_SEC, B_BG);
    tx(84, y - 1, &FONT_SB, v, B_TXT, B_BG);
}
static void crash_foot(uint32_t left)           /* the time left, on its own strip */
{
    char s[20];
    cv_begin(240, 28, B_BG);
    tx(8, 4, &FONT_LB, "PHOTOGRAPH THIS", B_SEC, B_BG);
    cat(catn(cat(s, "RESTART "), (int32_t)left), " s");
    txr(232, 3, &FONT_SB, s, B_TXT, B_BG);
    cv_blit(0, 212);
}
static void crash_draw(uint32_t pc, uint32_t rets, uint32_t emu, uint32_t dbg, uint32_t sp, uint32_t core, uint32_t up_ms, uint32_t count)
{
    uint32_t half;
    char s[32], *p;
    idle_ok = 0;                                /* (no MIDI from the screen's waits now) */
    for (half = 0; half < 2u; half++) {
        cv_begin(240, 106, B_BG);
        cv_oy = -(int32_t)(half * 106u);
        cv_rect(0, 0, 240, 32, B_REC);
        tx(8, -1, &FONT_ST, APP_NAME " CRASH", B_INK, B_REC);
        crash_row(36, "CAUSE", crash_cause(emu, dbg));
        hex8(s, pc); crash_row(58, "PC", s);
        hex8(s, rets); crash_row(80, "CALLER", s);
        hex8(cat(hex8(s, emu), "/"), dbg); crash_row(102, "EMU/DBG", s);
        hex8(s, sp); crash_row(124, "SP", s);
        cat(catn(cat(catn(cat(catn(s, (int32_t)cpu_cores()), " x "), (int32_t)cpu_mhz()), " MHz  CORE "), (int32_t)core), "");
        crash_row(146, "CPU", s);
        p = cat(catn(s, (int32_t)(up_ms / 60000u)), " m ");
        cat(catn(p, (int32_t)(up_ms / 1000u % 60u)), " s");
        crash_row(168, "UP", s);
        catn(s, (int32_t)count); crash_row(190, "CRASHES", s);
        cv_oy = 0;
        cv_blit(0, half * 106u);
    }
}
