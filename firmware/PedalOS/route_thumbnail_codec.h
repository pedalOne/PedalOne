#pragma once

#include <stddef.h>
#include <stdint.h>

// P1TH v3 stores RGB565 pixels as PackBits-style runs. A reader returns the
// number of bytes copied; passing no output validates a stream without keeping
// a second image buffer in memory.
template <typename Reader>
bool decodeRouteThumbnailRLE565(Reader read, uint32_t payloadBytes,
                                uint16_t *output, size_t pixelCount) {
  uint32_t consumed=0;
  size_t decoded=0;
  uint8_t scratch[128];
  while(consumed<payloadBytes && decoded<pixelCount) {
    uint8_t token=0;
    if(read(&token,1)!=1)return false;
    ++consumed;
    const size_t count=size_t(token&0x7f)+1;
    if(count>pixelCount-decoded)return false;
    const size_t bytes=(token&0x80) ? 2 : count*2;
    if(bytes>payloadBytes-consumed)return false;
    if(token&0x80) {
      uint8_t color[2];
      if(read(color,2)!=2)return false;
      if(output) {
        const uint16_t value=uint16_t(color[0]) | (uint16_t(color[1])<<8);
        for(size_t i=0;i<count;++i)output[decoded+i]=value;
      }
    } else if(output) {
      if(read(reinterpret_cast<uint8_t*>(output+decoded),bytes)!=bytes)return false;
    } else {
      size_t left=bytes;
      while(left) {
        const size_t chunk=left<sizeof(scratch) ? left : sizeof(scratch);
        if(read(scratch,chunk)!=chunk)return false;
        left-=chunk;
      }
    }
    consumed+=bytes;
    decoded+=count;
  }
  return consumed==payloadBytes && decoded==pixelCount;
}
