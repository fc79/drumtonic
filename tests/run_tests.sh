#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Dinghy's host tests (no hardware). Run from the repo root after building, the SMK-37 first, the FM-1 last
# (both boards write the same build/ intermediates; the tests expect the FM-1 binaries):
#   BOARD=smk37 sh build.sh; BOARD=smk37 MODEL=elite sh build.sh; BOARD=smk37 MODEL=mke sh build.sh; sh build.sh
#   sh tests/run_tests.sh
# The FM-1 app on the host (tests/run_host.sh), MIDI and the SMK-37 on the host (tests/scripts), the flash map, the
# update path (ota.c, the loader, tools/fm1_install.py; from the fm1-to-smk37 kit's reference layer,
# genkigenki/sloop-fm1 smk37) and the packages.
set -e
export AC79_SDK="${AC79_SDK:-$HOME/fw-AC79_AIoT_SDK}"   # (fm1pkg_make.py: the SDK files of a package)
cd "$(dirname "$0")/.."
OUT=build/host
mkdir -p "$OUT"
CC="${CC:-cc} -O1 -Wall -Wno-unused-function"
fail=0
run() { echo "== $1"; shift; "$@" || fail=1; }

[ -f build/drumtonic.fwsc ] || { echo "run ./build.sh first"; exit 1; }

