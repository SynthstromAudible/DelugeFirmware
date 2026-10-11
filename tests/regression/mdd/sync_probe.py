from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[3]
s=(r/'src/deluge/model/clip/instrument_clip.cpp').read_text()
def prefix(name,end):
 a=s.index(name);b=s.index(end,a);return s[a:b]+'}\n'
seek=prefix('void InstrumentClip::setPos(', '\tClip::setPos(modelStack, newPos, useActualPosForParamManagers)')
seek=seek[:-2]+'Clip::setPos(modelStack,newPos,useActualPosForParamManagers);}\n'
move=prefix('void InstrumentClip::incrementPos(', '\n\tticksTilNextNoteRowEvent -=')
switch=prefix('void InstrumentClip::processCurrentPos(', '\n\tif (mddEnabled) {')
code=r'''
#include "sequencer/mdd/controller.hpp"
#include <cassert>
#include <iostream>
enum class OutputType{KIT,SYNTH};struct Output{OutputType type=OutputType::SYNTH;};
struct Song{int quarter=24;int getQuarterNoteLength(){return quarter;}}song;
struct ModelStackWithTimelineCounter{Song*song=&::song;};
struct Clock{int64_t lastSwungTickActioned=0;int swungTicksTilNextEvent=1000;}playbackHandler;
struct Clip{int lastProcessedPos=0,loopLength=96,repeatCount=0;bool currentlyPlayingReversed=false;
 void incrementPos(ModelStackWithTimelineCounter*,int n){lastProcessedPos+=n;if(lastProcessedPos>=loopLength){repeatCount+=lastProcessedPos/loopLength;lastProcessedPos%=loopLength;}}
 void setPos(ModelStackWithTimelineCounter*,int n,bool){repeatCount=n/loopLength;lastProcessedPos=n%loopLength;}
};
struct InstrumentClip:Clip{Output*output;snake::Controller mdd;bool mddEnabled=false,mddRequested=false,mddPending=false,mddNormalReversed=false;int mddNormalPos=0,mddNormalRepeat=0,closed=0,stopped=0;uint64_t mddTick=0,mddLastProcessed=UINT64_MAX;
 void closeMdd(ModelStackWithTimelineCounter*){++closed;}void stopAllNotesPlaying(ModelStackWithTimelineCounter*){++stopped;}
 void setPos(ModelStackWithTimelineCounter*,int,bool);void incrementPos(ModelStackWithTimelineCounter*,int);void processCurrentPos(ModelStackWithTimelineCounter*,uint32_t);
};
'''+seek+move+switch+r'''
int main(){ModelStackWithTimelineCounter stack;Output out;
for(int quarter:{24,72}){song.quarter=quarter;InstrumentClip c;c.output=&out;c.loopLength=quarter*4;c.mdd.quarterTicks=quarter;for(auto&l:c.mdd.core.lanes)c.mdd.setSpeed(l,2);
 c.setPos(&stack,0,true);assert(c.mddTick==0&&c.mddLastProcessed==UINT64_MAX);
 // Native tick deltas can be one tick or batched, for internal or external clock.
 for(int n:{1,2,3,quarter-6,quarter,quarter*2})c.incrementPos(&stack,n);
 assert(c.mddTick==uint64_t(quarter*4));assert(c.lastProcessedPos==0);
 c.mddRequested=c.mddPending=true;playbackHandler.lastSwungTickActioned=quarter*4-1;
 playbackHandler.swungTicksTilNextEvent=1000;c.processCurrentPos(&stack,0);assert(c.mddPending&&!c.mddEnabled&&playbackHandler.swungTicksTilNextEvent==1);
 playbackHandler.lastSwungTickActioned=quarter*4;c.processCurrentPos(&stack,0);assert(c.mddEnabled&&!c.mddPending);
 assert(c.mdd.core.lanes[0].cell(c.mddTick)==0);
 c.incrementPos(&stack,quarter*2);int live=c.lastProcessedPos,repeat=c.repeatCount;
 c.mddRequested=false;c.mddPending=true;playbackHandler.lastSwungTickActioned=quarter*6;c.processCurrentPos(&stack,0);
 assert(!c.mddEnabled&&c.lastProcessedPos==live&&c.repeatCount==repeat); // no rewind
 c.incrementPos(&stack,quarter);assert(c.mddTick==uint64_t(quarter*7));
 c.mddRequested=c.mddPending=true;playbackHandler.lastSwungTickActioned=quarter*7;c.processCurrentPos(&stack,0);assert(c.mddEnabled);
 assert(c.mdd.core.lanes[0].cell(c.mddTick)==12); // phase follows song, not frozen MDD
 c.mddLastProcessed=0;c.setPos(&stack,0,true);assert(c.mddTick==0&&c.mddLastProcessed==UINT64_MAX);
 c.setPos(&stack,quarter,true);assert(c.mddTick==uint64_t(quarter)&&c.mdd.core.lanes[0].cell(c.mddTick)==4);
 c.setPos(&stack,quarter*5,true);assert(c.mddTick==uint64_t(quarter*5)&&c.lastProcessedPos==quarter);
}
std::cout<<"PASS native sync: shared tick deltas in both modes, live mode switch without rewind, start/restart/seek, batched clock ticks and two song tick resolutions\n";}
'''
with tempfile.TemporaryDirectory()as d:
 p=Path(d)/'probe.cpp';p.write_text(code);exe=Path(d)/'probe'
 subprocess.run(['g++','-std=c++23','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-misleading-indentation','-fsanitize=undefined','-I'+str(r/'src/deluge'),str(p),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
