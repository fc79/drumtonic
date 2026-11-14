/* SPDX-License-Identifier: GPL-3.0-only */
/* Drumtonic's app on the FM-1 (drumtonic.c includes it), with its interface (ui_fm1.c): 32 patterns of up
 * to 64 steps (seq_fm1.c), knob motion, p-locks and step conditions (motion.c), 8 LFOs a pattern (lfo.c), the
 * reverb, the delay and the master (fx.c), 8 kits, a sound library (64 of the user's in slots 0 and 1).
 *
 * What is kept, where (store.h's FM-1 sectors; other FM-1 firmwares share them, a save overwrites what they kept):
 *   settings      0xFC000      the kit, tempo, pattern, channel, clock (SAVE, only if they changed)
 *   blob 0        0xF4000      the sound playing (the kit, the sends, the effects) and the 8 kits (SAVE, a kit saved)
 *   blobs 1..5    0xF5000..    the patterns, their lanes and their LFOs (SAVE)
 * Drumtonic 0.0's kit and 8 x 16-step patterns load into this format the first time. */
#include "motion.h"
#include "fx_param.h"
#ifdef DINGHY_HOST
#define POOL
#else
#define POOL __attribute__((section(".pool")))
#endif

/* ---- the settings */
#define SET_FMT 0xD3u                           /* (0xD2: 8 channels, before MT and HT) */
static struct { uint8_t fmt, kit, bpm, pat, ch, ext, cpu_mode, rec_free; } set = {SET_FMT, 0, 120, 0, 0, 0, 0, 0};   /* (cpu_mode:
                                                 * SETTINGS > CPU MODE until 2026-11-12, now ignored: 2 x 320 MHz always;
                                                 * rec_free: SETTINGS > REC, 0 GRID, 1 FREE: a hit by hand recorded with
                                                 * its micro timing; a spare byte before, 0 in older saves) */
static uint8_t set_saved[sizeof set];           /* the settings as in the flash: SAVE writes them only if they differ */

/* ---- the sound: the kit (drumtonic.c's kit) and kitx, the sends and the effects; the 8 kits */
#define NKIT 8u
enum { X_SEND = 0, X_FX = 2 * DRUM_NCH, SND_NX = 64 };   /* kitx: the sends (channel c: 2c reverb, 2c + 1 delay), FX_P from X_FX */
typedef struct { drum_kit_t k; uint8_t x[SND_NX]; } snd_t;
#define SND_FMT 0x33444E53u                     /* "SND3": 10 channels */
typedef struct { uint32_t fmt; snd_t cur, kits[NKIT]; uint8_t used, kc, rsv[2]; } snd_blob_t;   /* 3176 B (kc: cur's kit + 1,
                                                 * 0 not known: until 2026-11-12, the settings' kit) */
#define SND_FMT2 0x32444E53u                    /* "SND2": 8 channels, before MT and HT (kitx: the effects from 16) */
typedef struct { uint32_t fmt; struct { drum_kit8_t k; uint8_t x[SND_NX]; } cur, kits[NKIT]; uint8_t used, rsv[3]; } snd2_blob_t;
_Static_assert(X_FX + FX_NP <= SND_NX, "kitx: the sends and the effects do not fit");
static uint8_t kitx[SND_NX];
static snd_t kits[NKIT];
static uint8_t kits_used, kit_cur, snd_kc;
static snd_blob_t snd_io POOL;

static void mot_kit_changed(uint32_t ch);
static void mot_all_changed(void);
#include "fx.c"

static void snd_x_defaults(uint8_t *x)
{
    uint32_t i;
    memset(x, 0, SND_NX);
    for (i = 0; i < FX_NP; i++) x[X_FX + i] = FX_P[i].def;
}
static void sound_apply(void) { mot_all_changed(); fx_apply(); }

/* 8 channels (the factory kits, what was saved before MT and HT) -> 10: MT and HT made from LT (channel 6), higher,
 * shorter and to the right; RS and CY after them */
