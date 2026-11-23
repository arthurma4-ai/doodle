#pragma once
#include <algorithm>
#include <cstdint>
namespace finger {
inline uint32_t euclidean(int steps,int hits,int rotation=0)
{
    steps=std::clamp(steps,1,32);hits=std::clamp(hits,0,steps);rotation=(rotation%steps+steps)%steps;uint32_t result=0;
    for(int i=0;i<steps;++i)if((i*hits)%steps<hits)result|=uint32_t(1)<<((i+rotation)%steps);return result;
}
}
