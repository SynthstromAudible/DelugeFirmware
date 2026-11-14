/// @file
/// @brief What a followed CC does, driven through `PlaybackHandler::midiCCReceived`: which clip's owner it moves a
/// param
///        of (a synth, a kit's selected row or, with affect-entire, the kit's chain), the clone the arrangement makes
///        before the CC is looked up, how a held step region is written, and which params the feedback scan sends.
///
/// The writes are observed from outside: the undo log lists each param written, every value is read back through the
/// param's address, and a feedback CC leaves the sample time it was sent at in `MidiFollow::timeLastCCSent`.

#include "gui/ui/ui.h"
#include "gui/views/session_view.h"
#include "gui/views/view.h"
#include "io/midi/midi_device_manager.h"
#include "io/midi/midi_engine.h"
#include "io/midi/midi_follow.h"
#include "model/action/action.h"
#include "model/action/action_logger.h"
#include "model/clip/instrument_clip.h"
#include "model/consequence/consequence_param_change.h"
#include "model/instrument/kit.h"
#include "model/model_stack.h"
#include "model/project_bridge/controls_bridge.h"
#include "model/project_bridge/param_stack.h"
#include "model/song/song.h"
#include "modulation/automation/auto_param.h"
#include "modulation/params/param.h"
#include "modulation/params/param_collection.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_set.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <vector>

// Before <cppspec.hpp>: it #defines `it`, which collides with locals named `it` in the project headers.
#include "support/learned_fixture.h"
#include "support/song_fixture.h"

#include "cppspec.hpp"

#include "spec/support/spec_main.h"

namespace spec = deluge::scenes::spec;
namespace params = deluge::modulation::params;
namespace project = deluge::project;

using project::OwnerId;
using project::ParamAddress;
using namespace deluge::scenes::spec::learned;

