/* SPDX-License-Identifier: GPL-3.0-only */
/* Drumtonic's step sequencer on the FM-1 (seq_fm1.h). Needs drum.c's drum_hit_isr. */
#include "seq_fm1.h"

#define PAIR_PER_BPM 51952u                     /* an eighth as 2^32, per block of 16 frames, per BPM:
                                                 * 2^32 * 16 * 2 / (60 * 44100) */
#define PCT 42949673u                           /* 1 % of 2^32 */
#ifndef SEQ_STEP_HOOK
#define SEQ_STEP_HOOK(pat, st) ((void)0)
#endif
#ifndef SEQ_COND
#define SEQ_COND(pat, ch, st) 1
#endif
#ifndef SEQ_MICRO                               /* micro timing: a step's hit late (+) or early (-), in 24ths of a step */
#define SEQ_MICRO(pat, ch, st) 0
#define SEQ_COND_AT(pat, ch, st) 1
#define SEQ_LOCK_CH(pat, st, ch) ((void)0)
#endif
#ifndef SEQ_REC_MICRO                           /* a hit recorded (main, or note repeat's in main): its step and how far
                                                 * from it it came, in 24ths (+ late, - early; -12..12) */
#define SEQ_REC_MICRO(pat, ch, st, m) ((void)0)
#endif
#ifndef SEQ_RATCH                               /* ratchets: a step's hits, 1..4, evenly through it */
#define SEQ_RATCH(pat, ch, st) 1u
#endif
static const uint8_t SEQ_TPS[SEQ_NRATE] = {6, 4, 3, 8};    /* MIDI clocks a step: 1/16, 1/16T, 1/32, 1/8T */

/* The latency. A hit starts at a 16-frame block of a render; the render fills the I2S half that plays after the one
 * playing now (256 frames), and fx.c's limiter looks 64 frames ahead: a hit made at frame f of the time line (seq_fr,
 * the frames rendered) is heard when the DAC reaches f + 64, and the DAC is at aud_F - 256 when the render of the
 * frames from aud_F starts (aud_t, TIMER4). So:
 *   clock and notes out (internal clock): each goes out when its sound is heard, not when it is made (~7 ms early);
 *   clock in: the clocks' arrival (rt_t, the timer ISR) on the DAC's time line, filtered (the tempo, the phase), and
 *   the steps made ahead so they are heard on the clock; until the filter has locked (4 clocks), as they come. */
#ifndef RING_PUBLISH
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
#endif
#define SEQ_HALF 256                            /* platform.c's HALF_FRAMES */
#define SEQ_LA 64                               /* fx.c's FX_LA, the limiter's look-ahead */
#define SEQ_FPT (44100.0f / (1e6f * (float)FM1_TICKS_PER_US))   /* frames a TIMER4 tick */
#define SEQ_TPF (1e6f * (float)FM1_TICKS_PER_US / 44100.0f)     /* TIMER4 ticks a frame */
static uint32_t seq_fr;                         /* ISR: the time line, frames (16 a block) */
static volatile uint32_t aud_F, aud_t;          /* ISR: seq_fr and the time when this render started */
static void seq_at_render(void) { aud_F = seq_fr; aud_t = fm1_ticks(); }
static uint32_t seq_due(void)                   /* ISR: the time this block is heard (TIMER4) */
{
    return aud_t + (uint32_t)(int32_t)((float)(int32_t)(seq_fr - aud_F + SEQ_HALF + SEQ_LA) * SEQ_TPF);
}
#define SEQ_NCO 32u
static volatile uint32_t seq_co_t[SEQ_NCO], seq_co_w;   /* ISR -> main: the clocks out, when each is due */
static uint32_t seq_co_r;                       /* (main) */
static uint32_t ck_seen, ck_r, ck_q, ck_d, ck_lock;   /* ISR, the clock in: rt_ entries taken, clocks in, the next
                                                 * clock to play (as a count of clocks in), the downbeat's, lock */
static float ck_a, ck_e, ck_T = 919.0f;         /* (the last clock in, its filtered time, both from this block's
                                                 * frame on the DAC's time line; a clock, frames) */
static uint8_t ck_ext;                          /* (following the clock at the last block) */

static seq_song_t seq_song;
static volatile struct {
    uint8_t run, ext;                           /* main: playing; following a MIDI clock */
    uint8_t pat, next;                          /* ISR: the pattern playing / main: the one asked for */
    uint8_t step, frac;                         /* ISR: the step playing, how far into it (0..255) */
    uint8_t bpm;                                /* main: 40..250 */
    uint16_t mute, solo;                        /* main: bit = channel */
    uint8_t restart;                            /* main sets, ISR clears: start at step 1 */
    uint8_t chain_n, chain_at;                  /* main: the chain's length / ISR: where in it */
    uint16_t pass;                              /* ISR: times round the pattern since PLAY or it changed (cycles) */
    uint32_t clk_in;                            /* main: MIDI clocks received */
    uint32_t clk_out;                           /* ISR: MIDI clocks due (internal clock) */
    uint32_t eighths;                           /* ISR: eighths since PLAY (the LFOs' time) */
} seq = {.bpm = 120};
static uint8_t seq_chain_l[SEQ_NCHAIN];         /* (main writes it before chain_n) */
static volatile uint8_t seq_skip[DRUM_NCH];     /* step + 1 not to play once (a hit recorded ahead: main by hand, the ISR
                                                 * a note repeat's): the next step only, then cleared */
