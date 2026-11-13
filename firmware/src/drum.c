/* SPDX-License-Identifier: GPL-3.0-only */
/* Drumtonic's drum engine (drum.h). Per channel: an oscillator (sine, triangle, saw) with a pitch modulation
 * (decay, sine LFO or sampled noise) and an attack-decay envelope; a noise through a state-variable filter
 * (low, band or high pass) with an exponential, linear or clap envelope, mono or stereo; their mix through a
 * distortion and a peaking EQ, then level and pan. Choke groups 1..3. A peak limiter on the sum.
 *
 * Formats: audio Q15 in int32; envelopes Q30 (ONE); gains Q15; pitch in Q16 octaves; phases 32 bit.
 * Control values change once every DRUM_BLK frames; the envelopes are interpolated in between. */
#include "drum.h"
#include "drum_tables.h"

#define DRUM_FS 44100u
#define ONE (1u << 30)                          /* an envelope at full level */
#define ENV_FLOOR (1u << 14)                    /* -96 dB: the envelope ends */
#define INC_20HZ 1947831u                       /* 20 Hz as a phase step: 20 * 2^32 / 44100 */
#define SVF_20HZ 486958u                        /* 20 Hz for the filter's sine: 20 * 2^32 / (4 * 44100) */
#define LIM_THR 30000                           /* the limiter's ceiling, Q15 */
#define DRUM_HOT static inline __attribute__((always_inline))   /* per-sample helpers: no call, no arguments on the stack */

/* ---- tables and conversions (also used by the main loop when it compiles a patch) */
static uint32_t drum_exp2(int32_t x)            /* 2^(x / 65536) in Q16, for x in about -16..14 octaves */
{
    int32_t i = x >> 16;
    uint32_t f = (uint32_t)x & 0xFFFFu, k = f >> 8, fr = f & 0xFFu;
    uint32_t m = DRUM_EXP2[k] + (((DRUM_EXP2[k + 1u] - DRUM_EXP2[k]) * fr) >> 8);
    if (i >= 0)
        return i > 14 ? 0xFFFFFFFFu : m << i;
    return i < -31 ? 0u : m >> -i;
}

DRUM_HOT int32_t drum_sin(uint32_t ph)            /* a full cycle over 2^32 -> Q15 */
{
    uint32_t i = ph >> 22, fr = (ph >> 6) & 0xFFFFu;
    int32_t a = DRUM_SINE[i], b = DRUM_SINE[i + 1u];
    return a + (int32_t)(((int64_t)(b - a) * (int32_t)fr) >> 16);
}

static uint32_t knob_ms(uint32_t v) { return drum_exp2((int32_t)(v * 6347u)) >> 16; }   /* 1 ms .. 5 s */
static uint32_t knob_mode(uint32_t v) { return v * 3u >> 7; }                            /* 0, 1, 2 */

static uint32_t decay_k(uint32_t ms)            /* the per-block factor (Q16) that falls 60 dB in ms */
{
    return drum_exp2(-(int32_t)(236964u / (ms ? ms : 1u)));   /* 2^(-9.966 * 16 / (ms * 44.1)) */
}
static uint32_t ramp_step(uint32_t ms)          /* the per-block step (Q30) that goes 0 -> ONE in ms; 0 ms: at once */
{
    return ms ? 389565470u / ms : ONE;          /* 2^30 * 16 / 44.1 */
}

static int32_t svf_f(uint32_t ph, int32_t q, int32_t limit)   /* 2 sin(pi fc / (2 fs')) in Q15, kept stable */
{
    int32_t f = 2 * drum_sin(ph), fmax = ((limit - q) * 61) >> 6;
    return f > fmax ? fmax : f < 64 ? 64 : f;
}

