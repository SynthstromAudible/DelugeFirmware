# M1d — Edit Undo/Redo Scripting Hook — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A self-checking sim harness (`edit_undo_redo <song.XML>`) that drives `deluge::edits::SetParamValue`/`apply` over a real loaded song and asserts (a) the edit resolves through a live DOM address and (b) `ActionLogger::revert` undo/redo restores state — the loaded-song, addressable-owner coverage `edit_test` can't reach.

**Architecture:** Mirror `tests/golden/param_record_diff.cpp`'s native scaffolding (`load_song`, `setupModelStackWithSongAsTimelineCounter`). For each target param build a real (song-resolved) `ModelStackWithAutoParam`, assert `addressFor` returns a live address, then script `apply(SetParamValue{...})` → snapshot → `revert(BEFORE)` (undo, assert == pre) → `revert(AFTER)` (redo, assert == post). Two targets: the song-global `UNPATCHED_VOLUME` (always), and the first synth/kit `InstrumentClip` param (when present). Self-checking → no baseline; a corpus-driven ctest that SKIPs (77) without `$DELUGE_SONG_CORPUS`.

**Tech Stack:** C++23 sim harness; `build-sim`/`build-sim-arm` (qemu) via ctest; Python runner mirroring `param_record_run.py`.

Spec: `docs/superpowers/specs/2026-07-07-m1d-edit-undo-redo-hook-design.md`.

## Global Constraints

- **No firmware behaviour change** — M1d is pure test code (new files under `tests/golden/` + `sim/CMakeLists.txt`). The `./dbt build Debug` gate only confirms no accidental app edits.
- **Crash-safety (headless ActionLogger):** never call `deleteAllLogs` with consequences present, and never let one target's `getNewAction` free another's Action — freeing a consequence pumps the uninitialised audio engine. `revert` only *moves* Actions between queues (safe). Between the two targets, close the prior Action (`openForAdditions = false`) so the second target opens its own Action instead of coalescing.
- **Assertion scope:** compare `autoParam->getCurrentValue()` + `doc()->nodes`, **not** `doc()->current_value`. `revert`→`swapState` restores the engine value + nodes, not the store scalar (M3+).
- **`recording = RecordingMode::OFF`** so the Edit's pre-log opens a real Action (discrete edit, not the recording branch).
- Reuse `param_record_diff`'s `load_song` verbatim (native deserialize → `currentSong`); create `display` up front as `edit_test`/`clip_clone_roundtrip` do.
- Commit at the end of each task. Never add Co-Authored-By trailers.

---

### Task 1: `edit_undo_redo` harness — song-global target + runner + ctest

**Files:**
- Create: `tests/golden/edit_undo_redo.cpp`
- Create: `tests/golden/edit_undo_redo_run.py`
- Modify: `sim/CMakeLists.txt` (add_executable after the `param_record_diff` block ~line 514; `add_test` with `SKIP_RETURN_CODE 77` near the `param_record_diff` test ~line 638)

**Interfaces:**
- Produces: `edit_undo_redo <song.XML>` — exit `0` all-pass, `1` load error, `2` usage/IO, `3` a check failed. `edit_undo_redo_run.py --binary <exe> [--wrapper ...]` — corpus driver, exit 0 (OK/SKIP) or 1 (FAIL).

- [ ] **Step 1: Write the harness (song-global target only)**

Create `tests/golden/edit_undo_redo.cpp`:

```cpp
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
#include <vector>

namespace params = deluge::modulation::params;

namespace {

int failures = 0;
void check(bool ok, const char* what) {
	if (!ok) {
		std::fprintf(stderr, "CHECK FAIL: %s\n", what);
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
	std::printf("# target %s\n", label);

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
		std::fprintf(stderr, "usage: %s <song.XML>\n", argv[0]);
		return 2;
	}
	display = new deluge::hid::display::SevenSegment;

	FILE* f = std::fopen(argv[1], "rb");
	if (!f) {
		std::fprintf(stderr, "cannot open %s\n", argv[1]);
		return 2;
	}
	std::fseek(f, 0, SEEK_END);
	long sz = std::ftell(f);
	std::fseek(f, 0, SEEK_SET);
	char* xml = (char*)std::malloc(sz + 1);
	if (std::fread(xml, 1, sz, f) != (size_t)sz) {
		std::fprintf(stderr, "read error\n");
		return 2;
	}
	xml[sz] = 0;
	std::fclose(f);

	Error loadErr = Error::NONE;
	Song* song = load_song(xml, sz, &loadErr);
	if (!song) {
		std::fprintf(stderr, "load error %d\n", (int)loadErr);
		return 1;
	}
	playbackHandler.recording = RecordingMode::OFF;

	std::printf("# edit_undo_redo: %s\n", argv[1]);

	// Target 1 — song-global UNPATCHED_VOLUME (OwnerId::song()). Always present.
	{
		char msMem[MODEL_STACK_MAX_SIZE];
		ModelStackWithThreeMainThings* ms3 = song->setupModelStackWithSongAsTimelineCounter(msMem);
		ModelStackWithAutoParam* msap = ms3->getUnpatchedAutoParamFromId(params::UNPATCHED_VOLUME);
		if (!msap || !msap->autoParam) {
			std::fprintf(stderr, "no song-global UNPATCHED_VOLUME param\n");
			return 3;
		}
		scriptEditUndoRedo("song-global", song, msap);
	}

	std::free(xml);
	return failures ? 3 : 0;
}
```

- [ ] **Step 2: Write the corpus runner**

Create `tests/golden/edit_undo_redo_run.py`:

```python
#!/usr/bin/env python3
"""M1d Edit undo/redo hook runner. Runs edit_undo_redo over the BYO song corpus; the binary is
self-checking (exit 0 = pass, 3 = a check failed), so there is no baseline to diff.

    export DELUGE_SONG_CORPUS=~/path/to/SONGS
    python3 tests/golden/edit_undo_redo_run.py --binary build-sim/edit_undo_redo
"""
import argparse, os, shlex, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_CORPUS = os.path.join(HERE, "songs")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--binary", required=True)
    ap.add_argument("--wrapper", default="")
    ap.add_argument("--corpus", default=os.environ.get("DELUGE_SONG_CORPUS", DEFAULT_CORPUS))
    ap.add_argument("--filter", default="")
    ap.add_argument("--timeout", type=int, default=60)
    args = ap.parse_args()

    if not os.path.isdir(args.corpus):
        print("SKIP: DELUGE_SONG_CORPUS not set to a directory")
        return 77

    songs = []
    for root, _dirs, files in os.walk(args.corpus):
        for n in files:
            if n.lower().endswith(".xml") and args.filter in n:
                songs.append(os.path.join(root, n))
    songs.sort()
    if not songs:
        print("SKIP: no songs found in corpus")
        return 77

    pre = shlex.split(args.wrapper)
    ok = fail = 0
    for song in songs:
        rel = os.path.relpath(song, args.corpus)
        try:
            r = subprocess.run(pre + [args.binary, song], capture_output=True, timeout=args.timeout)
        except subprocess.TimeoutExpired:
            print(f"FAIL(timeout) {rel}")
            fail += 1
            continue
        if r.returncode == 0:
            print(f"OK      {rel}")
            ok += 1
        elif r.returncode == 1:
            # load error (e.g. a non-song XML in the corpus) — not this harness's concern; skip it.
            print(f"skip(load) {rel}")
        else:
            sys.stdout.write(r.stderr.decode("utf-8", "replace"))
            print(f"FAIL(rc={r.returncode}) {rel}")
            fail += 1
    print("=" * 60)
    print(f"OK={ok} FAIL={fail}  (corpus={args.corpus})")
    return 1 if fail else 0


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 3: Register the executable + ctest in `sim/CMakeLists.txt`**

After the `param_record_diff` `add_executable(...)`/`target_link_libraries(...)` block (ends ~line 515), add (mirroring it exactly):
```cmake
# M1d Edit undo/redo hook (docs/dev/project_model_migration_plan.md): drives SetParamValue/apply over a loaded
# song and self-checks addressFor resolution + real ActionLogger undo/redo. No baseline (self-checking).
add_executable(edit_undo_redo
    ${REPO_ROOT}/src/bsp/host/host_bsp.c
    ${REPO_ROOT}/src/bsp/host/host_platform.c
    ${REPO_ROOT}/src/bsp/host/host_audio.c
    ${REPO_ROOT}/src/bsp/host/host_link.c
    ${REPO_ROOT}/src/bsp/host/host_pcm.c
    ${REPO_ROOT}/src/bsp/host/host_midi.c
    ${REPO_ROOT}/src/bsp/host/host_debug.cpp
    ${REPO_ROOT}/tests/golden/edit_undo_redo.cpp
)
target_include_directories(edit_undo_redo PRIVATE ${HOST_INCLUDE_DIRS} ${REPO_ROOT}/src/deluge)
target_link_libraries(edit_undo_redo PRIVATE deluge_app deluge_scheduler ${SIM_ALLOC_LINK})
if(RT_LIB)
    target_link_libraries(edit_undo_redo PRIVATE ${RT_LIB})