static void kit_from8(drum_kit_t *d, const drum_kit8_t *s)
{
    uint32_t c, t;
    memcpy(d->name, s->name, sizeof d->name);
    for (c = 0; c < 8u; c++) {
        memcpy(d->ch[c < 6u ? c : c + 2u], s->ch[c], 4);
        d->pat[c < 6u ? c : c + 2u] = s->pat[c];
    }
    for (t = 1; t <= 2u; t++) {
        drum_patch_t *p = &d->pat[5u + t];
        uint32_t f = s->pat[5].p[P_FREQ] + 6u * t, pan = s->pat[5].p[P_PAN] + 18u * t;
        *p = s->pat[5];
        p->p[P_FREQ] = (uint8_t)(f > 127u ? 127u : f);   /* (+6: ~4.5 semitones a tom) */
        p->p[P_ODEC] = (uint8_t)(p->p[P_ODEC] > 4u * t ? p->p[P_ODEC] - 4u * t : 0u);
        p->p[P_NDEC] = (uint8_t)(p->p[P_NDEC] > 4u * t ? p->p[P_NDEC] - 4u * t : 0u);
        p->p[P_PAN] = (uint8_t)(pan > 127u ? 127u : pan);
        memcpy(d->ch[5u + t], t == 1u ? "MT" : "HT", 3);
    }
    for (c = 8; c < DRUM_NCH; c++)              /* (INDUSTR's metal was "MT") */
        if (d->ch[c][0] == 'M' && d->ch[c][1] == 'T' && !d->ch[c][2]) d->ch[c][1] = 'E';
}
static void x_from8(uint8_t *x)                 /* kitx: the sends of 8 channels and the effects at 16 -> 10 and X_FX */
{
    uint8_t o[SND_NX];
    uint32_t c;
    memcpy(o, x, SND_NX);
    memset(x, 0, SND_NX);
    for (c = 0; c < 8u; c++) { x[X_SEND + (c < 6u ? c : c + 2u) * 2u] = o[c * 2u]; x[X_SEND + (c < 6u ? c : c + 2u) * 2u + 1u] = o[c * 2u + 1u]; }
    for (c = 6; c < 8u; c++) { x[X_SEND + c * 2u] = o[10]; x[X_SEND + c * 2u + 1u] = o[11]; }   /* the toms: as LT */
    memcpy(x + X_FX, o + 16, FX_NP);
}
static int snd_save(void)
{
    snd_io.fmt = SND_FMT;
    snd_io.cur.k = kit;
    memcpy(snd_io.cur.x, kitx, SND_NX);
    memcpy(snd_io.kits, kits, sizeof kits);
    snd_io.used = kits_used;
    snd_io.kc = (uint8_t)(kit_cur + 1u);
    return blob_save(0, &snd_io, sizeof snd_io);
}
static void snd_load(void)
{
    static snd2_blob_t old POOL;
    static drum_kit8_t old0 POOL;
    uint32_t k;
    if (!blob_load(0, &snd_io, sizeof snd_io) && snd_io.fmt == SND_FMT) {
        kit = snd_io.cur.k;
        memcpy(kitx, snd_io.cur.x, SND_NX);
        memcpy(kits, snd_io.kits, sizeof kits);
        kits_used = snd_io.used;
        snd_kc = snd_io.kc;
        return;
    }
    if (!blob_load(0, &old, sizeof old) && old.fmt == SND_FMT2) {   /* saved before MT and HT */
        kit_from8(&kit, &old.cur.k);
        memcpy(kitx, old.cur.x, SND_NX); x_from8(kitx);
        for (k = 0; k < NKIT; k++) { kit_from8(&kits[k].k, &old.kits[k].k); memcpy(kits[k].x, old.kits[k].x, SND_NX); x_from8(kits[k].x); }
        kits_used = old.used;
        return;
    }
    for (k = 0; k < NKIT; k++) {                /* the factory kits in the first places */
        kit_from8(&kits[k].k, &DRUM_KITS[k < DRUM_NKITS ? k : 0]);
        snd_x_defaults(kits[k].x);
    }
    kits_used = (uint8_t)((1u << DRUM_NKITS) - 1u);
    if (blob_load(0, &old0, sizeof old0))       /* (Drumtonic 0.0 kept the kit alone) */
        old0 = DRUM_KITS[0];
    kit_from8(&kit, &old0);
    snd_x_defaults(kitx);
}
/* ---- each pattern's kit (2026-11-12): seq_pat_t.kit, 0 none, else the kit + 1. A pattern with steps and none takes
 * the kit playing (a pattern made, or one saved before this); one with no steps has none; a kit loaded (MIX) is the
 * pattern playing's. The pattern playing's kit plays, its effects too: stopped, at once (main); playing, as a pattern
 * starts (the ISR, mot_step, before its first hits: kit_isr_take, staged by kit_follow). The kits' edits are kept while
 * another plays (kw: each kit's working copy, kw_on: those that differ from the saved one); SAVE saves them all into
 * their places; a tap on the kit playing (MIX) takes it back to the saved one.
 * Only main writes kit, kitx and kit_cur (kit and kitx are always kit_cur's): the ISR's swap compiles the sound from
 * kit_stage (motion.c mot_kit, mot_kx; fx_apply_x), and main takes it over (kit_take_over) before the next input. */
