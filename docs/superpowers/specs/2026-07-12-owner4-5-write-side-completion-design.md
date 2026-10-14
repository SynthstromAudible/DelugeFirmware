# Owner-4 / Owner-5 write-side completion — `sampleHolder` + `ranges`

**Status:** design (approved shape: approach ①, two-phase)
**Branch:** `deluge-project-param`
**Date:** 2026-07-12

## 1. Goal

Finish M3 save-authority. Two persistent field-groups are still engine-authoritative at
save — the last `syncDoc*` residues in the whole migration:

- **owner-4:** `AudioClip::sampleHolder` (+ the `overdubsShouldCloneOutput` Clip-base bool).
- **owner-5:** `Source::ranges` (the `MultiRangeArray` of sample/wavetable key-splits, each
  carrying a `sampleHolder`).

When both flip, **no `syncDoc*` engine-walk remains in any save path** — every persistent
field of every owner serializes directly from the `deluge::project` model.

### Naming — this finishes M3, it is *not* the plan's M4

The migration plan's M4 is the **read-side** *snapshot boundary + event feed* (seal the audio
thread behind a derived snapshot; events drive incremental patches). That remains deferred as
separate later work. This milestone is the **write-side** funnel + undo rework that the two
deferred groups were waiting on. It is scoped as *finishing M3*, not as M4.

The two groups were both filed under "M4-gated" in the owner notes, but the entanglement map
(below) shows they are gated for **different** reasons, only one of which is real undo work.

## 2. Scope

**In scope**
- Move the forward mutation of `sampleHolder` and `ranges` fields through model-writing
  setters (record → derive engine cache).
- Rework the two AudioClip undo Consequences to revert through the model instead of poking
  the engine field / a raw pointer.
- Flip both save paths to read the model; delete `syncDocAudioClip` and `syncDocSound`.