/* ---- a channel's compiled patch: written by the main loop, read by the ISR */
typedef struct {
    uint32_t inc;                               /* the oscillator's pitch as a phase step */
    uint32_t phase0;                            /* where it starts */
    int32_t pamt;                               /* pitch modulation depth, Q16 octaves */
    uint32_t pk, plfo;                          /* decay factor of the pitch envelope; LFO / sample-and-hold step */
    uint32_t oatk, ok, natk, nk, nlin;          /* envelopes: attack steps, decay factors, linear decay step */
    uint32_t nmorph;                            /* the noise's decay: 0 exponential .. 256 linear (the clap: 0) */
    int32_t nf, nq, nbpg, eqf, eqg;             /* noise filter and EQ coefficients (Q15) */
    int32_t og, ng;                             /* osc and noise in the mix, Q15 */
    int32_t ovel, nvel;                         /* velocity sensitivity, Q15 */
    int32_t drive;                              /* distortion: 0 off, else a gain in Q12 */
    int32_t gl, gr;                             /* level times pan, Q15 */
    uint8_t wave, pmode, nfilt, nenv, stereo, choke;
#ifdef DRUM_SENDS
    int32_t srev, sdly;                         /* the sends to the reverb and the delay, Q15 (set after drum_set_patch) */
#endif
} drum_cfg_t;

/* ---- a channel's running state (the ISR's only) */
enum { E_OFF, E_ATK, E_DEC, E_LIN, E_CLAP };
typedef struct { uint32_t lvl, cnt; uint8_t st, burst; } drum_env_t;
typedef struct {
    uint32_t ph, lfo, rng, rng2, seen;
    int32_t hold, ogv, ngv, gl, gr;             /* sampled-noise value; this hit's mix and output gains with its velocity */
    int32_t semi_q16;                           /* this hit's transposition, Q16 octaves */
    uint32_t penv;                              /* pitch envelope, Q30 */
    drum_env_t eo, en, el;                      /* osc; noise: its exponential (or clap) and its linear decay */
    int32_t lp, bp, lp2, bp2;                   /* noise filter (L, and R when stereo) */
    int32_t eql, ebl, eqr, ebr;                 /* EQ filter, L and R */
    uint8_t on, kill;
} drum_voice_t;

static drum_cfg_t drum_cfg[DRUM_NCH];
static drum_voice_t drum_v[DRUM_NCH];
static volatile struct { uint32_t seq; uint8_t vel; int8_t semi; } drum_req[DRUM_NCH];
static int32_t drum_lim = 32768;                /* the limiter's gain, Q15 */
#ifdef DRUM_SENDS
/* DRUM_SENDS (the FM-1): each channel also feeds two mono buses, the reverb's and the delay's (fx.c), before the limiter;
 * and counts its hits (the master's PUMP keys on them) */
static int32_t drum_bus[2][256];                /* reverb, delay: a render's frames (DRUM_BUS_N) */
static int32_t drum_sblk[2][DRUM_BLK];
static volatile uint32_t drum_hits[DRUM_NCH];
#define DRUM_BUS_N 256u
#endif

/* the distortion's makeup: how much louder DIST makes a sound on average (the RMS of a hit's first 300 ms, the 59
 * library sounds, 2026-11-12: +4.6 dB at 8, +9.2 at 32, +12.5 at 127; one sound to another within about 5 dB), in Q16
 * octaves at DIST 0 1 8 16 32 48 64 80 96 112 127; taken off the level, so DIST changes the sound, not its volume */
static const uint8_t DIST_AT[11] = {0, 1, 8, 16, 32, 48, 64, 80, 96, 112, 127};
static const int32_t DIST_UP[11] = {0, 6531, 50072, 76197, 100145, 113207, 120827, 126269, 130624, 133889, 136066};
static uint32_t dist_makeup(uint32_t d)        /* Q16: 1 at DIST off, 0.24 at 127 */
{
    uint32_t i;
    for (i = 1; i < 10u && d > DIST_AT[i]; i++) {}
    if (d > DIST_AT[i]) d = DIST_AT[i];
    return drum_exp2(-(DIST_UP[i - 1u] + (DIST_UP[i] - DIST_UP[i - 1u]) * (int32_t)(d - DIST_AT[i - 1u]) / (int32_t)(DIST_AT[i] - DIST_AT[i - 1u])));
}

