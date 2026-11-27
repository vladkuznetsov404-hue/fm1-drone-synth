# MASSA — specification (v0.1)

## What it is

MASSA turns the M-VAVE FM-1 into a drone instrument for slow, heavy, Sunn O)))-style walls of sound: low notes that
hang forever, thick detuned layers that beat against each other, a fuzz and a 4×12 cabinet, and a cassette loop that
records the instrument's own output and plays it back with wow, flutter, wear, half speed, reverse and a tape-stop
brake. It is a playing instrument (no step sequencer); the cassette is the only "sequencing". Kind: synthesizer.

## Controls

The FM-1 has 27 keys, 14 buttons (FX SCL ENV LFO EDIT GLO HOME SAVE ARP SEQ PLAY REC OCT− OCT+) and 7 encoders
(SELECT, ALGORITHM, PRESETS, KNOB 1..4). OCT− + OCT+ held 5 s is reserved by the platform (recovery) and is not used.

| Control | Action | Effect |
| --- | --- | --- |
| Keys (27) | press | **Latch**: a key not sounding starts its note, which drones until the same key is pressed again (then it fades out with the release time). At most 4 notes at once; a 5th steals the oldest. |
| OCT− / OCT+ | press | Octave of the keys, −3..+1 (default −2). LEDs as the platform does. |
| OCT− + OCT+ | 5 s hold | Reserved (platform recovery). |
| KNOB 1 | turn (HOME) | **MASSA** 0..100: drive of the fuzz + sub level + engine feedback, together. |
| KNOB 2 | turn (HOME) | **BEATS** 0..100: detune spread of the layers (slow beating at low values, sour chorus at high). |
| KNOB 3 | turn (HOME) | **DARK** 0..100: low-pass cutoff (100 = darkest). |
| KNOB 4 | turn (HOME) | **BREATH** 0..100: depth (and a little rate) of the slow LFOs on cutoff, pitch and level. |
| KNOB 1..4 | turn (other pages) | The four parameters of that page (see Screens). |
| SELECT | turn | Next / previous page (HOME, EDIT, FX, ENV, LFO, TAPE, GLO). |
| ALGORITHM | turn | Engine: **SAW** (detuned saws + sub) or **FM** (feedback FM + sub). The switch crossfades, notes keep sounding. |
| PRESETS | turn | Pick a sound slot 1..16 and load it (the held notes keep sounding with the new sound). |
| HOME | press | HOME page. |
| EDIT | press | EDIT page (engine). |
| FX | press | FX page (fuzz, cabinet). |
| ENV | press | ENV page (swell). |
| LFO | press | LFO page (breath). |
| SEQ | press | TAPE page. |
| GLO | press | GLO page (global settings). |
| SCL | press | **Silence**: release every latched note (fade out). |
| SAVE | press | Save dialog: the current sound into the slot shown (PRESETS picks it), confirm with SAVE again or OCT+, cancel with OCT−. |
| REC | press | Cassette: start recording (the first pass sets the loop length); while the tape plays, toggles **overdub**. |
| REC | hold 1 s | Erase the tape. |
| PLAY | press | Cassette: play / stop. Stop runs the **tape brake** (the loop slows down to silence over the BRAKE time); play starts it again (spins up). |
| ARP | press | Not used in v1 (screen says "SOON"). |

## Screens (240 × 240, dark)

