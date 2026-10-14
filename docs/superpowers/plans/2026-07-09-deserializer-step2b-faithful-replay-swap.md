# Record-First Deserializer — Step 2b (Faithful-Replay Swap) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Route the whole song load through the `Value` tree (`parseXml`/`parseJson` → `ValueTreeDeserializer` → existing `loadSong`), bit-for-bit identically to the streaming load — proven by flipping the golden harnesses to load-via-tree and driving them to bit-identical over the corpus.

**Architecture:** Hard-swap (no fallback). The golden harnesses are flipped to load-via-tree so bit-identity over the 15-song corpus IS the whole-load fidelity test; the production `performLoad` seam gets the same tree path. The Step-2a `xml_replay_diff` harness is the isolation tool for any golden divergence.

**Tech Stack:** C++23/26. Golden harnesses are host-sim binaries (`build-sim` targets in `sim/CMakeLists.txt`), run via `python3 tests/golden/<name>_run.py`. Firmware via `./dbt build Debug`. Uses Step-2a's `deluge::storage::parseXml`, `ValueTreeDeserializer`, `SourceFormat`.

## Global Constraints

- **Hard-swap, golden is the sole net.** Success = `songs` `OK=15 REGRESS=0`, `param_record` `OK=3`, `edit_undo_redo` `OK=40` (all with load-via-tree), firmware links. Corpus: `export DELUGE_SONG_CORPUS="/home/kate/Deluge Backup/SONGS"`.
- Load-via-tree must be **bit-identical** to load-via-stream (the baselines are stream-generated).
- Idiomatic C++23/26; no C-casts except where interfacing legacy APIs.
- Single fresh `git commit` per task — never chain `--amend` (clang-format hook reformats+fails; recover with a new commit).
- The swap is **song-file-only**; do not touch preset/instrument file loads or `Song::readFromFile`/sub-`readFromFile` bodies.

## File structure

- Modify `src/deluge/storage/value_tree_deserializer.cpp` — real `tryReadingFirmwareTagFromFile` (Task 1).
- Modify `tests/golden/song_roundtrip.cpp` — load-via-tree (Task 2).
- Modify `tests/golden/edit_undo_redo.cpp`, `tests/golden/param_record_diff.cpp` (or its source) — load-via-tree (Task 3).
- Create `src/deluge/storage/read_whole_file.{h,cpp}` (or a helper in storage_manager) + Modify `src/deluge/gui/ui/load/load_song_ui.cpp` — production seam (Task 4).
- Create `tests/golden/json_roundtrip.cpp` + a `sim/CMakeLists.txt` target — JSON round-trip fixture (Task 5).
- Possibly add `xml_replay_diff` fragments during Task 2 debugging.

---

### Task 1: Adapter `tryReadingFirmwareTagFromFile` real implementation

**Files:** Modify `src/deluge/storage/value_tree_deserializer.cpp` (the currently-stubbed `tryReadingFirmwareTagFromFile`).

**Interfaces — Produces:** `ValueTreeDeserializer::tryReadingFirmwareTagFromFile(char const* tagName, bool ignoreIncorrectFirmware)` reproducing `XMLDeserializer`'s logic verbatim (it's format-agnostic — same code works for both modes).

