#include "../Source/SequencerEngine.h"
#include "../Source/Euclidean.h"
#include <iostream>
#include <vector>
#include <stdexcept>
#include <cmath>
using namespace finger;
static void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
struct Rig
{
    SequencerEngine engine;
    double q = 0.0;
    struct Event { double at; Hit hit; };
    std::vector<Event> events;
    Rig(PlayMode mode) { engine.setMode(mode); engine.beginBlock(0, 0.125, true); }
    void to(double end)
    {
        while (q <= end + 1.0e-8)
        {
            const auto hits = engine.tick(q);
            for (int i = 0; i < hits.count; ++i) events.push_back({q, hits.items[(size_t)i]});
            q += 0.125;
        }
    }
    bool hitAt(double at, int pattern, int step) const
    {
        for (const auto& e : events) if (std::abs(e.at-at)<1.0e-6 && e.hit.pattern == pattern && e.hit.step == step) return true;
        return false;
    }
    void down(int p) { engine.midiKey(p, 0, true); }
    void up(int p) { engine.midiKey(p, 0, false); }
};
int main()
{
    try
    {
        for (auto mode : {PlayMode::join, PlayMode::replace, PlayMode::fill})
        {
            Rig r(mode);
            r.to(0); r.down(0); r.to(1); check(r.hitAt(1,0,0), "First press quantized to next 16th");
            r.to(3); r.down(1); r.to(4);
            check(r.engine.performance().primary == 0 && r.engine.performance().secondary == 1, "Two performers");
            if (mode == PlayMode::replace)
            {
                check(r.engine.performance().queued && !r.hitAt(4,1,0), "Replace queues, does not layer");
                r.to(6); r.up(0); r.to(7);
                check(r.engine.performance().primary == 1 && r.hitAt(7,1,0), "Replace waits for first release then starts roll");
                r.up(1); r.to(8); check(r.engine.performance().primary == -1, "Replacement releases");
            }
            else
            {
                check(r.hitAt(4,1,0), "Second starts on boundary");
                r.to(6); r.up(1); r.to(7);
                check(r.hitAt(7,0,6), "Primary returns in phase at groove step 6");
                check(!r.hitAt(7,1,3), "Secondary release stops roll");
                if (mode == PlayMode::fill)
                {
                    for (const auto& e : r.events) if (e.at >=4 && e.at <7) check(e.hit.pattern !=0, "Fill mutes primary");
                }
                else check(r.hitAt(5,0,4) && r.hitAt(5,1,1), "Join layers kick/hat and roll");
                r.up(0); r.to(8); check(r.engine.performance().primary == -1, "Primary releases");
            }
            std::cout << "PASS " << (mode==PlayMode::join ? "Join" : mode==PlayMode::replace ? "Replace" : "Fill In")
                      << ": press 1 -> press 2 -> release " << (mode==PlayMode::replace ? "1" : "2") << " -> release remaining key\n";
        }
        for (auto mode : {PlayMode::join, PlayMode::fill})
        {
            Rig r(mode); r.engine.setHold(true); r.down(0); r.to(0); r.up(0); r.to(2);
            check(r.engine.performance().primary==0 && r.engine.performance().latched, "Hold latches groove");
            r.down(1); r.to(3); r.up(1); r.to(4);
            check(r.engine.performance().primary==0 && r.engine.performance().secondary<0, "Momentary layer/fill with Hold");
            r.engine.requestStop(); check(r.engine.beginBlock(r.q,0.125,true), "Explicit stop requests audio panic");
            check(r.engine.performance().primary<0, "Stop clears latch");
        }
        for(auto mode : {PlayMode::join,PlayMode::replace,PlayMode::fill})
        {
            Rig r(mode);r.down(0);r.to(0);r.down(1);r.to(1);r.up(0);r.to(2);
            check(r.engine.performance().primary==1,"First release promotes second in every mode");
            check(r.engine.performance().steps[1]==(mode==PlayMode::replace?0:1),"Playing secondary keeps phase; queued secondary starts at zero");
            r.up(1);r.to(3);check(r.engine.performance().primary<0,"Final release always stops");
        }
        {
            Rig r(PlayMode::replace); r.down(0); r.to(0); r.down(1); r.to(1); r.up(1); r.to(2); r.up(0); r.to(3);
            check(r.engine.performance().primary<0, "Release queued before primary never launches it");
        }
        {
            Rig r(PlayMode::join); r.to(0); r.down(0); r.up(0); r.to(1);
            check(r.engine.performance().primary<0, "Fast tap before boundary cancels");
            r.down(0); r.to(2); r.down(0); r.to(3); check(r.engine.performance().steps[0]==1, "Repeated key-on does not restart");
            r.down(1); r.to(4); r.up(0); r.to(5);
            check(r.engine.performance().primary==1 && r.engine.performance().steps[1]==1, "Primary released first promotes layer in phase");
            r.engine.beginBlock(0,0.125,true); r.q=0; r.to(1);
            check(r.engine.performance().primary==1, "Loop wrap retains valid held performer");
            check(r.engine.beginBlock(r.q,0.125,false), "DAW stop requests silence");
            check(r.engine.performance().primary<0 && r.engine.performance().held==0, "Stop clears lost MIDI note-off");
            r.engine.beginBlock(0,0.125,true); r.q=0; r.to(1); check(r.engine.performance().primary<0, "Restart cannot revive stuck key");
        }
        {
            Rig r(PlayMode::join); r.engine.setUiHeld(0,0,true); r.engine.beginBlock(0,0.125,true); r.to(0);
            r.engine.clearUiHeld(); r.engine.beginBlock(r.q,0.125,true); r.to(1);
            check(r.engine.performance().primary<0, "Focus loss clears lost computer key-up");
        }
        {
            Rig r(PlayMode::join); r.engine.setLength(0,3); r.engine.setLength(1,5);
            r.engine.setSwing(1,75); r.down(0); r.down(1); r.to(6);
            check(r.hitAt(1.5,1,1) && !r.hitAt(1,1,1), "75% swing delays odd steps by half a 16th");
            check(r.hitAt(3,0,0) && r.hitAt(5,1,0), "Independent lengths wrap independently");
        }
        std::cout << "PASS Hold, Stop, release order, fast taps, repeat, independent lengths/swing, loop wrap, transport stop, focus-loss recovery\n";
        for(bool fill:{false,true})
        {
            Rig r(fill?PlayMode::fill:PlayMode::join);
            r.engine.setLaneLength(0,0,16);r.engine.setLaneLength(0,2,12);
            r.engine.setSwing(0,50);r.engine.setStep(0,0,12,true);r.engine.setStep(0,2,0,true);
            r.down(0);r.to(0);
            if(fill){r.to(4);r.down(1);r.to(11);r.up(1);}
            r.to(12);
            bool kick=false,hat=false;
            for(const auto& e:r.events)if(std::abs(e.at-12)<1e-6&&e.hit.pattern==0)
            {kick|=e.hit.lane==0&&e.hit.step==12;hat|=e.hit.lane==2&&e.hit.step==0;}
            const auto view=r.engine.performance();
            check(kick&&hat&&view.laneSteps[0][0]==12&&view.laneSteps[0][2]==0,"At +12: kick step 13 and hat step 1 both fire; playheads agree");
            std::cout<<"PASS "<<(fill?"Fill return":"Straight playback")<<": +12 sixteenths -> kick step 13 + hat step 1, both actual hits\n";
        }
        {
            SequencerEngine e;e.selectEditPattern(8);e.armStepRecord(true);auto chord=e.entry();
            e.selectEditPattern(9);e.moveEntry(2);
            for(int lane=0;lane<soundCount;++lane)e.recordSound(lane,chord);
            for(int lane=0;lane<soundCount;++lane)check(e.pattern(8).lanes[(size_t)lane]==1&&e.pattern(9).lanes[(size_t)lane]==0,"Chord snapshot is independent of concurrent UI target change");
            e.setStep(0,0,31,true);e.setLaneLength(0,0,1);check((e.pattern(0).lanes[0]&(uint32_t(1)<<31))!=0,"Shortening retains hidden cells");
            e.restore(SequencerEngine::defaults());check(!e.entry().armed,"Restore disarms Step Record");
        }
{
    Rig r(PlayMode::join);SequenceState kit;for(auto& pattern:kit.patterns)pattern=Pattern{};
    for(int p=0;p<patternCount;++p)for(int lane=0;lane<soundCount;++lane)kit.patterns[(size_t)p].lanes[(size_t)lane]=1;
    r.engine.restore(kit);r.engine.beginBlock(0,.125,true);for(int p=0;p<patternCount;++p)r.down(p);r.to(0);
    check(r.events.size()==77&&r.engine.performance().active==2047,"Join launches all eleven patterns / 77 simultaneous lane hits without overflow");
    for(int p=0;p<patternCount;++p)r.up(p);r.to(1);check(r.engine.performance().active==0,"All eleven Join releases stop independently");
}
{
    Rig r(PlayMode::join);for(int p=0;p<3;++p){r.engine.setLength(p,3+p*2);r.engine.setStep(p,0,0,true);}
    r.down(0);r.to(0);r.to(1);r.down(1);r.to(2);r.down(2);r.to(3);
    check(r.engine.performance().active==7,"Three joined patterns are audible");r.up(0);r.to(4);
    auto view=r.engine.performance();check(view.active==6&&view.laneSteps[1][0]==2&&view.laneSteps[2][0]==1,"Releasing primary preserves both remaining Join phases");
    r.up(1);r.to(5);check(r.engine.performance().active==4&&r.engine.performance().laneSteps[2][0]==2,"Third Join pattern survives first two releases in phase");
    r.engine.beginBlock(r.q,.125,false);check(r.engine.performance().active==0,"Host stop clears every Join layer");
}
{
    SequencerEngine e;e.selectEditPattern(10);e.armStepRecord(true);
    e.recordKey(0,61,0,true);e.recordKey(1,63,0,true);e.recordKey(0,61,0,true);
    e.recordKey(0,61,0,false);check(e.entry().step==0,"Chord waits for every sound release");e.recordKey(1,63,0,false);
    check(e.entry().step==1&&e.pattern(10).lanes[0]==1&&e.pattern(10).lanes[1]==1,"Final chord release advances once and preserves both hits");
    e.skipEntry();check(e.entry().step==2,"Skip advances empty rest");e.recordKey(2,66,0,true);e.recordKey(3,68,0,true);e.recordKey(3,68,0,false);e.recordKey(2,66,0,false);
    check(e.entry().step==3&&e.pattern(10).lanes[2]==4&&e.pattern(10).lanes[3]==4,"Overlapping separate presses stack at step 3");
    e.recordKey(0,61,0,true);e.recordKey(0,61,1,true);e.recordKey(0,61,0,false);check(e.entry().step==3,"Same note on another MIDI channel stays held");e.recordKey(0,61,1,false);check(e.entry().step==4,"Last channel release advances");
    e.recordKey(4,70,0,true);e.skipEntry();e.recordKey(4,70,0,false);check(e.entry().step==5&&e.pattern(10).lanes[4]==0,"Skip clears a held chord and cancels its late release advance");
    e.recordKey(0,61,0,true);e.selectEditPattern(9);e.recordKey(0,61,0,false);check(e.entry().pattern==9&&e.entry().step==0,"Late key-up cannot advance newly selected pattern");
    e.armStepRecord(true);e.recordKey(0,61,0,true);e.armStepRecord(false);e.armStepRecord(true);e.recordKey(0,61,0,false);check(e.entry().step==0,"Rearming cancels previous held chord even at same target/cursor");
    e.setLength(9,4);e.setLaneLength(9,0,1);e.setEntryStep(2);auto before=e.pattern(9).lanes[0];e.recordKey(0,61,0,true);e.recordKey(0,61,0,false);check(e.entry().step==3&&e.pattern(9).lanes[0]==before,"Short lane auditions without writing/wrapping and still completes chord");
    e.recordKey(1,63,0,true);e.recordKey(1,63,0,false);e.skipEntry();check(e.entry().step==3,"Final step never wraps or advances past end");
    e.recordKey(0,61,0,true);e.panic();e.recordKey(0,61,0,false);check(e.entry().step==3,"Panic cancels incomplete record chord");
}
std::cout<<"PASS 11-way Join, independent release/phase, 77 hits, chord-release auto entry, Skip, MIDI channels and UI target generations\n";
        {
            for(int steps=1;steps<=32;++steps)for(int hits=0;hits<=steps;++hits){auto bits=finger::euclidean(steps,hits);int count=0;for(int i=0;i<32;++i)count+=(bits>>i)&1u;check(count==hits,"Euclidean generator has exact hit count");if(steps<32)check((bits>>steps)==0,"No Euclidean bits beyond row length");auto rotated=finger::euclidean(steps,hits,1);uint32_t expected=0;for(int i=0;i<steps;++i)if(bits&(uint32_t(1)<<i))expected|=uint32_t(1)<<((i+1)%steps);check(rotated==expected,"Rotation wraps at row length");}
            Rig r(PlayMode::join);auto p=r.engine.pattern(0);for(auto& bits:p.lanes)bits=0;p.barTiming=true;p.lengths[0]=8;p.speeds[0]=1;p.lanes[0]=1;r.engine.setPattern(0,p);r.down(0);r.to(0);check(r.hitAt(0,0,0),"Euclidean starts at 12 o'clock / step 1");r.to(8);check(r.engine.performance().laneSteps[0][0]==4,"Speed 1: half a bar reaches opposite circle side");r.to(16);check(r.hitAt(16,0,0),"Speed 1 wraps after one 4/4 bar regardless of steps");
            Rig slow(PlayMode::join);p.speeds[0]=.5;slow.engine.setPattern(0,p);slow.down(0);slow.to(16);check(slow.engine.performance().laneSteps[0][0]==4,"Speed .5: one bar is half a circle");slow.to(32);check(slow.hitAt(32,0,0),"Speed .5 wraps after two bars");
            Rig triple(PlayMode::join);triple.engine.barLength.store(12);p.speeds[0]=1;triple.engine.setPattern(0,p);triple.down(0);triple.to(12);check(triple.hitAt(12,0,0),"Speed 1 follows a 3/4 host bar");
            Rig extended(PlayMode::join);p.lanes.resize(100,0);p.lengths.resize(100,16);p.speeds.resize(100,1);p.enabled.resize(100,0);p.lanes[99]=1;extended.engine.setPattern(0,p);extended.down(0);int found=0;extended.engine.beginBlock(0,.125,true);extended.engine.tick(0,[](void* context,const Hit& h){if(h.lane==99)++*static_cast<int*>(context);},&found);check(found==1,"Dynamic sound lane 100 schedules through real-time callback without a fixed-seven limit");
        }
        return 0;
    }
    catch(const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
}
