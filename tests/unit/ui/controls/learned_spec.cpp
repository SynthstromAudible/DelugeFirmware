// clang-format off
#include "ui/controls/learned.h"
#include "ui/controls/knob_math.h"
#include <cppspec.hpp>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace deluge;
using namespace deluge::ui::controls;

namespace {
using project::ControlsAddress;
using project::MidiCommand;
using project::OwnerId;
using project::OwnerParamTarget;
using project::ParamAddress;
using Kind = ControlsAddress::Kind;

/// The world every example plays: the song; a synth (output 1, clip 11, take 21); a kit (output 2, clip 12, take 22)
/// whose sound rows are drums 5 and 6; an audio track (output 3, clip 13, take 23); a MIDI track (output 4, clip 14).
constexpr ControlsAddress kSong{Kind::SONG, 0};
constexpr ControlsAddress kSynth{Kind::OUTPUT, 1};
constexpr ControlsAddress kKit{Kind::OUTPUT, 2};
constexpr ControlsAddress kAudio{Kind::OUTPUT, 3};
constexpr ControlsAddress kMidi{Kind::OUTPUT, 4};
constexpr ControlsAddress kRow5{Kind::DRUM, 5};
constexpr ControlsAddress kRow6{Kind::DRUM, 6};
/// A param the global effectable's rows map as UNIPOLAR; the sound's have no row for it.
constexpr uint32_t kUnipolar = 9;
constexpr uint32_t kParam = 7;
constexpr uint8_t kCc = 1;
constexpr uint8_t kBend = 128;

const plugin::ParamInfo kUnipolarRow{.id = plugin::ParamId{kUnipolar}, .mapping = plugin::MappingKind::UNIPOLAR};

/// A short name for @p address: owner kind and id, "@clip", "#param".
std::string name(const ParamAddress& address) {
	std::string out;
	switch (address.owner.kind) {
	case OwnerId::Kind::SONG: out = "S"; break;
	case OwnerId::Kind::CLIP: out = "C" + std::to_string(address.owner.id); break;
	case OwnerId::Kind::INSTRUMENT: out = "I" + std::to_string(address.owner.id); break;
	case OwnerId::Kind::DRUM: out = "D" + std::to_string(address.owner.id); break;
	case OwnerId::Kind::NOTE_ROW: out = "N" + std::to_string(address.owner.id); break;
	}
	if (address.clip_id) {
		out += "@" + std::to_string(*address.clip_id);
	}
	return out + "#" + std::to_string(address.id.value);
}

std::string joined(const std::vector<std::string>& lines) {
	std::string out;
	for (const std::string& line : lines) {
		out += (out.empty() ? "" : " | ") + line;
	}
	return out;
}

/// What the fakes share: the params' values, the step region, the events in the order they happened.
struct World {
	std::map<std::string, int32_t> values;  ///< by name(); a param not here reads 0
	std::set<std::string> missing;          ///< params the song does not hold yet
	std::set<std::string> unmakeable;       ///< missing params that cannot be made
	std::set<std::string> refused;          ///< params whose writes are refused
	std::optional<uint32_t> region_clip;    ///< the clip a step region is held on
	std::pair<int32_t, int32_t> region{96, 48};
	std::map<uint32_t, uint32_t> takes;     ///< each output's take, while the arrangement records
	TakeoverInputs inputs{};
	std::vector<std::string> events;
	std::vector<std::string> writes;        ///< each write: "name=value" and the region when there is one
	std::vector<std::string> follows;       ///< each learned_written: "name pos"
};

class FakeFacts final : public Facts {
public:
	explicit FakeFacts(World& world) : world_{world} {
		outputs_ = {
		    {kSynth, OwnerId::instrument(1), PlayingOutput::Kind::SYNTH, 11},
		    {kKit, OwnerId::instrument(2), PlayingOutput::Kind::KIT, 12},
		    {kAudio, OwnerId::clip(13), PlayingOutput::Kind::AUDIO, 13},
		    {kMidi, OwnerId::instrument(4), PlayingOutput::Kind::OTHER, 14},
		};
	}
	[[nodiscard]] std::optional<int32_t> param_value(const ParamAddress& address) const override {
		world_.events.push_back("value " + name(address));
		return value(address);
	}
	[[nodiscard]] std::optional<int32_t> param_value_at(const ParamAddress& address, uint32_t pos) const override {
		world_.events.push_back("value " + name(address) + " at " + std::to_string(pos));
		return value(address);
	}
	[[nodiscard]] bool param_automated(const ParamAddress&) const override { return false; }
	[[nodiscard]] std::optional<int32_t> clip_param_value(const ParamAddress&, std::optional<uint32_t>) const override {
		return std::nullopt;
	}
	[[nodiscard]] bool stutter_active() const override { return false; }
	[[nodiscard]] bool allows_note_tails(project::SlotAddress, std::optional<project::ClipAddress>) const override {
		return false;
	}
	[[nodiscard]] bool alt_delay() const override { return false; }
	[[nodiscard]] std::span<const PlayingOutput> playing_outputs() const override {
		outputs_read++;
		return outputs_;
	}
	[[nodiscard]] std::span<const KitRow> sound_rows(uint32_t clip_id) const override {
		world_.events.push_back("rows " + std::to_string(clip_id));
		rows_ = {{5, OwnerId::drum(5)}, {6, OwnerId::drum(6)}};
		return rows_;
	}
	[[nodiscard]] TakeoverInputs takeover_inputs(bool cable_relative) const override {
		TakeoverInputs inputs = world_.inputs;
		inputs.cable_relative = cable_relative;
		return inputs;
	}
	[[nodiscard]] std::optional<std::pair<int32_t, int32_t>>
	step_region(std::optional<uint32_t> clip_id) const override {
		world_.events.push_back("region " + (clip_id ? std::to_string(*clip_id) : std::string{"none"}));
		return clip_id && clip_id == world_.region_clip ? std::optional{world_.region} : std::nullopt;
	}
	[[nodiscard]] const plugin::ParamInfo* param_info(plugin::PluginId plugin,
	                                                  const project::ParamTarget& target) const override {
		return plugin == plugin::kGlobalEffectable && target.id.value == kUnipolar ? &kUnipolarRow : nullptr;
	}

