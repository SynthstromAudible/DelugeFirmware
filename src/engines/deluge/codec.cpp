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

#include "engines/deluge/codec.h"

#include "engines/deluge/dx7_init_voice.h"
#include "serialization/tree.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iterator>
#include <span>
#include <string>
#include <utility>

namespace deluge::instrument {

using serialization::Value;

namespace {

/// The top-level keys encode writes, in file order.
constexpr std::array<std::string_view, 17> kStateKeys{
    "polyphonic", "voicePriority", "sideChainSend", "mode",   "transpose", "cents",
    "maxVoices",  "osc1",          "osc2",          "lfo1",   "lfo2",      "lfo3",
    "lfo4",       "modulator1",    "modulator2",    "unison", "midiOutput"};

/// The top-level keys of older files decode still reads and encode never writes: the oscillators by their old names,
/// ring mod as a flag, the retrigger switch, and an early sound's one sample.
constexpr std::array<std::string_view, 8> kOldStateKeys{"oscillatorA", "oscillatorB", "ringMod",  "oscillatorReset",
                                                        "fileName",    "continuous",  "reversed", "zone"};

/// A number as a file holds it: a 32-bit signed integer, whatever the field's own type.
Value number(int64_t value) {
	return Value::makeNumber(static_cast<int32_t>(value));
}

Value text(std::string_view value) {
	return Value::makeScalar(std::string(value));
}

/// @p bytes as upper-case hex, two digits a byte.
std::string hex(std::span<const uint8_t> bytes) {
	constexpr std::string_view kDigits = "0123456789ABCDEF";
	std::string out;
	out.reserve(bytes.size() * 2);
	for (uint8_t b : bytes) {
		out.push_back(kDigits[b >> 4]);
		out.push_back(kDigits[b & 15]);
	}
	return out;
}

/// The sample positions of a range's zone.
Value zone(const RangeState& range) {
	auto map = Value::makeMap();
	map.set("startSamplePos", number(static_cast<int64_t>(range.start_pos)));
	map.set("endSamplePos", number(static_cast<int64_t>(range.end_pos)));
	if (range.loop_start_pos != 0) {
		map.set("startLoopPos", number(range.loop_start_pos));
	}
	if (range.loop_end_pos != 0) {
		map.set("endLoopPos", number(range.loop_end_pos));
	}
	return map;
}

/// A sample range's file, tuning and zone, into @p map.
void sampleRange(Value& map, const RangeState& range) {
	map.set("fileName", text(range.file_path));
	if (range.transpose != 0) {
		map.set("transpose", number(range.transpose));
	}
	if (range.cents != 0) {
		map.set("cents", number(range.cents));
	}
	map.set("zone", zone(range));
}

Value boxed(std::string_view name, Value value) {
	auto box = Value::makeMap();
	box.set(std::string(name), std::move(value));
	return box;
}

Value source(const SoundState& state, size_t s) {
	const SourceState& src = state.sources[s];
	const bool fm = state.synth_mode == SynthMode::FM;
	auto map = Value::makeMap();
	if (!fm) {
		map.set("type", text(to_string(src.osc_type)));
	}
	const auto numRanges = src.ranges.size();
	if (src.osc_type == OscType::SAMPLE && !fm) {
		map.set("loopMode", number(std::to_underlying(src.repeat_mode)));
		map.set("reversed", number(src.reversed ? 1 : 0));
		map.set("timeStretchEnable", number(src.pitch_and_speed_independent ? 1 : 0));
		map.set("timeStretchAmount", number(src.time_stretch_amount));
		if (src.interpolation_mode == engine::InterpolationMode::LINEAR) {
			map.set("linearInterpolation", number(1));
		}
		if (numRanges == 1) {
			sampleRange(map, src.ranges.front());
		}
		else if (numRanges > 1) {
			auto list = Value::makeArray();
			for (size_t e = 0; e < numRanges; e++) {
				auto range = Value::makeMap();
				if (e != numRanges - 1) {
					range.set("rangeTopNote", number(src.ranges[e].top_note));
				}
				sampleRange(range, src.ranges[e]);
				list.append(boxed("sampleRange", std::move(range)));
			}
			map.set("sampleRanges", std::move(list));
		}
		return map;
	}
	map.set("transpose", number(src.transpose));
	map.set("cents", number(src.cents));
	if (s == 1 && state.oscillator_sync) {
		map.set("oscillatorSync", number(1));
	}
	map.set("retrigPhase", number(state.osc_retrigger_phase[s]));
	if (src.osc_type == OscType::WAVETABLE && !fm) {
		if (numRanges == 1) {
			map.set("fileName", text(src.ranges.front().file_path));
		}
		else if (numRanges > 1) {
			auto list = Value::makeArray();
			for (size_t e = 0; e < numRanges; e++) {
				auto range = Value::makeMap();
				if (e != numRanges - 1) {
					range.set("rangeTopNote", number(src.ranges[e].top_note));
				}
				range.set("fileName", text(src.ranges[e].file_path));
				list.append(boxed("wavetableRange", std::move(range)));
			}
			map.set("wavetableRanges", std::move(list));
		}
	}
	else if (src.osc_type == OscType::DX7 && !fm && src.dx_patch) {
		map.set("dx7patch", text(hex(src.dx_patch->params)));
		if (src.dx_patch->engine_mode != 0) {
			map.set("dx7enginemode", number(src.dx_patch->engine_mode));
		}
		if (src.dx_patch->random_detune != 0) {
			map.set("dx7randomdetune", number(src.dx_patch->random_detune));
		}
	}
	return map;
}

Value lfo(const SoundState& state, size_t l, const engine::CodecContext& context) {
	auto map = Value::makeMap();
	map.set("type", text(to_string(state.lfo_wave_type[l])));
	map.set("syncLevel", number(engine::sync_level_to_file(engine::index_of(state.lfo_sync_level[l]),
	                                                       context.input_tick_magnitude)));
	map.set("syncType", number(std::to_underlying(state.lfo_sync_type[l])));
	return map;
}

Value modulator(const SoundState& state, size_t m) {
	auto map = Value::makeMap();
	map.set("transpose", number(state.modulator_transpose[m]));
	map.set("cents", number(state.modulator_cents[m]));
	map.set("retrigPhase", number(state.modulator_retrigger_phase[m]));
	if (m == 1) {
		map.set("toModulator1", number(state.modulator1_to_modulator0 ? 1 : 0));
	}
	return map;
}

void append(Value& to, const Value& from) {
	for (const auto& [key, value] : from.entries()) {
		to.set(key, value);
	}
}

} // namespace

SoundPieces encode_pieces(const SoundState& state, const engine::CodecContext& context) {
	SoundPieces pieces{.head = Value::makeMap(),
	                   .max_voices = Value::makeMap(),
	                   .body = Value::makeMap(),
	                   .midi_output = Value::makeMap()};
	pieces.head.set("polyphonic", text(to_string(state.polyphonic)));
	pieces.head.set("voicePriority", number(std::to_underlying(state.voice_priority)));
	if (state.side_chain_send_level != 0) {
		pieces.head.set("sideChainSend", number(state.side_chain_send_level));
	}
	pieces.head.set("mode", text(to_string(state.synth_mode)));
	if (state.transpose != 0) {
		pieces.head.set("transpose", number(state.transpose));
	}
	if (state.cents != 0) {
		pieces.head.set("cents", number(state.cents));
	}

	pieces.max_voices.set("maxVoices", number(state.max_voice_count));

	pieces.body.set("osc1", source(state, 0));
	pieces.body.set("osc2", source(state, 1));
	for (size_t l = 0; l < state.lfo_wave_type.size(); l++) {
		pieces.body.set("lfo" + std::to_string(l + 1), lfo(state, l, context));
	}
	if (state.synth_mode == SynthMode::FM) {
		pieces.body.set("modulator1", modulator(state, 0));
		pieces.body.set("modulator2", modulator(state, 1));
	}
	auto unison = Value::makeMap();
	unison.set("num", number(state.num_unison));
	unison.set("detune", number(state.unison_detune));
	unison.set("spread", number(state.unison_stereo_spread));
	pieces.body.set("unison", std::move(unison));

	auto midi = Value::makeMap();
	midi.set("channel", number(state.output_midi_channel));
	midi.set("noteForDrum", number(state.output_midi_note_for_drum));
	pieces.midi_output.set("midiOutput", std::move(midi));
	return pieces;
}

Value encode(const SoundState& state, const engine::CodecContext& context) {
	SoundPieces pieces = encode_pieces(state, context);
	auto map = Value::makeMap();
	append(map, pieces.head);
	append(map, pieces.max_voices);
	append(map, pieces.body);
	append(map, pieces.midi_output);
	return map;
}

bool is_state_key(std::string_view key) {
	return std::ranges::find(kStateKeys, key) != kStateKeys.end();
}

serialization::Value with_state(const serialization::Value& element, const SoundState& state,
                                const engine::CodecContext& context) {
	const serialization::Value encoded = encode(state, context);
	serialization::Value out = serialization::Value::makeMap();
	std::vector<std::string_view> placed;
	if (element.isMap()) {
		for (const auto& [key, value] : element.entries()) {
			if (std::ranges::find(kOldStateKeys, key) != kOldStateKeys.end()) {
				continue; // the encoding says it now, and the old form read after it would undo it
			}
			if (!is_state_key(key)) {
				out.set(key, value);
				continue;
			}
			if (const serialization::Value* now = encoded.find(key)) {
				out.set(key, *now);
				placed.push_back(key);
			}
		}
	}
	for (const auto& [key, value] : encoded.entries()) {
		if (std::ranges::find(placed, std::string_view(key)) == placed.end()) {
			out.set(key, value);
		}
	}
	return out;
}

namespace {

int32_t intOf(const Value& value) {
	return serialization::int_of(&value).value_or(0);
}

std::string_view textOf(const Value& value) {
	return value.isScalar() ? std::string_view(value.str()) : std::string_view{};
}

/// What the reader knows of a source beyond its state: whether its ranges are wavetable ranges, and whether two of them
/// on one top note are refused (a file's) or kept in order (a slot's, whose runtime holds them).
struct SourceReading {
	bool wavetableRanges{false};
	bool refuseDuplicates{true};
};

/// A source's ranges become @p wavetable ranges, keeping only their top notes, and there is at least one, as switching
/// a source to a sample or wavetable oscillator does when its ranges were the other kind.
void changeRangeKind(SourceState& src, SourceReading& reading, bool wavetable) {
	if (reading.wavetableRanges == wavetable) {
		return;
	}
	reading.wavetableRanges = wavetable;
	for (RangeState& range : src.ranges) {
		range = RangeState{.top_note = range.top_note};
	}
	if (src.ranges.empty()) {
		src.ranges.push_back(RangeState{.top_note = kOpenTopNote});
	}
}

RangeState& firstRange(SourceState& src) {
	if (src.ranges.empty()) {
		src.ranges.push_back(RangeState{.top_note = kOpenTopNote});
	}
	return src.ranges.front();
}

DxPatchState& dxPatch(SourceState& src) {
	if (!src.dx_patch) {
		src.dx_patch.emplace(DxPatchState{.params = kDx7InitVoice});
	}
	return *src.dx_patch;
}

void setOscType(SourceState& src, SourceReading& reading, OscType type) {
	if (type == OscType::SAMPLE) {
		changeRangeKind(src, reading, false);
	}
	else if (type == OscType::WAVETABLE) {
		changeRangeKind(src, reading, true);
	}
	src.osc_type = type;
	if (type == OscType::DX7) {
		(void)dxPatch(src);
	}
}

SampleRepeatMode repeatModeOf(const Value& value) {
	const int32_t mode = std::clamp(intOf(value), 0, static_cast<int32_t>(SampleRepeatMode::STRETCH));
	return static_cast<SampleRepeatMode>(mode);
}

/// A zone in seconds and milliseconds (early-2016) or in samples, into @p range.
void readZone(const Value& zone, RangeState& range, bool resetPositions) {
	if (resetPositions) {
		range.start_msec = 0;
		range.end_msec = 0;
		range.start_pos = 0;
		range.end_pos = 0;
	}
	if (!zone.isMap()) {
		return;
	}
	for (const auto& [key, value] : zone.entries()) {
		if (key == "startSeconds") {
			range.start_msec += intOf(value) * 1000;
		}
		else if (key == "startMilliseconds") {
			range.start_msec += intOf(value);
		}
		else if (key == "endSeconds") {
			range.end_msec += intOf(value) * 1000;
		}
		else if (key == "endMilliseconds") {
			range.end_msec += intOf(value);
		}
		else if (key == "startSamplePos") {
			range.start_pos =
			    static_cast<uint64_t>(static_cast<int64_t>(intOf(value))); // a 32-bit file value, sign-extended
		}
		else if (key == "endSamplePos") {
			range.end_pos = static_cast<uint64_t>(static_cast<int64_t>(intOf(value)));
		}
		else if (key == "startLoopPos") {
			range.loop_start_pos = static_cast<uint32_t>(intOf(value));
		}
		else if (key == "endLoopPos") {
			range.loop_end_pos = static_cast<uint32_t>(intOf(value));
		}
	}
}

std::expected<void, engine::StateError> readRanges(const Value& list, SourceState& src, bool refuseDuplicates) {
	const bool wavetable = src.osc_type == OscType::WAVETABLE;
	for (const serialization::NamedChild& child : serialization::children(list)) {
		if ((child.name != "sampleRange" && child.name != "wavetableRange") || !child.node->isMap()) {
			continue;
		}
		RangeState range{.top_note = kOpenTopNote};
		for (const auto& [key, value] : child.node->entries()) {
			if (key == "fileName") {
				range.file_path = std::string(textOf(value));
			}
			else if (key == "rangeTopNote") {
				range.top_note = static_cast<int16_t>(intOf(value));
			}
			else if (!wavetable && key == "zone") {
				readZone(value, range, false);
			}
			else if (!wavetable && key == "transpose") {
				range.transpose = static_cast<int16_t>(intOf(value));
			}
			else if (!wavetable && key == "cents") {
				range.cents = static_cast<int8_t>(intOf(value));
			}
		}
		auto at = std::ranges::upper_bound(src.ranges, range.top_note, {}, &RangeState::top_note);
		if (refuseDuplicates && at != src.ranges.begin() && std::prev(at)->top_note == range.top_note) {
			return std::unexpected(engine::StateError::DUPLICATE_RANGE);
		}
		src.ranges.insert(at, std::move(range));
	}
	return {};
}

std::expected<void, engine::StateError> readSource(const Value& osc, SoundState& state, size_t s,
                                                   SourceReading& reading) {
	SourceState& src = state.sources[s];
	if (!osc.isMap()) {
		return {};
	}
	for (const auto& [key, value] : osc.entries()) {
		if (key == "type") {
			setOscType(src, reading, parse_osc_type(textOf(value)));
		}
		else if (key == "transpose") {
			src.transpose = static_cast<int16_t>(intOf(value));
		}
		else if (key == "cents") {
			src.cents = static_cast<int8_t>(intOf(value));
		}
		else if (key == "loopMode") {
			src.repeat_mode = repeatModeOf(value);
		}
		else if (key == "oscillatorSync") {
			state.oscillator_sync = intOf(value) != 0;
		}
		else if (key == "reversed") {
			src.reversed = intOf(value) != 0;
		}
		else if (key == "dx7patch") {
			DxPatchState& patch = dxPatch(src);
			const std::string_view hexText = textOf(value);
			for (size_t b = 0; b < patch.params.size() && 2 * b + 1 < hexText.size(); b++) {
				auto nibble = [](char c) -> int {
					return c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : c - 'a' + 10;
				};
				patch.params[b] = static_cast<uint8_t>((nibble(hexText[2 * b]) << 4) | nibble(hexText[2 * b + 1]));
			}
		}
		else if (key == "dx7randomdetune") {
			dxPatch(src).random_detune = intOf(value);
		}
		else if (key == "dx7enginemode") {
			dxPatch(src).engine_mode = static_cast<uint8_t>(intOf(value));
		}
		else if (key == "timeStretchEnable") {
			src.pitch_and_speed_independent = intOf(value) != 0;
		}
		else if (key == "timeStretchAmount") {
			src.time_stretch_amount = static_cast<int8_t>(intOf(value));
		}
		else if (key == "linearInterpolation") {
			if (intOf(value) != 0) {
				src.interpolation_mode = engine::InterpolationMode::LINEAR;
			}
		}
		else if (key == "retrigPhase") {
			state.osc_retrigger_phase[s] = static_cast<uint32_t>(intOf(value));
		}
		else if (key == "fileName") {
			firstRange(src).file_path = std::string(textOf(value));
		}
		else if (key == "zone") {
			readZone(value, firstRange(src), true);
		}
		else if (key == "sampleRanges" || key == "wavetableRanges") {
			src.ranges.clear(); // the list is every range the source has, the one switching its type made included
			if (auto read = readRanges(value, src, reading.refuseDuplicates); !read) {
				return read;
			}
		}
	}
	return {};
}

void readModulator(const Value& modulator, SoundState& state, size_t m) {
	if (!modulator.isMap()) {
		return;
	}
	for (const auto& [key, value] : modulator.entries()) {
		if (key == "transpose") {
			state.modulator_transpose[m] = static_cast<int16_t>(state.modulator_transpose[m] + intOf(value));
		}
		else if (key == "cents") {
			state.modulator_cents[m] = static_cast<int8_t>(intOf(value));
		}
		else if (key == "retrigPhase") {
			state.modulator_retrigger_phase[m] = static_cast<uint32_t>(intOf(value));
		}
		else if (m == 1 && key == "toModulator1") {
			state.modulator1_to_modulator0 = intOf(value) != 0;
		}
	}
}

void readLfo(const Value& lfo, SoundState& state, size_t l, const engine::CodecContext& context) {
	state.lfo_sync_type[l] = engine::SyncType::EVEN;
	state.lfo_sync_level[l] = engine::SyncLevel::NONE;
	if (!lfo.isMap()) {
		return;
	}
	for (const auto& [key, value] : lfo.entries()) {
		if (key == "type") {
			state.lfo_wave_type[l] = parse_lfo_type(textOf(value));
		}
		else if (key == "syncType") {
			state.lfo_sync_type[l] = static_cast<engine::SyncType>(intOf(value));
		}
		else if (key == "syncLevel") {
			state.lfo_sync_level[l] = static_cast<engine::SyncLevel>(
			    engine::sync_level_from_file(intOf(value), context.input_tick_magnitude));
		}
	}
}

/// The pre-mid-2016 oscillator elements: `oscillatorA` / `oscillatorB`.
void readOldOscillator(const Value& osc, SoundState& state, size_t s) {
	if (!osc.isMap()) {
		return;
	}
	for (const auto& [key, value] : osc.entries()) {
		if (key == "type") {
			state.sources[s].osc_type = parse_osc_type(textOf(value));
		}
		else if (s == 0 && key == "note") {
			const int32_t offset = std::clamp(intOf(value), 1, 127) - 60;
			for (size_t i = 0; i < 2; i++) {
				state.sources[i].transpose = static_cast<int16_t>(state.sources[i].transpose + offset);
				state.modulator_transpose[i] = static_cast<int16_t>(state.modulator_transpose[i] + offset);
			}
		}
		else if (s == 1 && key == "transpose") {
			state.sources[1].transpose = static_cast<int16_t>(state.sources[1].transpose + intOf(value));
		}
		else if (s == 1 && key == "cents") {
			state.sources[1].cents = static_cast<int8_t>(intOf(value));
		}
	}
}

Value dumpRange(const RangeState& range) {
	auto map = Value::makeMap();
	map.set("topNote", number(range.top_note));
	map.set("filePath", text(range.file_path));
	map.set("startPos", Value::makeNumber(static_cast<int64_t>(range.start_pos)));
	map.set("endPos", Value::makeNumber(static_cast<int64_t>(range.end_pos)));
	map.set("loopStartPos", Value::makeNumber(range.loop_start_pos));
	map.set("loopEndPos", Value::makeNumber(range.loop_end_pos));
	map.set("transpose", number(range.transpose));
	map.set("cents", number(range.cents));
	map.set("startMsec", number(range.start_msec));
	map.set("endMsec", number(range.end_msec));
	return map;
}

} // namespace

namespace {

/// Read @p element into @p state; @p fileRead says it is a file's read, whose own normalisations it applies.
std::expected<void, engine::StateError> decodeImpl(const Value& element, SoundState& state,
                                                   const engine::CodecContext& context, bool fileRead) {
	if (!element.isMap()) {
		return std::unexpected(engine::StateError::NOT_A_MAP);
	}
	// What reading a sound always starts with, whatever the file holds.
	state.modulator_transpose[1] = 0;
	state.osc_retrigger_phase = {};
	state.modulator_retrigger_phase = {};
	std::array<SourceReading, 2> reading{SourceReading{.refuseDuplicates = fileRead},
	                                     SourceReading{.refuseDuplicates = fileRead}};

	for (const auto& [key, value] : element.entries()) {
		if (key == "osc1" || key == "osc2") {
			if (auto read = readSource(value, state, key == "osc1" ? 0 : 1, reading[key == "osc1" ? 0 : 1]); !read) {
				return read;
			}
		}
		else if (key == "mode") {
			if (state.synth_mode != SynthMode::RINGMOD) {
				state.synth_mode = parse_synth_mode(textOf(value));
			}
		}
		else if (key == "ringMod") {
			if (intOf(value) == 1) {
				state.synth_mode = SynthMode::RINGMOD;
			}
		}
		else if (key == "oscillatorA" || key == "oscillatorB") {
			readOldOscillator(value, state, key == "oscillatorA" ? 0 : 1);
		}
		else if (key == "modulator1" || key == "modulator2") {
			readModulator(value, state, key == "modulator1" ? 0 : 1);
		}
		else if (key == "transpose") {
			state.transpose = static_cast<int16_t>(intOf(value));
		}
		else if (key == "cents") {
			state.cents = static_cast<int8_t>(intOf(value));
		}
		else if (key == "oscillatorReset") {
			if (intOf(value) == 0) {
				state.osc_retrigger_phase = {0xFFFFFFFF, 0xFFFFFFFF};
				state.modulator_retrigger_phase = {0xFFFFFFFF, 0xFFFFFFFF};
			}
		}
		else if (key == "unison") {
			if (value.isMap()) {
				for (const auto& [unisonKey, unisonValue] : value.entries()) {
					if (unisonKey == "num") {
						state.num_unison = static_cast<uint8_t>(std::clamp(intOf(unisonValue), 0, kMaxUnisonVoices));
					}
					else if (unisonKey == "detune") {
						state.unison_detune = static_cast<int8_t>(std::clamp(intOf(unisonValue), 0, kMaxUnisonDetune));
					}
					else if (unisonKey == "spread") {
						state.unison_stereo_spread =
						    static_cast<uint8_t>(std::clamp(intOf(unisonValue), 0, kMaxUnisonSpread));
					}
				}
			}
		}
		else if (key == "fileName") { // early-2016: the sound's one sample
			firstRange(state.sources[0]).file_path = std::string(textOf(value));
			state.sources[0].osc_type = OscType::SAMPLE;
		}
		else if (key == "continuous") {
			state.sources[0].repeat_mode = repeatModeOf(value);
		}
		else if (key == "reversed") {
			state.sources[0].reversed = intOf(value) != 0;
		}
		else if (key == "zone") {
			readZone(value, firstRange(state.sources[0]), true);
		}
		else if (key.size() == 4 && key.starts_with("lfo") && key[3] >= '1' && key[3] <= '4') {
			readLfo(value, state, static_cast<size_t>(key[3] - '1'), context);
		}
		else if (key == "sideChainSend") {
			state.side_chain_send_level = intOf(value);
		}
		else if (key == "polyphonic") {
			state.polyphonic = parse_polyphony_mode(textOf(value));
		}
		else if (key == "maxVoices") {
			state.max_voice_count = static_cast<uint8_t>(intOf(value));
		}
		else if (key == "voicePriority") {
			state.voice_priority = static_cast<engine::VoicePriority>(intOf(value));
		}
		else if (key == "midiOutput") {
			if (value.isMap()) {
				for (const auto& [midiKey, midiValue] : value.entries()) {
					if (midiKey == "channel") {
						state.output_midi_channel = static_cast<uint8_t>(intOf(midiValue));
					}
					else if (midiKey == "noteForDrum") {
						state.output_midi_note_for_drum = static_cast<uint8_t>(intOf(midiValue));
					}
				}
			}
		}
	}
	// What finishing a read does to each source: FM plays sines, ring modulation takes nothing past a wavetable, and a
	// sample source's own transpose and cents move into each of its ranges.
	for (SourceState& src : state.sources) {
		if (state.synth_mode == SynthMode::FM) {
			src.osc_type = OscType::SINE;
		}
		else if (state.synth_mode == SynthMode::RINGMOD && fileRead) {
			src.osc_type = std::min(src.osc_type, OscType::WAVETABLE);
		}
		if (state.synth_mode != SynthMode::FM && src.osc_type == OscType::SAMPLE) {
			for (RangeState& range : src.ranges) {
				range.transpose = static_cast<int16_t>(range.transpose + src.transpose);
				range.cents = static_cast<int8_t>(range.cents + src.cents);
			}
			src.transpose = 0;
			src.cents = 0;
		}
	}
	// A range holds a zone and tuning only in a sample source.
	for (SourceState& src : state.sources) {
		if (src.osc_type != OscType::SAMPLE) {
			for (RangeState& range : src.ranges) {
				range = RangeState{.top_note = range.top_note, .file_path = std::move(range.file_path)};
			}
		}
	}
	return {};
}

/// The entries of an oscillator's element that hold its ranges.
constexpr std::array<std::string_view, 4> kRangeKeys{"fileName", "zone", "sampleRanges", "wavetableRanges"};

/// Whether oscillator elements @p a and @p b hold the same ranges.
bool sameRanges(const Value* a, const Value* b) {
	for (std::string_view key : kRangeKeys) {
		const Value* inA = a != nullptr ? a->find(key) : nullptr;
		const Value* inB = b != nullptr ? b->find(key) : nullptr;
		if ((inA == nullptr) != (inB == nullptr)
		    || (inA != nullptr && !serialization::first_difference(*inA, *inB).empty())) {
			return false;
		}
	}
	return true;
}

/// The part of @p state the entry @p key of a sound element writes, copied from @p from.
void keepEntry(std::string_view key, SoundState& state, const SoundState& from) {
	if (key == "polyphonic") {
		state.polyphonic = from.polyphonic;
	}
	else if (key == "voicePriority") {
		state.voice_priority = from.voice_priority;
	}
	else if (key == "sideChainSend") {
		state.side_chain_send_level = from.side_chain_send_level;
	}
	else if (key == "mode") {
		state.synth_mode = from.synth_mode;
	}
	else if (key == "transpose") {
		state.transpose = from.transpose;
	}
	else if (key == "cents") {
		state.cents = from.cents;
	}
	else if (key == "maxVoices") {
		state.max_voice_count = from.max_voice_count;
	}
	else if (key == "osc1" || key == "osc2") {
		const size_t s = key == "osc1" ? 0 : 1;
		state.sources[s] = from.sources[s];
		state.osc_retrigger_phase[s] = from.osc_retrigger_phase[s];
		if (s == 1) {
			state.oscillator_sync = from.oscillator_sync;
		}
	}
	else if (key.starts_with("lfo")) {
		const size_t l = static_cast<size_t>(key[3] - '1');
		state.lfo_wave_type[l] = from.lfo_wave_type[l];
		state.lfo_sync_type[l] = from.lfo_sync_type[l];
		state.lfo_sync_level[l] = from.lfo_sync_level[l];
	}
	else if (key == "modulator1" || key == "modulator2") {
		const size_t m = key == "modulator1" ? 0 : 1;
		state.modulator_transpose[m] = from.modulator_transpose[m];
		state.modulator_cents[m] = from.modulator_cents[m];
		state.modulator_retrigger_phase[m] = from.modulator_retrigger_phase[m];
		if (m == 1) {
			state.modulator1_to_modulator0 = from.modulator1_to_modulator0;
		}
	}
	else if (key == "unison") {
		state.num_unison = from.num_unison;
		state.unison_detune = from.unison_detune;
		state.unison_stereo_spread = from.unison_stereo_spread;
	}
	else if (key == "midiOutput") {
		state.output_midi_channel = from.output_midi_channel;
		state.output_midi_note_for_drum = from.output_midi_note_for_drum;
	}
}

} // namespace

std::expected<void, engine::StateError> decode(const Value& element, SoundState& state,
                                               const engine::CodecContext& context) {
	return decodeImpl(element, state, context, /*fileRead=*/true);
}

void keep_unwritten(SoundState& state, const SoundState& from) {
	state.mod_controllable = from.mod_controllable; // the effects', not the instrument's
	const bool fm = state.synth_mode == SynthMode::FM;
	if (!fm) {
		state.modulator_transpose = from.modulator_transpose;
		state.modulator_cents = from.modulator_cents;
		state.modulator_retrigger_phase = from.modulator_retrigger_phase;
		state.modulator1_to_modulator0 = from.modulator1_to_modulator0;
	}
	for (size_t s = 0; s < state.sources.size(); s++) {
		SourceState& src = state.sources[s];
		const SourceState& old = from.sources[s];
		const auto keepPlayback = [&] {
			src.repeat_mode = old.repeat_mode;
			src.reversed = old.reversed;
			src.pitch_and_speed_independent = old.pitch_and_speed_independent;
			src.time_stretch_amount = old.time_stretch_amount;
			src.interpolation_mode = old.interpolation_mode;
		};
		if (fm) {
			src.osc_type = old.osc_type;
			keepPlayback();
			src.ranges = old.ranges;
			src.dx_patch = old.dx_patch;
			continue;
		}
		if (src.osc_type == OscType::SAMPLE) {
			// A sample source's own tuning: the file holds its ranges', and reading folds the source's into them.
			src.transpose = old.transpose;
			src.cents = old.cents;
			state.osc_retrigger_phase[s] = from.osc_retrigger_phase[s];
			if (s == 1) {
				state.oscillator_sync = from.oscillator_sync;
			}
			src.dx_patch = old.dx_patch;
			continue;
		}
		keepPlayback();
		if (src.osc_type != OscType::WAVETABLE) {
			src.ranges = old.ranges;
		}
		if (src.osc_type != OscType::DX7) {
			src.dx_patch = old.dx_patch;
		}
	}
}

std::expected<SoundState, engine::StateError> decode_onto(const Value& element, const SoundState& current,
                                                          const engine::CodecContext& context) {
	SoundState decoded = new_synth_state();
	if (auto read = decodeImpl(element, decoded, context, /*fileRead=*/false); !read) {
		return std::unexpected(read.error());
	}
	keep_unwritten(decoded, current);
	// An entry the element holds as the current state writes it is the current state's, whatever a file read of it
	// would make: a legacy zone, a sync level the tick magnitude cannot write. Only a changed entry is read.
	const serialization::Value mine = encode(current, context);
	for (std::string_view key : kStateKeys) {
		const serialization::Value* held = element.find(key);
		const serialization::Value* written = mine.find(key);
		const bool unchanged =
		    (held == nullptr && written == nullptr)
		    || (held != nullptr && written != nullptr && serialization::first_difference(*held, *written).empty());
		if (unchanged) {
			keepEntry(key, decoded, current);
		}
	}
	// A changed oscillator whose ranges are the current ones as its new type writes them carries them over whole,
	// unless they became the other kind (sample to wavetable, or back), which keeps only their top notes.
	for (size_t s = 0; s < decoded.sources.size(); s++) {
		const OscType type = decoded.sources[s].osc_type;
		const OscType was = current.sources[s].osc_type;
		const std::string_view key = s == 0 ? "osc1" : "osc2";
		const bool ranged = type == OscType::SAMPLE || type == OscType::WAVETABLE;
		const bool otherKind = (was == OscType::SAMPLE || was == OscType::WAVETABLE) && was != type;
		if (decoded.synth_mode == SynthMode::FM || !ranged || otherKind) {
			continue;
		}
		SoundState carried = current;
		carried.synth_mode = decoded.synth_mode;
		carried.sources[s].osc_type = type;
		if (sameRanges(element.find(key), encode(carried, context).find(key))) {
			decoded.sources[s].ranges = current.sources[s].ranges;
		}
	}
	return decoded;
}

SoundState new_synth_state() {
	SoundState state{
	    .max_voice_count = 8,
	    .output_midi_channel = 255,
	    .output_midi_note_for_drum = 255,
	    .polyphonic = PolyphonyMode::POLY,
	    .num_unison = 1,
	    .unison_detune = 8,
	    .modulator_transpose = {0, -12},
	    .osc_retrigger_phase = {0xFFFFFFFF, 0xFFFFFFFF},
	    .lfo_wave_type = {LFOType::TRIANGLE, LFOType::TRIANGLE, LFOType::TRIANGLE, LFOType::TRIANGLE},
	};
	for (SourceState& src : state.sources) {
		src.osc_type = OscType::SQUARE;
		src.interpolation_mode = engine::InterpolationMode::SMOOTH;
	}
	return state;
}

Value dump_state(const SoundState& state) {
	Value map = encode(state, {.input_tick_magnitude = 1});
	map.set("voicePriorityIndex", number(std::to_underlying(state.voice_priority)));
	map.set("oscillatorSync", number(state.oscillator_sync ? 1 : 0));
	map.set("syncTypes", text(std::to_string(std::to_underlying(state.lfo_sync_type[0])) + ","
	                          + std::to_string(std::to_underlying(state.lfo_sync_type[1])) + ","
	                          + std::to_string(std::to_underlying(state.lfo_sync_type[2])) + ","
	                          + std::to_string(std::to_underlying(state.lfo_sync_type[3]))));
	map.set("modulatorTranspose",
	        text(std::to_string(state.modulator_transpose[0]) + "," + std::to_string(state.modulator_transpose[1])));
	map.set("modulatorCents",
	        text(std::to_string(state.modulator_cents[0]) + "," + std::to_string(state.modulator_cents[1])));
	map.set("modulatorPhases", text(std::to_string(state.modulator_retrigger_phase[0]) + ","
	                                + std::to_string(state.modulator_retrigger_phase[1])));
	map.set("toModulator1", number(state.modulator1_to_modulator0 ? 1 : 0));
	for (size_t s = 0; s < state.sources.size(); s++) {
		const SourceState& src = state.sources[s];
		auto source = Value::makeMap();
		source.set("oscType", text(to_string(src.osc_type)));
		source.set("transpose", number(src.transpose));
		source.set("cents", number(src.cents));
		source.set("repeatMode", number(std::to_underlying(src.repeat_mode)));
		source.set("timeStretchAmount", number(src.time_stretch_amount));
		source.set("interpolation", number(std::to_underlying(src.interpolation_mode)));
		source.set("pitchAndSpeedIndependent", number(src.pitch_and_speed_independent ? 1 : 0));
		source.set("reversed", number(src.reversed ? 1 : 0));
		if (src.dx_patch) {
			source.set("dx7patch", text(hex(src.dx_patch->params)));
			source.set("dx7engineMode", number(src.dx_patch->engine_mode));
			source.set("dx7randomDetune", number(src.dx_patch->random_detune));
		}
		auto ranges = Value::makeArray();
		for (const RangeState& range : src.ranges) {
			ranges.append(dumpRange(range));
		}
		source.set("ranges", std::move(ranges));
		map.set("source" + std::to_string(s + 1), std::move(source));
	}
	return map;
}

} // namespace deluge::instrument
