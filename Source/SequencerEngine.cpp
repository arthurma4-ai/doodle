#include "SequencerEngine.h"
#include <algorithm>
#include <cmath>
namespace finger
{
SequenceState SequencerEngine::defaults()
{
    SequenceState s;s.patterns[0].lanes[0]=0x1111;s.patterns[0].lanes[2]=0x5555;s.patterns[1].lanes[1]=0xffff;return s;
}
SequencerEngine::SequencerEngine()
{
    ensureLaneCount(soundCount);
    for(auto& v:fires)v.store(0);for(auto& v:uiHeld)v.store(0);
    restore(defaults());stopRequested.store(false);
}
void SequencerEngine::ensureLaneCount(int count)
{
    for(auto& p:patterns)if((int)p.owners.size()<count){auto list=std::make_unique<LaneList>();for(auto& lane:p.owners)list->push_back(lane.get());while((int)p.owners.size()<count){auto lane=std::make_unique<AtomicLane>();list->push_back(lane.get());p.owners.push_back(std::move(lane));}p.tracks.store(list.get());p.versions.push_back(std::move(list));}
}
Pattern SequencerEngine::pattern(int index) const
{
    Pattern p;const auto& a=patterns.at((size_t)index);const auto* list=a.tracks.load();p.lanes.resize(list->size());p.lengths.resize(list->size());p.speeds.resize(list->size());p.enabled.resize(list->size());p.rotations.resize(list->size());
    for(size_t i=0;i<list->size();++i){auto& lane=*(*list)[i];p.lanes[i]=lane.bits.load();p.lengths[i]=lane.length.load();p.speeds[i]=lane.speed.load();p.enabled[i]=lane.enabled.load();p.rotations[i]=lane.rotation.load();}
    p.swing=a.swing.load();p.barTiming=a.barTiming.load();return p;
}
void SequencerEngine::setPattern(int index,const Pattern& data)
{
    ensureLaneCount((int)data.lanes.size());auto& p=patterns.at((size_t)index);auto* list=p.tracks.load();
    for(size_t i=0;i<list->size();++i){auto& lane=*(*list)[i];lane.bits.store(i<data.lanes.size()?data.lanes[i]:0);lane.length.store(i<data.lengths.size()?std::clamp(data.lengths[i],1,32):16);lane.speed.store(i<data.speeds.size()?(std::isfinite(data.speeds[i])?std::clamp(data.speeds[i],.125,8.0):1.0):1.0);lane.enabled.store(i<data.enabled.size()?data.enabled[i]:0);lane.rotation.store(i<data.rotations.size()?data.rotations[i]:0);}
    p.swing.store(std::isfinite(data.swing)?std::clamp(data.swing,50.0,75.0):50.0);p.barTiming.store(data.barTiming);
}
SequenceState SequencerEngine::state() const
{
    SequenceState s;for(int i=0;i<patternCount;++i)s.patterns[(size_t)i]=pattern(i);s.mode=getMode();s.hold=getHold();return s;
}
void SequencerEngine::restore(const SequenceState& s)
{
    requestStop();entryState.fetch_add(uint64_t(1)<<32);entryState.fetch_and(~uint64_t(0x1ffff));
    for(int p=0;p<patternCount;++p)setPattern(p,s.patterns[(size_t)p]);
    setMode((PlayMode)std::clamp((int)s.mode,0,2));setHold(s.hold);
}
void SequencerEngine::setStep(int p,int lane,int step,bool enabled)
{
    if(p<0||p>=patternCount||lane<0||lane>=(int)patterns[(size_t)p].tracks.load()->size()||step<0||step>=32)return;
    auto& bits=(*patterns[(size_t)p].tracks.load())[(size_t)lane]->bits;const uint32_t mask=uint32_t(1)<<step;
    if(enabled)bits.fetch_or(mask);else bits.fetch_and(~mask);
}
void SequencerEngine::setLength(int p,int n){for(int lane=0;lane<(int)patterns[(size_t)p].tracks.load()->size();++lane)setLaneLength(p,lane,n);}
void SequencerEngine::setLaneLength(int p,int lane,int n){patterns.at((size_t)p).tracks.load()->at((size_t)lane)->length.store(std::clamp(n,1,32));}
void SequencerEngine::setSwing(int p,double v){patterns.at((size_t)p).swing.store(std::clamp(std::isfinite(v)?v:50.0,50.0,75.0));}
int SequencerEngine::entryRange(int p) const
{int range=1;for(auto* lane:*patterns[(size_t)p].tracks.load())if(!patterns[(size_t)p].barTiming.load()||lane->enabled.load()||lane->bits.load())range=std::max(range,lane->length.load());return range;}
Entry SequencerEngine::entry() const
{
    const auto value=entryState.load();Entry e;e.armed=(value&65536u)!=0;e.pattern=(int)((value>>8)&15u);e.range=entryRange(e.pattern);e.step=std::min((int)(value&63u),e.range-1);return e;
}
void SequencerEngine::selectEditPattern(int p)
{
    if(p<0||p>=patternCount)return;
    auto old=entryState.load();do{if((int)((old>>8)&15u)==p)return;}
    while(!entryState.compare_exchange_weak(old,((old+(uint64_t(1)<<32))&~uint64_t(0x1ffff))|packEntry((old&65536u)!=0,p,0)));
}
void SequencerEngine::armStepRecord(bool on)
{
    auto old=entryState.load();uint64_t next;
    do{next=((old+(uint64_t(1)<<32))&~uint64_t(0x1ffff))|packEntry(on,(int)((old>>8)&15u),on?0:(int)(old&63u));}while(!entryState.compare_exchange_weak(old,next));
}
void SequencerEngine::setEntryStep(int step)
{
    auto old=entryState.load();uint64_t next;
    do{const int p=(int)((old>>8)&15u);next=((old+(uint64_t(1)<<32))&~uint64_t(0x1ffff))|packEntry((old&65536u)!=0,p,std::clamp(step,0,entryRange(p)-1));}
    while(!entryState.compare_exchange_weak(old,next));
}
void SequencerEngine::moveEntry(int delta)
{
    auto old=entryState.load();uint64_t next;
    do{const int p=(int)((old>>8)&15u);const int n=entryRange(p);next=((old+(uint64_t(1)<<32))&~uint64_t(0x1ffff))|packEntry((old&65536u)!=0,p,std::clamp(std::min((int)(old&63u),n-1)+delta,0,n-1));}
    while(!entryState.compare_exchange_weak(old,next));
}
void SequencerEngine::clearEntryStep()
{
    const auto e=entry();for(int lane=0;lane<(int)patterns[(size_t)e.pattern].tracks.load()->size();++lane)if(e.step<patterns[(size_t)e.pattern].tracks.load()->at((size_t)lane)->length.load())setStep(e.pattern,lane,e.step,false);
}
void SequencerEngine::recordSound(int lane,const Entry& e)
{
    if(e.armed&&lane>=0&&e.pattern>=0&&e.pattern<patternCount&&lane<(int)patterns[(size_t)e.pattern].tracks.load()->size()&&e.step>=0&&e.step<patterns[(size_t)e.pattern].tracks.load()->at((size_t)lane)->length.load())setStep(e.pattern,lane,e.step,true);
}
void SequencerEngine::resetRecordGroup(){entryHeld.fill(0);groupActive=false;}
void SequencerEngine::recordKey(int lane,int note,int channel,bool down)
{
    if(note<0||note>127||channel<0||channel>15)return;
    const auto token=entryState.load();
    if(groupActive&&groupToken!=token)resetRecordGroup();
    if((token&65536u)==0){resetRecordGroup();return;}
    const auto bit=(uint16_t)(uint16_t(1)<<channel);
    if(down)
    {
        if(!groupActive){groupActive=true;groupToken=token;groupEntry={true,(int)((token>>8)&15u),std::min((int)(token&63u),entryRange((int)((token>>8)&15u))-1),entryRange((int)((token>>8)&15u))};}
        entryHeld[(size_t)note]|=bit;recordSound(lane,groupEntry);
    }
    else if(groupActive)
    {
        entryHeld[(size_t)note]&=(uint16_t)~bit;
        for(auto heldNote:entryHeld)if(heldNote!=0)return;
        auto expected=groupToken;
        const int next=std::min(groupEntry.step+1,entryRange(groupEntry.pattern)-1);
        const auto value=((groupToken+(uint64_t(1)<<32))&~uint64_t(0x1ffff))|packEntry(true,groupEntry.pattern,next);
        entryState.compare_exchange_strong(expected,value);resetRecordGroup();
    }
}
void SequencerEngine::skipEntry()
{
auto old=entryState.load();uint64_t next;Entry target;
do{target={bool(old&65536u),(int)((old>>8)&15u),(int)(old&63u),0};target.range=entryRange(target.pattern);target.step=std::min(target.step,target.range-1);
    next=((old+(uint64_t(1)<<32))&~uint64_t(0x1ffff))|packEntry(target.armed,target.pattern,std::min(target.step+1,target.range-1));}
while(!entryState.compare_exchange_weak(old,next));
for(int lane=0;lane<(int)patterns[(size_t)target.pattern].tracks.load()->size();++lane)if(target.step<patterns[(size_t)target.pattern].tracks.load()->at((size_t)lane)->length.load())setStep(target.pattern,lane,target.step,false);
}
void SequencerEngine::setUiHeld(int p,int source,bool down)
{
    if(p<0||p>=patternCount||source<0||source>1)return;auto& bits=uiHeld[(size_t)p];
    if(down)bits.fetch_or(uint32_t(1)<<source);else bits.fetch_and(~(uint32_t(1)<<source));
}
void SequencerEngine::clearUiHeld(){for(auto& v:uiHeld)v.store(0);}
void SequencerEngine::key(int p,uint64_t mask,bool down)
{
    auto& bits=held[(size_t)p];const bool before=bits!=0;if(down)bits|=mask;else bits&=~mask;if(!before&&bits!=0)order[(size_t)p]=++serial;
}
void SequencerEngine::midiKey(int p,int channel,bool down,bool alternate)
{if(p>=0&&p<patternCount&&channel>=0&&channel<16)key(p,uint64_t(1)<<(channel+(alternate?18:0)),down);}
void SequencerEngine::stop()
{
    resetRecordGroup();joined=0;held.fill(0);order.fill(0);clearUiHeld();primary=secondary=-1;secondaryWasQueued=false;
    for(auto& p:patterns)for(auto* lane:*p.tracks.load())lane->shown.store(-1);publish();
}
bool SequencerEngine::beginBlock(double q,double delta,bool hostPlaying)
{
    const bool panic=stopRequested.exchange(false)||(playing&&!hostPlaying);
    playing=hostPlaying;clockValid=std::isfinite(q)&&std::isfinite(delta)&&delta>0;
    if(panic)stop();
    if(!expectedValid||std::abs(q-expectedQ)>1e-5)lastQ=q-1e-7;
    expectedQ=q;sampleAdvance=delta;expectedValid=clockValid;
    for(int i=0;i<patternCount;++i)
    {const auto bits=uiHeld[(size_t)i].load();key(i,uint32_t(1)<<16,(bits&1)!=0);key(i,uint32_t(1)<<17,(bits&2)!=0);}
    publish();return panic;
}
int SequencerEngine::oldestHeld() const
{int p=-1;for(int i=0;i<patternCount;++i)if(held[(size_t)i]&&(p<0||order[(size_t)i]<order[(size_t)p]))p=i;return p;}
int SequencerEngine::newestOther(int exclude) const
{int p=-1;for(int i=0;i<patternCount;++i)if(i!=exclude&&held[(size_t)i]&&(p<0||order[(size_t)i]>order[(size_t)p]))p=i;return p;}
void SequencerEngine::transition(double boundary)
{
const auto previousMode=activeMode;activeMode=getMode();
if(activeMode==PlayMode::join)
{
    uint32_t desired=0;for(int p=0;p<patternCount;++p)if(held[(size_t)p]||(p==primary&&getHold()))desired|=uint32_t(1)<<p;
    const uint32_t previous=previousMode==PlayMode::join?joined:0;
    for(int p=0;p<patternCount;++p)if((desired&(uint32_t(1)<<p))&&!(previous&(uint32_t(1)<<p)))
        joinStarts[(size_t)p]=previousMode!=PlayMode::join&&p==primary?primaryStart:previousMode!=PlayMode::join&&p==secondary&&!secondaryWasQueued?secondaryStart:boundary;
    joined=desired;
    if(primary<0||(desired&(uint32_t(1)<<primary))==0)primary=oldestHeld();
    secondary=primary>=0?newestOther(primary):-1;
    if(primary>=0)primaryStart=joinStarts[(size_t)primary];
    if(secondary>=0)secondaryStart=joinStarts[(size_t)secondary];
    secondaryWasQueued=false;publish();return;
}
if(previousMode==PlayMode::join)
{if(primary>=0)primaryStart=joinStarts[(size_t)primary];if(secondary>=0)secondaryStart=joinStarts[(size_t)secondary];}
joined=0;
    if(primary<0||(!held[(size_t)primary]&&!getHold()))
    {
        const int next=secondary>=0&&held[(size_t)secondary]?secondary:oldestHeld();
        const bool preserve=next>=0&&next==secondary&&!secondaryWasQueued;
        primary=next;primaryStart=preserve?secondaryStart:boundary;secondary=-1;
    }
    if(primary>=0){const int next=newestOther(primary);if(next!=secondary){secondary=next;secondaryStart=boundary;}}
    else secondary=-1;
    if(secondaryWasQueued&&secondary>=0&&activeMode!=PlayMode::replace)secondaryStart=boundary;
    secondaryWasQueued=secondary>=0&&activeMode==PlayMode::replace;publish();
}
void SequencerEngine::publish()
{
    shownPrimary.store(primary);shownSecondary.store(secondary);shownPlaying.store(playing);
    shownQueued.store(secondary>=0&&activeMode==PlayMode::replace);shownAudible.store(primary>=0&&!(secondary>=0&&activeMode==PlayMode::fill));
    uint32_t active=activeMode==PlayMode::join?joined:0;
if(activeMode!=PlayMode::join){if(primary>=0&&!(secondary>=0&&activeMode==PlayMode::fill))active|=uint32_t(1)<<primary;if(secondary>=0&&activeMode!=PlayMode::replace)active|=uint32_t(1)<<secondary;}
shownActive.store(active);
shownLatched.store(primary>=0&&getHold()&&!held[(size_t)primary]);uint32_t bits=0;
    for(int i=0;i<patternCount;++i)if(held[(size_t)i])bits|=uint32_t(1)<<i;shownHeld.store(bits);
}
Hits SequencerEngine::tick(double q,HitSink sink,void* context)
{
    Hits result;if(!clockValid)return result;
    if(std::floor(q+1e-8)!=std::floor(lastQ+1e-8))transition(std::floor(q+1e-8));
    auto perform=[&](int p,double anchor,bool audible){
if(p<0)return;const auto& data=patterns[(size_t)p];const auto* list=data.tracks.load();bool fired=false;
for(size_t lane=0;lane<list->size();++lane)
{
    const auto& track=*(*list)[lane];const int n=track.length.load();const double factor=track.speed.load()*(data.barTiming.load()?n/barLength.load():1.0);
    const auto position=(int64_t)std::floor((q-anchor)*factor+1e-8);const int step=(int)((position%n+n)%n);(*list)[lane]->shown.store(step);double phase=std::fmod((q-anchor)*factor/(double)n,1.0);(*list)[lane]->phase.store(phase<0?phase+1:phase);
    const double delay=step%2?2.0*(data.swing.load()/100.0-0.5):0.0;const double when=anchor+((double)position+delay)/factor;
    if(when>lastQ+1e-8&&when<=q+1e-8&&audible&&(track.bits.load()&(uint32_t(1)<<step))){Hit hit{p,(int)lane,step};if(result.count<(int)result.items.size())result.items[(size_t)result.count++]=hit;if(sink)sink(context,hit);fired=true;}
}
if(fired)fires[(size_t)p].fetch_add(1);
    };
if(activeMode==PlayMode::join){for(int p=0;p<patternCount;++p)if(joined&(uint32_t(1)<<p))perform(p,joinStarts[(size_t)p],true);}
else{perform(primary,primaryStart,!(secondary>=0&&activeMode==PlayMode::fill));perform(secondary,secondaryStart,activeMode!=PlayMode::replace);}
    lastQ=q;expectedQ=q+sampleAdvance;return result;
}
Performance SequencerEngine::performance() const
{
    Performance v;v.primary=shownPrimary.load();v.secondary=shownSecondary.load();v.queued=shownQueued.load();v.primaryAudible=shownAudible.load();v.playing=shownPlaying.load();v.latched=shownLatched.load();v.held=shownHeld.load();v.active=shownActive.load();
    for(int p=0;p<patternCount;++p){auto* list=patterns[(size_t)p].tracks.load();v.laneSteps[(size_t)p].resize(list->size());v.phases[(size_t)p].resize(list->size());for(size_t lane=0;lane<list->size();++lane){v.laneSteps[(size_t)p][lane]=(*list)[lane]->shown.load();v.phases[(size_t)p][lane]=(*list)[lane]->phase.load();}v.steps[(size_t)p]=v.laneSteps[(size_t)p].empty()?-1:v.laneSteps[(size_t)p][0];v.fires[(size_t)p]=fires[(size_t)p].load();}return v;
}
}