	std::vector<PlayingOutput> outputs_;
	mutable int outputs_read{0}; ///< how many times playing_outputs() was read

private:
	[[nodiscard]] std::optional<int32_t> value(const ParamAddress& address) const {
		if (world_.missing.contains(name(address))) {
			return std::nullopt;
		}
		const auto found = world_.values.find(name(address));
		return found != world_.values.end() ? found->second : 0;
	}
	World& world_;
	mutable std::vector<KitRow> rows_;
};

class FakeSink final : public Sink {
public:
	explicit FakeSink(World& world) : world_{world} {}
	bool write_param(const ParamAddress& address, int32_t value, int32_t pos, int32_t length,
	                 Resolution resolution) override {
		world_.events.push_back("write " + name(address));
		if (world_.refused.contains(name(address))) {
			return false;
		}
		std::string line = name(address) + "=" + std::to_string(value);
		if (length != 0) {
			line += " [" + std::to_string(pos) + "+" + std::to_string(length) + "]";
		}
		if (resolution != Resolution::LEARNED) {
			line += " not learned";
		}
		world_.writes.push_back(line);
		world_.values[name(address)] = value;
		return true;
	}
	void learned_written(const ParamAddress& address, int32_t knob_pos) override {
		world_.follows.push_back(name(address) + " " + std::to_string(knob_pos));
	}
	std::optional<uint32_t> take_for(uint32_t output_id) override {
		world_.events.push_back("take " + std::to_string(output_id));
		const auto found = world_.takes.find(output_id);
		return found != world_.takes.end() ? std::optional{found->second} : std::nullopt;
	}
	bool make_param(const ParamAddress& address) override {
		world_.events.push_back("make " + name(address));
		if (world_.unmakeable.contains(name(address))) {
			return false;
		}
		world_.missing.erase(name(address));
		return true;
	}
	void knob_absolute(const OwnerParamTarget& target, const MidiCommand&) override {
		world_.events.push_back("absolute #" + std::to_string(target.param.id.value));
	}
	bool knob_mode(project::KnobModeOwner, uint8_t) override { return false; }
	bool chain_view(project::ChainAddress, std::string_view, std::string_view) override { return false; }
	bool write_controls(ControlsAddress, std::vector<project::ControllerMapping>) override { return false; }
	bool stutter(const KnobFocus&, bool) override { return false; }
	bool grab_region(const KnobFocus&, uint32_t) override { return false; }
	bool write_effect(const EffectOwner&, std::string_view, const serialization::Value&) override { return false; }
	bool midi_cc_select(const KnobFocus&, int32_t, bool) override { return false; }
	bool delete_knob_automation(const ParamAddress&) override { return false; }
	bool instrument_edited(const KnobFocus&) override { return false; }

private:
	World& world_;
};

/// The table's knobs, owner by owner, in table order, as the index hands them out; every one is hit.
class FakeHits final : public HitsOf {
public:
	bool any() override { return !knobs.empty(); }
	KnobHits of(ControlsAddress owner) override {
		asked.push_back(owner);
		answer_.clear();
		for (const LearnedHit& hit : knobs) {
			if (hit.target.owner == owner) {
				answer_.push_back(hit);
			}
		}
		return answer_;
	}
	void add(ControlsAddress owner, uint32_t param, uint8_t number = kCc, bool relative = false) {
		add(owner, project::engine_target(param), number, relative);
	}
	void add(ControlsAddress owner, project::ParamTarget param, uint8_t number = kCc, bool relative = false) {
		knobs.push_back({{owner, param}, {.channel_or_zone = 0, .note_or_cc = number}, relative});
	}
	std::vector<LearnedHit> knobs;
	std::vector<ControlsAddress> asked;

private:
	std::vector<LearnedHit> answer_;
};

/// One dispatch's fakes and store.
struct Rig {
	World world;
	FakeFacts facts{world};
	FakeSink sink{world};
	FakeHits hits;
	TakeoverStore takeovers;