static volatile uint8_t seq_realign_req[DRUM_NCH];   /* main: + 1 when a part's length changed (PRESETS) */
static uint8_t seq_realign_done[DRUM_NCH];      /* ISR: the requests done: the part back in step with the pattern */
/* micro timing (ISR): a hit late plays in its step once frac reaches its 24ths; a hit early plays in the step before,
 * once frac is that many 24ths from its end (its condition and its locks then), and its step then skips it */
#define SEQ_FRAC24(k) (((k) * 255u + 12u) / 24u)   /* k 24ths of a step, as frac (0..255) */
static uint8_t seq_late[DRUM_NCH], seq_late_vel[DRUM_NCH];   /* this step's hit waiting: its 24ths (0: none) */
static uint8_t seq_early[DRUM_NCH];             /* the next step's hit, early: its 24ths (0: none) */
static uint8_t seq_early_done[DRUM_NCH], seq_early_pat[DRUM_NCH];   /* step + 1 (and pattern) whose hit went early */
static uint8_t seq_nx_pat, seq_nx_st;           /* the next step, and its pattern */
static uint8_t seq_late_n[DRUM_NCH];            /* (its ratchet) */
/* ratchets (ISR): a hit's next ones, each 24 / n 24ths of a step after the one before; where the next is due, in 24ths
 * of the step playing: 24 and on is in the next step (a late or an early hit's ratchet runs over a step's end) */
static uint8_t seq_rat_left[DRUM_NCH], seq_rat_at[DRUM_NCH], seq_rat_d[DRUM_NCH], seq_rat_vel[DRUM_NCH];
static uint8_t seq_pos[DRUM_NCH], seq_nx_pos[DRUM_NCH];   /* the step each part is on, and its next (parts loop on their own lengths) */
static volatile uint16_t seq_ev[32];            /* ISR -> main: the hits played, ch << 8 | vel */
static volatile uint32_t seq_ev_t[32];          /* (when each is heard, TIMER4) */
static volatile uint32_t seq_ev_w;
static uint32_t seq_ev_r;                       /* (main) */
static uint32_t seq_pp, seq_bp, seq_ticks;   /* (ISR) the step pair's phase, the eighth's (tempo) */
static uint8_t seq_half, seq_clkph;

static uint32_t seq_bit(const uint32_t *w, uint32_t st) { return w[st >> 5 & 1u] >> (st & 31u) & 1u; }
static uint32_t seq_get(uint32_t pat, uint32_t ch, uint32_t st)
{
    const seq_track_t *t = &seq_song.p[pat % SEQ_NPAT].t[ch % DRUM_NCH];
    st %= SEQ_NSTEP;
    return seq_bit(t->hit, st) ? seq_bit(t->acc, st) ? STEP_ACC : STEP_ON : STEP_OFF;
}
static void seq_set(uint32_t pat, uint32_t ch, uint32_t st, uint32_t v)
{
    seq_track_t *t = &seq_song.p[pat % SEQ_NPAT].t[ch % DRUM_NCH];
    uint32_t w, b;
    st %= SEQ_NSTEP;
    w = st >> 5;
    b = 1u << (st & 31u);
    t->acc[w] = v == STEP_ACC ? t->acc[w] | b : t->acc[w] & ~b;
    t->hit[w] = v ? t->hit[w] | b : t->hit[w] & ~b;
}

/* ---- the ISR side */
static uint32_t seq_hit_ms[DRUM_NCH], seq_hits[DRUM_NCH];   /* (the host tests: when each channel's last hit was made, how many) */
static void seq_out(uint32_t ch, uint32_t vel)  /* a hit now, and out on MIDI when it is heard */
{
    seq_hit_ms[ch] = fm1_ms;
    seq_hits[ch]++;
    drum_hit_isr(ch, vel, 0);
    seq_ev[seq_ev_w % 32u] = (uint16_t)(ch << 8 | vel);
    seq_ev_t[seq_ev_w % 32u] = seq_due();
    seq_ev_w++;
}

/* beat repeat (ISR; HOME, ARP on + a white key): the sequencer's hits of the slice in progress (1/4, 1/8, 1/16, 1/32,
 * on the beat's grid from PLAY) again and again; with no hit in it so far, the slice of the last hit (within a bar): what
 * was just heard, never silence (until 2026-11-11 an empty step's 16th or 32nd repeated silence). A slice already over
 * loops from the next of its hits on the grid, not at once. Another length while it loops (a second white held, or the
 * first one's again): a piece of the loop being heard, not of the pattern going on unheard (until 2026-11-12 the jump):
 * shorter, the loop's first piece (no hit there: the piece of its first hit); longer, up to the loop's first length, the
 * piece holding it; on the grid, from where the new piece would be. Longer than that: a new slice, as at first.
 * The time is musical, from PLAY (seq_now: an eighth is 4096);
 * every hit the sequencer makes goes in a history. The slice plays once as it is; from its end on the sequencer's own
 * hits are not heard (it goes on: when the repeat ends the pattern is where it would have been) and the slice's hits,
 * from the history, are, each at its place in the slice, as the channels' mutes are now (a channel muted: silent; one
 * unmuted while it loops: only if it was heard in the slice, as only what is heard goes in the history) */
