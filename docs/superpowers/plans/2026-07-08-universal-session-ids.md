# Universal Session IDs (Plan A) — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** A single unconditional `Song::assignSessionIds()` pass at load mints a session id for every no-serialId
clip / output / drum, so every entity is id-addressable; remove the now-redundant conditional mints. Golden-neutral.

**Architecture:** Engine `Song` method (iterates engine entities, calls `project.mintSessionId()`), called early in
`readFromFile` before the param-relocation loop. Per the 2026-07-08-universal-session-ids design.

**Tech Stack:** C++23, sim `edit_undo_redo`, golden `songs`/`param_record`, `dbt build Debug`.

## Global Constraints

- **Golden-neutral**: session ids are never serialized; golden roundtrip must stay OK=15 REGRESS=0.
- Behaviour-preserving: the pass only guarantees a session id exists; it must NOT change the relocation's
  serialId-vs-session keying decisions.
- After a clang-format pre-commit failure, re-run a **fresh** `git commit` (never `--amend`) — see [[clang-format-hook-amend-trap]].
- Corpus: `export DELUGE_SONG_CORPUS="/home/kate/Deluge Backup/SONGS"`.

---

### Task 1: `Song::assignSessionIds()`

**Files:**
- Modify: `src/deluge/model/song/song.h` (declare), `src/deluge/model/song/song.cpp` (define)

**Interfaces:**
- Produces: `void Song::assignSessionIds();` — mints a session id (via `project.mintSessionId()`) for every
  session/arrangement clip, output, and drum that has no `serialId()` and `sessionId() == 0`.

- [ ] **Step 1: Declare** in `song.h` (near other readFromFile helpers / public section):

```cpp
	/// M3: mint a transient session id for every clip / output / drum that lacks a durable serialId, so every entity
	/// is id-addressable. Called at load before the param relocation; idempotent (skips entities that already have an
	/// id). Session ids are never serialized → golden-neutral.
	void assignSessionIds();
```

- [ ] **Step 2: Define** in `song.cpp` (place near `readFromFile`; needs `kit.h`/`drum.h` — already included there):

```cpp
void Song::assignSessionIds() {
	auto assignClip = [this](Clip* c) {
		if (!c->serialId() && c->sessionId() == 0) {
			c->setSessionId(project.mintSessionId());
		}
	};
	for (int32_t c = 0; c < std::ssize(project.clips.session_clips); c++) {
		assignClip(project.clips.session_clips[c]);
	}
	for (int32_t c = 0; c < std::ssize(project.clips.arrangement_only_clips); c++) {
		assignClip(project.clips.arrangement_only_clips[c]);
	}
	for (Output* o = project.media.first_output; o != nullptr; o = o->next) {
		if (!o->serialId() && o->sessionId() == 0) {
			o->setSessionId(project.mintSessionId());
		}
		if (o->type == OutputType::KIT) {
			for (Drum* d = static_cast<Kit*>(o)->firstDrum; d != nullptr; d = d->next) {
				if (!d->serialId() && d->sessionId() == 0) {
					d->setSessionId(project.mintSessionId());
				}
			}
		}
	}
}
```

- [ ] **Step 3: Build** — `cmake --build build-sim --target edit_undo_redo 2>&1 | grep -iE "error:" | head`. Expected: no output. (If `Kit`/`Drum`/`Output` types aren't visible, add `#include "model/instrument/kit.h"` / `model/drum/drum.h` — verify they're already present, they are used elsewhere in song.cpp.)

---

### Task 2: Call it before the relocation

**Files:**
- Modify: `src/deluge/model/song/song.cpp` (readFromFile, ~line 1794, just before `for (ClipArray* clipArray : arrays)`)

- [ ] **Step 1:** Insert the call immediately before the "Phase C (DOM single-store)" relocation comment/loop (~line 1794):

```cpp
	// M3 universal ids: assign a session id to every no-serialId clip/output/drum BEFORE the relocation, so the
	// relocation (and the loop-length load-binding pass below) resolve by an id that already exists — instead of
	// minting conditionally inline. Golden-neutral (session ids aren't serialized).
	assignSessionIds();

```

- [ ] **Step 2: Build** — no errors.

---

### Task 3: Remove the redundant conditional mints

**Files:**
- Modify: `src/deluge/model/song/song.cpp`

With ids pre-assigned, the inline `setSessionId(mintSessionId())` blocks are dead (their `sessionId() == 0` guards are
now false). Remove them, keeping the surrounding relocation that reads `sessionId()`:

- [ ] **Step 1:** Audio clip (~1817-1818) — delete the two lines:
```cpp
					uint32_t sid = project.mintSessionId();
					thisClip->setSessionId(sid);
```
and change the following `session_clip_params.getOrCreate(sid)` to `getOrCreate(thisClip->sessionId())`.

