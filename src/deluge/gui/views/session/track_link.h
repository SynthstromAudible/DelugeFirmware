/*
 * Copyright © 2026 Rudement (Initial Author), Sean Ditny (Follow-up Author)
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

class Clip;
class Output;

/// Temporary Session selection and sound-menu targeting.
/// Output owns lock state and cross-clip parameter propagation.
namespace deluge::gui::track_link {

bool enabled();

/// True while the mode is on (whether or not a track has been picked yet).
bool modeIsOn();
void toggleLock(Output* output);
/// Whether the current edit target propagates parameter changes (also controls the LED).
bool editingSelectedTrack();
/// Toggle a clip's output lock without changing the clip's kit/drum target.
void toggleLockForClip(Clip* clip);
Output* selectedOutput();
Clip* selectedClip();

/// True if the mode is on and `output` is the picked track.
bool isSelected(Output const* output);

/// Leaves the mode and puts song view's knobs back as they were. Safe to call when the mode is off.
void exitMode();

/// Picks a track. `preferred` is the clip the knobs should target if it belongs to the track, such as the clip under
/// the pad that was pressed; pass nullptr to let the first suitable clip be used.
void selectOutput(Output* output, Clip* preferred);

/// SELECT while the mode is on: open the picked track's sound menu on top of song view, aimed at the clip the gold
/// knobs are on. Automatable parameter changes reach the other clips on the output, the same as a knob turn.
/// Returns false if the mode is off, so SELECT keeps its ordinary meaning.
bool openMenu();

/// Whether the Track Edit menu is open. SoundEditor consults this so that,
/// with song view underneath, it still edits a clip rather than the song.
bool isMenuOpen();

} // namespace deluge::gui::track_link
