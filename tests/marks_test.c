/* SPDX-License-Identifier: GPL-3.0-only */
/* The step marks (firmware/src/motion.c): the table against a plain model of every step, through the edits the main
 * loop makes (mot_set on P_COND / P_MICRO / P_RATCH, mot_clear, mot_clear_step, mot_copy, mot_rotate,
 * mot_repeat); a word's bits as the layout says (key 0 too), a condition off the knob's line at its nearest place;
 * a RAMP (P_RAMP, a lane since 2026-11-24) beside its ratchet's word, gone with its hits; a RAMP's velocities
 * (seq_fm1.c seq_ramp_vel).
 * Checked: every step's values as the model has them, the count, NMARK refused past 896 and only there, every word
 * found where a search looks for it, no key twice; and, after each single store (MARK_WR_HOOK), what a search made
 * between two stores finds: every other step as it was, the step being edited old or new (never another's values).
 * Then the patterns' record (app_fm1.c song_pack / song_unpack): SNG7 saved and loaded back the same; SNG6 and SNG5
 * records (five and three RAMPs in the marks' words; SNG5's micro and ratchet in fields of their own) load as the
 * same steps, their RAMPs in lanes (all lanes taken: the RAMPs lost, -2, the rest loaded), and an SNG4 one made here
 * (its words in their old layout: hits 1..4, conditions as bytes) too; SNG3, SNG2 and
 * SNG1 records made here (their conditions, micro timing, a first build's micro timing in fifths, ratchets and QUANT as
 * lanes) load as marks and QUANT; more knob lanes than NLANE: what fits, -2; one pattern from a record (HOME + REC),
 * SNG4's too; a step copied (mot_copy_step, a parameter at a time) with its ratchet and RAMP, onto another channel
 * and onto itself.
 * The FM-1 build on the host, run before its main:
 *   cc -O1 -Wall -Wno-unused-function -DDINGHY_HOST -Ifirmware/hal -Ifirmware/src -Ifirmware/gen \
 *      -o OUT tests/marks_test.c
 *   OUT   (exit 0: all ok) */
#include <stddef.h>
#include <stdint.h>
static void mark_test_hook(void);
#define MARK_WR_HOOK() mark_test_hook()
#include "dinghy.c"

#define NKEY (SEQ_NPAT * DRUM_NCH * SEQ_NSTEP)
static uint32_t model[NKEY];                    /* each step's values (MARK_NONE: none), as mark_get gives them */
static uint32_t t_on, t_key = 0xFFFFFFFFu, t_old, t_new, t_fail, t_stores, t_shifts, t_full, t_max;
static uint32_t t_rng = 12345u;
static uint32_t rnd(uint32_t n) { t_rng ^= t_rng << 13; t_rng ^= t_rng >> 17; t_rng ^= t_rng << 5; return t_rng % n; }
static uint32_t kidx(uint32_t pat, uint32_t ch, uint32_t st) { return (pat * DRUM_NCH + ch) * SEQ_NSTEP + st; }

static void fail(const char *what, uint32_t k)
{
    if (t_fail++ < 10u) printf("marks: FAIL %s (pattern %u channel %u step %u)\n", what, k / (DRUM_NCH * SEQ_NSTEP),
                               k / SEQ_NSTEP % DRUM_NCH, k % SEQ_NSTEP);
}
static void mark_test_hook(void)                /* a search between two stores: as before, or the step edited old/new */
{
    uint32_t k;
    if (!t_on) return;
    t_stores++;
    for (k = 0; k < NKEY; k++) {
        uint32_t v = mark_get(k / (DRUM_NCH * SEQ_NSTEP), k / SEQ_NSTEP % DRUM_NCH, k % SEQ_NSTEP);
        if (k == t_key ? v != t_old && v != t_new : v != model[k]) { fail("between two stores", k); return; }
    }
}
static void check(const char *what)             /* the whole table against the model */
{
    uint32_t k, n = 0, i, at;
    for (k = 0; k < NKEY; k++) {
        if (mark_get(k / (DRUM_NCH * SEQ_NSTEP), k / SEQ_NSTEP % DRUM_NCH, k % SEQ_NSTEP) != model[k]) fail(what, k);
        n += model[k] != MARK_NONE;
    }
    if (n != mark_n) { printf("marks: FAIL %s: %u in use, the model %u\n", what, mark_n, n); t_fail++; }
    for (i = n = 0; i < MARK_NSLOT; i++) {
        if (!marks[i]) continue;
        n++;
        if (!mark_find(marks[i] & MARK_KEY, &at) || at != i)
            { printf("marks: FAIL %s: slot %u not found there\n", what, i); t_fail++; }
        if ((marks[i] & MARK_VAL) == MARK_NONE)
            { printf("marks: FAIL %s: slot %u holds nothing\n", what, i); t_fail++; }
    }
    if (n != mark_n) { printf("marks: FAIL %s: %u words, %u counted\n", what, n, mark_n); t_fail++; }
}
static uint32_t model_n(void)
{
    uint32_t k, n = 0;
    for (k = 0; k < NKEY; k++) n += model[k] != MARK_NONE;
    return n;
}