static snd_t kw[NKIT] POOL;                     /* (the kit playing's is kit and kitx: kw only while another plays) */
static uint8_t kw_on;
static snd_t kit_stage POOL;                    /* main: the next pattern's kit (not written while armed or swapped) */
static volatile uint8_t kit_stage_pat = 0xFFu, kit_stage_k, kit_swaps;
static uint8_t kit_swaps_seen;
static const drum_kit_t *volatile mot_kit = &kit;   /* (motion.c's ISR: where the sound is compiled from) */
static const uint8_t *volatile mot_kx = kitx;
static uint32_t kit_differs(uint32_t k)         /* the kit playing (k) is not its saved one */
{
    return memcmp(&kit, &kits[k].k, sizeof kit) || memcmp(kitx, kits[k].x, SND_NX);
}
static uint32_t kits_dirty(void)                /* some kit has edits SAVE would keep */
{
    return (kw_on & ~(1u << kit_cur)) || kit_differs(kit_cur);
}
static void kit_park(const snd_t *cur)          /* the kit playing's edits, kept for when it plays again */
{
    kw[kit_cur] = *cur;
    if (memcmp(cur, &kits[kit_cur], sizeof *cur)) kw_on |= (uint8_t)(1u << kit_cur);
    else kw_on &= (uint8_t)~(1u << kit_cur);
}
static const snd_t *kit_src(uint32_t k) { return kw_on >> k & 1u ? &kw[k] : &kits[k]; }   /* (with its edits) */
static void kit_switch(uint32_t k)              /* main: kit k plays, with its edits; this one's kept */
{
    snd_t cur;
    k %= NKIT;
    kit_stage_pat = 0xFFu;                      /* (the ISR swaps no more) */
    cur.k = kit; memcpy(cur.x, kitx, SND_NX);
    kit_park(&cur);
    kit = kit_src(k)->k;
    memcpy(kitx, kit_src(k)->x, SND_NX);
    kit_cur = (uint8_t)k;
    __asm__ volatile("" ::: "memory");
    mot_kit = &kit; mot_kx = kitx;              /* (heard from kit again, if the ISR had swapped) */
    sound_apply();
}
static uint32_t kit_isr_take(uint32_t pat)      /* ISR, a pattern starting (mot_step): its kit, if staged; 1 taken */
{
    if (pat != kit_stage_pat) return 0;
    kit_stage_pat = 0xFFu;
    mot_kit = &kit_stage.k; mot_kx = kit_stage.x;
    fx_apply_x(kit_stage.x);
    kit_swaps++;
    return 1;
}
static uint32_t kit_take_over(void)             /* main, before the input: the kit the ISR swapped in is kit_cur now */
{
    snd_t cur;
    if (kit_swaps == kit_swaps_seen) return 0;
    kit_swaps_seen = kit_swaps;
    cur.k = kit; memcpy(cur.x, kitx, SND_NX);
    kit_park(&cur);
    kit = kit_stage.k;
    memcpy(kitx, kit_stage.x, SND_NX);
    kit_cur = kit_stage_k;
    __asm__ volatile("" ::: "memory");
    mot_kit = &kit; mot_kx = kitx;
    sound_apply();                              /* (fx_apply again: the ISR's may have met main's) */
    return 1;
}
static void kit_load(uint32_t k)                /* MIX: kit k (with its edits); the one playing: back to its saved one */
{
    k %= NKIT;
    if (k == kit_cur) {
        kit = kits[k].k;
        memcpy(kitx, kits[k].x, SND_NX);
        kw_on &= (uint8_t)~(1u << k);
        sound_apply();
    } else {
        kit_switch(k);
    }
    set.kit = (uint8_t)k;                       /* (the settings remember it: SAVE) */
    seq_song.p[seq.pat].kit = (uint8_t)(k + 1u);   /* (the pattern playing's kit now) */
    kit_edited = 1;
}
static int kit_store(uint32_t k)                /* written at once */
{
    k %= NKIT;
    kit_stage_pat = 0xFFu;
    kits[k].k = kit;
    memcpy(kits[k].x, kitx, SND_NX);
    kits_used |= (uint8_t)(1u << k);
    kw_on &= (uint8_t)~(1u << kit_cur | 1u << k);   /* (the edits are k's now: the one playing goes back to its own) */
    kit_cur = set.kit = (uint8_t)k;             /* (the settings remember it: SAVE) */
    seq_song.p[seq.pat].kit = (uint8_t)(k + 1u);
    return snd_save();
}

/* ---- the sound library (ui_fm1.c, EDIT's last bank): SND_LIB's sounds and 64 of the user's, in slot 0 (0x97000, empty
 * under the stock firmware: its own data at 0xFA000 stays); a sound saved there is written at once, in slices */
#include "sound_lib.h"
#define LIB_USER 64u
#define LIB_SLOT 0u                             /* and LIB_SLOT + 1: written in turn, the newer valid one loads, so a */
                                                /* save cut short loses that save only, never the 63 others */
#define LIB_FMT 0x3142494Cu                     /* "LIB1" */
typedef struct { char name[4]; drum_patch_t p; } lib_snd_t;   /* 28 B */
typedef struct { uint32_t fmt, gen; uint64_t used; lib_snd_t s[LIB_USER]; } lib_blob_t;   /* 1808 B; gen: the saves */
static lib_blob_t lib_user POOL, lib_other POOL;
static uint8_t lib_save_req;
static void lib_load(void)                      /* the newer of the two valid copies (also: back to the flash's after a failed save) */
{
    int a = !slot_load(LIB_SLOT, &lib_user, sizeof lib_user) && lib_user.fmt == LIB_FMT;
    int b = !slot_load(LIB_SLOT + 1u, &lib_other, sizeof lib_other) && lib_other.fmt == LIB_FMT;
    if (b && (!a || (int32_t)(lib_other.gen - lib_user.gen) > 0)) lib_user = lib_other;
    else if (!a) {
        memset(&lib_user, 0, sizeof lib_user);
        lib_user.fmt = LIB_FMT;
    }
}
static int lib_write(void)                      /* into the slot not holding the newer copy */
{
    int rc;
    lib_user.gen++;
    if ((rc = slot_save(LIB_SLOT + (lib_user.gen & 1u), &lib_user, sizeof lib_user)) != 0) lib_load();
    return rc;
}

/* ---- the patterns, their lanes and LFOs: one record over blobs 1..5 */
#include "motion.c"
#include "lfo.c"
#define SONG_FMT 0x33474E53u                    /* "SNG3": each part its own length */
#define SONG_FMT2 0x32474E53u                   /* "SNG2": 10 channels, one length */
#define SONG_FMT1 0x31474E53u                   /* "SNG1": 8, before MT and HT (RS and CY were 7 and 8) */
typedef struct { seq_track_t t[8]; uint8_t len, rate, swing, rsv; } seq_pat8_t;
typedef struct { seq_track_t t[DRUM_NCH]; uint8_t len, rate, swing, rsv; } seq_pat2_t;
#define SONG_BLOB 1u
#define SONG_NBLOB 5u
#define SONG_MAX (4u + sizeof(seq_song_t) + 4u + NLANE * sizeof(lane_t) + sizeof lfos)
static uint8_t song_io[SONG_MAX] POOL;          /* a save's bytes, a load's, HOME + REC's */
_Static_assert(SONG_MAX <= SONG_NBLOB * 0x1000u - 12u, "the patterns' record is bigger than its blobs");

