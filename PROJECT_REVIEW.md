# Finger Skate project review

Review of the current Finger Sequencer / Finger Euclidean source, version 0.7.0. “Finger Skate” is the requested review name; the AU is still registered as **Finger Sequencer**. This review describes the implementation, not a proposed redesign. No playback code was changed for this review.

## Purpose and creative experience

This is a macOS Audio Unit instrument for building and performing layered sample rhythms inside Logic Pro. It is aimed at musicians who want to make rhythmic ideas quickly, assign them to musical keys, and combine or replace them by playing rather than programming a DAW arrangement.

The intended cycle is: choose samples, create a rhythm with circular Euclidean controls or direct step editing, drag the completed sequence onto a performance key, and play that key. A vault stores reusable patterns. Memory banks group samples, assigned sequences, the maker draft, and vault patterns. The experience should feel immediate and tactile: a small rhythm instrument played from MIDI, with the editor optional during performance.

The current interface has three pages: Performance, Euclidean Sequencer, and Sounds. It has replaced the earlier OP-1 screenshot-inspired artwork. It is a functioning prototype with significant automated coverage, but it has not completed acceptance testing in Logic.

## Architecture and signal flow

The project uses C++17, JUCE, CMake, JUCE’s AU wrapper, and Apple AudioToolbox for an installed-component test. The CMake configuration produces an AU music instrument, with MIDI input and mono or stereo audio output. There is no audio input or MIDI output. JUCE is reused from `/Users/arthur/Documents/JUCE`; that machine-specific path is a configurable CMake cache setting.

The processor owns the sampler, the performance sequencer, a second sequencer object used for draft Step Record, and memory-bank state. The editor owns presentation and interactions, not sample playback. Closing it does not remove the MIDI processing engine.

```text
Logic MIDI + host position/tempo
              |
        FingerProcessor
        /             \
pattern notes       sample notes
      |                  |----> draft Step Record
SequencerEngine          |
      | timed lane hits  |
      +----------> SamplerEngine voices ---> AU audio output
                         ^
             prepared immutable sample assets
                         ^
             WAV decode / tune / stretch worker
```

The audio callback reads host position, tempo, and time signature once per block. It visits each audio frame, consumes MIDI at its supplied sample offset, updates the sequencer, triggers samples, and mixes voices. Pattern changes are applied at sixteenth-note boundaries. While the host is stopped, a local clock provides audition at the last known project tempo. A host stop clears the performance and active voices; normal pattern release permits sample tails to finish.

Pattern keys use eleven white-key offsets from a base note, and the first seven sample keys use intervening black-key offsets. At the default base of 60, an additional mapping beginning at 36 is accepted. Velocity-zero note-ons are treated as releases. The A/S/D-style labels describe MIDI Musical Typing assignments; the editor does not capture raw performance letters.

Join supports all eleven patterns together. Replace waits for the primary release before handing over to a still-held queued pattern. Fill In keeps the primary anchor running silently and returns to its continuing phase. Legacy Hold machinery remains internally, although its button was removed.

The UI writes atomic settings and hit masks, and reads atomic performance snapshots on a 30 Hz timer. Sample edits update a mutex-protected control model; a worker decodes and renders the result, then publishes an asset pointer. Voices protect assets with hazard pointers so the worker can retire old buffers without deleting data that is still sounding. Memory-folder import/export uses an editor-owned background thread.

This avoids obvious disk reads, FFT processing, mutex acquisition, and buffer allocation in the normal audio callback. It does **not** establish that all control/state operations are coherently synchronized or that worst-case processing meets an audio deadline.

## Important files