static void op_set(void)                        /* one step's condition, micro timing or ratchet, or none */
{
    uint32_t k = rnd(NKEY), pat, ch, st, prm = P_COND + rnd(3), v, want, full, old = rnd(2) && model_n();
    int rc;
    while (old && model[k] == MARK_NONE) k = (k + 1u) % NKEY;   /* (half the time a step with a mark: removals) */
    pat = k / (DRUM_NCH * SEQ_NSTEP); ch = k / SEQ_NSTEP % DRUM_NCH; st = k % SEQ_NSTEP;
    v = rnd(old ? 2 : 4) == 0 ? MOT_NONE : prm == P_COND ? (rnd(8) ? cond_pos_val(COND_POS_MIN + (int32_t)rnd(57))
                                                                  : rnd(COND_NFILL + 1u)) :   /* (now and then off
                                                 * the line: its nearest place) */
        prm == P_MICRO ? MICRO0 - 23u + rnd(47) : 1u + rnd(RATCH_MAX);
    want = mark_with(model[k], prm, v);
    full = model[k] == MARK_NONE && want != MARK_NONE && model_n() >= NMARK;
    t_key = k; t_old = model[k]; t_new = want;
    k = t_stores;
    rc = mot_set(pat, ch, prm, st, v);
    t_shifts += t_stores - k > 1u;              /* (a removal that moved words back) */
    k = t_key;
    t_key = 0xFFFFFFFFu;
    t_full += full;
    if (mark_n > t_max) t_max = mark_n;
    if (rc != (full ? -1 : 0)) { fail(full ? "MOTION FULL not said" : "MOTION FULL said with room", k); return; }
    if (!full) model[k] = want;
    if (full) v = t_old == MARK_NONE ? MOT_NONE : mark_lane(t_old, prm);   /* (as it was) */
    else if (prm == P_COND && v != MOT_NONE) v = cond_pos_val(cond_val_pos(v));   /* (its place's) */
    else if (prm == P_MICRO && v == MICRO0) v = MOT_NONE;   /* (micro 0, one hit: none) */
    else if (prm == P_RATCH && v == 1u) v = MOT_NONE;
    if (mot_get(pat, ch, prm, st) != v) fail("mot_get after mot_set", k);
}
static void op_bulk(void)                       /* the edits on many steps at once (no search between their stores) */
{
    uint32_t pat = rnd(SEQ_NPAT), ch = rnd(DRUM_NCH + 1u), c, s, k, tmp[NKEY / SEQ_NPAT];
    uint32_t r = rnd(5);
    t_on = 0;
    if (r == 0) {                               /* clear a channel or the pattern */
        mot_clear(pat, ch);
        for (c = 0; c < DRUM_NCH; c++)
            if (ch >= DRUM_NCH || c == ch) for (s = 0; s < SEQ_NSTEP; s++) model[kidx(pat, c, s)] = MARK_NONE;
    } else if (r == 1) {                        /* clear a step */
        s = rnd(SEQ_NSTEP);
        mot_clear_step(pat, ch, s);
        for (c = 0; c < DRUM_NCH; c++) if (ch >= DRUM_NCH || c == ch) model[kidx(pat, c, s)] = MARK_NONE;
    } else if (r == 2 && ch < DRUM_NCH) {       /* rotate a channel within a length */
        uint32_t len = 1u + rnd(SEQ_NSTEP);
        int32_t d = rnd(2) ? 1 : -1;
        mot_rotate(pat, ch, len, d);
        for (s = 0; s < len; s++) tmp[s] = model[kidx(pat, ch, s)];
        for (s = 0; s < len; s++) model[kidx(pat, ch, d > 0 ? (s + 1u) % len : (s + len - 1u) % len)] = tmp[s];
    } else if (r == 3 && ch < DRUM_NCH) {       /* a part lengthened: its steps again (all or none copied: rc) */
        uint32_t from = 1u + rnd(32), to = from + rnd(SEQ_NSTEP - from + 1u);
        int rc = mot_repeat(pat, ch, from, to);
        for (s = from; s < to; s++) {           /* (full: a step not copied keeps its own) */
            uint32_t want = model[kidx(pat, ch, s % from)], got = mark_get(pat, ch, s);
            k = kidx(pat, ch, s);
            if (got != want && !(rc && got == model[k])) fail("mot_repeat", k);
            model[k] = got;
        }
    } else {                                    /* copy a channel or a pattern onto another */
        uint32_t dp = rnd(SEQ_NPAT), dc = ch < DRUM_NCH ? rnd(DRUM_NCH) : 0, before = model_n(), add = 0;
        int rc;
        for (c = 0; c < DRUM_NCH; c++)
            for (s = 0; s < SEQ_NSTEP; s++) tmp[c * SEQ_NSTEP + s] = model[kidx(pat, c, s)];
        rc = mot_copy(pat, ch, dp, dc);
        for (c = 0; c < DRUM_NCH; c++) {
            if (ch < DRUM_NCH && c != dc) continue;
            for (s = 0; s < SEQ_NSTEP; s++) {
                k = kidx(dp, c, s);
                if (model[k] != MARK_NONE) before--;
                model[k] = MARK_NONE;
            }
        }
        for (c = 0; c < DRUM_NCH; c++)
            for (s = 0; s < SEQ_NSTEP; s++)
                if ((ch >= DRUM_NCH || c == ch) && tmp[c * SEQ_NSTEP + s] != MARK_NONE) add++;
        if (rc != (before + add > NMARK ? -1 : 0)) fail("mot_copy's MOTION FULL", kidx(dp, dc, 0));
        for (c = 0; c < DRUM_NCH; c++)          /* (full: some not copied, left empty) */
            for (s = 0; s < SEQ_NSTEP; s++) {
                uint32_t src = tmp[(ch >= DRUM_NCH ? c : ch) * SEQ_NSTEP + s], got;
                if (ch < DRUM_NCH && c != dc) continue;
                got = mark_get(dp, c, s);
                if (got != src && !(rc && got == MARK_NONE)) fail("mot_copy", kidx(dp, c, s));
                model[kidx(dp, c, s)] = got;
            }
    }
}