- [ ] **Step 2:** Synth/kit clip (~1848-1850) — delete:
```cpp
					if (thisClip->sessionId() == 0) {
						thisClip->setSessionId(project.mintSessionId());
					}
```

- [ ] **Step 3:** Synth/kit output (~1851-1853) — delete:
```cpp
					if (thisClip->output->sessionId() == 0) { // shared across the output's clips — mint once
						thisClip->output->setSessionId(project.mintSessionId());
					}
```

- [ ] **Step 4:** Drum (~1905-1906) — delete:
```cpp
								if (nr.drum->sessionId() == 0) {
									nr.drum->setSessionId(project.mintSessionId());
								}
```

- [ ] **Step 5:** MIDI clip (~2172-2174) — delete:
```cpp
							if (thisClip->sessionId() == 0) {
								thisClip->setSessionId(project.mintSessionId());
							}
```

- [ ] **Step 6: Build** — no errors. Read each removal site's surrounding lines first to keep the `getOrCreate(...)` / `OwnerId::…Session(...)` calls that follow, which read the (now pre-assigned) `sessionId()`.

---

### Task 4: Prove universalization + neutrality

**Files:**
- Modify: `tests/golden/edit_undo_redo.cpp`

- [ ] **Step 1: Add a `universalIdsTarget`** (before `} // namespace`), asserting every clip is addressable + bound:

```cpp
// Plan A: after load + assignSessionIds + the M3 load-binding pass, EVERY clip is id-addressable and bound to a DocClip.
void universalIdsTarget(Song* song) {
	printf("# target universal-ids\n");
	int32_t clips = 0, addressable = 0, bound = 0;
	auto checkArray = [&](auto& arr) {
		for (int32_t c = 0; c < std::ssize(arr); c++) {
			Clip* cl = arr[c];
			clips++;
			std::optional<deluge::project::ClipAddress> a = deluge::edits::clipAddressFor(*cl);
			if (a.has_value()) {
				addressable++;
				if (song->project.resolveClip(*a) != nullptr) {
					bound++;
				}
			}
		}
	};
	checkArray(song->project.clips.session_clips);
	checkArray(song->project.clips.arrangement_only_clips);
	check(clips > 0, "universal-ids: song has clips");
	check(addressable == clips, "universal-ids: EVERY clip is id-addressable");
	check(bound == clips, "universal-ids: EVERY clip is bound to a DocClip (load flip is total)");
}
```

- [ ] **Step 2: Call it** in `main` after the other targets (e.g. after `arrangementTarget(song);`). Note: run it BEFORE the destructive kit/clip targets, or before any target that mutates clip membership, so the counts reflect the loaded song. Place it FIRST among the post-load targets (right after the param/note targets, before kitAdd/kitRemove which add/delete clips-or-drums). Simplest: call `universalIdsTarget(song);` immediately after `load` in `main`, before the existing target sequence.

- [ ] **Step 3: Build + run** — `cmake --build build-sim --target edit_undo_redo` then `python3 tests/golden/edit_undo_redo_run.py --binary build-sim/edit_undo_redo 2>&1 | grep -E "^(FAIL|OK=)"`. Expected `OK=40 FAIL=0`. (If some clips are still unaddressable, the assign pass missed a tier — debug which clip type; do NOT weaken the assertion.)

- [ ] **Step 4: Confirm it exercised** on the corpus (grep a song's stdout for `# target universal-ids` + no CHECK FAIL).

---

### Task 5: Gates + commit

- [ ] **Step 1:** edit_test — `cmake --build build-sim --target edit_test && ./build-sim/edit_test` → `edit_test: OK`.
- [ ] **Step 2: GOLDEN NEUTRALITY (the key gate)** — `python3 tests/golden/songs_run.py 2>&1 | tail -1` must be `OK=15 REGRESS=0` (session ids aren't serialized → roundtrip unchanged). If REGRESS>0, the pass changed serialized output — STOP and investigate (a session id must have leaked into save, or the removal changed relocation keying).
- [ ] **Step 3:** `python3 tests/golden/param_record_run.py --binary build-sim/param_record_diff 2>&1 | tail -1` → `OK=3 REGRESS=0`; `./dbt build Debug 2>&1 | grep -E "error:|^\s+text"` → links, note elf text.
- [ ] **Step 4: Update memory** — `m1-edit-substrate.md`: Plan A done; every clip universally addressable+bound; conditional mints removed; unblocks `loopLength_` retirement (modulo runtime-created-clip binding).
- [ ] **Step 5: Commit** (fresh commit; re-run `git commit` after any clang-format reformat):

```bash
git add src/deluge/model/song/song.h src/deluge/model/song/song.cpp tests/golden/edit_undo_redo.cpp
git commit -m "universal session ids: assignSessionIds pass — every clip/output/drum addressable at load"
```