endif()
```

Next to the `param_record_diff` `add_test(...)` block (~line 638), add:
```cmake
add_test(
    NAME edit_undo_redo
    COMMAND ${Python_EXECUTABLE} ${REPO_ROOT}/tests/golden/edit_undo_redo_run.py
            --binary $<TARGET_FILE:edit_undo_redo>
)
set_tests_properties(edit_undo_redo PROPERTIES SKIP_RETURN_CODE 77)
```

- [ ] **Step 4: Build and run over the corpus**

Run:
```bash
cd /home/kate/GitHub/DelugeFirmware
export DELUGE_SONG_CORPUS="/home/kate/Deluge Backup/SONGS"
cmake --build build-sim -j --target edit_undo_redo 2>&1 | grep -iE "error|FAILED" || echo "build clean"
python3 tests/golden/edit_undo_redo_run.py --binary build-sim/edit_undo_redo
```
Expected: `build clean`; `OK=N FAIL=0` (N = songs in the corpus; `skip(load)` lines for any non-song XML are fine).

- [ ] **Step 5: Confirm the regression net + firmware**

Run:
```bash
export DELUGE_SONG_CORPUS="/home/kate/Deluge Backup/SONGS"
ctest --test-dir build-sim -R 'edit_test|edit_undo_redo' --output-on-failure 2>&1 | tail -8
python3 tests/golden/songs_run.py 2>&1 | tail -1
python3 tests/golden/param_record_run.py --binary build-sim/param_record_diff 2>&1 | tail -1
./dbt build Debug 2>&1 | tail -2
```
Expected: both ctests pass; songs OK=15; param_record OK=3; firmware clean.

- [ ] **Step 6: Commit**

```bash
git add tests/golden/edit_undo_redo.cpp tests/golden/edit_undo_redo_run.py sim/CMakeLists.txt
git commit -m "test: M1d — Edit undo/redo hook over a loaded song (song-global target)"
```

---

### Task 2: Add the instrument-clip target (`OwnerId::instrument()`)

Extend the harness with a second target: the first synth/kit `InstrumentClip`'s unpatched param, exercising `addressFor`'s instrument classification (`OwnerId::instrument(output serialId)`) — a branch the song-global target and `edit_test` never hit. Defensive: skip gracefully if no such clip, or if the chosen param doesn't resolve to a live instrument address.

**Files:**
- Modify: `tests/golden/edit_undo_redo.cpp` (add the target + a finder helper)

**Interfaces:** none new.

- [ ] **Step 1: Add the instrument-clip finder + target to `main`**

In `tests/golden/edit_undo_redo.cpp`, after the song-global target block and before `std::free(xml)`, add:

```cpp
	// Between targets: close the song-global Action so target 2 opens its own Action rather than coalescing
	// into it (getNewAction coalesces same-type, still-open Actions). No free -> no audio-engine pump.
	if (actionLogger.firstAction[BEFORE] != nullptr) {
		actionLogger.firstAction[BEFORE]->openForAdditions = false;
	}

	// Target 2 — first synth/kit InstrumentClip's unpatched param (OwnerId::instrument()). Skipped if absent
	// or if the param doesn't resolve to a live instrument address (defensive; the song-global target is the
	// guaranteed coverage).
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
			std::printf("# no-instrument-clip\n");
		}
		else {
			char msMem[MODEL_STACK_MAX_SIZE];
			ModelStackWithThreeMainThings* ms3 = setupModelStackWithThreeMainThingsButNoNoteRow(
			    msMem, song, instClip->output->toModControllable(), instClip, &instClip->paramManager);
			ModelStackWithAutoParam* msap = ms3->getUnpatchedAutoParamFromId(params::UNPATCHED_VOLUME);
			// Defensive: need a live param AND a live, tree-RESOLVED instrument address (else this song offers
			// no instrument-owner coverage — not a failure, just nothing to assert here). Unlike the song-global
			// target (a guaranteed invariant), the instrument target is best-effort: pre-gate on resolvability so
			// scriptEditUndoRedo's resolveParam==doc() assertion can't hard-fail on a non-tree-resident param.
			std::optional<deluge::project::ParamAddress> instAddr = msap ? deluge::edits::addressFor(*msap)
			                                                             : std::nullopt;
			if (!msap || !msap->autoParam || !instAddr.has_value()
			    || song->project.resolveParam(*instAddr) == nullptr) {
				std::printf("# instrument-param-not-addressed\n");
			}
			else {
				scriptEditUndoRedo("instrument-clip", song, msap);
			}
		}
	}
