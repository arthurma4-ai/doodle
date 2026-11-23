#include "PluginProcessor.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <cmath>
static void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
struct HostClock:juce::AudioPlayHead
{PositionInfo position;juce::Optional<PositionInfo> getPosition() const override{return position;}};
static void ready(SamplerEngine& engine,int lane)
{
    for(int i=0;i<2000;++i){auto pad=engine.view(lane);if(pad.readyRevision==pad.revision){check(pad.status=="Ready",pad.status.toRawUTF8());return;}std::this_thread::sleep_for(std::chrono::milliseconds(5));}throw std::runtime_error("Worker timeout");
}
int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    try
    {
        using namespace finger;
        const auto root=juce::File::getCurrentWorkingDirectory();const auto dir=root.getChildFile("build/sequence-fixtures");check(dir.createDirectory().wasOk(),"Fixture directory");
        FingerProcessor processor;processor.sequencer.restore(SequencerEngine::defaults());std::array<juce::AudioBuffer<float>,soundCount> fixtures;
        for(int lane=0;lane<soundCount;++lane)
        {
            auto& drum=fixtures[(size_t)lane];drum.setSize(2,2400);juce::Random random(1234+lane);
            for(int frame=0;frame<2400;++frame)
            {const float t=(float)frame/48000;const float sample=lane==0?0.6f*std::sin(2*juce::MathConstants<float>::pi*70*t)*std::exp(-t*70):0.3f*(random.nextFloat()*2-1)*std::exp(-t*(lane==2?200.0f:80.0f));drum.setSample(0,frame,sample);drum.setSample(1,frame,sample);}
            const auto file=dir.getChildFile("drum"+juce::String(lane)+".wav");file.deleteFile();std::unique_ptr<juce::OutputStream> output=std::make_unique<juce::FileOutputStream>(file);juce::WavAudioFormat wav;
            auto writer=wav.createWriterFor(output,juce::AudioFormatWriterOptions().withSampleRate(48000).withNumChannels(2).withBitsPerSample(24));check(writer&&writer->writeFromAudioSampleBuffer(drum,0,2400),"Write drum");writer.reset();auto s=processor.sampler.view(lane).settings;s.path=file.getFullPathName();processor.sampler.configure(lane,s);
        }
        for(int lane=0;lane<soundCount;++lane)ready(processor.sampler,lane);
        HostClock host;host.position.setBpm(120);host.position.setIsPlaying(true);processor.setPlayHead(&host);processor.prepareToPlay(48000,137);
        juce::MidiBuffer midi;juce::AudioBuffer<float> audio(2,137);
        // All 18 keys work with no editor; tab/record target never follows launches.
        processor.sequencer.selectEditPattern(10);
        for(int midiBase:{36,60})for(int p=0;p<patternCount;++p)
        {
            processor.sequencer.panic();host.position.setPpqPosition(0);
            midi.addEvent(juce::MidiMessage::noteOn(1,midiBase+MidiMapping::patterns[(size_t)p],(juce::uint8)100),0);processor.processBlock(audio,midi);
            check(processor.sequencer.performance().primary==p&&processor.sequencer.entry().pattern==10,"All eleven MIDI launches leave edit target alone");
            host.position.setPpqPosition(0.25);midi.addEvent(juce::MidiMessage::noteOn(1,midiBase+MidiMapping::patterns[(size_t)p],(juce::uint8)0),0);processor.processBlock(audio,midi);
            check(processor.sequencer.performance().primary<0,"Velocity-zero pattern note-on releases");
        }
        for(int midiBase:{36,60})for(int lane=0;lane<soundCount;++lane)
        {
            processor.sampler.reset();midi.addEvent(juce::MidiMessage::noteOn(1,midiBase+MidiMapping::sounds[(size_t)lane],(juce::uint8)100),64);processor.processBlock(audio,midi);
            check(audio.getMagnitude(0,64)<1e-9f&&audio.getMagnitude(64,73)>0.00001f,"All seven sound keys honor MIDI offsets");
        }
processor.sequencer.panic();host.position.setPpqPosition(0);midi.addEvent(juce::MidiMessage::noteOn(1,36,(juce::uint8)100),0);midi.addEvent(juce::MidiMessage::noteOn(1,60,(juce::uint8)100),0);processor.processBlock(audio,midi);
host.position.setPpqPosition(.25);midi.addEvent(juce::MidiMessage::noteOff(1,60),0);processor.processBlock(audio,midi);check(processor.sequencer.performance().primary==0,"Releasing upper A does not release held C1");
host.position.setPpqPosition(.5);midi.addEvent(juce::MidiMessage::noteOff(1,36),0);processor.processBlock(audio,midi);check(processor.sequencer.performance().primary<0,"C1 release stops A");
{
    SequenceState stacked;for(auto& pattern:stacked.patterns)pattern=Pattern{};
    for(int p=0;p<3;++p)stacked.patterns[(size_t)p].lanes[(size_t)p]=1;
    processor.sequencer.restore(stacked);processor.prepareToPlay(48000,137);host.position.setPpqPosition(0);
    for(int p=0;p<3;++p)midi.addEvent(juce::MidiMessage::noteOn(1,60+MidiMapping::patterns[(size_t)p],(juce::uint8)100),0);
    processor.processBlock(audio,midi);const float expected=(fixtures[0].getSample(0,10)+fixtures[1].getSample(0,10)+fixtures[2].getSample(0,10))*(10.0f/64.0f)*.425f;
    check(std::abs(audio.getSample(0,10)-expected)<1e-5f&&processor.sequencer.performance().active==7,"Actual audio mixes three independently joined patterns");
    for(int p=0;p<3;++p)midi.addEvent(juce::MidiMessage::noteOff(1,60+MidiMapping::patterns[(size_t)p]),0);
    host.position.setPpqPosition(.25);processor.processBlock(audio,midi);check(processor.sequencer.performance().active==0,"Three MIDI releases clear Join layers");processor.sequencer.restore(SequencerEngine::defaults());
}
{
    SequenceState full;for(auto& pattern:full.patterns)for(auto& lane:pattern.lanes)lane=1;
    processor.sequencer.restore(full);processor.prepareToPlay(48000,137);host.position.setPpqPosition(0);
    for(int p=0;p<patternCount;++p)midi.addEvent(juce::MidiMessage::noteOn(1,60+MidiMapping::patterns[(size_t)p],(juce::uint8)100),0);
    processor.processBlock(audio,midi);float expected=0;for(const auto& fixture:fixtures)expected+=fixture.getSample(0,10)*(10.0f/64.0f)*.425f*11;
    check(std::abs(audio.getSample(0,10)-expected)<1e-4f&&processor.sequencer.performance().active==2047,"All 77 joined hits render without stealing new voices");
    processor.sequencer.restore(SequencerEngine::defaults());
}
for(auto mode:{PlayMode::join,PlayMode::replace,PlayMode::fill})
        {
            processor.sequencer.setMode(mode);processor.prepareToPlay(48000,137);host.position.setIsPlaying(true);const auto before=processor.sequencer.performance().fires;int first=-1;
            for(int block=0;block<227;++block)
            {
                const int offset=block*137;host.position.setPpqPosition(offset/24000.0);
                auto event=[&](int at,const juce::MidiMessage& note){if(at>=offset&&at<offset+137)midi.addEvent(note,at-offset);};
                event(64,juce::MidiMessage::noteOn(1,60,(juce::uint8)100));event(12064,juce::MidiMessage::noteOn(1,62,(juce::uint8)100));event(24064,juce::MidiMessage::noteOff(1,mode==PlayMode::replace?60:62));processor.processBlock(audio,midi);
                for(int i=0;i<137;++i)if(first<0&&std::abs(audio.getSample(0,i))>1e-7f)first=offset+i;
            }
            auto after=processor.sequencer.performance();check(first==6001,"Next-16th launch uses offsets, not block edges");check(after.fires[1]-before[1]==(mode==PlayMode::replace?1u:2u),"Join/Replace/Fill MIDI release timing");
            if(mode==PlayMode::fill)check(after.primary==0&&after.steps[0]==4&&after.fires[0]-before[0]==2,"Fill returns in phase");
            if(mode==PlayMode::replace)check(after.primary==1&&after.steps[1]==0,"Replace waits for first release");
            host.position.setIsPlaying(false);processor.processBlock(audio,midi);check(processor.sequencer.performance().held==0&&audio.getMagnitude(0,137)<1e-9f,"Host stop clears lost final MIDI key-up");
        }
        // Required lane phase check through actual sample mixing, not just counters.
        for(bool fill:{false,true})
        {
            processor.sequencer.setMode(fill?PlayMode::fill:PlayMode::join);processor.sequencer.setLaneLength(0,0,16);processor.sequencer.setLaneLength(0,2,12);processor.prepareToPlay(48000,137);host.position.setIsPlaying(true);
            bool checked=false;
            for(int block=0;block<527;++block)
            {
                const int offset=block*137;host.position.setPpqPosition(offset/24000.0);auto event=[&](int at,const juce::MidiMessage& note){if(at>=offset&&at<offset+137)midi.addEvent(note,at-offset);};
                event(0,juce::MidiMessage::noteOn(1,60,(juce::uint8)100));if(fill){event(24064,juce::MidiMessage::noteOn(1,62,(juce::uint8)100));event(66064,juce::MidiMessage::noteOff(1,62));}
                processor.processBlock(audio,midi);
                if(offset<=72010&&offset+137>72010)
                {
                    const float expected=(fixtures[0].getSample(0,10)+fixtures[2].getSample(0,10))*(10.0f/64.0f)*0.425f;
                    check(std::abs(audio.getSample(0,72010-offset)-expected)<1e-5f,"Kick step 13 and hat step 1 both mix at +12 sixteenths");checked=true;
                    const auto view=processor.sequencer.performance();check(view.laneSteps[0][0]==12&&view.laneSteps[0][2]==0,"Independent lane playheads agree with actual mixed hits");
                }
            }
            check(checked,"Required phase boundary observed");
        }
        // Stopped preview uses project tempo, and host stop cannot resurrect a stale hold.
        host.position.setIsPlaying(false);processor.processBlock(audio,midi);processor.prepareToPlay(48000,137);host.position.setBpm(90);int first=-1;
        for(int block=0;block<70;++block)
        {host.position.setPpqPosition(0);if(block==0)midi.addEvent(juce::MidiMessage::noteOn(1,60,(juce::uint8)100),64);processor.processBlock(audio,midi);for(int i=0;i<137;++i)if(first<0&&std::abs(audio.getSample(0,i))>1e-7f)first=block*137+i;}
        check(first==8001,"Fresh stopped pattern preview follows project tempo 90 BPM");
        processor.sequencer.panic();processor.sampler.reset();
        // Step Record: W+E at 1; rest at 2; T then Y at 3, manual advance only.
        processor.sequencer.selectEditPattern(8);processor.sequencer.armStepRecord(true);
        auto send=[&](std::initializer_list<int> notes){for(int note:notes)midi.addEvent(juce::MidiMessage::noteOn(1,note,(juce::uint8)100),64);processor.processBlock(audio,midi);};
        send({61,63});send({61});check(processor.sequencer.entry().step==0,"Chord/repeat do not auto-advance");
        midi.addEvent(juce::MidiMessage::noteOff(1,61),0);midi.addEvent(juce::MidiMessage::noteOn(1,63,(juce::uint8)0),0);processor.processBlock(audio,midi);
        check(processor.sequencer.entry().step==1,"Final W/E release advances exactly once");processor.sequencer.skipEntry();send({66});send({68});
midi.addEvent(juce::MidiMessage::noteOff(1,66),0);processor.processBlock(audio,midi);check(processor.sequencer.entry().step==2,"T release waits for held Y");
midi.addEvent(juce::MidiMessage::noteOff(1,68),0);processor.processBlock(audio,midi);check(processor.sequencer.entry().step==3,"Final Y release advances chord exactly once");
        auto recorded=processor.sequencer.pattern(8);check(recorded.lanes[0]==1&&recorded.lanes[1]==1&&recorded.lanes[2]==4&&recorded.lanes[3]==4,"W+E chord, step-2 rest, T then Y stacked at step 3");
        send({60});check(processor.sequencer.entry().pattern==8&&processor.sequencer.entry().step==3,"Launching MIDI does not change record target/cursor");processor.sequencer.requestStop();processor.processBlock(audio,midi);
        juce::MemoryBlock recordedState;processor.getStateInformation(recordedState);FingerProcessor reopened;reopened.setStateInformation(recordedState.getData(),(int)recordedState.getSize());
        const auto recalled=reopened.sequencer.pattern(8);check(recalled.lanes==recorded.lanes&&!reopened.sequencer.entry().armed&&reopened.sequencer.performance().primary<0,"Four recorded hits survive reopen; entry off, playback idle");
        for(int lane=0;lane<soundCount;++lane)ready(reopened.sampler,lane);
        reopened.prepareToPlay(48000,137);bool stepOne=false,rest=false,stepThree=false;
        for(int block=0;block<91;++block)
        {
            if(block==0)midi.addEvent(juce::MidiMessage::noteOn(1,74,(juce::uint8)100),0);
            reopened.processBlock(audio,midi);const int offset=block*137;
            if(offset==0)stepOne=audio.getMagnitude(0,137)>1e-6f;
            if(offset>6000&&offset<6100)rest=audio.getMagnitude(0,137)<1e-9f;
            if(offset<=12010&&offset+137>12010)stepThree=audio.getMagnitude(0,137)>1e-6f;
        }
        check(stepOne&&rest&&stepThree&&reopened.sequencer.performance().fires[8]==2,"Reopened recorded pattern actually plays chord, rest, then stacked T/Y hits");
        processor.sequencer.setEntryStep(2);processor.sequencer.skipEntry();check(processor.sequencer.pattern(8).lanes[2]==0&&processor.sequencer.pattern(8).lanes[3]==0,"Clear Step clears stacked hits");
        processor.sequencer.selectEditPattern(9);processor.sequencer.setLength(9,4);processor.sequencer.setLaneLength(9,0,1);processor.sequencer.armStepRecord(true);processor.sequencer.moveEntry(2);processor.sampler.reset();send({61});
        check(processor.sequencer.pattern(9).lanes[0]==0&&audio.getMagnitude(0,137)>1e-6f,"Past a short lane: audition only, no write or wrap to step 1");
        processor.sequencer.moveEntry(99);check(processor.sequencer.entry().step==3,"Manual advance stops at longest lane end");
        processor.sequencer.armStepRecord(false);send({63});check(processor.sequencer.pattern(9).lanes[1]==0,"Record off is audition only");
        processor.sequencer.selectEditPattern(7);processor.sequencer.armStepRecord(true);send({61,63,66,68,70,73,75});
        for(auto mask:processor.sequencer.pattern(7).lanes)check(mask==1,"Seven simultaneous sound notes all enter the same cursor step");
// Complete chords in a single callback, then start the next chord in that callback.
processor.sequencer.selectEditPattern(6);processor.sequencer.armStepRecord(true);
midi.addEvent(juce::MidiMessage::noteOn(1,61,(juce::uint8)100),2);midi.addEvent(juce::MidiMessage::noteOff(1,61),20);
midi.addEvent(juce::MidiMessage::noteOn(1,63,(juce::uint8)100),40);midi.addEvent(juce::MidiMessage::noteOn(1,66,(juce::uint8)100),40);
midi.addEvent(juce::MidiMessage::noteOff(1,63),60);midi.addEvent(juce::MidiMessage::noteOn(1,66,(juce::uint8)0),80);processor.processBlock(audio,midi);
auto two=processor.sequencer.pattern(6);check(two.lanes[0]==1&&two.lanes[1]==2&&two.lanes[2]==2&&processor.sequencer.entry().step==2,"Same-block sequential chords keep offsets, share their step, and advance on final/velocity-zero release");
// Low MIDI row: O/P enter and audition together, release in separate callbacks.
midi.addEvent(juce::MidiMessage::noteOn(1,49,(juce::uint8)100),10);midi.addEvent(juce::MidiMessage::noteOn(1,51,(juce::uint8)100),10);processor.processBlock(audio,midi);
midi.addEvent(juce::MidiMessage::noteOff(1,49),10);processor.processBlock(audio,midi);check(processor.sequencer.entry().step==2,"Low-row shaker release waits for tom");
midi.addEvent(juce::MidiMessage::noteOff(1,51),10);processor.processBlock(audio,midi);check(processor.sequencer.entry().step==3&&processor.sequencer.pattern(6).lanes[5]==4&&processor.sequencer.pattern(6).lanes[6]==4,"Shaker/tom chord advances and writes both lanes");
        processor.learnFromA();send({127});check(processor.midiBase()==60&&processor.learning()&&processor.learnRejected(),"Out-of-range learned base rejected");send({50});check(processor.midiBase()==50&&!processor.learning()&&processor.lastMidiNote()==50,"Learn from A + last note readout");check(processor.setMidiBase(110)&&!processor.setMidiBase(111)&&processor.midiBase()==110,"All derived notes range-checked");processor.setMidiBase(60);processor.processBlock(audio,midi);
        // Legacy session migration, including the actual stored seventh sample in v1.
        for(const auto name:{"legacy6.state","legacy12.state"})
        {
            juce::MemoryBlock legacy;check(root.getChildFile("Tests/Fixtures").getChildFile(name).loadFileAsData(legacy),"Load legacy fixture");auto xml=juce::AudioProcessor::getXmlFromBinary(legacy.getData(),(int)legacy.getSize());check(xml!=nullptr,"Legacy XML");FingerProcessor migrated;migrated.setStateInformation(legacy.getData(),(int)legacy.getSize());
            for(auto* child:xml->getChildIterator())
            {
                if(child->hasTagName("Pad")){const int slot=child->getIntAttribute("slot");if(slot>=7)continue;auto s=migrated.sampler.view(slot).settings;check(s.path==child->getStringAttribute("path")&&s.note==child->getIntAttribute("note"),"Legacy reference/settings retained");check(std::abs(s.start-child->getDoubleAttribute("start"))<1e-9&&std::abs(s.end-child->getDoubleAttribute("end"))<1e-9&&std::abs(s.tune-child->getDoubleAttribute("tune"))<1e-9&&s.stretch==child->getBoolAttribute("stretch")&&std::abs(s.ratio-child->getDoubleAttribute("ratio"))<1e-9,"Legacy trim/tune/stretch retained");ready(migrated.sampler,slot);}
                if(child->hasTagName("Pattern")){const int index=child->getIntAttribute("index");auto p=migrated.sequencer.pattern(index);for(int lane=0;lane<7;++lane){check(p.lengths[(size_t)lane]==child->getIntAttribute("length",16),"Legacy pattern length copied to all seven lanes");check(p.lanes[(size_t)lane]==(uint32_t)child->getStringAttribute("lane"+juce::String(lane)).getHexValue64(),"Legacy edits retained");}check(std::abs(p.swing-child->getDoubleAttribute("swing"))<1e-9,"Legacy swing");}
            }
            for(int p=8;p<11;++p)for(auto mask:migrated.sequencer.pattern(p).lanes)check(mask==0,"Three new patterns empty");
            if(juce::String(name)=="legacy6.state")check(migrated.sampler.view(6).settings.path=="factory:Tom","Legacy empty seventh slot receives tom");
            check(!migrated.sequencer.entry().armed&&migrated.sequencer.performance().primary<0,"Legacy reopen idle and Step Record off");
        }
        auto seq=processor.sequencer.state();seq.hold=true;seq.mode=PlayMode::fill;seq.patterns[10].lengths[6]=32;seq.patterns[10].lanes[6]=uint32_t(1)<<31;seq.patterns[10].swing=67;processor.sequencer.restore(seq);processor.setMidiBase(48);
        juce::MemoryBlock saved;processor.getStateInformation(saved);check(dir.getChildFile("seven-kit.state").replaceWithData(saved.getData(),saved.getSize()),"Save installed AU fixture");FingerProcessor restored;restored.setStateInformation(saved.getData(),(int)saved.getSize());auto p=restored.sequencer.pattern(10);check(p.lengths[6]==32&&p.lanes[6]==(uint32_t(1)<<31)&&std::abs(p.swing-67)<1e-9&&restored.midiBase()==48,"Eleventh pattern, seventh lane, step32 and learned base persist");
        {
            juce::XmlElement legacy("FingerSampler");legacy.setAttribute("version",3);legacy.setAttribute("midiBase",48);
            for(int lane=0;lane<7;++lane){auto* pad=legacy.createNewChildElement("Pad");pad->setAttribute("slot",lane);pad->setAttribute("path",processor.sampler.view(lane).settings.path);pad->setAttribute("start",.1);pad->setAttribute("end",.8);pad->setAttribute("tune",-3);pad->setAttribute("stretch",true);pad->setAttribute("ratio",1.5);}
            for(int key=0;key<11;++key){auto* p=legacy.createNewChildElement("Pattern");p->setAttribute("index",key);p->setAttribute("swing",63.0);p->setAttribute("tracks",64);for(int lane=0;lane<7;++lane){p->setAttribute("lane"+juce::String(lane),key==10&&lane==6?"80000000":"1");p->setAttribute("length"+juce::String(lane),lane==2?12:32);}}
            juce::MemoryBlock bytes;juce::AudioProcessor::copyXmlToBinary(legacy,bytes);FingerProcessor migrated;migrated.setStateInformation(bytes.getData(),(int)bytes.getSize());
            check(migrated.memoryNames().size()==1&&migrated.midiBase()==48&&migrated.sequencer.pattern(10).lanes[6]==(uint32_t(1)<<31)&&migrated.sequencer.pattern(0).lengths[2]==12&&!migrated.sequencer.pattern(0).barTiming&&migrated.sequencer.pattern(0).enabled[6],"State-v3 migrates into Main Memory, preserving per-lane phase semantics, step 32, mapping and explicit rows");
            auto settings=migrated.sampler.view(0).settings;check(settings.tune==-3&&settings.stretch&&settings.ratio==1.5&&settings.start==.1&&settings.end==.8,"State-v3 sampler settings retained");
        }
        FingerProcessor maker;maker.prepareToPlay(48000,137);auto clean=maker.draft();for(auto& mask:clean.lanes)mask=0;maker.setDraft(clean);auto keyBefore=maker.sequencer.pattern(0).lanes;maker.draftEntry.armStepRecord(true);
        midi.addEvent(juce::MidiMessage::noteOn(1,61,(juce::uint8)100),2);midi.addEvent(juce::MidiMessage::noteOn(1,63,(juce::uint8)100),2);maker.processBlock(audio,midi);midi.addEvent(juce::MidiMessage::noteOff(1,61),3);maker.processBlock(audio,midi);check(maker.draftEntry.entry().step==0,"Maker chord waits for last sound key");midi.addEvent(juce::MidiMessage::noteOff(1,63),3);maker.processBlock(audio,midi);
        check(maker.draftEntry.entry().step==1&&maker.draft().lanes[0]==1&&maker.draft().lanes[1]==1&&maker.sequencer.pattern(0).lanes==keyBefore,"Step Record edits separate draft and advances on release without touching keys");maker.draftEntry.skipEntry();check(maker.draftEntry.entry().step==2,"Maker Skip advances a rest");maker.storeDraft();maker.assignDraft(10);maker.addMemory();check(!maker.draftEntry.entry().armed&&maker.sampler.slotCount()==0,"Bank switch disarms draft and empties new library");maker.switchMemory(0);check(maker.draft().lanes[0]==1&&maker.currentMemory().vault.size()==1&&maker.sequencer.pattern(10).lanes[0]==1,"Bank switch restores draft, assigned key and vault together");
        std::cout<<"PASS all 18 MIDI keys without editor, offsets, velocity-zero release, three modes, host stop, stopped preview at project tempo\nPASS kick step 13 + 12-step hat step 1 at +12, actual mixed audio and playheads, with/without Fill\nPASS release-to-advance Step Record, same-block chords, Skip/rest/stack/repeat, short-lane bounds, recall off/idle, learn/reset/range, v1/v2 migration, v3 persistence\n";return 0;
    }
    catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