static void drum_set_patch(uint32_t ch, const drum_patch_t *pp)
{
    const uint8_t *p = pp->p;
    drum_cfg_t *c = &drum_cfg[ch % DRUM_NCH];
    uint32_t v;
    int32_t q, mix;

    c->wave = (uint8_t)knob_mode(p[P_WAVE]);
    c->inc = (uint32_t)(((uint64_t)INC_20HZ * drum_exp2((int32_t)(p[P_FREQ] * 4128u))) >> 16);   /* 20 Hz .. 5.1 kHz */
    c->phase0 = (uint32_t)p[P_PHASE] * 8454951u;                                                /* 0 .. 90 degrees */
    c->pmode = (uint8_t)knob_mode(p[P_PMODE]);
    c->pamt = ((int32_t)p[P_PAMT] - 64) * 4161;                                                 /* +-48 semitones */
    c->pk = decay_k(knob_ms(p[P_PRATE]));
    c->plfo = (uint32_t)(((uint64_t)155831u * drum_exp2((int32_t)(p[P_PRATE] * 7378u))) >> 16); /* 0.1 Hz .. 2 kHz, a step a block */
    c->oatk = p[P_OATK] ? ramp_step(knob_ms(p[P_OATK]) >> 2) : ONE;
    c->ok = decay_k(knob_ms(p[P_ODEC]));
    c->ovel = (int32_t)p[P_OVEL] * 258;

    c->nfilt = (uint8_t)knob_mode(p[P_NFILT]);
    q = (int32_t)((46341u * drum_exp2(-(int32_t)(p[P_NQ] * 2580u))) >> 16);                     /* 1/Q: Q 0.7 .. 22 */
    c->nq = q;
    c->nbpg = (int32_t)((38968u * drum_exp2(-(int32_t)(p[P_NQ] * 1290u))) >> 16);                 /* sqrt(1/Q) */
    c->nf = svf_f((uint32_t)(((uint64_t)SVF_20HZ * drum_exp2((int32_t)(p[P_NFREQ] * 5160u))) >> 16), q, 65536);   /* 20 Hz .. 20 kHz, run twice a frame */
    c->nenv = (uint8_t)knob_mode(p[P_NENV]);
    c->nmorph = p[P_NENV] > NENV_V_EXP && p[P_NENV] < NENV_V_LIN ? (p[P_NENV] - NENV_V_EXP) * 256u / (NENV_V_LIN - NENV_V_EXP)
                                                                 : c->nenv == NENV_LIN ? 256u : 0u;
    c->natk = p[P_NATK] ? ramp_step(knob_ms(p[P_NATK]) >> 2) : ONE;
    v = knob_ms(p[P_NDEC]);
    c->nk = decay_k(v);
    c->nlin = ramp_step(v);
    c->stereo = p[P_NSTEREO] >= 64u;
    c->nvel = (int32_t)p[P_NVEL] * 258;

    mix = (int32_t)p[P_MIX];
    c->drive = p[P_DIST] ? 4096 + (int32_t)p[P_DIST] * 484 : 0;                                  /* x1 .. x16 */
    c->eqf = svf_f((uint32_t)(((uint64_t)(SVF_20HZ * 5u) * drum_exp2((int32_t)(p[P_EQF] * 3922u))) >> 16), 21845, 65536);   /* 50 Hz .. 9.5 kHz (once a frame), Q 1.5 */
    c->eqg = p[P_EQG] < 64u ? ((int32_t)p[P_EQG] - 64) * 512 : ((int32_t)p[P_EQG] - 64) * 1560;  /* cut to -inf .. +12 dB */
    c->og = (127 - mix) * 258;                                                                   /* the mix, Q15 */
    c->ng = mix * 258;
    v = (uint32_t)p[P_LEVEL] * p[P_LEVEL] * 3u >> 1;                                             /* level after the distortion: a square law, 127 -> 0.74 */
    if (p[P_DIST]) v = (uint32_t)(((uint64_t)v * dist_makeup(p[P_DIST])) >> 16);                /* (less the distortion's makeup) */
    c->gr = (int32_t)(((int64_t)drum_sin((uint32_t)p[P_PAN] * 8454951u) * (int32_t)v) >> 15);  /* pan: constant power */
    c->gl = (int32_t)(((int64_t)drum_sin((uint32_t)p[P_PAN] * 8454951u + 0x40000000u) * (int32_t)v) >> 15);
    c->choke = (uint8_t)(p[P_CHOKE] >> 5);
}

static void drum_trigger(uint32_t ch, uint32_t vel, int32_t semi)
{
    ch %= DRUM_NCH;
    drum_req[ch].vel = (uint8_t)(vel > 127u ? 127u : vel);
    drum_req[ch].semi = (int8_t)semi;
    drum_req[ch].seq++;                         /* last: the ISR starts the hit when it sees the new count */
}

