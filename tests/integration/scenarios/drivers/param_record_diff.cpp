// Lean differential "record-playback" harness.
//
//   param_record_diff <song.XML>
//
// The load->save golden and song_edit_roundtrip cannot see the ENGINE's live recording path
// (setCurrentValueInResponseToUserInput + processCurrentPos + undo) — the code that forwards to the
// DOM kernels. This harness drives that path natively and deterministically, then dumps a canonical snapshot of
// the resulting automation. The oracle is a committed baseline: param_record_run.py diffs stdout against it, and the
// dump must reproduce it bit-for-bit. No real audio scheduler is driven — "time" is controlled by writing
// AudioEngine::audioSampleTimer and passing an explicit livePos.
//
// Targets: the song-global unpatched param UNPATCHED_VOLUME, recorded under RecordingMode::ARRANGEMENT (which makes
// Song::isPlayingAutomationNow() true and satisfies the whole `isPlaying && recording && armed` gate), undone, redone,
// saved and reloaded, then its latch drifting back as the arrangement plays on. Then clip lanes under
// RecordingMode::NORMAL, each take undone and redone: a synth's patched param, a kit row's, a MIDI CC, an MPE lane and
// a reversed clip's, and a latch drifting back across a clip lane's nodes. A clip records only while active, so the
// driver makes each one active and armed; a song without the clip kind prints `case <name> none`.
//
// A recorded value reaches its lane when the render next starts a block, so the driver starts one (marked as the
// render's) after each write and around each latch tick. Two probes ride along on stderr: no recorded write makes the
// render log undo (the latch still does, for now), and 70 writes with no block between them, past the render's queue,
// leave the lane as the same writes a block apart do.
//
// Exit: 0 = ran + dumped; 1 = load error; 2 = usage/IO; 3 = setup precondition or a probe failed.
//
// The dump is deterministic, so param_record_run.py byte-compares stdout against a committed baseline captured from
// engine-native recording.

#include "deluge-project/param_automation.h"
#include "deluge-project/param_edit.h"
#include "memory/general_memory_allocator.h"
#include "memory/heaps.h"
#include "model/action/action_logger.h"
#include "model/clip/instrument_clip.h"
#include "model/drum/drum.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/project_bridge/song_serialization.h"
#include "model/song/song.h"
#include "modulation/automation/auto_param.h"
#include "modulation/automation/record_events.h"
#include "modulation/params/param.h"
#include "modulation/params/param_set.h"
#include "playback/mode/arrangement.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "scenarios/drivers/tree_load.h"
#include "storage/storage_manager.h"
#include "storage/value_tree_deserializer.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "model/project_bridge/param_ids.h"
#include "scenarios/drivers/driver.h"

// A live scheduler global (playback_handler.cpp). When true, PlaybackHandler::expectEvent() no-ops instead of
// calling scheduleSwungTick() — which would reach into the RT scheduler that this driver deliberately does not
// run. Setting it true is exactly right here: we drive "ticks" manually, so there is no next event to schedule.
extern bool currentlyActioningSwungTickOrResettingPlayPos;

