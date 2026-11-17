/* SPDX-License-Identifier: GPL-3.0-only */
/* Drumtonic's saves over MIDI: a backup on a computer, and a restore from it (tools/fm1_install.py --backup / --restore,
 * the installer page). The computer pulls every record as the flash holds it, or sends them back: the FM-1 asks first
 * on its own screen, writes each record through the usual saves, empties what the backup had empty, then restarts.
 * Included by app_fm1.c (the record table names its blobs and slots).
 *
 * The wire: the updater's SysEx (ota.c), F0 pack7(00 59 CMD L0 L1 L2 BODY CHK) F7, where pack7 packs the bytes as one
 * LSB-first bit stream into 7-bit bytes (ota.c ota_pack7), L the body's length (24 bits, little-endian) and CHK =
 * ~(the sum of the body's bytes) & 0xFF. Every body starts with the tag 'D' 'B'; a message without it, or a command
 * not below, gets no answer. Numbers are little-endian. Send these only to an FM-1 whose identity (cmd 0x11) is
 * FM-1_987: Drumtonic's. One ask at a time: wait for its answer.
 *
 *   0x60 INFO   ask:    'D' 'B'
 *               answer: 'D' 'B', proto (2: backup and restore; 1: backup only), chunk u16 (the most a READ or a WRITE
 *                       carries: 256), gen u32, count u8, then count records of 16 bytes: id u8, flags u8 (bit 0: holds
 *                       a valid record), name 4 ASCII, cap u32 (the area's size), len u32 (the record's bytes with its
 *                       12-byte header; 0: empty, or not valid), 2 bytes 0
 *   0x61 READ   ask:    'D' 'B', id u8, off u32, n u16 (1 .. chunk)
 *               answer: 'D' 'B', id u8, off u32, n u16, status u8, gen u32, then the n bytes if status is 0
 *   0x62 DONE   ask:    'D' 'B', result u8 (0: the backup is complete on the computer, else it failed)
 *               answer: 'D' 'B', 0; the FM-1 says BACKUP / SENT (or BACKUP / FAILED)
 *
 * Restore (proto 2):
 *   0x63 BEGIN  ask:    'D' 'B', count u8 (= INFO's), then count records of 16 bytes, in id order: id u8, 3 bytes 0,
 *                       len u32 (as INFO's: 0 = the backup has none, the FM-1's is emptied), crc u32 (the header's),
 *                       fmt u32 (the record's first 4 bytes after its header; 0 if len < 16)
 *               answer: 'D' 'B', status u8, bad u8 (the record refused, 255: none). Status 0: the FM-1 asks REPLACE ALL
 *                       SAVES? (HOME no, SEL yes; 60 s, then no); 4: the list refused (count, a length past the area or
 *                       past what the FM-1 can hold, a format it does not load); 5: busy (a restore under way)
 *   0x64 STATE  ask:    'D' 'B'
 *               answer: 'D' 'B', state u8, written u8 (bit id: that record written). State: 0 idle, 1 asking, 2 yes
 *                       (send the records), 3 no (HOME, or 60 s without an answer, or ABORT before yes), 4 failed
 *                       (nothing more is written; the FM-1 goes on, its RAM as before: SAVE writes it back), 5 done
 *                       (it restarts)
 *   0x65 WRITE  ask:    'D' 'B', id u8, off u32, n u16 (1 .. chunk), then n bytes: the record as READ gave it, header
 *                       included. Every record whose len is not 0, in id order, each from off 0 to its end, no gaps.
 *                       The last chunk of a record is answered after the record is written (up to ~1 s)
 *               answer: 'D' 'B', id u8, off u32, n u16, status u8, state u8. Status: 0 ok, 2 not the next bytes
 *                       expected, 3 the flash failed, 4 a bad ask, 6 not now (state not 2), 7 the record is not the
 *                       one listed (its header, crc or format): 3 and 7 leave state 4
 *   0x66 COMMIT ask:    'D' 'B'
 *               answer: 'D' 'B', status u8 (0: every record written and the areas the backup had empty emptied; the
 *                       FM-1 restarts 0.3 s later; 2: a record not complete; 3: the flash failed; 6: not now)
 *   0x67 ABORT  ask:    'D' 'B'
 *               answer: 'D' 'B', state u8 (asking: 3, no; after yes: 4, failed)
 *   After yes, 10 s without a message: state 4.
 *
 * READ statuses: 0 ok, 1 no such id, 2 off + n past the record (its len now), 3 the flash failed, 4 a bad ask.
 * A record's bytes are [magic u32][length u32][crc32 u32] then length bytes: crc32 is zlib's, of those bytes. gen counts
 * the saves written since power-on: if a READ's gen is not INFO's, a save came in between, so ask INFO again and pull
 * every record again. Only Drumtonic's own areas are in the table (never 0xFA000.., M-VAVE's data); what is read is
 * what was saved, not the edits not saved yet. A restore cut short leaves the records written so far from the backup
 * and the others as they were: the FM-1 still works, the restore can be done again. */

