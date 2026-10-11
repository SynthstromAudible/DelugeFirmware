#!/usr/bin/env python3
"""Compile real reverb headers and verbatim extracted methods in host scaffolds."""
from pathlib import Path
import argparse
import subprocess
import tempfile
HERE=Path(__file__).resolve().parent
p=argparse.ArgumentParser()
p.add_argument('--source-root',type=Path,default=HERE.parents[2])
p.add_argument('--cxx',default='g++')
a=p.parse_args();repo=a.source_root.resolve()
def method(path,signature):
 s=(repo/path).read_text();start=s.index(signature);end=s.index('{',start)+1;depth=1
 while depth:
  depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[start:end]
grain=(HERE/'grain_scaffold.hpp').read_text()+method('src/deluge/util/functions.cpp','int32_t quickLog(uint32_t input)')
for f in ['setWrapsToShutdown','setupGrainFX','processGrainFX']:
 grain+='\n'+method('src/deluge/dsp/granular/GranularProcessor.cpp','void GranularProcessor::'+f)
grain+=r'''
int main(int argc,char** argv){
 assert(argc==2);int32_t vol=1<<27;GranularProcessor g;
 if(std::string(argv[1])=="allocation"){
  g.processGrainFX({},1<<25,0,0,0,&vol,true,120,0);assert(g.allocations==1&&g.grainBuffer&&g.grainBuffer->inUse);
  g.grainBuffer=nullptr;g.allocationSucceeds=false;vol=1<<27;StereoSample dry{123,-456};
  g.processGrainFX(std::span{&dry,1},1<<25,0,0,0,&vol,true,120,0);
  assert(g.allocations==2&&!g.grainBuffer&&dry.l==123&&dry.r==-456&&vol==(1<<27));
  g.wrapsToShutdown=-1;g.processGrainFX({},1<<25,0,0,0,&vol,false,120,0);assert(g.allocations==2);
 }else{
  for(int32_t density:{-1073741824,0,1073741824}){
   GranularProcessor x,y;x.setupGrainFX(1<<18,0,density,0,&vol,120);x.setupGrainFX(1<<25,0,density,0,&vol,120);
   y.setupGrainFX(1<<25,0,density,0,&vol,120);assert(x._grainRate==y._grainRate&&x._grainSize==y._grainSize);
   int expected=1760+q31_mult(x._grainRate<<3,density/2+1073741824);assert(x._grainSize==expected);
   x.setupGrainFX(1<<25,0,density,0,&vol,120);assert(x._grainSize==expected);
  }
 }
}
'''
delay='#include <cassert>\n#include <initializer_list>\nstruct Delay{bool pingPong=true,analog=false;int syncLevel=4,syncType=0;\n'
delay+=method('src/deluge/dsp/delay/delay.h','Delay& operator=(const Delay& rhs)')+r'''};
int main(){for(int type:{0,1,2}){Delay x,y;x.syncType=type;x.syncLevel=7;x.pingPong=false;x.analog=true;y=x;
assert(y.syncType==type&&y.syncLevel==7&&!y.pingPong&&y.analog);y=y;assert(y.syncType==type);}}
'''
hpf=(HERE/'hpf_scaffold.hpp').read_text()+method('src/deluge/processing/engines/audio_engine.cpp','void getReverbParamsFromSong(Song* song)')+r'''
int main(){for(auto model:{deluge::dsp::Reverb::Model::MUTABLE,deluge::dsp::Reverb::Model::DIGITAL}){
 Song s;s.model=model;for(float f:{0.f,.2f,1.f}){reverb.setHPF(.8f);s.reverbHPF=f;getReverbParamsFromSong(&s);assert(std::abs(reverb.getHPF()-f)<1e-6f);}}}
'''
menu=r'''
#include <vector>
#include <span>
#include <algorithm>
#include <cassert>
#include <string>
#include <cstdint>
constexpr size_t kOLEDMenuNumOptionsVisible=4;
namespace etl{template<class T,size_t N>struct vector:std::vector<T>{vector(){this->reserve(N);}size_t capacity()const{return N;}};}
struct{void* currentModControllable=nullptr;int currentSourceIndex=0;}soundEditor;
struct MenuItem{bool relevant;bool isRelevant(void*,int){return relevant;}};
struct Submenu{
 std::vector<MenuItem*> items;std::vector<MenuItem*>::iterator current_item_;bool wrap=false;
 std::vector<MenuItem*> visible;int selected=-1;
 bool wrapAround(){return wrap;}void updateDisplay(){}
 void selectEncoderAction(int32_t);void drawPixelsForOled();
 void drawSubmenuItemsForOled(std::span<MenuItem*> rows,int pos){visible.assign(rows.begin(),rows.end());selected=pos;}
};
'''
for f in ['void Submenu::selectEncoderAction(int32_t offset)','void Submenu::drawPixelsForOled()']:
 menu+=method('src/deluge/gui/menu_item/submenu.cpp',f)