#define SEQ_BR_E8 4096u
#define SEQ_BH_N 512u                           /* (a 1/4 slice: 10 channels at 1/32 with ratchet 4 are 320 hits) */
static volatile uint16_t seq_br_len;            /* main: the slice, in 4096ths of an eighth (0: off) */
static volatile uint8_t seq_br_req;             /* main: + 1 after seq_br_len: start (again) / stop */
static volatile uint8_t seq_br_done, seq_br_on;   /* (ISR: the requests done; on) */
static uint8_t seq_br_loop;                     /* (past the slice's first time: looping) */
static uint32_t seq_br_g, seq_br_L, seq_br_tl, seq_br_cur, seq_br_n;   /* (the slice's start and length; the last block's
                                                 * time from the start; the loop's next hit; its hits' end) */
static uint32_t seq_br_L0, seq_br_n0, seq_br_b, seq_br_i0;   /* (the loop as first made: its length, its hits; the piece
                                                 * of it playing: where in it, its first hit) */
static uint32_t seq_bh_t[SEQ_BH_N], seq_bh_w;   /* (the history: each hit's time, ch << 8 | vel) */
static uint16_t seq_bh_cv[SEQ_BH_N];
static uint16_t seq_bl_o[SEQ_BH_N], seq_bl_cv[SEQ_BH_N];   /* (the loop: each hit's place in the slice, ch << 8 | vel) */
static uint32_t seq_clk_n;                      /* (a MIDI clock: clocks since the downbeat; the time since the last, in */
static float seq_sub;                           /*  clocks, from the filter; */
static uint32_t seq_now_x;                      /*  the last time made of them) */
static uint32_t seq_now(void)                   /* (never back: on a MIDI clock the filter's corrections, a clock late or
                                                 * a tempo changed, would take it back up to a clock; it waits instead) */
{
    if (seq.ext) {
        uint32_t v = seq_clk_n / 3u * 1024u + seq_clk_n % 3u * 1024u / 3u + (uint32_t)(seq_sub * (SEQ_BR_E8 / 12.0f));
        if ((int32_t)(v - seq_now_x) > 0) seq_now_x = v;
        return seq_now_x;
    }
    return seq.eighths * SEQ_BR_E8 + (seq_bp >> 20);
}
static void seq_hit(uint32_t ch, uint32_t vel)  /* the sequencer's hit: in the history; heard unless a repeat loops */
{
    uint32_t t = seq_now();
    seq_bh_t[seq_bh_w % SEQ_BH_N] = t;
    seq_bh_cv[seq_bh_w % SEQ_BH_N] = (uint16_t)(ch << 8 | vel);
    seq_bh_w++;
    if (seq_br_on && (seq_br_loop || t - seq_br_g >= seq_br_L)) return;
    seq_out(ch, vel);
}
static void seq_br_play(uint32_t cv)
{
    uint32_t ch = cv >> 8, mute = seq.mute, solo = seq.solo;
    if (ch >= DRUM_NCH || (mute >> ch & 1u) || (solo && !(solo >> ch & 1u))) return;
    seq_out(ch, cv & 0xFFu);
}
static void seq_br_block(void)                  /* every block, after the sequencer's */
{
    uint32_t t, p, i;
    if (seq_br_done != seq_br_req) {
        seq_br_done = seq_br_req;
        t = seq_now();
        if (seq_br_on && seq_br_loop && seq.run && seq_br_len && seq_br_len <= seq_br_L0 && t - seq_br_g < 0x80000000u) {
            uint32_t L = seq_br_len, hp = seq_br_b + (t - seq_br_g) % seq_br_L, b = hp - hp % L;   /* (where in the loop) */
            if (L < seq_br_L) {                 /* (shorter: the loop's start; no hit there, its first hit's piece) */
                b = seq_br_b;
                if (seq_br_i0 < seq_br_n && seq_bl_o[seq_br_i0] >= b + L) b = seq_bl_o[seq_br_i0] - seq_bl_o[seq_br_i0] % L;
            }
            seq_br_b = b;
            seq_br_L = L;
            seq_br_g = t - t % L;
            p = t % L;
            for (i = 0; i < seq_br_n0 && seq_bl_o[i] < b; i++) {}
            seq_br_i0 = i;
            for (; i < seq_br_n0 && seq_bl_o[i] < b + L; i++) {}
            seq_br_n = i;
            for (i = seq_br_i0; i < seq_br_n && seq_bl_o[i] - b <= p; i++) {}
            seq_br_cur = i;
            seq_br_tl = p;
            return;
        }
        seq_br_on = 0;
        if (seq.run && seq_br_len) {
            t = seq_now();
            seq_br_L = seq_br_len;
            seq_br_g = t - t % seq_br_L;
            if (seq_bh_w) {                     /* (no hit in it so far: the last hit's) */
                uint32_t tl = seq_bh_t[(seq_bh_w - 1u) % SEQ_BH_N];
                if ((int32_t)(tl - seq_br_g) < 0 && t - tl < 8u * SEQ_BR_E8) seq_br_g = tl - tl % seq_br_L;
            }
            seq_br_on = 1;
            seq_br_loop = 0;
        }
    }
    if (!seq_br_on) return;
    if (!seq.run) { seq_br_on = 0; return; }
    t = seq_now() - seq_br_g;
    if (t >= 0x80000000u) { seq_br_on = 0; return; }   /* (the time went back: PLAY again) */
    if (!seq_br_loop && t < seq_br_L) return;   /* (the slice's first time: as it is) */
    if (!seq_br_loop) {                         /* its end: the loop, its hits from the history */
        seq_br_loop = 1;
        seq_br_n = seq_br_cur = 0;
        p = t % seq_br_L;
        for (i = seq_bh_w > SEQ_BH_N ? seq_bh_w - SEQ_BH_N : 0u; i != seq_bh_w; i++) {
            uint32_t o = seq_bh_t[i % SEQ_BH_N] - seq_br_g;
            if (o >= seq_br_L) continue;
            seq_bl_o[seq_br_n] = (uint16_t)o;
            seq_bl_cv[seq_br_n++] = seq_bh_cv[i % SEQ_BH_N];
        }
        if (t - seq_br_L > 32u)                 /* (a slice already over: on the grid, from its next hit) */
            while (seq_br_cur < seq_br_n && seq_bl_o[seq_br_cur] <= p) seq_br_cur++;
        seq_br_tl = t - p;
        seq_br_L0 = seq_br_L; seq_br_n0 = seq_br_n; seq_br_b = seq_br_i0 = 0;
    }
    p = t % seq_br_L;
    if (t / seq_br_L != seq_br_tl / seq_br_L) {  /* round again (the time never goes back): the end's, then on */
        while (seq_br_cur < seq_br_n) seq_br_play(seq_bl_cv[seq_br_cur++]);
        seq_br_cur = seq_br_i0;
    }
    while (seq_br_cur < seq_br_n && seq_bl_o[seq_br_cur] - seq_br_b <= p) seq_br_play(seq_bl_cv[seq_br_cur++]);
    seq_br_tl = t;
}

