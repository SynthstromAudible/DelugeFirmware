/// @file
/// @brief What a learned MIDI CC or pitch bend does, driven through `PlaybackHandler::midiCCReceived` and
///        `pitchBendReceived`: who is offered it and in what order, how takeover turns the message into a position, how
///        the write lands (the arrangement's take, a missing cable, no edited mark, one undo step), and the pitch
///        bend's mapping and consumption.
///
/// The writes are observed from outside the dispatch: the undo log lists each param written, in write order, and every
/// value is read back through the param's address. Nothing here reads an owner's knob list.

#include "gui/ui/ui.h"
#include "gui/views/session_view.h"
#include "gui/views/view.h"
#include "io/midi/mapping_index.h"
#include "io/midi/midi_device_manager.h"
#include "io/midi/midi_engine.h"
#include "model/action/action.h"
#include "model/action/action_logger.h"
#include "model/clip/audio_clip.h"
#include "model/clip/instrument_clip.h"
#include "model/consequence/consequence_param_change.h"
#include "model/drum/gate_drum.h"
#include "model/drum/midi_drum.h"
#include "model/edits/learn_mapping.h"
#include "model/fx/chain_params.h"
#include "model/instrument/kit.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/project_bridge/controls_bridge.h"
#include "model/project_bridge/param_stack.h"
#include "model/song/song.h"
#include "modulation/automation/auto_param.h"
#include "modulation/params/param.h"
#include "modulation/params/param_collection.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_set.h"
#include "playback/playback_handler.h"
#include "processing/audio_output.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"
#include "project/mapping_table.h"
#include "storage/storage_manager.h"
#include "synth/modulation/cable.h"

#include <algorithm>
#include <climits>
#include <optional>
#include <string>
#include <vector>

// Before <cppspec.hpp>: it #defines `it`, which collides with locals named `it` in the project headers.
#include "support/learned_fixture.h"
#include "support/song_fixture.h"

#include "cppspec.hpp"

#include "support/spec_main.h"

namespace spec = deluge::ui::scenes::spec;
namespace params = deluge::modulation::params;
namespace project = deluge::project;

using project::ControlsAddress;
using project::OwnerId;
using project::OwnerParamTarget;
using project::ParamAddress;
using project::ParamSpace;
using Command = project::MidiCommand;
using Kind = ControlsAddress::Kind;
using namespace deluge::ui::scenes::spec::learned;

