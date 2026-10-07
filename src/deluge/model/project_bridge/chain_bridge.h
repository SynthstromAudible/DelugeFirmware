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

#include "deluge-project/fx_chain.h"

#include <cstdint>
#include <optional>

class GlobalEffectable;
class ModControllableAudio;
class Output;
class Song;

namespace deluge::bridge {

/// @brief The ChainCodec the app gives the model: the effects library's chain codec.
[[nodiscard]] const deluge::project::ChainCodec& chainCodec();

/// @brief The owner whose effects @p address names (a kit, an audio output, or the song's global effectable), or null.
[[nodiscard]] ::GlobalEffectable* chainOwnerAt(deluge::project::ChainAddress address, ::Song& song);

/// @brief The address of @p owner's chain in @p song, or nothing (a sound, or an owner not in the song).
[[nodiscard]] std::optional<deluge::project::ChainAddress> chainAddressOf(const ::ModControllableAudio& owner,
                                                                          ::Song& song);

/// @brief @p owner's chain as its runtime state encodes it now, at @p tick_magnitude: its effects, the compressor
/// threshold a save writes, and its view.
[[nodiscard]] deluge::project::FxChain encodeChainOf(::GlobalEffectable& owner, deluge::project::ChainHome home,
                                                     int32_t tick_magnitude);

/// @brief After a song read: every kit's, audio track's and the master's chain from what the owner loaded.
void fillChains(::Song& song);

/// @brief A host owner that joined or was rebuilt natively (adopted, created, cloned, a preset read over it): records
/// its chain.
void recordOutputChain(::Output& output, ::Song& song);

} // namespace deluge::bridge
