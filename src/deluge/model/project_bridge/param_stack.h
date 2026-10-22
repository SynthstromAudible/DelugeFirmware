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

/// @file
/// @brief An address's engine param, as the engine's own stack names it.
///
/// The one place on the param path where a ParamAddress becomes a ModelStack. The stack is to be retired: the Edits
/// that still need one (undo's pre-log, recording automation, the engine's projection of a write) take it from here,
/// below the Edit, so nothing above an Edit builds one.

#include "deluge-project/param_address.h"

class ModelStackWithAutoParam;
class PatchCableSet;
class Song;
enum class Polarity : uint8_t;

namespace deluge::bridge {

/// @brief The engine stack naming @p address's AutoParam in @p song: the clip by id, the output or drum by id, the
/// drum's note row in that clip, the param collection the id is in, and the param by its runtime index.
///
/// An engine param of a synth or a sound drum (INSTRUMENT or DRUM, with its clip; a cable's depth too), of a kit's own
/// effects (INSTRUMENT, with its clip), of an audio clip (CLIP) or of the song (SONG); a MIDI clip's CC lane (CLIP,
/// MIDI_CC); an expression lane of a MIDI clip or a sound. An INSTRUMENT or DRUM owner without a clip names nothing
/// here, and nothing is created: a cable or a lane the owner does not hold yet is not found.
///
/// @param song    The song holding the owner.
/// @param address The param's address.
/// @param memory  MODEL_STACK_MAX_SIZE bytes, aligned as ModelStackWithAutoParam, which the stack lives in.
/// @return The stack, or null where @p song holds no such owner or param.
[[nodiscard]] ::ModelStackWithAutoParam* paramStackAt(::Song& song, const project::ParamAddress& address, void* memory);

/// @brief The engine stack naming the cable at @p address (a synth's or a sound drum's depth), made with @p polarity
/// where the sound does not hold it yet. The record takes the new cable, polarity and all, when the sound's cable set
/// is bound to it (it is, once relocated; the Edit that writes the cable relocates it).
/// @param song     The song holding the sound.
/// @param address  The cable's depth address.
/// @param polarity The polarity a new cable takes; an existing cable keeps its own.
/// @param memory   MODEL_STACK_MAX_SIZE bytes, aligned as ModelStackWithAutoParam, which the stack lives in.
/// @return The stack, or null where the sound does not resolve or its cable set is full.
[[nodiscard]] ::ModelStackWithAutoParam* cableStackAt(::Song& song, const project::ParamAddress& address,
                                                      ::Polarity polarity, void* memory);

/// @brief The patch cables of the sound @p owner names (a synth or a sound drum, with its clip; the id is not read).
/// @param song   The song holding the sound.
/// @param owner  The sound's address.
/// @param memory MODEL_STACK_MAX_SIZE bytes, aligned as ModelStackWithAutoParam, the lookup's scratch.
/// @return The cable set, or null where the sound does not resolve or has none.
[[nodiscard]] ::PatchCableSet* cableSetAt(::Song& song, const project::ParamAddress& owner, void* memory);

} // namespace deluge::bridge
