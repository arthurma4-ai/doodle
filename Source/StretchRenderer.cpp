#include "StretchRenderer.h"
#include <juce_dsp/juce_dsp.h>
#include <complex>
#include <vector>
#include <cmath>

juce::AudioBuffer<float> stretchToLength(const juce::AudioBuffer<float>& input, int targetFrames)
{
    constexpr int order = 11, size = 1 << order, hop = 256, bins = size / 2 + 1;
    constexpr double pi = juce::MathConstants<double>::pi;
    const int sourceFrames = input.getNumSamples();
    if (targetFrames == sourceFrames) return juce::AudioBuffer<float>(input);
    const double factor = (double)targetFrames / sourceFrames;
    juce::dsp::FFT fft(order);
    std::vector<float> window(size), weights((size_t)targetFrames, 0.0f);
    for (int i = 0; i < size; ++i) window[(size_t)i] = (float)(0.5 - 0.5 * std::cos(2.0 * pi * i / size));
    juce::AudioBuffer<float> output(input.getNumChannels(), targetFrames);
    output.clear();
    const int outputSteps = (targetFrames + size / 2 + hop - 1) / hop;
    for (int step = 0; step < outputSteps; ++step)
        for (int i = 0; i < size; ++i)
        {
            const int position = step * hop - size / 2 + i;
            if (position >= 0 && position < targetFrames) weights[(size_t)position] += window[(size_t)i] * window[(size_t)i];
        }
    for (int channel = 0; channel < input.getNumChannels(); ++channel)
    {
        std::vector<juce::dsp::Complex<float>> time(size), first(size), second(size), spectrum(size), inverse(size);
        std::vector<double> phase(bins);
        int cached = -1;
        const auto* data = input.getReadPointer(channel);
        auto transform = [&](int index, auto& destination) {
            for (int i = 0; i < size; ++i)
            {
                const int position = index * hop - size / 2 + i;
                time[(size_t)i] = { position >= 0 && position < sourceFrames ? data[position] * window[(size_t)i] : 0.0f, 0.0f };
            }
            fft.perform(time.data(), destination.data(), false);
        };
        for (int step = 0; step < outputSteps; ++step)
        {
            const double sourcePosition = step / factor;
            const int index = (int)sourcePosition;
            const float fraction = (float)(sourcePosition - index);
            if (index != cached)
            {
                if (index == cached + 1 && cached >= 0) first.swap(second);
                else transform(index, first);
                transform(index + 1, second);
                cached = index;
            }
            for (int bin = 0; bin < bins; ++bin)
            {
                const auto k = (size_t)bin;
                const double a = std::arg(first[k]), b = std::arg(second[k]);
                if (step == 0) phase[k] = a;
                const float magnitude = std::abs(first[k]) * (1.0f - fraction) + std::abs(second[k]) * fraction;
                spectrum[k] = std::polar(magnitude, (float)phase[k]);
                const double expected = 2.0 * pi * bin * hop / size;
                const double deviation = std::remainder(b - a - expected, 2.0 * pi);
                phase[k] = std::remainder(phase[k] + expected + deviation, 2.0 * pi);
            }
            spectrum[0] = { spectrum[0].real(), 0.0f };
            spectrum[size / 2] = { spectrum[size / 2].real(), 0.0f };
            for (int bin = 1; bin < size / 2; ++bin) spectrum[(size_t)(size - bin)] = std::conj(spectrum[(size_t)bin]);
            fft.perform(spectrum.data(), inverse.data(), true);
            for (int i = 0; i < size; ++i)
            {
                const int position = step * hop - size / 2 + i;
                if (position >= 0 && position < targetFrames)
                    output.addSample(channel, position, inverse[(size_t)i].real() * window[(size_t)i]);
            }
        }
        auto* destination = output.getWritePointer(channel);
        for (int i = 0; i < targetFrames; ++i)
            destination[i] /= juce::jmax(0.000001f, weights[(size_t)i]);
    }
    return output;
}
