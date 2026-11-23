#pragma once
#include <array>
namespace finger
{
inline constexpr int soundCount=7, patternCount=11;
struct MidiMapping
{
    static constexpr int defaultBase=60, maxBase=110;
    static constexpr std::array<int,patternCount> patterns {0,2,4,5,7,9,11,12,14,16,17};
    static constexpr std::array<int,soundCount> sounds {1,3,6,8,10,13,15};
    static constexpr std::array<const char*,patternCount> patternKeys {"A","S","D","F","G","H","J","K","L",";","'"};
    static constexpr std::array<const char*,soundCount> soundKeys {"W","E","T","Y","U","O","P"};
    static bool valid(int base) {return base>=0 && base<=maxBase;}
    static int pattern(int base,int note)
    {for(int i=0;i<patternCount;++i)if(base+patterns[(size_t)i]==note)return i;return -1;}
    static int sound(int base,int note)
    {for(int i=0;i<soundCount;++i)if(base+sounds[(size_t)i]==note)return i;return -1;}
};
}
