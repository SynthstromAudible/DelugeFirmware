#include "engine-api/types/setting.h"
#include "engine-api/types/sync.h"
#include "engines/deluge/codec.h"
#include "engines/deluge/settings.h"
#include "serialization/tree.h"
#include "support/state_matrix.h"

#include <algorithm>
#include <array>
#include <cppspec.hpp>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

using namespace deluge::instrument;
using deluge::serialization::Value;

namespace deluge::instrument {
/// A state, printed as its dump, so a failing comparison shows it.
std::ostream& operator<<(std::ostream& out, const SoundState& state) {
	return out << deluge::serialization::first_difference(dump_state(state), Value::makeMap());
}

/// A range, printed as its fields, so a failing comparison of range lists shows it.
std::ostream& operator<<(std::ostream& out, const RangeState& range) {
	return out << "{top " << range.top_note << " " << range.file_path << " " << range.start_pos << "-" << range.end_pos
	           << "}";
}
} // namespace deluge::instrument

namespace {

constexpr deluge::engine::CodecContext kContext{.input_tick_magnitude = 1};

/// @p tree with the leaf at @p path (a/b/c) set to @p leaf, in place or appended, as a slot's leaf is changed.
void set_leaf(Value& tree, std::string_view path, Value leaf) {
	const size_t slash = path.find('/');
	const std::string_view key = path.substr(0, slash);
	if (slash == std::string_view::npos) {
		tree.replace_or_append(key, std::move(leaf));
		return;
	}
	if (tree.find_mut(key) == nullptr) {
		tree.replace_or_append(key, Value::makeMap());
	}
	set_leaf(*tree.find_mut(key), path.substr(slash + 1), std::move(leaf));
}

/// The paths a menu reshapes more than its own leaf for (the bridge's stateWithSetting writes them).
bool reshapes(std::string_view path) {
	return path == "mode" || path.ends_with("/type") && path.starts_with("osc") || path.ends_with("/loopMode");
}

/// Where with_setting and the decode of the whole slot with the leaf changed disagree, over every state of the matrix,
/// every path a write reaches, three of its values and two magnitudes.
std::vector<std::string> with_setting_disagreements() {
	std::vector<std::string> found;
	for (const int32_t magnitude : {1, 3}) {
		const deluge::engine::CodecContext codec{.input_tick_magnitude = magnitude};
		for (const auto& [name, state] : deluge::instrument::test::matrix()) {
			const Value whole = encode(state, codec);
			for (const deluge::engine::SettingInfo& row : kSettings) {
				if (row.kind == deluge::engine::SettingKind::STRUCTURED || reshapes(row.path)) {
					continue;
				}
				const auto [least, greatest] = deluge::engine::menu_range(row);
				for (const int32_t menu : {least, greatest, least + (greatest - least) / 2}) {
					const auto stored = deluge::engine::from_menu(row, menu);
					if (!stored) {
						continue;
					}
					std::array<char, 16> scratch{};
					const auto word = row.kind == deluge::engine::SettingKind::CHOICE
					                      ? deluge::engine::stored_text(row, stored->first, scratch)
					                      : std::nullopt;
					const Value given = word ? Value::makeScalar(std::string(*word)) : Value::makeNumber(stored->first);
					const auto value = canonical_setting(row.path, given);
					if (!value) {
						continue;
					}
					Value leaf = *value;
					if (row.path.ends_with("/syncLevel")) {
						leaf = Value::makeNumber(deluge::engine::sync_level_to_file(
						    deluge::serialization::int_of(&*value).value_or(0), magnitude));
					}
					Value element = whole;
					set_leaf(element, row.path, leaf);
					const auto want = decode_change(element, whole, state, codec);
					const auto got = with_setting(state, row.path, *value, codec);
					const std::string at =
					    name + " " + std::string(row.path) + "=" + value->str() + "@" + std::to_string(magnitude);
					if (want.has_value() != got.has_value()) {
						found.push_back(at + " (refusal)");
					}
					else if (want && (!(want->state == got->state) || want->changed != got->changed)) {
						found.push_back(at);
					}
				}
			}
		}
	}
	return found;
}

/// The paths whose value no file holds in some state of the matrix, where the encoding leaves the field out (a
/// modulator outside FM; sample playback off a sample source; a sample source's own tuning and phase): with_setting
/// loses such a write, and write_setting keeps it.
constexpr std::array<std::string_view, 18> kUnheldPaths{
    "modulator1/transpose",    "modulator1/cents",         "modulator1/retrigPhase",
    "modulator2/transpose",    "modulator2/cents",         "modulator2/retrigPhase",
    "modulator2/toModulator1", "osc1/transpose",           "osc1/cents",
    "osc1/retrigPhase",        "osc1/linearInterpolation", "osc1/reversed",
    "osc1/timeStretchEnable",  "osc1/timeStretchAmount",   "osc2/linearInterpolation",
    "osc2/reversed",           "osc2/timeStretchEnable",   "osc2/timeStretchAmount",
};

/// Where write_setting on a copy of the state and with_setting disagree, over the same states, paths, values and
/// magnitudes as with_setting_disagreements: the state, and the entries touched, which are the entries whose typed
/// fields changed (with_setting also names one whose file form alone changed, a leaf the encoding leaves out at its
/// default written as it). Where with_setting loses a write of an unheld path, the writer must give its state with
/// that one field written.
std::vector<std::string> write_setting_disagreements() {
	std::vector<std::string> found;
	for (const int32_t magnitude : {1, 3}) {
		const deluge::engine::CodecContext codec{.input_tick_magnitude = magnitude};
		for (const auto& [name, state] : deluge::instrument::test::matrix()) {
			for (const deluge::engine::SettingInfo& row : kSettings) {
				if (row.kind == deluge::engine::SettingKind::STRUCTURED || reshapes(row.path)) {
					continue;
				}
				const auto [least, greatest] = deluge::engine::menu_range(row);
				for (const int32_t menu : {least, greatest, least + (greatest - least) / 2}) {
					const auto stored = deluge::engine::from_menu(row, menu);
					if (!stored) {
						continue;
					}
					std::array<char, 16> scratch{};
					const auto word = row.kind == deluge::engine::SettingKind::CHOICE
					                      ? deluge::engine::stored_text(row, stored->first, scratch)
					                      : std::nullopt;
					const Value given = word ? Value::makeScalar(std::string(*word)) : Value::makeNumber(stored->first);
					const auto value = canonical_setting(row.path, given);
					if (!value) {
						continue;
					}
					const std::string at =
					    name + " " + std::string(row.path) + "=" + value->str() + "@" + std::to_string(magnitude);
					const auto want = with_setting(state, row.path, *value, codec);
					if (!want) {
						found.push_back(at + " (with_setting refused)");
						continue;
					}
					SoundState got = state;
					const std::vector<std::string_view> changed = write_setting(got, row.path, *value);
					if (got == want->state) {
						const bool named = std::ranges::all_of(
						    changed, [&](std::string_view key) { return std::ranges::contains(want->changed, key); });
						if (changed != changed_entries(state, got) || !named) {
							found.push_back(at + " (touched entries)");
						}
						continue;
					}
					SoundState again = want->state;
					(void)write_setting(again, row.path, *value);
					const std::string_view key = row.path.substr(0, row.path.find('/'));
					const bool one_field =
					    again == got && changed_entries(want->state, got) == std::vector<std::string_view>{key};
					if (!one_field || std::ranges::find(kUnheldPaths, row.path) == kUnheldPaths.end()) {
						found.push_back(at + (one_field ? " (unheld, not listed)" : " (state)"));
					}
				}
			}
		}
	}
	return found;
}

/// A subtractive sound whose first source plays two sample ranges and whose second is a saw with a stale range.
SoundState sampled() {
	SoundState state = new_synth_state();
	SourceState& sample = state.sources[0];
	sample.osc_type = OscType::SAMPLE;
	sample.repeat_mode = SampleRepeatMode::LOOP;
	sample.reversed = true;
	sample.ranges = {RangeState{.top_note = 60, .file_path = "SAMPLES/LOW.WAV", .start_pos = 10, .end_pos = 900},
	                 RangeState{.top_note = 32767, .file_path = "SAMPLES/HIGH.WAV", .start_pos = 0, .end_pos = 700}};
	state.osc_retrigger_phase[0] = 12345;
	SourceState& saw = state.sources[1];
	saw.osc_type = OscType::SAW;
	saw.ranges = {RangeState{.top_note = 32767, .file_path = "SAMPLES/OLD.WAV"}};
	return state;
}

} // namespace

