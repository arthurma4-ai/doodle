#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

struct PadSettings
{
    juce::String path;
    double start = 0.0, end = 1.0, tune = 0.0, ratio = 1.0;
    bool stretch = false;
    int note = 36;
};
struct SampleData { juce::AudioBuffer<float> audio; double rate = 44100.0; };
struct PadView
{
    PadSettings settings;
    juce::String status = "Empty - drop WAV or Load";
    std::shared_ptr<const SampleData> source;
    uint64_t revision = 0, readyRevision = 0, fileRevision = 0;
};

class SamplerEngine
{
public:
    static constexpr int slots = 7, polyphony = 128;
    SamplerEngine();
    ~SamplerEngine();
    PadView view(int slot) const;
    void configure(int slot, PadSettings settings, bool reloadFile = false);
    void restore(const std::array<PadSettings, slots>&);
    std::array<PadSettings, slots> settings() const;
void ensureSlots(int count); // control thread only; stable slot objects for audio/worker readers
int slotCount() const{return activeSlots.load();}
std::vector<PadSettings> allSettings() const;
void restoreAll(const std::vector<PadSettings>&);
void prepare(double rate);

    void reset(bool clearRequests=true); // audio thread, or while processing is stopped
    void requestTrigger(int slot, float velocity = 1.0f); // UI thread
    void consumeRequests(); // audio thread
    void triggerBySlot(int slot, float velocity); // audio-thread entry for future sequencer
    void triggerMidi(int note, float velocity);
    void renderSample(juce::AudioBuffer<float>&, int sample);
    static std::unique_ptr<SampleData> render(const SampleData&, const PadSettings&);
private:
    struct Asset { SampleData sample; };
    struct Voice { Asset* asset = nullptr; double position = 0.0; float velocity = 0.0f; uint64_t serial = 0; };
    void work();
    void collect();
    mutable std::mutex mutex;
struct Slot {
    PadView pad;std::atomic<Asset*> published {nullptr};
    std::atomic<int> note {36};std::atomic<float> request {0};
    std::atomic<uint64_t> desired {0},prepared {0};
};
using Directory=std::vector<Slot*>;
std::vector<std::unique_ptr<Slot>> slotOwners;
std::vector<std::unique_ptr<Directory>> directories;
std::atomic<const Directory*> directory {nullptr};
std::atomic<int> activeSlots {7};
    // Hazard pointers protect each voice's immutable buffer. Only the worker frees assets.
    std::array<std::atomic<Asset*>, polyphony> hazards {};
    std::vector<std::unique_ptr<Asset>> retired;
    std::array<Voice, polyphony> voices;
    std::atomic<bool> stopping { false };
    std::thread worker;
    double hostRate = 44100.0;
    uint64_t nextSerial = 0;
};
