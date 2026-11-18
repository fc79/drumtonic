# Drumtonic: the manual

Drumtonic turns the M-Vave FM-1 into a drum machine with 10 synthesized channels (BD SD CP CH OH LT MT HT RS CY),
32 patterns of up to 64 steps, 16 kits, a reverb with five types, a delay and a master. The sound engine is described in
[DRUMS.md](DRUMS.md).

## The screen

<p align="center">
<img src="screens/steps.png" width="240" alt="STEPS: the knobs on top, the 10 channels by 16 steps, the state band">
<img src="screens/sound.png" width="240" alt="SOUND: the knobs on top, the channel and its banks, the state band">
</p>

The screen has three bands:

- **Top**: the 4 knobs, level with the real ones. The dots under them show the banks of the page.
- **Middle**: the page.
- **Bottom**: the state. It shows the channel, the page, the part's length and the pattern. It takes the channel's
  colour, turns red while recording and white for a message.

Turning a knob shows its name and value large over the page for a second. A gesture such as a paste or a save
ends in a large two-word message (`PASTE` / `P5`, `SAVED` / `KIT 3`). When something cannot be done, its first
word is grey (`NOTHING` / `TO PASTE`).

Before something is lost, a question fills the page (`CLEAR THE PATTERN?`, `ERASE KIT 5?`, `REPLACE KIT?`): HOME
answers NO and SEL answers YES, shown in large letters in the order the two buttons sit on the panel.

## How it works

1. **A button, a page.** A button opens its page; pressed again, it opens the page's next bank. The open page's
   button is lit, and the others that open something glow.
2. **The knobs accelerate.** Turned slowly they move one step a click. Turned faster they move 2, 4 and up to 6
   steps a click, so a quick turn crosses 0-127 in about a turn and a half. TEMPO, SWING, RATE, MICRO, switches and short
   ranges always move one step.
3. **The black keys 1-10 are the channels.** On LIVE they mute; on every other page they pick the channel. They
   light the same way everywhere: bright when the channel plays, dim when silent, dark when muted. The chosen
   channel blinks while stopped. While playing, it stays lit and dips on each hit; the white keys show the playhead
   the same way. Black key 11 held with white keys sets the channel's accents.
4. **The three encoders on the left** work on every page. SELECT picks the channel (1-10, then ALL, and round again to 1). On SEQ,
   PRESETS sets the part's length and ALGORITHM shifts its steps. In a list, SELECT picks the row and ALGORITHM sets
   the value.
5. **Hold a step and turn.** On SOUND, a knob locks that step's value (a p-lock). ALGORITHM sets its condition.
6. **Hold a button to see what it does.** Any button held alone for a quarter of a second (except SEL, PLAY and REC)
   shows a guide over the page, one gesture per row; letting it go then does nothing else.

<p align="center"><img src="screens/guide.png" width="240" alt="HOME held: its guide"></p>

## The pages

### HOME: LIVE, SONG and SETTINGS

<p align="center">
<img src="screens/live.png" width="240" alt="LIVE: the pattern, its kit, the meters">
<img src="screens/live-next.png" width="240" alt="LIVE: the next pattern waiting">
</p>

**LIVE** is the page to play from.

- **Patterns.** The white keys pick the pattern (OCT- and OCT+ switch between 1-16 and 17-32). While playing,
  the next pattern starts at the end of the pattern: `>P2` waits at the top, grey.
- **Where the pattern is.** While it plays, a bar across the screen fills up with the pattern, cut every 4 steps
  (a wider cut every 16). It is grey, and white while a next pattern waits: it changes when the bar is full.
- **Chains.** Hold a pattern key and tap others: a chain, played in that order.
- **Mute.** The black keys mute the channels (LIVE is the only page where they do).
- **Knobs**: TEMPO, SWING, and the master filter's RESO and FILTER.
- **The kit.** The pattern's kit is shown at the top right, large enough to read standing up. `KIT 3*` means it
  has edits not yet saved, and `>KIT 1` shows the next pattern's kit.
