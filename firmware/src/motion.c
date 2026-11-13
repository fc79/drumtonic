/* SPDX-License-Identifier: GPL-3.0-only */
/* The FM-1's knob motion, p-locks and step conditions (motion.h), and what reaches the engine: each channel's patch
 * as the kit has it, with its lanes' values and its LFOs on top.
 *
 *   main loop   mot_find / mot_alloc / mot_free, mot_touch (a knob turned while playing), the edits below; it changes
 *               the kit and calls mot_kit_changed(ch); it never writes drum_cfg itself
 *   ISR         mot_step (the sequencer, before a step's hits: SEQ_STEP_HOOK), mot_cond (SEQ_COND), mot_tick (every
 *               render, before drum_render: the glides, the LFOs, the kit's changes)
 *
 * A lane is written whole before used is set (and gen tells the ISR it was reused). Recording: with REC on, a knob
 * turned while playing writes its value on the steps it is turned at, and a pause up to MOT_GAP steps is filled in;
 * while the turning goes on the knob is heard, not the lane. With REC off, a turned knob plays instead of its lane
 * for one pass. Two steps next to each other with different values glide from one to the other. */
#include "motion.h"

static lane_t lanes[NLANE];
static struct {                                 /* (ISR) */
    uint8_t gen, from, to, ramp;                /* the lane as last seen; this step's value, the next one's */
    volatile uint8_t rec_req, hold_req;         /* main sets, ISR clears: write the knob here / let it play a pass */
    uint8_t rec_last, rec_quiet, hold_left;
} lane_rt[NLANE];
static volatile uint16_t mot_dirty;             /* main / ISR: bit = channel whose patch must be compiled again */
static uint8_t mot_ov[DRUM_NCH][P_COND];        /* (ISR) the lanes' values now, MOT_NONE where none plays */
static uint8_t mot_cond_now[DRUM_NCH];          /* (ISR) this step's condition, per channel */
static int8_t mot_lfo_off[DRUM_NCH][P_COND];    /* (ISR, lfo.c) the LFOs' offsets now */
static uint8_t mot_fill;                        /* main: SEL is held */
static uint32_t mot_rng = 0x2545F491u, mot_step_age, mot_step_len = 1;   /* (ISR) steps' length in renders, for glides */
static uint8_t mot_run, mot_pat = 0xFF;         /* (ISR) what the lanes were applied for */
static uint8_t mot_send[DRUM_NCH][2];           /* (ISR) the sends as applied (fx_*) */
static uint8_t mot_quant[SEQ_NPAT][DRUM_NCH];   /* main: QUANT, 0..100 %: how far each part's micro timing is pulled to
                                                 * the grid as it plays (the lane keeps it as set); SAVE writes it into
                                                 * the lanes and puts it back to 0 (mot_quant_apply), so it is never saved */

static const drum_patch_t *volatile mot_prev;   /* main: a library sound heard on mot_prev_ch instead of its own (ui_fm1.c) */
static volatile uint8_t mot_prev_ch;
static uint32_t mot_base(uint32_t ch, uint32_t prm)   /* the knob's own value */
{
    const drum_patch_t *q = mot_prev;
    if (prm < DRUM_NP) return q && ch == mot_prev_ch ? q->p[prm] : kit.pat[ch].p[prm];
    return prm < P_COND ? kitx[X_SEND + ch * 2u + (prm - P_REV)] : MOT_NONE;
}

/* ---- the main loop's side */
static int32_t mot_find(uint32_t pat, uint32_t ch, uint32_t prm)
{
    uint32_t i;
    for (i = 0; i < NLANE; i++)
        if (lanes[i].used && lanes[i].pat == pat && lanes[i].ch == ch && lanes[i].param == prm) return (int32_t)i;
    return -1;
}
static int32_t mot_alloc(uint32_t pat, uint32_t ch, uint32_t prm)   /* -1: all 160 in use (MOTION FULL) */
{
    int32_t i = mot_find(pat, ch, prm);
    uint32_t k;
    if (i >= 0) return i;
    for (k = 0; k < NLANE; k++)
        if (!lanes[k].used) {
            lane_t *l = &lanes[k];
            memset(l->val, MOT_NONE, sizeof l->val);
            l->pat = (uint8_t)pat; l->ch = (uint8_t)ch; l->param = (uint8_t)prm;
            l->gen++;
            __asm__ volatile("" ::: "memory");
            l->used = 1;
            return (int32_t)k;
        }
    return -1;
}
static void mot_free(uint32_t i)
{
    lanes[i].used = 0;
    lanes[i].gen++;
    mot_dirty |= (uint16_t)(1u << lanes[i].ch);
}
static uint32_t mot_lane_empty(const lane_t *l)
{
    uint32_t s;
    for (s = 0; s < SEQ_NSTEP; s++) if (l->val[s] != MOT_NONE) return 0;
    return 1;
}
static void mot_kit_changed(uint32_t ch) { mot_dirty |= (uint16_t)(1u << (ch % DRUM_NCH)); }
static void mot_all_changed(void) { mot_dirty = (uint16_t)((1u << DRUM_NCH) - 1u); }

