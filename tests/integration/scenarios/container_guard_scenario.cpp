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

#include "scenarios/container_guard_scenario.h"

#include "definitions_cxx.hpp"
#include "deluge-project/param_automation.h"
#include "engines/effects/granular.h"
#include "engines/effects/mod_fx.h"
#include "engines/effects/words.h"
#include "gui/ui/ui.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/session_view.h"
#include "gui/views/view.h"
#include "libdeluge/alloc.h"
#include "libdeluge/app.h"
#include "libdeluge/types.h"
#include "memory/fast_allocator.h"
#include "memory/general_memory_allocator.h"
#include "memory/heaps.h"
#include "model/action/action.h"
#include "model/action/action_logger.h"
#include "model/clip/audio_clip.h"
#include "model/clip/clip_instance.h"
#include "model/clip/instrument_clip.h"
#include "model/edits/change_effect.h"
#include "model/edits/edit.h"
#include "model/edits/param_address_for.h"
#include "model/edits/set_param_value.h"
#include "model/global_effectable/global_effectable.h"
#include "model/instrument/kit.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/output.h"
#include "model/song/song.h"
#include "model/voice/voice.h"
#include "modulation/automation/auto_param.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_set.h"
#include "modulation/patch/patch_cable_set.h"
#include "playback/playback_handler.h"
#include "processing/audio_output.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"
#include "processing/source.h"
#include "serialization/value.h"
#include "storage/preset_acquisition.h"
#include "storage/storage_manager.h"
#include "util/structural_edit_probe.h"
#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <unistd.h>
#include <vector>

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
/// RENDERALLOC then renders this many blocks of a synth's delay starting, resizing and abandoning its tapes.
constexpr int32_t kRenderAllocDelayBlocks = 2500;
/// The mod FX legs' knobs when they render a block through an owner's mod FX themselves: a moderate rate (UQ0.32 per
/// frame) and half depth (Q31).
constexpr int32_t kModFxRate = 0x00a00000;
constexpr int32_t kModFxDepth = 0x40000000;
/// Blocks of a ramp the mod FX legs render through an owner's mod FX: more frames than its ring holds, so a delay-line
/// effect reads back what it wrote.
constexpr int32_t kModFxBlocks = 8;

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
	PatchCable,
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
	VoiceFail,
	Heap,
	Slab,
	ModFxOom,
	ModFxClone,
	ModFxSwap,
	GrainOom,
	GrainSwap,
	DelayKnob,
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
AudioClip* g_audio = nullptr;
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
	case Leg::PatchCable:
		return "PATCHCABLE";
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
	case Leg::VoiceFail:
		return "VOICEFAIL";
	case Leg::Heap:
		return "HEAP";
	case Leg::Slab:
		return "SLAB";
	case Leg::ModFxOom:
		return "MODFXOOM";
	case Leg::ModFxClone:
		return "MODFXCLONE";
	case Leg::ModFxSwap:
		return "MODFXSWAP";
	case Leg::GrainOom:
		return "GRAINOOM";
	case Leg::GrainSwap:
		return "GRAINSWAP";
	case Leg::DelayKnob:
		return "DELAYKNOB";
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
	case StructuralContainer::HEAP:
		return "heap";
	case StructuralContainer::SLAB:
		return "slab";
	case StructuralContainer::ALLOC_METADATA:
		return "memguard";
	case StructuralContainer::PATCH_CABLES:
		return "patchcables";
	case StructuralContainer::MOD_FX_RING:
		return "modfxring";
	case StructuralContainer::GRAIN_BLOCK:
		return "grainblock";
	case StructuralContainer::DELAY_TAPE:
		return "delaytape";
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

/// The bytes of a mod FX ring, the allocation the mod FX legs make fail.
constexpr std::size_t kModFxRingBytes = deluge::effects::ModFx::Kernel::kRingFrames * sizeof(deluge::dsp::StereoSample);

