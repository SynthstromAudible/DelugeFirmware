// clang-format off
#include "controls/facts.h"
#include "controls/sink.h"
#include <cppspec.hpp>

using namespace deluge;
using namespace deluge::controls;

namespace {
/// A sink that keeps what it was asked, to show the interface needs nothing from the app.
class RecordingKnobSink final : public Sink {
public:
	bool write_param(const project::ParamAddress& address, int32_t value, int32_t pos, int32_t length,
	                 Resolution resolution) override {
		written = address;
		written_resolution = resolution;
		written_value = value;
		written_pos = pos;
		written_length = length;
		return true;
	}
	void learned_written(const project::ParamAddress& address, int32_t knob_pos) override {
		followed = address;
		followed_pos = knob_pos;
	}
	std::optional<uint32_t> take_for(uint32_t output_id) override {
		take_asked = output_id;
		return output_id + 1000;
	}
	void knob_absolute(const project::OwnerParamTarget& target, const project::MidiCommand& source) override {
		absolute_target = target;
		absolute_source = source;
	}
	bool delete_knob_automation(const project::ParamAddress&) override { return false; }
	bool knob_mode(project::KnobModeOwner, uint8_t mode) override {
		mode_set = mode;
		return true;
	}
	bool instrument_edited(const KnobFocus&) override { return false; }
	bool grab_region(const KnobFocus&, uint32_t) override { return false; }
	bool write_effect(const EffectOwner& owner, std::string_view path, const serialization::Value&) override {
		effect_owner = owner;
		effect_path = std::string{path};
		return true;
	}
	bool chain_view(project::ChainAddress, std::string_view key, std::string_view word) override {
		view = std::string{key} + "=" + std::string{word};
		return true;
	}
	bool write_controls(project::ControlsAddress, std::vector<project::ControllerMapping> controls) override {
		control_count = controls.size();
		return true;
	}
	bool stutter(const KnobFocus&, bool begin) override {
		stuttering = begin;
		return true;
	}
	bool midi_cc_select(const KnobFocus&, int32_t knob, bool on) override {
		selecting = on ? knob : -1;
		return true;
	}

	project::ParamAddress written{};
	Resolution written_resolution{Resolution::KNOB};
	project::ParamAddress followed{};
	int32_t followed_pos{0};
	uint32_t take_asked{0};
	project::OwnerParamTarget absolute_target{};
	project::MidiCommand absolute_source{};
	int32_t written_value{0};
	int32_t written_pos{0};
	int32_t written_length{0};
	uint8_t mode_set{0};
	std::optional<EffectOwner> effect_owner;
	std::string effect_path;
	std::string view;
	size_t control_count{0};
	bool stuttering{false};
	int32_t selecting{-1};
};

class FixedFacts final : public Facts {
public:
	[[nodiscard]] std::optional<int32_t> param_value(const project::ParamAddress&) const override { return 7; }
	[[nodiscard]] std::optional<int32_t> param_value_at(const project::ParamAddress&, uint32_t pos) const override {
		return static_cast<int32_t>(pos);
	}
	[[nodiscard]] bool stutter_active() const override { return true; }
	[[nodiscard]] bool alt_delay() const override { return false; }
	[[nodiscard]] std::span<const PlayingOutput> playing_outputs() const override { return outputs; }
	[[nodiscard]] std::span<const KitRow> sound_rows(uint32_t clip_id) const override {
		return clip_id == 9 ? std::span<const KitRow>{rows} : std::span<const KitRow>{};
	}
	[[nodiscard]] TakeoverInputs takeover_inputs(bool cable_relative) const override {
		return {.mode = TakeoverMode::SCALE, .cable_relative = cable_relative, .recording = true};
	}
	[[nodiscard]] std::optional<std::pair<int32_t, int32_t>> step_region(std::optional<uint32_t> clip_id) const override {
		return clip_id ? std::optional{std::pair{int32_t{96}, int32_t{48}}} : std::nullopt;
	}
	std::vector<PlayingOutput> outputs;
	std::vector<KitRow> rows;
	[[nodiscard]] bool allows_note_tails(project::SlotAddress, std::optional<project::ClipAddress>) const override {
		return false;
	}
};
} // namespace

