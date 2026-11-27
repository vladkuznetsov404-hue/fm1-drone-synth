# MASSA — specification (v0.1)

## What it is

MASSA turns the M-VAVE FM-1 into a drone instrument for slow, heavy, Sunn O)))-style walls of sound: low notes that
hang forever, thick detuned layers that beat against each other, a fuzz and a 4×12 cabinet, and a cassette loop that
records the instrument's own output and plays it back with wow, flutter, wear, half speed, reverse and a tape-stop
brake. It is a playing instrument (no step sequencer); the cassette is the only "sequencing". Kind: synthesizer.

It has two **bodies** (SELECT turns them; the settings call it MODE): the **BLACK HOLE** (HARSH: the fuzz and the
cabinet, the red palette) and the **QUASAR** (SHOEGAZE: a softer fuzz into a chorus and a reverb, a cold blue-white
palette, a spinning disc). More bodies can be added later. Each body has its own 16 sounds, its own FX page and its own
colours; the cassette, the tape echo, the scales and the settings are shared. On top of the drone: a tape echo
(ANOMALY) in which MASS bends time between the repeats, and a freeze; a cassette whose pitch is apart from its speed
and that is played with 15 pads; scales and Sunn-style chords on the keys that glide into each other; the body on HOME
that moves with the sound. The pictures share one sky: the cassette is a **slice of time** (SLICE), the tape echo an
**anomaly** (ANOMALY). No page is a list of parameters with bars: every page is a picture that moves with its values.

## Controls

The FM-1 has 27 keys, 14 buttons (FX SCL ENV LFO EDIT GLO HOME SAVE ARP SEQ PLAY REC OCT− OCT+) and 7 encoders
(SELECT, ALGORITHM, PRESETS, KNOB 1..4). OCT− + OCT+ held 5 s is reserved by the platform (recovery) and is not used.

| Control | Action | Effect |
| --- | --- | --- |
| Keys (27) | press | **Latch**: a key not sounding starts its note, which drones until the same key is pressed again (then it fades out with the release time). At most 4 notes at once; a 5th steals the oldest. |
| OCT− / OCT+ | press | Octave of the keys, −3..+1 (default −2). LEDs as the platform does. |
| OCT− + OCT+ | 5 s hold | Reserved (platform recovery). |
| KNOB 1 | turn (HOME) | **MASSA** 0..100: drive of the fuzz + sub level + engine feedback, together. |
| KNOB 2 | turn (HOME) | **BEATS** 0..100: detune spread of the layers (slow beating at low values, sour chorus at high). On the QUASAR's HOME it is **SPIN** 0..100 (see Sound). |
| KNOB 3 | turn (HOME) | **DARK** 0..100: low-pass cutoff (100 = darkest). |
| KNOB 4 | turn (HOME) | **BREATH** 0..100: depth (and a little rate) of the slow LFOs on cutoff, pitch and level. |
| KNOB 1..4 | turn (other pages) | The four parameters of that page (see Screens). |
| SELECT | turn | The **body**: BLACK HOLE ↔ QUASAR (toast with its name; its own sounds, colours, FX page and HOME picture; each body keeps its unsaved edits while you switch). It does not turn pages: every page has its own button. |
| ALGORITHM | turn | Engine: **SAW** (detuned saws + sub) or **FM** (feedback FM + sub). The switch crossfades, notes keep sounding. On the SLICE pages it is the **jog**: each click is a push on the time (the speed leaps ±12.5 % and settles back). |
| PRESETS | turn | Pick a sound slot 1..16 and load it (the held notes keep sounding with the new sound; unsaved edits of the sound before are dropped). |
| HOME | press | HOME page. |
| EDIT | press | EDIT page (engine). |
| FX | press | FX page (fuzz, cabinet). |
| ENV | press | ENV page (swell). |
| LFO | press | LFO page (the visualised LFO); pressed again: LFO 2 (the three breath LFOs); again: back. |
| SEQ | tap | SLICE page; on SLICE: SLICE 2, and back. |
| SEQ | hold | **Pad mode**: the keys are the cassette's 15 pads (not notes) while SEQ is down; a SEQ press in which a pad was played does not change the page. Pads: 1 BACK (reverse), 2 SLOW (half), 3 FAST (double), 4 STILL (stop), 5..7 CUT 8 / 16 / 32 (a slice of the loop repeats), 8 HOLD (freeze), 9 TEAR (chew), 10 +12, 11 −12, 12 +7, 13 LEAP (skip), 14 ORIGIN (restart), 15 RIFT (latches the echo's freeze). Pads 1..14 act while held. |
| GLO | press | GLO page (global settings: the sky of the body, MIDI channel, TRS MIDI IN, volume). |
| SCL | tap | The scale page (SCALE, ROOT, CHORD, LOW ROOT). |
| SCL | hold 0.8 s | **Silence**: release every latched note (fade out); toast SILENCE. |
| SAVE | press | Save dialog: the current sound into the slot shown (PRESETS picks it), confirm with SAVE again or OCT+, cancel with OCT−. |
| REC | press | Cassette (acts at once): on an empty tape start recording (the first pass sets the loop length); while recording close the loop and play; while the tape plays toggle **overdub**; when stopped play with overdub. |
| REC | hold 1 s | Erase the tape. |
| PLAY | press | Cassette: play / stop (while recording: close the loop and play; on an empty tape: nothing). Stop runs the **tape brake** (the loop slows down to silence over the BRAKE time); play starts it again (spins up). |
| ARP | press | The tape echo: ANOMALY, and again ANOMALY 2, and back. |

