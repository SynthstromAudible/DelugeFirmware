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

#include "effects/setting_params.h"

#include <string>
#include <utility>

namespace deluge::effects {

namespace {

[[nodiscard]] Stepped numbered(const EffectStates& after, int64_t value) {
	return {.after = after, .value = serialization::Value::makeNumber(value)};
}

[[nodiscard]] Stepped worded(const EffectStates& after, std::string_view word) {
	return {.after = after, .value = serialization::Value::makeScalar(std::string{word})};
}

} // namespace

std::optional<Stepped> step_setting(plugin::ParamId id, const EffectStates& fx, const StepContext& ctx) {
	EffectStates after = fx;
	if (id == setting::kDelaySyncType) {
		switch (fx.delay.sync_type) {
		case plugin::SyncType::TRIPLET:
			after.delay.sync_type = plugin::SyncType::DOTTED;
			break;
		case plugin::SyncType::DOTTED:
			after.delay.sync_type = plugin::SyncType::EVEN;
			break;
		default:
			after.delay.sync_type = plugin::SyncType::TRIPLET;
			break;
		}
		return numbered(after, std::to_underlying(after.delay.sync_type));
	}
	if (id == setting::kDelayPingPong) {
		after.delay.ping_pong = !fx.delay.ping_pong;
		return numbered(after, after.delay.ping_pong ? 1 : 0);
	}
	if (id == setting::kDelaySyncLevel) {
		// SyncLevel::NONE is skipped: the cycle runs 1 to kMaxSyncLevel.
		const int32_t level = plugin::index_of(fx.delay.sync_level) % plugin::kMaxSyncLevel + 1;
		after.delay.sync_level = plugin::sync_level_from_index(level);
		return numbered(after, level);
	}
	if (id == setting::kDelayType) {
		after.delay.type = next_delay_type(fx.delay.type);
		return worded(after, to_string(after.delay.type));
	}
	if (id == setting::kLpfMode) {
		after.filter.lpf_mode = static_cast<FilterMode>((std::to_underlying(fx.filter.lpf_mode) + 1) % kNumLPFModes);
		return worded(after, to_string(after.filter.lpf_mode));
	}
	if (id == setting::kHpfMode) {
		after.filter.hpf_mode =
		    static_cast<FilterMode>((std::to_underlying(fx.filter.hpf_mode) + 1) % kNumHPFModes + kFirstHPFMode);
		return worded(after, to_string(after.filter.hpf_mode));
	}
	if (id == setting::kSidechainSyncLevel) {
		// Between the song's slow level and its fast one.
		const int32_t level = plugin::index_of(fx.sidechain.sync_level) == 7 - ctx.tick_magnitude
		                          ? 9 - ctx.tick_magnitude
		                          : 7 - ctx.tick_magnitude;
		after.sidechain.sync_level = plugin::sync_level_from_index(level);
		return numbered(after, level);
	}
	if (id == setting::kModFXType) {
		after.mod_fx.type = static_cast<ModFXType>((std::to_underlying(fx.mod_fx.type) + 1) % kNumModFXTypes);
		return worded(after, to_string(after.mod_fx.type));
	}
	return std::nullopt;
}

} // namespace deluge::effects
