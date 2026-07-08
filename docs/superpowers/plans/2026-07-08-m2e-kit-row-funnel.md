# M2e Kit-row Funnel Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Reify kit-row add/remove as `deluge::edits::AddKitRow` / `RemoveKitRow` Commands addressed by a new `KitRowAddress`, **with no Consequence and no undo** (faithful funnel; the `deleteAllLogs()` calls stay). Prove via apply-then-assert-existence targets.

**Architecture:** Two funnel-phase engine-borrow Edits. `AddKitRow` reifies the synchronous MIDI/GATE branch of `createDrumForAuditionedNoteRow`; `RemoveKitRow` reifies the whole `deleteDrum` body verbatim (no undo → dealloc/reassignment stay). No Consequence — undo is deferred to the future command/undo-tree substrate (see the 2026-07-08-m2e-kit-row-funnel design).

**Tech Stack:** C++23, CMake `GLOB_RECURSE`, sim `edit_undo_redo`, golden nets, `dbt build Debug`.

## Global Constraints

- Behaviour-preserving; **no new Consequence**; keep the existing `deleteAllLogs()` calls.
- Idiomatic C++23 matching sibling edits.
- After a clang-format pre-commit failure, re-run a **fresh** `git commit` (never `--amend`) — see [[clang-format-hook-amend-trap]].
- Corpus: `export DELUGE_SONG_CORPUS="/home/kate/Deluge Backup/SONGS"`.

---

### Task 1: `KitRowAddress` + `kitRowAddressFor`

**Files:**
- Create: `src/deluge-project/kit_row_address.h`
- Create: `src/deluge/model/edits/kit_row_address_for.h`, `.cpp`

- [ ] **Step 1: Write `kit_row_address.h`**

```cpp
#pragma once

#include <cstdint>

namespace deluge::project {

/// The durable structural id of a kit row: which kit (Output) + which drum. Parallel to ClipAddress/NoteRowAddress.
struct KitRowAddress {
	uint32_t kit_id{0}; ///< Kit(Output)::serialId() when saved, else sessionId().
	bool kit_is_session{false};
	uint32_t drum_id{0}; ///< Drum::serialId() when saved, else sessionId(). 0 = unassigned (a to-be-created row).
	bool drum_is_session{false};

	auto operator<=>(const KitRowAddress&) const = default;
};

} // namespace deluge::project
```

- [ ] **Step 2: Write `kit_row_address_for.h`**

```cpp
#pragma once

#include "deluge-project/kit_row_address.h"
#include <optional>

class Kit;
class Drum;

namespace deluge::edits {

/// The durable address of an existing kit row (kit + drum): serial-else-session for both, else nullopt. The kit-tier
/// analogue of clipAddressFor().
std::optional<deluge::project::KitRowAddress> kitRowAddressFor(const Kit& kit, const Drum& drum);

} // namespace deluge::edits
```

- [ ] **Step 3: Write `kit_row_address_for.cpp`**

```cpp
#include "model/edits/kit_row_address_for.h"

#include "model/drum/drum.h"
#include "model/instrument/kit.h"

namespace deluge::edits {

std::optional<deluge::project::KitRowAddress> kitRowAddressFor(const Kit& kit, const Drum& drum) {
	deluge::project::KitRowAddress addr{};
	if (std::optional<uint32_t> serial = kit.serialId()) {
		addr.kit_id = *serial;
		addr.kit_is_session = false;
	}
	else if (uint32_t session = kit.sessionId(); session != 0) {
		addr.kit_id = session;
		addr.kit_is_session = true;
	}
	else {
		return std::nullopt;
	}
	if (std::optional<uint32_t> serial = drum.serialId()) {
		addr.drum_id = *serial;
		addr.drum_is_session = false;
	}
	else if (uint32_t session = drum.sessionId(); session != 0) {
		addr.drum_id = session;
		addr.drum_is_session = true;
	}
	else {
		return std::nullopt;
	}
	return addr;
}

} // namespace deluge::edits
```

- [ ] **Step 4: Build** — `cmake --build build-sim --target edit_undo_redo 2>&1 | grep -iE "error:" | head`. Expected: no output. (`Kit::serialId`/`sessionId` come from the `Output` base — `output.h`; `kit.h` includes it.)

---

### Task 2: `AddKitRow` Edit

**Files:**
- Create: `src/deluge/model/edits/add_kit_row.h`, `.cpp`

**Interfaces:**
- Produces: `class deluge::edits::AddKitRow` ctor `(Kit*, NoteRow*, DrumType, ModelStackWithNoteRow*)`, `Error apply(Context&)`, `Drum* drum() const`.

- [ ] **Step 1: Write `add_kit_row.h`**