static void seq_hit_n(uint32_t ch, uint32_t vel, uint32_t at, uint32_t n)   /* a hit at 24ths at of the step, n with its
                                                 * ratchet; while the channel's ratchet still has hits to come, none:
                                                 * the ratchet goes first (an early hit, the next step's) */
{
    if (seq_rat_left[ch]) return;
    seq_hit(ch, vel);
    if (n < 1u || n > 4u) n = 1u;
    seq_rat_left[ch] = (uint8_t)(n - 1u);
    seq_rat_d[ch] = (uint8_t)(24u / n);
    seq_rat_at[ch] = (uint8_t)(at + 24u / n);
    seq_rat_vel[ch] = (uint8_t)vel;
}

static uint32_t seq_tlen(const seq_pat_t *p, uint32_t ch) { return p->tlen[ch] ? p->tlen[ch] : p->len ? p->len : 16u; }
static uint32_t seq_pos_after(uint32_t pat, uint32_t ch)
{
    uint32_t s = seq_pos[ch] + 1u;
    return s >= seq_tlen(&seq_song.p[pat], ch) ? 0u : s;
}
static void seq_peek(uint32_t pat, uint32_t st)  /* the step after st: seq_nx_pat / seq_nx_st (as seq_take_next will) */
{
    uint32_t len = seq_song.p[pat].len;
    seq_nx_pat = (uint8_t)pat;
    seq_nx_st = (uint8_t)(st + 1u);
    if (st + 1u >= (len ? len : 16u)) {
        seq_nx_st = 0;
        seq_nx_pat = seq.chain_n >= 2u && seq.next == seq.pat ? seq_chain_l[(seq.chain_at + 1u) % seq.chain_n] : seq.next;
    }
    {
        uint32_t ch;
        for (ch = 0; ch < DRUM_NCH; ch++)       /* (a length changed, its realign still to come: as seq_next will) */
            seq_nx_pos[ch] = (uint8_t)(seq_nx_pat != pat ? 0u : seq_realign_done[ch] != seq_realign_req[ch]
                                       ? seq_nx_st % seq_tlen(&seq_song.p[pat], ch) : seq_pos_after(pat, ch));
    }
}

