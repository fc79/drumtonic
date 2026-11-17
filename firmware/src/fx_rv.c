/* SPDX-License-Identifier: GPL-3.0-only */
/* The send reverb (fx.c includes it): a low cut on the send, PRE DLY, one of five types, then WIDTH.
 *
 *   CLASSIC  four combs at 12 bits (the early-rack grain) with damping in the loop, two allpasses a side (the right
 *            reads the combs with alternate signs): after 9W9's send reverb, its comb and allpass lengths Freeverb's
 *            (Jezar at Dreampoint, public domain). Until 2026-11 the only one: at SIZE 100 %, MOD 0, WIDTH 100 % and no
 *            PRE DLY it is that reverb bit for bit (a kit saved before plays as it did)
 *   PLATE    Jon Dattorro's plate (J. Dattorro, "Effect Design, Part 1: Reverberator and Other Filters", J. Audio Eng.
 *            Soc. 45(9), 1997): its four input diffusers, its two-sided tank, its coefficients and its output taps
 *            (Table 2), from 29761 Hz to 44.1 kHz; SIZE 100 % is three quarters of its tank (the memory). Clean: 14-bit
 *            lines. DECAY its decay, TONE its damping, MOD its excursion (16 samples at its rate at 100 %)
 *   HALL     eight lines in a feedback delay network (Jot and Chaigne) mixed by a Hadamard matrix, each line damped and
 *            set to fall 60 dB in DECAY's time (0.8 .. 12 s); Dattorro's input diffusers before it. At half the rate
 *            (22 kHz) and 12 bits: dark, grainy, long
 *   ROOM     the same network with short lines, at 44.1 kHz and 14 bits (0.15 .. 2.5 s)
 *   GATED    ROOM's network with shorter lines and a long fall, behind a gate the send opens: DECAY is how long it
 *            stays open (60 .. 700 ms), then it shuts in 8 ms (the 1980s gated snare)
 *
 * One memory for all five (rv_mem: only one plays); a change of TYPE fades the tail out in 30 ms, clears the memory and
 * starts the new one (rv_isr). SIZE and PRE DLY glide (a change stretches the space instead of clicking). The ISR (or
 * the second core: fx_c1_job) runs rv_render; rv_params (main, or the ISR as a pattern brings its kit) sets rv_p. */

#define RV_PRE_N 4416                           /* PRE DLY's line: 100 ms and a little (14 bits) */
#define RV_MEM 25600                            /* the types' memory, 16-bit words (51 KB) */
#define RV_FADE (1.0f / 1323.0f)                /* a change of type: 30 ms out */
#define RV_GLIDE 0.0005f                        /* SIZE: ~45 ms to its new value */
#define RV_PGLIDE 0.0008f                       /* PRE DLY: ~28 ms, as the delay's TIME */
#define RV_S12 2048.0f                          /* a 12-bit line (CLASSIC, HALL) */
#define RV_S14 8192.0f                          /* a 14-bit line (the rest: clean, 12 dB of headroom) */
static int16_t rv_pre[RV_PRE_N] POOL;          /* (the pool, as rv_mem: the RAM is the scarcer) */
static volatile uint8_t rv_ready;               /* fx_init has laid the lines out: the audio starts before it (at power-on,
                                                 * while the saves load), and until then the lines are not there. (The
                                                 * first unit build read through them unset and crashed at power-on,
                                                 * 2026-11-16) */
static int16_t rv_mem[RV_MEM] POOL __attribute__((aligned(4)));

