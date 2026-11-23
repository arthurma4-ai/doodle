# Finger / Euclidean v0.7.0 verification

Identity preserved: **aumu / FgSq / ArAu**,
`com.arthuraudio.fingersequencer`, native arm64.

Installed AU:
`/Users/arthur/Library/Audio/Plug-Ins/Components/Finger Sequencer.component`

Build AU:
`/Users/arthur/Finger sequencer/build/FingerSequencer_artefacts/Release/AU/Finger Sequencer.component`

## Executed outside Logic

- Release AU build; SamplerTests, SequencerTests, PlaybackTests: PASS.
- Three editor pages rendered and visually inspected. Speed controls are to the
  left of the maker circle; colored row dots and Steps/Hits/Rotation are to its
  right; the vault is on the far right. The front page shows assigned sound rings
  and the memory selector. Sounds starts with seven sample cards.
- UI callbacks: all seven cards audition; WAV drops replace samples; waveform
  markers trim correctly. Draft dot edits leave assigned keys untouched. Dragging
  the maker onto a key assigns its complete pattern. Vault storage and vault-to-
  key dragging preserve the stored copy. Actual speed/steps/hits/rotation drag
  targets are exercised, with speed and rotation surviving DAW-state recall.
- Euclidean generator: every Steps value 1–32 and every legal Hits value tested
  for exact hit count and bounds. Rotation wraps around the lane length.
- Timing: speed 1 / 8 steps wraps in one 4/4 bar; half a bar reaches the opposite
  side. Speed 0.5 takes two bars. Speed 1 also follows a 3/4 host bar. Step 1
  launches at phase zero / 12 o'clock. Independent lane speed uses actual host
  time-signature bar length; active hand phase is published by playback.
- Growable library: 18 samples loaded with no seven-slot limit. A pattern uses
  sample 18 alone; actual rendered audio matches the expected source sample.
  A 100-lane pattern schedules lane 100 through the real-time hit callback.
  Playback polyphony remains 128 voices, distinct from library size.
- Memory banks: new banks contain zero samples and an empty vault. Switching
  stops active performance; switching back restores samples, draft, assigned
  patterns and vault. Session state restores multiple banks and active selection.
- Portable folder export/import: a memory with 18 samples exports WAV copies and
  `bank.xml`; re-import resolves relative paths to existing copied WAVs and
  restores the vault. Production editor export/import uses a background thread.
- Step Record: release-to-advance chord entry now targets the separate maker
  draft. Partial release waits; final release advances once. Assigned keys stay
  unchanged until assignment. Skip leaves a rest. Bank changes disarm entry.
- Existing timing and sampler regressions pass: MIDI offset triggering, overlap,
  trim, varispeed, independent pitch/duration stretch, relinking, three-way/all-
  eleven Join with 77 hits, Replace cancellation, Fill In phase restoration,
  fast taps, release order, loop wrap, stopped tempo preview and host stop.
- Required legacy fill boundary remains valid: after twelve sixteenths, a 16-step
  kick plays step 13 and a 12-step hat plays step 1, with actual mixed audio and
  matching playheads. Legacy sequences keep their old timing semantics.
- Actual state-v1/v2 migration fixtures pass. Constructed state-v3 coverage checks
  per-lane length 12, step 32, sample trim/tune/stretch, explicit empty rows and
  learned MIDI base. All migrate into Main Memory. Version-4 recall retains
  multiple banks, dynamic libraries, draft macro values, key patterns and vault.
- Installed-AU smoke test and Apple auval results are recorded after installation
  below. No Logic UI success is inferred from simulated-host tests.

## Installed AU

Strict codesign verification, AudioToolbox smoke test (seven factory samples,
factory A from MIDI 36/60, ClassInfo kit restore, all seven sound triggers, offsets
and tail silence), and `auval -v aumu FgSq ArAu`: PASS.

## Logic acceptance still pending

Following the project review, the user confirmed that the sound engine now works.
This resolves the earlier user-reported sound-engine failure. It does not establish
that every Logic acceptance case below has passed; no additional agent-observed
Logic tests accompanied this confirmation.

No Logic GUI/project checks were performed for v0.7.0. Earlier native UI automation
could not initialize. Automated editor callbacks, simulated host rendering and
installed-AU AudioToolbox tests are explicitly outside Logic.

1. Save/restart Logic and confirm 0.7.0. Select/record-enable the instrument track.
   Audition the seven starter sounds while stopped and while transport plays.
2. Make an 8-step/3-hit rhythm, rotate it, set speed 1 then 0.5, and confirm one-
   and two-bar loops. Add more sound lanes, direct-edit dots and scroll the rows.
3. Drag the maker onto A; edit the draft and confirm A stays unchanged. Store in
   the vault, drag a vault circle onto S, double-click it to reload the maker.
4. On Performance, hold A/S/D in Join and check colored rings and phase. Release
   in different orders, then test Replace/Fill In, loop wrap and host stop.
5. Create an empty memory; add more than seven samples and assign extra lanes.
   Switch banks and verify that kits, key sequences and vaults stay independent.
6. Save the Logic project with multiple banks; reopen idle and verify all data.
   Save Memory to a dedicated folder; move that folder and Open its bank.xml.
7. Step Record W+E at step 1, release both, Skip step 2, then overlap T and Y at
   step 3. Drag the draft to a key, play it, and verify save/reopen. Check short-
   lane/final-step bounds and cancelled late key-ups after cursor/bank changes.