#define BK_PROTO 2u
#define BK_CHUNK 256u
#define BK_MSG_MAX (7u + 14u + BK_CHUNK)        /* the longest answer, a READ's: 277 B (ota.c's OTA_APP_MAX) */
enum { BK_INFO = 0x60, BK_READ = 0x61, BK_DONE = 0x62, BK_BEGIN = 0x63, BK_STATE = 0x64, BK_WRITE = 0x65, BK_COMMIT = 0x66,
       BK_ABORT = 0x67 };
enum { BK_OK, BK_NO_ID, BK_RANGE, BK_FLASH, BK_BAD, BK_BUSY, BK_NOT_NOW, BK_MISMATCH };
enum { BS_IDLE, BS_ASK, BS_YES, BS_NO, BS_FAIL, BS_DONE };

static const struct { char name[4]; uint8_t kind, i, k; uint32_t fmt[3]; } BK_REC[] = {   /* id = the row; fmt: what its
                                                 * loader takes (0: any) */
    {{'S', 'E', 'T', 'S'}, STORE_SET, 0, 1, {0}},                       /* the settings */
    {{'K', 'I', 'T', '1'}, STORE_BLOB, 0, 1, {SND_FMT, SND_FMT2}},      /* the sound playing and kits 1-8 (snd_save) */
    {{'S', 'O', 'N', 'G'}, STORE_BLOBS, SONG_BLOB, SONG_NBLOB, {SONG_FMT, SONG_FMT2, SONG_FMT1}},   /* patterns, lanes, LFOs */
    {{'L', 'I', 'B', '0'}, STORE_SLOT, LIB_SLOT, 1, {LIB_FMT}},         /* the library: two copies, the newer one loads */
    {{'L', 'I', 'B', '1'}, STORE_SLOT, LIB_SLOT + 1u, 1, {LIB_FMT}},
    {{'K', 'I', 'T', '2'}, STORE_SLOT, SND_HI_SLOT, 1, {SND_FMT_HI}},  /* kits 9-16 */
};
#define BK_NREC (sizeof BK_REC / sizeof BK_REC[0])
_Static_assert(sizeof io_buf >= 4096u - 12u, "a restore's record does not fit io_buf");

static uint8_t bk_st, bk_restart;              /* the restore's state (BS_*); 1: restart once the answer has left */
static uint8_t bk_written, bk_cur, bk_h[12];   /* records written (bit id); the one coming; its header */
static uint32_t bk_t, bk_got;                  /* the question's or the last message's time; bytes of bk_cur so far */
static uint32_t bk_mlen[BK_NREC], bk_mcrc[BK_NREC], bk_mfmt[BK_NREC];   /* the list BEGIN sent */

static uint32_t bk_cap(uint32_t r)
{
    return BK_REC[r].kind == STORE_SLOT ? SLOT_SECT * 0x1000u : BK_REC[r].kind == STORE_BLOBS ? BK_REC[r].k * 0x1000u : 0x1000u;
}
static uint32_t bk_magic(uint32_t r)
{
    return BK_REC[r].kind == STORE_SLOT ? SLOT_MAGIC ^ STORE_MAGIC : BK_REC[r].kind == STORE_BLOBS ? BLOBS_MAGIC ^ STORE_MAGIC : STORE_MAGIC;
}
static uint32_t bk_rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static void bk_wr32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static uint32_t bk_crc(uint32_t c, const uint8_t *p, uint32_t n)   /* zlib's crc32, going on from c (start: 0) */
{
    uint32_t b;
    c = ~c;
    while (n--) {
        c ^= *p++;
        for (b = 0; b < 8u; b++) c = c & 1u ? (c >> 1) ^ 0xEDB88320u : c >> 1;
    }
    return ~c;
}

