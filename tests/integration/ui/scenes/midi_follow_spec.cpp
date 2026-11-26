/// @file
/// @brief What a followed CC does, driven through `PlaybackHandler::midiCCReceived`: which clip's owner it moves a
/// param
///        of (a synth, a kit's selected row or, with affect-entire, the kit's chain), the clone the arrangement makes
///        before the CC is looked up, how a held step region is written, and which params the feedback scan sends.
///
/// The writes are observed from outside: the undo log lists each param written, every value is read back through the
/// param's address, and a feedback CC leaves the sample time it was sent at in `MidiFollow::timeLastCCSent`.

#include "gui/ui/shell_host/host_ports.h"
#include "gui/ui/ui.h"
#include "gui/views/session_view.h"
#include "gui/views/view.h"
#include "instruments/classic/params.h"
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
#include "model/project_bridge/param_ids.h"
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
#include "project/mapping_table.h"
#include "ui/controls/knob_math.h"

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <vector>

// Before <cppspec.hpp>: it #defines `it`, which collides with locals named `it` in the project headers.
#include "support/learned_fixture.h"
#include "support/song_fixture.h"

#include "cppspec.hpp"

#include "support/spec_main.h"

using namespace deluge;

namespace spec = deluge::ui::scenes::spec;
namespace params = deluge::modulation::params;
namespace project = deluge::project;