namespace {

using deluge::scenarios::driver::Io;

/// Where this driver's prints go: what its process wrote to stdout and stderr.
Io io{};

namespace params = deluge::modulation::params;

// Same native load as song_edit_roundtrip: build a Song, deserialize, set currentSong. `xml` must be writable
// (the reader null-terminates in place) and outlive all uses.
static Song* load_song(char* xml, size_t xmlLen, deluge::storage::DocumentFormat format, Error* loadErr) {
	void* mem = GeneralMemoryAllocator::get().alloc(sizeof(Song));
	Song* song = new (mem) Song();
	song->paramManager.setupUnpatched();
	GlobalEffectable::initParams(&song->paramManager);

	// Load via the record-first Value tree: parse the whole XML to a Value, then
	// replay it to readFromFile through ValueTreeDeserializer -- bit-identical to the streaming load.
	std::optional<deluge::storage::Value> tree =
	    parseDocument(std::string_view(reinterpret_cast<const char*>(xml), static_cast<size_t>(xmlLen)), format);
	Error e = Error::FILE_CORRUPTED;
	if (tree) {
		deluge::storage::ValueTreeDeserializer adapter(*tree, sourceFormatOf(format));
		activeReader = &adapter; // in-memory: gates the AudioEngine render off during load (as the stream path does)
		e = openTreeFile(adapter, "song", format);
		if (e == Error::NONE) {
			e = deluge::bridge::loadSong(*song, adapter);
		}
		activeReader = &smDeserializer;
	}
	*loadErr = e;
	if (e != Error::NONE) {
		return nullptr;
	}
	currentSong = song;
	song->setCurrentClip(nullptr);
	return song;
}

// Print a canonical, deterministic snapshot of one AutoParam's automation state. Order + formatting are fixed so
// the differ can byte-compare across builds/arches.
static void dumpParam(const char* label, ModelStackWithAutoParam* msap) {
	// The nodes are owner-resolved via the ModelStack (the song-global param is bound to the DOM store), not read
	// off the engine AutoParam, which holds no pointer into DOM node data.
	const auto& nodes = msap->doc()->nodes;
	std::fprintf(io.out, "%s cv=%ld nodes=%d:", label, (long)msap->autoParam->getCurrentValue(),
	             (int)std::ssize(nodes));
	for (int32_t i = 0; i < std::ssize(nodes); i++) {
		const ParamNode& n = nodes[i];
		std::fprintf(io.out, " (%ld,%ld,%c)", (long)n.pos, (long)n.value, n.interpolated ? 'i' : 's');
	}
	std::fprintf(io.out, "\n");
}

// Save `song` to an in-memory XML buffer, reload it into a fresh Song, re-fetch the song-global param, and dump
// it under "after-reload". Proves the recorded automation survives the DOM serialization round-trip.
static void dumpAfterSaveReload(Song* song) {
	const uint32_t cap = 16u * 1024 * 1024;
	char* buf = (char*)malloc(cap);
	long n = deluge::scenarios::driver::saveSongCapture(*song, buf, cap);

	char* copy = (char*)malloc(n + 1);
	memcpy(copy, buf, n);
	copy[n] = 0;

	Error e = Error::NONE;
	Song* reloaded = load_song(copy, n, deluge::storage::kSongSaveFormat, &e);
	if (!reloaded) {
		std::fprintf(io.out, "after-reload LOAD-FAILED err=%d\n", (int)e);
		free(buf);
		return;
	}
	alignas(ModelStackWithAutoParam) char msMem[MODEL_STACK_MAX_SIZE]{};
	ModelStackWithThreeMainThings* ms3 = reloaded->setupModelStackWithSongAsTimelineCounter(msMem);
	ModelStackWithAutoParam* msap = ms3->getUnpatchedAutoParamFromId(params::UNPATCHED_VOLUME);
	if (!msap || !msap->autoParam) {
		std::fprintf(io.out, "after-reload NO-PARAM\n");
		free(buf);
		return;
	}
	dumpParam("after-reload", msap);
	free(buf);
}

// A deterministic series of "knob turns" at explicit play positions (livePos passed directly, bypassing the
// play-position plumbing). Between events AudioEngine::audioSampleTimer advances to model elapsed time, which the
// override-latch logic reads.
struct Event {
	int32_t livePos;
	int32_t value;
	uint32_t timerAdvance;
};
constexpr Event script[] = {
    {0, 400000000, 100},   {12, 900000000, 100}, {24, -300000000, 100},
    {36, 1500000000, 100}, {48, 200000000, 100}, {60, -800000000, 100},
};

/// A probe failed: the run exits 3 once it has dumped everything.
bool probeFailed = false;

// What the render does at the start of a block, as a render block: write the values the interaction tier recorded.
void renderBlockStart() {
	deluge::memory::noteRenderBlock(true);
	deluge::modulation::applyRecordEvents(*currentSong);
	deluge::memory::noteRenderBlock(false);
}

/// Whether the render tier logged undo since @p before; a recorded write's snapshot is the interaction tier's.
bool renderLoggedUndo([[maybe_unused]] uint32_t before, [[maybe_unused]] const char* name) {
#if ALPHA_OR_BETA_VERSION
	if (deluge::modulation::renderUndoCalls != before) {
		std::fprintf(io.err, "%s: the render logged undo %u times\n", name,
		             static_cast<unsigned>(deluge::modulation::renderUndoCalls - before));
		return true;
	}
#endif
	return false;
}

uint32_t renderUndoCallsNow() {
#if ALPHA_OR_BETA_VERSION
	return deluge::modulation::renderUndoCalls;
#else
	return 0;
#endif
}

// Run the script through the live-record path. With @p loopLength non-zero the positions scale to 0..5/6 of the loop,
// so a clip of any length records the same shape.
void recordScript(ModelStackWithAutoParam* msap, int32_t loopLength, bool mpe) {
	for (const Event& e : script) {
		AudioEngine::audioSampleTimer += e.timerAdvance;
		const int32_t livePos = loopLength != 0 ? e.livePos * loopLength / 72 : e.livePos;
		msap->autoParam->setCurrentValueInResponseToUserInput(e.value, msap, /*shouldLogAction=*/true, livePos,
		                                                      /*mayDeleteNodesInLinearRun=*/!mpe, mpe);
		renderBlockStart();
	}
}

// Play the latched lane forward over @p positions as the render's tick does: each position reached, the latch
// rewrites the node there while recording.
void tickLatch(ModelStackWithAutoParam* msap, std::span<const int32_t> positions, bool reversed,
               int32_t& lastProcessedPos) {
	for (int32_t pos : positions) {
		lastProcessedPos = pos;
		AudioEngine::audioSampleTimer += 2000;
		deluge::memory::noteRenderBlock(true);
		msap->autoParam->processCurrentPos(msap, reversed, false, true, false);
		deluge::memory::noteRenderBlock(false);
		char label[32];
		snprintf(label, sizeof label, "tick %ld", (long)pos);
		dumpParam(label, msap);
	}
}

// One clip lane: record the script, then undo and redo the take.
void clipCase(const char* name, ModelStackWithAutoParam* msap, Clip* clip, bool mpe) {
	if (msap == nullptr || msap->autoParam == nullptr) {
		std::fprintf(io.out, "case %s none\n", name);
		return;
	}
	std::fprintf(io.out, "case %s len=%ld\n", name, (long)clip->loopLength());
	dumpParam("initial", msap);
	const uint32_t undoBefore = renderUndoCallsNow();
	recordScript(msap, clip->loopLength(), mpe);
	probeFailed |= renderLoggedUndo(undoBefore, name);
	dumpParam("after-record", msap);
	actionLogger.closeAction(ActionType::RECORD);
	actionLogger.revert(BEFORE, false, false);
	dumpParam("after-undo", msap);
	actionLogger.revert(AFTER, false, false);
	dumpParam("after-redo", msap);
}

template <typename Pred>
InstrumentClip* firstInstrumentClip(Song* song, Pred pred) {
	for (Clip* clip : song->live.clips.session_clips) {
		if (clip->type == ClipType::INSTRUMENT && clip->output != nullptr && pred(*clip->output)) {
			return static_cast<InstrumentClip*>(clip);
		}
	}
	return nullptr;
}

void arm(Clip* clip) {
	clip->activeIfNoSolo = true;
	clip->soloingInSessionMode = false;
	clip->armedForRecording = true;
}

// Writes made faster than the render's blocks: 70 values with no block between them, past the queue's 64, must leave
// the lane as the same 70 written one block apart; the ones the queue had no room for the interaction tier writes,
// after what was queued. Checked on stderr: the drained run's nodes are already pinned by the cases above.
void burstCase(Song* song, InstrumentClip* clip, ModelStackWithAutoParam* lane) {
	constexpr int32_t kWrites = 70;
	const int32_t len = clip->loopLength();
	// Undo gives back the lane's played value but not its base, which the cleared stretches return to: each run starts
	// from the same one.
	const int32_t base = lane->doc()->current_value;
	auto record = [&](bool blockEach) {
		lane->autoParam->setBase(*lane->doc(), base);
		for (int32_t i = 0; i < kWrites; i++) {
			AudioEngine::audioSampleTimer += 100;
			lane->autoParam->setCurrentValueInResponseToUserInput((i % 7) * 300000000 - 900000000, lane, true,
			                                                      i * len / (kWrites + 10), true, false);
			if (blockEach) {
				renderBlockStart();
			}
		}
		renderBlockStart();
		std::vector<ParamNode> nodes(lane->doc()->nodes.begin(), lane->doc()->nodes.end());
		actionLogger.closeAction(ActionType::RECORD);
		actionLogger.revert(BEFORE, false, false);
		return nodes;
	};
	const std::vector<ParamNode> apart = record(true);
#if ALPHA_OR_BETA_VERSION
	const uint32_t fallbacksBefore = deluge::modulation::recordRingFullFallbacks;
#endif
	const std::vector<ParamNode> burst = record(false);
	const bool same = std::ranges::equal(apart, burst, [](const ParamNode& a, const ParamNode& b) {
		return a.pos == b.pos && a.value == b.value && a.interpolated == b.interpolated;
	});
#if ALPHA_OR_BETA_VERSION
	const uint32_t fallbacks = deluge::modulation::recordRingFullFallbacks - fallbacksBefore;
#else
	const uint32_t fallbacks = 0;
#endif
	for (size_t i = 0; i < std::min(apart.size(), burst.size()); i++) {
		if (apart[i].pos != burst[i].pos || apart[i].value != burst[i].value
		    || apart[i].interpolated != burst[i].interpolated) {
			std::fprintf(io.err, "burst first diff at %zu: (%ld,%ld,%d) vs (%ld,%ld,%d)\n", i, (long)apart[i].pos,
			             (long)apart[i].value, apart[i].interpolated, (long)burst[i].pos, (long)burst[i].value,
			             burst[i].interpolated);
			break;
		}
	}
	std::fprintf(io.err, "burst: %s, %zu/%zu nodes, %u written by the interaction tier\n", same ? "same" : "DIFFERENT",
	             apart.size(), burst.size(), static_cast<unsigned>(fallbacks));
	probeFailed |= !same || apart.empty();
	(void)song;
}

// Clip lanes recorded under RecordingMode::NORMAL: a synth's patched param, a kit row's, a MIDI CC, an MPE lane and a
// reversed clip's, each with its take undone and redone; then the latch drifting back across a clip lane's nodes.
void clipCases(Song* song) {
	playbackHandler.recording = RecordingMode::NORMAL;
	InstrumentClip* synth = firstInstrumentClip(song, [](Output& o) { return o.type == OutputType::SYNTH; });
	InstrumentClip* kit = firstInstrumentClip(song, [](Output& o) { return o.type == OutputType::KIT; });
	InstrumentClip* midi = firstInstrumentClip(song, [](Output& o) { return o.type == OutputType::MIDI_OUT; });
	for (InstrumentClip* clip : {synth, kit, midi}) {
		if (clip != nullptr) {
			arm(clip);
		}
	}

	alignas(ModelStackWithAutoParam) char m[MODEL_STACK_MAX_SIZE]{};
	auto synthStack = [&](InstrumentClip* clip) {
		return setupModelStackWithThreeMainThingsIncludingNoteRow(
		    m, song, clip, 0, nullptr, clip->output->toModControllable(), &clip->paramManager);
	};

	if (synth != nullptr) {
		clipCase("synth-patched", synthStack(synth)->getPatchedAutoParamFromId(params::LOCAL_LPF_FREQ), synth, false);
	}
	else {
		std::fprintf(io.out, "case synth-patched none\n");
	}

	ModelStackWithAutoParam* row = nullptr;
	if (kit != nullptr) {
		for (int32_t i = 0; i < std::ssize(kit->noteRows) && row == nullptr; i++) {
			NoteRow& nr = kit->noteRows[i];
			if (nr.drum != nullptr && nr.drum->type == DrumType::SOUND) {
				row = setupModelStackWithThreeMainThingsIncludingNoteRow(m, song, kit, kit->getNoteRowId(&nr, i), &nr,
				                                                         nr.drum->toModControllable(), &nr.paramManager)
				          ->getPatchedAutoParamFromId(params::LOCAL_LPF_FREQ);
			}
		}
	}
	clipCase("kit-row", row, kit, false);

	ModelStackWithAutoParam* cc = nullptr;
	if (midi != nullptr) {
		ModelStackWithThreeMainThings* owner = synthStack(midi);
		ParamCollectionSummary* summary = midi->paramManager.getMIDIParamCollectionSummary();
		if (summary->paramCollection != nullptr) {
			cc = summary->paramCollection->getAutoParamFromId(
			    owner->addParamCollectionAndId(summary->paramCollection, summary, 74), true);
		}
	}
	clipCase("midi-cc", cc, midi, false);

	ModelStackWithAutoParam* mpe = nullptr;
	if (synth != nullptr && synth->paramManager.ensureExpressionParamSetExists()) {
		ModelStackWithThreeMainThings* owner = synthStack(synth);
		ParamCollectionSummary* summary = owner->paramManager->getExpressionParamSetSummary();
		mpe = summary->paramCollection->getAutoParamFromId(
		    owner->addParamCollectionAndId(summary->paramCollection, summary, 1), false);
	}
	clipCase("mpe", mpe, synth, true);

	if (synth != nullptr) {
		synth->sequenceDirectionMode = SequenceDirection::REVERSE;
		synth->currentlyPlayingReversed = true;
		clipCase("reversed", synthStack(synth)->getPatchedAutoParamFromId(params::LOCAL_OSC_A_VOLUME), synth, false);
		synth->sequenceDirectionMode = SequenceDirection::FORWARD;
		synth->currentlyPlayingReversed = false;
	}
	else {
		std::fprintf(io.out, "case reversed none\n");
	}

	if (synth != nullptr) {
		ModelStackWithAutoParam* lane = synthStack(synth)->getPatchedAutoParamFromId(params::LOCAL_LPF_RESONANCE);
		const int32_t len = synth->loopLength();
		std::fprintf(io.out, "case latch len=%ld\n", (long)len);
		deluge::project::setNodeAtPos(*lane->doc(), 0, 100000000, false);
		deluge::project::setNodeAtPos(*lane->doc(), len / 2, 700000000, true);
		deluge::project::setNodeAtPos(*lane->doc(), len * 3 / 4, -200000000, true);
		AudioEngine::audioSampleTimer += 100;
		lane->autoParam->setCurrentValueInResponseToUserInput(500000000, lane, true, len / 8, true, false);
		renderBlockStart();
		dumpParam("after-record", lane);
		const int32_t positions[] = {len / 4, len / 2, len * 5 / 8, len * 3 / 4, len - 1};
		const uint32_t undoBefore = renderUndoCallsNow();
		tickLatch(lane, positions, false, synth->lastProcessedPos);
		// The latch keeps its own undo snapshot on the render for now: the probe sees it.
		std::fprintf(io.err, "latch: render undo calls %u\n", static_cast<unsigned>(renderUndoCallsNow() - undoBefore));
		actionLogger.closeAction(ActionType::RECORD);
		actionLogger.revert(BEFORE, false, false);
		dumpParam("after-undo", lane);
		actionLogger.revert(AFTER, false, false);
		dumpParam("after-redo", lane);
	}
	else {
		std::fprintf(io.out, "case latch none\n");
	}
	if (synth != nullptr) {
		burstCase(song, synth, synthStack(synth)->getPatchedAutoParamFromId(params::LOCAL_PITCH_ADJUST));
	}
	playbackHandler.recording = RecordingMode::ARRANGEMENT;
}

int param_record_diff_main(int argc, char** argv, Io driverIo) {
	io = driverIo;
	if (argc < 2) {
		std::fprintf(io.err, "usage: %s <song.XML>\n", argv[0]);
		return 2;
	}

	FILE* f = fopen(argv[1], "rb");
	if (!f) {
		std::fprintf(io.err, "cannot open %s\n", argv[1]);
		return 2;
	}
	fseek(f, 0, SEEK_END);
	long sz = ftell(f);
	fseek(f, 0, SEEK_SET);
	char* xml = (char*)malloc(sz + 1);
	if (fread(xml, 1, sz, f) != (size_t)sz) {
		std::fprintf(io.err, "read error\n");
		return 2;
	}
	xml[sz] = 0;
	fclose(f);

	Error loadErr = Error::NONE;
	Song* song = load_song(xml, sz, deluge::storage::formatOfPath(argv[1]), &loadErr);
	if (!song) {
		std::fprintf(io.err, "load error %d\n", (int)loadErr);
		return 1;
	}

	// --- Drive the recording gate deterministically -------------------------------------------------------
	// RecordingMode::ARRANGEMENT makes Song::isPlayingAutomationNow() true AND satisfies `recording != OFF`;
	// armedForRecording defaults true on the TimelineCounter (the Song). Internal clock active + no count-in.
	playbackHandler.playbackState = PLAYBACK_CLOCK_INTERNAL_ACTIVE;
	playbackHandler.recording = RecordingMode::ARRANGEMENT;
	playbackHandler.ticksLeftInCountIn = 0;
	currentlyActioningSwungTickOrResettingPlayPos =
	    true; // suppress expectEvent()'s scheduleSwungTick (no RT scheduler)
	AudioEngine::audioSampleTimer = 0;

	// Build the song-global unpatched-param ModelStackWithAutoParam via the engine's own helper.
	alignas(ModelStackWithAutoParam) char msMem[MODEL_STACK_MAX_SIZE]{};
	ModelStackWithThreeMainThings* ms3 = song->setupModelStackWithSongAsTimelineCounter(msMem);
	ModelStackWithAutoParam* msap = ms3->getUnpatchedAutoParamFromId(params::UNPATCHED_VOLUME);
	if (!msap || !msap->autoParam) {
		std::fprintf(io.err, "could not build ModelStackWithAutoParam for UNPATCHED_VOLUME\n");
		return 3;
	}
	AutoParam& ap = *msap->autoParam;

	// The song-global param's node store is resolved through the id-addressable project tree
	// (Song::resolveParam by ParamAddress), not the engine fallback. resolveParam must return the SAME lane doc()
	// returns, and it must be non-null (i.e. the addressed path is actually live, not silently falling back).
	{
		deluge::project::ParamAddress addr = deluge::bridge::addressOf(
		    deluge::project::OwnerId::song(), msap->paramCollection->getParamKind(), params::UNPATCHED_VOLUME);
		deluge::project::DocAutoParam* viaAddress = song->project.resolveParam(addr);
		if (viaAddress == nullptr || viaAddress != msap->doc()) {
			std::fprintf(io.err, "S1 FAIL: song-global not id-addressed (resolveParam=%p doc=%p)\n", (void*)viaAddress,
			             (void*)msap->doc());
			return 3;
		}
	}

	std::fprintf(io.out, "# param_record_diff: %s\n", argv[1]);
	dumpParam("initial", msap);

	// Capture the pre-record state (empty nodes + initial value) for the undo test below. AutoParamState is the
	// snapshot an Action holds for undo; swapState(&state) swaps it with the live param.
	AutoParamState undoState;
	undoState.value = ap.getCurrentValue();

	const uint32_t undoBefore = renderUndoCallsNow();
	recordScript(msap, 0, false);
	probeFailed |= renderLoggedUndo(undoBefore, "song-global");
	dumpParam("after-record", msap);

	// The engine edit method also writes the owner's DOM store scalar, so save can read the store. After the recorded
	// edits, the store's current_value must equal the engine cursor. To stderr so the stdout baseline is unaffected.
	if (msap->doc()->current_value != ap.getCurrentValue()) {
		std::fprintf(io.err, "PHASE1 FAIL: store current_value=%ld != engine currentValue=%ld\n",
		             (long)msap->doc()->current_value, (long)ap.getCurrentValue());
		return 3;
	}

	// Undo (swapState with the pre-record snapshot) must revert to empty/initial; redo (swap again) must restore
	// the recorded automation exactly — exercising the AutoParam::swapState node/value swap on the DOM store.
	ap.swapState(&undoState, msap);
	dumpParam("after-undo", msap);
	ap.swapState(&undoState, msap);
	dumpParam("after-redo", msap);

	// Persistence: save the song and reload it; the recorded automation must survive the DOM serialization
	// round-trip. Re-fetch the param from the fresh Song and dump it.
	dumpAfterSaveReload(song);
	currentSong = song; // the reload made the copy current; the cases below record into, and undo against, this one

	// The song param, still latched from the script, drifts back across its nodes as the arrangement plays on.
	std::fprintf(io.out, "case arranger-latch\n");
	arrangement.playbackStartedAtPos = 0;
	std::vector<int32_t> positions;
	for (const ParamNode& node : msap->doc()->nodes) {
		if (node.pos != 0) {
			positions.push_back(node.pos);
		}
	}
	for (int32_t pos : positions) {
		playbackHandler.lastSwungTickActioned = pos;
		AudioEngine::audioSampleTimer += 2000;
		deluge::memory::noteRenderBlock(true);
		ap.processCurrentPos(msap, false, false, true, false);
		deluge::memory::noteRenderBlock(false);
		char label[32];
		snprintf(label, sizeof label, "tick %ld", (long)pos);
		dumpParam(label, msap);
	}

	clipCases(song);
	if (probeFailed) {
		return 3;
	}

	free(xml);
	return 0;
}

} // namespace

DELUGE_DRIVER_SCENARIO(param_record_diff, param_record_diff_main)