// clang-format off
describe decoding_onto("a slot decoded onto a sound's current state", ${
	it("reads back a subtractive sound's state as it is", _ {
		const SoundState state = sampled();
		const auto decoded = decode_onto(encode(state, kContext), state, kContext);
		expect(decoded.has_value()).to_be_true();
		expect(*decoded).to_equal(state);
	});

	it("keeps an FM sound's oscillator types, samples and DX patch, which no file holds", _ {
		SoundState state = sampled();
		state.synth_mode = SynthMode::FM;
		state.sources[1].osc_type = OscType::DX7;
		state.sources[1].dx_patch = DxPatchState{.params = {}, .engine_mode = 1, .random_detune = 3};
		const auto decoded = decode_onto(encode(state, kContext), state, kContext);
		expect(decoded.has_value()).to_be_true();
		expect(*decoded).to_equal(state);
	});

	it("keeps a non-FM sound's modulators", _ {
		SoundState state = sampled();
		state.modulator_transpose = {5, -7};
		state.modulator1_to_modulator0 = true;
		const auto decoded = decode_onto(encode(state, kContext), state, kContext);
		expect(decoded->modulator_transpose[0]).to_equal(int16_t{5});
		expect(decoded->modulator_transpose[1]).to_equal(int16_t{-7});
		expect(decoded->modulator1_to_modulator0).to_be_true();
	});

	it("keeps a ring-mod sound's sample oscillator, which only reading a file clamps", _ {
		SoundState state = sampled();
		state.synth_mode = SynthMode::RINGMOD;
		const auto decoded = decode_onto(encode(state, kContext), state, kContext);
		expect(decoded->sources[0].osc_type).to_equal(OscType::SAMPLE);
		SoundState fresh = new_synth_state();
		expect(decode(encode(state, kContext), fresh, kContext).has_value()).to_be_true();
		expect(fresh.sources[0].osc_type).to_equal(OscType::WAVETABLE);
	});

	it("takes every value the element holds", _ {
		SoundState state = sampled();
		SoundState written = state;
		written.voice_priority = deluge::engine::VoicePriority::HIGH;
		const auto decoded = decode_onto(encode(written, kContext), state, kContext);
		expect(decoded->voice_priority).to_equal(deluge::engine::VoicePriority::HIGH);
	});

	it("keeps a sample source's own transpose and cents, which a file holds only as its ranges'", _ {
		SoundState state = sampled();
		state.sources[0].transpose = 7;
		state.sources[0].cents = -12;
		const auto decoded = decode_onto(encode(state, kContext), state, kContext);
		expect(*decoded).to_equal(state);
	});

	it("keeps what an unchanged entry says, even where the file could not say it (a sync level)", _ {
		SoundState state = sampled();
		state.lfo_sync_level[0] = static_cast<deluge::engine::SyncLevel>(1);
		constexpr deluge::engine::CodecContext kSlow{.input_tick_magnitude = 0};
		const auto decoded = decode_onto(encode(state, kSlow), state, kSlow);
		expect(*decoded).to_equal(state);
	});

	it("reads a changed entry as a file holds it", _ {
		SoundState state = sampled();
		SoundState written = state;
		written.lfo_sync_level[0] = static_cast<deluge::engine::SyncLevel>(1);
		constexpr deluge::engine::CodecContext kSlow{.input_tick_magnitude = 0};
		const auto decoded = decode_onto(encode(written, kSlow), state, kSlow);
		expect(static_cast<int32_t>(decoded->lfo_sync_level[0])).to_equal(2); // where magnitude 0's file keeps WHOLE
	});

	it("keeps the ranges a changed oscillator type carries over", _ {
		SoundState state = sampled();
		state.sources[1].ranges = {RangeState{.top_note = 32767, .file_path = "SAMPLES/OLD.WAV", .end_pos = 500}};
		SoundState written = state;
		written.sources[1].osc_type = OscType::SAMPLE;
		const auto decoded = decode_onto(encode(written, kContext), state, kContext);
		expect(decoded->sources[1].ranges).to_equal(state.sources[1].ranges);
	});

	it("reads the ranges a changed oscillator type reshapes", _ {
		SoundState state = sampled();
		SoundState written = state;
		written.sources[0].osc_type = OscType::WAVETABLE;
		written.sources[0].ranges = {RangeState{.top_note = 60}, RangeState{.top_note = 32767}};
		const auto decoded = decode_onto(encode(written, kContext), state, kContext);
		expect(decoded->sources[0].ranges).to_equal(written.sources[0].ranges);
	});

	it("carries no sample range's zone into a wavetable, even one with no file", _ {
		SoundState state = sampled();
		state.sources[0].ranges = {RangeState{.top_note = 32767, .end_pos = 9999999}};
		SoundState written = state;
		written.sources[0].osc_type = OscType::WAVETABLE;
		written.sources[0].ranges = {RangeState{.top_note = 32767}};
		const auto decoded = decode_onto(encode(written, kContext), state, kContext);
		expect(decoded->sources[0].ranges).to_equal(written.sources[0].ranges);
	});

	it("keeps an LFO's sync level the file could not say when only its other settings change", _ {
		SoundState state = sampled();
		state.lfo_sync_level[0] = static_cast<deluge::engine::SyncLevel>(1);
		SoundState written = state;
		written.lfo_wave_type[0] = LFOType::SAW;
		constexpr deluge::engine::CodecContext kSlow{.input_tick_magnitude = 0};
		const auto decoded = decode_onto(encode(written, kSlow), state, kSlow);
		expect(decoded->lfo_wave_type[0]).to_equal(LFOType::SAW);
		expect(static_cast<int32_t>(decoded->lfo_sync_level[0])).to_equal(1);
	});

	it("reads a changed effect entry and keeps the others", _ {
		SoundState state = sampled();
		state.effects.delay.type = deluge::effects::DelayType::ANALOG;
		SoundState written = state;
		written.effects.filter.lpf_mode = deluge::effects::FilterMode::SVF_NOTCH;
		const auto change = decode_change(encode(written, kContext), encode(state, kContext), state, kContext);
		expect(change->state.effects.filter.lpf_mode).to_equal(deluge::effects::FilterMode::SVF_NOTCH);
		expect(change->state.effects.delay.type).to_equal(deluge::effects::DelayType::ANALOG);
		expect(change->changed).to_equal(std::vector<std::string_view>{"lpfMode"});
	});

	it("reads whether a stutter uses the song's from the slot", _ {
		SoundState state = sampled();
		state.effects.stutter.use_song_stutter = false;
		SoundState written = state;
		written.effects.stutter.use_song_stutter = true;
		written.effects.stutter.reversed = true;
		const auto decoded = decode_onto(encode(written, kContext), state, kContext);
		expect(decoded->effects.stutter.reversed).to_be_true();
		expect(decoded->effects.stutter.use_song_stutter).to_be_true();
		expect(decode_onto(encode(state, kContext), state, kContext)->effects.stutter.use_song_stutter).to_be_false();
	});

	it("keeps an unchanged effect entry", _ {
		SoundState state = sampled();
		state.effects.filter.lpf_mode = deluge::effects::FilterMode::SVF_NOTCH;
		const auto decoded = decode_onto(encode(state, kContext), state, kContext);
		expect(decoded->effects.filter.lpf_mode).to_equal(deluge::effects::FilterMode::SVF_NOTCH);
	});

	it("writes a setting into the typed state as decoding the whole slot with the leaf changed does", _ {
		expect(with_setting_disagreements()).to_equal(std::vector<std::string>{});
	});

	it("writes a setting into the typed state as with_setting does, keeping a value no file holds", _ {
		expect(write_setting_disagreements()).to_equal(std::vector<std::string>{});
	});

	it("names no entry between a state and itself", _ {
		for (const auto& [name, state] : deluge::instrument::test::matrix()) {
			expect(changed_entries(state, state)).to_equal(std::vector<std::string_view>{});
		}
	});

	it("names each entry whose typed fields differ, in file order", _ {
		SoundState state = sampled();
		SoundState written = state;
		written.lfo_wave_type[1] = LFOType::SAW;
		written.voice_priority = deluge::engine::VoicePriority::HIGH;
		written.effects.delay.ping_pong = !state.effects.delay.ping_pong;
		written.osc_retrigger_phase[1] = 7;
		expect(changed_entries(state, written))
		    .to_equal(std::vector<std::string_view>{"voicePriority", "osc2", "lfo2", "delay"});
	});

	it("tells which entries a change to the slot touches, and reads them as decoding onto the state does", _ {
		SoundState state = sampled();
		SoundState written = state;
		written.lfo_wave_type[1] = LFOType::SAW;
		written.voice_priority = deluge::engine::VoicePriority::HIGH;
		const Value element = encode(written, kContext);
		const auto change = decode_change(element, encode(state, kContext), state, kContext);
		expect(change->changed).to_equal(std::vector<std::string_view>{"voicePriority", "lfo2"});
		expect(change->state).to_equal(*decode_onto(element, state, kContext));
	});
});

CPPSPEC_SPEC(decoding_onto)
