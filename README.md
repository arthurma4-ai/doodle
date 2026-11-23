# Finger / Euclidean v0.7.0

macOS AU instrument for Logic Pro. Identity unchanged: **aumu / FgSq / ArAu**,
bundle `com.arthuraudio.fingersequencer`. Native Apple Silicon build.

Installed AU:
`/Users/arthur/Library/Audio/Plug-Ins/Components/Finger Sequencer.component`

Save and restart Logic after updating; confirm **0.7.0** in the editor header.
On a selected/record-enabled software-instrument track choose **AU Instruments >
Arthur Audio > Finger Sequencer > Stereo**. Command-K opens Logic Musical Typing;
external MIDI works too, including with the editor closed.

## Three pages

**Performance** is the front page: one display of the assigned patterns' sound
rings. Inactive rings are dim; a playing key lights its rings in each sound's
color. The hand starts at 12 o'clock when a pattern launches. The key list shows
playing/queued state. Select a memory bank or add an empty one with **+ Memory**.

**Euclidean Sequencer** is a separate sequence maker. Its draft is not a live key
assignment: changing it never overwrites a key until you drag it there.

**Sounds** contains the current bank's sample library and waveform editor.
Main Memory starts with seven samples; there is no fixed seven-sample limit.
Libraries and pattern lanes grow with available memory. Playback has 128 voices;
it is not an unlimited simultaneous-voice engine.

## Make and assign a Euclidean sequence

1. Open **Euclidean Sequencer**. **New sequence** clears the maker draft.
2. **+ Sound lane** chooses a sample from this bank. Each sound adds a colored
   ring and a row. Click a row's sound name to audition it. Right-click the name
   to remove that lane from the draft; the sample remains in the bank.
3. Drag each numeric macro vertically:
   - **Speed**, at the left of the circle: 0.125–8x, in 0.125 increments.
   - **Steps**, at the right of its dot row: 1–32.
   - **Hits**: how many evenly distributed steps are filled, from zero to Steps.
   - **Rot**: rotate the generated rhythm around its ring.
4. Each row has exactly its selected number of dots. The first dot is **step 1**,
   at **12 o'clock** on its ring. Click or drag dots to paint; start on a filled
   dot or right-drag to erase. Direct edits can depart from the Euclidean rhythm;
   changing Steps/Hits/Rotation generates a fresh Euclidean distribution.
5. **Speed 1** is one rotation per host bar; **0.5** is one rotation per two bars.
   The number of dots changes subdivision, not the bar duration. Rows have their
   own speeds, lengths and colors. Host time signatures, including 3/4, determine
   the bar length. Scroll the row list for more sounds.
6. Drag the **large circle** onto one of the eleven key boxes at the top. This
   replaces that key's complete sequence, including its rows, timing and rotation.
   It does not replace the bank's samples.

Click key boxes to toggle mouse playback; right-click a key box to copy its
assigned sequence back into the maker. Editing that copy remains independent.
MIDI keys are held to play and released to stop new hits on the next sixteenth.
Closing the editor releases mouse launches but does not interrupt held MIDI.

## Pattern vault

The right-hand vault stores draft sequences within this bank. Click **Store in
vault**, or drag the large circle onto the vault. Each stored pattern appears as
a small multi-ring circle. Drag it onto a key to assign it, or onto the large
maker to edit a copy. Double-click also loads it into the maker. Scroll the vault
for more patterns. Stored patterns keep their original data when the draft is
subsequently edited. Key assignments, vault entries and drafts refer to this
bank's shared sample indices.

## Step Record and Skip

Step Record now enters the **maker draft**, not an assigned key. Arm it to begin
at step 1. Sound MIDI keys W/E/T/Y/U/O/P audition and enable hits at the cursor.
Overlapping presses form one chord. The cursor advances once the **last held sound
key is released**, even across MIDI channels and audio blocks. Repeated note-ons
leave hits enabled; velocity-zero note-on is a release.

**Skip** clears the current in-range step and advances, leaving a rest. At the
longest enabled lane's final step, entry stops advancing; it never wraps. Past a
shorter lane's end, its sound auditions without writing or wrapping. Pattern keys
are not part of the chord. Draft/cursor changes, Skip, rearming, bank switches,
panic and host stop cancel a stale chord's pending advance. The dot cursor and
step readout are visible while armed. Drag the finished draft onto a key to use
it in performance. Reopen is idle with Step Record off.

## Samples and memory banks

Main Memory starts with Kick, Clap, Snare, Closed Hat, Open Hat, **Shaker / O** and
**Tom / P**. Kick, clap, closed hat, shaker and tom use the supplied WAVs; snare and
open hat remain synthetic placeholders. Factory audio is embedded, independent
of Downloads. The initial A beat and S/D/F/G/H individual parts are retained;
J/K/L/;/' start empty.