static void drum_init(void)
{
    uint32_t i;
    for (i = 0; i < DRUM_NCH; i++) {
        drum_v[i].rng = 0x9E3779B9u * (i + 1u);
        drum_v[i].rng2 = 0x7F4A7C15u * (i + 3u);
        drum_v[i].seen = drum_req[i].seq;
    }
}

static int32_t vel_gain(int32_t sens, uint32_t vel)   /* 1 - sens + sens * (vel / 127)^2, Q15 (a drum machine's square law) */
{
    return 32768 - sens + (int32_t)((uint32_t)sens * (vel * vel) / 16129u);
}

static void env_start(drum_env_t *e, uint32_t atk, uint32_t mode)   /* mode: NENV_... (the oscillator: NENV_EXP) */
{
    e->burst = 0;
    e->cnt = 0;
    if (mode == NENV_CLAP) { e->st = E_CLAP; e->lvl = ONE; }
    else if (atk >= ONE) { e->st = mode == NENV_LIN ? E_LIN : E_DEC; e->lvl = ONE; }
    else { e->st = E_ATK; e->lvl = 0; }
}

static void drum_hit_isr(uint32_t ch, uint32_t vel, int32_t semi)   /* a hit now, from the audio ISR (DRUM_BLOCK_HOOK) */
{
    const drum_cfg_t *c = &drum_cfg[ch % DRUM_NCH];
    drum_voice_t *v = &drum_v[ch % DRUM_NCH];
    uint32_t i;
    ch %= DRUM_NCH;
    if (vel > 127u) vel = 127u;
    if (c->choke)                               /* the others of its group fall silent in ~10 ms */
        for (i = 0; i < DRUM_NCH; i++)
            if (i != ch && drum_cfg[i].choke == c->choke && drum_v[i].on)
                drum_v[i].kill = 1;
    v->semi_q16 = semi * 5461;                  /* 65536 / 12 */
    {   /* the velocity: after the distortion the volume, as the mix weighs osc and noise; before it, only their
         * balance (the louder part's gain left a soft hit loud when one page's VEL was low, a unit, 2026-10-28) */
        int32_t vo = vel_gain(c->ovel, vel), vn = vel_gain(c->nvel, vel), m;
        uint32_t w = (uint32_t)(c->og + c->ng);
        m = w ? (int32_t)(((uint32_t)vo * (uint32_t)c->og + (uint32_t)vn * (uint32_t)c->ng) / w) : vo > vn ? vo : vn;
        v->ogv = (int32_t)(((int64_t)c->og * (int32_t)(((uint32_t)vo << 15) / (uint32_t)m)) >> 15);   /* (<= 2^30: 32 bit) */
        v->ngv = (int32_t)(((int64_t)c->ng * (int32_t)(((uint32_t)vn << 15) / (uint32_t)m)) >> 15);
        v->gl = (int32_t)(((int64_t)c->gl * m) >> 15);
        v->gr = (int32_t)(((int64_t)c->gr * m) >> 15);
    }
    v->ph = c->phase0;
    v->lfo = 0;
    v->penv = ONE;
    v->hold = 0;
    env_start(&v->eo, c->oatk, NENV_EXP);
    env_start(&v->en, c->natk, c->nenv == NENV_CLAP ? NENV_CLAP : NENV_EXP);
    env_start(&v->el, c->natk, NENV_LIN);
    v->lp = v->bp = v->lp2 = v->bp2 = 0;
    v->eql = v->ebl = v->eqr = v->ebr = 0;
    v->kill = 0;
    v->on = 1;
#ifdef DRUM_SENDS
    drum_hits[ch]++;
#endif
}

