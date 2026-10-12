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
namespace deluge::modulation::params {constexpr int GLOBAL_MDD_CUSTOM_1=0;}
struct ParamNode{int pos=0;int32_t value=0;bool interpolated=false;};
struct LazyParamNodeVector{std::vector<ParamNode> data;int insertAtKey(int pos,bool){if(!allocation())return -1;data.push_back({pos});return data.size()-1;}int getNumElements(){return data.size();}ParamNode*getElement(int i){return &data.at(i);}void swapStateWith(LazyParamNodeVector*other){data.swap(other->data);}};
struct AutoParamState{LazyParamNodeVector nodes;int32_t value=0;};
struct ModelStackWithAutoParam;
struct AutoParam{LazyParamNodeVector nodes;int32_t value=0;void swapState(AutoParamState* s,ModelStackWithAutoParam*){std::swap(value,s->value);nodes.swapStateWith(&s->nodes);}};
struct ModelStackWithAutoParam{AutoParam*autoParam=nullptr;};
struct ParamModel{int id=0;ParamModel*addParamCollectionSummary(void*){return this;}ParamModel*addParamId(int n){id=n;return this;}};
struct Patched{AutoParam params[4];ModelStackWithAutoParam result;ModelStackWithAutoParam*getAutoParamFromId(ParamModel*m,bool){result.autoParam=&params[m->id];return &result;}};
struct Params{Patched patched;Patched*getPatchedParamSet(){return &patched;}void*getPatchedParamSetSummary(){return this;}};
struct Song{int getQuarterNoteLength(){return 24;}int getYVisualFromYNote(int n,bool){return n;}}song;struct ModelStackWithTimelineCounter{Song*song=&::song;ParamModel model;ParamModel*addOtherTwoThingsButNoNoteRow(void*,void*){return &model;}};
struct InstrumentClip;struct ModelStack{ModelStackWithTimelineCounter stack;ModelStackWithTimelineCounter*addTimelineCounter(InstrumentClip*){return &stack;}};
enum class TimeType{BEFORE,AFTER};
class Consequence{public:virtual ~Consequence()=default;virtual Error revert(TimeType,ModelStack*)=0;};
enum class ActionType{MDD_WRITE};enum class ActionAddition{NOT_ALLOWED};
struct Action{Consequence*cons=nullptr;void addConsequence(Consequence*c){cons=c;}};
struct Output{OutputType type=OutputType::SYNTH;void*toModControllable(){return this;}}output;
struct Logger{Action action;bool fail=false;Action*getNewAction(ActionType,ActionAddition){if(fail||!allocation())return nullptr;return &action;}void updateAction(Action*){}void closeAction(ActionType){}}actionLogger;
struct GeneralMemoryAllocator{static auto&get(){static GeneralMemoryAllocator a;return a;}void*allocLowSpeed(size_t n){return allocation()?std::malloc(n):nullptr;}};
void delugeDealloc(void*p){std::free(p);}
struct InstrumentClip{Output*output=&::output;Params paramManager;uint8_t mddBakedCustom=0;void publishMddCustom(){}int loopLength=96;snake::Controller mdd;uint64_t mddTick=37;SequenceDirection sequenceDirectionMode=SequenceDirection::REVERSE;bool currentlyPlayingReversed=true;NoteRowVector noteRows;bool mddRequested=true,mddEnabled=true,mddPending=false,inScaleMode=false;int noteRowsNumTicksBehindClip=8,ticksTilNextNoteRowEvent=7,yScroll=0,stops=0,resumes=0,events=0,lastProcessedPos=37;int mddQuantize(int n,Song*){return inScaleMode?n-n%2:n;}void stopAllNotesPlaying(ModelStackWithTimelineCounter*){++stops;}void resumePlayback(ModelStackWithTimelineCounter*,bool sound){assert(!sound);++resumes;}void expectEvent(){++events;}Error writeMddToNormal(ModelStackWithTimelineCounter*);};
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
 for(int budget=0;budget<38;++budget){InstrumentClip c;failAfter=-1;c.noteRows.insertNoteRowAtY(42)->notes.insertAtKey(13,true);for(int i=0;i<16;++i)c.mdd.core.lanes[0].values[i]=60+i;failAfter=budget;
 assert(c.writeMddToNormal(&stack)==Error::INSUFFICIENT_RAM);assert(c.mddEnabled&&c.mddRequested&&c.stops==0&&c.noteRows.getNumElements()==1&&c.noteRows.getElement(0)->y==42&&c.noteRows.getElement(0)->notes.data[0].pos==13);}
 failAfter=-1;InstrumentClip c;c.inScaleMode=true;c.mdd.core.lanes[0].values.fill(61);assert(c.writeMddToNormal(&stack)==Error::NONE);assert(!c.mddEnabled&&!c.mddRequested&&c.lastProcessedPos==37&&!c.currentlyPlayingReversed&&c.sequenceDirectionMode==SequenceDirection::FORWARD&&c.stops==1&&c.resumes==1);
 auto&notes=c.noteRows.getElement(0)->notes.data;assert(c.noteRows.getElement(0)->y==60&&notes.size()==16);for(int i=0;i<16;++i)assert(notes[i].pos==i*6&&notes[i].length==3&&notes[i].velocity==100);
 InstrumentClip restored;restored.noteRows.insertNoteRowAtY(42)->notes.insertAtKey(13,true);restored.noteRows.getElement(0)->expression=77;restored.yScroll=8;restored.paramManager.patched.params[0].value=456;
 restored.paramManager.patched.params[0].nodes.insertAtKey(5,true);restored.paramManager.patched.params[0].nodes.getElement(0)->value=789;
 restored.mdd.core.lanes[3].values[0]=12345;
 assert(restored.writeMddToNormal(&stack)==Error::NONE);assert(restored.mddBakedCustom==15);assert(restored.paramManager.patched.params[0].nodes.getNumElements()==2);
 ModelStack undoStack;auto*cons=actionLogger.action.cons;
 assert(cons->revert(TimeType::BEFORE,&undoStack)==Error::NONE);assert(!restored.mddEnabled&&restored.noteRows.getElement(0)->y==42&&restored.noteRows.getElement(0)->notes.data[0].pos==13&&restored.noteRows.getElement(0)->expression==77&&restored.sequenceDirectionMode==SequenceDirection::REVERSE&&restored.yScroll==8);
 assert(restored.mddBakedCustom==0&&restored.paramManager.patched.params[0].value==456&&restored.paramManager.patched.params[0].nodes.getElement(0)->value==789);
 assert(cons->revert(TimeType::AFTER,&undoStack)==Error::NONE);assert(restored.mddBakedCustom==15&&restored.paramManager.patched.params[0].nodes.getNumElements()==2);assert(restored.noteRows.getElement(0)->y==60&&restored.noteRows.getElement(0)->notes.data.size()==16&&restored.sequenceDirectionMode==SequenceDirection::FORWARD);delete cons;
 actionLogger.fail=true;InstrumentClip failed;failed.noteRows.insertNoteRowAtY(44);assert(failed.writeMddToNormal(&stack)==Error::INSUFFICIENT_RAM&&failed.mddEnabled&&failed.noteRows.getElement(0)->y==44);actionLogger.fail=false;
 InstrumentClip empty;empty.mdd.core.lanes[0].muted=65535;assert(empty.writeMddToNormal(&stack)==Error::NONE&&empty.noteRows.getNumElements()==0);
 // Reconstructed native automation must equal all four independent Custom streams.
 InstrumentClip curves;curves.mdd.needsInitialSpeed=false;
 for(int lane=3;lane<7;++lane){auto&l=curves.mdd.core.lanes[lane];for(int i=0;i<16;++i)l.values[i]=(lane*5678+i*3456)%65536;l.length=lane+2;l.shift=lane;l.ticks=lane+1;l.tickDenominator=2;l.direction=snake::Direction(lane-3);}
 curves.mdd.core.lanes[5].muted=0x55;auto expected=curves.mdd.core;
 assert(curves.writeMddToNormal(&stack)==Error::NONE);
 for(int t=0;t<96;++t){auto frame=expected.advance(t);for(int lane=0;lane<4;++lane){auto&ap=curves.paramManager.patched.params[lane];int32_t value=ap.value;for(auto&n:ap.nodes.data)if(n.pos<=t)value=n.value;assert(value==snake::normalizedQ31(frame.custom[lane]));}}
 assert(AudioEngine::services>0);
 std::cout<<"PASS actual WRITE: rollback at 38 allocation points including automation nodes, native consequence undo/redo restores notes/row expression/direction and rejects missing action, stock row commit, scale, gates, velocity, empty sequence, preserved transport phase, audio service without user-action reentrancy\n";
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