LEDs: the current page's button (ARP for the two ANOMALY pages, LFO for LFO and LFO 2, SEQ for SLICE and SLICE 2, SCL for SCL), SAVE while the dialog is open, REC while
recording or overdubbing, PLAY while playing, OCT− below octave 0, OCT+ above it, the keys of the latched notes (with a scale: the key
of each note); while SEQ is held the keys show the pads that are on.

## Screens (240 × 240, dark)

Palette `MASSA` (tools/gen_ui_palettes.py): black background, a warm grey for text and labels, red values
(250, 96, 64), a pale hot red (255, 196, 168) for the active item; the dark red is the TINT blend (tracks, wells).
The platform's contrast checks (black text on an 80 % fill of the value colour needs 4.5:1) do not allow a value
colour as dark as #8B0000. The name MASSA in the large font on the boot screen, until the first frame.

The SHOEGAZE palette (`SHOEGAZE`, violet and a cool pale pink on black) replaces it with the mode; the header shows
the mode next to the page title (HARSH / SHOE). SHOEGAZE: ground (0, 2, 10), text (206, 222, 242), values (98, 178, 255) blue,
the one active thing (255, 216, 160) a warm white-gold.

A page's foot shows the latched notes, or, while none is latched, a one-line hint for the page (ARP: NEXT PAGE, ALGO: NUDGE
THE TIME, ...) in the dim colour.

