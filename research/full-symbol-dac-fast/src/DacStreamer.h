#pragma once
#include <stdint.h>

namespace lora_sdr {
struct DacSegment { unsigned offset,length; bool down; };
struct DacStreamStats {
    unsigned buffers=0,lateBlocks=0,maxGapCycles=0,maxFillCycles=0;
    uint64_t gapCycles=0;
    bool timedOut=false;
};
// Experimental full-symbol playback. Internal phase ring and lookup tables
// must outlive this blocking call. The RF chain is keyed by the caller.
void streamDac(const uint8_t* phase,const uint32_t* down,const uint32_t* lookup,unsigned ringLength,
               const DacSegment* plan,unsigned segments,unsigned gapSamples,
               DacStreamStats& stats);
}