run "FM-1 app on the host: the default script plays, screens and sound" sh tests/run_host.sh
run "SMK-37 app on the host: the default script" env BOARD=smk37 sh tests/run_host.sh
run "FM-1 MIDI: USB, the TRS jack, a clock, the channel" sh tests/run_host.sh tests/scripts/midi.txt
run "SMK-37: velocity, a pad, an outer key, the pitch wheel" env BOARD=smk37 sh tests/run_host.sh tests/scripts/smk37.txt
run "Drumtonic, FM-1: every page, a held step, a help card, REC, PRESETS" sh tests/run_host.sh tests/scripts/drumtonic_fm1_tour.txt
run "Drumtonic, FM-1: the opening stays until a button, which only closes it (expect)" sh tests/run_host.sh tests/scripts/drumtonic_fm1_open.txt
run "Drumtonic, FM-1: a key that closes the opening does nothing else (expect)" sh tests/run_host.sh tests/scripts/drumtonic_fm1_open2.txt
run "Drumtonic, FM-1: ALGORITHM moves the steps on SEQ only (expect)" sh tests/run_host.sh tests/scripts/drumtonic_fm1_algo.txt
run "Drumtonic, FM-1: a sound pasted on another channel keeps its name (expect)" sh tests/run_host.sh tests/scripts/drumtonic_fm1_sound.txt
run "Drumtonic, FM-1: the sound library: preview, load, save, delete (expect)" sh tests/run_host.sh tests/scripts/drumtonic_fm1_library.txt
run "Drumtonic, FM-1: OCT- / OCT+ change the bank on EDIT (expect)" sh tests/run_host.sh tests/scripts/drumtonic_fm1_oct.txt
run "Drumtonic, FM-1: steps, accents, conditions and FILL, PRESETS and the original, ALGORITHM, ALL (expect)" sh tests/run_host.sh tests/scripts/drumtonic_fm1_steps.txt
run "Drumtonic, FM-1: GLO copy / paste / clear, SAVE and HOME + REC, a chain, kits, mutes, solo (expect)" sh tests/run_host.sh tests/scripts/drumtonic_fm1_glo.txt
run "Drumtonic, FM-1: a p-lock, knob motion, an LFO put on and taken off (expect)" sh tests/run_host.sh tests/scripts/drumtonic_fm1_motion.txt
run "Drumtonic, FM-1: an external clock, the downbeat, the steps made ahead of the latency (expect)" sh tests/run_host.sh tests/scripts/drumtonic_fm1_clock.txt
run "Drumtonic, FM-1: the reverb and the delay sent (expect)" sh tests/run_host.sh tests/scripts/drumtonic_fm1_fx.txt
run "Drumtonic, FM-1: micro timing, a step held + OCT-/OCT+ (expect)" sh tests/run_host.sh tests/scripts/drumtonic_fm1_micro.txt
run "Drumtonic, FM-1: micro timing on knob 2, stopping at 0 on its way across (expect)" sh tests/run_host.sh tests/scripts/drumtonic_fm1_micro_knob.txt
run "Drumtonic, FM-1: HOME + OCT- / OCT+ undo and redo; a change after an undo keeps the redo; a knob, a REC pass (expect)" sh tests/run_host.sh tests/scripts/drumtonic_fm1_undo.txt
run "Drumtonic, FM-1: GLO and HOME held alone show their guide after 250 ms; a gesture with them, a big bubble (expect)" sh tests/run_host.sh tests/scripts/drumtonic_fm1_bubbles.txt
run "Drumtonic, FM-1: a long message stays in its buffer (black 1 picks BD after one) (expect)" sh tests/run_host.sh tests/scripts/drumtonic_fm1_msg.txt
run "Drumtonic, FM-1: the chosen track's black key: blinking stopped, lit playing on SEQ, EDIT, ENV (expect)" sh tests/run_host.sh tests/scripts/drumtonic_fm1_chled.txt
run "Drumtonic, FM-1: playing, the chosen track's black key goes dim as it plays (expect)" sh tests/run_host.sh tests/scripts/drumtonic_fm1_chled_hit.txt
run "Drumtonic, FM-1: black keys mute only on LIVE, pick the channel elsewhere (expect)" sh tests/run_host.sh tests/scripts/drumtonic_fm1_mute.txt
run "Drumtonic, FM-1: a step held + ARP sets its ratchet; the step plays its hits (expect)" sh tests/run_host.sh tests/scripts/drumtonic_fm1_ratchet.txt
run "Drumtonic, FM-1: a late step's ratchet runs over the step's end (expect)" sh tests/run_host.sh tests/scripts/drumtonic_fm1_ratchet_micro.txt
run "Drumtonic, FM-1: a ratchet goes first: the next hit on its channel does not cut it (expect)" sh tests/run_host.sh tests/scripts/drumtonic_fm1_ratchet_first.txt
run "Drumtonic, FM-1: a part's length changed by PRESETS: back in step with the pattern (expect)" sh tests/run_host.sh tests/scripts/drumtonic_fm1_partlen_align.txt
run "Drumtonic, FM-1: note repeat on SEQ: ARP, the black keys, the rate, REC (expect)" sh tests/run_host.sh tests/scripts/drumtonic_fm1_noterepeat.txt
run "Drumtonic, FM-1: note repeat at 1/8, 1/8T, 1/4, 1/4T; its rate on knob 3 of SEQ (expect)" sh tests/run_host.sh tests/scripts/drumtonic_fm1_noterepeat_rate.txt
run "Drumtonic, FM-1: note repeat owns the black keys: HOME / GLO + a black key repeat it; a key held when it goes on (expect)" sh tests/run_host.sh tests/scripts/drumtonic_fm1_noterepeat_keys.txt
run "Drumtonic, FM-1: note repeat + REC on a part looping each repeat: no ratchet (expect)" sh tests/run_host.sh tests/scripts/drumtonic_fm1_noterepeat_loop.txt
run "Drumtonic, FM-1: REC FREE keeps the micro timing (hand, MIDI); QUANT, SAVE, undo, GLO + QUANT (expect)" sh tests/run_host.sh tests/scripts/drumtonic_fm1_recfree.txt
run "Drumtonic, FM-1: note repeat recorded with REC FREE keeps each hit's micro timing (expect)" sh tests/run_host.sh tests/scripts/drumtonic_fm1_recfree_nr.txt
run "Drumtonic, FM-1: the state band leaves fields out, never overlaps (expect)" sh tests/run_host.sh tests/scripts/drumtonic_fm1_band.txt
run "Drumtonic, FM-1: the knobs accelerate when spun, also on a slow main loop; TEMPO never (expect)" sh tests/run_host.sh tests/scripts/drumtonic_fm1_knob_accel.txt
run "Drumtonic, FM-1: each pattern's kit, loaded as it starts, edits kept, revert, undo, SAVE (expect)" sh tests/run_host.sh tests/scripts/drumtonic_fm1_patkit.txt
run "Drumtonic, FM-1: beat repeat on HOME: ARP on, whites 1-4, an empty slice, back in time (expect)" sh tests/run_host.sh tests/scripts/drumtonic_fm1_beatrepeat.txt
run "Drumtonic, FM-1: beat repeat on a wobbling MIDI clock, tempo changes (expect)" sh tests/run_host.sh tests/scripts/drumtonic_fm1_beatrepeat_ext.txt
run "Drumtonic, FM-1: beat repeat, another white while it loops: a piece of what is heard (expect)" sh tests/run_host.sh tests/scripts/drumtonic_fm1_beatrepeat_narrow.txt
run "Drumtonic, FM-1: every script under AddressSanitizer, nothing out of bounds" sh tests/asan.sh tests/scripts/drumtonic_fm1_*.txt tests/scripts/midi.txt
run "Drumtonic, FM-1: the second core changes nothing in the sound (one core = two, bit for bit)" sh tests/cpu2_same.sh tests/scripts/drumtonic_fm1_fx.txt tests/scripts/drumtonic_fm1_tour.txt tests/scripts/drumtonic_fm1_motion.txt
run "Drumtonic, FM-1: N.ENV's ENV from EXP through every % to LIN, then CLAP (expect)" sh tests/run_host.sh tests/scripts/drumtonic_fm1_nenv.txt
run "Drumtonic, FM-1: PRESETS on SEQ, each part its own length, looping on its own (expect)" sh tests/run_host.sh tests/scripts/drumtonic_fm1_partlen.txt
run "Drumtonic, FM-1: HOME held shows the patterns on the white keys, on any page (expect)" sh tests/run_host.sh tests/scripts/drumtonic_fm1_homehold.txt
run "Drumtonic, SMK-37: play, edit, steps, PLAY mode pads, kit, tempo, save" env BOARD=smk37 sh tests/run_host.sh tests/scripts/drumtonic.txt
run "Drumtonic, SMK-37: live recording onto the nearest step, an external clock" env BOARD=smk37 sh tests/run_host.sh tests/scripts/drumtonic_rec.txt

