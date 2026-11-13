/* SPDX-License-Identifier: GPL-3.0-only */
/* Drumtonic's app on the SMK-37 (drumtonic.c includes it): the 16-step sequencer with 8 patterns (seq.c), the
 * settings, the screen (header, channel, the knobs of the page, the 16 steps), the buttons and the main loop. The
 * panel (ui_smk.c) gives the keys, the knobs on the pages and the LEDs.
 *
 *   SEQ PLAY / PLAY   play / stop          SEQ REC / REC   record the hits into the playing pattern
 *   SEQ               STEP / PLAY mode     OCT- OCT+       the pattern
 *   ARP               mute the channel     NOTE REPEAT / LFO   solo the channel
 *   SCALE / SEL       clear the channel in this pattern     CHORD / ENV   tap tempo
 *   FX, PARA / EDIT   copy, paste a pattern                 GLOBE / GLO   internal / external MIDI clock
 *   PATCH / HOME      the next factory kit                  BT / SAVE     save kit and patterns (stopped) */

/* ---- what is kept: the settings (by themselves, when stopped), the kit and the patterns (BT / SAVE) */
enum { MODE_STEP, MODE_PLAY };
enum { Q_TEMPO = 100, Q_SWING, Q_NONE = 0xFF };   /* page slots that are not a channel's knob */
static struct { uint8_t kit, bpm, swing, pat, ch, page, ext, mode, sens; } set = {0, 120, 50, 0, 0, 0, 0, MODE_STEP, 0};
static uint8_t clip[DRUM_NCH][SEQ_NSTEP], clip_ok;
static uint32_t dirty, t_change;                /* the settings changed at t_change: saved 2 s later, when stopped */
static void touched(void) { dirty = 1; t_change = fm1_ms; }

static void demo_song(void)                     /* patterns A and B: a groove to start from */
{
    static const char *const A[DRUM_NCH] = {
        "X---x-----x-x---", "----X-------X---", "------------x---", "x-x-x-x-x-x-x---",
        "--------------X-", "----------------", "---x------x-----", "X---------------"};
    static const char *const B[DRUM_NCH] = {
        "X---x-----x-x--x", "----X--x-x--X-xx", "----x-------x---", "x-xxx-x-x-xxx-x-",
        "--------------X-", "-----------x--x-", "---x--x---x-----", "----------------"};
    uint32_t c, s;
    for (c = 0; c < DRUM_NCH; c++)
        for (s = 0; s < SEQ_NSTEP; s++) {
            seq_song.s[0][c][s] = A[c][s] == 'X' ? STEP_ACC : A[c][s] == 'x' ? STEP_ON : STEP_OFF;
            seq_song.s[1][c][s] = B[c][s] == 'X' ? STEP_ACC : B[c][s] == 'x' ? STEP_ON : STEP_OFF;
        }
}

static uint32_t ch_used(uint32_t pat, uint32_t ch)   /* the channel has a step in the pattern */
{
    uint32_t s, u = 0;
    for (s = 0; s < SEQ_NSTEP; s++) u |= seq_song.s[pat][ch][s];
    return u;
}
static uint32_t pat_used(uint32_t pat)
{
    uint32_t c, u = 0;
    for (c = 0; c < DRUM_NCH; c++) u |= ch_used(pat, c);
    return u;
}

static void step_tap(uint32_t s)                /* a step's key or pad: off -> on -> accent -> off */
{
    uint8_t *v = &seq_song.s[seq.pat][set.ch][s % SEQ_NSTEP];
    *v = (uint8_t)((*v + 1u) % 3u);
}

static void pattern_pick(uint32_t p)
{
    set.pat = (uint8_t)(p % SEQ_NPAT);
    seq_pattern(set.pat);
    touched();
}

/* ---- the panel (keys, pads, knobs, LEDs, the page table). It gives NPAGE, PAGE_NAME, NKNOB (knobs a
 * page), PAGE_PARAM[page][knob] (a channel's parameter, Q_TEMPO, Q_SWING or Q_NONE), PANEL_NBTN, PANEL_NSENS,
 * PANEL_PASTE_HINT and panel_init(), panel_input(), panel_buttons(edges), panel_leds() */
