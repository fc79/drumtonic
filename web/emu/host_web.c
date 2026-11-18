/* SPDX-License-Identifier: GPL-3.0-only */
/* A fake FM-1 in a browser: the app (firmware/src/dinghy.c) runs unchanged on it, as WebAssembly. It is
 * tests/host.c without the script, the files and main(): the page presses the keys and turns the knobs (emu_*
 * below), and reads the screen, the LEDs and the sound. web/emu/build.sh compiles it in place of tests/host.c (a copy
 * of the tree where this file is tests/host.c: platform.c includes it under DINGHY_HOST). Keep the two alike: what
 * the app needs from tests/host.c, it needs from here too (the build fails otherwise).
 *
 * Time: the app's loop never returns (app_main). Each pass calls platform_poll(), one millisecond: here it asks the
 * page for that millisecond's input (emu_yield, an import: the page suspends the app there, with Binaryen's Asyncify,
 * and resumes it when it wants more sound), then renders the sound that millisecond owes, in blocks of 256 frames as
 * tests/host.c does. The same input at the same milliseconds gives the same sound as tests/host.c, bit for bit
 * (web/emu/test_emu.mjs checks it). Storage is in RAM: nothing is kept after the page closes. FM-1 only. */
#include <stdint.h>
#include <string.h>

#if BOARD_SMK37
#error "web/emu/host_web.c: the FM-1 only"
#endif
#ifndef APP_NAME
#define APP_NAME "DINGHY"
#endif
#ifndef APP_VER
#define APP_VER "0.0"
#endif
#define FS 44100u
#define HALF_FRAMES 256u
#define EMU_EXPORT(name) __attribute__((export_name(#name))) name

__attribute__((import_module("env"), import_name("emu_yield"))) void emu_yield(void);   /* the page's turn */

static volatile uint32_t fm1_ms;
#define FM1_TICKS_PER_US 24u                    /* (as the FM-1's TIMER4) */
static uint32_t fm1_ticks(void) { return fm1_ms * 1000u * FM1_TICKS_PER_US; }   /* (to the millisecond) */
#define RT_N 32u                                /* the clock and transport in, as they arrive (platform.c) */
static volatile uint32_t rt_t[RT_N], rt_w;
static volatile uint8_t rt_b[RT_N];

#define HOST_NENC 7
static struct { uint32_t notes, buttons; } fm1_in;
static uint32_t host_edges, host_rel, host_rel_next;   /* press edges, release edges (a tap's release: the call after) */
static uint32_t fm1_input_edges(uint32_t *released)
{
    uint32_t b = host_edges;
    host_edges = 0;
    *released = host_rel;
    host_rel = host_rel_next;
    host_rel_next = 0;
    return b;
}
static uint32_t fm1_input_note_edges(void) { return 0; }   /* (the page sets fm1_in.notes) */
static int32_t host_enc[HOST_NENC];
static int32_t fm1_enc_take(uint32_t e) { int32_t v = host_enc[e % HOST_NENC]; host_enc[e % HOST_NENC] = 0; return v; }
static uint64_t host_leds, host_glow;           /* bit id: LED id lit, dim (0..13 buttons, 14..40 keys) */
static void fm1_irq_off(void) {}
static void fm1_irq_on(void) {}
static void fm1_led_key(uint32_t id, int on) { if (on) host_leds |= (uint64_t)1 << id; else host_leds &= ~((uint64_t)1 << id); }
static void fm1_led_key_glow(uint32_t id, int on) { if (on) host_glow |= (uint64_t)1 << id; else host_glow &= ~((uint64_t)1 << id); }

/* the screen: 240 x 240 RGB565, as the ST7789 gets it (gfx.c sends the bytes swapped) */
static uint16_t fb[240 * 240];
static uint32_t fb_gen;                         /* + 1 at each change: the page redraws only then */
static void lcd_sync(void) {}
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, j;
    fb_gen++;
    for (j = y; j < y + h && j < 240u; j++)
        for (i = x; i < x + w && i < 240u; i++)
            fb[j * 240u + i] = c;
}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px)
{
    uint32_t i, j;
    fb_gen++;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            if (x + i < 240u && y + j < 240u) {
                uint16_t c = px[j * w + i];
                fb[(y + j) * 240u + x + i] = (uint16_t)(c >> 8 | c << 8);
            }
}
#ifndef CV_MAX
#define CV_MAX (240u * 64u)
#endif
#include "gfx.c"

