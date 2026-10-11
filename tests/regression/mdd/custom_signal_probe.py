"""Production Custom source setter/polarity arithmetic, with isolated host services."""
from pathlib import Path
import subprocess, tempfile
r=Path(__file__).resolve().parents[3]
def method(path,name):
 s=(r/path).read_text(); a=s.index(name);i=s.index('{',a)+1;d=1
 while d:d+=(s[i]=='{')-(s[i]=='}');i+=1
 return s[a:i]
setter=method('src/deluge/processing/sound/sound.cpp','void Sound::setSnakeCustomValues')
polarity=method('src/deluge/modulation/patch/patch_cable.h','int32_t toPolarity')
default=method('src/deluge/modulation/patch/patch_cable.cpp','Polarity PatchCable::getDefaultPolarity')
source=r'''
#include "sequencer/mdd/snake.hpp"
#include <array>
#include <cassert>
#include <limits>
#include <iostream>
enum class PatchSource {AFTERTOUCH=11,X=9,Y=10,SIDECHAIN=2,SNAKE_CUSTOM_1=15,SNAKE_CUSTOM_2,SNAKE_CUSTOM_3,SNAKE_CUSTOM_4};
enum class Polarity {UNIPOLAR,BIPOLAR};
namespace util {int to_underlying(PatchSource p){return int(p);}}
struct FlashStorage {static inline Polarity defaultPatchCablePolarity=Polarity::UNIPOLAR;};
struct Sound {std::array<int32_t,19> globalSourceValues{};uint32_t sourcesChanged=0;void setSnakeCustomValues(const std::array<int32_t,4>&);};
struct PatchCable {PatchSource from;Polarity polarity;static Polarity getDefaultPolarity(PatchSource);
'''+polarity+'};\n'+setter+'\n'+default+r'''
int main(){
 Sound s;
 for(int lane=0;lane<4;++lane){
  const auto source=static_cast<PatchSource>(15+lane);
  for(auto preference:{Polarity::UNIPOLAR,Polarity::BIPOLAR}){
   FlashStorage::defaultPatchCablePolarity=preference;
   PatchCable c{source,PatchCable::getDefaultPolarity(source)};
   assert(c.polarity==Polarity::UNIPOLAR);
   for(uint16_t raw:{uint16_t(0),uint16_t(32768),uint16_t(65535)}){
    std::array<int32_t,4> v{};v[lane]=snake::normalizedQ31(raw);
    s.sourcesChanged=0;s.setSnakeCustomValues(v);
    assert(s.globalSourceValues[15+lane]==v[lane]);
    assert(c.toPolarity(v[lane])==v[lane]);
    PatchCable bipolar{source,Polarity::BIPOLAR};
    assert(bipolar.toPolarity(v[lane])==v[lane]);
    if(raw==65535)assert(bipolar.toPolarity(v[lane])==2147483647);
    for(int j=0;j<4;++j)if(j!=lane)assert(s.globalSourceValues[15+j]==0);
    auto expected=s.sourcesChanged;s.sourcesChanged=0;s.setSnakeCustomValues(v);assert(s.sourcesChanged==0);
    if(raw)assert(expected&(1u<<(15+lane)));
    // Signed depth has opposite effects, zero depth contributes zero.
    for(int depth:{-1073741824,0,1073741824}){
     auto contribution=int64_t(c.toPolarity(v[lane]))*depth/4294967296LL;
     assert(depth==0?contribution==0:depth<0?contribution<=0:contribution>=0);
    }
   }
  }
 }
 s.setSnakeCustomValues({});for(int i=15;i<19;++i)assert(s.globalSourceValues[i]==0);
 std::cout<<"PASS production Custom 1-4 setter: independent source masks, full unipolar endpoints under both default polarities, signed/zero depth and clearing\n";
}
'''
with tempfile.TemporaryDirectory() as td:
 p=Path(td)/'probe.cpp';p.write_text(source);exe=Path(td)/'probe'
 subprocess.run(['g++','-std=c++23','-fsanitize=undefined','-Isrc/deluge',str(p),'-o',str(exe)],cwd=r,check=True)
 subprocess.run([str(exe)],check=True)
