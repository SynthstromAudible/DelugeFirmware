import subprocess
import tempfile
from pathlib import Path

r = Path(__file__).resolve().parents[3]
s = (r / "src/deluge/gui/views/instrument_clip_view.cpp").read_text()
a = s.index("bool InstrumentClipView::renderMainPads(")
b = s.index("\n\tif (isUIModeActive", a)
method = s[a:b] + "return false;\n}"
g = s.index("void InstrumentClipView::graphicsRoutine()")
h = s.index("\n\tif (!currentSong)", g)
graphics = s[g:h] + "\n}"
code = (
    r"""
#include "sequencer/mdd/controller.hpp"
#include <cassert>
#include <cstring>
#include <cstdint>
#include <iostream>
constexpr int kDisplayWidth=16,kSideBarWidth=2,kDisplayHeight=8;namespace PadLEDs{void clearTickSquares(){}}int redraws=0;template<class T>void uiNeedsRendering(T*,uint32_t,uint32_t){++redraws;}
struct RGB{uint8_t r=0,g=0,b=0;static RGB monochrome(int v){return {uint8_t(v),uint8_t(v),uint8_t(v)};}};
struct Clip{snake::Controller mdd;uint64_t mddTick=0;bool mddEnabled=true;}clip;
Clip*getCurrentInstrumentClip(){return &clip;}
struct Song{bool active=true;int getQuarterNoteLength(){return 24;}bool isClipActive(Clip*){return active;}}song,*currentSong=&song;
struct Playback{bool running=true;bool isEitherClockActive(){return running;}}playbackHandler;
struct InstrumentClipView{int mddLastRenderedCell=-1;bool enabled=true,mddGridWasRendered=false;bool mddControls(){return enabled;}void graphicsRoutine();bool renderMainPads(uint32_t,RGB[][18],uint8_t[][18],bool);};
"""
    + method
    + graphics
    + r"""
int main(){InstrumentClipView view;RGB image[8][18]{};uint8_t mask[8][18]{};int frames=0;
for(int n=0;n<7;++n)for(int shape=0;shape<5;++shape)for(int dir=0;dir<4;++dir)for(int length=1;length<=16;++length){
 clip.mdd.choose(n);auto&lane=clip.mdd.core.lanes[n];lane.shape=snake::Shape(shape);lane.direction=snake::Direction(dir);lane.length=length;lane.shift=3;lane.muted=65535;
 for(int t=0;t<32;++t){clip.mddTick=t*lane.ticks;int expected=lane.cell(clip.mddTick);view.renderMainPads(~0u,image,mask,false);int lit=0;
 for(int i=0;i<16;++i){auto c=image[7-i/4][i%4];if(c.r==32&&c.g==32&&c.b==32){++lit;assert(i==expected);}assert(mask[7-i/4][i%4]==64);}
 assert(lit==1);for(int x=0;x<16;++x){assert(mask[3][x]==(x<10?64:0));if(x>=10)assert(image[3][x].r==0&&image[3][x].g==0&&image[3][x].b==0);}for(int i=0;i<16;++i)if(i!=expected){auto c=image[7-i/4][i%4];assert(c.r<=7&&c.g<=7&&c.b<=7);}++frames;
 }
}
playbackHandler.running=false;view.renderMainPads(~0u,image,mask,false);for(int i=0;i<16;++i)assert(image[7-i/4][i%4].r!=32);
playbackHandler.running=true;song.active=false;view.renderMainPads(~0u,image,mask,false);for(int i=0;i<16;++i)assert(image[7-i/4][i%4].r!=32);
view.enabled=false;view.renderMainPads(~0u,image,mask,false);assert(!view.mddGridWasRendered);
 for(int y=0;y<8;++y)for(int x=0;x<16;++x){assert(image[y][x].r==0&&image[y][x].g==0&&image[y][x].b==0&&mask[y][x]==0);}
view.enabled=true;song.active=true;playbackHandler.running=true;clip.mdd.choose(0);auto&lane=clip.mdd.core.lanes[0];lane.direction=snake::Direction::Forward;lane.shape=snake::Shape::Rows;lane.length=16;lane.shift=0;clip.mddTick=0;
view.renderMainPads(~0u,image,mask,false);redraws=0;for(int i=0;i<100;++i)view.graphicsRoutine();assert(redraws==0);
clip.mddTick=lane.ticks;view.graphicsRoutine();assert(redraws==1);view.renderMainPads(~0u,image,mask,false);view.graphicsRoutine();assert(redraws==1);
playbackHandler.running=false;view.graphicsRoutine();assert(redraws==2);view.renderMainPads(~0u,image,mask,false);for(int i=0;i<100;++i)view.graphicsRoutine();assert(redraws==2);
std::cout<<"PASS change-driven grid redraw: idle, next cell and stop\n";
std::cout<<"PASS cleanup and actual grid render: "<<frames<<" frames, 7 lanes, 5 shapes, 4 directions, lengths 1-16, shifted/muted playhead and stopped/inactive states\n";
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
            "-Wall",
            "-Wextra",
            "-Werror",
            "-Wno-unused-parameter",
            "-fsanitize=undefined",
            "-I" + str(r / "src/deluge"),
            str(p),
            "-o",
            str(exe),
        ],
        check=True,
    )
    subprocess.run([str(exe)], check=True)