static const int16_t RV_DIF[4] = {210, 159, 562, 410};          /* Dattorro's input diffusers (142 107 379 277) at 44.1 kHz */
static const int16_t RV_DIFH[4] = {105, 79, 281, 205};          /* ... at 22.05 kHz */
static const float RV_DIFG[4] = {0.75f, 0.75f, 0.625f, 0.625f}; /* (input diffusion 1, 2) */
static const int16_t RV_ROOM_L[8] = {443, 557, 677, 829, 977, 1103, 1277, 1453};    /* 10 .. 33 ms */
static const int16_t RV_GATE_L[8] = {337, 419, 521, 631, 743, 863, 991, 1117};      /* 8 .. 25 ms */
static const int16_t RV_HALL_L[8] = {1009, 1301, 1663, 2029, 2459, 2953, 3407, 3967};   /* at 22.05 kHz: 46 .. 180 ms */
static const float RV_GAIN[RV_NTYPE] = {1.0f, 0.6f, 0.53f, 0.57f, 0.5f};   /* each type about as loud as CLASSIC */
#define RV_PR (44100.0f / 29761.0f)             /* Dattorro's rate to ours */
#define RV_PS 0.75f                             /* PLATE: the tank at SIZE 100 % */
static const int16_t RV_PL_T[8] = {672, 4453, 1800, 3720, 908, 4217, 2656, 3163};   /* the tank: left allpass (moving),
                                                 * delay, allpass, delay; then the right's */
static const int16_t RV_PL_TAP[14] = {266, 2974, 1913, 1996, 1990, 187, 1066, 353, 3627, 1228, 2673, 2111, 335, 121};
static const uint8_t RV_PL_TAPL[14] = {5, 5, 6, 7, 1, 2, 3, 1, 1, 2, 3, 5, 6, 7};   /* which tank line each tap reads */
static const int8_t RV_PL_TAPS[14] = {1, 1, -1, 1, -1, -1, -1, 1, 1, -1, 1, -1, -1, -1};   /* (Table 2: 7 left, 7 right) */

static struct {                                 /* what the knobs mean for the reverb (main: rv_params) */
    int32_t type;
    float pre, size, mod, width;                /* PRE DLY in samples, SIZE 0.1 .. 1, MOD 0 .. 1, WIDTH 0 .. 1 */
    float t60, damp, decay, gate;               /* the networks' fall (s) and damping (a one-pole's share), PLATE's decay
                                                 * and damping, GATED's time (samples) */
} rv_p;
static struct {                                 /* (ISR) */
    int32_t cur, top, pw, pquiet;               /* the type playing, the memory it took, PRE DLY's write, its zeros */
    float fade, pcur, sz;                       /* the change's fade, PRE DLY and SIZE as they glide */
    int16_t *ln[8], *df[4];                     /* the lines (combs, tank, network) and the diffusers */
    float *ap[4];                               /* CLASSIC's allpasses (floats, as they were) */
    int32_t lr[8], lw[8], dr[4], dw[4], apos[4];   /* their lengths and where they write */
    float lp[8], g[8], dmp[2], bw;              /* dampings, the lines' gains (each block), PLATE's state */
    float hl0, hr0, hl1, hr1, hin;              /* HALL: its last two outputs (L, R), the input summed, which half */
    int32_t half;
    uint32_t ph[4];                             /* MOD's sines */
    float genv, ggain;                          /* GATED */
    int32_t gtime;
} rv;

/* ---- main's side */
static void rv_params(const uint8_t *x)         /* x: a kitx's effects */
{
    float dk = fx_pot(x[FX_RV_DECAY]), t = fx_pot(x[FX_RV_TONE]), fc, fs;
    int32_t type = (int32_t)fx_get(x, FX_RV_TYPE);  /* (rv_p.type last: the ISR may come in between) */
    rv_p.pre = (float)fx_get(x, FX_RV_PRE) * 44.1f;
    rv_p.size = (float)(fx_get(x, FX_RV_SIZE) + 10u) / 100.0f;
    rv_p.mod = (float)fx_get(x, FX_RV_MOD) / 100.0f;
    rv_p.width = (float)fx_get(x, FX_RV_WIDTH) / 100.0f;
    switch (type) {
    case RV_PLATE:
        rv_p.decay = 0.10f + 0.80f * dk;              /* (0.98 at the top built up for 20 s) */
        rv_p.damp = 0.9f * (1.0f - t) * (1.0f - t);
        break;
    case RV_HALL: rv_p.t60 = 0.8f * fx_exp2(dk * 3.907f); break;          /* 0.8 .. 12 s */
    case RV_ROOM: rv_p.t60 = 0.15f * fx_exp2(dk * 4.059f); break;         /* 0.15 .. 2.5 s */
    case RV_GATED:
        rv_p.t60 = 1.2f;
        rv_p.gate = 60.0f * fx_exp2(dk * 3.544f) * 44.1f;                 /* 60 .. 700 ms */
        break;
    default: break;
    }
    fs = type == RV_HALL ? 22050.0f : FX_SR;
    fc = (type == RV_HALL ? 1000.0f * fx_exp2(t * 3.2f) : 1500.0f * fx_exp2(t * 3.4f));   /* HALL 1 .. 9 kHz, else 1.5 .. 16 */
    if (type != RV_PLATE) rv_p.damp = 1.0f - fx_exp2(-6.2831853f * fc / fs * 1.442695f);
    rv_p.type = type;
}

