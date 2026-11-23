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

#include "model/clip/deferred_note_ons.h"
#include "OSLikeStuff/scheduler_api.h"
#include "definitions.h"
#include "definitions_cxx.hpp"
#include "model/clip/instrument_clip.h"
#include "model/instrument/kit.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/song/song.h"
#include "modulation/arpeggiator.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"
#include <algorithm>
#include <array>
#include <optional>

namespace DeferredNoteOns {
namespace {

enum class Kind : uint8_t {
	VOICE,     ///< A voice start the Sound has decided on (startVoice())
	SEQUENCED, ///< A sequenced note-on held back before it reached its Output (tryDefer())
};

struct DeferredNoteOn {
	Kind kind;
	bool invertReversed; // VOICE
	uint8_t velocity;
	Sound* sound; // The Sound whose voice it will be - for SEQUENCED, the Clip's SoundInstrument or the row's SoundDrum

	// SEQUENCED: the note's NoteRow. The pointer is only compared with the Clip's NoteRows, never followed until it's
	// found among them, and the NoteRow must still have the same id and Drum (rows can move in memory when others are
	// added or deleted; a deleted NoteRow takes its notes out of the queue in its destructor)
	InstrumentClip* clip;
	Output* output; // The Clip's Output when the note was queued
	NoteRow* noteRow;
	Drum* drum;
	int32_t noteRowId;

	// VOICE
	int16_t noteCodePreArp;
	int16_t noteCodePostArp;
	int16_t mpeValues[kNumExpressionDimensions];
	int32_t fromMIDIChannel;
	uint32_t samplesLate;
	uint32_t timeQueued; // AudioEngine::audioSampleTimer when queued, for samplesLate