describe sink("the controls' sink and facts", ${
	it("takes a knob's writes through the interface", _{
		RecordingKnobSink recording;
		Sink& sink = recording;
		project::ParamAddress address{};
		expect(sink.write_param(address, 5, 96, 24, Resolution::LEARNED)).to_be_true();
		expect(recording.written_resolution == Resolution::LEARNED).to_be_true();
		expect(recording.written_value).to_equal(5);
		expect(recording.written_pos).to_equal(96);
		expect(recording.written_length).to_equal(24);
		expect(sink.knob_mode(project::SongAddress{}, 3)).to_be_true();
		expect(int{recording.mode_set}).to_equal(3);
		expect(sink.stutter(KnobFocus{}, true)).to_be_true();
		expect(recording.stuttering).to_be_true();
		expect(sink.midi_cc_select(KnobFocus{}, 2, true)).to_be_true();
		expect(recording.selecting).to_equal(2);
	});
	it("takes a learned knob's follow, take and absolute notice through the interface", _{
		RecordingKnobSink recording;
		Sink& sink = recording;
		sink.learned_written(project::ParamAddress{.owner = project::OwnerId::instrument(3)}, -12);
		expect(recording.followed.owner == project::OwnerId::instrument(3)).to_be_true();
		expect(recording.followed_pos).to_equal(-12);
		expect(sink.take_for(7) == std::optional<uint32_t>{1007}).to_be_true();
		const project::OwnerParamTarget target{{project::ControlsAddress::Kind::OUTPUT, 7}, {}};
		const project::MidiCommand source{.channel_or_zone = 2, .note_or_cc = 5};
		sink.knob_absolute(target, source);
		expect(recording.absolute_target == target).to_be_true();
		expect(recording.absolute_source == source).to_be_true();
	});
	it("takes a chain's effect word and view word", _{
		RecordingKnobSink recording;
		Sink& sink = recording;
		const EffectOwner owner = project::ChainAddress{.kind = project::ChainAddress::Kind::SONG};
		expect(sink.write_effect(owner, "delay/amount", serialization::Value::makeNumber(1))).to_be_true();
		expect(recording.effect_path).to_equal(std::string{"delay/amount"});
		expect(recording.effect_owner == std::optional<EffectOwner>{owner}).to_be_true();
		expect(sink.chain_view(std::get<project::ChainAddress>(owner), "currentFilterType", "hpf")).to_be_true();
		expect(recording.view).to_equal(std::string{"currentFilterType=hpf"});
		expect(sink.write_controls(project::ControlsAddress{}, {project::ControllerMapping{}})).to_be_true();
		expect(recording.control_count).to_equal(size_t{1});
	});
	it("answers a learned dispatch's reads through the interface", _{
		FixedFacts fixed;
		fixed.outputs.push_back({.owner = {project::ControlsAddress::Kind::OUTPUT, 4},
		                         .param_owner = project::OwnerId::instrument(4),
		                         .kind = PlayingOutput::Kind::KIT,
		                         .clip_id = 9});
		fixed.rows.push_back({.drum_id = 2, .param_owner = project::OwnerId::drum(2)});
		const Facts& facts = fixed;
		expect(facts.playing_outputs().size()).to_equal(size_t{1});
		expect(facts.playing_outputs()[0].kind == PlayingOutput::Kind::KIT).to_be_true();
		expect(facts.sound_rows(9).size()).to_equal(size_t{1});
		expect(facts.sound_rows(8).empty()).to_be_true();
		const TakeoverInputs inputs = facts.takeover_inputs(true);
		expect(inputs.cable_relative && inputs.recording && !inputs.step_editing && !inputs.knob_relative).to_be_true();
		expect(facts.step_region(9) == std::optional{std::pair{int32_t{96}, int32_t{48}}}).to_be_true();
		expect(facts.step_region(std::nullopt).has_value()).to_be_false();
	});
	it("answers a knob's reads through the interface", _{
		const FixedFacts fixed;
		const Facts& facts = fixed;
		const project::ParamAddress address{};
		expect(facts.param_value(address) == std::optional<int32_t>{7}).to_be_true();
		expect(facts.param_value_at(address, 96) == std::optional<int32_t>{96}).to_be_true();
		expect(facts.stutter_active()).to_be_true();
		expect(facts.allows_note_tails(project::SlotAddress{}, std::nullopt)).to_be_false();
	});
});
// clang-format on

CPPSPEC_SPEC(sink)