static uint32_t song_pack(uint8_t *b)
{
    uint32_t n = 0, i, cnt = 0, f = SONG_FMT;
    memcpy(b, &f, 4); n = 4;
    memcpy(b + n, &seq_song, sizeof seq_song); n += sizeof seq_song;
    n += 4;
    for (i = 0; i < NLANE; i++)
        if (lanes[i].used) { memcpy(b + n, &lanes[i], sizeof(lane_t)); n += sizeof(lane_t); cnt++; }
    memcpy(b + 4u + sizeof seq_song, &cnt, 4);
    memcpy(b + n, lfos, sizeof lfos); n += sizeof lfos;
    return n;
}
static uint32_t ch_from8(uint32_t c) { return c < 6u ? c : c + 2u; }   /* a channel saved before MT and HT */
static void pat_unpack(seq_pat_t *d, const uint8_t *b, uint32_t i, uint32_t old, uint32_t v2)   /* pattern i of a record's */
{
    seq_pat8_t o;
    uint32_t c;
    if (v2) { seq_pat2_t o2; memcpy(&o2, b + i * sizeof o2, sizeof o2); memset(d, 0, sizeof *d); memcpy(d, &o2, sizeof o2); return; }   /* (tlen: song_unpack) */
    if (!old) { memcpy(d, b + i * sizeof(seq_pat_t), sizeof *d); return; }
    memcpy(&o, b + i * sizeof o, sizeof o);
    memset(d, 0, sizeof *d);
    for (c = 0; c < 8u; c++) d->t[ch_from8(c)] = o.t[c];
    d->len = o.len; d->rate = o.rate; d->swing = o.swing;
}
static void pat_fit_len(uint32_t pat)           /* a pattern ends with its longest part */
{
    seq_pat_t *q = &seq_song.p[pat];
    uint32_t c, m = 1;
    for (c = 0; c < DRUM_NCH; c++) if (q->tlen[c] > m) m = q->tlen[c];
    q->len = (uint8_t)m;
}
/* a record's pattern p (p SEQ_NPAT: all of them) into the live ones; 0 done, -1 not a record, -2 motion full.
 * A record of 8 channels (SNG1) is read as 10: its channels 7 and 8 (RS, CY), their lanes and LFOs, become 9 and 10 */
static int song_unpack(const uint8_t *b, uint32_t n, uint32_t p)
{
    uint32_t cnt, i, k0, at, f = (uint32_t)(b[0] | b[1] << 8 | b[2] << 16 | (uint32_t)b[3] << 24), old = f == SONG_FMT1, v2 = f == SONG_FMT2;
    uint32_t ss = SEQ_NPAT * (old ? sizeof(seq_pat8_t) : v2 ? sizeof(seq_pat2_t) : sizeof(seq_pat_t));
    int rc = 0;
    if (n < 8u + ss || (f != SONG_FMT && !old && !v2)) return -1;
    memcpy(&cnt, b + 4u + ss, 4);
    at = 8u + ss;
    if (cnt > NLANE || n != at + cnt * sizeof(lane_t) + sizeof lfos) return -1;
    if (p >= SEQ_NPAT) {
        for (i = 0; i < SEQ_NPAT; i++) pat_unpack(&seq_song.p[i], b + 4, i, old, v2);
        memset(lanes, 0, sizeof lanes);
        memset(mot_quant, 0, sizeof mot_quant);   /* (as saved: QUANT is never saved, SAVE puts it into the lanes) */
    } else {
        pat_unpack(&seq_song.p[p], b + 4, p, old, v2);
        mot_clear(p, DRUM_NCH);
        memset(mot_quant[p], 0, sizeof mot_quant[0]);
    }
    for (i = 0; i < cnt; i++) {
        lane_t l;
        int32_t k;
        memcpy(&l, b + at + i * sizeof(lane_t), sizeof l);
        if (old && l.ch < 8u) l.ch = (uint8_t)ch_from8(l.ch);
        if (l.pat >= SEQ_NPAT || l.ch >= DRUM_NCH || l.param >= P_NTARGET || (p < SEQ_NPAT && l.pat != p)) continue;
        if ((k = mot_alloc(l.pat, l.ch, l.param)) < 0) { rc = -2; break; }
        memcpy(lanes[k].val, l.val, sizeof l.val);
    }
    at += cnt * sizeof(lane_t);
    if (p >= SEQ_NPAT) memcpy(lfos, b + at, sizeof lfos);
    else memcpy(lfos[p], b + at + p * sizeof lfos[0], sizeof lfos[0]);
    if (old)
        for (i = 0; i < SEQ_NPAT; i++)
            if (p >= SEQ_NPAT || i == p)
                for (k0 = 0; k0 < NLFO; k0++)
                    if (lfos[i][k0].ch < 8u) lfos[i][k0].ch = (uint8_t)ch_from8(lfos[i][k0].ch);
    for (i = 0; i < SEQ_NPAT; i++) {            /* (whatever was saved: within range) */
        seq_pat_t *q = &seq_song.p[i];
        if (!q->len || q->len > SEQ_NSTEP) q->len = 16;
        for (k0 = 0; k0 < DRUM_NCH; k0++) if (!q->tlen[k0] || q->tlen[k0] > SEQ_NSTEP) q->tlen[k0] = q->len;
        pat_fit_len(i);
        q->rate %= SEQ_NRATE;
        if (q->swing < 50u || q->swing > 75u) q->swing = 50;
    }
    mot_all_changed();
    return rc;
}
static int song_save(void) { return blobs_save(SONG_BLOB, SONG_NBLOB, song_io, song_pack(song_io)); }
static int song_read(void)                      /* the saved record into song_io: its length, 0 none */
{
    uint32_t n;
    return blobs_load(SONG_BLOB, SONG_NBLOB, song_io, sizeof song_io, &n) ? 0 : (int)n;
}

