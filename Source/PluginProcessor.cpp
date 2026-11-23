#include "PluginProcessor.h"
#include "PluginEditor.h"
FingerProcessor::FingerProcessor():AudioProcessor(BusesProperties().withOutput("Output",juce::AudioChannelSet::stereo(),true))
{
    const char* names[]={"Kick","Clap","Snare","ClosedHat","OpenHat","Shaker","Tom"};
    for(int i=0;i<7;++i){PadSettings settings;settings.note=36+i;settings.path="factory:"+juce::String(names[i]);sampler.configure(i,settings);}
    auto starter=finger::SequencerEngine::defaults();for(auto& pattern:starter.patterns)pattern=finger::Pattern{};
    starter.patterns[0].lanes={0x1111,0x1010,0x1010,0x4444,0x4444,0,0};
    for(int i=0;i<5;++i)starter.patterns[(size_t)i+1].lanes[(size_t)i]=starter.patterns[0].lanes[(size_t)i];
    sequencer.restore(starter);initialiseMemory();
}
void FingerProcessor::prepareToPlay(double rate,int)
{hostRate=rate;sampler.prepare(rate);sequencer.panic();hostWasPlaying=false;previewClockStarted=false;stoppedQ=0;}
bool FingerProcessor::isBusesLayoutSupported(const BusesLayout& layout) const
{return layout.getMainInputChannelSet().isDisabled()&&(layout.getMainOutputChannelSet()==juce::AudioChannelSet::mono()||layout.getMainOutputChannelSet()==juce::AudioChannelSet::stereo());}
bool FingerProcessor::setMidiBase(int note)
{
    if(!finger::MidiMapping::valid(note)){rejected.store(true);return false;}
    base.store(note);rejected.store(false);learn.store(false);sequencer.requestStop();return true;
}
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
void FingerProcessor::getStateInformation(juce::MemoryBlock& data)
{
std::lock_guard<std::recursive_mutex> lock(memoryMutex);captureMemory();juce::XmlElement root("FingerSampler");root.setAttribute("version",4);root.setAttribute("midiBase",base.load());root.setAttribute("currentMemory",memoryIndex);
for(const auto& bank:memories)root.addChildElement(writeMemory(bank).release());copyXmlToBinary(root,data);
}
void FingerProcessor::setStateInformation(const void* data,int size)
{
    auto root=getXmlFromBinary(data,size);if(!root||!root->hasTagName("FingerSampler"))return;
if(root->getIntAttribute("version",1)>=4){std::lock_guard<std::recursive_mutex> lock(memoryMutex);memories.clear();for(auto* bank:root->getChildIterator())if(bank->hasTagName("Memory"))memories.push_back(readMemory(*bank));if(memories.empty())return;memoryIndex=juce::jlimit(0,(int)memories.size()-1,root->getIntAttribute("currentMemory",0));int b=root->getIntAttribute("midiBase",60);base.store(finger::MidiMapping::valid(b)?b:60);learn.store(false);lastNote.store(-1);applyMemory();return;}
    std::array<PadSettings,SamplerEngine::slots> kit;
    for(int i=0;i<SamplerEngine::slots;++i)kit[(size_t)i].note=36+i;
    auto state=finger::SequencerEngine::defaults();state.mode=(finger::PlayMode)juce::jlimit(0,2,root->getIntAttribute("mode",0));state.hold=false; // Hold control removed; legacy latches must not become invisible.
    for(auto& tracks:visibleTracks)tracks.store(0);lastPattern.store(-1);
    const int version=root->getIntAttribute("version",1);
    for(auto* child:root->getChildIterator())
    {
        if(child->hasTagName("Pad"))
        {
            const int slot=child->getIntAttribute("slot",-1);if(slot<0||slot>=SamplerEngine::slots)continue;
            auto& s=kit[(size_t)slot];s.path=child->getStringAttribute("path");s.start=child->getDoubleAttribute("start",0);s.end=child->getDoubleAttribute("end",1);
            s.tune=child->getDoubleAttribute("tune",0);s.stretch=child->getBoolAttribute("stretch",false);s.ratio=child->getDoubleAttribute("ratio",1);s.note=child->getIntAttribute("note",36+slot);
        }
        else if(child->hasTagName("Pattern"))
        {
            const int p=child->getIntAttribute("index",-1);if(p<0||p>=finger::patternCount)continue;
            visibleTracks[(size_t)p].store(child->getIntAttribute("tracks",0)&127);auto& pattern=state.patterns[(size_t)p];for(int lane=0;lane<7;++lane)pattern.enabled[(size_t)lane]=(visibleTracks[(size_t)p].load()&(1<<lane))!=0;pattern.swing=child->getDoubleAttribute("swing",50);
            for(int lane=0;lane<finger::soundCount;++lane)
            {
                pattern.lanes[(size_t)lane]=(uint32_t)child->getStringAttribute("lane"+juce::String(lane)).getHexValue64();
                pattern.lengths[(size_t)lane]=version<3?child->getIntAttribute("length",16):child->getIntAttribute("length"+juce::String(lane),16);
            }
        }
    }
    int b=root->getIntAttribute("midiBase",60);if(!finger::MidiMapping::valid(b))b=60;
    base.store(b);learn.store(false);rejected.store(false);lastNote.store(-1);
if(kit[5].path.isEmpty())kit[5].path="factory:Shaker";
if(kit[6].path.isEmpty())kit[6].path="factory:Tom";
    sampler.restore(kit);sequencer.restore(state);initialiseMemory();
}
void FingerProcessor::captureMemory()
{if(memories.empty())return;auto& bank=memories[(size_t)memoryIndex];bank.samples=sampler.allSettings();bank.sequences=sequencer.state();bank.draft=draftEntry.pattern(0);for(int p=0;p<finger::patternCount;++p)for(size_t lane=0;lane<bank.sequences.patterns[(size_t)p].enabled.size()&&lane<7;++lane)if(visibleTracks[(size_t)p].load()&(1<<(int)lane))bank.sequences.patterns[(size_t)p].enabled[lane]=1;}
void FingerProcessor::initialiseMemory()
{std::lock_guard<std::recursive_mutex> lock(memoryMutex);memories.clear();memoryIndex=0;MemoryBank bank;bank.samples=sampler.allSettings();bank.sequences=sequencer.state();bank.draft=bank.sequences.patterns[0];bank.draft.barTiming=true;bank.draft.enabled.assign(bank.samples.size(),1);memories.push_back(std::move(bank));draftEntry.setPattern(0,memories[0].draft);draftEntry.armStepRecord(false);}
MemoryBank FingerProcessor::currentMemory(){std::lock_guard<std::recursive_mutex> lock(memoryMutex);captureMemory();return memories[(size_t)memoryIndex];}
juce::StringArray FingerProcessor::memoryNames() const{std::lock_guard<std::recursive_mutex> lock(memoryMutex);juce::StringArray names;for(const auto& bank:memories)names.add(bank.name);return names;}
void FingerProcessor::applyMemory()
{auto& bank=memories[(size_t)memoryIndex];sampler.restoreAll(bank.samples);sequencer.restore(bank.sequences);for(auto& mask:visibleTracks)mask.store(0);lastPattern.store(-1);draftEntry.setPattern(0,bank.draft);draftEntry.armStepRecord(false);}
void FingerProcessor::switchMemory(int index){std::lock_guard<std::recursive_mutex> lock(memoryMutex);if(index<0||index>=(int)memories.size()||index==memoryIndex)return;captureMemory();memoryIndex=index;applyMemory();}
void FingerProcessor::addMemory(){MemoryBank bank;bank.name="Memory "+juce::String((int)memoryNames().size()+1);for(auto& p:bank.sequences.patterns){p.lanes.clear();p.lengths.clear();p.speeds.clear();p.enabled.clear();p.barTiming=true;}bank.draft=bank.sequences.patterns[0];installMemory(std::move(bank));}
void FingerProcessor::installMemory(MemoryBank bank){std::lock_guard<std::recursive_mutex> lock(memoryMutex);captureMemory();memories.push_back(std::move(bank));memoryIndex=(int)memories.size()-1;applyMemory();}
finger::Pattern FingerProcessor::draft() const{std::lock_guard<std::recursive_mutex> lock(memoryMutex);return draftEntry.pattern(0);}
void FingerProcessor::setDraft(const finger::Pattern& p){std::lock_guard<std::recursive_mutex> lock(memoryMutex);auto copy=p;copy.barTiming=true;memories[(size_t)memoryIndex].draft=copy;draftEntry.setPattern(0,copy);draftEntry.setEntryStep(draftEntry.entry().step);}
void FingerProcessor::assignDraft(int key){std::lock_guard<std::recursive_mutex> lock(memoryMutex);auto p=draft();sequencer.setPattern(key,p);}
void FingerProcessor::storeDraft(){std::lock_guard<std::recursive_mutex> lock(memoryMutex);auto& bank=memories[(size_t)memoryIndex];bank.draft=draftEntry.pattern(0);bank.vault.push_back({"Pattern "+juce::String((int)bank.vault.size()+1),bank.draft});}
void FingerProcessor::loadVault(int index){std::lock_guard<std::recursive_mutex> lock(memoryMutex);auto& bank=memories[(size_t)memoryIndex];if(index>=0&&index<(int)bank.vault.size()){bank.draft=bank.vault[(size_t)index].pattern;draftEntry.setPattern(0,bank.draft);draftEntry.setEntryStep(0);}}
int FingerProcessor::addSample(const juce::String& path){std::lock_guard<std::recursive_mutex> lock(memoryMutex);const int index=sampler.slotCount();PadSettings s;s.path=path;sampler.configure(index,s);sequencer.ensureLaneCount(index+1);auto p=draft();p.lanes.resize((size_t)index+1);p.lengths.resize((size_t)index+1,16);p.speeds.resize((size_t)index+1,1);p.enabled.resize((size_t)index+1,0);p.enabled[(size_t)index]=1;setDraft(p);captureMemory();return index;}
juce::AudioProcessorEditor* FingerProcessor::createEditor(){return new FingerEditor(*this);}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter(){return new FingerProcessor();}
