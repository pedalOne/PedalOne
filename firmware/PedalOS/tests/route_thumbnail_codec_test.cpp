#include "../route_thumbnail_codec.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>

static bool decode(const uint8_t *data,size_t bytes,uint16_t *output,
                   size_t pixels) {
  size_t cursor=0;
  const auto read=[&](uint8_t *destination,size_t count)->size_t {
    if(cursor+count>bytes)return 0;
    memcpy(destination,data+cursor,count);
    cursor+=count;
    return count;
  };
  return decodeRouteThumbnailRLE565(read,uint32_t(bytes),output,pixels);
}

int main() {
  // Three repeated blue pixels followed by three different literal pixels.
  const uint8_t encoded[]={0x82,0x1f,0x00,0x02,
                           0x00,0xf8,0xe0,0x07,0xff,0xff};
  uint16_t pixels[6]={};
  assert(decode(encoded,sizeof(encoded),pixels,6));
  assert(pixels[0]==0x001f && pixels[1]==0x001f && pixels[2]==0x001f);
  assert(pixels[3]==0xf800 && pixels[4]==0x07e0 && pixels[5]==0xffff);
  assert(decode(encoded,sizeof(encoded),nullptr,6));
  assert(!decode(encoded,sizeof(encoded)-1,pixels,6));
  assert(!decode(encoded,sizeof(encoded),pixels,5));
  assert(!decode(encoded,sizeof(encoded),pixels,7));
  std::puts("PASS: compressed route preview runs, literals, and malformed streams");
}
