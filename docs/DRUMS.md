# The drum engine

`firmware/src/drum.c` (API in `drum.h`): 10 channels on the FM-1, each an oscillator plus a filtered noise. Integers only; tables in `firmware/gen/drum_tables.h` (`tools/gen_drum_tables.py`). Factory kits in
`firmware/src/kits.c`, written by `tools/kit_values.py` from physical targets (Hz, ms, semitones).

## A channel's 24 knobs (0..127)

| Page | Knob | Range |
| --- | --- | --- |
| OSC | wave | sine / triangle / saw |
| | pitch | 20 Hz .. 5.1 kHz (exponential) |
| | mod mode | decay / sine LFO / sampled noise |
| | mod amount | -48 .. +48 semitones (64 = none) |
| | mod rate | decay: 1 ms .. 5 s; LFO and noise: 0.1 Hz .. 2 kHz |
| | attack | 0 .. 1.25 s |
| | decay | 1 ms .. 5 s (to -60 dB) |
| | velocity | how much the velocity changes the oscillator |
| NOISE | filter | low / band / high pass (a state-variable filter, run twice a frame) |
| | cutoff | 20 Hz .. 20 kHz (kept stable: at low Q the top is about 8 kHz) |
| | Q | 0.7 .. 22 (the band pass gains sqrt(Q) at its centre) |
| | envelope | exponential / linear / clap (three quick hits, then the tail) |
| | attack | 0 .. 1.25 s |
| | decay | 1 ms .. 5 s |
| | stereo | below 64 mono, from 64 two independent noises |
| | velocity | how much the velocity changes the noise |
| MIX | mix | 0 oscillator only .. 127 noise only |
| | distortion | off, then a drive of x1 .. x16 into a soft clipper; its makeup is taken off the level, so a sound is about as loud with it as without |
| | EQ freq | 50 Hz .. 9.5 kHz (a peak, Q 1.5) |
| | EQ gain | below 64 cut, 64 flat, above 64 boost up to +12 dB |
| | level | after the distortion, square law |
| | pan | constant power |
| | choke | 0 none, 1..3: a hit silences the others of its group in ~10 ms |
| | phase | the oscillator's start: 0 .. 90 degrees (more click) |

The velocity follows a square law, 1 - VEL + VEL * (velocity / 127)^2 (VEL 127: velocity 45 plays 18 dB under 127,
VEL 100: 10 dB). It sets the loudness after the distortion, weighed between the two VEL knobs by the mix, and the
osc/noise balance before it, so a distorted sound still plays softer when hit softer. The factory kits have VEL 100.
The sum goes through the effects and the master: [DRUMTONIC.md](DRUMTONIC.md#fx-reverb-delay-delay-2-master-comp).

## On a computer

`tests/run_drums.sh` renders every kit channel (a hard hit, then a soft one) and a 4-bar groove per kit to
`build/drums/`, checks that each channel sounds, stays under full scale, respects the velocity and that the closed
hat chokes the open one, and prints the render's cost on the computer.

## The cost on the chip

The audio interrupt has 5.8 ms for a block of 256 frames, and Drumtonic shares the channels between the chip's two
cores (320 MHz each). SETTINGS > CPU shows the load while it plays.
