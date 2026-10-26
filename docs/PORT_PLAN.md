# Audio ports and storage plan

Scope updated by the owner on 2026-10-26: bring over the better MONO2K audio algorithms, parameter laws, full sounds,
and missing effects from MONO2K and Hortator. Keep MNM-1's panel controls, page layout, theme and web interface.

## Baseline and evidence

- Verified fallback: v0.6.1, commit 6695245. Host suite, simulator/site and Ubuntu native build passed.
- Source: MONO2K commit 78d7cdfd676dd8a88a06e6b794fbcfb5fa2b1537, GPL-3.0-only. Credit each port.
- Source measurements use an owner-supplied OS in an emulator. They are not measurements made on a physical unit
  in this session. Preserve its documented tolerance limits; do not describe the result as exact replication.
- The source contains 24 current factory sounds and 96 migrated historical sounds in its host fixture. Import these
  as separate banks, with full sound parameters and provenance. Do not copy only SYNTH's eight values.
- Its VOX consonant and vowel-switch controls are not implemented. Its waves and drum synthesis are its own data and
  algorithms, not copied original wave or percussion samples.

## Integration order

1. Preserve v0.6.1 as an installable fallback. Existing model numbers and preset uids remain stable.
2. Add the calibrated audio primitives with numerical tests. Keep the legacy sound path available for existing
   projects and sounds; new calibrated machines must not silently reinterpret legacy knob values.
3. Port the chain as a unit: input headroom, EQ, resonant LP, clip, HP stages, remaining LP poles, AMP, VOL, SRR,
   stereo PAN/LEVEL. Keep the old drum path. Preserve the donor's gate semantics and 16-sample control cadence.
4. Port the complete machine algorithms and exact raw knob values. Add formatters where the value is fractional
   tuning, an interval, time or gain; do not round tuning to whole semitones. SWSAW's extended pair has half the
   first pair's spread, contrary to the older NEXT_UPDATES description. Keep its DRFT control.
5. Add full sound snapshot/restore for preset audition, cancel, load, edit detection and save. A sound must include
   its AMP/filter/FX/LFO values, not its pattern steps. Retain separate sound VOL and mixer LEVEL.
6. Preserve stereo side signals through machines, filters, routing and sends. Add ensemble/noise/chorus width only
   when the complete stereo path exists. Reuse per-track effect memory where lifetimes are mutually exclusive.
7. Add recovered STRNG/PIPE/CHORD/MODAL FX, tape, adaptive crunch, noise/dust and alternate reverb. Retain existing
   PERFORM, ROOM/SPRING, slicer and FX families. Improve RING and dynamics where evidence supports it.
8. Complete trig lanes, timing/arp, performance and expanded storage from NEXT_UPDATES. The current P_NSTORED equals
   P_COUNT: step.rsv is free today; its legacy extra-parameter helper currently stores zero entries.
9. Build each coherent release on Ubuntu, run host/render/simulator checks, verify package hashes, then run hardware
   benchmarks and listening checks before calling a release device-tested. No UI replacement or loader changes.

## Storage: approved; owner waived backup

The existing data map is fully allocated, even when sectors contain erased bytes:

| Region (end exclusive) | Existing owner |
| --- | --- |
| 0x97000..0x9F000 | Four project A/B pairs |
| 0x9F000..0xA0000 | Reserved; the inspected dump contains data here |
| 0xA0000..0xB4000 | User sample slot 1 |
| 0xB4000..0xC8000 | User sample slot 2 |
| 0xC8000..0xDC000 | User sample slot 3 |
| 0xDC000..0xE0000 | MIDI learn and legacy user presets |

Read-only dump inspected: fm1-transporter-dump-20261026-171248.bin, 1048576 bytes,
SHA-256 7f6ccb4f0c96a04946c1a456f5d6d0bd24728a18c86e23a7814ef50e39486da3.
It contains non-erased sectors at 0x9F000, 0xB8000 and 0xC8000. Do not treat these as free space.

