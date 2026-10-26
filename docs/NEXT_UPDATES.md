# MNM-1: plan for the next updates

A work plan for the next releases of MNM-1, written so that a coding agent (Codex, Claude Code or a person) can take
one release at a time and finish it without asking. Read the whole of section 1 before touching code: it lists the
rules that break the device or the project if they are ignored.

The longer list of ideas, with the reasons, is [ROADMAP.md](ROADMAP.md). This file picks from it and orders it into
releases, with the files to change and how each one is checked.

## 1. Rules for every change

### Name and wording (hard rule)

- The product is called **MNM-1** and is described as a "six-track synth firmware for the M-VAVE FM-1".
- **Never write the names of other companies' instruments or their maker** anywhere: code, comments, tests, docs,
  commit messages, the site, firmware strings. Describe features on their own terms ("pages of eight knobs",
  "parameter locks", "a tape delay"), never as "like X" or "X-style".
- Credit: "MNM-1 by **Veff**, inspired by and forked from Hortator by DEADACTIVE, built on Felucca by Leo Kuroshita
  (Hügelton Instruments)". Never the author's real name.
- Before every commit run: `git grep -n -i -E "<the names>"` (the owner gives the list) and expect no match.

### Hardware limits

| What | Limit | Where it is checked |
| --- | --- | --- |
| 64-bit division | **not linked** on the target (`__divdi3`, `__udivdi3` undefined): new code uses 32-bit math or constant power-of-two divisors | only the target link fails: build on the toolchain before a release |
| RAM pool | 333188 of 344064 B used (about 11 KB free) | the target link and `tests/drum_test.c` |
| `.data` + `.bss` | 45164 of 98304 B | the target link |
| A project | `sizeof(project_t) <= 3840` B, 4 slots in a 15696 B NOINIT region | `_Static_assert` in `firmware/src/project.c` |
| A step | `step_t` is 4 B: one spare bit and the `rsv` byte left | `firmware/src/core.h` |
| CPU | the cost references in `tests/drum_cost_ref.txt` (`extreme_max` and the realistic kit) | `tests/drum_test.c`, `tests/mnm_test.c` |

### Data that must not break

- **Model numbers** (`enum` in `firmware/src/dmodels.c`) are stored in projects and presets: new machines are
  **appended at the end**, never inserted.
- **Preset uids** are append-only (`tools/gen_presets.py` docstring). Edit `tools/presets.json`, then run
  `python3 tools/gen_presets.py` and commit both files. Each machine's first preset must equal its knobs' defaults
  (`tests/preset_test.c` checks it).
- **A machine's knobs** (`P_E0..P_E7`) are stored as numbers. If a release changes what a knob means, either keep the
  old meaning readable (a mapping when a project loads) or bump the project magic in `firmware/src/project.c`
  ("MNM3" now) and say in the CHANGELOG that older projects load as empty. Fix that machine's presets in the same
  change.

### Workflow

1. Work on a branch; one release per branch.
2. Host tests, on macOS or Linux with any C compiler: `sh tests/run_drum_tests.sh`. It must end with
   `ALL DRUM HOST TESTS PASSED`. Add tests for every new behaviour next to the existing ones (`tests/mnm_test.c` for
   machines and routing, `tests/ui_test.c` for pages, `tests/motion_test.c` for locks, `tests/preset_test.c`).
3. Web simulator and site: `tools/build_sim.sh --preview --package dist/mnm1-X.Y.Z.fwsc` (needs Pillow, Node 18+ and
   emscripten: `brew install emscripten` on a Mac). Its tests run inside the script.
4. Firmware package: `DRUM_PACKAGE=1 ./build.sh` with `JIELI_TOOLCHAIN` and `AC79_SDK` set (BUILDING.md). The
   toolchain is Linux x86-64 only. Scan the binary: `strings -n 5 build/felucca.bin | grep -c -i -E "<the names>"`
   must print 0.
5. Version: `VERSION.txt`, a `## X.Y.Z - date` section in `CHANGELOG.md`, a tag `vX.Y.Z`.
6. Publish: push `main` and the tag to `github.com/galfaroth/mnm1`; copy `build/sim/preview/` (plus `.nojekyll`) to
   the `gh-pages` branch. Check the live page: Power on, PLAY, no console error.