static void knob(uint32_t k, int32_t e);
#include "ui_smk.c"

/* ---- the screen: header, channel, the knobs of the page, the 16 steps */
static uint32_t knob_touched = 99;

static uint32_t slot_value(uint32_t q)          /* a page slot's value, 0..127 for the bar */
{
    return q == Q_TEMPO ? (set.bpm - 40u) * 127u / 210u : q == Q_SWING ? (set.swing - 50u) * 127u / 25u
                                                                       : kit.pat[set.ch].p[q];
}

static void param_text(char *s, uint32_t p, uint32_t v)
{
    static const char *const WAVE[3] = {"SIN", "TRI", "SAW"}, *const PMOD[3] = {"DEC", "LFO", "RND"},
                      *const FILT[3] = {"LP", "BP", "HP"}, *const ENV[3] = {"EXP", "LIN", "CLAP"};
    switch (p) {
    case Q_TEMPO: catn(s, set.bpm); break;
    case Q_SWING: cat(catn(s, set.swing), "%"); break;
    case P_WAVE: cat(s, WAVE[knob_mode(v)]); break;
    case P_PMODE: cat(s, PMOD[knob_mode(v)]); break;
    case P_NFILT: cat(s, FILT[knob_mode(v)]); break;
    case P_NENV: cat(s, ENV[knob_mode(v)]); break;
    case P_NSTEREO: cat(s, v >= 64u ? "ON" : "OFF"); break;
    case P_CHOKE: if (v >> 5) catn(cat(s, "G"), (int32_t)(v >> 5)); else cat(s, "-"); break;
    case P_PAMT: case P_EQG: case P_PAN: catn(s, (int32_t)v - 64); break;
    default: catn(s, (int32_t)v); break;
    }
}

static void draw_header(void)
{
    char s[48], *p = s;
    *p++ = (char)('A' + seq.pat);
    if (seq.run && seq.next != seq.pat) { *p++ = '>'; *p++ = (char)('A' + seq.next); }
    p = catn(cat(p, " "), set.bpm);              /* (compact: PLAY REC EXT all fit) */
    p = catn(cat(p, " BPM SW "), set.swing);
    p = cat(p, seq.run ? " PLAY" : " STOP");
    if (rec) p = cat(p, " REC");
    if (set.ext) p = cat(p, " EXT");
    draw_text_box(0, 2, 240, &FONT_S, s, seq.run ? C_WHITE : C_TXT, 0);
}

static void draw_status(uint32_t cpu)           /* the line under the header: a message, or the kit and the CPU */
{
    char s[40], *p = s;
    if (fm1_ms < msg_until) {
        draw_text_box(0, 20, 240, &FONT_S, msg, RGB(255, 220, 80), 0);
        return;
    }
    p = cat(cat(p, kit.name), kit_edited ? "*" : "");
#ifndef DINGHY_HOST
    p = catn(cat(p, "   CPU "), (int32_t)cpu);
    p = cat(p, "%");
#else
    (void)cpu;
#endif
    draw_text_box(0, 20, 240, &FONT_S, s, C_GREY, 0);
}

static void draw_channel(void)
{
    char s[16], *p = s;
    uint32_t c = set.ch;
    p = catn(p, (int32_t)c + 1);
    *p++ = ' ';
    cat(p, kit.ch[c]);
    draw_text_box(0, 40, 140, &FONT_L, s, ch_colour(c), 0);
    p = cat(s, PAGE_NAME[set.page]);
    if (seq.mute >> c & 1u) p = cat(p, " M");
    if (seq.solo >> c & 1u) p = cat(p, " S");
    draw_text_box(140, 48, 100, &FONT_S, s, C_TXT, 2);
}

