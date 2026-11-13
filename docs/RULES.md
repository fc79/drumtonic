# The chip's rules

The FM-1 runs a JieLi AC791N with JieLi's own clang (pi32v2). It is fast enough for a lot of synth, but it
has habits. These are the ones that cost the SLOOP / sloopDX / zp12 people time; the build catches most of them.

## Integers only

- **No `float`, no `double`** in the default build: no libgcc to emulate them, the link fails. Use fixed point:
  Q15 (a value times 32768) for levels, Q12 for gains, a 32-bit phase for oscillators. The AC79 does have a
  single-precision FPU: Drumtonic's FM-1 app turns it on (`-mcpu=r3 -mfprev1 -ffp-contract=off`, tools/build.py
  APP_FPU) for its effects and master (`fx.c`), floats in the audio interrupt included (its
  entry: `hal/fm1_isr.S`). `double` stays out everywhere.
  - The FPU is in the chip: every project of JieLi's AC79 SDK builds with `-mfprev1` (FPU revision 1).
  - Single precision only (32-bit `float`). A `double` would need libgcc's software routines, which are not
    there: the link fails, it does not just run slowly.
  - Write float constants with `f` (`0.5f`, not `0.5`): `0.5` is a `double` and drags the sum into `double`.
  - A fault in the FPU shows `FPU` as the cause on the crash screen (`crash_cause` in `ui_fm1.c`).
- **No 64-bit division.** `int64_t` multiplication is fine; `a / b` or `a % b` on 64-bit values needs
  `__udivdi3`, which is not there: the link fails. Shift instead, or divide 32-bit values.
- **A division by zero does not crash.** It gives a wrong value (the trap is off, as SLOOP has it, because the
  compiler may compute a division ahead of the test that guards it). Test your divisors.
- Compute constants on your computer and write them into the source as numbers (see `INC_C9` in dinghy.c).

## The build checks

`./build.sh` fails, with the reason, when:

- the image, the RAM or the pool is too big;
- code calls or loads an address in the chip ROM (0xFFC00000..0xFFCFFFFF). A constant that happens to look
  like one counts too: `-32767 << 7` is 0xFFC00080, whatever you meant by it. Change the arithmetic so that value
  never appears as a constant (clamp to another range, or shift after the clamp instead of before);
- code outside `hal/` touches a hardware register (a raw address, a `volatile` pointer cast, inline asm): put
  a helper in `hal/` and call it;
- code that runs while the flash is busy (`.ram_text`, the flash driver) calls anything.

## The audio interrupt

`app_audio()` has 5.8 ms for 256 frames, and less in practice: the USB and the key scan interrupt it. When it
runs late the sound crackles; when it never ends the watchdog resets the FM-1.

- Do the work per frame that has to be per frame. Envelopes, filters' coefficients, LFOs: once per block of
  16 or 32 frames is plenty.
- Measure: `fm1_ticks()` before and after, divided by `FM1_TICKS_PER_US`. Under half the budget (2.9 ms) leaves
  room.
- No flash, no screen, no `platform_poll()`, no waiting in it.
- Share with the main loop through `volatile` variables, one writer each. A 32-bit store is atomic; a struct
  is not: write the parts in an order that is safe to see half-done (dinghy.c sets the pitch before the gate).

## The main loop

- Call `platform_poll()` at least every few ms (USB) and never less than every 8 s (watchdog).
- Draw only what changed: a full screen is 115 KB over SPI.
- A flash erase stops everything for tens of milliseconds, the sound too. Save when silent.

## When it crashes

A fault shows a red screen with three numbers: the vector, the **PC** (where it happened) and the return
address. Look the PC up in `build/dinghy.dis` (the disassembly the build writes). Then it restarts.

If it crashes during boot, twice, the FM-1 starts in the USB rescue by itself, where the updater still works:
install a fixed build. You can always force the rescue: hold **OCT-** while switching on.

If even that is gone (the app is broken before it gets to its boot guard), the FM-1's own boot ROM still
updates over USB: [FM-1-transporter](https://github.com/kurogedelic/FM-1-transporter) gets it back.