	/// Offer a CC as the host does: the song, then each output in play order.
	void cc(uint8_t value, bool cable_relative = false) {
		const LearnedMessage message{.channel = 0, .number = kCc, .value = value, .cable_relative = cable_relative};
		dispatch_song_cc(hits, facts, sink, takeovers, message);
		for (const uint32_t output : {1u, 2u, 3u, 4u}) {
			dispatch_output_cc(output, hits, facts, sink, takeovers, message);
		}
	}
	/// Offer a bend as the host does, each output in play order.
	/// @return The outputs whose instruments do not bend.
	std::vector<uint32_t> bend(uint16_t value) {
		const LearnedMessage message{.channel = 0, .number = kBend, .value = value};
		std::vector<uint32_t> bent;
		for (const uint32_t output : {1u, 2u, 3u, 4u}) {
			if (dispatch_output_bend(output, hits, facts, sink, message)) {
				bent.push_back(output);
			}
		}
		return bent;
	}
	/// Put the param @p address names at knob position @p pos.
	void at(const std::string& address, int32_t pos) { world.values[address] = pos << 25; }
	[[nodiscard]] std::string writes() const { return joined(world.writes); }
	[[nodiscard]] std::string events() const { return joined(world.events); }
	/// Every take the arrangement would make.
	void recording() { world.takes = {{1, 21}, {2, 22}, {3, 23}}; }
	[[nodiscard]] std::string asked() const {
		std::string out;
		for (const ControlsAddress& owner : hits.asked) {
			out += std::string{out.empty() ? "" : " "} + (owner.kind == Kind::SONG ? "S" : owner.kind == Kind::DRUM ? "D" : "O")
			       + std::to_string(owner.id);
		}
		return out;
	}
};

constexpr int32_t kWordAt36 = 36 << 25; ///< a value of 100 is knob position 36
} // namespace

