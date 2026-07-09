# Durable-ID Step 3 — id-first clip construction / `ownDoc_`-kill (readFromTree slice 1) — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Construct a `Clip` with its durable `id` known up-front so the ctor binds its DOM structure record (`clip_structure[id]`) at construction, retiring `ownDoc_`, the `treeDoc_`-fallback, `adoptClip`, and the migrate-on-`setId` rekey — landing the first slice of the `readFromTree` migration.

**Architecture:** The tree adapter exposes the `Value` node it is positioned at (`currentValue()`). The clips-array loader reads the clip node's `id` (and the three pure shell fields) tree-natively, constructs the clip id-first (ctor binds `clip_structure[id]`), then delegates the rest of the clip to the existing streaming `readFromFile`. Every fix is behaviour-preserving; the bit-identity golden is the gate.

**Tech Stack:** C++23/26. Golden harnesses are host-sim binaries run via `python3 tests/golden/<name>_run.py`; firmware via `./dbt build Debug`; unit specs via `./dbt test`.

## Global Constraints

- **Behaviour-preserving; golden bit-identity is the hard gate.** Success = `song_roundtrip` `OK=15 REGRESS=0` (bit-identical), `edit_undo_redo` `OK=40`, `param_record` `OK=3`, `json_roundtrip` fixtures 14/14 + BYO backup `OK=37 FAIL=0`, `./dbt test` 15/15, `./dbt build Debug` links. Corpus: `export DELUGE_SONG_CORPUS="/home/kate/Deluge Backup/SONGS"`.
- Idiomatic C++23/26; no C-casts except where interfacing legacy APIs.
- Single fresh `git commit` per task — never chain `--amend` (the clang-format pre-commit hook reformats+fails; recover with a NEW commit, not `--amend`).
- **DEVIATION FROM SPEC (flagged for review):** the spec's "clip shell = id + the four DocClip fields" read tree-natively is reduced here to **id + the three PURE fields (`section`, `colourOffset`, `launchStyle`)**. `length`/`loopLength` STAYS in `readFromFile` because its handler also sets `originalLength` and `*readAutomationUpToPos` (which bounds the subsequent automation reads) — moving it changes read ordering and risks bit-identity. It writes to the bound store either way, so the `ownDoc_`-kill is unaffected; loopLength moves in a later `readFromTree` slice. See Task 3.

## File structure

- Modify `src/deluge/storage/value_tree_deserializer.h` — add `currentValue()` (Task 1).
- Modify `src/deluge/model/clip/clip.h` + `clip.cpp` — ctor `loadedId`, ctor-binding, delete `ownDoc_`, collapse accessors, delete `setId` (Tasks 2, 4).
- Modify `src/deluge/model/clip/instrument_clip.h`/`.cpp`, `audio_clip.h`/`.cpp` — thread `loadedId` (Task 2), pass file id at the load sites (Task 3).
- Modify `src/deluge/model/song/song.cpp` — `readClipsFromFile` (id-first + shell read, Task 3); delete `adoptClip` + its 4 call sites (Task 4).

---

### Task 1: `ValueTreeDeserializer::currentValue()` accessor

**Files:** Modify `src/deluge/storage/value_tree_deserializer.h`.

**Interfaces — Produces:** `const deluge::storage::Value* ValueTreeDeserializer::currentValue() const` — returns the node the adapter is positioned at (its `pending_`, the child element/value most recently named by `readNextTagOrAttributeName`).

- [ ] **Step 1: Add the accessor.** In `src/deluge/storage/value_tree_deserializer.h`, in the `public:` section (e.g. just after the `shouldCallRoutines()` override at line ~78), add:
```cpp
	/// The Value node this cursor is currently positioned at -- the child element/value most recently
	/// produced by readNextTagOrAttributeName() (null before the first read or once exhausted). Lets a
	/// tree-native loader (readClipFromTree) read a node's fields directly while the streaming interface
	/// still drives the rest. Read-only; does not advance the cursor.
	[[nodiscard]] const Value* currentValue() const { return pending_; }
```
- [ ] **Step 2: Build the golden harness to confirm it compiles.** Run: `cmake --build build-sim --target song_roundtrip 2>&1 | tail -2` → links.
- [ ] **Step 3: Commit.** `git add -A && git commit -q -m "storage: ValueTreeDeserializer::currentValue() accessor (durable-id step 3)"`.