namespace {

/// The CC every example follows, a second one, and the channel they come on (follow channel A).
constexpr uint8_t kCc = 5;
constexpr uint8_t kOtherCc = 6;
constexpr uint8_t kChannel = 0;
/// A CC value that is knob position 36.
constexpr uint8_t kValue = 100;

MIDICable& din() {
	return MIDIDeviceManager::dinMIDIPorts;
}

/// The target of the sound param @p p, as follow's sound table names it.
project::ParamTarget soundParam(int32_t p) {
	return project::engine_target(
	    deluge::bridge::controlTargetOf(deluge::modulation::knobParam(p), params::Kind::UNPATCHED_SOUND).value);
}

/// The target of the unpatched param @p p of @p kind, as the tables name it.
project::ParamTarget unpatched(params::Kind kind, int32_t p) {
	return project::engine_target(
	    deluge::bridge::controlTargetOf(deluge::modulation::knobParam(params::UNPATCHED_START + p), kind).value);
}

ParamAddress addressOf(OwnerId owner, project::ParamTarget target, const Clip* clip) {
	return ParamAddress{.owner = owner,
	                    .space = target.space,
	                    .id = target.id,
	                    .clip_id = clip != nullptr ? std::optional<uint32_t>{clip->id()} : std::nullopt};
}

/// The lane's current value as a knob position, -64..64 (a lane the song does not hold reads INT32_MIN).
int32_t knobPosOf(Song& song, const ParamAddress& address) {
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	ModelStackWithAutoParam* stack = deluge::bridge::paramStackExact(song, address, memory);
	return stack != nullptr && stack->autoParam != nullptr
	           ? stack->paramCollection->paramValueToKnobPos(stack->autoParam->getCurrentValue(), stack)
	           : INT32_MIN;
}

/// Put the lane at knob position @p pos with no undo step and no automation, as a preset's own value is set.
void putKnobAt(Song& song, const ParamAddress& address, int32_t pos) {
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	ModelStackWithAutoParam* stack = deluge::bridge::paramStackExact(song, address, memory);
	if (stack != nullptr && stack->autoParam != nullptr) {
		stack->autoParam->setCurrentValueWithNoReversionOrRecording(
		    stack, stack->paramCollection->knobPosToParamValue(pos, stack));
	}
}

/// Whether the song holds @p address's lane with automation on it.
bool automated(Song& song, const ParamAddress& address) {
	const project::DocAutoParam* lane = song.project.resolveParam(address);
	return lane != nullptr && !lane->nodes.empty();
}

/// How many params the undo log holds a step for.
size_t writeCount() {
	size_t count = 0;
	for (const Action* action = actionLogger.firstAction[BEFORE]; action != nullptr; action = action->nextAction) {
		for (const Consequence* step = action->firstConsequence; step != nullptr; step = step->next) {
			count += step->type == Consequence::PARAM_CHANGE ? 1 : 0;
		}
	}
	return count;
}

/// Send a CC on DIN as the app's MIDI input does.
void sendCc(uint8_t channel, uint8_t number, uint8_t value) {
	bool thru = false;
	playbackHandler.midiCCReceived(din(), channel, number, value, &thru);
}

/// MIDI follow's own state, put to what an example needs and back: channel A on DIN channel 0, no CC followed, JUMP
/// takeover, no feedback, no popups.
struct FollowWorld {
	LearnedMIDI savedChannel = midiEngine.midiFollowChannelType[0];
	std::array<std::optional<project::ParamTarget>, 128> savedSound = midiFollow.ccToSoundTarget;
	std::array<std::optional<project::ParamTarget>, 128> savedGlobal = midiFollow.ccToGlobalTarget;
	std::array<int32_t, 128> savedPrevious{};
	std::array<uint32_t, 128> savedSent{};
	MIDITakeoverMode savedMode = midiEngine.midiTakeover;
	MIDIFollowFeedbackChannelType savedFeedback = midiEngine.midiFollowFeedbackChannelType;
	MIDIFollowFeedbackAutomationMode savedAutomation = midiEngine.midiFollowFeedbackAutomation;
	bool savedFilter = midiEngine.midiFollowFeedbackFilter;
	bool savedDisplay = midiEngine.midiFollowDisplayParam;
	uint32_t savedTimer = AudioEngine::audioSampleTimer;