```

(`setupModelStackWithThreeMainThingsButNoNoteRow` does `addNoteRow(0, nullptr)`, so `addressFor` sees a null noteRow → the synth/kit InstrumentClip branch → `OwnerId::instrument(output serialId)`, provided the output has a serialId, which a loaded song's outputs carry.)

- [ ] **Step 2: Build + run over the corpus**

Run:
```bash
cd /home/kate/GitHub/DelugeFirmware
export DELUGE_SONG_CORPUS="/home/kate/Deluge Backup/SONGS"
cmake --build build-sim -j --target edit_undo_redo 2>&1 | grep -iE "error|FAILED" || echo "build clean"
python3 tests/golden/edit_undo_redo_run.py --binary build-sim/edit_undo_redo
```
Expected: `OK=N FAIL=0`. Spot-check one song's stdout shows `# target instrument-clip` (not `# no-instrument-clip` / `# instrument-param-not-addressed`) for at least one corpus song — confirming the instrument path is actually exercised:
```bash
build-sim/edit_undo_redo "$DELUGE_SONG_CORPUS/SONG1.XML"
```

- [ ] **Step 3: Regression net + firmware + targeted ARM**

Run:
```bash
export DELUGE_SONG_CORPUS="/home/kate/Deluge Backup/SONGS"
ctest --test-dir build-sim -R 'edit_test|edit_undo_redo' --output-on-failure 2>&1 | tail -8
python3 tests/golden/songs_run.py 2>&1 | tail -1
./dbt build Debug 2>&1 | tail -2
cmake --build build-sim-arm -j --target edit_undo_redo 2>&1 | grep -iE "error|FAILED" || echo "arm build clean"
python3 tests/golden/edit_undo_redo_run.py --binary build-sim-arm/edit_undo_redo \
    --wrapper 'qemu-arm -L /usr/arm-linux-gnueabihf -cpu cortex-a9' --filter SONG1
```
Expected: ctests pass; songs OK=15; firmware clean; ARM (targeted SONG1) OK=1.

- [ ] **Step 4: Commit**

```bash
git add tests/golden/edit_undo_redo.cpp
git commit -m "test: M1d — add instrument-clip target (OwnerId::instrument address coverage)"
```

---

## Milestone Close-Out (M1 complete)

- [ ] M1d closes M1. Update the migration-status memory: M1 (a/b/c/d) done; the interior-log deletion + delete/paste Edits remain (M4 / later). The on-device smoke for M1c still rides the next flash.

## Out of Scope

- Drum / audio-clip owner targets (`OwnerId::drum()` / `OwnerId::clip()`) — follow-up.
- Baseline-diff snapshots — intentionally not used (self-checking).