static void song_empty(void)
{
    uint32_t p;
    memset(&seq_song, 0, sizeof seq_song);
    memset(lanes, 0, sizeof lanes);
    memset(lfos, 0, sizeof lfos);
    for (p = 0; p < SEQ_NPAT; p++) { seq_song.p[p].len = 16; memset(seq_song.p[p].tlen, 16, DRUM_NCH); seq_song.p[p].swing = 50; }
}
static void demo_song(void)                     /* patterns 1 and 2: a groove to start from (as on the SMK-37) */
{
    static const char *const A[DRUM_NCH] = {   /* BD SD CP CH OH LT MT HT RS CY */
        "X---x-----x-x---", "----X-------X---", "------------x---", "x-x-x-x-x-x-x---",
        "--------------X-", "----------------", "----------------", "----------------", "---x------x-----", "X---------------"};
    static const char *const B[DRUM_NCH] = {
        "X---x-----x-x--x", "----X--x-x--X-xx", "----x-------x---", "x-xxx-x-x-xxx-x-",
        "--------------X-", "--------------x-", "------------x---", "----------x-----", "---x--x---x-----", "----------------"};
    uint32_t c, s;
    for (c = 0; c < DRUM_NCH; c++)
        for (s = 0; s < 16u; s++) {
            seq_set(0, c, s, A[c][s] == 'X' ? STEP_ACC : A[c][s] == 'x' ? STEP_ON : STEP_OFF);
            seq_set(1, c, s, B[c][s] == 'X' ? STEP_ACC : B[c][s] == 'x' ? STEP_ON : STEP_OFF);
        }
}
static void song_load(void)
{
    static uint8_t old[8][8][16];               /* Drumtonic 0.0: blob 1, 8 patterns x 8 channels x 16 steps */
    uint32_t n = (uint32_t)song_read(), p, c, s;
    song_empty();
    if (n && !song_unpack(song_io, n, SEQ_NPAT)) return;
    if (n) song_empty();
    if (!blob_load(1, old, sizeof old)) {
        for (p = 0; p < 8u; p++)
            for (c = 0; c < 8u; c++)
                for (s = 0; s < 16u; s++) seq_set(p, ch_from8(c), s, old[p][c][s] % 3u);
        return;
    }
    demo_song();
}

/* ---- the steps */
static uint32_t ch_used(uint32_t pat, uint32_t ch)   /* the channel has a step in the pattern */
{
    const seq_track_t *t = &seq_song.p[pat].t[ch];
    return t->hit[0] | t->hit[1];
}
static uint32_t pat_used(uint32_t pat)
{
    uint32_t c, u = 0;
    for (c = 0; c < DRUM_NCH; c++) u |= ch_used(pat, c);
    return u;
}
static uint32_t pat_len(uint32_t pat) { return seq_song.p[pat % SEQ_NPAT].len; }
static uint32_t track_len(uint32_t pat, uint32_t c) { return seq_tlen(&seq_song.p[pat % SEQ_NPAT], c % DRUM_NCH); }

static void track_clear(uint32_t pat, uint32_t ch)   /* ch DRUM_NCH: all the channels */
{
    uint32_t c;
    for (c = 0; c < DRUM_NCH; c++)
        if (ch >= DRUM_NCH || c == ch) { memset(&seq_song.p[pat].t[c], 0, sizeof(seq_track_t)); mot_quant[pat][c] = 0; }
    mot_clear(pat, ch);
    lfo_clear_ch(pat, ch);
}
static int track_copy(uint32_t sp, uint32_t sc, uint32_t dp, uint32_t dc)   /* sc DRUM_NCH: the whole pattern */
{
    if (sc >= DRUM_NCH) {
        if (sp != dp) { seq_song.p[dp] = seq_song.p[sp]; memcpy(lfos[dp], lfos[sp], sizeof lfos[0]); }
    } else {
        seq_song.p[dp].t[dc] = seq_song.p[sp].t[sc];
        seq_song.p[dp].tlen[dc] = seq_song.p[sp].tlen[sc];
        pat_fit_len(dp);
    }
    return mot_copy(sp, sc, dp, dc);
}
static void track_rotate(uint32_t pat, uint32_t ch, int32_t d)   /* ch DRUM_NCH: all; each within its length */
{
    uint32_t c, len, s, v0;
    for (c = 0; c < DRUM_NCH; c++) {
        if (ch < DRUM_NCH && c != ch) continue;
        len = track_len(pat, c);
        if (len < 2u) continue;
        if (d > 0) {
            v0 = seq_get(pat, c, len - 1u);
            for (s = len - 1u; s; s--) seq_set(pat, c, s, seq_get(pat, c, s - 1u));
            seq_set(pat, c, 0, v0);
        } else {
            v0 = seq_get(pat, c, 0);
            for (s = 0; s + 1u < len; s++) seq_set(pat, c, s, seq_get(pat, c, s + 1u));
            seq_set(pat, c, len - 1u, v0);
        }
        mot_rotate(pat, c, len, d);
    }
}

