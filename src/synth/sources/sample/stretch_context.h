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
#pragma once

#include "dsp/core/fixed_point.h"
#include "dsp/fixed/noise/cong.h"
#include "dsp/fixed/timestretch/cache_tail.h"

#include <array>
#include <bit>
#include <cstdint>
#include <optional>
#include <span>

namespace deluge::synth {

/// @file
/// @brief What every time stretcher shares, owned by the engine: the audio routine's hop budget, the noise stream and
/// the cache-tail buffers it lends (decision 2 of docs/dev/time_stretcher_design.md).

/// @brief The hops the audio routine allows across all voices (S3).
struct HopBudget {
	static constexpr uint8_t kHopsPerRoutine = 3;
	uint8_t hops = 0;              ///< Hops this audio routine call, across voices.
	bool culling_bypassed = false; ///< A hop set it; the engine ORs it into its own bypass where the legacy set it.

	/// @brief Whether a due hop that has been passed over @p passed_over times may be taken now: fewer than three this
	/// routine, and passed over at least twice as often as there have been hops.
	[[nodiscard]] constexpr bool allows(uint8_t passed_over) const {
		return hops < kHopsPerRoutine && static_cast<uint32_t>(passed_over) >= (static_cast<uint32_t>(hops) << 1);
	}
	/// @brief A hop is taken.
	constexpr void spend() {
		hops++;
		culling_bypassed = true;
	}
	/// @brief The audio routine starts again.
	constexpr void new_routine() {
		hops = 0;
		culling_bypassed = false;
	}
};

/// @brief A lent cache-tail buffer: its words, and the slot it goes back to.
struct TailLoan {
	std::span<dsp::Q<31>> words;
	uint8_t slot;
};

/// @brief Eight stereo cache-tail buffers, made with the engine and lent, so a voice leaving a stretched cache
/// allocates nothing; with all eight lent there is no crossfade (decision 2).
class TailBuffers {
public:
	static constexpr int32_t kCount = 8;
	static constexpr int32_t kWords = 2 * dsp::fixed::timestretch::kCacheTailCapacity;

	/// @brief The lowest free buffer, or nullopt when all are lent.
	[[nodiscard]] std::optional<TailLoan> lend() {
		const auto free = static_cast<uint8_t>(~lent_);
		if (free == 0) {
			return std::nullopt;
		}
		const auto slot = static_cast<uint8_t>(std::countr_zero(free));
		lent_ |= static_cast<uint8_t>(1u << slot);
		return TailLoan{.words = std::span<dsp::Q<31>>(buffers_[slot]), .slot = slot};
	}
	/// @brief @p loan's buffer is free again.
	void give_back(TailLoan loan) { lent_ &= static_cast<uint8_t>(~(1u << loan.slot)); }
	[[nodiscard]] int32_t free_count() const { return kCount - std::popcount(lent_); }

private:
	std::array<std::array<dsp::Q<31>, kWords>, kCount> buffers_{};
	uint8_t lent_ = 0; ///< One bit per lent slot.
};
static_assert(TailBuffers::kCount == 8, "the lent mask is a byte");

/// @brief The engine's shared state each stretcher call is handed; every pointer is the engine's and never null.
struct StretchContext {
	HopBudget* budget;
	dsp::fixed::Cong* noise;
	TailBuffers* tails;
};

} // namespace deluge::synth