/* a knob turned while playing: REC on, it is recorded; off, it plays instead of its lane for a pass. -1: MOTION FULL */
static int mot_touch(uint32_t ch, uint32_t prm)
{
    int32_t i;
    if (!seq.run || prm >= P_COND) return 0;
    if (rec) {
        if ((i = mot_alloc(seq.pat, ch, prm)) < 0) return -1;
        lane_rt[i].rec_req = 1;
    } else if ((i = mot_find(seq.pat, ch, prm)) >= 0) {
        lane_rt[i].hold_req = 1;
    }
    return 0;
}

static uint32_t mot_get(uint32_t pat, uint32_t ch, uint32_t prm, uint32_t st)   /* a step's own value, MOT_NONE: none */
{
    int32_t i = mot_find(pat, ch, prm);
    return i < 0 ? MOT_NONE : lanes[i].val[st % SEQ_NSTEP];
}
static int mot_set(uint32_t pat, uint32_t ch, uint32_t prm, uint32_t st, uint32_t v)   /* MOT_NONE clears it */
{
    int32_t i = v == MOT_NONE ? mot_find(pat, ch, prm) : mot_alloc(pat, ch, prm);
    if (i < 0) return v == MOT_NONE ? 0 : -1;
    lanes[i].val[st % SEQ_NSTEP] = (uint8_t)v;
    if (v == MOT_NONE && mot_lane_empty(&lanes[i])) mot_free((uint32_t)i);
    mot_dirty |= (uint16_t)(1u << ch);
    return 0;
}
static uint32_t mot_lock_mark(uint32_t pat, uint32_t ch, uint32_t st)   /* a lock or a condition (not micro timing) */
{
    uint32_t i;
    for (i = 0; i < NLANE; i++)
        if (lanes[i].used && lanes[i].pat == pat && lanes[i].ch == ch && lanes[i].param != P_MICRO &&
            lanes[i].param != P_RATCH && lanes[i].val[st % SEQ_NSTEP] != MOT_NONE)
            return 1;
    return 0;
}
static uint32_t mot_has_motion(uint32_t pat, uint32_t ch, uint32_t prm) { return mot_find(pat, ch, prm) >= 0; }

/* the lanes of a pattern's channel (ch DRUM_NCH: all its channels), for the edits on the steps */
#define MOT_EACH(i, p_, c_) for (i = 0; i < NLANE; i++) if (lanes[i].used && lanes[i].pat == (p_) && \
                                                             ((c_) >= DRUM_NCH || lanes[i].ch == (c_)))
