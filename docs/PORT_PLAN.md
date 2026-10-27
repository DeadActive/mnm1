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

## Expanded library integration (0.8 candidate)

The eight-pattern/eight-kit/128-sound library is connected to the existing controls. KIT adds
slot/name/load/save; PROJECT shows A–H in two groups of four. Saves require stopped transport.
A pattern save commits its selected kit and pattern together while retaining the kit's name.
SONG caches referenced steps/motion before PLAY, reusing the legacy RAM slots for six patterns
and 5000 bytes for the remaining two. The old flash records remain available for migration.

USB console `library` reports readiness; `library migrate-waived` performs the owner's authorized
migration without asserting that a backup exists. Boot never formats or migrates automatically.
Sample slots 2 and 3 are disabled before their payload can be replaced. Slot 1 is preserved.

The target build measured image 538588 bytes, data/BSS 95328 of 98304 bytes and pool 333220
of 344064 bytes before the final name-preservation change. The full host suite passed, as did
ASan/UBSan tests for KIT H, patterns E–H, last-slot reboot, model-limit refusal, read failures,
and cached SONG transitions with zero flash reads. Screen layout was inspected. Final candidate
build and device timing/listening are separate checks; the remaining sequencer/performance
roadmap is not complete.

## Trig lanes (0.9 candidate)

STEP knob 2 chooses ALL, AMP, FILTER or LFO. Taps edit the selected lane. Hold a step
and turn SELECT through ALL, AMP, FILTER, LFO, TRIGLESS, NOTE OFF, PITCHLESS and LOCKS.
The four held-step knobs retain probability, ratchet, accent and parameter slide.
Zero event flags preserve existing playback; typed events store their ratchet flags at
step start so later edits do not change an already running roll.

MNM5 retains the old project size but stores known event flags. MNM3/MNM4 readers clear
historical spare bytes. Expanded patterns use version two; version-one reads normalize
them to ALL before playback. Kit/sound versions are unchanged. Independent source
NOAMP events preserve the current gate even during slides; this deliberately avoids
the donor's slide shortcut reopening the gate. Pitchless events retain the actual last
sounding note when an ARP has restored its parent note in the sequencer state.

Focused source differential, DSP lane, UI and format-migration tests pass. The early
native build used 541088 image bytes and 95328 data/BSS bytes. The final target
build and full host regression passed. Timing/ARP extensions, performance assignment and expanded
SONG rows remain outstanding. The 0.8 package is on the Mac with SHA-256
`2c952b1fad5e664e1ebb9a3e71eef74eff8a43d54c3890640ec1f3ae98827398`;
0.8 was installed from the Mac. Its console confirms CPU1 online with no timeouts;
the clean hardware benchmark stopped after case 183 (cal-gsin6-full), followed by a reboot.
The device still answers. Heavy six-track calibrated workloads exceed the audio deadline;
this is not a device-stable release claim. Crash count 1 reports PC 0x01C0C268, RETS
0x01C0C278, vector 1, EMU 2. Its TIMER4-based timestamp wraps, so freshness is not yet
known. An exact 0.8 rebuild reproduced the package SHA for address mapping. The host
benchmark now saves incoming lines incrementally, retaining partial logs on timeout.
The 0.9 package is also on the Mac, SHA-256
`14c3e5ca6f58b5bd2ee202058451bc8c95feb2fcc3ec292c261794918a0c8927`.

## Device diagnostics and 0.10 candidate

Diagnostic-only packages 0.8.1 and 0.8.2 were installed from the Mac with owner
permission. They are not releases: both select the final four full calibrated
stress cases; 0.8.1 adds status/crash reporting, and 0.8.2 also enables normal
voice shedding and continuous rendering without benchmark rest halves.

All four completed without reboot or an increase in crash count. CPU1 executed
13760 jobs in 0.8.1, with maximum job 1281 us, maximum wait 8 us and at least
2364 bytes free on its user stack (supervisor stack untouched). Six full-effect
tracks required roughly 13–16.5 ms per 5.804 ms audio half. With voice shedding,
0.8.2 settled near 6.9 ms: delay tails and modulation still exceed the deadline.
The reported shed count is attempts, not proof that a voice existed each time.
Logs are in build/bench/0.8.0/diag1.log and diag2.log. The earlier full-run reboot
has not been reproduced; its cause remains unresolved. No sample migration ran.

