#include "PluginEditor.h"
namespace {
const juce::Colour bg(0xff10131a),panel(0xff1d2430),ink(0xffedf1f7),dim(0xff8390a5),mint(0xff80dfc7);
juce::Colour colour(int i){return juce::Colour::fromHSV((float)std::fmod(.34+(double)i*.61803398875,1.0),.60f,.95f,1.0f);}
int countBits(uint32_t bits){int n=0;while(bits){n+=(int)(bits&1);bits>>=1;}return n;}
void fitPattern(finger::Pattern& p,int n){p.lanes.resize((size_t)n);p.lengths.resize((size_t)n,16);p.speeds.resize((size_t)n,1);p.enabled.resize((size_t)n,0);p.rotations.resize((size_t)n,0);}
}
FingerEditor::FingerEditor(FingerProcessor& ownerProcessor):AudioProcessorEditor(ownerProcessor),owner(ownerProcessor)
{
    setWantsKeyboardFocus(false);
    for(auto* b:std::array<juce::TextButton*,14>{&stepRecord,&skip,&performance,&sequence,&sounds,&plusBank,&saveBank,&openBank,&addSample,&addSound,&store,&newPattern,&load,&stretch}){addAndMakeVisible(b);b->setWantsKeyboardFocus(false);b->setColour(juce::TextButton::buttonColourId,panel);b->setColour(juce::TextButton::buttonOnColourId,mint);b->setColour(juce::TextButton::textColourOnId,bg);}
    stepRecord.setClickingTogglesState(true);stepRecord.onClick=[this]{owner.draftEntry.armStepRecord(stepRecord.getToggleState());refresh();};skip.onClick=[this]{owner.draftEntry.skipEntry();refresh();};
    performance.onClick=[this]{pageTo(0);};sequence.onClick=[this]{pageTo(1);};sounds.onClick=[this]{pageTo(2);};plusBank.onClick=[this]{owner.addMemory();rowScroll=sampleScroll=vaultScroll=0;selected=0;refresh();};
    saveBank.onClick=[this]{bankFile(false);};openBank.onClick=[this]{bankFile(true);};addSample.onClick=[this]{chooseSample(true);};load.onClick=[this]{chooseSample(false);};addSound.onClick=[this]{addLane();};
    store.onClick=[this]{owner.storeDraft();refresh();};newPattern.onClick=[this]{finger::Pattern p;fitPattern(p,owner.sampler.slotCount());p.lanes.assign(p.lanes.size(),0);p.enabled.assign(p.enabled.size(),0);p.barTiming=true;owner.setDraft(p);rowScroll=0;refresh();};
    addAndMakeVisible(banks);banks.setWantsKeyboardFocus(false);banks.onChange=[this]{if(!updating)selectBank(banks.getSelectedId()-1);};
    addAndMakeVisible(mode);mode.setWantsKeyboardFocus(false);mode.addItem("Join",1);mode.addItem("Replace",2);mode.addItem("Fill In",3);mode.onChange=[this]{if(!updating)owner.sequencer.setMode((finger::PlayMode)(mode.getSelectedId()-1));};
    auto slider=[this](juce::Slider& s,double lo,double hi,double inc,const char* suffix){addAndMakeVisible(s);s.setRange(lo,hi,inc);s.setTextValueSuffix(suffix);s.setTextBoxStyle(juce::Slider::TextBoxBelow,false,90,22);s.setColour(juce::Slider::thumbColourId,mint);s.onValueChange=[this]{editSample();};s.setWantsKeyboardFocus(false);};
    slider(start,0,99.999,.001,"%");slider(end,.001,100,.001,"%");slider(tune,-24,24,.01," st");slider(ratio,50,400,1,"%");stretch.setClickingTogglesState(true);stretch.onClick=[this]{editSample();};
    setSize(1310,850);refresh();startTimerHz(30);
}
FingerEditor::~FingerEditor(){stopTimer();owner.sequencer.clearUiHeld();if(fileWorker.joinable())fileWorker.join();}
void FingerEditor::resized()
{
    performance.setBounds(24,66,134,30);sequence.setBounds(166,66,190,30);sounds.setBounds(364,66,104,30);banks.setBounds(784,26,240,30);plusBank.setBounds(1032,26,112,30);mode.setBounds(1152,26,134,30);
    saveBank.setBounds(1006,66,136,30);openBank.setBounds(1150,66,136,30);newPattern.setBounds(584,196,142,30);addSound.setBounds(734,196,138,30);store.setBounds(880,196,210,30);addSample.setBounds(26,116,140,30);
    stepRecord.setBounds(584,782,130,28);skip.setBounds(722,782,90,28);
    load.setBounds(1034,594,252,30);start.setBounds(558,667,140,64);end.setBounds(706,667,140,64);tune.setBounds(854,667,140,64);ratio.setBounds(1002,667,140,64);stretch.setBounds(1150,682,136,30);
}
void FingerEditor::pageTo(int p){page=p;painting=dragging=false;dragged.reset();refresh();}
void FingerEditor::selectBank(int index){owner.switchMemory(index);rowScroll=sampleScroll=vaultScroll=0;selected=0;dragged.reset();refresh();}
std::vector<int> FingerEditor::rows() const{auto p=owner.draft();std::vector<int> result;for(size_t i=0;i<p.lanes.size();++i)if((i<p.enabled.size()&&p.enabled[i])||p.lanes[i])result.push_back((int)i);return result;}
juce::String FingerEditor::sampleName(int slot) const
{
    if(slot<0||slot>=owner.sampler.slotCount())return "Empty";auto path=owner.sampler.view(slot).settings.path;
    if(path.startsWith("factory:"))return path.fromFirstOccurrenceOf(":",false,false);return path.isEmpty()?"Sample "+juce::String(slot+1):juce::File(path).getFileNameWithoutExtension();
}
void FingerEditor::refresh()
{
    updating=true;performance.setToggleState(page==0,juce::dontSendNotification);sequence.setToggleState(page==1,juce::dontSendNotification);sounds.setToggleState(page==2,juce::dontSendNotification);
    auto names=owner.memoryNames();if(banks.getNumItems()!=names.size()){banks.clear(juce::dontSendNotification);for(int i=0;i<names.size();++i)banks.addItem(names[i],i+1);}banks.setSelectedId(owner.currentMemoryIndex()+1,juce::dontSendNotification);mode.setSelectedId((int)owner.sequencer.getMode()+1,juce::dontSendNotification);
    stepRecord.setVisible(page==1);skip.setVisible(page==1);stepRecord.setToggleState(owner.draftEntry.entry().armed,juce::dontSendNotification);newPattern.setVisible(page==1);addSound.setVisible(page==1);store.setVisible(page==1);addSample.setVisible(page==2);
    const bool sampleControls=page==2&&owner.sampler.slotCount()>0;for(auto* c:std::array<juce::Component*,6>{&load,&start,&end,&tune,&ratio,&stretch})c->setVisible(sampleControls);
    if(sampleControls){selected=juce::jlimit(0,owner.sampler.slotCount()-1,selected);auto s=owner.sampler.view(selected).settings;start.setValue(s.start*100,juce::dontSendNotification);end.setValue(s.end*100,juce::dontSendNotification);tune.setValue(s.tune,juce::dontSendNotification);ratio.setValue(s.ratio*100,juce::dontSendNotification);stretch.setToggleState(s.stretch,juce::dontSendNotification);ratio.setEnabled(s.stretch);}
    updating=false;resized();repaint();
}
void FingerEditor::timerCallback(){refresh();}
void FingerEditor::paintCircle(juce::Graphics& g,juce::Rectangle<float> area,const finger::Pattern& p,bool active,float alpha)
{
    std::vector<int> lanes;for(size_t i=0;i<p.lanes.size();++i)if(p.lanes[i]||(i<p.enabled.size()&&p.enabled[i]))lanes.push_back((int)i);
    auto centre=area.getCentre();float radius=area.getWidth()*.47f;const float gap=juce::jmin(28.0f,radius/juce::jmax(1.0f,(float)lanes.size()+1));
    for(size_t row=0;row<lanes.size();++row){int lane=lanes[row],n=p.lengths[(size_t)lane];float r=radius-gap*(float)row;auto c=colour(lane);g.setColour(c.withAlpha(alpha*(active?.7f:.2f)));g.drawEllipse(centre.x-r,centre.y-r,2*r,2*r,1);
        for(int step=0;step<n;++step){float angle=juce::MathConstants<float>::twoPi*(float)step/(float)n;auto point=centre+juce::Point<float>(std::sin(angle)*r,-std::cos(angle)*r);bool hit=(p.lanes[(size_t)lane]&(uint32_t(1)<<step))!=0;g.setColour(c.withAlpha(alpha*(hit?(active?1.0f:.65f):.18f)));float dot=juce::jlimit(2.0f,6.0f,area.getWidth()*.012f);g.fillEllipse(point.x-dot,point.y-dot,dot*2,dot*2);}
        if(active){double speed=p.speeds[(size_t)lane];double duration=p.barTiming?owner.sequencer.barLength.load()/speed:n/speed;float angle=(float)std::fmod(owner.clockSixteenths.load()/duration,1.0)*juce::MathConstants<float>::twoPi;g.setColour(c.withAlpha(alpha));g.drawLine(centre.x,centre.y,centre.x+std::sin(angle)*r,centre.y-std::cos(angle)*r,area.getWidth()>200?2.0f:1.0f);}
    }
    if(lanes.empty()){g.setColour(dim.withAlpha(alpha));g.drawEllipse(area.reduced(12),1);g.setFont(area.getWidth()>200?17.0f:10.0f);g.drawText("Empty",area,juce::Justification::centred);}
}
void FingerEditor::paint(juce::Graphics& g)
{
    g.fillAll(bg);g.setColour(ink);g.setFont(23);g.drawText("FINGER / EUCLIDEAN   0.7.0",24,24,650,32,juce::Justification::left);
    const auto perf=owner.sequencer.performance();
    if(page==0){g.setColour(dim);g.setFont(13);g.drawText("Hold MIDI pattern keys to light their rings. Select a memory above.",24,122,1262,28,juce::Justification::centred);
        auto area=juce::Rectangle<float>(330,184,650,650);int rings=0;for(int key=0;key<finger::patternCount;++key){auto p=owner.sequencer.pattern(key);for(size_t lane=0;lane<p.lanes.size();++lane)if(p.lanes[lane]||(lane<p.enabled.size()&&p.enabled[lane]))++rings;}
        float radius=300,spacing=juce::jmin(18.0f,270.0f/(float)juce::jmax(1,rings));auto centre=area.getCentre();int row=0;
        for(int key=0;key<finger::patternCount;++key){auto p=owner.sequencer.pattern(key);bool active=(perf.active&(uint32_t(1)<<key))!=0;bool queued=perf.queued&&perf.secondary==key;for(size_t lane=0;lane<p.lanes.size();++lane)if(p.lanes[lane]||(lane<p.enabled.size()&&p.enabled[lane])){float r=radius-spacing*(float)row++;auto c=colour((int)lane);g.setColour(queued?juce::Colours::orange.withAlpha(.5f):c.withAlpha(active?.95f:.25f));g.drawEllipse(centre.x-r,centre.y-r,2*r,2*r,active?3:1);for(int step=0;step<p.lengths[lane];++step)if(p.lanes[lane]&(uint32_t(1)<<step)){float a=(float)step/(float)p.lengths[lane]*juce::MathConstants<float>::twoPi;g.fillEllipse(centre.x+std::sin(a)*r-3,centre.y-std::cos(a)*r-3,6,6);}if(active){float a=(float)perf.phases[(size_t)key][lane]*juce::MathConstants<float>::twoPi;g.drawLine(centre.x,centre.y,centre.x+std::sin(a)*r,centre.y-std::cos(a)*r,1);}}
            g.setColour(queued?juce::Colours::orange:active?mint:dim);g.setFont(15);g.drawText(juce::String(finger::MidiMapping::patternKeys[(size_t)key])+(active?"  playing":queued?"  queued":""),46,230+key*43,185,32,juce::Justification::left);}
        if(rings==0){g.setColour(dim);g.drawText("Empty memory — add samples, then make a sequence",area,juce::Justification::centred);}
    }
if(page==1){for(int key=0;key<finger::patternCount;++key){auto r=finger::layout::pattern(key);bool active=(perf.active&(uint32_t(1)<<key))!=0;g.setColour(active?mint.withAlpha(.2f):panel);g.fillRoundedRectangle(r.toFloat(),6);g.setColour(active?mint:ink);g.setFont(20);g.drawText(finger::MidiMapping::patternKeys[(size_t)key],r.withHeight(30),juce::Justification::centred);g.setFont(10);g.setColour(dim);g.drawText("Drop sequence",r.withTrimmedTop(30),juce::Justification::centred);}
        auto draft=owner.draft();paintCircle(g,finger::layout::maker().toFloat(),draft,true);g.setColour(dim);g.setFont(12);g.drawText("Drag the large circle to a key or the vault",28,756,530,24,juce::Justification::centred);
        auto list=rows();g.saveState();g.reduceClipRegion({20,238,1070,510});for(int row=rowScroll;row<(int)list.size()&&row<rowScroll+8;++row){int lane=list[(size_t)row];auto r=finger::layout::row(row-rowScroll);g.setColour(panel);g.fillRoundedRectangle(r.toFloat(),5);g.setColour(colour(lane));g.setFont(12);g.drawText(sampleName(lane),r.getX()+8,r.getY()+3,250,20,juce::Justification::left);g.setColour(dim);g.setFont(10);g.drawText("SPEED",24,r.getY()+6,58,13,juce::Justification::left);g.setColour(colour(lane));g.setFont(13);g.drawText(juce::String(draft.speeds[(size_t)lane],2)+"x",24,r.getY()+24,58,22,juce::Justification::left);
            int n=draft.lengths[(size_t)lane];float w=320.0f/(float)n;for(int step=0;step<n;++step){bool hit=(draft.lanes[(size_t)lane]&(uint32_t(1)<<step))!=0;g.setColour(colour(lane).withAlpha(hit?1.0f:.18f));g.fillEllipse((float)r.getX()+64+w*(float)step+2,(float)r.getY()+33,juce::jmin(9.0f,w-2),9);if(owner.draftEntry.entry().armed&&owner.draftEntry.entry().step==step){g.setColour(ink);g.drawRect((float)r.getX()+64+w*(float)step,(float)r.getY()+29,juce::jmin(13.0f,w),17.0f,1.0f);}}
            g.setColour(dim);g.drawText("STEPS  HITS  ROT",r.getX()+389,r.getY()+4,114,17,juce::Justification::left);g.setColour(ink);g.setFont(13);g.drawText(juce::String(n)+"    "+juce::String(countBits(draft.lanes[(size_t)lane]))+"    "+juce::String(draft.rotations[(size_t)lane]),r.getX()+389,r.getY()+27,114,24,juce::Justification::left);
        }g.restoreState();g.setColour(dim);g.setFont(11);g.drawText("Drag values up/down. First dot = step 1. Scroll for more sounds.",584,755,506,24,juce::Justification::left);
        g.setColour(dim);g.setFont(11);auto entry=owner.draftEntry.entry();g.drawText("Step "+juce::String(entry.step+1)+" / "+juce::String(entry.range)+" - release chord to advance",828,782,264,28,juce::Justification::left);auto bank=owner.currentMemory();auto vault=finger::layout::vault();g.setColour(panel);g.fillRoundedRectangle(vault.toFloat(),7);g.setColour(ink);g.setFont(14);g.drawText("PATTERN VAULT",vault.withHeight(34),juce::Justification::centred);
        g.saveState();g.reduceClipRegion(vault.withTrimmedTop(38));for(int i=vaultScroll;i<(int)bank.vault.size()&&i<vaultScroll+5;++i){auto r=juce::Rectangle<float>((float)vault.getX()+44,(float)vault.getY()+42+(float)(i-vaultScroll)*104,86,86);paintCircle(g,r,bank.vault[(size_t)i].pattern,false);g.setColour(dim);g.setFont(10);g.drawText(bank.vault[(size_t)i].name,juce::Rectangle<float>((float)vault.getX()+8,r.getBottom()+3,(float)vault.getWidth()-16,14),juce::Justification::centred);}g.restoreState();
        if(bank.vault.empty()){g.setColour(dim);g.setFont(12);g.drawFittedText("Drop a sequence here to store it. Drag a circle to a key; double-click to edit.",vault.reduced(16).withTrimmedTop(40),juce::Justification::topLeft,5);}
    }
    if(page==2){g.setColour(ink);g.setFont(13);g.drawText(juce::String(owner.sampler.slotCount())+" samples in this memory — scroll to browse",184,116,800,30,juce::Justification::left);g.saveState();g.reduceClipRegion({24,154,1086,408});int count=owner.sampler.slotCount();for(int i=sampleScroll*4;i<count&&i<sampleScroll*4+12;++i){auto r=finger::layout::sound(i-sampleScroll*4);g.setColour(panel);g.fillRoundedRectangle(r.toFloat(),7);if(i==selected){g.setColour(colour(i));g.drawRoundedRectangle(r.toFloat(),7,2);}g.setColour(colour(i));g.setFont(18);g.drawText(sampleName(i),r.reduced(12).withHeight(32),juce::Justification::left);g.setFont(11);g.setColour(dim);g.drawText(owner.sampler.view(i).status,r.reduced(12).withTrimmedTop(38).withHeight(24),juce::Justification::left);g.drawText(i<7?juce::String(finger::MidiMapping::soundKeys[(size_t)i])+" / click to audition":"Click to audition / drop WAV",r.reduced(12).withTrimmedTop(67),juce::Justification::left);}g.restoreState();if(count>0)paintWave(g);else{g.setColour(dim);g.drawText("This memory is empty. Add its first sample.",30,230,1050,60,juce::Justification::centred);}}
    if(!message.isEmpty()){g.setColour(mint);g.setFont(12);g.drawText(message,28,812,1258,25,juce::Justification::left);}
    if(dragging&&dragged){auto mouse=getMouseXYRelative().toFloat();g.setColour(mint.withAlpha(.16f));g.fillRoundedRectangle(mouse.x-45,mouse.y-45,90,90,10);paintCircle(g,{mouse.x-42,mouse.y-42,84,84},*dragged,false);}
}
void FingerEditor::paintWave(juce::Graphics& g)
{
    auto pad=owner.sampler.view(selected);auto wave=finger::layout::waveform();g.setColour(ink);g.setFont(14);g.drawText(sampleName(selected)+" / "+pad.status,28,588,970,32,juce::Justification::left);g.setColour(panel);g.fillRoundedRectangle(wave.toFloat(),6);
    if(pad.source){auto& audio=pad.source->audio;for(int x=0;x<wave.getWidth();++x){int a=x*audio.getNumSamples()/wave.getWidth(),b=juce::jmin(audio.getNumSamples(),juce::jmax(a+1,(x+1)*audio.getNumSamples()/wave.getWidth()));auto range=juce::FloatVectorOperations::findMinAndMax(audio.getReadPointer(0)+a,b-a);g.setColour(colour(selected));g.drawVerticalLine(wave.getX()+x,(float)wave.getCentreY()-range.getEnd()*48,(float)wave.getCentreY()-range.getStart()*48+1);}}
    int a=wave.getX()+(int)(pad.settings.start*wave.getWidth()),b=wave.getX()+(int)(pad.settings.end*wave.getWidth());g.setColour(mint);g.drawVerticalLine(a,(float)wave.getY(),(float)wave.getBottom());g.drawVerticalLine(b-1,(float)wave.getY(),(float)wave.getBottom());g.setFont(11);g.drawText("START",a+4,wave.getY()+2,60,20,juce::Justification::left);g.drawText("END",b-38,wave.getBottom()-24,34,20,juce::Justification::right);
    g.setColour(dim);g.drawText("Start",558,640,140,24,juce::Justification::left);g.drawText("End",706,640,140,24,juce::Justification::left);g.drawText("Tune / semitones",854,640,140,24,juce::Justification::left);g.drawText("Stretch / duration",1002,640,140,24,juce::Justification::left);
}
void FingerEditor::editSample()
{if(updating||owner.sampler.slotCount()==0)return;auto s=owner.sampler.view(selected).settings;s.start=start.getValue()/100;s.end=end.getValue()/100;if(s.start>=s.end)s.start=juce::jmax(0.,s.end-.00001);s.tune=tune.getValue();s.ratio=ratio.getValue()/100;s.stretch=stretch.getToggleState();owner.configureSample(selected,s);}
int FingerEditor::sampleAt(juce::Point<int> p) const{if(page!=2||p.y<154||p.y>=562)return -1;for(int i=sampleScroll*4;i<owner.sampler.slotCount()&&i<sampleScroll*4+12;++i)if(finger::layout::sound(i-sampleScroll*4).contains(p))return i;return -1;}
void FingerEditor::loadSample(const juce::File& file,int slot)
{if(slot<0||slot>=owner.sampler.slotCount())return;auto view=owner.sampler.view(slot);auto s=view.settings;s.path=file.getFullPathName();if(!view.status.startsWith("Missing")){s.start=0;s.end=1;}owner.configureSample(slot,s,true);selected=slot;refresh();}
void FingerEditor::chooseSample(bool adding)
{
    chooser=std::make_unique<juce::FileChooser>(adding?"Add WAV to this memory":"Load / Relink WAV",juce::File(),"*.wav");auto safe=juce::Component::SafePointer<FingerEditor>(this);const int slot=selected;
    chooser->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles,[safe,adding,slot](const juce::FileChooser& c){if(!safe||c.getResult()==juce::File())return;if(adding){safe->selected=safe->owner.addSample(c.getResult().getFullPathName());safe->sampleScroll=safe->selected/4;safe->refresh();}else safe->loadSample(c.getResult(),slot);});
}
void FingerEditor::addLane()
{
    juce::PopupMenu menu;auto p=owner.draft();for(int i=0;i<owner.sampler.slotCount();++i)if((size_t)i>=p.enabled.size()||!p.enabled[(size_t)i])menu.addItem(i+1,sampleName(i));auto safe=juce::Component::SafePointer<FingerEditor>(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&addSound),[safe](int i){if(!safe||i<=0)return;auto selectedPattern=safe->owner.draft();fitPattern(selectedPattern,safe->owner.sampler.slotCount());selectedPattern.enabled[(size_t)i-1]=1;selectedPattern.barTiming=true;safe->owner.setDraft(selectedPattern);safe->refresh();});
}
void FingerEditor::updateMacro(int lane,int type,double value)
{
    auto p=owner.draft();if(lane<0||(size_t)lane>=p.lanes.size())return;int n=p.lengths[(size_t)lane],hits=countBits(p.lanes[(size_t)lane]);p.barTiming=true;
    if(type==0)p.speeds[(size_t)lane]=juce::jlimit(.125,8.0,std::round(value*8)/8);
    int rotation=p.rotations[(size_t)lane];
    if(type==1){p.lengths[(size_t)lane]=juce::jlimit(1,32,(int)std::round(value));p.rotations[(size_t)lane]=rotation%p.lengths[(size_t)lane];p.lanes[(size_t)lane]=finger::euclidean(p.lengths[(size_t)lane],juce::jmin(hits,p.lengths[(size_t)lane]),rotation);}
    if(type==2)p.lanes[(size_t)lane]=finger::euclidean(n,juce::jlimit(0,n,(int)std::round(value)),rotation);
    if(type==3){p.rotations[(size_t)lane]=((int)std::round(value)%n+n)%n;p.lanes[(size_t)lane]=finger::euclidean(n,hits,p.rotations[(size_t)lane]);}owner.setDraft(p);repaint();
}
void FingerEditor::mouseDown(const juce::MouseEvent& e)
{
    marker=0;painting=false;macroLane=-1;dragging=false;dragged.reset();mouseStart=e.getPosition();
if(page==1&&e.x>=20&&e.x<84&&e.y>=242&&e.y<738){auto list=rows();int row=rowScroll+(e.y-242)/62;if(row<(int)list.size()){macroLane=list[(size_t)row];macroType=0;macroStart=owner.draft().speeds[(size_t)macroLane];return;}}
    if(page==1){for(int key=0;key<finger::patternCount;++key)if(finger::layout::pattern(key).contains(e.getPosition())){if(e.mods.isRightButtonDown()){owner.setDraft(owner.sequencer.pattern(key));rowScroll=0;}else owner.sequencer.setUiHeld(key,0,!owner.sequencer.isUiHeld(key));refresh();return;}
        if(finger::layout::maker().contains(e.getPosition())){dragged=owner.draft();return;}
        auto vault=finger::layout::vault();if(vault.contains(e.getPosition())){int i=vaultScroll+(e.y-vault.getY()-42)/104;auto bank=owner.currentMemory();if(e.y>=vault.getY()+42&&i>=0&&i<(int)bank.vault.size())dragged=bank.vault[(size_t)i].pattern;return;}
        auto list=rows();for(int row=rowScroll;row<(int)list.size()&&row<rowScroll+8;++row){auto r=finger::layout::row(row-rowScroll);if(!r.contains(e.getPosition()))continue;int lane=list[(size_t)row];auto p=owner.draft();if(e.y<r.getY()+25){if(e.mods.isRightButtonDown()){p.enabled[(size_t)lane]=0;p.lanes[(size_t)lane]=0;owner.setDraft(p);refresh();}else owner.sampler.requestTrigger(lane);return;}
            int x=e.x-r.getX();if(x>=64&&x<384){int step=juce::jlimit(0,p.lengths[(size_t)lane]-1,(x-64)*p.lengths[(size_t)lane]/320);paintValue=!e.mods.isRightButtonDown()&&(p.lanes[(size_t)lane]&(uint32_t(1)<<step))==0;paintLane=lane;lastPaintStep=step;painting=true;if(paintValue)p.lanes[(size_t)lane]|=uint32_t(1)<<step;else p.lanes[(size_t)lane]&=~(uint32_t(1)<<step);owner.setDraft(p);repaint();return;}
            macroLane=lane;macroType=x<64?0:x<429?1:x<467?2:3;macroStart=macroType==0?p.speeds[(size_t)lane]:macroType==1?p.lengths[(size_t)lane]:macroType==2?countBits(p.lanes[(size_t)lane]):p.rotations[(size_t)lane];return;
        }
    }
    if(page==2){int slot=sampleAt(e.getPosition());if(slot>=0){selected=slot;owner.sampler.requestTrigger(slot);refresh();return;}auto wave=finger::layout::waveform();if(owner.sampler.slotCount()>0&&wave.expanded(4,0).contains(e.getPosition())){auto s=owner.sampler.view(selected).settings;double x=(double)(e.x-wave.getX())/wave.getWidth();marker=std::abs(x-s.start)<std::abs(x-s.end)?1:2;mouseDrag(e);}}
}
void FingerEditor::mouseDrag(const juce::MouseEvent& e)
{
    if(dragged){dragging=e.getDistanceFromDragStart()>4;repaint();return;}
    if(macroLane>=0){double scale=macroType==0?.025:.15;updateMacro(macroLane,macroType,macroStart+(mouseStart.y-e.y)*scale);return;}
    if(painting){auto p=owner.draft();auto list=rows();auto where=std::find(list.begin(),list.end(),paintLane);if(where==list.end())return;auto r=finger::layout::row((int)(where-list.begin())-rowScroll);int step=juce::jlimit(0,p.lengths[(size_t)paintLane]-1,(e.x-r.getX()-64)*p.lengths[(size_t)paintLane]/320);for(int i=juce::jmin(step,lastPaintStep);i<=juce::jmax(step,lastPaintStep);++i){if(paintValue)p.lanes[(size_t)paintLane]|=uint32_t(1)<<i;else p.lanes[(size_t)paintLane]&=~(uint32_t(1)<<i);}lastPaintStep=step;owner.setDraft(p);repaint();return;}
    if(marker&&owner.sampler.slotCount()>0){auto s=owner.sampler.view(selected).settings;auto wave=finger::layout::waveform();double pos=juce::jlimit(0.,1.,(double)(e.x-wave.getX())/wave.getWidth());if(marker==1)s.start=juce::jmin(pos,s.end-.00001);else s.end=juce::jmax(pos,s.start+.00001);owner.configureSample(selected,s);refresh();}
}
void FingerEditor::mouseUp(const juce::MouseEvent& e)
{
    if(dragging&&dragged){bool assigned=false;for(int key=0;key<finger::patternCount;++key)if(finger::layout::pattern(key).contains(e.getPosition())){owner.sequencer.setPattern(key,*dragged);message="Assigned sequence to "+juce::String(finger::MidiMapping::patternKeys[(size_t)key]);assigned=true;break;}
        if(!assigned&&finger::layout::vault().contains(e.getPosition())){auto current=owner.draft();owner.setDraft(*dragged);owner.storeDraft();owner.setDraft(current);message="Stored in pattern vault";}
        else if(!assigned&&finger::layout::maker().contains(e.getPosition())){owner.setDraft(*dragged);rowScroll=0;message="Loaded into maker";}}
    marker=0;macroLane=-1;painting=dragging=false;dragged.reset();refresh();
}
void FingerEditor::mouseDoubleClick(const juce::MouseEvent& e)
{if(page==2){int slot=sampleAt(e.getPosition());if(slot>=0){selected=slot;chooseSample(false);}}if(page==1){auto v=finger::layout::vault();if(v.contains(e.getPosition())){int i=vaultScroll+(e.y-v.getY()-42)/104;owner.loadVault(i);rowScroll=0;refresh();}}}
void FingerEditor::mouseWheelMove(const juce::MouseEvent& e,const juce::MouseWheelDetails& wheel)
{int delta=wheel.deltaY<0?1:-1;if(page==2)sampleScroll=juce::jlimit(0,juce::jmax(0,(owner.sampler.slotCount()+3)/4-3),sampleScroll+delta);if(page==1){if(finger::layout::vault().contains(e.getPosition()))vaultScroll=juce::jlimit(0,juce::jmax(0,(int)owner.currentMemory().vault.size()-5),vaultScroll+delta);else rowScroll=juce::jlimit(0,juce::jmax(0,(int)rows().size()-8),rowScroll+delta);}repaint();}
bool FingerEditor::keyPressed(const juce::KeyPress& key){if(key.getKeyCode()==juce::KeyPress::escapeKey){owner.sequencer.requestStop();return true;}return false;}
bool FingerEditor::isInterestedInFileDrag(const juce::StringArray& files){for(auto& file:files)if(!juce::File(file).hasFileExtension("wav"))return false;return !files.isEmpty();}
void FingerEditor::filesDropped(const juce::StringArray& files,int x,int y)
{if(!isInterestedInFileDrag(files))return;int slot=sampleAt({x,y});if(files.size()==1&&slot>=0)loadSample(juce::File(files[0]),slot);else for(auto& file:files)selected=owner.addSample(file);refresh();}
void FingerEditor::bankFile(bool importing)
{
    if(fileWorker.joinable())fileWorker.join();chooser=std::make_unique<juce::FileChooser>(importing?"Open memory bank.xml":"Choose a folder to save this memory",juce::File(),importing?"*.xml":"*");auto safe=juce::Component::SafePointer<FingerEditor>(this);
    chooser->launchAsync(juce::FileBrowserComponent::openMode|(importing?juce::FileBrowserComponent::canSelectFiles:juce::FileBrowserComponent::canSelectDirectories),[safe,importing](const juce::FileChooser& c){if(!safe||c.getResult()==juce::File())return;auto path=c.getResult();auto snapshot=safe->owner.currentMemory();safe->message=importing?"Loading memory...":"Saving memory...";
        safe->fileWorker=std::thread([safe,importing,path,savedBank=std::move(snapshot)]() mutable {try{if(importing){auto imported=importMemory(path);juce::MessageManager::callAsync([safe,loaded=std::move(imported)]() mutable {if(safe){safe->owner.installMemory(std::move(loaded));safe->message="Memory loaded";safe->selected=safe->rowScroll=safe->sampleScroll=safe->vaultScroll=0;safe->refresh();}});}else{auto result=exportMemory(std::move(savedBank),path);juce::MessageManager::callAsync([safe,text=result.wasOk()?juce::String("Memory saved"):result.getErrorMessage()]{if(safe){safe->message=text;safe->repaint();}});}}catch(const std::exception& error){juce::MessageManager::callAsync([safe,text=juce::String(error.what())]{if(safe){safe->message=text;safe->repaint();}});}});
    });
}
