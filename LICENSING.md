# Licensing

Drumtonic, like the Dinghy it is built on, is free software under the GNU General Public License, version 3 only (`GPL-3.0-only`, full text in
`LICENSE`). You may use, study, change and share it. If you give someone a firmware built from it, give them its
complete source under the same licence too.

## Who made what

| | |
| --- | --- |
| The platform: `firmware/hal/`, `firmware/loader/`, `crt0.S`, `app.ld`, `firmware/src/` (`usb.c`, `ota.c`, `recovery.c`, `lcd.c`, `gfx.c`, `libc.c`, `bootguard.h`, `uac_fir.h`), `tools/` | Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments ([Felucca](https://github.com/hugelton/Felucca)), with the changes of [SLOOP](https://github.com/isod89/sloop-fm1) (isod89) |
| The SMK-37 board layer: `firmware/hal/smk_input.h`, `fm1_flash_map.h`, the SMK-37 parts of `fm1_cc.h`, `fm1_flash.h`, `fm1_lcd_hw.h`, `lcd.c`, `ota.c`, `bootguard.h`, `firmware/loader/`, `tools/fm1pkg_make.py`, `tools/fm1_install.py`, `tools/build.py`; `firmware/src/smk_wheel.h`; `tests/ota_test.c`, `ldr_test.c`, `install_test.py`, `smk_wheel_test.c`, `smk_pkg_test.py` | Copyright (C) 2026 M. Geisler (genkigenki/makkuro), from [genkigenki/sloop-fm1](https://github.com/genkigenki/sloop-fm1) branch `smk37` (the reference port of the [fm1-to-smk37](https://github.com/genkigenki/fm1-to-smk37) kit) |
| The TRS MIDI IN `firmware/src/midi_uart.c` | Felucca's, as sloopDX carries it, with the ring read by content from Felucca [Salt] 0.9-salt14 by ChanceTheMaker (found by keremimo) |
| The update loader's screen: `firmware/loader/ldr_screen.c`, `tests/ldr_screen_test.c` | Copyright (C) 2026 zvenson (from OberVave, the Firmware Hub's look); the idea, a loader that draws, its 5 x 7 letters and the order of things from [Skiff](https://github.com/illuminus5/skiff) by illuminus5, GPL-3.0 |
| From OberVave: the storage (blobs, slots), `LCD_IDLE`, the SMK-37 wiring of `platform.c`, `store.h`, the pad event queue in `smk_input.h`, `tests/smk_flash_test.c`, `tests/run_tests.sh`, the CI workflow | Copyright (C) 2026 zvenson |
| The app `firmware/src/dinghy.c`, the wiring `platform.c`, the host mode `tests/`, the docs | zvenson |
| After X0X by Charles Vestal (GPL-3.0): the FM-1 interface's design, the input and page model of `ui_fm1.c`, the lanes of `motion.c`, the LFOs of `lfo.c`, `fl_slice_ram` in `firmware/hal/fm1_flash.h`, `hal/fm1_isr.S`, the supplies and clock of `hal/fm1_power.h` and `fm1_clock.h`; the algorithms of `fx.c` (after 9W9's send reverb, delay and master, GPL-3.0, written again here) | Copyright (C) 2026 fc79 |
| `tools/gen_saira.py` (after `tools/gen_font.py` by the same author) | Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments |
| Drumtonic: `firmware/src/drumtonic.c`, `app_smk.c`, `app_fm1.c`, `ui_smk.c`, `ui_fm1.c`, `seq_fm1.c`, `seq_fm1.h`, `motion.c`, `motion.h`, `lfo.c`, `fx.c`, `fx_param.h`, `dinghy.c` (now its entry), `drum.c`, `drum.h`, `seq.c`, `seq.h`, `kits.c`, `firmware/gen/drum_tables.h`, `pad_curves.h`; `tools/gen_drum_tables.py`, `gen_pad_curves.py`, `kit_values.py`, `tools/backup/`; `tests/drum_render.c`, `run_drums.sh`, `smk_pad_test.c`, `tests/scripts/drumtonic*.txt`; the pad changes in `firmware/hal/smk_input.h` (re-arm, peak, crosstalk, velocity) and `SMK_MASTER_FROM_APP`; `README.md`, `docs/DRUMS.md`, `DRUMTONIC.md`, `BACKUP.md` | Copyright (C) 2026 fc79 |

## Third-party material

| What | Licence | Where |
| --- | --- | --- |
| Terminus font 8x16 (ter-u16n), rendered into `firmware/gen/felucca_font.h` | SIL OFL 1.1 | `assets/fonts/Terminus-LICENSE.txt` |
| Saira Condensed Medium and Bold (Copyright 2016 The Saira Project Authors, from Google Fonts), rendered into `firmware/gen/saira_font.h` (the FM-1's screen) | SIL OFL 1.1 | `assets/fonts/SairaCondensed-OFL.txt` |
| `tools/backup/jl-uboot-tool-macos/`: a macOS SCSI port and a patch for [jl-uboot-tool](https://github.com/kagaimiq/jl-uboot-tool) (Copyright (c) 2023 Andrey Grigoryev, kagaimiq), which they extend | MIT, as jl-uboot-tool | the files' headers |
| JieLi AC79 SDK: `uboot.boot`, `cfg_tool.bin`, `eq_cfg_hw.bin` are read from your SDK checkout at build time and placed in the package; no SDK files are in this tree | Apache-2.0 | <https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK> |

## The logos

`assets/logo.svg` / `logo.png` carry the designburg signet: Copyright (C) designburg, all rights reserved, not under
the GPL. It marks Dinghy itself; a fork uses its own logo.

`assets/drumtonic/` is the Drumtonic logo: Copyright (C) 2026 Federico Ciapi, all rights reserved, not under the GPL.
It marks Drumtonic itself; a fork uses its own logo.

## Trademarks

"Felucca" and "Hügelton Instruments" are names of Hügelton Instruments; "M-VAVE", "FM-1", "SMK-37" and "MKE-P37"
belong to their owners. Dinghy is not made or endorsed by them.