static void mot_clear(uint32_t pat, uint32_t ch)
{
    uint32_t i;
    MOT_EACH(i, pat, ch) mot_free(i);
}
static void mot_clear_step(uint32_t pat, uint32_t ch, uint32_t st)
{
    uint32_t i;
    MOT_EACH(i, pat, ch) {
        lanes[i].val[st % SEQ_NSTEP] = MOT_NONE;
        if (mot_lane_empty(&lanes[i])) mot_free(i);
    }
    mot_dirty |= (uint16_t)(1u << ch);
}
/* copy pattern sp's channel sc onto dp's dc (sc DRUM_NCH: every channel onto itself); -1: MOTION FULL (some not) */
static int mot_copy(uint32_t sp, uint32_t sc, uint32_t dp, uint32_t dc)
{
    static lane_t tmp[NLANE] POOL;              /* (the sources first: a paste onto itself stays whole) */
    uint32_t i, n = 0;
    int rc = 0;
    MOT_EACH(i, sp, sc) tmp[n++] = lanes[i];
    if (sc >= DRUM_NCH) { if (sp != dp) memcpy(mot_quant[dp % SEQ_NPAT], mot_quant[sp % SEQ_NPAT], sizeof mot_quant[0]); }   /* (QUANT
                                                 * goes with them; no memmove on the chip) */
    else mot_quant[dp % SEQ_NPAT][dc % DRUM_NCH] = mot_quant[sp % SEQ_NPAT][sc % DRUM_NCH];
    mot_clear(dp, sc >= DRUM_NCH ? DRUM_NCH : dc);
    for (i = 0; i < n; i++) {
        int32_t k = mot_alloc(dp, sc >= DRUM_NCH ? tmp[i].ch : dc, tmp[i].param);
        if (k < 0) { rc = -1; break; }
        memcpy(lanes[k].val, tmp[i].val, sizeof lanes[k].val);
    }
    mot_all_changed();
    return rc;
}
static void mot_copy_step(uint32_t sp, uint32_t sc, uint32_t ss, uint32_t dp, uint32_t ds)
{
    uint32_t i, prm;
    mot_clear_step(dp, sc, ds);
    for (prm = 0; prm < P_NTARGET; prm++)
        if ((i = mot_get(sp, sc, prm, ss)) != MOT_NONE) mot_set(dp, sc, prm, ds, i);
}
static void mot_rotate(uint32_t pat, uint32_t ch, uint32_t len, int32_t d)   /* with the steps: d +1 later */
{
    uint32_t i, s;
    if (len < 2u) return;
    MOT_EACH(i, pat, ch) {
        uint8_t *v = lanes[i].val, t;
        if (d > 0) { t = v[len - 1u]; for (s = len - 1u; s; s--) v[s] = v[s - 1u]; v[0] = t; }
        else { t = v[0]; for (s = 0; s + 1u < len; s++) v[s] = v[s + 1u]; v[len - 1u] = t; }
    }
    mot_all_changed();
}
static void mot_repeat(uint32_t pat, uint32_t ch, uint32_t from, uint32_t to)   /* lengthened: steps 0..from-1 again and again */
{
    uint32_t i, s;
    MOT_EACH(i, pat, ch)
        for (s = from; s < to && s < SEQ_NSTEP; s++) lanes[i].val[s] = lanes[i].val[s % from];
}
static void mot_release(void);
static void mot_preview(uint32_t pat, uint32_t ch, uint32_t st)   /* stopped, a step held: its locks are heard */
{
    uint32_t i;
    if (seq.run) return;
    MOT_EACH(i, pat, ch)
        if (lanes[i].param < P_COND) mot_ov[ch][lanes[i].param] = lanes[i].val[st % SEQ_NSTEP];
    mot_dirty |= (uint16_t)(1u << ch);
}
static void mot_preview_end(void)
{
    if (!seq.run) mot_release();
}
static uint32_t mot_count(void)
{
    uint32_t i, n = 0;
    for (i = 0; i < NLANE; i++) n += lanes[i].used;
    return n;
}

/* ---- the ISR's side */
static uint32_t mot_base_isr(uint32_t ch, uint32_t prm)   /* the knob's own value as heard: the kit mot_kit, mot_kx
                                                 * (kit, kitx; a pattern's next kit, swapped in: app_fm1.c) */
{
    const drum_patch_t *q = mot_prev;
    if (prm < DRUM_NP) return q && ch == mot_prev_ch ? q->p[prm] : mot_kit->pat[ch].p[prm];
    return prm < P_COND ? mot_kx[X_SEND + ch * 2u + (prm - P_REV)] : MOT_NONE;
}
static void mot_compile(uint32_t ch)            /* the kit's patch, the lanes' values over it, the LFOs on top */
{
    drum_patch_t p;
    uint32_t k;
    for (k = 0; k < P_COND; k++) {
        int32_t v = mot_ov[ch][k] != MOT_NONE ? mot_ov[ch][k] : (int32_t)mot_base_isr(ch, k);
        v += mot_lfo_off[ch][k];
        v = v < 0 ? 0 : v > 127 ? 127 : v;
        if (k < DRUM_NP) p.p[k] = (uint8_t)v;
        else mot_send[ch][k - P_REV] = (uint8_t)v;
    }
    drum_set_patch(ch, &p);
    drum_cfg[ch].srev = (int32_t)mot_send[ch][0] * mot_send[ch][0] * 2;   /* (a square law: 127 -> 0.98) */
    drum_cfg[ch].sdly = (int32_t)mot_send[ch][1] * mot_send[ch][1] * 2;
}

static void mot_release(void)                   /* stopped, or another pattern: every knob back to its own value */
{
    memset(mot_ov, MOT_NONE, sizeof mot_ov);
    memset(mot_cond_now, MOT_NONE, sizeof mot_cond_now);
    mot_dirty = (uint16_t)((1u << DRUM_NCH) - 1u);
}

