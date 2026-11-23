#pragma once
#include <juce_audio_basics/juce_audio_basics.h>

// Offline phase vocoder: change duration while retaining input pitch.
// Called exclusively by the sample-preparation worker (or offline tests).
juce::AudioBuffer<float> stretchToLength(const juce::AudioBuffer<float>& input, int targetFrames);