(Additive; no behaviour change, so the goldens are unaffected. Consumed by Task 3.)

---

### Task 2: ctor-binding — bind `clip_structure` in the `Clip` ctor, delete `ownDoc_`

**Files:** Modify `src/deluge/model/clip/clip.h`, `src/deluge/model/clip/clip.cpp`, `src/deluge/model/clip/instrument_clip.h`, `src/deluge/model/clip/audio_clip.h`.

**Interfaces — Produces:**
- `Clip(ClipType newType, Song& song, uint32_t loadedId = 0)` — binds `treeDoc_ = &song.project.getOrCreateDocClip({.clip_id = id_})` in the ctor; `treeDoc_` is non-null for the clip's whole life.
- `InstrumentClip(Song& song, bool initViewFromSong = true, uint32_t loadedId = 0)` and `AudioClip(Song& song, uint32_t loadedId = 0)` — forward `loadedId` to the base.

This task keeps the CURRENT load flow (loaded clips still mint a provisional id and let the `"id"` tag `setId`-rekey it; `adoptClip` stays but becomes a redundant no-op because the ctor already bound). Task 3 makes it truly id-first. Every accessor already resolves through `treeDoc_` once bound, so this is value-identical.

- [ ] **Step 1: Add `loadedId` to the base ctor decl.** In `src/deluge/model/clip/clip.h:51`, change `Clip(ClipType newType, Song& song);` to:
```cpp
	Clip(ClipType newType, Song& song, uint32_t loadedId = 0);
```
- [ ] **Step 2: Bind in the ctor body.** In `src/deluge/model/clip/clip.cpp:46-49`, change:
```cpp
Clip::Clip(ClipType newType, Song& song) : type(newType) {
	// Durable-id Phase 0: every clip gets its id in the ctor. A runtime clip mints from the per-song counter; a loaded
	// clip mints a provisional id here that readTagFromFile's "id" tag immediately overwrites with the file id.
	id_ = song.generateNewClipId();
```
to:
```cpp
Clip::Clip(ClipType newType, Song& song, uint32_t loadedId) : type(newType) {
	// Durable-id Step 3 (ctor-binding): use the loaded file id when known (id-first construction), else mint from the
	// per-song counter. Bind the DOM structure record under that id IN the ctor -- treeDoc_ is authoritative and never
	// null hereafter, so the clip works immediately with no bind-at-creation pass and no ownDoc_ fallback.
	id_ = loadedId != 0 ? loadedId : song.generateNewClipId();
	treeDoc_ = &song.project.getOrCreateDocClip({.clip_id = id_});
```
- [ ] **Step 3: Thread `loadedId` through the subclass ctors.** In `src/deluge/model/clip/instrument_clip.h:51`, change `explicit InstrumentClip(Song& song, bool initViewFromSong = true);` to:
```cpp
	InstrumentClip(Song& song, bool initViewFromSong = true, uint32_t loadedId = 0);
```
In `src/deluge/model/clip/audio_clip.h:37`, change `explicit AudioClip(Song& song);` to:
```cpp
	AudioClip(Song& song, uint32_t loadedId = 0);
```
Then update the two ctor definitions to forward `loadedId` to the base. In `src/deluge/model/clip/instrument_clip.cpp`, the `InstrumentClip::InstrumentClip(Song& song, bool initViewFromSong)` definition's signature becomes `InstrumentClip::InstrumentClip(Song& song, bool initViewFromSong, uint32_t loadedId)` and its base-init `: Clip(ClipType::INSTRUMENT, song)` becomes `: Clip(ClipType::INSTRUMENT, song, loadedId)`. In `src/deluge/model/clip/audio_clip.cpp`, `AudioClip::AudioClip(Song& song)` (audio_clip.cpp:50) becomes `AudioClip::AudioClip(Song& song, uint32_t loadedId)` with base-init `: Clip(ClipType::AUDIO, song, loadedId)`.

  Because `loadedId` defaults to 0 and is the LAST parameter, **none of the 16 existing construction sites change** (they use the default → mint, exactly as today).
