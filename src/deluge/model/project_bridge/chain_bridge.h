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
#include "model/fx/host_chain_state.h"

#include <cstdint>
#include <optional>

class GlobalEffectable;
class ModControllableAudio;
class Output;
class Song;

namespace deluge::bridge {

/// @brief The ChainCodec the app gives the model: the effects library's chain codec.
/// @return The codec.
[[nodiscard]] const deluge::project::ChainCodec& chainCodec();

/// @brief The owner whose effects @p address names (a kit, an audio output, or the song's global effectable), or null.
/// @param address Which chain.
/// @param song    The song to look in.
/// @return The owner, or null if @p address names none.
[[nodiscard]] ::GlobalEffectable* chainOwnerAt(deluge::project::ChainAddress address, ::Song& song);

/// @brief The address of @p owner's chain in @p song, or nothing (a sound, or an owner not in the song).
/// @param owner The effects owner.
/// @param song  The song to look in.
/// @return The chain's address, or nullopt.
[[nodiscard]] std::optional<deluge::project::ChainAddress> chainAddressOf(const ::ModControllableAudio& owner,
                                                                          ::Song& song);

/// @brief @p owner plays @p chain from now on: the chain takes @p home's slots and what the owner plays. Nothing when
/// the owner already plays it.
/// @param owner The host owner.
/// @param chain Its chain record.
/// @param home  Whether the chain is a clip's or the master's.
void adoptChainState(::GlobalEffectable& owner, deluge::project::FxChain& chain, deluge::project::ChainHome home);

/// @brief A host owner that joined the song (adopted, created, a preset read over it) plays its own chain record, made
/// when it has none. Nothing for another output, or one whose record is not its own.
/// @param output The host owner's output.
/// @param song   The song it belongs to.
void recordOutputChain(::Output& output, ::Song& song);

/// @brief @p output's effects owner when it runs a host chain (a kit, an audio track), else null.
/// @param output The output.
/// @return The owner, or null.
[[nodiscard]] ::GlobalEffectable* chainOwnerOf(::Output& output);

} // namespace deluge::bridge
