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

#include "engines/effects/codec.h"

#include "serialization/tree.h"

#include <algorithm>
#include <string>
#include <utility>

namespace deluge::effects {

namespace {

using serialization::Value;

/// A number as a file holds it: a 32-bit signed integer.
Value number(int64_t value) {
	return Value::makeNumber(static_cast<int32_t>(value));
}

Value text(std::string_view value) {
	return Value::makeScalar(std::string(value));
}

Value flag(bool on) {
	return number(on ? 1 : 0);
}

int32_t int_or_zero(const Value& value) {
	return serialization::int_of(&value).value_or(0);
}

std::string_view text_or_empty(const Value& value) {
	const std::string* text = serialization::text_of(&value);
	return text != nullptr ? std::string_view(*text) : std::string_view{};
}

/// A 0/1 file flag, as the runtime reads it: anything not 0 is on.
bool flag_of(const Value& value) {
	return std::clamp(int_or_zero(value), 0, 1) != 0;
}

int32_t sync_level_in_file(engine::SyncLevel level, const engine::CodecContext& context) {
	return engine::sync_level_to_file(engine::index_of(level), context.input_tick_magnitude);
}

engine::SyncLevel sync_level_read(const Value& value, const engine::CodecContext& context) {
	return engine::sync_level_from_index(
	    engine::sync_level_from_file(int_or_zero(value), context.input_tick_magnitude));
}

void read_delay(const Value& delay, DelayState& state, const engine::CodecContext& context) {
	state.sync_type = engine::SyncType::EVEN;
	state.sync_level = engine::SyncLevel::NONE;
	if (!delay.isMap()) {
		return;
	}
	for (const auto& [key, value] : delay.entries()) {
		if (key == "pingPong") {
			state.ping_pong = flag_of(value);
		}
		else if (key == "analog") {
			state.analog = flag_of(value);
		}
		else if (key == "syncType") {
			state.sync_type = static_cast<engine::SyncType>(int_or_zero(value));
		}
		else if (key == "syncLevel") {
			state.sync_level = sync_level_read(value, context);
		}
	}
}

void read_sidechain(const Value& sidechain, SidechainState& state, const engine::CodecContext& context) {
	state.sync_type = engine::SyncType::EVEN;
	state.sync_level = engine::SyncLevel::NONE;
	if (!sidechain.isMap()) {
		return;
	}
	for (const auto& [key, value] : sidechain.entries()) {
		if (key == "attack") {
			state.attack = int_or_zero(value);
		}
		else if (key == "release") {
			state.release = int_or_zero(value);
		}
		else if (key == "syncType") {
			state.sync_type = static_cast<engine::SyncType>(int_or_zero(value));
		}
		else if (key == "syncLevel") {
			state.sync_level = sync_level_read(value, context);
		}
	}
}

void read_compressor(const Value& compressor, CompressorState& state) {
	if (!compressor.isMap()) {
		return;
	}
	for (const auto& [key, value] : compressor.entries()) {
		if (key == "attack") {
			state.attack = int_or_zero(value);
		}
		else if (key == "release") {
			state.release = int_or_zero(value);
		}
		else if (key == "thresh") {
			state.threshold = int_or_zero(value);
		}
		else if (key == "ratio") {
			state.ratio = int_or_zero(value);
		}
		else if (key == "compHPF") {
			state.sidechain_hpf = int_or_zero(value);
		}
		else if (key == "compBlend") {
			state.blend = int_or_zero(value);
		}
	}
}

void read_stutter(const Value& stutter, StutterState& state) {
	state = StutterState{};
	if (!stutter.isMap()) {
		return;
	}
	for (const auto& [key, value] : stutter.entries()) {
		if (key == "quantized") {
			state.quantized = flag_of(value);
		}
		else if (key == "reverse") {
			state.reversed = flag_of(value);
		}
		else if (key == "pingPong") {
			state.ping_pong = flag_of(value);
		}
		else if (key == "useSongStutter") {
			state.use_song_stutter = flag_of(value);
		}
	}
}

} // namespace

EffectPieces encode_pieces(const EffectStates& states, const engine::CodecContext& context,
                           std::optional<int32_t> compressor_threshold) {
	EffectPieces pieces{.attributes = Value::makeMap(), .delay = Value::makeMap(), .tags = Value::makeMap()};
	pieces.attributes.set("modFXType", text(to_string(states.mod_fx.type)));
	pieces.attributes.set("lpfMode", text(to_string(states.filter.lpf_mode)));
	pieces.attributes.set("hpfMode", text(to_string(states.filter.hpf_mode)));
	pieces.attributes.set("filterRoute", text(to_string(states.filter.route)));
	if (states.saturation.clipping_amount != 0) {
		pieces.attributes.set("clippingAmount", number(states.saturation.clipping_amount));
	}

	auto delay = Value::makeMap();
	delay.set("pingPong", flag(states.delay.ping_pong));
	delay.set("analog", flag(states.delay.analog));
	delay.set("syncLevel", number(sync_level_in_file(states.delay.sync_level, context)));
	delay.set("syncType", number(std::to_underlying(states.delay.sync_type)));
	pieces.delay.set("delay", std::move(delay));

	auto sidechain = Value::makeMap();
	sidechain.set("attack", number(states.sidechain.attack));
	sidechain.set("release", number(states.sidechain.release));
	sidechain.set("syncLevel", number(sync_level_in_file(states.sidechain.sync_level, context)));
	sidechain.set("syncType", number(std::to_underlying(states.sidechain.sync_type)));
	pieces.tags.set("sidechain", std::move(sidechain));

	auto compressor = Value::makeMap();
	compressor.set("attack", number(states.compressor.attack));
	compressor.set("release", number(states.compressor.release));
	compressor.set("thresh", number(compressor_threshold.value_or(states.compressor.threshold)));
	compressor.set("ratio", number(states.compressor.ratio));
	compressor.set("compHPF", number(states.compressor.sidechain_hpf));
	compressor.set("compBlend", number(states.compressor.blend));
	pieces.tags.set("audioCompressor", std::move(compressor));

	auto stutter = Value::makeMap();
	stutter.set("quantized", flag(states.stutter.quantized));
	stutter.set("reverse", flag(states.stutter.reversed));
	stutter.set("pingPong", flag(states.stutter.ping_pong));
	if (!states.stutter.use_song_stutter) {
		stutter.set("useSongStutter", flag(false)); // absent, as in older files, it is true
	}
	pieces.tags.set("stutter", std::move(stutter));
	return pieces;
}

Value encode(const EffectStates& states, const engine::CodecContext& context) {
	EffectPieces pieces = encode_pieces(states, context);
	Value out = Value::makeMap();
	for (const Value* piece : {&pieces.attributes, &pieces.delay, &pieces.tags}) {
		for (const auto& [key, value] : piece->entries()) {
			out.set(key, value);
		}
	}
	return out;
}

void decode(const Value& element, EffectStates& states, const engine::CodecContext& context) {
	if (!element.isMap()) {
		return;
	}
	for (const auto& [key, value] : element.entries()) {
		if (key == "modFXType") {
			states.mod_fx.type = parse_mod_fx_type(text_or_empty(value));
		}
		else if (key == "lpfMode") {
			states.filter.lpf_mode = parse_filter_mode(text_or_empty(value));
		}
		else if (key == "hpfMode") {
			states.filter.hpf_mode = parse_filter_mode(text_or_empty(value));
		}
		else if (key == "filterRoute") {
			states.filter.route = parse_filter_route(text_or_empty(value));
		}
		else if (key == "clippingAmount") {
			states.saturation.clipping_amount = static_cast<uint8_t>(int_or_zero(value));
		}
		else if (key == "delay") {
			read_delay(value, states.delay, context);
		}
		else if (key == "sidechain" || key == "compressor") {
			read_sidechain(value, states.sidechain, context);
		}
		else if (key == "audioCompressor") {
			read_compressor(value, states.compressor);
		}
		else if (key == "stutter") {
			read_stutter(value, states.stutter);
		}
	}
}

bool is_effect_key(std::string_view key) {
	return key == "compressor" || std::ranges::find(kEffectKeys, key) != kEffectKeys.end();
}

void keep_effect_entry(std::string_view key, EffectStates& states, const EffectStates& from) {
	if (key == "modFXType") {
		states.mod_fx = from.mod_fx;
	}
	else if (key == "lpfMode") {
		states.filter.lpf_mode = from.filter.lpf_mode;
	}
	else if (key == "hpfMode") {
		states.filter.hpf_mode = from.filter.hpf_mode;
	}
	else if (key == "filterRoute") {
		states.filter.route = from.filter.route;
	}
	else if (key == "clippingAmount") {
		states.saturation = from.saturation;
	}
	else if (key == "delay") {
		states.delay = from.delay;
	}
	else if (key == "sidechain" || key == "compressor") {
		states.sidechain = from.sidechain;
	}
	else if (key == "audioCompressor") {
		states.compressor = from.compressor;
	}
	else if (key == "stutter") {
		states.stutter = from.stutter;
	}
}

} // namespace deluge::effects