/* storage in RAM, store.h's sizes (as tests/host.c, without HOST_FLASH) */
static uint8_t store_sliced;
static uint32_t store_gen;                      /* (as platform.c: +1 a save written) */
#include "store.h"
static uint8_t host_set[SECTOR_MAX + 4], host_blob[NBLOB][SECTOR_MAX + 4], host_slot[NSLOT][SLOT_MAX + 4];
static uint32_t emu_saves;                      /* the saves so far (the page can say "saved") */
static int host_load(uint8_t *a, uint32_t max, void *p, uint32_t n)   /* a[0] used, a[1..3] the length */
{
    if (!a[0] || n > max || (uint32_t)(a[1] | a[2] << 8 | a[3] << 16) != n) return -1;
    memcpy(p, a + 4, n);
    return 0;
}
static int host_save(uint8_t *a, uint32_t max, const void *p, uint32_t n)
{
    if (n > max) return -1;
    a[0] = 1; a[1] = (uint8_t)n; a[2] = (uint8_t)(n >> 8); a[3] = (uint8_t)(n >> 16);
    memcpy(a + 4, p, n);
    emu_saves++;
    store_gen++;
    return 0;
}
static int settings_load(void *p, uint32_t n) { return host_load(host_set, SECTOR_MAX, p, n); }
static int settings_save(const void *p, uint32_t n) { return host_save(host_set, SECTOR_MAX, p, n); }
static int blob_load(uint32_t i, void *p, uint32_t n) { return i < NBLOB ? host_load(host_blob[i], SECTOR_MAX, p, n) : -1; }
static int blob_save(uint32_t i, const void *p, uint32_t n) { return i < NBLOB ? host_save(host_blob[i], SECTOR_MAX, p, n) : -1; }
static int slot_load(uint32_t i, void *p, uint32_t n) { return i < NSLOT ? host_load(host_slot[i], SLOT_MAX, p, n) : -1; }
static int slot_save(uint32_t i, const void *p, uint32_t n) { return i < NSLOT ? host_save(host_slot[i], SLOT_MAX, p, n) : -1; }
static uint8_t host_blobs[NBLOB][NBLOB * 0x1000u + 4];   /* (blobs_*: a record over blobs i .. i + k - 1) */
static int blobs_load(uint32_t i, uint32_t k, void *p, uint32_t max, uint32_t *n)
{
    uint8_t *a;
    if (i + k > NBLOB || !host_blobs[i][0]) return -1;
    a = host_blobs[i];
    *n = (uint32_t)(a[1] | a[2] << 8 | a[3] << 16);
    return host_load(a, max < k * 0x1000u - 12u ? max : k * 0x1000u - 12u, p, *n);
}
static int blobs_save(uint32_t i, uint32_t k, const void *p, uint32_t n) { return i + k <= NBLOB ? host_save(host_blobs[i], k * 0x1000u - 12u, p, n) : -1; }

/* store_raw_clear, plat_restart (platform.c): an area emptied; a restart (here: counted, the run goes on) */
static uint32_t host_restarts;
static int store_raw_clear(uint32_t kind, uint32_t i, uint32_t k)
{
    uint8_t *a;
    if (kind == STORE_SET && i == 0u && k == 1u) a = host_set;
    else if (kind == STORE_BLOB && i < NBLOB && k == 1u) a = host_blob[i];
    else if (kind == STORE_BLOBS && k && i + k <= NBLOB) a = host_blobs[i];
    else if (kind == STORE_SLOT && i < NSLOT && k == 1u) a = host_slot[i];
    else return -1;
    a[0] = 0;
    store_gen++;
    return 0;
}
static void plat_restart(void) { host_restarts++; }
/* store_raw_read (platform.c, as tests/host.c): an area's bytes as the flash holds them, made from the record kept here: [magic]
 * [length][crc32] then its bytes, 0xFF after it and in an area never saved (a backup over MIDI: backup.c) */
