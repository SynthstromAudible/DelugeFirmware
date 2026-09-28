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

/// @brief The scenarios' own preset loader.
///
/// The firmware reads presets on the interaction tier: every production path goes through
/// `deluge::gui::InstrumentLoad`, and there is no blocking wrapper for the scenarios to share with it. A scenario
/// still needs a preset in memory before it can drive a gesture at one, so it builds its fixtures here instead --
/// blocking, called inline from a fixture build on the interaction tier.
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
/// `StorageManager::loadInstrumentFromFile()` did for production callers before they moved onto the interaction
/// tier. Samples are not read -- the caller decides, as the production paths do.
/// @param song      The song the parse hangs its param managers on.
/// @param outputType SYNTH or KIT.
/// @param getInstrument Out: the parsed Instrument, untouched on failure.
/// @param path      The document's full path.
/// @param name      In/out: the preset's name, as the parse rewrites it.
/// @param dirPath   In/out: the preset's folder, as the parse rewrites it.
/// @return Error::NONE, or why the document could not be read.
Error loadPresetFixture(Song* song, OutputType outputType, Instrument** getInstrument, char const* path,
                        std::string* name, std::string* dirPath);

/// @brief Make a new track and clip on the preset at @p presetPath, as the session view's empty-pad gesture does.
///
/// Creates a clip on the preset at @p presetPath, loaded as the new-track gesture's acquisition would load it — no
/// walk: a fixture names the preset it wrote. Synchronous; any context.
/// @param outputType SYNTH or KIT (MIDI and CV read no card and are acquired nothing).
/// @param yDisplay   The session row to build the clip on.
/// @param presetPath The preset file, e.g. "KITS/SCKIT.XML"; ignored for MIDI and CV.
/// @return The new clip, or null if the preset could not be loaded or the clip could not be made.
Clip* createNewInstrumentClip(OutputType outputType, int32_t yDisplay, const char* presetPath);

} // namespace deluge::harness

#endif // DELUGE_HOST
