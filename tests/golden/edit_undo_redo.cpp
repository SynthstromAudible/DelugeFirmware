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
#include "model/edits/edit.h"
#include "model/edits/param_address_for.h"
#include "model/edits/set_param_value.h"
#include "model/global_effectable/global_effectable.h"
#include "model/model_stack.h"
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

	std::free(xml);
	return failures ? 3 : 0;
}