	FollowWorld() {
		std::copy(std::begin(midiFollow.previousKnobPos), std::end(midiFollow.previousKnobPos), savedPrevious.begin());
		std::copy(std::begin(midiFollow.timeLastCCSent), std::end(midiFollow.timeLastCCSent), savedSent.begin());
		midiEngine.midiFollowChannelType[0].clear();
		midiEngine.midiFollowChannelType[0].cable = &din();
		midiEngine.midiFollowChannelType[0].channelOrZone = kChannel;
		midiFollow.ccToSoundTarget.fill(std::nullopt);
		midiFollow.ccToGlobalTarget.fill(std::nullopt);
		std::fill(std::begin(midiFollow.previousKnobPos), std::end(midiFollow.previousKnobPos), kNoSelection);
		std::fill(std::begin(midiFollow.timeLastCCSent), std::end(midiFollow.timeLastCCSent), 0u);
		midiEngine.midiTakeover = MIDITakeoverMode::JUMP;
		midiEngine.midiFollowFeedbackChannelType = MIDIFollowFeedbackChannelType::NONE;
		midiEngine.midiFollowDisplayParam = false;
	}
	~FollowWorld() {
		midiEngine.midiFollowChannelType[0] = savedChannel;
		midiFollow.ccToSoundTarget = savedSound;
		midiFollow.ccToGlobalTarget = savedGlobal;
		std::copy(savedPrevious.begin(), savedPrevious.end(), std::begin(midiFollow.previousKnobPos));
		std::copy(savedSent.begin(), savedSent.end(), std::begin(midiFollow.timeLastCCSent));
		midiEngine.midiTakeover = savedMode;
		midiEngine.midiFollowFeedbackChannelType = savedFeedback;
		midiEngine.midiFollowFeedbackAutomation = savedAutomation;
		midiEngine.midiFollowFeedbackFilter = savedFilter;
		midiEngine.midiFollowDisplayParam = savedDisplay;
		AudioEngine::audioSampleTimer = savedTimer;
	}
	FollowWorld(const FollowWorld&) = delete;
	FollowWorld& operator=(const FollowWorld&) = delete;
};

/// The arrangement recording @p f's clip at 0, for as long as it lives.
struct ArrangementRecording {
	Transport transport{RecordingMode::ARRANGEMENT};
	explicit ArrangementRecording(const spec::SongFixture& f) {
		f.clip->activeIfNoSolo = true;
		const int32_t at = f.instrument->clipInstances.insertSorted(0).value_or(-1);
		if (at >= 0) {
			f.instrument->clipInstances[at].clip = f.clip;
			f.instrument->clipInstances[at].length = f.clip->loopLength();
		}
	}
};

/// A song with a synth, and a kit with two sound rows, each the active clip of its output; the synth's clip is the
/// current one.
struct Songs {
	Song* song = nullptr;
	spec::SongFixture synth, kit;
	SoundDrum *rowA = nullptr, *rowB = nullptr;
	Songs() {
		song = spec::detail::make_bare_song();
		kit = addKit(song);
		rowA = addSoundDrum(kit);
		rowB = addSoundDrum(kit);
		addRow(kit, rowA);
		addRow(kit, rowB);
		synth = addSynth(song);
		activate(*song, *synth.instrument, *synth.clip);
		activate(*song, *kit.instrument, *kit.clip);
		useSynth();
	}
	void useSynth() const { song->setCurrentClip(synth.clip); }
	/// The kit's clip current, with affect-entire @p entire and @p rowA the selected row.
	void useKit(bool entire) const {
		kit.clip->affectEntire = entire;
		static_cast<Kit*>(kit.instrument)->selectedDrum = rowA;
		song->setCurrentClip(kit.clip);
	}
	[[nodiscard]] ParamAddress synthLpf() const {
		return addressOf(OwnerId::instrument(*synth.instrument->id()), soundParam(params::LOCAL_LPF_FREQ), synth.clip);
	}
	[[nodiscard]] ParamAddress rowLpf(const SoundDrum& row) const {
		return addressOf(OwnerId::drum(*row.id()), soundParam(params::LOCAL_LPF_FREQ), kit.clip);
	}
	[[nodiscard]] ParamAddress kitLpf() const {
		return addressOf(OwnerId::instrument(*kit.instrument->id()),
		                 unpatched(params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_LPF_FREQ), kit.clip);
	}
	void zero(const ParamAddress& address) const { putKnobAt(*song, address, 0); }
};

} // namespace