- **Beat repeat.** A tap on ARP turns it on (ARP lit, `REPEAT` in the bottom band) and another turns it off. While
  it is on, holding white key 1-4 repeats the last 1/4, 1/8, 1/16 or 1/32: the slice plays once as it is, then again
  and again while the key is held. Another of those keys while it loops changes the length, taking a piece of the
  loop you hear. If the slice had no hit yet, it repeats the slice of the last hit, so it never repeats silence.
  Let go, and the pattern is where it would have been. Beat repeat changes nothing in the pattern, and leaving
  HOME or stopping ends it. HOME + a white key still picks a pattern.

HOME again opens **SONG** (below), and once more **SETTINGS**: CLOCK, REC, CLEAR PATTERN, FACTORY KITS, CPU (the
load), VERSION, POWER. SELECT picks a row, SEL runs it. REC is GRID or FREE: how hits are recorded (see Playing by
hand). From any other page, HOME opens LIVE.

### SONG: songs

<p align="center">
<img src="screens/song.png" width="240" alt="SONG: a song's rows, row 1 playing, row 2 chosen">
<img src="screens/live-song.png" width="240" alt="LIVE while a song plays: its row and count">
</p>

A **song** is a list of rows played one after the other. A row is a pattern or a chain (up to 16 patterns), played
1 to 64 times, with its own mutes. A song holds up to 64 patterns in all its rows, ends by looping (LOOP) or stopping
(STOP), and has its own tempo. There are 8 songs: PRESETS picks one.

- **The list.** At the top the song, its tempo and the row chosen (`2 / 4`), with a strip of the rows, each as long
  as it plays. Each row shows its patterns as small boxes (a chain longer than six goes on a second line; a pattern
  with no steps is an empty box), its times (`x4`), and ten marks for its mutes in the black keys' order (a full
  mark plays, a red line is muted). The last line is END, with LOOP or STOP.
- **Writing a row.** SELECT picks a row. A white key makes it that pattern; hold a white key and tap others to make
  it that chain, written when you let go of the first. Nothing starts playing. On END a white key adds a new row
  (x1), and END stays chosen so you can add the next one straight away. OCT- / OCT+: patterns 1-16 / 17-32.
- **Times and end.** ALGORITHM sets the row's times (x1-x64); on END it sets LOOP or STOP.
- **Mutes.** A black key mutes or opens that channel in the row chosen (the bottom band says `OH MUTED` / `OH ON`,
  and the black keys' lights show the row's mutes). When a row starts, its mutes become the ones you hear; you can
  still change them by hand until the next row. A song where no row has mutes leaves your mutes as they are.
- **Tempo.** Knob 1 sets the song's tempo. A song takes the tempo playing when it gets its first row.
- **Copy, insert, delete.** GLO held copies the row chosen, GLO + SEL inserts the copy there, GLO + REC deletes the
  row. On END, GLO + REC clears the whole song after **CLEAR SONG 2?** (SEL yes, HOME no).
- **From a chain.** An empty song with a chain just made on LIVE says `SEL: FROM THE CHAIN`: SEL on END makes that
  chain its first row.
- `SONG FULL` says a song has its 64 patterns (a chain too long for what is left is cut there).

**Playing a song.** On SONG, choose a row and press SEL: stopped, the song is ready from that row (its tempo and its
first pattern's kit at once) and PLAY starts it; playing, the song goes there when the pattern playing ends. Each
row's pattern brings its kit, as in a chain. LOOP comes round to row 1; STOP stops after the last row (STOP is also
sent to MIDI). On LIVE, `SONG 1` and `ROW 2/4` are where the kit was, the row's count is beside the pattern
(`2/4`), and on its last pattern the next row's pattern shows instead (`>P2`) with the bar white. While a song is on,
TEMPO on LIVE is the song's tempo.

