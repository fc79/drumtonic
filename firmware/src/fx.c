/* SPDX-License-Identifier: GPL-3.0-only */
/* The FM-1's effects and master (fx_param.h), after drum_render, on the whole mix (app_audio):
 *
 *   out = limit( filter( comp( drive( dry + reverb(rev bus) + delay(dly bus) ))))
 *
 * The algorithms are after 9W9's send reverb, delay and master (GPL-3.0), written again here without
 * their helper libraries:
 *   reverb   five types (fx_rv.c): CLASSIC (four combs at 12 bits, the early-rack grain, with damping in the loop, two
 *            allpasses a side; the right reads the combs with alternate signs), PLATE, HALL, ROOM, GATED; a low cut and
 *            PRE DLY on the send, WIDTH after
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
#define FX_D2R 512u                             /* TO REV's ring: the delay's echo, 14 bits (8192 is 1.0), read by the
                                                 * reverb 256 frames later (fx_render) */
static int16_t fx_d2r[FX_D2R];
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
    float rv_fb, rv_damp, rv_lvl, rv_hpa, dl_ms, dl_fb, dl_tc, dl_lvl, dl_hpa, dl_rv, wear;
    int32_t tape, ping;
    float drive, c_thr, c_slope, c_atk, c_rel, c_mix, c_fall, c_mkt, c_hpa;
    int32_t c_on, f_mode;                       /* filter: 0 off, 1 LP, 2 HP */
    float f_g, f_k;
} fx_p;
static struct {                                 /* (ISR) */
    float cdmp[4];                              /* CLASSIC's dampings (its lines: fx_rv.c rv) */
    int32_t rv_quiet;
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
static float fx_psin(uint32_t ph) { float x = (float)(int32_t)ph * (1.0f / 2147483648.0f); return 4.0f * x * (1.0f - fx_absf(x)); }
static float fx_db2lin(float db) { return fx_exp2(db * 0.16609640f); }
static float fx_pot(uint32_t v) { return (float)v * (1.0f / 127.0f); }
static float fx_hpa(uint32_t v)                 /* a one-pole high-pass's coefficient: 20 .. 1000 Hz */
{
    float fc = 20.0f * fx_exp2(fx_pot(v) * 5.644f), rc = 1.0f / (6.2831853f * fc);
    return rc / (rc + 1.0f / FX_SR);
}

#ifdef DRUM_CPU2
static uint8_t fx_late;                         /* this render's reverb is the late one (fx_late_start): PRE DLY 256 frames
                                                 * shorter inside (fx_reverb) */
#else
#define fx_late 0
#endif
#include "fx_rv.c"

/* ---- the main loop's side */
static void fx_apply_rv(const uint8_t *x)       /* the reverb's part of a kit's effects (x: its kitx + X_FX) */
{
    fx_p.rv_fb = 0.2f + 0.73f * fx_pot(x[FX_RV_DECAY]);
    fx_p.rv_damp = 0.75f - fx_pot(x[FX_RV_TONE]) * 0.55f;
    fx_p.rv_lvl = 1.0f;                         /* (no RETURN knob: unity gain; the sends set how much) */
    fx_p.rv_hpa = fx_hpa(x[FX_RV_HPF]);
    rv_params(x);
}
#ifdef DRUM_CPU2
static const uint8_t *fx_rv_pend;              /* a kit's reverb settings the ISR brought while core 1 ran the reverb (a
                                                 * pattern's next kit): taken when it is done (fx_render) */
#endif
static void fx_apply_x(const uint8_t *kx)       /* from a kit's kitx (the ISR: a pattern's next kit, app_fm1.c) */
{
    const uint8_t *x = kx + X_FX;
    float ms, a;
    uint32_t f = x[FX_M_FILTER];
#ifdef DRUM_CPU2
    if (drum_c1_busy) fx_rv_pend = x;           /* (only the ISR, mid-render: main never runs while core 1 works) */
    else
#endif
    fx_apply_rv(x);
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
    fx_p.dl_rv = (float)fx_get(x, FX_DL_REV) / 100.0f;   /* TO REV: the delay into the reverb */
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
    memset(fx_d2r, 0, sizeof fx_d2r);
    fx.rv_quiet = FX_RV_QUIET;
    fx.dl_quiet = (int32_t)FX_DLEN;
    for (i = 0; i < FX_LA; i++) fx.lq[i] = fx.lb[i] = 1.0f;
    fx.lsum = (float)FX_LA;
    fx.lgr = 1.0f;
    fx_apply();
    fx.dcur = fx_p.dl_ms * 0.001f * FX_SR;
    rv_ready = 0;                               /* (main and the ISR never run at once: core 1 works inside the ISR) */
    memset(&rv, 0, sizeof rv);
    memset(rv_pre, 0, sizeof rv_pre);
    rv_start(rv_p.type);
    rv.fade = 1.0f;
    rv.sz = rv_p.size;
    rv.pcur = rv_p.pre;
    rv_ready = 1;
}

/* ---- the ISR's side */
static inline __attribute__((always_inline)) float fx_tap(int32_t w, float d)   /* the line d samples behind w, interpolated
                                                 * (inline: two a sample, 2026-11-23) */
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
static void fx_delay(const int32_t *bus, float *l, float *r, int16_t *wet, uint32_t at, uint32_t n)   /* wet: its echo, mono, into
                                                 * fx_d2r from at (TO REV) */
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
            for (i = 0; i < n; i++) wet[(at + i) & (FX_D2R - 1u)] = 0;
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
        { float e = (y + y2) * (0.5f * 8192.0f) * lvl; wet[(at + i) & (FX_D2R - 1u)] = (int16_t)(e > 32767.0f ? 32767.0f : e < -32767.0f ? -32767.0f : e); }
        quiet = (k == 0 && bus[i] == 0) ? quiet + 1 : 0;
    }
    fx.w = w; fx.dcur = dcur; fx.lp = lp; fx.tape_hp = hp; fx.dl_quiet = quiet;
    if (fx_absf(fx.dl_hy) < 1e-20f) fx.dl_hy = 0.0f;
}