namespace {

/// The CC every example learns and sends, and the channel it is on, unless it says otherwise.
constexpr uint8_t kCc = 1;
constexpr uint8_t kChannel = 0;
/// A bend is a knob's CC number 128.
constexpr uint8_t kBend = 128;
/// Where a lane's current value is read: its own, not a position in automation.
constexpr uint32_t kNoPos = 0xFFFFFFFF;

MIDICable& din() {
	return MIDIDeviceManager::dinMIDIPorts;
}

/// @brief A learned knob's source, as a knob's learn spells it: any device, no CC bias on the channel.
Command source(uint8_t channel, uint8_t number) {
	return Command{.channel_or_zone = channel, .note_or_cc = number};
}

/// @brief A patched param of a sound, as the table names it.
deluge::project::ParamTarget patched(int32_t p) {
	return deluge::project::engine_target(
	    deluge::bridge::controlTargetOf(deluge::modulation::knobParam(p), params::Kind::UNPATCHED_SOUND).id.value);
}

/// @brief An unpatched param of @p kind, as the table names it.
deluge::project::ParamTarget unpatched(params::Kind kind, int32_t p) {
	return deluge::project::engine_target(
	    deluge::bridge::controlTargetOf(deluge::modulation::knobParam(params::UNPATCHED_START + p), kind).id.value);
}

/// @brief The depth of the cable from @p source onto patched param @p p, as the table names it.
deluge::project::ParamTarget depthOf(int32_t p, PatchSource source) {
	return deluge::project::engine_target(
	    deluge::bridge::controlTargetOf(
	        deluge::modulation::knobTargetOf(deluge::synth::CableTarget::depth_of(static_cast<uint8_t>(p), source)),
	        params::Kind::UNPATCHED_SOUND)
	        .id.value);
}

/// @brief The address of @p id on @p owner, in @p clip's override (none: the owner's backed-up params).
ParamAddress addressOf(OwnerId owner, deluge::project::ParamTarget target, const Clip* clip) {
	return ParamAddress{.owner = owner,
	                    .space = target.space,
	                    .id = target.id,
	                    .clip_id = clip != nullptr ? std::optional<uint32_t>{clip->id()} : std::nullopt};
}

/// @brief The engine stack of exactly the lane @p address names, in @p memory.
ModelStackWithAutoParam* stackOf(Song& song, const ParamAddress& address, void* memory) {
	ModelStackWithAutoParam* stack = deluge::bridge::paramStackExact(song, address, memory);
	return stack != nullptr && stack->autoParam != nullptr ? stack : nullptr;
}

/// @brief The lane's current value, or none where the song holds no such lane.
std::optional<int32_t> valueOf(Song& song, const ParamAddress& address) {
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	const ModelStackWithAutoParam* stack = stackOf(song, address, memory);
	return stack != nullptr ? std::optional<int32_t>{stack->autoParam->getCurrentValue()} : std::nullopt;
}

/// @brief The lane's value as automation reads it at @p pos, or none where the song holds no such lane.
std::optional<int32_t> valueAtPos(Song& song, const ParamAddress& address, int32_t pos) {
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	ModelStackWithAutoParam* stack = stackOf(song, address, memory);
	return stack != nullptr ? std::optional<int32_t>{stack->autoParam->getValuePossiblyAtPos(pos, stack)}
	                        : std::nullopt;
}

/// @brief The lane's current value as a knob position, -64..64 (a lane the song does not hold reads INT32_MIN).
int32_t knobPosOf(Song& song, const ParamAddress& address) {
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	ModelStackWithAutoParam* stack = stackOf(song, address, memory);
	return stack != nullptr ? stack->paramCollection->paramValueToKnobPos(stack->autoParam->getCurrentValue(), stack)
	                        : INT32_MIN;
}

/// @brief Put the lane at knob position @p pos with no undo step and no automation, as a preset's own value is set.
void putKnobAt(Song& song, const ParamAddress& address, int32_t pos) {
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	ModelStackWithAutoParam* stack = stackOf(song, address, memory);
	if (stack != nullptr) {
		stack->autoParam->setCurrentValueWithNoReversionOrRecording(
		    stack, stack->paramCollection->knobPosToParamValue(pos, stack));
	}
}

/// @brief The value the lane takes when put at knob position @p pos, which it is left at.
int32_t valueForKnobPos(Song& song, const ParamAddress& address, int32_t pos) {
	putKnobAt(song, address, pos);
	return valueOf(song, address).value_or(0);
}

/// @brief Whether the song holds @p address's lane with automation on it.
bool automated(Song& song, const ParamAddress& address) {
	const project::DocAutoParam* lane = song.project.resolveParam(address);
	return lane != nullptr && !lane->nodes.empty();
}

/// @brief Every param the undo log holds a step for, in the order they were written.
std::vector<ParamAddress> writes() {
	std::vector<ParamAddress> out;
	for (const Action* action = actionLogger.firstAction[BEFORE]; action != nullptr; action = action->nextAction) {
		for (const Consequence* step = action->firstConsequence; step != nullptr; step = step->next) {
			if (step->type == Consequence::PARAM_CHANGE) {
				out.push_back(static_cast<const ConsequenceParamChange*>(step)->address);
			}
		}
	}
	std::ranges::reverse(out);
	return out;
}

/// @brief @p values, space separated.
std::string joined(const std::vector<int32_t>& values) {
	std::string out;
	for (const int32_t value : values) {
		out += (out.empty() ? "" : " ") + std::to_string(value);
	}
	return out;
}

/// @brief Send a CC on DIN as the app's MIDI input does, its index refreshed first.
void sendCc(uint8_t channel, uint8_t number, uint8_t value) {
	deluge::midi::refreshMappingIndex();
	bool thru = false;
	playbackHandler.midiCCReceived(din(), channel, number, value, &thru);
}

/// @brief Send a 14-bit pitch bend on DIN as the app's MIDI input does, its index refreshed first.
void sendBend(uint8_t channel, uint32_t value14) {
	deluge::midi::refreshMappingIndex();
	bool thru = false;
	playbackHandler.pitchBendReceived(din(), channel, static_cast<uint8_t>(value14 & 127),
	                                  static_cast<uint8_t>(value14 >> 7), &thru);
}

/// @brief The takeover mode, and whether the cable sends relative values, for one example.
struct Takeover {
	MIDITakeoverMode saved_mode = midiEngine.midiTakeover;
	bool saved_relative = din().is_relative;
	explicit Takeover(MIDITakeoverMode mode) { midiEngine.midiTakeover = mode; }
	~Takeover() {
		midiEngine.midiTakeover = saved_mode;
		din().is_relative = saved_relative;
	}
	Takeover(const Takeover&) = delete;
	Takeover& operator=(const Takeover&) = delete;
	static void cableIsRelative(bool relative) { din().is_relative = relative; }
};

/// @brief An MPE lower zone on DIN, channels 1..@p last its members, for one example.
struct MpeZone {
	MIDIPort& port = din().ports[MIDI_DIRECTION_INPUT_TO_DELUGE];
	uint8_t saved = port.mpeLowerZoneLastMemberChannel;
	explicit MpeZone(uint8_t last) { port.mpeLowerZoneLastMemberChannel = last; }
	~MpeZone() { port.mpeLowerZoneLastMemberChannel = saved; }
	MpeZone(const MpeZone&) = delete;
	MpeZone& operator=(const MpeZone&) = delete;
};

/// @brief One learned knob of the world: who owns it, what it turns, and where its lane is.
struct Lane {
	std::string name;
	OwnerParamTarget target;
	ParamAddress address;
	unsigned bit;
};

/// @brief The owners a world's knobs are learned on.
enum Owners : unsigned {
	SONG = 1,
	SYNTH = 2,
	KIT = 4,
	ROWS = 8,
	AUDIO = 16,
	IDLE = 32,
	ASLEEP = 64,
	EVERY = 127,
};

/// @brief A song with a synth, a kit (a sound row, a MIDI row, a gate row, another sound row, and a sound drum with no
///        row), an audio track, a synth whose clip is not active, a synth put to sleep, and the song's own knob.
///
/// Outputs are in play order synth, kit, audio, idle. The kit's rows are, in note-row order, B, the MIDI row, A, the
/// gate row; in its drum list A comes first.
struct World {
	Song* song = nullptr;
	spec::SongFixture synth, kit, idle;
	SoundInstrument* asleep = nullptr;
	AudioClip* audioClip = nullptr;
	AudioOutput* audio = nullptr;
	SoundDrum *rowA = nullptr, *rowB = nullptr, *stray = nullptr;

