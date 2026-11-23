#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "SamplerEngine.h"
#include "SequencerEngine.h"
#include "MemoryBank.h"
class FingerProcessor final:public juce::AudioProcessor
{
public:
    FingerProcessor();
    void prepareToPlay(double,int) override;
    void releaseResources() override{sampler.reset();sequencer.panic();}
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&,juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override{return true;}
    const juce::String getName() const override{return "Finger Sequencer";}
    bool acceptsMidi() const override{return true;}
    bool producesMidi() const override{return false;}
    bool isMidiEffect() const override{return false;}
    double getTailLengthSeconds() const override{return 120.0;}
    int getNumPrograms() override{return 1;}
    int getCurrentProgram() override{return 0;}
    void setCurrentProgram(int) override{}
    const juce::String getProgramName(int) override{return "Finger Drum";}
    void changeProgramName(int,const juce::String&) override{}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*,int) override;
    std::array<std::atomic<int>,finger::patternCount> visibleTracks {};
    std::atomic<int> lastPattern {-1};
    std::atomic<uint64_t> patternPress {0};
    std::atomic<uint64_t> audioCallbacks {0};
    std::atomic<float> outputPeak {0};
    SamplerEngine sampler;
    finger::SequencerEngine sequencer;
    finger::SequencerEngine draftEntry;
    bool configureSample(int slot,PadSettings s,bool reload=false){sampler.configure(slot,s,reload);return true;}
MemoryBank currentMemory();
juce::StringArray memoryNames() const;
int currentMemoryIndex() const{return memoryIndex;}
void switchMemory(int);
void addMemory();
void installMemory(MemoryBank);
finger::Pattern draft() const;
void setDraft(const finger::Pattern&);
void assignDraft(int);
void storeDraft();
void loadVault(int);
int addSample(const juce::String& path={});
std::atomic<double> clockSixteenths {0};
bool setMidiBase(int);

    int midiBase() const{return base.load();}
    int lastMidiNote() const{return lastNote.load();}
    void learnFromA(){rejected.store(false);learn.store(true);}
    bool learning() const{return learn.load();}
    bool learnRejected() const{return rejected.load();}
private:
void captureMemory();void applyMemory();void initialiseMemory();
mutable std::recursive_mutex memoryMutex;
std::vector<MemoryBank> memories;std::atomic<int> memoryIndex {0};
std::atomic<int> base {60},lastNote {-1};
    std::atomic<bool> learn {false},rejected {false};
    double hostRate=44100,stoppedQ=0,lastTempo=120;
    bool hostWasPlaying=false,previewClockStarted=false;
};
