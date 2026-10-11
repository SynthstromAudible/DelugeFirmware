#include "processing/plaits/engine_parameter.h"
#include "sequencer/mdd/snake.hpp"
#include <cassert>
#include <iostream>
int main(){using namespace deluge::plaits_control;
 for(int base=0;base<3;++base)for(int model=0;model<3;++model){auto preset=presetForModel(base,model);assert(modelFromPreset(base,preset)==model);int64_t combo=int64_t(preset)*1073741824/4294967296LL;int32_t final=combo*2;assert(modelFromFinal(base,final)==model);}
 const uint16_t steps[]={0,32768,65535};for(int i=0;i<3;++i){auto source=snake::normalizedQ31(steps[i]);int64_t combo=int64_t(source)*1073741824/4294967296LL;int32_t final=combo*2;assert(modelFromFinal(0,final)==i);}
 assert(modelFromFinal(0,INT32_MIN)==0&&modelFromFinal(2,INT32_MAX)==2);
 std::cout<<"PASS Engine parameter: all manual model/base combinations, Custom endpoints 1/2/3 and bounds\n";
}