static void draw_params(void)                   /* the page's knobs in 2 rows: the name, the value, a bar */
{
    uint32_t k, cols = NKNOB / 2u, w = 240u / cols;
    cv_begin(240, 112, C_BLACK);
    for (k = 0; k < NKNOB; k++) {
        uint32_t q = PAGE_PARAM[set.page][k], v;
        int32_t x = (int32_t)((k % cols) * w), y = (int32_t)(k / cols) * 56;
        const char *name = q == Q_TEMPO ? "TEMPO" : q == Q_SWING ? "SWING" : q < DRUM_NP ? PARAM_NAME[q] : "";
        char s[12] = "";
        uint16_t c = k == knob_touched ? C_WHITE : C_TXT;
        if (q == Q_NONE) continue;
        v = slot_value(q);
        cv_text(x + ((int32_t)w - text_w(&FONT_S, name)) / 2, y + 2, &FONT_S, name, C_GREY);
        param_text(s, q, v);
        cv_text(x + ((int32_t)w - text_w(&FONT_S, s)) / 2, y + 20, &FONT_S, s, c);
        cv_rect(x + 6, y + 42, (int32_t)w - 12, 4, RGB(40, 40, 40));
        cv_rect(x + 6, y + 42, (int32_t)(v * (w - 12u) / 127u), 4, k == knob_touched ? C_WHITE : ch_colour(set.ch));
    }
    cv_blit(0, 76);
}

static void draw_steps(uint32_t play)           /* steps 1-8 above, 9-16 below; play = the step playing + 1 */
{
    uint32_t s;
    cv_begin(240, 48, C_BLACK);
    for (s = 0; s < SEQ_NSTEP; s++) {
        uint32_t v = seq_song.s[seq.pat][set.ch][s];
        int32_t x = 4 + (int32_t)(s % 8u) * 29, y = (int32_t)(s / 8u) * 24;
        uint16_t c = v == STEP_ACC ? C_WHITE : v ? ch_colour(set.ch) : RGB(45, 45, 45);
        if (play == s + 1u) cv_rect(x - 2, y, 29, 22, RGB(255, 220, 80));
        cv_rect(x, y + 2, 25, 18, c);
    }
    cv_blit(0, 190);
}

static void save_all(void)                      /* BT / SAVE: once stopped and silent */
{
    save_req = 0;
    if (blob_save(0, &kit, sizeof kit) || blob_save(1, &seq_song, sizeof seq_song)) { say("SAVE FAILED"); return; }
    kit_edited = 0;
    say("SAVED");
}

static void knob(uint32_t k, int32_t e)         /* knob k of the page turned e detents */
{
    uint32_t p = PAGE_PARAM[set.page][k % NKNOB];
    int32_t v;
    if (p == Q_NONE) return;
    knob_touched = k;
    if (p == Q_TEMPO || p == Q_SWING) {
        v = (p == Q_TEMPO ? set.bpm : set.swing) + e;
        if (p == Q_TEMPO) { set.bpm = (uint8_t)(v < 40 ? 40 : v > 250 ? 250 : v); seq.bpm = set.bpm; }
        else { set.swing = (uint8_t)(v < 50 ? 50 : v > 75 ? 75 : v); seq.swing = set.swing; }
        touched();
        return;
    }
    v = kit.pat[set.ch].p[p];
    if (p == P_WAVE || p == P_PMODE || p == P_NFILT || p == P_NENV) v = (int32_t)knob_mode((uint32_t)v) * 43 + 21 + e * 43;
    else if (p == P_NSTEREO) v = e > 0 ? 127 : 0;
    else if (p == P_CHOKE) v = (v >> 5) * 32 + e * 32;
    else v += e > 1 || e < -1 ? e * 3 : e;      /* (turned fast: bigger steps) */
    v = v < 0 ? 0 : v > 127 ? 127 : v;
    kit.pat[set.ch].p[p] = (uint8_t)v;
    drum_set_patch(set.ch, &kit.pat[set.ch]);
    kit_edited = 1;
}