```cpp
#pragma once

#include "definitions_cxx.hpp" // DrumType
#include "model/edits/edit.h"

class Kit;
class NoteRow;
class Drum;
class ModelStackWithNoteRow;

namespace deluge::edits {

/// Add a new (MIDI/GATE) drum + attach it to a note-row — the reification of the synchronous branch of
/// InstrumentClipView::createDrumForAuditionedNoteRow. apply() creates the drum, adds it to the kit, and sets it on the
/// row. NON-UNDOABLE by design (the view keeps deleteAllLogs) — undo arrives with the command/undo-tree substrate, not
/// a Consequence. drum() exposes the created drum for the view's drawDrumName. The async SOUND-drum path stays in the
/// view.
class AddKitRow final : public Edit {
public:
	AddKitRow(Kit* kit, NoteRow* noteRow, DrumType drumType, ModelStackWithNoteRow* engineStack)
	    : kit_(kit), noteRow_(noteRow), drumType_(drumType), engineStack_(engineStack) {}

	Error apply(Context& ctx) override;

	[[nodiscard]] Drum* drum() const { return drum_; }

private:
	Kit* kit_;
	NoteRow* noteRow_;
	DrumType drumType_;
	ModelStackWithNoteRow* engineStack_; ///< Funnel-phase borrow.
	Drum* drum_ = nullptr;
};

} // namespace deluge::edits
```

- [ ] **Step 2: Write `add_kit_row.cpp`**

```cpp
#include "model/edits/add_kit_row.h"

#include "model/instrument/kit.h"
#include "model/note/note_row.h"
#include "modulation/params/param_manager.h"
#include "storage/storage_manager.h"

namespace deluge::edits {

Error AddKitRow::apply(Context& /*ctx*/) {
	Drum* drum = StorageManager::createNewDrum(drumType_);
	if (drum == nullptr) {
		return Error::INSUFFICIENT_RAM;
	}
	ParamManager paramManager;
	kit_->addDrum(drum);
	noteRow_->setDrum(drum, kit_, engineStack_, nullptr, &paramManager);
	kit_->beenEdited();
	drum_ = drum;
	return Error::NONE;
}

} // namespace deluge::edits
```

- [ ] **Step 3: Funnel the site.** In `instrument_clip_view.cpp` (the `else` branch at ~1023, currently `Drum* newDrum = StorageManager::createNewDrum(drumType); if (!newDrum) { goto ramError; } ParamManager paramManager; kit->addDrum(newDrum); ModelStackWithNoteRow* modelStackWithNoteRow = modelStack->addNoteRow(noteRowIndex, noteRow); noteRow->setDrum(newDrum, kit, modelStackWithNoteRow, nullptr, &paramManager); kit->beenEdited(); drawDrumName(newDrum);`), replace with:

```cpp
		ModelStackWithNoteRow* modelStackWithNoteRow = modelStack->addNoteRow(noteRowIndex, noteRow);
		deluge::edits::Context kitCtx{*currentSong, currentSong->project};
		deluge::edits::AddKitRow addEdit{kit, noteRow, drumType, modelStackWithNoteRow};
		if (deluge::edits::apply(kitCtx, addEdit) != Error::NONE) {
			goto ramError;
		}
		Drum* newDrum = addEdit.drum();
		drawDrumName(newDrum);
```
Keep whatever follows `drawDrumName` (read the surrounding lines 1041+ and preserve them). Add `#include "model/edits/add_kit_row.h"` + `#include "model/edits/edit.h"` (if not already present) to the includes.

- [ ] **Step 4: Build** — `cmake --build build-sim --target edit_undo_redo 2>&1 | grep -iE "error:" | head`. Expected: no output.

---

### Task 3: `RemoveKitRow` Edit

**Files:**
- Create: `src/deluge/model/edits/remove_kit_row.h`, `.cpp`

- [ ] **Step 1: Write `remove_kit_row.h`**

```cpp
#pragma once

#include "deluge-project/kit_row_address.h"
#include "model/edits/edit.h"
#include <optional>

class SoundDrum;
class ModelStackWithTimelineCounter;

namespace deluge::edits {

/// Delete a kit row (a SoundDrum + its NoteRow) — the reification of InstrumentClipView::deleteDrum. apply() reproduces
/// deleteDrum verbatim (kit->removeDrum, row reassignment / conditional deleteNoteRow, automation-store retirement,
/// destruct + dealloc). NON-UNDOABLE by design (the view keeps deleteAllLogs) — no Consequence.
class RemoveKitRow final : public Edit {
public:
	RemoveKitRow(std::optional<project::KitRowAddress> address, SoundDrum* drum, ModelStackWithTimelineCounter* engineStack)
	    : address_(address), drum_(drum), engineStack_(engineStack) {}

	Error apply(Context& ctx) override;

private:
	std::optional<project::KitRowAddress> address_;
	SoundDrum* drum_;
	ModelStackWithTimelineCounter* engineStack_; ///< Funnel-phase borrow.
};

} // namespace deluge::edits
```