static int store_raw_read(uint32_t kind, uint32_t i, uint32_t k, uint32_t off, void *p, uint32_t n)
{
    const uint8_t *a;
    uint8_t *d = p;
    uint32_t cap, magic, len, c = 0xFFFFFFFFu, h[3], j, b;
    if (kind == STORE_SET && i == 0u && k == 1u) { a = host_set; cap = 0x1000u; magic = STORE_MAGIC; }
    else if (kind == STORE_BLOB && i < NBLOB && k == 1u) { a = host_blob[i]; cap = 0x1000u; magic = STORE_MAGIC; }
    else if (kind == STORE_BLOBS && k && i + k <= NBLOB) { a = host_blobs[i]; cap = k * 0x1000u; magic = BLOBS_MAGIC ^ STORE_MAGIC; }
    else if (kind == STORE_SLOT && i < NSLOT && k == 1u) { a = host_slot[i]; cap = SLOT_SECT * 0x1000u; magic = SLOT_MAGIC ^ STORE_MAGIC; }
    else return -1;
    if (off > cap || n > cap - off) return -1;
    len = a[0] ? (uint32_t)(a[1] | a[2] << 8 | a[3] << 16) : 0u;
    if (len > cap - 12u) len = 0u;
    for (j = 0; j < len; j++) {
        c ^= a[4 + j];
        for (b = 0; b < 8u; b++) c = c & 1u ? (c >> 1) ^ 0xEDB88320u : c >> 1;
    }
    h[0] = magic; h[1] = len; h[2] = ~c;
    for (j = 0; j < n; j++) {
        uint32_t at = off + j;
        d[j] = !a[0] ? 0xFFu : at < 12u ? (uint8_t)(h[at / 4u] >> (8u * (at % 4u))) : at < 12u + len ? a[4 + at - 12u] : 0xFFu;
    }
    return 0;
}

/* MIDI in: status, data 1, data 2, source (1 USB, 2 TRS); out: dropped (the page has no MIDI out yet) */
static uint8_t mq[64][4];
static uint32_t mq_w, mq_r;
static int midi_get(uint8_t *st, uint8_t *d1, uint8_t *d2)
{
    int src;
    if (mq_r == mq_w) return 0;
    *st = mq[mq_r % 64u][0]; *d1 = mq[mq_r % 64u][1]; *d2 = mq[mq_r % 64u][2]; src = mq[mq_r % 64u][3] ? mq[mq_r % 64u][3] : 1;
    mq_r++;
    return src;
}
static void midi_send(uint32_t st, uint32_t d1, uint32_t d2) { (void)st; (void)d1; (void)d2; }

static void app_audio(int32_t *out, uint32_t n);
static void app_main(void);
/* the FM-1's second core: the job runs when it is waited for, as tests/host.c does by default (two cores) */
static void (*host_c1_fn)(uint32_t);
static uint32_t host_c1_arg;
static int cpu2_run(void (*fn)(uint32_t), uint32_t a) { host_c1_fn = fn; host_c1_arg = a; return 1; }
static int cpu2_wait(void) { host_c1_fn(host_c1_arg); return 0; }
static const uint32_t *cpu_power(void) { static const uint32_t v[4] = {1260u, 1400u, 1u, 3200u}; return v; }
static uint32_t cpu_cores(void) { return 2u; }
static uint32_t cpu_mhz(void) { return 360u; }
static int host_probe(const char *name, const int *a, int n, int *got);   /* (the app's; emu_probe below) */
static int host_list(const char *spec);          /* (the app's; emu_list below) */

/* the sound out: a ring of int16 stereo frames, as the FM-1 sends it over USB (MASTER all the way up); the page
 * reads it from emu_audio_buf(), up to emu_audio_w() */
#define EMU_RING 8192u                          /* frames (a power of two) */
static int16_t emu_ring[2u * EMU_RING];
static uint32_t emu_ring_w;

static void platform_poll(void)                 /* one call = one millisecond */
{
    static uint32_t owed;
    static int32_t blk[2u * HALF_FRAMES];
    fm1_ms++;
    emu_yield();                                /* the page: this millisecond's keys, knobs, MIDI (emu_* below) */
    owed += FS;                                 /* the frames this millisecond is worth, in blocks as the ISR renders */
    while (owed >= 1000u * HALF_FRAMES) {
        uint32_t i, w;
        owed -= 1000u * HALF_FRAMES;
        memset(blk, 0, sizeof blk);
        app_audio(blk, HALF_FRAMES);
        for (i = 0; i < HALF_FRAMES; i++) {
            w = (emu_ring_w + i) % EMU_RING;
            emu_ring[2u * w] = (int16_t)(blk[2u * i] > 32767 ? 32767 : blk[2u * i] < -32767 ? -32767 : blk[2u * i]);
            emu_ring[2u * w + 1u] = (int16_t)(blk[2u * i + 1u] > 32767 ? 32767 : blk[2u * i + 1u] < -32767 ? -32767 : blk[2u * i + 1u]);
        }
        emu_ring_w += HALF_FRAMES;
    }
}

