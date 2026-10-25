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
#include "drivers/pic/pic.h"
#include "gui/ui/root_ui.h"
#include "gui/ui/ui.h"
#include "gui/ui_timer_manager.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "memory/general_memory_allocator.h"
#include "model/action/action.h"
#include "model/action/action_logger.h"
#include "model/clip/instrument_clip.h"
#include "model/consequence/consequence_arp_freeze_layout.h"
#include "model/consequence/consequence_arp_settings_change.h"
#include "model/consequence/consequence_note_array_change.h"
#include "model/consequence/consequence_param_change.h"
#include "model/drum/drum.h"
#include "model/instrument/kit.h"
#include "model/instrument/midi_instrument.h"
#include "model/model_stack.h"
#include "model/note/note.h"
#include "model/note/note_row.h"
#include "model/song/song.h"
#include "modulation/arpeggiator.h"
#include "modulation/params/param_set.h"
#include "playback/mode/playback_mode.h"
#include "playback/mode/session.h"
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
#include <span>
#include <tuple>

extern "C" {
#include "RZA1/oled/oled_low_level.h"
}

namespace params = deluge::modulation::params;

namespace arp_freeze {
namespace {

/// Limit for the notes the arpeggiator gets and for the notes it plays, so that a freeze can't use up all the memory
constexpr size_t kMaxNotes = 16384;

/// How long a freeze is worked out before the working animation is shown, so that quick ones don't flash it
constexpr uint32_t kWorkingAnimationDelay = kSampleRate / 4;

/// While a freeze is worked out (see freeze()): when it started, and whether it shows the working animation
uint32_t work_start_time = 0;
bool showing_working_animation = false;

/// Called regularly while a freeze is worked out, which can take a while: keeps the audio going, and once it has taken
/// a while, shows the working animation and keeps it moving. stopWorkingAnimation() removes it.
void keepGoing() {
	AudioEngine::routineWithClusterLoading();
	if (!showing_working_animation) {
		if (AudioEngine::audioSampleTimer - work_start_time < kWorkingAnimationDelay) {
			return;
		}
		// The menu is left (or not) once the freeze is worked out
		uiTimerManager.unsetTimer(TimerName::BACK_MENU_EXIT);
		display->displayLoadingAnimationText("Working");
		showing_working_animation = true;
	}
	// As while a song is loaded
	uiTimerManager.routine();
	if (display->haveOLED()) {
		deluge::hid::display::OLED::sendMainImage();
		oledRoutine();
	}
	PIC::flush();
}

void stopWorkingAnimation() {
	if (showing_working_animation) {
		display->removeLoadingAnimation();
		showing_working_animation = false;
	}
}

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
		// keep[] and num_open_before index open_[], which start() leaves in place: after a note-on only the notes of
		// that chord stay open (the others are ended below), so at most ARP_MAX_INSTRUCTION_NOTES are open now, and
		// adding at most as many more always fits.
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
			return; // Can't happen, see handle(). Not making room, which would reorder open_[].
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

/// The length of the frozen clip: until the clip and the rows with notes going to the arpeggiator all start again
/// together. Returns -1 if that's too long.
int32_t getRenderLength(InstrumentClip* clip, deluge::vector<bool> const& rowsWithArpInput) {
	int64_t length = getCycleLength(clip->loopLength, clip->sequenceDirectionMode);
	if (length > kMaxRenderLength) {
		return -1;
	}
	for (int32_t r = 0; r < clip->noteRows.getNumElements(); r++) {
		if (!rowsWithArpInput[r]) {
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

/// Whether a note plays without fill being held. Fill notes only play while it's held, so they aren't frozen: they're
/// kept as they are. Notes set to play only without fill are frozen as if they always played.
bool playsWithoutFill(Note const* note) {
	return note->fill != FillMode::FILL;
}

/// Whether a row has notes the arpeggiator gets. A row without (empty, or with only fill notes) doesn't take part in
/// the freeze unless the arpeggiator plays notes on it.
bool hasNotesPlayingWithoutFill(NoteRow* noteRow) {
	for (int32_t i = 0; i < noteRow->notes.getNumElements(); i++) {
		if (playsWithoutFill(noteRow->notes.getElement(i))) {
			return true;
		}
	}
	return false;
}

/// Whether a row has fill notes
bool hasFillNotes(NoteRow* noteRow) {
	for (int32_t i = 0; i < noteRow->notes.getNumElements(); i++) {
		if (!playsWithoutFill(noteRow->notes.getElement(i))) {
			return true;
		}
	}
	return false;
}

/// Sort the intervals of a row and merge the overlapping ones: a note longer than the row overlaps itself, it keeps
/// sounding. Notes which only touch are played again, as when playing.
void mergeIntervals(deluge::vector<Interval>& intervals) {
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
	uint32_t num_notes_seen = 0;
	for (int32_t pass = -1; pass <= num_passes; pass++) {
		bool reversed =
		    direction == SequenceDirection::REVERSE || (direction == SequenceDirection::PINGPONG && (pass & 1) != 0);
		int64_t pass_start = (int64_t)pass * row_length;
		for (int32_t i = 0; i < noteRow->notes.getNumElements(); i++) {
			// Short rows with many (fill) notes take a while, keep the audio going
			if ((++num_notes_seen & 1023) == 0) {
				keepGoing();
			}
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

	mergeIntervals(intervals);
	return true;
}

/// The key a row has for the arpeggiator: the note for synth, MIDI and CV clips, the row index for kits
int16_t getRowKey(InstrumentClip* clip, int32_t rowIndex) {
	return (int16_t)(isKit(clip) ? rowIndex : clip->noteRows.getElement(rowIndex)->y);
}

/// Get the notes of the rows going through the arpeggiator, as it gets them while the clip plays
bool getInputEvents(InstrumentClip* clip, deluge::vector<bool> const& rowsWithArpInput, int32_t renderLength,
                    deluge::vector<InputEvent>& events) {
	deluge::vector<Interval> intervals;
	for (int32_t r = 0; r < clip->noteRows.getNumElements(); r++) {
		if (!rowsWithArpInput[r]) {
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
	// In the order InstrumentClip::processCurrentPos() sends them: at the same position the note-offs first, then the
	// note-ons (which wait in its pendingNoteOnList), each by row from the bottom up. The order of note-ons matters to
	// the arpeggiator (e.g. its "as played" note mode).
	std::ranges::sort(events, [](InputEvent const& a, InputEvent const& b) {
		if (a.pos != b.pos) {
			return a.pos < b.pos;
		}
		return a.on != b.on ? !a.on : a.rowIndex < b.rowIndex;
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
			keepGoing();
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
Result render(InstrumentClip* clip, deluge::vector<bool> const& rowsWithArpInput, int32_t renderLength,
              deluge::vector<FrozenNote>& frozenNotes) {
	deluge::vector<InputEvent> events;
	if (!getInputEvents(clip, rowsWithArpInput, renderLength, events)) {
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
	int32_t old_loop_length = clip->loopLength;
	for (int32_t r = 0; r < clip->noteRows.getNumElements(); r++) {
		NoteRow* note_row = clip->noteRows.getElement(r);
		if (rowsFrozen[r]) {
			note_row->loopLengthIfIndependent = 0;
			note_row->sequenceDirectionMode = SequenceDirection::OBEY_PARENT;
			continue;
		}
		if (note_row->loopLengthIfIndependent == 0 && newLoopLength != old_loop_length) {
			note_row->loopLengthIfIndependent = old_loop_length;
		}
		if (note_row->sequenceDirectionMode == SequenceDirection::OBEY_PARENT
		    && old_direction != SequenceDirection::FORWARD) {
			note_row->sequenceDirectionMode = old_direction;
		}
	}
	clip->sequenceDirectionMode = SequenceDirection::FORWARD;
	clip->currentlyPlayingReversed = false;

	// Once the rows have their own lengths. Playback is resynced and resumed once the clip is complete.
	if (newLoopLength != old_loop_length) {
		setLoopLength(clip, newLoopLength);
		// A row as long as the clip now follows it, as Clip::lengthChanged() makes it when a clip gets shorter
		for (int32_t r = 0; r < clip->noteRows.getNumElements(); r++) {
			NoteRow* note_row = clip->noteRows.getElement(r);
			if (note_row->loopLengthIfIndependent == newLoopLength) {
				note_row->loopLengthIfIndependent = 0;
			}
		}
	}
}

/// Sort the frozen notes by row and position, and shorten them so that they don't overlap in their row or go past the
/// end of the clip. Notes left with no length aren't written. Of the notes of a row starting on the same tick (a note
/// started and ended within a tick, then started again), the longest is kept: it's sorted after the others, which are
/// left with no length.
void fitNotes(deluge::vector<FrozenNote>& frozenNotes, int32_t loopLength) {
	std::ranges::sort(frozenNotes, [](FrozenNote const& a, FrozenNote const& b) {
		return std::tie(a.key, a.pos, a.length, a.velocity) < std::tie(b.key, b.pos, b.length, b.velocity);
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

/// The row a note of the kit arpeggiator is written to, if there's one. A key past the last row or on a row without a
/// drum gets no row: such a note isn't written. (When playing, such a note doesn't sound either, but it can hold up
/// the kit arpeggiator, which the render doesn't do.)
NoteRow* getKitRowPlayed(InstrumentClip* clip, int16_t key) {
	if (key < 0 || key >= clip->noteRows.getNumElements()) {
		return nullptr;
	}
	NoteRow* note_row = clip->noteRows.getElement(key);
	return note_row->drum != nullptr ? note_row : nullptr;
}

/// Whether the kit arpeggiator plays a row which doesn't go to it (e.g. reached through the octaves, or a muted row).
/// Its notes there couldn't be written without changing the row.
bool isUnfrozenKitRowPlayed(InstrumentClip* clip, deluge::vector<bool> const& rowsFeedingArp,
                            deluge::vector<FrozenNote> const& frozenNotes) {
	return std::ranges::any_of(frozenNotes, [&](FrozenNote const& note) {
		return note.length >= 1 && getKitRowPlayed(clip, note.key) != nullptr && !rowsFeedingArp[note.key];
	});
}

/// Mark the rows which get frozen notes as frozen too (e.g. an empty row an octave up), so that they follow the clip.
/// New rows for synth, MIDI and CV clips are made when the notes are written, following the clip.
void addRowsPlayed(InstrumentClip* clip, deluge::vector<FrozenNote> const& frozenNotes,
                   deluge::vector<bool>& rowsFrozen) {
	for (FrozenNote const& note : frozenNotes) {
		if (note.length < 1) {
			continue;
		}
		if (isKit(clip)) {
			if (getKitRowPlayed(clip, note.key) != nullptr) {
				rowsFrozen[note.key] = true;
			}
			continue;
		}
		int32_t row_index = 0;
		if (clip->getNoteRowForYNote(note.key, &row_index) != nullptr) {
			rowsFrozen[row_index] = true;
		}
	}
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

/// Whether a frozen row with fill notes would play differently once it follows the frozen clip: its fill notes would
/// have to be written out like the frozen notes, which isn't done
bool wouldFillNotesMove(InstrumentClip* clip, deluge::vector<bool> const& rowsFrozen, int32_t renderLength) {
	for (int32_t r = 0; r < clip->noteRows.getNumElements(); r++) {
		NoteRow* note_row = clip->noteRows.getElement(r);
		if (rowsFrozen[r] && hasFillNotes(note_row)
		    && (getRowLength(clip, note_row) != renderLength
		        || getRowDirection(clip, note_row) != SequenceDirection::FORWARD)) {
			return true;
		}
	}
	return false;
}

/// Whether a fill note, which is kept, is in the way of a frozen note on its row. The rows with fill notes keep their
/// layout (see wouldFillNotesMove()), so they are as long as the frozen clip. Rows which don't exist yet are empty.
bool areFillNotesInTheWay(InstrumentClip* clip, deluge::vector<FrozenNote> const& frozenNotes, int32_t loopLength) {
	return std::ranges::any_of(frozenNotes, [&](FrozenNote const& note) {
		NoteRow* note_row = isKit(clip) ? getKitRowPlayed(clip, note.key) : clip->getNoteRowForYNote(note.key);
		if (note.length < 1 || note_row == nullptr || note_row->notes.getNumElements() == 0) {
			return false;
		}
		NoteVector& notes = note_row->notes;
		// The notes of a row don't overlap, so the ones starting before the frozen note ends also end in that order
		for (int32_t i = notes.search(note.pos + note.length, GREATER_OR_EQUAL) - 1; i >= 0; i--) {
			Note* other = notes.getElement(i);
			if (other->pos + other->length <= note.pos) {
				break;
			}
			if (!playsWithoutFill(other)) {
				return true;
			}
		}
		// The last note can wrap around the end of the row
		Note* last = notes.getElement(notes.getNumElements() - 1);
		return !playsWithoutFill(last) && last->pos + last->length - loopLength > note.pos;
	});
}

/// Whether the notes outside 0-127 play: a MIDI clip doesn't send them (see MidiEngine::sendNote()) unless it sends to
/// an internal destination, synths and CV play them
bool playsNotesOutOfRange(InstrumentClip* clip) {
	return clip->output->type != OutputType::MIDI_OUT || static_cast<MIDIInstrument*>(clip->output)->sendsToInternal();
}

/// Notes outside 0-127 come from chords on high notes: the arpeggiator keeps the note it makes a chord on within 0-127,
/// not the notes it adds above it. For synth, MIDI and CV clips they're left out (given no length) where they're
/// silent when playing. Returns false if they play: they can't be frozen, as they would need rows outside 0-127.
bool leaveOutNotesOutOfRange(InstrumentClip* clip, deluge::vector<FrozenNote>& frozenNotes) {
	if (isKit(clip)) {
		return true; // Its keys are rows
	}
	for (FrozenNote& note : frozenNotes) {
		if (note.length < 1 || (note.key >= 0 && note.key <= kMaxMIDIValue)) {
			continue;
		}
		if (playsNotesOutOfRange(clip)) {
			return false;
		}
		note.length = 0;
	}
	return true;
}

/// Whether a note would need a new row outside the song's scale, in a scale mode clip (synth, MIDI or CV). Making it
/// would change the song's scale, as InstrumentClip::getOrCreateNoteRowForYNote() does, which deletes the empty rows of
/// the clips in scale mode. The arpeggiator plays notes outside the scale where it keeps them within 0-127.
bool needsRowOutsideScale(InstrumentClip* clip, deluge::vector<FrozenNote> const& frozenNotes) {
	if (isKit(clip)) {
		return false;
	}
	return std::ranges::any_of(frozenNotes, [&](FrozenNote const& note) {
		return note.length >= 1 && clip->getNoteRowForYNote(note.key) == nullptr
		       && !currentSong->isYNoteAllowed(note.key, clip->inScaleMode);
	});
}

/// The randomizer parameters of synths and kits which still apply with the arpeggiator off (in Arpeggiator::noteOn()),
/// with the values at which they have no effect
struct NeutralValue {
	int32_t param;
	int32_t value;
};
constexpr std::array<NeutralValue, 3> kRandomizerNeutralValues{{
    {.param = params::UNPATCHED_NOTE_PROBABILITY, .value = std::numeric_limits<int32_t>::max()},
    {.param = params::UNPATCHED_SPREAD_VELOCITY, .value = std::numeric_limits<int32_t>::min()},
    {.param = params::UNPATCHED_REVERSE_PROBABILITY, .value = std::numeric_limits<int32_t>::min()},
}};

bool hasRandomizerParams(InstrumentClip* clip) {
	return clip->output->type == OutputType::SYNTH || clip->output->type == OutputType::KIT;
}

ModelStackWithThreeMainThings* setupRandomizerModelStack(void* memory, InstrumentClip* clip) {
	return setupModelStackWithThreeMainThingsButNoNoteRow(memory, currentSong, clip->output->toModControllable(), clip,
	                                                      &clip->paramManager);
}

/// Whether one of the randomizer parameters which resetRandomizer() resets is automated: the automation would carry on
/// randomizing the frozen notes
bool isRandomizerAutomated(InstrumentClip* clip) {
	if (!hasRandomizerParams(clip)) {
		return false;
	}
	std::array<char, MODEL_STACK_MAX_SIZE> model_stack_memory{};
	ModelStackWithThreeMainThings* model_stack = setupRandomizerModelStack(model_stack_memory.data(), clip);
	return std::ranges::any_of(kRandomizerNeutralValues, [&](NeutralValue const& neutral) {
		ModelStackWithAutoParam* model_stack_with_param = model_stack->getUnpatchedAutoParamFromId(neutral.param);
		return model_stack_with_param != nullptr && model_stack_with_param->autoParam != nullptr
		       && model_stack_with_param->autoParam->isAutomated();
	});
}

/// What can refuse the freeze once the frozen notes are known (from fitNotes()). Leaves out the notes outside 0-127
/// which are silent, and marks the rows getting frozen notes as frozen.
Result checkFrozenNotes(InstrumentClip* clip, deluge::vector<bool> const& rowsFeedingArp,
                        deluge::vector<FrozenNote>& frozenNotes, int32_t renderLength,
                        deluge::vector<bool>& rowsFrozen) {
	if (!leaveOutNotesOutOfRange(clip, frozenNotes)) {
		return Result::OUT_OF_RANGE;
	}
	if (needsRowOutsideScale(clip, frozenNotes)) {
		return Result::OUTSIDE_SCALE;
	}
	if (isKit(clip) ? isUnfrozenKitRowPlayed(clip, rowsFeedingArp, frozenNotes)
	                : isMutedRowInTheWay(clip, frozenNotes)) {
		return isKit(clip) ? Result::UNFROZEN_ROW : Result::MUTED_ROW;
	}

	addRowsPlayed(clip, frozenNotes, rowsFrozen);
	if (changesLayout(clip, rowsFrozen, renderLength) && hasAutomation(clip, rowsFrozen)) {
		return Result::AUTOMATION;
	}
	if (wouldFillNotesMove(clip, rowsFrozen, renderLength) || areFillNotesInTheWay(clip, frozenNotes, renderLength)) {
		return Result::FILL_NOTES;
	}
	return Result::OK;
}

/// Destroy a consequence which wasn't given to an action, as the action would
void destroyConsequence(Consequence* consequence) {
	if (consequence != nullptr) {
		consequence->~Consequence();
		delugeDealloc(consequence);
	}
}

/// Make a consequence. Returns nullptr if it ran out of memory.
template <typename T, typename... Args>
T* makeConsequence(Args const&... args) {
	void* memory = GeneralMemoryAllocator::get().allocLowSpeed(sizeof(T));
	if (memory == nullptr) {
		return nullptr;
	}
	try {
		// NOLINTNEXTLINE(cppcoreguidelines-owning-memory): an action, or PreparedChange, owns and destroys it
		return new (memory) T(args...);
	} catch (deluge::exception e) {
		delugeDealloc(memory);
		throw;
	}
}

/// The consequences recording the freeze, made with everything the change needs before it's made (while the menu is
/// still open), so that making the change can't run out of memory. They're destroyed unless given to the action.
struct PreparedChange {
	PreparedChange() = default;
	PreparedChange(PreparedChange const&) = delete;
	PreparedChange(PreparedChange&&) = delete;
	PreparedChange& operator=(PreparedChange const&) = delete;
	PreparedChange& operator=(PreparedChange&&) = delete;
	~PreparedChange() {
		destroyConsequence(layout);
		destroyConsequence(settings);
		std::ranges::for_each(randomizer, destroyConsequence);
		std::ranges::for_each(rows, destroyConsequence);
	}

	ConsequenceArpFreezeLayout* layout = nullptr;
	ConsequenceArpSettingsChange* settings = nullptr;
	/// For each of kRandomizerNeutralValues which resetRandomizer() changes, the parameter as it is
	std::array<ConsequenceParamChange*, kRandomizerNeutralValues.size()> randomizer{};
	/// For each row getting notes (see prepareRows()), the notes it gets, to be swapped with its notes
	deluge::vector<ConsequenceNoteArrayChange*> rows{};
};

/// Give a prepared consequence to the action
template <typename T>
void addTo(Action* action, T*& consequence) {
	action->addConsequence(consequence);
	consequence = nullptr;
}

/// Prepare resetRandomizer(): record the randomizer parameters it changes. Returns false if it ran out of memory.
bool prepareRandomizerReset(InstrumentClip* clip, PreparedChange& prepared) {
	if (!hasRandomizerParams(clip)) {
		return true;
	}
	std::array<char, MODEL_STACK_MAX_SIZE> model_stack_memory{};
	ModelStackWithThreeMainThings* model_stack = setupRandomizerModelStack(model_stack_memory.data(), clip);
	for (size_t i = 0; i < kRandomizerNeutralValues.size(); i++) {
		ModelStackWithAutoParam* model_stack_with_param =
		    model_stack->getUnpatchedAutoParamFromId(kRandomizerNeutralValues[i].param);
		if (model_stack_with_param == nullptr || model_stack_with_param->autoParam == nullptr
		    || model_stack_with_param->autoParam->getCurrentValue() == kRandomizerNeutralValues[i].value) {
			continue;
		}
		prepared.randomizer[i] = makeConsequence<ConsequenceParamChange>(model_stack_with_param, false);
		if (prepared.randomizer[i] == nullptr) {
			return false;
		}
	}
	return true;
}

/// Make the randomizer settings which still apply with the arpeggiator off (in Arpeggiator::noteOn()) have no effect,
/// so that the frozen notes play as they were frozen. For synths and kits they're parameters of the clip (not
/// automated, see isRandomizerAutomated()), recorded by prepareRandomizerReset(); for MIDI and CV clips they're in its
/// arp settings (recorded by ConsequenceArpSettingsChange).
void resetRandomizer(InstrumentClip* clip, PreparedChange& prepared, Action* action) {
	clip->arpSettings.noteProbability = std::numeric_limits<uint32_t>::max();
	clip->arpSettings.spreadVelocity = 0;
	clip->arpSettings.reverseProbability = 0;
	if (!hasRandomizerParams(clip)) {
		return;
	}

	std::array<char, MODEL_STACK_MAX_SIZE> model_stack_memory{};
	ModelStackWithThreeMainThings* model_stack = setupRandomizerModelStack(model_stack_memory.data(), clip);
	for (size_t i = 0; i < kRandomizerNeutralValues.size(); i++) {
		if (prepared.randomizer[i] == nullptr) {
			continue;
		}
		addTo(action, prepared.randomizer[i]);
		ModelStackWithAutoParam* model_stack_with_param =
		    model_stack->getUnpatchedAutoParamFromId(kRandomizerNeutralValues[i].param);
		model_stack_with_param->autoParam->setCurrentValueWithNoReversionOrRecording(model_stack_with_param,
		                                                                             kRandomizerNeutralValues[i].value);
	}
}

/// The notes a row gets: its fill notes, kept as they are (noteRow is nullptr for a new row), and the frozen notes of
/// its key (sorted by fitNotes()), which don't overlap them (see areFillNotesInTheWay()). Returns false if it ran out
/// of memory.
bool getRowNotes(NoteRow* noteRow, std::span<FrozenNote const> frozenNotes, NoteVector& notes) {
	int32_t num_row_notes = noteRow != nullptr ? noteRow->notes.getNumElements() : 0;
	auto next_fill_note = [&](int32_t i) {
		while (i < num_row_notes && playsWithoutFill(noteRow->notes.getElement(i))) {
			i++;
		}
		return i;
	};
	auto next_frozen_note = [&](size_t i) {
		while (i < frozenNotes.size() && frozenNotes[i].length < 1) {
			i++;
		}
		return i;
	};

	int32_t num_notes = 0;
	for (int32_t i = next_fill_note(0); i < num_row_notes; i = next_fill_note(i + 1)) {
		num_notes++;
	}
	for (size_t i = next_frozen_note(0); i < frozenNotes.size(); i = next_frozen_note(i + 1)) {
		num_notes++;
	}
	if (num_notes == 0) {
		return true;
	}
	if (notes.insertAtIndex(0, num_notes) != Error::NONE) {
		return false;
	}

	// The fill notes and the frozen notes, merged by position
	int32_t fill = next_fill_note(0);
	size_t frozen = next_frozen_note(0);
	for (int32_t n = 0; n < num_notes; n++) {
		Note* note = notes.getElement(n);
		if (fill < num_row_notes
		    && (frozen == frozenNotes.size() || noteRow->notes.getElement(fill)->pos < frozenNotes[frozen].pos)) {
			*note = *noteRow->notes.getElement(fill);
			fill = next_fill_note(fill + 1);
			continue;
		}
		// As NoteRow::attemptNoteAdd() makes it
		FrozenNote const& frozen_note = frozenNotes[frozen];
		note->pos = frozen_note.pos;
		note->setLength(frozen_note.length);
		note->setVelocity(std::clamp<int32_t>(frozen_note.velocity, 1, 127));
		note->setLift(kDefaultLiftValue);
		note->setProbability(kNumProbabilityValues);
		note->setIterance(kDefaultIteranceValue);
		note->setFill(FillMode::OFF);
		frozen = next_frozen_note(frozen + 1);
	}
	return true;
}

/// Prepare the notes of the frozen rows (from checkFrozenNotes()) and of the new rows, for the notes on pitches without
/// a row: for each, a consequence holding the notes it gets, and room for the new rows. Returns false if it ran out of
/// memory. Throws deluge::exception::BAD_ALLOC if a vector does.
bool prepareRows(InstrumentClip* clip, deluge::vector<bool> const& rowsFrozen,
                 deluge::vector<FrozenNote> const& frozenNotes, PreparedChange& prepared) {
	// The keys of the new rows. For kits only frozen rows get notes (see addRowsPlayed()).
	deluge::vector<int16_t> new_keys;
	if (!isKit(clip)) {
		for (FrozenNote const& note : frozenNotes) {
			if (note.length >= 1 && (new_keys.empty() || new_keys.back() != note.key)
			    && clip->getNoteRowForYNote(note.key) == nullptr) {
				new_keys.push_back(note.key);
			}
		}
	}
	if (!new_keys.empty() && !clip->noteRows.ensureEnoughSpaceAllocated((int32_t)new_keys.size())) {
		return false;
	}
	prepared.rows.reserve((size_t)std::ranges::count(rowsFrozen, true) + new_keys.size());

	size_t num_notes_prepared = 0;
	auto prepare_row = [&](NoteRow* note_row, int32_t note_row_id, int16_t key) {
		auto [first, last] = std::ranges::equal_range(frozenNotes, key, std::less{}, &FrozenNote::key);
		NoteVector notes;
		if (!getRowNotes(note_row, {first, last}, notes)) {
			return false;
		}
		// Takes the notes
		auto* change = makeConsequence<ConsequenceNoteArrayChange>(clip, note_row_id, &notes, true);
		if (change == nullptr) {
			return false;
		}
		prepared.rows.push_back(change); // Reserved
		// Many notes take a while, keep the audio going
		num_notes_prepared += change->backedUpNoteVector.getNumElements();
		if (num_notes_prepared >= 1024) {
			keepGoing();
			num_notes_prepared = 0;
		}
		return true;
	};
	for (int32_t r = 0; r < clip->noteRows.getNumElements(); r++) {
		NoteRow* note_row = clip->noteRows.getElement(r);
		if (rowsFrozen[r] && !prepare_row(note_row, clip->getNoteRowId(note_row, r), getRowKey(clip, r))) {
			return false;
		}
	}
	// The id of a new row is its note
	return std::ranges::all_of(new_keys, [&](int16_t key) { return prepare_row(nullptr, key, key); });
}

/// Give the rows their notes (see prepareRows()), recording it in the action. Only swaps the notes, and makes the new
/// rows, which there's room for.
void writeRows(InstrumentClip* clip, PreparedChange& prepared, Action* action) {
	for (ConsequenceNoteArrayChange*& change : prepared.rows) {
		// Makes the row if it's new
		NoteRow* note_row = clip->getNoteRowFromId(change->noteRowId);
		if (note_row == nullptr) {
			continue; // Can't happen, there's room for the new rows
		}
		note_row->notes.swapStateWith(&change->backedUpNoteVector);
		addTo(action, change);
	}
	clip->expectEvent();
}

/// Make the freeze once the arpeggiator is off, recording the changes in the action. Everything it needs was prepared,
/// so it can't fail. No audio is rendered meanwhile: the notes going to the arpeggiator don't sound without it.
void changeClip(InstrumentClip* clip, PreparedChange& prepared, deluge::vector<bool> const& rowsFrozen,
                int32_t renderLength, int64_t playPos, Action* action) {
	resetRandomizer(clip, prepared, action);
	// Before the rows are written: rowsFrozen indexes the rows as they are, and new rows follow the clip
	unrollLayout(clip, rowsFrozen, renderLength);
	writeRows(clip, prepared, action);
	// Playback is resumed once the action is complete
	resyncPlayback(clip, playPos);
}

/// freeze() once the arpeggiator and recording were checked. Throws deluge::exception::BAD_ALLOC if a vector runs out
/// of memory, which can only happen before anything is changed.
Result freezeClip(InstrumentClip* clip, void (*beforeChanging)()) {
	// Everything which can refuse the freeze is checked first

	if (isRandomizerAutomated(clip)) {
		return Result::AUTOMATION;
	}

	std::array<char, MODEL_STACK_MAX_SIZE> model_stack_memory{};
	ModelStackWithTimelineCounter* model_stack =
	    setupModelStackWithTimelineCounter(model_stack_memory.data(), currentSong, clip);
	deluge::vector<bool> rows_feeding_arp(clip->noteRows.getNumElements());
	// The rows with notes going to the arpeggiator, then also the rows getting frozen notes. Rows without either (e.g.
	// empty) aren't changed.
	deluge::vector<bool> rows_frozen(clip->noteRows.getNumElements());
	for (int32_t r = 0; r < clip->noteRows.getNumElements(); r++) {
		rows_feeding_arp[r] = rowFeedsArp(clip, r, model_stack);
		rows_frozen[r] = rows_feeding_arp[r] && hasNotesPlayingWithoutFill(clip->noteRows.getElement(r));
	}
	if (std::ranges::find(rows_frozen, true) == rows_frozen.end()) {
		return Result::NO_NOTES;
	}

	// NOLINTNEXTLINE(cppcoreguidelines-init-variables): CI partial-parse false positive
	int32_t render_length = getRenderLength(clip, rows_frozen);
	if (render_length < 0) {
		return Result::TOO_LONG;
	}
	// Automation would have to be written out like the notes, which isn't done. Checked again below with the rows
	// getting frozen notes, but checking now saves rendering.
	if (changesLayout(clip, rows_frozen, render_length) && hasAutomation(clip, rows_frozen)) {
		return Result::AUTOMATION;
	}

	deluge::vector<FrozenNote> frozen_notes;
	// NOLINTNEXTLINE(cppcoreguidelines-init-variables): CI partial-parse false positive
	Result result = render(clip, rows_frozen, render_length, frozen_notes);
	if (result != Result::OK) {
		return result;
	}
	fitNotes(frozen_notes, render_length);
	result = checkFrozenNotes(clip, rows_feeding_arp, frozen_notes, render_length, rows_frozen);
	if (result != Result::OK) {
		return result;
	}

	// Everything the change needs is made before leaving the menu (which doesn't change any of it), so that running
	// out of memory is refused there, and the change can't fail. The randomizer is recorded last, as preparing the
	// rows keeps the audio going.
	PreparedChange prepared;
	prepared.layout = makeConsequence<ConsequenceArpFreezeLayout>(clip);
	prepared.settings = makeConsequence<ConsequenceArpSettingsChange>(clip);
	if (prepared.layout == nullptr || prepared.settings == nullptr
	    || !prepareRows(clip, rows_frozen, frozen_notes, prepared) || !prepareRandomizerReset(clip, prepared)) {
		return Result::NO_MEMORY;
	}

	// Before leaving the menu, which it's shown over. Making the change is quick.
	stopWorkingAnimation();
	if (beforeChanging != nullptr) {
		beforeChanging();
	}

	// The action is only made now, as there can only be a new action from the clip's view. That's checked first, as
	// making an action deletes the actions which could be redone.
	if (getCurrentUI() != getRootUI()) {
		return Result::NOT_IN_CLIP_VIEW;
	}
	Action* action = actionLogger.getNewAction(ActionType::MISC, ActionAddition::NOT_ALLOWED);
	if (action == nullptr) {
		return Result::NO_MEMORY; // Arrangement recording, which also prevents it, was refused at first
	}

	// Undo reverts in reverse order: the arpeggiator change first, then the notes, then the layout, which resumes
	// playback once everything is back. Redo reverts the other way round: the notes come back after the layout, so
	// that they aren't cut, and the arpeggiator change, done last, resumes playback.
	addTo(action, prepared.layout);
	int64_t play_pos = getPlayPos(clip);
	bool length_changed = (render_length != clip->loopLength);
	prepared.settings->lengthChanged = length_changed;

	// The arpeggiator is switched off first, so that it can't start notes again while the clip changes
	stopNotesForArpChange(clip);
	clip->arpSettings.preset = ArpPreset::OFF;
	clip->arpSettings.mode = ArpMode::OFF;
	clip->arpSettings.flagForceArpRestart = true;

	changeClip(clip, prepared, rows_frozen, render_length, play_pos, action);
	addTo(action, prepared.settings);
	actionLogger.closeAction(ActionType::MISC);

	resumePlayback(clip, length_changed);
	return Result::OK;
}

} // namespace

bool isAvailable(Clip* clip) {
	if (clip == nullptr || clip->type != ClipType::INSTRUMENT) {
		return false;
	}
	return static_cast<InstrumentClip*>(clip)->arpSettings.mode != ArpMode::OFF;
}

Result freeze(InstrumentClip* clip, void (*beforeChanging)()) {
	if (!isAvailable(clip)) {
		return Result::ARP_OFF;
	}
	if (clip->getCurrentlyRecordingLinearly() || playbackHandler.isCurrentlyRecording()) {
		return Result::RECORDING;
	}
	work_start_time = AudioEngine::audioSampleTimer;
	Result result = Result::NO_MEMORY;
	try {
		result = freezeClip(clip, beforeChanging);
	} catch (deluge::exception e) {
		if (e != deluge::exception::BAD_ALLOC) {
			stopWorkingAnimation();
			throw;
		}
	}
	// Before a refusal's popup
	stopWorkingAnimation();
	return result;
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
	// The instances of the clip in the arrangement keep their lengths: they play its start, which sounds as before
	currentSong->setClipLength(clip, loopLength, nullptr, /*mayReSyncClip=*/false, /*mayResumeClip=*/false,
	                           /*mayChangeClipInstances=*/false);
	// What PlaybackMode::reSyncClip() would do is left to resyncPlayback() and resumePlayback(), once the clip is
	// complete
}

int64_t getPlayPos(InstrumentClip* clip) {
	int32_t pos = clip->lastProcessedPos;
	// Playing backwards counts down from the end, which is at position 0
	if (clip->currentlyPlayingReversed && pos != 0) {
		pos = clip->loopLength - pos;
	}
	return ((int64_t)clip->repeatCount * clip->loopLength) + pos;
}

void resyncPlayback(InstrumentClip* clip, int64_t playPos) {
	if (!playbackHandler.isEitherClockActive() || !currentSong->isClipActive(clip)) {
		return;
	}
	std::array<char, MODEL_STACK_MAX_SIZE> model_stack_memory{};
	ModelStackWithTimelineCounter* model_stack =
	    setupModelStackWithTimelineCounter(model_stack_memory.data(), currentSong, clip);
	// The notes playing were from the old layout
	clip->stopAllNotesPlaying(model_stack);
	// Not PlaybackMode::reSyncClip(), which would cancel the clip being armed to launch or stop: its position is set
	// below. (resumePlayback() does what it does for the clip the external clock is scaled to.)

	// The clip, and each row with its own length or direction, carries on from the same place as before. This keeps
	// the rows which weren't frozen in time with the clip, and an unrolled clip in the pass it was playing.
	if (playPos > std::numeric_limits<int32_t>::max()) {
		playPos %= clip->loopLength;
	}
	clip->setPos(model_stack, (int32_t)std::max(playPos, -(int64_t)clip->loopLength));

	// Played backwards, the end of a pass is at position 0, where processing the position counts the next pass:
	// setPos() leaves the count one low for that, as it's followed by processing the position when a clip is
	// launched. Here playback has already processed it and carries on from the next tick, so the pass is counted now.
	// This keeps the passes (which iterance and ping-pong follow) as they were, and getPlayPos() where it was.
	if (playPos < 0) {
		return;
	}
	if (clip->currentlyPlayingReversed && clip->lastProcessedPos == 0) {
		clip->repeatCount++;
	}
	for (int32_t r = 0; r < clip->noteRows.getNumElements(); r++) {
		NoteRow* note_row = clip->noteRows.getElement(r);
		if (note_row->hasIndependentPlayPos() && note_row->currentlyPlayingReversedIfIndependent
		    && note_row->lastProcessedPosIfIndependent == 0) {
			note_row->repeatCountIfIndependent++;
		}
	}
}

void resumePlayback(InstrumentClip* clip, bool lengthChanged) {
	if (!playbackHandler.isEitherClockActive() || !currentSong->isClipActive(clip)) {
		return;
	}
	// As PlaybackMode::reSyncClip() does when the length of the clip the external clock is scaled to changes: all
	// playing clips are put where the clock is and resumed, which cancels their arming. Only done now that the clip is
	// complete, as it resumes it.
	if (lengthChanged && playbackHandler.isExternalClockActive() && clip == currentSong->getSyncScalingClip()) {
		playbackHandler.resyncInternalTicksToInputTicks(currentSong);
		// Arrangement playback doesn't resync its clips
		if (currentPlaybackMode == &session) {
			return;
		}
	}
	std::array<char, MODEL_STACK_MAX_SIZE> model_stack_memory{};
	clip->resumePlayback(setupModelStackWithTimelineCounter(model_stack_memory.data(), currentSong, clip));
}

} // namespace arp_freeze