// clang-format off
describe midi_follow("a followed CC through PlaybackHandler::midiCCReceived", $ {
	it("moves the param a synth's CC names, in one undo step, and marks no instrument edited", _ {
		FollowWorld follow;
		Songs s;
		RootView root;
		midiFollow.ccToSoundTarget[kCc] = soundParam(params::LOCAL_LPF_FREQ);
		s.zero(s.synthLpf());
		actionLogger.deleteAllLogs();
		sendCc(kChannel, kCc, kValue);
		expect(knobPosOf(*s.song, s.synthLpf())).to_equal(36);
		expect(static_cast<int>(writeCount())).to_equal(1);
		expect(static_cast<SoundInstrument*>(s.synth.instrument)->editedByUser).to_be_false();
	});

	it("moves a kit's selected row's param without affect-entire, by the sound table, and the kit's own with it, by the global table", _ {
		FollowWorld follow;
		Songs s;
		RootView root;
		midiFollow.ccToSoundTarget[kCc] = soundParam(params::LOCAL_LPF_FREQ);
		midiFollow.ccToGlobalTarget[kCc] = unpatched(params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_LPF_FREQ);
		for (const ParamAddress& lane : {s.rowLpf(*s.rowA), s.rowLpf(*s.rowB), s.kitLpf()}) {
			s.zero(lane);
		}
		s.useKit(false);
		actionLogger.deleteAllLogs();
		sendCc(kChannel, kCc, kValue);
		expect(knobPosOf(*s.song, s.rowLpf(*s.rowA))).to_equal(36);
		expect(knobPosOf(*s.song, s.rowLpf(*s.rowB))).to_equal(0);
		expect(knobPosOf(*s.song, s.kitLpf())).to_equal(0);

		s.zero(s.rowLpf(*s.rowA));
		s.useKit(true);
		sendCc(kChannel, kCc, 0);
		sendCc(kChannel, kCc, kValue);
		expect(knobPosOf(*s.song, s.kitLpf())).to_equal(36);
		expect(knobPosOf(*s.song, s.rowLpf(*s.rowA))).to_equal(0);
	});

	it("refuses a kit row the portamento a synth follows", _ {
		FollowWorld follow;
		Songs s;
		RootView root;
		midiFollow.ccToSoundTarget[kCc] = unpatched(params::Kind::UNPATCHED_SOUND, params::UNPATCHED_PORTAMENTO);
		const ParamAddress synthPorta =
		    addressOf(OwnerId::instrument(*s.synth.instrument->id()), *midiFollow.ccToSoundTarget[kCc], s.synth.clip);
		const ParamAddress rowPorta =
		    addressOf(OwnerId::drum(*s.rowA->id()), *midiFollow.ccToSoundTarget[kCc], s.kit.clip);
		s.zero(synthPorta);
		s.zero(rowPorta);
		actionLogger.deleteAllLogs();
		s.useKit(false);
		sendCc(kChannel, kCc, kValue);
		expect(knobPosOf(*s.song, rowPorta)).to_equal(0);
		expect(static_cast<int>(writeCount())).to_equal(0);
		s.useSynth();
		sendCc(kChannel, kCc, kValue);
		expect(knobPosOf(*s.song, synthPorta)).to_equal(36);
	});

	it("keeps the position a CC last had between messages, as a PICKUP takeover reads it", _ {
		FollowWorld follow;
		Songs s;
		RootView root;
		midiEngine.midiTakeover = MIDITakeoverMode::PICKUP;
		midiFollow.ccToSoundTarget[kCc] = soundParam(params::LOCAL_LPF_FREQ);
		putKnobAt(*s.song, s.synthLpf(), -40);
		expect(midiFollow.previousKnobPos[kCc] == kNoSelection).to_be_true();
		sendCc(kChannel, kCc, kValue); // the first value seeds the state; the knob is not met yet
		expect(midiFollow.previousKnobPos[kCc] != kNoSelection).to_be_true();
		expect(knobPosOf(*s.song, s.synthLpf())).to_equal(-40);
		sendCc(kChannel, kCc, 1); // the controller crosses the knob: it is picked up
		expect(knobPosOf(*s.song, s.synthLpf())).to_equal(-63);
	});

	it("clones the clip the arrangement records into its take whether or not the CC follows anything", _ {
		FollowWorld follow;
		Songs s;
		RootView root;
		const size_t arranged = s.song->live.clips.arrangement_only_clips.size();
		ArrangementRecording recording{s.synth};
		sendCc(kChannel, kOtherCc, kValue); // follows nothing
		Clip* const take = s.synth.instrument->getActiveClip();
		expect(s.song->live.clips.arrangement_only_clips.size()).to_equal(arranged + 1);
		expect(take != nullptr && take != s.synth.clip && take->beingRecordedFromClip == s.synth.clip).to_be_true();
	});

	it("writes a held step region into the take, the region read before the clone", _ {
		FollowWorld follow;
		Songs s;
		RootView root;
		midiFollow.ccToSoundTarget[kCc] = soundParam(params::LOCAL_LPF_FREQ);
		s.zero(s.synthLpf());
		HeldRegion region{*s.synth.clip, 96, 48};
		ArrangementRecording recording{s.synth};
		sendCc(kChannel, kCc, kValue);
		Clip* const take = s.synth.instrument->getActiveClip();
		expect(take != nullptr && take != s.synth.clip).to_be_true();
		if (take == nullptr || take == s.synth.clip) {
			return;
		}
		ParamAddress onTake = s.synthLpf();
		onTake.clip_id = take->id();
		expect(automated(*s.song, onTake)).to_be_true();
		expect(automated(*s.song, s.synthLpf())).to_be_false();
	});
});

