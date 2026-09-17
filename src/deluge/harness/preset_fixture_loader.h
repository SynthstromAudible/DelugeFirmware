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

/// @brief The scenarios' own preset loader, on the storage owner.
///
/// The firmware reads presets on the interaction tier now: every production path goes through
/// `deluge::gui::InstrumentLoad`, and the blocking wrapper the scenarios used to share with it
/// (`StorageManager::loadInstrumentFromFile()`) is gone. A scenario still needs a preset in memory before it can drive
/// a gesture at one, so it builds its fixtures here instead -- blocking, on the owner, where a scenario's setup runs.
///
/// @note `DELUGE_HOST`-only, compiled into `deluge_app` (the harness directory is globbed into the app's sources).
#ifdef DELUGE_HOST

#include "definitions_cxx.hpp"
#include "storage/preset_acquisition.h"

#include <string>

class Clip;
class Instrument;
class Song;

namespace deluge::harness {

/// @brief Read the preset document at @p path into a new Instrument, blocking.
///
/// Opens the document with the root tags @p outputType names, parses it, and frees the staging: what
/// `StorageManager::loadInstrumentFromFile()` did for the production callers this rung moved onto the interaction
/// tier. Samples are not read -- the caller decides, as the production paths do.
/// @param song      The song the parse hangs its param managers on.
/// @param outputType SYNTH or KIT.
/// @param getInstrument Out: the parsed Instrument, untouched on failure.
/// @param path      The document's full path.
/// @param name      In/out: the preset's name, as the parse rewrites it.
/// @param dirPath   In/out: the preset's folder, as the parse rewrites it.
/// @return Error::NONE, or why the document could not be read.
/// @pre Runs on the storage owner.
Error loadPresetFixture(Song* song, OutputType outputType, Instrument** getInstrument, char const* path,
                        std::string* name, std::string* dirPath);

/// @brief Walk the instrument folder for the first preset nobody in the song uses, and load it.
///
/// The walk today's gestures run (`findAnUnlaunchedPresetIncludingWithinSubfolders()`), then @ref loadPresetFixture
/// and the samples, shaped as the `AcquireResult` a gesture's follow-up is handed. An Instrument already active in the
/// song is taken as it is; a hibernating one has its samples loaded, as `InstrumentLoad` does for the same case.
/// @param outputType SYNTH or KIT.
/// @return What was found, or an error with no Instrument.
/// @pre Runs on the storage owner.
deluge::storage::AcquireResult acquireUnlaunchedPreset(OutputType outputType);

/// @brief Make a new track and clip on the first unused preset of @p outputType, as the session view's empty-pad
///        gesture does.
///
/// The scenarios' stand-in for that gesture: it acquires the preset itself (@ref acquireUnlaunchedPreset, on the
/// owner) and hands `SessionView::createNewInstrumentClip()` the result, which is what the gesture's own acquisition
/// does on the interaction tier.
/// @param outputType SYNTH or KIT (MIDI and CV read no card and are acquired nothing).
/// @param yDisplay   The session row to build the clip on.
/// @return The new clip, or null if the preset could not be acquired or the clip could not be made.
Clip* createNewInstrumentClip(OutputType outputType, int32_t yDisplay);

} // namespace deluge::harness

#endif // DELUGE_HOST
