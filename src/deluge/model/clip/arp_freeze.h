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

#include <cstdint>

class Clip;
class InstrumentClip;

/// Freezing the arpeggiator of a clip replaces the notes going through the arpeggiator with the notes it plays from
/// them, and switches the arpeggiator off. It's one undoable action.
///
/// The arpeggiator is run over the clip as it plays from its start. If the clip doesn't simply play forwards at its
/// length, it's written out: a reversed clip as it sounds, a ping-pong clip forwards then backwards, and rows with
/// their own length or direction until everything lines up again. The frozen clip then plays forwards at that length.
namespace arp_freeze {

enum class Result {
	OK,
	RECORDING,
	TOO_LONG, // The rows would only line up again after too long
	TOO_MANY_NOTES,
	NO_MEMORY,
};

/// Whether the clip has an arpeggiator which could be frozen: a synth, kit, MIDI or CV clip with the arpeggiator on
bool isAvailable(Clip* clip);

Result freeze(InstrumentClip* clip);

/// Stop the notes of a clip which is about to have its arpeggiator switched on or off. For synth, MIDI and CV clips it
/// does the same as arpeggiator::PresetMode::writeCurrentValue(); for kits it stops all the kit's notes.
void stopNotesForArpChange(InstrumentClip* clip);

/// Let the clip carry on playing from the right place after its layout was changed
void resyncPlayback(InstrumentClip* clip);

} // namespace arp_freeze
