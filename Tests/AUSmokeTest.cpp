#include <AudioToolbox/AudioToolbox.h>
#include "../Source/MidiMapping.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <vector>
#include <thread>
#include <chrono>

static void check(OSStatus status)
{
    if (status != noErr) { std::fprintf(stderr, "AU error: %d\n", (int) status); std::exit(1); }
}
int main(int argc, char** argv)
{
    std::ifstream input(argc > 1 ? argv[1] : "build/sequence-fixtures/seven-kit.state", std::ios::binary);
    const std::vector<unsigned char> state((std::istreambuf_iterator<char>(input)), {});
    if (state.empty()) { std::fprintf(stderr, "Run SamplerTests first to create the fixture kit\n"); return 1; }
    AudioComponentDescription description { kAudioUnitType_MusicDevice, 'FgSq', 'ArAu', 0, 0 };
    const auto component = AudioComponentFindNext(nullptr, &description);
    if (!component) { std::fprintf(stderr, "AU not registered\n"); return 1; }
    AudioUnit unit = nullptr;
    check(AudioComponentInstanceNew(component, &unit));
    AudioStreamBasicDescription format {};
    format.mSampleRate = 44100;
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags = kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved;
    format.mBytesPerPacket = format.mBytesPerFrame = sizeof(float);
    format.mFramesPerPacket = 1;
    format.mChannelsPerFrame = 2;
    format.mBitsPerChannel = 32;
    check(AudioUnitSetProperty(unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &format, sizeof(format)));
    check(AudioUnitInitialize(unit));
    struct StereoBuffers { UInt32 count; AudioBuffer buffers[2]; } buffers {};
    float left[512] {}, right[512] {};
    buffers.count = 2;
    buffers.buffers[0] = {1, sizeof(left), left}; buffers.buffers[1] = {1, sizeof(right), right};
    AudioTimeStamp time {};
    time.mFlags = kAudioTimeStampSampleTimeValid;
    auto render = [&] {
        AudioUnitRenderActionFlags flags = 0;
        check(AudioUnitRender(unit, &flags, &time, 0, 512, reinterpret_cast<AudioBufferList*>(&buffers)));
        time.mSampleTime += 512;
    };
// Exercise the installed AU's factory patterns before replacing its kit.
std::this_thread::sleep_for(std::chrono::milliseconds(200));
for(int slot=0;slot<7;++slot)
{
    check(MusicDeviceMIDIEvent(unit,0xb0,120,0,0));render();
    check(MusicDeviceMIDIEvent(unit,0x90,(UInt32)(60+finger::MidiMapping::sounds[(size_t)slot]),100,64));double energy=0;
    for(int block=0;block<8;++block){render();for(int i=0;i<512;++i){if(!std::isfinite(left[i]))return 10;if(block==0&&i<64&&left[i]!=0)return 11;energy+=left[i]*left[i];}}
    if(energy<0.001){std::fprintf(stderr,"Factory slot %d silent\n",slot+1);return 12;}
}
std::printf("PASS installed AU: all seven built-in sounds including shaker O and tom P render from MIDI\n");
for(UInt32 note:{36u,60u})
{
    check(MusicDeviceMIDIEvent(unit,0xb0,120,0,0));render();
    check(MusicDeviceMIDIEvent(unit,0x90,note,100,0));double energy=0;
    for(int block=0;block<100;++block){render();for(float value:left){if(!std::isfinite(value))return 7;energy+=value*value;}}
    if(energy<0.01){std::fprintf(stderr,"Factory A silent from MIDI %u\n",note);return 8;}
    check(MusicDeviceMIDIEvent(unit,0x80,note,0,0));for(int block=0;block<150;++block)render();
    for(float value:left)if(value!=0)return 9;
}
std::printf("PASS installed AU: factory A renders from C1/36 and A/60, release stops sequence\n");
    CFPropertyListRef original = nullptr;
    UInt32 bytes = sizeof(original);
    check(AudioUnitGetProperty(unit, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &original, &bytes));
    auto dictionary = CFDictionaryCreateMutableCopy(nullptr, 0, (CFDictionaryRef)original);
    auto data = CFDataCreate(nullptr, state.data(), (CFIndex)state.size());
    CFDictionarySetValue(dictionary, CFSTR("jucePluginState"), data);
    CFPropertyListRef kit = dictionary;
    check(AudioUnitSetProperty(unit, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &kit, sizeof(kit)));
    CFRelease(data); CFRelease(dictionary); CFRelease(original);
    render();
    for (float value : left) if (value != 0) return 2;
    for (int slot = 0; slot < 7; ++slot)
    {
        double energy = 0;
        for (int retry = 0; retry < 1000; ++retry)
        {
            check(MusicDeviceMIDIEvent(unit, 0xb0, 120, 0, 0)); render();
            check(MusicDeviceMIDIEvent(unit, 0x90, (UInt32)(48 + finger::MidiMapping::sounds[(size_t)slot]), 100, 64)); render();
            energy = 0;
            for (int i = 0; i < 512; ++i)
            {
                if (!std::isfinite(left[i]) || !std::isfinite(right[i])) return 3;
                if (i < 64 && left[i] != 0) return 4;
                energy += left[i] * left[i];
            }
            if (energy >= 0.01) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        if (energy < 0.01) { std::fprintf(stderr, "Slot %d silent\n", slot + 1); return 5; }
    }
    for (int i = 0; i < 180; ++i) render();
    for (float value : left) if (value != 0) return 6;
    check(AudioUnitUninitialize(unit)); check(AudioComponentInstanceDispose(unit));
    std::printf("PASS installed AU: kit restored via ClassInfo, all seven slots render from MIDI at 44.1 kHz, sample offsets and tail silence\n");
}
