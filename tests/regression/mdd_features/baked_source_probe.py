"""Run the actual Sound source publication code with native-channel host services."""

import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[3]
s = (root / "src/deluge/processing/sound/sound.cpp").read_text()


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
#include "sequencer/snake.h"
#include <array>
#include <cassert>
#include <algorithm>
#include <cstdint>
#include <iostream>
namespace params{constexpr int GLOBAL_MDD_CUSTOM_1=0;}
namespace util{template<class T>constexpr auto to_underlying(T x){return static_cast<unsigned>(x);}}
enum class PatchSource{SNAKE_CUSTOM_1=15};
struct Patched{std::array<int32_t,4> values{};int32_t getValue(unsigned i){return values.at(i);}};
struct Params{Patched p;Patched*getPatchedParamSet(){return &p;}};
using ParamManager=Params;
struct InstrumentClip{Params paramManager;};
struct Clock{bool isEitherClockActive(){return false;}int64_t getActualSwungTickCount(){return 0;}}playbackHandler;
struct Sound{
 bool mddClipActive=false,snakeCustomEnabled=false;uint8_t mddBakedSources=0;const void*mddClipOwner=nullptr;
 std::array<int32_t,19>globalSourceValues{};unsigned sourcesChanged=0;
 deluge::sequencer::SnakeConfig snakeCustomConfig;
 void setSnakeCustomValues(const std::array<int32_t,4>&);
 bool setSnakePlaitsModel(unsigned,int){return true;}
 void updateSnakeCustom(ParamManager*);
};
"""
    + method("void Sound::setSnakeCustomValues")
    + "\n"
    + method("void Sound::updateSnakeCustom")
    + r"""
int main(){
 InstrumentClip clip;Sound sound;sound.mddClipOwner=&clip;sound.mddClipActive=true;sound.mddBakedSources=15;
 clip.paramManager.p.values={0,INT32_MAX/2,INT32_MAX,-1};sound.updateSnakeCustom(&clip.paramManager);
 assert(sound.globalSourceValues[15]==0&&sound.globalSourceValues[16]==INT32_MAX/2&&sound.globalSourceValues[17]==INT32_MAX&&sound.globalSourceValues[18]==0);
 sound.updateSnakeCustom(&clip.paramManager);assert(sound.globalSourceValues[16]==INT32_MAX/2); // no double application
 sound.mddBakedSources=0;sound.setSnakeCustomValues({123,456,789,0});sound.updateSnakeCustom(&clip.paramManager);assert(sound.globalSourceValues[15]==123); // MDD live data wins over written curves
 sound.mddClipActive=false;sound.updateSnakeCustom(&clip.paramManager);for(int i=15;i<19;++i)assert(sound.globalSourceValues[i]==0);
 std::cout<<"PASS real Sound bridge: native written Custom values, no double modulation, MDD live override and cleanup\n";
}
"""
)
with tempfile.TemporaryDirectory() as d:
    p = Path(d) / "probe.cpp"
    p.write_text(code)
    exe = Path(d) / "probe"
    subprocess.run(
        [
            "g++",
            "-std=c++23",
            "-O1",
            "-fsanitize=undefined",
            "-Isrc/deluge",
            str(p),
            "-o",
            str(exe),
        ],
        cwd=root,
        check=True,
    )
    subprocess.run([str(exe)], check=True)