**Leaving a song.** Pick a pattern by hand: a white key on LIVE (or a chain), or HOME + a white key from any page.
It starts when the pattern playing ends, as usual, and the song stays as it is: SEL on a row plays it again. On the
SONG page the white keys write, so leave from LIVE or with HOME.

**Recording a song by playing it.** On SONG press REC (a song with rows asks **REPLACE SONG 2?** first). The bottom
band turns red, `REC SONG 2`. Now the white keys launch patterns and chains and the black keys mute, as on LIVE, and
the song writes itself as you play: each time a pattern or a chain goes round whole, its row's times go up; leave a
chain halfway and the patterns played make a row of their own. Mutes are taken a pattern at a time: a mute changed in
a pattern's last quarter counts from the next pattern (pressed just before the bar, it lands on the bar). You can go
to LIVE and back while recording. REC or STOP ends it (so does opening another page, or a full song: the music goes
on), and the song keeps the tempo playing.

### SEQ: STEPS

<p align="center">
<img src="screens/steps.png" width="240" alt="STEPS">
<img src="screens/step-held.png" width="240" alt="A step held: condition and micro timing">
</p>

The 10 channels by 16 steps; OCT- and OCT+ show steps 17-64. With a pattern longer than 16 steps, a row of
segments over the grid shows its parts of 16: the one on screen white, the one playing grey. OCT+ is lit while there
are steps after the ones shown, OCT- while there are steps before. The white keys are the chosen channel's steps.
A tap turns a step on or off. An empty step turns on as you press it, so you can press, hold and turn in one go.

- **Knobs**: RATE (1/16, 1/16T, 1/32, 1/8T), MICRO, REPEAT (note repeat's rate) and QUANT.
- **QUANT** pulls the chosen channel's micro timing toward the grid as it plays (with ALL selected, every
  channel's): 0 % plays it as recorded, 100 % on the grid. The steps keep their timing, so turning back gives it
  back; the cells show what you hear, and MICRO with a step held shows the timing as recorded. GLO + QUANT puts it
  back to 0 %. SAVE writes it into the steps and puts QUANT back to 0 %.
- **Part length.** PRESETS sets the chosen part's length: 1, 2, 3, 4, 5, 6, 8, 12, 16, 24, 32 or 64 steps (with
  ALL selected, every part's). A longer length copies the steps, and going back gives the original. Parts of
  different lengths loop on their own, and the next pattern starts at the end of the longest part.
- **Shift.** ALGORITHM moves the channel's steps one step a click (with ALL selected, every channel's).
- **Condition.** Hold a step and turn ALGORITHM: clockwise 95 % to 5 %, anticlockwise FILL, NOT FILL, then the
  cycles 1:2 to 8:8; 100 % in the middle.
- **Micro timing.** Hold a step and turn MICRO (or press OCT+ / OCT-): 1/24 of a step later or earlier per click,
  up to 23/24 either way. The knob pauses at 0 before crossing to the other side. GLO + MICRO puts the step back
  on the grid. The step's cell moves that way on the screen.
- **Ratchet.** Hold a step and press ARP: 1, 2, 3, 4 hits in the step, then 1 again. The cell shows a dot. A
  ratchet always plays to its last hit.
- **Note repeat.** A tap on ARP turns it on (`RPT 1/4` in the bottom band) and another turns it off; leaving SEQ
  turns it off too. While it is on, a black key repeats its channel while held: a hit at once, then evenly at the
  rate. The rates are 1/4, 1/4T, 1/8, 1/8T, 1/16, 1/16T, 1/32 and 1/32T, set with REPEAT or ARP held + white keys
  1-8. With REC on, each hit is recorded on the nearest step, one hit a step (with REC on FREE, with its micro
  timing).

A lock, a condition, micro timing and a ratchet are all copied, pasted, cleared, undone and saved with the step.

### EDIT: SOUND and LIBRARY

<p align="center">
<img src="screens/sound.png" width="240" alt="SOUND: OSC">
<img src="screens/library.png" width="240" alt="LIBRARY">
</p>