describe learned("learned knobs", ${
context("resolve_learned", _{
	it("puts the song's knob on the song, with no clip", _{
		const auto address = resolve_learned({kSong, project::engine_target(kParam)}, OwnerId::song(), 21);
		expect(address).to_have_value();
		if (address) {
			expect(name(*address)).to_equal(std::string{"S#7"});
		}
	});
	it("puts an instrument's and a row's knob on the instrument or drum, in the clip its writes go to", _{
		const auto synth = resolve_learned({kSynth, project::engine_target(kParam)}, OwnerId::instrument(1), 21);
		const auto row = resolve_learned({kRow5, project::engine_target(kParam)}, OwnerId::drum(5), 22);
		expect(synth).to_have_value("the synth's address");
		if (synth) {
			expect(name(*synth)).to_equal(std::string{"I1@21#7"});
		}
		expect(row).to_have_value("the row's address");
		if (row) {
			expect(name(*row)).to_equal(std::string{"D5@22#7"});
		}
	});
	it("puts an audio track's knob on the clip its writes go to, the take in place of the clip it records", _{
		const auto audio = resolve_learned({kAudio, project::engine_target(kParam)}, OwnerId::clip(13), 23);
		expect(audio).to_have_value();
		if (audio) {
			expect(name(*audio)).to_equal(std::string{"C23#7"});
		}
		expect(resolve_learned({kAudio, project::engine_target(kParam)}, OwnerId::clip(13), std::nullopt).has_value())
		    .to_be_false();
	});
	it("turns no param outside the owner's engine and arp params", _{
		const OwnerParamTarget cc{kSynth, {project::ParamSpace::MIDI_CC, plugin::ParamId{kParam}}};
		const OwnerParamTarget expression{kSynth, {project::ParamSpace::EXPRESSION, plugin::ParamId{0}}};
		expect(resolve_learned(cc, OwnerId::instrument(1), 11).has_value()).to_be_false();
		expect(resolve_learned(expression, OwnerId::instrument(1), 11).has_value()).to_be_false();
	});
	it("puts a knob on an arp param on the owner's arp, in the clip its writes go to", _{
		const OwnerParamTarget gate{kSynth, {project::ParamSpace::ARP, plugin::ParamId{0}}};
		const auto address = resolve_learned(gate, OwnerId::instrument(1), 21);
		expect(address).to_have_value("the arp param's address");
		if (address) {
			expect(name(*address)).to_equal(std::string{"I1@21#0"});
			expect(static_cast<int>(address->space)).to_equal(static_cast<int>(project::ParamSpace::ARP));
		}
	});
});

context("move_by_cc", _{
	it("writes the knob's new position over the region as a learned write, and answers the position", _{
		World world;
		FakeFacts facts{world};
		FakeSink sink{world};
		TakeoverState state;
		const CcMove move{.address = ParamAddress::of(OwnerId::instrument(1), project::engine_target(kParam), 11),
		                  .info = nullptr,
		                  .value = 0,
		                  .region = StepRegion{96, 48},
		                  .cc_value = 100};
		expect(move_by_cc(move, state, facts, sink)).to_equal(std::optional<int32_t>{36});
		expect(joined(world.writes)).to_equal(std::string{"I1@11#7=1207959552 [96+48]"});
		expect(state.previous).to_equal(std::optional<int32_t>{36});
		expect(joined(world.follows)).to_equal(std::string{}); // each caller tells the UI by its own rule
	});

	it("writes nothing, and answers none, where the position does not move", _{
		World world;
		FakeFacts facts{world};
		FakeSink sink{world};
		TakeoverState state;
		const CcMove move{.address = ParamAddress::of(OwnerId::song(), project::engine_target(kParam)),
		                  .value = kWordAt36,
		                  .cc_value = 100};
		expect(move_by_cc(move, state, facts, sink).has_value()).to_be_false();
		expect(joined(world.writes)).to_equal(std::string{});
	});

	it("reads the knob's and the cable's relative flags, and the step edit, into takeover", _{
		World world;
		FakeFacts facts{world};
		FakeSink sink{world};
		world.inputs.mode = TakeoverMode::PICKUP;
		const ParamAddress address = ParamAddress::of(OwnerId::song(), project::engine_target(kParam));
		const auto moved = [&](CcMove move) {
			TakeoverState state{.previous = -30};
			return move_by_cc(move, state, facts, sink);
		};
		// PICKUP holds a controller that has not met the param still, unless the knob or cable is relative or a step
		// region is held.
		expect(moved({.address = address, .value = 20 << 25, .cc_value = 0}).has_value()).to_be_false();
		expect(moved({.address = address, .value = 20 << 25, .cc_value = 1, .knob_relative = true}))
		    .to_equal(std::optional<int32_t>{21});
		expect(moved({.address = address, .value = 20 << 25, .cc_value = 1, .cable_relative = true}))
		    .to_equal(std::optional<int32_t>{21});
		expect(moved({.address = address, .value = 20 << 25, .region = StepRegion{0, 1}, .cc_value = 0}))
		    .to_equal(std::optional<int32_t>{-64});
	});
});

context("dispatch_song_cc and dispatch_output_cc", _{
	it("moves the song, then each playing output in play order, a kit's own knob before its rows in note-row order", _{
		Rig rig;
		for (const ControlsAddress owner : {kAudio, kRow6, kRow5, kKit, kSynth, kSong, kMidi}) {
			rig.hits.add(owner, kParam);
		}
		rig.cc(100);
		expect(rig.writes()).to_equal(std::string{
		    "S#7=1207959552 | I1@11#7=1207959552 | I2@12#7=1207959552 | D5@12#7=1207959552 | D6@12#7=1207959552"
		    " | C13#7=1207959552"});
		// The MIDI track has no knobs to ask about.
		expect(rig.asked()).to_equal(std::string{"S0 O1 O2 D5 D6 O3"});
	});

	it("moves the song's knobs in the song's step, and one output's knobs in that output's", _{
		Rig rig;
		rig.hits.add(kSong, kParam);
		rig.hits.add(kSynth, kParam);
		rig.hits.add(kKit, kParam);
		const LearnedMessage message{.channel = 0, .number = kCc, .value = 100};
		dispatch_song_cc(rig.hits, rig.facts, rig.sink, rig.takeovers, message);
		expect(rig.writes()).to_equal(std::string{"S#7=1207959552"});
		dispatch_output_cc(1, rig.hits, rig.facts, rig.sink, rig.takeovers, message);
		expect(rig.writes()).to_equal(std::string{"S#7=1207959552 | I1@11#7=1207959552"});
		expect(rig.asked()).to_equal(std::string{"S0 O1"});
	});

	it("moves nothing of an output that does not play, nor of a MIDI or CV track", _{
		Rig rig;
		rig.hits.add({Kind::OUTPUT, 9}, kParam);
		rig.hits.add(kMidi, kParam);
		const LearnedMessage message{.channel = 0, .number = kCc, .value = 100};
		dispatch_output_cc(9, rig.hits, rig.facts, rig.sink, rig.takeovers, message);
		dispatch_output_cc(4, rig.hits, rig.facts, rig.sink, rig.takeovers, message);
		expect(rig.asked()).to_equal(std::string{});
		expect(rig.writes()).to_equal(std::string{});
	});

	it("writes each output's params in the clip its take moved it to while the arrangement records", _{
		Rig rig;
		rig.recording();
		for (const ControlsAddress owner : {kSynth, kKit, kRow5, kAudio}) {
			rig.hits.add(owner, kParam);
		}
		rig.cc(100);
		expect(rig.writes()).to_equal(std::string{
		    "I1@21#7=1207959552 | I2@22#7=1207959552 | D5@22#7=1207959552 | C23#7=1207959552"});
	});

	it("asks for an output's take only on a hit of its own knobs or its rows', never for an output nothing hits", _{
		Rig rig;
		rig.recording();
		rig.hits.add(kRow6, kParam);
		rig.cc(100);
		expect(rig.events()).to_equal(std::string{
		    "rows 12 | region 12 | take 2 | value D6@22#7 | write D6@22#7"});
	});

	it("reads the step region on the clip before the take, and the value in the take after it", _{
		Rig rig;
		rig.recording();
		rig.world.region_clip = 11;
		rig.hits.add(kSynth, kParam);
		rig.hits.add(kSynth, 8);
		rig.cc(100);
		// The second hit's region is read on the take the first one moved the synth to, which holds none.
		expect(rig.events()).to_equal(std::string{
		    "region 11 | take 1 | value I1@21#7 at 96 | write I1@21#7"
		    " | region 21 | take 1 | value I1@21#8 | write I1@21#8 | rows 12"});
		expect(rig.writes()).to_equal(std::string{"I1@21#7=1207959552 [96+48] | I1@21#8=1207959552"});
	});

	it("reads a kit's rows on the clip its own knob's take moved it to", _{
		Rig rig;
		rig.recording();
		rig.hits.add(kKit, kParam);
		rig.cc(100);
		expect(rig.events()).to_equal(std::string{
		    "region 12 | take 2 | value I2@22#7 | write I2@22#7 | rows 22"});
	});

	it("jumps at a held step region in PICKUP mode, writing over the region", _{
		Rig rig;
		rig.world.inputs.mode = TakeoverMode::PICKUP;
		rig.world.region_clip = 11;
		rig.hits.add(kSynth, kParam);
		rig.at("I1@11#7", 20);
		TakeoverState& state = rig.takeovers.at({kSynth, project::engine_target(kParam)}, {.channel_or_zone = 0, .note_or_cc = kCc});
		state.previous = -30; // the controller has not met the param: PICKUP alone would hold still
		rig.cc(0);
		expect(rig.writes()).to_equal(std::string{"I1@11#7=-2147483648 [96+48]"});
	});

	it("clears a relative knob's flag on a value from 16 to 111, and reads that value as a position", _{
		for (const uint8_t value : {16, 111}) {
			Rig rig;
			rig.hits.add(kSynth, kParam, kCc, /*relative=*/true);
			rig.cc(value);
			expect(rig.world.events.front()).to_equal(std::string{"absolute #7"});
			expect(rig.writes()).to_equal("I1@11#7=" + std::to_string(value_of_knob({}, nullptr, value - 64)));
		}
		for (const uint8_t value : {15, 112}) {
			Rig rig;
			rig.hits.add(kSynth, kParam, kCc, /*relative=*/true);
			rig.cc(value);
			expect(rig.world.events.front()).to_equal(std::string{"region 11"});
			// An offset from 0: 15 up, 112 down by 16.
			const int32_t offset = value < 64 ? value : value - 128;
			expect(rig.writes()).to_equal("I1@11#7=" + std::to_string(value_of_knob({}, nullptr, offset)));
		}
	});

	it("reads an offset from a cable that sends relative values", _{
		Rig rig;
		rig.hits.add(kSynth, kParam);
		rig.at("I1@11#7", 10);
		rig.cc(5, /*cable_relative=*/true);
		expect(rig.writes()).to_equal("I1@11#7=" + std::to_string(15 << 25));
	});

	it("writes nothing, and tells the UI nothing, when the position does not move, though the take is made", _{
		Rig rig;
		rig.recording();
		rig.hits.add(kSynth, kParam);
		rig.at("I1@21#7", 36);
		rig.cc(100);
		expect(rig.writes()).to_equal(std::string{});
		expect(joined(rig.world.follows)).to_equal(std::string{});
		expect(rig.events()).to_equal(std::string{"region 11 | take 1 | value I1@21#7 | rows 12"});
	});

	it("keeps each knob's takeover state in the store, by its target and source", _{
		Rig rig;
		rig.world.inputs.mode = TakeoverMode::PICKUP;
		rig.hits.add(kSynth, kParam);
		rig.cc(64); // the controller's first value, where the param is: it seeds the state, and nothing moves
		rig.at("I1@11#7", 20);
		std::vector<std::string> seen;
		for (const uint8_t value : {54, 74, 89, 94}) {
			rig.cc(value);
			seen.push_back(std::to_string(knob_pos_of({}, nullptr, rig.world.values["I1@11#7"])));
		}
		expect(joined(seen)).to_equal(std::string{"20 | 20 | 25 | 30"});
		const TakeoverState* state =
		    rig.takeovers.find({kSynth, project::engine_target(kParam)}, {.channel_or_zone = 0, .note_or_cc = kCc});
		expect(state).not_().to_be_null();
		if (state != nullptr) {
			expect(state->previous).to_equal(std::optional<int32_t>{30});
		}
	});

	it("makes a param the song does not hold yet before it reads it, as 0", _{
		Rig rig;
		rig.hits.add(kSynth, kParam);
		rig.world.missing.insert("I1@11#7");
		rig.cc(64); // position 0, where a made param's 0 is: nothing is written
		expect(rig.events()).to_equal(std::string{"region 11 | take 1 | value I1@11#7 | make I1@11#7 | value I1@11#7 | rows 12"});
		expect(rig.writes()).to_equal(std::string{});
	});

	it("moves nothing, and keeps no takeover state, for a param that cannot be made", _{
		Rig rig;
		rig.hits.add(kSynth, kParam);
		rig.world.missing.insert("I1@11#7");
		rig.world.unmakeable.insert("I1@11#7");
		rig.cc(100);
		expect(rig.writes()).to_equal(std::string{});
		expect(rig.takeovers.size()).to_equal(size_t{0});
	});

	it("writes as a learned knob, and tells the UI the address written and the position", _{
		Rig rig;
		rig.recording();
		rig.hits.add(kSong, kParam);
		rig.hits.add(kKit, kParam);
		rig.cc(100);
		expect(joined(rig.world.follows)).to_equal(std::string{"S#7 36 | I2@22#7 36"});
		expect(rig.writes().find("not learned")).to_equal(std::string::npos);
	});

	it("maps a sound's params by the instrument's rows, and a kit's, an audio track's and the song's by the global effectable's", _{
		Rig rig;
		for (const ControlsAddress owner : {kSong, kSynth, kKit, kRow5, kAudio}) {
			rig.hits.add(owner, kUnipolar);
		}
		rig.cc(64);
		// Position 0: a word of 0 is there on a sound's param; a UNIPOLAR param's 0 is position -64, so it moves.
		expect(rig.writes()).to_equal(std::string{"S#9=1073741824 | I2@12#9=1073741824 | C13#9=1073741824"});
	});

	it("reads nothing of the song for a CC no knob hears, nor of a playing output or a kit's rows", _{
		Rig rig;
		rig.recording();
		rig.cc(100);
		expect(rig.events()).to_equal(std::string{});
		expect(rig.facts.outputs_read).to_equal(0);
		expect(rig.asked()).to_equal(std::string{"S0"});
	});

	it("passes over a knob outside its owner's engine params: no flag cleared, no region read, no take made", _{
		Rig rig;
		rig.recording();
		rig.hits.add(kSynth, project::ParamTarget{project::ParamSpace::MIDI_CC, plugin::ParamId{kParam}}, kCc,
		             /*relative=*/true);
		rig.cc(100);
		expect(rig.events()).to_equal(std::string{"rows 12"});
		expect(rig.writes()).to_equal(std::string{});
	});

	it("reads no step region on the song and makes it no take", _{
		Rig rig;
		rig.hits.add(kSong, kParam);
		rig.cc(100);
		expect(rig.events()).to_equal(std::string{"region none | value S#7 | write S#7 | rows 12"});
	});
});

context("dispatch_output_bend", _{
	it("writes (v << 18) - 2^31, with no takeover, no relative flag and no UI follow", _{
		Rig rig;
		rig.world.inputs.mode = TakeoverMode::PICKUP;
		rig.hits.add(kSynth, kParam, kBend, /*relative=*/true);
		for (const uint16_t value : {0, 8192, 16383}) {
			(void)rig.bend(value);
		}
		expect(rig.writes()).to_equal(std::string{"I1@11#7=-2147483648 | I1@11#7=0 | I1@11#7=2147221504"});
		expect(rig.events().find("absolute")).to_equal(std::string::npos);
		expect(joined(rig.world.follows)).to_equal(std::string{});
		expect(rig.takeovers.size()).to_equal(size_t{0});
	});

	it("offers a bend to each playing output's knobs and rows, and to no output that does not play or MIDI track", _{
		Rig rig;
		rig.hits.add(kSynth, kParam, kBend);
		rig.hits.add(kMidi, kParam, kBend);
		const LearnedMessage message{.channel = 0, .number = kBend, .value = 16383};
		expect(dispatch_output_bend(9, rig.hits, rig.facts, rig.sink, message)).to_be_false();
		expect(dispatch_output_bend(4, rig.hits, rig.facts, rig.sink, message)).to_be_false();
		expect(rig.asked()).to_equal(std::string{});
		(void)rig.bend(16383);
		expect(rig.writes()).to_equal(std::string{"I1@11#7=2147221504"});
		expect(rig.asked()).to_equal(std::string{"O1 O2 D5 D6 O3"});
	});

	it("writes the first of an owner's bend knobs, and not the next", _{
		Rig rig;
		rig.hits.add(kSynth, kParam, kBend);
		rig.hits.add(kSynth, 8, kBend);
		(void)rig.bend(16383);
		expect(rig.writes()).to_equal(std::string{"I1@11#7=2147221504"});
	});

	it("writes the next of an owner's bend knobs where the first's param is not written", _{
		Rig rig;
		rig.world.refused.insert("I1@11#7");
		rig.hits.add(kSynth, kParam, kBend);
		rig.hits.add(kSynth, 8, kBend);
		(void)rig.bend(16383);
		expect(rig.writes()).to_equal(std::string{"I1@11#8=2147221504"});
	});

	it("gives a kit's own knobs and each of its rows' their own first bend", _{
		Rig rig;
		rig.hits.add(kKit, kParam, kBend);
		rig.hits.add(kKit, 8, kBend);
		rig.hits.add(kRow5, kParam, kBend);
		rig.hits.add(kRow6, kParam, kBend);
		(void)rig.bend(0);
		expect(rig.writes()).to_equal(std::string{"I2@12#7=-2147483648 | D5@12#7=-2147483648 | D6@12#7=-2147483648"});
	});

	it("writes a kit row's bend to the row's own param, in the clip its writes go to", _{
		Rig rig;
		rig.recording();
		rig.hits.add(kRow5, kParam, kBend);
		(void)rig.bend(16383);
		expect(rig.writes()).to_equal(std::string{"D5@22#7=2147221504"});
	});

	it("answers each output a knob hears the bend on, whether or not a param is written, and no other", _{
		Rig rig;
		rig.world.refused.insert("D5@12#7");
		rig.hits.add(kSynth, kParam, kBend);
		rig.hits.add(kRow5, kParam, kBend);
		rig.hits.add(kSong, kParam, kBend);
		expect(rig.bend(16383)).to_equal(std::vector<uint32_t>{1, 2});
	});

	it("reads nothing of the song for a bend no knob hears, and lets every instrument bend", _{
		Rig rig;
		rig.recording();
		expect(rig.bend(16383).empty()).to_be_true();
		expect(rig.events()).to_equal(std::string{});
		expect(rig.facts.outputs_read).to_equal(0);
	});

	it("passes over a bend knob outside its owner's engine params, asking for no take, though its output hears it", _{
		Rig rig;
		rig.recording();
		rig.hits.add(kSynth, project::ParamTarget{project::ParamSpace::EXPRESSION, plugin::ParamId{0}}, kBend);
		expect(rig.bend(16383)).to_equal(std::vector<uint32_t>{1});
		expect(rig.events()).to_equal(std::string{"rows 12"});
		expect(rig.writes()).to_equal(std::string{});
	});

	it("reads the step region before the take, writes over it, and asks for a take only on a hit", _{
		Rig rig;
		rig.recording();
		rig.world.region_clip = 11;
		rig.hits.add(kSynth, kParam, kBend);
		(void)rig.bend(8192);
		expect(rig.events()).to_equal(std::string{"region 11 | take 1 | write I1@21#7 | rows 12"});
		expect(rig.writes()).to_equal(std::string{"I1@21#7=0 [96+48]"});
	});
});
});
// clang-format on

CPPSPEC_SPEC(learned)