/* the limiter's least (fx_render): place k of lq into q (qn long from qh), after dropping those not under its gain */
static inline void fx_lq_push(uint8_t *q, uint32_t qh, int32_t *qn, uint32_t k)
{
    const float v = fx.lq[k];
    int32_t n = *qn;
    while (n && fx.lq[q[(qh + (uint32_t)n - 1u) % FX_LA]] >= v) n--;
    q[(qh + (uint32_t)n) % FX_LA] = (uint8_t)k;
    *qn = n + 1;
}

static float fx_l[256], fx_r[256];
#ifdef DRUM_CPU2
static float fx_dl[256], fx_dr[256];            /* the delay's own lines, beside the reverb on core 1 (one pair for both
                                                 * ways: until 2026-11-25 each had its own, 2 KB) */
#endif
/* TO REV: the delay's echo into the reverb, a fixed 256 frames (5.8 ms) later: the reverb's input (fx_rvin) is made
 * before the delay runs, so it takes what the delay gave before (the user's choice, 2026-11-16: a block later, not one
 * core) */
static uint32_t fx_d2r_w;
static int32_t fx_rvin[256];
static const int32_t *fx_rvbus;                 /* the reverb's input this block: the sends, with the delay if TO REV */
#ifdef DRUM_CPU2
static void fx_c1_job(uint32_t n) { PF_C1_BEGIN(); fx_reverb(fx_rvbus, fx_l, fx_r, n); PF_C1_END(); }   /* (the second core) */
/* the reverb a render late (the user's choice, 2026-11-23: docs/dev/PERF.md): from the start of a render core 1 runs it on
 * the last render's input (fx_rvq), beside the voices (all on core 0 then: drum_c1_busy), and its return joins the mix
 * 256 frames (5.8 ms) late. The reverb itself is the same sample for sample, with its settings as they were at the end
 * of the render its input comes from (fx_rvs; a kit's change mid-render waits in fx_rv_pend). With one core it runs
 * here, at the same place: the same sound */
static int32_t fx_rvq[256];
static uint32_t fx_rvq_n;
static float fx_rvl[256], fx_rvr[256];         /* (core 1's return) */
#if PERF_TEST
static uint8_t fx_late_sw = 1;                 /* the timing page: PRESETS turns it off (the reverb as before) and on */
#else
#define fx_late_sw 1
#endif
static __typeof__(rv_p) fx_rvs;                 /* the reverb's settings at the end of the render fx_rvq comes from: a knob
                                                 * turned since (main, between renders) waits for the next input */