0.10 adds 33 bytes of per-track sequencer options and four pattern bytes. MNM6
stores descriptor-aware sound parameters as bytes, leaving room for these options
within the legacy flash payload. Expanded pattern v3 is 2906 bytes; old v1/v2
records keep their original 2704-byte reader. New UI pages preserve the existing
families and controls. Initial masked swing, fractional periods, repeated SONG
rows, held/latching/accumulating ARP, trigger switches and transpose have focused
host sanitizer tests. Full regression, target build and device proof are pending.

The exact LFO-control cache optimization preserves 20000 differential blocks and
2000 randomized cache/lookup cases. Static RAM is unchanged; it adds a 144-byte
local table. Host instruction cost of LFO preparation fell from 1261 to 256 per
sample. Diagnostic 0.10.1 repeated the continuous-render/normal-shedding test:
all four cases completed, with six voice cuts per case, roughly 5.08 ms average
and 5.178 ms worst after warmup, zero measured late halves. Initial overload still
reached 17.427 ms before shedding; all six held voices were cut in these extreme
cases. This proves recovery and tail budget, not six-track unrestricted playback.
CPU1 completed 13840 jobs, maximum job/wait 1295/505 us, no timeout, minimum user
stack headroom 2276 bytes. Boot count and crash record stayed unchanged. Full log:
build/bench/0.8.0/diag3-optimized.log.

The target 0.10 candidate builds within 97440/98304 bytes data/BSS and
333252/344064 bytes pool. Trigger setup for two legacy percussion machines is
isolated from unsafe wide-product scheduling; numeric fingerprints are unchanged.
The target checker reports 774 wide products, 275 inherited warnings and zero new
warnings. Inherited warnings remain an existing limitation, not a safety proof.

The 0.10 host regression is complete: initial groups in /tmp/mnm010-suite3.log,
updated UI fixtures in /tmp/mnm010-ui-final.log, remaining groups in
/tmp/mnm010-suite-tail.log and /tmp/mnm010-suite-rest.log, and the final boot/fidelity
checks in /tmp/mnm010-suite-last.log (ALL DRUM HOST TESTS PASSED). Fixture updates
account for compact byte parameters and twelve division choices; the UI page
coverage mask was widened to 64 bits to avoid an invalid shift on added pages.
The simulator build and glue tests pass. Standard target image is 545432 bytes;
Mac package SHA-256 is 3705bafb2a0938a92d52b0bcdcc6d94e4dcce0966a79eb97e7f11f6bd622e44b.
It is installed on FM-1. The 187-case hardware run stopped after case 136 (137 results);
50 cases remain untested in this run. The device rebooted (boots 7 to 8, crash count 1 to 2).
The new exception has vector 1, reported PC 0x0000070C, RETS 0x01C155EC, DBG 0x00080000,
SP 0x01C7BC68. The owner subsequently confirmed manually resetting this run. Keep these diagnostic values as raw
evidence, but do not classify this interruption as an unexplained firmware failure.
Evidence: build/bench/0.10.0/device.log, after-timeout.txt and crash-raw.txt.
The 0.10 site, matching simulator/source archive and verified package were published
from source commit 626a936 (Pages commit 968b8a6), explicitly marked beta. 0.11 and 1.0 remain.


The remaining 50 cases (137–186) completed in diagnostic build 0.10.2 without a
new crash or reboot. CPU1 completed 89,440 jobs, max job 1,272 us, max wait 8 us,
zero timeouts, 2,276 bytes of user-stack headroom. This is a segmented measurement,
not one uninterrupted regression. The owner confirmed that the earlier reset was manual. The diagnostic only changes the benchmark start and crash capture;
its SHA-256 is f56456e7f4bb31cf6376be0a83e3096b96ae92c29d63df1e36dcfba74c730471.
Evidence: build/bench/0.10.0/tail-diag.log and segmented-report.md.
Expanded crash-frame fields in this run are invalid: the retained record predates
the expanded layout; crash count did not change during the diagnostic run.

