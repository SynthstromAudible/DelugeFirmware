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

#include "model/clip/arp_freeze.h"
#include "definitions_cxx.hpp"
#include "memory/general_memory_allocator.h"
#include "model/action/action.h"
#include "model/action/action_logger.h"
#include "model/clip/instrument_clip.h"
#include "model/consequence/consequence_arp_freeze_layout.h"
#include "model/consequence/consequence_arp_settings_change.h"
#include "model/consequence/consequence_param_change.h"
#include "model/drum/drum.h"
#include "model/instrument/kit.h"
#include "model/model_stack.h"
#include "model/note/note.h"
#include "model/note/note_row.h"
#include "model/song/song.h"
#include "modulation/arpeggiator.h"
#include "modulation/params/param_set.h"
#include "playback/mode/playback_mode.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"
#include "util/containers.h"
#include "util/exceptions.h"
#include "util/functions.h"
#include "util/misc.h"
#include <algorithm>
#include <array>
#include <limits>
#include <numeric>

namespace params = deluge::modulation::params;

namespace arp_freeze {
namespace {

/// Limit for the notes the arpeggiator gets and for the notes it plays, so that a freeze can't use up all the memory
constexpr size_t kMaxNotes = 16384;

/// A note of the clip starting or ending, as the arpeggiator gets it
struct InputEvent {
	int32_t pos;
	int16_t rowIndex;
	uint8_t velocity;
	bool on;
};

struct FrozenNote {
	int32_t pos;
	int32_t length; // Negative while the note is still sounding
	int16_t key;
	uint8_t velocity;
};

/// Collects the notes the arpeggiator plays, from the instructions it returns
class Recorder {
public:
	[[nodiscard]] bool hasTooManyNotes() const { return too_many_notes_; }
	deluge::vector<FrozenNote> takeNotes() { return std::move(notes_); }

	void handle(ArpReturnInstruction& instruction, int32_t pos) {
		for (uint32_t n = 0; n < ARP_MAX_INSTRUCTION_NOTES; n++) {
			if (instruction.glideNoteCodeOffPostArp[n] != ARP_NOTE_NONE) {
				end(instruction.glideNoteCodeOffPostArp[n], pos);
			}
			if (instruction.noteCodeOffPostArp[n] != ARP_NOTE_NONE) {
				end(instruction.noteCodeOffPostArp[n], pos);
			}
		}
		if (instruction.arpNoteOn == nullptr) {
			return;
		}

		// Notes which are still sounding now are being glided from. Gliding is played when notes overlap, so let them
		// overlap the new notes by one tick. If a new note has the same key, the note just keeps sounding instead.
		//
		// keep[] and num_open_before index open_[], which start() would reorder if it had to make room by ending a
		// note. It never has to here: after a note-on only the notes of that chord stay open (the others are ended
		// below), so at most ARP_MAX_INSTRUCTION_NOTES are open now, and adding at most as many more fits.
		std::array<bool, kMaxOpenNotes> keep{};
		int32_t num_open_before = num_open_;
		static_assert(kMaxOpenNotes >= 2 * ARP_MAX_INSTRUCTION_NOTES);
		for (uint32_t n = 0; n < ARP_MAX_INSTRUCTION_NOTES; n++) {
			int16_t key = instruction.arpNoteOn->noteCodeOnPostArp[n];
			if (key == ARP_NOTE_NONE) {
				break;
			}
			// Done what the instrument would do with it
			instruction.arpNoteOn->noteStatus[n] = ArpNoteStatus::PLAYING;

			int32_t i = findOpen(key);
			if (i >= 0 && i < num_open_before) {
				keep[i] = true;
				continue;
			}
			if (i >= 0) {
				continue; // Same key twice in a chord
			}
			start(key, pos, instruction.arpNoteOn->velocity);
		}
		for (int32_t i = num_open_before - 1; i >= 0; i--) {
			if (!keep[i]) {
				endAt(i, pos + 1);
			}
		}
	}

	void endAll(int32_t pos) {
		while (num_open_ > 0) {
			endAt(num_open_ - 1, pos);
		}
	}

private:
	// Enough for a chord and the chord it glides from
	static constexpr int32_t kMaxOpenNotes = ARP_MAX_INSTRUCTION_NOTES * 2;
	deluge::vector<FrozenNote> notes_;
	bool too_many_notes_ = false;
	std::array<size_t, kMaxOpenNotes> open_{};
	int32_t num_open_ = 0;

	[[nodiscard]] int32_t findOpen(int16_t key) const {
		for (int32_t i = 0; i < num_open_; i++) {
			if (notes_[open_[i]].key == key) {
				return i;
			}
		}
		return -1;
	}

	void start(int16_t key, int32_t pos, uint8_t velocity) {
		if (notes_.size() >= kMaxNotes) {
			too_many_notes_ = true;
			return;
		}
		if (num_open_ == kMaxOpenNotes) {
			endAt(0, pos);
		}
		notes_.push_back({pos, -1, key, velocity});
		open_[num_open_++] = notes_.size() - 1;
	}

	void end(int16_t key, int32_t pos) {
		int32_t i = findOpen(key);
		if (i >= 0) {
			endAt(i, pos);
		}
	}

