/* SPDX-License-Identifier: GPL-3.0-only */
/* The FM-1's knob motion, p-locks and step conditions (motion.h), and what reaches the engine: each channel's patch
 * as the kit has it, with its lanes' values and its LFOs on top.
 *
 *   main loop   mot_find / mot_alloc / mot_free, mot_touch (a knob turned while playing), the edits below; it changes
 *               the kit and calls mot_kit_changed(ch); it never writes drum_cfg itself
 *   ISR         mot_step (the sequencer, before a step's hits: SEQ_STEP_HOOK), mot_cond (SEQ_COND), mot_tick (every
 *               render, before drum_render: the glides, the LFOs, the kit's changes)
 *
 * A lane is written whole before used is set (and gen tells the ISR it was reused); a mark is one word, written at
 * once (the marks below). Recording: with REC on, a knob
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
                                                 * the grid as it plays (the lane keeps it as set); saved as it is, with
                                                 * the patterns (app_fm1.c SONG_QUANT: the user's choice, 2026-11-23) */

static const drum_patch_t *volatile mot_prev;   /* main: a library sound heard on mot_prev_ch instead of its own (ui_fm1.c) */
static volatile uint8_t mot_prev_ch;
/* ---- the marks: an open-addressing hash of MARK_NSLOT words, linear probing; a word a step that has a mark (since
 * 2026-11-23; records SNG6 and SNG7 since 2026-11-24; SNG5's and SNG4's words, app_fm1.c, convert as they load):
 *   bits 0-14   its key: pattern << 10 | channel << 6 | step
 *   15-20       its condition's place on the knob's line (motion.h cond_val_pos) + MARK_C0: 1..57, never 0 (none: 38)
 *   21-31       its micro timing and ratchet, m + 47 r: m its micro timing in 24ths + MICRO_MAX (0..46), r its
 *               ratchet, 0 one hit, else 1 + (hits - 2) * 5 + R: 1..35. R (SNG6's RAMP: FLAT 0, DOWN 1, UP 2, DOWN+ 3,
 *               UP+ 4) is always 0 since the RAMP became a lane (SNG7, 2026-11-24): an SNG6 record's moves into
 *               its lane as it loads (app_fm1.c). SNG5's words (bits 21-26 m, 27-31 1 + (hits - 2) * 3 + R) convert as
 *               they load
 * The condition's field is never 0, so a word in use never is: 0 is a free slot (the key 0, pattern 1's BD's step 1,
 * is a real one: a search tests for 0 before it compares keys). A condition is only ever set from the knob's line,
 * so its place loses nothing (one off the line, none made: its nearest place). One hit has no RAMP: hits set back to
 * 1 clear the step's in its lane (mot_set), and ui_fm1.c sets none on one hit (RATCH FIRST).
 * A word exists only while it holds something (no condition, micro 0, one hit: removed); NMARK at most, so a search
 * meets a free slot soon. The ISR only reads (mot_get from SEQ_COND_AT, SEQ_MICRO, SEQ_RATCH, mot_step), the main loop
 * writes, each word with one 32-bit store: a search compares the key in the word it read and takes the values from
 * that same word, so it never gets another step's values nor half of one (a ratchet's hits; its RAMP, a lane's: a
 * second read, mot_ratch). A removal shifts back (no tombstones): each word after the one removed that may move into the hole is
 * stored there (its old place still holds it until the next one moves in), and only the last hole becomes free;
 * between two stores the table is always a valid one. A search racing an edit gets the step's old or new values, or,
 * if a word moved back past where it had already looked, no mark for that one step (its condition, micro timing or
 * ratchet missed once). Tombstones would need a rebuild, and a rebuild moves every word at once. */