/* ---- the ISR's side */
static float rv_rd(const int16_t *b, int32_t len, int32_t w, float d)   /* d samples behind w (1 .. len - 1), interpolated */
{
    float rp = (float)w - d, fr;
    int32_t i0, i1;
    if (rp < 0.0f) rp += (float)len;
    i0 = (int32_t)rp;
    fr = rp - (float)i0;
    if (i0 >= len) i0 -= len;
    if (fr == 0.0f) return (float)b[i0];
    i1 = i0 + 1 >= len ? 0 : i0 + 1;
    return (float)b[i0] + ((float)b[i1] - (float)b[i0]) * fr;
}
static int32_t rv_q(float v, float s)           /* a line's word, toward zero (a tail ends in 0s) */
{
    int32_t k = (int32_t)(v * s);
    return k > 32767 ? 32767 : k < -32767 ? -32767 : k;
}
static int16_t *rv_take(int32_t n)              /* n words of the memory (even: floats stay aligned) */
{
    int16_t *p = rv_mem + rv.top;
    rv.top += (n + 1) & ~1;
    return p;
}
static void rv_start(int32_t type)              /* the memory cleared, the type's lines laid out */
{
    int32_t i;
    const int16_t *L = type == RV_ROOM ? RV_ROOM_L : type == RV_GATED ? RV_GATE_L : RV_HALL_L;
    memset(rv_mem, 0, sizeof rv_mem);
    memset(rv.lw, 0, sizeof rv.lw); memset(rv.dw, 0, sizeof rv.dw); memset(rv.apos, 0, sizeof rv.apos);
    memset(rv.lp, 0, sizeof rv.lp); rv.dmp[0] = rv.dmp[1] = rv.bw = 0.0f;
    rv.hl0 = rv.hr0 = rv.hl1 = rv.hr1 = rv.hin = 0.0f; rv.half = 0;
    rv.genv = rv.ggain = 0.0f; rv.gtime = 0;
    rv.top = 0;
    rv.cur = type;
    switch (type) {
    case RV_CLASSIC:
        for (i = 0; i < 4; i++) { rv.lr[i] = FX_CL[i] + 18; rv.ln[i] = rv_take(rv.lr[i]); }   /* (+ MOD's 8 a side) */
        rv.ap[0] = (float *)(void *)rv_take(2 * FX_AL[0]); rv.ap[1] = (float *)(void *)rv_take(2 * FX_AL[1]);
        rv.ap[2] = (float *)(void *)rv_take(2 * FX_ALR[0]); rv.ap[3] = (float *)(void *)rv_take(2 * FX_ALR[1]);
        break;
    case RV_PLATE:
        for (i = 0; i < 4; i++) { rv.dr[i] = RV_DIF[i]; rv.df[i] = rv_take(rv.dr[i]); }
        for (i = 0; i < 8; i++) {
            rv.lr[i] = (int32_t)((float)RV_PL_T[i] * RV_PR * RV_PS) + ((i & 3) == 0 ? 28 : 3);   /* (the moving: 16 x RV_PR) */
            rv.ln[i] = rv_take(rv.lr[i]);
        }
        break;
    default: {                                  /* the networks */
        const int16_t *D = type == RV_HALL ? RV_DIFH : RV_DIF;
        for (i = 0; i < 4; i++) { rv.dr[i] = D[i]; rv.df[i] = rv_take(rv.dr[i]); }
        for (i = 0; i < 8; i++) { rv.lr[i] = L[i] + 12; rv.ln[i] = rv_take(rv.lr[i]); }   /* (+ MOD's 6 a side) */
        break;
    }
    }
}
static int32_t rv_used(int32_t type)            /* the memory a type takes (tests: it fits) */
{
    int32_t c = rv.cur, t = rv.top, n;
    rv_start(type);
    n = rv.top;
    rv_start(c);
    rv.top = t;
    return n;
}

