"""Exercise production knob conversion for full-range written Custom channels."""

import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[3]
s = (root / "src/deluge/modulation/params/param_set.cpp").read_text()


def method(signature):
    start = s.index(signature)
    end = s.index("{", start) + 1
    depth = 1
    while depth:
        depth += (s[end] == "{") - (s[end] == "}")
        end += 1
    return s[start:end]


code = (
    r"""
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iostream>
namespace params{constexpr int LOCAL_OSC_A_PHASE_WIDTH=1,LOCAL_OSC_B_PHASE_WIDTH=2,GLOBAL_MDD_CUSTOM_1=57,GLOBAL_MDD_CUSTOM_4=60;}
struct ModelStackWithAutoParam{int paramId=0;};
struct ParamSet{int32_t paramValueToKnobPos(int32_t,ModelStackWithAutoParam*){return 123;}int32_t knobPosToParamValue(int32_t,ModelStackWithAutoParam*){return 456;}};
struct PatchedParamSet:ParamSet{int32_t paramValueToKnobPos(int32_t,ModelStackWithAutoParam*);int32_t knobPosToParamValue(int32_t,ModelStackWithAutoParam*);};
"""
    + method("int32_t PatchedParamSet::paramValueToKnobPos")
    + "\n"
    + method("int32_t PatchedParamSet::knobPosToParamValue")
    + r"""
int main(){PatchedParamSet set;ModelStackWithAutoParam stack;
 for(int p=57;p<=60;++p){stack.paramId=p;
  assert(set.paramValueToKnobPos(0,&stack)==-64);assert(set.paramValueToKnobPos(INT32_MAX,&stack)==64);
  for(int k=-64;k<=64;++k){auto value=set.knobPosToParamValue(k,&stack);assert(value>=0);assert(set.paramValueToKnobPos(value,&stack)==k);}
  assert(set.knobPosToParamValue(-100,&stack)==0);assert(set.knobPosToParamValue(100,&stack)==INT32_MAX);
 }
 stack.paramId=10;assert(set.paramValueToKnobPos(0,&stack)==123&&set.knobPosToParamValue(0,&stack)==456);
 std::cout<<"PASS native Custom automation range: full knob/grid endpoints, 516 round trips, clamping and stock fallback\n";
}
"""
)
with tempfile.TemporaryDirectory() as d:
    p = Path(d) / "probe.cpp"
    p.write_text(code)
    exe = Path(d) / "probe"
    subprocess.run(
        ["g++", "-std=c++23", "-O1", "-fsanitize=undefined", str(p), "-o", str(exe)],
        check=True,
    )
    subprocess.run([str(exe)], check=True)