$CC -DBOARD_SMK37=1 -Ifirmware/hal -o "$OUT/smk_flash_test" tests/smk_flash_test.c
run "SMK-37 flash map: settings, blobs, owner tag, slots in the store; never the head, BTIF/USRTRIM, USR, key_mac" "$OUT/smk_flash_test"
$CC -DBOARD_SMK37=1 -Wno-int-to-pointer-cast -Ifirmware/hal -o "$OUT/smk_pad_test" tests/smk_pad_test.c
run "SMK-37 pads: one hit per strike (a rebound, a noisy release), every strike of a fast roll" "$OUT/smk_pad_test"
$CC -o "$OUT/smk_wheel_test" tests/smk_wheel_test.c
run "SMK-37 pitch wheel: the bend is back to 0 at rest, however slowly the wheel was moved" "$OUT/smk_wheel_test"

$CC -o "$OUT/ota_test" tests/ota_test.c
run "M-UPGRADE entry (FM-1)" "$OUT/ota_test" build/drumtonic.fwsc
head -c 60000 build/dinghy.bin > "$OUT/old_app.bin"
python3 tools/fm1pkg_make.py "$OUT/old_app.bin" build/loader/ota.bin "$OUT/old.fwsc" >/dev/null
$CC -o "$OUT/ldr_screen_test" tests/ldr_screen_test.c
run "update loader's screen (from Skiff): UPDATE MODE, WRITING and its bar, UPDATE FAILED" "$OUT/ldr_screen_test" "$OUT"
$CC -o "$OUT/ldr_test" tests/ldr_test.c
run "update loader (FM-1): other app -> this build" "$OUT/ldr_test" "$OUT/old.fwsc" build/drumtonic.fwsc

# SMK-37: the OTA staging and the loader windows on SMK-layout packages (made here from the FM-1 binaries: only the
# layout and the protocol are under test), then the real SMK packages
python3 tools/fm1pkg_make.py --board smk37 build/dinghy.bin build/loader/ota.bin "$OUT/smk_new.fwsc" >/dev/null
python3 tools/fm1pkg_make.py --board smk37 "$OUT/old_app.bin" build/loader/ota.bin "$OUT/smk_old.fwsc" >/dev/null
run "SMK-37 package layout (fm1pkg_make --board smk37): PRCT 0x9D000, stock region table" \
    python3 tests/smk_pkg_test.py "$OUT/smk_new.fwsc" "SMK-37 Pro_900"
$CC -DBOARD_SMK37=1 -o "$OUT/ota_test_smk" tests/ota_test.c
run "M-UPGRADE entry, SMK-37 Pro (staging 0xBB000, parm ota-SMK-37 Pro_016)" "$OUT/ota_test_smk" "$OUT/smk_new.fwsc"
for m in 1 2; do
    $CC -DBOARD_SMK37=1 -DT_MODEL=$m -o "$OUT/ota_test_smk_m$m" tests/ota_test.c
    run "M-UPGRADE entry, SMK-37 model $m (1 Elite, 2 MKE-P37): identity and parm under its name" "$OUT/ota_test_smk_m$m" "$OUT/smk_new.fwsc"
done
$CC -DBOARD_SMK37=1 -o "$OUT/ldr_test_smk" tests/ldr_test.c
run "update loader, SMK-37: app area 0x4000-0x9CFFF, protected ranges untouched" "$OUT/ldr_test_smk" "$OUT/smk_old.fwsc" "$OUT/smk_new.fwsc"
found=0
for m in ":SMK-37 Pro_923" "-elite:SMK-37 Elite_923" "-mke:MKE-P37_923"; do
    for f in build/drumtonic*-smk37${m%%:*}.fwsc; do
        [ -f "$f" ] || continue
        found=1
        run "SMK-37 package $f: layout, identity ${m#*:}, the loader marker" python3 tests/smk_pkg_test.py "$f" "${m#*:}"
    done
done
[ "$found" = 1 ] || echo "(no SMK-37 packages in build/: BOARD=smk37 sh build.sh first)"

run "installer (tools/fm1_install.py): FM-1 and SMK-37, install and the update-mode resume check the model" python3 tests/install_test.py
if command -v node >/dev/null; then
    run "web installer (web/fm1ota.js) on a fake MIDIAccess: install, resume, refusals, a pulled cable, the package" node web/test_web.mjs build/drumtonic.fwsc
    run "web installer page: build/drumtonic-installer.html from build/drumtonic.fwsc" python3 tools/make_installer.py
else echo "(web installer tests skipped: no node)"; fi

[ "$fail" = 0 ] && echo "ALL TESTS PASSED" || echo "SOME TESTS FAILED"
exit "$fail"