#define CHECK(c, what) do { if (!(c)) { printf("marks: FAIL %s (line %d)\n", what, __LINE__); t_fail++; } } while (0)
static uint32_t rec_lane(uint8_t *b, uint32_t pat, uint32_t ch, uint32_t prm, const uint8_t *v)   /* a lane_t's bytes */
{
    lane_t l;
    memset(&l, 0, sizeof l);
    l.used = 1; l.pat = (uint8_t)pat; l.ch = (uint8_t)ch; l.param = (uint8_t)prm;
    memset(l.val, MOT_NONE, sizeof l.val);
    if (v) memcpy(l.val, v, sizeof l.val);
    memcpy(b, &l, sizeof l);
    return sizeof l;
}
static uint32_t old_record(uint8_t *b, uint32_t fmt, uint32_t nknob)   /* SNG3 / 2 / 1, its lanes: see record_test */
{
    uint32_t ss = fmt == SONG_FMT1 ? sizeof(seq_pat8_t) : fmt == SONG_FMT2 ? sizeof(seq_pat2_t) : sizeof(seq_pat_t);
    uint32_t n, k, cnt = 0, at;
    uint8_t v[64];
    memset(b, 0, 8u + SEQ_NPAT * ss);
    memcpy(b, &fmt, 4);
    for (k = 0; k < SEQ_NPAT; k++)              /* (len) */
        b[4u + k * ss + (fmt == SONG_FMT1 ? 8u : DRUM_NCH) * sizeof(seq_track_t)] = 16;
    n = at = 8u + SEQ_NPAT * ss;
    for (k = 0; k < nknob; k++) {
        memset(v, MOT_NONE, 64); v[k % 64u] = (uint8_t)k;
        n += rec_lane(b + n, k / 26u % SEQ_NPAT, k / 2u % 8u, k % 26u, v); cnt++;
    }
    memset(v, MOT_NONE, 64); v[0] = 50; v[5] = COND_FILL; n += rec_lane(b + n, 2, 6, P_COND, v); cnt++;
    memset(v, MOT_NONE, 64); v[0] = 9; v[3] = (uint8_t)(MICRO0 + 23u); v[4] = (uint8_t)(MICRO0 - 7u); v[6] = 5;
    n += rec_lane(b + n, 2, 6, P_MICRO, v); cnt++;
    memset(v, MOT_NONE, 64); v[5] = 3; v[7] = 1; n += rec_lane(b + n, 2, 6, P_RATCH, v); cnt++;
    memset(v, MOT_NONE, 64); v[63] = 4; n += rec_lane(b + n, 31, 7, P_RATCH, v); cnt++;
    if (fmt == SONG_FMT3) {                     /* QUANT: patterns 0..5 and 30..31 */
        memset(v, 0, 64); v[1 * DRUM_NCH + 6] = 40; n += rec_lane(b + n, 0, 0, SONG_QUANT, v); cnt++;
        memset(v, 0, 64); v[1 * DRUM_NCH + 9] = 120; n += rec_lane(b + n, 30, 0, SONG_QUANT, v); cnt++;
    }
    memcpy(b + at - 4u, &cnt, 4);
    memset(b + n, 0, sizeof lfos); n += sizeof lfos;
    return n;
}
static uint32_t w_at(uint32_t pat, uint32_t ch, uint32_t st)   /* a step's word as the table holds it (0: none) */
{
    return mark_find(mark_key(pat, ch, st), 0);
}
#define RB(r) (((uint32_t)(r) & 0xFFu) << 8)   /* (a RAMP as SEQ_RATCH gives it) */
static void layout_test(void)                   /* the word's bits (motion.c): key 15, condition's place + 38, micro +
                                                 * 23 with the ratchet's 1 + (hits - 2) * 5 times 47; the RAMP a lane */
{
    static const struct { uint32_t c; int32_t pos; } NEAR[] = {
        {97, 1}, {98, 0}, {100, 0}, {93, 1}, {92, 2}, {3, 19}, {1, 19}, {0, 19}, {COND_NFILL + 1u, 0}, {200, 0}};
    static const int32_t R[] = {-100, -37, -1, 0, 1, 50, 100};
    uint32_t i, w;
    t_on = 0;
    song_empty();
    mot_set(0, 0, P_COND, 0, COND_CYC);         /* key 0 (pattern 1's BD's step 1): 1:2, micro -23, 5 hits DOWN 50 % */
    mot_set(0, 0, P_MICRO, 0, MICRO0 - 23u);
    mot_set(0, 0, P_RATCH, 0, 5);
    mot_set(0, 0, P_RAMP, 0, ramp_val(-50));
    w = w_at(0, 0, 0);
    CHECK(w == (35u << 15 | (0u + 47u * 16u) << 21), "key 0: its word (1:2 at place -3, micro -23, 5 hits)");
    CHECK(mot_get(0, 0, P_COND, 0) == COND_CYC && mot_micro(0, 0, 0) == -23 && mot_get(0, 0, P_RATCH, 0) == 5 &&
          ramp_of(mot_get(0, 0, P_RAMP, 0)) == -50 && mot_ratch(0, 0, 0) == (5u | RB(-50)) && mark_n == 1u &&
          mot_count() == 1u, "key 0: read back, its RAMP in a lane");
    CHECK(!mot_lock_mark(0, 0, 1) && mot_lock_mark(0, 0, 0) && mot_has_motion(0, 0, P_RAMP), "a RAMP: not a lock");
    mot_set(0, 0, P_COND, 0, MOT_NONE);
    CHECK(!mot_lock_mark(0, 0, 0), "a RAMP alone: not a lock");
    mot_set(31, 9, P_COND, 63, COND_CYC + COND_NCYC - 1u);   /* the last key: 8:8, +23, 8 hits UP 100 % */
    mot_set(31, 9, P_MICRO, 63, MICRO0 + 23u);
    mot_set(31, 9, P_RATCH, 63, 8);
    mot_set(31, 9, P_RAMP, 63, ramp_val(100));
    w = w_at(31, 9, 63);
    CHECK(w == (0x7E7Fu | 1u << 15 | (46u + 47u * 31u) << 21) && mot_ratch(31, 9, 63) == (8u | RB(100)),
          "the last key: its word (8:8 at place -37, +23, 8 hits), UP 100 %");
    mot_set(31, 9, P_COND, 63, 5);              /* 5 %: the line's other end */
    CHECK((w_at(31, 9, 63) >> 15 & 63u) == 57u && mot_get(31, 9, P_COND, 63) == 5, "5 %: place 19 + 38");
    mot_set(31, 9, P_RATCH, 63, 1);             /* one hit: RAMP dropped */
    CHECK(mark_r(w_at(31, 9, 63)) == 0u && mot_micro(31, 9, 63) == 23 && mot_get(31, 9, P_RAMP, 63) == MOT_NONE &&
          mot_count() == 1u, "hits back to 1: no RAMP (its lane gone), micro kept");
    mot_set(31, 9, P_RAMP, 63, ramp_val(40));   /* a RAMP on one hit: unheard (ui_fm1.c sets none: RATCH FIRST) */
    CHECK(mot_ratch(31, 9, 63) == 1u, "a RAMP on one hit: FLAT");
    mot_set(31, 9, P_RAMP, 63, MOT_NONE);
    for (i = 2; i <= RATCH_MAX; i++) {          /* hits 2..8, each RAMP: kept, and hits kept as RAMP changes */
        uint32_t r;
        for (r = 0; r < sizeof R / sizeof R[0]; r++) {
            mot_set(1, 2, P_RATCH, 3, i);
            mot_set(1, 2, P_RAMP, 3, ramp_val(R[r]));
            CHECK(mark_r(w_at(1, 2, 3)) == 1u + (i - 2u) * 5u && mot_ratch(1, 2, 3) == (i | RB(R[r])) &&
                  ramp_of(mot_get(1, 2, P_RAMP, 3)) == R[r] && (R[r] || mot_get(1, 2, P_RAMP, 3) == MOT_NONE),
                  "hits and RAMP: the field, SEQ_RATCH's hits | RAMP << 8 (FLAT: none)");
        }
        mot_set(1, 2, P_RATCH, 3, i == RATCH_MAX ? 2u : i + 1u);
        CHECK(ramp_of(mot_get(1, 2, P_RAMP, 3)) == 100, "hits changed: RAMP kept");
    }
    mot_set(1, 2, P_RATCH, 3, 9);               /* past 8: one hit (ratch_of) */
    CHECK(mot_get(1, 2, P_RATCH, 3) == MOT_NONE && w_at(1, 2, 3) == 0u && mot_get(1, 2, P_RAMP, 3) == MOT_NONE,
          "9 hits: none (the word gone, the RAMP too)");
    CHECK(ramp_val(-150) == RAMP0 - 100u && ramp_val(150) == RAMP0 + 100u && ramp_of(201) == 0 && ramp_of(MOT_NONE) == 0,
          "a RAMP past 100 %: 100 %; a byte past 200: FLAT");
    for (i = 0; i < sizeof NEAR / sizeof NEAR[0]; i++) {   /* off the line: the nearest place */
        mot_set(1, 3, P_COND, 4, NEAR[i].c);
        CHECK(mot_get(1, 3, P_COND, 4) == cond_pos_val(NEAR[i].pos), "a condition off the line: its nearest place");
    }
    song_empty();
}
static void ramp_test(void)                     /* seq_fm1.c's RAMP: each hit's velocity (DOWN 50 %: the old DOWN's) */
{
    static const struct { uint8_t v, n; int16_t ramp; uint8_t want[8]; } T[] = {
        {96, 4, -50, {96, 80, 64, 48}}, {96, 4, 50, {48, 64, 80, 96}}, {96, 4, 0, {96, 96, 96, 96}},
        {127, 2, -50, {127, 64}}, {127, 2, 50, {64, 127}},             /* (126 / 2: 63, a half up) */
        {127, 3, -50, {127, 95, 64}},
        {96, 5, -50, {96, 84, 72, 60, 48}}, {96, 7, 50, {48, 56, 64, 72, 80, 88, 96}},
        {127, 8, -50, {127, 118, 109, 100, 91, 82, 73, 64}},
        {1, 8, -50, {1, 1, 1, 1, 1, 1, 1, 1}},                         /* (never 0) */
        {96, 1, -50, {96}},                                             /* (one hit: none) */
        {96, 7, -100, {96, 80, 64, 48, 33, 17, 1}}, {127, 3, 100, {1, 64, 127}},   /* (to 1, from 1) */
        {96, 4, -100, {96, 64, 33, 1}}, {127, 2, 100, {1, 127}}, {1, 4, -100, {1, 1, 1, 1}},
        {127, 8, -100, {127, 109, 91, 73, 55, 37, 19, 1}},              /* (127 - 18 k) */
        {100, 5, -25, {100, 94, 88, 81, 75}}, {100, 5, 25, {75, 81, 88, 94, 100}},   /* (between) */
        {64, 3, -1, {64, 64, 63}}, {64, 3, 1, {63, 64, 64}},           /* (1 %) */
        {127, 4, -150, {127, 85, 43, 1}}, {2, 2, -100, {2, 1}}};      /* (past 100 %: 100 %) */
    uint32_t i, k;
    for (i = 0; i < sizeof T / sizeof T[0]; i++)
        for (k = 0; k < T[i].n; k++)
            if (seq_ramp_vel(T[i].v, k, T[i].n, T[i].ramp) != T[i].want[k]) {
                printf("marks: FAIL RAMP: velocity %u, %u hits, RAMP %d: hit %u is %u, not %u\n", T[i].v, T[i].n,
                       T[i].ramp, k, seq_ramp_vel(T[i].v, k, T[i].n, T[i].ramp), T[i].want[k]);
                t_fail++;
            }
}
static uint32_t w4(uint32_t pat, uint32_t ch, uint32_t st, uint32_t c, int32_t m, uint32_t hits)   /* an SNG4 word */
{
    return 0x8000u | pat << 10 | ch << 6 | st | c << 16 | (uint32_t)(m + 23) << 24 | (hits - 1u) << 30;
}
static uint32_t sng4_record(uint8_t *b)         /* an SNG4 record as that build saved it: no lanes, QUANT, 6 marks */
{
    const uint32_t W[] = {w4(2, 6, 0, 50, 19, 1), w4(2, 6, 5, COND_FILL, 0, 3), w4(31, 7, 63, MOT_NONE, 0, 4),
                          w4(0, 0, 0, COND_CYC + 34u, -23, 2), w4(1, 9, 10, 37, 0, 1), w4(3, 1, 2, MOT_NONE, 5, 1)};
    uint32_t n, k, f = 0x34474E53u, nm = sizeof W / sizeof W[0];
    memset(b, 0, 8u + sizeof(seq_song_t));
    memcpy(b, &f, 4);
    for (k = 0; k < SEQ_NPAT; k++) b[4u + k * sizeof(seq_pat_t) + offsetof(seq_pat_t, len)] = 16;
    n = 8u + sizeof(seq_song_t);                /* (no lanes: the count 0) */
    memset(b + n, 0, sizeof lfos); n += sizeof lfos;
    memset(b + n, 0, sizeof mot_quant); b[n + 1u * DRUM_NCH + 6u] = 40; n += sizeof mot_quant;
    memcpy(b + n, &nm, 4); n += 4;
    memcpy(b + n, W, sizeof W); n += sizeof W;
    return n;
}
static void sng4_test(void)                     /* an SNG4 record loads as the same steps, then is saved as SNG7 */
{
    static uint8_t rec[SONG_MAX], rec2[SONG_MAX];
    uint32_t n = sng4_record(rec), n2, pass;
    t_on = 0;
    for (pass = 0; pass < 2u; pass++) {         /* (the SNG4, then the SNG7 saved from it) */
        song_empty(); memset(mot_quant, 0, sizeof mot_quant);
        CHECK(song_unpack(rec, n, SEQ_NPAT) == 0, pass ? "SNG7 from SNG4 loads" : "SNG4 loads");
        CHECK(mot_get(2, 6, P_COND, 0) == 50 && mot_micro(2, 6, 0) == 19 && mot_ratch(2, 6, 0) == 1, "SNG4: 50 %, +19");
        CHECK(mot_get(2, 6, P_COND, 5) == COND_FILL && mot_micro(2, 6, 5) == 0 && mot_ratch(2, 6, 5) == 3 &&
              mot_get(2, 6, P_RAMP, 5) == MOT_NONE, "SNG4: FILL, 3 hits, FLAT");
        CHECK(mot_get(31, 7, P_COND, 63) == MOT_NONE && mot_ratch(31, 7, 63) == 4, "SNG4: 4 hits");
        CHECK(mot_get(0, 0, P_COND, 0) == COND_CYC + 34u && mot_micro(0, 0, 0) == -23 && mot_ratch(0, 0, 0) == 2,
              "SNG4: key 0, 8:8, -23, 2 hits");
        CHECK(mot_get(1, 9, P_COND, 10) == 35, "SNG4: 37 % (off the line): 35 %");
        CHECK(mot_micro(3, 1, 2) == 5 && mot_get(3, 1, P_COND, 2) == MOT_NONE, "SNG4: micro alone");
        CHECK(mark_n == 6u && mot_quant[1][6] == 40, "SNG4: 6 marks, QUANT");
        n2 = song_pack(rec2);
        CHECK(!memcmp(rec2, "SNG7", 4) && n2 == n, "SNG4 saved: SNG7, as long");
        if (!pass) { memcpy(rec, rec2, n2); n = n2; }
        else CHECK(!memcmp(rec, rec2, n2), "SNG7 from SNG4 saved again: the same bytes");
    }
    n = sng4_record(rec);                       /* one pattern from SNG4 (HOME + REC before a SAVE on this build) */
    song_empty();
    mot_set(2, 6, P_COND, 0, 25); mot_set(5, 1, P_RATCH, 1, 6); mot_set(5, 1, P_RAMP, 1, ramp_val(50));
    CHECK(song_unpack(rec, n, 2) == 0 && mot_get(2, 6, P_COND, 0) == 50 && mot_micro(2, 6, 0) == 19 &&
          mot_ratch(2, 6, 5) == 3 && mot_ratch(31, 7, 63) == 1 && mot_ratch(5, 1, 1) == (6u | RB(50)) &&
          mark_n == 3u, "one pattern from SNG4: its marks, the others kept");
    song_empty();
    mot_set(4, 3, P_COND, 9, COND_FILL); mot_set(4, 3, P_MICRO, 9, MICRO0 - 4u);   /* a step copied, all its marks */
    mot_set(4, 3, P_RATCH, 9, 7); mot_set(4, 3, P_RAMP, 9, ramp_val(-73));
    CHECK(mot_copy_step(4, 3, 9, 6, 3, 12) == 0, "a step copied: room");
    CHECK(mot_get(6, 3, P_COND, 12) == COND_FILL && mot_micro(6, 3, 12) == -4 &&
          mot_ratch(6, 3, 12) == (7u | RB(-73)) &&
          w_at(6, 3, 12) == ((w_at(4, 3, 9) & MARK_VAL) | mark_key(6, 3, 12)), "a step copied: its RAMP too");
    mot_set(6, 5, P_COND, 2, 50); mot_set(6, 3, P_COND, 2, 25);   /* onto another channel: there, not on the one
                                                 * copied from (until 2026-11-25 channel 3's step 2 got them) */
    mot_copy_step(4, 3, 9, 6, 5, 2);
    CHECK(mot_get(6, 5, P_COND, 2) == COND_FILL && mot_ratch(6, 5, 2) == (7u | RB(-73)) && mot_micro(6, 5, 2) == -4 &&
          mot_get(6, 3, P_COND, 2) == 25 && mot_ratch(6, 3, 2) == 1u, "a step copied onto another channel: there only");
    mot_copy_step(4, 3, 9, 4, 3, 9);            /* onto itself: kept (until 2026-11-25 cleared) */
    CHECK(mot_get(4, 3, P_COND, 9) == COND_FILL && mot_ratch(4, 3, 9) == (7u | RB(-73)) && mot_micro(4, 3, 9) == -4,
          "a step copied onto itself: kept");
    song_empty();
}
static void sng56_test(uint32_t six, uint32_t full)   /* an SNG6 record (2026-11-24: five RAMPs in the words, 1 +
                                                 * (hits - 2) * 5 + RAMP) or an SNG5 one (2026-11-23: three RAMPs, bits
                                                 * 21-26 micro, 27-31 1 + (hits - 2) * 3 + RAMP) loads as the same steps,
                                                 * their RAMPs in lanes (DOWN -50 %, UP +50 %, DOWN+ -100 %, UP+ +100 %),
                                                 * then saved is the SNG7 this build makes of the same; full: the record's
                                                 * NLANE knob lanes leave none for the RAMPs (-2, the rest loaded) */
{
    static uint8_t rec[SONG_MAX], rec2[SONG_MAX];
    static const struct { uint8_t pat, ch, st, hits, ramp; int8_t m; } S[] = {   /* (in the order saved) */
        {0, 0, 0, 2, 0, -23}, {1, 9, 63, 8, 2, 23}, {5, 3, 7, 5, 1, 0}, {5, 3, 9, 3, 4, 2}, {7, 2, 1, 1, 0, 4},
        {9, 0, 4, 4, 3, 0}, {31, 4, 32, 7, 2, -1}};
    uint32_t i, n, n2, nm, at, w, r, h, nl = full ? NLANE : 0u, nr = 0;
    int rc;
    t_on = 0;
    song_empty(); memset(mot_quant, 0, sizeof mot_quant);
    for (i = 0; i < nl; i++) mot_set(10u + i / (DRUM_NCH * 4u), i / 4u % DRUM_NCH, i % 4u, 0, 7);   /* (knob lanes) */
    for (i = 0; i < sizeof S / sizeof S[0]; i++) {   /* the steps, without their RAMPs: the words as SNG6's but R */
        mot_set(S[i].pat, S[i].ch, P_MICRO, S[i].st, MICRO0 + (uint32_t)(int32_t)S[i].m);
        mot_set(S[i].pat, S[i].ch, P_RATCH, S[i].st, S[i].hits);
    }
    mot_set(7, 2, P_COND, 1, 50);
    n = song_pack(rec2);                        /* the record of the older build: its words with their RAMPs */
    memcpy(rec2, six ? "SNG6" : "SNG5", 4);
    at = 8u + sizeof(seq_song_t) + nl * sizeof(lane_t) + sizeof lfos + sizeof mot_quant;
    memcpy(&nm, rec2 + at, 4);
    CHECK(nm == 7u, "SNG5/6: 7 marks");
    for (i = 0; i < nm; i++) {
        memcpy(&w, rec2 + at + 4u + i * 4u, 4);
        r = S[i].ramp;
        if (!six && r > 2u) r = 0u;             /* (SNG5: no DOWN+ UP+) */
        h = S[i].hits;
        if (six) w = (w & 0x001FFFFFu) | (mark_mi(w) + 47u * (h > 1u ? 1u + (h - 2u) * 5u + r : 0u)) << 21;
        else w = (w & 0x001FFFFFu) | mark_mi(w) << 21 | (h > 1u ? 1u + (h - 2u) * 3u + r : 0u) << 27;
        memcpy(rec2 + at + 4u + i * 4u, &w, 4);
    }
    song_empty();
    rc = song_unpack(rec2, n, SEQ_NPAT);
    CHECK(rc == (full ? -2 : 0) && mark_n == 7u, full ? "SNG5/6, all lanes taken: -2, the marks loaded" : "SNG5/6 loads");
    for (i = 0; i < sizeof S / sizeof S[0]; i++) {
        r = S[i].ramp;
        if (!six && r > 2u) r = 0u;
        r = full || S[i].hits < 2u ? 0u : RB(ramp_from6(r));
        nr += r != 0u;
        CHECK(mot_micro(S[i].pat, S[i].ch, S[i].st) == S[i].m &&
              mot_ratch(S[i].pat, S[i].ch, S[i].st) == (S[i].hits | r),
              "SNG5/6: each step's micro timing, hits and RAMP (all lanes taken: FLAT)");
        CHECK(mark_r(w_at(S[i].pat, S[i].ch, S[i].st)) == (S[i].hits > 1u ? 1u + (S[i].hits - 2u) * 5u : 0u),
              "SNG5/6: the words without their RAMPs");
    }
    CHECK(mot_get(7, 2, P_COND, 1) == 50 && nr == (full ? 0u : six ? 5u : 3u) &&
          mot_count() == (full ? NLANE : six ? 4u : 3u), "SNG5/6: its condition, a lane a pattern's channel with a "
          "RAMP (pattern 6's channel 4: two steps, one lane)");
    n2 = song_pack(rec2);
    song_empty();                               /* the same steps set here */
    for (i = 0; i < nl; i++) mot_set(10u + i / (DRUM_NCH * 4u), i / 4u % DRUM_NCH, i % 4u, 0, 7);
    for (i = 0; i < sizeof S / sizeof S[0]; i++) {
        r = S[i].ramp;
        if (!six && r > 2u) r = 0u;
        mot_set(S[i].pat, S[i].ch, P_MICRO, S[i].st, MICRO0 + (uint32_t)(int32_t)S[i].m);
        mot_set(S[i].pat, S[i].ch, P_RATCH, S[i].st, S[i].hits);
        if (!full && S[i].hits > 1u) mot_set(S[i].pat, S[i].ch, P_RAMP, S[i].st, ramp_val(ramp_from6(r)));
    }
    mot_set(7, 2, P_COND, 1, 50);
    n = song_pack(rec);
    CHECK(!memcmp(rec2, "SNG7", 4) && n2 == n && !memcmp(rec, rec2, n), "SNG5/6 saved: this build's SNG7, the same bytes");
    song_empty();
    CHECK(song_unpack(rec, n, SEQ_NPAT) == 0 && song_pack(rec2) == n && !memcmp(rec, rec2, n),
          "SNG7 loaded and saved again: the same bytes");
    song_empty();
}
static void record_test(void)
{
    static uint8_t rec[SONG_MAX], rec2[SONG_MAX];
    static const uint32_t F[3] = {SONG_FMT3, SONG_FMT2, SONG_FMT1};
    uint32_t i, n, n2, ch;
    int rc;
    t_on = 0;
    for (i = 0; i < 3u; i++) {                  /* the old records: channel 6 (SNG1's 6 is 8 now) */
        song_empty();
        n = old_record(rec, F[i], 3);
        ch = F[i] == SONG_FMT1 ? 8u : 6u;
        rc = song_unpack(rec, n, SEQ_NPAT);
        CHECK(rc == 0, "an old record loads");
        CHECK(mot_get(2, ch, P_COND, 0) == 50 && mot_get(2, ch, P_COND, 5) == COND_FILL &&
              mot_get(2, ch, P_COND, 1) == MOT_NONE, "its conditions");
        CHECK(mot_micro(2, ch, 0) == 19 && mot_get(2, ch, P_MICRO, 0) == MICRO0 + 19u,
              "a first build's micro timing (fifths: 9 -> 19/24)");
        CHECK(mot_micro(2, ch, 3) == 23 && mot_micro(2, ch, 4) == -7 && mot_micro(2, ch, 6) == 0, "its micro timing");
        CHECK(mot_ratch(2, ch, 5) == 3 && mot_ratch(2, ch, 7) == 1 &&
              mot_ratch(31, F[i] == SONG_FMT1 ? 9u : 7u, 63) == 4, "its ratchets");
        CHECK(mark_n == 5u && mot_count() == 3u, "its marks and lanes (micro 0, one hit: none)");
        CHECK(mot_get(0, 0, 0, 0) == 0 && mot_get(0, 0, 1, 1) == 1 && mot_get(0, 1, 2, 2) == 2, "its knob lanes");
        CHECK(F[i] != SONG_FMT3 || (mot_quant[1][6] == 40 && mot_quant[31][9] == 100 && mot_quant[0][6] == 0),
              "its QUANT");
        n2 = song_pack(rec2);                   /* saved as SNG7, loaded back */
        CHECK(!memcmp(rec2, "SNG7", 4), "saved as SNG7");
        memcpy(rec, rec2, n2);
        song_empty(); memset(mot_quant, 0, sizeof mot_quant);
        CHECK(song_unpack(rec, n2, SEQ_NPAT) == 0, "SNG7 loads");
        CHECK(mot_micro(2, ch, 0) == 19 && mot_ratch(2, ch, 5) == 3 && mot_get(2, ch, P_COND, 5) == COND_FILL &&
              mark_n == 5u && mot_count() == 3u && mot_get(0, 1, 2, 2) == 2, "SNG7: the same");
        CHECK(F[i] != SONG_FMT3 || (mot_quant[1][6] == 40 && mot_quant[31][9] == 100), "SNG7: QUANT the same");
        CHECK(song_pack(rec2) == n2 && !memcmp(rec, rec2, n2), "SNG7 saved again: the same bytes");
        mot_set(2, ch, P_COND, 0, MOT_NONE); mot_set(2, ch, P_RATCH, 5, MOT_NONE); mot_set(2, 0, P_RATCH, 1, 2);
        mot_quant[2][ch] = 7;
        CHECK(song_unpack(rec, n2, 2) == 0 && mot_get(2, ch, P_COND, 0) == 50 && mot_ratch(2, ch, 5) == 3 &&
              mot_ratch(2, 0, 1) == 1 && mot_quant[2][ch] == 0 && mark_n == 5u,
              "one pattern back from SNG7 (HOME + REC)");
        mot_set(2, ch, P_COND, 0, MOT_NONE); mot_set(31, 3, P_RATCH, 1, 2);
        n = old_record(rec, F[i], 3);
        CHECK(song_unpack(rec, n, 2) == 0 && mot_get(2, ch, P_COND, 0) == 50 && mot_ratch(31, 3, 1) == 2 &&
              mark_n == 6u, "one pattern back from an old record");
    }
    song_empty();                               /* SNG3 with 130 knob lanes: 120 load, then still the marks and QUANT */
    n = old_record(rec, SONG_FMT3, 130);
    CHECK(song_unpack(rec, n, SEQ_NPAT) == -2, "more knob lanes than NLANE: -2");
    CHECK(mot_count() == NLANE && mark_n == 5u && mot_micro(2, 6, 0) == 19 && mot_quant[1][6] == 40,
          "what fits loaded");
    for (i = 0; i < NMARK; i++) {               /* the most: NMARK marks, NLANE lanes, every QUANT */
        uint32_t m = MICRO0 - 23u + i % 47u;
        mot_set(i / (DRUM_NCH * SEQ_NSTEP), i / SEQ_NSTEP % DRUM_NCH, P_MICRO, i % SEQ_NSTEP, m == MICRO0 ? m + 1u : m);
    }
    memset(mot_quant, 100, sizeof mot_quant);
    n2 = song_pack(rec2);
    CHECK(mark_n == NMARK && n2 == SONG5_MAX, "full: the record at its largest");
    printf("marks: the record, full: %u B of %u\n", n2, SONG_NBLOB * 0x1000u - 12u);
    memcpy(rec, rec2, n2);
    song_empty();
    CHECK(song_unpack(rec, n2, SEQ_NPAT) == 0 && mark_n == NMARK && mot_count() == NLANE && song_pack(rec2) == n2 &&
          !memcmp(rec, rec2, n2), "full: loaded back the same");
    n2 = old_record(rec, 0x38474E53u, 0);
    CHECK(song_unpack(rec, n2, SEQ_NPAT) == -1, "another format: not a record");
}