| File or folder | Responsibility |
|---|---|
| [CMakeLists.txt](./CMakeLists.txt) | AU identity, JUCE dependency, embedded kit, build and three CTest targets. |
| [Source/PluginProcessor.h](./Source/PluginProcessor.h), [implementation](./Source/PluginProcessor.cpp) | Host integration, MIDI routing, clocks, memory ownership, session serialization and migration. |
| [Source/SequencerEngine.h](./Source/SequencerEngine.h), [implementation](./Source/SequencerEngine.cpp) | Pattern data, independent lane timing, performance gestures, quantization and grouped Step Record. |
| [Source/SamplerEngine.h](./Source/SamplerEngine.h), [implementation](./Source/SamplerEngine.cpp) | Sample settings, background preparation, publication, 128-voice playback and asset lifetime. |
| [Source/StretchRenderer.cpp](./Source/StretchRenderer.cpp) | Offline FFT phase-vocoder stretch. |
| [Source/Euclidean.h](./Source/Euclidean.h) | Generates rotated, evenly distributed hit masks. |
| [Source/MidiMapping.h](./Source/MidiMapping.h) | Eleven pattern offsets, seven audition offsets and base-note validation. |
| [Source/MemoryBank.h](./Source/MemoryBank.h), [implementation](./Source/MemoryBank.cpp) | Bank/vault model, XML and portable sample-folder import/export. |
| [Source/PluginEditor.h](./Source/PluginEditor.h), [implementation](./Source/PluginEditor.cpp) | Three pages, custom drawing, drag/drop, sample controls and file dialogs. |
| [Source/FingerLayout.h](./Source/FingerLayout.h) | Shared fixed rectangles for drawing and mouse hit testing. |
| [Source/DrumVoice.h](./Source/DrumVoice.h) | Leftover original synthesized drum implementation; not in the current audio path. |
| [Assets/Factory](./Assets/Factory/) | Seven embedded starter WAVs: kick, clap, snare, closed/open hats, shaker and tom. |
| [Tools/GenerateFactoryExtras.rb](./Tools/GenerateFactoryExtras.rb) | Generates supplemental factory drum assets. |
| [Tests/SamplerTests.cpp](./Tests/SamplerTests.cpp) | Sampler rendering, overlap, trim/tuning/stretch and related checks. |
| [Tests/SequencerTests.cpp](./Tests/SequencerTests.cpp) | Timing, launch/release modes, lane phase, Euclidean generation and step entry. |
| [Tests/PlaybackTests.cpp](./Tests/PlaybackTests.cpp) | Processor audio, MIDI offsets, state/migration, banks and editor interaction checks. |
| [Tests/AUSmokeTest.cpp](./Tests/AUSmokeTest.cpp) | Loads the installed AU through AudioToolbox and checks actual output/state. |
| [Tests/Fixtures](./Tests/Fixtures/) | Historical six-slot and twelve-pad state fixtures. |
| [README.md](./README.md), [VERIFICATION.md](./VERIFICATION.md) | Usage/build instructions and acceptance evidence, including outstanding Logic checks. |
| [build](./build/) | Generated binaries, test outputs, screenshots and AU bundle; not source. |

## Design decisions and tradeoffs

- **Playback separated from drawing.** Engine code can run without the editor. This is a useful foundation for replacing the interface. However, the editor directly accesses public engines and shares concrete pattern structures, so the boundary is not yet a small, stable application API.
- **Prepared sample buffers instead of live stretch.** Trimming, pitch resampling, and optional stretch happen before triggering. This reduces audio-thread work and lets existing voices finish with their old assets. The cost is preparation delay and substantial RAM use. Hits during preparation are dropped rather than queued.
- **One 32-bit mask per lane.** This is compact and makes atomic hit insertion straightforward. It deliberately limits a lane to 32 steps and currently represents on/off hits rather than velocity or probability per step.
- **Drafts are separate from assigned patterns.** Editing the maker does not silently change a performing key. Dragging explicitly commits a copy. This supports experimentation, but the distinction needs clearer visual explanation and a more discoverable way to load an assigned pattern back into the maker.
- **Two timing conventions are retained.** Legacy patterns use sixteenth-note lane lengths. New maker patterns use bar-relative speed: 1x is one host bar and 0.5x is two bars, with Steps determining subdivisions. This preserves old playback, but moving legacy material through the maker can change its timing interpretation.
- **Atomic lane fields and retained pointer directories.** These allow library growth without reallocating a directory that the audio thread is reading. Old directory versions remain alive until destruction. This is simple for readers, but it retains memory and does not make a whole-pattern replacement atomic.
- **Session XML stores references; folder export copies samples.** Sessions remain relatively small, while a separate bank export can be portable. Sessions alone are not self-contained for user WAVs.