describe midi_follow_feedback("the feedback scan sends the CCs of the params the clip's owner follows", $ {
	/// Follow channel A is the feedback channel, with two params followed, the first automated.
	struct Scanned {
		FollowWorld follow;
		Songs s;
		RootView root;
		Scanned() {
			midiEngine.midiFollowFeedbackChannelType = MIDIFollowFeedbackChannelType::A;
			midiFollow.ccToSoundTarget[kCc] = soundParam(params::LOCAL_LPF_FREQ);
			midiFollow.ccToSoundTarget[kOtherCc] = soundParam(params::LOCAL_HPF_FREQ);
			{
				HeldRegion region{*s.synth.clip, 96, 48};
				sendCc(kChannel, kCc, kValue); // writes the first into automation
			}
			AudioEngine::audioSampleTimer = 1234;
		}
		[[nodiscard]] static uint32_t sentAt(uint8_t cc) { return midiFollow.timeLastCCSent[cc]; }
	};

	it("sends every followed param", _ {
		Scanned w;
		midiFollow.sendCCWithoutModelStackForMidiFollowFeedback(false);
		expect(Scanned::sentAt(kCc)).to_equal(1234u);
		expect(Scanned::sentAt(kOtherCc)).to_equal(1234u);
		expect(Scanned::sentAt(7)).to_equal(0u);
	});

	it("sends only the automated params for the automation feedback", _ {
		Scanned w;
		midiFollow.sendCCWithoutModelStackForMidiFollowFeedback(true);
		expect(Scanned::sentAt(kCc)).to_equal(1234u);
		expect(Scanned::sentAt(kOtherCc)).to_equal(0u);
	});

	it("sends no automation feedback while a step region is held, and the values of the other feedback", _ {
		Scanned w;
		HeldRegion region{*w.s.synth.clip, 0, 48};
		midiFollow.sendCCWithoutModelStackForMidiFollowFeedback(true);
		expect(Scanned::sentAt(kCc)).to_equal(0u);
		midiFollow.sendCCWithoutModelStackForMidiFollowFeedback(false);
		expect(Scanned::sentAt(kCc)).to_equal(1234u);
	});

	it("sends a kit's chain params under affect-entire and a row's otherwise", _ {
		Scanned w;
		midiFollow.ccToGlobalTarget[7] = unpatched(params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_LPF_FREQ);
		w.s.useKit(true);
		midiFollow.sendCCWithoutModelStackForMidiFollowFeedback(false);
		expect(Scanned::sentAt(7)).to_equal(1234u);
		expect(Scanned::sentAt(kCc)).to_equal(0u);
		w.s.useKit(false);
		std::fill(std::begin(midiFollow.timeLastCCSent), std::end(midiFollow.timeLastCCSent), 0u);
		midiFollow.sendCCWithoutModelStackForMidiFollowFeedback(false);
		expect(Scanned::sentAt(7)).to_equal(0u);
		expect(Scanned::sentAt(kCc)).to_equal(1234u);
	});
});
// clang-format on

DELUGE_SPEC(midi_follow_spec, midi_follow, midi_follow_feedback)
