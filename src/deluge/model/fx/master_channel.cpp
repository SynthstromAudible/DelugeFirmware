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

#include "model/fx/master_channel.h"
#include "model/fx/channel_setup.h"
#include "model/mod_controllable/delay_arena_app.h"
#include "model/mod_controllable/mod_fx_allocator.h"

namespace deluge::model {

MasterChannel::MasterChannel() : chain_{kModFxRingAllocator, kGranularAllocator, delayArena()} {
	initChannelSettings(binding_, view_, chain_.core(), chain_.filter());
}

MasterChannel::~MasterChannel() {
	releaseDelayMemory(chain_.core().delay());
}

} // namespace deluge::model