**Out of scope (explicitly deferred)**
- The read-side snapshot boundary + event feed (the plan's real M4).
- Edit/Consequence convergence (M6) — `Consequence` stays the undo currency.
- **Adding undo to `ranges` edits.** They have no undo today; they will still have none after
  the funnel (decided — behavior-neutral). Making them undoable is separate later work.
- AudioClip loop points — `DocAudioClip` has no loop fields and the AudioClip save path does
  not persist them; unchanged.

## 3. Current state (what already exists)

### owner-4 — the record and load-bridge already exist; only write + undo are missing
- `DocAudioClip` (`src/deluge-project/doc_audio_clip.h:32`) **already holds** `file_path`,
  `start_pos`, `end_pos`, `transpose`, `cents`, `overdubs_should_clone` (plus the already-
  inverted `interpolation_mode` / `pitch_and_speed_independent` / `reversed` / `attack` /
  `voice_priority`).
- `AudioClip::deriveEngineFromDoc()` (`src/deluge/model/clip/audio_clip.cpp:1151`) already
  seeds the `sampleHolder` cache from `DocAudioClip` — at construction (`:66`) and after the
  DOM-first load parse (`:1323`). **This is the load-bridge; reuse it.**
- `AudioClip::syncDocAudioClip()` (`audio_clip.cpp:1129`) is the residual save-time shadow —
  it rebuilds `file_path`/`start_pos`/`end_pos`/`transpose`/`cents`/`overdubs_should_clone`
  from the engine. Nothing reads these back yet; that is exactly what this milestone flips.
- owner-4 already established the setter idiom (`setInterpolationMode`/`setReversed`/`setAttack`
  /`setVoicePriority` write record + cache). The sampleHolder setters follow the same shape.

### owner-5 — record exists, DocSource scalars + dx_patch already flipped
- `DocSource.ranges` is a `std::vector<DocRange>` (`doc_source.h:67`); `DocRange` =
  `top_note` + `file_path` + zone (`start_pos`/`end_pos`/`loop_start_pos`/`loop_end_pos`/
  `transpose`/`cents`).
- `Source` already carries `docSource_` (bound by `Sound::bindDocSound`, reseeded at
  `doneReadingFromFile`) and the scalar + dx_patch setters/reseeds. `syncDocSound` is now
  **purely** the `ranges` rebuild (`sound.cpp:4034-4074`) — the single remaining shadow.

### The entanglement map (why these were gated)
- **Only one genuine raw-pointer-into-engine exists in the whole undo system:**
  `ConsequenceClipLength::pointerToMarkerValue` — a `uint64_t*` aimed at
  `&sampleHolder.endPos` / `.startPos`, injected from `audio_clip_view.cpp` (`:649-651`,
  `:685-687`, `:709-711`) after the `SetClipLength` edit runs. `revert()`
  (`consequence_clip_length.cpp:31-47`) swaps through the pointer, then reverts clip length.
- **`ConsequenceAudioClipSetSample`** (`consequence_audio_clip_set_sample.*`) is **not** a raw
  pointer — it stores `filePathToRevertTo` (value), `endPosToRevertTo` (value) + an owning
  `AudioClip*`, and `revert()` writes those into `clip->sampleHolder.{filePath,endPos}` and
  calls `sampleHolder.loadFile(...)`. Created from `Action::recordAudioClipSampleChange`
  (`action.cpp:336`), called at `audio_clip.cpp:229` (record-finish) and `:1420` (clear).
- **`ranges` has no Consequences at all.** `sample_marker_editor.cpp` (zone edits, `:167`
  `.startPos=`, `:175-199` loop/end) and `multi_range.cpp:206` (add/remove) mutate
  `range->sampleHolder` / the `MultiRangeArray` directly with **zero** Action/Consequence
  integration. So `ranges` is blocked purely by *unfunnelled direct mutation*, not undo.
- `Action::revert` (`action.cpp:119-170`) walks the intrusive consequence list calling each
  `revert(time, modelStack)`, reversing the list order so undo/redo is symmetric; each
  consequence self-inverts by swapping its stored before/after into the live field on every
  `revert()`. That swap-into-the-live-field assumption is precisely what breaks once the field
  is a derived cache — hence the rework.

## 4. Approach (decided: ① value-ify Consequences in place)

Keep `Consequence` as the undo currency (north-star staged convergence — full Edit/Consequence
convergence is M6, not now). Make the two AudioClip Consequences **model-aware**: store the
old state by value (they nearly do already) and `revert()` by calling an `AudioClip` model
setter that writes `DocAudioClip` and re-derives the cache — never touching the engine field
directly and never holding a raw pointer into it.

This is the minimal change that (a) removes the lone raw `uint64_t*`, and (b) makes undo
restore the model, so the field can be a derived cache. Rejected alternative ② (reify the
mutations as forward Edits with value Consequences) front-loads M6 convergence for no
save-authority gain.

## 5. Phase A — owner-4 `sampleHolder` + `overdubsShouldCloneOutput`

### A1. AudioClip model setters
Add setters mirroring the existing owner-4 idiom (write `DocAudioClip` + update the
`sampleHolder` cache; reuse `deriveEngineFromDoc` / a targeted derive as appropriate):
- `setSampleStartPos(uint64_t)` / `setSampleEndPos(uint64_t)` — write record + cache; these
  are the marker-drag targets.
- `setSampleFilePathAndEndPos(string, uint64_t)` (or a small `setSample*` pair) + the
  reload — the sample-set / clear path (matches what `ConsequenceAudioClipSetSample::revert`
  must do: set file + endPos, `unassignVoiceSample`, `setAudioFile(nullptr)`, `loadFile`).
- `setOverdubsShouldCloneOutput(bool)` (Clip-base; write record + native).
- Transpose/cents already round-trip through `DocAudioClip` via existing paths; confirm their
  forward mutations funnel (audio-clip transpose menu already went through owner-4 work — verify).

Design the setters so the **Consequence revert** and the **forward UI mutation** share the same
setter — one funnel, do-site and undo-site converge on it.

### A2. Funnel the forward mutations
- `audio_clip_view.cpp` marker drag — the 3 sites currently do `*valueToChange = newPos` then
  patch the raw pointer. Replace with the start/end setters; the `SetClipLength` edit stays as
  the length funnel.
- `audio_clip.cpp:229` (linear-record finish) and `:1420` (clear) — route the
  `sampleHolder.filePath`/`endPos` writes through the sample setter.

### A3. Rework the two Consequences (approach ①)
- **`ConsequenceClipLength`:** delete `uint64_t* pointerToMarkerValue`. Keep the value
  (`markerValueToRevertTo`) and add a small discriminator (which marker: start vs end, or
  none). `revert()` swaps the stored value via `AudioClip::setSampleStartPos/EndPos` (model
  setter) instead of `*pointerToMarkerValue`. Remove the raw-pointer injection in
  `audio_clip_view.cpp`; the consequence captures the old marker value at record time instead.
- **`ConsequenceAudioClipSetSample`:** `revert()` writes the reverted file/endPos through the
  AudioClip sample setter (record → derive → reload) rather than assigning
  `clip->sampleHolder.filePath/endPos` directly. Its stored values are already value-based;
  only the write target changes.

Undo/redo symmetry (the self-swap on each `revert()`) is preserved — the setter is idempotent
w.r.t. record+cache, and the consequence still stashes the prior value back for re-inversion.

### A4. Save flip + delete the shadow
- `writeDataToFile` reads `DocAudioClip` for file/start/end/transpose/cents/overdubs (some
  already do from owner-4; complete the rest).
- **Delete `syncDocAudioClip`** and its call site. The record is authoritative: written by the
  setters/consequences, derived to cache at load, read at save.

## 6. Phase B — owner-5 `ranges`

No undo exists; keep it that way (decided). Funnel the direct mutations through new `Source`
range setters that write `DocSource.ranges` + the `sampleHolder` cache; save reads the record.

### B1. Source range setters (write `docSource_->ranges` + cache)
The collection is variable-length, so the record mirror must track add/remove **and** per-range
zone edits. Two sub-shapes:
- **Per-range zone edit** — `setRangeZone(rangeIndex, start/end/loop/transpose/cents)` (or
  field-granular setters) writing the matching `DocRange` + the engine `range->sampleHolder`.
- **Collection mutation** (add / remove / sample-file-set / reorder-by-top-note) — after the
  `MultiRangeArray` changes, **reseed the whole `DocSource.ranges` list** from the engine
  ranges (modKnobs/dx_patch-style whole-block reseed). Rationale: range add/remove reindexes
  and reorders (ordered by `top_note`); a whole-list reseed is simpler and less error-prone
  than incremental index bookkeeping, and there is no undo to keep in sync.

### B2. Funnel sites
- `sample_marker_editor.cpp` zone edits (`:167`, `:175-199`) — through the per-range setter.
- `multi_range.cpp:206` add + the delete path — through the collection reseed.
- Range sample-file set (file selector → `range->sampleHolder.loadFile`) — reseed after set.
- `Source::setReversed` already fixes up range holder endPos on reverse — confirm it also
  reseeds the affected ranges (it mutates `holder->endPos`).
- Slicer / sample_browser paths that build ranges — reseed after (like the DocSource scalars).

### B3. Save flip + delete `syncDocSound`
- `writeSourceToFile`'s SAMPLE and WAVETABLE range emission reads `DocSource.ranges` when
  bound (top_note + file_path + zone), matching the current engine-walk byte-for-byte.
- **Delete the ranges rebuild from `syncDocSound` → delete `syncDocSound` entirely.** owner-5
  save-flip complete.

## 7. Testing / gates

Standing golden gate, bit-identical, x86≡ARM, per slice:
`song_roundtrip OK=16`, `json_roundtrip OK=38`, `edit_undo_redo OK=40`, `param_record OK=3`,
`audio_clip_edit_diff OK=1`; firmware `./dbt build Debug` links. Corpus
`DELUGE_SONG_CORPUS="/home/kate/Deluge Backup/SONGS"`.

- Each field/consequence is dropped from the `syncDoc*` shadow **as it flips**, so a missed
  funnel fails the goldens rather than being masked by the save-time rebuild (the discipline
  used throughout owner-5).
- **`edit_undo_redo` is the key net for Phase A** — it exercises the Action/Consequence revert
  path, so the value-ified `ConsequenceClipLength` / `ConsequenceAudioClipSetSample` are
  covered there, not just at save. `audio_clip_edit_diff` (`Sampling.XML`, 20 audio clips)
  exercises the sampleHolder round-trip.
- **Coverage gap to flag:** the golden corpus's undo coverage for the *marker-drag* path
  specifically should be checked; if no fixture drives an AudioClip marker drag + undo, add a
  targeted case to `edit_undo_redo` (or note it as a hardware ear-check), since Phase A changes
  that exact revert path.

## 8. Risks & open questions

- **`ConsequenceClipLength` dual role.** It reverts *both* clip length *and* a sample marker
  (via the injected pointer today). The rework must preserve that it can carry a marker revert
  *or not* (length-only changes on non-audio clips). The discriminator handles this; verify the
  non-audio (InstrumentClip) length path is unaffected — it never sets the marker pointer.
- **`loadFile` cost on undo.** `ConsequenceAudioClipSetSample::revert` reloads the sample.
  Routing through the setter must keep the same reload semantics (same args to `loadFile`), or
  audio behavior changes. Behavior-preserving requirement: the setter's reload path must be
  byte-for-byte the existing `revert()` sequence.
- **Ranges reseed granularity.** Whole-list reseed on collection change is O(ranges) per edit;
  ranges are tiny (a handful) so this is negligible, but note it (no silent perf cliff).
- **overdubs funnel completeness.** `overdubsShouldCloneOutput` is Clip-base (polymorphic);
  find every write site (menu + any runtime) before flipping its save.

## 9. Exit criteria

- `syncDocAudioClip` and `syncDocSound` **deleted**; no `syncDoc*` engine-walk in any save path.
- Every `sampleHolder` / `ranges` persistent field serializes from the model, gated
  bit-identical (song/json/edit/param/audio_clip) x86≡ARM; firmware links.
- The two AudioClip Consequences hold **no** raw pointer into engine state and revert through
  model setters; `edit_undo_redo` green.
- `grep` for the retired shadow methods returns zero; owner-4 and owner-5 both at the owner-1
  "done" bar (model-authoritative save, engine keeps only derived caches).

## References
- Migration plan: `docs/dev/project_model_migration_plan.md` (M3/M4/M5 ladder)
- North star: `docs/dev/project_model_north_star.md` (write API, staged convergence)
- Owner-4 record + derive: `src/deluge/model/clip/audio_clip.{h,cpp}`, `src/deluge-project/doc_audio_clip.h`
- Owner-5 ranges: `src/deluge/processing/source.{h,cpp}`, `src/deluge-project/doc_source.h`, `Sound::syncDocSound`
- Consequences: `src/deluge/model/consequence/consequence_{clip_length,audio_clip_set_sample}.*`, `src/deluge/model/action/action.cpp`
- Forward-mutation sites: `src/deluge/gui/views/audio_clip_view.cpp`, `src/deluge/gui/ui/sample_marker_editor.cpp`, `src/deluge/gui/menu_item/multi_range.cpp`