static void buttons(uint32_t b)                 /* the buttons both panels have, under their FM-1 names */
{
    static uint32_t taps, tap_ms;
    uint32_t k;
    if (b) touched();
    if (b >> BTN_PLAY & 1u) {
        if (set.ext) say("EXT CLOCK: THE DAW PLAYS");
        else transport(!seq.run);
    }
    if (b >> BTN_REC & 1u) rec = !rec;
    if (b >> BTN_SEQ & 1u) set.mode = set.mode == MODE_STEP ? MODE_PLAY : MODE_STEP;
    if (b >> BTN_OCT_DOWN & 1u) pattern_pick(set.pat + SEQ_NPAT - 1u);
    if (b >> BTN_OCT_UP & 1u) pattern_pick(set.pat + 1u);
    if (b >> BTN_ARP & 1u) seq.mute ^= (uint8_t)(1u << set.ch);
    if (b >> BTN_LFO & 1u) seq.solo ^= (uint8_t)(1u << set.ch);
    if (b >> BTN_SEL & 1u) {
        for (k = 0; k < SEQ_NSTEP; k++) seq_song.s[seq.pat][set.ch][k] = STEP_OFF;
        say("CHANNEL CLEARED");
    }
    if (b >> BTN_ENV & 1u) {                    /* tap tempo: the average of the last taps */
        uint32_t dt = fm1_ms - tap_ms;
        tap_ms = fm1_ms;
        if (dt > 250u && dt < 1500u) {
            taps = taps ? (taps * 3u + dt) / 4u : dt;
            set.bpm = (uint8_t)(60000u / taps);
            seq.bpm = set.bpm;
        } else {
            taps = 0;
        }
    }
    if (b >> BTN_FX & 1u) {
        for (k = 0; k < DRUM_NCH * SEQ_NSTEP; k++) clip[k / SEQ_NSTEP][k % SEQ_NSTEP] = seq_song.s[seq.pat][k / SEQ_NSTEP][k % SEQ_NSTEP];
        clip_ok = 1;
        say("PATTERN COPIED");
    }
    if (b >> BTN_EDIT & 1u) {
        if (clip_ok) {
            for (k = 0; k < DRUM_NCH * SEQ_NSTEP; k++) seq_song.s[seq.pat][k / SEQ_NSTEP][k % SEQ_NSTEP] = clip[k / SEQ_NSTEP][k % SEQ_NSTEP];
            say("PATTERN PASTED");
        } else {
            say(PANEL_PASTE_HINT);
        }
    }
    if (b >> BTN_GLO & 1u) {
        if (seq.run) transport(0);
        set.ext = !set.ext;
        seq.ext = set.ext;
        say(set.ext ? "CLOCK: EXTERNAL" : "CLOCK: INTERNAL");
    }
    if (b >> BTN_HOME & 1u) {
        char s[24];
        set.kit = (uint8_t)((set.kit + 1u) % DRUM_NKITS);
        kit = DRUM_KITS[set.kit];
        kit_apply();
        kit_edited = 1;
        cat(cat(s, "KIT "), kit.name);
        say(s);
    }
    if (b >> BTN_SAVE & 1u) {
        if (seq.run) say("STOP FIRST, THEN SAVE");
        else { save_req = 1; say("SAVING..."); }
    }
}