7. **The owner flashes the device**, not the agent. Installing: `python3 tools/fm1_install.py dist/mnm1-X.Y.Z.fwsc
   --port FM-1 --yes`, with the FM-1 **fully charged**, a cable that sits firmly, the Mac on power and kept awake
   (`caffeinate -dimsu`). An install cut halfway bricks the unit; recovery is the Transporter route
   (`experiments/MvaveFM1Unbricker`, an RP2040-Zero on pads 0 = D+, 1 = D−, GND), which worked on 2026-10-26.

### Building the firmware on the Ubuntu box, through tmux

The JieLi toolchain runs only on Linux x86-64, so the `.fwsc` package is built on the owner's Ubuntu machine. The
agent reaches it through a **tmux session on the owner's Mac** whose pane is already logged in to that machine over
SSH (password login: the agent never types or stores the password, and **never adds an SSH key or anything else that
persists on the remote**). The owner tells the agent the session's name and the folder; the names below are
placeholders.

1. **Find the pane and check it is free.** `tmux ls`, then `tmux capture-pane -p -t <session> | tail -5`. Use it
   only when the last line is a plain shell prompt (`<user>@<host>:<dir>$`). If it shows another program (the owner
   sometimes runs an agent there), stop and ask the owner for a free shell.
2. **Send commands and read the result.** `tmux send-keys -t <session> '<command>' Enter`, wait, then
   `tmux capture-pane -p -t <session> | tail -N`. Start long commands with `clear;` so the capture shows only their
   output, and end them with an `echo DONE_<tag>` (or `echo EXIT=$?`) to wait for.
