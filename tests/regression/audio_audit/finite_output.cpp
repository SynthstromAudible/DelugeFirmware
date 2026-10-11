#include "dsp/plaits/voice.h"
#include <cassert>
#include <iostream>
int main() {
 for (uint32_t bits : {0x7f800000u,0xff800000u,0x7fc00001u,0xffc00001u,0x7f800001u}) {
  volatile uint32_t input=bits;
  float value=std::bit_cast<float>(uint32_t(input));
  assert(deluge::dsp::plaits_detail::sanitizeOutput(value)==0.f);
 }
 for(float value : {-10.f,-1.f,-.5f,0.f,.5f,1.f,10.f})
  assert(deluge::dsp::plaits_detail::sanitizeOutput(value)==std::clamp(value,-1.f,1.f));
 std::cout<<"PASS NaN/infinity rejection and finite clipping under -O3 -ffast-math\n";
}