static void seq_fire(uint32_t st)
{
    uint32_t ch, pat = seq.pat, mute = seq.mute, solo = seq.solo;
    const seq_pat_t *p = &seq_song.p[pat];
    for (ch = 0; ch < DRUM_NCH; ch++) {         /* (a late hit not out yet: now, before the next step's) */
        if (seq_late[ch]) { seq_late[ch] = 0; seq_hit_n(ch, seq_late_vel[ch], 24u, seq_late_n[ch]); }
        if (seq_rat_left[ch]) seq_rat_at[ch] = (uint8_t)(seq_rat_at[ch] >= 24u ? seq_rat_at[ch] - 24u : 0u);   /* (on into this step) */
    }
    seq.step = (uint8_t)st;
    SEQ_STEP_HOOK(pat, st);
    for (ch = 0; ch < DRUM_NCH; ch++) {
        uint32_t vel, s = seq_pos[ch], early = seq_early_done[ch] == s + 1u && seq_early_pat[ch] == pat;
        int32_t m;
        seq_early_done[ch] = 0;
        seq_early[ch] = 0;
        {                                       /* (for this step or none: one set late is not kept for a pass on) */
            uint32_t sk = seq_skip[ch];
            seq_skip[ch] = 0;
            if (sk == s + 1u) continue;
        }
        if (early) continue;                    /* (it went early, in the step before) */
        if (!seq_bit(p->t[ch].hit, s) || (mute >> ch & 1u) || (solo && !(solo >> ch & 1u))) continue;
        if (!SEQ_COND(pat, ch, s)) continue;
        vel = seq_bit(p->t[ch].acc, s) ? SEQ_VEL_ACC : SEQ_VEL;
        m = SEQ_MICRO(pat, ch, s);
        if (m > 0) { seq_late[ch] = (uint8_t)m; seq_late_vel[ch] = (uint8_t)vel; seq_late_n[ch] = (uint8_t)SEQ_RATCH(pat, ch, s); continue; }
        seq_hit_n(ch, vel, 0u, SEQ_RATCH(pat, ch, s));   /* (on the step; early but not sent early: the first step, a jump) */
    }
    seq_peek(pat, st);                          /* the next step's early hits */
    for (ch = 0; ch < DRUM_NCH; ch++) {
        int32_t m;
        if (!seq_bit(seq_song.p[seq_nx_pat].t[ch].hit, seq_nx_pos[ch])) continue;
        m = SEQ_MICRO(seq_nx_pat, ch, seq_nx_pos[ch]);
        if (m < 0) seq_early[ch] = (uint8_t)-m;
    }
}

static void seq_micro_block(void)               /* every block, frac set: the late hits, the ratchets, the next step's early hits */
{
    uint32_t ch, f = seq.frac, mute = seq.mute, solo = seq.solo;
    for (ch = 0; ch < DRUM_NCH; ch++) {
        if (seq_rat_left[ch] && seq_rat_at[ch] < 24u && f >= SEQ_FRAC24(seq_rat_at[ch])) {
            if (!(mute >> ch & 1u) && !(solo && !(solo >> ch & 1u))) seq_hit(ch, seq_rat_vel[ch]);
            seq_rat_left[ch]--;
            seq_rat_at[ch] = (uint8_t)(seq_rat_at[ch] + seq_rat_d[ch]);
        }
        if (seq_late[ch] && f >= SEQ_FRAC24(seq_late[ch])) {   /* (muted since its step: not heard, nor its ratchet) */
            uint32_t at = seq_late[ch];
            seq_late[ch] = 0;
            if (!(mute >> ch & 1u) && !(solo && !(solo >> ch & 1u))) seq_hit_n(ch, seq_late_vel[ch], at, seq_late_n[ch]);
        }
        if (seq_early[ch] && f >= 255u - SEQ_FRAC24(seq_early[ch])) {
            uint32_t pat = seq_nx_pat, st = seq_nx_pos[ch], at = 24u - seq_early[ch];
            const seq_pat_t *p = &seq_song.p[pat];
            seq_early[ch] = 0;
            if (seq_skip[ch] == st + 1u) continue;   /* (a hit by hand recorded there: the step will skip it) */
            seq_early_done[ch] = (uint8_t)(st + 1u);
            seq_early_pat[ch] = (uint8_t)pat;
            if (!seq_bit(p->t[ch].hit, st) || (mute >> ch & 1u) || (solo && !(solo >> ch & 1u))) continue;
            if (!SEQ_COND_AT(pat, ch, st)) continue;
            SEQ_LOCK_CH(pat, st, ch);
            seq_hit_n(ch, seq_bit(p->t[ch].acc, st) ? SEQ_VEL_ACC : SEQ_VEL, at, SEQ_RATCH(pat, ch, st));
        }
    }
}

static void seq_take_next(void)                 /* the pattern's end: the asked-for one, or the chain's next */
{
    uint32_t to = seq.next;
    if (seq.chain_n >= 2u && seq.next == seq.pat) {
        seq.chain_at = (uint8_t)((seq.chain_at + 1u) % seq.chain_n);
        to = seq_chain_l[seq.chain_at];
        seq.next = (uint8_t)to;
    }
    if (to != seq.pat) seq.pass = 0;
    else seq.pass++;
    seq.pat = (uint8_t)to;
}

