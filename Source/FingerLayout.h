#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
namespace finger::layout {
inline juce::Rectangle<int> pattern(int i){return {24+i*96,116,88,56};}
inline juce::Rectangle<int> maker(){return {90,224,468,468};}
inline juce::Rectangle<int> vault(){return {1112,196,174,578};}
inline juce::Rectangle<int> row(int i){return {584,242+i*62,506,58};}
inline juce::Rectangle<int> sound(int i){return {26+(i%4)*270,158+(i/4)*130,252,114};}
inline juce::Rectangle<int> waveform(){return {28,640,500,112};}
}