	[[nodiscard]] SoundInstrument& synthOut() const { return *static_cast<SoundInstrument*>(synth.instrument); }
	[[nodiscard]] Kit& kitOut() const { return *static_cast<Kit*>(kit.instrument); }
	[[nodiscard]] InstrumentClip& kitClip() const { return *kit.clip; }
	[[nodiscard]] InstrumentClip& synthClip() const { return *synth.clip; }
};

World makeWorld() {
	World w;
	w.song = spec::detail::make_bare_song();
	w.asleep = makeLooseSynth(*w.song);
	w.song->addOutput(w.asleep);
	w.idle = addSynth(w.song);
	w.audioClip = spec::detail::place<AudioClip>(*w.song);
	w.audioClip->setLoopLength(kDefaultClipLength << w.song->project.transport.inside_world_tick_magnitude);
	w.audioClip->paramManager.setupUnpatched();
	deluge::model::initGlobalParams(&w.audioClip->paramManager);
	(void)w.song->live.clips.session_clips.insertClipAt(w.audioClip, 0);
	w.audio = w.song->createNewAudioOutput();
	w.audioClip->output = w.audio;
	w.kit = addKit(w.song);
	w.rowA = addSoundDrum(w.kit);
	auto* midiRow = addPlainDrum<MIDIDrum>(w.kit);
	auto* gateRow = addPlainDrum<GateDrum>(w.kit);
	w.rowB = addSoundDrum(w.kit);
	w.stray = addSoundDrum(w.kit);
	addRow(w.kit, w.rowB);
	addRow(w.kit, midiRow);
	addRow(w.kit, w.rowA);
	addRow(w.kit, gateRow);
	w.synth = addSynth(w.song);
	activate(*w.song, *w.synth.instrument, *w.synth.clip);
	activate(*w.song, *w.kit.instrument, *w.kit.clip);
	activate(*w.song, *w.audio, *w.audioClip);
	w.song->setCurrentClip(w.synth.clip);
	return w;
}

/// @brief The world's lanes in the order their owners are offered a message: the song, the synth, the kit, its rows in
///        note-row order, the audio track; then the two that are not offered it.
std::vector<Lane> lanesOf(const World& w) {
	const auto lpf = patched(params::LOCAL_LPF_FREQ);
	const auto globalLpf = unpatched(params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_LPF_FREQ);
	const auto instrument = [](const Output& o) { return OwnerId::instrument(*o.id()); };
	const auto output = [](const Output& o) { return ControlsAddress{Kind::OUTPUT, *o.id()}; };
	const auto row = [&](const char* name, const SoundDrum& d) {
		return Lane{name, {{Kind::DRUM, *d.id()}, lpf}, addressOf(OwnerId::drum(*d.id()), lpf, w.kit.clip), ROWS};
	};
	std::vector<Lane> lanes;
	const auto songVolume = unpatched(params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_VOLUME);
	lanes.push_back({"song", {{Kind::SONG, 0}, songVolume}, addressOf(OwnerId::song(), songVolume, nullptr), SONG});
	lanes.push_back({"synth",
	                 {output(*w.synth.instrument), lpf},
	                 addressOf(instrument(*w.synth.instrument), lpf, w.synth.clip),
	                 SYNTH});
	lanes.push_back({"kit",
	                 {output(*w.kit.instrument), globalLpf},
	                 addressOf(instrument(*w.kit.instrument), globalLpf, w.kit.clip),
	                 KIT});
	lanes.push_back(row("rowB", *w.rowB));
	lanes.push_back(row("rowA", *w.rowA));
	lanes.push_back({"audio",
	                 {output(*w.audio), globalLpf},
	                 addressOf(OwnerId::clip(w.audioClip->id()), globalLpf, nullptr),
	                 AUDIO});
	lanes.push_back(
	    {"idle", {output(*w.idle.instrument), lpf}, addressOf(instrument(*w.idle.instrument), lpf, w.idle.clip), IDLE});
	lanes.push_back({"asleep", {output(*w.asleep), lpf}, addressOf(instrument(*w.asleep), lpf, nullptr), ASLEEP});
	return lanes;
}

/// @brief Learn @p number on @p channel onto the lanes of @p owners, put each at knob position 0, and put the sleeping
///        synth to sleep.
void learnAll(World& w, unsigned owners, uint8_t channel = kChannel, uint8_t number = kCc) {
	for (const Lane& lane : lanesOf(w)) {
		if ((lane.bit & owners) != 0) {
			deluge::edits::learnMappingFor(source(channel, number), lane.target);
		}
	}
	w.asleep->editedByUser = true;
	w.song->deleteOrHibernateOutput(w.asleep);
	for (const Lane& lane : lanesOf(w)) {
		putKnobAt(*w.song, lane.address, 0);
	}
	actionLogger.deleteAllLogs();
}

/// @brief What @p w's lanes sit at as knob positions, "name:pos" each.
std::string positions(const World& w) {
	std::string out;
	for (const Lane& lane : lanesOf(w)) {
		out += (out.empty() ? "" : " ") + lane.name + ":" + std::to_string(knobPosOf(*w.song, lane.address));
	}
	return out;
}

/// @brief The name @p w gives the lane @p address names, "@take" after it for a clip that is not the lane's own.
std::string labelOf(const World& w, const ParamAddress& address) {
	for (const Lane& lane : lanesOf(w)) {
		if (lane.address.owner == address.owner && address.space == ParamSpace::EXPRESSION) {
			return lane.name + ".expression";
		}
		if (lane.address.owner == address.owner && lane.address.id == address.id) {
			return lane.name + (lane.address.clip_id == address.clip_id ? "" : "@take");
		}
	}
	return "unknown";
}

/// @brief The names of the lanes the undo log holds a step for, in write order.
std::string written(const World& w) {
	std::string out;
	for (const ParamAddress& address : writes()) {
		out += (out.empty() ? "" : " ") + labelOf(w, address);
	}
	return out;
}

/// @brief A synth in the song, learned and active, for the takeover and pitch-bend examples.
struct Rig {
	spec::SongFixture f = spec::make_synth_song();
	OwnerParamTarget target{{Kind::OUTPUT, *f.instrument->id()}, patched(params::LOCAL_LPF_FREQ)};
	ParamAddress address = addressOf(OwnerId::instrument(*f.instrument->id()), target.param, f.clip);