	// Both
	// The note's parameter locks, and their values when the note was due: it plays with those, whatever is edited
	// while it waits
	deluge::note_locks::NoteRef locks;
	deluge::note_locks::LockSet locksWhenDue;
	uint32_t sampleSyncLength;
	int32_t ticksLate;
	int64_t swungTickQueued; // playbackHandler.lastSwungTickActioned when queued, which ticksLate is relative to
};

// A 20-note chord on a busy Deluge queues most of its notes for a render or two; a few busy kits and synths stepping
// together queue a few times that. If it's ever full, notes start straight away instead, over the per-render limit:
// it's only ever a burst of notes, and a note late by a whole queue's worth of renders would be useless anyway
constexpr int32_t kMaxDeferred = 256;

// Oldest first
PLACE_SDRAM_BSS std::array<DeferredNoteOn, kMaxDeferred> queue;
int32_t numDeferred = 0;

// While sendWhatCanStart() sends one, so that what it sends isn't queued again behind the Sound's later notes
bool sending = false;

template <typename Pred>
void removeIf(Pred pred) {
	if (numDeferred == 0) {
		return;
	}
	auto end = std::remove_if(queue.begin(), queue.begin() + numDeferred, pred);
	numDeferred = static_cast<int32_t>(end - queue.begin());
}

bool anyQueuedFor(Sound const* sound) {
	return std::any_of(queue.begin(), queue.begin() + numDeferred,
	                   [sound](DeferredNoteOn const& d) { return d.sound == sound; });
}

/// Whether a note-on must wait: this render can't start another voice, or the Sound still has notes waiting
bool mustWait(Sound const* sound) {
	return AudioEngine::wouldRefuseVoiceStart() || (!sending && numDeferred > 0 && anyQueuedFor(sound));
}

/// Swung ticks have passed since the note was queued, so it's that much later than its ticksLate said. That's what a
/// time-stretched sample's sync is measured from (Voice::noteOn()), so it stays in sync with the tick it was due on
int32_t ticksLateNow(DeferredNoteOn const& d) {
	int64_t swungTicksSince = playbackHandler.lastSwungTickActioned - d.swungTickQueued;
	return d.ticksLate + static_cast<int32_t>(std::max<int64_t>(swungTicksSince, 0));
}

/// The Sound a sequenced note-on just starts a voice on, with nothing (an arpeggiator, mono / legato voice handling,
/// choke) that depends on the Output seeing it at once - or nullptr if the note must go through the Output now
Sound* soundJustPlayingNote(InstrumentClip* clip, NoteRow* noteRow) {
	Output* output = clip->output;
	if (output == nullptr || !output->inValidState || clip->arpSettings.mode != ArpMode::OFF) {
		return nullptr;
	}
	auto polyphonic = [](PolyphonyMode mode) { return mode == PolyphonyMode::POLY || mode == PolyphonyMode::AUTO; };

	if (output->type == OutputType::SYNTH) {
		auto* sound = static_cast<SoundInstrument*>(output);
		return polyphonic(sound->polyphonic) ? sound : nullptr;
	}

	if (output->type == OutputType::KIT) {
		Drum* drum = noteRow->drum;
		if (drum == nullptr || drum->type != DrumType::SOUND || drum->arpSettings.mode != ArpMode::OFF) {
			return nullptr;
		}
		auto* sound = static_cast<SoundDrum*>(drum);
		return polyphonic(sound->polyphonic) ? sound : nullptr;
	}

	return nullptr;
}

bool clipIsInCurrentSong(InstrumentClip* clip) {
	return currentSong != nullptr
	       && (currentSong->sessionClips.getIndexForClip(clip) >= 0
	           || currentSong->arrangementOnlyClips.getIndexForClip(clip) >= 0);
}

void sendSequenced(DeferredNoteOn const& d) {
	InstrumentClip* clip = d.clip;

	// Anything that ends the note while it's queued takes it out of the queue, so these only catch edits made in the
	// few ms it's queued for
	if (!playbackHandler.isEitherClockActive() || !clipIsInCurrentSong(clip) || clip->output != d.output
	    || clip->output->getActiveClip() != clip) {
		return;
	}
	int32_t noteRowIndex = -1;
	for (int32_t i = 0; i < clip->noteRows.getNumElements(); i++) {
		if (clip->noteRows.getElement(i) == d.noteRow) {
			noteRowIndex = i;
			break;
		}
	}
	if (noteRowIndex < 0) {
		return;
	}
	NoteRow* noteRow = d.noteRow;
	int32_t noteRowId = clip->getNoteRowId(noteRow, noteRowIndex);
	if (noteRow->muted || noteRow->drum != d.drum || noteRowId != d.noteRowId
	    || soundJustPlayingNote(clip, noteRow) != d.sound) {
		return;
	}

	PendingNoteOn noteOn{};
	noteOn.noteRow = noteRow;
	noteOn.noteRowId = noteRowId;
	noteOn.sampleSyncLength = d.sampleSyncLength;
	noteOn.velocity = d.velocity;
	noteOn.ticksLate = ticksLateNow(d);
	noteOn.locks = d.locks;
	noteOn.locksWhenDue = &d.locksWhenDue;

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = setupModelStackWithTimelineCounter(modelStackMemory, currentSong, clip);
	clip->sendPendingNoteOn(modelStack, &noteOn);
}

bool outputIsInCurrentSong(Output const* output) {
	for (Output* thisOutput = currentSong->firstOutput; thisOutput != nullptr; thisOutput = thisOutput->next) {
		if (thisOutput == output) {
			return true;
		}
	}
	return false;
}

/// Whether the Sound is (still) part of the current song. A Sound being torn down - e.g. the old song's, during a song
/// swap, when the audio routine runs between deleting its parts - must not be touched
bool soundIsInCurrentSong(Sound* sound) {
	if (currentSong == nullptr) {
		return false;
	}
	if (!sound->isDrum()) {
		return outputIsInCurrentSong(static_cast<SoundInstrument*>(sound));
	}
	auto* drum = static_cast<SoundDrum*>(sound);
	if (drum->kit == nullptr || !outputIsInCurrentSong(drum->kit)) {
		return false;
	}
	for (Drum* thisDrum = drum->kit->firstDrum; thisDrum != nullptr; thisDrum = thisDrum->next) {
		if (thisDrum == drum) {
			return true;
		}
	}
	return false;
}

/// The ModelStack the Sound renders with - the same one its Output (or the sample preview) would build. nullptr if
/// the Sound currently has nowhere to render from (not in the current song, or e.g. its kit row was just deleted)
ModelStackWithSoundFlags* setupModelStackForSound(void* memory, Sound* sound) {
	if (sound == AudioEngine::sampleForPreview) {
		return setupModelStackWithThreeMainThingsButNoNoteRow(memory, currentSong, sound, nullptr,
		                                                      AudioEngine::paramManagerForSamplePreview)
		    ->addSoundFlags();
	}
	if (!soundIsInCurrentSong(sound)) {
		return nullptr;
	}

	if (!sound->isDrum()) {
		auto* instrument = static_cast<SoundInstrument*>(sound);
		ParamManager* paramManager = instrument->getParamManager(currentSong);
		if (paramManager == nullptr) {
			return nullptr;
		}
		return setupModelStackWithThreeMainThingsButNoNoteRow(memory, currentSong, sound, instrument->getActiveClip(),
		                                                      paramManager)
		    ->addSoundFlags();
	}

	auto* drum = static_cast<SoundDrum*>(sound);
	auto* clip = static_cast<InstrumentClip*>(drum->kit->getActiveClip());
	int32_t noteRowIndex = -1;
	NoteRow* noteRow = (clip != nullptr) ? clip->getNoteRowForDrum(drum, &noteRowIndex) : nullptr;
	if (noteRow != nullptr) {
		return setupModelStackWithThreeMainThingsIncludingNoteRow(memory, currentSong, clip, noteRowIndex, noteRow,
		                                                          sound, &noteRow->paramManager)
		    ->addSoundFlags();
	}
	ParamManager* paramManager = currentSong->getBackedUpParamManagerPreferablyWithClip(drum, nullptr);
	if (paramManager == nullptr) {
		return nullptr;
	}
	return setupModelStackWithThreeMainThingsButNoNoteRow(memory, currentSong, sound, clip, paramManager)
	    ->addSoundFlags();
}

void startVoiceNow(ModelStackWithSoundFlags* modelStack, Sound& sound, VoiceStart const& start) {
	uint32_t startTicks = getSystemTicks();
	sound.invertReversed = start.invertReversed;
	// The voice picks the note's parameter locks up from here (VoiceNoteLocks::takePending)
	std::optional<deluge::note_locks::PendingScope> locks;
	if (start.locksWhenDue != nullptr) {
		locks.emplace(*start.locksWhenDue, start.locks);
	}
	else {
		locks.emplace(modelStack->song->noteLocks, start.locks);
	}
	sound.noteOnPostArpeggiator(modelStack, start.noteCodePreArp, start.noteCodePostArp, start.velocity,
	                            start.mpeValues, start.sampleSyncLength, start.ticksLate, start.samplesLate,
	                            start.fromMIDIChannel);
	// Charge it to the budget, and teach the budget what a start costs. When the queue is full, this may go over it
	AudioEngine::voiceStarted(getSystemTicks() - startTicks);
}

void sendVoice(DeferredNoteOn const& d) {
	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithSoundFlags* modelStack = setupModelStackForSound(modelStackMemory, d.sound);
	if (modelStack == nullptr) {
		return;
	}
	// A late-started sample (samplesLate) carries on from where it would have got to by now. Other notes just start
	// now, as they did when they started from pending
	uint32_t samplesLate = d.samplesLate;
	if (samplesLate != 0) {
		samplesLate += AudioEngine::audioSampleTimer - d.timeQueued;
	}
	startVoiceNow(modelStack, *d.sound,
	              VoiceStart{
	                  .noteCodePreArp = d.noteCodePreArp,
	                  .noteCodePostArp = d.noteCodePostArp,
	                  .velocity = d.velocity,
	                  .mpeValues = d.mpeValues,
	                  .sampleSyncLength = d.sampleSyncLength,
	                  .ticksLate = ticksLateNow(d),
	                  .samplesLate = samplesLate,
	                  .fromMIDIChannel = d.fromMIDIChannel,
	                  .invertReversed = d.invertReversed,
	                  .locks = d.locks,
	                  .locksWhenDue = &d.locksWhenDue,
	              });
}

} // namespace

void startVoice(ModelStackWithSoundFlags* modelStack, Sound& sound, VoiceStart const& start) {
	if (!mustWait(&sound) || numDeferred >= kMaxDeferred) {
		startVoiceNow(modelStack, sound, start);
		return;
	}

	DeferredNoteOn& d = queue[numDeferred++];
	d = DeferredNoteOn{};
	d.kind = Kind::VOICE;
	d.invertReversed = start.invertReversed;
	d.velocity = static_cast<uint8_t>(start.velocity);
	d.sound = &sound;
	d.noteCodePreArp = static_cast<int16_t>(start.noteCodePreArp);
	d.noteCodePostArp = static_cast<int16_t>(start.noteCodePostArp);
	std::copy(start.mpeValues, start.mpeValues + kNumExpressionDimensions, d.mpeValues);
	d.fromMIDIChannel = start.fromMIDIChannel;
	d.locks = start.locks;
	d.locksWhenDue = start.locksWhenDue != nullptr
	                     ? *start.locksWhenDue
	                     : deluge::note_locks::currentLocks(modelStack->song->noteLocks, start.locks);
	d.samplesLate = start.samplesLate;
	d.timeQueued = AudioEngine::audioSampleTimer;
	d.sampleSyncLength = start.sampleSyncLength;
	d.ticksLate = start.ticksLate;
	d.swungTickQueued = playbackHandler.lastSwungTickActioned;
}

bool tryDefer(InstrumentClip* clip, PendingNoteOn const& note) {
	if (numDeferred >= kMaxDeferred) {
		return false;
	}
	Sound* sound = soundJustPlayingNote(clip, note.noteRow);
	if (sound == nullptr || !mustWait(sound)) {
		return false;
	}

	DeferredNoteOn& d = queue[numDeferred++];
	d = DeferredNoteOn{};
	d.kind = Kind::SEQUENCED;
	d.velocity = note.velocity;
	d.sound = sound;
	d.clip = clip;
	d.output = clip->output;
	d.noteRow = note.noteRow;
	d.drum = note.noteRow->drum;
	d.noteRowId = note.noteRowId;
	d.locks = note.locks;
	d.locksWhenDue = deluge::note_locks::currentLocks(currentSong->noteLocks, note.locks);
	d.sampleSyncLength = note.sampleSyncLength;
	d.ticksLate = note.ticksLate;
	d.swungTickQueued = playbackHandler.lastSwungTickActioned;
	return true;
}

void sendWhatCanStart() {
	while (numDeferred > 0 && !AudioEngine::wouldRefuseVoiceStart()) {
		// Take it out first: starting it can end other notes (e.g. mono voice handling), which edits the queue
		DeferredNoteOn d = queue[0];
		std::move(queue.begin() + 1, queue.begin() + numDeferred, queue.begin());
		numDeferred--;

		sending = true;
		if (d.kind == Kind::SEQUENCED) {
			sendSequenced(d);
		}
		else {
			sendVoice(d);
		}
		sending = false;
	}
}

void forgetNoteRow(NoteRow const* noteRow) {
	removeIf([noteRow](DeferredNoteOn const& d) { return d.kind == Kind::SEQUENCED && d.noteRow == noteRow; });
}

void forgetClip(InstrumentClip const* clip) {
	removeIf([clip](DeferredNoteOn const& d) { return d.kind == Kind::SEQUENCED && d.clip == clip; });
}

void forgetNoteOff(Sound const* sound, int32_t noteCodePostArp) {
	if (noteCodePostArp == ALL_NOTES_OFF) {
		forgetSound(sound);
		return;
	}
	removeIf([sound, noteCodePostArp](DeferredNoteOn const& d) {
		return d.kind == Kind::VOICE && d.sound == sound && d.noteCodePostArp == noteCodePostArp;
	});
}

void forgetSound(Sound const* sound) {
	removeIf([sound](DeferredNoteOn const& d) { return d.sound == sound; });
}

void forgetSequenced() {
	removeIf([](DeferredNoteOn const& d) { return d.kind == Kind::SEQUENCED; });
}

} // namespace DeferredNoteOns
