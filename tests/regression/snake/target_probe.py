#!/usr/bin/env python3
"""Envelope target coverage, using the actual firmware acceptance function and parameter enums."""
from pathlib import Path
import tempfile, subprocess, os
root = Path(__file__).resolve().parents[3]
def extract(text, marker):
    start = text.index(marker); brace = text.index('{',start); end = brace+1; depth = 1
    while depth:
        depth += (text[end]=='{') - (text[end]=='}'); end += 1
    return text[start:end]
defs = (root/'src/definitions_cxx.hpp').read_text()
params = (root/'src/deluge/modulation/params/param.h').read_text()
sound = (root/'src/deluge/processing/sound/sound.cpp').read_text()
code = '#include <array>\n#include <cassert>\n#include <cstdint>\n#include <iostream>\n'
for name in ['PatchSource','SynthMode','PatchCableAcceptance','OscType']:
    code += extract(defs,'enum class '+name)+';\n'
code += extract(defs,'constexpr bool isPlaits')+'\n' if 'kFirstLocalSource' not in extract(defs,'constexpr bool isPlaits') else ''
code += 'constexpr auto kFirstLocalSource = PatchSource::ENVELOPE_0;\n'
code += extract(defs,'constexpr bool isGlobalPatchSource')+'\n'
code += 'namespace params { using ParamType=uint8_t;\n'
for name in ['Local','Global']: code += extract(params,'enum '+name+' :')+';\n'
code += '''}
enum class FilterMode { OFF, ON };
constexpr unsigned LFO1_ID=0, LFO3_ID=1, SYNC_LEVEL_NONE=0;
struct Param { bool active = true; bool containsSomething(int32_t) { return active; } };
struct PatchedParamSet { std::array<Param, params::GLOBAL_NONE> params; };
struct ParamManager { PatchedParamSet set; PatchedParamSet* getPatchedParamSet() { return &set; } };
struct Sound {
 bool drum=false, active=true;
 SynthMode synthMode=SynthMode::SUBTRACTIVE;
 FilterMode lpfMode=FilterMode::ON, hpfMode=FilterMode::ON;
 struct Source { OscType oscType=OscType::SQUARE; };
 std::array<Source,2> sources;
 struct Lfo { unsigned syncLevel=0; };
 std::array<Lfo,2> lfoConfig;
 bool isDrum() { return drum; }
 SynthMode getSynthMode() { return synthMode; }
 bool isSourceActiveEverDisregardingMissingSample(int,ParamManager*) { return active; }
 bool isSourceActiveEver(int,ParamManager*) { return active; }
 PatchCableAcceptance maySourcePatchToParam(PatchSource,uint8_t,ParamManager*);
};
'''
code += extract(sound,'PatchCableAcceptance Sound::maySourcePatchToParam')+'\n'
code += '''int main() {
 Sound sound; ParamManager manager; unsigned checks=0;
 for (auto mode : {SynthMode::SUBTRACTIVE,SynthMode::FM,SynthMode::RINGMOD})
 for (bool active : {false,true}) for (bool filters : {false,true})
 for (bool plaits : {false,true}) for (bool sync : {false,true}) {
  sound.synthMode=mode; sound.active=active;
  sound.lpfMode=sound.hpfMode=filters ? FilterMode::ON : FilterMode::OFF;
  for (auto& source : sound.sources) source.oscType=plaits ? OscType::PLAITS_FM : OscType::SQUARE;
  for (auto& lfo : sound.lfoConfig) lfo.syncLevel=sync;
  for (auto& param : manager.set.params) param.active=active;
  for (unsigned p=0; p<params::GLOBAL_NONE; ++p)
  for (auto env : {PatchSource::ENVELOPE_0,PatchSource::ENVELOPE_1,PatchSource::ENVELOPE_2})
  for (auto custom : {PatchSource::SNAKE_CUSTOM_1,PatchSource::SNAKE_CUSTOM_2,PatchSource::SNAKE_CUSTOM_3,PatchSource::SNAKE_CUSTOM_4}) {
    auto expected=sound.maySourcePatchToParam(env,p,&manager);
    auto actual=sound.maySourcePatchToParam(custom,p,&manager);
    if (expected!=PatchCableAcceptance::DISALLOWED) assert(actual==expected);
    ++checks;
  }
 }
 sound.synthMode=SynthMode::SUBTRACTIVE;
 for (auto custom : {PatchSource::SNAKE_CUSTOM_1,PatchSource::SNAKE_CUSTOM_2,PatchSource::SNAKE_CUSTOM_3,PatchSource::SNAKE_CUSTOM_4}) {
  assert(sound.maySourcePatchToParam(custom,params::GLOBAL_DELAY_FEEDBACK,&manager)==PatchCableAcceptance::ALLOWED);
  assert(sound.maySourcePatchToParam(custom,params::GLOBAL_REVERB_AMOUNT,&manager)==PatchCableAcceptance::ALLOWED);
 }
 std::cout << "Envelope 1-3 destination coverage for all four Snake sources PASS: " << checks << " comparisons\\n";
}'''
with tempfile.TemporaryDirectory() as tmp:
    src=Path(tmp)/'probe.cpp'; src.write_text(code); out=Path(tmp)/'probe'
    subprocess.run(['g++','-std=c++23','-O1','-fsanitize=address,undefined',str(src),'-o',str(out)],check=True)
    subprocess.run([str(out)],check=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0'})