/* one sample of the network (HALL, ROOM, GATED): in -> *yl, *yr; returns its writes or'ed (0: silent) */
static int32_t rv_fdn(float in, float s, float a, float sz, float md, float *yl, float *yr)
{
    float o[8], h[8], is = 1.0f / s, t;
    int32_t i, wr = 0, k;
    for (i = 0; i < 4; i++) {                   /* the diffusers */
        int16_t *b = rv.df[i];
        int32_t p = rv.dw[i];
        float bo = (float)b[p] * is, v = in - RV_DIFG[i] * bo;
        k = rv_q(v, s); b[p] = (int16_t)k; wr |= k;
        in = bo + RV_DIFG[i] * v;
        rv.dw[i] = p + 1 >= rv.dr[i] ? 0 : p + 1;
    }
    for (i = 0; i < 8; i++) {
        float d = (float)(rv.lr[i] - 12) * sz;
        if (md != 0.0f && (i == 1 || i == 3 || i == 4 || i == 6)) d += md * fx_psin(rv.ph[(i >> 1) & 3]);
        o[i] = rv_rd(rv.ln[i], rv.lr[i], rv.lw[i], d < 1.0f ? 1.0f : d) * is;
        rv.lp[i] += (o[i] - rv.lp[i]) * a;
        h[i] = rv.lp[i] * rv.g[i];
    }
    for (i = 0; i < 8; i += 2) { t = h[i]; h[i] = t + h[i + 1]; h[i + 1] = t - h[i + 1]; }   /* Hadamard: three butterflies */
    for (i = 0; i < 8; i += 4) {
        t = h[i]; h[i] = t + h[i + 2]; h[i + 2] = t - h[i + 2];
        t = h[i + 1]; h[i + 1] = t + h[i + 3]; h[i + 3] = t - h[i + 3];
    }
    for (i = 0; i < 4; i++) { t = h[i]; h[i] = t + h[i + 4]; h[i + 4] = t - h[i + 4]; }
    for (i = 0; i < 8; i++) {
        k = rv_q(h[i] * 0.35355339f + (i < 4 ? in : -in), s);   /* (in, L and R: three orthogonal sign patterns) */
        rv.ln[i][rv.lw[i]] = (int16_t)k; wr |= k;
        rv.lw[i] = rv.lw[i] + 1 >= rv.lr[i] ? 0 : rv.lw[i] + 1;
    }
    *yl = (o[0] - o[1] + o[2] - o[3] + o[4] - o[5] + o[6] - o[7]) * 0.3f;
    *yr = (o[0] + o[1] - o[2] - o[3] + o[4] + o[5] - o[6] - o[7]) * 0.3f;
    return wr;
}