__attribute__((constructor)) static void marks_test(void)
{
    uint32_t k, i, ops = 0;
    for (k = 0; k < NKEY; k++) model[k] = MARK_NONE;
    mark_reset();
    for (i = 0; i < 60000u; i++) {              /* filling up to full and on, the stores watched */
        t_on = i < 4000u || (i >= 40000u && i < 46000u);
        if (rnd(50)) op_set(); else op_bulk();
        ops++;
        if (!(i % 997u)) check("random edits");
    }
    check("random edits");
    printf("marks: %u edits, %u stores watched (%u removals moving words back), %u MOTION FULL, %u in use at most\n",
           ops, t_stores, t_shifts, t_full, t_max);
    t_on = 0;
    for (i = 0; i < 4000u && model_n() < NMARK; i++) op_set();   /* full: every new step refused, others still edited */
    for (k = 0; k < NKEY && model_n() < NMARK; k++)
        if (model[k] == MARK_NONE) {
            mot_set(k / (DRUM_NCH * SEQ_NSTEP), k / SEQ_NSTEP % DRUM_NCH, P_COND, k % SEQ_NSTEP, 50);
            model[k] = mark_with(MARK_NONE, P_COND, 50);
        }
    check("filled");
    if (mark_n != NMARK) { printf("marks: FAIL not filled: %u\n", mark_n); t_fail++; }
    t_on = 1;
    for (i = 0; i < 3000u; i++) op_set();
    check("full");
    printf("marks: full, %u edits more: %u stores watched (%u removals moving words back), %u MOTION FULL\n", i,
           t_stores, t_shifts, t_full);
    layout_test();
    ramp_test();
    sng4_test();
    sng56_test(0, 0);
    sng56_test(1, 0);
    sng56_test(1, 1);
    record_test();
    printf("marks: %s\n", t_fail ? "FAILED" : "all ok");
    exit(t_fail ? 1 : 0);
}
