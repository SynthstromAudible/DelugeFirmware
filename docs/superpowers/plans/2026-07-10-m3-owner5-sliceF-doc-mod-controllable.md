# M3 owner-5 Slice F — shared DocModControllable + nest in DocSound — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:executing-plans. Steps use checkbox (`- [ ]`).

**Goal:** Define a portable, reusable `DocModControllable` mirroring `ModControllableAudio`'s non-param structural fields (filter/modFX + delay/sidechain/compressor/stutter), nest it in `DocSound`, and populate it from `ModControllableAudio::syncDocModControllable()`. Additive shadow → golden bit-identical.

**Architecture:** `ModControllableAudio` is a base shared by Sound + GlobalEffectableForClip/Song, so its DOM representation is a STANDALONE `DocModControllable` (not folded into DocSound), reusable when those FX owners migrate. `DocSound` nests it by value (`DocModControllable mod_controllable;` — always present for a Sound). The copy logic lives on the shared base: `ModControllableAudio::syncDocModControllable(DocModControllable&)` copies engine→doc; `Sound::syncDocSound()` calls it on the nested member. Rebuild-at-save (rides syncDocSound). `midi_knobs` (MIDI-learn) is EXCLUDED (deferred M4/M5).

**Tech Stack:** C++23. Firmware `./dbt build Debug`; sim goldens; specs `./dbt test`.

## Global Constraints

- **Golden BIT-IDENTICAL / additive.** Success = `song_roundtrip OK=16 REGRESS=0`, `edit_undo_redo OK=40`, `param_record OK=3`, `json_roundtrip OK=38`, `edit_test: OK`, `param_automation projection: OK`, `./dbt test` 15/15, firmware links. Corpus: `export DELUGE_SONG_CORPUS="/home/kate/Deluge Backup/SONGS"`.
- `doc_mod_controllable.h` portable — `<cstdint>` only. `DocModControllable` WRITTEN, never READ this slice. Engine stays authoritative.
- Store the RAW engine field values (e.g. `delay.syncLevel` internal value) — do NOT apply the file-value conversions (`writeAbsoluteSyncLevelToFile`/`convertSyncLevelFromFileValueToInternalValue`); those are a save-flip concern, not the shadow's.
- Idiomatic C++23. **NEVER `git commit --amend`** (clang-format hook → FRESH `git commit`). **NEVER add Co-Authored-By / "Generated with Claude" trailers.**

## Field map (from recon — VERIFY names/types)

