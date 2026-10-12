"""Run production OLED scroller setup/timer with MDD playback opt-in."""

import subprocess
import tempfile
from pathlib import Path

r = Path(__file__).resolve().parents[3]
s = (r / "src/deluge/hid/display/oled.cpp").read_text()


def method(name):
    a = s.index(name)
    i = s.index("{", a) + 1
    d = 1
    while d:
        d += (s[i] == "{") - (s[i] == "}")
        i += 1
    return s[a:i]


a = s.index("struct SideScroller {")
b = s.index("SideScroller sideScrollers", a)
struct = s[a:b].replace("#define NUM_SIDE_SCROLLERS 2", "")
src = (
    r"""
#include <cassert>
#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <iostream>
struct String{std::string s;void set(const char*p,int n){s.assign(p,n);}const char*get(){return s.c_str();}};
struct Canvas{int getCharSpacingInPixels(char,int,bool){return 0;}int getCharWidthInPixels(char,int size){return size==10?9:6;}void clearAreaExact(int,int,int end,int){assert(end<128);}void drawString(const char*,int,int,int,int,int,int){}void invertArea(int,int,int,int){}};
struct Clock{bool running=true;bool isEitherClockActive(){return running;}}playbackHandler;
enum class MenuHighlighting{NO_INVERSION,INVERSION};struct FlashStorage{static inline auto accessibilityMenuHighlighting=MenuHighlighting::NO_INVERSION;};
enum class TimerName{OLED_SCROLLING_AND_BLINKING};struct Timer{void setTimer(TimerName,int){}}uiTimerManager;
constexpr int kScrollTime=100,NUM_SIDE_SCROLLERS=2;int sideScrollerDirection=0;struct{unsigned u32=0;}blinkArea;
struct OLED{static inline Canvas main;static void markChanged(){}static void sendMainImage(){}static void performBlink(){}static void setupSideScroller(int,std::string_view,int,int,int,int,int,int,bool,bool);static void scrollingAndBlinkingTimerEvent();};
"""
    + struct
    + "\nSideScroller sideScrollers[2]{};\n"
    + method("void OLED::setupSideScroller(")
    + "\n"
    + method("void OLED::scrollingAndBlinkingTimerEvent()")
    + r"""
int main(){
OLED::setupSideScroller(0,"Stock long label with many words",0,64,20,27,6,7,false,false);
OLED::setupSideScroller(1,"Replace stock notes?",0,128,1,11,9,10,false,true);
int high=0;for(int i=0;i<500;++i){OLED::scrollingAndBlinkingTimerEvent();assert(sideScrollers[0].pos==0);assert(sideScrollers[1].pos>=0&&sideScrollers[1].pos<=sideScrollers[1].stringLengthPixels-128);high=std::max(high,sideScrollers[1].pos);}
assert(high==sideScrollers[1].stringLengthPixels-128);
playbackHandler.running=false;int stockHigh=0;for(int i=0;i<200;++i){OLED::scrollingAndBlinkingTimerEvent();assert(sideScrollers[0].pos>=0);stockHigh=std::max(stockHigh,sideScrollers[0].pos);}assert(stockHigh>0);
std::cout<<"PASS production OLED: full WRITE header scrolls during playback, bounded forward/reverse cycle, stock labels remain still during playback and scroll when stopped\n";
}
"""
)
with tempfile.TemporaryDirectory() as td:
    p = Path(td) / "probe.cpp"
    p.write_text(src)
    exe = Path(td) / "probe"
    subprocess.run(
        ["g++", "-std=c++23", "-fsanitize=undefined", str(p), "-o", str(exe)],
        check=True,
    )
    subprocess.run([str(exe)], check=True)
