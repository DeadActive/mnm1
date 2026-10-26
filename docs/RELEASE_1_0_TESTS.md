# MNM-1 1.0 release validation

The owner confirmed physical-panel SAVE works and explicitly authorized publishing
1.0 with the updated site and no public roadmap. The published package is the
same binary installed and checked as RC1, reporting `MNM1-1.0.0`.
The remaining checks below are recorded honestly; publication does not turn
pending checks into passes.

## Implemented

- SONG: 16 rows, pattern/repeats, transpose, offset, length, per-track mutes,
  tempo, finite nested loops and jumps.
- SONG FILE: NEW clears the working song/kit; DEMO loads AFTER HOURS. Both
  confirm replacement of unsaved work and leave saved projects, kits, sounds
  and samples untouched. Saved demo arrangements retain their built-in sources.
- Old project/pattern readers preserve older data; reads do not rewrite flash.
- Mixer meters retain the outlined background and solid level fill from 0.11.

## SONG controls

Press SEQ repeatedly or use SELECT within its page family.

| Page | Knob 1 | Knob 2 | Knob 3 | Knob 4 |
|---|---|---|---|---|
| SONG | Row | Pattern A–H (demo A–B) | Repeats 1–16 | Whole-song loop |
| SONG RANGE | Row | Transpose −48…48 | Source offset 0…63 | Length AUTO/1…64 |
| SONG MIX | Row | Track 1–6 | Row mute OFF/ON | Tempo KEEP/40…240 |
| SONG FLOW | Row | Jump NEXT/row | Extra jumps INF/1…16 | Whole-song loop |
| SONG FILE | NEW (turn right) | DEMO (turn right) | — | — |

Length AUTO retains each source track's length. A longer row repeats its source
steps; offset follows notes, locks, slides and swing masks. Row transpose adds to
pattern transpose; FIX tracks ignore both. Row mutes add to the pre-song mute mask.
Tempo KEEP retains the preceding row's tempo (the original tempo in the first row).
STOP restores original tempo, mutes and sequencer options.

JUMP selects a destination after the row's repeats. COUNT is the number of extra
jumps before continuing to the next row; INF always takes the jump. Backward outer
loops reset the inner counters on re-entry. Whole-song LOOP resets all counters.
Deleting a destination row clears jumps to it and renumbers later destinations.

AFTER HOURS has eight rows, two repeats each at 112 BPM, about 34.29 seconds per
loop. It demonstrates drums, bass, melody, parameter locks, phaser, row mutes,
transposition and offset. It never loads automatically over an existing project.

## Host evidence

- Full regression, target build and simulator results are recorded in PORT_PLAN.md
  and the candidate manifest after preparation.
- Full demo traversal: all eight rows and the return to row 1; saved flash and
  project/user records unchanged. The exported WAV is an aid to listening, not
  proof of target timing or speaker sound.
- Project saves: all four banks, full 3648-byte payload, startup/readback after
  clearing RAM, failure at every erase/program operation and simulated protection.
- Expanded library: atomic saves, interrupted migration, pattern/kit/sound groups,
  old pattern versions and unchanged reads remain covered by the existing tests.

## Device checklist — scope and remaining coverage

1. **Reported bank-A error (not reproduced; SAVE accepted by owner).** Record the exact UI message and
   whether transport was stopped. After attempting SAVE, read USB `storage` and
   `status` before another save changes the diagnostic. Do not clear flash
   protection or format a library to hide this error.
2. **Legacy save/readback/reboot.** Save a known pattern in A while stopped;
   change the working pattern, LOAD A, then restart the device and compare notes,
   sounds, locks, SONG rows and performance settings. Repeat a second save to
   exercise both A/B sector copies. Confirm another saved bank stays unchanged.
3. **Demo and NEW.** Load DEMO from SONG FILE; play at least two complete loops,
   listen for kick/snare/melody/locks/phaser, test STOP/resume and row changes.
   NEW must leave an empty working arrangement while saved slots remain loadable.
4. **SONG edges.** Test offset through source wrap, shortened/extended length,
   muted rows, tempo changes, nested finite loops and an explicit infinite jump.
   STOP must restore original controls. Try internal and external MIDI clock.
5. **Performance regression.** MULTI, overlapping panel/MIDI notes, ARP, controller
   assignments and locks; verify no stuck notes after STOP or kit load.
6. **Expanded storage migration.** Only after the save fault is understood and
   resolved: execute the owner's already-authorized `library migrate-waived`.
   It reuses sample slots 2/3 and preserves slot 1. Then save/load/reboot patterns,
   kits and full USER sounds, including A and H. No migration ran during offline
   preparation. The owner waived a backup; no new approval is required.
7. **Timing and release.** Run the full device benchmark with matching host
   report, record CPU/late halves and stack/timeouts, and distinguish any manual
   intervention from device failure. Heavy combinations can exceed CPU limits.
   The owner accepted publication after the save checks; a fresh full 1.0 benchmark
   remains pending.

## Read-only USB storage diagnostic

`storage` prints dispatch mode, library readiness, last project/settings result,
last legacy save error, operation, object, address, SFC mode and flash status.
It does not write flash or change protection.

| Stage | Operation |
|---|---|
| 0 | Last legacy save succeeded |
| 1 | Validate input / locate current copy |
| 2 | Erase destination sector |
| 3 | Program payload page |
| 4 | Program commit header |
| 5 | Verify readback |

Driver errors: −2 unsupported continuous-read mode; −3 write-enable latch unset;
−4 timeout; −7 readback mismatch; −8 rejected wrapper address. Expanded-library
errors have their own codes; use `expanded`, `library_ready` and
`project_save_error` before interpreting a legacy stage.

`SAVED; BOOT ERROR` means the project committed but the startup-bank marker did
not. Manual LOAD can retrieve the new project, but restart may select the old
bank until a successful retry. Ordinary `SAVE ERROR` must never be treated as a
successful persistent save. Software fault tests do not identify the cause of
the owner's physical-device error.

Explicit USB test actions: `storage save-a yes` invokes the ordinary bank-A SAVE
while stopped; `reboot yes` restarts while stopped. Neither runs from a plain
`storage` query. The save action replaces bank A with the current working project.

## Device evidence — 2026-10-26, RC1

Installed from the Mac after the Ubuntu target build; CDC reports `MNM1-1.0.0`.
Package SHA-256: `44c1285da48dcf27d83e42ac8fa1652cc6b1df28f8b42285c285780e83e9aac5`.
Two stopped bank-A saves through the ordinary `project_save(0)` path committed
3648-byte MNM8 records (sequences 5 and 6); both header and payload CRCs passed.
After an explicit firmware reboot, both sectors were byte-identical to their
pre-reboot state; bank B's two sectors were unchanged throughout. A command
without `yes` also left A unchanged. Project/settings error codes were zero.
Flash status was 0x0200 and SFC mode 3. Evidence: `build/bench/1.0.0/save-device.txt`.

This verifies saving the current stopped working project (GND, 120 BPM), not
all populated-song hardware cases. The earlier owner-reported error was not
reproduced and its cause is not established. The populated-song host fault
suite passed again under ASan/UBSan. Migration, full-song hardware audition and
final target performance acceptance remain separate checks before publication.

Owner confirmation: panel SAVE works; publish 1.0 and remove the public roadmap.