Top-level (`mod_controllable_audio.h:106-115`, public raw): `clippingAmount` (uint8_t), `lpfMode`/`hpfMode` (`FilterMode`), `filterRoute` (`FilterRoute`), `modFXType_` (`ModFXType`, uint8_t-backed). All small enums → store as `uint8_t` underlying.
Nested:
- `delay` (`Delay`, `dsp/delay/delay.h`, public): `pingPong` (bool), `analog` (bool), `syncType` (`SyncType`, u8), `syncLevel` (`SyncLevel`, u8).
- `sidechain` (`SideChain`, `modulation/sidechain/sidechain.h`, public): `attack` (int32_t), `release` (int32_t), `syncType` (u8), `syncLevel` (u8).
- `compressor` (`RMSFeedbackCompressor`, `dsp/compressor/rms_feedback.h`) — PRIVATE fields, via GETTERS (all return q31_t = int32_t): `getAttack()`, `getRelease()`, `getThreshold()`, `getRatio()`, `getSidechain()` (this is the compressor's own HPF cutoff, named `compHPF` in the file — NOT the SideChain member), `getBlend()`.
- `stutterConfig` (`StutterConfig`, `model/fx/stutterer.h`, public): `quantized` (bool), `reversed` (bool), `pingPong` (bool). OMIT `useSongStutter` (never serialized).

Sound inherits ModControllableAudio (`Sound : … ModControllableAudio`). `Sound::syncDocSound()` (sound.cpp, from Slice B) is the call site.

---

### Task 1: `DocModControllable` + nest in `DocSound` (additive, unused)

**Files:** `src/deluge-project/doc_mod_controllable.h` (new), `doc_sound.h` (nest).

- [ ] **Step 1: `DocModControllable`.** Create `src/deluge-project/doc_mod_controllable.h` (copyright header, `<cstdint>` only):
```cpp
#pragma once
#include <cstdint>
namespace deluge::project {
/// Portable DOM record for ModControllableAudio's non-param structural fields (filter/modFX + delay/sidechain/
/// compressor/stutter). SHARED base — nested in DocSound now, reusable by future GlobalEffectable FX doc types.
/// Enums stored as underlying; compressor knob-positions are q31 (int32). midi_knobs (MIDI-learn) is out of scope.
struct DocModControllable {
	uint8_t clipping_amount{0};
	uint8_t lpf_mode{0};
	uint8_t hpf_mode{0};
	uint8_t filter_route{0};
	uint8_t mod_fx_type{0};

	struct Delay {
		bool ping_pong{true};
		bool analog{false};
		uint8_t sync_type{0};
		uint8_t sync_level{0};
	} delay;

	struct Sidechain {
		int32_t attack{0};
		int32_t release{0};
		uint8_t sync_type{0};
		uint8_t sync_level{0};
	} sidechain;

	struct Compressor { ///< q31 knob positions (via getters); NOT in the param-DOM tier
		int32_t attack{0};
		int32_t release{0};
		int32_t threshold{0};
		int32_t ratio{0};
		int32_t sidechain_hpf{0};
		int32_t blend{0};
	} compressor;

	struct Stutter {
		bool quantized{true};
		bool reversed{false};
		bool ping_pong{false};
	} stutter;
};
} // namespace deluge::project
```
- [ ] **Step 2: nest in `DocSound`.** In `doc_sound.h`: `#include "deluge-project/doc_mod_controllable.h"`, add member `DocModControllable mod_controllable;` on `DocSound`.
- [ ] **Step 3: Build + full gate** (block in Task 2). UNCHANGED (unused). Watch for any golden shift (would signal a Sound layout perturbation — but DocSound isn't in Sound's layout, so none expected).
- [ ] **Step 4: Commit.** `git commit -m "fx: DocModControllable (shared) nested in DocSound (owner-5 slice F data structures, unused)"`.

---

### Task 2: `ModControllableAudio::syncDocModControllable` + call from `syncDocSound`

**Files:** `src/deluge/model/mod_controllable/mod_controllable_audio.h`/`.cpp`, `src/deluge/processing/sound/sound.cpp`.

- [ ] **Step 1: `syncDocModControllable`.** In `mod_controllable_audio.cpp` (declare in the header; include `deluge-project/doc_mod_controllable.h`):
```cpp
void ModControllableAudio::syncDocModControllable(deluge::project::DocModControllable& d) {
	d.clipping_amount = clippingAmount;
	d.lpf_mode = static_cast<uint8_t>(lpfMode);
	d.hpf_mode = static_cast<uint8_t>(hpfMode);
	d.filter_route = static_cast<uint8_t>(filterRoute);
	d.mod_fx_type = static_cast<uint8_t>(modFXType_);
	d.delay.ping_pong = delay.pingPong;
	d.delay.analog = delay.analog;
	d.delay.sync_type = static_cast<uint8_t>(delay.syncType);
	d.delay.sync_level = static_cast<uint8_t>(delay.syncLevel);
	d.sidechain.attack = sidechain.attack;
	d.sidechain.release = sidechain.release;
	d.sidechain.sync_type = static_cast<uint8_t>(sidechain.syncType);
	d.sidechain.sync_level = static_cast<uint8_t>(sidechain.syncLevel);
	d.compressor.attack = compressor.getAttack();
	d.compressor.release = compressor.getRelease();
	d.compressor.threshold = compressor.getThreshold();
	d.compressor.ratio = compressor.getRatio();
	d.compressor.sidechain_hpf = compressor.getSidechain();
	d.compressor.blend = compressor.getBlend();
	d.stutter.quantized = stutterConfig.quantized;
	d.stutter.reversed = stutterConfig.reversed;
	d.stutter.ping_pong = stutterConfig.pingPong;
}
```
(VERIFY every engine field name/getter against the headers. The compressor getters may be non-const — `syncDocModControllable` is non-const, fine. If any getter needs a non-const object, ensure the method isn't const.)
- [ ] **Step 2: call from `Sound::syncDocSound`.** In `Sound::syncDocSound()` (sound.cpp), after the existing lines, add `syncDocModControllable(docSound_->mod_controllable);` (Sound IS-A ModControllableAudio, so the unqualified call resolves; `docSound_` is non-null here — it's guarded at the top of syncDocSound).
- [ ] **Step 3: Build + full gate.**
```
export DELUGE_SONG_CORPUS="/home/kate/Deluge Backup/SONGS"
./dbt build Debug 2>&1 | tail -3
cmake --build build-sim --target song_roundtrip edit_undo_redo param_record_diff json_roundtrip edit_test param_projection_test 2>&1 | tail -3
python3 tests/golden/songs_run.py --binary build-sim/song_roundtrip 2>&1 | tail -1
python3 tests/golden/edit_undo_redo_run.py --binary build-sim/edit_undo_redo 2>&1 | tail -1
python3 tests/golden/param_record_run.py --binary build-sim/param_record_diff 2>&1 | tail -1
python3 tests/golden/json_roundtrip_run.py --binary build-sim/json_roundtrip 2>&1 | tail -1
./build-sim/edit_test 2>&1 | tail -1
./build-sim/param_projection_test 2>&1 | tail -1
./dbt test 2>&1 | grep -iE "tests passed|failed" | tail -1
```
Expected all UNCHANGED. A REGRESS = a sync read a wrong field/getter or had a side effect (getters must be pure reads — the compressor GETTERS just return knob positions; do NOT accidentally call setters). If a compressor getter isn't available/const-correct, adjust the call, not the engine's DSP.
- [ ] **Step 4: Commit.** `git commit -m "fx: ModControllableAudio::syncDocModControllable + call from Sound::syncDocSound rebuild-at-save (owner-5 slice F; shadow)"`.

---

## Self-Review

**Spec coverage.** DocModControllable = the full non-param ModControllableAudio surface (filter/modFX top-level + delay/sidechain/compressor/stutter nested), SHARED (standalone struct, nested in DocSound), reusable by future GlobalEffectable docs. midi_knobs deferred. Compressor knob-positions included (they're NOT in the param tier — recon confirmed).

**Placeholder scan.** Concrete struct + sync; "verify names/getters" is a real executor step.

**Type consistency.** Enums → u8 underlying; delay/stutter bools; sidechain attack/release i32; compressor knob-positions i32 (q31 via getters). RAW engine values stored (no file-value sync-level conversion — that's save-flip's job). Nested sub-structs, `<cstdint>`-only.

**Risk.** (1) The compressor fields are behind GETTERS (private raw) — use `getAttack()`/…/`getBlend()`, never touch the private members. (2) `getSidechain()` = the compressor's OWN HPF knob (file attr `compHPF`), distinct from the `sidechain` member — don't conflate. (3) `delay`/`sidechain` syncLevel stored raw (internal), not file-converted — correct for a shadow. (4) Nothing reads the shadow → drift silent this slice.
