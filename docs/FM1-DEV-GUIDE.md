# Programming the M-VAVE FM-1: a developer's guide

What was learned about the FM-1 and its chip, the JieLi AC791N, while writing
[Drumtonic](https://github.com/fc79/drumtonic), an open-source drum machine firmware for it. Written for someone who
wants to write an FM-1 firmware and has never seen that code.

- **State:** 2026-11-23, Drumtonic 0.9.6. By fc79.
- **Scope:** the FM-1. Its sister keyboard, the SMK-37 (same chip, same boot code), is mentioned only where a fact
  was learned there. For the SMK-37 itself read the [fm1-to-smk37](https://github.com/genkigenki/fm1-to-smk37) guide.
- **It is not official.** Nothing here comes from M-VAVE or JieLi documentation for this product. M-VAVE's own
  "FM-1 Open" workbench was not examined for this guide.
- **Installing firmware is at your own risk.** Read section 1 before you write a single byte.

## How to read the tags

Every claim says how it is known. When two sources disagreed, the code that runs today or the real unit wins, and
the text says so.

| Tag | Meaning |
| --- | --- |
| **[unit]** | Tried or measured on a real FM-1. One unit only: stock V15 when first read, Puya flash chip. |
| **[stock]** | Read from M-VAVE's own firmware packages or from the stock flash, without running anything (static analysis). Described in words; no stock code is reproduced. |
| **[code]** | How the open platform's source does it today (Felucca, SLOOP, Dinghy, Drumtonic). That code runs on many units. |
| **[sdk]** | From JieLi's public AC79 SDK (headers, linker settings, configuration comments). |
| **[community]** | Reported by another project or its users. Not reproduced here. |
| **[host]** | Tested only on a computer, never on the instrument. |
| **[inferred]** | A conclusion drawn from the above. Could be wrong. |

Terms used throughout:

- **Head:** the first 16 KiB of flash, with the second-stage boot loader (SPL, `uboot.boot`) and the boot settings.
- **Mask ROM / UBOOT:** the boot loader inside the chip itself. It cannot be erased and can read and write the
  whole flash over USB.
- **XIP (execute in place):** code runs straight from flash, through a cache, without being copied to RAM.
- **Update loader:** a small program (`ota.bin`) that runs from RAM and writes a new app into flash.
- **Identity:** the text an FM-1 answers when asked what it runs, such as `FM-1_015`.
- **Host mode:** the firmware compiled for a computer, with a fake FM-1 under it.

---

## 1. Rules that keep a unit alive

1. **Back up the whole flash before the first install.** 1 MiB, read twice, compared, kept in two places. It
   writes nothing to the FM-1 (section 7). **[unit]**
2. **Never write the head, `0x00000-0x03FFF`.** The family's installers and update loaders never do. **[code]**
   If the head is damaged, only the mask ROM can bring the unit back. **[inferred]**
3. **Never write `0xFF000-0xFFFFF` (`key_mac`).** It is in no package, so only your own dump has it. **[code]**
4. **Keep the updater in your firmware** (`FELUCCA_OTA 1`). Without it nothing can be installed over your
   firmware except through the rescue. **[code]**
5. **Let the build refuse.** The platform's build fails on the things that brick or break a unit: an image that
   does not fit, a call into the chip ROM, code that runs while the flash is off and calls something, a hardware
   register touched outside `hal/`. Do not work around a refusal; fix its cause. **[code]**
6. **Erase and program only through an allow-list.** The flash driver takes a macro, `FL_RANGE_OK`, that names the
   only ranges your app may write. A bug then cannot erase anything else. **[code]**
7. **Call `platform_poll()` often.** It feeds the watchdog (8 s, then a reset) and runs USB, so the updater stays
   reachable while your firmware runs. **[code]**
8. **Never let the audio interrupt wait.** If it never ends, the watchdog resets the FM-1; at boot, twice, and the
   unit goes to the rescue. **[code]**, and **[unit]** by accident (section 12, mistake 1).
9. **Never drive pin PB1 low.** The boot settings make PB1 held low for 8 s a chip reset. The platform keeps it an
   analog input (the battery divider). **[stock]** **[code]**
10. **On stock V16, go back to V15 before you install** a community firmware, with M-VAVE's own updater. Users
    report that installs fail from V16 **[community]**; V16 also moves the flash regions under the family's saved
    data (section 2.3). Not confirmed on a unit here.
11. **Check your identity number with the hub's own check**, not with a table: two firmwares with the same
    identity look like one to every installer (section 6.2).

A rule that is often repeated for the FM-1 but belongs to the SMK-37: "never write `0xC0000-0xC1FFF`". On the SMK-37
that range holds Bluetooth pairing and the factory key calibration. On the FM-1 it is ordinary space inside the
stock settings area, and it was empty on the unit read here **[unit]**; the FM-1 has no calibration record there
**[unit]**. Dinghy and Drumtonic do not write it on the FM-1 (their slots end at `0xB6FFF`), but Felucca's own
store window, `0x97000-0xDFFFF`, covers it **[code]**. Following the rule costs nothing; it is just not what
protects an FM-1.

---

## 2. The hardware

### 2.1 Overview

| Part | What | Source |
| --- | --- | --- |
| Chip | JieLi **AC791N**. The mask ROM calls itself `WL82` (its USB strings say WL80). | **[unit]** **[stock]** |
| CPU | Two cores, JieLi's own 32-bit architecture **pi32v2**, with a single-precision FPU | **[sdk]** **[code]** |
| Clock | 24 MHz crystal, a PLL. Stock V15 boots at **360 MHz**, stock V16 at **320 MHz** | **[stock]** |
| RAM | **512 KiB** inside the chip, `0x01C00000-0x01C7FFFF`. No external SDRAM (`ENABLE_SDRAM=0`) | **[stock]** **[code]** |
| Flash | **1 MiB** SPI NOR. Puya P25Q80, JEDEC id `0x856014`; units sold since autumn 2026 may carry a UC25HQ80, `0xB36014` | **[unit]** (Puya), **[community]** (UC25HQ80) |
| Chip key | `0x980F`, the same in every stock package and on every unit read (FM-1 and SMK-37). Packages are encrypted with it. | **[unit]** **[stock]** |
| Screen | 240 x 240, 16-bit colour (RGB565, high byte first), ST7789-class, on SPI1 | **[code]** |
| Keys | 27 keys F3..G5, **no velocity** | **[code]** **[unit]** |
| Buttons | 14 | **[code]** **[unit]** |
| Encoders | 7 endless encoders with detents | **[code]** **[unit]** |
| Pot | 1, MASTER, read by the ADC | **[code]** **[unit]** |
| LEDs | One under every key and button, two brightness levels | **[code]** **[unit]** |
| Audio out | I2S to an external codec, 44.1 kHz stereo | **[code]** **[unit]** |
| USB | Full-speed device: MIDI and audio | **[code]** **[unit]** |
| MIDI | USB in and out; a TRS jack, **in only** | **[code]** **[unit]** |

### 2.2 Pins and peripherals (FM-1)

All from the platform's hardware layer (`firmware/hal/`), which runs on real units. **[code]**

| Function | Wiring |
| --- | --- |
| Screen | SPI1, pin group B: clock PC9, data PC10, chip select PC7, data/command PC8. Backlight PA2, active low. SPI1 clock = low-speed bus / (BAUD + 1); BAUD 4. |
| Key matrix | 11 columns x 6 rows with diodes, behind two 74HC595 shift registers (PA4 data, PA3 clock, PA1 latch), bit-banged. Rows PA0, PA5..PA8, PB7 with pull-ups (low = closed). |
| LEDs | Four lines (PH6, PH9, PA9, PA10) light the LEDs of the selected column, so LEDs and keys share the scan. |
| MASTER pot | PB6, ADC channel 4, 10 bits. |
| Battery | PB1, ADC channel 3, raw value (the divider ratio is not known). |
| Audio | ALNK0 (I2S) with DMA; codec control lines on PC0, PC1, PC2, PC6. |
| TRS MIDI IN | UART1 receive on PH8, 31250 baud from the 48 MHz clock, received by DMA into a ring. |
| Flash | SPI0, taken over by the flash controller (SFC) for XIP; chip select PD0. |
| USB | USB0, full speed, polled (no interrupt). |

USB identities:

| Who | VID:PID | Source |
| --- | --- | --- |
| The mask ROM, as a USB disk `WL82 UBOOT1.00` | `4C4A:8057` | **[unit]** |
| The family's app (MIDI, audio) | `1209:0001`, the pid.codes test id | **[code]** |
| The family's update loader | `1209:0002` | **[code]** |
| The stock app; the stock update loader | `4C4A:C755`; `4D4A:4155` | **[community]** (fm1-to-smk37, from analysis) |

### 2.3 The flash map

Offsets are absolute, from the start of the flash. Sectors are 4 KiB.

| Range | Stock V15 | Stock V16 | The open platform (Felucca family, Dinghy, Drumtonic) |
| --- | --- | --- | --- |
| `0x00000-0x03FFF` | Head: SPL and boot settings | Head: another SPL build, other settings | **Never written** |
| `0x04000-0x92FFF` | App area; `app.bin` starts at `0x4120`. (The package counts it from 0: 588 KiB) | App area up to `0xA8FFF` (from 0: 676 KiB), sound bank inside | The app: a slot of `0x8DFBC` bytes at `0x4120`. Only the update loader writes `0x04000-0x92FFF` |
| `0x93000-0xE8FFF` | VM: the stock settings store (344 KiB) | VM `0xBA000-0xD9FFF`; SEQ64 `0xDA000-0xEBFFF` | Main store `0x97000-0xDFFFF` (Dinghy: slots 0..7 at `0x97000-0xB6FFF`). Update staging `0xE0000-0xE4FFF` |
| `0xE9000-0xE9FFF` | BTIF | BTIF at `0xEC000` | Not used |
| `0xEA000-0xFBFFF` | USR: the factory sound bank (572 sounds of 128 bytes) | USER `0xED000-0xFEFFF`: "kept free for later updates", no file in it | Dinghy: blobs 0..7 at `0xF4000-0xFBFFF` |
| `0xFC000-0xFEFFF` | Not listed | Inside USER | Felucca's globals; Dinghy: settings at `0xFC000` |
| `0xFF000-0xFFFFF` | `key_mac` | `key_mac` | **Never written** |

How each column is known:

- **V15:** the region sizes from the stock package, and a dump of one unit. On that unit the sectors holding data
  were `0x00000-0x93FFF`, `0xE9000-0xF1FFF` and `0xFA000-0xFBFFF`; everything else read as erased, `key_mac`
  included. **[stock]** **[unit]** Other units may differ: back up yours.
- **V16:** read from the V16 package only. No unit running V16 was dumped. **[stock]**
- **The platform:** `hal/fm1_flash_map.h`, `src/store.h`, `src/ota.c`, `loader/ldr_core.c`, `tools/fm1pkg_make.py`.
  **[code]**

**A trap with these addresses.** The stock packages list their regions as absolute offsets. Some unpackers add the
app area's start, `0x4000`, and report every region 16 KiB too high (VM at `0x97000`, the V15 sound bank ending past
`key_mac`). Notes made that way circulated in this project too. The dump settles it: on the unit, BTIF and the sound
bank begin at `0xE9000` and `0xEA000`, as the package says. **[unit]** The fm1-to-smk37 guide gives the same warning
for the SMK-37. **[community]**

**What this means for saved data:**

- The family's firmwares share the store. A firmware that finds another's data sees a wrong magic number or length
  and starts fresh. Its first save overwrites what was there. **[code]**
- Blobs 6 and 7 (`0xFA000-0xFBFFF`) lie on stock V15 data. Drumtonic leaves them alone and keeps its extra kits
  elsewhere. **[code]** **[unit]** (that range was the same in every later dump)
- Against V16's layout the family's places collide: the slots at `0x97000` are inside V16's app, the update staging
  is inside SEQ64, and the blobs and settings are inside USER. Whether V16 ever writes USER is not known.
  **[inferred]** from the two maps.
- The update loader may erase its own records anywhere in `0x93000-0xFBFFF`, a window that contains stock BTIF.
  **[code]**

### 2.4 The memory map

From the linker script and the hardware layer. **[code]**

| Address | Size | What |
| --- | --- | --- |
| `0x01C00000` | 24 KiB | RAM code: what must run while the flash is off (the flash driver, the clock change, the second core's waiting loop) |
| `0x01C06000` | 8 KiB | Left free for code run from RAM while debugging |
| `0x01C08000` | 96 KiB (98,304 B) | `.data` and `.bss`: your globals and statics |
| `0x01C20000` | 336 KiB (344,064 B) | The pool: big buffers, zeroed at boot (`__attribute__((section(".pool")))`) |
| `0x01C74000` | 256 B | Guard band (a write here is a fault) |
| `0x01C74100` | 24,320 B | The user stack: the main loop starts on it. Keep big arrays off it (`static`) |
| `0x01C7A000` | 256 B | Guard band |
| `0x01C7A100` | 7,936 B | The supervisor stack |
| `0x01C7C000` | 15,696 B | No-init RAM: survives a warm reset. The crash record and the boot guard live here |
| `0x01C7FD50` | to the top | Reserved: boot information, the mailbox the boot loader reads (`0x01C7FD80`), the update record (`0x01C7FD88`, 112 B), the vector table (`0x01C7FE00`, 128 entries) |
| `0x02000000` | 1 MiB window | Flash as memory (XIP). Flash offset `off` (from `0x4000` up) appears at `0x02000000 + off - 0x4000`. The app starts at `0x02000120` |
| `0xFFC00000` | | The chip's ROM. The build refuses any reference to `0xFFC00000-0xFFCFFFFF` |

Peripheral registers: `0x10000-0x13FFF` (clocks, timers, SPI, UART, USB, ADC, audio), `0x40000-0x43FFF` (the flash
controller), `0x50000-0x51FFF` (pins and pin routing), `0x1EE0000-0x1EEFFFF` (the CPU: cache, interrupts, guards,
the second core). Names and addresses follow the SDK's headers. **[sdk]** **[code]**

Other load addresses: the SPL at `0x01C02000` **[stock]**; the update loader at `0x01C0A800`, up to 80 KiB
**[code]**.

---

## 3. The CPU

### 3.1 Architecture

- **pi32v2** is JieLi's own instruction set, not ARM or RISC-V. Standard disassemblers do not know it. The
  toolchain's own `objdump` does (ELF machine number 241); there is also
  [ghidra-jieli](https://github.com/kagaimiq/ghidra-jieli) (not tried here). **[community]**; the objdump route was
  used here on a computer **[host]**
- Instructions are 2 or 4 bytes long. Sixteen general registers `r0-r15`, plus `sp`, separate user and supervisor
  stack pointers (`usp`, `ssp`), status (`psr`), interrupt configuration (`icfg`) and return registers (`rets`,
  `reti`, `rete`, `retx`). Seen in the platform's assembly and in the build's own listing. **[code]**
- The compiler is JieLi's clang. The toolchain's objdump reports LLVM 4.0.1. **[host]**
- There is a hardware 32-bit divide instruction. **[code]** (the listing shows it)
- A call reaches about 23 bits. Flash code (`0x02xxxxxx`) cannot call RAM code (`0x01C0xxxx`) directly: go through
  a function pointer the compiler cannot fold back (`FL_FAR` in the flash driver). **[code]**
- **`csync`** is an instruction that makes the core wait until earlier effects have landed. The platform uses it
  before returning from an interrupt, after register writes whose order matters, and around every word shared
  between the two cores. JieLi's clang does not emit the `csync` needed before `rti`, so **interrupt handlers are
  never written in C directly**: a small assembly wrapper saves registers, calls the C body, does `csync`, then
  `rti`. **[code]**

### 3.2 Numbers: what exists and what does not

| | Works? | Notes | Source |
| --- | --- | --- | --- |
| 32-bit integer maths, shifts | Yes | The normal way: Q15 for levels, Q12 for gains, a 32-bit phase for oscillators | **[code]** |
| 64-bit multiply | Yes | | **[code]** |
| **64-bit divide or modulo** | **No** | Needs `__udivdi3` from libgcc, which is not there: **the link fails** | **[code]** |
| `float` (32-bit) | Yes, when turned on | `-mcpu=r3 -mfprev1 -ffp-contract=off`. Every project in JieLi's SDK builds with `-mfprev1` | **[sdk]** **[unit]** |
| **`double`** | **No** | Would need software routines that are not there: the link fails. Write `0.5f`, never `0.5` | **[code]** |
| Division by zero | Gives a wrong value | The trap is switched off on purpose (see below) | **[code]** |

- **Floats on a real unit.** Drumtonic's effects, compressor, limiter and filter are all `float`, run in the audio
  interrupt, on both cores, and the main loop computes float coefficients for them. Long sessions on the unit
  showed no fault. **[unit]** In compiled code a float value sits in a general register (`r0-r15`) like any
  integer; no listing shows separate FPU registers. **[host]**
- **The division trap is off.** This compiler treats a divide as harmless and may move one ahead of the `if` that
  guards it. With the trap on, correct code crashed. With it off, a real divide by zero gives rubbish silently.
  Test your divisors; the host tests run with a divide-by-zero sanitizer instead. **[community]** (SLOOP, Felucca)
  **[code]**
- **A constant that looks like a ROM address fails the build.** `-32767 << 7` is `0xFFC00080`. Rearrange the
  arithmetic so the value never appears as a constant. **[code]**
- **So does a number that looks like a register.** This check reads the source, not the listing: outside `hal/`
  it refuses any hex literal that falls in a register window, whatever it is for. A test value `0x00040003` was
  refused because `0x40000-0x43FFF` is the flash controller's window. Change the number, not the check.
  **[code]**
- **Compute constants on your computer** and write them into the source as numbers. **[code]**
- **More in the core than the build uses?** Not a newer level. The stock apps of the FM-1 and the SMK-37 (V15
  and V16) disassemble with no unknown instruction in their code under plain `-mcpu=r3`, the build's own
  setting, which includes the single-precision FPU. None uses hardware `double` (`fprev2`), a newer instruction
  revision, packed SIMD or CORDIC. The earlier clue (fewer unknown instructions with `-mcpu=r4`,
  `+fprev2,+instrev3,+simd,+cordic`) was a measuring artefact: the baseline had no CPU level at all, the rest was
  data read as code, and the disassembler ignores `+simd` and `+cordic`. **[stock]** **[host]** What the `r3`
  level does hold, and the compiler never writes: a float multiply-add and multiply-subtract (the stock firmware
  contains them **[stock]**), a saturating add, packed 8- and 16-bit forms and a CORDIC instruction, `copex`
  (JieLi's SDK uses them for this chip in hand-written assembly **[sdk]**). The assembler takes all of them with
  the build's flags **[host]**. Compiler options do not reach them: `-mcpu=r4`, `-mfprev2`, `-msimd`, `-mcordic`
  and `-minstrev3` leave every float loop as it is. **[host]**
- **Those hand-only instructions run on an FM-1.** Tried on one unit (2026-11-22) in a development build with a
  hidden test page: each instruction alone in a small function of the hardware layer, started by hand from the
  main loop more than 30 s after power-on, audio interrupt and second core running as always. Every result is a
  value compared with one worked out on a computer.

  | Instruction | On the unit | |
  | --- | --- | --- |
  | Float multiply-add and multiply-subtract (`acc += a * b`, `acc -= a * b`) | Right on 16 of 16 inputs. **It rounds twice**, the product and then the sum, exactly like `acc + a * b` in C: it is not a fused multiply-add | **[unit]** |
  | 32-bit saturating add | Right on 13 of 13 inputs, 7 of them past a limit | **[unit]** |
  | Packed forms: byte unpack (signed) and pack | 9 of 9 as expected: four bytes become four sign-extended 16-bit lanes, low byte first, and pack gives the word back. One pair out of a large group; the rest is untried | **[unit]** |
  | CORDIC, `copex` | Works, by the numbering of the SDK's fast-maths functions (next point) | **[unit]** |

  The check that an illegal instruction really faults on this chip was left out, so "it did not crash" proves
  nothing here; only the values do. Hand-written assembly belongs in `hal/`: the build refuses inline assembly
  anywhere else. **[code]**
- **What `copex` computes.** One instruction takes two words (low, high) and a number that chooses the function.
  JieLi's libraries hold two different numberings; the unit follows the one of the SDK's fast-maths functions
  ([`math_fast_function.h`](https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK/blob/AC79NN_SDK_V1.2.1_2023-12-13/include_lib/driver/cpu/wl82/asm/math_fast_function.h)),
  read from the prebuilt libraries on a computer. **[sdk]** **[host]** A float goes in and comes out as its 32
  bits. The numbers below ran on the unit, 15 inputs in all; "steps" are steps of a float's last bit. **[unit]**

  | Number | Function | In | Out, and what the unit gave |
  | --- | --- | --- | --- |
  | 11 | Float add | x low, y high | Low word. Exact |
  | 12 | Float subtract | | Low word: **high minus low**. Exact |
  | 13, 14 | Float to integer, integer to float | Low word | Low word. Exact |
  | 16 | Sine and cosine together | Low word: the angle **divided by pi** | Sine high, cosine low. Exact to within 4 steps |
  | 18 | Length of (x, y) | x low, y high | Low word, to be multiplied by 0.6072530. Exact for (3, 4). The high word held the angle divided by pi |
  | 22 | Exponential | Low word | Low word. Within 1 step |
  | 23 | Natural logarithm | Low word | Low word. Within 2 steps |
  | 24 | Square root | Low word | Low word, to be multiplied by 1.2074971. Exact to within 1 step |
  | 25 | Float multiply | x low, y high | Low word. Exact |
  | 26 | Float divide | | Low word: **high over low**. One step under the nearest float |
  | 7 | Logarithm of a fixed-point number | Value low, its scale high | Value low, with 24 fractional bits; the scale (24) high |

  Fifteen inputs are a spot check, not a measurement; the one readable SDK source that uses `copex` (for a
  vector's length, in the other numbering, marked as not in use) gives its result 22 bits of precision.
  **[sdk]** The use for a drum machine is the one the EQ designer in JieLi's chip library makes of it: filter
  coefficients worked out in the main loop when a knob moves, in place of tables. **[sdk]** **[host]** Untried:
  the other fixed-point functions, `copex` in the audio interrupt or on the second core, and what it costs.

### 3.3 Cache and XIP

- Code and constants run from flash through a cache. `const` tables cost no RAM. **[code]**
- The cache is invalidated one 32-byte line at a time after a flash write, and has an "idle" flag the flash driver
  waits for. **[code]**
- The two cores share the cache, according to the platform's dual-core code. **[code]**, not checked further.
- **While the flash is being erased or programmed, XIP is off.** Any code that runs then must be in RAM and must
  call nothing in flash, with interrupts off. The build checks the RAM section for calls. **[code]**
- Cache size, its policy and the cost of a miss are **not known** (section 13).

### 3.4 The clock

| | V15 head | V16 head | Drumtonic |
| --- | --- | --- | --- |
| CPU clock | 360 MHz | 320 MHz | 320 MHz, set by the firmware at boot |
| High-speed bus (CPU / 2) | 180 MHz | 160 MHz | 160 MHz |
| Low-speed bus (/ 3) | 60 MHz | 53.3 MHz | 53.3 MHz |

- 320 MHz is the highest setting the chip maker's configuration lists. V15's 360 MHz is above it, and the V16
  configuration opens with the maker's warning against overclocking in production. **[stock]**
- The PLL multiplies 2 MHz (the 24 MHz crystal / 12). A 12-bit field holds the multiplier minus 2: 268 gives
  540 MHz, 238 gives 480 MHz, and the CPU takes two thirds of that. **[code]** **[unit]** (the firmware reads the
  field back and shows 320)
- **Changing the clock at run time** follows JieLi's own sequence: CPU on the internal 16 MHz RC, cache off, XIP
  off, write the PLL, a fixed wait (there is no lock flag), XIP and cache back, CPU on the PLL. All of it from RAM,
  interrupts off, before the second core and the audio start. **[sdk]** **[code]** **[unit]**
- Every wait in that sequence is a counted loop. A wait that read a timer hung on some cold boots right after the
  PLL write. **[community]** (X0X, after Melodee)
- **What does not change with the CPU clock:** the timers (24 MHz crystal), USB and the MIDI UART (48 MHz), the
  audio rate. **What does:** your code's speed and the screen's SPI clock, about 11 % between 360 and 320.
  **[code]**
- An older note in this project said Drumtonic runs at whatever the head sets. That is no longer true: it forces
  320 MHz and restarts with a crash screen if the PLL does not say 320. **[code]**

### 3.5 Supplies

The chip's power block is reached through a slow serial port called P33. Its settings **survive a reset**; only
power-off clears them. **[code]**

| Rail | Formula | Boot loader | Stock app (V15 and V16) | Drumtonic |
| --- | --- | --- | --- | --- |
| VDC14 | 1.25 V + 50 mV a level | 1.40 V, from the LDO | 1.60 V, from the internal DC-DC | 1.40 V, DC-DC |
| SYSVDD (core and RAM) | 0.93 V + 30 mV a level | 1.26 V | 1.38 V | 1.26 V |
| VDDIO | 2.8 V + 0.1 V a level | | 3.2 V | 3.2 V |

- The stock app raises the rails as it starts, in both versions. **[stock]**
- On the boot loader's rails at 360 MHz, with both cores loaded, some units read flash and RAM wrong: the second
  core faulted within seconds. Still seen at 1.35 / 1.45 V. **[community]** (Jangada, X0X)
- Move a rail one level at a time, 100 us a step. Going up: VDC14 first. Going down: SYSVDD first, because SYSVDD is
  regulated from VDC14. **[code]**
- Drumtonic's 2 x 320 MHz on 1.26 / 1.40 V from the DC-DC ran for long sessions on one unit. **[unit]** No other
  unit has been tried. On a unit where the second core still faults, Drumtonic would restart again and again.

### 3.6 Interrupts and timing

| What | Value | Source |
| --- | --- | --- |
| Vectors | 128, table in RAM at `0x01C7FE00`. Vector 1 is the CPU exception | **[code]** |
| Priorities | 0..7, higher interrupts a lower one | **[code]** |
| Audio interrupt | Source 11 (ALNK0), priority 3. Every 256 frames: **5.8 ms** | **[code]** |
| System tick | Source 63 (TIMER5), priority 4, **10 kHz**. It outranks the audio and interrupts a render | **[code]** |
| Time base | TIMER4, free-running at **24 MHz** (24 ticks a microsecond). It **wraps every 178.9 s**: always subtract, `now - t0` | **[code]** |
| Watchdog | 8 s as the platform arms it (settings from 1 s to 32 s), in the power block | **[code]** |
| Key scan | One column a tick: a full frame every 1.1 ms. A press counts after 2 frames, a release after 8 (~9 ms) | **[code]** |
| USB, TRS MIDI | Both polled from the tick at 2 kHz. No USB interrupt | **[code]** |

- Nested inside a render, the tick only scans the keys, counts time and feeds the USB audio stream. USB and MIDI
  polling wait until the render is over. **[code]**
- **The audio interrupt starts before your main loop has finished setting up.** Anything it touches must be valid
  from power-on, or guarded by a "ready" flag (section 12, mistake 1). **[unit]**

### 3.7 The two cores

Dinghy as published does not start the second core: its hardware layer has no file for it. **[code]** Drumtonic
adds one, following the SDK's start-up and what Melodee and Jangada found on real units, and renders half its drum
channels and its reverb there. What that took:

- **Start it at power-on, before the audio and the timers.** Started later it never reached its entry, as the
  code notes; where that was first seen is not recorded. **[code]**
- It boots through the chip's ROM. If you use the program-counter guard (section 3.8), open it while the core
  starts, then close it again. **[community]** (Melodee) **[code]**
- Its entry address goes into a fixed word at the top of RAM (`0x01C7FFF8`); a control register holds it or lets it
  run. As the SDK's own start-up does it. **[sdk]** **[code]**
- It has **no interrupts**, its own small stacks (2816 B and 256 B), and a waiting loop that lives **in RAM**, so
  an idle second core never fetches from flash. **[code]**
- **One job at a time.** Core 0 writes a function, an argument and a job number into a mailbox and waits; core 1
  runs it and writes the number back. One writer for each word, a `csync` around each, and each core's words on
  cache lines of their own. **[code]** (the layout after Jangada)
- **A check word.** On some units core 1 reads words that are not in RAM, now and then, mostly under load. So the
  job carries a checksum of function, argument and number, and core 1 runs nothing that does not match.
  **[community]** (Jangada, Melodee) **[code]**
- **The flash and the second core.** The job itself runs from flash. That is safe only because core 1 works for
  the audio interrupt alone, which waits for it, and core 0 switches the flash off only with interrupts off.
  **[code]**
- **Reset.** Hold core 1 before any reset, and first thing at boot, before RAM is cleared: a core left running
  reads the memory you are wiping. A fault on core 1 must not call the reset routine itself (it would hold
  itself and freeze the unit); it only writes the crash record. **[community]** **[code]**
- **The sound must not depend on the core count.** Drumtonic's host test renders every script on one core and on
  two and compares the files bit for bit. **[host]**
- **Failure policy.** Core 0 waits at most 8 ms for a job. Earlier builds fell back to one core for the rest of
  the session. Since 2026-11-16 a core that does not start, misreads a job, or is late shows the crash screen and
  restarts: two cores or none. Some comments in the source still describe the old fallback; the code restarts.
  **[code]** **[unit]** (the restart policy ran on the unit as version 0.3 and later)
- **In the stock firmware:** V15 gives the second core one job, a conversion of the audio to 24 bits. V16 carries a
  loop for FFT jobs that drives the chip's hardware FFT unit (it names the unit's control register and uses the
  SDK's configuration words) **[stock]**; nothing in the app seems to call it. **[stock]** **[inferred]**

### 3.8 The guards and the crash screen

The chip has debugging hardware that the platform turns into a safety net: **[code]**

- a stack limit (a stack overflow is a fault, not silent corruption);
- write-protected windows (the guard bands, and the reserved top of RAM once the vectors are set);
- traps for reads, writes and fetches of unmapped memory;
- a program-counter limit (running outside your code is a fault);
- a trace of the last branches.

A fault draws a red screen with the cause, the program counter (PC) and the return address, waits 30 s so you can
photograph it, then restarts. Look the PC up in the listing the build writes (`build/dinghy.dis`). The record
survives the reset in no-init RAM. **[code]** **[unit]**

One trap for no-init data: declare it non-`static`. A file-local object lets the compiler assume it starts at zero
and remove your "is the magic number there?" test. **[code]**

### 3.9 Measured numbers

| What | Number | Conditions | Source |
| --- | --- | --- | --- |
| Audio block | 256 frames, 5.8 ms | 44.1 kHz | **[code]** |
| A drum voice | about 180 instructions a frame mono, 280 stereo | Counted in the listing, not timed | **[code]** |
| Load, first FM-1 build | about 30 % of a block | 2026-10-28: the drum engine and sequencer only, before the effects and the second core existed, at the V15 head's 360 MHz | **[unit]** |
| Load, 0.9 | 78-81 % | Drumtonic 0.9 (10 drum channels, reverb, delay, compressor, limiter, 24 dB filter) playing one pattern, 2 x 320 MHz; which effects were on was not noted. It is the first core's **longest render of each second**, its wait for the second core included, and it moves about 3 % by itself | **[unit]** |
| Latency | about 7 ms | 5.8 ms (one block) + 1.45 ms (the limiter's look-ahead) | **[code]** |
| Full screen | 115,200 bytes over SPI | 240 x 240 x 2 | **[code]** |
| A strip of 240 x 64 | about 3 ms by DMA | | **[code]** |
| Sector erase | about 50 ms, interrupts off | Puya; the driver allows 500 ms | **[code]** **[unit]** (heard as a dropout) |
| Page program | up to 256 bytes; the driver allows 10 ms | | **[code]** |
| Erase suspend | 22 us | Puya | **[code]** (the driver's note) |
| Flash wear | about 100,000 erases a sector | The usual figure for this kind of part, not measured | **[code]** |
| Whole flash read over the mask ROM | about 2 s | 1 MiB | **[unit]** |
| Saves pulled over USB-MIDI SysEx | 17,146 bytes in 0.85 s | | **[unit]** |
| An install from the browser | about 40 s | | **[unit]** |
| ADC conversion | about 200 us | With the FM-1's ADC setting | **[community]** (fm1-to-smk37) |

Two lessons about measuring:

- **Speed on the computer does not predict speed on the chip.** A rewrite of the voice loop was 20 % faster on the
  host and gained nothing on the FM-1 (`-Os`). It was reverted. **[unit]**
- **A peak meter cannot show a small gain.** Measuring the chip's load needs an average over many blocks. No
  release has one; a development build shows the effects' mean time a render in microseconds, and gave the
  figures of the next point. **[unit]**
- **Fewer instructions did not mean less time.** Drumtonic's effects (reverb, delay, master) were built a second
  time with the float multiply-add of section 3.2 written by hand: 58 of them, 2 % fewer instructions, the same
  samples on the computer **[host]**. On the unit that copy ran in the audio interrupt and on the second core
  without a fault, sounded the same, and was **slower**: about 3,113 us a render against 3,055 us for plain C
  (1.9 %), twelve readings with a switch turned back and forth, each setting steady within 7 us. One build, one
  unit, one pattern. **[unit]** Whether the instruction costs more than a multiply and an add, or the second copy
  of the code just sits less well in memory, is not known.

---

## 4. The platform to start from

Do not start from nothing. [Dinghy](https://github.com/zvenson/dinghy) by zvenson is the smallest firmware for the
FM-1, made to be copied. It gives you the update path, the rescue, USB MIDI and audio, the keys, LEDs and screen,
storage, a crash screen, host mode, tests and the build checks. It carries the platform of
[Felucca](https://github.com/hugelton/Felucca) by Leo Kuroshita (Hügelton Instruments), with the changes of
[SLOOP](https://github.com/isod89/sloop-fm1) by isod89. Its documentation lists every call; Drumtonic ships a copy
in [docs/dinghy/](https://github.com/fc79/drumtonic/tree/main/docs/dinghy).

Your app supplies two functions:

```c
static void app_audio(int32_t *out, uint32_t n);   /* the audio interrupt: add n stereo frames to out */
static void app_main(void);                        /* your main loop; never returns; calls platform_poll() */
```

Everything is one compilation unit: your file includes `platform.c` first. **[code]**

---

## 5. Build and toolchain

| | | Source |
| --- | --- | --- |
| Compiler | JieLi's clang for pi32v2. **Linux x86-64 only** | **[code]** |
| On macOS or Windows | The build runs each tool in a `linux/amd64` container by itself | **[code]**; used on macOS **[host]** |
| SDK | Three files of JieLi's AC79 SDK, tag `AC79NN_SDK_V1.2.1_2023-12-13`, read at build time and put in the package: `uboot.boot`, `cfg_tool.bin`, `eq_cfg_hw.bin` (Apache-2.0). The build warns if their hashes differ | **[code]** |
| Flags | `-Os -ffunction-sections -fno-builtin`; with floats: `-mcpu=r3 -mfprev1 -ffp-contract=off` | **[code]** |
| Output | The app, the update loader, a package `.fwsc`, and the listing `build/dinghy.dis` | **[code]** |

- **Keep `-Os`.** With `-O2` the FM-1's USB-MIDI input babbled on the device. Which line `-O2` breaks is not
  known. **[community]** (OberVave, 2026-10-13)
- **`-ffp-contract=off` on both sides.** A newer GCC on a CI machine fused multiply and add in the float code and
  the computer's reference sound stopped matching bit for bit. Turn contraction off for the chip and for every host
  build. **[host]**
- The build prints image, RAM and pool use and fails above the limits. **[code]**
- The update loader is built to run **entirely from RAM** (it rewrites the flash under itself), and its image is
  stored in LZ4 blocks of at most 4 KiB. **[code]**
- The package (`.fwsc`) is a JieLi "UFW" image with the identity woven into its first bytes: one marker byte after
  every 47 data bytes, for 20 blocks. `tools/fm1pkg_make.py` builds it. **[code]** **[stock]**
- A release can be built by GitHub from a tag, with a build attestation (proof the files came from that source).
  **[code]**

---

## 6. Installing, identities, versions

### 6.1 How an install runs

1. **Check.** The host asks the FM-1 what it runs, then sends "enter upgrade". The running app pulls parts of the
   package, checks them, copies the package's update loader to the staging area (`0xE0000`), writes an update
   record, and resets.
2. **Write.** The SPL finds the record and starts the loader from RAM. The loader pulls the package again and
   writes the app area only. Then it clears its records and resets.
3. **Run.** The new app starts.

- Nothing is committed before the host confirms. A failed check erases the staging area and the old firmware
  carries on. **[code]**
- If power fails during step 2, the record is still there: power-cycle, run the installer again, it resumes.
  **[code]**; a real cable pulled mid-write was tested only against a simulated FM-1 **[host]**.
- The messages are SysEx over USB MIDI: `F0 pack7(00 59 cmd len body checksum) F7`. The device asks, the host
  answers. **[code]** The fm1-to-smk37 guide documents the protocol in full.
- Three ways to install, all the same protocol: a command-line installer (`tools/fm1_install.py`), a browser page
  using Web MIDI (Chrome or Edge), and the [FM-1 Firmware Hub](https://thefirmwarehub.com). **[unit]** (the first
  two)
- The stock V15 app accepts a community package and starts its loader. **[unit]**

### 6.2 Identities

| Who answers | Identity |
| --- | --- |
| Stock firmware | `FM-1_015`, `FM-1_016` |
| A community firmware | `FM-1_9NN` by convention |
| The USB rescue | `FM-1_000` |
| The family's update loader | `ota-FM-1_900` |

- Installers and the hub tell firmwares apart by this text alone. Identities are compared whole. **[code]**
- Keep the form `FM-1_9` + two digits: the installers resume a cut-off install only for loaders they know.
  **[code]**
- **There is no registry, and the public table is not enough.** Drumtonic changed number three times: one number
  was taken by a firmware listed days later, one was answered by another firmware too, and one lay inside a range
  another firmware had reserved as a pattern the table does not show. Use the hub's own check
  (`thefirmwarehub.com/submit/#check`). **[unit]**
- One identity for every build, and a separate version text, works well: the handshake carries the version after
  the identity, and installers show it. **[code]** **[unit]**
- The handshake also carries the flash chip's id, so an installer can warn about an unknown part. **[code]**

### 6.3 The newer flash chip

The platform accepts a flash chip by its id. A firmware that does not know the UC25HQ80 keeps its saves in RAM
only and refuses the next update: safe, but useless. Accept both ids in the app, in the rescue and in the update
loader. Not tried on such a unit here. **[community]** **[code]**

### 6.4 Stock V15 and V16

Read from the two packages, nothing run. **[stock]**

- The update loader (`ota.bin`) is byte for byte the same in both.
- The app's update code is a different build in V16.
- The SPL is a different build: 104 bytes more code, a new setting name `USB_UPDATE_CUSTOM`. V15's SPL is nearly
  the chip maker's SDK copy, and identical to the SMK-37's.
- The clock is 320 MHz instead of 360, with the boot rails at the maker's defaults.
- The flash layout moves (section 2.3). The first 256 factory sounds change.
- The community installers never write the head, so after V16 a community firmware runs on V16's head until M-VAVE's
  updater puts V15 back. **[inferred]** from what each tool writes.

---

## 7. Backup, getting back, recovery

### 7.1 The full backup (do this first)

Drumtonic's [BACKUP.md](https://github.com/fc79/drumtonic/blob/main/docs/BACKUP.md) has the commands. In short:

1. Start [jl-uboot-tool](https://github.com/kagaimiq/jl-uboot-tool) (kagaimiq) waiting for the device, with a
   `read 0 0x100000` command given twice.
2. Send the **UBOOT soft key**, the SysEx `F0 22 24 35 7D F7`, as one 8-byte USB-MIDI transfer. The firmware
   restarts into the mask ROM, which appears as a USB disk named `WL82 UBOOT1.00`.
3. The tool loads a helper into RAM and reads the flash. Compare the two files. Power-cycle.

- Nothing is written to flash. **[unit]**
- **The stock V15 app honours the soft key**, and so do the family's apps and loaders. **[unit]** The fm1-to-smk37
  guide says the stock app does not; on a stock FM-1 V15 and a stock SMK-37 V15 it did. V16 was not tried.
- **The mask ROM leaves by itself** after a short idle time. Start the reader first, then send the key. **[unit]**
- Use only `read`. The same tool can erase and write anything.
- On macOS the tool needs an adapter (Drumtonic ships one) and `sudo`. **[unit]**

### 7.2 Backing up what your firmware saved

A full dump is clumsy for users. Drumtonic also pulls its own saves over the updater's SysEx channel and can write
them back, with a question on the screen first. The whole thing was checked against a full dump, record by record.
**[unit]** If your firmware has saves worth keeping, give it something like this.

### 7.3 The recovery ladder

| Situation | What happens | Tried here? |
| --- | --- | --- |
| A crash | Crash screen, 30 s, restart | Yes **[unit]** |
| Two failed boots in a row | The firmware opens the **USB rescue** by itself: no sound, only the updater, identity `FM-1_000`. Install a fixed build from there | Yes **[unit]** |
| You want the rescue | Hold **OCT-** while switching on | Platform feature **[code]**; not logged as tried in this project |
| The rescue itself resets | Next boot goes to the mask ROM | No **[code]** |
| The firmware runs and you want the mask ROM | The UBOOT soft key | Yes **[unit]** |
| Nothing answers over USB | A USB dongle that sends the chip's boot key at power-on: [FM-1 Transporter](https://github.com/kurogedelic/FM-1-transporter), [MvaveFM1Unbricker](https://github.com/Quixotic7/MvaveFM1Unbricker) | No **[community]** |

A boot counts as good after 30 s of running. The boot guard lives in RAM only, so it **cannot rescue an image that
fails before it reaches the guard**; that is what the mask ROM is for. **[code]**

### 7.4 Leaving your firmware

- **To another community firmware and back:** Drumtonic to Dinghy 0.5 and back, with the command-line installer,
  each step answering the expected identity. **[unit]**
- **Back to the stock firmware:** install M-VAVE's package with M-VAVE's updater or through the hub. **Never tried
  here.**
- **One report** says the official updater, started from a custom firmware, erased a unit's calibration. Which
  instrument it was is not recorded. Keep your dump before you try. **[community]**
- **Writing your dump back** through the mask ROM: not tried, and the ranges to write on an FM-1 have not been
  worked out. If it is ever needed: whole sectors only, cut from your own dump, never the head or `key_mac` unless
  they are damaged.

---

## 8. Audio and DSP

### 8.1 The contract

- `app_audio()` runs in the audio interrupt, every 5.8 ms, with 256 stereo frames at 44.1 kHz. **[code]**
- The buffer arrives **zeroed**: add to it. Full scale is +-32767; more is clipped. **[code]**
- After you come the MASTER knob, the clip, the USB audio stream and the codec. The codec gets the signal 6 dB under
  its full scale so the output stage does not distort. **[code]**
- USB audio shows the FM-1 as a 16-bit stereo input at 44.1 or 48 kHz; at 48 the platform resamples. The computer
  records exactly what the output plays. **[code]** **[unit]**

### 8.2 Budget

- You have 5.8 ms, and less in practice: the tick interrupts you 58 times in a block. **[code]**
- **Aim under half** (2.9 ms) for a worst case. Time it with the 24 MHz counter before and after. **[code]**
- Late means crackle. Never-ending means a watchdog reset. **[code]**
- No flash, no screen, no waiting, no `platform_poll()` inside it. **[code]**

### 8.3 Practices that held up

- **Work per frame only where it must be per frame.** Envelopes, filter coefficients, LFOs: once per block of 16
  or 32 frames. Drumtonic renders in blocks of 16 frames and its sequencer clock ticks there. **[code]** **[unit]**
- **An oscillator is a 32-bit phase that wraps.** `inc = f * 2^32 / 44100`, `ph += inc`: no division at run time.
  **[code]**
- **Fixed point for voices, float where it pays.** Drumtonic's drum voices are integers; its effects and master are
  floats, because writing a compressor or a limiter in fixed point is slow work and easy to get wrong. Both run on
  the unit. **[unit]**
- **Delay lines as integers.** 16-bit (or fewer) samples halve the memory, and integer lines never meet tiny float
  values (section 13). **[code]**
- **Let idle effects sleep.** Reverb and delay stop computing when their input and their lines are silent.
  **[code]**
- **Mind the widths.** An oscillator step was cut to 32 bits before its clamp, and a pitch swept down from above
  half the sample rate wrapped. **[host]**

### 8.4 Talking between the interrupt and the main loop

- `volatile` variables, **one writer each**. A 32-bit store is atomic; a struct is not. **[code]**
- Write the parts of a change in an order that is safe to see half-done (pitch before gate). **[code]**
- For anything bigger, keep two buffers: the main loop fills the one the interrupt is not reading, then publishes it
  with a single 32-bit write. Put a generation counter in that word if "the same again" must be told from "new".
  **[code]** **[host]**
- Events from the interrupt to the main loop go through a small ring, so nothing is missed when the main loop is
  slow (a redraw, a save). **[code]**

### 8.5 What caused glitches

| Symptom | Cause | Fix | Source |
| --- | --- | --- | --- |
| The sound distorts or drops while saving | A sector erase holds interrupts off for about 50 ms; two erases, 100 ms | Erase and program in slices (section 9.2), or save only when silent | **[unit]** |
| A chord or a fast controller stream stumbles during a redraw | The main loop waits for the screen | `LCD_IDLE`: read MIDI and keys while the screen's DMA runs | **[code]** |
| Button LEDs flicker | The app cleared every LED and set them again each loop; the scan, in the interrupt, caught them dark | Compute the whole picture, then write only the LEDs that changed | **[unit]** |
| Everything stops for a moment, LEDs included | A save made by itself (an automatic save) | No automatic saves: only an explicit SAVE writes | **[unit]** |

---

## 9. Storage

### 9.1 What the platform gives

Three kinds of place, each kept as `[magic][length][crc32]` then the bytes: **[code]**

| | Size | Where (FM-1) |
| --- | --- | --- |
| Settings | 1 sector (up to 4084 bytes) | `0xFC000` |
| Blobs 0..7 | 1 sector each | `0xF4000-0xFBFFF` |
| Slots 0..7 | 4 sectors each (up to 16,372 bytes) | `0x97000-0xB6FFF` |

- A wrong magic, length or CRC reads as "nothing saved": start from defaults. **[code]**
- **The header is written last**, so a save cut off by a power loss stays invalid instead of half-valid. **[code]**
- **Give your firmware its own magic number**, so it never takes another firmware's data for its own. **[code]**

### 9.2 Saving without stopping the sound

An erase or a page program can be **suspended**: start it, let it run 400 us with interrupts off, suspend it (the
flash reads again, XIP comes back), let the interrupts run, resume. Drumtonic saves this way while it plays.
**[unit]** (Puya)

- On the UC25HQ80, erase suspend is untested. If the part ignores the suspend, a slice simply lasts until the erase
  ends: a stall of about 50 ms, not a failure. **[code]**

### 9.3 Wear

- About 100,000 erases a sector. **Never save in a loop**, and not on every knob step. **[code]**
- Write only what changed. Drumtonic compares its settings with what the flash holds and skips the write if they
  are equal; a slot save erases only the sectors its length needs. **[code]**

### 9.4 Formats

- **Every record starts with a format number.** Convert an old format on first load; never overwrite one you do
  not recognise without saying so. **[code]** **[host]**
- **Append, do not reorder.** A new field added at the end, stored so that zero means "as before", keeps old
  saves sounding the same bit for bit. **[host]** **[unit]**
- **Think about going back.** When a record grew (more automation lanes, songs in the settings record), an older
  version refused it and started from defaults. That is safe, but users must be told before they downgrade.
  **[host]**
- **Check everything you load.** A length inside the record is not proof: bound every read by the area's size, not
  by a stored length. **[code]**

### 9.5 Sharing the flash with other firmwares

- The family's firmwares use the same ranges. Say in your documentation which sectors you write, and remind users
  to back up. **[code]**
- Do not read another firmware's data. Treat anything without your magic as empty. **[code]**
- Stay off `0xFA000-0xFBFFF` if you want a clean return to stock V15: stock data lies there. **[inferred]**

---

## 10. Interface, in brief

The panel is small and is often played standing, in the dark. What worked on the unit: **[unit]**

- **Draw only what changed.** The screen keeps what you draw; a full redraw is 115 KB. Draw into a canvas and send
  it in one go to avoid flicker. **[code]**
- **Big type.** Values at 30 px and more. Confirmation questions at 22 px were too small on the real screen; they
  went to 31 and 38 px.
- **Fixed places.** The four knobs' names and values along the top, level with the real knobs; the page in the
  middle; a state band along the bottom.
- **One button, one page.** The lit button says where you are, without reading the screen.
- **Few combinations**, in families that always mean the same thing (one button is always "copy", one is always
  "go and come back").
- **Ask before anything is lost**, with "no" and "yes" on two buttons in the order they sit on the panel.
- **Hold a button to see what it does** (250 ms).
- **Knobs accelerate from their speed**, measured as detents over time since the last read, so a slow main loop
  does not hide a fast turn. An encoder detent is one full quadrature cycle.
- **Dim LEDs as landmarks.** A short pulse each scan gives about 1/24 brightness: "something is here".
- **Check on the real screen.** A photo of the unit showed text cut off and bands misaligned that the computer's
  screenshots had made look fine.

fc79 keeps a fuller interface guide for FM-1 firmwares (pages, gestures, timings, screen bands, lights). It is in
Italian and not published yet.

---

## 11. Test on the computer first

Host mode runs your unchanged app on a computer: the screen comes out as images, the sound as a WAV file, and a
script presses keys and turns knobs over time. **[code]**

What paid off in Drumtonic:

- **Scripts with expectations.** A script line asks the app for a value and fails the run if it is wrong. Every
  feature gets a script; every bug gets one that fails before the fix. **[host]**
- **Byte-for-byte comparisons.** After a refactor, every script must give the same WAV, screens and log. This is
  what makes large changes to an audio engine safe. **[host]**
- **AddressSanitizer.** On the FM-1 a write past an array lands silently on whatever is next in RAM. A text
  buffer overran into a neighbouring variable for days; ASan on the host found it, and later a second one in the
  automation code. **[host]**
- **A fake flash file** that persists between runs, to test "save, restart, load" and old formats. **[host]**
- **One core against two**, compared bit for bit. **[host]**
- **The installer against a simulated FM-1**: wrong model, cut-off install, resume. **[host]**
- **The same firmware in the browser** (WebAssembly), checked to give the host build's sound bit for bit, so people
  can try it without the instrument. **[host]**

What host mode **cannot** show:

- **Timing and boot order.** The host calls the audio only after the main loop is set up; the FM-1 does not.
- **The real cost.** Count on installing to learn the load.
- **Compiler differences.** The chip's clang inlines and unrolls differently.
- **How it looks and feels.** Sizes, brightness, the knobs.

So the last test is always the instrument, with a backup taken first.

---

## 12. Mistakes already made

| # | What went wrong | Why | How to avoid it |
| --- | --- | --- | --- |
| 1 | A new build crashed at power-on, twice, and landed in the rescue | The audio interrupt ran before the main loop had initialised the reverb, and read through pointers not yet set. The host never showed it | Everything the audio interrupt touches is valid from power-on, or behind a "ready" flag |
| 2 | The sound distorted during a save | A save with interrupts off for two erases (~100 ms) | Sliced saves (9.2) |
| 3 | The identity had to change three times | Chosen from a public table that does not show reserved ranges | The hub's own check (6.2) |
| 4 | Saves kept in RAM only, updates refused, on newer units | The flash chip's id was unknown to the firmware | Accept both ids, in app, rescue and loader (6.3) |
| 5 | A variable changed "by itself" | A text buffer overran into its neighbour | ASan on the host (11) |
| 6 | A pitch sweep wrapped around | A value cut to 32 bits before its clamp | Clamp first, then narrow |
| 7 | An optimisation was kept that did nothing | Measured on the host, and on the chip with a peak meter | Measure on the chip, with an average (3.9) |
| 8 | The computer's reference sound stopped matching | A newer compiler fused float operations | `-ffp-contract=off` everywhere (5) |
| 9 | A build failed on a "ROM address" nobody wrote | `-32767 << 7` equals `0xFFC00080` | Rearrange the arithmetic (3.2) |
| 10 | Correct code crashed on a divide | The compiler moved a divide ahead of its guard | The trap is off; test divisors (3.2) |
| 11 | USB-MIDI input babbled | `-O2` | Keep `-Os` (5) |
| 12 | Button LEDs flickered | All LEDs cleared and set again every loop | Write only changes (8.5) |
| 13 | The second core never started | It was started after the audio and the timers | Start it first (3.7) |
| 14 | A boot hung now and then | A timer-based wait right after the PLL write | Counted loops (3.4) |
| 15 | An older version lost its patterns after a downgrade | A record had grown | Format numbers, and tell users (9.4) |
| 16 | Flash regions were noted 16 KiB too high | An unpacker added `0x4000` to absolute offsets | Check against a dump (2.3) |
| 17 | A boot on one core surprised the user | A silent fallback after a crash | Fail loudly: crash screen, restart (3.7) |
| 18 | The crash screen showed a wrong uptime | The 24 MHz counter wraps every 179 s | Use the millisecond counter for long times (3.6) |
| 19 | A knob's acceleration almost never triggered | Fixed thresholds per loop pass | Measure speed over time (10) |
| 20 | Screen text was cut off on the unit | Layout judged from computer screenshots | Photograph the real screen (10) |

Sources: 1-5, 7, 12, 15, 17, 19, 20 **[unit]**; 6, 8 **[host]**; 9-11, 13-14, 18 **[community]** and **[code]**;
16 **[unit]**.

---

## 13. Open questions

**The CPU**

- **Float state and interrupts.** The interrupt wrapper saves only `r0-r3` and the status and return registers,
  the tick interrupt nests inside the audio interrupt, and floats are used in the interrupt and in the main loop.
  It works on the unit. The reason is now mostly clear: compiled listings show float values in the general
  registers, and every compiled function saves the registers from `r4` up that it uses, so the wrapper's
  `r0-r3` are enough. **[host]** Still not known: whether the FPU keeps status flags or a rounding mode of its
  own that an interrupt could disturb.
- **Tiny (subnormal) floats.** Whether the FPU traps on them, slows down or does not care is not known. The SDK's
  fault table for this chip names five FPU causes, "tiny" among them, but not whether they are switched on.
  **[sdk]** Drumtonic avoids them (integer delay lines). A float reverb or filter left to decay will meet them.
- **Multiply and add in one step.** Mostly answered. The compiler never writes the float multiply-add
  instruction, in any mode tried **[host]**, and written by hand it rounds twice, like plain C (section 3.2)
  **[unit]**: the chip gives no reason for its samples to differ from the computer's. `-ffp-contract=off` is
  there for the host's own compiler (section 5). Still open: why the multiply-add made Drumtonic's effects
  slower (section 3.9), and whether it is faster anywhere.
- **The hand-only instructions** (section 3.2). The stock firmware needs nothing beyond `-mcpu=r3`, and the
  float multiply-add, the saturating add, one packed pair and `copex` gave right values on one unit. Still open:
  what each costs; the saturating add, the packed forms and `copex` inside the audio interrupt or on the second
  core (only the multiply-add ran there); the other packed forms, the 16-bit multiply-accumulate ones above all;
  `copex`'s error over its whole range, whether an interrupt may cut into it, and its fixed-point functions other
  than the logarithm; whether the chip has hardware `double` (`fprev2`) at all, for which nothing was found.
  Also open: whether an illegal instruction really brings up the crash screen. That control was never run, so a
  test must compare results with values computed on a computer (a quiet screen proves nothing), and must not run
  at boot (two failed starts send the unit to the rescue).
- **The hardware FFT and vector unit.** JieLi's SDK has a driver for one on this chip (`JL_FFT`, four registers
  at `0x01EE0400`): FFTs of 10 to 2,048 points on 32-bit integers, and arithmetic over whole arrays of 8-, 16- or
  32-bit integers (add, multiply, multiply-accumulate, dot product, clip). No floats. The driver hands it a block
  of words in RAM, polls a "done" bit, and lets one user in at a time; the unit reads and writes the RAM by
  itself. **[sdk]** The answers JieLi's own self-test stores are the plain transform to within a few counts, not
  bit-exact. **[sdk]** **[host]** Stock V16 carries code that drives it and nothing that calls that code (section
  3.7), so the stock firmware does not show that it works on an FM-1. **[stock]** **[inferred]** Never run
  here. Also open: whether it needs a clock or power switch set first, and whether the write guards see what the
  unit itself writes.
- **The cache:** size, policy, the cost of a miss, how much XIP really costs against RAM code.
- **Which line `-O2` breaks** in the USB code.
- **Cycle costs.** There are instruction counts and load percentages, but no table of what a multiply, a divide, a
  float operation or a flash read costs.

**Clock, power, the second core**

- **Does 2 x 320 MHz at 1.26 / 1.40 V run on every unit?** One unit so far. The units whose second core faulted
  were all measured at 360 MHz.
- **The lowest safe rails** for one core and for two.
- **The second core's wrong reads** on some units: the cause is not known, only the defence (the check word).

**Flash and the stock firmware**

- **The UC25HQ80:** installs, saves and erase suspend on a real unit.
- **Stock V16:** whether installs from it really fail and why; whether its SPL still accepts the family's
  updates; whether it writes the USER area where the family keeps blobs and settings; what `USB_UPDATE_CUSTOM`,
  `farg.cfg` and `blimit.bin` are for.
- **Does M-VAVE's updater rewrite the head?** Assumed from the fact that V16 changes it. Not observed.
- **Back to stock from a community firmware**, and **writing a dump back**: never tried here.
- **Data at `0xE5000-0xE6FFF` and `0xE8000`** appeared on the unit after the first install and stayed. Nothing in
  Drumtonic's allow-list writes there. A likely cause: the stock app stages the incoming loader in the last five
  sectors of its own settings area (`0xE4000-0xE8FFF`, as it does on the SMK-37), and the family's clean-up covers
  only `0xE0000-0xE4FFF`. **[inferred]**, not traced.
- **The battery reading:** the divider ratio, and so the voltage.

**Tools**

- **A real load meter** (average, per stage) on the chip. So far only a mean time for the effects, in a
  development build (section 3.9).
- **M-VAVE's "FM-1 Open" workbench:** how it relates to this platform, and whether firmwares built with it can share
  the flash safely with the family's.

If you know the answer to one of these, please say so where this guide is published.

---

## 14. Credits and sources

This guide stands on other people's work. The facts tagged **[code]** and **[community]** are theirs.

| Who | What |
| --- | --- |
| Leo Kuroshita (@kurogedelic), Hügelton Instruments | [Felucca](https://github.com/hugelton/Felucca): the platform (boot, hardware layer, USB, updater, loader, flash driver, guards), and [FM-1 Transporter](https://github.com/kurogedelic/FM-1-transporter) |
| isod89 | [SLOOP](https://github.com/isod89/sloop-fm1): the platform's changes, the division-trap decision |
| zvenson | [Dinghy](https://github.com/zvenson/dinghy): the starting point, host mode, the build checks, storage, the documentation this guide leans on; OberVave |
| M. Geisler (genkigenki) | The SMK-37 board layer and the [fm1-to-smk37](https://github.com/genkigenki/fm1-to-smk37) guide (hardware, update protocol, pitfalls) |
| Charles Vestal | X0X: the interface model, the supplies and the clock change, and the other parts Drumtonic's LICENSING.md lists as made after it |
| keremimo | [Melodee](https://github.com/keremimo/melodee): the dual-core work, the clock change's origin |
| zednaked | [Jangada](https://github.com/zednaked/jangada): the second core's faults measured, the mailbox layout |
| The 9W9 project | The effects' algorithms Drumtonic's are written after |
| illuminus5 | [Skiff](https://github.com/illuminus5/skiff): a loader that draws |
| ChanceTheMaker, keremimo | Felucca [Salt]: the TRS MIDI ring fix |
| sloopDX | The TRS MIDI parser, the audio block size |
| spinkham | The division-trap fix proposed to SLOOP |
| kagaimiq | [jl-uboot-tool](https://github.com/kagaimiq/jl-uboot-tool), jl-misctools, ghidra-jieli |
| Quixotic7 | [MvaveFM1Unbricker](https://github.com/Quixotic7/MvaveFM1Unbricker) |
| The FM-1 Firmware Hub | [thefirmwarehub.com](https://thefirmwarehub.com): the catalogue, the installer, the identity check |
| JieLi | The [AC79 SDK](https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK) (Apache-2.0) and the toolchain |

Drumtonic's own source and documents: <https://github.com/fc79/drumtonic> (GPL-3.0-only). Who made which file:
its [LICENSING.md](https://github.com/fc79/drumtonic/blob/main/LICENSING.md).

"M-VAVE", "FM-1" and "SMK-37" belong to their owners. This guide is not made or endorsed by them.

fc79, November 2026