/* one block's step of an envelope */
#define KFAST 52000u                            /* ~10 ms to silence (Q16), for a choke */
#define CLAP_GAP 30u                            /* blocks between the clap's first hits (~11 ms) */
static void env_step(drum_env_t *e, uint32_t atk, uint32_t k, uint32_t lin, int kill)
{
    switch (e->st) {
    case E_ATK:
        e->lvl += atk;
        if (e->lvl >= ONE) { e->lvl = ONE; e->st = lin ? E_LIN : E_DEC; }
        break;
    case E_CLAP:                                /* three short hits, then the tail */
        e->lvl = (uint32_t)(((uint64_t)e->lvl * 46000u) >> 16);   /* each hit: ~3 ms */
        if (++e->cnt >= CLAP_GAP) {
            e->cnt = 0;
            e->lvl = ONE;
            if (++e->burst >= 3u) e->st = E_DEC;
        }
        break;
    case E_DEC:
        e->lvl = (uint32_t)(((uint64_t)e->lvl * k) >> 16);
        break;
    case E_LIN:
        e->lvl = e->lvl > lin ? e->lvl - lin : 0u;
        break;
    default:
        return;
    }
    if (kill) e->lvl = (uint32_t)(((uint64_t)e->lvl * KFAST) >> 16);
    if (e->st != E_ATK && e->st != E_CLAP && e->lvl < ENV_FLOOR) { e->lvl = 0; e->st = E_OFF; }
}

static uint32_t noise_lvl(const drum_cfg_t *c, const drum_voice_t *v)   /* the two decays in their proportion (0, 256: one alone, exactly) */
{
    return (uint32_t)(((uint64_t)v->en.lvl * (256u - c->nmorph) + (uint64_t)v->el.lvl * c->nmorph) >> 8);
}

DRUM_HOT int32_t softclip(int32_t x)              /* x - 4/27 x^3 up to 1.5, then 1.0 (Q15) */
{
    int64_t x3;
    if (x > 49152) return 32768;
    if (x < -49152) return -32768;
    x3 = ((int64_t)x * x >> 15) * x >> 15;
    return x - (int32_t)((x3 * 4855) >> 15);
}

/* the noise filter, run twice a frame; the band pass gains sqrt(Q) at its centre (bpg = sqrt(1/Q), Q15) */
DRUM_HOT int32_t svf2(int32_t x, int32_t f, int32_t q, int32_t bpg, int32_t *lp, int32_t *bp, uint32_t mode)
{
    int32_t l = *lp, b = *bp, h = 0, i;
    for (i = 0; i < 2; i++) {
        l += (int32_t)(((int64_t)f * b) >> 15);
        h = x - l - (int32_t)(((int64_t)q * b) >> 15);
        b += (int32_t)(((int64_t)f * h) >> 15);
    }
    *lp = l;
    *bp = b;
    return mode == NF_LP ? l : mode == NF_HP ? h : (int32_t)(((int64_t)bpg * b) >> 15);
}

DRUM_HOT int32_t eq(int32_t x, const drum_cfg_t *c, int32_t *lp, int32_t *bp)   /* x + gain * band pass (Q 1.5) */
{
    int32_t l = *lp + (int32_t)(((int64_t)c->eqf * *bp) >> 15);
    int32_t h = x - l - (int32_t)((21845 * (int64_t)*bp) >> 15);
    int32_t b = *bp + (int32_t)(((int64_t)c->eqf * h) >> 15);
    *lp = l;
    *bp = b;
    return x + (int32_t)(((int64_t)c->eqg * ((21845 * (int64_t)b) >> 15)) >> 15);
}

DRUM_HOT int32_t shape(int32_t x, const drum_cfg_t *c)
{
    return c->drive ? softclip((int32_t)(((int64_t)x * c->drive) >> 12)) : x;
}

DRUM_HOT uint32_t xorshift(uint32_t *s)
{
    uint32_t x = *s;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return *s = x;
}

/* one block of one channel, added to out (DRUM_BLK frames) and, with DRUM_SENDS, to the sends sb. It touches only
 * this channel's voice (and reads its cfg): two cores can render two channels at once (DRUM_CPU2) */