static volatile uint32_t marks[MARK_NSLOT];
static uint32_t mark_n;                         /* main: the words in use */
#define MARK_KEY 0x00007FFFu                    /* (a word's key; the rest, its values) */
#define MARK_VAL 0xFFFF8000u
#define MARK_C0 38u                             /* (the condition's field: its place + 38; none, place 0: 38) */
#define MARK_CMAX (MARK_C0 + COND_POS_MAX)      /* (57) */
#define MARK_NMI (2u * MICRO_MAX + 1u)           /* (the micro timings: 47) */
#define MARK_NR6 5u                             /* (SNG6's RAMPs: the ratchet's field keeps their room) */
#define MARK_RMAX (1u + (RATCH_MAX - 2u) * MARK_NR6 + MARK_NR6 - 1u)   /* (the ratchet's field at most: 35) */
#define MARK_FMAX (MARK_NMI * (MARK_RMAX + 1u) - 1u)   /* (bits 21-31 at most: 1691) */
#define MARK_MASK (MARK_NSLOT - 1u)
#define MARK_NONE (MARK_C0 << 15 | (uint32_t)MICRO_MAX << 21)   /* a step without a mark: its values (no condition,
                                                 * micro 0, one hit) */
#define MARK_RD(i) (marks[(i) & MARK_MASK])
#ifndef MARK_WR_HOOK
#define MARK_WR_HOOK() ((void)0)                /* (tests/marks_test.c: the table checked after each store) */
#endif
#define MARK_WR(i, w) do { marks[(i) & MARK_MASK] = (w); MARK_WR_HOOK(); } while (0)
_Static_assert(SEQ_NPAT <= 32u && DRUM_NCH <= 16u && SEQ_NSTEP == 64u,
               "a mark's key: 5 bits of pattern, 4 of channel, 6 of step");
_Static_assert(NMARK < MARK_NSLOT && MARK_NSLOT == 1024u, "the marks' table: 1024 slots (mark_home's 10 bits)");
_Static_assert(MARK_C0 + COND_POS_MIN == 1 && MARK_CMAX < 64u && MARK_FMAX < 2048u,
               "a mark's fields: the condition 1..63, the micro timing 6 bits, the ratchet 5");