static float fx_rvs4[4];                        /* (fx_p's: rv_fb, rv_damp, rv_lvl, rv_hpa) */
static uint8_t fx_rvs_ok;
static void fx_rvs_swap(void)                   /* the settings playing <-> the held ones (only the reverb reads them
                                                 * while core 1 works: main does not run, the ISR's kit waits) */
{
    __typeof__(rv_p) t = rv_p;
    float f[4] = {fx_p.rv_fb, fx_p.rv_damp, fx_p.rv_lvl, fx_p.rv_hpa};
    rv_p = fx_rvs; fx_rvs = t;
    fx_p.rv_fb = fx_rvs4[0]; fx_p.rv_damp = fx_rvs4[1]; fx_p.rv_lvl = fx_rvs4[2]; fx_p.rv_hpa = fx_rvs4[3];
    fx_rvs4[0] = f[0]; fx_rvs4[1] = f[1]; fx_rvs4[2] = f[2]; fx_rvs4[3] = f[3];
}
static void fx_c1_late(uint32_t n)
{
    uint32_t i, ok = fx_rvs_ok;
    PF_C1_BEGIN();
    for (i = 0; i < n; i++) fx_rvl[i] = fx_rvr[i] = 0.0f;
    if (ok) fx_rvs_swap();
    fx_reverb(fx_rvq, fx_rvl, fx_rvr, n);
    if (ok) fx_rvs_swap();
    PF_C1_END();
}
static void fx_late_start(void)                 /* app_audio, first thing */
{
    fx_late = fx_late_sw;
    if (!fx_late) return;
    if (cpu2_run(fx_c1_late, fx_rvq_n)) drum_c1_busy = 1;
    else fx_c1_late(fx_rvq_n);                  /* (one core: here, now) */
}
#endif

