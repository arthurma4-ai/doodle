#pragma once
#include "SamplerEngine.h"
#include "SequencerEngine.h"
struct VaultPattern {juce::String name;finger::Pattern pattern;};
struct MemoryBank {
    juce::String name="Main Memory";
    std::vector<PadSettings> samples;
    finger::SequenceState sequences;
    finger::Pattern draft;
    std::vector<VaultPattern> vault;
};
std::unique_ptr<juce::XmlElement> writeMemory(const MemoryBank&);
MemoryBank readMemory(const juce::XmlElement&);
juce::Result exportMemory(MemoryBank,const juce::File& folder);
MemoryBank importMemory(const juce::File& manifest);
