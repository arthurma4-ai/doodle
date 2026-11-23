#pragma once
#include <cmath>

// Audio-only, allocation-free voice. The future sequencer can call trigger().
class DrumVoice
{
public:
    void prepare(double rate) { sampleRate = rate; reset(); }
    void reset() { age = 1.0; phase = 0.0; velocity = 0.0f; }
    void trigger(float level) { age = 0.0; phase = 0.0; velocity = level; }
    float nextSample()
    {
        if (age >= 0.8) return 0.0f;
        constexpr double pi = 3.14159265358979323846;
        const double frequency = 48.0 + 130.0 * std::exp(-age * 65.0);
        phase += 2.0 * pi * frequency / sampleRate;
        if (phase >= 2.0 * pi) phase -= 2.0 * pi;
        const double attack = 1.0 - std::exp(-age * 1800.0);
        const float value = static_cast<float>(0.85 * velocity * attack
                                             * std::exp(-age * 10.0) * std::sin(phase));
        age += 1.0 / sampleRate;
        return value;
    }
private:
    double sampleRate = 44100.0, age = 1.0, phase = 0.0;
    float velocity = 0.0f;
};
