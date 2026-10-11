#include <cassert>
#include <string>
#include <algorithm>
#include <bit>
#include <cstdint>
#include <span>
#include <iostream>
#include <limits>
using q31_t=int32_t;
constexpr int kSampleRate=44100,kModFXGrainBufferSize=65536;
constexpr int32_t ONE_OVER_SQRT2_Q31=1518500250;
int32_t multiply_32x32_rshift32(int32_t a,int32_t b){return (int64_t(a)*b)>>32;}
int32_t multiply_32x32_rshift32_rounded(int32_t a,int32_t b){return (int64_t(a)*b+0x80000000LL)>>32;}
int32_t q31_mult(int32_t a,int32_t b){return ((int64_t(a)*b)>>32)*2;}
int32_t toPositive(int32_t a){return (a/2)+1073741824;}
int32_t getMagnitudeOld(uint32_t a){return 32-std::countl_zero(a);}
int32_t increaseMagnitude(int32_t a,int32_t n){return n>=0?a<<n:a>>-n;}
int32_t add_saturate(int32_t a,int32_t b){return std::clamp<int64_t>(int64_t(a)+b,INT32_MIN,INT32_MAX);}
struct StereoSample{int32_t l,r;};
struct Filter {int32_t doFilter(int32_t a,int32_t){return a;}};
struct GrainBuffer{bool inUse=true;};
namespace AudioEngine {void feedReverbBackdoorForGrain(int,int32_t){}}
struct GranularProcessor {
 int wrapsToShutdown=0;uint32_t bufferWriteIndex=0;
 int _grainFeedbackVol=161061273,_grainSize=13230,_grainRate=1260,_grainShift=13230;
 int _grainVol=0,_grainDryVol=INT32_MAX,_pitchRandomness=0;
 int _densityKnobPos=0,_rateKnobPos=0,_mixKnobPos=0;
 bool grainInitialized=false,bufferFull=false,allocationSucceeds=true;
 int allocations=0;
 GrainBuffer* grainBuffer=nullptr;Filter lpf_l,lpf_r;
 void getBuffer(){++allocations;static GrainBuffer b;if(allocationSucceeds)grainBuffer=&b;}
 StereoSample processOneGrainSample(StereoSample s){return s;}
 void setWrapsToShutdown();
 void setupGrainFX(int32_t,int32_t,int32_t,int32_t,int32_t*,float);
 void processGrainFX(std::span<StereoSample>,int32_t,int32_t,int32_t,int32_t,int32_t*,bool,float,q31_t);
};
