# Owner-4 / Owner-5 write-side completion (sampleHolder + ranges) — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make `AudioClip::sampleHolder` (+ `overdubsShouldCloneOutput`) and `Source::ranges` DOM-authoritative at save, then delete `syncDocAudioClip` and `syncDocSound` — finishing M3 save-authority.

**Architecture:** Behavior-preserving refactor. The `DocAudioClip` / `DocSource.ranges` records already exist and are seeded at load (`AudioClip::deriveEngineFromDoc`, `Source` bind/reseed). This plan adds the *write* path (setters that write record + engine cache) and reworks the two AudioClip undo Consequences to revert **through the model setters** (approach ①: `Consequence` stays the undo currency — no Edit/Consequence convergence). Each field is dropped from the `syncDoc*` shadow as it flips, so a missed funnel fails the goldens.

**Tech Stack:** C++23; firmware `./dbt build Debug`; host-sim goldens under `tests/golden/` (Python runners driving `build-sim/<target>`).

## Global Constraints

- **Behavior-preserving.** Every slice stays bit-identical on the golden gate; no new user-visible behavior. (The one genuinely-new artifact is a *test fixture*, Task A0, not product behavior.)
- **Standing golden gate, per slice, x86≡ARM:** `song_roundtrip OK=16 REGRESS=0`, `json_roundtrip OK=38 FAIL=0`, `edit_undo_redo OK=40 FAIL=0`, `param_record OK=3 REGRESS=0`, `audio_clip_edit_diff OK=1 REGRESS=0`; firmware `./dbt build Debug` links.
- **Corpus:** `export DELUGE_SONG_CORPUS="/home/kate/Deluge Backup/SONGS"` (out-of-repo; never commit song XML).
- **Consequence stays the undo currency** (north-star staged convergence; Edit/Consequence convergence is M6, out of scope).
- **`ranges` gains no undo** (behavior-neutral, decided).
- **Commit discipline:** the pre-commit clang-format hook reformats + fails the first commit; recover with a FRESH `git commit` (never `--amend`). Never add Co-Authored-By trailers. Branch is `deluge-project-param` (not main) — commit freely at each green slice.
- **AudioClip setters FREEZE when unbound** (`FREEZE_WITH_ERROR("AC4S")`), matching the existing owner-4 setters — AudioClips are ctor-bound so this is always satisfied. `deriveEngineFromDoc` writes the cache DIRECTLY (record→cache) and must NOT be routed through the setters.

## Verification model (read once)

This is regression-gated refactoring, not feature TDD. The "test" for a behavior-preserving slice is: **the pre-existing golden suite stays green (bit-identical).** The single exception is Task A0, which adds genuine new coverage (an AudioClip marker-drag + undo case) *before* the code it must catch regressions in.

**The gate command block** (referenced as `RUN THE GATE` below):

```bash
export DELUGE_SONG_CORPUS="/home/kate/Deluge Backup/SONGS"
cmake --build build-sim --target song_roundtrip json_roundtrip edit_undo_redo param_record_diff audio_clip_edit_diff 2>&1 | grep -E "error:|FAILED" ; echo "BUILD rc=$?"
python3 tests/golden/songs_run.py | tail -2
python3 tests/golden/json_roundtrip_run.py --binary build-sim/json_roundtrip | tail -1
python3 tests/golden/edit_undo_redo_run.py --binary build-sim/edit_undo_redo | tail -1
python3 tests/golden/param_record_run.py --binary build-sim/param_record_diff | tail -1
python3 tests/golden/audio_clip_edit_run.py --binary build-sim/audio_clip_edit_diff | tail -1
./dbt build Debug 2>&1 | grep -E "error:|Linking CXX exec"
```
Expected: all OK counts as in Global Constraints; firmware links.

---

# Phase A — owner-4 `sampleHolder` + `overdubsShouldCloneOutput`

