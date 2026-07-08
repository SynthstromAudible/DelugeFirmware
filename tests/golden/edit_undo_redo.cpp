// Self-checking Edit undo/redo hook (M1d, docs/dev/project_model_migration_plan.md).
//
//   edit_undo_redo <song.XML>
//
// Drives the EDIT path (deluge::edits::SetParamValue -> apply) over a REAL loaded song, proving what the
// song-less edit_test unit harness cannot: that a loaded-song edit resolves through a live DOM address
// (addressFor -> ParamAddress, resolveParam == doc()) and that the REAL undo/redo path (ActionLogger::revert)
// restores state. Self-checking (no baseline): exit 0 = all pass, 1 = load error, 2 = usage/IO, 3 = check failed.
//
// Undo is feasible headless here (unlike edit_test's empty song): a loaded song has real clips, so getNewAction's
// clip snapshot allocates and the Action opens; revert only MOVES Actions between queues (no consequence free ->
// no audio-engine pump). recording=OFF so the pre-log opens the Action. Assertions compare currentValue + nodes,
// NOT doc()->current_value (store-scalar undo is M3+).

#include "deluge-project/param_address.h"
#include "deluge-project/param_automation.h"
#include "deluge-project/serialization.h"
#include "hid/display/display.h"
#include "hid/display/seven_segment.h"
#include "memory/general_memory_allocator.h"
#include "model/action/action.h"
#include "model/action/action_logger.h"
#include "model/clip/clip.h"
#include "model/clip/instrument_clip.h"
#include "model/drum/drum.h"
#include "model/edits/add_note.h"
#include "model/edits/clip_address_for.h"
#include "model/edits/edit.h"
#include "model/edits/note_address_for.h"
#include "model/edits/nudge_note.h"
#include "model/edits/param_address_for.h"
#include "model/edits/remove_note.h"
#include "model/edits/repeat_note_edit.h"
#include "model/edits/set_euclidean_events.h"
#include "model/edits/set_note_row_length.h"
#include "model/edits/set_param_value.h"
#include "model/edits/shift_clip.h"
#include "model/edits/shift_note_row.h"
#include "model/global_effectable/global_effectable.h"
#include "model/iterance/iterance.h"
#include "model/model_stack.h"
#include "model/note/note.h"
#include "model/note/note_row.h"
#include "model/output.h"
#include "model/song/song.h"
#include "modulation/automation/auto_param.h"
#include "modulation/params/param.h"
#include "modulation/params/param_node.h"
#include "modulation/params/param_set.h"
#include "playback/playback_handler.h"
#include "storage/storage_manager.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <vector>

namespace params = deluge::modulation::params;

