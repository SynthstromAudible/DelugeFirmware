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

#include "effects/chain.h"
#include "effects/state.h"
#include "model/fx/effects_owner.h"
#include "model/lane_owner.h"
#include "project/fx_chain.h"

#include <cstdint>
#include <optional>

class ModControllable;
class Output;
class Song;

namespace deluge::bridge {

/// @brief The ChainCodec the app gives the model: the effects library's chain codec.
/// @return The codec.
[[nodiscard]] const deluge::project::ChainCodec& chainCodec();

/// @brief The effects of the chain @p address names (a kit's, an audio output's, or the song master's).
/// @param address Which chain.
/// @param song    The song to look in.
/// @return The chain's effects, or nullopt if @p address names none.
[[nodiscard]] std::optional<deluge::model::EffectsOwner> chainAt(deluge::project::ChainAddress address, ::Song& song);

/// @brief The owner of the chain @p address names: a kit, an audio output, or the song's master channel.
/// @param address Which chain.
/// @param song    The song to look in.
/// @return The owner; empty if @p address names none.
[[nodiscard]] deluge::model::LaneOwner chain_owner_at(deluge::project::ChainAddress address, ::Song& song);

/// @brief The owner of the chain @p address names, as a ModelStack's owner slot holds it (a kit, an audio output, or
/// the song's own owner); null if @p address names none. A bridge: it goes with the slot.
/// @param address Which chain.
/// @param song    The song to look in.
/// @return The owner, or null.
[[nodiscard]] ::ModControllable* chainSlotAt(deluge::project::ChainAddress address, ::Song& song);

/// @brief The address of @p owner's chain in @p song, or nothing (a sound, or an owner not in the song).
/// @param owner The effects owner.
/// @param song  The song to look in.
/// @return The chain's address, or nullopt.
[[nodiscard]] std::optional<deluge::project::ChainAddress> chainAddressOf(deluge::model::EffectsOwner owner,
                                                                          ::Song& song);

/// @brief @p owner plays @p chain from now on: the chain takes @p home's slots and what the owner plays. Nothing when
/// the owner already plays it.
/// @param owner The host owner.
/// @param chain Its chain record.
/// @param home  Whether the chain is a clip's or the master's.
void adoptChainState(deluge::model::EffectsOwner owner, deluge::project::FxChain& chain,
                     deluge::project::ChainHome home);

/// @brief A host owner that joined the song (adopted, created, a preset read over it) plays its own chain record, made
/// when it has none. Nothing for another output, or one whose record is not its own.
/// @param output The host owner's output.
/// @param song   The song it belongs to.
void recordOutputChain(::Output& output, ::Song& song);

/// @brief Under the typed-write audit (typedWriteAudit()), compare @p got, the typed write of @p value at @p path onto
/// @p before, with the tree way: @p before's chain encoded at @p tick_magnitude, the leaf written into its effect's
/// slot in its file form, and the slot read back onto @p before. They agree, or differ only where the file cannot
/// tell (both encode alike: a value the file cannot hold, which the typed write kept); else the audit counts a
/// mismatch.
/// @param before         What the owner played.
/// @param home           Whether the chain is a clip's or the master's.
/// @param path           The setting's path.
/// @param value          The value, in its Edit form (a sync level is the level itself).
/// @param tick_magnitude The song's input tick magnitude.
/// @param got            The typed write's result.
void auditEffectWrite(const deluge::effects::EffectStates& before, deluge::effects::Home home, std::string_view path,
                      const deluge::serialization::Value& value, int32_t tick_magnitude,
                      const deluge::effects::EffectStates& got);

/// @brief @p output's effects when it runs a host chain (a kit, an audio track), else nullopt.
/// @param output The output.
/// @return The chain's effects, or nullopt.
[[nodiscard]] std::optional<deluge::model::EffectsOwner> chainOwnerOf(::Output& output);

} // namespace deluge::bridge