	explicit Rig(uint8_t number = kCc) {
		activate(*f.song, *f.instrument, *f.clip);
		deluge::edits::learnMappingFor(source(kChannel, number), target);
	}
	[[nodiscard]] int32_t pos() const { return knobPosOf(*f.song, address); }
	void at(int32_t knobPos) const { putKnobAt(*f.song, address, knobPos); }
	/// @brief Send @p value as a CC, the undo log empty first.
	void send(uint8_t value) const {
		actionLogger.deleteAllLogs();
		sendCc(kChannel, kCc, value);
	}
	[[nodiscard]] size_t steps() const { return writes().size(); }
	[[nodiscard]] bool relativeInTable() const {
		const project::Mapping* entry = f.song->project.mappings.find(target);
		return entry != nullptr && entry->relative;
	}
	/// @brief Tell the knob its controller is absolute, with no write: the param at 0 hears a 64, which clears the
	///        relative flag and moves nothing.
	void absolute() const {
		at(0);
		send(64);
		actionLogger.deleteAllLogs();
	}
	/// @brief Send each of @p values, and the position after each.
	std::vector<int32_t> walk(std::initializer_list<uint8_t> values) const {
		std::vector<int32_t> out;
		for (const uint8_t value : values) {
			send(value);
			out.push_back(pos());
		}
		return out;
	}
};

/// @brief How far from a synth's session clip a take is: the expression dimension @p dimension's value on @p clip, 0
///        where the clip holds no expression.
int32_t expression(Clip& clip, int32_t dimension) {
	ExpressionParamSet* set = clip.paramManager.getExpressionParamSet();
	return set != nullptr ? set->params[dimension].getCurrentValue() : 0;
}

/// @brief Hear MIDI on DIN channel 0 as @p output's own input, so it is offered what no knob consumes.
void listensOnDin(Output& output) {
	static_cast<MelodicInstrument&>(output).midiInput.channelOrZone = kChannel;
}

/// @brief The arrangement recording @p clips' instruments' clips at 0, for as long as it lives.
struct ArrangementRecording {
	Transport transport{RecordingMode::ARRANGEMENT};
	explicit ArrangementRecording(const std::vector<spec::SongFixture>& fixtures) {
		for (const spec::SongFixture& f : fixtures) {
			f.clip->activeIfNoSolo = true;
			const int32_t at = f.instrument->clipInstances.insertSorted(0).value_or(-1);
			if (at >= 0) {
				f.instrument->clipInstances[at].clip = f.clip;
				f.instrument->clipInstances[at].length = f.clip->loopLength();
			}
		}
	}
};

} // namespace

// clang-format off
describe learned_cc_order("a learned CC through PlaybackHandler::midiCCReceived: who is offered it, in what order", $ {
	it("moves the song, then each playing output with an active clip, its own knob before its rows, rows in note-row order", _ {
		Takeover takeover{MIDITakeoverMode::JUMP};
		World w = makeWorld();
		learnAll(w, EVERY);
		RootView root;
		expect(w.synth.instrument->next).to_equal(static_cast<Output*>(w.kit.instrument), "the output after the synth");
		expect(w.kit.instrument->next).to_equal(static_cast<Output*>(w.audio), "the output after the kit");
		expect(w.audio->next).to_equal(static_cast<Output*>(w.idle.instrument), "the output after the audio track");
		expect(w.idle.instrument->next).to_be_null("the output after the idle instrument");
		sendCc(kChannel, kCc, 100);
		expect(written(w)).to_equal(std::string{"song synth kit rowB rowA audio"});
		// A value 100 is knob position 36, jumped to; the idle track's clip is not active, the sleeping synth's is gone.
		expect(positions(w)).to_equal(std::string{
		    "song:36 synth:36 kit:36 rowB:36 rowA:36 audio:36 idle:0 asleep:0"});
		// A learned CC marks no instrument edited.
		expect(w.synthOut().editedByUser).to_be_false();
		expect(w.kitOut().editedByUser).to_be_false();
	});

	it("moves nothing on a channel no knob is learned on, nor for a different CC", _ {
		Takeover takeover{MIDITakeoverMode::JUMP};
		World w = makeWorld();
		learnAll(w, EVERY);
		RootView root;
		sendCc(5, kCc, 100);
		sendCc(kChannel, 2, 100);
		expect(written(w)).to_equal(std::string{});
		expect(positions(w)).to_equal(std::string{
		    "song:0 synth:0 kit:0 rowB:0 rowA:0 audio:0 idle:0 asleep:0"});
	});

	it("moves only the song when the CC is on an MPE member channel", _ {
		Takeover takeover{MIDITakeoverMode::JUMP};
		World w = makeWorld();
		learnAll(w, EVERY, /*channel=*/2);
		MpeZone zone{4};
		RootView root;
		sendCc(2, kCc, 100);
		expect(written(w)).to_equal(std::string{"song"});
		expect(positions(w)).to_equal(std::string{
		    "song:36 synth:0 kit:0 rowB:0 rowA:0 audio:0 idle:0 asleep:0"});
	});

	it("never moves a knob learned on an MPE zone, on any channel", _ {
		Takeover takeover{MIDITakeoverMode::JUMP};
		World w = makeWorld();
		learnAll(w, EVERY, /*channel=*/16);
		MpeZone zone{4};
		RootView root;
		for (uint8_t channel = 0; channel < 16; channel++) {
			sendCc(channel, kCc, 100);
		}
		expect(written(w)).to_equal(std::string{});
		expect(positions(w)).to_equal(std::string{
		    "song:0 synth:0 kit:0 rowB:0 rowA:0 audio:0 idle:0 asleep:0"});
	});

	it("still offers the CC to the instrument after a learned hit", _ {
		Takeover takeover{MIDITakeoverMode::JUMP};
		World w = makeWorld();
		learnAll(w, SYNTH);
		listensOnDin(*w.synth.instrument);
		RootView root;
		expect(expression(w.synthClip(), 1)).to_equal(0);
		sendCc(kChannel, kCc, 100); // CC 1 is the mod wheel: the synth's Y axis
		expect(written(w)).to_equal(std::string{"synth synth.expression"});
		expect(expression(w.synthClip(), 1)).to_equal(100 << 24);
	});
});