namespace {

int failures = 0;
void check(bool ok, const char* what) {
	if (!ok) {
		fprintf(stderr, "CHECK FAIL: %s\n", what);
		failures++;
	}
}

// Same native load as param_record_diff. `xml` must be writable and outlive all uses.
Song* load_song(char* xml, size_t xmlLen, Error* loadErr) {
	void* mem = GeneralMemoryAllocator::get().alloc(sizeof(Song));
	Song* song = new (mem) Song();
	song->paramManager.setupUnpatched();
	GlobalEffectable::initParams(&song->paramManager);

	XMLDeserializer reader((uint8_t*)xml, xmlLen);
	activeDeserializer = &reader;
	Error e = reader.openXMLFile(nullptr, "song");
	if (e == Error::NONE) {
		e = deluge::project::loadSong(*song, reader);
	}
	*loadErr = e;
	if (e != Error::NONE) {
		return nullptr;
	}
	currentSong = song;
	song->setCurrentClip(nullptr);
	return song;
}

// A comparable snapshot of one param lane: the engine cursor + a copy of the DOM node store.
struct Snapshot {
	int32_t currentValue;
	std::vector<ParamNode> nodes;
};
Snapshot snapshotOf(ModelStackWithAutoParam* msap) {
	Snapshot s;
	s.currentValue = msap->autoParam->getCurrentValue();
	const auto& nodes = msap->doc()->nodes;
	for (int32_t i = 0; i < std::ssize(nodes); i++) {
		s.nodes.push_back(nodes[i]);
	}
	return s;
}
bool equalSnapshot(const Snapshot& a, const Snapshot& b) {
	if (a.currentValue != b.currentValue || a.nodes.size() != b.nodes.size()) {
		return false;
	}
	for (size_t i = 0; i < a.nodes.size(); i++) {
		if (a.nodes[i].pos != b.nodes[i].pos || a.nodes[i].value != b.nodes[i].value
		    || a.nodes[i].interpolated != b.nodes[i].interpolated) {
			return false;
		}
	}
	return true;
}

// Script apply(SetParamValue) -> undo -> redo over one resolved param lane, asserting the invariants.
void scriptEditUndoRedo(const char* label, Song* song, ModelStackWithAutoParam* msap) {
	printf("# target %s\n", label);

	// (1) The edit resolves through a LIVE DOM address (not the engine fallback) — the coverage edit_test lacks.
	std::optional<deluge::project::ParamAddress> addr = deluge::edits::addressFor(*msap);
	check(addr.has_value(), "addressFor resolves a real ParamAddress");
	if (!addr) {
		return;
	}
	check(song->project.resolveParam(*addr) == msap->doc(), "resolveParam(addr) == doc() (addressed path is live)");

	Snapshot pre = snapshotOf(msap);
	int32_t newValue = msap->autoParam->getCurrentValue() + 12345678; // guaranteed != current

	// (2) Forward: the Edit funnels the mutation and pre-logs its ConsequenceParamChange.
	deluge::edits::Context ctx{*currentSong, currentSong->project};
	deluge::edits::SetParamValue edit{addr, newValue, 0, 0, msap};
	check(deluge::edits::apply(ctx, edit) == Error::NONE, "apply succeeds");
	Snapshot post = snapshotOf(msap);
	check(post.currentValue == newValue, "forward: value mutated");

	Action* a = actionLogger.firstAction[BEFORE];
	check(a != nullptr, "an Action was opened");
	if (!a) {
		return;
	}
	check(a->type == ActionType::PARAM_UNAUTOMATED_VALUE_CHANGE, "Action type is param change");

	// (3) Real UNDO restores the pre-edit lane state (cv + nodes).
	actionLogger.revert(BEFORE, /*updateVisually=*/false, /*doNavigation=*/false);
	check(equalSnapshot(snapshotOf(msap), pre), "undo restores cv+nodes to pre-edit");

	// (4) Real REDO re-applies the edited lane state.
	actionLogger.revert(AFTER, /*updateVisually=*/false, /*doNavigation=*/false);
	check(equalSnapshot(snapshotOf(msap), post), "redo restores cv+nodes to post-edit");
}

// Presence of a note at pos in a note-row (NoteVector is an OrderedPosVector<Note>).
bool noteAtPos(NoteRow* noteRow, int32_t pos) {
	int32_t i = noteRow->notes.firstAtOrAfter(pos);
	return i < std::ssize(noteRow->notes) && noteRow->notes[i].pos == pos;
}

// Drive RemoveNote+undo/redo on a real note and AddNote+undo/redo re-inserting it into the (now clear) slot
// with its own length — robust against attemptNoteAdd's overlap rules (no guessing a free pos). Shared by the
// melodic (M2b) and kit (M2c) targets.
void driveNoteEdits(Song* song, InstrumentClip* clip, NoteRow* noteRow, int32_t rowIndex, const char* label) {
	char mem[MODEL_STACK_MAX_SIZE];
	int32_t noteRowId = clip->getNoteRowId(noteRow, rowIndex);
	ModelStackWithNoteRow* ms = setupModelStackWithTimelineCounter(mem, song, clip)->addNoteRow(noteRowId, noteRow);
	check(deluge::edits::noteRowAddressFor(*ms).has_value(), "note: noteRowAddressFor resolves");

	Note& seed = noteRow->notes[0];
	int32_t pos0 = seed.pos, len0 = seed.getLength(), vel0 = seed.getVelocity(), prob0 = seed.getProbability();
	Iterance iter0 = seed.getIterance();
	check(noteAtPos(noteRow, pos0), "note: seed note present");

	deluge::edits::Context ctx{*currentSong, currentSong->project};

	// A. RemoveNote the real note -> undo restores -> redo removes.
	actionLogger.firstAction[BEFORE] = nullptr;
	actionLogger.firstAction[AFTER] = nullptr;
	deluge::edits::RemoveNote rem{deluge::edits::noteRowAddressFor(*ms), pos0, ms};
	check(deluge::edits::apply(ctx, rem) == Error::NONE, "note-remove: apply succeeds");
	check(!noteAtPos(noteRow, pos0), "note-remove: note absent after remove");
	actionLogger.revert(BEFORE, false, false);
	check(noteAtPos(noteRow, pos0), "note-remove: undo restores the note");
	actionLogger.revert(AFTER, false, false);
	check(!noteAtPos(noteRow, pos0), "note-remove: redo removes the note again");

	// B. AddNote it back into the now-clear slot (same length -> no overlap) -> undo -> redo.
	actionLogger.firstAction[BEFORE] = nullptr;
	actionLogger.firstAction[AFTER] = nullptr;
	deluge::edits::AddNote add{deluge::edits::noteRowAddressFor(*ms), pos0, len0, vel0, prob0, iter0, /*fill=*/0, ms};
	check(deluge::edits::apply(ctx, add) == Error::NONE, "note-add: apply succeeds");
	check(noteAtPos(noteRow, pos0), "note-add: note present after add");
	actionLogger.revert(BEFORE, false, false);
	check(!noteAtPos(noteRow, pos0), "note-add: undo removes the note");
	actionLogger.revert(AFTER, false, false);
	check(noteAtPos(noteRow, pos0), "note-add: redo restores the note");
	(void)label;
}

// True if this clip is note-addressable (has a durable clip id — see the S5-b note-only-clip gap).
bool clipIsAddressable(InstrumentClip* ic) {
	return ic->serialId().has_value() || ic->sessionId() != 0;
}

// M2b: first MELODIC (synth/MIDI) InstrumentClip note-row with a note.
void noteTarget(Song* song) {
	for (int32_t c = 0; c < std::ssize(song->project.clips.session_clips); c++) {
		Clip* cl = song->project.clips.session_clips[c];
		if (cl->type != ClipType::INSTRUMENT || cl->output == nullptr || cl->output->type == OutputType::KIT
		    || cl->output->type == OutputType::AUDIO) {
			continue;
		}
		auto* ic = (InstrumentClip*)cl;
		if (!clipIsAddressable(ic)) {
			continue;
		}
		for (int32_t r = 0; r < std::ssize(ic->noteRows); r++) {
			if (!ic->noteRows[r].notes.empty()) {
				printf("# target note-add-remove\n");
				driveNoteEdits(song, ic, &ic->noteRows[r], r, "melodic");
				return;
			}
		}
	}
	printf("# no-addressable-melodic-notes\n");
}

// M2c: first KIT InstrumentClip SOUND-drum note-row with a note (row keyed by drum id).
void kitNoteTarget(Song* song) {
	for (int32_t c = 0; c < std::ssize(song->project.clips.session_clips); c++) {
		Clip* cl = song->project.clips.session_clips[c];
		if (cl->type != ClipType::INSTRUMENT || cl->output == nullptr || cl->output->type != OutputType::KIT) {
			continue;
		}
		auto* ic = (InstrumentClip*)cl;
		if (!clipIsAddressable(ic)) {
			continue;
		}
		for (int32_t r = 0; r < std::ssize(ic->noteRows); r++) {
			NoteRow& nr = ic->noteRows[r];
			if (!nr.notes.empty() && nr.drum != nullptr && nr.drum->type == DrumType::SOUND
			    && (nr.drum->serialId().has_value() || nr.drum->sessionId() != 0)) {
				printf("# target kit-note-add-remove\n");
				driveNoteEdits(song, ic, &nr, r, "kit");
				return;
			}
		}
	}
	printf("# no-addressable-kit-notes\n");
}

// M2c: set a melodic note-row's independent length via SetNoteRowLength, undo/redo. ConsequenceNoteRowLength's
// revert re-resolves the clip via song->getCurrentClip(), so the harness sets the current clip first (the
// coverage that lets these getCurrentClip-dependent ops be tested headless at all).
void lengthTarget(Song* song) {
	InstrumentClip* clip = nullptr;
	NoteRow* noteRow = nullptr;
	for (int32_t c = 0; c < std::ssize(song->project.clips.session_clips) && clip == nullptr; c++) {
		Clip* cl = song->project.clips.session_clips[c];
		if (cl->type != ClipType::INSTRUMENT || cl->output == nullptr || cl->output->type == OutputType::KIT
		    || cl->output->type == OutputType::AUDIO) {
			continue;
		}
		auto* ic = (InstrumentClip*)cl;
		if (clipIsAddressable(ic) && std::ssize(ic->noteRows) > 0) {
			clip = ic;
			noteRow = &ic->noteRows[0];
		}
	}
	if (clip == nullptr) {
		printf("# no-addressable-melodic-row\n");
		return;
	}
	printf("# target noterow-length\n");
	song->setCurrentClip(clip); // ConsequenceNoteRowLength::revert resolves the row via getCurrentClip()

	char mem[MODEL_STACK_MAX_SIZE];
	ModelStackWithNoteRow* ms =
	    setupModelStackWithTimelineCounter(mem, song, clip)->addNoteRow(clip->getNoteRowId(noteRow, 0), noteRow);

	int32_t oldLen = ms->getLoopLength();
	int32_t newLen = oldLen * 2; // lengthening is the safe direction

	deluge::edits::Context ctx{*currentSong, currentSong->project};
	actionLogger.firstAction[BEFORE] = nullptr;
	actionLogger.firstAction[AFTER] = nullptr;
	deluge::edits::SetNoteRowLength edit{deluge::edits::noteRowAddressFor(*ms), newLen, /*oldPos=*/0,
	                                     noteRow->hasIndependentPlayPos(), ms};
	check(deluge::edits::apply(ctx, edit) == Error::NONE, "length: apply succeeds");
	check(ms->getLoopLength() == newLen, "length: note-row length changed");
	actionLogger.revert(BEFORE, false, false);
	check(ms->getLoopLength() == oldLen, "length: undo restores old length");
	actionLogger.revert(AFTER, false, false);
	check(ms->getLoopLength() == newLen, "length: redo re-applies new length");

	song->setCurrentClip(nullptr);
}

std::vector<int32_t> notePositions(NoteRow* nr) {
	std::vector<int32_t> v;
	for (int32_t i = 0; i < std::ssize(nr->notes); i++) {
		v.push_back(nr->notes[i].pos);
	}
	return v;
}

// M2c: shift a melodic note-row horizontally via ShiftNoteRow, undo/redo (position restoration is the invariant).
void shiftTarget(Song* song) {
	InstrumentClip* clip = nullptr;
	NoteRow* noteRow = nullptr;
	int32_t rowIndex = -1;
	for (int32_t c = 0; c < std::ssize(song->project.clips.session_clips) && clip == nullptr; c++) {
		Clip* cl = song->project.clips.session_clips[c];
		if (cl->type != ClipType::INSTRUMENT || cl->output == nullptr || cl->output->type == OutputType::KIT
		    || cl->output->type == OutputType::AUDIO) {
			continue;
		}
		auto* ic = (InstrumentClip*)cl;
		if (!clipIsAddressable(ic)) {
			continue;
		}
		for (int32_t r = 0; r < std::ssize(ic->noteRows); r++) {
			if (!ic->noteRows[r].notes.empty()) {
				clip = ic;
				noteRow = &ic->noteRows[r];
				rowIndex = r;
				break;
			}
		}
	}
	if (clip == nullptr) {
		printf("# no-addressable-melodic-notes-for-shift\n");
		return;
	}
	printf("# target noterow-shift\n");
	song->setCurrentClip(clip);

	char mem[MODEL_STACK_MAX_SIZE];
	ModelStackWithNoteRow* ms =
	    setupModelStackWithTimelineCounter(mem, song, clip)->addNoteRow(clip->getNoteRowId(noteRow, rowIndex), noteRow);

	std::vector<int32_t> before = notePositions(noteRow);
	actionLogger.firstAction[BEFORE] = nullptr;
	actionLogger.firstAction[AFTER] = nullptr;
	deluge::edits::Context ctx{*currentSong, currentSong->project};
	deluge::edits::ShiftNoteRow edit{deluge::edits::noteRowAddressFor(*ms), /*shiftAmount=*/3,
	                                 /*shiftAutomation=*/false, /*shiftSequenceAndMPE=*/true, ms};
	check(deluge::edits::apply(ctx, edit) == Error::NONE, "shift: apply succeeds");
	check(notePositions(noteRow) != before, "shift: note positions changed");
	actionLogger.revert(BEFORE, false, false);
	check(notePositions(noteRow) == before, "shift: undo restores positions");
	actionLogger.revert(AFTER, false, false);
	check(notePositions(noteRow) != before, "shift: redo re-applies");

	song->setCurrentClip(nullptr);
}

// M2c: rebuild a melodic note-row as a Euclidean pattern via SetEuclideanEvents, undo/redo (the old note array is
// stolen into the undo Consequence and restored). The view keeps its clamp/display; we feed already-clamped inputs.
void euclideanTarget(Song* song) {
	InstrumentClip* clip = nullptr;
	NoteRow* noteRow = nullptr;
	int32_t rowIndex = -1;
	for (int32_t c = 0; c < std::ssize(song->project.clips.session_clips) && clip == nullptr; c++) {
		Clip* cl = song->project.clips.session_clips[c];
		if (cl->type != ClipType::INSTRUMENT || cl->output == nullptr || cl->output->type == OutputType::KIT
		    || cl->output->type == OutputType::AUDIO) {
			continue;
		}
		auto* ic = (InstrumentClip*)cl;
		if (!clipIsAddressable(ic)) {
			continue;
		}
		for (int32_t r = 0; r < std::ssize(ic->noteRows); r++) {
			if (!ic->noteRows[r].notes.empty()) {
				clip = ic;
				noteRow = &ic->noteRows[r];
				rowIndex = r;
				break;
			}
		}
	}
	if (clip == nullptr) {
		printf("# no-addressable-melodic-notes-for-euclidean\n");
		return;
	}
	printf("# target noterow-euclidean\n");
	song->setCurrentClip(clip); // undoJustOneConsequencePerNoteRow / note-array revert resolve via getCurrentClip()

	char mem[MODEL_STACK_MAX_SIZE];
	ModelStackWithNoteRow* ms =
	    setupModelStackWithTimelineCounter(mem, song, clip)->addNoteRow(clip->getNoteRowId(noteRow, rowIndex), noteRow);

	// Feed the Edit the same grid maths the view produces. squareWidth=1 gives the maximum step count
	// (numStepsAvailable == effectiveLength), which is always >= the row's note count — so we never violate the
	// view's guarantee that newNumNotes <= numStepsAvailable (that would yield duplicate positions -> E376).
	int32_t effectiveLength = ms->getLoopLength();
	uint32_t squareWidth = 1;
	int32_t numStepsAvailable = effectiveLength; // (effectiveLength - 1) / 1 + 1
	// Force a different event count than the row currently holds so the pattern (and count) provably changes.
	int32_t origCount = std::ssize(noteRow->notes);
	int32_t newNumNotes = (origCount + 1 <= numStepsAvailable) ? origCount + 1 : origCount - 1;
	if (newNumNotes < 1) {
		newNumNotes = 1;
	}

	std::vector<int32_t> before = notePositions(noteRow);
	actionLogger.firstAction[BEFORE] = nullptr;
	actionLogger.firstAction[AFTER] = nullptr;
	deluge::edits::Context ctx{*currentSong, currentSong->project};
	deluge::edits::SetEuclideanEvents edit{
	    deluge::edits::noteRowAddressFor(*ms),
	    /*offset=*/newNumNotes - origCount,    newNumNotes, numStepsAvailable, effectiveLength, squareWidth, ms};
	check(deluge::edits::apply(ctx, edit) == Error::NONE, "euclidean: apply succeeds");
	check(std::ssize(noteRow->notes) == newNumNotes, "euclidean: note count matches requested events");
	check(notePositions(noteRow) != before, "euclidean: note pattern changed");
	actionLogger.revert(BEFORE, false, false);
	check(notePositions(noteRow) == before, "euclidean: undo restores original notes");
	actionLogger.revert(AFTER, false, false);
	check(std::ssize(noteRow->notes) == newNumNotes, "euclidean: redo re-applies the pattern");

	song->setCurrentClip(nullptr);
}

// M2c: split one note in a square into two repeats via RepeatNoteEdit, undo/redo. We synthesise a single-screen
// square (wrapEditLevel == effectiveLength) around a note that has room after it, so the repeat adds exactly one
// note (no cross-screen fan-out, no position collisions).
void repeatTarget(Song* song) {
	InstrumentClip* clip = nullptr;
	NoteRow* noteRow = nullptr;
	int32_t rowIndex = -1;
	int32_t editPos = 0;
	int32_t squareWidth = 0;
	for (int32_t c = 0; c < std::ssize(song->project.clips.session_clips) && clip == nullptr; c++) {
		Clip* cl = song->project.clips.session_clips[c];
		if (cl->type != ClipType::INSTRUMENT || cl->output == nullptr || cl->output->type == OutputType::KIT
		    || cl->output->type == OutputType::AUDIO) {
			continue;
		}
		auto* ic = (InstrumentClip*)cl;
		if (!clipIsAddressable(ic)) {
			continue;
		}
		for (int32_t r = 0; r < std::ssize(ic->noteRows) && clip == nullptr; r++) {
			NoteRow& nr = ic->noteRows[r];
			if (nr.notes.empty()) {
				continue;
			}
			int32_t p0 = nr.notes[0].pos;
			int32_t nextPos = (std::ssize(nr.notes) > 1) ? nr.notes[1].pos : nr.loopLengthIfIndependent;
			// loopLengthIfIndependent is 0 for a shared-length row; fall back to a generous span in that case.
			if (nextPos <= p0) {
				nextPos = p0 + 32;
			}
			int32_t gap = nextPos - p0;
			if (gap >= 4) { // room to place a 2nd note strictly inside [p0, p0 + gap/2)
				clip = ic;
				noteRow = &nr;
				rowIndex = r;
				editPos = p0;
				squareWidth = gap / 2;
			}
		}
	}
	if (clip == nullptr) {
		printf("# no-addressable-melodic-notes-for-repeat\n");
		return;
	}
	printf("# target noterow-repeat\n");
	song->setCurrentClip(clip); // secret-undo / note-array revert resolve the clip via getCurrentClip()

	char mem[MODEL_STACK_MAX_SIZE];
	ModelStackWithNoteRow* ms =
	    setupModelStackWithTimelineCounter(mem, song, clip)->addNoteRow(clip->getNoteRowId(noteRow, rowIndex), noteRow);

	int32_t effectiveLength = ms->getLoopLength();
	uint32_t wrapEditLevel = effectiveLength; // one screen: numScreens == 1, no cross-screen fan-out
	int32_t origTotal = std::ssize(noteRow->notes);

	actionLogger.firstAction[BEFORE] = nullptr;
	actionLogger.firstAction[AFTER] = nullptr;
	deluge::edits::Context ctx{*currentSong, currentSong->project};
	deluge::edits::RepeatNoteEdit edit{deluge::edits::noteRowAddressFor(*ms),
	                                   /*offset=*/1,
	                                   editPos,
	                                   squareWidth,
	                                   /*newNumNotes=*/2,
	                                   wrapEditLevel,
	                                   ms};
	check(deluge::edits::apply(ctx, edit) == Error::NONE, "repeat: apply succeeds");
	check(std::ssize(noteRow->notes) == origTotal + 1, "repeat: one repeat note added");
	actionLogger.revert(BEFORE, false, false);
	check(std::ssize(noteRow->notes) == origTotal, "repeat: undo restores the original note count");
	actionLogger.revert(AFTER, false, false);
	check(std::ssize(noteRow->notes) == origTotal + 1, "repeat: redo re-adds the repeat");

	song->setCurrentClip(nullptr);
}

// M2c: nudge one note right by +1 via NudgeNote, undo/redo. We pick a note with a free slot at pos+1 (gap >= 2) and
// one screen (wrapEditLevel == effectiveLength) so exactly that note moves.
void nudgeTarget(Song* song) {
	InstrumentClip* clip = nullptr;
	NoteRow* noteRow = nullptr;
	int32_t rowIndex = -1;
	int32_t notePos = -1;
	for (int32_t c = 0; c < std::ssize(song->project.clips.session_clips) && clip == nullptr; c++) {
		Clip* cl = song->project.clips.session_clips[c];
		if (cl->type != ClipType::INSTRUMENT || cl->output == nullptr || cl->output->type == OutputType::KIT
		    || cl->output->type == OutputType::AUDIO) {
			continue;
		}
		auto* ic = (InstrumentClip*)cl;
		if (!clipIsAddressable(ic)) {
			continue;
		}
		for (int32_t r = 0; r < std::ssize(ic->noteRows) && clip == nullptr; r++) {
			NoteRow& nr = ic->noteRows[r];
			if (nr.notes.empty()) {
				continue;
			}
			int32_t p0 = nr.notes[0].pos;
			int32_t nextPos = (std::ssize(nr.notes) > 1) ? nr.notes[1].pos : (p0 + 2);
			if (nextPos - p0 >= 2) { // slot at p0+1 is free
				clip = ic;
				noteRow = &nr;
				rowIndex = r;
				notePos = p0;
			}
		}
	}
	if (clip == nullptr) {
		printf("# no-addressable-melodic-notes-for-nudge\n");
		return;
	}
	printf("# target noterow-nudge\n");
	song->setCurrentClip(clip); // note-array + automation reverts resolve the clip via getCurrentClip()

	char mem[MODEL_STACK_MAX_SIZE];
	ModelStackWithNoteRow* ms =
	    setupModelStackWithTimelineCounter(mem, song, clip)->addNoteRow(clip->getNoteRowId(noteRow, rowIndex), noteRow);

	uint32_t wrapEditLevel = ms->getLoopLength(); // one screen: only this note moves
	check(noteAtPos(noteRow, notePos), "nudge: seed note present");

	actionLogger.firstAction[BEFORE] = nullptr;
	actionLogger.firstAction[AFTER] = nullptr;
	deluge::edits::Context ctx{*currentSong, currentSong->project};
	deluge::edits::NudgeNote edit{deluge::edits::noteRowAddressFor(*ms),
	                              notePos,
	                              /*offset=*/1,
	                              wrapEditLevel,
	                              /*nudgeAutomation=*/false,
	                              /*nudgeMPE=*/true,
	                              ms};
	check(deluge::edits::apply(ctx, edit) == Error::NONE, "nudge: apply succeeds");
	check(noteAtPos(noteRow, notePos + 1), "nudge: note moved right by 1");
	check(!noteAtPos(noteRow, notePos), "nudge: original slot now empty");
	actionLogger.revert(BEFORE, false, false);
	check(noteAtPos(noteRow, notePos), "nudge: undo restores the note");
	check(!noteAtPos(noteRow, notePos + 1), "nudge: undo clears the nudged slot");
	actionLogger.revert(AFTER, false, false);
	check(noteAtPos(noteRow, notePos + 1), "nudge: redo re-applies the nudge");

	song->setCurrentClip(nullptr);
}

// M2d pilot: shift a whole clip horizontally via ShiftClip, undo/redo (position restoration is the invariant). The
// clip-tier twin of shiftTarget; exercises ClipAddress + clipAddressFor.
void clipShiftTarget(Song* song) {
	InstrumentClip* clip = nullptr;
	NoteRow* noteRow = nullptr;
	for (int32_t c = 0; c < std::ssize(song->project.clips.session_clips) && clip == nullptr; c++) {
		Clip* cl = song->project.clips.session_clips[c];
		if (cl->type != ClipType::INSTRUMENT || cl->output == nullptr || cl->output->type == OutputType::KIT
		    || cl->output->type == OutputType::AUDIO) {
			continue;
		}
		if (!deluge::edits::clipAddressFor(*cl).has_value()) {
			continue;
		}
		auto* ic = (InstrumentClip*)cl;
		for (int32_t r = 0; r < std::ssize(ic->noteRows); r++) {
			if (!ic->noteRows[r].notes.empty()) {
				clip = ic;
				noteRow = &ic->noteRows[r];
				break;
			}
		}
	}
	if (clip == nullptr) {
		printf("# no-addressable-clip-with-notes-for-shift\n");
		return;
	}
	printf("# target clip-shift\n");
	song->setCurrentClip(clip); // ConsequenceClipHorizontalShift::revert resolves the clip via getCurrentClip()

	char mem[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* ms = setupModelStackWithTimelineCounter(mem, song, clip);

	std::vector<int32_t> before = notePositions(noteRow);
	actionLogger.firstAction[BEFORE] = nullptr;
	actionLogger.firstAction[AFTER] = nullptr;
	deluge::edits::Context ctx{*currentSong, currentSong->project};
	deluge::edits::ShiftClip edit{deluge::edits::clipAddressFor(*clip), /*shiftAmount=*/3,
	                              /*shiftAutomation=*/false, /*shiftSequenceAndMPE=*/true, ms};
	check(deluge::edits::apply(ctx, edit) == Error::NONE, "clip-shift: apply succeeds");
	check(edit.shifted(), "clip-shift: the engine reported a shift");
	check(notePositions(noteRow) != before, "clip-shift: note positions changed");
	actionLogger.revert(BEFORE, false, false);
	check(notePositions(noteRow) == before, "clip-shift: undo restores positions");
	actionLogger.revert(AFTER, false, false);
	check(notePositions(noteRow) != before, "clip-shift: redo re-applies");

	song->setCurrentClip(nullptr);
}

} // namespace

int main(int argc, char** argv) {
	if (argc < 2) {
		fprintf(stderr, "usage: %s <song.XML>\n", argv[0]);
		return 2;
	}
	display = new deluge::hid::display::SevenSegment;

	FILE* f = std::fopen(argv[1], "rb");
	if (!f) {
		fprintf(stderr, "cannot open %s\n", argv[1]);
		return 2;
	}
	std::fseek(f, 0, SEEK_END);
	long sz = std::ftell(f);
	std::fseek(f, 0, SEEK_SET);
	char* xml = (char*)std::malloc(sz + 1);
	if (std::fread(xml, 1, sz, f) != (size_t)sz) {
		fprintf(stderr, "read error\n");
		return 2;
	}
	xml[sz] = 0;
	std::fclose(f);

	Error loadErr = Error::NONE;
	Song* song = load_song(xml, sz, &loadErr);
	if (!song) {
		fprintf(stderr, "load error %d\n", (int)loadErr);
		return 1;
	}
	playbackHandler.recording = RecordingMode::OFF;

	printf("# edit_undo_redo: %s\n", argv[1]);

	// Target 1 — song-global UNPATCHED_VOLUME (OwnerId::song()). Always present.
	{
		char msMem[MODEL_STACK_MAX_SIZE];
		ModelStackWithThreeMainThings* ms3 = song->setupModelStackWithSongAsTimelineCounter(msMem);
		ModelStackWithAutoParam* msap = ms3->getUnpatchedAutoParamFromId(params::UNPATCHED_VOLUME);
		if (!msap || !msap->autoParam) {
			fprintf(stderr, "no song-global UNPATCHED_VOLUME param\n");
			return 3;
		}
		scriptEditUndoRedo("song-global", song, msap);
	}

	// Between targets: close the song-global Action so target 2 opens its own Action rather than coalescing
	// into it (getNewAction coalesces same-type, still-open Actions). No free -> no audio-engine pump.
	if (actionLogger.firstAction[BEFORE] != nullptr) {
		actionLogger.firstAction[BEFORE]->openForAdditions = false;
	}

	// Target 2 — first synth/kit InstrumentClip's unpatched param (OwnerId::instrument[Session]()). S5-b's
	// post-load relocation mints session ids and moves clip-level instrument params into the tree, so a freshly
	// loaded (never-saved) song resolves them. Defensive: skip if no such clip, or if no unpatched param on it
	// resolves to a live instrument address (still nothing to assert then; song-global is the guaranteed coverage).
	{
		InstrumentClip* instClip = nullptr;
		for (int32_t c = 0; c < std::ssize(song->project.clips.session_clips); c++) {
			Clip* clip = song->project.clips.session_clips[c];
			if (clip->type == ClipType::INSTRUMENT && clip->output != nullptr
			    && (clip->output->type == OutputType::SYNTH || clip->output->type == OutputType::KIT)) {
				instClip = (InstrumentClip*)clip;
				break;
			}
		}
		if (instClip == nullptr) {
			printf("# no-instrument-clip\n");
		}
		else {
			// Find the first unpatched param on this clip that resolves to a live instrument address (a relocated
			// lane). Unlike the song-global invariant, the instrument target is best-effort per song.
			int32_t chosenPid = -1;
			for (int32_t pid = 0; pid < params::kMaxNumUnpatchedParams && chosenPid < 0; pid++) {
				char pm[MODEL_STACK_MAX_SIZE];
				ModelStackWithThreeMainThings* p3 = setupModelStackWithThreeMainThingsButNoNoteRow(
				    pm, song, instClip->output->toModControllable(), instClip, &instClip->paramManager);
				ModelStackWithAutoParam* pmsap = p3->getUnpatchedAutoParamFromId(pid);
				if (!pmsap || !pmsap->autoParam) {
					continue;
				}
				std::optional<deluge::project::ParamAddress> pa = deluge::edits::addressFor(*pmsap);
				if (pa.has_value() && song->project.resolveParam(*pa) != nullptr) {
					chosenPid = pid;
				}
			}
			if (chosenPid < 0) {
				printf("# instrument-param-not-addressed\n");
			}
			else {
				char instMem[MODEL_STACK_MAX_SIZE];
				ModelStackWithThreeMainThings* i3 = setupModelStackWithThreeMainThingsButNoNoteRow(
				    instMem, song, instClip->output->toModControllable(), instClip, &instClip->paramManager);
				scriptEditUndoRedo("instrument-clip", song, i3->getUnpatchedAutoParamFromId(chosenPid));
			}
		}
	}

	noteTarget(song);
	kitNoteTarget(song);
	lengthTarget(song);
	shiftTarget(song);
	euclideanTarget(song);
	repeatTarget(song);
	nudgeTarget(song);
	clipShiftTarget(song);

	std::free(xml);
	return failures ? 3 : 0;
}