/// @return Whether @p owner's mod FX holds its ring.
bool holdsRing(ModControllableAudio& owner) {
	return owner.modfx.holdsRing();
}

/// @return The ring index @p owner's mod FX writes next: it moves each block the render runs the effect.
int32_t ringWrite(ModControllableAudio& owner) {
	return owner.modfx.state().write;
}

/// @return Whether @p owner's mod FX, run as @p type over kModFxBlocks blocks of a ramp, changed any of them: the
/// owner's block as its render makes it, with @p params' offset and feedback.
bool changesBlock(ModControllableAudio& owner, ParamManager& params, ModFXType type) {
	static StereoSample block[kRenderBlockFrames]{};
	UnpatchedParamSet* unpatched = params.getUnpatchedParamSet();
	const deluge::effects::ModFxInputs in{
	    .active = type,
	    .rate = kModFxRate,
	    .depth = kModFxDepth,
	    .offset = unpatched->getValue(deluge::modulation::params::UNPATCHED_MOD_FX_OFFSET),
	    .feedback = unpatched->getValue(deluge::modulation::params::UNPATCHED_MOD_FX_FEEDBACK),
	    .stereo = AudioEngine::renderInStereo,
	    .sound = true,
	};
	bool changed = false;
	int32_t volume = ONE_Q31 >> 1;
	for (int32_t b = 0; b < kModFxBlocks; b++) {
		for (uint32_t i = 0; i < kRenderBlockFrames; i++) {
			const auto v = static_cast<int32_t>((static_cast<uint32_t>(b) * kRenderBlockFrames + i) << 20);
			block[i] = {v, -v};
		}
		owner.modfx.render(block, in, volume);
		for (uint32_t i = 0; i < kRenderBlockFrames; i++) {
			const auto v = static_cast<int32_t>((static_cast<uint32_t>(b) * kRenderBlockFrames + i) << 20);
			changed = changed || block[i].l != v || block[i].r != -v;
		}
	}
	return changed;
}

/// The bytes of the grain's ring, the allocation the grain legs make fail.
constexpr std::size_t kGrainRingBytes =
    deluge::effects::Granular::Kernel::kRingFrames * sizeof(deluge::dsp::StereoSample);

/// What the song master's effects stage made of kModFxBlocks blocks of a ramp: each block's frames.
struct MasterFxRender {
	std::vector<StereoSample> frames; ///< kModFxBlocks blocks, one after the other.

	/// @return Whether this render's audio differs from @p other's in any frame. The post-FX volume is not compared: a
	/// mod FX type scales the post-FX volume for the filters' headroom whether or not it holds a ring.
	[[nodiscard]] bool differsFrom(const MasterFxRender& other) const {
		return !std::equal(frames.begin(), frames.end(), other.frames.begin(), other.frames.end(),
		                   [](const StereoSample& a, const StereoSample& b) { return a.l == b.l && a.r == b.r; });
	}
};

/// @return The blocks of a ramp as the song master's real effects stage makes them, under @p params' knobs and the
/// master's current type: GlobalEffectable::processFXForGlobalEffectable, which Song::renderAllOutputs calls on the
/// master's mix each block, over kModFxBlocks blocks.
MasterFxRender renderMasterFx(GlobalEffectable& master, ParamManager& params) {
	static q31_t send[kRenderBlockFrames]{};
	MasterFxRender made;
	const deluge::effects::Delay::State delayState = master.createDelayWorkingState(params);
	for (int32_t b = 0; b < kModFxBlocks; b++) {
		StereoSample block[kRenderBlockFrames]{};
		for (uint32_t i = 0; i < kRenderBlockFrames; i++) {
			const auto v = static_cast<int32_t>((static_cast<uint32_t>(b) * kRenderBlockFrames + i) << 20);
			block[i] = {v, -v};
		}
		int32_t volume = ONE_Q31 >> 1;
		master.processFXForGlobalEffectable(block, &volume, &params, delayState, true, 0, send);
		made.frames.insert(made.frames.end(), std::begin(block), std::end(block));
	}
	return made;
}