/* record r's length as stored, its header included; 0: empty or not valid. crc: its bytes checked too (scratch: 256 B) */
static uint32_t bk_len(uint32_t r, uint8_t *scratch, uint32_t crc)
{
    uint8_t h[12];
    uint32_t n, at, c = 0;
    if (store_raw_read(BK_REC[r].kind, BK_REC[r].i, BK_REC[r].k, 0, h, sizeof h) || bk_rd32(h) != bk_magic(r))
        return 0;
    n = bk_rd32(h + 4);
    if (n > bk_cap(r) - 12u) return 0;          /* (a length past the area: never read beyond it) */
    if (!crc) return n + 12u;
    for (at = 0; at < n; at += BK_CHUNK) {
        uint32_t k = n - at < BK_CHUNK ? n - at : BK_CHUNK;
        if (store_raw_read(BK_REC[r].kind, BK_REC[r].i, BK_REC[r].k, 12u + at, scratch, k)) return 0;
        c = bk_crc(c, scratch, k);
    }
    return c == bk_rd32(h + 8) ? n + 12u : 0u;
}

static uint32_t bk_answer(uint8_t *o, uint32_t cmd, uint32_t n)   /* the body is at o + 6: the frame around it */
{
    uint32_t s = 0, i;
    o[0] = 0; o[1] = 0x59; o[2] = (uint8_t)cmd;
    o[3] = (uint8_t)n; o[4] = (uint8_t)(n >> 8); o[5] = 0;
    for (i = 0; i < n; i++) s += o[6 + i];
    o[6 + n] = (uint8_t)~s;
    return 7u + n;
}

static void ui_backup_done(uint32_t ok);