/* PRESETS, on SEQ: a part's length (ch DRUM_NCH: all of them), copying its steps into a longer one; the part as it was
 * is kept until changed by hand */
static const uint8_t LENS[12] = {1, 2, 3, 4, 5, 6, 8, 12, 16, 24, 32, 64};
static struct { uint8_t on, pat, ch, len; uint32_t hash; seq_pat_t p; } orig;
static lane_t orig_lanes[24] POOL;
static uint8_t orig_n;
static uint32_t part_hash(uint32_t pat, uint32_t ch)
{
    const seq_pat_t *q = &seq_song.p[pat];
    uint32_t h = 2166136261u, i, c, k;
    for (c = 0; c < DRUM_NCH; c++) {
        const uint8_t *b = (const uint8_t *)&q->t[c];
        if (ch < DRUM_NCH && c != ch) continue;
        for (i = 0; i < sizeof(seq_track_t); i++) h = (h ^ b[i]) * 16777619u;
        h = (h ^ q->tlen[c]) * 16777619u;
    }
    MOT_EACH(i, pat, ch)
        for (k = 0; k < sizeof lanes[i].val; k++) h = (h ^ lanes[i].val[k]) * 16777619u;
    return h;
}
static uint32_t orig_len(uint32_t pat, uint32_t ch)   /* the length PRESETS started from, 0: none kept */
{
    return orig.on && orig.pat == pat && orig.ch == ch && orig.hash == part_hash(pat, ch) ? orig.len : 0;
}
static void part_set_len(uint32_t pat, uint32_t ch, int32_t d)   /* one step along LENS */
{
    uint32_t cur = ch < DRUM_NCH ? track_len(pat, ch) : pat_len(pat), i, k, to, s, c;
    seq_pat_t *q = &seq_song.p[pat];
    for (i = 0; i < 12u && LENS[i] < cur; i++) {}
    if (d > 0) to = i < 11u ? LENS[LENS[i] == cur ? i + 1u : i] : 64u;
    else to = i ? LENS[i - 1u] : 1u;
    if (to == cur) return;
    if (!orig_len(pat, ch)) {                   /* the first change by PRESETS: keep the part as it was */
        orig.on = 1; orig.pat = (uint8_t)pat; orig.ch = (uint8_t)ch; orig.len = (uint8_t)cur; orig.p = *q; orig_n = 0;
        MOT_EACH(k, pat, ch)
            if (orig_n < 24u) orig_lanes[orig_n++] = lanes[k];
        orig.on = orig_n < 24u || mot_count() == 0;
    }
    if (orig.on && orig.pat == pat && orig.ch == ch && to == orig.len) {   /* back to where it started: exactly as it was */
        for (c = 0; c < DRUM_NCH; c++)
            if (ch >= DRUM_NCH || c == ch) { q->t[c] = orig.p.t[c]; q->tlen[c] = orig.p.tlen[c]; }
        mot_clear(pat, ch);
        for (k = 0; k < orig_n; k++) {
            int32_t j = mot_alloc(pat, orig_lanes[k].ch, orig_lanes[k].param);
            if (j >= 0) memcpy(lanes[j].val, orig_lanes[k].val, sizeof lanes[j].val);
        }
    } else {
        for (c = 0; c < DRUM_NCH; c++) {
            uint32_t src = orig.on && orig.pat == pat && orig.ch == ch ? orig.p.tlen[c] : q->tlen[c];
            if (ch < DRUM_NCH && c != ch) continue;
            if (to > src) {                     /* longer: what the turn started from, again and again */
                for (s = src; s < to; s++) seq_set(pat, c, s, seq_get(pat, c, s % src));
                mot_repeat(pat, c, src, to);
            }
            q->tlen[c] = (uint8_t)to;
        }
    }
    pat_fit_len(pat);
    for (c = 0; c < DRUM_NCH; c++)              /* (the part goes on in step with the pattern: seq_fm1.c) */
        if (ch >= DRUM_NCH || c == ch) seq_realign_req[c]++;
    orig.hash = part_hash(pat, ch);
    mot_all_changed();
}

/* ---- saving, and HOME + REC */
static uint8_t save_req;
static int save_all(void)                       /* SAVE: the sound and the kits (their edits: into their places), the patterns */
{
    uint32_t k;
    uint8_t on = kw_on;
    save_req = 0;
    for (k = 0; k < NKIT; k++)
        if (k != kit_cur && (kw_on >> k & 1u)) { kits[k] = kw[k]; kits_used |= (uint8_t)(1u << k); }
    kits[kit_cur].k = kit;
    memcpy(kits[kit_cur].x, kitx, SND_NX);
    kits_used |= (uint8_t)(1u << kit_cur);
    kw_on = 0;
    mot_quant_apply();                          /* (QUANT into the micro timing: what is heard is what is saved) */
    if (snd_save() || song_save()) { kw_on = on; return -1; }   /* (not written: the edits still SAVE's to do) */
    if (memcmp(set_saved, &set, sizeof set)) {
        if (settings_save(&set, sizeof set)) return -1;
        memcpy(set_saved, &set, sizeof set);
    }
    kit_edited = 0;
    return 0;
}
static int pat_revert(uint32_t pat)             /* the pattern as last saved: 0, -1 nothing saved, -2 motion full */
{
    int n = song_read();
    if (!n) return -1;
    return song_unpack(song_io, (uint32_t)n, pat);
}

