/* SPDX-License-Identifier: GPL-3.0-only */
/* The FM-1's effects and master (fx_param.h), after drum_render, on the whole mix (app_audio):
 *
 *   out = limit( filter( comp( drive( dry + reverb(rev bus) + delay(dly bus) ))))
 *
 * The algorithms are after 9W9's send reverb, delay and master (GPL-3.0), written again here without
 * their helper libraries:
 *   reverb   four combs at 12 bits (the early-rack grain) with damping in the loop, two allpasses a side (the right
 *            reads the combs with alternate signs); a low cut on the send
 *   delay    a note value of the tempo, slewed (a change warps instead of clicking), feedback through a darkening
 *            one-pole, 12-bit line; TAPE above 0 adds wow and flutter, saturation in the loop and a darker loop, more
 *            as it turns; PING: the left at the time, the right at twice it
 *   master   DRIVE (a soft clip), FILTER (one knob: low-pass under the middle, high-pass over it; 24 dB an octave, two
 *            state-variable stages, until 2026-11-12 one: 12 dB) with RESO, then the limiter
 *            (always on, no knob: a look-ahead peak limiter, transparent: 1.45 ms ahead, the gain ramps down so
 *            the peak meets the ceiling, -0.2 dBFS, never above it, then comes back in ~120 ms; idle below it);
 *            COMP, its own page: THRESH, RATIO (1:1 off), ATTACK, RELEASE, a 6 dB soft knee, the detector high-passed
 *            at 90 Hz (the kick does not run it alone), the make-up fixed by THRESH and RATIO (a slow automatic one
 *            undid RELEASE, and raised the tails); MIX (dry / wet,
 *            parallel compression) on ALGORITHM; fx_gr feeds the page's gain-reduction meter. PUMP, MAKEUP, MIX and the one-knob COMP are gone (their
 *            values in a saved kit are ignored)
 * Both effects go idle when their bus and their lines are silent. Floats; the main loop sets fx_p (fx_apply), the
 * ISR reads it. */

#define FX_SR 44100.0f
#define FX_DLEN 65536u                          /* the delay line: 1.49 s */
static int16_t fx_dline[FX_DLEN] POOL;
#define FX_LA 64                                /* the limiter's look-ahead: 1.45 ms */
#define FX_CEIL 0.98f                           /* its ceiling (-0.18 dBFS) */
static volatile float fx_gr;                   /* ISR -> main: the most gain reduction since the screen looked, dB (<= 0) */
static const int32_t FX_CL[4] = {1116, 1188, 1277, 1356}, FX_AL[2] = {556, 441}, FX_ALR[2] = {579, 457};
#define FX_RV_QUIET (1356 + 8192)
static const float FX_BEATS[13] = {0.125f, 1.0f / 6.0f, 0.25f, 1.0f / 3.0f, 0.375f, 0.5f, 2.0f / 3.0f, 0.75f, 1.0f,
                                   4.0f / 3.0f, 1.5f, 2.0f, 3.0f};
static const float FX_C_HP[4] = {0.0f, 0.008512f, 0.01274f, 0.021144f};   /* the compressor's detector: off, or a 60 / 90 / 150 Hz one-pole high-pass */
#define FX_MK 0.6f                              /* the make-up: this share of the loss at full scale */
static const float FX_RATIO[10] = {1.0f, 1.5f, 2.0f, 3.0f, 4.0f, 6.0f, 8.0f, 12.0f, 20.0f, 100.0f};

