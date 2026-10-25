# Changelog

MNM-1's versions, newest first. MNM-1 is inspired by and forked from Hortator 0.23.3 by DEADACTIVE; Hortator's
own history is in its repository.

## Unreleased

## 0.5.1 - 2026-10-25

- The PRESETS bank of the TRX and EFM voices is named MD. ABOUT's credits are shorter.
- Web simulator: the firmware compiled to WebAssembly, on the project's page, with MNM-1 demos (four projects built on
  the power-on kit: SID lead, DPRO pad, VO-6 voice, FM+ bass) and a reel of the 8-knob pages.

## 0.5.0 - 2026-10-25

- Parameter locks: a step with a trig or a lock first puts the locked knobs back to the kit's value, then sets its
  own locks. A locked value lasts until the next trig, and an unlocked trig plays the kit's sound. A lock on a step
  with no trig applies at that step. Up to 240 locks per project.
- Parameter slide: on a SLIDE step (STEP page, hold a step, KNOB 4) each locked knob glides to its next lock on the
  track. Unlocked trigs in between don't stop it; a knob locked only once doesn't slide.
- LOCK mode: on STEP, hold a step and press EDIT, ENV, FX or LFO. The 8-knob page opens with the keys still the
  steps. Hold a step and turn a knob to lock it there; OCT- clears the held step's locks; SEQ leaves.
- Projects are "MNM3"; older records load as empty.

## 0.4.0 - 2026-10-25

- FILTER (ENV): a resonant high-pass at BASE into a low-pass at BASE + WDTH, with separate HPQ and LPQ, tracking the
  note (BASE 0: two octaves under it, 8 steps an octave), and an AD envelope adding BOFS to BASE and WOFS to WDTH.
- EFFECTS (FX): EQF EQG SRR DTIM / DSND DFB DBAS DWID. A peaking EQ (+-36 dB) and a stereo tape delay on every
  track: DTIM in 1/256 notes, DSND's sign keeps the image or ping-pongs, DFB above 64 grows into the clip.
- Three LFOs per track: PAGE DEST TRIG WAVE / MULT SPD INTL DPTH. Any knob of SYN, AMP, FLT or EFX, another LFO's
  knobs, or PTCH (vibrato to 16 octaves). Tempo-locked, FREE TRIG HOLD ONE HALF, eleven waves, INTL.
- The REVERB and CHORUS sends move to TRACK; SLICER is its own page. Projects are "MNM2".

## 0.3.0 - 2026-10-25

- 8-knob pages: two rows of 4 on the FM-1's four knobs, a tab strip (SYN AMP FLT EFX LF1 LF2 LF3).
  Pressing a page button again steps through its rows and pages. SELECT picks the track's machine on these pages.

## 0.2.0 - 2026-10-25

- Six tracks. FX machines (THRU REVERB CHORUS DYNAMIX RINGMOD PHASER FLANGER) fed by the
  previous track (NEIB) or a bus (AB, CD, EF), as an insert or a send and return; each track's OUT bus.
- The keys play the selected track's notes at power-on (SCL turns KEYS off).
- Power-on kit: GND kick, FM+ snare through a FLANGER comb, SWSAW bass, SWPLS arp into a REVERB send.

## 0.1.0 - 2026-10-24

- Seven synth machines (SWSAW SWPLS SID FM+ DPRO GND VO-6) with an AMP envelope (ATK HOLD DEC REL)
  and portamento, a dual HP/LP filter, per-track SRR and an arpeggiator per track. MNM theme and name.