static void seq_next(void)
{
    uint32_t st = seq.step + 1u, len = seq_song.p[seq.pat].len, from = seq.pat, ch;
    if (st >= (len ? len : 16u)) {
        st = 0;
        seq_take_next();
    }
    for (ch = 0; ch < DRUM_NCH; ch++) {         /* (the same pattern again: each part just goes on) */
        seq_pos[ch] = (uint8_t)(seq.pat != from ? 0u : seq_pos_after(from, ch));
        if (seq_realign_done[ch] != seq_realign_req[ch]) {   /* its length changed: in step with the pattern again */
            seq_realign_done[ch] = seq_realign_req[ch];
            seq_pos[ch] = (uint8_t)(st % seq_tlen(&seq_song.p[seq.pat], ch));
        }
    }
    seq_fire(st);
}

static void seq_clk_out(uint32_t n)             /* n MIDI clocks out, due when this block is heard */
{
    uint32_t t = seq_due();
    while (n--) { seq_co_t[seq_co_w % SEQ_NCO] = t; RING_PUBLISH(); seq_co_w++; }
}

static void seq_rx(void)                        /* the clock and transport in since the last block: the filter */
{
    while (ck_seen != rt_w) {
        uint32_t i = ck_seen++ % RT_N, b = rt_b[i];
        float a, d, e;
        if (b == 0xFAu) { ck_d = ck_r + 1u; continue; }   /* START: the next clock is the downbeat */
        if (b == 0xFBu) { ck_q = ck_r + 1u; continue; }   /* CONTINUE: on from the next clock */
        if (b != 0xF8u) continue;
        a = (float)(int32_t)(aud_F - seq_fr) - (float)SEQ_HALF + (float)(int32_t)(rt_t[i] - aud_t) * SEQ_FPT;
        d = a - ck_a;                           /* (the gap since the last; e: against the filter's guess) */
        e = a - (ck_e + ck_T);
        ck_r++;
        ck_a = a;
        if (d < 150.0f || d > 12000.0f) {       /* (300 BPM is 367 frames a clock, 20 BPM 5512): a gap, start over */
            ck_lock = 0;
            ck_e = a;
        } else if (ck_lock < 4u || e > 0.5f * ck_T || e < -0.5f * ck_T) {   /* locking (again): the tempo, the gaps */
            ck_T = ck_lock && ck_lock < 4u ? ck_T + (d - ck_T) * 0.5f : d;
            ck_lock = ck_lock < 4u ? ck_lock + 1u : 1u;
            ck_e = a;
        } else {                                /* locked: the jitter (USB frames, the timer) smoothed out */
            ck_e += ck_T + e * 0.2f;
            ck_T += e * 0.02f;
        }
    }
}

static void seq_ext(uint32_t tps)               /* following the clock: the clocks that are due, heard on time */
{
    uint32_t n;
    for (n = 0; n < 8u; n++) {                  /* (behind: catching up, 8 a block at most) */
        int32_t ahead = (int32_t)(ck_q - ck_r); /* (1: the clock still to come) */
        if (ahead > 0 && (ck_lock < 4u || (float)(ahead - 1) * ck_T > 800.0f ||
                          ck_e + (float)ahead * ck_T > (float)(SEQ_LA + 8)))
            break;                              /* (not locked: as they come; else when it is heard on time) */
        seq_sub = 0.0f;
        if (ck_q == ck_d) {                     /* the downbeat */
            ck_d = ck_q - 1u;
            seq_clk_n = 0;
            seq_now_x = 0;
            seq_br_on = 0;
            seq_bh_w = 0;
            seq_ticks = 0;
            seq_clkph = 0;
            memset(seq_pos, 0, sizeof seq_pos);
            seq_fire(0);
        } else {
            seq_clk_n++;
            if (++seq_ticks >= tps) { seq_ticks = 0; seq_next(); }
            if (++seq_clkph >= 12u) { seq_clkph = 0; seq.eighths++; }
        }
        ck_q++;
    }
    {                                           /* frac: between the clocks too, from the filter (micro timing) */
        float sub = 0.0f;
        if (ck_lock >= 4u) {
            sub = 1.0f - (ck_e + (float)(int32_t)(ck_q - ck_r) * ck_T - (float)(SEQ_LA + 8)) / ck_T;
            sub = sub < 0.0f ? 0.0f : sub > 0.99f ? 0.99f : sub;
        }
        seq_sub = sub;
        seq.frac = (uint8_t)(((float)seq_ticks + sub) * 255.0f / (float)tps);
    }
}

