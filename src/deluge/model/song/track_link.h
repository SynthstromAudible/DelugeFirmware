/*
 * Copyright © 2026 Rudement
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

#include <cstdint>

class Clip;
class ModelStackWithAutoParam;
class Output;

/// Track edit mode (song view). With the Track Edit community feature on, Shift + Affect-entire switches it on.
/// Pressing a pad then picks that pad's track, every clip on the track flashes, and a parameter turned with the gold
/// knobs is written into the same parameter of every clip on that track, so "pan the hi-hat" is one move instead of one
/// per clip. SELECT opens the track's sound menu, and parameters changed there (filters, envelopes and so on) reach
/// every clip the same way.
///
/// The mode itself is never saved. The values it writes are ordinary per-clip parameter values, so they are saved
/// with the song exactly like a value set one clip at a time.
namespace deluge::model::track_link {

/// True while the mode is on (whether or not a track has been picked yet).
bool modeIsOn();

/// True if the mode is on and `output` is the picked track.
bool isSelected(Output const* output);

/// Kits: true if the gold knobs should act on the kit's selected drum row rather than the whole kit.
/// Only meaningful for the picked track; consulted by InstrumentClip::getActiveModControllable.
bool targetsDrumRow(Output const* output);

/// Text for song view's screen while the mode is on ("TRACK EDIT", "WHOLE KIT", "DRUM ROW", "PICK A TRACK"), or
/// nullptr when the mode is off.
char const* statusText();

/// Shift + Affect-entire. Does nothing unless the Track Edit community feature is on.
void toggleMode();

/// Leaves the mode and puts song view's knobs back as they were. Safe to call when the mode is off.
void exitMode();

/// Picks a track. `preferred` is the clip the knobs should target if it belongs to the track, such as the clip under
/// the pad that was pressed; pass nullptr to let the first suitable clip be used.
void selectOutput(Output* output, Clip* preferred);

/// Plain Affect-entire while the mode is on, on a kit track: switch between the whole kit and the selected drum row.
void toggleKitTarget();

/// SELECT while the mode is on: open the picked track's sound menu on top of song view, aimed at the clip the gold
/// knobs are on. Every parameter changed in that menu then reaches every clip on the track, the same as a knob turn.
/// Returns false if the mode is off, so SELECT keeps its ordinary meaning.
bool openMenu();

/// The clip the Track Edit menu is editing, or nullptr when that menu is not open. SoundEditor consults this so that,
/// with song view underneath, it still edits a clip rather than the song.
Clip* menuClip();

/// Called from SessionView::focusRegained. If song view is getting focus back from the Track Edit menu, puts the
/// song's current clip and the kit's Affect-entire back as they were, re-aims the knobs, and returns true so the mode
/// stays on. Returns false otherwise.
bool returnFromMenu();

/// Called from AutoParam after a user edit has been applied to `source`. Writes the same value to the same
/// parameter of every other clip on the picked track. Does nothing unless the mode is on, the edit was made on a
/// clip of the picked track, and the parameter is a patched or unpatched one.
void propagate(int32_t value, ModelStackWithAutoParam const* source, bool shouldLogAction,
               bool mayDeleteNodesInLinearRun, bool doMPEMode);

} // namespace deluge::model::track_link
