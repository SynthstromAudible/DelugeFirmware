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

#include "model/fx/effects_owner.h"
#include "model/lane_owner.h"
#include "serialization/value.h"

#include <string_view>

class Song;

namespace deluge::edits {

/// @brief Apply a gesture's change of an owner's effect setting.
///
/// For a sound bound in the current song, issues a SetEngineSetting on its slot, so the slot keeps holding its
/// effects; for a host chain in the song, a SetEffectSetting on its chain; for an owner outside the song's slots and
/// chains, writes its settings directly (bridge::applyEffectSetting).
/// @param owner The effect owner being edited.
/// @param path  Path of the setting within the effects' schema.
/// @param value The new value, in its Edit form.
void changeEffect(deluge::model::LaneOwner owner, std::string_view path, deluge::serialization::Value value);

/// @brief changeEffect in @p song rather than the current song: for a song being loaded, which is not current yet.
/// @param song  The song the owner belongs to.
/// @param owner The effect owner being edited.
/// @param path  Path of the setting within the effects' schema.
/// @param value The new value, in its Edit form.
void changeEffect(::Song& song, deluge::model::LaneOwner owner, std::string_view path,
                  deluge::serialization::Value value);

/// @brief changeEffect for an owner named by its effects: a walk over the song's chains, a ModelStack's slot.
/// @param owner The effect owner being edited.
/// @param path  Path of the setting within the effects' schema.
/// @param value The new value, in its Edit form.
void changeEffect(deluge::model::EffectsOwner owner, std::string_view path, deluge::serialization::Value value);

/// @brief changeEffect in @p song for an owner named by its effects.
/// @param song  The song the owner belongs to.
/// @param owner The effect owner being edited.
/// @param path  Path of the setting within the effects' schema.
/// @param value The new value, in its Edit form.
void changeEffect(::Song& song, deluge::model::EffectsOwner owner, std::string_view path,
                  deluge::serialization::Value value);

/// @brief Apply a gesture's change of which value an owner's knobs edit.
///
/// Issues a SetChainView on the owner's chain when it has one in the current song, else sets its view
/// (model::setChainView).
/// @pre @p owner is a host chain's (a kit, an audio track, the master): a sound has no chain view.
/// @param owner The effect owner being edited.
/// @param key   `modFXCurrentParam` or `currentFilterType`.
/// @param word  The new selection, by name.
void changeView(deluge::model::LaneOwner owner, std::string_view key, std::string_view word);

} // namespace deluge::edits