- [ ] **Step 4: Delete `ownDoc_` and collapse the accessors.** In `src/deluge/model/clip/clip.h`:
  - Delete line 207: `deluge::project::DocClip ownDoc_{};`
  - Change every accessor to drop the fallback (lines 168, 170, 175, 176, 199, 200, 235, 238):
```cpp
	[[nodiscard]] int16_t colourOffset() const { return treeDoc_->colour_offset; }
	void setColourOffset(int32_t newOffset) { treeDoc_->colour_offset = static_cast<int16_t>(newOffset); }
	...
	[[nodiscard]] uint8_t section() const { return treeDoc_->section; }
	void setSection(uint8_t newSection) { treeDoc_->section = newSection; }
	...
	[[nodiscard]] int32_t loopLength() const { return treeDoc_->loop_length; }
	void setLoopLength(int32_t newLength) { treeDoc_->loop_length = newLength; }
	...
	[[nodiscard]] LaunchStyle launchStyle() const { return static_cast<LaunchStyle>(treeDoc_->launch_style); }
	void setLaunchStyle(LaunchStyle s) { treeDoc_->launch_style = static_cast<uint8_t>(s); }
```
  - Update the stale comments at 192-198 (they describe `ownDoc_`): replace with a one-line note that `treeDoc_` is bound in the ctor and is always valid.
- [ ] **Step 5: Build firmware + all goldens.** Run:
```
./dbt build Debug 2>&1 | tail -2
cmake --build build-sim --target song_roundtrip edit_undo_redo param_record_diff json_roundtrip 2>&1 | tail -2
export DELUGE_SONG_CORPUS="/home/kate/Deluge Backup/SONGS"
python3 tests/golden/songs_run.py --binary build-sim/song_roundtrip 2>&1 | tail -1
python3 tests/golden/edit_undo_redo_run.py --binary build-sim/edit_undo_redo 2>&1 | tail -1
python3 tests/golden/param_record_run.py --binary build-sim/param_record_diff 2>&1 | tail -1
python3 tests/golden/json_roundtrip_run.py --binary build-sim/json_roundtrip 2>&1 | tail -1
./dbt test 2>&1 | grep -iE "tests passed|failed" | head -1
```
Expected: `OK=15 REGRESS=0`, `OK=40 FAIL=0`, `OK=3 REGRESS=0`, `OK=37 FAIL=0`, `100% tests passed`, firmware links.
- [ ] **Step 6: Commit.** `git add -A && git commit -q -m "clip: bind clip_structure in the Clip ctor; delete ownDoc_ (durable-id step 3)"`.