/* one sample of PLATE */
static int32_t rv_plate(float in, float sz, float md, float *yl, float *yr)
{
    const float is = 1.0f / RV_S14, sc = RV_PR * RV_PS * sz, dec = rv_p.decay, dm = rv_p.damp;
    const float dd2 = dec + 0.15f < 0.25f ? 0.25f : dec + 0.15f > 0.5f ? 0.5f : dec + 0.15f;
    float endl, endr, acc, v, bo, d, u;
    int32_t i, wr = 0, k, side;
    rv.bw += (in - rv.bw) * 0.9995f;            /* bandwidth */
    in = rv.bw;
    for (i = 0; i < 4; i++) {
        int16_t *b = rv.df[i];
        int32_t p = rv.dw[i];
        bo = (float)b[p] * is; v = in - RV_DIFG[i] * bo;
        k = rv_q(v, RV_S14); b[p] = (int16_t)k; wr |= k;
        in = bo + RV_DIFG[i] * v;
        rv.dw[i] = p + 1 >= rv.dr[i] ? 0 : p + 1;
    }
    endl = rv_rd(rv.ln[3], rv.lr[3], rv.lw[3], (float)RV_PL_T[3] * sc) * is;
    endr = rv_rd(rv.ln[7], rv.lr[7], rv.lw[7], (float)RV_PL_T[7] * sc) * is;
    for (side = 0; side < 2; side++) {          /* the left takes the right's end, the right the left's */
        const int32_t o = side * 4;
        float a = in + dec * (side ? endl : endr);
        d = (float)RV_PL_T[o] * sc;
        if (md != 0.0f) d += md * fx_psin(rv.ph[side]);
        bo = rv_rd(rv.ln[o], rv.lr[o], rv.lw[o], d) * is;          /* allpass, decay diffusion 1 (-0.70), moving */
        v = a + 0.70f * bo;
        k = rv_q(v, RV_S14); rv.ln[o][rv.lw[o]] = (int16_t)k; wr |= k;
        u = bo - 0.70f * v;
        k = rv_q(u, RV_S14); rv.ln[o + 1][rv.lw[o + 1]] = (int16_t)k; wr |= k;   /* the delay */
        u = rv_rd(rv.ln[o + 1], rv.lr[o + 1], rv.lw[o + 1], (float)RV_PL_T[o + 1] * sc) * is;
        rv.dmp[side] = u + (rv.dmp[side] - u) * dm;                 /* damping */
        u = rv.dmp[side] * dec;
        bo = rv_rd(rv.ln[o + 2], rv.lr[o + 2], rv.lw[o + 2], (float)RV_PL_T[o + 2] * sc) * is;   /* allpass, decay diffusion 2 */
        v = u - dd2 * bo;
        k = rv_q(v, RV_S14); rv.ln[o + 2][rv.lw[o + 2]] = (int16_t)k; wr |= k;
        u = bo + dd2 * v;
        k = rv_q(u, RV_S14); rv.ln[o + 3][rv.lw[o + 3]] = (int16_t)k; wr |= k;   /* the delay to the other side */
    }
    for (side = 0; side < 2; side++) {          /* the taps (Table 2), as the tank's lines are now */
        acc = 0.0f;
        for (i = 7 * side; i < 7 * side + 7; i++) {
            int32_t l = RV_PL_TAPL[i];
            acc += (float)RV_PL_TAPS[i] * rv_rd(rv.ln[l], rv.lr[l], rv.lw[l], (float)RV_PL_TAP[i] * sc + 1.0f);
        }
        *(side ? yr : yl) = acc * is * 0.6f;
    }
    for (i = 0; i < 8; i++) rv.lw[i] = rv.lw[i] + 1 >= rv.lr[i] ? 0 : rv.lw[i] + 1;
    return wr;
}

static void rv_gains(float sz)                  /* each line's gain: down 60 dB in t60 */
{
    int32_t i;
    float fs = rv.cur == RV_HALL ? 22050.0f : FX_SR, t60 = rv_p.t60 > 0.05f ? rv_p.t60 : 0.05f;
    for (i = 0; i < 8; i++)
        rv.g[i] = fx_exp2(-9.965784f * (float)(rv.lr[i] - 12) * sz / (t60 * fs));
}