static struct {                                 /* what the knobs mean (main: fx_apply) */
    float rv_fb, rv_damp, rv_lvl, rv_hpa, dl_ms, dl_fb, dl_tc, dl_lvl, dl_hpa, wear;
    int32_t tape, ping;
    float drive, c_thr, c_slope, c_atk, c_rel, c_mix, c_fall, c_mkt, c_hpa;
    int32_t c_on, f_mode;                       /* filter: 0 off, 1 LP, 2 HP */
    float f_g, f_k;
} fx_p;
static struct {                                 /* (ISR) */
    int16_t comb[1116 + 1188 + 1277 + 1356];
    float ap[556 + 441], apr[579 + 457], cdmp[4];
    int32_t cpos[4], apos[2], aposr[2], rv_quiet;
    float rv_hx, rv_hy;                         /* the sends' low cuts */
    int32_t w, dl_quiet;
    float dcur, lp, tape_hp, dl_hx, dl_hy;
    uint32_t wow, flut;
    float c_env, c_det, c_mk, c_hp[2];          /* c_mk: the make-up (x); c_hp: the detector's high-pass */
    float la[2][FX_LA], lq[FX_LA], lb[FX_LA];   /* the limiter: the audio delayed, the gains wanted, the ramp's box */
    float lsum, lgr;                            /* (lsum: lb's sum; lgr: the gain after the release) */
    int32_t li, lhold;                          /* (where in the rings; samples since a gain under 1 was wanted) */
    float s1[2], s2[2], s3[2], s4[2];           /* the filter's state, its two stages, L and R */
} fx POOL;

/* ---- small maths */
static float fx_absf(float x) { return x < 0.0f ? -x : x; }
static float fx_tanh(float x)                   /* a rational tanh, exact enough for a clip */
{
    float x2;
    if (x > 3.0f) return 1.0f;
    if (x < -3.0f) return -1.0f;
    x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}
static float fx_exp2(float x)                   /* 2^x, x in about -60..60 */
{
    union { float f; int32_t i; } u;
    float fl = (float)(int32_t)x, f;
    if (x < fl) fl -= 1.0f;
    f = x - fl;
    u.f = 1.0f + f * (0.6951786f + f * (0.2261792f + f * 0.0780463f));
    u.i += (int32_t)fl * 8388608;              /* the exponent: fl << 23 (a shift of a negative is undefined) */
    return u.f;
}
static float fx_tc(float ms)                    /* a one-pole's coefficient, e^(-1 / (ms * SR)), ms >= 0.1: a series. (Not fx_exp2:
                                                 * just under 0 it gives 0.9997, which caps any time at about 75 ms) */
{
    float y = 1.0f / (ms * 0.001f * FX_SR);
    return 1.0f - y * (1.0f - y * 0.5f * (1.0f - y * (1.0f / 3.0f) * (1.0f - y * 0.25f)));
}
static float fx_log2(float x)                   /* x > 0: the exponent, then 2 atanh((m - 1) / (m + 1)) / ln 2 */
{
    union { float f; int32_t i; } u = {x};
    float e = (float)((u.i >> 23 & 255) - 127), s, s2;
    u.i = (u.i & 0x007FFFFF) | 0x3F800000;
    s = (u.f - 1.0f) / (u.f + 1.0f);
    s2 = s * s;
    return e + 2.8853901f * s * (1.0f + s2 * (0.33333333f + s2 * (0.2f + s2 * 0.142857f)));
}
static float fx_sqrtf(float x)                  /* x > 0: from the exponent, then three Newton steps */
{
    union { float f; int32_t i; } u = {x};
    float y;
    u.i = (u.i >> 1) + 0x1FC00000;
    y = u.f;
    y = 0.5f * (y + x / y); y = 0.5f * (y + x / y); y = 0.5f * (y + x / y);
    return y;
}
static float fx_db2lin(float db) { return fx_exp2(db * 0.16609640f); }
static float fx_pot(uint32_t v) { return (float)v * (1.0f / 127.0f); }
static float fx_hpa(uint32_t v)                 /* a one-pole high-pass's coefficient: 20 .. 1000 Hz */
{
    float fc = 20.0f * fx_exp2(fx_pot(v) * 5.644f), rc = 1.0f / (6.2831853f * fc);
    return rc / (rc + 1.0f / FX_SR);
}