Proposed first complete expanded store: retain sample slot 1; reclaim sample slots 2 and 3 for eight patterns,
eight independent kits, 128 full user sounds and a committed catalogue. Keep the old project/MIDI/user-preset
regions intact during migration. Candidate sector budget in the reclaimed 160 KiB:

- Eight patterns, each one A/B sector pair: 64 KiB. Each packed pattern must fit 3840 bytes including locks.
- Eight kits, two kits per A/B object: 32 KiB. Maximum packed kit 1920 bytes, less shared header overhead.
- 128 full sounds, four A/B objects: 32 KiB. Fixed sound record must fit 120 bytes, less per-object header overhead.
- Catalogue/migration record, one A/B pair: 8 KiB.
- Unassigned reserve: 24 KiB. This is not permission for an additional feature.

The original approval required a verified backup. The owner subsequently waived it explicitly in chat
("nie potrzebuje backupu") and specified installation from the Mac, with compilation on Ubuntu.
Reclaiming sample slots 2 and 3 is authorized without a new backup; the owner was told their contents will be lost.
Still require an explicit storage migration action. Never migrate destructively at boot. A rollback restores those backed-up
sectors before sample slots 2 and 3 are re-enabled by an older firmware.

Use the existing header-last CRC-protected A/B mechanism. The catalogue must reference exact object generations;
an uncommitted save cannot replace the only generation the current catalogue names. Test every power-cut boundary,
including an interrupted save followed by another save, shared kit changes, wraparound, corrupt headers/payloads and
full storage. No filesystem, bootloader changes or writes outside the approved data regions.

Approval is for reclaiming the two sample slots and this capacity target, not a claim that the format is implemented.
The owner approved this layout in chat: "Tak, po zweryfikowanym backupie", then waived the backup requirement.
The standalone storage API retains its explicit destructive-action gate; the production bridge must distinguish
verified backup from the owner's explicit waiver, without falsely recording a backup as verified.

## Build verification and outstanding hardware checks

The Ubuntu 0.7.0 candidate links without runtime 64-bit division. Its measured image is 524112 bytes,
.data + .bss is 82816 / 98304 bytes, and pool usage is 333220 / 344064 bytes. These figures include coefficient caching and CPU1 stacks, and precede expanded-library integration; repeat the target build after either change.

The multiply scheduling check compares exact normalized instruction windows against v0.6.1 (6695245).
The baseline file records the disassembly SHA-256 and 299 inherited warnings; the candidate has 289 inherited
windows and zero newly introduced warnings. This is a regression check, not proof that the inherited warnings
are harmless. Hardware listening and stress tests remain necessary.

Host instruction measurements reveal that six calibrated tracks with heavy inserts, LFOs and delay tails
exceed the existing CPU budget. The donor distributes work across two cores; the initial port used the
existing single audio core. Do not raise test thresholds or advertise unrestricted six-track calibrated
polyphony on the strength of the donor's performance. Optimize and measure, then establish the supported
resource limit before treating the candidate as ready for normal use.

The candidate now reuses the donor's CPU1 worker protocol for independent calibrated synth rendering, with
8 KiB of dedicated stacks in BSS. FX, SID and the track immediately before SID retain serial rendering;
bus routing, compressor, inserts and delays keep their original ordering. The worker joins before mixing
or restoring controls. A failed startup leaves the serial path; a runtime timeout resets instead of
replaying partially mutated DSP state. Host ASan/UBSan and ThreadSanitizer tests cover 4000 concurrent
jobs and 4000 refused submissions with identical serial/threaded audio and state. This does not replace
device timing, stack watermark, reset/update and listening checks.

Stereo delay uses the existing 16 KiB per-track allocation: its normal full-bandwidth interval is shorter
than the donor's mono mode. Existing stutter capture remains mono. These are limits of this integration,
not evidence of differences in the reference instrument.
