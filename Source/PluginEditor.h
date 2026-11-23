#pragma once
#include "PluginProcessor.h"
#include "FingerLayout.h"
#include "Euclidean.h"
#include <juce_audio_utils/juce_audio_utils.h>
class FingerEditor final:public juce::AudioProcessorEditor,public juce::FileDragAndDropTarget,private juce::Timer {
public:
    explicit FingerEditor(FingerProcessor&);~FingerEditor() override;
    void paint(juce::Graphics&) override;void resized() override;
    void mouseDown(const juce::MouseEvent&) override;void mouseDrag(const juce::MouseEvent&) override;void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;void mouseWheelMove(const juce::MouseEvent&,const juce::MouseWheelDetails&) override;
    bool keyPressed(const juce::KeyPress&) override;
    bool isInterestedInFileDrag(const juce::StringArray&) override;void filesDropped(const juce::StringArray&,int,int) override;
private:
    void timerCallback() override;void refresh();void pageTo(int);void paintCircle(juce::Graphics&,juce::Rectangle<float>,const finger::Pattern&,bool,float alpha=1);
    void paintWave(juce::Graphics&);void chooseSample(bool adding);void loadSample(const juce::File&,int);void editSample();void addLane();void bankFile(bool importing);
    std::vector<int> rows() const;int sampleAt(juce::Point<int>) const;juce::String sampleName(int) const;void updateMacro(int,int,double);void selectBank(int);
    FingerProcessor& owner;int page=0,selected=0,rowScroll=0,sampleScroll=0,vaultScroll=0,marker=0;
    bool updating=false,dragging=false,painting=false,paintValue=false;int paintLane=-1,lastPaintStep=-1,macroLane=-1,macroType=-1;
    juce::Point<int> mouseStart;double macroStart=0;std::optional<finger::Pattern> dragged;
    juce::TextButton stepRecord {"Step Record"},skip {"Skip"};
    juce::TextButton performance {"Performance"},sequence {"Euclidean Sequencer"},sounds {"Sounds"},plusBank {"+ Memory"},saveBank {"Save Memory..."},openBank {"Open Memory..."},addSample {"+ Sample"},addSound {"+ Sound lane"},store {"Store in vault"},newPattern {"New sequence"},load {"Load / Relink WAV..."},stretch {"Stretch"};
    juce::ComboBox banks,mode;juce::Slider start,end,tune,ratio;
    juce::String message;std::unique_ptr<juce::FileChooser> chooser;std::thread fileWorker;
};