/* ---- the main loop's side */
static void fx_apply_x(const uint8_t *kx)       /* from a kit's kitx (the ISR: a pattern's next kit, app_fm1.c) */
{
    const uint8_t *x = kx + X_FX;
    float ms, a;
    uint32_t f = x[FX_M_FILTER];
    fx_p.rv_fb = 0.2f + 0.73f * fx_pot(x[FX_RV_DECAY]);
    fx_p.rv_damp = 0.75f - fx_pot(x[FX_RV_TONE]) * 0.55f;
    fx_p.rv_lvl = 1.0f;                         /* (no RETURN knob: unity gain; the sends set how much) */
    fx_p.rv_hpa = fx_hpa(x[FX_RV_HPF]);
    ms = FX_BEATS[x[FX_DL_TIME] % 13u] * 60000.0f / (float)(set.bpm ? set.bpm : 120u);
    while (ms > (float)(FX_DLEN - 256u) / FX_SR * 1000.0f) ms *= 0.5f;   /* longer than the line: half of it */
    fx_p.dl_ms = ms;
    fx_p.tape = x[FX_DL_TAPE] > 0u;
    fx_p.wear = fx_pot(x[FX_DL_TAPE]);
    fx_p.dl_fb = 0.85f * fx_pot(x[FX_DL_FDBK]) * (fx_p.tape ? 1.15f / 0.85f : 1.0f);
    fx_p.dl_tc = (0.06f + fx_pot(x[FX_DL_TONE]) * 0.6f) * (fx_p.tape ? 0.6f - 0.3f * fx_p.wear : 1.0f);
    fx_p.dl_lvl = 1.0f;                         /* (no RETURN knob: unity gain; the sends set how much) */
    fx_p.dl_hpa = fx_hpa(x[FX_DL_HPF]);
    fx_p.ping = x[FX_DL_PING] != 0u;
    fx_p.drive = x[FX_M_DRIVE] ? 1.0f + 15.0f * fx_pot(x[FX_M_DRIVE]) : 0.0f;
    fx_p.c_on = x[FX_M_RATIO] > 0u;
    fx_p.c_thr = -48.0f + 48.0f * fx_pot(x[FX_M_THRESH]);
    fx_p.c_slope = 1.0f - 1.0f / FX_RATIO[x[FX_M_RATIO] % 10u];
    fx_p.c_hpa = FX_C_HP[x[FX_M_SCHP] < 4u ? x[FX_M_SCHP] : 0u];   /* (PRESETS on the COMP page) */
    fx_p.c_mix = fx_pot(x[FX_M_MIX]);          /* (dry / wet: ALGORITHM on the COMP page; no MAKEUP knob) */
    a = 0.1f * fx_exp2(fx_pot(x[FX_M_ATTACK]) * 9.966f);            /* 0.1 .. 100 ms */
    fx_p.c_atk = fx_tc(a);
    a = 10.0f * fx_exp2(fx_pot(x[FX_M_RELEASE]) * 7.229f);          /* 10 .. 1500 ms */
    fx_p.c_rel = fx_tc(a);
    a = a < 15.0f ? a : 15.0f;                  /* the detector's fall: never slower than RELEASE, or it would hide it */
    fx_p.c_fall = fx_tc(a);
    fx_p.c_mkt = fx_db2lin(-fx_p.c_thr * fx_p.c_slope * FX_MK);   /* the make-up: a share of what a full-scale peak loses */
    if (f == 64u) fx_p.f_mode = 0;
    else {                                      /* the cutoff: down from 20 kHz, or up from 20 Hz, 10 octaves */
        float fc = f < 64u ? 20000.0f * fx_exp2(-(float)(64u - f) * (10.0f / 64.0f)) : 20.0f * fx_exp2((float)(f - 64u) * (10.0f / 63.0f));
        float w = 3.14159265f * fc / FX_SR;
        fx_p.f_g = w < 1.4f ? w * (1.0f + w * w * (1.0f / 3.0f)) : 1.4f * 1.65f;   /* ~tan(w) */
        fx_p.f_k = 0.7654f - 0.6854f * fx_pot(x[FX_M_RESO]);   /* (the second stage: Q 1.31, a Butterworth, .. 12.5) */
        fx_p.f_mode = f < 64u ? 1 : 2;
    }
}
static void fx_apply(void) { fx_apply_x(kitx); }
static void fx_comp_knob(uint32_t v)            /* COMP turned: THRESH, RATIO, MAKEUP with it (3/4: 4:1 from -24 dB, +16) */
{
    uint8_t *x = kitx + X_FX;
    uint32_t t = v * 24u / 96u;
    x[FX_M_THRESH] = (uint8_t)(127u - (t > 48u ? 48u : t) * 127u / 48u);
    x[FX_M_RATIO] = (uint8_t)(v ? 1u + v * 3u / 96u : 0u);
    if (x[FX_M_RATIO] > 9u) x[FX_M_RATIO] = 9;
    t = v * 16u / 96u;
    x[FX_M_MAKEUP] = (uint8_t)((t > 24u ? 24u : t) * 127u / 24u);
}
static void fx_init(void)
{
    int32_t i;
    memset(&fx, 0, sizeof fx);
    fx.c_mk = 1.0f;
    memset(fx_dline, 0, sizeof fx_dline);
    fx.rv_quiet = FX_RV_QUIET;
    fx.dl_quiet = (int32_t)FX_DLEN;
    for (i = 0; i < FX_LA; i++) fx.lq[i] = fx.lb[i] = 1.0f;
    fx.lsum = (float)FX_LA;
    fx.lgr = 1.0f;
    fx_apply();
    fx.dcur = fx_p.dl_ms * 0.001f * FX_SR;
}

