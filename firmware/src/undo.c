/* SPDX-License-Identifier: GPL-3.0-only */
/* The FM-1's undo, one level (FM1-INTERFACCIA.md): HOME + OCT- goes back to the state before the last change, HOME +
 * OCT+ to the state the last OCT- left; each keeps the state it leaves for the other, so a change made after an undo
 * does not lose what was undone. What it covers: the patterns (steps, lengths, rate, swing), the lanes (motion, p-locks,
 * conditions, micro timing), the LFOs, the kit playing (sounds, names) and its sends and effects (kitx). Not the tempo,
 * mute, solo, the settings, nor anything written to the flash (SAVE, the kits' slots, the library).
 *
 * Three copies in the pool: OCT-'s, OCT+'s and the work one (the state before the change in progress). A change is
 * found by comparing: an input event that starts a gesture copies the state into the work copy; if the state then
 * differs from it, the work copy becomes OCT-'s. A gesture: one button press, a knob or an encoder until 1 s without
 * a click, the keys until 2 s without a tap, a REC pass from on to off; a paste, a clear, a sound or a kit loaded each
 * start their own (undo_begin). Included by app_fm1.c before ui_fm1.c. */

typedef struct {
    seq_song_t song;
    lane_t lanes[NLANE];
    lfo_t lfos[SEQ_NPAT][NLFO];
    drum_kit_t kit;
    uint8_t x[SND_NX];
    uint8_t kc;                                 /* (which kit it is: kit_cur) */
} undo_st_t;
static undo_st_t undo_st[3] POOL;               /* ~19.5 KB each */

#define UNDO_LBL 8u
enum { UK_NONE, UK_REC = 1, UK_KEYS, UK_NAMED, UK_KNOB = 0x10, UK_ENC = 0x20, UK_BTN = 0x40 };
#define UNDO_PRESS 0xFFFFFFFFu                  /* gap: the same press goes on (a button's release) */
static struct {
    int8_t back, fwd, work;                     /* the copies OCT- and OCT+ go to (-1: none); the state before the change */
    uint8_t open, done;                         /* a gesture going on (its state before: work); done: it changed something */
    uint32_t kind, t, chk_t;
    char lbl[3][UNDO_LBL], now[UNDO_LBL];       /* each copy's: what going to it undoes / redoes; the gesture's */
} undo = {-1, -1, -1};

