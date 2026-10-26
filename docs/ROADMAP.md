# MNM-1 roadmap

What is planned for MNM-1, within what the FM-1's panel, CPU and memory allow. Nothing here is promised by a date;
the order is the current plan.

Legend: **[next]** being worked on next, **[planned]** wanted and feasible, **[maybe]** possible but costly on the
FM-1, **[no]** not possible on this hardware.

## 1. Deeper machines [next]

The families are there (GND, SWAVE, SID, DPRO, FM+, VO and the 7 FX machines). Each gets a fuller set of knobs and
variants:

- **SWAVE-SAW, SWAVE-PULSE:** unison level and width, an extended unison pair, square and sine subs; PW with an
  automatic sweep and its restart. [next]
- **SWAVE-ENS:** chords of up to 4 notes from one track (three pitch offsets, wave, PW, chorus level and width).
  [planned]
- **SID:** PW sweep and restart, MOD (RING, SYNC, both), and a modulation source that can be the previous track's
  pitch. [next]
- **DPRO-WAVE:** 32 single-cycle waves with a morph, its sweep and restart, and sync (own frequency or the previous
  track's pitch). [planned]
- **DPRO-BBOX:** 24 percussion sounds across two octaves, START, RTRG, RTIM. [maybe: sample memory]
- **DPRO-DDRW, DPRO-DENS:** double wave and ensemble. [maybe]
- **FM+STAT, FM+PAR, FM+DYN:** three FM machines instead of one. [planned]
- **VO-6:** consonants (CONS, CLEN, CVOL) and a two-formant vowel space. [planned]
- **GND-GND, GND-SIN, GND-NOIS:** three separate machines (NOIS with stereo width and colour). [planned]
- **FX:** stereo WID on CHORUS, PHASER and FLANGER. [planned]

## 2. Sequencer

- **AMP, FILTER and LFO trig tracks** per track, so pitch and envelopes trigger separately. [planned]
- **Trigless trigs** (a pitch or locks with no envelope), **note-off trigs**, **pitchless trigs**. [planned]
- **Clear one lock:** in LOCK mode hold a step and OCT−, then turn its knob. [implemented: 0.6.1]
- **Swing as a step mask:** one amount for the pattern and a per-track mask of the steps it delays. [planned]
- **Tempo multipliers 3/4X and 3/2X.** [planned]
- **A fuller arpeggiator:** KEY, SID and ADD modes, PLAY (TRUE UP DOWN CYCLE RND), RNGE, OJMP, AMP/FLT/LFO retrigger
  switches and a 16-step rhythm and note-offset track. [planned]
- **Transpose:** TRACK, PAT, SCALE (FIX MAJ MIN) and KEY. [planned]
- **Copy, paste and clear** of steps, tracks and patterns. [planned]

## 3. Memory and structure

- **More patterns and kits.** MNM-1 has 4 project slots (each a set of sequences and sounds), limited by the ~15 KB
  of flash it keeps for projects. A pattern/kit split stored in the FM-1's free flash is planned. [planned]
- **A fuller song mode:** rows with transpose, offset, length, mutes, BPM, nested loops and jumps. [maybe]

## 4. Performance and playing

- **POLY mode:** the selected track as a 6-voice synth. [maybe: CPU]
- **MULTI TRIG:** one keyboard playing all tracks at once, a keyboard split, the pattern restarted or transposed by
  the keys, with a global envelope; patterns laid out across the keyboard. [planned]
- **TRIG POS:** forward a track's notes to another track. [planned]
- **ASSIGN:** velocity and keytrack to any knob; HP and LP keytracking switches. [planned]
- **6 MIDI sequencer tracks** (LEN VEL PB PCHG CC1-4). [maybe]

## 5. Sound and signal path

- **24 dB filter slopes** (the filter's stages are 12 dB now, to fit the CPU budget). [maybe]
- **DIST inside the filter**, setting the filter's and EQ's headroom (now a separate insert). [planned]
- **Longer delays at full quality** (each track's tape loop has 4096 cells per channel; above ~93 ms it gets grainy).
  [maybe: RAM]
- **External audio inputs for FX machines.** [no: the FM-1 has no audio input]
- **Six outputs.** [no: one stereo output]

## 6. From Hortator

Kept from Hortator: per-track length, PROB and A/B conditions, ratchets, accents, note length (GATE), the slicer,
PERFORM and the COMP sidechain compressor (HOME, then SELECT to COMP).

Taken out to make room, and could come back as MNM-1 machines:

- **RESON** (string, pipe, chord and modal resonators) as an FX machine, FX-RESON, on a track fed by NEIB or a bus.
  [maybe: RAM]
- **Grids** (Mutable Instruments' pattern generator) as a trig source for any track. [planned]

## 7. Look and feel

- The MNM theme's colours may change.
- Eight knobs on four encoders: two rows of four, the page button switches rows.