**SOUND** is the chosen channel's sound in eight banks: OSC, MOD, AMP, NOISE, N.ENV, OUT, CHANNEL (level, pan,
the reverb and delay sends) and LIBRARY. EDIT again or OCT+ opens the next bank, and OCT- the one before. The white
keys are the steps here too, for p-locks: hold a step and turn a knob.

**LIBRARY** lists 59 factory sounds (the kits' and 33 more: kicks, snares, hats, toms, congas, cowbell, clave,
shaker, cymbals, effects) and 64 places for yours. SELECT moves through the rows (ALGORITHM 8 at a time), and the
channel plays each one at once, in the running pattern. SEL loads it with its name. GLO + SEL saves the channel's
sound (on a factory row, into the first free place). GLO + REC deletes one of your sounds.

On SOUND, GLO held copies the channel's sound and GLO + a black key pastes it onto another channel, with its
name and sends.

### ENV: MIX and the kits

<p align="center"><img src="screens/mix.png" width="240" alt="MIX"></p>

Every channel's level and meter. The black keys pick the channel, and the knobs are its LEVEL, REV and DLY (the
sends) and PAN.

The 16 white keys are the **kits** 1-16, and the row of 16 squares under the meters shows them in the same order:
white the kit playing, light grey a saved kit, dark an empty place. A tap loads a kit, and a tap on the kit already
playing takes it back to its saved version; an empty one says `EMPTY KIT`. GLO + a white key saves the kit there
(over another saved kit it asks first). GLO + REC asks `ERASE KIT 5?` and empties the place of the kit playing: the
sound goes on until another kit is loaded, and a pattern that had that kit keeps its sound.

**Each pattern has its kit.** A pattern takes the kit playing when it gets its first step, a kit loaded here
becomes the playing pattern's kit, and a pasted pattern brings its own. A pattern that starts with another kit
loads it, effects included, just before its first hit (in a chain too). The edits of a kit are kept while another
plays; SAVE stores every kit, and its LED blinks while any kit has edits not saved.

### LFO

<p align="center"><img src="screens/lfo.png" width="240" alt="LFO"></p>

The pattern's 8 LFOs. To put one on a knob, hold LFO and turn that knob. SELECT picks an LFO; the knobs are RATE
(1/32 to 16 bars when synced, 0.05 to 20 Hz when free), SHAPE (triangle, saw, square, sample & hold), DEPTH
(-100 to +100 %) and SYNC.

### FX: REVERB, REVERB 2, DELAY, DELAY 2, MASTER, COMP

<p align="center">
<img src="screens/reverb.png" width="240" alt="REVERB: CLASSIC on ALGORITHM">
<img src="screens/delay.png" width="240" alt="DELAY">
<img src="screens/master.png" width="240" alt="MASTER">
<img src="screens/comp.png" width="240" alt="COMP">
</p>

FX again or OCT+ opens the next bank, OCT- the one before. The sends are on MIX and on SOUND's CHANNEL bank.

- **REVERB**: DECAY, PRE DLY (0-100 ms before the reverb starts), SIZE (10-100 %, the space glides as you turn it)
  and TONE. ALGORITHM picks the type, shown large in the middle of REVERB and REVERB 2:
  - **CLASSIC**: the reverb of the earlier versions.
  - **PLATE**: bright and dense.
  - **HALL**: a large space with a long tail.
  - **ROOM**: a small, close space.
  - **GATED**: a dense burst cut short; DECAY sets how long it lasts.

  A new type fades the old tail out and starts afresh. The reverb returns at unity gain; each channel's REV send sets
  how much it gets. A kit saved with an earlier version plays CLASSIC, as it did.
- **REVERB 2**: HP IN, MOD and WIDTH. HP IN is a high-pass on what goes into the reverb, shown as its cutoff (20 Hz
  to 1 kHz), so the low end stays dry. MOD moves the tail slowly, as a real space does; WIDTH goes from mono (0 %)
  to full stereo (100 %).
