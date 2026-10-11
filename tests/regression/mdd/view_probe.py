from pathlib import Path
import subprocess, tempfile
r = Path(__file__).resolve().parents[3]
s = (r / "src/deluge/gui/views/instrument_clip_view.cpp").read_text()
start = s.index("bool InstrumentClipView::renderMddOLED")
i = s.index("{", start) + 1
n = 1
while n:
    n += (s[i] == "{") - (s[i] == "}")
    i += 1
method = s[start:i]
code = r'''
#include "sequencer/mdd/controller.hpp"
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>
#include <iostream>
namespace deluge::hid::display::oled_canvas {
struct Canvas {
 std::vector<std::string> lines;
 void drawString(const char* s,int,int y,int,int h) {assert(y>=0 && y+h<=48);lines.emplace_back(s);}
};
}
namespace deluge::modulation::params {
enum class Kind {PATCHED};
constexpr int LOCAL_PLAITS_A_ENGINE=42,LOCAL_PLAITS_B_ENGINE=43;
const char* getParamDisplayName(Kind,int){return "Engine";}
}
struct AudioEngine {static inline unsigned audioSampleTimer=0;};
uint64_t mddTimeMs(){return uint64_t(AudioEngine::audioSampleTimer)*1000/44100;}
struct Song {struct {int rootNote=0;}key;int getQuarterNoteLength(){return 24;}int getCurrentScale(){return 0;}} song;
Song* currentSong=&song;
int mod(int a,int b){return (a%b+b)%b;}
const char* getScaleName(int){return "Major";}
void noteCodeToString(int n,char* s,void*,bool){snprintf(s,16,"Note%d",n);}
struct Clip {snake::Controller mdd;int mddQuantize(int n,Song*){return n;}} clip;
struct InstrumentClipView {
 bool mddControls(){return true;}
 Clip* getCurrentInstrumentClip(){return &clip;}
 bool renderMddOLED(deluge::hid::display::oled_canvas::Canvas&);
};
''' + method + r'''
int main(){
 InstrumentClipView view;
 using deluge::hid::display::oled_canvas::Canvas;
 Canvas c; assert(view.renderMddOLED(c)); assert(c.lines.size()==4);assert(c.lines[0]=="Notes Root C");
 song.key.rootNote=62;c.lines.clear();view.renderMddOLED(c);assert(c.lines[0]=="Notes Root D");song.key.rootNote=0;
 clip.mdd.core.lanes[0].length=7;clip.mdd.core.lanes[0].shift=5;
 c.lines.clear();view.renderMddOLED(c);assert(c.lines[1]=="Steps 7 Shift 5");
 clip.mdd.core.lanes[0].direction=snake::Direction::Pendulum;
 c.lines.clear();view.renderMddOLED(c);assert(c.lines[2]=="Pendulum Rows");
 assert(c.lines[3]=="1/16 SELECT: Synth edit");
 clip.mdd.choose(3);clip.mdd.routes[0][0].target=42;clip.mdd.gestures.held=0;clip.mdd.gestures.downAt=0;
 AudioEngine::audioSampleTimer=20000;clip.mdd.core.lanes[3].values[0]=65535;
 c.lines.clear();view.renderMddOLED(c);assert(c.lines[1]=="Step 1 Engine 3");
 clip.mdd.core.lanes[3].values[0]=16384;
 c.lines.clear();view.renderMddOLED(c);assert(c.lines[1]=="Step 1 Engine 2");
 std::cout<<"PASS actual OLED rendering: 48px bounds, root status, native menu hint and discrete Engine values\n";
}
'''
with tempfile.TemporaryDirectory() as d:
    cpp=Path(d)/"probe.cpp";out=Path(d)/"probe"
    cpp.write_text(code)
    subprocess.run(["g++","-std=c++23","-Wall","-Wextra","-Werror","-fsanitize=undefined","-I"+str(r/"src/deluge"),str(cpp),"-o",str(out)],check=True)
    subprocess.run([str(out)],check=True)
