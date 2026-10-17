/*
 * Copyright © 2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#include "engines/deluge/settings.h"

#include "engine-api/settings.h"

#include "engine-api/types/arp.h"
#include "engine-api/types/sync.h"
#include "engine-api/types/voice_priority.h"
#include "engines/deluge/codec.h"
#include "engines/deluge/words.h"
#include "engines/effects/settings.h"
#include "serialization/tree.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <limits>
#include <string>
#include <utility>

namespace deluge::instrument {

namespace {

using engine::SettingInfo;
using enum engine::SettingKind;

/// The names a file writes for each value of @p Enum, from its first to @p last.
template <class Enum, size_t N>
constexpr std::array<std::string_view, N> names_of() {
	std::array<std::string_view, N> names{};
	for (size_t i = 0; i < N; ++i) {
		names[i] = to_string(static_cast<Enum>(i));
	}
	return names;
}

constexpr auto kPolyphony = names_of<PolyphonyMode, 5>();
constexpr auto kSynthModes = names_of<SynthMode, 3>();
constexpr auto kOscTypes = names_of<OscType, 12>();
constexpr auto kLfoTypes = names_of<LFOType, 7>();
/// A synced rate's type, as its stored number (even, triplet, dotted).
constexpr std::array<std::string_view, 3> kSyncTypes{"0", "10", "19"};
/// A sound's MIDI output channel as a file writes it: 0–15, or 255 for none. Nothing between is a channel.
constexpr std::array<std::string_view, 17> kMidiChannels{"0", "1",  "2",  "3",  "4",  "5",  "6",  "7",  "8",
                                                         "9", "10", "11", "12", "13", "14", "15", "255"};

constexpr int32_t kTransposeRange = 96; ///< semitones either way: the transpose menus' ±9600 cents
constexpr int32_t kCentsRange = 50;
/// A retrigger phase as a file writes it: the 32-bit phase (a fraction of 2^32) as a signed integer, −1 (0xFFFFFFFF)
/// free-running. The menu's degrees are 11930464 apart.
constexpr int32_t kPhaseMin = std::numeric_limits<int32_t>::min();
constexpr int32_t kPhaseMax = std::numeric_limits<int32_t>::max();
/// The voice limit the max-voices menu's "off" writes: 1–16 are limits, this one is none in practice.
constexpr int32_t kMaxVoicesOff = 127;
/// The greatest limit the max-voices menu offers.
constexpr int32_t kMaxVoicesMenu = 16;
/// The max-voices menu's 0 is "off", stored as kMaxVoicesOff.
constexpr std::array<engine::SettingSentinel, 1> kMaxVoicesSentinels{{{.user = 0, .stored = kMaxVoicesOff}}};
/// A stored limit past the menu's (none a menu writes) shows as off.
constexpr engine::SettingDescriptorExtras kMaxVoicesExtras{
    .mapping = {.kind = engine::DisplayMappingKind::CLAMP_HIGH, .clamp_to = 0}};

/// A voice priority as its stored number, in engine::VoicePriority's order (low, medium, high).
constexpr std::array<std::string_view, 3> kVoicePriorities{"0", "1", "2"};
static_assert(std::to_underlying(engine::VoicePriority::HIGH) + 1 == kVoicePriorities.size());

/// Choke shares a kit's voices among its lanes, so only a kit lane offers it.
constexpr engine::SettingDescriptorExtras kPolyphonyExtras{
    .choice_applies = [](const engine::SettingContext& shown_for, size_t choice) {
	    return choice != std::to_underlying(PolyphonyMode::CHOKE)
	           || shown_for.owner == engine::SettingContext::Owner::KIT_LANE;
    }};

// An LFO's sync level is the file's number, which is relative to the song's input tick magnitude; its range is the
// internal one's, the numbers a song at the default magnitude writes.

#define OSC_ROWS(n)                                                                                                    \
	SettingInfo{.path = "osc" #n "/type", .kind = CHOICE, .choices = kOscTypes},                                       \
	    SettingInfo{.path = "osc" #n "/transpose", .min = -kTransposeRange, .max = kTransposeRange},                   \
	    SettingInfo{.path = "osc" #n "/cents", .min = -kCentsRange, .max = kCentsRange},                               \
	    SettingInfo{.path = "osc" #n "/retrigPhase", .min = kPhaseMin, .max = kPhaseMax},                              \
	    SettingInfo{.path = "osc" #n "/loopMode", .min = 0, .max = 3},                                                 \
	    SettingInfo{.path = "osc" #n "/reversed", .kind = BOOL},                                                       \
	    SettingInfo{.path = "osc" #n "/timeStretchEnable", .kind = BOOL},                                              \
	    SettingInfo{.path = "osc" #n "/timeStretchAmount", .min = -48, .max = 48},                                     \
	    SettingInfo{.path = "osc" #n "/linearInterpolation", .kind = BOOL},                                            \
	    SettingInfo{.path = "osc" #n "/sampleRanges", .kind = STRUCTURED},                                             \
	    SettingInfo{.path = "osc" #n "/wavetableRanges", .kind = STRUCTURED},                                          \
	    SettingInfo{.path = "osc" #n "/fileName", .kind = STRUCTURED},                                                 \
	    SettingInfo{.path = "osc" #n "/zone", .kind = STRUCTURED},                                                     \
	    SettingInfo{.path = "osc" #n "/dx7patch", .kind = STRUCTURED},                                                 \
	    SettingInfo{.path = "osc" #n "/dx7enginemode", .kind = STRUCTURED}, SettingInfo {                              \
		.path = "osc" #n "/dx7randomdetune", .kind = STRUCTURED                                                        \
	}

/// An LFO's sync menu: one value over its level and its type row, @p partner.
constexpr engine::SettingDescriptorExtras lfo_sync_extras(std::string_view partner) {
	return {.mapping = {.kind = engine::DisplayMappingKind::COMBINED_SYNC, .partner = partner}};
}
constexpr std::array kLfoSyncExtras{lfo_sync_extras("lfo1/syncType"), lfo_sync_extras("lfo2/syncType"),
                                    lfo_sync_extras("lfo3/syncType"), lfo_sync_extras("lfo4/syncType")};

// An LFO's syncLevel is the level itself (1–9, 0 = off), whatever the song's tick magnitude; a file, and so a slot,
// writes it relative to that magnitude.
#define LFO_ROWS(n)                                                                                                    \
	SettingInfo{.path = "lfo" #n "/type", .kind = CHOICE, .choices = kLfoTypes},                                       \
	    SettingInfo{.path = "lfo" #n "/syncLevel",                                                                     \
	                .min = 0,                                                                                          \
	                .max = engine::kMaxSyncLevel,                                                                      \
	                .units = engine::SettingUnits::SYNC_LEVEL,                                                         \
	                .extras = &kLfoSyncExtras[n - 1]},                                                                 \
	    SettingInfo {                                                                                                  \
		.path = "lfo" #n "/syncType", .kind = CHOICE, .choices = kSyncTypes                                            \
	}

#define MODULATOR_ROWS(n)                                                                                              \
	SettingInfo{.path = "modulator" #n "/transpose", .min = -kTransposeRange, .max = kTransposeRange},                 \
	    SettingInfo{.path = "modulator" #n "/cents", .min = -kCentsRange, .max = kCentsRange}, SettingInfo {           \
		.path = "modulator" #n "/retrigPhase", .min = kPhaseMin, .max = kPhaseMax                                      \
	}

constexpr std::array kRows{
    SettingInfo{.path = "polyphonic", .kind = CHOICE, .choices = kPolyphony, .extras = &kPolyphonyExtras},
    SettingInfo{.path = "voicePriority", .kind = CHOICE, .choices = kVoicePriorities},
    SettingInfo{.path = "sideChainSend", .min = 0, .max = std::numeric_limits<int32_t>::max()},
    SettingInfo{.path = "mode", .kind = CHOICE, .choices = kSynthModes},
    SettingInfo{.path = "transpose", .min = -kTransposeRange, .max = kTransposeRange},
    SettingInfo{.path = "cents", .min = -kCentsRange, .max = kCentsRange},
    // A file holds any limit 1–127; the menu offers 0 (off) to kMaxVoicesMenu.
    SettingInfo{.path = "maxVoices",
                .min = 1,
                .max = kMaxVoicesOff,
                .user = {.user_min = 0,
                         .user_max = kMaxVoicesMenu,
                         .stored_min = 0,
                         .stored_max = kMaxVoicesMenu,
                         .sentinels = kMaxVoicesSentinels},
                .extras = &kMaxVoicesExtras},
    OSC_ROWS(1),
    OSC_ROWS(2),
    SettingInfo{.path = "osc2/oscillatorSync", .kind = BOOL},
    LFO_ROWS(1),
    LFO_ROWS(2),
    LFO_ROWS(3),
    LFO_ROWS(4),
    MODULATOR_ROWS(1),
    MODULATOR_ROWS(2),
    SettingInfo{.path = "modulator2/toModulator1", .kind = BOOL},
    SettingInfo{.path = "unison/num", .min = 1, .max = kMaxUnisonVoices},
    SettingInfo{.path = "unison/detune", .min = 0, .max = kMaxUnisonDetune},
    SettingInfo{.path = "unison/spread", .min = 0, .max = kMaxUnisonSpread},
    SettingInfo{.path = "midiOutput/channel", .kind = CHOICE, .choices = kMidiChannels},
    SettingInfo{.path = "midiOutput/noteForDrum", .min = 0, .max = 127},
};

#undef OSC_ROWS
#undef LFO_ROWS
#undef MODULATOR_ROWS

using Answer = std::optional<serialization::Value>;

Answer number(int64_t n) {
	return serialization::Value::makeNumber(n);
}

Answer flag(bool on) {
	return number(on ? 1 : 0);
}

Answer word(std::string_view text) {
	return serialization::Value::makeScalar(std::string(text));
}

/// A retrigger phase as a file writes it: the 32-bit phase as a signed integer.
Answer phase(uint32_t value) {
	return number(static_cast<int32_t>(value));
}

/// The digit after a family's prefix as an index from 0, or nothing when @p path has no `<digit>/` there.
std::optional<size_t> indexAfter(std::string_view path, size_t at, char last) {
	if (path.size() <= at + 1 || path[at] < '1' || path[at] > last || path[at + 1] != '/') {
		return std::nullopt;
	}
	return static_cast<size_t>(path[at] - '1');
}

Answer topLevelValue(const SoundState& state, std::string_view path) {
	if (path == "polyphonic") {
		return word(to_string(state.polyphonic));
	}
	if (path == "voicePriority") {
		return number(std::to_underlying(state.voice_priority));
	}
	if (path == "sideChainSend") {
		return number(state.side_chain_send_level);
	}
	if (path == "mode") {
		return word(to_string(state.synth_mode));
	}
	if (path == "transpose") {
		return number(state.transpose);
	}
	if (path == "cents") {
		return number(state.cents);
	}
	if (path == "maxVoices") {
		return number(state.max_voice_count);
	}
	return std::nullopt;
}

Answer oscillatorValue(const SoundState& state, std::string_view path) {
	const std::optional<size_t> s = indexAfter(path, 3, '2');
	if (!s) {
		return std::nullopt;
	}
	const SourceState& src = state.sources[*s];
	const std::string_view rest = path.substr(5);
	const bool fm = state.synth_mode == SynthMode::FM;
	const bool sample = src.osc_type == OscType::SAMPLE;
	const bool input =
	    src.osc_type == OscType::INPUT_L || src.osc_type == OscType::INPUT_R || src.osc_type == OscType::INPUT_STEREO;
	if (rest == "linearInterpolation") {
		return !fm && (sample || input) ? flag(src.interpolation_mode == engine::InterpolationMode::LINEAR)
		                                : std::nullopt;
	}
	if (rest == "loopMode" || rest == "reversed" || rest == "timeStretchEnable" || rest == "timeStretchAmount") {
		if (fm || !sample) {
			return std::nullopt;
		}
		return rest == "loopMode"            ? number(std::to_underlying(src.repeat_mode))
		       : rest == "reversed"          ? flag(src.reversed)
		       : rest == "timeStretchEnable" ? flag(src.pitch_and_speed_independent)
		                                     : number(src.time_stretch_amount);
	}
	if (rest == "type") {
		return fm ? std::nullopt : word(to_string(src.osc_type));
	}
	if (rest == "transpose" || rest == "cents") {
		if (!fm && sample) {
			return std::nullopt; // a sample source's tuning is its ranges'
		}
		return rest == "transpose" ? number(src.transpose) : number(src.cents);
	}
	if (rest == "retrigPhase") {
		return phase(state.osc_retrigger_phase[*s]);
	}
	if (rest == "oscillatorSync" && *s == 1) {
		return flag(state.oscillator_sync);
	}
	return std::nullopt;
}

Answer lfoValue(const SoundState& state, std::string_view path) {
	const std::optional<size_t> l = indexAfter(path, 3, '4');
	if (!l) {
		return std::nullopt;
	}
	const std::string_view rest = path.substr(5);
	if (rest == "type") {
		return word(to_string(state.lfo_wave_type[*l]));
	}
	if (rest == "syncLevel") {
		return number(engine::index_of(state.lfo_sync_level[*l]));
	}
	if (rest == "syncType") {
		return number(std::to_underlying(state.lfo_sync_type[*l]));
	}
	return std::nullopt;
}

Answer modulatorValue(const SoundState& state, std::string_view path) {
	const std::optional<size_t> m = indexAfter(path, 9, '2');
	if (!m || state.synth_mode != SynthMode::FM) {
		return std::nullopt; // only FM writes its modulators
	}
	const std::string_view rest = path.substr(11);
	if (rest == "transpose") {
		return number(state.modulator_transpose[*m]);
	}
	if (rest == "cents") {
		return number(state.modulator_cents[*m]);
	}
	if (rest == "retrigPhase") {
		return phase(state.modulator_retrigger_phase[*m]);
	}
	if (rest == "toModulator1" && *m == 1) {
		return flag(state.modulator1_to_modulator0);
	}
	return std::nullopt;
}

Answer unisonValue(const SoundState& state, std::string_view path) {
	const std::string_view rest = path.substr(7);
	if (rest == "num") {
		return number(state.num_unison);
	}
	if (rest == "detune") {
		return number(state.unison_detune);
	}
	if (rest == "spread") {
		return number(state.unison_stereo_spread);
	}
	return std::nullopt;
}

Answer midiOutputValue(const SoundState& state, std::string_view path) {
	const std::string_view rest = path.substr(11);
	if (rest == "channel") {
		return number(state.output_midi_channel);
	}
	if (rest == "noteForDrum") {
		return number(state.output_midi_note_for_drum);
	}
	return std::nullopt;
}

/// Each family of setting paths, by its prefix, and how a state answers one; the empty prefix takes the rest.
struct Family {
	std::string_view prefix;
	Answer (*value)(const SoundState&, std::string_view);
};
constexpr std::array kFamilies{
    Family{"osc", oscillatorValue},         Family{"lfo", lfoValue},
    Family{"modulator", modulatorValue},    Family{"unison/", unisonValue},
    Family{"midiOutput/", midiOutputValue}, Family{"", topLevelValue},
};

/// The instrument's rows, then its effects'.
constexpr auto kAllRows = [] {
	std::array<engine::SettingInfo, kRows.size() + effects::kEffectSettings.size()> all{};
	std::ranges::copy(kRows, all.begin());
	std::ranges::copy(effects::kEffectSettings, all.begin() + kRows.size());
	return all;
}();

} // namespace

const std::span<const engine::SettingInfo> kSettings{kAllRows};
const std::span<const engine::SettingInfo> kClipScopedSettings{engine::kArpSettingRows};

namespace {

/// @p path's value in @p state, an unset one included, or nothing where the path does not apply.
Answer anyValue(const SoundState& state, std::string_view path) {
	const SettingInfo* row = engine::find_setting(kSettings, path);
	if (row == nullptr || row->kind == STRUCTURED) {
		return std::nullopt;
	}
	if (engine::find_setting(effects::kEffectSettings, path) != nullptr) {
		return effects::effect_setting_value(state.effects, path);
	}
	for (const Family& family : kFamilies) {
		if (path.starts_with(family.prefix)) {
			return family.value(state, path);
		}
	}
	return std::nullopt;
}

} // namespace

std::optional<serialization::Value> canonical_setting(std::string_view path, const serialization::Value& value) {
	if (!check_setting(path, value)) {
		return std::nullopt;
	}
	if (engine::find_setting(kSettings, path)->kind == INTEGER) {
		return serialization::Value::makeNumber(*engine::whole_number(*serialization::text_of(&value)));
	}
	return value;
}

std::optional<serialization::Value> setting_value(const SoundState& state, std::string_view path) {
	Answer value = anyValue(state, path);
	if (value && path == "midiOutput/noteForDrum" && value->str() == "255") {
		return std::nullopt; // unset: the menu shows its default there and writes only a note
	}
	return value;
}

bool allows_setting(const SoundState& state, std::string_view path, const serialization::Value& value) {
	const auto ringable = [](OscType type) { return type <= OscType::WAVETABLE; };
	if (path == "mode" && parse_synth_mode(value.str()) == SynthMode::RINGMOD) {
		return std::ranges::all_of(state.sources, ringable, &SourceState::osc_type);
	}
	if (state.synth_mode == SynthMode::RINGMOD && path.starts_with("osc") && path.ends_with("/type")) {
		return ringable(parse_osc_type(value.str()));
	}
	return true;
}

bool takes_setting(const SoundState& state, std::string_view path) {
	return anyValue(state, path).has_value();
}

bool check_setting(std::string_view path, const serialization::Value& value) {
	return engine::check_setting(kSettings, path, value);
}

} // namespace deluge::instrument