using project::OwnerId;
using project::ParamAddress;
using namespace deluge::ui::scenes::spec::learned;

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
	/// A second session clip of the synth, not its active one, made the current clip.
	InstrumentClip* addInactiveClip() const {
		InstrumentClip* other = spec::detail::make_session_clip(song);
		ParamManagerForTimeline other_params;
		other_params.setupWithPatching();
		Sound::initParams(&other_params);
		other->setAudioInstrument(synth.instrument, song, true, &other_params);
		song->setCurrentClip(other);
		return other;
	}
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
	[[nodiscard]] ParamAddress rowHpf(const SoundDrum& row) const {
		return addressOf(OwnerId::drum(*row.id()), soundParam(params::LOCAL_HPF_FREQ), kit.clip);
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
		expect(midiFollow.previousKnobPos[kCc]).to_equal(kNoSelection, "the CC's position before a message");
		sendCc(kChannel, kCc, kValue); // the first value seeds the state; the knob is not met yet
		expect(midiFollow.previousKnobPos[kCc]).not_().to_equal(kNoSelection, "the CC's position after a message");
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
		expect(take).not_().to_be_null("the take");
		expect(take).not_().to_equal(static_cast<Clip*>(s.synth.clip), "the take");
		if (take != nullptr) {
			expect(take->beingRecordedFromClip).to_equal(static_cast<Clip*>(s.synth.clip), "the clip the take records from");
		}
	});

	it("clones nothing for a selected clip that is not its output's active clip", _ {
		FollowWorld follow;
		Songs s;
		const size_t arranged = s.song->live.clips.arrangement_only_clips.size();
		ArrangementRecording recording{s.synth};
		InstrumentClip* const other = s.addInactiveClip();
		Clip* const active = s.synth.instrument->getActiveClip();
		sendCc(kChannel, kOtherCc, kValue); // no root UI: the current clip is the one followed
		expect(static_cast<Clip*>(other)).not_().to_equal(active, "the inactive clip");
		expect(s.song->live.clips.arrangement_only_clips.size()).to_equal(arranged);
		expect(s.synth.instrument->getActiveClip()).to_equal(active, "the active clip");
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
		expect(take).not_().to_be_null("the take");
		expect(take).not_().to_equal(static_cast<Clip*>(s.synth.clip), "the take");
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
	/// "cc:position" of each CC the scan sends, space separated.
	auto fed = [](bool automation) {
		std::array<MidiFollow::FollowFeedback, 128> out;
		const size_t count = midiFollow.collectFollowFeedback(automation, out);
		std::string text;
		for (size_t i = 0; i < count; i++) {
			text += (text.empty() ? "" : " ") + std::to_string(out[i].cc) + ":" + std::to_string(out[i].knobPos);
		}
		return text;
	};
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
				sendCc(kChannel, kCc, kValue); // writes the first into automation, at knob position 36
			}
			AudioEngine::audioSampleTimer = 1234;
		}
		[[nodiscard]] static uint32_t sentAt(uint8_t cc) { return midiFollow.timeLastCCSent[cc]; }
		[[nodiscard]] int32_t hpf() const {
			return knobPosOf(*s.song, addressOf(OwnerId::instrument(*s.synth.instrument->id()),
			                                    soundParam(params::LOCAL_HPF_FREQ), s.synth.clip));
		}
	};

	it("sends every followed param, as the knob position it is at", _ {
		Scanned w;
		const int32_t at = knobPosOf(*w.s.song, w.s.synthLpf());
		expect(fed(false)).to_equal(std::to_string(kCc) + ":" + std::to_string(at) + " " + std::to_string(kOtherCc) + ":"
		                            + std::to_string(w.hpf()));
		midiFollow.sendCCWithoutModelStackForMidiFollowFeedback(false);
		expect(Scanned::sentAt(kCc)).to_equal(1234u);
		expect(Scanned::sentAt(kOtherCc)).to_equal(1234u);
		expect(Scanned::sentAt(7)).to_equal(0u);
	});

	it("sends only the automated params for the automation feedback", _ {
		Scanned w;
		expect(fed(true)).to_equal(std::to_string(kCc) + ":" + std::to_string(knobPosOf(*w.s.song, w.s.synthLpf())));
		midiFollow.sendCCWithoutModelStackForMidiFollowFeedback(true);
		expect(Scanned::sentAt(kCc)).to_equal(1234u);
		expect(Scanned::sentAt(kOtherCc)).to_equal(0u);
	});

	it("sends no automation feedback while a step region is held, and the value at its start for the other feedback", _ {
		Scanned w;
		HeldRegion region{*w.s.synth.clip, 96, 48};
		expect(fed(true)).to_equal(std::string{});
		expect(fed(false)).to_equal(std::string{"5:36 6:"} + std::to_string(w.hpf()));
		midiFollow.sendCCWithoutModelStackForMidiFollowFeedback(true);
		expect(Scanned::sentAt(kCc)).to_equal(0u);
	});

	it("sends a kit's chain params under affect-entire and a row's otherwise", _ {
		Scanned w;
		midiFollow.ccToGlobalTarget[7] = unpatched(params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_LPF_FREQ);
		const int32_t chain = knobPosOf(*w.s.song, w.s.kitLpf());
		w.s.useKit(true);
		expect(fed(false)).to_equal(std::string{"7:"} + std::to_string(chain));
		w.s.useKit(false);
		expect(fed(false)).to_equal(std::string{"5:"} + std::to_string(knobPosOf(*w.s.song, w.s.rowLpf(*w.s.rowA)))
		                            + " 6:" + std::to_string(knobPosOf(*w.s.song, w.s.rowHpf(*w.s.rowA))));
	});

	it("reports the clip it follows as it is while the arrangement records, not the take its CCs write to", _ {
		FollowWorld follow;
		Songs s;
		midiEngine.midiFollowFeedbackChannelType = MIDIFollowFeedbackChannelType::A;
		midiFollow.ccToSoundTarget[kCc] = soundParam(params::LOCAL_LPF_FREQ);
		s.zero(s.synthLpf());
		ArrangementRecording recording{s.synth};
		{
			RootView root;
			HeldRegion region{*s.synth.clip, 96, 48};
			sendCc(kChannel, kCc, kValue); // lands in the take, automated there
		}
		Clip* const take = s.synth.instrument->getActiveClip();
		expect(take).not_().to_be_null("the take");
		expect(take).not_().to_equal(static_cast<Clip*>(s.synth.clip), "the take");
		if (take == nullptr || take == s.synth.clip) {
			return;
		}
		ParamAddress onTake = s.synthLpf();
		onTake.clip_id = take->id();
		expect(automated(*s.song, onTake)).to_be_true();
		expect(automated(*s.song, s.synthLpf())).to_be_false();
		// With no root UI the followed clip is the current one: the session clip.
		expect(fed(false)).to_equal(std::string{"5:0"});
		expect(fed(true)).to_equal(std::string{});
	});
});