static void undo_take(uint32_t i)
{
    undo_st_t *d = &undo_st[i % 3u];
    memcpy(&d->song, &seq_song, sizeof seq_song);
    memcpy(d->lanes, lanes, sizeof lanes);
    memcpy(d->lfos, lfos, sizeof lfos);
    d->kit = kit;
    memcpy(d->x, kitx, SND_NX);
    d->kc = kit_cur;
}
static uint32_t undo_same(uint32_t i)
{
    const undo_st_t *d = &undo_st[i % 3u];
    return !memcmp(&d->song, &seq_song, sizeof seq_song) && !memcmp(d->lanes, lanes, sizeof lanes) &&
           !memcmp(d->lfos, lfos, sizeof lfos) && !memcmp(&d->kit, &kit, sizeof kit) && !memcmp(d->x, kitx, SND_NX) && d->kc == kit_cur;
}
static void undo_put(uint32_t i)                /* copy i becomes the state: as the main loop's edits do it */
{
    const undo_st_t *d = &undo_st[i % 3u];
    uint32_t k;
    memcpy(&seq_song, &d->song, sizeof seq_song);
    for (k = 0; k < NLANE; k++)                 /* a lane changed: off, written, a new gen, then on (as mot_alloc) */
        if (memcmp(&lanes[k], &d->lanes[k], sizeof lanes[k])) {
            lane_t l = d->lanes[k];
            uint8_t used = l.used;
            l.used = 0;
            l.gen = (uint8_t)(lanes[k].gen + 1u);
            lanes[k].used = 0;
            __asm__ volatile("" ::: "memory");
            lanes[k] = l;
            __asm__ volatile("" ::: "memory");
            lanes[k].used = used;
        }
    memcpy(lfos, d->lfos, sizeof lfos);
    if (d->kc == kit_cur) {                     /* the kit playing: as it was */
        kit = d->kit;
        memcpy(kitx, d->x, SND_NX);
    } else if (d->kc < NKIT) {                  /* another kit's state: into its working copy (app_fm1.c; the
                                                 * pattern's kit, restored with the patterns, plays: kit_follow) */
        snd_t s;
        s.k = d->kit; memcpy(s.x, d->x, SND_NX);
        kw[d->kc] = s;
        if (memcmp(&s, &kits[d->kc], sizeof s)) kw_on |= (uint8_t)(1u << d->kc);
        else kw_on &= (uint8_t)~(1u << d->kc);
    }
    orig.on = 0;                                /* (PRESETS' original: of a part that may be gone) */
    kit_edited = 1;
    sound_apply();
}
static void undo_rebase(void)                   /* the kit changed by itself (a pattern's): not a change to undo (a
                                                 * gesture that changed something already: its state before stays) */
{
    if (undo.open && !undo.done && undo.work >= 0) {
        undo_st_t *d = &undo_st[undo.work % 3];
        d->kit = kit; memcpy(d->x, kitx, SND_NX); d->kc = kit_cur;
    }
}
static int8_t undo_spare(void)                  /* a copy neither key goes to */
{
    int8_t i;
    for (i = 0; i < 3; i++) if (i != undo.back && i != undo.fwd) return i;
    return 0;
}
static void undo_commit(void)                   /* the gesture changed something: its state before is OCT-'s */
{
    undo.back = undo.work;
    memcpy(undo.lbl[undo.back], undo.now, UNDO_LBL);
    undo.done = 1;
}
static void undo_close(void)
{
    if (undo.open && !undo.done && !undo_same((uint32_t)undo.work)) undo_commit();
    undo.open = 0;
}
static void undo_open(uint32_t kind, const char *lbl)
{
    uint32_t i;
    undo.work = undo_spare();
    undo_take((uint32_t)undo.work);
    undo.open = 1; undo.done = 0;
    undo.kind = kind; undo.t = undo.chk_t = fm1_ms;
    for (i = 0; i + 1u < UNDO_LBL && lbl[i]; i++) undo.now[i] = lbl[i];
    undo.now[i] = 0;
}
/* an input event, before it is handled: it goes on the gesture of its kind (within gap ms of the last), or starts one */
static void undo_event(uint32_t kind, uint32_t gap, const char *lbl)
{
    if (rec) kind = UK_REC, lbl = "REC";        /* a REC pass: everything until REC goes off */
    if (undo.open && kind == undo.kind && (kind == UK_REC || (gap && fm1_ms - undo.t <= gap))) { undo.t = fm1_ms; return; }
    undo_close();
    undo_open(kind, lbl);
}
static void undo_begin(const char *lbl)         /* a change of its own (a paste, a clear, a sound): before it is made */
{
    undo_close();
    undo_open(UK_NAMED, lbl);
}
static void undo_check(uint32_t now)            /* after the events (now), and while REC records: changed yet? */
{
    if (!undo.open || undo.done) return;
    if (!now && !(rec && fm1_ms - undo.chk_t >= 100u)) return;
    undo.chk_t = fm1_ms;
    if (!undo_same((uint32_t)undo.work)) undo_commit();
}
/* HOME + OCT- (d < 0) / OCT+: 0 done, its label in lbl; -1 nothing there */
static int undo_go(int32_t d, char *lbl)
{
    int8_t *to = d < 0 ? &undo.back : &undo.fwd, *other = d < 0 ? &undo.fwd : &undo.back, w;
    undo_close();
    if (*to < 0) return -1;
    w = undo_spare();                           /* (with both kept: the third) the state left goes there */
    undo_take((uint32_t)w);
    undo_put((uint32_t)*to);
    memcpy(undo.lbl[w], undo.lbl[*to], UNDO_LBL);
    memcpy(lbl, undo.lbl[w], UNDO_LBL);
    *other = w;                                 /* the state left: the other key's (what it held before is gone) */
    *to = -1;
    return 0;
}