	// NOLINTNEXTLINE(readability-convert-member-functions-to-static): CI partial-parse false positive
	void endAt(int32_t i, int32_t pos) {
		FrozenNote& note = notes_[open_[i]];
		note.length = std::max(pos - note.pos, 1_i32);
		open_[i] = open_[--num_open_];
	}
};

/// Longest clip a freeze can make: rows of different lengths are written out until they line up again
constexpr int64_t kMaxRenderLength = 1 << 20;

/// The audio engine renders at most this many samples at once, which is how often an unsynced arpeggiator moves on
constexpr int32_t kMaxSamplesPerRender = 128;

/// A note of a row sounding, from one pass of the row
struct Interval {
	int32_t start;
	int32_t end;
	uint8_t velocity;
};

bool isKit(InstrumentClip* clip) {
	return clip->output->type == OutputType::KIT;
}

/// Whether the notes of a row go through the arpeggiator of the clip, as in Kit::noteOnPreKitArp()
bool rowFeedsArp(InstrumentClip* clip, int32_t rowIndex, ModelStackWithTimelineCounter* modelStack) {
	NoteRow* note_row = clip->noteRows.getElement(rowIndex);
	if (note_row->muted) {
		return false;
	}
	if (!isKit(clip)) {
		return true;
	}
	Drum* drum = note_row->drum;
	if (drum == nullptr || !drum->arpSettings.includeInKitArp) {
		return false;
	}
	if (drum->type == DrumType::SOUND) {
		auto* sound_drum = static_cast<SoundDrum*>(drum);
		ModelStackWithSoundFlags* model_stack_with_sound_flags =
		    modelStack->addNoteRow(clip->getNoteRowId(note_row, rowIndex), note_row)
		        ->addOtherTwoThings(sound_drum, &note_row->paramManager)
		        ->addSoundFlags();
		return sound_drum->allowNoteTails(model_stack_with_sound_flags, true);
	}
	return true;
}

int32_t getRowLength(InstrumentClip* clip, NoteRow* noteRow) {
	return noteRow->loopLengthIfIndependent != 0 ? noteRow->loopLengthIfIndependent : clip->loopLength;
}

SequenceDirection getRowDirection(InstrumentClip* clip, NoteRow* noteRow) {
	return noteRow->sequenceDirectionMode == SequenceDirection::OBEY_PARENT ? clip->sequenceDirectionMode
	                                                                        : noteRow->sequenceDirectionMode;
}

/// How long it takes until something plays the same again: ping-pong plays forwards then backwards
int64_t getCycleLength(int32_t length, SequenceDirection direction) {
	return direction == SequenceDirection::PINGPONG ? 2 * (int64_t)length : length;
}

/// The length of the frozen clip: until the clip and the rows going through the arpeggiator all start again together.
/// Returns -1 if that's too long.
int32_t getRenderLength(InstrumentClip* clip, deluge::vector<bool> const& rowsFeedingArp) {
	int64_t length = getCycleLength(clip->loopLength, clip->sequenceDirectionMode);
	if (length > kMaxRenderLength) {
		return -1;
	}
	for (int32_t r = 0; r < clip->noteRows.getNumElements(); r++) {
		if (!rowsFeedingArp[r]) {
			continue;
		}
		NoteRow* note_row = clip->noteRows.getElement(r);
		length = std::lcm(length, getCycleLength(getRowLength(clip, note_row), getRowDirection(clip, note_row)));
		if (length > kMaxRenderLength) {
			return -1;
		}
	}
	return (int32_t)length;
}

/// Whether a note plays without fill being held. Fill notes only play while it's held, so they aren't frozen.
bool playsWithoutFill(Note const* note) {
	return note->fill != FillMode::FILL;
}

/// When the notes of a row sound during the render, with the row played as it plays: looping at its own length, and
/// mirrored on the passes it plays backwards (synth notes then sound from their end to their start). Returns false if
/// there would be more than maxIntervals, before merging, so that a row can't use up all the memory.
bool getRowIntervals(InstrumentClip* clip, NoteRow* noteRow, int32_t renderLength, size_t maxIntervals,
                     deluge::vector<Interval>& intervals) {
	int32_t row_length = getRowLength(clip, noteRow);
	SequenceDirection direction = getRowDirection(clip, noteRow);
	int32_t num_passes = renderLength / row_length;

	intervals.clear();

	// A note filling the whole row drones: when playing it's never released as the row loops or changes direction, so
	// the arpeggiator gets it once for the whole render. It's the only note of its row.
	if (noteRow->notes.getNumElements() == 1) {
		Note* note = noteRow->notes.getElement(0);
		if (note->isDrone(row_length) && playsWithoutFill(note)) {
			intervals.push_back({0, renderLength, note->velocity});
			return true;
		}
	}

	// From the pass before the render, for the notes which wrap around into it
	for (int32_t pass = -1; pass <= num_passes; pass++) {
		bool reversed =
		    direction == SequenceDirection::REVERSE || (direction == SequenceDirection::PINGPONG && (pass & 1) != 0);
		int64_t pass_start = (int64_t)pass * row_length;
		for (int32_t i = 0; i < noteRow->notes.getNumElements(); i++) {
			Note* note = noteRow->notes.getElement(i);
			if (!playsWithoutFill(note)) {
				continue;
			}
			int64_t start = reversed ? pass_start + row_length - (note->pos + note->length) : pass_start + note->pos;
			int64_t end = std::min(start + note->length, (int64_t)renderLength);
			start = std::max(start, 0_i64);
			if (start >= end) {
				continue;
			}
			if (intervals.size() >= maxIntervals) {
				return false;
			}
			intervals.push_back({(int32_t)start, (int32_t)end, note->velocity});
		}
	}

	// A note longer than the row overlaps itself: it keeps sounding. Notes which only touch are played again, as when
	// playing.
	std::ranges::sort(intervals, [](Interval const& a, Interval const& b) { return a.start < b.start; });
	size_t num_merged = 0;
	for (Interval const& interval : intervals) {
		if (num_merged > 0 && interval.start < intervals[num_merged - 1].end) {
			intervals[num_merged - 1].end = std::max(intervals[num_merged - 1].end, interval.end);
			continue;
		}
		intervals[num_merged++] = interval;
	}
	intervals.resize(num_merged);
	return true;
}

/// The key a row has for the arpeggiator: the note for synth, MIDI and CV clips, the row index for kits
int16_t getRowKey(InstrumentClip* clip, int32_t rowIndex) {
	return (int16_t)(isKit(clip) ? rowIndex : clip->noteRows.getElement(rowIndex)->y);
}

/// Get the notes of the rows going through the arpeggiator, as it gets them while the clip plays
bool getInputEvents(InstrumentClip* clip, deluge::vector<bool> const& rowsFeedingArp, int32_t renderLength,
                    deluge::vector<InputEvent>& events) {
	deluge::vector<Interval> intervals;
	for (int32_t r = 0; r < clip->noteRows.getNumElements(); r++) {
		if (!rowsFeedingArp[r]) {
			continue;
		}
		// Each interval makes up to two events
		size_t max_intervals = (kMaxNotes * 2 - events.size()) / 2;
		if (!getRowIntervals(clip, clip->noteRows.getElement(r), renderLength, max_intervals, intervals)) {
			return false;
		}
		auto row_index = (int16_t)r;
		for (Interval const& interval : intervals) {
			events.push_back({interval.start, row_index, interval.velocity, true});
			if (interval.end < renderLength) {
				events.push_back({interval.end, row_index, interval.velocity, false});
			}
		}
	}
	// Note-offs before note-ons at the same position, as when playing
	std::ranges::sort(events, [](InputEvent const& a, InputEvent const& b) {
		return a.pos != b.pos ? a.pos < b.pos : (!a.on && b.on);
	});
	return true;
}

struct ClipPos {
	int32_t pos;
	bool reversed;
};

/// Where the clip is and whether it plays backwards, a number of ticks after it started, as Clip::setPos() and
/// Clip::processCurrentPos() work it out. A synced arpeggiator is ticked with that position.
ClipPos getClipPos(int32_t loopLength, SequenceDirection direction, int32_t ticks) {
	int32_t pos_in_cycle = ticks % (int32_t)getCycleLength(loopLength, direction);
	bool reversed = direction == SequenceDirection::REVERSE
	                || (direction == SequenceDirection::PINGPONG && pos_in_cycle >= loopLength);
	if (direction == SequenceDirection::PINGPONG && reversed) {
		pos_in_cycle -= loopLength;
	}
	// Playing backwards counts down from the end, which is at position 0
	return {.pos = (reversed && pos_in_cycle != 0) ? loopLength - pos_in_cycle : pos_in_cycle, .reversed = reversed};
}

/// The expression values a row's notes get when they start, as NoteRow::playNote() gives them (used by the arp's MPE
/// velocity). These are the values where the row is now: expression automation isn't followed.
using MPEValues = std::array<int16_t, kNumExpressionDimensions>;
deluge::vector<MPEValues> getRowMPEValues(InstrumentClip* clip) {
	std::array<char, MODEL_STACK_MAX_SIZE> model_stack_memory{};
	ModelStackWithTimelineCounter* model_stack =
	    setupModelStackWithTimelineCounter(model_stack_memory.data(), currentSong, clip);
	deluge::vector<MPEValues> values(clip->noteRows.getNumElements());
	for (int32_t r = 0; r < clip->noteRows.getNumElements(); r++) {
		NoteRow* note_row = clip->noteRows.getElement(r);
		note_row->getMPEValues(model_stack->addNoteRow(clip->getNoteRowId(note_row, r), note_row), values[r].data());
	}
	return values;
}

/// render() is run once per tick for the gate of a synced arpeggiator, as at this fraction of a tick before the next
/// tick: what the gate does during a tick is put at its start. A gate of 50 % or 100 % ends a hair before a tick, and
/// is put at that tick, as it sounds.
constexpr uint64_t kGateSubticks = 16;

/// Run the arpeggiator over the notes it gets, tick by tick
Result runArpeggiator(Arpeggiator& arpeggiator, ArpeggiatorSettings& settings, uint32_t gateThreshold,
                      uint32_t phaseIncrement, InstrumentClip* clip, deluge::vector<InputEvent> const& events,
                      int32_t renderLength, deluge::vector<FrozenNote>& frozenNotes) {
	bool synced = settings.syncLevel != SYNC_LEVEL_NONE;
	uint64_t ticks_per_period = synced ? settings.getTicksPerPeriod() : 1;
	// An unsynced arpeggiator runs in samples, at the current tempo
	float samples_per_tick = playbackHandler.getTimePerInternalTickFloat();
	float samples_to_render = 0;

	arpeggiator.setSimulateClock(true);
	Recorder recorder;
	deluge::vector<MPEValues> mpe_values = getRowMPEValues(clip);
	size_t e = 0;
	int32_t gate_start = 0;

	for (int32_t pos = 0; pos < renderLength; pos++) {
		// Long clips take a while, keep the audio going
		if ((pos & 1023) == 1023) {
			AudioEngine::routineWithClusterLoading();
		}

		// The notes of the clip first, then the arpeggiator, as in Session::doTickForward()
		for (; e < events.size() && events[e].pos == pos; e++) {
			ArpReturnInstruction instruction;
			int16_t key = getRowKey(clip, events[e].rowIndex);
			if (events[e].on) {
				arpeggiator.noteOn(&settings, key, events[e].velocity, &instruction, MIDI_CHANNEL_NONE,
				                   mpe_values[events[e].rowIndex].data());
			}
			else {
				arpeggiator.noteOff(&settings, key, &instruction);
			}
			recorder.handle(instruction, pos);
		}

		// Ticked with the position of the clip, as in Session::doTickForward(): its steps line up with the clip (also
		// when the clip plays backwards), and the locked randomizer starts again where the clip does. Unsynced, this
		// only does the latter.
		ClipPos clip_pos = getClipPos(clip->loopLength, clip->sequenceDirectionMode, pos);
		ArpReturnInstruction tick_instruction;
		arpeggiator.doTickForward(&settings, &tick_instruction, clip_pos.pos, clip_pos.reversed);
		if (tick_instruction.arpNoteOn != nullptr) {
			gate_start = pos; // switchNoteOn() reset gatePos
		}
		recorder.handle(tick_instruction, pos);

		if (synced) {
			// What render() does with the gate (note-offs and ratchets) during this tick. When playing it runs per
			// audio buffer, here it's run once with gatePos as it is just before the next tick (see kGateSubticks).
			ArpReturnInstruction gate_instruction;
			uint64_t gate_time = ((uint64_t)(pos - gate_start) * kGateSubticks) + kGateSubticks - 1;
			arpeggiator.gatePos = (uint32_t)std::min<uint64_t>((gate_time << 24) / (ticks_per_period * kGateSubticks),
			                                                   std::numeric_limits<uint32_t>::max());
			arpeggiator.render(&settings, &gate_instruction, 0, gateThreshold, 0);
			recorder.handle(gate_instruction, pos);
		}
		else {
			// Unsynced, render() plays the notes too, buffer by buffer
			samples_to_render += samples_per_tick;
			while (samples_to_render >= 1) {
				int32_t num_samples = std::min((int32_t)samples_to_render, kMaxSamplesPerRender);
				ArpReturnInstruction instruction;
				arpeggiator.render(&settings, &instruction, num_samples, gateThreshold, phaseIncrement);
				recorder.handle(instruction, pos);
				samples_to_render -= (float)num_samples;
			}
		}

		if (recorder.hasTooManyNotes()) {
			return Result::TOO_MANY_NOTES;
		}
	}
	recorder.endAll(renderLength);

	frozenNotes = recorder.takeNotes();
	return Result::OK;
}

/// Run the arpeggiator over the clip, as it runs when the clip plays from its start
Result render(InstrumentClip* clip, deluge::vector<bool> const& rowsFeedingArp, int32_t renderLength,
              deluge::vector<FrozenNote>& frozenNotes) {
	deluge::vector<InputEvent> events;
	if (!getInputEvents(clip, rowsFeedingArp, renderLength, events)) {
		return Result::TOO_MANY_NOTES;
	}

	// A copy, so that rendering doesn't change the randomizer state of the clip. The parameters are taken as
	// Sound::render(), Kit::setupAndRenderArpPreOutput() and NonAudioInstrument::renderOutput() take them.
	ArpeggiatorSettings settings = clip->arpSettings;
	uint32_t gate_threshold = 0;
	int32_t rate = 0;
	OutputType type = clip->output->type;
	if (type == OutputType::SYNTH || type == OutputType::KIT) {
		UnpatchedParamSet* unpatched_params = clip->paramManager.getUnpatchedParamSet();
		settings.updateParamsFromUnpatchedParamSet(unpatched_params);
		gate_threshold = (uint32_t)unpatched_params->getValue(params::UNPATCHED_ARP_GATE) + 2147483648;
		if (type == OutputType::SYNTH) {
			// The clip's own value, without modulation, as the patcher makes it with no patch cables
			rate = getFinalParameterValueExp(
			    paramNeutralValues[params::GLOBAL_ARP_RATE],
			    cableToExpParamShortcut(clip->paramManager.getPatchedParamSet()->getValue(params::GLOBAL_ARP_RATE)));
		}
		else {
			rate = getFinalParameterValueExp(
			    paramNeutralValues[params::GLOBAL_ARP_RATE],
			    cableToExpParamShortcut(unpatched_params->getValue(params::UNPATCHED_ARP_RATE)));
			// Not supported by the kit arpeggiator
			settings.chordPolyphony = 0;
			settings.chordProbability = 0;
			settings.spreadOctave = 0;
		}
	}
	else {
		gate_threshold = (uint32_t)settings.gate + 2147483648;
		rate = getFinalParameterValueExp(paramNeutralValues[params::GLOBAL_ARP_RATE],
		                                 cableToExpParamShortcut(settings.rate));
	}
	uint32_t phase_increment = settings.getPhaseIncrement(rate);

	if (isKit(clip)) {
		ArpeggiatorForKit arpeggiator;
		return runArpeggiator(arpeggiator, settings, gate_threshold, phase_increment, clip, events, renderLength,
		                      frozenNotes);
	}
	Arpeggiator arpeggiator;
	return runArpeggiator(arpeggiator, settings, gate_threshold, phase_increment, clip, events, renderLength,
	                      frozenNotes);
}

/// Make the clip play forwards at its new length, its rows which were frozen following it, and the other rows playing
/// as before
void unrollLayout(InstrumentClip* clip, deluge::vector<bool> const& rowsFrozen, int32_t newLoopLength) {
	SequenceDirection old_direction = clip->sequenceDirectionMode;
	for (int32_t r = 0; r < clip->noteRows.getNumElements(); r++) {
		NoteRow* note_row = clip->noteRows.getElement(r);
		if (rowsFrozen[r]) {
			note_row->loopLengthIfIndependent = 0;
			note_row->sequenceDirectionMode = SequenceDirection::OBEY_PARENT;
			continue;
		}
		if (note_row->loopLengthIfIndependent == 0 && newLoopLength != clip->loopLength) {
			note_row->loopLengthIfIndependent = clip->loopLength;
		}
		if (note_row->sequenceDirectionMode == SequenceDirection::OBEY_PARENT
		    && old_direction != SequenceDirection::FORWARD) {
			note_row->sequenceDirectionMode = old_direction;
		}
	}
	clip->sequenceDirectionMode = SequenceDirection::FORWARD;
	clip->currentlyPlayingReversed = false;

	// Once the rows have their own lengths. Playback is resynced and resumed afterwards.
	if (newLoopLength != clip->loopLength) {
		setLoopLength(clip, newLoopLength);
	}
}

/// Sort the frozen notes by row and position, and shorten them so that they don't overlap in their row or go past the
/// end of the clip. Notes left with no length aren't written.
void fitNotes(deluge::vector<FrozenNote>& frozenNotes, int32_t loopLength) {
	std::ranges::sort(frozenNotes, [](FrozenNote const& a, FrozenNote const& b) {
		return a.key != b.key ? a.key < b.key : a.pos < b.pos;
	});
	for (size_t i = 0; i < frozenNotes.size(); i++) {
		FrozenNote& note = frozenNotes[i];
		note.length = std::min(note.length, loopLength - note.pos);
		if (i + 1 < frozenNotes.size() && frozenNotes[i + 1].key == note.key) {
			note.length = std::min(note.length, frozenNotes[i + 1].pos - note.pos);
		}
	}
}

/// Whether a muted row is in the way of a note the arpeggiator plays, for synth, MIDI and CV clips (e.g. an octave
/// above a note, on a muted row). The note would be silent there.
bool isMutedRowInTheWay(InstrumentClip* clip, deluge::vector<FrozenNote> const& frozenNotes) {
	for (FrozenNote const& note : frozenNotes) {
		if (note.length < 1) {
			continue;
		}
		NoteRow* note_row = clip->getNoteRowForYNote(note.key);
		if (note_row != nullptr && note_row->muted) {
			return true;
		}
	}
	return false;
}

/// The row the kit arpeggiator plays for a key, if there's one: as in Kit::doTickForwardForArp(), a key past the last
/// row or on a row without a drum plays nothing.
NoteRow* getKitRowPlayed(InstrumentClip* clip, int16_t key) {
	if (key < 0 || key >= clip->noteRows.getNumElements()) {
		return nullptr;
	}
	NoteRow* note_row = clip->noteRows.getElement(key);
	return note_row->drum != nullptr ? note_row : nullptr;
}

/// Whether the kit arpeggiator plays a row which isn't frozen, as it doesn't go to the kit arpeggiator (e.g. reached
/// through the octaves, or a muted row). Its notes there couldn't be written without changing the row.
bool isUnfrozenKitRowPlayed(InstrumentClip* clip, deluge::vector<bool> const& rowsFrozen,
                            deluge::vector<FrozenNote> const& frozenNotes) {
	return std::ranges::any_of(frozenNotes, [&](FrozenNote const& note) {
		return note.length >= 1 && getKitRowPlayed(clip, note.key) != nullptr && !rowsFrozen[note.key];
	});
}

/// Whether the freeze changes the layout of the clip: its length or direction, or the length or direction of a row
/// which is frozen
bool changesLayout(InstrumentClip* clip, deluge::vector<bool> const& rowsFrozen, int32_t renderLength) {
	if (renderLength != clip->loopLength || clip->sequenceDirectionMode != SequenceDirection::FORWARD) {
		return true;
	}
	for (int32_t r = 0; r < clip->noteRows.getNumElements(); r++) {
		NoteRow* note_row = clip->noteRows.getElement(r);
		if (rowsFrozen[r]
		    && (note_row->loopLengthIfIndependent != 0
		        || note_row->sequenceDirectionMode != SequenceDirection::OBEY_PARENT)) {
			return true;
		}
	}
	return false;
}

/// Whether the clip, or a row which is frozen, has automation. It isn't written out when the layout changes.
bool hasAutomation(InstrumentClip* clip, deluge::vector<bool> const& rowsFrozen) {
	if (clip->paramManager.mightContainAutomation()) {
		return true;
	}
	for (int32_t r = 0; r < clip->noteRows.getNumElements(); r++) {
		if (rowsFrozen[r] && clip->noteRows.getElement(r)->paramManager.mightContainAutomation()) {
			return true;
		}
	}
	return false;
}

/// Clear the rows which are frozen by moving all their notes into the action, as NoteRow::clearArea() does with the
/// notes it keeps. Returns false if it ran out of memory.
bool clearFrozenRows(InstrumentClip* clip, deluge::vector<bool> const& rowsFrozen, Action* action) {
	for (int32_t r = 0; r < clip->noteRows.getNumElements(); r++) {
		if (!rowsFrozen[r]) {
			continue;
		}
		NoteRow* note_row = clip->noteRows.getElement(r);
		if (action->recordNoteArrayChangeDefinitely(clip, clip->getNoteRowId(note_row, r), &note_row->notes, true)
		    != Error::NONE) {
			return false;
		}
		clip->expectEvent();
	}
	return true;
}

/// Write the frozen notes (from fitNotes()), once the rows going through the arpeggiator are cleared. Returns false if
/// it ran out of memory, with what it changed until then recorded in the action.
bool writeNotes(InstrumentClip* clip, deluge::vector<FrozenNote> const& frozenNotes, Action* action) {
	std::array<char, MODEL_STACK_MAX_SIZE> model_stack_memory{};
	ModelStackWithTimelineCounter* model_stack =
	    setupModelStackWithTimelineCounter(model_stack_memory.data(), currentSong, clip);

	NoteRow* snapshotted_row = nullptr;
	for (size_t i = 0; i < frozenNotes.size(); i++) {
		// Many notes take a while, keep the audio going
		if ((i & 1023) == 1023) {
			AudioEngine::routineWithClusterLoading();
		}

		FrozenNote const& note = frozenNotes[i];
		if (note.length < 1) {
			continue;
		}

		ModelStackWithNoteRow* model_stack_with_note_row = nullptr;
		NoteRow* note_row = nullptr;
		if (isKit(clip)) {
			// Only frozen rows are played (isUnfrozenKitRowPlayed()), or nothing
			note_row = getKitRowPlayed(clip, note.key);
			if (note_row == nullptr) {
				continue;
			}
			model_stack_with_note_row = model_stack->addNoteRow(clip->getNoteRowId(note_row, note.key), note_row);
		}
		else {
			model_stack_with_note_row = clip->getOrCreateNoteRowForYNote(note.key, model_stack, action, nullptr);
			note_row = model_stack_with_note_row->getNoteRowAllowNull();
			if (note_row == nullptr) {
				return false;
			}
		}

		// Each row is snapshotted for undo once (new rows too), rather than each note added. The notes are sorted by
		// row, so this only needs doing when the row changes.
		if (note_row != snapshotted_row) {
			if (action->recordNoteArrayChangeIfNotAlreadySnapshotted(clip, model_stack_with_note_row->noteRowId,
			                                                         &note_row->notes, true)
			    != Error::NONE) {
				return false;
			}
			snapshotted_row = note_row;
		}

		if (note_row->attemptNoteAdd(note.pos, note.length, std::clamp<int32_t>(note.velocity, 1, 127),
		                             kNumProbabilityValues, kDefaultIteranceValue, FillMode::OFF,
		                             model_stack_with_note_row, nullptr)
		    == 0) {
			return false; // The notes don't overlap, so it ran out of memory
		}
	}
	return true;
}

/// Make the randomizer settings which still apply with the arpeggiator off (in Arpeggiator::noteOn()) have no effect,
/// so that the frozen notes play as they were frozen. For synths and kits they're parameters of the clip, for MIDI and
/// CV clips they're in its arp settings (recorded by ConsequenceArpSettingsChange). Returns false if it ran out of
/// memory.
bool resetRandomizer(InstrumentClip* clip, Action* action) {
	clip->arpSettings.noteProbability = std::numeric_limits<uint32_t>::max();
	clip->arpSettings.spreadVelocity = 0;
	clip->arpSettings.reverseProbability = 0;
	if (clip->output->type != OutputType::SYNTH && clip->output->type != OutputType::KIT) {
		return true;
	}

	struct NeutralValue {
		int32_t param;
		int32_t value;
	};
	constexpr std::array<NeutralValue, 3> neutral_values{{
	    {.param = params::UNPATCHED_NOTE_PROBABILITY, .value = std::numeric_limits<int32_t>::max()},
	    {.param = params::UNPATCHED_SPREAD_VELOCITY, .value = std::numeric_limits<int32_t>::min()},
	    {.param = params::UNPATCHED_REVERSE_PROBABILITY, .value = std::numeric_limits<int32_t>::min()},
	}};
	std::array<char, MODEL_STACK_MAX_SIZE> model_stack_memory{};
	ModelStackWithThreeMainThings* model_stack = setupModelStackWithThreeMainThingsButNoNoteRow(
	    model_stack_memory.data(), currentSong, clip->output->toModControllable(), clip, &clip->paramManager);
	for (NeutralValue const& neutral : neutral_values) {
		ModelStackWithAutoParam* model_stack_with_param = model_stack->getUnpatchedAutoParamFromId(neutral.param);
		if (model_stack_with_param == nullptr || model_stack_with_param->autoParam == nullptr
		    || model_stack_with_param->autoParam->getCurrentValue() == neutral.value) {
			continue;
		}
		// Only the current value is set: automation, which isn't frozen either, stays as it is
		void* consequence_memory = GeneralMemoryAllocator::get().allocLowSpeed(sizeof(ConsequenceParamChange));
		if (consequence_memory == nullptr) {
			return false;
		}
		// NOLINTNEXTLINE(cppcoreguidelines-owning-memory): the action owns and destroys its consequences
		action->addConsequence(new (consequence_memory) ConsequenceParamChange(model_stack_with_param, false));
		model_stack_with_param->autoParam->setCurrentValueWithNoReversionOrRecording(model_stack_with_param,
		                                                                             neutral.value);
	}
	return true;
}

/// freeze() once recording was checked. Throws deluge::exception::BAD_ALLOC if a vector runs out of memory, which can
/// only happen before anything is changed.
Result freezeClip(InstrumentClip* clip) {
	std::array<char, MODEL_STACK_MAX_SIZE> model_stack_memory{};
	ModelStackWithTimelineCounter* model_stack =
	    setupModelStackWithTimelineCounter(model_stack_memory.data(), currentSong, clip);
	deluge::vector<bool> rows_feeding_arp(clip->noteRows.getNumElements());
	for (int32_t r = 0; r < clip->noteRows.getNumElements(); r++) {
		rows_feeding_arp[r] = rowFeedsArp(clip, r, model_stack);
	}

	// NOLINTNEXTLINE(cppcoreguidelines-init-variables): CI partial-parse false positive
	int32_t render_length = getRenderLength(clip, rows_feeding_arp);
	if (render_length < 0) {
		return Result::TOO_LONG;
	}
	// Automation would have to be written out like the notes, which isn't done
	if (changesLayout(clip, rows_feeding_arp, render_length) && hasAutomation(clip, rows_feeding_arp)) {
		return Result::AUTOMATION;
	}

	deluge::vector<FrozenNote> frozen_notes;
	// NOLINTNEXTLINE(cppcoreguidelines-init-variables): CI partial-parse false positive
	Result result = render(clip, rows_feeding_arp, render_length, frozen_notes);
	if (result != Result::OK) {
		return result;
	}
	fitNotes(frozen_notes, render_length);
	if (isKit(clip) ? isUnfrozenKitRowPlayed(clip, rows_feeding_arp, frozen_notes)
	                : isMutedRowInTheWay(clip, frozen_notes)) {
		return isKit(clip) ? Result::UNFROZEN_ROW : Result::MUTED_ROW;
	}

	// The consequences first, so that running out of memory leaves no action behind. The layout is snapshotted before
	// anything changes.
	void* layout_memory = GeneralMemoryAllocator::get().allocLowSpeed(sizeof(ConsequenceArpFreezeLayout));
	if (layout_memory == nullptr) {
		return Result::NO_MEMORY;
	}
	ConsequenceArpFreezeLayout* layout = nullptr;
	try {
		// NOLINTNEXTLINE(cppcoreguidelines-owning-memory): the action owns and destroys its consequences
		layout = new (layout_memory) ConsequenceArpFreezeLayout(clip);
	} catch (deluge::exception e) {
		delugeDealloc(layout_memory);
		throw;
	}
	void* settings_memory = GeneralMemoryAllocator::get().allocLowSpeed(sizeof(ConsequenceArpSettingsChange));
	Action* action = nullptr;
	if (settings_memory != nullptr) {
		action = actionLogger.getNewAction(ActionType::MISC, ActionAddition::NOT_ALLOWED);
	}
	if (action == nullptr) {
		layout->~ConsequenceArpFreezeLayout();
		delugeDealloc(layout_memory);
		if (settings_memory != nullptr) {
			delugeDealloc(settings_memory);
		}
		return Result::NO_MEMORY;
	}

	// Undo reverts in reverse order: the notes come back before the layout, so that they aren't cut when the clip gets
	// shorter, and the arpeggiator change is reverted last when redoing, so that it can start the frozen notes again
	action->addConsequence(layout);
	// NOLINTNEXTLINE(cppcoreguidelines-owning-memory): the action owns and destroys its consequences
	auto* settings = new (settings_memory) ConsequenceArpSettingsChange(clip);
	int64_t play_pos = getPlayPos(clip);

	// The arpeggiator is switched off first, so that it can't start notes again while the clip changes
	stopNotesForArpChange(clip);
	clip->arpSettings.preset = ArpPreset::OFF;
	clip->arpSettings.mode = ArpMode::OFF;
	clip->arpSettings.flagForceArpRestart = true;

	// The notes going to the arpeggiator mustn't sound without it, while writing many notes keeps the audio going: the
	// rows are cleared first, and playback is only resumed once the frozen notes are written. Until then the clip is
	// kept in place.
	bool done = resetRandomizer(clip, action);
	done = done && clearFrozenRows(clip, rows_feeding_arp, action);
	if (done) {
		unrollLayout(clip, rows_feeding_arp, render_length);
		resyncPlayback(clip, play_pos, false);
		done = writeNotes(clip, frozen_notes, action);
	}
	action->addConsequence(settings);
	actionLogger.closeAction(ActionType::MISC);

	if (!done) {
		// Undo what was done, so that the clip is left as it was
		if (actionLogger.firstAction[BEFORE] == action) {
			actionLogger.revert(BEFORE, false, false);
			actionLogger.deleteLog(AFTER);
		}
		return Result::NO_MEMORY;
	}

	resumePlayback(clip);
	return Result::OK;
}

} // namespace

bool isAvailable(Clip* clip) {
	if (clip == nullptr || clip->type != ClipType::INSTRUMENT) {
		return false;
	}
	return static_cast<InstrumentClip*>(clip)->arpSettings.mode != ArpMode::OFF;
}

Result freeze(InstrumentClip* clip) {
	if (clip->getCurrentlyRecordingLinearly() || playbackHandler.isCurrentlyRecording()) {
		return Result::RECORDING;
	}
	try {
		return freezeClip(clip);
	} catch (deluge::exception e) {
		if (e != deluge::exception::BAD_ALLOC) {
			throw;
		}
		return Result::NO_MEMORY;
	}
}

void stopNotesForArpChange(InstrumentClip* clip) {
	if (!clip->isActiveOnOutput()) {
		return;
	}
	std::array<char, MODEL_STACK_MAX_SIZE> model_stack_memory{};
	switch (clip->output->type) {
	case OutputType::SYNTH: {
		// Not playing the notes any more, so that resuming playback starts them again
		clip->stopAllNotesPlaying(setupModelStackWithTimelineCounter(model_stack_memory.data(), currentSong, clip));
		auto* sound = static_cast<SoundInstrument*>(clip->output);
		ModelStackWithSoundFlags* model_stack =
		    setupModelStackWithThreeMainThingsButNoNoteRow(model_stack_memory.data(), currentSong, sound, clip,
		                                                   &clip->paramManager)
		        ->addSoundFlags();
		sound->allNotesOff(model_stack, sound->getArp());
		sound->reassessRenderSkippingStatus(model_stack);
		break;
	}
	case OutputType::KIT:
		clip->stopAllNotesPlaying(setupModelStackWithTimelineCounter(model_stack_memory.data(), currentSong, clip));
		clip->output->cutAllSound();
		break;
	default:
		clip->stopAllNotesForMIDIOrCV(setupModelStackWithTimelineCounter(model_stack_memory.data(), currentSong, clip));
		break;
	}
}

void setLoopLength(InstrumentClip* clip, int32_t loopLength) {
	currentSong->setClipLength(clip, loopLength, nullptr, false, false);
	// What PlaybackMode::reSyncClip() would do for the clip the external clock is scaled to (which, as for any change
	// to that clip's length, resyncs all clips). Otherwise all that's left to do is set the position of the clip,
	// which resyncPlayback() does.
	if (playbackHandler.isExternalClockActive() && clip == currentSong->getSyncScalingClip()) {
		playbackHandler.resyncInternalTicksToInputTicks(currentSong);
	}
}

int64_t getPlayPos(InstrumentClip* clip) {
	int32_t pos = clip->lastProcessedPos;
	// Playing backwards counts down from the end, which is at position 0
	if (clip->currentlyPlayingReversed && pos != 0) {
		pos = clip->loopLength - pos;
	}
	return ((int64_t)clip->repeatCount * clip->loopLength) + pos;
}

void resyncPlayback(InstrumentClip* clip, int64_t playPos, bool resume) {
	if (!playbackHandler.isEitherClockActive() || !currentSong->isClipActive(clip)) {
		return;
	}
	std::array<char, MODEL_STACK_MAX_SIZE> model_stack_memory{};
	ModelStackWithTimelineCounter* model_stack =
	    setupModelStackWithTimelineCounter(model_stack_memory.data(), currentSong, clip);
	// The notes playing were from the old layout
	clip->stopAllNotesPlaying(model_stack);
	// Not PlaybackMode::reSyncClip(), which would cancel the clip being armed to launch or stop: its position is set
	// below. (setLoopLength() did what it does for the clip the external clock is scaled to.)

	// The clip, and each row with its own length or direction, carries on from the same place as before. This keeps
	// the rows which weren't frozen in time with the clip, and an unrolled clip in the pass it was playing.
	if (playPos > std::numeric_limits<int32_t>::max()) {
		playPos %= clip->loopLength;
	}
	clip->setPos(model_stack, (int32_t)std::max(playPos, -(int64_t)clip->loopLength));
	if (resume) {
		clip->resumePlayback(model_stack);
	}
}

void resumePlayback(InstrumentClip* clip) {
	if (!playbackHandler.isEitherClockActive() || !currentSong->isClipActive(clip)) {
		return;
	}
	std::array<char, MODEL_STACK_MAX_SIZE> model_stack_memory{};
	clip->resumePlayback(setupModelStackWithTimelineCounter(model_stack_memory.data(), currentSong, clip));
}

} // namespace arp_freeze