0.11 implementation: MNM7 projects (3520 bytes), version-two full sounds
(119 bytes) and kits preserve old readers and new performance/MULTI settings.
Migration, record round trips and interrupted saves pass host sanitizer tests.
The mixer level meter now has an empty outlined background and solid level fill;
its peak-marker partial redraw is tested. These changes are not yet in the public
0.10 firmware.

The 0.11 target candidate passes link and instruction checks: image 554524 bytes,
data/BSS 96736 of 98304, pool 335264 of 344064. RING uses isolated rounded products;
the final scan reports 770 wide multiplies, 272 inherited hazards and no new ones.
Package SHA-256: 33d39148970eeb7586a23b3fd8c661b9d7ddd9cb0f3287f7c2b43bd07511e4d3.
This scan checks instruction scheduling; it is not a hardware stability proof.
Performance, controller/lock/LFO composition, note ownership, filter tracking,
project conversion and transactional record tests pass with host sanitizers.

The complete 0.11 host regression passed in segments (suite-final, rest, ui-rest logs in /tmp);
updated navigation fixtures include all nine legacy EDIT pages. The final RING cost and
MULTI integration checks pass, as do the matching host/WASM simulator and site tests.
The fixture-only screenshot command was rerun separately with Python and theme_sheet.py.


### 1.0.0 RC1 — installed, 2026-10-26

SONG ranges/mutes/tempo/nested jumps, AFTER HOURS demo and confirmed NEW are
implemented, with MNM8 / pattern-v4 readers preserving older records. Host
regressions passed in segmented runs, including the final UI, boot, migration,
save roundtrip and demo traversal checks. Target image: 564232 bytes;
data+BSS: 96896 bytes; pool: 335264 bytes. Wide-multiply check: 770 products,
272 inherited hazards, no new hazards. Package SHA and physical save/reboot
results are in RELEASE_1_0_TESTS.md. Device confirmed MNM1-1.0.0; bank-A copies
both committed and survived reboot, bank B unchanged. Earlier owner SAVE ERROR
not reproduced; no physical root-cause claim. Public release remains 0.11.0
pending the remaining device checks. No expanded-library migration yet.


### 1.0.1 — dedicated hub identity, 2026-10-27

The Firmware Hub checker reported `FM-1_61000100` free. This is MNM-1's stable
package/app identity, independent of the ABOUT/console version. The generic
update loader remains byte-identical to its pinned binary. No DSP, UI or storage
format changed. Target image: 564244 bytes; data+BSS 96896; pool 335264;
wide-multiply check unchanged (770 / 272 inherited / no new hazards).
Package SHA-256: `d44011bf035314c037e16ab36a8e7895d9274732329e1d60e0bd94bd512eabf9`.
The app contains the new identity and `MNM1-1.0.1`; both package readers agree.
Host tests cover the long identity, upgrading from the legacy identity and
leaving it again, OTA staging, and loader flash writes. A 240 × 240 screen
capture renders the existing AFTER HOURS SONG page in the firmware host harness.
The FM-1 was not connected for 1.0.1: no new physical install/USB identity test
is claimed. The preceding 1.0 hardware evidence remains documented separately.


### 1.0.1 — physical validation, 2026-10-27

The owner connected the FM-1 after publication. The exact release package
(SHA-256 `d44011bf035314c037e16ab36a8e7895d9274732329e1d60e0bd94bd512eabf9`)
installed successfully from 1.0.0. The running device replied `FM-1_61000100`;
CDC reported `MNM1-1.0.1`, with the owner's project at 132 BPM / track 4 FM+.
All four legacy project banks and settings were byte-identical after upgrading.
Two bank-A saves committed valid header/payload CRCs (sequences 13 and 14), and
both copies survived an explicit reboot. Banks B, C and D were unchanged.
Sixteen MIDI note-on/off pairs were received; no reboot, voice shedding or
core1 timeout occurred. This is a MIDI/render smoke check, not an audio audition.

Physical rollback 1.0.1 → 1.0.0 and re-upgrade 1.0.0 → 1.0.1 both completed.
All four banks matched the post-save snapshot after that round trip. The device
was left stopped on 1.0.1 with the new identity and the same 132 BPM project.
Logs: `build/bench/1.0.1/` (local; project snapshots are not published).
No stock-V15 round trip, expanded-library migration or full stress benchmark
was performed in this session. The release binary and version tag are unchanged.