**Complete forward-mutation site map (verified — larger than the spec's estimate):**
- `gui/views/audio_clip_view.cpp:641` (`*valueToChange = newEndPosSamples`, end/start via `valueToChange`), `:674` (`sampleHolder.endPos =`), `:698` (`sampleHolder.startPos =`) — the 3 marker-drag sites.
- `model/clip/audio_clip.cpp:254` (`sampleHolder.filePath = recorder->sample->filePath` — record-finish), `:1386` (`sampleHolder.endPos = …` — arrangement-record length quantize), `:1423` (`sampleHolder.filePath.clear()` — sample clear), `:1514`/`:1515` (`sampleHolder.startPos/endPos =` — clip shift/nudge).
- `deriveEngineFromDoc` (`:1156-1160`) writes the cache directly — **leave as-is** (load-bridge, not a forward mutation).
- Undo: `ConsequenceClipLength` (raw `uint64_t*`) + `ConsequenceAudioClipSetSample` (value + member access).
- Save: `writeDataToFile` (`:1174` sync call; `:1178` filePath, `:1180` start, `:1181` end, `:1197` transpose, `:1200` cents, `:1203` overdubs).

**overdubs write sites:** `view.cpp:2756/2762` (menu toggle), `playback_handler.cpp:3385`, `session_view.cpp:3630`, `song.cpp:5487`, `audio_clip.cpp:137` (clone).

---

### Task A0: Add AudioClip marker-drag + undo coverage to `edit_undo_redo`

**Rationale:** Phase A rewrites the `ConsequenceClipLength` marker-revert path. `edit_undo_redo` must exercise it *before* the rewrite so a regression is caught. First check whether any existing case drives an AudioClip length/marker change + revert; if not, add one.

**Files:**
- Modify: `tests/golden/edit_undo_redo.cpp`
- Reference: `tests/golden/edit_undo_redo_run.py`

- [ ] **Step 1: Determine current coverage.**

Run:
```bash
grep -n "AudioClip\|sampleHolder\|ClipLength\|changeUnderlyingSample\|CLIP_LENGTH" tests/golden/edit_undo_redo.cpp
```
Expected: note whether any target loads an AudioClip and drives a length/marker edit + `ActionLogger::revert`. If a case already does an AudioClip `setClipLength` + marker + revert, mark this task done (record which case) and skip to A1.

- [ ] **Step 2: If uncovered, add a target that drives the marker path.**

Read the file's existing target pattern (how it loads a song, opens an Action, applies an edit, calls revert, self-checks). Add a target that: finds the first `AudioClip` with a loaded sample in the corpus song, records its `sampleHolder.endPos`, calls the same code path the UI uses to change underlying sample length (drive `AudioClipView::changeUnderlyingSampleLengthAndEndPos`-equivalent, or directly build an `Action`, apply `SetClipLength`, and back-patch `ConsequenceClipLength` exactly as `audio_clip_view.cpp:643-654` does), then `ActionLogger::revert` and assert `sampleHolder.endPos` returned to the recorded value.

Match the file's existing helpers (bare `printf`/`fprintf`, `loadSong`, recording OFF, no `deleteAllLogs` — see the harness gotchas already documented in the file/comments).

- [ ] **Step 3: Build + run just the new case on current (unmodified) code.**

Run:
```bash
cmake --build build-sim --target edit_undo_redo 2>&1 | grep -E "error:|Linking"
export DELUGE_SONG_CORPUS="/home/kate/Deluge Backup/SONGS"
python3 tests/golden/edit_undo_redo_run.py --binary build-sim/edit_undo_redo | tail -3
```
Expected: PASS on current code (the marker revert works today via the raw pointer). If it fails, the harness setup is wrong — fix before proceeding. The point is a baseline that must STAY green through A3.

- [ ] **Step 4: Confirm total count and commit.**

Run the full `edit_undo_redo_run.py`; expected `OK=41` (was 40) if a case was added, or `OK=40` if coverage already existed (skip commit in that case).
```bash
git add tests/golden/edit_undo_redo.cpp
git commit -m "test: cover AudioClip marker-drag + undo in edit_undo_redo (guards the ConsequenceClipLength rework)"
```

---

### Task A1: Add AudioClip sampleHolder model setters

**Files:**
- Modify: `src/deluge/model/clip/audio_clip.h` (declare), `src/deluge/model/clip/audio_clip.cpp` (define, near the existing owner-4 setters ~`:1107-1127`)

**Interfaces:**
- Produces: `void AudioClip::setSampleStartPos(uint64_t)`, `void AudioClip::setSampleEndPos(uint64_t)`, `void AudioClip::setSampleFilePath(const char*)`, `void AudioClip::setOverdubsShouldCloneOutput(bool)`. Each writes `docAudioClip_->*` + the engine cache. No `loadFile`/reload here — callers that need reload keep doing it (the record write is the added part).

- [ ] **Step 1: Declare in `audio_clip.h`** next to `setReversed`:

```cpp
void setSampleStartPos(uint64_t value);
void setSampleEndPos(uint64_t value);
void setSampleFilePath(const char* value);
void setOverdubsShouldCloneOutput(bool value);
```

- [ ] **Step 2: Define in `audio_clip.cpp`** after `setReversed` (~`:1127`):

```cpp
void AudioClip::setSampleStartPos(uint64_t value) {
	if (docAudioClip_ == nullptr) {
		FREEZE_WITH_ERROR("AC4H");
	}
	docAudioClip_->start_pos = value;
	sampleHolder.startPos = value;
}
void AudioClip::setSampleEndPos(uint64_t value) {
	if (docAudioClip_ == nullptr) {
		FREEZE_WITH_ERROR("AC4H");
	}
	docAudioClip_->end_pos = value;
	sampleHolder.endPos = value;
}
void AudioClip::setSampleFilePath(const char* value) {
	if (docAudioClip_ == nullptr) {
		FREEZE_WITH_ERROR("AC4H");
	}
	docAudioClip_->file_path = value;
	sampleHolder.filePath = value;
}
void AudioClip::setOverdubsShouldCloneOutput(bool value) {
	if (docAudioClip_ == nullptr) {
		FREEZE_WITH_ERROR("AC4H");
	}
	docAudioClip_->overdubs_should_clone = value;
	overdubsShouldCloneOutput = value;
}
```

- [ ] **Step 3: Build sim + firmware (no behavior change — nothing calls these yet).**

Run:
```bash
cmake --build build-sim --target audio_clip_edit_diff 2>&1 | grep -E "error:|Linking"
./dbt build Debug 2>&1 | grep -E "error:|Linking CXX exec"
```
Expected: compiles, links. (Unused-method warnings are fine.)

- [ ] **Step 4: RUN THE GATE.** Expected: all green (no call sites changed yet).

- [ ] **Step 5: Commit** (fresh commit after clang-format reformat):
```bash
git add -A && git commit -m "audio_clip: add sampleHolder/overdubs model setters (record + cache)"
```

---

### Task A2: Funnel the forward `sampleHolder` mutations through the setters

**Files:**
- Modify: `src/deluge/gui/views/audio_clip_view.cpp` (`:641`, `:674`, `:698`), `src/deluge/model/clip/audio_clip.cpp` (`:254`, `:1386`, `:1514-1515`)

**Interfaces:** Consumes A1's setters.

- [ ] **Step 1: `audio_clip_view.cpp:641`** — replace `*valueToChange = newEndPosSamples;` with a setter call. Because `valueToChange` is either `&clip.sampleHolder.startPos` or `&clip.sampleHolder.endPos`, dispatch on which:

```cpp
// M3 owner-4: funnel through the model setter (record + cache). oldValue captured above.
if (valueToChange == &clip.sampleHolder.startPos) {
	clip.setSampleStartPos(newEndPosSamples);
}
else {
	clip.setSampleEndPos(newEndPosSamples);
}
```
Keep the `uint64_t oldValue = *valueToChange;` read on the line before (it reads the pre-change value — still valid before the setter runs).

- [ ] **Step 2: `audio_clip_view.cpp:674`** — `clip.sampleHolder.endPos = newEndPos;` → `clip.setSampleEndPos(newEndPos);`

- [ ] **Step 3: `audio_clip_view.cpp:698`** — `clip.sampleHolder.startPos = newStartPos;` → `clip.setSampleStartPos(newStartPos);`

- [ ] **Step 4: `audio_clip.cpp:254`** — `sampleHolder.filePath = recorder->sample->filePath;` → `setSampleFilePath(recorder->sample->filePath.c_str());`
  (`filePath` is a fixed-cap string; `.c_str()` matches the setter param. Confirm `recorder->sample->filePath` has `.c_str()`.)

- [ ] **Step 5: `audio_clip.cpp:1386`** — `sampleHolder.endPos = sampleHolder.startPos + samplesPerTick * suggestedLength + 0.5;` → compute into a local then set:

```cpp
setSampleEndPos(sampleHolder.startPos + samplesPerTick * suggestedLength + 0.5); // Rounds it
```

- [ ] **Step 6: `audio_clip.cpp:1514-1515`** — 
```cpp
sampleHolder.startPos = newStartPos;
sampleHolder.endPos = newStartPos + length;
```
→
```cpp
setSampleStartPos(newStartPos);
setSampleEndPos(newStartPos + length);
```

- [ ] **Step 7: RUN THE GATE.** Expected: all green. The record now tracks these mutations, but save still reads the engine cache (via `syncDocAudioClip`), so bit-identical is guaranteed while proving the funnel compiles/runs.

- [ ] **Step 8: Commit:**
```bash
git add -A && git commit -m "audio_clip: funnel sampleHolder start/end/filePath forward mutations through the setters"
```

---

### Task A3: Value-ify `ConsequenceClipLength` (drop the raw `uint64_t*`)

**Files:**
- Modify: `src/deluge/model/consequence/consequence_clip_length.h`, `.cpp`, `src/deluge/gui/views/audio_clip_view.cpp` (`:648-652`, `:684-688`, `:708-712`)

**Interfaces:** Consumes A1's `setSampleStartPos`/`setSampleEndPos`.

**Design:** replace `uint64_t* pointerToMarkerValue` with a discriminator + owning `AudioClip*`. `revert()` swaps the stored value through the setter. The three injection sites set the discriminator + old value instead of a raw pointer.

- [ ] **Step 1: `consequence_clip_length.h`** — replace the marker fields:

```cpp
class Clip;
class AudioClip;

class ConsequenceClipLength final : public Consequence {
public:
	ConsequenceClipLength(Clip* newClip, int32_t oldLength);
	Error revert(TimeType time, ModelStack* modelStack) override;

	Clip* clip;
	int32_t lengthToRevertTo;

	// M3 owner-4: which AudioClip sample marker (if any) this length change also moved, reverted through the model
	// setter — replaces the former raw uint64_t* into sampleHolder. NONE for non-audio clips (length only).
	enum class Marker : uint8_t { NONE, START, END };
	Marker marker{Marker::NONE};
	AudioClip* audioClip{nullptr};
	uint64_t markerValueToRevertTo{};
};
```

- [ ] **Step 2: `consequence_clip_length.cpp`** — update `#include` + constructor + `revert`:

```cpp
#include "model/consequence/consequence_clip_length.h"
#include "model/clip/audio_clip.h"
#include "model/clip/clip.h"
#include "model/model_stack.h"
#include "model/song/song.h"

ConsequenceClipLength::ConsequenceClipLength(Clip* newClip, int32_t oldLength) {
	type = Consequence::CLIP_LENGTH;
	clip = newClip;
	lengthToRevertTo = oldLength;
}

Error ConsequenceClipLength::revert(TimeType time, ModelStack* modelStack) {

	if (marker != Marker::NONE && audioClip != nullptr) {
		uint64_t markerValueBeforeRevert =
		    (marker == Marker::START) ? audioClip->sampleHolder.startPos : audioClip->sampleHolder.endPos;
		if (marker == Marker::START) {
			audioClip->setSampleStartPos(markerValueToRevertTo);
		}
		else {
			audioClip->setSampleEndPos(markerValueToRevertTo);
		}
		markerValueToRevertTo = markerValueBeforeRevert;
	}

	int32_t lengthBeforeRevert = clip->loopLength();
	modelStack->song->setClipLength(clip, lengthToRevertTo, nullptr);
	lengthToRevertTo = lengthBeforeRevert;

	return Error::NONE;
}
```

- [ ] **Step 3: `audio_clip_view.cpp:648-652`** — replace the raw-pointer back-patch:

```cpp
if (action->firstConsequence && action->firstConsequence->type == Consequence::CLIP_LENGTH) {
	ConsequenceClipLength* consequence = (ConsequenceClipLength*)action->firstConsequence;
	consequence->audioClip = &clip;
	consequence->marker = (valueToChange == &clip.sampleHolder.startPos)
	                          ? ConsequenceClipLength::Marker::START
	                          : ConsequenceClipLength::Marker::END;
	consequence->markerValueToRevertTo = oldValue;
}
```

- [ ] **Step 4: `audio_clip_view.cpp:684-688`** (reversed-start branch, marker = END):

```cpp
if (action->firstConsequence && action->firstConsequence->type == Consequence::CLIP_LENGTH) {
	ConsequenceClipLength* consequence = (ConsequenceClipLength*)action->firstConsequence;
	consequence->audioClip = &clip;
	consequence->marker = ConsequenceClipLength::Marker::END;
	consequence->markerValueToRevertTo = oldValue;
}
```

- [ ] **Step 5: `audio_clip_view.cpp:708-712`** (forward-start branch, marker = START):

```cpp
if (action->firstConsequence && action->firstConsequence->type == Consequence::CLIP_LENGTH) {
	ConsequenceClipLength* consequence = (ConsequenceClipLength*)action->firstConsequence;
	consequence->audioClip = &clip;
	consequence->marker = ConsequenceClipLength::Marker::START;
	consequence->markerValueToRevertTo = oldValue;
}
```

- [ ] **Step 6: Build + RUN THE GATE**, with special attention to `edit_undo_redo` (Task A0's case exercises this revert). Expected: `edit_undo_redo OK=41` (or 40) and all other gates green. If A0's marker case fails, the discriminator wiring is wrong.

- [ ] **Step 7: Commit:**
```bash
git add -A && git commit -m "consequence: value-ify ConsequenceClipLength marker revert through the model setter (drop raw uint64_t*)"
```

---

### Task A4: Route `ConsequenceAudioClipSetSample::revert` + the clear site through the model

**Files:**
- Modify: `src/deluge/model/consequence/consequence_audio_clip_set_sample.cpp` (`revert`), `src/deluge/model/clip/audio_clip.cpp:1423` (clear)

**Design:** `revert` currently assigns `clip->sampleHolder.filePath`/`endPos` directly (`:41-42`). Route those two writes through `setSampleFilePath`/`setSampleEndPos` so the record follows. The `loadFile`/reload/deactivate logic is unchanged (byte-for-byte the same `loadFile(false, false, true)` call). The clear site `sampleHolder.filePath.clear()` (`:1423`) becomes `setSampleFilePath("")`.

- [ ] **Step 1: `consequence_audio_clip_set_sample.cpp:41-42`** — replace the two direct assignments:

```cpp
clip->setSampleFilePath(filePathToRevertTo.c_str());
clip->setSampleEndPos(endPosToRevertTo);
```
Leave `clip->sampleHolder.setAudioFile(nullptr);` (line 40) and everything else (`loadFile`, deactivate, resume, re-render, the swap-back at 68-69) unchanged.

- [ ] **Step 2: `audio_clip.cpp:1423`** — `sampleHolder.filePath.clear();` → `setSampleFilePath("");`

- [ ] **Step 3: Build + RUN THE GATE**, attention to `edit_undo_redo` + `audio_clip_edit_diff`. Expected: green. (The set-sample consequence isn't exercised by a load→save golden, so this leans on `edit_undo_redo` if it drives a sample set/clear; note if uncovered — see A6.)

- [ ] **Step 4: Commit:**
```bash
git add -A && git commit -m "consequence: route ConsequenceAudioClipSetSample revert + clip clear through the sampleHolder setters"
```

---

### Task A5: Flip the AudioClip save + funnel overdubs + delete `syncDocAudioClip`

**Files:**
- Modify: `src/deluge/model/clip/audio_clip.cpp` (`writeDataToFile` `:1174-1203`, delete `syncDocAudioClip` `:1129-1144`), `src/deluge/model/clip/audio_clip.h` (remove the `syncDocAudioClip` decl), `src/deluge/gui/views/view.cpp:2756/2762`, `playback_handler.cpp:3385`, `session_view.cpp:3630`, `song.cpp:5487`, `audio_clip.cpp:137` (overdubs writes)

**Interfaces:** Consumes A1 setters. Terminates owner-4.

- [ ] **Step 1: Funnel overdubs writes** through `setOverdubsShouldCloneOutput`:
  - `view.cpp:2756` `((AudioClip*)clip)->overdubsShouldCloneOutput = false;` → `((AudioClip*)clip)->setOverdubsShouldCloneOutput(false);`
  - `view.cpp:2762` → `setOverdubsShouldCloneOutput(true);`
  - `playback_handler.cpp:3385` `clipToCreateOverdubFrom->overdubsShouldCloneOutput = true;` → `setOverdubsShouldCloneOutput(true);`
  - `session_view.cpp:3630` `newClip->overdubsShouldCloneOutput = currentSong->defaultAudioClipOverdubOutputCloning;` → `newClip->setOverdubsShouldCloneOutput(currentSong->defaultAudioClipOverdubOutputCloning);`
  - `song.cpp:5487` `newClip->overdubsShouldCloneOutput = defaultAudioClipOverdubOutputCloning;` → `newClip->setOverdubsShouldCloneOutput(defaultAudioClipOverdubOutputCloning);`
  - `audio_clip.cpp:137` (clone) `overdubsShouldCloneOutput = ((AudioClip*)otherClip)->overdubsShouldCloneOutput;` → `setOverdubsShouldCloneOutput(((AudioClip*)otherClip)->overdubsShouldCloneOutput);`
  (All are on ctor-bound AudioClips. If any runs pre-bind, the FREEZE will surface it in the gate — none are expected to.)

- [ ] **Step 2: Flip `writeDataToFile` reads** (`audio_clip.cpp:1178-1203`) to read the record:

```cpp
writer.writeAttribute("filePath", docAudioClip_->file_path.c_str());
writer.writeAttribute("startSamplePos", docAudioClip_->start_pos);
writer.writeAttribute("endSamplePos", docAudioClip_->end_pos);
```
(These are `:1178-1181`.) And transpose/cents (`:1196-1201`):
```cpp
if (docAudioClip_->transpose) {
	writer.writeAttribute("transpose", docAudioClip_->transpose);
}
if (docAudioClip_->cents) {
	writer.writeAttribute("cents", docAudioClip_->cents);
}
```
And overdubs (`:1203`):
```cpp
writer.writeAttribute("overdubsShouldCloneAudioTrack", docAudioClip_->overdubs_should_clone);
```
**Note on filePath:** the old save wrote `sampleHolder.audioFile ? sampleHolder.audioFile->filePath : sampleHolder.filePath`. `syncDocAudioClip` already resolved that same `audioFile ? … : …` into `file_path` (`:1135`). So reading `docAudioClip_->file_path` is byte-identical **iff** the record's `file_path` is kept current with `audioFile->filePath` on load. `deriveEngineFromDoc` seeds `sampleHolder.filePath = d.file_path`; after `loadFile` the `audioFile` may carry a normalized path. **Verify** in Step 4 that `audio_clip_edit_diff` (real samples) stays bit-identical; if it diverges, the setter for filePath must also capture `audioFile->filePath` post-load (add a small `syncFilePathFromAudioFile()` called where `loadFile` completes — see Risks).

- [ ] **Step 3: Delete `syncDocAudioClip`** — remove the call at `:1174`, the definition `:1129-1144`, and the declaration in `audio_clip.h`.

- [ ] **Step 4: RUN THE GATE.** Expected: green, especially `audio_clip_edit_diff OK=1` (the `Sampling.XML` 20-clip fixture) and `song_roundtrip OK=16`. If `audio_clip_edit_diff` diverges on `filePath`, apply the Step-2 note's fix and re-gate.

- [ ] **Step 5: Verify no residual sampleHolder engine-read at save + commit:**
```bash
grep -n "syncDocAudioClip\|sampleHolder.startPos)\|sampleHolder.endPos)" src/deluge/model/clip/audio_clip.cpp
git add -A && git commit -m "audio_clip: save reads DocAudioClip for sampleHolder+overdubs; delete syncDocAudioClip (owner-4 complete)"
```
Expected grep: no `syncDocAudioClip`; the `writeDataToFile` no longer reads `sampleHolder.startPos/endPos`.

---

### Task A6: (conditional) close the set-sample undo coverage gap

- [ ] **Step 1:** If Task A4's Step 3 noted that no golden exercises the set-sample/clear revert, add an `edit_undo_redo` case that sets/clears an AudioClip sample and reverts, asserting `docAudioClip_->file_path` + `sampleHolder.filePath` round-trip. Same harness idiom as A0. If already covered, mark done and skip.

- [ ] **Step 2:** RUN THE GATE (`edit_undo_redo` count reflects any added case); commit if a case was added:
```bash
git add tests/golden/edit_undo_redo.cpp && git commit -m "test: cover AudioClip set-sample undo in edit_undo_redo"
```

---

# Phase B — owner-5 `ranges`

**Site map:**
- Zone edits: `gui/ui/sample_marker_editor.cpp:167` (`getCurrentSampleHolder().startPos = value`), `:175-176,:180,:190-195` (loop), `:199` (endPos) — all on `getCurrentMultisampleRange().sampleHolder` for non-audio clips.
- Collection: `gui/menu_item/multi_range.cpp:206` (`insertMultiRange`) + the delete path; range sample-file set via the file selector → `range->sampleHolder.loadFile`.
- Range builders: `slicer.cpp`, `sample_browser.cpp` (create ranges).
- Save: `Sound::writeSourceToFile` SAMPLE-range block (~`sound.cpp:3730-3765`) + WAVETABLE-range block (~`:3789-3820`) read `range->sampleHolder` + `range->topNote`.
- Shadow: the `syncDocSound` ranges rebuild (`sound.cpp:4034-4074`).

**Design:** `ranges` has no undo (keep it that way). The collection reorders by `top_note` on add/remove, so use a **whole-list reseed** (`Source::seedDocRanges()`, modKnobs/dx_patch-style) after any collection change, and a **per-range setter** for in-place zone edits. Both write `docSource_->ranges` (the `DocRange` vector) + the engine cache.

---

### Task B1: Add `Source::seedDocRanges()` (whole-list reseed)

**Files:**
- Modify: `src/deluge/processing/source.h` (declare), `src/deluge/processing/source.cpp` (define, near `seedDocDxPatch`)

**Interfaces:**
- Produces: `void Source::seedDocRanges()` (whole-list native→record; mirrors the `syncDocSound` ranges loop). This single reseed covers both zone edits and collection changes — the range mutation sites (marker editor, add/remove, sample-set, reverse) already write the engine `sampleHolder`/`ranges` directly; the reseed reflects the whole list into the record after each. No per-field setter is needed (a whole-list reseed of a handful of ranges is cheap and cannot drift on reorder-by-top-note).

- [ ] **Step 1: Declare in `source.h`** next to `seedDocDxPatch`:
```cpp
// ranges (owner-5, no undo): whole-list reseed of the DocRange list after any range mutation (zone edit, add/remove,
// sample-set, reverse). The engine sampleHolder/ranges stay the working structure; this mirrors them into the record.
void seedDocRanges();
```

- [ ] **Step 2: Define `seedDocRanges` in `source.cpp`** — lift the exact logic from `Sound::syncDocSound`'s ranges loop (`sound.cpp:4053-4073`), operating on `this` + `docSource_`:

```cpp
void Source::seedDocRanges() {
	if (docSource_ == nullptr) {
		return;
	}
	docSource_->ranges.clear();
	int32_t numRanges = std::ssize(ranges);
	docSource_->ranges.reserve(numRanges);
	for (int32_t e = 0; e < numRanges; e++) {
		MultisampleRange* range = (MultisampleRange*)ranges.getElement(e);
		deluge::project::DocRange dr;
		dr.top_note = range->topNote;
		dr.file_path =
		    range->sampleHolder.audioFile ? range->sampleHolder.audioFile->filePath : range->sampleHolder.filePath;
		if (oscType == OscType::SAMPLE) {
			dr.start_pos = range->sampleHolder.startPos;
			dr.end_pos = range->sampleHolder.endPos;
			dr.loop_start_pos = range->sampleHolder.loopStartPos;
			dr.loop_end_pos = range->sampleHolder.loopEndPos;
			dr.transpose = range->sampleHolder.transpose;
			dr.cents = range->sampleHolder.cents;
		}
		docSource_->ranges.push_back(std::move(dr));
	}
}
```
(Include `storage/multi_range/multisample_range.h` and `deluge-project/doc_source.h` — already included per Task G2 work. Verify.)

- [ ] **Step 3: Also seed ranges at bind + doneReadingFromFile** so the record starts correct (mirroring the scalar/dx_patch seeds). In `source.cpp`, `bindDocSource` already calls `seedDocSourceScalars()` + `seedDocDxPatch()`; add `seedDocRanges();`. In `Sound::doneReadingFromFile` (`sound.cpp`), the per-source reseed loop already calls `seedDocSourceScalars()` + `seedDocDxPatch()`; add `sources[s].seedDocRanges();`.

- [ ] **Step 4: Build sim + firmware. RUN THE GATE.** Expected: green (the record is now seeded, but save still reads the engine via `syncDocSound`, so bit-identical).

- [ ] **Step 5: Commit:**
```bash
git add -A && git commit -m "source: add seedDocRanges (whole-list reseed) + seed at bind/doneReadingFromFile"
```

---

### Task B2: Funnel the sample-marker-editor zone edits

**Files:**
- Modify: `src/deluge/gui/ui/sample_marker_editor.cpp` (`:167`, `:175-199`)

**Design:** the marker editor writes individual `getCurrentSampleHolder().<field> = value` on a range's sampleHolder (for non-audio clips) or the AudioClip's sampleHolder (owner-4 — leave those alone). It must reflect into the record. Since it writes one field at a time, the cleanest funnel is: after each field write, if the target is a Source range (not an AudioClip), reseed just that range's DocRange. Rather than call the multi-arg `setRangeZone` per field, add a tiny local reseed at the marker-editor level, OR reseed the whole source ranges after the edit.

- [ ] **Step 1: Read `sample_marker_editor.cpp:60-70` + `:160-200`** to see how it reaches the range (`getCurrentMultisampleRange()`, `getCurrentSampleHolder()`, and whether it has the owning `Source` + range index handy — it uses `soundEditor.currentSource` + `soundEditor.currentMultiRangeIndex`).

- [ ] **Step 2:** After the marker field write(s) settle (end of the edit handler that mutates the holder — around `:199` and the loop sites), for the non-audio-clip case add:
```cpp
// M3 owner-5: reflect the marker edit into the DOM range record. AudioClip markers are owner-4 (handled elsewhere).
if (getCurrentAudioClip() == nullptr && soundEditor.currentSource != nullptr) {
	soundEditor.currentSource->seedDocRanges();
}
```
Place this once per edit-commit path so it isn't called per-pixel-drag more than necessary (a whole-list reseed of a few ranges is cheap, but put it at the settle point, matching how the editor already batches). If the editor has a single "value changed" chokepoint, put it there.

- [ ] **Step 3: RUN THE GATE.** Expected: green (record now tracks zone edits; save still reads engine via syncDocSound, so bit-identical).

- [ ] **Step 4: Commit:**
```bash
git add -A && git commit -m "sample_marker_editor: reseed DocSource ranges after zone edits (owner-5)"
```

---

### Task B3: Funnel range collection changes (add/remove/sample-set/builders)

**Files:**
- Modify: `src/deluge/gui/menu_item/multi_range.cpp` (`:206` add + delete path), the range sample-file set path, `slicer.cpp` / `sample_browser.cpp` range builders

- [ ] **Step 1:** Find each collection mutation on a Source's `ranges` (`insertMultiRange`, the delete, `changeType`, sample-file load onto a range). Grep:
```bash
grep -rIn "\.ranges\.\|->ranges\.\|insertMultiRange\|ranges.getElement\|ranges.deleteAtIndex\|ranges.changeType" src/deluge/gui/menu_item/multi_range.cpp src/deluge/gui/ui/slicer.cpp src/deluge/gui/ui/browser/sample_browser.cpp | grep -v "getNumElements\|std::ssize"
```

- [ ] **Step 2:** After each collection mutation on `soundEditor.currentSource` (or the drum's source), call `seedDocRanges()` on that source. Example at `multi_range.cpp:206` after `insertMultiRange`:
```cpp
soundEditor.currentSource->ranges.insertMultiRange(newI); // existing
soundEditor.currentSource->seedDocRanges(); // M3 owner-5: reflect the collection change into the record
```
Repeat at the delete path and any range-sample-file set (after `loadFile` onto the range). For slicer/sample_browser range builders, reseed after the ranges are populated (on the bound drum/source).

- [ ] **Step 3:** `Source::setReversed` already fixes up range holder `endPos` (`source.cpp:112-125`). Add a reseed at its end so those endPos fixups reach the record:
```cpp
// (end of Source::setReversed)
seedDocRanges(); // M3 owner-5: the endPos fixups above changed range zones — reflect into the record
```

- [ ] **Step 4: RUN THE GATE.** Expected: green.

- [ ] **Step 5: Commit:**
```bash
git add -A && git commit -m "source: reseed DocSource ranges after collection changes (add/remove/sample-set/reverse/builders)"
```

---

### Task B4: Flip `writeSourceToFile` ranges to the record + delete `syncDocSound`

**Files:**
- Modify: `src/deluge/processing/sound/sound.cpp` (`writeSourceToFile` SAMPLE + WAVETABLE range blocks; delete the `syncDocSound` ranges loop → delete `syncDocSound`), `src/deluge/processing/sound/sound.h` (remove `syncDocSound` decl)

**Design:** the two range-emission blocks currently read `range->sampleHolder` + `range->topNote`. Flip them to read `docSound_->sources[s].ranges` (the `DocRange` list) when bound, matching byte-for-byte. Then delete `syncDocSound` (its only remaining content is the ranges rebuild) and its call in `writeToFile`.

- [ ] **Step 1: Read the two range-emission blocks** in full (`sound.cpp` ~`:3721-3775` SAMPLE, ~`:3789-3825` WAVETABLE) to get the exact attribute sequence (`fileName`, `rangeTopNote`, `transpose`, `cents`, `zone` startSamplePos/endSamplePos/startLoopPos/endLoopPos). Note the conditional attrs (only-if-nonzero) so the record read reproduces them identically.

- [ ] **Step 2:** In each block, when `docSound_ != nullptr`, iterate `docSound_->sources[s].ranges` (the `DocRange` vector, already keyed by index to the engine ranges) and emit from `dr.top_note` / `dr.file_path` / `dr.start_pos` / `dr.end_pos` / `dr.loop_start_pos` / `dr.loop_end_pos` / `dr.transpose` / `dr.cents` — preserving every conditional and the `numRanges > 1` array framing. Keep the engine-read path as the `else` (unbound) fallback. (Because the emission is intricate, do SAMPLE first, gate, then WAVETABLE, gate — two commits if safer.)

- [ ] **Step 3:** Delete the ranges loop from `syncDocSound` (`sound.cpp:4034-4074`). `syncDocSound` is now empty except the `docSound_==nullptr` guard → delete the whole method + its call at the top of `writeToFile` + the declaration in `sound.h`.

- [ ] **Step 4: RUN THE GATE.** Expected: green — `song_roundtrip OK=16` is the key net (corpus songs with multisample kits exercise ranges). If a multisample/wavetable range diverges, diff the emitted XML for one fixture and reconcile the conditional-attr logic.

- [ ] **Step 5: Verify + commit:**
```bash
grep -n "syncDocSound" src/deluge/processing/sound/sound.cpp src/deluge/processing/sound/sound.h
git add -A && git commit -m "sound: writeSourceToFile ranges read DocSource record; delete syncDocSound (owner-5 complete)"
```
Expected grep: zero matches — `syncDocSound` fully gone.

---

### Task C1: Milestone exit verification

- [ ] **Step 1: Prove no `syncDoc*` engine-walk remains in any save path:**
```bash
grep -rIn "syncDoc" src/deluge src/deluge-project | grep -v "seedDoc\|bindDoc\|deriveEngineFromDoc"
```
Expected: zero `syncDoc*` methods remain (all deleted). `seedDoc*`/`bindDoc*`/`deriveEngineFromDoc` (load-bridges) are expected to remain.

- [ ] **Step 2: Full x86 + targeted-ARM gate at the phase boundary.** Run the full gate on x86, then the ARM oracle on the range-relevant fixtures (per the golden-arm-runs-targeted convention — `songs_run.py --filter <multisample fixtures>` on ARM). Expected: bit-identical x86≡ARM.

- [ ] **Step 3: Update the migration docs.** In `docs/dev/project_model_migration_plan.md`, mark M3 save-authority complete (owner-4 + owner-5 done; `syncDoc*` retired). Note the real M4 (snapshot boundary) remains the next frontier. Commit:
```bash
git add docs/ && git commit -m "docs: M3 save-authority complete — sampleHolder + ranges DOM-authoritative, syncDoc* retired"
```

---

## Notes / Risks (carried from the spec)

- **filePath normalization (A5 Step 2).** If `audioFile->filePath` differs from the stored `file_path` after `loadFile`, the record must capture the resolved path. Fix: a small hook where load completes (`loadSample`/`loadFile` success) calling `docAudioClip_->file_path = sampleHolder.audioFile->filePath` (record ← resolved). Gate `audio_clip_edit_diff` proves it.
- **`loadFile` semantics on undo (A4).** The reverted reload must keep the exact `loadFile(false, false, true)` args and surrounding deactivate/resume logic — do not "simplify" into the setter.
- **Ranges reseed cost (B).** Whole-list reseed is O(ranges) per edit; ranges are a handful, negligible — but it is per-edit, not per-render, so no RT concern.
- **Coverage.** `song_roundtrip` covers ranges (multisample kits in the corpus). `edit_undo_redo` (A0/A6) covers the reworked AudioClip consequences. `audio_clip_edit_diff` covers sampleHolder round-trip. The marker-drag + undo path had no dedicated golden before A0 — that gap is why A0 is first.
- **DX7 was golden-blind** (prior slice) — unrelated here, but the same caveat applies if any range fixture is absent; `grep`-verify shadow deletion is the backstop that a missed funnel can't hide.

## References
- Spec: `docs/superpowers/specs/2026-07-12-owner4-5-write-side-completion-design.md`
- Owner-4 setter idiom + save: `src/deluge/model/clip/audio_clip.cpp:1107-1219`
- Consequences: `src/deluge/model/consequence/consequence_clip_length.*`, `consequence_audio_clip_set_sample.*`
- Marker drag: `src/deluge/gui/views/audio_clip_view.cpp:600-716`
- Ranges save + shadow: `src/deluge/processing/sound/sound.cpp` (writeSourceToFile, syncDocSound)