/* ---- the ISR's side */
static void fx_reverb(const int32_t *bus, float *l, float *r, uint32_t n)
{
    int16_t *cb[4];
    float *a0 = fx.ap, *a1 = fx.ap + FX_AL[0], *b0 = fx.apr, *b1 = fx.apr + FX_ALR[0];
    const float damp = fx_p.rv_damp, fb = fx_p.rv_fb, lvl = fx_p.rv_lvl, hpa = fx_p.rv_hpa;
    uint32_t i, any = 0;
    int32_t quiet = fx.rv_quiet;
    if (quiet >= FX_RV_QUIET) {
        for (i = 0; i < n; i++) any |= (uint32_t)bus[i];
        if (!any) return;
    }
    cb[0] = fx.comb; cb[1] = cb[0] + FX_CL[0]; cb[2] = cb[1] + FX_CL[1]; cb[3] = cb[2] + FX_CL[2];
    for (i = 0; i < n; i++) {
        float in = (float)bus[i] * (1.0f / 32768.0f), x, acc = 0.0f, accr = 0.0f, y, yr, bo;
        int32_t c, wr = 0, p;
        fx.rv_hy = hpa * (fx.rv_hy + in - fx.rv_hx);
        fx.rv_hx = in;
        x = fx.rv_hy;
        for (c = 0; c < 4; c++) {
            int16_t *b = cb[c];
            int32_t k;
            float o;
            p = fx.cpos[c];
            o = (float)b[p] * (1.0f / 2048.0f);
            acc += o;
            accr += (c & 1) ? -o : o;
            fx.cdmp[c] = o + (fx.cdmp[c] - o) * damp;
            k = (int32_t)((x + fx.cdmp[c] * fb) * 2048.0f);   /* the loop at 12 bits, toward zero */
            k = k > 32767 ? 32767 : k < -32767 ? -32767 : k;
            b[p] = (int16_t)k;
            wr |= k;
            fx.cpos[c] = p + 1 >= FX_CL[c] ? 0 : p + 1;
        }
        y = acc * 0.25f;
        p = fx.apos[0]; bo = a0[p]; a0[p] = y + bo * 0.5f; y = bo - y * 0.5f; fx.apos[0] = p + 1 >= FX_AL[0] ? 0 : p + 1;
        p = fx.apos[1]; bo = a1[p]; a1[p] = y + bo * 0.5f; y = bo - y * 0.5f; fx.apos[1] = p + 1 >= FX_AL[1] ? 0 : p + 1;
        yr = accr * 0.25f;
        p = fx.aposr[0]; bo = b0[p]; b0[p] = yr + bo * 0.5f; yr = bo - yr * 0.5f; fx.aposr[0] = p + 1 >= FX_ALR[0] ? 0 : p + 1;
        p = fx.aposr[1]; bo = b1[p]; b1[p] = yr + bo * 0.5f; yr = bo - yr * 0.5f; fx.aposr[1] = p + 1 >= FX_ALR[1] ? 0 : p + 1;
        l[i] += y * lvl;
        r[i] += yr * lvl;
        quiet = (wr == 0 && bus[i] == 0) ? quiet + 1 : 0;
    }
    fx.rv_quiet = quiet;
    for (i = 0; i < 4u; i++) if (fx_absf(fx.cdmp[i]) < 1e-20f) fx.cdmp[i] = 0.0f;
    if (fx_absf(fx.rv_hy) < 1e-20f) fx.rv_hy = 0.0f;
}

