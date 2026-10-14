# Durable-ID Phase 0: `id` at Construction — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans (inline). Steps use checkbox (`- [ ]`) syntax.

**Goal:** Make every clip acquire its durable `id` **in its constructor** — loaded clips from the file (identity-first loader), runtime clips minted from the per-song counter — so a clip is never id-less, honouring the ctor-binding invariant the structure flip (Phase 2) depends on.

**Architecture:** Two golden-gated sub-slices. 0a threads `Song&` + `loadedId` into the clip ctors and sets `id_` there (loaded value or a fresh mint), plus the identity-first loader; `id_` stays `optional` (always `Some` in practice). 0b flips `id_` to non-optional and fixes the readers. Refines the spec's Phase 0/1 order (loader folded into 0a; minting is in-ctor per the chosen ctor-binding invariant, **not** attach-first). Clips only; outputs/drums later.

**Tech Stack:** C++23 firmware; host-sim golden harnesses (`song_roundtrip`, `param_record_diff`, `edit_undo_redo`) in `build-sim`; firmware `./dbt build Debug`.

## Global Constraints

- Behaviour-preserving, golden bit-identical each sub-slice: `songs OK=15 REGRESS=0`, `param_record OK=3`, `edit_undo_redo OK=40`, firmware Debug links.
- No file-format change (on-disk attribute is `"id"`, already written first).
- Single fresh `git commit` per sub-slice — never chain `--amend` (clang-format hook reformats + fails the first commit; recover with a new commit).
- Corpus: `export DELUGE_SONG_CORPUS="/home/kate/Deluge Backup/SONGS"`.
- `id_` stays `std::optional<uint32_t>` through 0a; becomes `uint32_t` in 0b.

## Context the implementer needs