/* ---- what the page calls (between two turns of the app: it is suspended in emu_yield) */
static uint8_t emu_asy[64 * 1024];              /* Asyncify's stack: the app's frames while it waits for the page */
void EMU_EXPORT(emu_boot)(void) { app_main(); }   /* the app; it returns at each emu_yield, and is resumed through here */
uint8_t *EMU_EXPORT(emu_asyncify_data)(void)
{
    uint32_t *h = (uint32_t *)(void *)emu_asy;  /* Asyncify's header: where its data goes, and where it ends */
    h[0] = (uint32_t)(uintptr_t)(emu_asy + 8);
    h[1] = (uint32_t)(uintptr_t)(emu_asy + sizeof emu_asy);
    return emu_asy;
}
uint32_t EMU_EXPORT(emu_ms)(void) { return fm1_ms; }
void EMU_EXPORT(emu_keys)(uint32_t mask) { fm1_in.notes = mask & 0x7FFFFFFu; }   /* the note keys held: bit 0 F3 .. 26 G5 */
/* a button (BTN_*): 1 pressed, 0 let go, 2 a tap (pressed and let go: tests/host.c's "btn"). Let go before the app saw
 * it pressed (both in one millisecond): a tap, so the press is not lost */
void EMU_EXPORT(emu_button)(uint32_t id, uint32_t how)
{
    uint32_t b = 1u << (id & 31u);
    if (how == 1u) { host_edges |= b; fm1_in.buttons |= b; }
    else if (how == 2u || (host_edges & b)) { host_edges |= b; host_rel_next |= b; fm1_in.buttons &= ~b; }
    else { host_rel |= b; fm1_in.buttons &= ~b; }
}
void EMU_EXPORT(emu_encoder)(uint32_t id, int32_t detents) { host_enc[id % HOST_NENC] += detents; }   /* ENC_*, + clockwise */
void EMU_EXPORT(emu_midi)(uint32_t st, uint32_t d1, uint32_t d2, uint32_t src)   /* src 1 USB, 2 TRS */
{
    mq[mq_w % 64u][0] = (uint8_t)st; mq[mq_w % 64u][1] = (uint8_t)d1; mq[mq_w % 64u][2] = (uint8_t)d2;
    mq[mq_w % 64u][3] = (uint8_t)src;
    mq_w++;
    if (st == 0xF8u || st == 0xFAu || st == 0xFBu || st == 0xFCu)
        { rt_t[rt_w % RT_N] = fm1_ticks(); rt_b[rt_w % RT_N] = (uint8_t)st; rt_w++; }
}
uint16_t *EMU_EXPORT(emu_screen)(void) { return fb; }   /* 240 x 240 RGB565 */
uint32_t EMU_EXPORT(emu_screen_gen)(void) { return fb_gen; }
uint32_t EMU_EXPORT(emu_leds)(uint32_t which)   /* 0 lit 0..31, 1 lit 32..63, 2 dim 0..31, 3 dim 32..63 */
{
    uint64_t v = which & 2u ? host_glow : host_leds;
    return (uint32_t)(which & 1u ? v >> 32 : v);
}
int16_t *EMU_EXPORT(emu_audio_buf)(void) { return emu_ring; }
uint32_t EMU_EXPORT(emu_audio_w)(void) { return emu_ring_w; }   /* frames written so far (the ring holds the last EMU_RING) */
uint32_t EMU_EXPORT(emu_audio_ring)(void) { return EMU_RING; }
uint32_t EMU_EXPORT(emu_saved)(void) { return emu_saves; }
/* tests/host.c's "expect": the app's state NAME (its arguments in a[0..n-1]); -32768 if the app has no such name */
static char emu_pname[16];
static int emu_pargs[4];
char *EMU_EXPORT(emu_probe_name)(void) { return emu_pname; }
int *EMU_EXPORT(emu_probe_args)(void) { return emu_pargs; }
int32_t EMU_EXPORT(emu_probe)(int32_t n)
{
    int got = 0;
    emu_pname[15] = 0;
    return host_probe(emu_pname, emu_pargs, n, &got) ? -32768 : got;
}
/* tests/host.c's "list": a play list as a song will make it (its text in emu_list_spec's 256 B); 0 done, -1 not one */
static char emu_lspec[256];
char *EMU_EXPORT(emu_list_spec)(void) { return emu_lspec; }
int32_t EMU_EXPORT(emu_list)(void) { emu_lspec[255] = 0; return host_list(emu_lspec); }
