#include "SamplerEngine.h"
#include "StretchRenderer.h"
#include <BinaryData.h>
#include <algorithm>
#include <cmath>
#include <chrono>

SamplerEngine::SamplerEngine()
{
    ensureSlots(slots);
    for (auto& pointer : hazards) pointer.store(nullptr);
    worker = std::thread([this] { work(); });
}
SamplerEngine::~SamplerEngine()
{
    stopping.store(true);
    worker.join();
    for(auto& slot:slotOwners)delete slot->published.exchange(nullptr);
}
PadView SamplerEngine::view(int slot) const
{
    std::lock_guard<std::mutex> lock(mutex);
    return slotOwners.at((size_t)slot)->pad;
}
std::array<PadSettings, SamplerEngine::slots> SamplerEngine::settings() const
{
    std::lock_guard<std::mutex> lock(mutex);
    std::array<PadSettings, slots> result;
    for (int i = 0; i < slots; ++i) result[(size_t)i] = slotOwners[(size_t)i]->pad.settings;
    return result;
}
static PadSettings sanitise(PadSettings s)
{
    auto finite = [](double v, double fallback) { return std::isfinite(v) ? v : fallback; };
    s.start = juce::jlimit(0.0, 0.99999, finite(s.start, 0.0));
    s.end = juce::jlimit(s.start + 0.00001, 1.0, finite(s.end, 1.0));
    s.tune = juce::jlimit(-24.0, 24.0, finite(s.tune, 0.0));
    s.ratio = juce::jlimit(0.5, 4.0, finite(s.ratio, 1.0));
    s.note = juce::jlimit(0, 127, s.note);
    return s;
}
void SamplerEngine::configure(int slot, PadSettings s, bool reloadFile)
{
    if(slot<0)return;ensureSlots(slot+1);
    std::lock_guard<std::mutex> lock(mutex);
    auto& entry=*slotOwners[(size_t)slot];auto& pad=entry.pad;
    if (reloadFile || s.path != pad.settings.path) ++pad.fileRevision;
    pad.settings = sanitise(s);
    ++pad.revision;
    pad.status = pad.settings.path.isEmpty() ? "Empty - drop WAV or Load" : "Preparing...";
    entry.note.store(pad.settings.note);entry.desired.store(pad.revision);activeSlots.store(juce::jmax(activeSlots.load(),slot+1));
}
void SamplerEngine::restore(const std::array<PadSettings, slots>& state)
{
    restoreAll(std::vector<PadSettings>(state.begin(),state.end()));
}
void SamplerEngine::ensureSlots(int count)
{
    std::lock_guard<std::mutex> lock(mutex);
    if(count<=(int)slotOwners.size())return;
    auto next=std::make_unique<Directory>();for(auto& slot:slotOwners)next->push_back(slot.get());
    while((int)slotOwners.size()<count){auto slot=std::make_unique<Slot>();slot->pad.settings.note=juce::jmin(127,36+(int)slotOwners.size());slot->note.store(slot->pad.settings.note);next->push_back(slot.get());slotOwners.push_back(std::move(slot));}
    directory.store(next.get());directories.push_back(std::move(next));activeSlots.store(count);
}
std::vector<PadSettings> SamplerEngine::allSettings() const
{std::lock_guard<std::mutex> lock(mutex);std::vector<PadSettings> result;for(int i=0;i<activeSlots.load();++i)result.push_back(slotOwners[(size_t)i]->pad.settings);return result;}
void SamplerEngine::restoreAll(const std::vector<PadSettings>& state)
{
    ensureSlots((int)state.size());std::lock_guard<std::mutex> lock(mutex);
    for(size_t i=0;i<slotOwners.size();++i){auto& slot=*slotOwners[i];auto& pad=slot.pad;slot.request.store(0);++pad.fileRevision;pad.settings=sanitise(i<state.size()?state[i]:PadSettings{});++pad.revision;pad.status=pad.settings.path.isEmpty()?"Empty - drop WAV or Load":"Preparing...";slot.note.store(pad.settings.note);slot.desired.store(pad.revision);}
    activeSlots.store((int)state.size());
}
void SamplerEngine::prepare(double rate) { hostRate = rate; reset(false); }
void SamplerEngine::reset(bool clearRequests)
{
    for (size_t i = 0; i < voices.size(); ++i) { voices[i] = {}; hazards[i].store(nullptr); }
    if(clearRequests)if(auto* list=directory.load())for(auto* slot:*list)slot->request.store(0.0f);
}
void SamplerEngine::requestTrigger(int slot, float velocity)
{
    if(auto* list=directory.load())if(slot>=0&&slot<activeSlots.load()&&slot<(int)list->size())(*list)[(size_t)slot]->request.store(velocity);
}
void SamplerEngine::consumeRequests()
{
    const auto* list=directory.load();for (int i = 0; i < juce::jmin(activeSlots.load(),(int)list->size()); ++i)
    {
        const auto velocity = (*list)[(size_t)i]->request.exchange(0.0f);
        if (velocity > 0.0f) triggerBySlot(i, velocity);
    }
}
void SamplerEngine::triggerMidi(int note, float velocity)
{
    const auto* list=directory.load();for (int i = 0; i < juce::jmin(activeSlots.load(),(int)list->size()); ++i) if ((*list)[(size_t)i]->note.load() == note) triggerBySlot(i, velocity);
}
void SamplerEngine::triggerBySlot(int slot, float velocity)
{
    const auto* list=directory.load();if(slot<0||slot>=activeSlots.load()||slot>=(int)list->size()||velocity<=0)return;
    auto& entry=*(*list)[(size_t)slot];if(entry.desired.load()!=entry.prepared.load()||entry.published.load()==nullptr)return;
    size_t index = 0;
    for (size_t i = 0; i < voices.size(); ++i)
    {
        if (voices[i].asset == nullptr) { index = i; break; }
        if (voices[i].serial < voices[index].serial) index = i;
    }
    Asset* asset;
    // Publish protection before dereferencing and recheck after acquiring it.
    do { asset = entry.published.load(); hazards[index].store(asset); }
    while (asset != entry.published.load());
    voices[index] = { asset, 0.0, juce::jlimit(0.0f, 1.0f, velocity), ++nextSerial };
}
void SamplerEngine::renderSample(juce::AudioBuffer<float>& output, int frame)
{
    for (size_t i = 0; i < voices.size(); ++i)
    {
        auto& voice = voices[i];
        if (voice.asset == nullptr) continue;
        const auto& sample = voice.asset->sample;
        const int count = sample.audio.getNumSamples();
        const int a = (int) voice.position, b = juce::jmin(a + 1, count - 1);
        const float fraction = (float)(voice.position - a);
        for (int channel = 0; channel < output.getNumChannels(); ++channel)
        {
            float value = 0.0f;
            const int channels = output.getNumChannels() == 1 ? sample.audio.getNumChannels() : 1;
            for (int source = 0; source < channels; ++source)
            {
                const int sc = channels > 1 ? source : juce::jmin(channel, sample.audio.getNumChannels() - 1);
                const auto* data = sample.audio.getReadPointer(sc);
                value += data[a] + fraction * (data[b] - data[a]);
            }
            output.addSample(channel, frame, value * voice.velocity * 0.5f / (float)channels);
        }
        voice.position += sample.rate / hostRate;
        if (voice.position >= count) { voice.asset = nullptr; hazards[i].store(nullptr); }
    }
}
std::unique_ptr<SampleData> SamplerEngine::render(const SampleData& input, const PadSettings& s)
{
    const int total = input.audio.getNumSamples();
    const int first = juce::jlimit(0, total - 1, (int)(s.start * total));
    const int end = juce::jlimit(first + 1, total, (int)(s.end * total));
    const double pitch = std::pow(2.0, s.tune / 12.0);
    const int count = juce::jmax(1, (int)std::llround((end - first) / pitch));
    auto result = std::make_unique<SampleData>();
    result->rate = input.rate;
    result->audio.setSize(input.audio.getNumChannels(), count);
    for (int c = 0; c < result->audio.getNumChannels(); ++c)
        for (int i = 0; i < count; ++i)
        {
            const double pos = juce::jmin((double)(end - 1), first + i * pitch);
            const int a = (int)pos, b = juce::jmin(a + 1, end - 1);
            const auto* data = input.audio.getReadPointer(c);
            result->audio.setSample(c, i, data[a] + (float)(pos - a) * (data[b] - data[a]));
        }
    if (s.stretch)
    {
        const int target = juce::jmax(1, (int)std::llround((end - first) * s.ratio));
        result->audio = stretchToLength(result->audio, target);
    }
    // Short edge ramps avoid hard-cut clicks at arbitrary trim boundaries.
    const int fade = juce::jmin(64, result->audio.getNumSamples() / 2);
    for (int c = 0; c < result->audio.getNumChannels(); ++c)
        for (int i = 0; i < fade; ++i)
        {
            const float gain = (float)i / (float)fade;
            result->audio.getWritePointer(c)[i] *= gain;
            result->audio.getWritePointer(c)[result->audio.getNumSamples() - 1 - i] *= gain;
        }
    return result;
}
void SamplerEngine::collect()
{
    retired.erase(std::remove_if(retired.begin(), retired.end(), [this](const auto& asset) {
        for (const auto& hazard : hazards) if (hazard.load() == asset.get()) return false;
        return true;
    }), retired.end());
}
void SamplerEngine::work()
{
    juce::WavAudioFormat format;
    std::vector<uint64_t> completed;
    std::vector<juce::String> loadedPaths;
    std::vector<uint64_t> loadedRevisions;
    std::vector<std::shared_ptr<const SampleData>> sources;
    while (!stopping.load())
    {
const auto* list=directory.load();completed.resize(list->size());loadedPaths.resize(list->size());loadedRevisions.resize(list->size());sources.resize(list->size());
for (int i = 0; i < (int)list->size() && !stopping.load(); ++i)
        {
            const auto index = (size_t)i;
            if((*list)[index]->desired.load()==completed[index])continue;
            auto snapshot = view(i);
            if (snapshot.revision == completed[index]) continue;
            const auto revision = snapshot.revision;
            const auto& s = snapshot.settings;
            juce::String status;
            std::unique_ptr<Asset> asset;
            try
            {
                if (!s.path.startsWith("factory:") && !s.path.isEmpty() && !juce::File(s.path).existsAsFile()) sources[index].reset();
                if (s.path != loadedPaths[index] || !sources[index] || snapshot.fileRevision != loadedRevisions[index])
                {
                    loadedRevisions[index] = snapshot.fileRevision;
                    sources[index].reset();
                    loadedPaths[index] = s.path;
                    const juce::File file(s.path);
                    if (s.path.isEmpty()) status = "Empty - drop WAV or Load";
                    else if (!s.path.startsWith("factory:") && !file.existsAsFile()) status = "Missing file - Load to relink";
                    else if (!s.path.startsWith("factory:") && !file.hasFileExtension("wav")) status = "WAV files only";
                    else
                    {
                        std::unique_ptr<juce::InputStream> stream;
                        if(s.path.startsWith("factory:"))
                        {
                            int bytes=0;const auto name=s.path.fromFirstOccurrenceOf(":",false,false)+"_wav";
                            if(const auto* data=BinaryData::getNamedResource(name.toRawUTF8(),bytes))stream=std::make_unique<juce::MemoryInputStream>(data,(size_t)bytes,false);
                        }
                        else stream=std::make_unique<juce::FileInputStream>(file);
                        std::unique_ptr<juce::AudioFormatReader> reader(stream?format.createReaderFor(stream.release(), true):nullptr);

                        if (!reader || reader->numChannels < 1 || reader->numChannels > 2
                            || reader->lengthInSamples < 1 || reader->sampleRate > 192000
                            || (double)reader->lengthInSamples / reader->sampleRate > 30.0)
                            status = "Use mono/stereo WAV, <=30 sec, <=192 kHz";
                        else
                        {
                            auto source = std::make_shared<SampleData>();
                            source->rate = reader->sampleRate;
                            source->audio.setSize((int)reader->numChannels, (int)reader->lengthInSamples);
                            if (reader->read(&source->audio, 0, source->audio.getNumSamples(), 0, true, true))
                            {
                                for (int c = 0; c < source->audio.getNumChannels(); ++c)
                                    for (int frame = 0; frame < source->audio.getNumSamples(); ++frame)
                                        if (!std::isfinite(source->audio.getSample(c, frame))) source->audio.setSample(c, frame, 0.0f);
                                sources[index] = source;
                            }
                            else status = "WAV read failed - Load to retry";
                        }
                    }
                }
                if (sources[index])
                {
                    asset = std::make_unique<Asset>();
                    asset->sample = std::move(*render(*sources[index], s));
                    status = "Ready";
                }
            }
            catch (const std::exception&) { status = "Could not prepare sample"; }
            {
                std::lock_guard<std::mutex> lock(mutex);
                auto& slot=*slotOwners[index];auto& pad=slot.pad;
                if (pad.revision != revision) continue; // discard stale worker result
                if (auto* previous = slot.published.exchange(asset.release())) retired.emplace_back(previous);
                pad.source = sources[index];
                pad.status = status;
                pad.readyRevision = revision;
                completed[index] = revision;slot.prepared.store(revision);
            }
        }
        collect();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}