static void mot_step(uint32_t pat, uint32_t st)  /* SEQ_STEP_HOOK: before step st's hits */
{
    uint32_t i, len, nx, ch;
    uint16_t dirty = 0;
    mot_step_len = mot_step_age ? mot_step_age : 1u;
    mot_step_age = 0;
    if (pat != mot_pat || !mot_run) {           /* another pattern: its kit, if it has another (app_fm1.c), then its lanes */
        if (kit_isr_take(pat)) dirty = (uint16_t)((1u << DRUM_NCH) - 1u);
        mot_release(); mot_pat = (uint8_t)pat; mot_run = 1;
    }
    memset(mot_cond_now, MOT_NONE, sizeof mot_cond_now);
    for (i = 0; i < NLANE; i++) {
        lane_t *l = &lanes[i];
        uint32_t v;
        if (lane_rt[i].gen != l->gen) {         /* a new lane, or this one reused: start afresh */
            lane_rt[i].gen = l->gen;
            lane_rt[i].rec_last = lane_rt[i].rec_quiet = lane_rt[i].hold_left = 0;
            lane_rt[i].ramp = 0;
            if (l->ch < DRUM_NCH && l->param < P_COND) { mot_ov[l->ch][l->param] = MOT_NONE; dirty |= (uint16_t)(1u << l->ch); }
        }
        if (!l->used || l->pat != pat || l->ch >= DRUM_NCH) continue;
        ch = l->ch;
        st = seq_pos[ch];                       /* (each part on its own length) */
        len = seq_tlen(&seq_song.p[pat], ch);
        nx = st + 1u >= len ? 0u : st + 1u;
        if (l->param == P_COND) { mot_cond_now[ch] = l->val[st]; continue; }
        if (l->param >= P_COND) continue;
        if (lane_rt[i].rec_req) {               /* REC: the knob's value now, on this step; a short pause filled in */
            uint32_t now = mot_base(ch, l->param), last = lane_rt[i].rec_last, s;
            lane_rt[i].rec_req = 0;
            if (last && st + 1u > last && st + 1u - last <= MOT_GAP && last - 1u < len) {
                uint32_t a = l->val[last - 1u], gap = st + 1u - last;
                for (s = 1; s < gap && a != MOT_NONE; s++)
                    l->val[last - 1u + s] = (uint8_t)(a + ((int32_t)now - (int32_t)a) * (int32_t)s / (int32_t)gap);
            }
            l->val[st] = (uint8_t)now;
            lane_rt[i].rec_last = (uint8_t)(st + 1u);
            lane_rt[i].rec_quiet = 0;
            lane_rt[i].hold_left = 0;
            if (mot_ov[ch][l->param] != MOT_NONE) { mot_ov[ch][l->param] = MOT_NONE; dirty |= (uint16_t)(1u << ch); }
            continue;                           /* (the knob is heard while it turns) */
        }
        if (lane_rt[i].rec_last && ++lane_rt[i].rec_quiet > MOT_GAP) lane_rt[i].rec_last = 0;
        if (lane_rt[i].hold_req) { lane_rt[i].hold_req = 0; lane_rt[i].hold_left = (uint8_t)len; }
        if (lane_rt[i].hold_left) {
            lane_rt[i].hold_left--;
            if (mot_ov[ch][l->param] != MOT_NONE) { mot_ov[ch][l->param] = MOT_NONE; dirty |= (uint16_t)(1u << ch); }
            continue;
        }
        if (lane_rt[i].rec_last) continue;
        v = l->val[st];
        lane_rt[i].from = (uint8_t)v;
        lane_rt[i].to = l->val[nx];
        lane_rt[i].ramp = v != MOT_NONE && l->val[nx] != MOT_NONE && l->val[nx] != v;
        if (mot_ov[ch][l->param] != v) { mot_ov[ch][l->param] = (uint8_t)v; dirty |= (uint16_t)(1u << ch); }
    }
    for (ch = 0; ch < DRUM_NCH; ch++)           /* the step's sound before its hit */
        if (dirty >> ch & 1u) mot_compile(ch);
}