These are observable implementation choices. The source does not document a formal evaluation of rejected alternatives such as a commercial stretch engine, immutable whole-state snapshots, or event-based rather than per-sample scheduling; claiming such alternatives were benchmarked would be speculation.

## Current state and evidence

The source supports a growable sample library starting with seven sounds, eleven assigned patterns, a maker draft, a vault, and multiple memory banks. WAV loading/relinking, mouse audition, overlapping hits, trim, tune, manual stretch, direct dots, Euclidean Steps/Hits/Rotation/Speed, and grouped Step Record are present. Release of the last sound key advances Step Record once; Skip clears the current step and advances. Only the first seven samples have the defined audition MIDI keys; additional sounds are mouse-auditionable and sequencable.

Sample playback has 128 voices with oldest-voice stealing. WAV input is limited to mono/stereo, 30 seconds, and 192 kHz. Tune covers ±24 semitones; stretch covers 50–400%. With stretch enabled, duration is based on the trimmed source rather than the tuned intermediate length. Audio-quality acceptance, especially for drum transients and stereo material, remains separate from numerical pitch/duration tests.

State version 4 saves banks, sample references/settings, draft, vault, assigned patterns, lane timing, swing and MIDI base. Existing tests cover migration from historical states and reopening idle with Step Record disarmed. There is no host-automatable parameter tree: controls are internal model settings saved in the session.

**Fresh check during this review:** the existing configured test executables passed all three CTest suites: SamplerTests, SequencerTests and PlaybackTests, in 1.26 seconds total. They were not rebuilt during this review.

**Previously recorded v0.7.0 checks:** release build, editor callback tests and screenshots, strict signing validation, installed AudioToolbox smoke test and `auval -v aumu FgSq ArAu` passed, according to [VERIFICATION.md](./VERIFICATION.md). These checks were not rerun here. The smoke test is not wired into the three CTest targets.

**User confirmation:** following this review, the user confirmed that the sound engine now works. The earlier sound-engine failure is therefore no longer listed as an unresolved user report. This is user-reported confirmation, not a new automated or agent-observed Logic test. Detailed MIDI routing, stopped audition, editor-closed performance, mode behavior and Logic save/reopen acceptance remain pending unless separately confirmed.

AU identity remains `aumu / FgSq / ArAu`, bundle ID `com.arthuraudio.fingersequencer`. The recorded installed component is `/Users/arthur/Library/Audio/Plug-Ins/Components/Finger Sequencer.component`; the build output is [Finger Sequencer.component](./build/FingerSequencer_artefacts/Release/AU/Finger%20Sequencer.component). The recorded build is native arm64, not a universal distribution build.

## Problems and weak spots

### Confirmed from source inspection