describe learned_cc_takeover("a learned CC through takeover (ui::controls::knob_pos_after), one case at a time", $ {
	it("follows a relative cable by its offset, clamped to the knob's range, in every mode", _ {
		for (const MIDITakeoverMode mode : {MIDITakeoverMode::JUMP, MIDITakeoverMode::PICKUP, MIDITakeoverMode::SCALE}) {
			Takeover takeover{mode};
			Rig rig;
			RootView root;
			rig.absolute();
			Takeover::cableIsRelative(true);
			expect(joined(rig.walk({1, 127, 10, 100}))).to_equal(std::string{"1 0 10 -18"});
			rig.at(-60);
			rig.send(100); // -28, below the range
			expect(rig.pos()).to_equal(-64);
			rig.at(60);
			rig.send(10);
			expect(rig.pos()).to_equal(64);
		}
	});

	it("follows a fresh knob by its offset in every mode, and a value from 16 to 111 shows it is absolute", _ {
		for (const MIDITakeoverMode mode : {MIDITakeoverMode::JUMP, MIDITakeoverMode::PICKUP, MIDITakeoverMode::SCALE}) {
			Takeover takeover{mode};
			Rig rig;
			RootView root;
			rig.at(0);
			expect(rig.relativeInTable()).to_be_true();
			expect(joined(rig.walk({1, 127, 127}))).to_equal(std::string{"1 0 -1"});
			expect(rig.relativeInTable()).to_be_true();
			// 64 ends it, and the write that follows jumps (JUMP) or finds the param already where 64 is.
			rig.send(64);
			expect(rig.relativeInTable()).to_be_false();
		}
	});

	it("clears the relative flag from 16 to 111, and not outside", _ {
		Takeover takeover{MIDITakeoverMode::JUMP};
		for (const uint8_t value : {15, 112, 127, 0}) {
			Rig rig;
			RootView root;
			rig.at(0);
			rig.send(value);
			expect(rig.relativeInTable()).to_be_true();
		}
		for (const uint8_t value : {16, 111}) {
			Rig rig;
			RootView root;
			rig.at(0);
			rig.send(value);
			expect(rig.relativeInTable()).to_be_false();
		}
	});

	it("jumps to the controller's value in JUMP mode, and 127 reads as +64", _ {
		Takeover takeover{MIDITakeoverMode::JUMP};
		Rig rig;
		RootView root;
		rig.absolute();
		expect(joined(rig.walk({0, 127, 64, 100}))).to_equal(std::string{"-64 64 0 36"});
	});

	it("jumps while the clock records, in PICKUP mode", _ {
		Takeover takeover{MIDITakeoverMode::PICKUP};
		Rig rig;
		RootView root;
		rig.absolute();
		rig.at(20);
		rig.send(0); // not recording: the controller has not met the param
		expect(rig.pos()).to_equal(20);
		expect(rig.steps()).to_equal(size_t{0});
		Transport transport{RecordingMode::NORMAL};
		rig.send(0);
		expect(rig.pos()).to_equal(-64);
	});

	it("jumps while a step is held, in PICKUP mode, and writes at the held region", _ {
		Takeover takeover{MIDITakeoverMode::PICKUP};
		Rig rig;
		RootView root;
		rig.absolute();
		rig.at(20);
		HeldRegion region{*rig.f.clip, 96, 48};
		expect(automated(*rig.f.song, rig.address)).to_be_false();
		rig.send(0);
		expect(automated(*rig.f.song, rig.address)).to_be_true();
		expect(rig.steps()).to_equal(size_t{1});
		const std::optional<int32_t> inRegion = valueAtPos(*rig.f.song, rig.address, 100);
		const std::optional<int32_t> beforeRegion = valueAtPos(*rig.f.song, rig.address, 0);
		expect(inRegion.has_value()).to_be_true("the value in the region");
		if (inRegion.has_value()) {
			expect(*inRegion).to_equal(valueForKnobPos(*rig.f.song, rig.address, -64), "the value in the region");
		}
		expect(inRegion).not_().to_equal(beforeRegion, "the value in the region");
		// The region is 48 long: still written at its last tick, back to where it was past its end.
		expect(valueAtPos(*rig.f.song, rig.address, 96 + 47)).to_equal(inRegion, "the value at the region's last tick");
		expect(valueAtPos(*rig.f.song, rig.address, 96 + 48 + 10))
		    .not_()
		    .to_equal(inRegion, "the value past the region's end");
	});

	it("picks up in PICKUP mode only when the controller crosses the param", _ {
		Takeover takeover{MIDITakeoverMode::PICKUP};
		Rig rig;
		RootView root;
		rig.absolute();
		rig.at(20);
		std::vector<int32_t> seen;
		std::vector<int32_t> steps;
		for (const uint8_t value : {54, 74, 89, 94}) {
			rig.send(value);
			seen.push_back(rig.pos());
			steps.push_back(static_cast<int32_t>(rig.steps()));
		}
		expect(joined(seen)).to_equal(std::string{"20 20 25 30"});
		expect(joined(steps)).to_equal(std::string{"0 0 1 1"});
	});

	it("scales the runway in SCALE mode, rounding the float", _ {
		Takeover takeover{MIDITakeoverMode::SCALE};
		Rig rig;
		RootView root;
		rig.absolute();
		rig.at(20);
		// Left from 0 to -30: 20 + 84 * (-30 / 34) = -54.1; then right to -10: -54 + 118 * (20 / 74) = -22.1.
		expect(joined(rig.walk({34, 54}))).to_equal(std::string{"-54 -22"});
	});

	it("writes nothing when the controller leaves the param where it is, in every mode", _ {
		for (const MIDITakeoverMode mode : {MIDITakeoverMode::JUMP, MIDITakeoverMode::PICKUP, MIDITakeoverMode::SCALE}) {
			Takeover takeover{mode};
			Rig rig;
			RootView root;
			rig.absolute();
			rig.at(20);
			rig.send(84); // knob position 20
			expect(rig.pos()).to_equal(20);
			expect(rig.steps()).to_equal(size_t{0});
		}
	});
});

