/*
 * Copyright © 2024 Synthstrom Audible Limited
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

#include "model/sync.h"
#include <cstdint>

namespace deluge::project {

/// A song's reverb send settings (the reverb algorithm/`model` enum stays on the controller for
/// now). A component of deluge::project::Song. Field names keep their `reverb*` prefix during the
/// boundary-first migration; they lose it in the later idiomatic-refinement pass.
struct ReverbSettings {
	float reverbRoomSize{0};
	float reverbHPF{0};
	float reverbLPF{0};
	float reverbDamp{0};
	float reverbWidth{0};
	int32_t reverbPan{0};
	int32_t reverbSidechainVolume{0};
	int32_t reverbSidechainShape{0};
	int32_t reverbSidechainAttack{0};
	int32_t reverbSidechainRelease{0};
	SyncLevel reverbSidechainSync{};
};

} // namespace deluge::project
