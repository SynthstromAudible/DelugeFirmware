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
	NO_NOTES, // No notes go to the arpeggiator
	TOO_LONG, // The rows would only line up again after too long
	TOO_MANY_NOTES,
	MUTED_ROW,     // A muted row is in the way of notes the arpeggiator plays
	UNFROZEN_ROW,  // The kit arpeggiator plays a row which doesn't go to it, so it can't be frozen
	AUTOMATION,    // The layout would change, which automation can't follow, or the randomizer is automated
	FILL_NOTES,    // Fill notes on a frozen row would have to be written out, or are in the way of frozen notes
	OUTSIDE_SCALE, // A note would need a new row outside the song's scale, in scale mode
	OUT_OF_RANGE,  // A note outside 0-127 (from a chord on a high note) would play, but can't have a row
	ARP_OFF,
	NO_MEMORY,
	NOT_IN_CLIP_VIEW, // beforeChanging didn't get back to the clip's view, where the change can be recorded for undo
};

/// Whether the clip has an arpeggiator which could be frozen: a synth, kit, MIDI or CV clip with the arpeggiator on
bool isAvailable(Clip* clip);

/// Freeze the arpeggiator of the clip. Everything which can refuse the freeze is checked first; then, before anything
/// is changed, beforeChanging is called (if given), e.g. to leave the menu, as the change is made from the clip's view.
Result freeze(InstrumentClip* clip, void (*beforeChanging)() = nullptr);

/// Stop the notes of a clip which is about to have its arpeggiator switched on or off. For synth, MIDI and CV clips it
/// does the same as arpeggiator::PresetMode::writeCurrentValue(); for kits it stops all the kit's notes.
void stopNotesForArpChange(InstrumentClip* clip);

/// Change the length of the clip as Song::setClipLength() does, leaving it to resyncPlayback() and resumePlayback()
/// to carry on playing. The clip's instances in the arrangement keep their lengths: they play the start of the clip,
/// which sounds the same.
void setLoopLength(InstrumentClip* clip, int32_t loopLength);

/// How far the clip has played since it started (negative before it starts). Unlike its play position, this keeps the
/// place of the clip and its rows when their lengths or directions change.
int64_t getPlayPos(InstrumentClip* clip);

/// Let the clip carry on from playPos (from getPlayPos()) after its layout was changed. The clip is put in place but
/// left silent, for resumePlayback() to start its notes once everything is changed.
void resyncPlayback(InstrumentClip* clip, int64_t playPos);

/// Start the notes the clip is in the middle of, after its notes or its arpeggiator changed. If its length changed
/// (lengthChanged) and it's the clip the external clock is scaled to, all clips are resynced to the clock instead, as
/// for any change to that clip's length, which also does this.
void resumePlayback(InstrumentClip* clip, bool lengthChanged = false);

} // namespace arp_freeze
