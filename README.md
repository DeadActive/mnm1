# MNM-1

[![License: GPL-3.0-only](https://img.shields.io/badge/license-GPL--3.0--only-blue.svg)](LICENSE)

**A six-track synth firmware for the M-VAVE FM-1.** Six monophonic tracks of synth and FX machines,
pages of eight knobs, a resonant HP/LP filter and a tape delay on every track, three LFOs a track,
parameter locks and parameter slide, all on the FM-1's own keys, knobs and screen.

MNM-1 is inspired by and forked from [Hortator](https://deadactive.github.io/hortator/) by DEADACTIVE (0.23.3), which
is built on [Felucca](https://github.com/hugelton/Felucca) by Leo Kuroshita (Hügelton Instruments). Hortator's
sequencer, project storage, screen, PERFORM effects and compressor carry over; the tracks, machines and pages are
MNM-1's own.

- **Play it in your browser:** <https://galfaroth.github.io/mnm1/>. The page runs the real firmware, compiled to
  WebAssembly, on an FM-1 you can play with the mouse, a touch screen or the computer keyboard.
- **Install it on your FM-1:** <https://galfaroth.github.io/mnm1/webapp/installer/>, in Chrome or Edge, over the
  FM-1's USB cable. Or from a terminal: `python3 tools/fm1_install.py mnm1-0.6.0.fwsc --port FM-1 --yes`
  (needs `mido` and `python-rtmidi`).
- **What comes next:** [docs/ROADMAP.md](docs/ROADMAP.md).
- **What changed:** [CHANGELOG.md](CHANGELOG.md).

**Beta.** MNM-1 has been tested on one FM-1 only. You install it at your own risk: the author accepts no
responsibility if your FM-1 is damaged or misbehaves. M-VAVE's own updater takes you back to the official firmware
([below](#back-to-the-official-firmware)).

## What it does

- **Six tracks, one machine each.** Synth machines: SWSAW, SWPLS (SuperWave), SID, FM+, DPRO, GND, VO-6. FX machines:
  THRU, REVRB, CHRS, DYNMX, RING, PHASR, FLANG. Any machine on any track.
- **Routing.** An FX machine takes the previous track (NEIB) or a bus (AB, CD, EF) as its input,
  as an insert or a send and return. Each track's OUT picks its bus, or none.
- **8-knob pages.** SYNTH, AMP (ATK HOLD DEC REL / DIST LVL PAN PORT), FILTER (BASE WDTH HPQ LPQ / ATK DEC BOFS WOFS),
  EFFECTS (EQF EQG SRR DTIM / DSND DFB DBAS DWID), LFO 1-3 (PAGE DEST TRIG WAVE / MULT SPD INTL DPTH). Two rows of
  four on the FM-1's four knobs.
- **Filter:** a resonant high-pass into a low-pass, each with its own Q, tracking the note, with an AD envelope.
- **Tape delay on every track:** tempo-synced, ping-pong or stereo, feedback up to self-oscillation.
- **Three LFOs a track:** any knob of the track, another LFO, or pitch; FREE, TRIG, HOLD, ONE, HALF; eleven waves.
- **Parameter locks:** a lock lasts until the next trig; up to 240 a project, recorded live or set in
  LOCK mode; **parameter slide** glides locked values to the next lock.
- **Presets:** 55 factory presets for the synth and FX machines (bank MNM), besides the drum banks and 128 user
  slots. The PRESETS knob steps through them; hold EDIT for the list.
- **From Hortator:** up to 64 steps per track with its own length, PROB and A/B conditions, ratchets, accents,
  a per-track arpeggiator, live recording, mutes, SONG, the COMP sidechain compressor, PERFORM (live master effects),
  MIDI and clock over USB and TRS, MIDI LEARN, four project slots.

## On the panel

| Button | What it does |
| --- | --- |
| **EDIT** | SYNTH (the machine's 8 knobs), then AMP, then TRACK (MODEL, NOTE, OUT, IN, GATE, sends). Press again for the next row or page |
| **ENV** | FILTER |
| **FX** | EFFECTS, then SLICER, REVERB, CHORUS. Hold for PERFORM |
| **LFO** | LFO 1, 2, 3 |
| **ARP** | the track's arpeggiator: MODE SPEED RANGE CHORD |
| **SEQ** | STEP (the steps on the white keys), PATTERN, MOTION, SONG |
| **HOME** | a tap: TRACKS (levels, lengths, pans) or HOME (the scope; SELECT on to COMP). Hold: the menu |
| **GLO** | a tap: global settings; held: the white keys mute tracks |
| **SAVE** | projects (save, load, name) and tools |
| **SCL** (SEL) | KEYS on / off: the keys play the selected track (on at power-on); OCT- / OCT+ set the octave |
| **REC** | arm recording; on STEP, stopped, with KEYS on: step record |
| **PLAY** | start / stop |

**ALGORITHM** picks the track. **SELECT** changes the track's machine on the 8-knob pages, turns pages elsewhere, and
sets the tempo on TRACKS. **KNOB 1-4** edit the lit row.

### Steps and locks

- On **STEP**, tap a white key to turn a step on or off; OCT- / OCT+ change the bank of 16.
- Hold a step: KNOB 1 its chance (PROB), KNOB 2 ratchets, KNOB 3 accent, KNOB 4 SLIDE.
- **LOCK mode:** hold a step and press EDIT, ENV, FX or LFO. The page opens with the keys still the steps; hold any
  step and turn a knob to lock that value there (inverted on screen, `LOCK S5`). OCT- clears the held step's locks.
  SEQ leaves LOCK mode.
- Press REC while playing and turn knobs to record locks live.

## Building

The firmware builds with JieLi's pi32v2 toolchain and the AC79 SDK (Linux x86-64), see [BUILDING.md](BUILDING.md):

```sh
JIELI_TOOLCHAIN=... AC79_SDK=... DRUM_PACKAGE=1 ./build.sh
```

The host tests run on macOS or Linux with any C compiler: `tests/run_drum_tests.sh`.

The web simulator and site ([docs/SIMULATOR.md](docs/SIMULATOR.md)) need Python 3 with Pillow, Node 18+ and
emscripten (a native `emcc`, e.g. `brew install emscripten`, or Docker):

```sh
tools/build_sim.sh --preview --package dist/mnm1-0.6.0.fwsc
cd build/sim/preview && python3 -m http.server 8001
```

## Back to the official firmware

M-VAVE's updater, M-UPGRADE, puts it back: from [m-vave.com/download](https://www.m-vave.com/download), Downloads →
PC Software → M-UPGRADE, with PC Firmware → FM-1 V15. If the FM-1 stops answering, see
[docs/RECOVERY.md](docs/RECOVERY.md).

## Licence and credits

MNM-1's code is GPL-3.0-only, as Hortator's and Felucca's; see [LICENSE](LICENSE) and [LICENSING.md](LICENSING.md)
for the assets and third-party parts (Mutable Instruments' Plaits, Streams and Grids ports, DaisySP, CC0 samples, the
Terminus font, the JieLi SDK files in the package under Apache-2.0).

- **Hortator** by DEADACTIVE: the firmware this is inspired by and forked from
  (<https://deadactive.github.io/hortator/>).
- **Felucca** by Leo Kuroshita (@kurogedelic), Hügelton Instruments.
- M-VAVE and FM-1 are trademarks of their owners; MNM-1 is not affiliated with M-VAVE.
