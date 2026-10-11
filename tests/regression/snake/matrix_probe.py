#!/usr/bin/env python3
"""Compile exact source enum, source serializers and runtime setters with a minimal Sound shell."""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[3]
def extract(text, marker):
    start = text.index(marker)
    brace = text.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]
defs = (root / 'src/definitions_cxx.hpp').read_text()
functions = (root / 'src/deluge/util/functions.cpp').read_text()
sound = (root / 'src/deluge/processing/sound/sound.cpp').read_text()
code = '''#include "sequencer/snake.h"
#include <array>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cassert>
#include <type_traits>
#include <iostream>
namespace util { template<class T> constexpr auto to_underlying(T v) { return static_cast<std::underlying_type_t<T>>(v); } }
'''
code += extract(defs, 'enum class PatchSource') + ';\n'
code += 'constexpr PatchSource kFirstLocalSource = PatchSource::ENVELOPE_0;\nconstexpr int kNumPatchSources = static_cast<int>(PatchSource::NONE);\nconstexpr unsigned kNumSources = 2;\n'
code += extract(defs, 'constexpr bool isGlobalPatchSource') + '\n'
for marker in ['char const* sourceToString(', 'char const* sourceToStringShort(', 'PatchSource stringToSource(']:
    code += extract(functions, marker) + '\n'
code += '''struct Sound {
std::array<int32_t,kNumPatchSources> globalSourceValues{};
std::array<int8_t,2> snakePlaitsModels{-1,-1};
bool mddClipActive = false;
bool snakeCustomEnabled = false;
deluge::sequencer::SnakeConfig snakeCustomConfig;
void updateSnakeCustom();
uint32_t sourcesChanged = 0;
void setSnakeCustomValues(const std::array<int32_t,4>&);
bool setSnakePlaitsModel(unsigned,int);
};\n'''
code += extract(sound, 'void Sound::setSnakeCustomValues') + '\n'
code += extract(sound, 'bool Sound::setSnakePlaitsModel') + '\n'
code += '''struct Clock { bool active = false; int64_t tick = 0; bool isEitherClockActive() { return active; } int64_t getActualSwungTickCount() { return tick; } } playbackHandler;\n'''
code += extract(sound, 'void Sound::updateSnakeCustom') + '\n'
code += '''int main() {
static_assert(static_cast<int>(PatchSource::RANDOM) == 14);
static_assert(kNumPatchSources == 19);
for (int i=0; i<kNumPatchSources; ++i) {
 auto source = static_cast<PatchSource>(i);
 assert(stringToSource(sourceToString(source)) == source);
 assert(strlen(sourceToStringShort(source)) == 4);
 assert(isGlobalPatchSource(source) == (i<3 || i>=15));
}
assert(!isGlobalPatchSource(PatchSource::NONE));
assert(!isGlobalPatchSource(PatchSource::NOT_AVAILABLE));
Sound s;
s.setSnakeCustomValues({0,123,2147483647,-1});
assert(s.globalSourceValues[16]==123 && s.globalSourceValues[17]==2147483647);
assert(s.sourcesChanged == ((1u<<16)|(1u<<17)));
s.sourcesChanged=0;
s.setSnakeCustomValues({0,123,2147483647,-1});
assert(s.sourcesChanged==0);
s.setSnakeCustomValues({0,0,0,0});
assert(s.sourcesChanged == ((1u<<16)|(1u<<17)));
assert(s.setSnakePlaitsModel(1,2));
assert(s.snakePlaitsModels[1]==2);
assert(!s.setSnakePlaitsModel(2,1));
assert(!s.setSnakePlaitsModel(0,3));
assert(s.setSnakePlaitsModel(1,-1));
s.snakeCustomEnabled = true;
assert(s.snakeCustomConfig.modulation[0].division == 24);
s.snakeCustomConfig.modulation[0].division = 1;
s.snakeCustomConfig.modulation[0].length = 3;
s.snakeCustomConfig.modulation[0].values[0] = 0;
s.snakeCustomConfig.modulation[0].values[1] = 32768;
s.snakeCustomConfig.modulation[0].values[2] = 65535;
s.snakeCustomConfig.mappings[0].target = deluge::sequencer::SnakeTarget::PlaitsModel;
playbackHandler.active = true;
for (int tick=0; tick<30; ++tick) {
 playbackHandler.tick=tick; s.updateSnakeCustom();
 assert(s.snakePlaitsModels[0] == tick%3);
}
playbackHandler.active=false; s.updateSnakeCustom();
assert(s.snakePlaitsModels[0] == -1 && s.globalSourceValues[15] == 0);
playbackHandler.active=true; playbackHandler.tick=2; s.updateSnakeCustom();
assert(s.snakePlaitsModels[0] == 2);
s.snakeCustomEnabled=false; s.updateSnakeCustom();
assert(s.snakePlaitsModels[0] == -1 && s.globalSourceValues[15] == 0);
std::cout << "Snake matrix source IDs, global classification, serialization and runtime setters PASS\\n";
}'''
with tempfile.TemporaryDirectory() as tmp:
    src = Path(tmp) / 'probe.cpp'
    src.write_text(code)
    binary = Path(tmp) / 'probe'
    subprocess.run(['g++','-std=c++23','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(root / 'src/deluge'),str(src),'-o',str(binary)],check=True)
    import os
    subprocess.run([str(binary)],check=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0'})
