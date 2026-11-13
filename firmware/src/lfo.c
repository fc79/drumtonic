/* SPDX-License-Identifier: GPL-3.0-only */
/* The FM-1's LFOs (motion.h): 8 a pattern, the playing pattern's run. Each moves one parameter of one channel up and
 * down around the value it has (the knob's, its motion or its p-lock), while the pattern plays: in time (1/32 to 16
 * bars, following the tempo or the MIDI clock, from the start at every PLAY) or free (0.05 to 20 Hz); TRI, SAW, SQR,
 * S&H; DEPTH -100..+100 % (100 % = +-64). */

static lfo_t lfos[SEQ_NPAT][NLFO];
static const char *const LFO_SYNC_N[LFO_NRATE] = {"1/32", "1/16", "1/8", "1/4", "1/2", "1 BAR", "2 BAR", "4 BAR", "8 BAR", "16 BAR"};
static const char *const LFO_HZ_N[LFO_NRATE] = {"0.05HZ", "0.1HZ", "0.2HZ", "0.5HZ", "1HZ", "2HZ", "4HZ", "8HZ", "12HZ", "20HZ"};
static const char *const LFO_SHAPE_N[LFO_NSHAPE] = {"TRI", "SAW", "SQR", "S&H"};
static const uint16_t LFO_HZ_STEP[LFO_NRATE] = {19, 38, 76, 190, 380, 761, 1522, 3044, 4565, 7609};   /* Q16 cycles a
                                                 * render: Hz * 256 / 44100 * 65536 */
static struct { uint32_t ph, last; int32_t sh; uint8_t on, ch, param; } lfo_rt[NLFO];   /* (ISR) */
static uint32_t lfo_rng = 0x6C8E9CF5u;

/* ---- the main loop's side */
static int32_t lfo_find(uint32_t pat, uint32_t ch, uint32_t prm)
{
    uint32_t k;
    for (k = 0; k < NLFO; k++)
        if (lfos[pat][k].used && lfos[pat][k].ch == ch && lfos[pat][k].param == prm) return (int32_t)k;
    return -1;
}
static int lfo_ok(uint32_t prm)                 /* continuous parameters only (not the switches) */
{
    return prm < P_COND && prm != P_WAVE && prm != P_PMODE && prm != P_NFILT && prm != P_NENV && prm != P_NSTEREO &&
           prm != P_CHOKE;
}
static int32_t lfo_assign(uint32_t pat, uint32_t ch, uint32_t prm)   /* the first free one; -1 full, -2 not this one */
{
    int32_t k = lfo_find(pat, ch, prm);
    uint32_t i;
    if (!lfo_ok(prm)) return -2;
    if (k >= 0) return k;
    for (i = 0; i < NLFO; i++)
        if (!lfos[pat][i].used) {
            lfo_t *l = &lfos[pat][i];
            l->ch = (uint8_t)ch; l->param = (uint8_t)prm;
            l->rate = 5; l->shape = LFO_TRI; l->depth = 30; l->sync = 1;   /* 1 BAR, TRI, +30 %, in time */
            __asm__ volatile("" ::: "memory");
            l->used = 1;
            return (int32_t)i;
        }
    return -1;
}
static void lfo_clear(uint32_t pat, uint32_t k) { lfos[pat][k % NLFO].used = 0; }
static void lfo_clear_ch(uint32_t pat, uint32_t ch)   /* ch DRUM_NCH: all */
{
    uint32_t k;
    for (k = 0; k < NLFO; k++)
        if (ch >= DRUM_NCH || lfos[pat][k].ch == ch) lfos[pat][k].used = 0;
}

/* ---- the ISR's side */
static void lfo_off(uint32_t k)
{
    if (!lfo_rt[k].on) return;
    lfo_rt[k].on = 0;
    if (mot_lfo_off[lfo_rt[k].ch][lfo_rt[k].param]) {
        mot_lfo_off[lfo_rt[k].ch][lfo_rt[k].param] = 0;
        mot_dirty |= (uint16_t)(1u << lfo_rt[k].ch);
    }
}
static void lfo_tick(void)                      /* every render, from mot_tick */
{
    uint32_t k, beats = 0;
    if (seq.run) {                              /* the beats since PLAY, Q16 (an eighth's phase: seq_bp) */
        uint32_t e = seq.eighths;
        beats = (uint32_t)(((uint64_t)e << 15) + (seq.ext ? (uint32_t)seq_clkph * 2731u : seq_bp >> 17));
    }
    for (k = 0; k < NLFO; k++) {
        const lfo_t *l = &lfos[seq.pat][k];
        uint32_t p, wrap;
        int32_t w, off;
        if (!seq.run || !l->used || l->ch >= DRUM_NCH || l->param >= P_COND) { lfo_off(k); continue; }
        if (lfo_rt[k].on && (lfo_rt[k].ch != l->ch || lfo_rt[k].param != l->param)) lfo_off(k);
        if (!lfo_rt[k].on) { lfo_rt[k].on = 1; lfo_rt[k].ch = l->ch; lfo_rt[k].param = l->param; lfo_rt[k].ph = 0; }
        if (l->sync) p = (uint32_t)(((uint64_t)beats * 8u) >> (l->rate % LFO_NRATE));
        else p = lfo_rt[k].ph += LFO_HZ_STEP[l->rate % LFO_NRATE];
        wrap = (p ^ lfo_rt[k].last) >> 16;      /* a new cycle: S&H takes a new value */
        lfo_rt[k].last = p;
        p &= 0xFFFFu;
        if (wrap || !lfo_rt[k].sh) { lfo_rng ^= lfo_rng << 13; lfo_rng ^= lfo_rng >> 17; lfo_rng ^= lfo_rng << 5; lfo_rt[k].sh = (int32_t)(lfo_rng >> 16) - 32768 + 1; }
        switch (l->shape) {
        case LFO_SAW: w = (int32_t)p - 32768; break;
        case LFO_SQR: w = p < 32768u ? 32767 : -32768; break;
        case LFO_SH: w = lfo_rt[k].sh; break;
        default: w = p < 32768u ? -32768 + 2 * (int32_t)p : 32767 - 2 * ((int32_t)p - 32768); break;
        }
        off = w * l->depth / 100 * 64 / 32768;
        if (mot_lfo_off[l->ch][l->param] != off) {
            mot_lfo_off[l->ch][l->param] = (int8_t)off;
            mot_dirty |= (uint16_t)(1u << l->ch);
        }
    }
}