- [ ] **Step 2: Write `remove_kit_row.cpp`** — reproduce `deleteDrum`'s body (instrument_clip_view.cpp:5569–5607) verbatim, sourcing the clip/kit from `engineStack_` and the song from `ctx.song`. Read that block and translate `getCurrentKit()` → `static_cast<Kit*>(clip->output)`, `getCurrentInstrumentClip()` → `clip`, `currentSong` → `&ctx.song`, where `auto* clip = static_cast<InstrumentClip*>(engineStack_->getTimelineCounter());`. Build the inner `ModelStackWithNoteRow` from `engineStack_` exactly as deleteDrum does. Includes: `kit.h`, `instrument_clip.h`, `note_row.h`, `sound_drum.h`, `model_stack.h`, `song/song.h`, `processing/engines/audio_engine.h`, `memory/general_memory_allocator.h`, and whatever `deleteDrum` references (`param_backup`, `backed_params`, `OwnerId`). Cross-check against the live `deleteDrum` at execution time — do not guess the exact statements.

- [ ] **Step 3: Funnel the site.** Replace the body of `InstrumentClipView::deleteDrum(SoundDrum* drum)` (5569–5607) with:

```cpp
	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
	deluge::edits::Context kitCtx{*currentSong, currentSong->project};
	deluge::edits::RemoveKitRow removeEdit{
	    deluge::edits::kitRowAddressFor(*getCurrentKit(), *drum), drum, modelStack};
	deluge::edits::apply(kitCtx, removeEdit);
```
(Keep the trailing comment about repopulate.) Add `#include "model/edits/remove_kit_row.h"` + `#include "model/edits/kit_row_address_for.h"`.

- [ ] **Step 4: Build** — `cmake --build build-sim --target edit_undo_redo 2>&1 | grep -iE "error:" | head`. Expected: no output.

---

### Task 4: Tests — apply-then-assert-existence

**Files:**
- Modify: `tests/golden/edit_undo_redo.cpp`

- [ ] **Step 1: Add includes** — `model/edits/add_kit_row.h`, `model/edits/remove_kit_row.h`, `model/edits/kit_row_address_for.h`, `model/instrument/kit.h`, `processing/sound/sound_drum.h`.

- [ ] **Step 2: Add a drum-count helper + `kitAddTarget`** (before `} // namespace`):

```cpp
int32_t drumCount(Kit* kit) {
	int32_t n = 0;
	for (Drum* d = kit->firstDrum; d != nullptr; d = d->next) {
		n++;
	}
	return n;
}

// M2e: add a GATE drum + row via AddKitRow (apply-only — non-undoable by design).
void kitAddTarget(Song* song) {
	InstrumentClip* clip = nullptr;
	for (int32_t c = 0; c < std::ssize(song->project.clips.session_clips) && clip == nullptr; c++) {
		Clip* cl = song->project.clips.session_clips[c];
		if (cl->type == ClipType::INSTRUMENT && cl->output != nullptr && cl->output->type == OutputType::KIT
		    && !((InstrumentClip*)cl)->noteRows.empty()) {
			clip = (InstrumentClip*)cl;
		}
	}
	if (clip == nullptr) {
		printf("# no-kit-clip-for-add\n");
		return;
	}
	printf("# target kit-add\n");
	song->setCurrentClip(clip);
	auto* kit = static_cast<Kit*>(clip->output);
	int32_t rowIndex = 0;
	NoteRow* noteRow = &clip->noteRows[rowIndex];

	char mem[MODEL_STACK_MAX_SIZE];
	ModelStackWithNoteRow* ms =
	    setupModelStackWithTimelineCounter(mem, song, clip)->addNoteRow(clip->getNoteRowId(noteRow, rowIndex), noteRow);

	int32_t before = drumCount(kit);
	deluge::edits::Context ctx{*currentSong, currentSong->project};
	deluge::edits::AddKitRow edit{kit, noteRow, DrumType::GATE, ms};
	check(deluge::edits::apply(ctx, edit) == Error::NONE, "kit-add: apply succeeds");
	check(edit.drum() != nullptr, "kit-add: a drum was created");
	check(drumCount(kit) == before + 1, "kit-add: kit gained a drum");
	check(kit->getDrumIndex(edit.drum()) >= 0, "kit-add: drum is in the kit");
	check(noteRow->drum == edit.drum(), "kit-add: row points at the new drum");

	song->setCurrentClip(nullptr);
}
```

- [ ] **Step 3: Add `kitRemoveTarget`** (find a kit clip with a SOUND drum whose row we can delete):