static uint32_t mot_cond_val(uint32_t c)        /* does a hit with condition c play now */
{
    uint32_t a, b, k;
    if (c == MOT_NONE || (c >= 100u && c < COND_CYC)) return 1;
    if (c == COND_FILL) return mot_fill;
    if (c == COND_NFILL) return !mot_fill;
    if (c >= COND_CYC) {                        /* a:b: the a-th of every b passes */
        k = c - COND_CYC;
        for (b = 2; k >= b; b++) k -= b;
        a = k + 1u;
        return seq.pass % b == a - 1u;
    }
    mot_rng ^= mot_rng << 13; mot_rng ^= mot_rng >> 17; mot_rng ^= mot_rng << 5;
    return mot_rng % 100u < c;
}
static uint32_t mot_cond(uint32_t pat, uint32_t ch, uint32_t st)   /* SEQ_COND: does the hit play */
{
    (void)pat; (void)st;
    return mot_cond_val(mot_cond_now[ch]);
}
static uint32_t mot_cond_at(uint32_t pat, uint32_t ch, uint32_t st)   /* SEQ_COND_AT: another step's (a hit played early) */
{
    return mot_cond_val(mot_get(pat, ch, P_COND, st));
}
static int32_t micro_q(int32_t m, uint32_t q)   /* m 24ths with QUANT q %: the timing played (rounded away from 0) */
{
    int32_t t;
    if (!q || !m) return m;
    t = m * (int32_t)(100u - (q > 100u ? 100u : q));
    return (t + (t < 0 ? -50 : 50)) / 100;
}
static int32_t mot_micro(uint32_t pat, uint32_t ch, uint32_t st)   /* SEQ_MICRO: 24ths of a step, + late, - early, as played */
{
    return micro_q(micro_of(mot_get(pat, ch, P_MICRO, st)), mot_quant[pat % SEQ_NPAT][ch % DRUM_NCH]);
}
static void mot_quant_apply(void)               /* SAVE: each part's QUANT into its micro timing, then QUANT 0 everywhere */
{
    uint32_t i, s;
    for (i = 0; i < NLANE; i++) {
        lane_t *l = &lanes[i];
        uint32_t q;
        if (!l->used || l->param != P_MICRO || l->pat >= SEQ_NPAT || l->ch >= DRUM_NCH) continue;
        if (!(q = mot_quant[l->pat][l->ch])) continue;
        for (s = 0; s < SEQ_NSTEP; s++)
            if (l->val[s] != MOT_NONE) {
                int32_t m = micro_q(micro_of(l->val[s]), q);
                l->val[s] = m ? (uint8_t)((int32_t)MICRO0 + m) : MOT_NONE;
            }
        if (mot_lane_empty(l)) mot_free(i);
    }
    memset(mot_quant, 0, sizeof mot_quant);
}
static uint32_t ratch_of(uint32_t v) { return v >= 2u && v <= 4u ? v : 1u; }   /* a P_RATCH value: hits in the step */
static uint32_t mot_ratch(uint32_t pat, uint32_t ch, uint32_t st)   /* SEQ_RATCH: the step's hits, 1..4 */
{
    return ratch_of(mot_get(pat, ch, P_RATCH, st));
}
static void mot_lock_ch(uint32_t pat, uint32_t st, uint32_t ch)   /* SEQ_LOCK_CH: a hit played early: its step's locks,
                                                 * on its channel, before it (mot_step sets the same at the step) */
{
    uint32_t i, dirty = 0;
    for (i = 0; i < NLANE; i++) {
        const lane_t *l = &lanes[i];
        uint32_t v;
        if (!l->used || l->pat != pat || l->ch != ch || l->param >= P_COND) continue;
        if (lane_rt[i].rec_last || lane_rt[i].hold_left || lane_rt[i].gen != l->gen) continue;
        v = l->val[st % SEQ_NSTEP];
        lane_rt[i].ramp = 0;
        if (mot_ov[ch][l->param] != v) { mot_ov[ch][l->param] = (uint8_t)v; dirty = 1; }
    }
    if (dirty) mot_compile(ch);
}

static void lfo_tick(void);
static void mot_tick(void)                      /* every render (256 frames), before drum_render */
{
    uint32_t i, ch;
    uint16_t d;
    if (!seq.run && mot_run) { mot_run = 0; mot_pat = 0xFF; mot_release(); }
    if (seq.run) {
        mot_step_age++;
        for (i = 0; i < NLANE; i++) {           /* the glides between neighbours */
            const lane_t *l = &lanes[i];
            int32_t a, b, v;
            if (!lane_rt[i].ramp || !l->used || l->pat != seq.pat || l->ch >= DRUM_NCH || lane_rt[i].hold_left ||
                lane_rt[i].rec_last)
                continue;
            a = lane_rt[i].from; b = lane_rt[i].to;
            v = a + (b - a) * (int32_t)(mot_step_age < mot_step_len ? mot_step_age : mot_step_len) / (int32_t)mot_step_len;
            if (mot_ov[l->ch][l->param] != v) { mot_ov[l->ch][l->param] = (uint8_t)v; mot_dirty |= (uint16_t)(1u << l->ch); }
        }
    }
    lfo_tick();
    d = mot_dirty;
    mot_dirty = 0;
    for (ch = 0; ch < DRUM_NCH; ch++)
        if (d >> ch & 1u) mot_compile(ch);
}
