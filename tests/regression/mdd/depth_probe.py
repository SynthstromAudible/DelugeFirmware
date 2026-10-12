"""Exercise the production cable-strength conversion for Custom vs stock sources."""

import subprocess
import tempfile
from pathlib import Path

r = Path(__file__).resolve().parents[3]
s = (r / "src/deluge/modulation/patch/patch_cable_set.cpp").read_text()
a = s.index("int32_t PatchCableSet::getModifiedPatchCableAmount(")
i = s.index("{", a) + 1
d = 1
while d:
    d += (s[i] == "{") - (s[i] == "}")
    i += 1
src = (
    r"""
#include "sequencer/mdd/controller.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
namespace params{enum{LOCAL_PITCH_ADJUST,LOCAL_OSC_A_PITCH_ADJUST,LOCAL_OSC_B_PITCH_ADJUST,LOCAL_MODULATOR_0_PITCH_ADJUST,LOCAL_MODULATOR_1_PITCH_ADJUST,GLOBAL_DELAY_RATE};}
enum class PatchSource{VELOCITY,NATIVE,SNAKE_CUSTOM_1=15,SNAKE_CUSTOM_2,SNAKE_CUSTOM_3,SNAKE_CUSTOM_4};
int32_t multiply_32x32_rshift32_rounded(int32_t a,int32_t b){return (int64_t(a)*b+2147483648LL)>>32;}
struct Param{int32_t v;int32_t getCurrentValue(){return v;}};
struct Cable{PatchSource from;Param param;};
struct PatchCableSet{Cable patchCables[1];int32_t getModifiedPatchCableAmount(int32_t,int32_t);};
"""
    + s[a:i]
    + r"""
int main(){PatchCableSet set;
for(int pct=-100;pct<=100;++pct){int16_t raw=pct*32767/100;assert(snake::depthPercent(raw)==pct);assert(snake::depthQ30(raw)==int64_t(pct)*1073741824/100);}
for(int source=15;source<19;++source)for(int target=0;target<6;++target)for(int amount:{-1073741824,-536870912,0,536870912,1073741824}){
 set.patchCables[0]={static_cast<PatchSource>(source),{amount}};
 assert(set.getModifiedPatchCableAmount(0,target)==amount);
 auto signal=int64_t(123456789)*set.getModifiedPatchCableAmount(0,target);
 assert(amount==536870912?signal*2==int64_t(123456789)*1073741824:true);
}
set.patchCables[0]={PatchSource::NATIVE,{536870912}};
assert(set.getModifiedPatchCableAmount(0,params::LOCAL_OSC_A_PITCH_ADJUST)==134217728);
std::cout<<"PASS production Custom Depth: all four sources, pitch/modulator/delay-rate, half/full/negative/zero, stock curve unchanged\n";
}
"""
)
with tempfile.TemporaryDirectory() as td:
    p = Path(td) / "probe.cpp"
    p.write_text(src)
    exe = Path(td) / "probe"
    subprocess.run(
        [
            "g++",
            "-std=c++23",
            "-fsanitize=undefined",
            "-I" + str(r / "src/deluge"),
            str(p),
            "-o",
            str(exe),
        ],
        check=True,
    )
    subprocess.run([str(exe)], check=True)
