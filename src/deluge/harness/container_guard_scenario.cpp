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

#include "harness/container_guard_scenario.h"

#ifdef DELUGE_HOST

#include "definitions_cxx.hpp"
#include "gui/ui/ui.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/session_view.h"
#include "gui/views/view.h"
#include "libdeluge/app.h"
#include "libdeluge/types.h"
#include "memory/fast_allocator.h"
#include "memory/heaps.h"
#include "model/action/action.h"
#include "model/action/action_logger.h"
#include "model/clip/clip_instance.h"
#include "model/clip/instrument_clip.h"
#include "model/instrument/kit.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/output.h"
#include "model/song/song.h"
#include "modulation/automation/auto_param.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_set.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"
#include "processing/source.h"
#include "storage/preset_acquisition.h"
#include "storage/storage_manager.h"
#include "util/structural_edit_probe.h"
#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <unistd.h>

// The firmware headers above reach `src/lib/printf.h`, which soft-aliases `printf` to the embedded `printf_`. This
// harness links the Rust BSP, which has no `putchar_` provider, and every line this file writes IS the gate's output.
#ifdef printf
#undef printf
#endif

namespace {

using deluge::StructuralContainer;

/// The render block the scenario drives: the golden harness skips the audio task's own render once boot has finished,
/// and the internal clock's ticks are actioned inside the render.
constexpr uint32_t kRenderBlockFrames = 128;
/// Blocks rendered with the clip playing before the edit (at least), and after it.
constexpr int32_t kBlocksBefore = 8;
constexpr int32_t kBlocksAfter = 4;
/// The most blocks rendered waiting for the render to action a timer tick of this playback.
constexpr int32_t kMaxBlocksBefore = 400;
/// A y beyond the grid: the new-clip gesture then appends the clip after every other session clip.
constexpr int32_t kAppendY = 1000;
/// Note rows and positions the legs use.
constexpr int32_t kRowNote = 60;
constexpr int32_t kAddedRowNote = 72;
constexpr int32_t kNotePos = 48;
constexpr int32_t kNoteLength = 24;
constexpr int32_t kVelocity = 100;
/// BIGROW's clip has this many note rows before its edit inserts one more at the bottom, which moves every row.
constexpr int32_t kBigRowCount = 128;
/// RENDERALLOC renders this many blocks with its drum starting and stopping, counting the render's heap allocations.
constexpr int32_t kRenderAllocBlocks = 1500;

enum class Leg : uint8_t {
	NoteAdd,
	NoteDel,
	RowAdd,
	RowDel,
	ClipNew,
	ClipDel,
	ClipDup,
	Arrange,
	Automate,
	Drum,
	Replace,
	Undo,
	BigRow,
	RowDrag,
	ClipMove,
	OutputMove,
	Quantize,
	RenderSet,
	RenderAlloc,
	Choke,
	VoicePoly,
	VoiceMono,
	VoiceKill,
	VoiceMultiRange,
	Done,
};

enum class Stage : uint8_t { Setup, Before, Edit, After, Summary };

Stage g_stage = Stage::Setup;
Leg g_leg = Leg::NoteAdd;
bool g_stepDone = false;
bool g_summaryPrinted = false;
int32_t g_failures = 0;
int32_t g_blocks = 0;
uint32_t g_timerAtStart = 0;
Clip* g_home = nullptr;
InstrumentClip* g_clip = nullptr;
NoteRow* g_row = nullptr;
int32_t g_rowIndex = -1;
Kit* g_kit = nullptr;
SoundDrum* g_drum = nullptr;
const char* g_step = nullptr;
uint32_t g_before[static_cast<size_t>(StructuralContainer::COUNT_)]{};
uint32_t g_totalBefore = 0;

const char* legName(Leg leg) {
	switch (leg) {
	case Leg::NoteAdd:
		return "NOTEADD";
	case Leg::NoteDel:
		return "NOTEDEL";
	case Leg::RowAdd:
		return "ROWADD";
	case Leg::RowDel:
		return "ROWDEL";
	case Leg::ClipNew:
		return "CLIPNEW";
	case Leg::ClipDel:
		return "CLIPDEL";
	case Leg::ClipDup:
		return "CLIPDUP";
	case Leg::Arrange:
		return "ARRANGE";
	case Leg::Automate:
		return "AUTOMATE";
	case Leg::Drum:
		return "DRUM";
	case Leg::Replace:
		return "REPLACE";
	case Leg::Undo:
		return "UNDO";
	case Leg::BigRow:
		return "BIGROW";
	case Leg::RowDrag:
		return "ROWDRAG";
	case Leg::ClipMove:
		return "CLIPMOVE";
	case Leg::OutputMove:
		return "OUTPUTMOVE";
	case Leg::Quantize:
		return "QUANTIZE";
	case Leg::RenderSet:
		return "RENDERSET";
	case Leg::RenderAlloc:
		return "RENDERALLOC";
	case Leg::Choke:
		return "CHOKE";
	case Leg::VoicePoly:
		return "VOICEPOLY";
	case Leg::VoiceMono:
		return "VOICEMONO";
	case Leg::VoiceKill:
		return "VOICEKILL";
	case Leg::VoiceMultiRange:
		return "VOICEMULTIRANGE";
	case Leg::Done:
		return "DONE";
	}
	return "?";
}

const char* kindName(StructuralContainer kind) {
	switch (kind) {
	case StructuralContainer::ORDERED_POS_VECTOR:
		return "vector";
	case StructuralContainer::CLIP_ARRAY:
		return "clips";
	case StructuralContainer::OUTPUT_LIST:
		return "outputs";
	case StructuralContainer::DRUM_LIST:
		return "drums";
	case StructuralContainer::RENDERING_DRUMS:
		return "renderdrums";
	case StructuralContainer::VOICES:
		return "voices";
	case StructuralContainer::COUNT_:
		break;
	}
	return "?";
}

bool fail(const char* what) {
	printf("CG-SETUP FAIL leg=%s %s\n", legName(g_leg), what);
	g_failures++;
	return false;
}

void renderBlocks(int32_t count) {
	static DelugeStereoSample silence[kRenderBlockFrames]{};
	static DelugeStereoSample output[kRenderBlockFrames]{};
	for (int32_t block = 0; block < count; block++) {
		deluge_app_render(silence, output, kRenderBlockFrames);
	}
}

/// A playing-ready session clip on a blank synth of its own, made through the new-clip gesture (as
/// clip_lifetime_scenario.cpp makes one), appended after every other clip and marked active so playback starts it.
InstrumentClip* makeSynthClip() {
	ParamManagerForTimeline paramManager;
	Instrument* instrument = StorageManager::createNewInstrument(OutputType::SYNTH, &paramManager);
	if (instrument == nullptr) {
		fail("no synth");
		return nullptr;
	}
	auto* synth = static_cast<SoundInstrument*>(instrument);
	synth->setupAsBlankSynth(&paramManager, false);
	currentSong->backUpParamManager(static_cast<ModControllableAudio*>(synth->toModControllable()), nullptr,
	                                &paramManager, true);
	deluge::storage::AcquireResult acquired;
	acquired.instrument = synth;
	acquired.loadedFromFile = true;
	Clip* clip = sessionView.createNewInstrumentClipAcquired(OutputType::SYNTH, kAppendY, &acquired);
	if (clip == nullptr) {
		fail("the new-clip gesture made no clip");
		return nullptr;
	}
	clip->activeIfNoSolo = true;
	return static_cast<InstrumentClip*>(clip);
}

/// A blank-synth drum, built as sound_lifetime_scenario.cpp builds one; addDrum() is the structural edit.
SoundDrum* makeDrum(Kit* kit) {
	ParamManagerForTimeline paramManager;
	if (paramManager.setupWithPatching() != Error::NONE) {
		fail("a drum's ParamManager could not be set up");
		return nullptr;
	}
	void* memory = deluge::memory::alloc_fast(sizeof(SoundDrum));
	if (memory == nullptr) {
		fail("no memory for a drum");
		return nullptr;
	}
	auto* drum = new (memory) SoundDrum();
	Sound::initParams(&paramManager);
	kit->addDrum(drum);
	drum->setupAsBlankSynth(&paramManager);
	currentSong->backUpParamManager(drum, nullptr, &paramManager, true);
	return drum;
}

/// A playing kit clip with one blank-synth drum, made through the new-clip gesture.
InstrumentClip* makeKitClip() {
	ParamManagerForTimeline kitParams;
	Instrument* instrument = StorageManager::createNewInstrument(OutputType::KIT, &kitParams);
	if (instrument == nullptr) {
		fail("no kit");
		return nullptr;
	}
	auto* kit = static_cast<Kit*>(instrument);
	currentSong->backUpParamManager(static_cast<ModControllableAudio*>(kit->toModControllable()), nullptr, &kitParams,
	                                true);
	if (makeDrum(kit) == nullptr) {
		return nullptr;
	}
	deluge::storage::AcquireResult acquired;
	acquired.instrument = kit;
	acquired.loadedFromFile = true;
	Clip* clip = sessionView.createNewInstrumentClipAcquired(OutputType::KIT, kAppendY, &acquired);
	if (clip == nullptr) {
		fail("the new-clip gesture made no kit clip");
		return nullptr;
	}
	clip->activeIfNoSolo = true;
	g_kit = kit;
	return static_cast<InstrumentClip*>(clip);
}

/// The note row for @p note on g_clip, made if it is not there, with its index.
NoteRow* rowFor(int32_t note, int32_t* index) {
	char memory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* ms = setupModelStackWithTimelineCounter(memory, currentSong, g_clip);
	ModelStackWithNoteRow* row = g_clip->getOrCreateNoteRowForYNote(note, ms, nullptr);
	if (row == nullptr || row->getNoteRowAllowNull() == nullptr) {
		fail("no note row");
		return nullptr;
	}
	if (index != nullptr) {
		*index = row->noteRowId;
	}
	return row->getNoteRowAllowNull();
}

/// Audition @p note on g_clip's synth from the interaction tier, the shape a pad audition takes (no note row).
void auditionSynth(int32_t note) {
	auto* synth = static_cast<SoundInstrument*>(g_clip->output);
	char memory[MODEL_STACK_MAX_SIZE];
	ModelStackWithThreeMainThings* ms =
	    setupModelStackWithThreeMainThingsButNoNoteRow(memory, currentSong, synth, g_clip, &g_clip->paramManager);
	synth->Sound::noteOn(ms, &synth->arpeggiator, note, zeroMPEValues);
}

/// Add a note to g_row through the note-add entry point the pad press uses.
void addNote(int32_t pos, Action* action) {
	char memory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* ms = setupModelStackWithTimelineCounter(memory, currentSong, g_clip);
	ModelStackWithNoteRow* rowStack = ms->addNoteRow(g_rowIndex, g_row);
	(void)g_row->attemptNoteAdd(pos, kNoteLength, kVelocity, g_row->getDefaultProbability(),
	                            g_row->getDefaultIterance(), g_row->getDefaultFill(rowStack), rowStack, action);
}

/// Delete g_row's note at @p pos through the entry point the pad press uses.
void deleteNote(int32_t pos, Action* action) {
	char memory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* ms = setupModelStackWithTimelineCounter(memory, currentSong, g_clip);
	ModelStackWithNoteRow* rowStack = ms->addNoteRow(g_rowIndex, g_row);
	g_row->deleteNoteByPos(rowStack, pos, action);
}

/// Everything a leg needs before playback starts. Setup edits are not counted: the counts are taken after it.
bool setupLeg() {
	g_clip = nullptr;
	g_row = nullptr;
	g_rowIndex = -1;
	g_kit = nullptr;
	g_drum = nullptr;
	if (g_leg == Leg::ClipNew) {
		// The edit is the clip's creation; the clip already playing is the home clip.
		return true;
	}
	// A note row is deleted only from a kit clip (InstrumentClip::deleteNoteRow() treats the output as a Kit).
	g_clip = (g_leg == Leg::Drum || g_leg == Leg::RowDel || g_leg == Leg::RenderSet || g_leg == Leg::RenderAlloc
	          || g_leg == Leg::Choke)
	             ? makeKitClip()
	             : makeSynthClip();
	if (g_clip == nullptr) {
		return false;
	}
	switch (g_leg) {
	case Leg::RowDrag:
		// Two rows to swap, straight into the row vector (scale mode discards empty out-of-scale rows).
		return (g_clip->noteRows.insertNoteRowAtY(kRowNote) != nullptr
		        && g_clip->noteRows.insertNoteRowAtY(kRowNote + 2) != nullptr)
		           ? true
		           : fail("no memory for two note rows");
	case Leg::RenderAlloc: {
		// Notes on the drum's row every 24 ticks, so the drum starts and stops sounding on the render.
		Drum* drum = g_kit->firstDrum;
		g_row = (drum != nullptr) ? g_clip->getNoteRowForDrum(drum, &g_rowIndex) : nullptr;
		if (g_row == nullptr) {
			return fail("the kit clip has no note row for its drum");
		}
		// Four short notes spread over the loop: the drum falls silent between them, so each one re-enters the set.
		for (int32_t n = 0; n < 4; n++) {
			char memory[MODEL_STACK_MAX_SIZE];
			ModelStackWithTimelineCounter* ms = setupModelStackWithTimelineCounter(memory, currentSong, g_clip);
			ModelStackWithNoteRow* rowStack = ms->addNoteRow(g_rowIndex, g_row);
			(void)g_row->attemptNoteAdd(n * (g_clip->loopLength / 4), 1, kVelocity, g_row->getDefaultProbability(),
			                            g_row->getDefaultIterance(), g_row->getDefaultFill(rowStack), rowStack,
			                            nullptr);
		}
		return true;
	}
	case Leg::Choke: {
		// A second drum, and both in choke mode: sounding one fast-releases the other.
		SoundDrum* second = makeDrum(g_kit);
		if (second == nullptr) {
			return false;
		}
		static_cast<SoundDrum*>(g_kit->firstDrum)->polyphonic = PolyphonyMode::CHOKE;
		second->polyphonic = PolyphonyMode::CHOKE;
		g_drum = second;
		return true;
	}
	case Leg::VoiceMono:
		// MONO: a second note re-triggers the voice the render is already playing.
		static_cast<SoundInstrument*>(g_clip->output)->polyphonic = PolyphonyMode::MONO;
		auditionSynth(kRowNote);
		return true;
	case Leg::VoiceKill:
		auditionSynth(kRowNote);
		return true;
	case Leg::VoiceMultiRange: {
		// Two ranges on source 0, so one can be deleted.
		Source& source = static_cast<SoundInstrument*>(g_clip->output)->sources[0];
		if (source.getOrCreateFirstRange() == nullptr || source.ranges.insertMultiRange(1) == nullptr) {
			return fail("no memory for two multi-ranges");
		}
		auditionSynth(kRowNote);
		return true;
	}
	case Leg::Quantize:
		g_row = rowFor(kRowNote, &g_rowIndex);
		if (g_row == nullptr) {
			return false;
		}
		addNote(kNotePos + 3, nullptr);
		addNote(kNotePos + 29, nullptr);
		return true;
	case Leg::NoteAdd:
	case Leg::NoteDel:
	case Leg::Undo:
		g_row = rowFor(kRowNote, &g_rowIndex);
		if (g_row == nullptr) {
			return false;
		}
		if (g_leg != Leg::NoteAdd) {
			addNote(kNotePos, nullptr);
		}
		return true;
	case Leg::RowDel: {
		// The kit clip's one row, for its one drum.
		Drum* drum = g_kit->firstDrum;
		g_row = (drum != nullptr) ? g_clip->getNoteRowForDrum(drum, &g_rowIndex) : nullptr;
		return (g_row != nullptr) ? true : fail("the kit clip has no note row for its drum");
	}
	case Leg::BigRow:
		// Straight into the row vector: the song is in scale mode, where getOrCreateNoteRowForYNote() recalculates the
		// scale for an out-of-scale row and discards empty rows.
		for (int32_t n = 1; n <= kBigRowCount; n++) {
			if (g_clip->noteRows.insertNoteRowAtY(n) == nullptr) {
				return fail("no memory for a note row");
			}
		}
		return true;
	default:
		return true;
	}
}

/// The leg's one structural edit, through the model's own entry point.
void editLeg() {
	char memory[MODEL_STACK_MAX_SIZE];
	switch (g_leg) {
	case Leg::NoteAdd:
		g_step = "attemptNoteAdd";
		addNote(kNotePos, nullptr);
		break;
	case Leg::NoteDel:
		g_step = "deleteNoteByPos";
		deleteNote(kNotePos, nullptr);
		break;
	case Leg::RowAdd:
		g_step = "getOrCreateNoteRowForYNote";
		(void)rowFor(kAddedRowNote, nullptr);
		break;
	case Leg::RowDel: {
		g_step = "deleteNoteRow";
		ModelStackWithTimelineCounter* ms = setupModelStackWithTimelineCounter(memory, currentSong, g_clip);
		g_clip->deleteNoteRow(ms, g_rowIndex);
		g_row = nullptr;
		break;
	}
	case Leg::ClipNew:
		g_step = "createNewInstrumentClip";
		g_clip = makeSynthClip();
		break;
	case Leg::ClipDel: {
		g_step = "removeSessionClip";
		const int32_t index = currentSong->sessionClips.getIndexForClip(g_clip);
		currentSong->removeSessionClip(g_clip, index);
		g_clip = nullptr;
		break;
	}
	case Leg::ClipDup: {
		g_step = "cloneClip";
		const int32_t index = currentSong->sessionClips.getIndexForClip(g_clip);
		const int32_t y = index - currentSong->songViewYScroll;
		sessionView.cloneClip(static_cast<uint8_t>(y), static_cast<uint8_t>(y + 1));
		break;
	}
	case Leg::Arrange: {
		g_step = "clipInstances";
		Output* output = g_clip->output;
		const int32_t i = output->clipInstances.insertSorted(0).value_or(-1);
		if (i >= 0) {
			output->clipInstances[i].length = g_clip->loopLength;
			output->clipInstances[i].clip = g_clip;
			output->clipInstances.erase(output->clipInstances.begin() + i);
		}
		break;
	}
	case Leg::Automate: {
		g_step = "automationNode";
		ParamCollectionSummary* summary = g_clip->paramManager.getPatchedParamSetSummary();
		auto* params = static_cast<ParamSet*>(summary->paramCollection);
		AutoParam& param = params->params[deluge::modulation::params::LOCAL_VOLUME];
		const int32_t i = param.nodes.insertSorted(kNotePos).value_or(-1);
		if (i >= 0) {
			param.nodes.erase(param.nodes.begin() + i);
		}
		break;
	}
	case Leg::Drum:
		g_step = "addDrum";
		g_drum = makeDrum(g_kit);
		if (g_drum != nullptr) {
			g_step = "removeDrum";
			// Never sounded and in no note row: out of the kit, then handed to the render, as a drum delete does.
			g_kit->renderingDrums.remove(g_drum);
			g_kit->removeDrum(g_drum);
			g_drum->kit = nullptr;
			AudioEngine::retire(g_drum);
			g_drum = nullptr;
		}
		break;
	case Leg::Replace: {
		g_step = "replaceInstrument";
		ParamManagerForTimeline paramManager;
		Instrument* fresh = StorageManager::createNewInstrument(OutputType::SYNTH, &paramManager);
		if (fresh == nullptr) {
			fail("no replacement synth");
			break;
		}
		auto* synth = static_cast<SoundInstrument*>(fresh);
		synth->setupAsBlankSynth(&paramManager, false);
		currentSong->backUpParamManager(static_cast<ModControllableAudio*>(synth->toModControllable()), nullptr,
		                                &paramManager, true);
		currentSong->replaceInstrument(static_cast<Instrument*>(g_clip->output), fresh);
		break;
	}
	case Leg::Undo: {
		g_step = "deleteNoteByPos";
		Action* action = actionLogger.getNewAction(ActionType::NOTE_EDIT, ActionAddition::NOT_ALLOWED);
		if (action == nullptr) {
			fail("no action for the undo");
			break;
		}
		deleteNote(kNotePos, action);
		actionLogger.closeAction(ActionType::NOTE_EDIT);
		g_step = "undo";
		(void)actionLogger.undo();
		break;
	}
	case Leg::BigRow:
		g_step = "insertNoteRowAtY(0)";
		// Below every other row: the insert moves all kBigRowCount rows (and reallocates when full).
		(void)g_clip->noteRows.insertNoteRowAtY(0);
		break;
	case Leg::RowDrag:
		g_step = "scrollVertical_dragSelectedNoteRow";
		// The clip view's note-row drag, which swaps the dragged row with its neighbour.
		instrumentClipView.scrollVertical_dragSelectedNoteRow(g_clip, g_clip->output, false, 0, 1);
		break;
	case Leg::ClipMove: {
		g_step = "swapClips";
		// The session view's clip drag: the clip swaps places with the one above it.
		const int32_t index = currentSong->sessionClips.getIndexForClip(g_clip);
		currentSong->sessionClips.swapClips(index, index - 1);
		break;
	}
	case Leg::OutputMove:
		g_step = "moveOutput";
		// The arranger's output drag: the clip's output moves one place along the output list.
		currentSong->moveOutput(g_clip->output, (g_clip->output->next != nullptr) ? 1 : -1);
		break;
	case Leg::Quantize: {
		g_step = "quantize";
		char rowMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* ms = setupModelStackWithTimelineCounter(rowMemory, currentSong, g_clip);
		ModelStackWithNoteRow* rowStack = ms->addNoteRow(g_rowIndex, g_row);
		(void)g_row->quantize(rowStack, kNoteLength, 10);
		break;
	}
	case Leg::RenderSet: {
		g_step = "noteOn";
		// An audition's note-on, from the interaction tier: the drum starts rendering (its kit's rendering set gains
		// it) while the render may be walking that set.
		auto* drum = static_cast<SoundDrum*>(g_kit->firstDrum);
		NoteRow* row = g_clip->getNoteRowForDrum(drum);
		if (drum == nullptr || row == nullptr) {
			fail("the kit clip has no drum with a note row");
			break;
		}
		char drumMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithThreeMainThings* ms =
		    setupModelStackWithThreeMainThingsButNoNoteRow(drumMemory, currentSong, drum, g_clip, &row->paramManager);
		drum->Sound::noteOn(ms, &drum->arpeggiator, kRowNote, zeroMPEValues);
		break;
	}
	case Leg::Choke: {
		g_step = "choke";
		// The two drums sound in turn from the interaction tier, each choking the other, while the render walks the
		// kit's rendering drums as they start and stop.
		auto* first = static_cast<SoundDrum*>(g_kit->firstDrum);
		NoteRow* row = g_clip->getNoteRowForDrum(first);
		if (row == nullptr) {
			fail("the kit clip has no note row for its first drum");
			break;
		}
		for (int32_t n = 0; n < 40; n++) {
			SoundDrum* drum = (n % 2 == 0) ? first : g_drum;
			char drumMemory[MODEL_STACK_MAX_SIZE];
			ModelStackWithThreeMainThings* ms = setupModelStackWithThreeMainThingsButNoNoteRow(
			    drumMemory, currentSong, drum, g_clip, &row->paramManager);
			drum->noteOn(ms, kVelocity, zeroMPEValues);
			renderBlocks(10);
		}
		break;
	}
	case Leg::VoicePoly: {
		g_step = "noteOn";
		// More notes than the synth's voice limit: the limit is enforced as each one is published.
		auto* synth = static_cast<SoundInstrument*>(g_clip->output);
		for (int32_t n = 0; n < synth->maxVoiceCount + 2; n++) {
			auditionSynth(kRowNote + n);
		}
		break;
	}
	case Leg::VoiceMono:
		g_step = "noteOn";
		auditionSynth(kRowNote + 2);
		break;
	case Leg::VoiceKill: {
		g_step = "killAllVoices";
		// The oscillator-type menu's shape (gui/menu_item/osc/type.h): end the voices, then change the type.
		auto* synth = static_cast<SoundInstrument*>(g_clip->output);
		synth->killAllVoices();
		synth->sources[0].setOscType(OscType::SQUARE);
		break;
	}
	case Leg::VoiceMultiRange:
		g_step = "deleteMultiRange";
		static_cast<SoundInstrument*>(g_clip->output)->deleteMultiRange(0, 1);
		break;
	case Leg::RenderAlloc: {
		g_step = "render";
		const uint32_t before = deluge::memory::audioTierAllocations();
		renderBlocks(kRenderAllocBlocks);
		printf("CG-RENDERALLOC renderAllocs=%u\n",
		       static_cast<unsigned>(deluge::memory::audioTierAllocations() - before));
		fflush(stdout);
		break;
	}
	case Leg::Done:
		break;
	}
	g_step = nullptr;
}

void printLine(bool aborted) {
	const uint32_t total = deluge::unguardedStructuralEditsTotal() - g_totalBefore;
	char kinds[160] = "";
	size_t used = 0;
	for (size_t k = 0; k < static_cast<size_t>(StructuralContainer::COUNT_); k++) {
		const uint32_t delta = deluge::unguardedStructuralEdits(static_cast<StructuralContainer>(k)) - g_before[k];
		if (delta != 0 && used < sizeof(kinds)) {
			used += static_cast<size_t>(snprintf(kinds + used, sizeof(kinds) - used, "%s%s=%u", used ? "," : "",
			                                     kindName(static_cast<StructuralContainer>(k)),
			                                     static_cast<unsigned>(delta)));
		}
	}
	char line[384];
	const int n =
	    snprintf(line, sizeof(line), "CG-%s unguarded=%u kinds=%s%s%s\n", legName(g_leg), static_cast<unsigned>(total),
	             used ? kinds : "-", aborted ? " aborted=1 step=" : "", aborted ? (g_step ? g_step : "?") : "");
	fflush(stdout);
	if (n > 0) {
		(void)write(STDOUT_FILENO, line, static_cast<size_t>(std::min<int>(n, sizeof(line) - 1)));
	}
}

/// Installed around an edit and the blocks after it: a freeze (FREEZE_WITH_ERROR aborts on the host BSP) or a fault
/// still reports the leg, marked aborted=1 with its step, before the process dies with the signal.
/// @note Not async-signal-safe (snprintf, fflush); accepted for a host-only diagnostic that is about to die anyway.
void reportAborted(int signal) {
	printLine(true);
	char line[64];
	const int n = snprintf(line, sizeof(line), "CG-RESULT failures=%d\n", g_failures);
	if (n > 0) {
		(void)write(STDOUT_FILENO, line, static_cast<size_t>(n));
	}
	std::signal(signal, SIG_DFL);
	std::raise(signal);
}

void cleanUpLeg() {
	if (playbackHandler.isEitherClockActive()) {
		playbackHandler.endPlayback();
	}
	changeRootUI(&sessionView);
	currentSong->setCurrentClip(g_home);
	view.setActiveModControllableTimelineCounter(g_home);
	g_clip = nullptr;
	g_row = nullptr;
}

void runTick() {
	switch (g_stage) {
	case Stage::Setup:
		if (g_leg == Leg::Done) {
			g_stage = Stage::Summary;
			break;
		}
		if (!setupLeg()) {
			g_stage = Stage::Summary;
			break;
		}
		playbackHandler.setupPlaybackUsingInternalClock(0, false);
		g_timerAtStart = AudioEngine::audioSampleTimer;
		g_blocks = 0;
		g_stage = Stage::Before;
		break;
	case Stage::Before: {
		renderBlocks(1);
		// The first tick of this playback is told by its time: lastTimerTickActioned keeps the previous playback's
		// value until then.
		const bool ticked =
		    static_cast<int32_t>(static_cast<uint32_t>(playbackHandler.timeLastTimerTickBig >> 32) - g_timerAtStart)
		    >= 0;
		const auto ticks = ticked ? static_cast<int32_t>(playbackHandler.lastTimerTickActioned) : 0;
		if (++g_blocks < kBlocksBefore || (ticks <= 0 && g_blocks < kMaxBlocksBefore)) {
			break;
		}
		if (ticks <= 0) {
			fail("the render actioned no ticks: the clock never ran");
			g_stage = Stage::Summary;
			break;
		}
		g_stage = Stage::Edit;
		break;
	}
	case Stage::Edit: {
		g_totalBefore = deluge::unguardedStructuralEditsTotal();
		for (size_t k = 0; k < static_cast<size_t>(StructuralContainer::COUNT_); k++) {
			g_before[k] = deluge::unguardedStructuralEdits(static_cast<StructuralContainer>(k));
		}
		std::signal(SIGABRT, reportAborted);
		std::signal(SIGSEGV, reportAborted);
		const auto editStart = std::chrono::steady_clock::now();
		editLeg();
		const auto editMicros =
		    std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - editStart).count();
		if (g_leg == Leg::BigRow) {
			// The whole edit's host wall time: an upper bound on the section it holds -- the row vector's insert,
			// which moves every row -- against a render block of 128 frames, some 2900 us.
			printf("CG-MASK leg=BIGROW rows=%d maskMicros=%lld\n", static_cast<int>(g_clip->noteRows.size()),
			       static_cast<long long>(editMicros));
			fflush(stdout);
		}
		renderBlocks(kBlocksAfter);
		std::signal(SIGABRT, SIG_DFL);
		std::signal(SIGSEGV, SIG_DFL);
		printLine(false);
		g_stage = Stage::After;
		break;
	}
	case Stage::After:
		cleanUpLeg();
		g_leg = static_cast<Leg>(static_cast<uint8_t>(g_leg) + 1);
		g_stage = Stage::Setup;
		break;
	case Stage::Summary:
		printf("CG-RESULT failures=%d\n", g_failures);
		fflush(stdout);
		g_summaryPrinted = true;
		break;
	}
}

} // namespace

extern "C" {

void deluge_scenario_container_guard_begin(void) {
	setvbuf(stdout, nullptr, _IOLBF, 0);
	g_stage = Stage::Setup;
	g_leg = Leg::NoteAdd;
	g_stepDone = false;
	g_summaryPrinted = false;
	g_failures = 0;
	// Every leg's gesture is made from the session view (the clip view the harness boots into dereferences
	// getCurrentInstrumentClip() unconditionally).
	changeRootUI(&sessionView);
	g_home = currentSong->sessionClips.size() > 0 ? currentSong->sessionClips[0] : nullptr;
	if (g_home == nullptr) {
		fail("the boot song has no clip");
		g_stage = Stage::Summary;
	}
}

bool deluge_scenario_container_guard_complete(void) {
	return g_summaryPrinted;
}

void deluge_scenario_container_guard_dispatch_step(void) {
	if (g_summaryPrinted) {
		return;
	}
	g_stepDone = false;
	runTick();
	g_stepDone = true;
}

bool deluge_scenario_container_guard_step_done(void) {
	return g_stepDone;
}

int32_t deluge_scenario_container_guard_failures(void) {
	return g_failures;
}

} // extern "C"

#endif // DELUGE_HOST