static void seq_run(void)
{
    uint32_t old, thr, k, rate = seq_song.p[seq.pat].rate % SEQ_NRATE, tps = SEQ_TPS[rate];
    if (seq.ext != ck_ext) { ck_ext = seq.ext; ck_q = ck_r + 1u; }   /* (switched over: on from the next clock) */
    if (!seq.run) {                             /* (stopped: no late hit, ratchet or early hit waiting; a CONTINUE from
                                                 * a DAW goes on clean) */
        memset(seq_late, 0, sizeof seq_late);
        memset(seq_early, 0, sizeof seq_early);
        memset(seq_rat_left, 0, sizeof seq_rat_left);
        return;
    }
    if (seq.restart) {
        seq.restart = 0;
        seq_pp = seq_bp = 0;
        seq_half = 0;
        seq_clkph = 0;
        seq_ticks = 0;
        seq.pat = seq.next;
        seq.chain_at = 0;
        seq.pass = 0;
        seq.eighths = 0;
        seq.step = 0;
        memset(seq_late, 0, sizeof seq_late);
        memset(seq_early, 0, sizeof seq_early);
        memset(seq_early_done, 0, sizeof seq_early_done);
        memset(seq_rat_left, 0, sizeof seq_rat_left);
        memset((void *)seq_skip, 0, sizeof seq_skip);
        seq_br_on = 0;
        seq_bh_w = 0;                           /* (the history: this run's times only) */
        seq.frac = 0;
        if (seq.ext) { ck_q = ck_d; seq_ext(tps); return; }   /* (the downbeat: the first clock after START) */
        seq.clk_out++;                          /* the first clock is the downbeat */
        seq_clk_out(1);
        memset(seq_pos, 0, sizeof seq_pos);
        seq_fire(0);
        return;
    }
    if (seq.ext) { seq_ext(tps); return; }
    old = seq_bp;                               /* the tempo: MIDI clocks out (24 a quarter), the eighths */
    seq_bp += (uint32_t)seq.bpm * PAIR_PER_BPM;
    if (seq_bp < old) seq.eighths++;
    k = (uint32_t)(((uint64_t)seq_bp * 12u) >> 32);
    if (k != seq_clkph) {
        seq.clk_out += (k + 12u - seq_clkph) % 12u;
        seq_clk_out((k + 12u - seq_clkph) % 12u);
        seq_clkph = (uint8_t)k;
    }
    thr = rate == RATE_16 || rate == RATE_32 ? (uint32_t)seq_song.p[seq.pat].swing * PCT : 50u * PCT;   /* (triplets
                                                 * are never swung) */
    old = seq_pp;                               /* the steps: a pair as 2^32 */
    seq_pp += (uint32_t)((uint64_t)seq.bpm * PAIR_PER_BPM * 6u / tps);
    if (seq_pp < old) { seq_half = 0; seq_next(); }
    else if (!seq_half && seq_pp >= thr) { seq_half = 1; seq_next(); }
    seq.frac = (uint8_t)(seq_half ? ((seq_pp - thr) >> 16) * 255u / ((0u - thr) >> 16)
                                  : (seq_pp >> 16) * 255u / ((thr >> 16) | 1u));
}

/* note repeat (ISR): the channels held repeat at the tempo, div hits a half note (2, 3, 4, 6, 8, 12, 16, 24: 1/4, 1/4T,
 * 1/8, 1/8T, 1/16, 1/16T, 1/32, 1/32T), straight, counted from the key: a key pressed plays at once, then every 1/div
 * of a half note from there (stopped or playing: until 2026-11-11 playing kept the sequencer's grid, so the second hit
 * came anywhere from half a grid step to one later). Each hit goes to main too (seq_nr_take: REC records it on the step
 * nearest, the next step's in the pattern that will play it; that step then skips it, set here, at once) */
static volatile uint16_t seq_nr_mask;           /* main: the channels repeating (held) */
static volatile uint8_t seq_nr_req[DRUM_NCH];   /* main: + 1 a key pressed (its first hit, now) */
static volatile uint8_t seq_nr_div = 2u, seq_nr_vel = 100u;
static uint8_t seq_nr_done[DRUM_NCH];
static uint32_t seq_nr_ph[DRUM_NCH];            /* (each one's phase since its last hit: a grid step as 2^32) */
static volatile uint8_t seq_nr_rec;             /* main: REC on, playing (the next step's hit: that step skips it) */
#define SEQ_NR_N 64u
static volatile uint32_t seq_nr_ev[SEQ_NR_N];   /* ISR -> main: ch | the step nearest << 4 | its pattern << 10 | 0x8000
                                                 * if it is the next step | its 24ths from that step + 16 << 16
                                                 * (seq_frac_micro; until 2026-11-13 its time round, for the ratchets a
                                                 * recording made then) */
