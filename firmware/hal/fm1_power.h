/* SPDX-License-Identifier: GPL-3.0-only */
/* The FM-1's supplies as M-VAVE's own firmware sets them (fc79).
 *
 * VDC14 (P3_ANA_CON6 [2:0]): 1.25 V + 50 mV a level; SYSVDD (P3_ANA_CON9 [3:0], the core and SRAM):
 * 0.93 V + 30 mV a level. The boot loader leaves 3 (1.40 V) and 11 (1.26 V); M-VAVE's own firmware
 * (V15) raises them to 7 (1.60 V) and 15 (1.38 V) as it starts, at the same 360 MHz (fm1-x0x#21,
 * fc79's reading of its __hw_power_init), with VDC14 from the internal DC-DC and VDDIOM / VDDIOW at
 * 3.2 V. At the boot loader's, under load (both cores), some units read flash and SRAM wrong (Jangada
 * measured it on this FM-1: core 1 faulted within seconds). Up only, a level per 100 us, VDC14 first:
 * SYSVDD is regulated from it. Needs fm1_time.h (fm1_delay_us) and fm1_sys.h (P33). */
#pragma once
#include <stdint.h>

#define FM1_P3_ANA_CON0 0x00u
#define FM1_P3_ANA_CON5 0x05u
#define FM1_P3_ANA_CON6 0x06u
#define FM1_P3_ANA_CON9 0x09u
static uint32_t fm1_vdc14(void) { return fm1_p33_read(FM1_P3_ANA_CON6) & 7u; }
static uint32_t fm1_sysvdd(void) { return fm1_p33_read(FM1_P3_ANA_CON9) & 15u; }

/* a rail to a level, either way, a level per 100 us */
static void fm1_rail_to(uint32_t a, uint32_t shift, uint32_t mask, uint32_t level)
{
    uint32_t v = fm1_p33_read(a) >> shift & mask;
    while (v != level) {
        v = v < level ? v + 1u : v - 1u;
        fm1_p33_write(a, (uint8_t)((fm1_p33_read(a) & ~(mask << shift)) | v << shift));
        fm1_delay_us(100);
    }
}

/* VDC14 from the internal DC-DC instead of the LDO, with its tuning, as M-VAVE's own firmware does it
 * (V15 at 0x2004E70: the SDK's __hw_power_init / power_dcdc_ldo_config with vdc14_dcdc = 1). LDO13 on, DCDC13
 * on, LDO13 off; then P3_ANA_CON1 0x20 (MAIN_VDD_LOAD), VDDIO_HD 1, VD14_HD 1, BTDCDC tuning 0xA5 / 0x96, and
 * P3_ANA_CON9 b4. The levels are kept, and set after, a step at a time. Returns P3_ANA_CON0 as found. */
static uint8_t fm1_dcdc_on(void)
{
    uint8_t was = fm1_p33_read(FM1_P3_ANA_CON0);
    fm1_p33_write(FM1_P3_ANA_CON0, (uint8_t)(fm1_p33_read(FM1_P3_ANA_CON0) | 4u));
    fm1_delay_us(1000);
    fm1_p33_write(FM1_P3_ANA_CON0, (uint8_t)(fm1_p33_read(FM1_P3_ANA_CON0) | 8u));
    fm1_delay_us(1000);
    fm1_p33_write(FM1_P3_ANA_CON0, (uint8_t)(fm1_p33_read(FM1_P3_ANA_CON0) & ~4u));
    fm1_delay_us(1000);
    fm1_p33_write(0x01u, 0x20u);
    fm1_p33_write(FM1_P3_ANA_CON5, (uint8_t)((fm1_p33_read(FM1_P3_ANA_CON5) & 0x3Fu) | 0x40u));
    fm1_p33_write(FM1_P3_ANA_CON6, (uint8_t)((fm1_p33_read(FM1_P3_ANA_CON6) & 0x07u) | 0x08u));
    fm1_p33_write(0x07u, 0xA5u);
    fm1_p33_write(0x08u, 0x96u);
    fm1_p33_write(FM1_P3_ANA_CON9, (uint8_t)(fm1_p33_read(FM1_P3_ANA_CON9) | 0x10u));
    return was;
}

/* VDC14 from the LDO (the boot loader's way): a DC-DC left on by an earlier boot (P33 keeps it over a reset) handed
 * back to the LDO, LDO first */
static void fm1_ldo_on(void)
{
    if (fm1_p33_read(FM1_P3_ANA_CON0) & 8u) {
        fm1_p33_write(FM1_P3_ANA_CON0, (uint8_t)(fm1_p33_read(FM1_P3_ANA_CON0) | 4u));
        fm1_delay_us(1000);
        fm1_p33_write(FM1_P3_ANA_CON0, (uint8_t)(fm1_p33_read(FM1_P3_ANA_CON0) & ~8u));
        fm1_delay_us(1000);
    }
}

/* 320 MHz supplies from the LDO: SYSVDD to sysvdd, VDC14 to vdc14 (up: VDC14 first; down: SYSVDD first, as SYSVDD is
 * regulated from VDC14) */
static void fm1_supply_ldo(uint32_t sysvdd, uint32_t vdc14)
{
    fm1_ldo_on();
    if (vdc14 >= fm1_vdc14()) {
        fm1_rail_to(FM1_P3_ANA_CON6, 0, 7u, vdc14);
        fm1_rail_to(FM1_P3_ANA_CON9, 0, 15u, sysvdd);
    } else {
        fm1_rail_to(FM1_P3_ANA_CON9, 0, 15u, sysvdd);
        fm1_rail_to(FM1_P3_ANA_CON6, 0, 7u, vdc14);
    }
}

/* 320 MHz supplies from the DC-DC: SYSVDD to sysvdd, VDC14 to vdc14, VDDIOM and VDDIOW 3.2 V (2 x 320 MHz) */
static void fm1_supply_dcdc(uint32_t sysvdd, uint32_t vdc14)
{
    (void)fm1_dcdc_on();
    if (vdc14 >= fm1_vdc14()) {
        fm1_rail_to(FM1_P3_ANA_CON6, 0, 7u, vdc14);
        fm1_rail_to(FM1_P3_ANA_CON9, 0, 15u, sysvdd);
    } else {
        fm1_rail_to(FM1_P3_ANA_CON9, 0, 15u, sysvdd);
        fm1_rail_to(FM1_P3_ANA_CON6, 0, 7u, vdc14);
    }
    fm1_rail_to(FM1_P3_ANA_CON5, 0, 7u, 4u);
    fm1_rail_to(FM1_P3_ANA_CON5, 4, 3u, 3u);
}

/* M-VAVE's supplies, before anything loads them: the DC-DC, VDC14 1.60 V, SYSVDD 1.38 V, VDDIOM and VDDIOW 3.2 V */
static void fm1_supply_mvave(void)
{
    (void)fm1_dcdc_on();
    fm1_rail_to(FM1_P3_ANA_CON6, 0, 7u, 7u);
    fm1_rail_to(FM1_P3_ANA_CON9, 0, 15u, 15u);
    fm1_rail_to(FM1_P3_ANA_CON5, 0, 7u, 4u);
    fm1_rail_to(FM1_P3_ANA_CON5, 4, 3u, 3u);
}