- [ ] **Step 1: Replace the stub** with the real logic (copy `XMLDeserializer::tryReadingFirmwareTagFromFile` from `src/deluge/storage/Deserializer.cpp`):
```cpp
Error ValueTreeDeserializer::tryReadingFirmwareTagFromFile(char const* tagName, bool ignoreIncorrectFirmware) {
	if (!strcmp(tagName, "firmwareVersion")) {
		song_firmware_version = FirmwareVersion::parse(readTagOrAttributeValue());
	}
	else if (!strcmp(tagName, "earliestCompatibleFirmware")) {
		auto earliest = FirmwareVersion::parse(readTagOrAttributeValue());
		if (earliest > FirmwareVersion::current() && !ignoreIncorrectFirmware) {
			return Error::FILE_FIRMWARE_VERSION_TOO_NEW;
		}
	}
	else {
		return Error::RESULT_TAG_UNUSED;
	}
	return Error::NONE;
}
```
Include whatever `song_firmware_version` / `FirmwareVersion` need (they're in `storage_manager.h`/`firmware_version.h`; the adapter's `.cpp` already links the host-sim env). Note: the real impl calls `closeWriter()` before the too-new return — the adapter has no writer, so omit that (the `Error` return is what matters).
- [ ] **Step 2: Build the harness to confirm it still compiles** — `cmake --build build-sim --target xml_replay_diff 2>&1 | tail -2` → links. (`./dbt test` also stays green — the CppSpec adapter path doesn't call this.)
- [ ] **Step 3: Commit** — `git commit -q -m "storage: ValueTreeDeserializer tryReadingFirmwareTagFromFile real impl (deserializer step 2b)"`.

(This is validated end-to-end by Task 2 — a real song's `firmwareVersion` tag flows through it and the load must stay bit-identical.)

---

### Task 2: Flip `song_roundtrip` to load-via-tree + drive to bit-identical (the crux)

**Files:** Modify `tests/golden/song_roundtrip.cpp` (the load site — around line 49). Possibly add `xml_replay_diff` fragments while debugging.

**Interfaces — Consumes:** `parseXml` (Step 2a), `ValueTreeDeserializer{Value, SourceFormat}`, Task 1's firmware impl.

Current load (song_roundtrip.cpp:49-53):
```cpp
XMLDeserializer reader((uint8_t*)xml, xmlLen);
activeDeserializer = &reader;
Error e = reader.openXMLFile(nullptr, "song");
if (e == Error::NONE) e = deluge::project::loadSong(*song, reader);
```

- [ ] **Step 1: Replace with the tree load.** Parse the whole XML to a `Value`, position the adapter at the `<song>` element's content, and run `loadSong` over it:
```cpp
std::optional<deluge::storage::Value> tree = deluge::storage::parseXml(std::string_view((const char*)xml, (size_t)xmlLen));
Error e = tree ? Error::NONE : Error::FILE_CORRUPTED;
if (e == Error::NONE) {
	// parseXml yields a Map whose single entry "song" is the <song> element's content map;
	// the stream reader hands loadSong a reader positioned INSIDE <song>, so use that inner map as the root.
	const deluge::storage::Value* songNode = tree->find("song");
	e = songNode ? Error::NONE : Error::FILE_CORRUPTED;
	if (e == Error::NONE) {
		deluge::storage::ValueTreeDeserializer adapter(*songNode, deluge::storage::SourceFormat::Xml);
		activeDeserializer = &adapter; // keeps the AudioEngine-yield gate off, as the stream path did
		e = deluge::project::loadSong(*song, adapter);
	}
}
```
(If `loadSong` expects to `match('{')` INTO `song` itself rather than start inside it, use `*tree` as the root instead — determine which by making the golden pass; the positioning is the one thing to nail. Keep the `#include "storage/parse_xml.h"` and `storage/value_tree_deserializer.h`.)
- [ ] **Step 2: Build + run the songs golden** — `cmake --build build-sim --target song_roundtrip 2>&1 | tail -2 && export DELUGE_SONG_CORPUS="/home/kate/Deluge Backup/SONGS" && python3 tests/golden/songs_run.py --binary build-sim/song_roundtrip 2>&1 | tail -3`. Expected on first run: likely some `REGRESS` (positioning / an adapter fidelity gap over the full load). Target: `OK=15 REGRESS=0`.
- [ ] **Step 3: For each divergence, isolate then fix.** A `REGRESS` means load-via-tree diverged from load-via-stream for that song. Isolate: find the smallest XML structure in that song that the adapter replays wrong, add it as a fragment to `tests/golden/xml_replay_diff.cpp` (the Step-2a differential harness — it compares the adapter to the live `XMLDeserializer` token-for-token), watch it FAIL, fix `ValueTreeDeserializer` XML mode (or `parseXml`) until that fragment AND the song pass. Re-run `xml_replay_diff` (must stay `OK`) and the songs golden. Repeat until `OK=15 REGRESS=0`.
- [ ] **Step 4: Confirm the isolation harness is still green** — `cmake --build build-sim --target xml_replay_diff && build-sim/xml_replay_diff` → `XML_REPLAY_DIFF: OK`; `./dbt test 2>&1 | tail -3` → all green.
- [ ] **Step 5: Commit** — `git commit -q -m "test(golden): song_roundtrip loads via the Value tree, bit-identical (deserializer step 2b)"` (fold any adapter/parseXml fixes + new harness fragments into this commit or adjacent commits).

---

### Task 3: Flip `edit_undo_redo` + `param_record_diff` to load-via-tree

**Files:** Modify `tests/golden/edit_undo_redo.cpp` (load at :97) and the `param_record_diff` source (its load site, same `XMLDeserializer`+`loadSong` pattern — grep it).

**Interfaces — Consumes:** the now-proven tree load from Task 2 (same `parseXml`→`find("song")`→adapter→`loadSong` shape).

- [ ] **Step 1: Apply the identical flip** to both harnesses' load sites (mirror Task 2's Step 1 exactly — same `parseXml`/`find("song")`/adapter code). These should pass once Task 2 proved the adapter, but they exercise different code paths (undo/redo, param replay) so may surface a new fidelity gap.
- [ ] **Step 2: Run both goldens** — `cmake --build build-sim --target edit_undo_redo param_record_diff 2>&1 | tail -3`, then `python3 tests/golden/edit_undo_redo_run.py --binary build-sim/edit_undo_redo | tail -1` (expect `OK=40 FAIL=0`) and `python3 tests/golden/param_record_run.py --binary build-sim/param_record_diff | tail -1` (expect `OK=3 REGRESS=0`).
- [ ] **Step 3: Any divergence → same isolate-via-`xml_replay_diff`-then-fix loop as Task 2.**
- [ ] **Step 4: Commit** — `git commit -q -m "test(golden): edit_undo_redo + param_record load via the Value tree (deserializer step 2b)"`.

---

### Task 4: Whole-file read helper + wire the `performLoad` production seam

**Files:** Create `src/deluge/storage/read_whole_file.{h,cpp}` (or add a `StorageManager` method); Modify `src/deluge/gui/ui/load/load_song_ui.cpp` (the seam: `openDelugeFile(currentFileItem, "song")` at ~:344, `loadSong(*preLoadedSong, *activeDeserializer)` at ~:449).

**Interfaces — Produces:** a helper that reads a whole file into a heap buffer: `Error readWholeFile(const FilePointer& fp, char** outBuf, uint32_t* outLen)` (allocate `f_size` bytes via the deluge allocator, `f_read`, caller frees), OR reuse the existing cluster reader in a loop. Then the seam parses → adapter → `loadSong`.

- [ ] **Step 1: Implement the whole-file read helper** — open the file (`f_open`/the existing `openFilePointer`), get `f_size`, allocate a buffer via `GeneralMemoryAllocator`, `f_read` the whole file (loop if `f_read` caps per call), return `Error::INSUFFICIENT_RAM` if the allocation fails and a file error on read failure. Free happens at the call site after parse.
- [ ] **Step 2: Wire the seam in `load_song_ui.cpp`** — replace the `openDelugeFile("song")` + `loadSong(*preLoadedSong, *activeDeserializer)` pair with: read whole file → `parseJson`/`parseXml` (by `.Json` filename) → free buffer → `ValueTreeDeserializer adapter(*songNode, mode)` → `loadSong(*preLoadedSong, adapter)`. Match Task 2's positioning (`find("song")` root). Return `FILE_CORRUPTED` on parse `nullopt`, `INSUFFICIENT_RAM` on OOM. Keep the existing error-display flow.
- [ ] **Step 3: Build firmware** — `./dbt build Debug 2>&1 | tail -2` → links.
- [ ] **Step 4: Re-run the songs golden** — still `OK=15 REGRESS=0` (the production seam feeds the same proven tree path; this confirms no regression from the helper/seam wiring). `./dbt test` green.
- [ ] **Step 5: Commit** — `git commit -q -m "storage: hard-swap song load through the Value tree at the performLoad seam (deserializer step 2b)"`.

(On-device peak-RAM check + a real load-and-play smoke are the user's to run — the hard-swap has no toggle-off, so a big song could OOM where streaming didn't; flag it in the report, do not attempt on-device from here.)

---

### Task 5: JSON round-trip fixture (exercise JSON mode end-to-end)

**Files:** Create `tests/golden/json_roundtrip.cpp`; Modify `sim/CMakeLists.txt` (add a `json_roundtrip` target, mirror `song_roundtrip` at its `add_executable` block).

**Interfaces — Consumes:** `parseJson`, `ValueTreeDeserializer{Value, SourceFormat::Json}`, `JsonSerializer` (`src/deluge/storage/JSONSerializer.cpp`), `loadSong`.

- [ ] **Step 1: Write the fixture** — for each corpus song: (a) load it via the XML tree path (as Task 2), (b) save it to a JSON buffer via `JsonSerializer`, (c) load *that* JSON via `parseJson` + `SourceFormat::Json` + `loadSong` into a fresh `Song`, (d) save *that* to a buffer and assert it equals the buffer from (b) — i.e. JSON-load ≡ JSON-save round-trips identically (proving `parseJson` + JSON mode reproduce what `JsonSerializer` wrote). Self-checking (exit 0/nonzero), mirror `song_roundtrip`'s host-sim setup.
- [ ] **Step 2: Add the `json_roundtrip` target** to `sim/CMakeLists.txt` (copy `song_roundtrip`'s `add_executable`/`target_*` block, swap the source to `tests/golden/json_roundtrip.cpp`).
- [ ] **Step 3: Build + run** — `cmake --build build-sim --target json_roundtrip 2>&1 | tail -2 && export DELUGE_SONG_CORPUS="/home/kate/Deluge Backup/SONGS" && build-sim/json_roundtrip 2>&1 | tail -3` → passes (JSON round-trip identical). If it diverges, JSON mode or `parseJson` has a gap — isolate + fix.
- [ ] **Step 4: Commit** — `git commit -q -m "test(golden): JSON round-trip fixture exercises parseJson + JSON-mode replay (deserializer step 2b)"`.

---

## Self-Review

**Spec coverage.** Task 1 = the `firmwareVersion` real impl (spec §firmwareVersion). Task 2+3 = the golden-as-differential-test flip (spec §Testing) — the whole-load fidelity proof. Task 4 = the whole-file helper + `performLoad` production seam + OOM handling (spec §The swap, §RAM/lifetime). Task 5 = the JSON round-trip fixture (spec §Testing, closes the carry-forward). On-device RAM/smoke is explicitly the user's (spec §Testing, §Risks) — not a task.

**Placeholder scan.** Task 1 has the exact code. Task 2's positioning has a stated determination path (make the golden pass, `find("song")` vs `*tree`) — not a TODO but a genuine wire-time decision the golden adjudicates. Tasks 2/3's "isolate-then-fix" loop is inherently iterative (debugging to bit-identity); the mechanism (add an `xml_replay_diff` fragment, fix, re-run) is concrete.

**Type consistency.** `parseXml/parseJson → std::optional<Value>`; `ValueTreeDeserializer(const Value&, SourceFormat)`; `loadSong(::Song&, Deserializer&)`; `SourceFormat::{Xml,Json}`. The `find("song")` root and `activeDeserializer = &adapter` are consistent across Tasks 2/3/4.

**Note for the executor.** Task 2 is the crux and the debugging grind — dispatch it on a strong model and expect iteration; its `OK=15 REGRESS=0` is the whole-branch's real gate. Tasks 3/4/5 are lower-risk once the adapter is proven. Do NOT attempt any on-device step.