/* ---- the restore */
static void bk_say(const char *b, uint32_t grey) { big("RESTORE", b, grey); }
static void bk_fail(void) { bk_st = BS_FAIL; bk_say("FAILED", 1); }
static uint32_t bk_next(uint32_t r)             /* the next record to come, from r on (BK_NREC: none) */
{
    while (r < BK_NREC && (!bk_mlen[r] || (bk_written >> r & 1u))) r++;
    return r;
}
static void bk_yes(void)                        /* SEL on REPLACE ALL SAVES? (ui_fm1.c ask_yes) */
{
    if (bk_st != BS_ASK) return;
    bk_st = BS_YES; bk_t = fm1_ms;
    bk_written = 0; bk_got = 0; bk_cur = (uint8_t)bk_next(0);
    transport(0);                               /* (the sound stops at yes, not at the question: a no costs nothing) */
    save_req = 0; lib_save_req = 0;             /* (nothing from RAM over what comes) */
    ui.view = V_LIVE; ui.help = 0;              /* (a page where the big message shows) */
    bk_say("WRITING", 0);
    ui.big_until = fm1_ms + 600000u;            /* (until the restart, or FAILED over it) */
}
static uint32_t bk_busy(void) { return bk_st == BS_YES || bk_st == BS_DONE; }   /* (app_fm1.c: no input, no saves) */
static void bk_poll(void)                       /* the main loop, once a pass */
{
    if (bk_st == BS_ASK && ui.ask != A_RESTORE) { bk_st = BS_NO; bk_say("CANCELLED", 1); }   /* (HOME) */
    else if (bk_st == BS_ASK && fm1_ms - bk_t >= 60000u) { ui.ask = 0; bk_st = BS_NO; bk_say("CANCELLED", 1); }
    else if (bk_st == BS_YES && fm1_ms - bk_t >= 10000u) bk_fail();   /* (the computer went silent) */
    else if (bk_st == BS_DONE && bk_restart && fm1_ms - bk_t >= 300u) { bk_restart = 0; plat_restart(); }
}
static uint32_t bk_save(uint32_t r, const uint8_t *p, uint32_t n)   /* a record written through the usual saves */
{
    int rc;
    store_sliced = 1;
    rc = BK_REC[r].kind == STORE_SET ? settings_save(p, n) : BK_REC[r].kind == STORE_BLOB ? blob_save(BK_REC[r].i, p, n) :
         BK_REC[r].kind == STORE_BLOBS ? blobs_save(BK_REC[r].i, BK_REC[r].k, p, n) : slot_save(BK_REC[r].i, p, n);
    store_sliced = 0;
    return rc ? BK_FLASH : BK_OK;
}
static uint32_t bk_begin(const uint8_t *m, uint32_t len, uint8_t *bad)
{
    uint32_t r, j;
    if (bk_st == BS_YES || bk_st == BS_DONE || bk_st == BS_ASK) return BK_BUSY;
    if (len != 3u + 16u * BK_NREC || m[8] != BK_NREC) return BK_BAD;
    for (r = 0; r < BK_NREC; r++) {
        const uint8_t *e = m + 9 + 16u * r;
        uint32_t n = bk_rd32(e + 4), f = bk_rd32(e + 12), ok = !BK_REC[r].fmt[0];
        *bad = (uint8_t)r;
        if (e[0] != r || (n && (n < 12u || n > bk_cap(r) || n - 12u > sizeof io_buf))) return BK_BAD;
        for (j = 0; j < 3u; j++) ok |= BK_REC[r].fmt[j] && f == BK_REC[r].fmt[j];
        if (n >= 16u && !ok) return BK_BAD;
        if (n && n < 16u && BK_REC[r].fmt[0]) return BK_BAD;
        bk_mlen[r] = n; bk_mcrc[r] = bk_rd32(e + 8); bk_mfmt[r] = f;
    }
    *bad = 255;
    bk_st = BS_ASK; bk_t = fm1_ms; bk_written = 0;
    if (ui.splash) { ui.splash = 0; lcd_fill(0, 0, 240, 240, B_BG); ui.h_top = ui.h_mid = ui.h_bot = ~0u; }   /* (as a press) */
    ask(A_RESTORE, 0, 0, "REPLACE ALL SAVES?");
    return BK_OK;
}
static uint32_t bk_write(uint32_t r, uint32_t off, const uint8_t *d, uint32_t k)
{
    uint32_t j;
    if (bk_st != BS_YES) return BK_NOT_NOW;
    if (r != bk_cur || r >= BK_NREC || off != bk_got || k > bk_mlen[r] - off) return BK_RANGE;
    for (j = 0; j < k; j++) {
        uint32_t at = off + j;
        if (at < 12u) bk_h[at] = d[j];
        else io_buf.song[at - 12u] = d[j];
    }
    bk_got += k;
    if (bk_got < bk_mlen[r]) return BK_OK;
    if (bk_rd32(bk_h) != bk_magic(r) || bk_rd32(bk_h + 4) + 12u != bk_mlen[r] || bk_rd32(bk_h + 8) != bk_mcrc[r] ||
        bk_crc(0, io_buf.song, bk_mlen[r] - 12u) != bk_mcrc[r] ||
        (bk_mlen[r] >= 16u && bk_rd32(io_buf.song) != bk_mfmt[r])) { bk_fail(); return BK_MISMATCH; }
    if (bk_save(r, io_buf.song, bk_mlen[r] - 12u)) { bk_fail(); return BK_FLASH; }
    bk_written |= (uint8_t)(1u << r);
    bk_cur = (uint8_t)bk_next(r + 1u); bk_got = 0;
    return BK_OK;
}
static uint32_t bk_commit(void)
{
    uint32_t r;
    if (bk_st != BS_YES) return BK_NOT_NOW;
    if (bk_next(0) < BK_NREC) return BK_RANGE;
    for (r = 0; r < BK_NREC; r++)               /* empty in the backup: empty here too */
        if (!bk_mlen[r] && bk_len(r, 0, 0)) {
            store_sliced = 1;
            if (store_raw_clear(BK_REC[r].kind, BK_REC[r].i, BK_REC[r].k)) { store_sliced = 0; bk_fail(); return BK_FLASH; }
            store_sliced = 0;
        }
    bk_st = BS_DONE; bk_restart = 1; bk_t = fm1_ms;
    return BK_OK;
}

/* a message for the app (ota.c ota_service, decoded): its answer in out (out may be m: the ask is read first), its
 * length; 0: not ours, no answer */
