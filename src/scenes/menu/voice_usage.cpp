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

/// @file
/// @brief The voice usage page's sampling clock. See `voice_usage.h`.

#include "scenes/menu/voice_usage.h"

#include "scenes/menu/menu_core.h"
#include "scenes/menu/nodes/nodes.h"

#include <utility>

namespace deluge::scenes::menu {

namespace {
/// `cpu::VoiceUsage::kRefreshIntervalMs` (`cpu/voice_usage.h:91`).
constexpr uint32_t kVoiceUsageSampleMs = 500;
} // namespace

VoiceUsageClock::Place VoiceUsageClock::place_of(const Node* focused) {
	if (focused == &nodes::voice_usage) {
		return Place::Page;
	}
	if (focused == &nodes::voice_usage_live || focused == &nodes::voice_usage_average
	    || focused == &nodes::voice_usage_maximum) {
		return Place::Counter;
	}
	return Place::None;
}

std::optional<uint32_t> VoiceUsageClock::next_timer(const MenuState& st) const {
	if (place_of(focused(st)) == Place::None) {
		return std::nullopt;
	}
	return kVoiceUsageSampleMs - std::min(elapsed_ms_, kVoiceUsageSampleMs);
}

bool VoiceUsageClock::tick(const MenuContext& ctx, const MenuState& st, uint32_t dt_ms) {
	const Place place = place_of(focused(st));
	const Place was = std::exchange(at_, place);
	if (place == Place::None) {
		return false;
	}
	const int32_t live = ctx.facts != nullptr ? ctx.facts->live_voice_count() : 0;
	if (place != was) {
		// `VoiceUsage::beginSession` resets and samples, on arrival and on the way back from a counter
		// (`cpu/voice_usage.h:70-76`); `VoiceCount::beginSession` samples without a reset (`:29-33`).
		if (place == Place::Page) {
			sampler_.reset();
		}
		sampler_.sample(live);
		elapsed_ms_ = 0;
	}
	else {
		elapsed_ms_ += dt_ms;
		if (elapsed_ms_ < kVoiceUsageSampleMs) {
			return false;
		}
		elapsed_ms_ = 0;
		sampler_.sample(live);
	}
	// The VERTICAL style re-reads the open counter (`:81-86`); the page's columns read the sampler as they draw.
	return place == Place::Counter;
}

} // namespace deluge::scenes::menu