static volatile uint32_t seq_nr_w, seq_nr_r;    /* (r: main's; full: the ISR drops the hit's record, not main's place) */
static int32_t seq_frac_micro(uint32_t f)       /* where in the step (frac) as 24ths from the step nearest: 0..12 after
                                                 * this one (frac under 128), -12..-1 before the next one */
{
    return f < 128u ? (int32_t)((f * 24u + 127u) / 255u) : -(int32_t)(((255u - f) * 24u + 127u) / 255u);
}
static void seq_nr_hit(uint32_t ch)
{
    uint32_t pat = seq.pat, st = seq_pos[ch], f = seq.frac, nx = seq.run && f >= 128u;
    int32_t m = seq.run ? seq_frac_micro(f) : 0;
    seq_out(ch, seq_nr_vel);                    /* (not the sequencer's: not in the beat repeat's history) */
    if (nx) {                                   /* (as seq_peek saw it at the step: the pattern after, a part's loop) */
        pat = seq_nx_pat;
        st = seq_nx_pos[ch];
        if (seq_nr_rec) seq_skip[ch] = (uint8_t)(st + 1u);   /* (heard now, not again) */
    }
    if (seq_nr_w - seq_nr_r >= SEQ_NR_N) return;
    seq_nr_ev[seq_nr_w % SEQ_NR_N] = ch | st << 4 | pat % SEQ_NPAT << 10 | (nx ? 0x8000u : 0u) | (uint32_t)(m + 16) << 16;
    RING_PUBLISH();
    seq_nr_w++;
}
static void seq_nr_block(void)
{
    uint32_t ch, div = seq_nr_div ? seq_nr_div : 1u, mask = seq_nr_mask, old;
    /* a block's share of a grid step (an eighth is PAIR_PER_BPM * BPM a block; a grid step, 4 / div eighths; on a MIDI
     * clock, 48 / div clocks of ck_T frames: 2^32 * 16 * div / (48 * ck_T)) */
    uint32_t add = seq.ext && ck_T > 100.0f ? (uint32_t)(4294967296.0f * (float)div / (3.0f * ck_T))
                                            : (uint32_t)((uint64_t)seq.bpm * PAIR_PER_BPM * div >> 2);
    for (ch = 0; ch < DRUM_NCH; ch++) {
        if (seq_nr_done[ch] != seq_nr_req[ch]) { seq_nr_done[ch] = seq_nr_req[ch]; seq_nr_ph[ch] = 0; seq_nr_hit(ch); continue; }
        if (!(mask >> ch & 1u)) continue;
        old = seq_nr_ph[ch];
        seq_nr_ph[ch] += add;
        if (seq_nr_ph[ch] < old) seq_nr_hit(ch);
    }
}

static void seq_block(void)                     /* DRUM_BLOCK_HOOK: every 16 frames */
{
    seq_rx();
    seq_run();
    if (seq.run) seq_micro_block();
    seq_br_block();
    seq_nr_block();
    if (ck_a > -1e6f) ck_a -= 16.0f;            /* (from the next block's frame on) */
    if (ck_e > -1e6f) ck_e -= 16.0f;
    seq_fr += 16u;
}

/* ---- the main loop side */
static void seq_play(void)
{
    seq.restart = 1;
    seq.run = 1;
}
static void seq_stop(void) { seq.run = 0; }

static void seq_pattern(uint32_t p)
{
    seq.chain_n = 0;
    seq.next = (uint8_t)(p % SEQ_NPAT);
    if (!seq.run) seq.pat = seq.next;
}

static void seq_chain(const uint8_t *list, uint32_t n)
{
    uint32_t i;
    if (n > SEQ_NCHAIN) n = SEQ_NCHAIN;
    seq.chain_n = 0;
    for (i = 0; i < n; i++) seq_chain_l[i] = list[i] % SEQ_NPAT;
    if (n < 2u) { if (n) seq_pattern(list[0]); return; }
    seq.chain_at = 0;
    seq.next = seq_chain_l[0];
    if (!seq.run) seq.pat = seq.next;
    seq.chain_n = (uint8_t)n;
}

static void seq_rec(uint32_t ch, uint32_t vel)
{
    uint32_t st, pat = seq.pat, f = seq.frac;   /* (main: the frac the last render ended on, where this hit is made) */
    ch %= DRUM_NCH;
    st = seq_pos[ch];
    if (f >= 128u) {                            /* nearer the next step: that one (in the pattern after, a part's loop:
                                                 * seq_peek's), and it must not sound again */
        pat = seq_nx_pat;
        st = seq_nx_pos[ch];
        seq_skip[ch] = (uint8_t)(st + 1u);
    }
    seq_set(pat, ch, st, vel > 110u ? STEP_ACC : STEP_ON);
    SEQ_REC_MICRO(pat, ch, st, seq_frac_micro(f));
}

static int32_t seq_nr_take(void)                /* main: the next note repeat hit, as seq_nr_ev; -1: none */
{
    int32_t e;
    if (seq_nr_r == seq_nr_w) return -1;
    e = seq_nr_ev[seq_nr_r % SEQ_NR_N];
    RING_PUBLISH();
    seq_nr_r++;
    return e;
}

static int32_t seq_out_take(void)               /* the next hit played, when it is heard */
{
    if (seq_ev_w - seq_ev_r > 32u) seq_ev_r = seq_ev_w - 32u;   /* (fell behind: the newest 32) */
    if (seq_ev_r == seq_ev_w || ((int32_t)(fm1_ticks() - seq_ev_t[seq_ev_r % 32u]) < 0 && seq_ev_w - seq_ev_r < 24u))
        return -1;
    return seq_ev[seq_ev_r++ % 32u];
}

static uint32_t seq_clocks_due(void)            /* the MIDI clocks to send now: each when its sound is heard */
{
    uint32_t n = 0;
    if (seq_co_w - seq_co_r > SEQ_NCO) seq_co_r = seq_co_w - SEQ_NCO;
    while (seq_co_r != seq_co_w && ((int32_t)(fm1_ticks() - seq_co_t[seq_co_r % SEQ_NCO]) >= 0 ||
                                    seq_co_w - seq_co_r >= 24u)) {
        seq_co_r++;
        n++;
    }
    return n;
}
