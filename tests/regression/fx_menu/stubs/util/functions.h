#pragma once
#include "definitions_cxx.hpp"
inline int32_t multiply_32x32_rshift32_rounded(int32_t a,int32_t b){return (int64_t(a)*b+0x80000000LL)>>32;}
inline int32_t multiply_32x32_rshift32(int32_t a,int32_t b){return (int64_t(a)*b)>>32;}
namespace deluge::util { constexpr float map(float v,float a,float b,float c,float d){return c+(v-a)/(b-a)*(d-c);} }