static float fx_tap(int32_t w, float d)         /* the line d samples behind w, interpolated */
{
    float rp = (float)w - d, fr;
    int32_t i0, i1;
    while (rp < 0.0f) rp += (float)FX_DLEN;
    i0 = (int32_t)rp;
    fr = rp - (float)i0;
    i0 &= (int32_t)(FX_DLEN - 1u);
    i1 = (i0 + 1) & (int32_t)(FX_DLEN - 1u);
    return ((float)fx_dline[i0] + ((float)fx_dline[i1] - (float)fx_dline[i0]) * fr) * (1.0f / 2048.0f);
}
static float fx_psin(uint32_t ph) { float x = (float)(int32_t)ph * (1.0f / 2147483648.0f); return 4.0f * x * (1.0f - fx_absf(x)); }
static void fx_delay(const int32_t *bus, float *l, float *r, uint32_t n)
{
    const float target = fx_p.dl_ms * 0.001f * FX_SR, fb = fx_p.dl_fb, tc = fx_p.dl_tc, lvl = fx_p.dl_lvl, hpa = fx_p.dl_hpa;
    const float wear = fx_p.wear, depth = 0.1f + 1.9f * wear, wow_a = 66.15f * depth, flut_a = 6.615f * depth;
    const float g = 1.0f + 1.5f * wear, ig = 1.0f / g;
    const int32_t tape = fx_p.tape;
    uint32_t i, any = 0;
    int32_t w = fx.w, quiet = fx.dl_quiet, ping;
    float dcur = fx.dcur, lp = fx.lp, hp = fx.tape_hp;
    if (quiet >= (int32_t)FX_DLEN) {
        for (i = 0; i < n; i++) any |= (uint32_t)bus[i];
        if (!any) {                             /* a silent line: time moves on, nothing to read */
            fx.w = (int32_t)(((uint32_t)w + n) & (FX_DLEN - 1u));
            fx.dcur = target;
            return;
        }
    }
    ping = fx_p.ping && 2.0f * (dcur > target ? dcur : target) + 4.0f < (float)FX_DLEN;
    for (i = 0; i < n; i++) {
        float in = (float)bus[i] * (1.0f / 32768.0f), x, d, y, y2, s;
        int32_t k;
        fx.dl_hy = hpa * (fx.dl_hy + in - fx.dl_hx);
        fx.dl_hx = in;
        x = fx.dl_hy;
        dcur += (target - dcur) * 0.0008f;
        d = dcur;
        if (tape) { fx.wow += 48695u; fx.flut += 594075u; d += wow_a * fx_psin(fx.wow) + flut_a * fx_psin(fx.flut); }   /* 0.5 / 6.1 Hz */
        y = fx_tap(w, d);
        y2 = ping ? fx_tap(w, 2.0f * d) : y;
        lp += (y2 * fb - lp) * tc;
        if (fx_absf(lp) < 1e-20f) lp = 0.0f;
        s = lp;
        if (tape) { s = fx_tanh(lp * g) * ig; hp += (s - hp) * 0.0042725f; s -= hp; }   /* (30 Hz: no DC past unity) */
        k = (int32_t)((x + s) * 2048.0f);
        k = k > 32767 ? 32767 : k < -32767 ? -32767 : k;
        fx_dline[w] = (int16_t)k;
        w = (w + 1) & (int32_t)(FX_DLEN - 1u);
        l[i] += y * lvl;
        r[i] += y2 * lvl;
        quiet = (k == 0 && bus[i] == 0) ? quiet + 1 : 0;
    }
    fx.w = w; fx.dcur = dcur; fx.lp = lp; fx.tape_hp = hp; fx.dl_quiet = quiet;
    if (fx_absf(fx.dl_hy) < 1e-20f) fx.dl_hy = 0.0f;
}