- Clip ctors today: `Clip(ClipType)` (base), `InstrumentClip(Song* song = nullptr)` (the `Song*` is used **only** to init yScroll; `instrument_clip.cpp:162` passes `nullptr` on purpose so a clone's yScroll is left for `copyBasicsFrom`), `AudioClip()`.
- Lazy id mint today is at save: `clip.cpp:665` `if (!id().has_value()) setId(song->generateNewClipId());` inside `writeDataToFile`.
- Loader: `song.cpp` `readClipsFromFile` matches the element type (`"instrumentClip"`/`"audioClip"`), `new InstrumentClip()`/`new AudioClip()`, then `newClip->readFromFile(reader, this)`. `id` is the **first** attribute `readFromFile` reads (save writes it first, `clip.cpp:665`).
- Per-song counter: `Song::generateNewClipId()` (`clipIdCounter_`); already advanced past loaded ids by `song.cpp:3638-3649` `advancePast(clipIdCounter_, clip->id())`.
- ~17 runtime construction sites: `grep -rn "new .*InstrumentClip\|new .*AudioClip" src/ tests/`. All have a song in scope (`currentSong`, `this`, `modelStack->song`).

## Ctor API (honours the ctor-binding invariant)

```cpp
// clip.h — base ctor takes the song (for the counter now; for clip_structure binding in Phase 2)
// and an optional loaded id. Runtime: mints. Load: uses the file id.
Clip(ClipType type, Song& song, std::optional<uint32_t> loadedId = std::nullopt);
```

- Base ctor body: `id_ = loadedId ? loadedId : std::optional{song.generateNewClipId()};` — `id_` is always `Some` after construction.
- `InstrumentClip(Song& song, std::optional<uint32_t> loadedId = std::nullopt, bool initViewFromSong = true)` — forwards `(song, loadedId)` to the base; the existing yScroll-from-song block runs only when `initViewFromSong` (replacing the old "was a non-null `Song*` passed?" test). The clone site (`instrument_clip.cpp:162`) passes `initViewFromSong=false` — same yScroll behaviour as passing `nullptr` today, but the song is still available for the id.
- `AudioClip(Song& song, std::optional<uint32_t> loadedId = std::nullopt)`.
- The save-time lazy-mint guard (`clip.cpp:665`) is removed — `id_` is always set; keep `writeAttribute("id", *id())`.

---

## Sub-slice 0a: `id` minted/loaded in the ctor + identity-first loader

**Files:** `clip.h`, `clip.cpp`, `instrument_clip.h/.cpp`, `audio_clip.h/.cpp` (ctor sigs + bodies + yScroll gate); `song.cpp` `readClipsFromFile` (id-peek + pass); all ~17 runtime construction sites (thread the in-scope song; clone sites pass `initViewFromSong=false`).

**Interfaces:**
- Produces: `Clip(ClipType, Song&, std::optional<uint32_t> loadedId = nullopt)` setting `id_`; `InstrumentClip(Song&, optional<uint32_t> = nullopt, bool initViewFromSong = true)`; `AudioClip(Song&, optional<uint32_t> = nullopt)`. After construction every clip has `id()` set.

- [ ] **Step 1: New ctor signatures + bodies.** Base sets `id_` from `loadedId`-or-mint; `InstrumentClip` gates yScroll on `initViewFromSong`; `AudioClip` forwards. Do **not** touch call sites yet — build will fail at the sites; that is the compiler-driven worklist.

- [ ] **Step 2: Thread the song through all runtime construction sites.** Each `new InstrumentClip(...)` / `new AudioClip(...)` passes the in-scope song (`currentSong` / `this` / `modelStack->song`). The two clone sites that today pass `nullptr` (`instrument_clip.cpp:162`, and check `audio_clip.cpp` clones) pass the song + `initViewFromSong=false`. `tests/projection/edit_test.cpp:263` `new AudioClip()` → pass its `song`.

- [ ] **Step 3: Identity-first loader.** In `readClipsFromFile`, after matching the element type, read the next name; if `"id"`, read its int and construct `new InstrumentClip(*this, fid)` / `new AudioClip(*this, fid)`, then have `readFromFile` continue from the tag **after** id; else construct `new InstrumentClip(*this)` / `new AudioClip(*this)` (ctor mints a provisional id) and let `readFromFile` process the peeked tag as its first (old-file / no-id branch — `readFromFile` overwrites the provisional via `setId` if a later `id` appears).

- [ ] **Step 4: Remove the save-time lazy-mint guard** (`clip.cpp:665`) — keep `writeAttribute("id", *id())`.

- [ ] **Step 5: Build + gate + firmware + commit.** `cmake --build build-sim --target edit_undo_redo song_roundtrip param_record_diff`; run all three nets (`OK=15 REGRESS=0` / `OK=3` / `OK=40`); `./dbt build Debug` links. Single fresh commit `dom(id): mint/load clip id in the ctor + identity-first loader (durable-id Phase 0a)`.

**Golden-neutral rationale:** corpus clips load their id from the file (same value, set in the ctor instead of mid-parse) → byte-identical save. Runtime clips shift from save-order to ctor-order ids (untested by the load→save corpus). `advancePast` still yields `counter = max(loaded id)+1`. A provisional mint for an old-file no-id clip is overwritten by `setId` and the counter re-advanced, matching today's fresh-id assignment.

## Sub-slice 0b: `id_` non-optional + fix readers

**Files:** `clip.h` (`std::optional<uint32_t> id_` → `uint32_t id_{0}`; `id()` returns `uint32_t`); the ~25 optional-readers from `grep -rIn "id()\.has_value\|!.*->id()\| id() ==\|\*.*->id()\|id().value" src/ tests/`.

- Fix by category: **is-addressable** (`!clip->id() && clip->sessionId()==0`) → the `!id()` disjunct is now always false; simplify to the session-only check (which itself dies in Phase 3/4). **id-equality** (`id() == x`) → valid on `uint32_t`, unchanged. **deref** (`*id()` / `id().value()`) → `id()`. **lazy-mint guards** → already removed in 0a.
- Audit each site for a genuine "never persisted" dependence (distinct from "has an id"). The survey shows the only such uses were the removed lazy guards and the macro-ref writes (`serialization.cpp:184,190`, now always-write — golden-neutral since corpus clips all have ids). Add an explicit `unsaved_` bool **only** if a genuine dependence is found; otherwise none is needed.
- Build + all three nets + firmware; single fresh commit `dom(id): Clip::id_ non-optional (durable-id Phase 0b)`.

---

## Self-Review

**Spec coverage.** Implements the spec's *Phase 0* (id at construction, `id_` non-optional, counter init) and folds the *identity-first loader* (spec Phase 1) into 0a because in-ctor minting depends on it to avoid throwaway mints for loaded clips — a documented refinement, not a gap. **Honours the ctor-binding invariant** the user chose (id minted *in the ctor*, `Song&` threaded in — the same `Song&` Phase 2 needs to bind `clip_structure`), rather than the rejected attach-first/site-minting. The `InstrumentClip` yScroll-`Song*` collision is resolved by an explicit `initViewFromSong` flag, preserving the clone sites' behaviour. Outputs/drums deferred (same recipe, later phase).

**Placeholder scan.** 0a fully stepped. 0b lists exact files + reader categories; its bite-sized steps are finalized when 0a lands (reader fixes depend on the 0a shape), per this project's per-slice rhythm.

**Type consistency.** `Clip(ClipType, Song&, std::optional<uint32_t>)`; `id()` returns `std::optional<uint32_t>` through 0a then `uint32_t` from 0b; `setId(uint32_t)` unchanged; `loadedId` is `std::optional<uint32_t>`.
