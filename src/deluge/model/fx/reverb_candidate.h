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

#include "dsp-next/core/fixed_point.h"
#include "effects/state.h"
#include <cstdint>

namespace deluge::model {

/// @brief The loudest reverb send the reverb's auto duck has found so far, and how the winner ducks the return. The
/// search starts from the song's own send, which the sidechain can't duck: if no owner beats it, the return isn't
/// ducked.
struct ReverbCandidate {
	int32_t amount{0};               ///< The send compared, signed Q31.
	bool found{false};               ///< Whether an owner's send beat the song's.
	dsp::Q<29> duck_depth{};         ///< The winner's sidechain volume as a patch cable's depth.
	dsp::Q<31> duck_shape{};         ///< The winner's sidechain shape param.
	effects::SidechainState rates{}; ///< The winner's envelope; the reverb takes its attack, release and sync level.
};

/// @brief Whether a send of @p amount beats @p best: strictly louder, so the first owner at a tie keeps it.
/// @param amount The owner's send, signed Q31.
/// @param best   The best so far.
/// @return True when the owner wins.
[[nodiscard]] constexpr bool beats(int32_t amount, const ReverbCandidate& best) {
	return best.amount < amount;
}

} // namespace deluge::model