static uint32_t app_sysex(const uint8_t *m, uint32_t n, uint8_t *out, uint32_t max)
{
    uint32_t cmd, len, s = 0, i, r, off = 0, k = 0, res = 0, st = BK_OK;
    uint8_t *b = out + 6, bad = 255;
    if (n < 9u || max < BK_MSG_MAX || m[0] != 0 || m[1] != 0x59) return 0;
    cmd = m[2];
    len = (uint32_t)m[3] | (uint32_t)m[4] << 8 | (uint32_t)m[5] << 16;
    if (len + 7u != n || cmd < BK_INFO || cmd > BK_ABORT || m[6] != 'D' || m[7] != 'B') return 0;
    for (i = 0; i < len; i++) s += m[6 + i];
    if ((uint8_t)~s != m[n - 1u]) return 0;
    if (bk_st == BS_YES) bk_t = fm1_ms;         /* (the computer is there) */
    r = len >= 3u ? m[8] : 0u;                  /* (the ask's fields, before out is written over it) */
    if (cmd == BK_READ || cmd == BK_WRITE) {
        if (cmd == BK_READ ? len != 9u : len < 9u) st = BK_BAD;
        else { off = bk_rd32(m + 9); k = (uint32_t)m[13] | (uint32_t)m[14] << 8; }
        if (cmd == BK_WRITE && st == BK_OK && (k == 0u || k > BK_CHUNK || len != 9u + k)) st = BK_BAD;
        if (cmd == BK_WRITE && st == BK_OK) st = bk_write(r, off, m + 15, k);   /* (before out is written) */
    }
    if (cmd == BK_DONE) res = len == 3u ? m[8] : 1u;
    if (cmd == BK_BEGIN) st = bk_begin(m, len, &bad);
    b[0] = 'D'; b[1] = 'B';
    switch (cmd) {
    case BK_INFO: {
        uint32_t ln[BK_NREC];
        for (r = 0; r < BK_NREC; r++) ln[r] = bk_len(r, out + BK_MSG_MAX - BK_CHUNK, 1);   /* (out's tail: scratch) */
        b[2] = BK_PROTO; b[3] = (uint8_t)BK_CHUNK; b[4] = (uint8_t)(BK_CHUNK >> 8);
        bk_wr32(b + 5, store_gen);
        b[9] = (uint8_t)BK_NREC;
        for (r = 0; r < BK_NREC; r++) {
            uint8_t *e = b + 10 + 16u * r;
            e[0] = (uint8_t)r; e[1] = ln[r] ? 1u : 0u;
            for (i = 0; i < 4u; i++) e[2 + i] = (uint8_t)BK_REC[r].name[i];
            bk_wr32(e + 6, bk_cap(r)); bk_wr32(e + 10, ln[r]);
            e[14] = e[15] = 0;
        }
        return bk_answer(out, cmd, 10u + 16u * BK_NREC);
    }
    case BK_DONE:
        b[2] = 0;
        ui_backup_done(res == 0u);
        return bk_answer(out, cmd, 3u);
    case BK_BEGIN:
        b[2] = (uint8_t)st; b[3] = bad;
        return bk_answer(out, cmd, 4u);
    case BK_STATE:
        b[2] = bk_st; b[3] = bk_written;
        return bk_answer(out, cmd, 4u);
    case BK_WRITE:
        b[2] = (uint8_t)r; bk_wr32(b + 3, off); b[7] = (uint8_t)k; b[8] = (uint8_t)(k >> 8);
        b[9] = (uint8_t)st; b[10] = bk_st;
        return bk_answer(out, cmd, 11u);
    case BK_COMMIT:
        b[2] = (uint8_t)bk_commit();
        return bk_answer(out, cmd, 3u);
    case BK_ABORT:
        if (bk_st == BS_ASK) { ui.ask = 0; bk_st = BS_NO; bk_say("CANCELLED", 1); }
        else if (bk_st == BS_YES) bk_fail();
        b[2] = bk_st;
        return bk_answer(out, cmd, 3u);
    default: break;
    }
    if (st == BK_OK && (k == 0u || k > BK_CHUNK)) st = BK_BAD;   /* READ */
    else if (st == BK_OK && r >= BK_NREC) st = BK_NO_ID;
    else if (st == BK_OK) {
        uint32_t ln = bk_len(r, 0, 0);
        if (off > ln || k > ln - off) st = BK_RANGE;
        else if (store_raw_read(BK_REC[r].kind, BK_REC[r].i, BK_REC[r].k, off, b + 14, k)) st = BK_FLASH;
    }
    b[2] = (uint8_t)r; bk_wr32(b + 3, off); b[7] = (uint8_t)k; b[8] = (uint8_t)(k >> 8);
    b[9] = (uint8_t)st; bk_wr32(b + 10, store_gen);
    return bk_answer(out, cmd, 14u + (st == BK_OK ? k : 0u));
}

#ifdef DINGHY_HOST
static int bk_area(uint32_t id, uint32_t *kind, uint32_t *i, uint32_t *k)   /* (tests/host.c: what record id holds) */
{
    if (id >= BK_NREC) return -1;
    *kind = BK_REC[id].kind; *i = BK_REC[id].i; *k = BK_REC[id].k;
    return 0;
}
#endif
