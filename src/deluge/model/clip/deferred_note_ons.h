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

#include "model/note/note_lock_playback.h"
#include <cstdint>

class InstrumentClip;
class ModelStackWithSoundFlags;
class NoteRow;
class Sound;
struct PendingNoteOn;

/// Note-ons that can't start a voice in the current render, held until one that can.
///
/// A voice start is expensive, so voices only start while the time they take fits in the current render
/// (AudioEngine::wouldRefuseVoiceStart()), and a big chord's notes may start over several renders. Every voice start a
/// Sound makes goes through startVoice(): it starts now if this render still may, and otherwise it's queued here. Once
/// per render, after the song tick and before the song renders, sendWhatCanStart() starts as many queued notes as that
/// render may, oldest first. A queued note always starts at the start of a render, and never before a note queued
/// earlier for the same Sound.
///
/// There are two kinds of entry:
/// - A voice start (startVoice()): a note the Sound has already been told about - its arpeggiator, voice priority
///   and choke have all been dealt with - just waiting for its voice. This covers every source of note-ons: sequenced
///   notes, auditioning, MIDI input, and the notes the synth, kit and drum arpeggiators generate.
/// - A sequenced note (tryDefer()): a note the song tick holds back before it even reaches its Output, when it couldn't
///   start a voice anyway. With a 20-note step, the instrument, kit-arp and arpeggiator bookkeeping of the notes that
///   can't start is most of the tick's cost, and the tick comes on top of the render it precedes. Holding them back
///   spreads that work over the renders that start their voices. Only for notes that just start a voice on a Sound
///   (polyphonic or auto voice priority, arpeggiators off, not a choke drum); everything else goes through the Output
///   at once and, if need be, waits as a voice start.
///
/// A queued note is dropped when its note ends (note-off for that note, the NoteRow's note stopping, all-notes-off,
/// choke) or anything it refers to goes away (Sound, Clip, NoteRow, or playback stopping for a sequenced note), so it
/// never starts late for a note that has already finished.
namespace DeferredNoteOns {

/// A voice start that a Sound has decided on: the arguments of Sound::noteOnPostArpeggiator()
struct VoiceStart {
	int32_t noteCodePreArp;
	int32_t noteCodePostArp;
	int32_t velocity;
	int16_t const* mpeValues;
	uint32_t sampleSyncLength;
	int32_t ticksLate;
	uint32_t samplesLate;
	int32_t fromMIDIChannel;
	bool invertReversed;               ///< The arpeggiator's reverse flip for this note, see Sound::invertReversed
	deluge::note_locks::NoteRef locks; ///< The note's parameter locks, which its voice plays with
	/// Set for a note that waited: its locks as they were when it was due, used rather than looking `locks` up now
	deluge::note_locks::LockSet const* locksWhenDue = nullptr;
};

/// Start a voice on the Sound now, or queue it if this render can't start another (or the Sound already has notes
/// waiting, which must start first). The only way a Sound starts a voice for a new note
void startVoice(ModelStackWithSoundFlags* modelStack, Sound& sound, VoiceStart const& start);

/// Called by the song tick instead of sending a sequenced note-on to its Output. Returns whether the note was queued;
/// if not, send it as usual
bool tryDefer(InstrumentClip* clip, PendingNoteOn const& note);

/// Start what can start in this render. Call once per render, after the song tick and before rendering
void sendWhatCanStart();

/// The NoteRow's sequenced note is being stopped, or the NoteRow is going away
void forgetNoteRow(NoteRow const* noteRow);

/// The Clip is going away
void forgetClip(InstrumentClip const* clip);

/// A note-off for this post-arpeggiator note code (or all notes) reached the Sound
void forgetNoteOff(Sound const* sound, int32_t noteCodePostArp);

/// Nothing queued for this Sound may start any more: all its notes are cut, choked, or it's going away
void forgetSound(Sound const* sound);

/// Playback stopped: sequenced notes still waiting mustn't start (voice starts from auditioning or MIDI input stay)
void forgetSequenced();

} // namespace DeferredNoteOns