1. **Malformed modern state can invalidate the memory model.** `setStateInformation` clears existing memories before checking whether the new XML contains a usable bank. A version-4 document with no Memory children returns with the collection empty. Later draft/current-memory access indexes it without a fallback. Parsing into temporary validated state before committing would avoid this path.
2. **Whole-pattern and bank updates are not coherent transactions.** Pattern bits, lengths, speeds and timing mode are published separately. Playback can observe a mixture during replacement. Bank switching similarly changes sampler and sequencer state in separate operations. Atomic individual fields prevent certain data races; they do not guarantee a musically coherent snapshot.
3. **Directory growth has no common writer lock.** Sequencer lane-owner/version vectors are mutated by control methods without internal serialization. Some processor methods use a memory mutex, but editor drag assignment calls `sequencer.setPattern` directly. Concurrent host restore and UI growth therefore have no consistently enforced writer boundary. A crash under that interleaving was not reproduced in this review.
4. **The audio workload grows with library size.** Every audio frame scans all lanes of every active pattern, including empty lanes, computes phases and swing timing, and writes playhead atomics. Rendering also scans 128 voices per frame. The 100-lane callback test proves delivery, not acceptable worst-case CPU or deadline behavior.
5. **Display work is unnecessarily expensive.** The editor refreshes at 30 Hz, repeatedly allocates/copies pattern snapshots, and the vault paint path calls `currentMemory`, which captures live state and copies a complete bank. Waveform painting rescans source audio instead of using cached peaks. Large banks will make this worse.
6. **Library growth retains allocation history.** Old directory lists remain until destruction and allocated slots/lanes do not shrink with smaller banks. Large temporary banks leave a high-water footprint. Unlimited sample count is not unlimited capacity in practice.
7. **Audition requests are mailboxes, not events.** Each slot has one atomic velocity request. Multiple UI requests before the next audio block coalesce into one. A pending sample revision also blocks new hits until rendering finishes. Neither behavior has a clear performance-facing explanation.
8. **Export is not transactional.** Sample files are written before the final manifest, so a failed export can leave a partially updated destination. The editor joins its file worker on close and before another operation; a slow copy can freeze the UI at those moments.
9. **Mixing has no final headroom management.** Each voice is scaled, but many loud overlapping voices can still exceed full scale. There is no master level, meter-driven clipping indication or limiter.
10. **The new editor is incomplete as a product interface.** It is fixed at 1310×850, uses many custom hit regions, and has no clear undo model. Pattern buttons toggle mouse-held playback rather than behaving like MIDI press/release. Maker hands animate without the draft itself playing. Vault/bank naming, deletion and sample organization are limited. Swing remains in the engine/state but has no visible control in this editor. Escape requests Stop, but keyboard focus is deliberately disabled, so it is not a dependable visible stop action.
11. **Old and new concepts coexist.** Learn, Hold, earlier entry APIs, diagnostics and the unused drum voice remain. Much implementation is compressed into long single-line methods. This makes threading and migration changes harder to review than necessary.

### Risks needing targeted measurement or tests

The callback looks allocation-free in its ordinary path, but lock-free assumptions and worst-case timing have not been established by a real-time checker or stress benchmark. Concurrent state/UI changes need sanitizer testing. Missing MIDI note-offs can keep a held pattern active until a host stop, panic or explicit release; there is no reliable visible recovery control on the current interface. Time-signature changes alter bar-relative timing factors and need musical phase testing. The simple phase vocoder needs listening tests for transient smearing and stereo image behavior; successful frequency/duration checks do not establish sound quality. Existing functional tests do not substitute for long-running host stress or crash recovery tests.

## Recommended next work, in order

1. **Run a reproducible Logic acceptance session first.** Confirm the loaded version and MIDI numbers, capture actual audio from mouse audition and both MIDI rows, test editor-closed performance, stopped preview, all modes, transport stop and project recall. Record failures with exact steps rather than expanding features.
2. **Harden state and control ownership.** Validate into temporary models, retain the last good state on failure, serialize control writers and publish coherent pattern/bank changes at an audio-safe boundary. Add malformed-state and concurrent restore/edit regressions.
3. **Measure and bound real-time work.** Benchmark small buffers with eleven patterns and large libraries; schedule actual step events rather than recomputing every lane per sample. Publish UI playheads at a lower rate and exclude inactive lanes from timing work.
4. **Clarify the core interaction.** Clearly label draft versus assigned sequence, make loading and assignment discoverable, distinguish queued/held/playing states everywhere, and provide reliable recovery. Show why a sample is silent or preparing. Resolve legacy versus bar-relative timing explicitly.
5. **Make saving resilient.** Export to a temporary destination and commit only when complete. Add cancellation/progress and avoid joining long file operations on the message thread. Consider stable sample IDs before allowing deletion/reordering.
6. **Improve maintainability and audio quality.** Reformat and split large methods, remove obsolete APIs after migration review, cache waveform/UI snapshots, add undo/accessibility/resizing, and assess stretch quality and output headroom. Add host automation only with stable parameter IDs and a compatibility plan.
7. **Make release checks repeatable.** Integrate installed-AU smoke testing and validation into a documented release command, pin supported JUCE/toolchain versions, and decide whether Intel/universal builds and distributable signing are required.