/// @return The ring index @p owner's grain writes next, or -1 before its State is taken: it moves each block the
/// render runs the effect.
int64_t grainWrite(ModControllableAudio& owner) {
	const auto* state = owner.granular.state();
	return state == nullptr ? -1 : static_cast<int64_t>(state->write);
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
	currentSong->live.param_backup.backUp(static_cast<ModControllableAudio*>(synth->toModControllable()), nullptr,
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
	currentSong->live.param_backup.backUp(drum, nullptr, &paramManager, true);
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
	currentSong->live.param_backup.backUp(static_cast<ModControllableAudio*>(kit->toModControllable()), nullptr,
	                                      &kitParams, true);
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

/// @brief Turns param @p id of @p owner to @p value as the gold knob does, by its SetParamValue Edit: a Sound's patched
/// param, or a chain's unpatched one, in @p paramManager.
void turnParam(ModelStackWithThreeMainThings* ms, int32_t id, int32_t value) {
	ParamManager& paramManager = *ms->paramManager;
	ParamCollectionSummary* summary = static_cast<ModControllableAudio*>(ms->modControllable)->asSound() != nullptr
	                                      ? paramManager.getPatchedParamSetSummary()
	                                      : paramManager.getUnpatchedParamSetSummary();
	ModelStackWithParamId* withId = ms->addParamCollectionAndId(summary->paramCollection, summary, id);
	ModelStackWithAutoParam* withParam = withId->paramCollection->getAutoParamFromId(withId);
	deluge::edits::Context context{*currentSong, currentSong->project};
	deluge::edits::SetParamValue edit{deluge::edits::addressFor(*withParam), value, 0, 0, withParam};
	deluge::edits::apply(context, edit);
}

/// @brief turnParam on @p owner with no note row: a synth's sound or a chain, its params in @p paramManager.
void turnParam(ModControllableAudio& owner, TimelineCounter* clip, ParamManager& paramManager, int32_t id,
               int32_t value) {
	char memory[MODEL_STACK_MAX_SIZE];
	turnParam(setupModelStackWithThreeMainThingsButNoNoteRow(memory, currentSong, &owner, clip, &paramManager), id,
	          value);
}

/// The delay feedback param of @p owner: a Sound's patched GLOBAL_DELAY_FEEDBACK, or a chain's unpatched
/// UNPATCHED_DELAY_AMOUNT.
int32_t delayFeedbackId(ModControllableAudio& owner) {
	return owner.asSound() != nullptr ? static_cast<int32_t>(deluge::modulation::params::GLOBAL_DELAY_FEEDBACK)
	                                  : static_cast<int32_t>(deluge::modulation::params::UNPATCHED_DELAY_AMOUNT);
}

/// @brief Turns @p owner's delay feedback to @p value as the gold knob does.
void turnDelayFeedback(ModControllableAudio& owner, TimelineCounter* clip, ParamManager& paramManager, int32_t value) {
	turnParam(owner, clip, paramManager, delayFeedbackId(owner), value);
}

/// @brief Sets @p owner's delay type by the Edit the delay type menu makes.
void setDelayType(ModControllableAudio& owner, deluge::effects::DelayType type) {
	deluge::edits::changeEffect(
	    owner, "delay/type", deluge::serialization::Value::makeScalar(std::string(deluge::effects::to_string(type))));
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
	g_audio = nullptr;
	if (g_leg == Leg::ClipNew) {
		// The edit is the clip's creation; the clip already playing is the home clip.
		return true;
	}
	if (g_leg == Leg::ModFxClone) {
		// An audio track whose chain runs chorus with its depth above minimum, so the chorus is on.
		Clip* clip = sessionView.createNewAudioClipForHarness(kAppendY);
		if (clip == nullptr || clip->type != ClipType::AUDIO) {
			return fail("no audio clip");
		}
		clip->activeIfNoSolo = true;
		g_audio = static_cast<AudioClip*>(clip);
		auto* output = static_cast<AudioOutput*>(clip->output);
		clip->paramManager.getUnpatchedParamSet()
		    ->params[deluge::modulation::params::UNPATCHED_MOD_FX_DEPTH]
		    .setCurrentValueBasicForSetup(0);
		output->currentModFXParam = ModFXParam::DEPTH;
		deluge::edits::changeEffect(
		    *output, "modFXType",
		    deluge::serialization::Value::makeScalar(std::string(deluge::effects::to_string(ModFXType::CHORUS))));
		return (output->effects().mod_fx.type == ModFXType::CHORUS) ? true : fail("the audio track took no chorus");
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
			(void)g_row->attemptNoteAdd(n * (g_clip->loopLength() / 4), 1, kVelocity, g_row->getDefaultProbability(),
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
	case Leg::VoiceFail: {
		// Ring-mod with a sample source that has no sample: Sound::noteOn lets a ring-mod note through, and the voice's
		// own note-on fails on the inactive source (Voice::noteOn returns false only in ring-mod).
		auto* synth = static_cast<SoundInstrument*>(g_clip->output);
		synth->setSynthMode(SynthMode::RINGMOD, currentSong);
		synth->sources[0].setOscType(OscType::SAMPLE);
		return true;
	}
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
		const int32_t index = currentSong->live.clips.session_clips.getIndexForClip(g_clip);
		currentSong->removeSessionClip(g_clip, index);
		g_clip = nullptr;
		break;
	}
	case Leg::ClipDup: {
		g_step = "cloneClip";
		const int32_t index = currentSong->live.clips.session_clips.getIndexForClip(g_clip);
		const int32_t y = index - currentSong->persistentView.songViewYScroll;
		sessionView.cloneClip(static_cast<uint8_t>(y), static_cast<uint8_t>(y + 1));
		break;
	}
	case Leg::Arrange: {
		g_step = "clipInstances";
		Output* output = g_clip->output;
		const int32_t i = output->clipInstances.insertSorted(0).value_or(-1);
		if (i >= 0) {
			output->clipInstances[i].length = g_clip->loopLength();
			output->clipInstances[i].clip = g_clip;
			output->clipInstances.erase(output->clipInstances.begin() + i);
		}
		break;
	}
	case Leg::Automate: {
		g_step = "automationNode";
		ParamCollectionSummary* summary = g_clip->paramManager.getPatchedParamSetSummary();
		auto* params = static_cast<ParamSet*>(summary->paramCollection);
		// The node store the render walks is the DocAutoParam's, which the engine param borrows.
		deluge::project::DocAutoParam* doc = params->docForParam(deluge::modulation::params::LOCAL_VOLUME);
		if (doc == nullptr) {
			break;
		}
		const int32_t i = doc->nodes.insertSorted(kNotePos).value_or(-1);
		if (i >= 0) {
			doc->nodes.erase(doc->nodes.begin() + i);
		}
		break;
	}
	case Leg::PatchCable: {
		// A cable added and removed again through the set's own entry points, the sound editor's and a knob's: each
		// grows or shrinks the cable storage and re-runs setupPatching(), which reorders the cables the render walks.
		g_step = "addPatchCable";
		auto* synth = static_cast<SoundInstrument*>(g_clip->output);
		char memory[MODEL_STACK_MAX_SIZE];
		ModelStackWithThreeMainThings* three =
		    setupModelStackWithThreeMainThingsButNoNoteRow(memory, currentSong, synth, g_clip, &g_clip->paramManager);
		ModelStackWithParamCollection* ms =
		    three->addParamCollectionSummary(g_clip->paramManager.getPatchCableSetSummary());
		auto* cables = static_cast<PatchCableSet*>(ms->paramCollection);
		const uint8_t before = cables->numPatchCables;
		ParamDescriptor destination;
		destination.setToHaveParamOnly(deluge::modulation::params::LOCAL_PITCH_ADJUST);
		const uint8_t c = cables->getPatchCableIndex(PatchSource::ENVELOPE_1, destination, ms, true);
		if (c == 255 || cables->numPatchCables != before + 1) {
			fail("the patch cable was not added");
			break;
		}
		g_step = "deletePatchCable";
		cables->deletePatchCable(ms, c);
		if (cables->numPatchCables != before) {
			fail("the patch cable was not deleted");
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
		currentSong->live.param_backup.backUp(static_cast<ModControllableAudio*>(synth->toModControllable()), nullptr,
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
		const int32_t index = currentSong->live.clips.session_clips.getIndexForClip(g_clip);
		currentSong->live.clips.session_clips.swapClips(index, index - 1);
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
	case Leg::VoiceFail: {
		g_step = "noteOn";
		// The failed note-on's voice goes back to the pool and was never in voices_.
		auto* synth = static_cast<SoundInstrument*>(g_clip->output);
		const size_t freeBefore = AudioEngine::VoicePool::get().size();
		const size_t voicesBefore = synth->voices().size();
		auditionSynth(kRowNote);
		// voicesDelta=0: the voice was never published, so its note-on failed. lastNoteSet=1: the note-on still ran to
		// its end (portamento's last note, MIDI out), as it did before a failed voice returned early.
		printf("CG-VOICEFAIL poolFreeDelta=%d voicesDelta=%d lastNoteSet=%d\n",
		       static_cast<int>(AudioEngine::VoicePool::get().size()) - static_cast<int>(freeBefore),
		       static_cast<int>(synth->voices().size()) - static_cast<int>(voicesBefore),
		       synth->lastNoteCode == kRowNote ? 1 : 0);
		fflush(stdout);
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
		// Then the drum's own delay, sounding with each of its notes: it starts, has its rate stepped past double, down
		// and below half (each a new tape grown and swapped in), and abandons its tapes between the notes.
		constexpr int32_t kRate = static_cast<int32_t>(deluge::modulation::params::GLOBAL_DELAY_RATE);
		auto* drum = static_cast<SoundDrum*>(g_kit->firstDrum);
		const auto turnDrum = [&](int32_t id, int32_t value) {
			char memory[MODEL_STACK_MAX_SIZE];
			turnParam(setupModelStackWithTimelineCounter(memory, currentSong, g_clip)
			              ->addNoteRow(g_rowIndex, g_row)
			              ->addOtherTwoThings(drum, &g_row->paramManager),
			          id, value);
		};
		setDelayType(*drum, deluge::effects::DelayType::DIGITAL);
		turnDrum(delayFeedbackId(*drum), -0x60000000);
		turnDrum(kRate, 0);
		int32_t starts = 0;
		int32_t stops = 0;
		bool was = drum->delay.isActive();
		for (int32_t b = 0; b < kRenderAllocDelayBlocks; b++) {
			if (b == 400) {
				turnDrum(kRate, 0x30000000);
			}
			else if (b == 800) {
				turnDrum(kRate, -0x40000000);
			}
			renderBlocks(1);
			const bool now = drum->delay.isActive();
			starts += (!was && now) ? 1 : 0;
			stops += (was && !now) ? 1 : 0;
			was = now;
		}
		printf("CG-RENDERALLOC renderAllocs=%u delayStarts=%d delayStops=%d\n",
		       static_cast<unsigned>(deluge::memory::audioTierAllocations() - before), static_cast<int>(starts),
		       static_cast<int>(stops));
		fflush(stdout);
		turnDrum(delayFeedbackId(*drum), std::numeric_limits<int32_t>::min());
		turnDrum(kRate, 0);
		setDelayType(*drum, deluge::effects::DelayType::OFF);
		renderBlocks(1);
		break;
	}
	case Leg::Heap: {
		g_step = "alloc";
		// The interaction tier allocating and freeing while the render plays: every heap step must be sectioned.
		void* fast = deluge::memory::alloc_fast(64, 16);
		void* sdram = delugeAlloc(4096, false);
		delugeDealloc(sdram);
		deluge::memory::dealloc(fast);
		break;
	}
	case Leg::Slab: {
		g_step = "slab";
		// A cluster-slab slot claimed, touched and released from the interaction tier while the render plays.
		DelugeSlab* slab = GeneralMemoryAllocator::get().clusterSlab();
		if (slab == nullptr) {
			fail("no cluster slab");
			break;
		}
		void* slot = deluge_slab_acquire(slab, nullptr);
		if (slot == nullptr) {
			fail("cluster slab acquire failed");
			break;
		}
		deluge_slab_touch(slab, slot);
		GeneralMemoryAllocator::get().freeSdram(slot); // releases through the slab
		break;
	}
	case Leg::ModFxOom: {
		g_step = "modFXType";
		const uint32_t notedBefore = deluge::notedStructuralEdits(StructuralContainer::MOD_FX_RING);
		// The song master's chorus with its ring's allocation made to fail: the type is set and its blocks pass the
		// signal through. A change to the dimension then takes a ring, and the master's render runs the effect over it.
		auto& fx = currentSong->live.global_effectable;
		currentSong->paramManager.getUnpatchedParamSet()
		    ->params[deluge::modulation::params::UNPATCHED_MOD_FX_DEPTH]
		    .setCurrentValueBasicForSetup(0);
		// Which mod FX parameter gates the effect on: the song's default names feedback, which sits at its minimum.
		const ModFXParam paramBefore = fx.currentModFXParam;
		fx.currentModFXParam = ModFXParam::DEPTH;
		if (fx.effects().mod_fx.type != ModFXType::NONE) {
			fail("the master's mod FX is not off");
			break;
		}
		// The master's dry render: its effects stage over a ramp with no mod FX.
		const MasterFxRender dry = renderMasterFx(fx, currentSong->paramManager);
		deluge::memory::failNextAllocationOfSize(kModFxRingBytes);
		fx.setModFXType(ModFXType::CHORUS);
		const bool injected = !deluge::memory::allocationFailurePending();
		renderBlocks(4);
		const bool passthrough = !holdsRing(fx) && !renderMasterFx(fx, currentSong->paramManager).differsFrom(dry);
		fx.setModFXType(ModFXType::DIMENSION);
		const int32_t writeBefore = ringWrite(fx);
		renderBlocks(1);
		const bool recovered = holdsRing(fx) && ringWrite(fx) != writeBefore
		                       && renderMasterFx(fx, currentSong->paramManager).differsFrom(dry);
		fx.setModFXType(ModFXType::NONE);
		fx.currentModFXParam = paramBefore;
		renderBlocks(1);
		const bool noted = deluge::notedStructuralEdits(StructuralContainer::MOD_FX_RING) > notedBefore;
		printf("CG-MODFXOOM injected=%d passthrough=%d recovered=%d noted=%d\n", injected ? 1 : 0, passthrough ? 1 : 0,
		       recovered ? 1 : 0, noted ? 1 : 0);
		fflush(stdout);
		break;
	}
	case Leg::ModFxClone: {
		g_step = "cloneFrom";
		// The audio track's clone, as its clip's overdub clone and the session view's clone into a new track make one:
		// a new audio track whose chain is a copy of the chorus one. It holds its own ring from the clone on, before
		// any block renders, and runs the chorus over it.
		auto* source = static_cast<AudioOutput*>(g_audio->output);
		AudioOutput* clone = currentSong->createNewAudioOutput();
		if (clone != nullptr) {
			clone->cloneFrom(source);
		}
		const bool ring = clone != nullptr && clone->effects().mod_fx.type == ModFXType::CHORUS && holdsRing(*clone);
		const bool renders = clone != nullptr && changesBlock(*clone, g_audio->paramManager, ModFXType::CHORUS);
		printf("CG-MODFXCLONE ring=%d renders=%d\n", ring ? 1 : 0, renders ? 1 : 0);
		fflush(stdout);
		break;
	}
	case Leg::ModFxSwap: {
		g_step = "modFXType";
		// The mod FX type menu's Edit on a synth's chain while the song plays: each change to a delay-line type holds
		// a ring and each change away from one gives it back.
		auto* synth = static_cast<SoundInstrument*>(g_clip->output);
		const uint32_t notedBefore = deluge::notedStructuralEdits(StructuralContainer::MOD_FX_RING);
		bool matched = true;
		for (const ModFXType type : {ModFXType::CHORUS, ModFXType::FLANGER, ModFXType::PHASER, ModFXType::CHORUS}) {
			deluge::edits::changeEffect(
			    *synth, "modFXType",
			    deluge::serialization::Value::makeScalar(std::string(deluge::effects::to_string(type))));
			renderBlocks(2);
			matched = matched && synth->effects().mod_fx.type == type
			          && holdsRing(*synth) == deluge::effects::is_ring_type(type);
		}
		const bool noted = deluge::notedStructuralEdits(StructuralContainer::MOD_FX_RING) > notedBefore;
		printf("CG-MODFXSWAP rings=%d noted=%d\n", matched ? 1 : 0, noted ? 1 : 0);
		fflush(stdout);
		break;
	}
	case Leg::GrainOom: {
		g_step = "modFXType";
		const uint32_t notedBefore = deluge::notedStructuralEdits(StructuralContainer::GRAIN_BLOCK);
		// The song master's grain with its ring's allocation made to fail: the type is set and its blocks pass the
		// signal through. Setting the grain again takes a ring, and the master's render runs the effect over it.
		auto& fx = currentSong->live.global_effectable;
		currentSong->paramManager.getUnpatchedParamSet()
		    ->params[deluge::modulation::params::UNPATCHED_MOD_FX_DEPTH]
		    .setCurrentValueBasicForSetup(0);
		const ModFXParam paramBefore = fx.currentModFXParam;
		fx.currentModFXParam = ModFXParam::DEPTH;
		if (fx.effects().mod_fx.type != ModFXType::NONE) {
			fail("the master's mod FX is not off");
			break;
		}
		// The master's dry render: its effects stage over a ramp with no mod FX.
		const MasterFxRender dry = renderMasterFx(fx, currentSong->paramManager);
		deluge::memory::failNextAllocationOfSize(kGrainRingBytes);
		fx.setModFXType(ModFXType::GRAIN);
		const bool injected = !deluge::memory::allocationFailurePending();
		renderBlocks(4);
		const bool passthrough =
		    !fx.granular.holdsRing() && !renderMasterFx(fx, currentSong->paramManager).differsFrom(dry);
		fx.setModFXType(ModFXType::GRAIN);
		const int64_t writeBefore = grainWrite(fx);
		renderBlocks(1);
		const bool recovered = fx.granular.holdsRing() && grainWrite(fx) != writeBefore
		                       && renderMasterFx(fx, currentSong->paramManager).differsFrom(dry);
		fx.setModFXType(ModFXType::NONE);
		fx.currentModFXParam = paramBefore;
		renderBlocks(1);
		const bool noted = deluge::notedStructuralEdits(StructuralContainer::GRAIN_BLOCK) > notedBefore;
		printf("CG-GRAINOOM injected=%d passthrough=%d recovered=%d noted=%d\n", injected ? 1 : 0, passthrough ? 1 : 0,
		       recovered ? 1 : 0, noted ? 1 : 0);
		fflush(stdout);
		break;
	}
	case Leg::DelayKnob: {
		g_step = "delayFeedback";
		// A knob lifting an OFF delay's feedback off its minimum turns it on, digital; one already on, or set OFF
		// with its feedback up, keeps its type. On a synth's sound, then on the song master's chain.
		using deluge::effects::DelayType;
		constexpr int32_t kMinimum = std::numeric_limits<int32_t>::min();
		auto* synth = static_cast<SoundInstrument*>(g_clip->output);
		ParamManager& synthParams = g_clip->paramManager;
		turnDelayFeedback(*synth, g_clip, synthParams, kMinimum);
		setDelayType(*synth, DelayType::OFF);
		turnDelayFeedback(*synth, g_clip, synthParams, 0);
		const bool lifted = synth->effects().delay.type == DelayType::DIGITAL;
		setDelayType(*synth, DelayType::ANALOG);
		turnDelayFeedback(*synth, g_clip, synthParams, kMinimum);
		turnDelayFeedback(*synth, g_clip, synthParams, 0);
		const bool analogKept = synth->effects().delay.type == DelayType::ANALOG;
		setDelayType(*synth, DelayType::OFF);
		turnDelayFeedback(*synth, g_clip, synthParams, 0x20000000);
		const bool offKept = synth->effects().delay.type == DelayType::OFF;
		turnDelayFeedback(*synth, g_clip, synthParams, kMinimum);

		auto& master = currentSong->live.global_effectable;
		turnDelayFeedback(master, nullptr, currentSong->paramManager, kMinimum);
		setDelayType(master, DelayType::OFF);
		turnDelayFeedback(master, nullptr, currentSong->paramManager, 0);
		const bool chain = master.effects().delay.type == DelayType::DIGITAL;
		turnDelayFeedback(master, nullptr, currentSong->paramManager, kMinimum);
		setDelayType(master, DelayType::OFF);
		renderBlocks(1);
		printf("CG-DELAYKNOB lifted=%d analogKept=%d offKept=%d chain=%d\n", lifted ? 1 : 0, analogKept ? 1 : 0,
		       offKept ? 1 : 0, chain ? 1 : 0);
		fflush(stdout);
		break;
	}
	case Leg::GrainSwap: {
		g_step = "modFXType";
		// The mod FX type menu's Edit on a synth's chain while the song plays: the grain holds its ring exactly while
		// its type is the grain, and the chorus between holds the mod FX ring.
		auto* synth = static_cast<SoundInstrument*>(g_clip->output);
		const uint32_t notedBefore = deluge::notedStructuralEdits(StructuralContainer::GRAIN_BLOCK);
		bool matched = true;
		for (const ModFXType type : {ModFXType::GRAIN, ModFXType::CHORUS, ModFXType::GRAIN, ModFXType::NONE}) {
			deluge::edits::changeEffect(
			    *synth, "modFXType",
			    deluge::serialization::Value::makeScalar(std::string(deluge::effects::to_string(type))));
			renderBlocks(2);
			matched = matched && synth->effects().mod_fx.type == type
			          && synth->granular.holdsRing() == (type == ModFXType::GRAIN)
			          && holdsRing(*synth) == deluge::effects::is_ring_type(type);
		}
		const bool noted = deluge::notedStructuralEdits(StructuralContainer::GRAIN_BLOCK) > notedBefore;
		printf("CG-GRAINSWAP blocks=%d noted=%d\n", matched ? 1 : 0, noted ? 1 : 0);
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
		if (g_leg == Leg::VoiceMono) {
			// The MONO re-trigger runs wholly inside one section: its host wall time bounds that section.
			printf("CG-MASK leg=VOICEMONO maskMicros=%lld\n", static_cast<long long>(editMicros));
			fflush(stdout);
		}
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
	g_home = currentSong->live.clips.session_clips.size() > 0 ? currentSong->live.clips.session_clips[0] : nullptr;
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
