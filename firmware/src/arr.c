/* SPDX-License-Identifier: GPL-3.0-only */
/* The songs (song mode, docs/dev/SONG-MODE.md 9-11, 2026-11-17): on the screen SONG; "song" in the code is the 32
 * patterns (seq_song), so these are "arr". A song is a list of elements: an element is a chain of 1..16 patterns
 * played rep times (x1..x64) with its mutes; up to 64 patterns in all its elements, then its end (LOOP / STOP) and its
 * tempo. Its slots are the play list's (seq_fm1.c seq_slot_t, with the mutes): bit SEQ_EL of pat on an element's first
 * slot, which also holds its times and its mutes. Saved by SAVE with the settings (app_fm1.c sets_save: the settings'
 * 8 bytes, ARR_FMT, the 8 songs; the backup's SETS record carries them as bytes). Main only. */
#define ARR_NSONG 8u
#define ARR_NSLOT SEQ_NSLOT
#define ARR_ELMAX SEQ_NCHAIN
enum { ARR_LOOP, ARR_STOP };
typedef struct { uint8_t pat, rep; uint16_t mute; } arr_slot_t;   /* pat: the pattern (bits 0-4) | SEQ_EL; rep, mute on an
                                                 * element's first slot: its times 1..64, its mutes (bit c: channel c) */
typedef struct { uint8_t n, end, bpm, rsv; arr_slot_t s[ARR_NSLOT]; } arr_t;   /* slots used; ARR_LOOP / ARR_STOP; its
                                                 * tempo (0: none yet, it takes the one playing) */
static arr_t arr[ARR_NSONG];
static uint8_t arr_dirty;                       /* a song changed since the last SAVE or load (SAVE's light) */
#define ARR_FMT 0x31525241u                     /* "ARR1" */

static uint32_t arr_ok(const arr_t *a)          /* a song that makes sense */
{
    uint32_t i, ok = a->n <= ARR_NSLOT && a->end <= ARR_STOP && (!a->bpm || (a->bpm >= 40u && a->bpm <= 250u)) &&
                     (!a->n || (a->s[0].pat & SEQ_EL));
    for (i = 0; ok && i < a->n; i++) {
        uint32_t run = 1;                       /* (an element: 1..64 times, 1..16 patterns) */
        if (!(a->s[i].pat & SEQ_EL)) continue;
        if (a->s[i].rep < 1u || a->s[i].rep > 64u || (a->s[i].pat & 0x60u)) ok = 0;
        while (i + run < a->n && !(a->s[i + run].pat & SEQ_EL)) run++;
        if (run > ARR_ELMAX) ok = 0;
    }
    return ok;
}
static void arr_check(void)                     /* the songs loaded: one that does not make sense is emptied */
{
    uint32_t k;
    for (k = 0; k < ARR_NSONG; k++) if (!arr_ok(&arr[k])) memset(&arr[k], 0, sizeof arr[k]);
}

static uint32_t arr_nel(const arr_t *a)         /* its elements */
{
    uint32_t i, n = 0;
    for (i = 0; i < a->n; i++) n += (a->s[i].pat & SEQ_EL) != 0;
    return n;
}
static uint32_t arr_first(const arr_t *a, uint32_t k)   /* element k's first slot (k = its elements: a->n) */
{
    uint32_t i, e = 0;
    for (i = 0; i < a->n; i++)
        if ((a->s[i].pat & SEQ_EL) && e++ == k) return i;
    return a->n;
}
static uint32_t arr_len(const arr_t *a, uint32_t k)     /* element k's patterns */
{
    uint32_t f = arr_first(a, k), i = f + 1u;
    if (f >= a->n) return 0;
    while (i < a->n && !(a->s[i].pat & SEQ_EL)) i++;
    return i - f;
}
static uint32_t arr_pat(const arr_t *a, uint32_t slot) { return a->s[slot].pat & (SEQ_NPAT - 1u); }