## Important code excerpts

The excerpts below are actual current source. They expose the implementation directly rather than replacing it with idealized pseudocode.

### Processor audio callback

From [Source/PluginProcessor.cpp](./Source/PluginProcessor.cpp):

```cpp
void FingerProcessor::processBlock(juce::AudioBuffer<float>& buffer,juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;buffer.clear();
    double ppq=0;bool playing=false;
    if(auto* head=getPlayHead())if(auto position=head->getPosition())
    {
        if(auto signature=position->getTimeSignature())if(signature->numerator>0&&signature->denominator>0)sequencer.barLength.store(signature->numerator*16.0/signature->denominator);
        if(auto bpm=position->getBpm())if(std::isfinite(*bpm)&&*bpm>0)lastTempo=*bpm;
        if(auto beat=position->getPpqPosition())if(std::isfinite(*beat)){ppq=*beat;playing=position->getIsPlaying();}
    }
    const double advance=4.0*lastTempo/(60.0*hostRate);
    if(!playing&&(!previewClockStarted||hostWasPlaying)){stoppedQ=ppq*4;previewClockStarted=true;}
    const double q=playing?ppq*4:stoppedQ;
    if(sequencer.beginBlock(q,advance,playing)){sampler.reset(false);draftEntry.panic();}
    hostWasPlaying=playing;
    sampler.consumeRequests();
    auto event=midi.cbegin();
    for(int frame=0;frame<buffer.getNumSamples();++frame)
    {
        while(event!=midi.cend()&&(*event).samplePosition<=frame)
        {
            const auto metadata=*event;
            if(metadata.numBytes>=3)
            {
                const auto* bytes=metadata.data;const int command=bytes[0]&0xf0,channel=bytes[0]&15;
                if(command==0x90||command==0x80)
                {
                    lastNote.store(bytes[1]);const bool down=command==0x90&&bytes[2]>0;
                    bool consumed=false;
                    if(down&&learn.load())
                    {
                        consumed=true;
                        if(setMidiBase(bytes[1])){sequencer.panic();sampler.reset();}
                    }
                    if(!consumed)
                    {
                        const int b=base.load();bool alternate=false;int pattern=finger::MidiMapping::pattern(b,bytes[1]);
                        int sound=finger::MidiMapping::sound(b,bytes[1]);
                        if(b==60&&pattern<0&&sound<0){pattern=finger::MidiMapping::pattern(36,bytes[1]);sound=finger::MidiMapping::sound(36,bytes[1]);alternate=true;}
                        if(pattern>=0){if(down){lastPattern.store(pattern);patternPress.fetch_add(1);}sequencer.midiKey(pattern,channel,down,alternate);}
                        else if(sound>=0)
                        {if(down)sampler.triggerBySlot(sound,(float)bytes[2]/127.0f);sequencer.recordKey(sound,bytes[1],channel,down);draftEntry.recordKey(sound,bytes[1],channel,down);}
                    }
                }
                else if(command==0xb0&&(bytes[1]==120||bytes[1]==123)){sampler.reset();sequencer.panic();draftEntry.panic();}
            }
            ++event;
        }
        sequencer.tick(q+frame*advance,[](void* context,const finger::Hit& hit){static_cast<SamplerEngine*>(context)->triggerBySlot(hit.lane,.85f);},&sampler);
        sampler.renderSample(buffer,frame);
    }
    if(!playing)stoppedQ=q+buffer.getNumSamples()*advance;
    clockSixteenths.store(q+buffer.getNumSamples()*advance);
    outputPeak.store(buffer.getMagnitude(0,buffer.getNumSamples()));audioCallbacks.fetch_add(1);
    midi.clear();
}
```

