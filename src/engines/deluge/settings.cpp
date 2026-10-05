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

#include "engine-api/types/sync.h"
#include "engines/deluge/codec.h"
#include "engines/deluge/words.h"
#include "serialization/tree.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <limits>
#include <string>

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

#define LFO_ROWS(n)                                                                                                    \
	SettingInfo{.path = "lfo" #n "/type", .kind = CHOICE, .choices = kLfoTypes},                                       \
	    SettingInfo{.path = "lfo" #n "/syncLevel", .min = 0, .max = engine::kMaxSyncLevel}, SettingInfo {              \
		.path = "lfo" #n "/syncType", .kind = CHOICE, .choices = kSyncTypes                                            \
	}

#define MODULATOR_ROWS(n)                                                                                              \
	SettingInfo{.path = "modulator" #n "/transpose", .min = -kTransposeRange, .max = kTransposeRange},                 \
	    SettingInfo{.path = "modulator" #n "/cents", .min = -kCentsRange, .max = kCentsRange}, SettingInfo {           \
		.path = "modulator" #n "/retrigPhase", .min = kPhaseMin, .max = kPhaseMax                                      \
	}

constexpr std::array kRows{
    SettingInfo{.path = "polyphonic", .kind = CHOICE, .choices = kPolyphony},
    SettingInfo{.path = "voicePriority", .min = 0, .max = 2},
    SettingInfo{.path = "sideChainSend", .min = 0, .max = std::numeric_limits<int32_t>::max()},
    SettingInfo{.path = "mode", .kind = CHOICE, .choices = kSynthModes},
    SettingInfo{.path = "transpose", .min = -kTransposeRange, .max = kTransposeRange},
    SettingInfo{.path = "cents", .min = -kCentsRange, .max = kCentsRange},
    SettingInfo{.path = "maxVoices", .min = 1, .max = kMaxVoicesOff},
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

/// @p text as a whole decimal integer, or nothing.
std::optional<int64_t> whole_number(std::string_view text) {
	int64_t n = 0;
	const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), n);
	if (error != std::errc{} || end != text.data() + text.size()) {
		return std::nullopt;
	}
	return n;
}

} // namespace

const std::span<const engine::SettingInfo> kSettings{kRows};

bool check_setting(std::string_view path, const serialization::Value& value) {
	const SettingInfo* row = engine::find_setting(kSettings, path);
	const std::string* text = serialization::text_of(&value);
	if (row == nullptr || text == nullptr) {
		return false;
	}
	switch (row->kind) {
	case CHOICE:
		return std::ranges::find(row->choices, std::string_view(*text)) != row->choices.end();
	case INTEGER: {
		const std::optional<int64_t> n = whole_number(*text);
		return n && *n >= row->min && *n <= row->max;
	}
	case BOOL:
		return *text == "0" || *text == "1";
	case STRUCTURED:
		break;
	}
	return false;
}

} // namespace deluge::instrument