#include "undo.c"
#include "ui_fm1.c"

#ifdef DINGHY_HOST
static int host_probe(const char *name, const int *a, int n, int *got)   /* tests/host.c's "expect" (the docs there) */
{
#define IS(s) (!strcmp(name, s))
#define A(i) ((uint32_t)(i < n ? a[i] : 0))
    if (IS("step")) *got = (int)seq_get(A(0), A(1), A(2));              /* pattern channel step: 0 off 1 on 2 accent */
    else if (IS("len")) *got = (int)pat_len(A(0));
    else if (IS("br")) *got = ui.br_k + 1 + seq_br_on * (16 + seq_br_loop * 32) + ui.br_mode * 64;   /* beat repeat: its length + 1 (0 off), ISR on, looping */
    else if (IS("nr")) *got = ui.nr_on * 16 + ui.nr_rate;   /* note repeat: on * 16 + its rate (0 1/4 .. 7 1/32T) */
    else if (IS("ratch")) *got = (int)ratch_of(mot_get(A(0), A(1), P_RATCH, A(2)));   /* pattern channel step: its hits, 1..4 */
    else if (IS("blackled")) *got = (int)(led_on >> KEY_LED(BLACK[A(0) % 11u]) & 1u) * 2 + (int)(led_glow >> KEY_LED(BLACK[A(0) % 11u]) & 1u);
    else if (IS("whiteled")) *got = (int)(led_on >> KEY_LED(WHITE[A(0) % 16u]) & 1u) * 2 + (int)(led_glow >> KEY_LED(WHITE[A(0) % 16u]) & 1u);
    else if (IS("tlen")) *got = (int)track_len(A(0), A(1));
    else if (IS("pos")) *got = seq_pos[A(0) % DRUM_NCH];
    else if (IS("rate")) *got = seq_song.p[A(0) % SEQ_NPAT].rate;
    else if (IS("swing")) *got = seq_song.p[A(0) % SEQ_NPAT].swing;
    else if (IS("pat")) *got = seq.pat;
    else if (IS("next")) *got = seq.next;
    else if (IS("chain")) *got = seq.chain_n;
    else if (IS("run")) *got = seq.run;
    else if (IS("rec")) *got = rec;
    else if (IS("view")) *got = ui.view;                                /* V_LIVE 0, SET, MIX, STEPS, SOUND, LFO, FX */
    else if (IS("bank")) *got = ui.bank[ui.view] % VIEWS[ui.view].n;
    else if (IS("ch")) *got = set.ch;
    else if (IS("spage")) *got = ui.spage;                              /* STEPS: which 16 steps are shown (0: 1-16) */
    else if (IS("led")) *got = (int)(led_on >> A(0) & 1u) + 2 * (int)(led_glow >> A(0) & 1u);   /* button: 1 lit, 2 dim */
    else if (IS("mute")) *got = seq.mute;
    else if (IS("solo")) *got = seq.solo;
    else if (IS("fill")) *got = mot_fill;
    else if (IS("ask")) *got = ui.ask;
    else if (IS("held")) *got = ui.held;
    else if (IS("librow")) *got = ui.lib_row;
    else if (IS("libused")) *got = (int)(lib_user.used >> A(0) % LIB_USER & 1u);   /* user row n (0..63) */
    else if (IS("preview")) *got = mot_prev ? (int)mot_prev_ch : -1;
    else if (IS("name")) *got = kit.ch[A(0) % DRUM_NCH][0] << 8 | kit.ch[A(0) % DRUM_NCH][1];   /* a channel's name: its 2 first letters */
    else if (IS("open")) *got = ui.splash != 0;                         /* the opening on screen */
    else if (IS("lane")) { uint32_t v = mot_get(A(0), A(1), A(2), A(3)); *got = v == MOT_NONE ? -1 : (int)v; }   /* pat ch param step */
    else if (IS("lanes")) *got = (int)mot_count();
    else if (IS("lfo")) *got = lfos[A(0) % SEQ_NPAT][A(1) % NLFO].used ? lfos[A(0) % SEQ_NPAT][A(1) % NLFO].param : -1;
    else if (IS("lfooff")) *got = mot_lfo_off[A(0) % DRUM_NCH][A(1) % P_COND];
    else if (IS("param")) *got = (int)mot_base(A(0) % DRUM_NCH, A(1));  /* the knob's value: channel param */
    else if (IS("live")) { uint32_t v = mot_ov[A(0) % DRUM_NCH][A(1) % P_COND]; *got = v == MOT_NONE ? -1 : (int)v; }   /* what a lane plays now */
    else if (IS("fx")) *got = kitx[X_FX + A(0) % FX_NP];
    else if (IS("undo")) *got = (undo.back >= 0) | (undo.fwd >= 0) << 1;   /* 1: OCT- has somewhere to go, 2: OCT+ */
    else if (IS("guide")) *got = guide_btn();                            /* the button whose guide shows, -1 none */
    else if (IS("bot")) *got = bot_drop * 2 + bot_ok;                     /* the band's right side: fields left out * 2 + fits beside the left */
    else if (IS("big")) *got = fm1_ms < ui.big_until;                     /* the big bubble (GLO, HOME, undo) */
    else if (IS("kitused")) *got = kits_used;
    else if (IS("kitcur")) *got = kit_cur;
    else if (IS("kitswaps")) *got = kit_swaps;                          /* kits the ISR took as a pattern started */
    else if (IS("patkit")) *got = seq_song.p[A(0) % SEQ_NPAT].kit;   /* pattern: its kit + 1, 0 none */
    else if (IS("kitdirty")) *got = (int)kits_dirty() * 2 + (int)kit_differs(kit_cur);   /* some kit's edits * 2 + the one playing's */
    else if (IS("bpm")) *got = set.bpm;
    else if (IS("count")) { uint32_t i; *got = 0; for (i = 0; i < SEQ_NSTEP; i++) *got += seq_get(A(0), A(1), i) != 0; }   /* pattern channel: steps on */
    else if (IS("hits")) *got = (int)seq_ev_w;                          /* the sequencer's hits so far */
    else if (IS("chhits")) *got = (int)seq_hits[A(0) % DRUM_NCH];   /* channel: hits out of the sequencer since power on (its
                                                                     * steps, ratchets, note repeat, beat repeat) */
    else if (IS("hitms")) *got = (int)seq_hit_ms[A(0) % DRUM_NCH];   /* channel: when its last hit was made, ms */
    else if (IS("micro")) *got = (int)mot_micro(A(0), A(1), A(2));      /* pattern channel step: 24ths, + late */
    else if (IS("quant")) *got = mot_quant[A(0) % SEQ_NPAT][A(1) % DRUM_NCH];   /* pattern channel: QUANT % */
    else if (IS("recfree")) *got = set.rec_free;                        /* SETTINGS > REC: 1 FREE */
    else if (IS("cklock")) *got = (int)ck_lock;                         /* clocks in a row the filter took (4: locked) */
    else if (IS("playstep")) *got = seq.step;
    else if (IS("send")) *got = A(1) ? drum_cfg[A(0) % DRUM_NCH].sdly : drum_cfg[A(0) % DRUM_NCH].srev;   /* channel 0 rev / 1 dly, Q15 */
    else if (IS("rvquiet")) *got = fx.rv_quiet;
    else return -1;
    return 0;
#undef IS
#undef A
}
#endif

