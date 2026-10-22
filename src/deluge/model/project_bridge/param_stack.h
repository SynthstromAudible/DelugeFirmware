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
class Song;

namespace deluge::bridge {

/// @brief The engine stack naming @p address's AutoParam in @p song: the clip by id, the output or drum by id, the
/// drum's note row in that clip, the param collection the id is in, and the param by its runtime index.
///
/// An engine param of a synth or a sound drum (INSTRUMENT or DRUM, with its clip), of a kit's own effects
/// (INSTRUMENT, with its clip), of an audio clip (CLIP) or of the song (SONG). Another space, or an INSTRUMENT or DRUM
/// owner without a clip, names nothing here.
///
/// @param song    The song holding the owner.
/// @param address The param's address.
/// @param memory  MODEL_STACK_MAX_SIZE bytes, aligned as ModelStackWithAutoParam, which the stack lives in.
/// @return The stack, or null where @p song holds no such owner or param.
[[nodiscard]] ::ModelStackWithAutoParam* paramStackAt(::Song& song, const project::ParamAddress& address, void* memory);

} // namespace deluge::bridge
