#pragma once
#include "MidiMapping.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <vector>
#include <memory>
namespace finger
{
enum class PlayMode {join,replace,fill};
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
struct SequenceState {std::array<Pattern,patternCount> patterns;PlayMode mode=PlayMode::join;bool hold=false;};
struct Entry {bool armed=false;int pattern=0,step=0,range=16;};
struct Performance
{
    int primary=-1,secondary=-1;
    bool queued=false,primaryAudible=false,playing=false,latched=false;
    uint32_t held=0,active=0;
    std::array<int,patternCount> steps;
    std::array<std::vector<int>,patternCount> laneSteps;
    std::array<std::vector<double>,patternCount> phases;
    std::array<uint64_t,patternCount> fires;
};
struct Hit {int pattern=0,lane=0,step=0;};
struct Hits {std::array<Hit,soundCount*patternCount> items;int count=0;};
class SequencerEngine
{
public:
    SequencerEngine();
    static SequenceState defaults();
    SequenceState state() const;
    void restore(const SequenceState&);
    Pattern pattern(int) const;
    void setStep(int pattern,int lane,int step,bool enabled);
    void setLength(int pattern,int length); // compatibility/helper: all lanes
    void setLaneLength(int pattern,int lane,int length);
    void setSwing(int pattern,double percent);
    void setMode(PlayMode value){mode.store((int)value);}
    PlayMode getMode() const{return (PlayMode)mode.load();}
    void setHold(bool value){hold.store(value);}
    bool getHold() const{return hold.load();}
    Entry entry() const;
    void selectEditPattern(int);
    void armStepRecord(bool);
    void setEntryStep(int);
    void moveEntry(int);
    void clearEntryStep();
    void recordSound(int lane,const Entry& target); // atomic OR into shared grid
    void recordKey(int lane,int note,int channel,bool down); // audio thread; advance on final release
    void skipEntry(); // UI; clear this step and advance atomically
    void setUiHeld(int pattern,int source,bool down);
    void clearUiHeld();
    bool isUiHeld(int p) const{return p>=0&&p<patternCount&&uiHeld[(size_t)p].load()!=0;}
    void requestStop(){stopRequested.store(true);}
    void panic(){stopRequested.store(false);stop();}
    void midiKey(int pattern,int channel,bool down,bool alternate=false);
    bool beginBlock(double sixteenths,double perSample,bool hostPlaying);
    std::atomic<double> barLength {16};
    using HitSink=void(*)(void*,const Hit&);
    Hits tick(double sixteenths,HitSink sink=nullptr,void* context=nullptr);
    void ensureLaneCount(int); // control thread; no limit beyond available memory
    void setPattern(int,const Pattern&);
    Performance performance() const;
private:
struct AtomicLane {
    std::atomic<uint32_t> bits {0};std::atomic<int> length {16},enabled {0},shown {-1},rotation {0};std::atomic<double> speed {1},phase {0};
};
using LaneList=std::vector<AtomicLane*>;
struct AtomicPattern {
    std::vector<std::unique_ptr<AtomicLane>> owners;
    std::vector<std::unique_ptr<LaneList>> versions;
    std::atomic<const LaneList*> tracks {nullptr};
    std::atomic<double> swing {50.0};std::atomic<bool> barTiming {false};
};
    int entryRange(int pattern) const;
    static uint64_t packEntry(bool armed,int pattern,int step){return (armed?65536u:0u)|((uint32_t)pattern<<8)|(uint32_t)step;}
    void resetRecordGroup();
    void key(int,uint64_t,bool);
    std::array<uint16_t,128> entryHeld {};
    uint64_t groupToken=0;
    bool groupActive=false;
    Entry groupEntry;
    void stop();void transition(double);int newestOther(int) const;int oldestHeld() const;void publish();
    std::array<AtomicPattern,patternCount> patterns;
    std::atomic<uint64_t> entryState {0}; // one atomic transaction for target, cursor and arm state
    std::atomic<int> mode {0};
    std::atomic<bool> hold {false},stopRequested {false};
    std::array<std::atomic<uint32_t>,patternCount> uiHeld {};
    std::array<uint64_t,patternCount> held {};
    std::array<uint64_t,patternCount> order {};
    uint64_t serial=0;
    int primary=-1,secondary=-1;
    uint32_t joined=0;
    std::array<double,patternCount> joinStarts {};
    double primaryStart=0,secondaryStart=0,lastQ=-1e-6,expectedQ=0,sampleAdvance=0;
    PlayMode activeMode=PlayMode::join;
    bool playing=false,clockValid=false,expectedValid=false,secondaryWasQueued=false;
    std::atomic<int> shownPrimary {-1},shownSecondary {-1};
    std::atomic<bool> shownQueued {false},shownAudible {false},shownPlaying {false},shownLatched {false};
    std::atomic<uint32_t> shownHeld {0},shownActive {0};
    std::array<std::atomic<uint64_t>,patternCount> fires {};
};
}
