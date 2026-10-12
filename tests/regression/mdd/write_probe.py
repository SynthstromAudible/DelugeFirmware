"""Run the actual transactional InstrumentClip WRITE body with fault-injected containers."""

import subprocess
import tempfile
from pathlib import Path

r = Path(__file__).resolve().parents[3]
s = (r / "src/deluge/model/clip/instrument_clip.cpp").read_text()
a = s.index("Error InstrumentClip::writeMddToNormal(")
b = s.index("\nint InstrumentClip::mddQuantize(", a)
body = s[a:b]
stubs = r"""
#include "sequencer/mdd/bake.hpp"
#include "sequencer/mdd/controller.hpp"
#include <vector>
#include <algorithm>
#include <cassert>
#include <iostream>
namespace AudioEngine { int services=0; void routineWithClusterLoading(bool userActions) { assert(!userActions); ++services; } }
enum class Error{NONE,UNSPECIFIED,INSUFFICIENT_RAM};enum class OutputType{SYNTH,KIT};enum class FillMode{OFF};enum class SequenceDirection{FORWARD,REVERSE};
constexpr int GREATER_OR_EQUAL=0,kDefaultLiftValue=64,kNumProbabilityValues=20,kDefaultIteranceValue=1;
int failAfter=-1;bool allocation(){return failAfter<0||failAfter-->0;}
struct Note{int pos=0,length=0,velocity=0,lift=0,probability=0,iterance=0,fill=0;};
struct Notes{std::vector<Note>data;int insertAtKey(int pos,bool){if(!allocation())return -1;data.push_back({pos});return data.size()-1;}Note*getElement(int i){return &data.at(i);}};
struct NoteRow{int y;Notes notes;int expression=0;};
struct NoteRowVector{std::vector<NoteRow>data;int getNumElements(){return data.size();}int search(int y,int){return std::lower_bound(data.begin(),data.end(),y,[](auto&r,int v){return r.y<v;})-data.begin();}NoteRow*getElement(int i){return &data.at(i);}NoteRow*insertNoteRowAtY(int y){if(!allocation())return nullptr;int i=search(y,0);data.insert(data.begin()+i,{y,{}});return &data[i];}void swapStateWith(NoteRowVector*o){data.swap(o->data);}};
struct Song{int getQuarterNoteLength(){return 24;}int getYVisualFromYNote(int n,bool){return n;}}song;struct ModelStackWithTimelineCounter{Song*song=&::song;};
struct InstrumentClip;struct ModelStack{ModelStackWithTimelineCounter stack;ModelStackWithTimelineCounter*addTimelineCounter(InstrumentClip*){return &stack;}};
enum class TimeType{BEFORE,AFTER};
class Consequence{public:virtual ~Consequence()=default;virtual Error revert(TimeType,ModelStack*)=0;};
enum class ActionType{MDD_WRITE};enum class ActionAddition{NOT_ALLOWED};
struct Action{Consequence*cons=nullptr;void addConsequence(Consequence*c){cons=c;}};
struct Output{OutputType type=OutputType::SYNTH;}output;
struct Logger{Action action;bool fail=false;Action*getNewAction(ActionType,ActionAddition){if(fail||!allocation())return nullptr;return &action;}void updateAction(Action*){}void closeAction(ActionType){}}actionLogger;
struct GeneralMemoryAllocator{static auto&get(){static GeneralMemoryAllocator a;return a;}void*allocLowSpeed(size_t n){return allocation()?std::malloc(n):nullptr;}};
void delugeDealloc(void*p){std::free(p);}
struct InstrumentClip{Output*output=&::output;int loopLength=96;snake::Controller mdd;uint64_t mddTick=37;SequenceDirection sequenceDirectionMode=SequenceDirection::REVERSE;bool currentlyPlayingReversed=true;NoteRowVector noteRows;bool mddRequested=true,mddEnabled=true,mddPending=false,inScaleMode=false;int noteRowsNumTicksBehindClip=8,ticksTilNextNoteRowEvent=7,yScroll=0,stops=0,resumes=0,events=0,lastProcessedPos=37;int mddQuantize(int n,Song*){return inScaleMode?n-n%2:n;}void stopAllNotesPlaying(ModelStackWithTimelineCounter*){++stops;}void resumePlayback(ModelStackWithTimelineCounter*,bool sound){assert(!sound);++resumes;}void expectEvent(){++events;}Error writeMddToNormal(ModelStackWithTimelineCounter*);};
"""
header = (r / "src/deluge/model/consequence/consequence_mdd_write.h").read_text()
header = "\n".join(l for l in header.splitlines() if not l.startswith("#"))
implementation = (
    r / "src/deluge/model/consequence/consequence_mdd_write.cpp"
).read_text()
implementation = "\n".join(
    l for l in implementation.splitlines() if not l.startswith("#")
)
stubs += header + "\n" + implementation + "\n"
checks = r"""
int main(){ModelStackWithTimelineCounter stack;
 for(int budget=0;budget<34;++budget){InstrumentClip c;failAfter=-1;c.noteRows.insertNoteRowAtY(42)->notes.insertAtKey(13,true);for(int i=0;i<16;++i)c.mdd.core.lanes[0].values[i]=60+i;failAfter=budget;
 assert(c.writeMddToNormal(&stack)==Error::INSUFFICIENT_RAM);assert(c.mddEnabled&&c.mddRequested&&c.stops==0&&c.noteRows.getNumElements()==1&&c.noteRows.getElement(0)->y==42&&c.noteRows.getElement(0)->notes.data[0].pos==13);}
 failAfter=-1;InstrumentClip c;c.inScaleMode=true;c.mdd.core.lanes[0].values.fill(61);assert(c.writeMddToNormal(&stack)==Error::NONE);assert(!c.mddEnabled&&!c.mddRequested&&c.lastProcessedPos==37&&!c.currentlyPlayingReversed&&c.sequenceDirectionMode==SequenceDirection::FORWARD&&c.stops==1&&c.resumes==1);
 auto&notes=c.noteRows.getElement(0)->notes.data;assert(c.noteRows.getElement(0)->y==60&&notes.size()==16);for(int i=0;i<16;++i)assert(notes[i].pos==i*6&&notes[i].length==3&&notes[i].velocity==100);
 InstrumentClip restored;restored.noteRows.insertNoteRowAtY(42)->notes.insertAtKey(13,true);restored.noteRows.getElement(0)->expression=77;restored.yScroll=8;
 assert(restored.writeMddToNormal(&stack)==Error::NONE);ModelStack undoStack;auto*cons=actionLogger.action.cons;
 assert(cons->revert(TimeType::BEFORE,&undoStack)==Error::NONE);assert(!restored.mddEnabled&&restored.noteRows.getElement(0)->y==42&&restored.noteRows.getElement(0)->notes.data[0].pos==13&&restored.noteRows.getElement(0)->expression==77&&restored.sequenceDirectionMode==SequenceDirection::REVERSE&&restored.yScroll==8);
 assert(cons->revert(TimeType::AFTER,&undoStack)==Error::NONE);assert(restored.noteRows.getElement(0)->y==60&&restored.noteRows.getElement(0)->notes.data.size()==16&&restored.sequenceDirectionMode==SequenceDirection::FORWARD);delete cons;
 actionLogger.fail=true;InstrumentClip failed;failed.noteRows.insertNoteRowAtY(44);assert(failed.writeMddToNormal(&stack)==Error::INSUFFICIENT_RAM&&failed.mddEnabled&&failed.noteRows.getElement(0)->y==44);actionLogger.fail=false;
 InstrumentClip empty;empty.mdd.core.lanes[0].muted=65535;assert(empty.writeMddToNormal(&stack)==Error::NONE&&empty.noteRows.getNumElements()==0);
 assert(AudioEngine::services>0);
 std::cout<<"PASS actual WRITE: rollback at 34 allocation points, native consequence undo/redo restores notes/row expression/direction and rejects missing action, stock row commit, scale, gates, velocity, empty sequence, preserved transport phase, audio service without user-action reentrancy\n";
}
"""
with tempfile.TemporaryDirectory() as d:
    p = Path(d) / "probe.cpp"
    p.write_text(stubs + body + checks)
    exe = Path(d) / "probe"
    subprocess.run(
        [
            "g++",
            "-std=c++23",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-fsanitize=undefined",
            "-D_GLIBCXX_DEBUG",
            "-I" + str(r / "src/deluge"),
            str(p),
            "-o",
            str(exe),
        ],
        check=True,
    )
    subprocess.run([str(exe)], check=True)