- **DELAY**: time as a note value, feedback, tone and TAPE (0 is a clean digital delay; turned up, it adds wow,
  flutter and saturation).
- **DELAY 2**: HP IN, a high-pass on what goes into the delay shown as its cutoff in Hz, ping-pong, and TO REV:
  how much of the delay's echo goes on into the reverb (0 % none, 100 % all of it), so the echoes fade into the
  space. It is saved with the kit; a kit saved with an earlier version starts at 0 %. Like the
  reverb, the delay returns at unity gain.
- **MASTER**: DRIVE; FILTER, a 24 dB/octave low-pass below the middle and high-pass above it (shown L1-L64,
  OFF, H1-H63); RESO, from none to a peak of +17 dB.
- **COMP**: THRESH, RATIO (1:1 is off), ATTACK and RELEASE in ms, with a soft knee and automatic make-up. PRESETS
  is the sidechain high-pass (OFF, 60, 90, 150 Hz), so the kick drives it less. ALGORITHM is dry/wet, for parallel
  compression. The page shows the gain reduction.

The master's chain, in order: the channels and the two returns, DRIVE, COMP, FILTER, then a look-ahead limiter
that is always on. It does nothing under -0.2 dBFS and stops every peak at the ceiling.

## Buttons and gestures

| | |
| --- | --- |
| PLAY | play and stop |
| REC | record: hits on white keys 1-10 (except on LIVE and SONG) and knob moves; on SONG, record a song |
| SAVE | saves everything: the sound, the 16 kits, the 32 patterns (with QUANT written into the steps) and the 8 songs. It works while playing too; the sound goes on |
| SEL | held: FILL. In a question: yes (HOME: no). In a list: run the row (SONG: play from it) |
| ARP | HOME: beat repeat. SEQ: note repeat. A step held: its ratchet |
| MASTER | the volume |
| GLO held | copy what the page shows: the step held, the channel's part on STEPS, its sound on SOUND, the LFO on LFO, the row on SONG, otherwise the pattern |
| GLO + a key | paste there |
| GLO + REC | clear; on MIX, erase the kit playing; on SONG, delete the row (on END, the song) |
| GLO + a knob | remove that knob's motion and LFO |
| HOME + a white key | pick a pattern from any page |
| HOME + a black key | solo the channel |
| HOME + REC | the pattern as last saved |
| HOME + OCT- / OCT+ | undo / redo |

**Knob motion.** With REC on and the pattern playing, turn a sound knob: the movement is recorded on the steps it
passes and plays back gliding from step to step. Turned without REC, the knob takes over for one pass.

**Playing by hand.** On SEQ and EDIT a black key plays its channel while stopped; while playing, only with REC
on, and then the hit is recorded. With REC on, the white keys 1-10 are the channels (except on LIVE and SONG). The keys
have no velocity: a hit by hand plays at 100, and accents come from the steps. A hit goes on the nearest step;
with SETTINGS > REC on FREE it also keeps how early or late it came, up to half a step, as that step's micro timing
(over what the step had). The same goes for notes over MIDI and for note repeat.

**Undo.** HOME + OCT- goes back to before the last change, and HOME + OCT+ goes forward again. It covers the
patterns, motion, p-locks, conditions, micro timing, QUANT, LFOs, the sounds, the kit's sends and the effects. It does
not cover the tempo, mute, solo, SETTINGS, the songs, or what is already saved. A change is one button press, a knob or an
encoder until 1 s without a click, the keys until 2 s without a tap, or one REC pass. Undo is kept in memory
until power-off.

**At power-on** the first pattern plays under a playhead, then the screen shows the name, the version and the
identity until a key or a button is pressed (that press only closes it).

## MIDI and USB

- **In**, over USB and the TRS jack: notes 36-45 on any channel play channels 1-10, and record with REC on.
- **Out**, over USB: every hit the sequencer plays and every channel hit by hand, as notes 36-45 on channel 10,
  plus the clock (24 a quarter), START and STOP.