static void fx_reverb(const int32_t *bus, float *l, float *r, uint32_t n)
{
    const float damp = fx_p.rv_damp, fb = fx_p.rv_fb, lvl = fx_p.rv_lvl, hpa = fx_p.rv_hpa;
    const int32_t want = rv_p.type, pre_on = rv_p.pre > 0.0f || rv.pcur > 0.0f;
    const float szt = rv_p.size, md = rv_p.mod, wd = rv_p.width, pt = rv_p.pre;
    uint32_t i, any = 0;
    int32_t quiet, qlim;
    if (!rv_ready) return;
    quiet = fx.rv_quiet;
    qlim = rv.cur == RV_CLASSIC ? FX_RV_QUIET : 2 * rv.top + 64;
    if (want != rv.cur && (quiet >= qlim || rv.fade <= 0.0f)) { rv_start(want); rv.fade = 1.0f; quiet = fx.rv_quiet = 0; }
    if (quiet >= qlim && (!pre_on || rv.pquiet >= RV_PRE_N)) {
        for (i = 0; i < n; i++) any |= (uint32_t)bus[i];
        if (!any) { rv.pcur = pt; rv.sz = szt; return; }
    }
    if (rv.cur != RV_CLASSIC && rv.cur != RV_PLATE) rv_gains(rv.sz);
    for (i = 0; i < n; i++) {
        float in = (float)bus[i] * (1.0f / 32768.0f), x, y = 0.0f, yr = 0.0f;
        int32_t wr = 0, p, k;
        fx.rv_hy = hpa * (fx.rv_hy + in - fx.rv_hx);
        fx.rv_hx = in;
        x = fx.rv_hy;
        k = rv_q(x, RV_S14);                    /* PRE DLY: always written, read when on */
        rv_pre[rv.pw] = (int16_t)k;
        rv.pquiet = k == 0 ? rv.pquiet + 1 : 0;
        if (pre_on) {
            rv.pcur += (pt - rv.pcur) * RV_PGLIDE;
            if (fx_absf(pt - rv.pcur) < 0.001f) rv.pcur = pt;
            if (pt == 0.0f && rv.pcur < 0.5f) rv.pcur = 0.0f;
            if (rv.pcur > 0.0f) x = rv_rd(rv_pre, RV_PRE_N, rv.pw, rv.pcur < 1.0f ? 1.0f : rv.pcur) * (1.0f / RV_S14);
        }
        rv.pw = rv.pw + 1 >= RV_PRE_N ? 0 : rv.pw + 1;
        if (rv.sz != szt) {
            rv.sz += (szt - rv.sz) * RV_GLIDE;
            if (fx_absf(szt - rv.sz) < 1e-5f) rv.sz = szt;
        }
        if (md != 0.0f) { rv.ph[0] += 26781u; rv.ph[1] += 37112u; rv.ph[2] += 52143u; rv.ph[3] += 70119u; }   /* 0.27 .. 0.72 Hz */
        switch (rv.cur) {
        case RV_CLASSIC: {
            float acc = 0.0f, accr = 0.0f, bo, *a0 = rv.ap[0], *a1 = rv.ap[1], *b0 = rv.ap[2], *b1 = rv.ap[3];
            int32_t c;
            for (c = 0; c < 4; c++) {
                float o, d = (float)FX_CL[c] * rv.sz;
                if (md != 0.0f) d += md * 8.0f * fx_psin(rv.ph[c]);
                o = rv_rd(rv.ln[c], rv.lr[c], rv.lw[c], d) * (1.0f / 2048.0f);
                acc += o;
                accr += (c & 1) ? -o : o;
                fx.cdmp[c] = o + (fx.cdmp[c] - o) * damp;
                k = (int32_t)((x + fx.cdmp[c] * fb) * 2048.0f);   /* the loop at 12 bits, toward zero */
                k = k > 32767 ? 32767 : k < -32767 ? -32767 : k;
                rv.ln[c][rv.lw[c]] = (int16_t)k;
                wr |= k;
                rv.lw[c] = rv.lw[c] + 1 >= rv.lr[c] ? 0 : rv.lw[c] + 1;
            }
            y = acc * 0.25f;
            p = rv.apos[0]; bo = a0[p]; a0[p] = y + bo * 0.5f; y = bo - y * 0.5f; rv.apos[0] = p + 1 >= FX_AL[0] ? 0 : p + 1;
            p = rv.apos[1]; bo = a1[p]; a1[p] = y + bo * 0.5f; y = bo - y * 0.5f; rv.apos[1] = p + 1 >= FX_AL[1] ? 0 : p + 1;
            yr = accr * 0.25f;
            p = rv.apos[2]; bo = b0[p]; b0[p] = yr + bo * 0.5f; yr = bo - yr * 0.5f; rv.apos[2] = p + 1 >= FX_ALR[0] ? 0 : p + 1;
            p = rv.apos[3]; bo = b1[p]; b1[p] = yr + bo * 0.5f; yr = bo - yr * 0.5f; rv.apos[3] = p + 1 >= FX_ALR[1] ? 0 : p + 1;
            break;
        }
        case RV_PLATE:
            wr = rv_plate(x, rv.sz, md * 16.0f * RV_PR, &y, &yr);
            break;
        case RV_HALL:                           /* every other sample, on the two summed; between, halfway */
            rv.hin += x;
            if (rv.half) {
                rv.hl0 = rv.hl1; rv.hr0 = rv.hr1;
                wr = rv_fdn(rv.hin * 0.5f, RV_S12, rv_p.damp, rv.sz, md * 6.0f, &rv.hl1, &rv.hr1);
                rv.hin = 0.0f;
                y = rv.hl1; yr = rv.hr1;
            } else {
                y = (rv.hl0 + rv.hl1) * 0.5f; yr = (rv.hr0 + rv.hr1) * 0.5f;
            }
            rv.half ^= 1;
            break;
        default:                                /* ROOM, GATED */
            wr = rv_fdn(x, RV_S14, rv_p.damp, rv.sz, md * 6.0f, &y, &yr);
            if (rv.cur == RV_GATED) {           /* the send opens the gate for DECAY's time, then it shuts in 8 ms */
                float m = fx_absf(x);
                if (m > 0.004f && m > rv.genv * 1.5f) rv.gtime = (int32_t)rv_p.gate;
                rv.genv = m > rv.genv ? m : rv.genv < 1e-6f ? 0.0f : rv.genv * 0.9995f;
                if (rv.gtime > 0) { rv.gtime--; rv.ggain += (1.0f - rv.ggain) * 0.05f; }
                else rv.ggain = rv.ggain < 1e-6f ? 0.0f : rv.ggain * 0.9972f;   /* (shut: 0, no denormals) */
                y *= rv.ggain; yr *= rv.ggain;
            }
            break;
        }
        if (rv.cur != RV_CLASSIC) { y *= RV_GAIN[rv.cur]; yr *= RV_GAIN[rv.cur]; }
        if (wd != 1.0f) { float m = (y + yr) * 0.5f, s = (y - yr) * 0.5f * wd; y = m + s; yr = m - s; }
        if (want != rv.cur || rv.fade < 1.0f) {   /* a change of type: out, then (next block) the new one */
            rv.fade += want != rv.cur ? -RV_FADE : RV_FADE;
            rv.fade = rv.fade < 0.0f ? 0.0f : rv.fade > 1.0f ? 1.0f : rv.fade;
            y *= rv.fade; yr *= rv.fade;
        }
        l[i] += y * lvl;
        r[i] += yr * lvl;
        quiet = (wr == 0 && bus[i] == 0) ? quiet + 1 : 0;
    }
    fx.rv_quiet = quiet;
    for (i = 0; i < 4u; i++) if (fx_absf(fx.cdmp[i]) < 1e-20f) fx.cdmp[i] = 0.0f;
    for (i = 0; i < 8u; i++) if (fx_absf(rv.lp[i]) < 1e-20f) rv.lp[i] = 0.0f;
    if (fx_absf(fx.rv_hy) < 1e-20f) fx.rv_hy = 0.0f;
    if (fx_absf(rv.dmp[0]) < 1e-20f) rv.dmp[0] = 0.0f;
    if (fx_absf(rv.dmp[1]) < 1e-20f) rv.dmp[1] = 0.0f;
    if (fx_absf(rv.bw) < 1e-20f) rv.bw = 0.0f;
}
