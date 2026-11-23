#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>
#include <iostream>
#include <thread>
#include <chrono>

static void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}
static void waitReady(SamplerEngine& engine, int slot, const juce::String& status = "Ready")
{
    for (int i = 0; i < 2000; ++i)
    {
        const auto pad = engine.view(slot);
        if (pad.revision == pad.readyRevision)
        { require(pad.status.startsWith(status), pad.status.toRawUTF8()); return; }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    throw std::runtime_error("Worker timeout");
}
static double frequency(const SampleData& sample)
{
    const int first = sample.audio.getNumSamples() / 4;
    const int end = sample.audio.getNumSamples() * 3 / 4;
    int crossings = 0;
    double firstCrossing = 0.0, lastCrossing = 0.0;
    const auto* data = sample.audio.getReadPointer(0);
    for (int i = first + 1; i < end; ++i)
        if (data[i - 1] <= 0 && data[i] > 0)
        {
            const double crossing = (double)(i - 1) - (double)data[i - 1] / (data[i] - data[i - 1]);
            if (crossings++ == 0) firstCrossing = crossing;
            lastCrossing = crossing;
        }
    return (crossings - 1) * sample.rate / (lastCrossing - firstCrossing);
}
static SampleData tone(int channels = 2)
{
    SampleData sample;
    sample.rate = 48000;
    sample.audio.setSize(channels, 48000);
    for (int c = 0; c < channels; ++c)
        for (int i = 0; i < 48000; ++i)
            sample.audio.setSample(c, i, 0.3f * (float)std::sin(2.0 * juce::MathConstants<double>::pi * 440.0 * i / 48000.0 + c * 0.3));
    return sample;
}
int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    try
    {
        auto source = tone();
        PadSettings s;
        s.start = 0.25; s.end = 0.75; s.tune = 12;
        auto pitched = SamplerEngine::render(source, s);
        require(pitched->audio.getNumSamples() == 12000, "Varispeed trim duration");
        require(std::abs(frequency(*pitched) - 880) < 10, "Varispeed octave");
        std::cout << "PASS trim + varispeed: 0.5s trim, +12st -> 0.25s / " << frequency(*pitched) << " Hz\n";

        s.stretch = true;
        for (double semitones : { -12.0, 0.0, 12.0 })
            for (double ratio : { 0.5, 1.0, 2.0, 4.0 })
            {
                s.tune = semitones; s.ratio = ratio;
                auto stretched = SamplerEngine::render(source, s);
                const double expectedPitch = 440.0 * std::pow(2.0, semitones / 12.0);
                require(stretched->audio.getNumSamples() == (int)std::llround(24000 * ratio), "Independent stretch duration");
                const double measured = frequency(*stretched);
                std::cout << "STRETCH " << semitones << "st / " << ratio * 100 << "%: "
                          << measured << " Hz / " << stretched->audio.getNumSamples() / 48000.0 << " sec\n";
                require(std::abs(measured - expectedPitch) < 3.0, "Independent stretch pitch");
                require(stretched->audio.getRMSLevel(0, 0, stretched->audio.getNumSamples()) > 0.05f, "Stretch audible energy");
                for (int c = 0; c < 2; ++c)
                    for (int j = 0; j < stretched->audio.getNumSamples(); ++j)
                        require(std::isfinite(stretched->audio.getSample(c, j)), "Finite stretch samples");
            }
        std::cout << "PASS independent pitch/duration matrix\n";

        const auto directory = juce::File::getCurrentWorkingDirectory().getChildFile("build/test-samples");
        require(directory.createDirectory().wasOk(), "Create fixture directory");
        const auto wav = directory.getChildFile("stereo.wav");
        wav.deleteFile();
        juce::WavAudioFormat format;
        auto options = juce::AudioFormatWriterOptions().withSampleRate(48000).withNumChannels(2).withBitsPerSample(24);
        auto stream = std::make_unique<juce::FileOutputStream>(wav);
        std::unique_ptr<juce::OutputStream> output(std::move(stream));
        auto writer = format.createWriterFor(output, options);
        require(writer != nullptr, "Create fixture writer");
        require(writer->writeFromAudioSampleBuffer(source.audio, 0, 48000), "Write WAV");
        writer.reset();
        FingerProcessor processor;
for(int i=0;i<7;++i){waitReady(processor.sampler,i);require(processor.sampler.view(i).settings.path.startsWith("factory:"),"Bundled factory kit");}
require(processor.sampler.view(5).settings.path=="factory:Shaker"&&processor.sampler.view(6).settings.path=="factory:Tom","Shaker O and Tom P preloaded");
require(processor.sequencer.pattern(0).lanes[0]==0x1111&&processor.sequencer.pattern(0).lanes[4]==0x4444,"Starter four-on-floor / offbeat hats");
processor.prepareToPlay(48000, 512);

        for (int i = 0; i < 7; ++i)
        {
            auto config = processor.sampler.view(i).settings;
            config.path = wav.getFullPathName();
            processor.sampler.configure(i, config);
        }
        for (int i = 0; i < 7; ++i) waitReady(processor.sampler, i);
        juce::AudioBuffer<float> buffer(2, 512);
        juce::MidiBuffer midi;
        for (int i = 0; i < 7; ++i)
        {
            processor.sampler.reset(); midi.addEvent(juce::MidiMessage::noteOn(1, 60 + finger::MidiMapping::sounds[(size_t)i], (juce::uint8)100), 64);
            processor.processBlock(buffer, midi);
            require(buffer.getMagnitude(0, 64) < 1.0e-9f, "Sample-offset silence");
            require(buffer.getMagnitude(64, 448) > 0.01f, "All 7 MIDI slots sound");
        }
        processor.sampler.reset();
        midi.addEvent(juce::MidiMessage::noteOn(1, 61, (juce::uint8)100), 0);
        processor.processBlock(buffer, midi);
        const float single = buffer.getSample(0, 100);
        processor.sampler.reset();
        for (int i = 0; i < 3; ++i) midi.addEvent(juce::MidiMessage::noteOn(1, 60 + finger::MidiMapping::sounds[(size_t)i], (juce::uint8)100), 0);
        processor.processBlock(buffer, midi);
        require(std::abs(buffer.getSample(0, 100) - 3 * single) < 0.0001f, "Overlapping slots sum");
        auto config = processor.sampler.view(2).settings;
        config.start = 0.1; config.end = 0.8; config.tune = -7; config.stretch = true; config.ratio = 2; config.note = 60;
        processor.sampler.configure(2, config);
        juce::MemoryBlock state;
        processor.getStateInformation(state);
        require(directory.getChildFile("kit.state").replaceWithData(state.getData(), state.getSize()), "Save AU smoke-test kit");
        FingerProcessor restored;
        restored.setStateInformation(state.getData(), (int)state.getSize());
        for (int i = 0; i < 7; ++i) waitReady(restored.sampler, i);
        const auto recall = restored.sampler.view(2).settings;
        require(recall.path == config.path && std::abs(recall.start - config.start) < 1.0e-9
                && std::abs(recall.end - config.end) < 1.0e-9 && std::abs(recall.tune + 7) < 1.0e-9
                && recall.stretch && std::abs(recall.ratio - 2) < 1.0e-9 && recall.note == 60, "Session settings recall");
        config.path = directory.getChildFile("moved.wav").getFullPathName();
        restored.sampler.configure(2, config); waitReady(restored.sampler, 2, "Missing file");
        restored.prepareToPlay(48000, 512); restored.sampler.triggerBySlot(2, 1.0f);
        midi.clear(); restored.processBlock(buffer, midi);
        require(buffer.getMagnitude(0, 512) < 1.0e-9f, "Missing sample stays silent");
        std::cout << "PASS all 7 MIDI slots, sample offsets, overlap, WAV loading, session recall and missing-file silence\n";

        std::atomic<bool> editsDone { false };
        std::thread edits([&] {
            for (int i = 0; i < 40; ++i)
            {
                auto setting = processor.sampler.view(0).settings;
                setting.tune = (i % 5) - 2;
                processor.sampler.configure(0, setting);
                std::this_thread::sleep_for(std::chrono::milliseconds(3));
            }
            editsDone.store(true);
        });
        bool finite = true;
        while (!editsDone.load())
        {
            processor.sampler.triggerBySlot(0, 0.5f);
            midi.clear(); processor.processBlock(buffer, midi);
            for (int i = 0; i < 512; ++i) finite = finite && std::isfinite(buffer.getSample(0, i));
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        edits.join(); waitReady(processor.sampler, 0);
        require(finite, "Concurrent asset replacement stays finite");
        std::cout << "PASS concurrent preparation/publication during 64-voice playback\n";

        auto mono = tone(1);
        auto monoOutput = SamplerEngine::render(mono, s);
        require(monoOutput->audio.getNumChannels() == 1, "Mono stretch remains mono");
        const auto monoFile = directory.getChildFile("mono.wav");
        monoFile.deleteFile();
        std::unique_ptr<juce::OutputStream> monoStream = std::make_unique<juce::FileOutputStream>(monoFile);
        auto monoWriter = format.createWriterFor(monoStream,
            juce::AudioFormatWriterOptions().withSampleRate(48000).withNumChannels(1).withBitsPerSample(24));
        require(monoWriter && monoWriter->writeFromAudioSampleBuffer(mono.audio, 0, 48000), "Write mono fixture");
        monoWriter.reset();
        auto monoConfig = restored.sampler.view(0).settings;
        monoConfig.path = monoFile.getFullPathName(); monoConfig.start = 0.1; monoConfig.end = 0.9;
        restored.sampler.configure(0, monoConfig); waitReady(restored.sampler, 0);
        restored.sampler.reset(); restored.sampler.requestTrigger(0);
        restored.processBlock(buffer, midi);
        require(buffer.getMagnitude(0, 512) > 0.01f, "Mono WAV loads and plays");
        for (int i = 0; i < 512; ++i)
            require(std::abs(buffer.getSample(0, i) - buffer.getSample(1, i)) < 1.0e-8f, "Mono duplicated to stereo");
        juce::MemoryBlock monoState;
        restored.getStateInformation(monoState);
        const auto movedFile = directory.getChildFile("mono-moved.wav");
        movedFile.deleteFile();
        require(monoFile.moveFileTo(movedFile), "Move referenced WAV");
        restored.setStateInformation(monoState.getData(), (int)monoState.getSize());
        waitReady(restored.sampler, 0, "Missing file");
        {
            std::unique_ptr<juce::AudioProcessorEditor> relinkEditor(restored.createEditor());
            for(int i=0;i<relinkEditor->getNumChildComponents();++i)if(auto* b=dynamic_cast<juce::Button*>(relinkEditor->getChildComponent(i)))if(b->getButtonText()=="Sounds")b->onClick(); dynamic_cast<juce::FileDragAndDropTarget*>(relinkEditor.get())->filesDropped({movedFile.getFullPathName()}, 100, 200);
            waitReady(restored.sampler, 0);
            const auto relinked = restored.sampler.view(0).settings;
            require(relinked.path == movedFile.getFullPathName() && std::abs(relinked.start - 0.1) < 1.0e-9
                    && std::abs(relinked.end - 0.9) < 1.0e-9, "Relink preserves trim");
        }
        std::cout << "PASS mono WAV playback, moved-file recall on same instance, relink preserving settings\n";
        std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
        auto button=[&](const juce::String& text)->juce::Button*{for(int i=0;i<editor->getNumChildComponents();++i)if(auto* b=dynamic_cast<juce::Button*>(editor->getChildComponent(i)))if(b->getButtonText()==text)return b;throw std::runtime_error("Missing button: "+text.toStdString());};
        auto event=[&](juce::Point<int> point,juce::Point<int> origin){auto time=juce::Time::getCurrentTime();return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(),point.toFloat(),juce::ModifierKeys::leftButtonModifier,1,0,0,0,0,editor.get(),editor.get(),time,origin.toFloat(),time,1,false);};
        auto click=[&](juce::Point<int> point){editor->mouseDown(event(point,point));editor->mouseUp(event(point,point));};
        auto drag=[&](juce::Point<int> from,juce::Point<int> to){editor->mouseDown(event(from,from));editor->mouseDrag(event(to,from));editor->mouseUp(event(to,from));};
        button("Sounds")->onClick();auto* drops=dynamic_cast<juce::FileDragAndDropTarget*>(editor.get());require(drops,"WAV drop target");
        for(int i=0;i<7;++i){auto point=finger::layout::sound(i).getCentre();drops->filesDropped({wav.getFullPathName()},point.x,point.y);waitReady(processor.sampler,i);processor.sampler.reset();click(point);processor.processBlock(buffer,midi);require(buffer.getMagnitude(0,512)>.01f,"Seven sample cards audition");}
        auto wave=finger::layout::waveform();drag({wave.getX(),wave.getCentreY()},{wave.getX()+wave.getWidth()/4,wave.getCentreY()});drag({wave.getRight(),wave.getCentreY()},{wave.getX()+wave.getWidth()*3/4,wave.getCentreY()});
        auto trimmed=processor.sampler.view(6).settings;require(std::abs(trimmed.start-.25)<.001&&std::abs(trimmed.end-.75)<.001,"New waveform markers trim");
        button("Euclidean Sequencer")->onClick();auto draft=processor.draft();draft.barTiming=true;draft.lanes[0]=finger::euclidean(16,4);processor.setDraft(draft);auto oldKey=processor.sequencer.pattern(10).lanes;
        auto row=finger::layout::row(0);click({row.getX()+68,row.getY()+38});require(processor.sequencer.pattern(10).lanes==oldKey,"Draft edits leave keys unchanged");
        auto maker=finger::layout::maker().getCentre();drag(maker,finger::layout::pattern(10).getCentre());require(processor.sequencer.pattern(10).lanes==processor.draft().lanes,"Maker circle drags whole sequence onto key");
        button("Store in vault")->onClick();require(processor.currentMemory().vault.size()==1,"Vault stores draft");draft=processor.draft();draft.lanes[0]=0;processor.setDraft(draft);drag({1200,280},finger::layout::pattern(9).getCentre());require(processor.sequencer.pattern(9).lanes[0]!=0,"Vault circle drags stored sequence onto key");
        // Macro drag hit targets and persisted rotation/speed in the maker.
        auto first=finger::layout::row(0);double speedBefore=processor.draft().speeds[0];drag({48,first.getY()+38},{48,first.getY()+58});require(processor.draft().speeds[0]<speedBefore,"Left-side speed macro changes circle duration");
        drag({first.getX()+410,first.getY()+38},{first.getX()+410,first.getY()-2});require(processor.draft().lengths[0]>16,"Steps macro generates a longer editable dot row");
        drag({first.getX()+445,first.getY()+38},{first.getX()+445,first.getY()-2});require(processor.draft().lanes[0]==finger::euclidean(processor.draft().lengths[0],6,processor.draft().rotations[0]),"Hits macro generates evenly spaced rhythm");
        drag({first.getX()+485,first.getY()+38},{first.getX()+485,first.getY()-2});require(processor.draft().rotations[0]>0,"Rotation macro rotates generated rhythm");
        for(int i=7;i<18;++i){int slot=processor.addSample(wav.getFullPathName());require(slot==i,"Library grows beyond seven");waitReady(processor.sampler,slot);}
        draft=processor.draft();for(auto& bits:draft.lanes)bits=0;draft.lanes[17]=1;draft.enabled[17]=1;processor.setDraft(draft);processor.assignDraft(8);require(processor.sequencer.pattern(8).lanes.size()==18,"Pattern gains sound lanes beyond seven");
        processor.prepareToPlay(48000,512);processor.sequencer.setMode(finger::PlayMode::join);midi.addEvent(juce::MidiMessage::noteOn(1,74,(juce::uint8)100),0);bool heard=false;for(int block=0;block<20;++block){processor.processBlock(buffer,midi);if(block==0)require(std::abs(buffer.getSample(0,10)-source.audio.getSample(0,10)*(10.0f/64.0f)*.425f)<1e-5f,"Dynamic sample 18 alone matches actual rendered audio");heard|=buffer.getMagnitude(0,512)>.001f;}require(heard,"Assigned dynamic pattern plays audio");
        processor.addMemory();require(processor.sampler.slotCount()==0&&processor.currentMemory().vault.empty(),"New memory has empty samples/vault");processor.processBlock(buffer,midi);require(processor.sequencer.performance().active==0,"Memory switch clears performance");
        processor.switchMemory(0);require(processor.sampler.slotCount()==18&&processor.currentMemory().vault.size()==1,"Main memory restores sample library/vault");require(processor.sequencer.pattern(8).lanes[17]==1,"Memory retains dynamic key sequence");
        juce::MemoryBlock bankState;processor.getStateInformation(bankState);FingerProcessor bankRecall;bankRecall.setStateInformation(bankState.getData(),(int)bankState.getSize());require(bankRecall.draft().speeds[0]==.5&&bankRecall.draft().rotations[0]==6,"Draft speed/rotation survive DAW recall");require(bankRecall.memoryNames().size()==2&&bankRecall.sampler.slotCount()==18&&bankRecall.currentMemory().vault.size()==1,"DAW state recalls all memories/library/vault");
        const auto exported=directory.getSiblingFile("memory-export");exported.deleteRecursively();require(exportMemory(processor.currentMemory(),exported).wasOk(),"Export self-contained memory folder");auto portable=importMemory(exported.getChildFile("bank.xml"));require(portable.samples.size()==18&&juce::File(portable.samples[17].path).existsAsFile()&&portable.vault.size()==1,"Portable bank import resolves copied samples and vault");
        auto* typed=dynamic_cast<FingerEditor*>(editor.get());require(typed&&!typed->keyPressed(juce::KeyPress('A')),"Raw Musical Typing key passes through");
        FingerProcessor factory;std::unique_ptr<juce::AudioProcessorEditor> visual(factory.createEditor());
        auto visualButton=[&](const char* name){for(int i=0;i<visual->getNumChildComponents();++i)if(auto* b=dynamic_cast<juce::Button*>(visual->getChildComponent(i)))if(b->getButtonText()==name){b->onClick();return;}throw std::runtime_error("Visual page button");};
        auto snapshot=[&](const char* name){auto image=visual->createComponentSnapshot(visual->getLocalBounds());juce::FileOutputStream imageOutput(directory.getSiblingFile(name));imageOutput.setPosition(0);imageOutput.truncate();juce::PNGImageFormat png;require(png.writeImageToStream(image,imageOutput),"Page screenshot");};
        snapshot("performance-page.png");visualButton("Euclidean Sequencer");snapshot("sequencer-page.png");for(int i=0;i<7;++i)waitReady(factory.sampler,i);visualButton("Sounds");snapshot("sounds-page.png");
        std::cout<<"PASS three pages, WAV audition/trim, draft isolation, Euclidean/key/vault drag, dynamic 18-sound library, empty memory, session recall and portable folder export/import\n";return 0;
    }
    catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