s=(repo/'src/deluge/gui/ui/sound_editor.cpp').read_text();start=s.index('void SoundEditor::selectEncoderAction(int8_t offset)');start=s.index('{',start)+1
menu+='\nnamespace deluge::hid::button{constexpr int SHIFT=0;}namespace Buttons{bool held=false;bool isButtonPressed(int){return held;}}\n'
menu+='int32_t accelerated(int8_t offset){'+s[start:s.index('MenuItem* item =',start)]+'return scaledOffset;}\n'
menu+=r'''
int main(int argc,char** argv){assert(argc==2);
 if(std::string(argv[1])=="shift"){
  for(bool held:{false,true}){Buttons::held=held;for(int i=-128;i<=127;++i)assert(accelerated(i)==i*(held?5:1));}
 }else{
 MenuItem a{true},b{false},c{true},d{false},e{true};
 for(bool wrap:{false,true})for(int start=0;start<3;++start)for(int offset=-127;offset<=127;++offset){
  Submenu m;m.items={&a,&b,&c,&d,&e};m.current_item_=m.items.begin()+start*2;m.wrap=wrap;m.selectEncoderAction(offset);
  int expected=wrap?((start+offset)%3+3)%3:std::clamp(start+offset,0,2);assert(m.current_item_-m.items.begin()==expected*2);
  m.drawPixelsForOled();assert(m.visible.size()==3&&m.visible[m.selected]==*m.current_item_);
 }
 MenuItem all[20];for(auto& item:all)item.relevant=true;
 for(int i=0;i<20;++i){Submenu m;for(auto& item:all)m.items.push_back(&item);m.current_item_=m.items.begin()+i;
 m.drawPixelsForOled();assert(m.visible.size()==4&&m.visible[m.selected]==*m.current_item_);}
 Submenu m;m.current_item_=m.items.end();m.drawPixelsForOled();assert(m.visible.empty());
 }
}
'''
cases=[('grain',grain,['allocation','rate']),('delay',delay,['copy']),('hpf',hpf,['load']),('reverb',(HERE/'reverb_probe.cpp').read_text(),['damping','width']),('menu',menu,['shift','navigation'])]
with tempfile.TemporaryDirectory(prefix='deluge-regression-') as d:
 d=Path(d);failed=[]
 for name,code,checks in cases:
  source=d/(name+'.cpp');source.write_text(code);exe=d/name
  cmd=[a.cxx,'-std=c++23','-O1','-g','-fsanitize=undefined','-fno-sanitize-recover=all','-D_GLIBCXX_DEBUG','-Wno-attributes','-I'+str(HERE/'stubs'),'-I'+str(repo/'src/deluge'),'-I'+str(repo/'src'),str(source)]
  if name in ('hpf','reverb'):cmd+=[str(repo/'src/deluge/dsp/reverb/freeverb/freeverb.cpp')]
  subprocess.run(cmd+['-o',str(exe)],check=True)
  for check in checks:
   r=subprocess.run([str(exe),check],text=True,capture_output=True);label=name+'/'+check
   print(('PASS ' if r.returncode==0 else 'FAIL ')+label,flush=True)
   if r.returncode:failed.append(label);print(r.stderr[:2000],flush=True)
 if failed:raise SystemExit('Failed: '+', '.join(failed))
 print('All 8 host regression groups passed; hardware testing is separate.')
