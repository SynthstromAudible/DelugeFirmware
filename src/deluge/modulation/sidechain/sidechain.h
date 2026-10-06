/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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

#include "definitions_cxx.hpp"
#include "engines/effects/state.h"
#include "modulation/sync.h"
#include "synth/envelope.h" // Envelope::Stage

#include <cstdint>

class Song;

class SideChain {
public:
	SideChain();

	Envelope::Stage status;
	uint32_t pos;
	int32_t lastValue;
	int32_t pendingHitStrength;

	int32_t envelopeOffset{};
	int32_t envelopeHeight{};

	/// The envelope's next @p numSamples, shaped by @p shapeValue, at @p settings' attack, release and sync.
	int32_t render(uint16_t numSamples, int32_t shapeValue, const deluge::effects::SidechainState& settings);
	void registerHit(int32_t strength);
	void registerHitRetrospectively(int32_t strength, uint32_t numSamplesAgo,
	                                const deluge::effects::SidechainState& settings);

	/// The settings an owner's sidechain starts with: the menus' default attack and release, and a sync level of an
	/// eighth of the song being loaded (or the current one) at its tick magnitude.
	[[nodiscard]] static deluge::effects::SidechainState defaultSettings();

private:
	static int32_t getActualAttackRate(const deluge::effects::SidechainState& settings);
	static int32_t getActualReleaseRate(const deluge::effects::SidechainState& settings);
};
