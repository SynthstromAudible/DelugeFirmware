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

#include "engines/effects/mod_fx.h"
#include "memory/general_memory_allocator.h"
#include "memory/heaps.h"
#include "memory/memory_allocator_interface.h"

#include <cstddef>

namespace deluge::model {

/// @brief The mod FX rings come from the app's heap, for the owners and for the bench alike.
inline constexpr effects::RingAllocator kModFxRingAllocator{
    .allocate = [](std::size_t bytes) { return delugeAlloc(static_cast<unsigned int>(bytes)); },
    .release = [](void* block) { delugeDealloc(block); },
};

/// @brief The granular State and ring come from the non-stealable external SDRAM heap, 16-byte aligned, for the owners
/// and for the bench alike.
inline constexpr effects::RingAllocator kGranularAllocator{
    .allocate = [](std::size_t bytes) { return memory::alloc_external(bytes); },
    .release = [](void* block) { delugeDealloc(block); },
};

/// @brief The stutter's tape comes from the app's low-speed heap, for the song's stutter and for the bench alike.
inline constexpr effects::RingAllocator kStutterAllocator{
    .allocate = [](std::size_t bytes) { return allocLowSpeed(static_cast<uint32_t>(bytes)); },
    .release = [](void* block) { delugeDealloc(block); },
};

} // namespace deluge::model