describe learned_pitch_bend("a learned pitch bend through PlaybackHandler::pitchBendReceived", $ {
	it("maps the 14-bit value to (v << 18) - 2^31, with no takeover", _ {
		for (const MIDITakeoverMode mode : {MIDITakeoverMode::JUMP, MIDITakeoverMode::PICKUP, MIDITakeoverMode::SCALE}) {
			Takeover takeover{mode};
			Rig rig{kBend};
			RootView root;
			expect(rig.relativeInTable()).to_be_false();
			std::vector<int32_t> seen;
			for (const uint32_t value14 : {0u, 8192u, 16383u}) {
				actionLogger.deleteAllLogs();
				sendBend(kChannel, value14);
				seen.push_back(valueOf(*rig.f.song, rig.address).value_or(0));
			}
			expect(joined(seen)).to_equal(std::string{"-2147483648 0 2147221504"});
		}
	});

	it("writes the first of an owner's two bend knobs, and not the second", _ {
		Rig rig{kBend};
		const OwnerParamTarget second{rig.target.owner, patched(params::LOCAL_PAN)};
		deluge::edits::learnMappingFor(source(kChannel, kBend), second);
		const ParamAddress pan = addressOf(rig.address.owner, second.param, rig.f.clip);
		RootView root;
		expect(static_cast<int>(rig.f.song->project.mappings.params_of(rig.target.owner).size())).to_equal(2);
		const std::optional<int32_t> pan_before = valueOf(*rig.f.song, pan);
		sendBend(kChannel, 16383);
		expect(valueOf(*rig.f.song, rig.address).value_or(0)).to_equal(2147221504);
		expect(valueOf(*rig.f.song, pan)).to_equal(pan_before, "the pan");
	});

	it("stops the bend reaching the instrument of an output it is learned on, and that output's only", _ {
		Rig learned{kBend};
		spec::SongFixture other = addSynth(learned.f.song);
		activate(*learned.f.song, *other.instrument, *other.clip);
		listensOnDin(*learned.f.instrument);
		listensOnDin(*other.instrument);
		RootView root;
		sendBend(kChannel, 16383);
		expect(valueOf(*learned.f.song, learned.address).value_or(0)).to_equal(2147221504);
		expect(expression(*learned.f.clip, 0)).to_equal(0);
		expect(expression(*other.clip, 0)).to_equal(8191 << 18);
	});

	it("never offers the song a bend", _ {
		Rig rig;
		const OwnerParamTarget songVolume{{Kind::SONG, 0}, unpatched(params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_VOLUME)};
		deluge::edits::learnMappingFor(source(kChannel, kBend), songVolume);
		const ParamAddress volume = addressOf(OwnerId::song(), songVolume.param, nullptr);
		const std::optional<int32_t> before = valueOf(*rig.f.song, volume);
		RootView root;
		actionLogger.deleteAllLogs();
		sendBend(kChannel, 16383);
		expect(valueOf(*rig.f.song, volume)).to_equal(before, "the volume");
		expect(rig.steps()).to_equal(size_t{0});
	});

	it("writes a kit row's learned bend to the row's own param, as its CC does, the kit's untouched", _ {
		// A row's bend and CC both reach the row's own params: an unpatched target and a patched one alike.
		spec::SongFixture kit = addKit(spec::detail::make_bare_song());
		SoundDrum* row = addSoundDrum(kit);
		addRow(kit, row);
		activate(*kit.song, *kit.instrument, *kit.clip);
		const auto bass = [](params::Kind kind) { return unpatched(kind, params::UNPATCHED_BASS); };
		const OwnerParamTarget learned{{Kind::DRUM, *row->id()}, bass(params::Kind::UNPATCHED_SOUND)};
		const OwnerParamTarget patchedTarget{{Kind::DRUM, *row->id()}, patched(params::LOCAL_LPF_FREQ)};
		deluge::edits::learnMappingFor(source(kChannel, kBend), learned);
		deluge::edits::learnMappingFor(source(1, kBend), patchedTarget);
		deluge::edits::learnMappingFor(source(kChannel, kCc), learned);
		const ParamAddress rowOwn = addressOf(OwnerId::drum(*row->id()), learned.param, kit.clip);
		const ParamAddress rowLpf = addressOf(OwnerId::drum(*row->id()), patchedTarget.param, kit.clip);
		const ParamAddress kitGlobal =
		    addressOf(OwnerId::instrument(*kit.instrument->id()), bass(params::Kind::UNPATCHED_GLOBAL), kit.clip);
		Takeover takeover{MIDITakeoverMode::JUMP};
		RootView root;
		putKnobAt(*kit.song, rowOwn, 0);
		putKnobAt(*kit.song, kitGlobal, 0);
		const std::optional<int32_t> kit_before = valueOf(*kit.song, kitGlobal);
		sendBend(kChannel, 16383);
		expect(valueOf(*kit.song, rowOwn).value_or(0)).to_equal(2147221504);
		expect(valueOf(*kit.song, kitGlobal)).to_equal(kit_before, "the kit's own param");
		sendBend(1, 0);
		expect(valueOf(*kit.song, rowLpf).value_or(0)).to_equal(INT32_MIN);
		// The same row's CC knob writes the row's own param.
		putKnobAt(*kit.song, rowOwn, 0);
		sendCc(kChannel, kCc, 100);
		expect(knobPosOf(*kit.song, rowOwn)).to_equal(36);
		expect(valueOf(*kit.song, kitGlobal)).to_equal(kit_before, "the kit's own param");
	});
});

