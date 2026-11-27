#include "instruments/classic/codec.h"
#include "instruments/classic/settings.h"
#include "instruments/classic/state_access.h"
#include "instruments/classic/state_rules.h"
#include "plugin/types/setting.h"
#include "plugin/types/sync.h"
#include "serialization/tree.h"
#include "support/printers/sound_record_printer.h"
#include "support/record_state.h"
#include "support/sound_matrix.h"

#include <algorithm>
#include <array>
#include <cppspec.hpp>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace deluge::instruments::classic;
using deluge::serialization::Value;
using deluge::synth::SourceRange;

namespace {

constexpr deluge::plugin::CodecContext kContext{.input_tick_magnitude = 1};

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
	return path == "mode" || (path.ends_with("/type") && path.starts_with("osc")) || path.ends_with("/loopMode");
}

/// Where with_setting and the decode of the whole slot with the leaf changed disagree, over every state of the matrix,
/// every path a write reaches, three of its values and two magnitudes.
std::vector<std::string> with_setting_disagreements() {
	std::vector<std::string> found;
	for (const int32_t magnitude : {1, 3}) {
		const deluge::plugin::CodecContext codec{.input_tick_magnitude = magnitude};
		for (const auto& [name, state] : deluge::test::corner_sounds()) {
			const Value whole = encode(state, codec);
			for (const deluge::plugin::SettingInfo& row : kSettings) {
				if (row.kind == deluge::plugin::SettingKind::STRUCTURED || reshapes(row.path)) {
					continue;
				}
				const auto [least, greatest] = deluge::plugin::menu_range(row);
				for (const int32_t menu : {least, greatest, least + (greatest - least) / 2}) {
					const auto stored = deluge::plugin::from_menu(row, menu);
					if (!stored) {
						continue;
					}
					std::array<char, 16> scratch{};
					const auto word = row.kind == deluge::plugin::SettingKind::CHOICE
					                      ? deluge::plugin::stored_text(row, stored->first, scratch)
					                      : std::nullopt;
					const Value given = word ? Value::makeScalar(std::string(*word)) : Value::makeNumber(stored->first);
					const auto value = canonical_setting(row.path, given);
					if (!value) {
						continue;
					}
					Value leaf = *value;
					if (row.path.ends_with("/syncLevel")) {
						leaf = Value::makeNumber(deluge::plugin::sync_level_to_file(
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
/// modulator outside FM; the DX7 outside DX7 mode; sample playback off a sample source; a sample source's own tuning
/// and phase; an oscillator the DX7 parks): with_setting loses such a write, and write_setting keeps it.
constexpr std::array<std::string_view, 25> kUnheldPaths{
    "dx7/transpose",
    "dx7/cents",
    "dx7/retrigPhase",
    "modulator1/transpose",
    "modulator1/cents",
    "modulator1/retrigPhase",
    "modulator2/transpose",
    "modulator2/cents",
    "modulator2/retrigPhase",
    "modulator2/toModulator1",
    "osc1/transpose",
    "osc1/cents",
    "osc1/retrigPhase",
    "osc1/linearInterpolation",
    "osc1/reversed",
    "osc1/timeStretchEnable",
    "osc1/timeStretchAmount",
    "osc2/transpose",
    "osc2/cents",
    "osc2/retrigPhase",
    "osc2/oscillatorSync",
    "osc2/linearInterpolation",
    "osc2/reversed",
    "osc2/timeStretchEnable",
    "osc2/timeStretchAmount",
};

/// Where write_setting on a copy of the state and with_setting disagree, over the same states, paths, values and
/// magnitudes as with_setting_disagreements: the state, and the entries touched, which are the entries whose typed
/// fields changed (with_setting also names one whose file form alone changed, a leaf the encoding leaves out at its
/// default written as it). Where with_setting loses a write of an unheld path, the writer must give its state with
/// that one field written.
std::vector<std::string> write_setting_disagreements() {
	std::vector<std::string> found;
	for (const int32_t magnitude : {1, 3}) {
		const deluge::plugin::CodecContext codec{.input_tick_magnitude = magnitude};
		for (const auto& [name, state] : deluge::test::corner_sounds()) {
			for (const deluge::plugin::SettingInfo& row : kSettings) {
				if (row.kind == deluge::plugin::SettingKind::STRUCTURED || reshapes(row.path)) {
					continue;
				}
				const auto [least, greatest] = deluge::plugin::menu_range(row);
				for (const int32_t menu : {least, greatest, least + (greatest - least) / 2}) {
					const auto stored = deluge::plugin::from_menu(row, menu);
					if (!stored) {
						continue;
					}
					std::array<char, 16> scratch{};
					const auto word = row.kind == deluge::plugin::SettingKind::CHOICE
					                      ? deluge::plugin::stored_text(row, stored->first, scratch)
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
	SoundState state;
	state = deluge::test::with_kind(state, SourceId::A, OscType::SAMPLE);
	state = with_loop_mode(std::move(state), SourceId::A, SampleRepeatMode::LOOP);
	deluge::test::set(state, "osc1/reversed", "1");
	state = with_ranges(
	    std::move(state), SourceId::A,
	    {SourceRange{.top_note = 60, .file_path = "SAMPLES/LOW.WAV", .zone = {.start = 10, .end = 900}},
	     SourceRange{.top_note = 32767, .file_path = "SAMPLES/HIGH.WAV", .zone = {.start = 0, .end = 700}}});
	deluge::test::set(state, "osc1/retrigPhase", "12345");
	state = deluge::test::with_kind(state, SourceId::B, OscType::SAW);
	state =
	    with_ranges(std::move(state), SourceId::B, {SourceRange{.top_note = 32767, .file_path = "SAMPLES/OLD.WAV"}});
	return state;
}

/// The file text of the high voice priority.
std::string high_priority() {
	return std::to_string(std::to_underlying(deluge::plugin::VoicePriority::HIGH));
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

	it("keeps an FM sound's oscillator types, samples and DX7, which no file holds", _ {
		SoundState state = sampled();
		state = deluge::test::with_kind(state, SourceId::B, OscType::SAW);
		state = with_dx7_patch(std::move(state), DxPatchState{.params = {}, .engine_mode = 1, .random_detune = 3});
		state = switch_engine(std::move(state), SynthMode::FM);
		const auto decoded = decode_onto(encode(state, kContext), state, kContext);
		expect(decoded.has_value()).to_be_true();
		expect(*decoded).to_equal(state);
	});

	it("keeps a non-FM sound's modulators", _ {
		SoundState state = sampled();
		deluge::test::set(state, "modulator1/transpose", "5");
		deluge::test::set(state, "modulator2/transpose", "-7");
		deluge::test::set(state, "modulator2/toModulator1", "1");
		const auto decoded = decode_onto(encode(state, kContext), state, kContext);
		expect(modulators(*decoded)[0].tuning.transpose).to_equal(int16_t{5});
		expect(modulators(*decoded)[1].tuning.transpose).to_equal(int16_t{-7});
		expect(modulator1_to_modulator0(*decoded)).to_be_true();
	});

	it("keeps a ring-mod sound's sample oscillator, which only reading a file clamps", _ {
		SoundState state = switch_engine(sampled(), SynthMode::RINGMOD);
		const auto decoded = decode_onto(encode(state, kContext), state, kContext);
		expect(source_type(*decoded, SourceId::A)).to_equal(OscType::SAMPLE);
		SoundState fresh;
		expect(decode(encode(state, kContext), fresh, kContext).has_value()).to_be_true();
		expect(source_type(fresh, SourceId::A)).to_equal(OscType::WAVETABLE);
	});

	it("takes every value the element holds", _ {
		SoundState state = sampled();
		SoundState written = state;
		deluge::test::set(written, "voicePriority", high_priority());
		const auto decoded = decode_onto(encode(written, kContext), state, kContext);
		expect(voicing(*decoded).priority).to_equal(deluge::plugin::VoicePriority::HIGH);
	});

	it("keeps a sample source's own transpose and cents, which a file holds only as its ranges'", _ {
		SoundState state = sampled();
		deluge::test::set(state, "osc1/transpose", "7");
		deluge::test::set(state, "osc1/cents", "-12");
		const auto decoded = decode_onto(encode(state, kContext), state, kContext);
		expect(*decoded).to_equal(state);
	});

	it("keeps what an unchanged entry says, even where the file could not say it (a sync level)", _ {
		SoundState state = sampled();
		deluge::test::set(state, "lfo1/syncLevel", "1");
		constexpr deluge::plugin::CodecContext kSlow{.input_tick_magnitude = 0};
		const auto decoded = decode_onto(encode(state, kSlow), state, kSlow);
		expect(*decoded).to_equal(state);
	});

	it("reads a changed entry as a file holds it", _ {
		SoundState state = sampled();
		SoundState written = state;
		deluge::test::set(written, "lfo1/syncLevel", "1");
		constexpr deluge::plugin::CodecContext kSlow{.input_tick_magnitude = 0};
		const auto decoded = decode_onto(encode(written, kSlow), state, kSlow);
		expect(static_cast<int32_t>(lfo(*decoded, 0).sync_level)).to_equal(2); // where magnitude 0's file keeps WHOLE
	});

	it("keeps the ranges a changed oscillator type carries over", _ {
		SoundState state = sampled();
		state = with_ranges(std::move(state), SourceId::B,
		                    {SourceRange{.top_note = 32767, .file_path = "SAMPLES/OLD.WAV", .zone = {.end = 500}}});
		const SoundState written = deluge::test::with_kind(state, SourceId::B, OscType::SAMPLE);
		const auto decoded = decode_onto(encode(written, kContext), state, kContext);
		expect(source_ranges(*decoded, SourceId::B)).to_equal(source_ranges(state, SourceId::B));
	});

	it("reads the ranges a changed oscillator type reshapes", _ {
		SoundState state = sampled();
		SoundState written = deluge::test::with_kind(state, SourceId::A, OscType::WAVETABLE);
		written = with_ranges(std::move(written), SourceId::A, {SourceRange{.top_note = 60}, SourceRange{.top_note = 32767}});
		const auto decoded = decode_onto(encode(written, kContext), state, kContext);
		expect(source_ranges(*decoded, SourceId::A)).to_equal(source_ranges(written, SourceId::A));
	});

	it("carries no sample range's zone into a wavetable, even one with no file", _ {
		SoundState state = sampled();
		state = with_ranges(std::move(state), SourceId::A, {SourceRange{.top_note = 32767, .zone = {.end = 9999999}}});
		SoundState written = deluge::test::with_kind(state, SourceId::A, OscType::WAVETABLE);
		written = with_ranges(std::move(written), SourceId::A, {SourceRange{.top_note = 32767}});
		const auto decoded = decode_onto(encode(written, kContext), state, kContext);
		expect(source_ranges(*decoded, SourceId::A)).to_equal(source_ranges(written, SourceId::A));
	});

	it("keeps an LFO's sync level the file could not say when only its other settings change", _ {
		SoundState state = sampled();
		deluge::test::set(state, "lfo1/syncLevel", "1");
		SoundState written = state;
		deluge::test::set(written, "lfo1/type", to_string(LFOType::SAW));
		constexpr deluge::plugin::CodecContext kSlow{.input_tick_magnitude = 0};
		const auto decoded = decode_onto(encode(written, kSlow), state, kSlow);
		expect(lfo(*decoded, 0).shape).to_equal(LFOType::SAW);
		expect(static_cast<int32_t>(lfo(*decoded, 0).sync_level)).to_equal(1);
	});

	it("reads a changed effect entry and keeps the others", _ {
		SoundState state = sampled();
		sound_effects(state).delay.type = deluge::effects::DelayType::ANALOG;
		SoundState written = state;
		sound_effects(written).filter.lpf_mode = deluge::effects::FilterMode::SVF_NOTCH;
		const auto change = decode_change(encode(written, kContext), encode(state, kContext), state, kContext);
		expect(sound_effects(change->state).filter.lpf_mode).to_equal(deluge::effects::FilterMode::SVF_NOTCH);
		expect(sound_effects(change->state).delay.type).to_equal(deluge::effects::DelayType::ANALOG);
		expect(change->changed).to_equal(std::vector<std::string_view>{"lpfMode"});
	});

	it("reads whether a stutter uses the song's from the slot", _ {
		SoundState state = sampled();
		sound_effects(state).stutter.use_song_stutter = false;
		SoundState written = state;
		sound_effects(written).stutter.use_song_stutter = true;
		sound_effects(written).stutter.reversed = true;
		const auto decoded = decode_onto(encode(written, kContext), state, kContext);
		expect(sound_effects(*decoded).stutter.reversed).to_be_true();
		expect(sound_effects(*decoded).stutter.use_song_stutter).to_be_true();
		expect(sound_effects(*decode_onto(encode(state, kContext), state, kContext)).stutter.use_song_stutter)
		    .to_be_false();
	});

	it("keeps an unchanged effect entry", _ {
		SoundState state = sampled();
		sound_effects(state).filter.lpf_mode = deluge::effects::FilterMode::SVF_NOTCH;
		const auto decoded = decode_onto(encode(state, kContext), state, kContext);
		expect(sound_effects(*decoded).filter.lpf_mode).to_equal(deluge::effects::FilterMode::SVF_NOTCH);
	});

	it("writes a setting into the typed state as decoding the whole slot with the leaf changed does", _ {
		expect(with_setting_disagreements()).to_equal(std::vector<std::string>{});
	});

	it("writes a setting into the typed state as with_setting does, keeping a value no file holds", _ {
		expect(write_setting_disagreements()).to_equal(std::vector<std::string>{});
	});

	it("names no entry between a state and itself", _ {
		for (const auto& [name, state] : deluge::test::corner_sounds()) {
			expect(changed_entries(state, state)).to_equal(std::vector<std::string_view>{});
		}
	});

	it("names each entry whose typed fields differ, in file order", _ {
		SoundState state = sampled();
		SoundState written = state;
		deluge::test::set(written, "lfo2/type", to_string(LFOType::SAW));
		deluge::test::set(written, "voicePriority", high_priority());
		sound_effects(written).delay.ping_pong = !sound_effects(state).delay.ping_pong;
		deluge::test::set(written, "osc2/retrigPhase", "7");
		expect(changed_entries(state, written))
		    .to_equal(std::vector<std::string_view>{"voicePriority", "osc2", "lfo2", "delay"});
	});

	it("tells which entries a change to the slot touches, and reads them as decoding onto the state does", _ {
		SoundState state = sampled();
		SoundState written = state;
		deluge::test::set(written, "lfo2/type", to_string(LFOType::SAW));
		deluge::test::set(written, "voicePriority", high_priority());
		const Value element = encode(written, kContext);
		const auto change = decode_change(element, encode(state, kContext), state, kContext);
		expect(change->changed).to_equal(std::vector<std::string_view>{"voicePriority", "lfo2"});
		expect(change->state).to_equal(*decode_onto(element, state, kContext));
	});
});

CPPSPEC_SPEC(decoding_onto)