static void fx_render(int32_t *out, uint32_t n)
{
    float *l = fx_l, *r = fx_r;
    uint32_t i;
    if (n > 256u) n = 256u;
    for (i = 0; i < n; i++) { l[i] = (float)out[2u * i] * (1.0f / 32768.0f); r[i] = (float)out[2u * i + 1u] * (1.0f / 32768.0f); }
    fx_rvbus = drum_bus[0];
    if (fx_p.dl_rv > 0.0f) {                    /* (0 %: the sends alone, the reverb as before bit for bit) */
        const float a = fx_p.dl_rv * 4.0f;   /* (8192 -> the bus's 32768) */
        for (i = 0; i < n; i++) {
            float v = (float)drum_bus[0][i] + (float)fx_d2r[(fx_d2r_w + FX_D2R - 256u + i) & (FX_D2R - 1u)] * a;
            fx_rvin[i] = v > 2.0e9f ? 2000000000 : v < -2.0e9f ? -2000000000 : (int32_t)v;
        }
        fx_rvbus = fx_rvin;
    }
    PF_LAP(PF_FXIN);
#ifdef DRUM_CPU2
    if (fx_late) {                              /* the late reverb (fx_late_start): the delay here, then the reverb's return
                                                 * and the delay's added in today's order (the same sums) */
        float *dl = fx_dl, *dr = fx_dr;
        uint32_t m = fx_rvq_n;
        for (i = 0; i < n; i++) dl[i] = dr[i] = 0.0f;
        fx_delay(drum_bus[1], dl, dr, fx_d2r, fx_d2r_w, n);
        if (drum_c1_busy) {                     /* (cpu2_wait never fails today: fm1_c1_fail stops all. With one core
                                                 * as a fall-back, docs/dev/ONE-CORE.md, this needs one: fx_rvl here) */
            (void)PF_WAIT(cpu2_wait(), PF_DLY, PF_RVW);
            PF_C1(PF_RV1);
            drum_c1_busy = 0;
        }
        for (i = 0; i < n && i < m; i++) { l[i] += fx_rvl[i]; r[i] += fx_rvr[i]; }
        for (i = 0; i < n; i++) { l[i] += dl[i]; r[i] += dr[i]; }
        if (fx_rv_pend) { fx_apply_rv(fx_rv_pend); fx_rv_pend = 0; }
        PF_LAP(PF_DLY);
    } else if (cpu2_run(fx_c1_job, n)) {        /* the reverb there, the delay here into its own lines, added after:
                                                 * each adds one term a sample, so the sum is the same bit for bit */
        float *dl = fx_dl, *dr = fx_dr;
        for (i = 0; i < n; i++) dl[i] = dr[i] = 0.0f;
        fx_delay(drum_bus[1], dl, dr, fx_d2r, fx_d2r_w, n);
        if (PF_WAIT(cpu2_wait(), PF_DLY, PF_RVW) != 0) fx_reverb(fx_rvbus, l, r, n);   /* (it failed: here) */
        else PF_C1(PF_RV1);
        for (i = 0; i < n; i++) { l[i] += dl[i]; r[i] += dr[i]; }
        PF_LAP(PF_DLY);
    } else {
        fx_reverb(fx_rvbus, l, r, n);
        fx_delay(drum_bus[1], l, r, fx_d2r, fx_d2r_w, n);
    }
    for (i = 0; i < n; i++) fx_rvq[i] = fx_rvbus[i];   /* the next render's reverb input, and its settings now */
    fx_rvq_n = n;
    fx_rvs = rv_p;
    fx_rvs4[0] = fx_p.rv_fb; fx_rvs4[1] = fx_p.rv_damp; fx_rvs4[2] = fx_p.rv_lvl; fx_rvs4[3] = fx_p.rv_hpa;
    fx_rvs_ok = rv_ready;
#else
    fx_reverb(fx_rvbus, l, r, n);
    fx_delay(drum_bus[1], l, r, fx_d2r, fx_d2r_w, n);
#endif
    fx_d2r_w = (fx_d2r_w + n) & (FX_D2R - 1u);
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
    PF_LAP(PF_MAST);
    {   /* the limiter: the gain each sample wants (ceiling / peak), the least over the look-ahead, a release, then a
         * box of FX_LA so the gain ramps down and reaches it as that sample leaves the delay. The least: q, the
         * window's places from the oldest, each one's gain under all the newer ones' (fx_lq_push), so the first is the
         * least; built again from lq when a gain under 1 comes back into the window, then a push a sample (until
         * 2026-11-17 all FX_LA looked through at each sample, 64 compares). Its state in locals, written back after */
        uint32_t li = (uint32_t)fx.li, j, qh = 0;
        int32_t lhold = fx.lhold, qn = -1;      /* (qn: q's length, -1 not built) */
        uint8_t q[FX_LA];
        float lgr = fx.lgr, lsum = fx.lsum;
        for (i = 0; i < n; i++) {
            float a = fx_absf(l[i]) > fx_absf(r[i]) ? fx_absf(l[i]) : fx_absf(r[i]), want = a > FX_CEIL ? FX_CEIL / a : 1.0f;
            float least = want, lv, rv, g;
            if (want < 1.0f) lhold = 0;
            else if (lhold < FX_LA) lhold++;
            if (lhold >= FX_LA) {               /* (no gain under 1 in the window: 1) */
                qn = -1;
                fx.lq[li] = want;
            } else {
                if (qn < 0) { qn = 0; qh = 0; for (j = 1; j < FX_LA; j++) fx_lq_push(q, qh, &qn, (li + j) % FX_LA); }
                else if (qn && q[qh] == li) { qh = (qh + 1u) % FX_LA; qn--; }   /* (the oldest leaves) */
                fx.lq[li] = want;
                fx_lq_push(q, qh, &qn, li);
                least = fx.lq[q[qh]];
            }
            lgr = least < lgr ? least : lgr + (least - lgr) * 1.9e-4f;   /* (release ~120 ms) */
            lsum += lgr - fx.lb[li];
            fx.lb[li] = lgr;
            g = lsum * (1.0f / (float)FX_LA);
            lv = fx.la[0][li] * g; rv = fx.la[1][li] * g;   /* (the sample of FX_LA ago) */
            fx.la[0][li] = l[i]; fx.la[1][li] = r[i];
            li = li + 1u == FX_LA ? 0u : li + 1u;
            lv *= 32768.0f; rv *= 32768.0f;
            out[2u * i] = (int32_t)(lv > 32767.0f ? 32767.0f : lv < -32767.0f ? -32767.0f : lv);
            out[2u * i + 1u] = (int32_t)(rv > 32767.0f ? 32767.0f : rv < -32767.0f ? -32767.0f : rv);
        }
        fx.li = (int32_t)li; fx.lhold = lhold; fx.lgr = lgr; fx.lsum = lsum;
    }
    PF_LAP(PF_LIM);
}