describe learned_writes("how a learned write lands", $ {
	it("clones the clip into a take once while the arrangement records, and writes the take after", _ {
		Takeover takeover{MIDITakeoverMode::JUMP};
		World w = makeWorld();
		learnAll(w, SYNTH | KIT | ROWS);
		listensOnDin(*w.synth.instrument);
		const std::vector<Lane> lanes = lanesOf(w);
		RootView root;
		const size_t arranged = w.song->live.clips.arrangement_only_clips.size();
		ArrangementRecording recording{{w.synth, w.kit}};
		sendCc(kChannel, kCc, 100);
		Clip* const synthTake = w.synth.instrument->getActiveClip();
		Clip* const kitTake = w.kit.instrument->getActiveClip();
		expect(w.song->live.clips.arrangement_only_clips.size()).to_equal(arranged + 2);
		expect(synthTake).not_().to_be_null("the synth's take");
		expect(synthTake).not_().to_equal(w.synth.clip, "the synth's take");
		if (synthTake != nullptr) {
			expect(synthTake->beingRecordedFromClip).to_equal(w.synth.clip, "the clip the synth's take records from");
		}
		expect(kitTake).not_().to_be_null("the kit's take");
		expect(kitTake).not_().to_equal(w.kit.clip, "the kit's take");
		if (kitTake != nullptr) {
			expect(kitTake->beingRecordedFromClip).to_equal(w.kit.clip, "the clip the kit's take records from");
		}
		if (synthTake == nullptr || kitTake == nullptr) {
			return;
		}
		const auto onTake = [&](const Lane& lane, const Clip& take) {
			ParamAddress address = lane.address;
			address.clip_id = take.id();
			return knobPosOf(*w.song, address);
		};
		std::string moved;
		for (const Lane& lane : lanes) {
			if (lane.name == "synth") {
				moved += "synth:" + std::to_string(onTake(lane, *synthTake)) + "/" + std::to_string(knobPosOf(*w.song, lane.address)) + " ";
			}
			else if (lane.name == "kit" || lane.name == "rowA" || lane.name == "rowB") {
				moved += lane.name + ":" + std::to_string(onTake(lane, *kitTake)) + "/" + std::to_string(knobPosOf(*w.song, lane.address)) + " ";
			}
		}
		// The take holds the learned positions; the session clips stay where they were.
		expect(moved).to_equal(std::string{"synth:36/0 kit:36/0 rowB:36/0 rowA:36/0 "});
		// The offer that follows sees the take: the synth's mod wheel lands on it.
		expect(expression(*synthTake, 1)).to_equal(100 << 24);
		expect(expression(w.synthClip(), 1)).to_equal(0);
		// A second move writes the same takes.
		sendCc(kChannel, kCc, 110);
		expect(w.song->live.clips.arrangement_only_clips.size()).to_equal(arranged + 2);
		expect(w.synth.instrument->getActiveClip()).to_equal(synthTake, "the synth's active clip");
		expect(w.kit.instrument->getActiveClip()).to_equal(kitTake, "the kit's active clip");
		expect(onTake(lanes[1], *synthTake)).to_equal(46);
		// The arrangement records no undo steps, and no instrument is marked edited.
		expect(written(w)).to_equal(std::string{});
		expect(w.synthOut().editedByUser).to_be_false();
		expect(w.kitOut().editedByUser).to_be_false();
	});

	it("offers each output its learned knobs and then its own input before the next output, so takes are made in that order", _ {
		// The synth plays before the kit. Its own input makes its take on the CC or bend; the kit's learned knob makes
		// the kit's. Each take goes to the front of the arrangement's clips, which a song saves in that order.
		for (const bool bend : {false, true}) {
			Takeover takeover{MIDITakeoverMode::JUMP};
			World w = makeWorld();
			const Lane kitLane = lanesOf(w)[2];
			deluge::edits::learnMappingFor(source(kChannel, bend ? kBend : kCc), kitLane.target);
			listensOnDin(*w.synth.instrument);
			RootView root;
			const size_t arranged = w.song->live.clips.arrangement_only_clips.size();
			ArrangementRecording recording{{w.synth, w.kit}};
			if (bend) {
				sendBend(kChannel, 16383);
			}
			else {
				sendCc(kChannel, kCc, 100);
			}
			const auto& clips = w.song->live.clips.arrangement_only_clips;
			expect(clips.size()).to_equal(arranged + 2);
			if (clips.size() < 2) {
				continue;
			}
			expect(clips[0]).to_equal(w.kit.instrument->getActiveClip(), "the first arranged clip");
			expect(clips[1]).to_equal(w.synth.instrument->getActiveClip(), "the second arranged clip");
		}
	});

	it("clones the clip into a take on a hit that leaves the knob where it is", _ {
		Takeover takeover{MIDITakeoverMode::JUMP};
		World w = makeWorld();
		learnAll(w, SYNTH);
		const ParamAddress synthLane = lanesOf(w)[1].address;
		putKnobAt(*w.song, synthLane, 36);
		RootView root;
		const size_t arranged = w.song->live.clips.arrangement_only_clips.size();
		ArrangementRecording recording{{w.synth}};
		sendCc(kChannel, kCc, 100); // knob position 36, where the synth's param already is
		Clip* const take = w.synth.instrument->getActiveClip();
		expect(written(w)).to_equal(std::string{});
		expect(w.song->live.clips.arrangement_only_clips.size()).to_equal(arranged + 1);
		expect(take).not_().to_be_null("the take");
		expect(take).not_().to_equal(w.synth.clip, "the take");
		if (take != nullptr) {
			expect(take->beingRecordedFromClip).to_equal(w.synth.clip, "the clip the take records from");
		}
	});

	it("reads the held step region on the session clip before it clones, and writes it into the take", _ {
		Takeover takeover{MIDITakeoverMode::JUMP};
		Rig rig;
		RootView root;
		rig.absolute();
		HeldRegion region{*rig.f.clip, 96, 48};
		ArrangementRecording recording{{rig.f}};
		rig.send(0);
		Clip* const take = rig.f.instrument->getActiveClip();
		expect(take).not_().to_be_null("the take");
		expect(take).not_().to_equal(rig.f.clip, "the take");
		if (take == nullptr || take == rig.f.clip) {
			return;
		}
		ParamAddress onTake = rig.address;
		onTake.clip_id = take->id();
		expect(automated(*rig.f.song, onTake)).to_be_true();
		expect(automated(*rig.f.song, rig.address)).to_be_false();
		const std::optional<int32_t> inRegion = valueAtPos(*rig.f.song, onTake, 100);
		expect(valueAtPos(*rig.f.song, onTake, 96 + 47)).to_equal(inRegion, "the take's value at the region's last tick");
		expect(valueAtPos(*rig.f.song, onTake, 96 + 48 + 10))
		    .not_()
		    .to_equal(inRegion, "the take's value past the region's end");
	});

	it("makes a cable depth the learned knob turns when the sound has none yet", _ {
		Takeover takeover{MIDITakeoverMode::JUMP};
		spec::SongFixture f = spec::make_synth_song();
		activate(*f.song, *f.instrument, *f.clip);
		const deluge::project::ParamTarget depth = depthOf(params::LOCAL_LPF_FREQ, PatchSource::LFO_GLOBAL_1);
		const OwnerParamTarget cable{{Kind::OUTPUT, *f.instrument->id()}, depth};
		deluge::edits::learnMappingFor(source(kChannel, kCc), cable);
		const ParamAddress address = addressOf(OwnerId::instrument(*f.instrument->id()), depth, f.clip);
		RootView root;
		expect(valueOf(*f.song, address).has_value()).to_be_false();
		actionLogger.deleteAllLogs();
		sendCc(kChannel, kCc, 100);
		expect(valueOf(*f.song, address).has_value()).to_be_true();
		expect(valueOf(*f.song, address).value_or(0)).not_().to_equal(0, "the cable depth");
		expect(static_cast<Instrument*>(f.instrument)->editedByUser).to_be_false();
	});

	it("makes the cable depth a learned knob turns even when the message leaves it at 0, writing nothing", _ {
		Takeover takeover{MIDITakeoverMode::JUMP};
		spec::SongFixture f = spec::make_synth_song();
		activate(*f.song, *f.instrument, *f.clip);
		const deluge::project::ParamTarget depth = depthOf(params::LOCAL_LPF_FREQ, PatchSource::LFO_GLOBAL_1);
		const OwnerParamTarget cable{{Kind::OUTPUT, *f.instrument->id()}, depth};
		deluge::edits::learnMappingFor(source(kChannel, kCc), cable);
		const ParamAddress address = addressOf(OwnerId::instrument(*f.instrument->id()), depth, f.clip);
		RootView root;
		expect(valueOf(*f.song, address).has_value()).to_be_false();
		actionLogger.deleteAllLogs();
		sendCc(kChannel, kCc, 0); // a fresh knob reads it as an offset of 0
		const std::optional<int32_t> made = valueOf(*f.song, address);
		expect(made.has_value()).to_be_true("the cable depth");
		if (made.has_value()) {
			expect(*made).to_equal(0, "the cable depth");
		}
		expect(writes().empty()).to_be_true();
		expect(actionLogger.firstAction[BEFORE]).not_().to_be_null("the cable's own undo step");
	});

	it("marks no owner edited: a synth, a kit's own knob, or a kit's rows", _ {
		for (const unsigned owners : {unsigned{SYNTH}, unsigned{KIT}, unsigned{ROWS}}) {
			Takeover takeover{MIDITakeoverMode::JUMP};
			World w = makeWorld();
			learnAll(w, owners);
			RootView root;
			sendCc(kChannel, kCc, 100);
			expect(writes().empty()).to_be_false(); // the owner was written
			expect(w.synthOut().editedByUser).to_be_false();
			expect(w.kitOut().editedByUser).to_be_false();
		}
	});

	it("makes one undo record per lane written, and one undo reverts the message's writes", _ {
		Takeover takeover{MIDITakeoverMode::JUMP};
		World w = makeWorld();
		learnAll(w, SONG | SYNTH | KIT | ROWS | AUDIO);
		RootView root;
		sendCc(kChannel, kCc, 100);
		expect(static_cast<int>(writes().size())).to_equal(6);
		// A second message to the same lanes adds to the same step, so every lane keeps one record.
		sendCc(kChannel, kCc, 110);
		expect(static_cast<int>(writes().size())).to_equal(6);
		int steps = 0;
		for (const Action* a = actionLogger.firstAction[BEFORE]; a != nullptr; a = a->nextAction) {
			steps++;
		}
		expect(steps).to_equal(1);
		expect(actionLogger.revert(BEFORE, false, false)).to_be_true();
		expect(positions(w)).to_equal(std::string{
		    "song:0 synth:0 kit:0 rowB:0 rowA:0 audio:0 idle:0 asleep:0"});
	});
});
// clang-format on

DELUGE_SPEC(learned_dispatch_characterisation_spec, learned_cc_order, learned_cc_takeover, learned_pitch_bend,
            learned_writes)
