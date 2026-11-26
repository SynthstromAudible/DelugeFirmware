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

#include "synth/engines/engine.h"
#include "synth/engines/subtractive_engine.h"
#include "synth/voice/unison_part.h"
#include <cstdint>
#include <span>

namespace deluge::synth {

/// @brief The DX7 engine: one DX7 voice per unison part on the subtractive chain, in place of the two oscillator
/// slots. Slot 0 keeps the lifecycle the DX7 plays through (its active flag, its phase increment from the DX7's pitch,
/// its level as the subtractive render ramps it); slot 1 never sounds.
class Dx7Engine final : public SubtractiveEngine {
public:
	/// @brief Marks slot 0's parts active and works out their increments, then starts each active part's DX7.
	/// @return False when a part's DX7 cannot start (its pool is empty).
	bool note_on(const EngineNote& note) override;
	/// @brief The subtractive render with each part's DX7 as slot 0's source.
	/// @return What the block left for the voice.
	EngineRenderResult render(const EngineBlock& block) override;
	/// @brief Keys up each part's DX7, which releases on its own envelopes.
	void release(std::span<UnisonPart> parts, const ReleaseInfo& info, int32_t priority_rating) override;
};

} // namespace deluge::synth