```cpp
// M2e: delete a SOUND-drum kit row via RemoveKitRow (apply-only — non-undoable by design).
void kitRemoveTarget(Song* song) {
	InstrumentClip* clip = nullptr;
	SoundDrum* drum = nullptr;
	for (int32_t c = 0; c < std::ssize(song->project.clips.session_clips) && clip == nullptr; c++) {
		Clip* cl = song->project.clips.session_clips[c];
		if (cl->type != ClipType::INSTRUMENT || cl->output == nullptr || cl->output->type != OutputType::KIT) {
			continue;
		}
		auto* kit = static_cast<Kit*>(cl->output);
		if (drumCount(kit) < 2) { // keep at least one drum around
			continue;
		}
		for (Drum* d = kit->firstDrum; d != nullptr; d = d->next) {
			if (d->type == DrumType::SOUND) {
				clip = (InstrumentClip*)cl;
				drum = (SoundDrum*)d;
				break;
			}
		}
	}
	if (clip == nullptr) {
		printf("# no-kit-sound-drum-for-remove\n");
		return;
	}
	printf("# target kit-remove\n");
	song->setCurrentClip(clip);
	auto* kit = static_cast<Kit*>(clip->output);
	int32_t before = drumCount(kit);

	char mem[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* ms = setupModelStackWithTimelineCounter(mem, song, clip);
	deluge::edits::Context ctx{*currentSong, currentSong->project};
	deluge::edits::RemoveKitRow edit{deluge::edits::kitRowAddressFor(*kit, *drum), drum, ms};
	check(deluge::edits::apply(ctx, edit) == Error::NONE, "kit-remove: apply succeeds");
	check(drumCount(kit) == before - 1, "kit-remove: kit lost a drum"); // drum is now freed; do NOT touch it

	song->setCurrentClip(nullptr);
}
```

- [ ] **Step 4: Call both in `main`** after `pasteTarget(song);`:

```cpp
	kitAddTarget(song);
	kitRemoveTarget(song);
```

- [ ] **Step 5: Build + run** — `cmake --build build-sim --target edit_undo_redo 2>&1 | grep -iE "error:"` then `python3 tests/golden/edit_undo_redo_run.py --binary build-sim/edit_undo_redo 2>&1 | grep -E "^(FAIL|OK=)"`. Expected: `OK=40 FAIL=0`. If a kit target crashes/aborts, debug with gdb (as done for the string_view/testSequentiality issues) and adjust.

- [ ] **Step 6: Confirm exercise** — grep a few songs' stdout for `# target kit-add` / `# target kit-remove` (vs skip markers). Expected: exercised on ≥1 song each. If none of the corpus songs have kit clips with SOUND drums, note it and rely on the compile + the add target.

---

### Task 5: Gates + commit

- [ ] **Step 1:** `cmake --build build-sim --target edit_test && ./build-sim/edit_test` → `edit_test: OK`.
- [ ] **Step 2:** `./dbt build Debug 2>&1 | grep -E "error:|^\s+text"` → links; note elf text size.
- [ ] **Step 3:** golden: `python3 tests/golden/songs_run.py 2>&1 | tail -1` (`OK=15 REGRESS=0`), `python3 tests/golden/param_record_run.py --binary build-sim/param_record_diff 2>&1 | tail -1` (`OK=3 REGRESS=0`).
- [ ] **Step 4: Update catalog** — set the kit/drum row in `docs/dev/edit_catalog.md` to **landed (funnel; non-undoable by design — undo deferred to the command/undo-tree substrate, no Consequence added)**; list `AddKitRow`/`RemoveKitRow`/`KitRowAddress`; note async SOUND add + delete-row-pad deferred.
- [ ] **Step 5: Commit** (fresh commit; re-run `git commit` after any clang-format reformat, never `--amend`):

```bash
git add src/deluge-project/kit_row_address.h src/deluge/model/edits/kit_row_address_for.h \
        src/deluge/model/edits/kit_row_address_for.cpp src/deluge/model/edits/add_kit_row.h \
        src/deluge/model/edits/add_kit_row.cpp src/deluge/model/edits/remove_kit_row.h \
        src/deluge/model/edits/remove_kit_row.cpp src/deluge/gui/views/instrument_clip_view.cpp \
        tests/golden/edit_undo_redo.cpp docs/dev/edit_catalog.md
git commit -m "m2e: AddKitRow/RemoveKitRow — funnel kit-row structure (no Consequence, non-undoable by design)"
```

- [ ] **Step 6: Update memory** — record M2e slice 1 in `m1-edit-substrate.md`: faithful funnel, no Consequence (undo deferred), `KitRowAddress`, async SOUND + delete-pad + undo deferred.