describe midi_follow_descriptors("the controls' knob arithmetic for every param MIDI follow can map", $ {
	/// What a descriptor mismatch reports: the param, and the value or position.
	auto check = [](Song& song, const ParamAddress& address, plugin::PluginId plugin, const std::string& name,
	                std::string& mismatches, int& compared) {
		alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
		ModelStackWithAutoParam* stack = deluge::bridge::paramStackExact(song, address, memory);
		if (stack == nullptr || stack->autoParam == nullptr) {
			mismatches += " [no lane: " + name + "]";
			return;
		}
		const plugin::ParamInfo* info = deluge::gui::shell_host::host_ports().controls->param_info(plugin, address.target());
		std::vector<int32_t> words{INT32_MIN, INT32_MIN + 1, -0x40000000, -1, 0, 1, 0x40000000, INT32_MAX - 1, INT32_MAX,
		                           0x12345678, -0x12345678};
		for (int32_t pos = -64; pos <= 64; pos++) {
			words.push_back(stack->paramCollection->knobPosToParamValue(pos, stack));
		}
		for (const int32_t word : words) {
			compared++;
			const int32_t legacy = stack->paramCollection->paramValueToKnobPos(word, stack);
			if (deluge::ui::controls::knob_pos_of(address, info, word) != legacy) {
				mismatches += " [" + name + " word " + std::to_string(word) + "]";
			}
		}
		for (int32_t pos = -64; pos <= 64; pos++) {
			compared++;
			if (deluge::ui::controls::value_of_knob(address, info, pos)
			    != stack->paramCollection->knobPosToParamValue(pos, stack)) {
				mismatches += " [" + name + " pos " + std::to_string(pos) + "]";
			}
		}
	};

	it("gives the legacy conversions, both ways, for every sound and global param a CC can be mapped to", _ {
		Songs s;
		std::string mismatches;
		int compared = 0;
		int params_checked = 0;
		const auto each = [&](deluge::bridge::OwnerPlugin owner, params::Kind kind, int32_t count) {
			for (int32_t index = 0; index < count; index++) {
				const std::optional<project::ParamTarget> target = deluge::bridge::targetOf(owner, kind, index);
				if (!target) {
					continue;
				}
				const std::string name = std::to_string(static_cast<int>(kind)) + "/" + std::to_string(index);
				params_checked++;
				if (owner == deluge::bridge::OwnerPlugin::INSTRUMENT) {
					check(*s.song, addressOf(OwnerId::instrument(*s.synth.instrument->id()), *target, s.synth.clip),
					      plugin::kDelugeInstrument, "synth " + name, mismatches, compared);
					check(*s.song, addressOf(OwnerId::drum(*s.rowA->id()), *target, s.kit.clip),
					      plugin::kDelugeInstrument, "row " + name, mismatches, compared);
				}
				else {
					check(*s.song, addressOf(OwnerId::instrument(*s.kit.instrument->id()), *target, s.kit.clip),
					      plugin::kGlobalEffectable, "kit " + name, mismatches, compared);
				}
			}
		};
		each(deluge::bridge::OwnerPlugin::INSTRUMENT, params::Kind::PATCHED, params::GLOBAL_NONE);
		each(deluge::bridge::OwnerPlugin::INSTRUMENT, params::Kind::UNPATCHED_SOUND, params::UNPATCHED_SOUND_MAX_NUM);
		each(deluge::bridge::OwnerPlugin::GLOBAL_EFFECTABLE, params::Kind::UNPATCHED_GLOBAL,
		     params::UNPATCHED_GLOBAL_MAX_NUM);
		expect(params_checked).to_be_greater_than(60, "the params checked");
		expect(compared).to_be_greater_than(10000, "the values compared");
		expect(mismatches).to_equal(std::string{});
	});

	it("maps no cable depth: a CC's targets are the sound's patched and unpatched params and the global effectable's", _ {
		for (int32_t index = 0; index < params::GLOBAL_NONE; index++) {
			const auto target = deluge::bridge::targetOf(deluge::bridge::OwnerPlugin::INSTRUMENT, params::Kind::PATCHED, index);
			if (target) {
				expect(deluge::instruments::classic::is_cable_depth(target->id)).to_be_false("param " + std::to_string(index) + "'s target");
			}
		}
		for (int32_t index = 0; index < params::UNPATCHED_SOUND_MAX_NUM; index++) {
			const auto target =
			    deluge::bridge::targetOf(deluge::bridge::OwnerPlugin::INSTRUMENT, params::Kind::UNPATCHED_SOUND, index);
			if (target) {
				expect(deluge::instruments::classic::is_cable_depth(target->id)).to_be_false("param " + std::to_string(index) + "'s target");
			}
		}
	});
});
// clang-format on

DELUGE_SPEC(midi_follow_spec, midi_follow, midi_follow_feedback, midi_follow_descriptors)
