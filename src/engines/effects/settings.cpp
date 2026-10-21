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

#include "engines/effects/settings.h"

#include "engine-api/settings.h"
#include "engines/effects/words.h"
#include "serialization/tree.h"

#include <string>
#include <utility>

namespace deluge::effects {

namespace {

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

Answer sync_answer(std::string_view rest, engine::SyncType type, engine::SyncLevel level) {
	if (rest == "syncLevel") {
		return number(engine::index_of(level));
	}
	if (rest == "syncType") {
		return number(std::to_underlying(type));
	}
	return std::nullopt;
}

} // namespace

bool check_effect_setting(std::string_view path, const serialization::Value& value) {
	return engine::check_setting(kEffectSettings, path, value);
}

std::optional<serialization::Value> effect_setting_value(const EffectStates& states, std::string_view path) {
	if (engine::find_setting(kEffectSettings, path) == nullptr) {
		return std::nullopt;
	}
	const size_t slash = path.find('/');
	const std::string_view entry = path.substr(0, slash);
	const std::string_view rest = slash == std::string_view::npos ? std::string_view{} : path.substr(slash + 1);
	if (entry == "modFXType") {
		return word(to_string(states.mod_fx.type));
	}
	if (entry == "lpfMode") {
		return word(to_string(states.filter.lpf_mode));
	}
	if (entry == "hpfMode") {
		return word(to_string(states.filter.hpf_mode));
	}
	if (entry == "filterRoute") {
		return word(to_string(states.filter.route));
	}
	if (entry == "clippingAmount") {
		return number(states.saturation.clipping_amount);
	}
	if (entry == "delay") {
		if (rest == "pingPong") {
			return flag(states.delay.ping_pong);
		}
		if (rest == "type") {
			return word(to_string(states.delay.type));
		}
		return sync_answer(rest, states.delay.sync_type, states.delay.sync_level);
	}
	if (entry == "sidechain") {
		if (rest == "attack") {
			return number(states.sidechain.attack);
		}
		if (rest == "release") {
			return number(states.sidechain.release);
		}
		return sync_answer(rest, states.sidechain.sync_type, states.sidechain.sync_level);
	}
	if (entry == "audioCompressor") {
		const CompressorState& c = states.compressor;
		return rest == "attack"    ? number(c.attack)
		       : rest == "release" ? number(c.release)
		       : rest == "thresh"  ? number(c.threshold)
		       : rest == "ratio"   ? number(c.ratio)
		       : rest == "compHPF" ? number(c.sidechain_hpf)
		                           : number(c.blend);
	}
	// stutter
	return rest == "quantized"        ? flag(states.stutter.quantized)
	       : rest == "reverse"        ? flag(states.stutter.reversed)
	       : rest == "useSongStutter" ? flag(states.stutter.use_song_stutter)
	                                  : flag(states.stutter.ping_pong);
}

bool write_effect_setting(EffectStates& states, std::string_view path, const serialization::Value& value) {
	if (engine::find_setting(kEffectSettings, path) == nullptr) {
		return false;
	}
	const size_t slash = path.find('/');
	const std::string_view entry = path.substr(0, slash);
	const std::string_view rest = slash == std::string_view::npos ? std::string_view{} : path.substr(slash + 1);
	const int32_t n = serialization::int_of(&value).value_or(0);
	const auto sync = [&](engine::SyncType& type, engine::SyncLevel& level) {
		if (rest == "syncLevel") {
			level = engine::sync_level_from_index(n);
		}
		else if (rest == "syncType") {
			type = static_cast<engine::SyncType>(n);
		}
	};
	if (entry == "modFXType") {
		states.mod_fx.type = parse_mod_fx_type(value.str());
	}
	else if (entry == "lpfMode") {
		states.filter.lpf_mode = parse_filter_mode(value.str());
	}
	else if (entry == "hpfMode") {
		states.filter.hpf_mode = parse_filter_mode(value.str());
	}
	else if (entry == "filterRoute") {
		states.filter.route = parse_filter_route(value.str());
	}
	else if (entry == "clippingAmount") {
		states.saturation.clipping_amount = static_cast<uint8_t>(n);
	}
	else if (entry == "delay") {
		if (rest == "pingPong") {
			states.delay.ping_pong = n != 0;
		}
		else if (rest == "type") {
			states.delay.type = parse_delay_type(value.str());
		}
		else {
			sync(states.delay.sync_type, states.delay.sync_level);
		}
	}
	else if (entry == "sidechain") {
		if (rest == "attack") {
			states.sidechain.attack = n;
		}
		else if (rest == "release") {
			states.sidechain.release = n;
		}
		else {
			sync(states.sidechain.sync_type, states.sidechain.sync_level);
		}
	}
	else if (entry == "audioCompressor") {
		CompressorState& c = states.compressor;
		(rest == "attack"    ? c.attack
		 : rest == "release" ? c.release
		 : rest == "thresh"  ? c.threshold
		 : rest == "ratio"   ? c.ratio
		 : rest == "compHPF" ? c.sidechain_hpf
		                     : c.blend) = n;
	}
	else { // stutter
		(rest == "quantized"        ? states.stutter.quantized
		 : rest == "reverse"        ? states.stutter.reversed
		 : rest == "useSongStutter" ? states.stutter.use_song_stutter
		                            : states.stutter.ping_pong) = n != 0;
	}
	return true;
}

std::optional<int32_t> held_effect_setting(const EffectStates& states, std::string_view path) {
	const engine::SettingInfo* row = engine::find_setting(kEffectSettings, path);
	const Answer value = effect_setting_value(states, path);
	if (row == nullptr || !value) {
		return std::nullopt;
	}
	if (row->kind == engine::SettingKind::CHOICE) {
		return engine::stored_value(*row, value->str());
	}
	// Any other row's held text is a whole number (a BOOL's too), read without the row's bounds.
	const std::optional<int64_t> n = engine::whole_number(value->str());
	return n ? std::optional<int32_t>{static_cast<int32_t>(*n)} : std::nullopt;
}

} // namespace deluge::effects