On **Sounds**, **+ Sample** adds a mono/stereo WAV. Dropping multiple WAVs adds
multiple samples. Drop a single WAV on an existing card, double-click it, or use
**Load / Relink WAV...** to replace/relink it. Click cards to audition. Scroll to
browse additional cards. The first seven retain W/E/T/Y/U/O/P MIDI audition;
additional sounds are auditioned by mouse and can have their own sequence lanes.

Start/End markers trim non-destructively. Tune is ±24 semitones. Stretch off uses
varispeed; Stretch on preserves selected pitch independently of duration: 100%
is original trimmed duration, 200% doubles it. Ratio range 50–400%. WAV limit
remains 30 seconds / 192 kHz. Wait for **Ready** after changes. Loading and stretch
preprocessing run on the sample worker, not the audio thread. Missing files remain
visible; relinking retains trim, Tune and Stretch settings.

Each **memory bank** contains its sample references/settings, eleven key
assignments, maker draft and pattern vault. **+ Memory** creates a bank with no
samples and no patterns. Switching banks stops the current performance and
loads the selected bank; it never merges kits or continues old patterns.
Existing custom banks stay available in the selector.

All banks are saved with the DAW session. For a portable disk folder, **Save
Memory...** chooses a dedicated folder and writes:

- `bank.xml`: sample settings, key sequences, draft, vault, lengths, speeds and
  rotation data;
- `Samples/`: copies of the bank's WAVs, including embedded factory sounds.

**Open Memory...** selects that folder's `bank.xml`, resolves its sample paths
relative to the folder, and adds it as a new bank. Export/import file work runs
on a background thread. Save a dedicated memory folder to move or share a kit;
ordinary DAW session recall keeps external user-WAV references.

## MIDI performance

| Pattern | A | S | D | F | G | H | J | K | L | ; | ' |
|---|---|---|---|---|---|---|---|---|---|---|---|
| Musical Typing | 60 | 62 | 64 | 65 | 67 | 69 | 71 | 72 | 74 | 76 | 77 |
| C1-based row | 36 | 38 | 40 | 41 | 43 | 45 | 47 | 48 | 50 | 52 | 53 |

| Sound | W | E | T | Y | U | O | P |
|---|---|---|---|---|---|---|---|
| Musical Typing | 61 | 63 | 66 | 68 | 70 | 73 | 75 |
| C1-based row | 37 | 39 | 42 | 44 | 46 | 49 | 51 |

Both rows work with the default A=60 mapping. Releases are tracked separately.
Previously learned MIDI bases are preserved in old projects; those projects use
their saved row. Octave labels vary by DAW; numeric MIDI notes are authoritative.
The AU does not intercept raw letter shortcuts from Logic.

**Join** layers every held/toggled pattern, up to all eleven. **Replace** queues
the newest other pattern until primary release; releasing its queued key first
cancels it. **Fill In** temporarily substitutes the newest other pattern, then
returns to the primary at its continuing phase. Handoffs use the next sixteenth;
MIDI event offsets are honored. Host stop clears all performance state. Fresh
presses while stopped can preview at project tempo if Logic is rendering the
instrument track. Toggle mouse patterns off, release MIDI, stop the host, send
CC120/123, or use Esc when delivered to the editor to stop.

## Compatibility and implementation

State version 4 stores multiple memory banks. Earlier 12-pad, six-slot/eight-
pattern and seven-slot/eleven-pattern sessions migrate into Main Memory. Their
sample settings, references, MIDI base, steps, swing and independent lengths are
retained. Legacy lanes keep their original sixteenth-note timing; newly made
Euclidean drafts use the new bar/speed definition. Empty legacy O/P slots gain
shaker/tom; custom samples are retained. Old Hold latches remain disabled.

[Editor](Source/PluginEditor.cpp), [layout](Source/FingerLayout.h),
[Euclidean generator](Source/Euclidean.h), [memory format](Source/MemoryBank.cpp),
[sequencer](Source/SequencerEngine.cpp) and [sampler](Source/SamplerEngine.cpp)
remain separate. Sample/lane growth happens on the control thread with stable
objects published to audio; playback does not allocate growing lane lists or
perform WAV/folder I/O. Old directory snapshots are retained until plug-in teardown
so audio never frees them. Large libraries therefore consume RAM, as do loaded
sample buffers.

Build output:
`build/FingerSequencer_artefacts/Release/AU/Finger Sequencer.component`

```sh
/Applications/CMake.app/Contents/bin/cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64
/Applications/CMake.app/Contents/bin/cmake --build build --target FingerSequencer_AU SamplerTests SequencerTests PlaybackTests -j 6
/Applications/CMake.app/Contents/bin/ctest --test-dir build --output-on-failure
```

Reuses existing JUCE 9.0.5. [VERIFICATION.md](VERIFICATION.md) distinguishes executed
checks from Logic GUI tests still pending.