static void voice_block(uint32_t ch, int32_t *out, int32_t (*sb)[DRUM_BLK])
{
    const drum_cfg_t *c = &drum_cfg[ch];
    drum_voice_t *v = &drum_v[ch];
    uint32_t i, o0 = v->eo.lvl, n0 = noise_lvl(c, v), inc;
    int32_t pitch, dO, dN, eo, en;

    /* the control step: envelopes to the block's end, the pitch for this block */
    env_step(&v->eo, c->oatk, c->ok, 0, v->kill);
    env_step(&v->en, c->natk, c->nk, 0, v->kill);
    env_step(&v->el, c->natk, c->nk, c->nlin, v->kill);
    if (v->eo.st == E_OFF && (c->nmorph == 256u || v->en.st == E_OFF) && (c->nmorph == 0u || v->el.st == E_OFF)) { v->on = 0; return; }
    switch (c->pmode) {
    case PMOD_DECAY:
        pitch = (int32_t)(((int64_t)c->pamt * (v->penv >> 14)) >> 16);
        v->penv = (uint32_t)(((uint64_t)v->penv * c->pk) >> 16);
        break;
    case PMOD_SINE:
        pitch = (int32_t)(((int64_t)c->pamt * drum_sin(v->lfo)) >> 15);
        v->lfo += c->plfo;
        break;
    default:                                    /* a new random pitch at each LFO cycle */
        if (v->lfo + c->plfo < v->lfo) v->hold = (int32_t)xorshift(&v->rng2) >> 16;
        v->lfo += c->plfo;
        pitch = (int32_t)(((int64_t)c->pamt * v->hold) >> 15);
        break;
    }
    pitch += v->semi_q16;
    pitch = pitch > 6 * 65536 ? 6 * 65536 : pitch < -10 * 65536 ? -10 * 65536 : pitch;
    inc = (uint32_t)(((uint64_t)c->inc * drum_exp2(pitch)) >> 16);
    if (inc > 0x7FFFFFFFu) inc = 0x7FFFFFFFu;

    eo = (int32_t)(o0 >> 15);                   /* Q15, then a straight line to the block's end */
    en = (int32_t)(n0 >> 15);
    dO = ((int32_t)(v->eo.lvl >> 15) - eo) >> 4;
    dN = ((int32_t)(noise_lvl(c, v) >> 15) - en) >> 4;

    for (i = 0; i < DRUM_BLK; i++) {
        int32_t s = 0, sr, osc, nz;
        if (v->ogv) {
            uint32_t ph = v->ph;
            osc = c->wave == WAVE_SINE ? drum_sin(ph)
                : c->wave == WAVE_TRI ? (int32_t)((ph & 0x80000000u ? ~ph : ph) >> 15) - 32768
                : (int32_t)ph >> 16;
            v->ph = ph + inc;
            s = (int32_t)(((int64_t)osc * eo >> 15) * v->ogv >> 15);
        }
        sr = s;
        if (v->ngv) {
            nz = (int32_t)xorshift(&v->rng) >> 16;   /* full scale */
            nz = svf2(nz, c->nf, c->nq, c->nbpg, &v->lp, &v->bp, c->nfilt);
            s += (int32_t)(((int64_t)nz * en >> 15) * v->ngv >> 15);
            if (c->stereo) {
                nz = (int32_t)xorshift(&v->rng2) >> 16;
                nz = svf2(nz, c->nf, c->nq, c->nbpg, &v->lp2, &v->bp2, c->nfilt);
                sr += (int32_t)(((int64_t)nz * en >> 15) * v->ngv >> 15);
            } else {
                sr = s;
            }
        }
        s = shape(s, c);
        if (c->eqg) s = eq(s, c, &v->eql, &v->ebl);
        if (c->stereo && v->ngv) {
            sr = shape(sr, c);
            if (c->eqg) sr = eq(sr, c, &v->eqr, &v->ebr);
        } else {
            sr = s;
        }
        s = (int32_t)(((int64_t)s * v->gl) >> 15);
        sr = (int32_t)(((int64_t)sr * v->gr) >> 15);
        out[2u * i] += s;
        out[2u * i + 1u] += sr;
#ifdef DRUM_SENDS
        if (c->srev | c->sdly) {
            int32_t m = (s + sr) >> 1;
            sb[0][i] += (int32_t)(((int64_t)m * c->srev) >> 15);
            sb[1][i] += (int32_t)(((int64_t)m * c->sdly) >> 15);
        }
#endif
        eo += dO;
        en += dN;
    }
}

#ifdef DRUM_CPU2
/* the second core (DRUM_CPU2: cpu2_run / cpu2_wait, platform.c): each block, every other channel sounding renders
 * there into its own mix and sends, added after; integer sums, so the sound is the same bit for bit */
static int32_t drum_c1_mix[2u * DRUM_BLK], drum_c1_sb[2][DRUM_BLK];
static uint8_t drum_c1_ch[DRUM_NCH];
static uint32_t drum_c1_n;
static void drum_c1_job(uint32_t a)
{
    uint32_t i;
    (void)a;
    for (i = 0; i < 2u * DRUM_BLK; i++) drum_c1_mix[i] = 0;
    for (i = 0; i < DRUM_BLK; i++) drum_c1_sb[0][i] = drum_c1_sb[1][i] = 0;
    for (i = 0; i < drum_c1_n; i++) voice_block(drum_c1_ch[i], drum_c1_mix, drum_c1_sb);
}
#endif

