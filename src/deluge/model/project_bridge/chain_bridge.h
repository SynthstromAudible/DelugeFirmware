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

/// @brief A chain record holding @p state: its effects and its view, its slots @p home's effects in order.
/// @param state The host chain state.
/// @param home  Whether the chain is a clip's or the master's.
/// @return The chain record.
[[nodiscard]] deluge::project::FxChain encodeChainOf(const deluge::model::HostChainState& state,
                                                     deluge::project::ChainHome home);

/// @brief The state @p chain holds: its effects (the defaults when it holds none) and its view.
/// @param chain The chain record.
/// @return The decoded state.
[[nodiscard]] deluge::model::HostChainState chainStateOf(const deluge::project::FxChain& chain);

/// @brief After a song read: every kit's, audio track's and the master's chain record made from what the owner loaded
/// (its element decoded onto the owner's defaults), then its state set from the record.
/// @param song The song just read.
void fillChains(::Song& song);

/// @brief A host owner that joined or was rebuilt natively (adopted, created, cloned, a preset read over it): its chain
/// record made from what it holds, then its state set from the record.
/// @param output The host owner's output.
/// @param song   The song it belongs to.
void recordOutputChain(::Output& output, ::Song& song);

} // namespace deluge::bridge