(If a golden REGRESSES: the likely culprit is a transient/clone clip whose `~Clip` must `removeDocClip` — verify `~Clip` still calls it — or `copyBasicsFrom` not writing via setters — it already does. `edit_undo_redo`'s undo-reinsert path is the sharpest probe.)

---

### Task 3: id-first — `readClipFromTree` shell read + delegate; wire `readClipsFromFile`

**Files:** Modify `src/deluge/model/song/song.cpp` (`readClipsFromFile`, ~2241-2295), `src/deluge/model/clip/clip.cpp` (remove the `id`/`section`/`colourOffset`/`launchStyle` handlers from `Clip::readTagFromFile`).

**Interfaces — Consumes:** `ValueTreeDeserializer::currentValue()` (Task 1); `Clip(…, loadedId)` (Task 2).

The clips loop reads the clip node's `id` and the three pure shell fields directly from the `Value`, constructs the clip id-first, then hands the node to the existing `readFromFile`. The moved tags no longer have handlers, so `readFromFile` skips them.

- [ ] **Step 1: Read id + pure shell fields and construct id-first.** In `src/deluge/model/song/song.cpp`'s `readClipsFromFile` (the `readClip:` block, ~2263-2271), replace the construction:
```cpp
			Clip* newClip;
			if (clipType == ClipType::INSTRUMENT) {
				newClip = new (memory) InstrumentClip(*this, /*initViewFromSong=*/false);
			}
			else {
				newClip = new (memory) AudioClip(*this);
			}

			Error error = newClip->readFromFile(reader, this);
```
with (note: `reader` here is the `Deserializer&`; downcast to the tree adapter since after Step 2b the song load is always tree-backed):
```cpp
			// Durable-id Step 3: read the clip's shell (id + the three pure DocClip fields) tree-natively from the
			// Value node, construct the clip WITH its file id (the ctor binds clip_structure[id]), then delegate the
			// rest to the streaming readFromFile. The moved tags no longer have handlers there, so they are skipped.
			const deluge::storage::Value* clipNode =
			    static_cast<deluge::storage::ValueTreeDeserializer&>(reader).currentValue();
			uint32_t loadedId = 0;
			if (clipNode != nullptr) {
				if (const deluge::storage::Value* n = clipNode->find("id")) {
					loadedId = static_cast<uint32_t>(stringToInt(n->str().c_str()));
				}
			}

			Clip* newClip;
			if (clipType == ClipType::INSTRUMENT) {
				newClip = new (memory) InstrumentClip(*this, /*initViewFromSong=*/false, loadedId);
			}
			else {
				newClip = new (memory) AudioClip(*this, loadedId);
			}

			// Shell fields (pure setters -> the bound store). length/loopLength stays in readFromFile (its handler also
			// sets originalLength + readAutomationUpToPos, coupling it to the automation read; it writes the same store).
			if (clipNode != nullptr) {
				if (const deluge::storage::Value* n = clipNode->find("section")) {
					newClip->setSection(std::min<uint8_t>(stringToInt(n->str().c_str()), (uint8_t)(kMaxNumSections - 1)));
				}
				if (const deluge::storage::Value* n = clipNode->find("colourOffset")) {
					newClip->setColourOffset(stringToInt(n->str().c_str()));
				}
				if (const deluge::storage::Value* n = clipNode->find("launchStyle")) {
					newClip->setLaunchStyle(stringToLaunchStyle(n->str().c_str()));
				}
			}

			Error error = newClip->readFromFile(reader, this);
```
  Ensure the includes at the top of `song.cpp` cover `storage/value_tree_deserializer.h` and `storage/value.h` (add them if missing). `stringToInt`/`stringToLaunchStyle`/`kMaxNumSections` are already in scope (used by the current clip read).
- [ ] **Step 2: Remove the moved handlers from `Clip::readTagFromFile`.** In `src/deluge/model/clip/clip.cpp`, delete these four `else if` blocks (they are now read tree-natively): `"id"` (721-725), `"section"` (741-743), `"colourOffset"` (752-754), `"launchStyle"` (788-790). Do **NOT** touch `"trackLength"`/`"length"` (745-750) — loopLength stays here. Removing a handler makes the tag fall through to the final `else reader.exitTag(tagName)` (skipped), which is correct because the loader already read them.
- [ ] **Step 3: Build firmware + all goldens** (same commands as Task 2 Step 5). Expected: `OK=15 REGRESS=0`, `OK=40`, `OK=3`, `OK=37`, specs 15/15, firmware links. A REGRESS here means either the shell read diverges (compare a REGRESSing song's `id`/`section`/`colourOffset`/`launchStyle` in gen1 vs the store) or the `currentValue()` positioning is off (it must be the clip-content Map at construction time).
- [ ] **Step 4: Commit.** `git add -A && git commit -q -m "song: id-first clip construction reads shell tree-natively, delegates rest (durable-id step 3)"`.

---

### Task 4: delete `adoptClip` + call sites and `Clip::setId`

**Files:** Modify `src/deluge/model/song/song.cpp` (delete `adoptClip` ~1215-1234 and its 4 call sites), `src/deluge/model/song/song.h` (its decl), `src/deluge/model/clip/clip.h` (delete `setId`).

With id-first construction, the ctor binds under the file id directly, so `adoptClip`'s seed-then-flip is dead (its `isDomBound()` guard early-returns for every clip), and `Clip::setId` has no callers (the `"id"` handler is gone).

- [ ] **Step 1: Confirm `Clip::setId` and `adoptClip` are now callerless.** Run:
```
grep -rn "\.setId(\|->setId(" src/deluge/ | grep -iv "output\|drum"
grep -rn "adoptClip" src/deluge/
```
Expected for `setId`: no clip hits (only Output/Drum, which keep their own `setId`). For `adoptClip`: only its definition + the 4 call sites (song.cpp ~370, ~1986, ~1989, ~5264) + the header decl.
- [ ] **Step 2: Delete the 4 `adoptClip(...)` call sites** in `src/deluge/model/song/song.cpp` (~370 `adoptClip(firstClip);`, ~1986/~1989 the two in the post-load loop, ~5264 `adoptClip(newClip);`). Delete the loop wrapper if a call was the loop's only body; otherwise just the call line.
- [ ] **Step 3: Delete the `adoptClip` definition** (`void Song::adoptClip(Clip* clip) { … }`, ~1215-1234 in song.cpp) and its declaration in `src/deluge/model/song/song.h`.
- [ ] **Step 4: Delete `Clip::setId`** in `src/deluge/model/clip/clip.h:225` (`void setId(uint32_t id) { id_ = id; }`). Keep `id()` (225 above it). If the compiler flags any remaining `setId` use on a clip, that is a real gap — surface it, do not re-add blindly.
- [ ] **Step 5: Build firmware + all goldens** (same commands as Task 2 Step 5). Expected all green.
- [ ] **Step 6: Commit.** `git add -A && git commit -q -m "song/clip: delete adoptClip + Clip::setId, now redundant under ctor-binding (durable-id step 3)"`.

---

## Self-Review

**Spec coverage.** Task 1 = `currentValue()` (spec Component 1). Task 2 = ctor-binding + `ownDoc_` delete + accessor collapse (Component 2). Task 3 = `readClipFromTree` shell read + delegate + handler removal (Component 3) — with the documented loopLength deviation (Global Constraints). Task 4 = deletions (Component 4). The `migrate-on-setId` rekey retires implicitly: with the `"id"` handler gone (Task 3) and `setId` gone (Task 4), nothing triggers the rekey; if a distinct rekey function exists it becomes dead and Task 4's callerless-check surfaces it.

**Placeholder scan.** All steps carry exact code/paths/commands. The one judgement call — the loopLength deviation — is stated explicitly with its reason, not left vague.

**Type consistency.** `currentValue() → const Value*`; `Clip(ClipType, Song&, uint32_t)`; `InstrumentClip(Song&, bool, uint32_t)`; `AudioClip(Song&, uint32_t)`; `getOrCreateDocClip(ClipAddress{.clip_id})`; `find(key)→const Value*` then `str()`. Consistent across tasks.

**Note for the executor.** Task 2 is the load-bearing change (ctor-binding + `ownDoc_` delete); expect the golden to catch any clone/undo lifetime issue there. Task 3's `currentValue()` positioning and the shell-before-contents ordering are the two things to watch. The loopLength deviation from the spec is flagged for the human at execution handoff.