static uint32_t mark_key(uint32_t pat, uint32_t ch, uint32_t st)
{
    return pat << 10 | ch << 6 | st % SEQ_NSTEP;
}
static uint32_t mark_home(uint32_t k) { return (k * 0x9E3779B1u) >> 22; }   /* (a multiplicative hash, 10 bits) */
static uint32_t mark_pat(uint32_t w) { return w >> 10 & 31u; }
static uint32_t mark_ch(uint32_t w) { return w >> 6 & 15u; }
static uint32_t mark_st(uint32_t w) { return w & 63u; }
static uint32_t mark_in(uint32_t w, uint32_t pat, uint32_t ch)   /* a word of pattern pat's channel ch (DRUM_NCH:
                                                 * any) */
{
    return w && mark_pat(w) == pat && (ch >= DRUM_NCH || mark_ch(w) == ch);
}
static uint32_t mark_find(uint32_t k, uint32_t *at)   /* (ISR too) key k's word, 0 none; at: its slot, or the free
                                                 * one */
{
    uint32_t i = mark_home(k), n, w;
    for (n = 0; n < MARK_NSLOT; n++, i = (i + 1u) & MARK_MASK) {
        w = MARK_RD(i);
        if (!w) break;                          /* (first: a free slot's 0 is also key 0's) */
        if ((w & MARK_KEY) == k) { if (at) *at = i; return w; }
    }
    if (at) *at = n < MARK_NSLOT ? i : MARK_NSLOT;
    return 0;
}
static uint32_t mark_get(uint32_t pat, uint32_t ch, uint32_t st)   /* (ISR too) a step's values, MARK_NONE: no mark */
{
    uint32_t w;
    if (pat >= SEQ_NPAT || ch >= DRUM_NCH) return MARK_NONE;
    w = mark_find(mark_key(pat, ch, st), 0);
    return w ? w & MARK_VAL : MARK_NONE;
}
static void mark_del_at(uint32_t i)             /* main: the word in slot i goes; the ones after it shift back */
{
    uint32_t j = i, w, h;
    for (;;) {
        j = (j + 1u) & MARK_MASK;
        if (!(w = marks[j])) break;
        h = mark_home(w & MARK_KEY);
        if (((j - h) & MARK_MASK) >= ((j - i) & MARK_MASK)) { MARK_WR(i, w); i = j; }   /* (its home not in i+1..j) */
    }
    MARK_WR(i, 0u);
    mark_n--;
}
static int mark_put(uint32_t k, uint32_t v)     /* main: key k's values v (MARK_NONE: none); -1: NMARK in use */
{
    uint32_t i, w = mark_find(k, &i);
    v &= MARK_VAL;
    if (w) {
        if (v == MARK_NONE) mark_del_at(i);
        else MARK_WR(i, k | v);
        return 0;
    }
    if (v == MARK_NONE) return 0;
    if (mark_n >= NMARK || i >= MARK_NSLOT) return -1;
    MARK_WR(i, k | v);
    mark_n++;
    return 0;
}
static void mark_clear(uint32_t pat, uint32_t ch)   /* pattern pat's channel ch's marks (DRUM_NCH: all its channels) */
{
    uint32_t i;
    for (i = 0; i < MARK_NSLOT; i++)
        while (mark_in(marks[i], pat, ch)) mark_del_at(i);   /* (a word shifted into slot i: looked at again; one
                                                 * shifted round past the end comes from a slot already looked at) */
}
static void mark_reset(void)                    /* none (a word at a time: never a key over another's values) */
{
    uint32_t i;
    for (i = 0; i < MARK_NSLOT; i++) MARK_WR(i, 0u);
    mark_n = 0;
}
static uint32_t mark_take(uint32_t pat, uint32_t ch, uint32_t *d)   /* pattern pat's channel ch's words into d: how
                                                 * many */
{
    uint32_t i, n = 0;
    for (i = 0; i < MARK_NSLOT; i++) if (mark_in(marks[i], pat, ch)) d[n++] = marks[i];
    return n;
}
static uint32_t mark_hash(uint32_t pat, uint32_t ch)   /* the marks of pattern pat's channel ch (DRUM_NCH: all), not
                                                 * where they sit in the table (that moves with others' edits) */
{
    uint32_t i, h = 0, w;
    for (i = 0; i < MARK_NSLOT; i++)
        if (mark_in(w = marks[i], pat, ch)) { w *= 0x9E3779B1u; h += w ^ w >> 15; }
    return h;
}
static uint32_t mark_mi(uint32_t v) { return (v >> 21) % MARK_NMI; }   /* (its micro timing + MICRO_MAX) */
static uint32_t mark_r(uint32_t v) { return (v >> 21) / MARK_NMI; }    /* (its ratchet and RAMP) */
static uint32_t mark_mr(uint32_t v, uint32_t mi, uint32_t r)   /* values v with micro timing mi and ratchet r */
{
    return (v & 0x001FFFFFu) | (mi + MARK_NMI * r) << 21;
}
static int32_t mark_micro(uint32_t v) { return (int32_t)mark_mi(v) - MICRO_MAX; }
static uint32_t mark_rcode(uint32_t hits, uint32_t r6)   /* the ratchet's field: hits 1..8 and SNG6's RAMP (0 since
                                                 * SNG7) */
{
    return hits < 2u ? 0u : 1u + (hits - 2u) * MARK_NR6 + (r6 < MARK_NR6 ? r6 : 0u);
}
static uint32_t mark_ramp6(uint32_t v)          /* an SNG6 word's RAMP (ramp_from6), 0 FLAT */
{
    uint32_t r = mark_r(v);
    return r ? (r - 1u) % MARK_NR6 : 0u;
}
static uint32_t mark_noramp(uint32_t v)         /* the word without it (SNG7's) */
{
    uint32_t r = mark_r(v);
    return mark_mr(v, mark_mi(v), r ? mark_rcode(2u + (r - 1u) / MARK_NR6, 0u) : 0u);
}
static uint32_t mark_lane(uint32_t v, uint32_t prm)   /* a mark's values as a lane held them: MOT_NONE none */
{
    uint32_t r = mark_r(v);
    if (prm == P_COND) return cond_pos_val((int32_t)(v >> 15 & 63u) - (int32_t)MARK_C0);
    if (prm == P_MICRO) { int32_t m = mark_micro(v); return m ? (uint32_t)((int32_t)MICRO0 + m) : MOT_NONE; }
    if (prm == P_RATCH) return r ? 2u + (r - 1u) / MARK_NR6 : MOT_NONE;
    return MOT_NONE;                            /* (P_RAMP: a lane's) */
}
static uint32_t mark_with(uint32_t v, uint32_t prm, uint32_t x)   /* values v with prm's (as a lane holds it) now x */
{
    int32_t m;
    uint32_t r = mark_r(v);
    if (prm == P_COND) return (v & ~0x001F8000u) | (uint32_t)(cond_val_pos(x) + (int32_t)MARK_C0) << 15;
    if (prm == P_MICRO) {
        m = micro_of(x);
        m = m < -MICRO_MAX ? -MICRO_MAX : m > MICRO_MAX ? MICRO_MAX : m;
        return mark_mr(v, (uint32_t)(m + MICRO_MAX), r);
    }
    if (prm == P_RATCH) return mark_mr(v, mark_mi(v), mark_rcode(ratch_of(x), 0u));   /* (1: its RAMP goes too, mot_set) */
    return v;                                   /* (P_RAMP: a lane's) */
}
static uint32_t mark_fit(uint32_t w)            /* a word as loaded (app_fm1.c), each field within its range */
{
    uint32_t c = w >> 15 & 63u;
    if (c < 1u || c > MARK_CMAX) w = (w & ~0x001F8000u) | MARK_C0 << 15;   /* (none) */
    if ((w >> 21) > MARK_FMAX) w = mark_mr(w, mark_mi(w), MARK_RMAX);
    return w;
}
static uint32_t mark_from5(uint32_t w)          /* an SNG5 record's word as SNG6's: bits 21-26 its micro timing, 27-31
                                                 * 1 + (hits - 2) * 3 + RAMP (FLAT, DOWN, UP: their values unchanged) */
{
    uint32_t m = w >> 21 & 63u, r = w >> 27;
    if (r > 21u) r = 21u;
    return mark_mr(w, m > 2u * MICRO_MAX ? 2u * MICRO_MAX : m, r ? mark_rcode(2u + (r - 1u) / 3u, (r - 1u) % 3u) : 0u);
}

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
static int32_t mot_alloc(uint32_t pat, uint32_t ch, uint32_t prm)   /* -1: all NLANE in use (MOTION FULL) */
{
    int32_t i = mot_find(pat, ch, prm);
    uint32_t k;
    if (i >= 0) return i;
    if (prm >= P_COND && prm != P_RAMP) return -1;   /* (the marks') */
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

static uint32_t mot_get(uint32_t pat, uint32_t ch, uint32_t prm, uint32_t st)   /* a step's own value, MOT_NONE: none
                                                 * (P_COND and up: its mark's, ISR too) */
{
    int32_t i;
    if (prm >= P_COND && prm != P_RAMP) return prm < P_NTARGET ? mark_lane(mark_get(pat, ch, st), prm) : MOT_NONE;
    i = mot_find(pat, ch, prm);
    return i < 0 ? MOT_NONE : lanes[i].val[st % SEQ_NSTEP];
}
static int mot_set(uint32_t pat, uint32_t ch, uint32_t prm, uint32_t st, uint32_t v)   /* MOT_NONE clears it; -1: MOTION
                                                 * FULL (no lane, or no mark, left) */
{
    int32_t i;
    if (prm >= P_COND && prm != P_RAMP) {
        if (pat >= SEQ_NPAT || ch >= DRUM_NCH || prm >= P_NTARGET) return v == MOT_NONE ? 0 : -1;
        if (mark_put(mark_key(pat, ch, st), mark_with(mark_get(pat, ch, st), prm, v))) return -1;
        if (prm == P_RATCH && ratch_of(v) < 2u) mot_set(pat, ch, P_RAMP, st, MOT_NONE);   /* (one hit: no RAMP) */
        mot_dirty |= (uint16_t)(1u << ch);
        return 0;
    }
    i = v == MOT_NONE ? mot_find(pat, ch, prm) : mot_alloc(pat, ch, prm);
    if (i < 0) return v == MOT_NONE ? 0 : -1;
    lanes[i].val[st % SEQ_NSTEP] = (uint8_t)v;
    if (v == MOT_NONE && mot_lane_empty(&lanes[i])) mot_free((uint32_t)i);
    mot_dirty |= (uint16_t)(1u << ch);
    return 0;
}
static uint32_t mot_lock_mark(uint32_t pat, uint32_t ch, uint32_t st)   /* a lock or a condition (not micro timing,
                                                 * not a RAMP) */
{
    uint32_t i;
    for (i = 0; i < NLANE; i++)
        if (lanes[i].used && lanes[i].pat == pat && lanes[i].ch == ch && lanes[i].param != P_RAMP &&
            lanes[i].val[st % SEQ_NSTEP] != MOT_NONE)
            return 1;
    return mot_get(pat, ch, P_COND, st) != MOT_NONE;
}
static uint32_t mot_has_motion(uint32_t pat, uint32_t ch, uint32_t prm)
{
    uint32_t i;
    if (prm < P_COND || prm == P_RAMP) return mot_find(pat, ch, prm) >= 0;
    for (i = 0; i < MARK_NSLOT; i++)
        if (mark_in(marks[i], pat, ch) && ch < DRUM_NCH && mark_lane(marks[i] & MARK_VAL, prm) != MOT_NONE) return 1;
    return 0;
}

/* the lanes of a pattern's channel (ch DRUM_NCH: all its channels), for the edits on the steps (each takes the marks
 * too) */
#define MOT_EACH(i, p_, c_) for (i = 0; i < NLANE; i++) if (lanes[i].used && lanes[i].pat == (p_) && \
                                                             ((c_) >= DRUM_NCH || lanes[i].ch == (c_)))