Palette: black background, dark red (#8B0000-ish) for drawing, a dim grey for labels, a brighter red for the
selected / active item. The name MASSA in the large font on the boot screen.

- **HOME**: centre: a live picture of the drone (the output waveform or a slow spectrum, dark red); top: engine
  (SAW / FM), sound slot and name, octave; a row of up to 4 held note names; a small cassette state (EMPTY, REC, PLAY,
  DUB, STOP, with the loop position as a bar); bottom: the four macro knobs MASSA, BEATS, DARK, BREATH as bars with
  values.
- **EDIT** (engine): SAW: layers per note (1..3), sub level, sub octave (−1/−2), spread curve. FM: ratio, feedback,
  index, sub level.
- **FX**: fuzz drive, fuzz tone, cabinet (off / 4×12 / 4×12 dark), output level.
- **ENV**: swell (attack 0..10 s), release 0..20 s, plus two free slots shown as "—".
- **LFO**: breath rate (0.01..1 Hz), to pitch, to cutoff, to level.
- **TAPE**: speed (½, 1, varispeed ½..1), direction (forward / reverse), decay per pass in overdub (0..100), wear
  (wow, flutter, saturation and hiss together, 0..100); a fifth value BRAKE time (0.2..4 s) on SELECT-press or a
  second TAPE page.
- **GLO**: MIDI channel (omni / 1..16), TRS MIDI IN on/off, master volume, a version line.
- **SAVE** dialog: slot number and name, "SAVE?" with the confirm / cancel keys.

Each page draws only what changed (the platform's lazy drawing).

## Sound

- 44.1 kHz, mono engine into a stereo output (both channels the same in v1).
- Up to **4 latched notes**. Per note:
  - **SAW** engine: 1..3 band-limited saws detuned by BEATS (cents scale from ~1 c to ~40 c), plus a sine sub one or
    two octaves down.
  - **FM** engine: 2-operator voices with operator feedback (feedback = a dirty saw-like tone), 1..2 detuned copies,
    plus the sine sub. The DX7 engine of the stock firmware is not used.
- A shared chain after the voices: one-pole DC block → **fuzz** (asymmetric soft clip with a drive of up to ~40 dB,
  tone tilt) → **4×12 cabinet** (a fixed low-pass ~4.5 kHz + a resonant hump ~100–150 Hz + a dip ~500 Hz, as IIR
  biquads) → low-pass (DARK) → level → the cassette.
- **Breath**: three slow LFOs at unrelated rates (cutoff, pitch ±few cents, level ±1.5 dB), depth from BREATH.
- **Swell / release**: per-note amplitude ramps (attack 0..10 s, release 0..20 s); no clicks on start, steal or stop.
- The MASSA macro maps 0..100 to: fuzz drive (main), sub level (+), engine feedback / FM index (+). The exact curves
  are tuned by ear in the emulator.

### The cassette

- One loop buffer in the pool, **8-bit, ~22 kHz** (every second output sample, with a simple anti-alias filter):
  about **10 s** (~220 KB). Lo-fi is intended.
- REC on an empty tape records the instrument's output (after the cabinet) until REC is pressed again: that sets the
  loop length (max ~10 s; at the end of the buffer it closes the loop by itself).
- Playback mixes the tape into the output with the live sound. **Overdub**: the live sound is added onto the tape
  while it plays; the old material is scaled by (1 − decay) each pass (Frippertronics-style).
- **Speed**: ½ (an octave down) or 1, or varispeed between; **reverse**; **brake**: the speed ramps to zero over the
  BRAKE time (pitch falls), then the tape stops; PLAY spins it up again.
- **Wear**: wow (~0.5 Hz) and flutter (~6–10 Hz) as a modulated read position, a soft tape saturation, hiss, and a
  slow loss of highs on each overdub pass.
- The tape lives in RAM only: it is lost at power-off (by design).

## Timing and sequencing

No step sequencer, no arpeggiator, no MIDI clock in v1. The cassette loop is free-running (its length is set by the
REC presses).

## What is stored

- **16 sounds** (all engine, FX, ENV, LFO and macro values, a 12-character name, the engine choice): two records.
- **Settings** (MIDI channel, TRS MIDI IN, volume, octave, last sound): one record.
- Not stored: the latched notes, the cassette.

## MIDI

- In over USB and TRS (TRS MIDI IN on by default, type A): **notes only**. Note-on latches like a key; note-off is
  ignored (latch); a second note-on of the same note releases it. Channel from GLO (omni default).
- No CC, no clock, no MIDI out in v1.

## Open questions

- Whether 4 notes × 3 layers of the FM engine fit the audio ISR with the fuzz, cabinet and tape: measure in the
  emulator and on the unit; the fallback is fewer FM copies per note.
- The best picture for HOME (waveform vs. a slow spectrum): try both in the emulator.
- The second TAPE value page vs. putting BRAKE on GLO.

## Out of scope for v1

Step sequencer, arpeggiator, saving the tape, MIDI CC / clock / out, stereo effects (reverb, delay), audio input
(the FM-1 has none).

## Milestones

1. **Skeleton**: every control from the table does something visible; all pages drawn; one plain saw voice with
   latch from keys and MIDI; SCL silence; the settings and sound records with defaults, SAVE / load working.
2. **SAW drone engine**: 4 latched notes, detuned layers + sub, BEATS, swell / release, steal without clicks.
3. **Fuzz + cabinet + DARK + MASSA macro**: tuned by ear in the emulator.
4. **BREATH**: the slow LFOs.
5. **FM engine** and the ALGORITHM crossfade; measure the ISR budget.
6. **Cassette I**: record, loop, play / stop, overdub with decay, erase.
7. **Cassette II**: speed ½ / varispeed, reverse, brake, wear (wow, flutter, saturation, hiss).
8. **Presets**: 16 slots with names, PRESETS browsing, factory sounds.
9. **HOME picture** and polish; then the first install on the FM-1 (new UC25HQ80 flash chip: the platform supports it).
