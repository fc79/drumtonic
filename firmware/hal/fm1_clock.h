/* SPDX-License-Identifier: GPL-3.0-only */
/* SYS from 360 to 320 MHz at boot, as X0X does it: X0X took it from Melodee (keremimo/melodee, PR #27) and rewrote
 * it after JieLi's own sequence when Melodee's hung at boot. IRQs off, core 1 not started, the code in SRAM.
 *
 * PLL2_CON1 (0x119AC) b[11:0] is FBDS = the SYS VCO multiplier - 2, from 24 MHz / 12: the boot loader's 268 is VCO 540,
 * and CLK_CON2 takes 2/3 of it: 360 MHz. FBDS 238: VCO 480, SYS 320 (HSB 160, LSB 53.3, SFC 80). TIMER4, the I2S, USB
 * and the UART run on their own clocks. JieLi's sequence (AC79 SDK V1.2.1, cpu.a clock.c: sys_clock_update): wait for
 * the cache and the SFC; SYS from the RC 16 MHz; cache off; SFC (XIP) off; the PLL; a fixed wait (no lock bit; 20 ms
 * here); SFC and cache back; SYS from the PLL, the RC left at 16 MHz. Every wait is a counted loop (no timer read: a
 * TIMER4 wait hung on some cold boots right after the PLL write). The supplies are the caller's, set before. */
#pragma once
#include <stdint.h>
/* needs fm1_flash.h (RAMFN, FL_FAR) included first, after the app's store.h: its FL_RANGE_OK must come first */

#define FCLK_PLL2_CON1 0x119ACu
#define FCLK_CLK_CON0  0x1000Cu
#define FCLK_CACHE_CON 0x1EEE008u
#define FCLK_SFC_CON   0x40200u
#define FCLK_READ(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define FCLK_WRITE(a, v) (*(volatile uint32_t *)(uintptr_t)(a) = (v))
#define FCLK_SYNC() __asm__ volatile("csync" ::: "memory")
#define FCLK_FAILED() do { for (;;) FCLK_SYNC(); } while (0)   /* (the watchdog resets; never back into XIP on the RC) */
#define FCLK_IN __attribute__((always_inline)) static inline   /* (inlined into the SRAM function: no call) */

FCLK_IN void fm1_clock_us(uint32_t us)          /* JieLi's RC16M_udelay: 2 loops a microsecond on the RC */
{
    volatile uint32_t n = us * 2u;
    while (--n)
        __asm__ volatile("nop");
}
FCLK_IN int fm1_clock_idle(uint32_t mask)      /* 1 = the cache idle (CACHE_CON b14), 2 = the SFC not busy (b31) */
{
    uint32_t n = 40000u;
    for (;;) {
        FCLK_SYNC();
        if ((!(mask & 1u) || (FCLK_READ(FCLK_CACHE_CON) & (1u << 14))) &&
            (!(mask & 2u) || !(FCLK_READ(FCLK_SFC_CON) & (1u << 31))))
            return 1;
        if (!--n)
            return 0;
    }
}

/* 0 = now 320 MHz; 1 = left as it was (not the boot loader's 360: FBDS not 268, or the cache / SFC busy).
 * *st: the stage reached (1..8), kept in noinit by the caller: a boot that hung in here is seen at the next */
static RAMFN int fm1_clock_320_ram(volatile uint32_t *st)
{
    uint32_t con0 = FCLK_READ(FCLK_CLK_CON0), pll = FCLK_READ(FCLK_PLL2_CON1), cache, sfc;
    if ((pll & 0xFFFu) != 268u || !(con0 & (1u << 8)) || !fm1_clock_idle(3u))
        return 1;
    *st = 1;
    FCLK_WRITE(FCLK_CLK_CON0, (con0 | (1u << 1)) & ~(1u << 8));   /* SYS from the RC 16 MHz */
    FCLK_SYNC();
    fm1_clock_us(300u);
    *st = 2;
    if (!fm1_clock_idle(1u))
        FCLK_FAILED();
    cache = FCLK_READ(FCLK_CACHE_CON);
    FCLK_WRITE(FCLK_CACHE_CON, cache & ~(3u << 8));               /* cache off */
    FCLK_SYNC();
    *st = 3;
    if (!fm1_clock_idle(2u))
        FCLK_FAILED();
    sfc = FCLK_READ(FCLK_SFC_CON);
    FCLK_WRITE(FCLK_SFC_CON, sfc & ~1u);                          /* SFC (XIP) off */
    FCLK_SYNC();
    fm1_clock_us(60u);
    *st = 4;
    FCLK_WRITE(FCLK_PLL2_CON1, (pll & ~0xFFFu) | 238u);           /* VCO 540 -> 480: SYS 320 */
    FCLK_SYNC();
    fm1_clock_us(20000u);
    *st = 5;
    if (!fm1_clock_idle(3u))
        FCLK_FAILED();
    FCLK_WRITE(FCLK_SFC_CON, sfc);                                /* SFC back */
    FCLK_SYNC();
    fm1_clock_us(60u);
    *st = 6;
    FCLK_WRITE(FCLK_CACHE_CON, FCLK_READ(FCLK_CACHE_CON) | (cache & (3u << 8)));   /* cache back */
    FCLK_SYNC();
    *st = 7;
    if (!fm1_clock_idle(3u))
        FCLK_FAILED();
    FCLK_WRITE(FCLK_CLK_CON0, FCLK_READ(FCLK_CLK_CON0) | (1u << 8));  /* SYS from the PLL; the RC stays at 16 MHz */
    FCLK_SYNC();
    fm1_clock_us(20u);
    *st = 8;
    return 0;
}
#define fm1_clock_320(st) FL_FAR(fm1_clock_320_ram)(st)

/* SYS in MHz as the PLL says (CLK_CON2 = 6, 2/3 of the VCO: the boot loader's; else 0, not known) */
static uint32_t fm1_clock_mhz(void)
{
    uint32_t fbds = FCLK_READ(FCLK_PLL2_CON1) & 0xFFFu;
    return (FCLK_READ(0x10014u) & 15u) == 6u ? (fbds + 2u) * 4u / 3u : 0u;
}