static float fx_l[256], fx_r[256];
#ifdef DRUM_CPU2
static void fx_c1_job(uint32_t n) { fx_reverb(drum_bus[0], fx_l, fx_r, n); }   /* (the second core) */
#endif

static void fx_render(int32_t *out, uint32_t n)
{
    float *l = fx_l, *r = fx_r;
    uint32_t i;
    if (n > 256u) n = 256u;
    for (i = 0; i < n; i++) { l[i] = (float)out[2u * i] * (1.0f / 32768.0f); r[i] = (float)out[2u * i + 1u] * (1.0f / 32768.0f); }
#ifdef DRUM_CPU2
    if (cpu2_run(fx_c1_job, n)) {               /* the reverb there, the delay here into its own lines, added after:
                                                 * each adds one term a sample, so the sum is the same bit for bit */
        static float dl[256], dr[256];
        for (i = 0; i < n; i++) dl[i] = dr[i] = 0.0f;
        fx_delay(drum_bus[1], dl, dr, n);
        if (cpu2_wait() != 0) fx_reverb(drum_bus[0], l, r, n);   /* (it failed: here) */
        for (i = 0; i < n; i++) { l[i] += dl[i]; r[i] += dr[i]; }
    } else {
        fx_reverb(drum_bus[0], l, r, n);
        fx_delay(drum_bus[1], l, r, n);
    }
#else
    fx_reverb(drum_bus[0], l, r, n);
    fx_delay(drum_bus[1], l, r, n);
#endif
    if (fx_p.drive > 0.0f)
        for (i = 0; i < n; i++) { l[i] = fx_tanh(l[i] * fx_p.drive) * 0.7f; r[i] = fx_tanh(r[i] * fx_p.drive) * 0.7f; }
    if (fx_p.c_on) {                            /* the compressor: a peak detector (high-passed if SC HPF is on), its fall at most
                                                 * RELEASE's, 6 dB knee, the make-up as THRESH and RATIO set it */
        float det = fx.c_det, env = fx.c_env, gmin = 0.0f, hl = fx.c_hp[0], hr = fx.c_hp[1], hpa = fx_p.c_hpa;
        if (hpa == 0.0f) hl = hr = 0.0f;        /* (off: nothing held over from when it was on) */
        fx.c_mk += (fx_p.c_mkt - fx.c_mk) * 0.1f;   /* (a knob turned: no step) */
        for (i = 0; i < n; i++) {
            float dl = l[i] - hl, dr = r[i] - hr, m, over, gdb, gain;
            hl += dl * hpa; hr += dr * hpa;
            m = fx_absf(dl) > fx_absf(dr) ? fx_absf(dl) : fx_absf(dr);
            det = m > det ? m : det * fx_p.c_fall;
            over = (det > 1e-9f ? 6.0206f * fx_log2(det) : -120.0f) - fx_p.c_thr;
            gdb = over >= 3.0f ? -over * fx_p.c_slope : over > -3.0f ? -(over + 3.0f) * (over + 3.0f) * (1.0f / 12.0f) * fx_p.c_slope : 0.0f;
            env = gdb + (env - gdb) * (gdb < env ? fx_p.c_atk : fx_p.c_rel);
            gain = fx_db2lin(env);
            gain = 1.0f + (gain * fx.c_mk - 1.0f) * fx_p.c_mix;   /* MIX: the dry and the compressed (level-matched) */
            if (env < gmin) gmin = env;
            l[i] *= gain;
            r[i] *= gain;
        }
        fx.c_det = det; fx.c_env = env; fx.c_hp[0] = hl; fx.c_hp[1] = hr;
        if (gmin < fx_gr) fx_gr = gmin;         /* (the screen takes it and sets 0) */
    } else {
        fx.c_mk = 1.0f;                         /* (on again: from no make-up) */
    }
    if (fx_p.f_mode) {                          /* two state-variable filters (TPT) in a row, both sides: 24 dB an octave;
                                                 * the first a fixed Q 0.54, the second RESO's (at 0 a Butterworth) */
        const float g = fx_p.f_g, k1 = 1.8478f, k = fx_p.f_k;
        const float a1 = 1.0f / (1.0f + g * (g + k1)), a2 = 1.0f / (1.0f + g * (g + k));
        const int lp = fx_p.f_mode == 1;
        uint32_t c;
        for (c = 0; c < 2u; c++) {
            float *x = c ? r : l, s1 = fx.s1[c], s2 = fx.s2[c], s3 = fx.s3[c], s4 = fx.s4[c];
            for (i = 0; i < n; i++) {
                float hpo = (x[i] - (g + k1) * s1 - s2) * a1, bp = g * hpo + s1, lpo = g * bp + s2, y;
                s1 = g * hpo + bp;
                s2 = g * bp + lpo;
                y = lp ? lpo : hpo;
                hpo = (y - (g + k) * s3 - s4) * a2; bp = g * hpo + s3; lpo = g * bp + s4;
                s3 = g * hpo + bp;
                s4 = g * bp + lpo;
                x[i] = lp ? lpo : hpo;
            }
            fx.s1[c] = fx_absf(s1) < 1e-20f ? 0.0f : s1;
            fx.s2[c] = fx_absf(s2) < 1e-20f ? 0.0f : s2;
            fx.s3[c] = fx_absf(s3) < 1e-20f ? 0.0f : s3;
            fx.s4[c] = fx_absf(s4) < 1e-20f ? 0.0f : s4;
        }
    }
    for (i = 0; i < n; i++) {                   /* the limiter: the gain each sample wants (ceiling / peak), the
                                                 * least over the look-ahead, a release, then a box of FX_LA so the
                                                 * gain ramps down and reaches it as that sample leaves the delay */
        float a = fx_absf(l[i]) > fx_absf(r[i]) ? fx_absf(l[i]) : fx_absf(r[i]), want = a > FX_CEIL ? FX_CEIL / a : 1.0f;
        float least = want, lv, rv, g;
        int32_t j, li = fx.li;
        fx.lq[li] = want;
        if (want < 1.0f) fx.lhold = 0;
        else if (fx.lhold < FX_LA) fx.lhold++;
        if (fx.lhold < FX_LA)                   /* (a gain under 1 in the window: its least) */
            for (j = 0; j < FX_LA; j++) if (fx.lq[j] < least) least = fx.lq[j];
        fx.lgr = least < fx.lgr ? least : fx.lgr + (least - fx.lgr) * 1.9e-4f;   /* (release ~120 ms) */
        fx.lsum += fx.lgr - fx.lb[li];
        fx.lb[li] = fx.lgr;
        g = fx.lsum * (1.0f / (float)FX_LA);
        lv = fx.la[0][li] * g; rv = fx.la[1][li] * g;   /* (the sample of FX_LA ago) */
        fx.la[0][li] = l[i]; fx.la[1][li] = r[i];
        fx.li = li + 1 == FX_LA ? 0 : li + 1;
        lv *= 32768.0f; rv *= 32768.0f;
        out[2u * i] = (int32_t)(lv > 32767.0f ? 32767.0f : lv < -32767.0f ? -32767.0f : lv);
        out[2u * i + 1u] = (int32_t)(rv > 32767.0f ? 32767.0f : rv < -32767.0f ? -32767.0f : rv);
    }
}
