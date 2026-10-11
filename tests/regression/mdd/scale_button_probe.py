"""Exercise the exact native MDD SCALE branch, including release and SD deferral."""
from pathlib import Path
import tempfile,subprocess
r=Path(__file__).resolve().parents[3];s=(r/'src/deluge/gui/views/instrument_clip_view.cpp').read_text();a=s.index('if (mddControls() && currentUIMode == UI_MODE_NONE && !Buttons::isButtonPressed(LEARN))');i=s.index('{',a)+1;d=1
while d:d+=(s[i]=='{')-(s[i]=='}');i+=1
src=r'''
#include <cassert>
#include <iostream>
#include <string>
enum class ActionResult{DEALT_WITH,REMIND_ME_OUTSIDE_CARD_ROUTINE,NOT_DEALT_WITH};
constexpr int UI_MODE_NONE=0,LEARN=1;int currentUIMode=0;
struct Buttons{static inline bool shift=false,learn=false;static bool isButtonPressed(int){return learn;}static bool isShiftButtonPressed(){return shift;}};
struct Clip{bool inScaleMode=false;}clip;
struct Song{int root=2,scale=0;int displays=0;void cycleThroughScales(){++scale;}void displayCurrentRootNoteAndScaleName(){++displays;}}song,*currentSong=&song;
struct Logger{void deleteAllLogs(){}}actionLogger;
enum class IndicatorLED{SCALE_MODE};namespace indicator_leds{bool led=false;void setLedState(IndicatorLED,bool b){led=b;}}
struct Display{std::string text;void displayPopup(const char*s){text=s;}}displayObject,*display=&displayObject;
struct View{bool enabled=true;bool mddControls(){return enabled;}Clip*getCurrentInstrumentClip(){return &clip;}ActionResult button(bool on,bool inCardRoutine);};void uiNeedsRendering(View*){}
ActionResult View::button(bool on,bool inCardRoutine){
'''+s[a:i]+r'''
return ActionResult::NOT_DEALT_WITH;}
int main(){View view;
assert(view.button(true,true)==ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE&&!clip.inScaleMode);
assert(view.button(true,false)==ActionResult::DEALT_WITH&&clip.inScaleMode&&song.root==2&&indicator_leds::led);
assert(view.button(false,false)==ActionResult::DEALT_WITH&&clip.inScaleMode);
assert(view.button(true,false)==ActionResult::DEALT_WITH&&!clip.inScaleMode&&display->text=="Chromatic");
Buttons::shift=true;view.button(true,false);assert(clip.inScaleMode&&song.scale==1&&song.root==2);
Buttons::learn=true;assert(view.button(true,false)==ActionResult::NOT_DEALT_WITH);Buttons::learn=false;
view.enabled=false;assert(view.button(true,false)==ActionResult::NOT_DEALT_WITH);
std::cout<<"PASS production MDD SCALE: Song-key quantization toggle, release no double toggle, SHIFT cycle, LEARN/stock routing and SD deferral\n";
}
'''
with tempfile.TemporaryDirectory() as td:
 p=Path(td)/'probe.cpp';p.write_text(src);exe=Path(td)/'probe';subprocess.run(['g++','-std=c++23','-fsanitize=undefined',str(p),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
