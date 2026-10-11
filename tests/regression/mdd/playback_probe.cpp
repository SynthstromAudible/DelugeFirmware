#include "sequencer/mdd/controller.hpp"
#include <cassert>
#include <iostream>
#include <vector>
constexpr int MIDI_CHANNEL_NONE=-1,kNumExpressionDimensions=3;
enum class OutputType{SYNTH,MIDI_OUT,CV,KIT};
struct Key{int degreeOf(int n){return (n%12==0||n%12==4||n%12==7)?1:-1;}};
struct Song{Key key;int quarter=24;int getQuarterNoteLength(){return quarter;}};
Song defaultSong;Song*currentSong=&defaultSong;
struct ModelStackWithThreeMainThings{};
struct ModelStackWithTimelineCounter{Song* song;ModelStackWithThreeMainThings* addOtherTwoThingsButNoNoteRow(void*,void*){static ModelStackWithThreeMainThings s;return &s;}};
struct Sound{const void* mddClipOwner=nullptr;bool mddClipActive=false;std::array<int32_t,4> v{};void setSnakeCustomValues(std::array<int32_t,4> x){v=x;}};
struct MelodicInstrument{void* activeClip=nullptr;OutputType type=OutputType::SYNTH;Sound sound;std::vector<int> events;void* toModControllable(){return &sound;}void sendNote(ModelStackWithThreeMainThings*,bool on,int note,const int16_t*,int=-1,uint8_t=64){events.push_back(on?note+1:-note-1);}};
struct Clock{bool active=true;void expectEvent(){}bool isEitherClockActive(){return active;}}playbackHandler;
struct InstrumentClip{snake::Controller mdd;MelodicInstrument* output;int paramManager=0,ticksTilNextNoteRowEvent=0,lastProcessedPos=0,loopLength=96,repeatCount=0,mddNormalPos=0,mddNormalRepeat=0;bool mddNormalReversed=false,currentlyPlayingReversed=false;bool inScaleMode=false,mddNoteOn=false,mddEnabled=false,mddRequested=false,mddPending=false;uint64_t mddTick=0,mddLastProcessed=UINT64_MAX,mddOffAt=0;uint8_t mddNote=60;bool isActiveOnOutput()const{return output->activeClip==this;}void requestMdd(bool);void closeMdd(ModelStackWithTimelineCounter*);void processMdd(ModelStackWithTimelineCounter*);int mddQuantize(int,Song*)const;};
int InstrumentClip::mddQuantize(int note, Song* song) const {
	if (!inScaleMode)
		return note;
	for (int d = 0; d < 12; ++d) {
		if (note - d >= 0 && song->key.degreeOf(note - d) >= 0)
			return note - d;
		if (note + d <= 127 && song->key.degreeOf(note + d) >= 0)
			return note + d;
	}
	return note;
}
void InstrumentClip::closeMdd(ModelStackWithTimelineCounter* stack) {
    if (!output) { mddNoteOn = false; return; }
	if (mddNoteOn && output->type != OutputType::KIT) {
		auto* model = stack->addOtherTwoThingsButNoNoteRow(output->toModControllable(), &paramManager);
		static_cast<MelodicInstrument*>(output)->sendNote(model, false, mddNote, nullptr);
	}
	mddNoteOn = false;
	if (output->type == OutputType::SYNTH) {
		auto* sound = static_cast<Sound*>(output->toModControllable());
        if (sound->mddClipOwner == this) {
            sound->mddClipActive = false;
            sound->mddClipOwner = nullptr;
            sound->setSnakeCustomValues({});
        }
	}
}
void InstrumentClip::processMdd(ModelStackWithTimelineCounter* stack) {
	if (!output || !isActiveOnOutput() || output->type == OutputType::KIT)
		return;
	auto* model = stack->addOtherTwoThingsButNoNoteRow(output->toModControllable(), &paramManager);
	auto* instrument = static_cast<MelodicInstrument*>(output);
	if (mddNoteOn && mddTick >= mddOffAt) {
		instrument->sendNote(model, false, mddNote, nullptr);
		mddNoteOn = false;
	}
	mdd.activateClock(stack->song->getQuarterNoteLength());
	auto frame = mdd.core.advance(mddTick);
	if (mddLastProcessed == mddTick)
		frame.trigger = false;
	mddLastProcessed = mddTick;
	if (output->type == OutputType::SYNTH) {
		auto* sound = static_cast<Sound*>(output->toModControllable());
		sound->mddClipActive = true;
        sound->mddClipOwner = this;
		std::array<int32_t, 4> values{};
		for (int i = 0; i < 4; ++i)
			values[i] = snake::normalizedQ31(frame.custom[i]);
		sound->setSnakeCustomValues(values);
	}
	if (frame.trigger) {
		if (mddNoteOn)
			instrument->sendNote(model, false, mddNote, nullptr);
		mddNote = mddQuantize(frame.note, stack->song);
		int16_t mpe[kNumExpressionDimensions]{};
		instrument->sendNote(model, true, mddNote, mpe, MIDI_CHANNEL_NONE, frame.velocity);
		mddNoteOn = true;
		mddOffAt = mddTick + frame.duration;
	}
}
void InstrumentClip::requestMdd(bool enabled) {
    if (!output) return;
	if (output->type != OutputType::SYNTH && output->type != OutputType::MIDI_OUT && output->type != OutputType::CV)
		return;
	if (enabled) mdd.activateClock(currentSong->getQuarterNoteLength());
	else mdd.syncClock(currentSong->getQuarterNoteLength());
	mddRequested = enabled;
	mddPending = enabled != mddEnabled;
	ticksTilNextNoteRowEvent = 0;
	playbackHandler.expectEvent();
	if (!playbackHandler.isEitherClockActive()) {
		if (enabled && !mddEnabled) {
			mddNormalPos = lastProcessedPos;
			mddNormalRepeat = repeatCount;
			mddNormalReversed = currentlyPlayingReversed;
		}
		if (!enabled && mddEnabled) {
			lastProcessedPos = std::clamp<int32_t>(mddNormalPos, 0, loopLength - 1);
			repeatCount = mddNormalRepeat;
			currentlyPlayingReversed = mddNormalReversed;
		}
		mddEnabled = enabled;
		mddPending = false;
	}
}
int main(){
 {Song coldSong;coldSong.quarter=96;currentSong=&coldSong;InstrumentClip cold;MelodicInstrument output;cold.output=&output;output.activeClip=&cold;ModelStackWithTimelineCounter coldStack{&coldSong};cold.requestMdd(true);
 for(auto&lane:cold.mdd.core.lanes)assert(lane.ticks==24&&lane.tickDenominator==1&&cold.mdd.speedIndex(lane)==2);
 cold.processMdd(&coldStack);assert(output.events.size()==1);cold.mddTick=6;cold.processMdd(&coldStack);assert(output.events.size()==1);cold.mddTick=12;cold.processMdd(&coldStack);assert(output.events.size()==2);cold.mddTick=24;cold.processMdd(&coldStack);assert(output.events.size()==3);
 currentSong=&defaultSong;}
 InstrumentClip c;MelodicInstrument out;c.output=&out;out.activeClip=&c;Song song;ModelStackWithTimelineCounter stack{&song};c.mdd.core.lanes[3].values.fill(12345);c.requestMdd(true);assert(c.mddPending&&!c.mddEnabled);c.processMdd(&stack);assert(out.events.size()==1&&out.events[0]==61&&out.sound.mddClipActive&&out.sound.v[0]>0);c.processMdd(&stack);assert(out.events.size()==1);c.mddTick=3;c.processMdd(&stack);assert(out.events.size()==2&&out.events[1]==-61);c.mddTick=6;c.processMdd(&stack);assert(out.events.size()==3);InstrumentClip inactive;inactive.output=&out;inactive.closeMdd(&stack);assert(out.sound.mddClipActive&&out.sound.mddClipOwner==&c&&out.sound.v[0]>0);c.closeMdd(&stack);assert(out.events.size()==4&&!out.sound.mddClipActive&&out.sound.v[0]==0&&c.mddTick==6&&out.sound.mddClipOwner==nullptr);c.mddTick=12;c.mdd.core.lanes[1].values.fill(0);c.processMdd(&stack);assert(out.events.size()==4);c.inScaleMode=true;assert(c.mddQuantize(61,&song)==60);playbackHandler.active=false;c.requestMdd(false);assert(!c.mddEnabled&&!c.mddPending);c.lastProcessedPos=13;c.repeatCount=3;c.currentlyPlayingReversed=true;c.requestMdd(true);assert(c.mddEnabled&&c.mddNormalPos==13);c.lastProcessedPos=69;c.repeatCount=9;c.currentlyPlayingReversed=false;c.requestMdd(false);assert(c.lastProcessedPos==13&&c.repeatCount==3&&c.currentlyPlayingReversed);std::cout<<"PASS actual InstrumentClip bridge: note-on/off, gate duration, duplicate tick, Custom output, cleanup, root/scale and deferred selection\n";}