static void mot_clear(uint32_t pat, uint32_t ch)
{
    uint32_t i;
    MOT_EACH(i, pat, ch) mot_free(i);
    mark_clear(pat, ch);
}
static void mot_clear_step(uint32_t pat, uint32_t ch, uint32_t st)
{
    uint32_t i;
    MOT_EACH(i, pat, ch) {
        lanes[i].val[st % SEQ_NSTEP] = MOT_NONE;
        if (mot_lane_empty(&lanes[i])) mot_free(i);
    }
    for (i = 0; i < DRUM_NCH; i++)
        if (ch >= DRUM_NCH || i == ch) mark_put(mark_key(pat, i, st), MARK_NONE);
    mot_dirty |= (uint16_t)(1u << ch);
}
/* copy pattern sp's channel sc onto dp's dc (sc DRUM_NCH: every channel onto itself); -1: MOTION FULL (some not) */
static int mot_copy(uint32_t sp, uint32_t sc, uint32_t dp, uint32_t dc)
{
    lane_t *tmp = (lane_t *)(void *)io_scratch();   /* (the sources first: a paste onto itself stays whole; io_buf as
                                                 * scratch, app_fm1.c: a paste never meets a save or a load) */
    uint32_t *mt = (uint32_t *)(void *)(io_scratch() + NLANE * sizeof(lane_t)), nm, i, n = 0;   /* (the marks after) */
    int rc = 0;
    MOT_EACH(i, sp, sc) tmp[n++] = lanes[i];
    nm = mark_take(sp, sc, mt);
    if (sc >= DRUM_NCH) { if (sp != dp) memcpy(mot_quant[dp % SEQ_NPAT], mot_quant[sp % SEQ_NPAT], sizeof mot_quant[0]); }   /* (QUANT
                                                 * goes with them; no memmove on the chip) */
    else mot_quant[dp % SEQ_NPAT][dc % DRUM_NCH] = mot_quant[sp % SEQ_NPAT][sc % DRUM_NCH];
    mot_clear(dp, sc >= DRUM_NCH ? DRUM_NCH : dc);
    for (i = 0; i < n; i++) {
        int32_t k = mot_alloc(dp, sc >= DRUM_NCH ? tmp[i].ch : dc, tmp[i].param);
        if (k < 0) { rc = -1; break; }
        memcpy(lanes[k].val, tmp[i].val, sizeof lanes[k].val);
    }
    for (i = 0; i < nm; i++)
        if (mark_put(mark_key(dp, sc >= DRUM_NCH ? mark_ch(mt[i]) : dc, mark_st(mt[i])), mt[i])) { rc = -1; break; }
    mot_all_changed();
    return rc;
}
static int mot_copy_step(uint32_t sp, uint32_t sc, uint32_t ss, uint32_t dp, uint32_t dc, uint32_t ds)   /* a step's
                                                 * locks, marks and RAMP onto pattern dp's channel dc's step ds (read
                                                 * first: onto itself, kept; until 2026-11-25 onto channel sc, and a step
                                                 * pasted on itself lost them); -1: MOTION FULL (what fit copied) */
{
    uint8_t v[P_NTARGET];
    uint32_t prm;
    int rc = 0;
    for (prm = 0; prm < P_NTARGET; prm++) v[prm] = (uint8_t)mot_get(sp, sc, prm, ss);
    mot_clear_step(dp, dc, ds);
    for (prm = 0; prm < P_NTARGET; prm++)       /* (in order: P_RATCH before P_RAMP: a RAMP only if the hits fit) */
        if (v[prm] != MOT_NONE && !(prm == P_RAMP && ratch_of(mot_get(dp, dc, P_RATCH, ds)) < 2u) &&
            mot_set(dp, dc, prm, ds, v[prm])) rc = -1;
    return rc;
}
static void mot_rotate(uint32_t pat, uint32_t ch, uint32_t len, int32_t d)   /* with the steps: d +1 later (ch one
                                                 * channel) */
{
    uint32_t i, s, n, mk[SEQ_NSTEP];
    if (len < 2u || ch >= DRUM_NCH) return;
    MOT_EACH(i, pat, ch) {
        uint8_t *v = lanes[i].val, t;
        if (d > 0) { t = v[len - 1u]; for (s = len - 1u; s; s--) v[s] = v[s - 1u]; v[0] = t; }
        else { t = v[0]; for (s = 0; s + 1u < len; s++) v[s] = v[s + 1u]; v[len - 1u] = t; }
    }
    for (i = n = 0; i < MARK_NSLOT; i++)        /* the marks within the length: taken, removed, put back moved (as
                                                 * many: always room) */
        if (mark_in(marks[i], pat, ch) && mark_st(marks[i]) < len) mk[n++] = marks[i];
    for (i = 0; i < n; i++) mark_put(mk[i] & MARK_KEY, MARK_NONE);
    for (i = 0; i < n; i++) {
        s = mark_st(mk[i]);
        s = d > 0 ? (s + 1u == len ? 0u : s + 1u) : (s ? s - 1u : len - 1u);
        mark_put(mark_key(pat, ch, s), mk[i]);
    }
    mot_all_changed();
}
static int mot_repeat(uint32_t pat, uint32_t ch, uint32_t from, uint32_t to)   /* lengthened: steps 0..from-1 again and
                                                 * again (ch one channel); -1: marks not all copied (NMARK in use) */
{
    uint32_t i, s;
    int rc = 0;
    MOT_EACH(i, pat, ch)
        for (s = from; s < to && s < SEQ_NSTEP; s++) lanes[i].val[s] = lanes[i].val[s % from];
    for (s = from; s < to && s < SEQ_NSTEP; s++)
        if (mark_put(mark_key(pat, ch, s), mark_get(pat, ch, s % from))) rc = -1;
    return rc;
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
static uint32_t mot_count(void)                 /* the lanes in use (the marks: mark_n) */
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
    for (ch = 0; ch < DRUM_NCH; ch++)           /* the conditions (each part on its own length) */
        mot_cond_now[ch] = (uint8_t)mot_get(pat, ch, P_COND, seq_pos[ch]);
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
        if (l->param >= P_COND) continue;       /* (P_RAMP's: no knob, never in mot_ov) */
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
static uint32_t mot_ratch(uint32_t pat, uint32_t ch, uint32_t st)   /* SEQ_RATCH: the step's hits, 1..8, | its RAMP,
                                                 * -100..+100 as a byte, << 8 (FLAT: 0; its lane read only on 2 hits or
                                                 * more; a lane being reused: past 200, FLAT) */
{
    uint32_t h = ratch_of(mark_lane(mark_get(pat, ch, st), P_RATCH));
    int32_t r = h < 2u ? 0 : ramp_of(mot_get(pat, ch, P_RAMP, st));
    return h | ((uint32_t)r & 0xFFu) << 8;
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
        for (i = 0; i < NLANE; i++) {           /* the glides between neighbours (a lane reused since its step: not
                                                 * its glide; until 2026-11-17 it went on into the new lane's row, a
                                                 * condition's, micro timing's or ratchet's past the parameters') */
            const lane_t *l = &lanes[i];
            int32_t a, b, v;
            if (!lane_rt[i].ramp || !l->used || l->pat != seq.pat || l->ch >= DRUM_NCH || l->param >= P_COND ||
                lane_rt[i].gen != l->gen || lane_rt[i].hold_left || lane_rt[i].rec_last)
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