static void drum_render(int32_t *out, uint32_t n)
{
    static int32_t mix[2u * DRUM_BLK];
    uint32_t ch, b, i;
    for (ch = 0; ch < DRUM_NCH; ch++)           /* new hits, at the start of the render */
        if (drum_req[ch].seq != drum_v[ch].seen) {
            drum_v[ch].seen = drum_req[ch].seq;
            drum_hit_isr(ch, drum_req[ch].vel, drum_req[ch].semi);
        }
    for (b = 0; b + DRUM_BLK <= n; b += DRUM_BLK) {
        int32_t peak = 0, g0, g1;
#ifdef DRUM_BLOCK_HOOK
        DRUM_BLOCK_HOOK();                      /* (the sequencer: its hits start at this block, 0.36 ms steps) */
#endif
        for (i = 0; i < 2u * DRUM_BLK; i++)
            mix[i] = 0;
#ifdef DRUM_SENDS
        for (i = 0; i < DRUM_BLK; i++)
            drum_sblk[0][i] = drum_sblk[1][i] = 0;
#endif
#ifdef DRUM_CPU2
        {
            uint32_t act[DRUM_NCH], na = 0, k, two;
            for (ch = 0; ch < DRUM_NCH; ch++)
                if (drum_v[ch].on) act[na++] = ch;
            drum_c1_n = 0;
            for (k = 1; k < na; k += 2) drum_c1_ch[drum_c1_n++] = (uint8_t)act[k];
            two = drum_c1_n && cpu2_run(drum_c1_job, 0);
            for (k = 0; k < na; k += two ? 2u : 1u) voice_block(act[k], mix, drum_sblk);
            if (two) {
                if (cpu2_wait() == 0) {
                    for (i = 0; i < 2u * DRUM_BLK; i++) mix[i] += drum_c1_mix[i];
                    for (i = 0; i < DRUM_BLK; i++) { drum_sblk[0][i] += drum_c1_sb[0][i]; drum_sblk[1][i] += drum_c1_sb[1][i]; }
                } else {                        /* (it failed: held for good; its channels here) */
                    for (k = 1; k < na; k += 2) voice_block(act[k], mix, drum_sblk);
                }
            }
        }
#else
        for (ch = 0; ch < DRUM_NCH; ch++)
            if (drum_v[ch].on)
#ifdef DRUM_SENDS
                voice_block(ch, mix, drum_sblk);
#else
                voice_block(ch, mix, 0);
#endif
#endif
#ifdef DRUM_SENDS
        for (i = 0; i < DRUM_BLK && b + i < DRUM_BUS_N; i++) {
            drum_bus[0][b + i] = drum_sblk[0][i];
            drum_bus[1][b + i] = drum_sblk[1][i];
        }
#endif
        for (i = 0; i < 2u * DRUM_BLK; i++) {   /* the limiter: no lookahead, instant attack, ~90 ms release */
            int32_t a = mix[i] < 0 ? -mix[i] : mix[i];
            if (a > peak) peak = a;
        }
        g0 = drum_lim;
        if ((int64_t)peak * g0 > (int64_t)LIM_THR << 15)
            g1 = (int32_t)(((uint32_t)LIM_THR << 15) / (uint32_t)peak);
        else
            g1 = g0 + ((32768 - g0) >> 8);
        drum_lim = g1;
        if (g1 < g0) g0 = g1;                   /* (attack: the whole block at the lower gain) */
        for (i = 0; i < DRUM_BLK; i++) {
            int32_t g = g0 + (((g1 - g0) * (int32_t)i) >> 4);
            int32_t l = (int32_t)(((int64_t)mix[2u * i] * g) >> 15), r = (int32_t)(((int64_t)mix[2u * i + 1u] * g) >> 15);
            out[2u * (b + i)] += l > 32767 ? 32767 : l < -32767 ? -32767 : l;
            out[2u * (b + i) + 1u] += r > 32767 ? 32767 : r < -32767 ? -32767 : r;
        }
    }
}