- **USB audio**: the computer sees the FM-1 as a stereo audio input and records exactly what it plays.

## The processor

Drumtonic runs both of the chip's cores at 320 MHz, always. In each block of 16 frames, half of the sounding
channels and the reverb are rendered on the second core. The result is the same, bit for bit, as on one core. If
the second core ever fails to answer, the screen says why and the FM-1 restarts. SETTINGS > CPU shows the load and
what runs.

## What is kept

SAVE writes the sound, its sends and effects, the 16 kits and the 32 patterns with their lanes and LFOs. The
settings (kit, tempo, pattern, channel, clock) and the 8 songs are saved with SAVE too, and nothing else is written by
itself. SAVE is lit while a song has changes. A
kit is written as soon as you save it on MIX or erase it, and so is a library sound when you save or delete one;
the sound goes on while they are written.

These are the places the FM-1 firmwares share: the settings and the songs at 0xFC000, kits 1-8 and the patterns at
0xF4000-0xF9FFF, the library at 0x97000 and 0x9B000, kits 9-16 at 0x9F000. An earlier Drumtonic still finds
kits 1-8. **A save replaces what another FM-1 firmware kept there**, so back up first:
[BACKUP.md](BACKUP.md).

Drumtonic reports itself as `FM-1_987`.

## Backup and restore

A backup keeps everything SAVE writes in one small file (`.dtbak`, about 19 KB): the settings and the songs, the 16
kits, the 32 patterns with their lanes and LFOs, and the sound library. It holds what was saved, not edits made since the last
SAVE. You can keep it on the computer, put it back later, or send it to someone else with an FM-1.

**From the browser.** Open the installer page in Chrome or Edge, go to the **Backup** tab and connect the FM-1 over
USB.

- **Back up the FM-1**: press **Back up**. The saves are read in under a second, nothing on the FM-1 changes, and
  the browser asks where to save the file. The FM-1 shows BACKUP / SENT.
- **Restore a backup**: press **Choose backup file**, or drop the file on the page. The page shows when the backup
  was made, with which Drumtonic, and what it holds. Press **Restore**: the FM-1 asks **REPLACE ALL SAVES?**.
  **SEL** says yes, **HOME** says no, and after a minute without an answer it is a no (RESTORE / CANCELLED, nothing
  changed). After yes the FM-1 shows RESTORE / WRITING, writes the saves and restarts with them. Do not unplug it
  until it has restarted.

**A restore replaces every save on the FM-1**, and what the backup has empty is empty afterwards. To keep what is
there now, back it up first. This holds for a backup someone sends you too: it brings their kits, patterns, library
and settings, all together, never one kit alone.

**From the command line**, with the same files:

```sh
python3 tools/fm1_install.py --backup                    # drumtonic-backup-DATE-TIME.dtbak, here
python3 tools/fm1_install.py --check-backup FILE.dtbak   # what a backup holds
python3 tools/fm1_install.py --restore FILE.dtbak        # the FM-1 asks first, on its screen
```

A backup of your saves is not a copy of the whole memory. That one keeps everything, the firmware and the unit's
own key too, and it is for repairs: [BACKUP.md](BACKUP.md).

## If it goes wrong

If an install is cut off, power-cycle the FM-1 and run the install again. If Drumtonic fails to start twice, the
FM-1 opens a USB rescue by itself. You can get there yourself by holding **OCT-** while switching on, and
install a working package from it.

## On a computer

The installer page's **Try** tab plays Drumtonic in the browser, without an FM-1: the same firmware, on a drawn
panel. Play it with the mouse (Shift and click holds a button down), the computer's keyboard (the keys are written
under each control) or a MIDI controller. Point at a control to read what it does. What you save is kept until the
page closes.

`tests/run_host.sh tests/scripts/drumtonic_fm1_tour.txt` runs the firmware on the computer and writes the screens
and the sound to `build/host/`. `tools/docs_screens.sh` makes the screenshots of these pages, and
`tests/run_tests.sh` runs the whole test suite.