3. **The build folder** on the Ubuntu box (the owner's layout today, `<dir>`): `jieli/jieli-linux-toolchains-20260730.1`
   (the toolchain), `sdk/` (the AC79 SDK), and the source.
4. **Get the source there.**
   - Pushed code (simplest): `git clone https://github.com/galfaroth/mnm1 mnm` once, then
     `cd mnm && git fetch && git checkout <branch-or-tag>`.
   - Unpushed changes: make a patch on the Mac, `git diff <base> HEAD --binary -- . ':!docs' ':!web' | gzip -9 |
     base64 -b 76 > p.b64`, then in the pane run `stty -echo; cat > p.b64; stty echo`, paste the file with
     `tmux load-buffer -b p p.b64 && tmux paste-buffer -b p -t <session>`, send `C-d`, and compare
     `sha256sum p.b64` on both sides before `base64 -d p.b64 | gunzip > p.patch && git apply --binary p.patch`.
5. **Build:**

   ```sh
   export JIELI_TOOLCHAIN=<dir>/jieli/jieli-linux-toolchains-20260730.1 AC79_SDK=<dir>/sdk
   clear; (DRUM_PACKAGE=1 ./build.sh > ../build.log 2>&1; echo BUILD_EXIT=$? >> ../build.log); tail -5 ../build.log
   ```

   Success ends with `BUILD_EXIT=0` and `build/felucca-UNTESTED.fwsc`. A link error naming `__divdi3` or `__udivdi3`
   is a 64-bit division in new code: rewrite it in 32-bit math.
6. **Check the binary there:** `strings -n 5 build/felucca.bin | grep -c -i -E "<the names>"` must print 0;
   `strings -n 5 build/felucca.bin | grep MNM1-` shows the version.
7. **Bring the package to the Mac:** in the pane, `sha256sum build/felucca-UNTESTED.fwsc`, then serve it for two
   minutes only: `cd build && (timeout 120 python3 -m http.server 8765 >/dev/null 2>&1 &); cd ..`. On the Mac:
   `curl -s -o dist/mnm1-X.Y.Z.fwsc http://<host>:8765/felucca-UNTESTED.fwsc` and compare `shasum -a 256` with the
   remote sum. `dist/` is git-ignored.

### Definition of done for a release

- Host tests, simulator tests and site tests pass; the new behaviour has its own tests.
- The target build links (no 64-bit division), the RAM pool and the project size fit, and the cost tests pass.
- CHANGELOG, README (features) and the site's manual (`web/sim/index.html`) describe the change. ROADMAP.md moves
  the item out of "planned".
- No forbidden name anywhere, the binary included.

## 2. Code to reuse: MONO2K

[MONO2K](https://github.com/InigoHernandez/mono2k) by Inigo Hernandez is another six-track synth firmware for the FM-1
(1.0.3, runs on the device). It is **GPL-3.0-only, like MNM-1**, so its code may be ported into MNM-1, with these
conditions:

- Each ported file or function keeps the GPL header and gets a credit line: `Ported from MONO2K by Inigo Hernandez
  (GPL-3.0-only), https://github.com/InigoHernandez/mono2k`. LICENSING.md and the README's credits list it.
- Its comments name other companies' instruments throughout: **rewrite every ported comment** so that no forbidden name
  survives (the rule in section 1).
- Port the algorithms, not the engine. MONO2K renders 16-sample blocks with knobs as 0..127 bytes (`P7()`), per-track
  state in `trk_t` (`ph[]`, `ms[]`, `menv[]`) and its own tables (`tools/gen_tables.py`, `tools/gen_waves.py`).
  MNM-1 renders 32-sample blocks (`CTL`), a machine is a `DM_*_DEF` in `firmware/src/mm_synth.c` with int16 knobs
  and their own ranges, per-voice state in `dvoice_t` / `mm_t`, and the AMP envelope, PORT and the filter come after
  it (`mm_out`). Port each machine as a new `mm*_trigger` / `mm*_render` pair, with knob ranges converted.
- Its code is integer-only, without 64-bit division (its own rule), and it runs on the FM-1, so the algorithms fit
  the CPU. RAM does not come free: MONO2K keeps per-track buffers that MNM-1 cannot afford all at once (a 1024-sample
  chorus line per track for the ensemble machine is 12 KB for six tracks, more than MNM-1 has free). Share one buffer,
  halve it, or limit the machine to one or two tracks, and check the RAM pool after each port.

What is there (`firmware/src/m2k_dsp.c`, `machine_render`, one `case` per machine; knob lists as in its comments):

| MONO2K machine | Its knobs | Useful for |
| --- | --- | --- |
| `M_SAW` | UNIL UNIW UNIX DRFT SUBX SUB1 SUB2 TUNE | 0.7.0 SWSAW |
| `M_PUL` | UNIL UNIW SUB1 SUB2 PW PWAD PWRS TUNE | 0.7.0 SWPLS |
| `M_SID` | PW PWAD PWRS WAVE MOD MSRC MFRQ TUNE | 0.7.0 SID |
| `M_ENS` | PCH2 PCH3 PCH4 WAVE PW CHRL CHRW TUNE | 0.8.0 SWAVE-ENS (mind the RAM) |
| `M_FMS`, `M_FMD`, `M_FMP` | 1FRQ 1FIN 1ENV 1FB 2FRQ 2VOL TONE TUNE; 1FRQ 1FEN 1VOL 1VEN 2FRQ 2ENV 2FB TUNE; 1FRQ 1ENV 2FRQ 2ENV 3FRQ 3ENV TONE TUNE | 0.8.0 the three FM+ machines |
| `M_GSIN`, `M_GNOI` | FOLD SUB NOIS PDEP PDEC CLIK HARM TUNE; ST RED STON TUNE | 0.8.0 the GND split |
| `M_VOX` | VOC1 VOC2 V-SW VOIC CONS CLEN CVOL TUNE | 0.8.0 VO-6 consonants (its author lists the consonants as still being tuned) |
| `M_WAV` | WAVE SCAN STIM SYNC BITS DTUN MIX2 STEP | a wavetable machine; compare with the plan's DPRO-WAVE before porting |
| `M_DKIK`, `M_DSNR`, `M_DHAT`, `M_DCYM` | kick, snare, hat, cymbal | extras, if wanted |
| FX chorus / ring / dynamics (`M_FXC`, `M_FXR`, `M_FXD`) | | compare with MNM-1's own FX machines |

Also there: the trig lanes (`ST_NOAMP`, `ST_NOFLT`, `ST_NOLFO` in `m2k.h`, used in `firmware/src/m2k_seq.c`) for
0.9.0, trig conditions, and an amplitude envelope whose curves its README documents. MONO2K's README describes how
each machine's levels and curves were set; read it before porting a machine, and keep MNM-1's own tests passing.

The source is cloned next to this repo in `experiments/mono2k` (or `git clone` it).

## 3. Releases

Ordered by value and risk. Each one is small enough for one agent session. Do them in this order unless the owner
says otherwise.

### 0.6.1: polish (small, low risk)

- **Preset levels:** ORGAN (DPRO) and ROBOT (VO-6) play about 10 dB under the others (`tests/mnm_test.c`
  `test_presets` prints rms). Raise them by their own knobs (DPRO: a little FOLD or MIX2; VO-6: RES, SRC) so their rms
  is within 6 dB of the bank's median. Change `tools/presets.json` only, keep the uids.
- **Clear one lock:** in LOCK mode, a step held + OCT− clears all of the step's locks today. Add: step held + a knob
  turned while OCT− is held clears only that knob's lock on that step (`firmware/src/ui_input.c`, `motion.c`
  `lock_drop` already takes a parameter id). Test in `tests/ui_test.c`.
- **Site:** add the presets to the manual's page table and say which buttons open them.

Acceptance: the two presets' rms within 6 dB of the median; a test locks two knobs on one step, clears one, and the
other stays.

### 0.7.0: deeper machines, part 1 (SWAVE and SID)

New knob sets on the existing machines (same model numbers). Start from MONO2K's `M_SAW`, `M_PUL` and `M_SID`
(section 2) rather than from scratch:

- **SWSAW:** UNIL (unison pair level), UNIW (unison detune), UNIX (an extended pair at twice UNIW, limited by
  UNIL), SUBX (square one octave down), SUB1 (sine one octave down), SUB2 (sine two octaves down), TUNE, and one spare.
- **SWPLS:** UNIL, UNIW, SUB1, SUB2, PW (centre is square, both ends a spike), PWAD (automatic PW sweep speed, 0 off),
  PWRS (restart the sweep on each note), TUNE.
- **SID:** PW, PWAD, PWRS, WAVE (TRI SAW PULS MIX NOIS), MOD (OFF RING SYNC R+S), MSRC (MFRQ or the previous
  track's pitch), MFRQ, TUNE. The previous track's pitch is track N−1's current pitch (its `mm_p16`, `mm_synth.c`).

Files: `firmware/src/mm_synth.c` (the `DM_MM*_DEF` tables and the render functions), `tools/presets.json` (the MNM
bank's SWSAW, SWPLS and SID presets redone, new uids for redesigned sounds, the first preset of each = the new
defaults), `firmware/src/project.c` (project magic "MNM4", or a load-time mapping), `tests/mnm_test.c`.

Acceptance:
- every new knob changes the sound, checked by a render test (rms or spectrum before and after);
- SID with MSRC = previous track: track 2's ring or sync follows track 1's note (a frequency test);
- PWAD sweeps (the spectrum moves over time), PWRS restarts it on each note;
- the cost tests still pass with six SWSAW tracks at full unison.

### 0.8.0: deeper machines, part 2 (new machines)

Appended at the end of the model enum, so older projects keep their machines. MONO2K has `M_FMS`, `M_FMD`,
`M_FMP`, `M_GSIN`, `M_GNOI`, `M_VOX` and `M_ENS` to port from (section 2):

- **FM+ split into three:** FM+STAT (two FM blocks, curated ratios, feedback, tone), FM+PAR (three parallel blocks),
  FM+DYN (continuous frequencies with their own envelopes).
- **GND split:** GND-GND (silence), GND-SIN (a plain sine), GND-NOIS (noise with colour, stereo width and its switch).
- **VO-6 consonants:** CONS (a list: none, B D F G H J K L M N P R RR S SJ T TJ TH V Z), CLEN, CVOL; a consonant plays
  before the vowel on each trig.
- **DPRO-WAVE:** 32 single-cycle waves of 512 samples (generate them with a script in `tools/`, store them as a
  const table in flash, never in RAM), WP morph, WPM sweep, WPRS restart, SYNC (own frequency or the previous track).

Acceptance: every new machine has a render test, at least its first preset (= its defaults) and up to 7 more in the
MNM bank; RAM pool and cost within limits; the MODEL list on the device and the simulator show them.

### 0.9.0: the sequencer, part 1 (trig types)

- **Three trig lanes per track:** AMP, FILTER and LFO (MONO2K does it with three step flags, `ST_NOAMP`,
  `ST_NOFLT`, `ST_NOLFO`: see section 2). A trig in the ALL lane sets all three and a pitch; a trig in
  one lane fires only that envelope or LFO and carries no pitch. Store the lanes in the step's spare bit and `rsv`
  byte (`step_t`), so the project does not grow.
- **Trigless trigs** (a pitch and/or locks, no envelope), **note-off trigs** (start the release), **pitchless trigs**
  (fire the envelopes, keep the previous note).
- UI: a lane selector on the STEP page (a free button or knob; propose one and document it).

Acceptance: render tests for each trig type (an envelope fires or not; the pitch changes or not); old projects
(steps without lanes) play exactly as before (a regression test with a saved project).

### 0.10.0: the sequencer, part 2 (timing and arpeggiator)

- **Swing as a step mask:** one amount for the pattern (50 to 80 %) and per track a mask of the steps it delays
  (default: every second sixteenth).
- **Speed multipliers** 3/4X and 3/2X besides 1X and 2X.
- **Arpeggiator:** modes KEY (held keys), SID (latched, two or more keys start it), ADD (notes accumulate while keys
  are held); PLAY order TRUE UP DOWN CYCLE RND; RNGE (octaves); OJMP (where in the chord the octave jumps);
  retrigger switches for AMP, FLT and LFO; a 16-step rhythm track with a note offset per step.
- **Transpose:** per track and per pattern, SCALE (chromatic, FIX, MAJ, MIN) and KEY.

Acceptance: timing tests against the sample clock (swing positions, 3/4X step length), arpeggiator note-order tests
for each mode, and the existing ARP tests updated or kept.

### 0.11.0: playing and performance

- **TRIG POS:** a track's notes forwarded to another track (chainable).
- **MULTI TRIG:** one keyboard plays all six tracks at once; a keyboard split (a key and a first track); notes
  restart or transpose the pattern; a global ADSR over all tracks in those modes.
- **ASSIGN:** velocity and keytrack to any two knobs with an amount (−64..+63); HP and LP keytracking switches for the
  filter.

### 1.0.0: memory

- **Patterns and kits:** more than 4 projects, a pattern/kit split stored in the FM-1's free flash (the area map is
  in `experiments/MvaveFM1Unbricker/fm1_unbrick.py`: `0x97000..0xE0000` is the main store; check what is free in a
  real dump in `experiments/fm1-rescue/backup/`). This is the largest change: write a design note in `docs/` first
  and get the owner's approval before coding.
- **Song mode:** rows with transpose, offset, length, mutes and tempo, loops and jumps.

### Any time: extras (ask the owner first)

- **Grids** as a trig source for any track (the engine is still in `firmware/src/grids.c`).
- **FX-RESON:** the resonators back as an FX machine fed by NEIB or a bus (RAM is tight: measure first).
- **Stereo WID** on the chorus, phaser and flanger FX machines.
- **24 dB filter slopes** if the cost tests allow it.

## 4. Notes for the agent

- The firmware is one translation unit (`firmware/src/felucca.c` includes the `.c` files); the host tests include the
  same sources. A new source file must be added there and in the simulator's list (`tests/sim_host.h`;
  `tests/sim_test.c` "drift" fails otherwise).
- The UI draws on a 240 x 240 screen; `tests/ui_test.c` writes screenshots to `build/ui_shots/`: look at them after
  any UI change.
- Sound design can't be fully checked by tests. For presets and new machines, render WAV files (`tests/mnm_test.c`
  writes demos when given a folder: `build/host/mnm_test build/mnm_renders`) and leave the final judgment to the
  owner's ears.
- Keep commits small, with a message that says what changed and why. End every commit message with the
  attribution line the owner's tooling asks for.