The callback uses the event offsets correctly by consuming MIDI before rendering the relevant frame. It samples the host tempo once per block, so it does not model a tempo ramp inside that block. Host playing state is only accepted when a finite PPQ position is available. Sequenced hits currently use fixed velocity 0.85, while direct sample notes use incoming velocity.

### Main settings, not AU automation parameters

There is no `AudioProcessorValueTreeState` or registered host parameter list. The important definitions are these plain model structures from [Source/SamplerEngine.h](./Source/SamplerEngine.h) and [Source/SequencerEngine.h](./Source/SequencerEngine.h):

```cpp
struct PadSettings
{
    juce::String path;
    double start = 0.0, end = 1.0, tune = 0.0, ratio = 1.0;
    bool stretch = false;
    int note = 36;
};

struct Pattern
{
    std::vector<uint32_t> lanes=std::vector<uint32_t>(soundCount,0);
    std::vector<int> lengths=std::vector<int>(soundCount,16);
    std::vector<double> speeds=std::vector<double>(soundCount,1.0);
    std::vector<int> rotations=std::vector<int>(soundCount,0);
    std::vector<int> enabled=std::vector<int>(soundCount,0);
    bool barTiming=false;
    double swing=50.0;
    int range() const {int n=1;for(auto v:lengths)if(v>n)n=v;return n;}
};
```

`start`/`end` are normalized source positions, `tune` is semitones and `ratio` is target duration relative to the original trimmed audio. A lane mask contains up to 32 hits. Parallel vectors require their indices and sizes to stay aligned.

### Asset protection at trigger time

From [Source/SamplerEngine.cpp](./Source/SamplerEngine.cpp), after selecting an available or oldest voice:

```cpp
Asset* asset;
// Publish protection before dereferencing and recheck after acquiring it.
do { asset = entry.published.load(); hazards[index].store(asset); }
while (asset != entry.published.load());
voices[index] = { asset, 0.0, juce::jlimit(0.0f, 1.0f, velocity), ++nextSerial };
```

The worker retires replaced assets and deletes them only when no voice hazard points at them. This is a useful real-time lifetime strategy, but it deserves dedicated concurrency stress coverage rather than relying solely on normal playback tests.

### Independent lane timing

From [Source/SequencerEngine.cpp](./Source/SequencerEngine.cpp):

```cpp
const auto& track=*(*list)[lane];const int n=track.length.load();const double factor=track.speed.load()*(data.barTiming.load()?n/barLength.load():1.0);
const auto position=(int64_t)std::floor((q-anchor)*factor+1e-8);const int step=(int)((position%n+n)%n);(*list)[lane]->shown.store(step);double phase=std::fmod((q-anchor)*factor/(double)n,1.0);(*list)[lane]->phase.store(phase<0?phase+1:phase);
const double delay=step%2?2.0*(data.swing.load()/100.0-0.5):0.0;const double when=anchor+((double)position+delay)/factor;
```

Each lane derives its step from a continuing pattern anchor, which is why a fill can return without resetting the groove. This same calculation also illustrates the per-sample math cost and the distinction between legacy and bar-relative timing. The test suite includes the concrete legacy boundary case: after twelve sixteenths, a 16-step kick reaches step 13 while a 12-step hat reaches step 1, including actual hits after a fill.
