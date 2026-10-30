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

#include "deluge-project/song_codec.h"

#include "serialization/tree.h"

#include <algorithm>
#include <bitset>

namespace deluge::project {

using serialization::int_of;
using serialization::Value;

namespace {

constexpr float kQ31One = 2147483648.0f;

/// A [0, 1] fraction as the Q31 the file holds, saturating at the largest Q31.
int32_t q31_of(float fraction) {
	uint32_t q = static_cast<uint32_t>(fraction * kQ31One);
	return static_cast<int32_t>(std::min(q, uint32_t{2147483647}));
}

float fraction_of(int32_t q31) {
	return static_cast<float>(q31) / kQ31One;
}

} // namespace

Value encode(const Transport& transport) {
	auto map = Value::makeMap();
	map.set("timePerTimerTick", Value::makeNumber(static_cast<int32_t>(transport.samples_per_timer_tick_big >> 32)));
	map.set("timerTickFraction",
	        Value::makeNumber(static_cast<int32_t>(static_cast<uint32_t>(transport.samples_per_timer_tick_big))));
	map.set("inputTickMagnitude", Value::makeNumber(transport.inside_world_tick_magnitude
	                                                + transport.inside_world_tick_magnitude_offset_from_bpm));
	return map;
}

std::expected<void, LoadError> decode(const Value& song, Transport& transport) {
	if (!song.isMap()) {
		return std::unexpected(LoadError::NOT_A_MAP);
	}
	if (auto high = int_of(song.find("timePerTimerTick"))) {
		transport.samples_per_timer_tick_big =
		    (transport.samples_per_timer_tick_big & uint64_t{0xFFFFFFFF}) | (static_cast<uint64_t>(*high) << 32);
	}
	if (auto low = int_of(song.find("timerTickFraction"))) {
		transport.samples_per_timer_tick_big = (transport.samples_per_timer_tick_big & (uint64_t{0xFFFFFFFF} << 32))
		                                       | static_cast<uint64_t>(static_cast<uint32_t>(*low));
	}
	if (auto magnitude = int_of(song.find("inputTickMagnitude"))) {
		transport.inside_world_tick_magnitude = *magnitude;
	}
	return {};
}

Value encode(const Groove& groove, int32_t input_tick_magnitude) {
	auto map = Value::makeMap();
	map.set("swingAmount", Value::makeNumber(groove.swing_amount));
	map.set("swingInterval",
	        Value::makeNumber(sync_level_to_file(plugin::index_of(groove.swing_interval), input_tick_magnitude)));
	return map;
}

std::expected<void, LoadError> decode(const Value& song, Groove& groove, const DecodeContext& context) {
	if (!song.isMap()) {
		return std::unexpected(LoadError::NOT_A_MAP);
	}
	if (auto amount = int_of(song.find("swingAmount"))) {
		// Narrowed to the field first, then clamped, as the song reader always has.
		groove.swing_amount = std::clamp(static_cast<int8_t>(*amount), int8_t{-49}, int8_t{49});
	}
	if (auto interval = int_of(song.find("swingInterval"))) {
		const int32_t level = sync_level_from_file(static_cast<uint8_t>(*interval), // wraps, as the reader always has
		                                           context.input_tick_magnitude);
		groove.swing_interval = plugin::sync_level_from_index(static_cast<uint8_t>(std::min(level, int32_t{9})));
	}
	return {};
}

Value encode(const ScaleSettings& scale) {
	auto map = Value::makeMap();
	map.set("rootNote", Value::makeNumber(scale.key.rootNote));
	auto modeNotes = Value::makeArray();
	for (int32_t i = 0; i < scale.key.modeNotes.count(); i++) {
		auto box = Value::makeMap();
		box.set("modeNote", Value::makeNumber(scale.key.modeNotes[i]));
		modeNotes.append(std::move(box));
	}
	map.set("modeNotes", std::move(modeNotes));
	auto scales = Value::makeMap();
	scales.set("userScale", Value::makeNumber(scale.user_scale_notes.toBits()));
	scales.set("disabledPresetScales",
	           Value::makeNumber(static_cast<int64_t>(scale.disabled_preset_scales.to_ulong())));
	map.set("scales", std::move(scales));
	return map;
}

std::expected<void, LoadError> decode(const Value& song, ScaleSettings& scale) {
	if (!song.isMap()) {
		return std::unexpected(LoadError::NOT_A_MAP);
	}
	if (auto root = int_of(song.find("rootNote"))) {
		scale.key.rootNote = static_cast<int16_t>(*root);
	}
	if (const Value* modeNotes = song.find("modeNotes")) {
		scale.key.modeNotes.clear();
		for (const Value* note : serialization::elements_named(*modeNotes, "modeNote")) {
			scale.key.modeNotes.addUntrusted(static_cast<uint8_t>(int_of(note).value_or(0)));
		}
	}
	if (const Value* scales = song.find("scales"); scales != nullptr && scales->isMap()) {
		if (auto bits = int_of(scales->find("userScale"))) {
			scale.user_scale_notes = music::NoteSet(static_cast<uint16_t>(*bits));
		}
		if (auto bits = int_of(scales->find("disabledPresetScales"))) {
			scale.disabled_preset_scales =
			    std::bitset<music::NUM_PRESET_SCALES>(static_cast<unsigned long long>(*bits));
		}
	}
	return {};
}

Value encode(const ReverbSettings& reverb, int32_t input_tick_magnitude) {
	auto map = Value::makeMap();
	map.set("roomSize", Value::makeNumber(q31_of(reverb.room_size)));
	map.set("dampening", Value::makeNumber(q31_of(reverb.damping)));
	map.set("width", Value::makeNumber(q31_of(reverb.width)));
	map.set("hpf", Value::makeNumber(q31_of(reverb.hpf)));
	map.set("lpf", Value::makeNumber(q31_of(reverb.lpf)));
	map.set("pan", Value::makeNumber(reverb.pan));
	auto compressor = Value::makeMap();
	compressor.set("attack", Value::makeNumber(reverb.sidechain_attack));
	compressor.set("release", Value::makeNumber(reverb.sidechain_release));
	compressor.set("volume", Value::makeNumber(reverb.sidechain_volume));
	compressor.set("shape", Value::makeNumber(reverb.sidechain_shape));
	compressor.set("syncLevel", Value::makeNumber(
	                                sync_level_to_file(plugin::index_of(reverb.sidechain_sync), input_tick_magnitude)));
	map.set("compressor", std::move(compressor));
	return map;
}

std::expected<void, LoadError> decode(const Value& reverb, ReverbSettings& settings, const DecodeContext& context) {
	if (!reverb.isMap()) {
		return std::unexpected(LoadError::NOT_A_MAP);
	}
	if (auto q = int_of(reverb.find("roomSize"))) {
		settings.room_size = fraction_of(*q);
	}
	if (auto q = int_of(reverb.find("dampening"))) {
		settings.damping = fraction_of(*q);
	}
	if (auto q = int_of(reverb.find("width"))) {
		// Firmware 2.1.0-beta1 and its alphas saved full width as INT32_MIN.
		settings.width = fraction_of(*q == INT32_MIN ? INT32_MAX : *q);
	}
	if (auto q = int_of(reverb.find("hpf"))) {
		settings.hpf = fraction_of(*q);
	}
	if (auto q = int_of(reverb.find("lpf"))) {
		settings.lpf = fraction_of(*q);
	}
	if (auto pan = int_of(reverb.find("pan"))) {
		settings.pan = *pan;
	}
	if (const Value* compressor = reverb.find("compressor"); compressor != nullptr && compressor->isMap()) {
		if (auto v = int_of(compressor->find("attack"))) {
			settings.sidechain_attack = *v;
		}
		if (auto v = int_of(compressor->find("release"))) {
			settings.sidechain_release = *v;
		}
		if (auto v = int_of(compressor->find("volume"))) {
			settings.sidechain_volume = *v;
		}
		if (auto v = int_of(compressor->find("shape"))) {
			settings.sidechain_shape = *v;
		}
		if (auto v = int_of(compressor->find("syncLevel"))) {
			const int32_t level = sync_level_from_file(*v, context.input_tick_magnitude);
			settings.sidechain_sync = plugin::sync_level_from_index(static_cast<uint8_t>(std::min(level, int32_t{9})));
		}
	}
	return {};
}

} // namespace deluge::project