Every page is a *picture page*: a header (the page, the body's tag and the engine, `*` when edited, a message in its
place for 1.5 s), a 240 × 124 picture redrawn as it moves (~70 ms for the tape, ~100 ms for the amp), a strip of four
dials (label, a knob with its pointer, the value; the label is at most 58 px wide) and the footer. HOME and the other
pages that show the body's sound have the sound line (slot, name, `*`, the last MIDI note) under the header. The FX page
of HARSH has no dial strip (the combo's knobs are the dials). A value that does not reach the sound yet is not shown
(there is no SOON any more).

- **HOME**: header and sound line; a 240 × 104 picture of **a black hole** that moves with the sound: the
  waveform of the drone (the scope's captures, ~10 a second) bends the rim of the hole, stars are lensed round it,
  halos and a ring pulse with the level, the held notes sit under it (in SHOEGAZE the core is a glow instead of a
  shadow). In SHOEGAZE the hole is a **quasar**: a bright core, a thin accretion disc (DARK widens it, the wave ripples
  its rim) and two jets along the axis that whip with the sound (MASSA lengthens them, the level flares the core).
  The quasar **rotates with SPIN**: the disc's knots orbit and the jets precess at the rotor's rate. Then the SLICE's
  state (EMPTY, REC, PLAY, DUB, STOP) with the loop position as a bar, and the four macro dials: BLACK HOLE MASSA,
  BEATS, DARK, BREATH; QUASAR MASSA, **SPIN**, BLOOM (the reverb), DARK (SHIMMER, the chorus, is on the FX page).
- **EDIT** (engine; a picture of the engine): SAW: layers per note (1..3), sub level, sub octave (−1/−2), spread curve:
  the layers are drawn as saws drifting against each other, with the sub under them. FM: ratio, feedback, index, sub
  level: a carrier and a modulator wave, the modulator's ratio and the feedback bending the carrier.
- **FX** (HARSH): fuzz drive, fuzz tone, cabinet (off / 4×12 / dark), output level. The combo is the control surface:
  its four knobs and their values are the page's (no second strip), two valves glow and, with DRIVE, flame; the scope
  of what comes out squares as DRIVE goes up; below, four speakers whose cones move with the level; CAB changes what
  is drawn (bare cones, a grille, a denser grille). **FX** (QUASAR): the **corona**: FUZZ, CHORUS (SHIMMER), REVERB
  (BLOOM) and LEVEL are drawn as parts of the quasar itself (the disc's heat, a doubled ring, a halo, the jets' reach),
  turning with SPIN, with the four dials under it.
- **SCL**: SCALE (MANUAL, MINOR, PHRYGIAN, LOCRIAN, HARM MINOR, PENT MINOR, WHOLE), ROOT (C..B), CHORD (SINGLE, POWER,
  POWER+8, SUS4, MINOR, TRITONE, DOOM, OCTAVES), LOW ROOT (off, −1, −2 octaves). A picture: the twelve notes round a
  wheel (the scale's notes lit, the root ringed, the chord's shape as a polygon, bright when it sounds), the scale's
  note names, the chord's intervals, what is latched. **MANUAL** (the default, as at power-on): the keys are free,
  latching single notes from F3 up whatever the ROOT, CHORD SINGLE; the player picks the notes and they stack.
  **Any other scale is live**: what is set in the menu is what sounds. Changing SCALE or ROOT re-maps the keys that
  are latched (each sounding voice glides to the same key's note in the new scale, by SLIDE) and re-morphs a latched
  chord from its key; changing CHORD or LOW ROOT re-voices it as before. Turning SCALE back to MANUAL puts the keys
  back to their free notes.
- **ENV** (a picture of the envelope): swell (attack 0..10 s), release 0..20 s, **SLIDE** (0..6 s, the time a note
  takes to move to its place in the next chord, a setting kept with the others); the curve is drawn with a note's
  swell and fall, and SLIDE as a glide between two levels. The fourth dial is empty.
- **LFO** (a visualised, mappable LFO, beside BREATH): RATE (0.01..1 Hz), SHAPE (SINE, TRIANGLE, RAMP, SQUARE,
  RANDOM), TARGET (PITCH, CUTOFF, LEVEL, FUZZ, SPIN), DEPTH 0..100. The picture is the wave itself, running at the
  rate, with a dot that rides it and an arrow to the target it moves (the target's own picture: a pitch line, a
  filter slope, a level bar, a flame, a rotor). The LFO is worked out once per block like BREATH; its output goes
  on the target only, at DEPTH 100: PITCH ±30 cents, CUTOFF ±50 DARK steps, LEVEL about ±50 %, FUZZ the drive's gain
  about ±40 % (capped), SPIN ±100 steps of the SPIN knob.
  **LFO 2** (LFO pressed again): the three BREATH LFOs: BREATH (depth), TO PITCH, TO CUTOFF, TO LEVEL, as three
  waves with their slow phase relations.
- **ANOMALY** (ARP; the tape echo, two pages): TIME (60..950 ms between the heads, quadratic), REPEATS, ECHO (mix, 0 =
  off: the default) and **MASS** (0..100, default 25). **ANOMALY 2**: ORBIT (the wobble: a slow sine of 0.35 Hz sways
  the heads like an orbit, plus the wow and flutter), DULL, GLIDE (the motor's speed), TAPE DRIVE (saturation in the
  loop). **MASS is gravity**: the heads do not sit at D, 2 D, 3 D but are pulled towards the source, so the spacing
  between repeats shrinks (time bends) the higher the mass, and the repeats are read at changing speeds; with MASS
  above 0 and ECHO above 0 a few grains are derived from MASS and mixed in (a cloud around the well). The engine
  still keeps HEADS and the grain values among the settings (they are not on any page; the host tests and old
  settings records use them), and the main loop derives them from MASS. The picture: the source (the record head)
  with ripples spreading from it (faster the shorter the delay), a ray carrying the sound itself (the delay line's
  cells, on a log scale) and the echoes as numbered lenses along it that crowd towards the source as MASS goes up, a
  dimension line from the source to echo 1 that is TIME, and under them a sheet of lines that sags into a well under
  the source (the deeper the more ECHO, REPEATS and MASS) and sways with ORBIT. While TIME glides the ghosts of the
  lenses show where it is *going*, and the bottom row says GOING TO <ms> and SPINNING UP / SLOWING. The grains are
  drawn from the engine's own gap and window.
- **SLICE** (the cassette; SEQ): SPEED (**X0.10 .. X3.00**, shown as X1.00, a varispeed in both directions: the
  stored value is a percent, 10..300), PITCH (−12..+12 st), **FORGET** (the old DECAY: how much of the old tape is
  scaled away each overdub pass, 0..100) and **AGE** (the old WEAR: wow, flutter, saturation, hiss and a loss of highs
  per pass, 0..100). The picture: the loop round a ring with the sound standing on it as bars (the newest, just
  behind the head, bright), the head sweeping at the tape's speed, its state, position, rate and pitch beside it; a
  CUT pad cuts a wedge out of the ring (the slice that repeats); FORGET is drawn as five bars that decay one after
  another, AGE as a wobbling line with hiss. At speeds above ~2× some cells are not written while dubbing, and below
  ¼ speed an overdub writes nothing (the head leaves a cell too slowly). **SLICE 2**: BRAKE time (0.2..4 s), DIRECTION
  (forward / reverse), LOCK (on: SPEED changes the time only, the pitch stays), DROPOUT (0..100, how often the tape
  drops out). The picture: the 15 pads (see SEQ), lit while on.
- **GLO** (a picture of the settings: the body's sky with a corona round the volume): MIDI channel (omni / 1..16),
  TRS MIDI IN on/off, master volume, a version line (in the title). The body is no longer set here: SELECT turns it.
- **SAVE** dialog: SAVE, the target slot and the name now in it, the keys (SAVE / OCT+ yes, OCT− no, PRESETS another
  slot); knobs and other buttons do nothing while it is open.

Each page draws only what changed (the platform's lazy drawing).

## Sound

- 44.1 kHz, mono engine into a stereo output (both channels the same in v1).
- Up to **4 latched notes**. Per note:
  - **SAW** engine (milestone 2, built): 1..3 saws band-limited with polyBLEP, plus a sine sub (a parabolic sine,
    SUB LEVEL 0..100 up to ~0.64 of one saw's peak) one or two octaves down (SUB OCTAVE). BEATS sets the spread: the
    outer layers at ±40 cents × shape, where shape is BEATS / 100 at SPREAD CURVE 0 (even steps) and its cube at
    SPREAD CURVE 100 (fine steps at the low end, where the slow beating is), mixed in between; BEATS 0 is unison.
    1 layer: at the note; 2: at ±half the spread (no centre, they beat against each other); 3: at the note and ±the
    spread. A layer's level falls with their number (7000, 4400, 3400 of 32 767) so 2 and 3 layers sound about as
    loud as one. New notes start their layers a third of a cycle apart (no phasey attack). The tuning is worked out
    when a note starts or a tuning value changes, never per sample.
  - **FM** engine (milestone 5, built): per note 1..2 detuned pairs (LAYERS, capped at 2; spread like the saws) of a
    carrier at the note and a modulator at RATIO × the note (INDEX: up to 4 rad); the carrier feeds back on itself
    (the average of its last two outputs, up to ~1.75 rad: FEEDBACK + half of MASSA), so FEEDBACK alone turns the
    sine into a dirty saw. Sines: a corrected parabola (~0.1 %). Plus the sine sub. ALGORITHM crossfades SAW ↔ FM
    over ~50 ms (both engines render meanwhile), the notes keep sounding. The DX7 engine of the stock firmware is
    not used.
- A shared chain after the voices (milestone 3, built: `fx.c`, integer): DC block → **fuzz** (gain up to 48 dB:
  FUZZ DRIVE and MASSA add up to 25 dB each; an asymmetric cubic soft clip, +1.0 / −0.8) → DC block → **tone**
  (a ~1 kHz one-pole, the rest mixed back in by FUZZ TONE) → **LEVEL** → **cabinet** (off; 4×12: a 4th-order
  low-pass at 4.2 kHz and +5 dB at 110 Hz; 4×12 dark: 2.3 kHz and +6 dB at 100 Hz; three biquads) → **DARK** (two
  one-poles, 12 dB/oct, 12 kHz at 0 down to 150 Hz at 100, even steps in pitch) → the cassette. MASSA also lifts the
  sub by up to 60 %. After ~0.19 s of silent voices the chain is cleared and outputs exact zero.
- **SHOEGAZE** (`shoe.c`, after the chain above; HARSH drops it): a **chorus** (a 2048-sample line, two taps of
  ~16 and 20 ms swung at 0.55 and 0.83 Hz, depth 20 + 3·CHORUS samples, mixed up to one half) into a **reverb**
  (Freeverb-lite: four combs of 1116, 1188, 1277 and 1356 samples with a one-pole damping, two all-passes of 556 and
  441, feedback 0.75 + REVERB/546, wet up to 0.55 × REVERB/100). It comes after the fuzz, the cabinet and DARK, so the
  FX page's DRIVE and LEVEL still work as before (CAB and TONE keep the sound's values, not shown on that page). Its own quiet test clears both after 0.5 s of silence. 16 KB of the pool. The sound's values CHORUS and
  REVERB are stored with it.
- **Chords glide** (`sound.c`): with CHORD other than SINGLE, or LOW ROOT on, the key whose chord is latched ends it when
  pressed again; another key takes the sounding notes over: the lowest voice goes to the lowest new note, and so on,
  each gliding by a one-pole whose time constant is SLIDE (tenths of a second, 0 = at once; the phases carry on, so
  there is no click); a smaller chord lets its highest voices go, a bigger one swells new voices in. Turning CHORD or
  LOW ROOT while a chord sounds re-voices it the same way, and a stolen voice glides to its new note too. With SINGLE
  and LOW off (MANUAL, the default) the keys stack as single notes.
- **Breath** (milestone 4, built: `breath.c`): three slow LFOs (parabolic sines) at RATE, ×1.375 and ×0.75 (they
  line up only every ~11 cycles), on the pitch (±8 cents), the DARK cutoff (±25 steps) and the level (±1.5 dB), each
  weighed by its TO value; BREATH is the depth of all three and nudges the rate up to +20 %. Worked out once per
  32-sample block.
- **LFO** (`breath.c`, beside BREATH): one more LFO with a SHAPE (sine, triangle, ramp up, square smoothed, random
  gliding), a TARGET (pitch, cutoff, level, fuzz gain, spin) and a DEPTH. Only the target is moved. Worked out once
  per block; the fuzz's gain with it is kept ≤ 70 000 (Q8) so the shaper's input stays inside 32 bits.
- **SPIN** (`shoe.c`, the QUASAR's; after the chorus and the reverb): a rotary. SPIN 0 is off and the output is the
  plain mono duplicate. Otherwise the output is stereo: the mono signal is panned round a circle, the left and right
  gains moving in quadrature at a rate of 0.12..12 Hz (quadratic in SPIN) with **inertia** (the rate follows the knob
  by a fraction of the gap per block, so turning it spins the rotor up and down, the "вжух"), and the depth ramps in
  over the first 20 % of the knob. The gains are computed once per block (Q15) and slid per sample (Q20): no zipper,
  no divides in the sample loop. The LFO can target it.
- **Swell / release**: per-note amplitude ramps (attack 0..10 s, release 0..20 s, at least ~6 ms); no clicks on
  start, steal or stop. A stolen voice keeps its phase and level and changes pitch, then swells from there. A key or
  note-on whose note is still releasing swells that voice back up.
- The MASSA macro maps 0..100 to: fuzz drive (main), sub level (+), engine feedback / FM index (+). The exact curves
  are tuned by ear in the emulator.

### The cassette

Built (milestones 6 and 7, `tape.c`). The audio ISR owns the tape (its clock is the sample count); the main loop
posts the buttons and shows the state. The details as built:

- Cells: the chain's output × 5 / 512 (its ~12 000 peak uses ~117 of 127 steps), two output samples averaged per
  cell; writes saturate softly above 96. Playback interpolates two cells linearly. A loop is at least 0.1 s.
- Overdub writes the cell the head leaves with the average of the live samples on it (none while the speed is below
  ¼, and at above ~2× some cells are skipped); FORGET (G_DECAY) scales the old cell per pass. AGE (G_WEAR): wow ±0.61 % at ~0.5 Hz, flutter ±0.15 % at ~8 Hz, hiss up
  to ~−44 dB, and a cell-rate one-pole on every write (WEAR 100: ~1.8 kHz) so the highs go pass by pass.
- PLAY from STOP spins up in ~0.3 s; the brake slows linearly over BRAKE (0.2–4 s). STOP shows at once.

- One loop buffer in the pool, **8-bit, ~22 kHz** (every second output sample, with a simple anti-alias filter):
  about **10 s** (~220 KB). Lo-fi is intended.
- REC on an empty tape records the instrument's output (after the cabinet) until REC is pressed again: that sets the
  loop length (max ~10 s; at the end of the buffer it closes the loop by itself).
- Playback mixes the tape into the output with the live sound. **Overdub**: the live sound is added onto the tape
  while it plays; the old material is scaled by (1 − decay) each pass (Frippertronics-style).
- **Speed**: X0.10 .. X3.00 (stored as 10..300 %): varispeed both ways, the head steps up to three cells a sample;
  **reverse**; **brake**: the speed ramps to zero over the
  BRAKE time (pitch falls), then the tape stops; PLAY spins it up again.
- **Pitch apart from speed**: when PITCH (plus the pads +12 / −12 / +7, clamped ±24 st) is not what the speed implies,
  two overlapping triangle-windowed grains (1024 samples) read at the pitch's rate from the head position; with
  LOCK the pitch stays when SPEED moves. The plain read comes back after three respawns with no pitch.
- **Pads** (SEQ held): REV flips the direction, HALF / DOUBLE / STOP multiply the speed (the speed slews, so they bend),
  STUT 8 / 16 / 32 repeat the piece of the loop (1/8, 1/16, 1/32 of its length) that starts where the pad went down
  (a stutter), FREEZE holds the head, SKIP jumps an eighth of the loop ahead, RESTART goes back to the start, CHEW (held) jitters the speed and adds dropouts and a low-pass, ECHO
  latches the tape echo's freeze. The jog (ALGORITHM) pushes the speed ±12.5 %, decaying.
- **Dropouts**: with DROPOUT (and WEAR, and CHEW) the tape now and then loses level for 6..40+ blocks, as a worn
  oxide does. At low speed the highs are lost (a one-pole follows the speed).
- **Wear**: wow (~0.5 Hz) and flutter (~8 Hz) as a modulated read position, a soft tape saturation, hiss, and a
  slow loss of highs on each overdub pass.
- The tape lives in RAM only: it is lost at power-off (by design).

### The tape echo (ANOMALY)

Over the whole master (after the cassette, before the volume). A tape machine in the Space Echo manner: a record
head and playback heads at multiples of D. TIME is the spacing D and it moves like a motor: it glides at a limited
rate (~6 s across the range, with inertia), so while you turn it every repeat is read faster or slower than it was
written (head 1 up to ±16 %, head 3 three times as far): the echoes bend as on a real machine. ORBIT (G_DWOB) adds
wow (~0.5 Hz) and flutter (~7 Hz) to D and a slow 0.35 Hz sine sway. Each pass through the loop loses highs (DULL),
DC, and saturates softly; REPEATS up to 100 (feedback 1.07) lets the loop play itself, held at a peak of ~21 000 by
the saturation. The line is 16-bit cells at 11 025 Hz, 32 768 of them (64 KB in the pool, 2.97 s). An empty line costs
a scan of the block; a line quiet for its whole length is cleared, and so is one with ECHO 0 for 3 s.

- **The motor**: TIME's target is chased by a slew whose speed is GLIDE (11 steps, 0.15 s .. 6 s across the range; the
  error scales the speed, so a small change lands fast and a big one runs at the limit); a turn that outruns the
  motor is dragged along, never waiting for the knob to stop. The head step is clamped to ±3 × (a head may read
  backwards on a fast glide: the echoes throw).
- **MASS** (G_AMASS, 0..100, default 25): gravity. A stretch factor Bq (Q8, MASS × 77 / 100, smoothed) pulls the heads
  in: head n sits at `n·D + n(n−1)·(D/256)·Bq` in the unclamped form of the code, and reads at a speed that
  falls with n and Bq, so the later repeats crowd and bend: time itself is not even. The positions are clamped
  (2 130 000 000 in Q-units) so a huge TIME and MASS cannot overflow. MASS 0 is the plain Space Echo. The heads are
  chosen by the engine's HEADS setting, which the UI no longer shows (the default is all three).
- **Grains from MASS**: the engine still has the six-grain delay (SIZE 20..620 ms, DENSITY, SCATTER, GRAIN PITCH
  from a table `GR_STEP` (unison, an octave down, a fifth, an octave up, a seventh down), REVERSE, GRAIN FEED), but
  its values are no longer on pages: the main loop's `dly_mass_apply()` sets them from MASS (mix, size, density,
  scatter), and only when ECHO > 0. With MASS 0 the grains are off. The older stored values of the grains are still
  read (see What is stored) and the host tests set the engine's own values directly.
- **TAPE DRIVE**: a drive in front of the loop's soft clip (gain 1 .. 2.6, knee down from 20 000 to 6 000): the
  repeats saturate and thicken as they circle.
- **FREEZE** (the RIFT pad): the line stops recording, the heads loop what is on it (feedback by head count).

## Timing and sequencing

No step sequencer, no arpeggiator, no MIDI clock in v1. The cassette loop is free-running (its length is set by the
REC presses).

## What is stored

- **16 sounds per mode** (all engine, FX, ENV, LFO and macro values, a 12-character name, the engine choice): one
  record each (store slot 1 = BLACK HOLE, slot 2 = QUASAR, 16 × 94 bytes = 1 504 of 4 076). Each sound keeps its value
  count and room for 48 values (27 now, the new ones being SPIN, LFO SHAPE, TARGET and DEPTH, appended), so new values are appended and an older sound reads with them at their defaults.
  SAVE writes the bank of the body that is on. A fresh unit: the BLACK HOLE's slot 1 is INIT and 2..16 the factory
  sounds; the QUASAR has its own INIT and 15 factory sounds, with a SPIN set on each (a table in `data.c`). An older
  sound reads with SPIN 0 and the LFO at SINE, PITCH, DEPTH 0 (off).
- **Settings** (MIDI channel, TRS MIDI IN, volume, octave, last sound, MODE (the body), the cassette's values SPEED
  (a percent now, 10..300; an old ½..1 value reads as the same percent), PITCH, LOCK, DIRECTION, DECAY (FORGET), WEAR (AGE),
  BRAKE, DROPOUT, SLIDE, the tape echo's TIME, REPEATS, ECHO, HEADS, WOBBLE (ORBIT), DULL, GLIDE, TAPE DRIVE, the
  grains' GRAINS, SIZE, DENSITY, GRAIN PITCH, SCATTER, REVERSE, GRAIN FEED, **MASS** (appended last), and the scale's
  SCALE, ROOT, CHORD, LOW ROOT): one record (store slot 0), with its own value count (40 now); new values are only
  ever appended. A record without MASS reads it at 25.
- All are written by SAVE (confirmed in the dialog), never in the background: a save fades the audio ~100 ms.
  Every value read back is range-checked; a bad one takes its default, the rest are kept.
- Not stored: the latched notes, the cassette.

## MIDI

- In over USB and TRS (TRS MIDI IN on by default, type A): **notes only**, single notes (the scale and the chords are
  for the keys). Note-on latches like a key; note-off is ignored (latch); a second note-on of the same note releases it. Channel from GLO (omni default). TRS MIDI IN off
  drops what arrives over TRS.
- A lost stream (the input ring overflowed) only loses toggles, since note-offs do nothing: the drones go on, SCL
  held silences them.
- No CC, no clock, no MIDI out in v1.

## Open questions

- Whether 4 notes × 3 layers of the FM engine fit the audio ISR with the fuzz, cabinet and tape: measure in the
  emulator and on the unit; the fallback is fewer FM copies per note.
- The best picture for HOME (waveform vs. a slow spectrum): try both in the emulator.
- **Not measured on hardware**: the audio ISR's load with the grains (delay and cassette) and the SHOEGAZE chain
  all on (`audio_cpu_q8` on the first install), the pool (~16 KB more than before: check `budget_report.sh`),
  the drawing's frame rate (HOME and FX redraw ~100 ms canvases; FX redraws two), how the reverb sounds on the real
  speaker, and whether the grains click on a pad stutter.
- The motor's "throw" at a high GLIDE (the heads read backwards for a moment) is intended but may be too wild: tune on the unit.
- **Not measured on hardware (this round)**: the audio ISR with the rotor (a stereo loop), the LFO and the MASS
  grains; how the rotor, MASS and the grains sound; the frame rate of the animated pages (the quasar and the corona
  turn all the time, ANOMALY and LFO move without input); the cost of the pictures. SELECT no longer scrolls pages
  (every page has its button); easy to bring back as an option if the unit shows it is missed.

## Out of scope for v1

Step sequencer, arpeggiator, saving the tape, MIDI CC / clock / out, true stereo (the chorus and reverb are mono),
audio input (the FM-1 has none).

## Milestones

1. **Skeleton** (done, v0.1): every control from the table does something visible; all pages drawn; four latched
   notes of one plain saw each, swell and release, volume, from keys and MIDI; SCL silence; the cassette's transport
   without audio; the settings and sound records with defaults, SAVE / load working.
2. **SAW drone engine**: 4 latched notes, detuned layers + sub, BEATS, swell / release, steal without clicks.
3. **Fuzz + cabinet + DARK + MASSA macro**: tuned by ear in the emulator.
4. **BREATH**: the slow LFOs.
5. **FM engine** and the ALGORITHM crossfade; measure the ISR budget.
6. **Cassette I**: record, loop, play / stop, overdub with decay, erase.
7. **Cassette II**: speed ½ / varispeed, reverse, brake, wear (wow, flutter, saturation, hiss).
8. **Presets** (built): 16 slots with names, PRESETS browsing; on a fresh unit slot 1 is INIT (the values' defaults)
   and slots 2..16 are factory sounds: MONOLITH, SUNN WALL, BEATING, DARK SUB, FM RUST, BELL TOLL, FM GROWL, SOUR
   CHORUS, TIDE, GLASS, TAR PIT, SWARM, ORGAN DOOM, FIZZ, HUM (`data.c` FACTORY).
9. **HOME picture** and polish; then the first install on the FM-1 (new UC25HQ80 flash chip: the platform supports it).
10. **Big update** (built): the tape echo's motor, drive, freeze and grains; the cassette's pitch, lock, pads, jog,
    dropouts and chew; the HARSH / SHOEGAZE modes with their own banks, palette and pedalboard; the scales and chords;
    the black hole on HOME, the interactive combo, the pictures of SCL, DELAY 3 / 4, TAPE and TAPE 2. All host-tested;
    the speaker, the ISR load and the screen's speed wait for the unit.
11. **Quasar and slices** (built): SHOEGAZE's HOME is a quasar with its own four dials (MASSA, BLOOM, SHIMMER, DARK) and a
    cold palette; the cassette is the SLICE (a ring with the head sweeping it), the tape echo the ANOMALY (a source, lenses,
    a bent sheet); chords glide (SLIDE on ENV) and CHORD / LOW ROOT re-voice a sounding chord; MANUAL is the default scale;
    every page's foot carries a one-line hint.
12. **Bodies, spin and pictures** (built): SELECT turns the body (BLACK HOLE ↔ QUASAR; the old MODE in GLO is gone and
    SELECT no longer turns pages); the QUASAR gets SPIN (a rotary with inertia, stereo output) on HOME and a corona FX
    page; a visualised, mappable LFO (SHAPE, TARGET, DEPTH) beside BREATH as LFO 2; pictures for ENV, LFO, EDIT and GLO
    (no page is a list of bars); the ANOMALY redesigned around MASS (gravity: the heads crowd, time bends) and ORBIT,
    two pages, HEADS and the grains hidden and derived from MASS; SCL live (any scale but MANUAL re-maps the latched
    keys at once); the SLICE's SPEED X0.10..X3.00 with FORGET and AGE. Host-tested with sanitizers and mutation
    checks; the speaker, the ISR load and the frame rate wait for the unit.
