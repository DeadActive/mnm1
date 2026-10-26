# MNM-1 roadmap

What is planned for MNM-1, within what the FM-1's panel, CPU and memory allow. Nothing here is promised by a date;
the order is the current plan.

Legend: **[next]** being worked on next, **[planned]** wanted and feasible, **[maybe]** possible but costly on the
FM-1, **[no]** not possible on this hardware.

## Current release: 0.11.0

Implemented through 0.10: the available MONO2K engines, presets and effects; expanded storage (8 patterns,
8 kits and 128 user sounds after explicit migration); separate AMP/FILTER/LFO trig lanes and special trig
kinds; swing masks, fractional tempo multipliers, manual arpeggiator modes and transpose/scale controls.
The firmware and matching browser simulator are published together. A 0.10 stress run reset during an
interrupt return; all 187 cases were measured across two runs, without establishing uninterrupted stability.
Some heavy combinations exceed the audio deadline. This remains a beta release.

## Implemented in 0.11 — performance

- TRIG POS, including forwarding between tracks.
- MULTI TRIG: all tracks, split keyboard, sequence restart/transpose, timing and shared envelope.
- Two velocity and two keytrack assignments per track; independent HP/LP keytracking switches.

## Next: 1.0 — SONG

- Song rows with transpose, offset, length, mutes and tempo.
- Nested loops and jumps, followed by integration and hardware validation.
- A musical demo SONG with kick, snare, melody, parameter locks and audible phaser/FX transitions.
- An accessible NEW SONG action to leave the demo and start an empty arrangement without erasing saved work;
  the demo remains available to load again.

The optional items below are outside the planned 1.0 release. Missing source features, including VOX
consonants, still require implementation and validation; they are not claimed as imported.

## 1. Deeper machines

0.7.0 ports 19 calibrated machines and the complete audio chain, 120 full factory sounds, source LFO/ARP laws,
96 wave cycles (32 generated plus 64 owner-authored), restored resonator FX and additional bus effects.
Legacy models remain separate. The source's missing VOX consonants remain planned. See [PORT_PLAN.md](PORT_PLAN.md)
for the evidence, compatibility approach and approved expanded-storage layout.

The families are there (GND, SWAVE, SID, DPRO, FM+, VO and the 7 FX machines). Each gets a fuller set of knobs and
variants:

- **SWAVE-SAW, SWAVE-PULSE:** unison level and width, an extended unison pair, square and sine subs; PW with an
  automatic sweep and its restart. [implemented: SAW2/PUL2 in 0.7.0]
- **SWAVE-ENS:** chords of up to 4 notes from one track (three pitch offsets, wave, PW, chorus level and width).
  [implemented: ENS in 0.7.0]
- **SID:** PW sweep and restart, MOD (RING, SYNC, both), and a modulation source that can be the previous track's
  pitch. [implemented: SID2 in 0.7.0]
- **DPRO-WAVE:** 32 single-cycle waves with a morph, its sweep and restart, and sync (own frequency or the previous
  track's pitch). [implemented: WAVE in 0.7.0]
- **DPRO-BBOX:** 24 percussion sounds across two octaves, START, RTRG, RTIM. [maybe: sample memory]
- **DPRO-DDRW, DPRO-DENS:** double wave and ensemble. [maybe]
- **FM+STAT, FM+PAR, FM+DYN:** three FM machines alongside the legacy model. [implemented: FMST/FMPAR/FMDYN in 0.7.0]
- **VO-6:** consonants (CONS, CLEN, CVOL) and a two-formant vowel space. [planned]
- **GND-GND, GND-SIN, GND-NOIS:** sine and noise with stereo width and colour. [implemented: G-SIN/NOISE in 0.7.0; silence remains a muted track]
- **FX:** stereo WID on CHORUS, PHASER and FLANGER. [planned]

## 2. Sequencer

- **AMP, FILTER and LFO trig tracks** per track, so pitch and envelopes trigger separately. [implemented: 0.9.0]
- **Trigless trigs** (a pitch or locks with no envelope), **note-off trigs**, **pitchless trigs**. [implemented: 0.9.0]
- **Clear one lock:** in LOCK mode hold a step and OCT−, then turn its knob. [implemented: 0.6.1]
- **Swing as a step mask:** one amount for the pattern and a per-track mask of the steps it delays. [implemented: 0.10.0]
- **Tempo multipliers 3/4X and 3/2X.** [implemented: 0.10.0]
- **A fuller arpeggiator:** KEY, SID and ADD modes, PLAY (TRUE UP DOWN CYCLE RND), RNGE, OJMP, AMP/FLT/LFO retrigger
  switches and a 16-step rhythm and note-offset track. [implemented: 0.10.0]
- **Transpose:** TRACK, PAT, SCALE (FIX MAJ MIN) and KEY. [implemented: 0.10.0]
- **Copy, paste and clear** of steps, tracks and patterns. [planned]

## 3. Memory and structure

- **More patterns and kits.** 8 patterns, 8 kits and 128 full user sounds after explicit migration.
  Migration reuses sample slots 2 and 3; slot 1 stays available. [implemented: 0.8.0]
- **A fuller song mode:** rows with transpose, offset, length, mutes, BPM, nested loops and jumps. [planned: 1.0]

## 4. Performance and playing

- **POLY mode:** the selected track as a 6-voice synth. [maybe: CPU]
- **MULTI TRIG:** one keyboard playing all tracks at once, a keyboard split, the pattern restarted or transposed by
  the keys, with a global envelope; patterns laid out across the keyboard. [planned]
- **TRIG POS:** forward a track's notes to another track. [planned]
- **ASSIGN:** velocity and keytrack to any knob; HP and LP keytracking switches. [planned]
- **6 MIDI sequencer tracks** (LEN VEL PB PCHG CC1-4). [maybe]

## 5. Sound and signal path

- **24 dB filter slopes** (the filter's stages are 12 dB now, to fit the CPU budget). [maybe]
- **DIST inside the filter**, setting the filter's and EQ's headroom. [implemented for calibrated models: 0.7.0]
- **Longer delays at full quality** (each track's tape loop has 4096 cells per channel; above ~93 ms it gets grainy).
  [maybe: RAM]
- **External audio inputs for FX machines.** [no: the FM-1 has no audio input]
- **Six outputs.** [no: one stereo output]

## 6. From Hortator

Kept from Hortator: per-track length, PROB and A/B conditions, ratchets, accents, note length (GATE), the slicer,
PERFORM and the COMP sidechain compressor (HOME, then SELECT to COMP).

Taken out to make room, and could come back as MNM-1 machines:

- **RESON** (string, pipe, chord and modal resonators) as an FX machine, FX-RESON, on a track fed by NEIB or a bus.
  [implemented: STRNG/PIPE/CHORD/MODAL in 0.7.0]
- **Grids** (Mutable Instruments' pattern generator) as a trig source for any track. [planned]

## 7. Look and feel

- The MNM theme's colours may change.
- Eight knobs on four encoders: two rows of four, the page button switches rows.