static void app_main(void)
{
    uint32_t k, cpu = 0, cpu_t = 0;
    uint32_t h_head = ~0u, h_stat = ~0u, h_ch = ~0u, h_par = ~0u, h_steps = ~0u, msg_shown = 0, splash;
    int32_t e;

    if (settings_load(&set, sizeof set) || set.kit >= DRUM_NKITS || set.bpm < 40u || set.bpm > 250u || set.swing < 50u ||
        set.swing > 75u || set.pat >= SEQ_NPAT || set.ch >= DRUM_NCH || set.page >= NPAGE || set.mode > MODE_PLAY ||
        set.sens >= PANEL_NSENS)
        set.kit = 0, set.bpm = 120, set.swing = 50, set.pat = 0, set.ch = 0, set.page = 0, set.ext = 0, set.mode = MODE_STEP,
        set.sens = 0;
    if (blob_load(0, &kit, sizeof kit))
        kit = DRUM_KITS[set.kit];
    if (blob_load(1, &seq_song, sizeof seq_song))
        demo_song();
    drum_init();
    kit_apply();
    seq.bpm = set.bpm;
    seq.swing = set.swing;
    seq.ext = set.ext;
    seq_pattern(set.pat);
    panel_init();
    draw_splash();
    splash = fm1_ms | 1u;                       /* (when it went up; 0: gone) */

    for (;;) {
        uint32_t b, rel, h;
        platform_poll();

        panel_input();                          /* keys, pads, knobs, faders, the panel's own buttons */
        b = fm1_input_edges(&rel);
        buttons(b);
        panel_buttons(b);
        if (save_req && !seq.run && quiet()) save_all();

        /* MIDI: in; out the sequencer's hits (channel 10) and, on the internal clock, the clock */
        midi_in();
        while ((e = seq_out_take()) >= 0) {
            uint32_t ch = (uint32_t)e >> 8;
            hit_ms[ch] = fm1_ms;
            midi_send(0x99, 36u + ch, (uint32_t)e & 0x7Fu);
            midi_send(0x89, 36u + ch, 0);
        }
        h = seq_clocks_due();
        if (seq.run && !set.ext)
            while (h--) midi_send(0xF8, 0, 0);

        /* the LEDs: the buttons, then the panel's keys or pads */
        for (k = 0; k < PANEL_NBTN; k++) fm1_led_key(k, 0);
        fm1_led_key(BTN_PLAY, seq.run && (seq.step % 4u == 0u || set.ext));
        fm1_led_key(BTN_REC, rec);
        fm1_led_key(BTN_SEQ, set.mode == MODE_STEP);
        fm1_led_key(BTN_ARP, seq.mute >> set.ch & 1u);
        fm1_led_key(BTN_LFO, seq.solo >> set.ch & 1u);
        fm1_led_key(BTN_GLO, set.ext);
        fm1_led_key(BTN_SAVE, kit_edited);
        panel_leds();

        /* the screen: only what changed */
        if (fm1_ms - cpu_t >= 1000u) {
            cpu_t = fm1_ms;
            cpu = cpu_us_max * 100u / 5805u;    /* of a block's time (256 frames) */
            cpu_us_max = 0;
        }
        if (splash && (fm1_ms - splash >= SPLASH_MS || b)) {
            splash = 0;
            lcd_fill(0, 0, 240, 240, C_BLACK);
        }
        if (!splash) {
            idle_ok = 1;
            h = (uint32_t)seq.pat | (uint32_t)seq.next << 4 | (uint32_t)set.bpm << 8 | (uint32_t)set.swing << 16 |
                (uint32_t)seq.run << 24 | (uint32_t)rec << 25 | (uint32_t)set.ext << 26;
            if (h != h_head) { h_head = h; draw_header(); }
            h = fm1_ms < msg_until ? 0x80000000u | msg_until : (uint32_t)kit_edited | cpu << 1 | (uint32_t)set.kit << 12;
            if (h != h_stat || (msg_shown && fm1_ms >= msg_until)) { h_stat = h; msg_shown = fm1_ms < msg_until; draw_status(cpu); }
            h = (uint32_t)set.ch | (uint32_t)set.page << 4 | (uint32_t)seq.mute << 8 | (uint32_t)seq.solo << 16 | (uint32_t)set.kit << 24;
            if (h != h_ch) { h_ch = h; draw_channel(); }
            for (h = set.page * 31u + set.ch * 7u + knob_touched * 131u, k = 0; k < NKNOB; k++) {
                uint32_t q = PAGE_PARAM[set.page][k];
                h = h * 33u + (q == Q_NONE ? 0u : slot_value(q));
            }
            if (h != h_par) { h_par = h; draw_params(); }
            for (h = (seq.run ? seq.step + 1u : 0u) | (uint32_t)seq.pat << 5 | set.ch << 8, k = 0; k < SEQ_NSTEP; k++)
                h = h * 3u + seq_song.s[seq.pat][set.ch][k];
            if (h != h_steps) { h_steps = h; draw_steps(seq.run ? seq.step + 1u : 0u); }
            idle_ok = 0;
        }

        /* the settings, 2 s after a change, when stopped and silent (an erase stalls the sound) */
        if (dirty && !seq.run && fm1_ms - t_change > 2000u && quiet()) {
            settings_save(&set, sizeof set);
            dirty = 0;
        }
    }
}
#ifdef DINGHY_HOST
static int host_probe(const char *name, const int *a, int n, int *got) { (void)name; (void)a; (void)n; (void)got; return -1; }
#endif