/* the processor, for platform.c at power-on (before the app: the clock, the supplies, core 1): two cores at 320 MHz on
 * the DC-DC, always (until 2026-11-12 SETTINGS > CPU MODE chose it, 2 x 360 MHz without a saved choice) */
static uint32_t app_cpu_mode(void) { return CPU_2X320; }

/* MIDI out, from the main loop and while the screen draws (app_idle): the sequencer's hits (channel 10) and, on the
 * internal clock, the clock, each when its sound is heard (seq_fm1.c) */
static void midi_out_due(void)
{
    uint32_t h;
    int32_t e;
    while ((e = seq_out_take()) >= 0) {
        uint32_t ch = (uint32_t)e >> 8;
        hit_ms[ch] = fm1_ms;
        midi_send(0x99, 36u + ch, (uint32_t)e & 0x7Fu);
        midi_send(0x89, 36u + ch, 0);
    }
    h = seq_clocks_due();
    if (seq.run && !seq.ext)
        while (h--) midi_send(0xF8, 0, 0);
}

static void app_main(void)
{
    uint32_t cpu_t = 0;

    if (!settings_load(&set, sizeof set)) memcpy(set_saved, &set, sizeof set);   /* (as in the flash: an old format */
    if (set.fmt == 0xD2u && set.ch <= 8u) {     /* differs, so SAVE writes the new one) RS, CY, ALL: 2 places on */
        set.ch = (uint8_t)(set.ch < 8u ? ch_from8(set.ch) : DRUM_NCH);
        set.fmt = SET_FMT;
    }
    if (set.fmt != SET_FMT || set.bpm < 40u || set.bpm > 250u || set.pat >= SEQ_NPAT ||
        set.ch > DRUM_NCH || set.kit >= NKIT)
        set.fmt = SET_FMT, set.kit = 0, set.bpm = 120, set.pat = 0, set.ch = 0, set.ext = 0, set.cpu_mode = 0;
    if (set.cpu_mode >= CPU_NMODE) set.cpu_mode = 0;
    if (set.rec_free > 1u) set.rec_free = 0;
    snd_load();
    lib_load();
    song_load();
    kit_cur = snd_kc && snd_kc <= NKIT ? snd_kc - 1u : set.kit;   /* (the sound saved is that kit's) */
    drum_init();
    mot_release();
    kit_apply();
    fx_init();
    sound_apply();
    seq.bpm = set.bpm;
    seq.ext = set.ext;
    seq_pattern(set.pat);
    ui_init();
    ui.splash = fm1_ms | 1u;                    /* the opening (ui_fm1.c open_frame) */

    for (;;) {
        platform_poll();
        ui_input();
        if (lib_save_req) {                     /* a sound into the library: in slices, playing or not */
            lib_save_req = 0;
            store_sliced = 1;
            ui_lib_saved(lib_write());
            store_sliced = 0;
        }
        if (save_req && (seq.run || quiet())) {   /* playing: the erases in slices, the sound goes on (store_sliced) */
            store_sliced = seq.run;
            ui_saved(save_all());
            store_sliced = 0;
        }

        midi_in();
        midi_out_due();

        if (fm1_ms - cpu_t >= 1000u) {
            cpu_t = fm1_ms;
            ui.cpu = cpu_us_max * 100u / 5805u;  /* of a block's time (256 frames) */
            cpu_us_max = 0;
        }
        ui_leds();
        idle_ok = 1;
        ui_draw();
        idle_ok = 0;

    }
}
