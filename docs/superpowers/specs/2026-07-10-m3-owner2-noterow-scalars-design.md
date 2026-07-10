# M3 owner-2 (notes) — Slice 1: DocNoteRow scalar metadata — Design

**Status:** design, approved for planning
**Branch:** `deluge-project-param`
**Date:** 2026-07-10
**Parent:** M3 migration (`docs/dev/project_model_migration_plan.md`). Owner-1 (clip structure) is complete — `DocClip` is the sole authoritative store for loop/colour/section/launch, keyed by `Clip::id`, bound in the Clip ctor. Owner-2 is Notes/NoteRows, the first inversion of an owner **without a durable id and with a mutable key** (melodic `y`). This slice lands the note-row DOM record + its binding infrastructure on the cheap scalar fields, additively (dual-write, no read-flip).

## The identity decision (Model B, chosen)

A note-row has no durable id, and melodic rows are identified by `y` which **mutates in place** on transpose/scale changes — so an address-keyed store (`NoteRowAddress → DocNoteRow`) would re-key on every transpose. Instead: **`DocClip` owns an ordered collection of `DocNoteRow`**, identity = position-within-clip (faithful — note-rows are an ordered per-clip collection the file already serializes in order). `y` becomes a mutable **field**, so transpose just writes a field — no re-key. Kit-row → drum linkage rides as a `drum_id` field (joins with the note-vector slice). The param tier is untouched (kit-row params stay keyed by `drum_id` in `backed_params`).

## Scope

**In (Slice 1):** the `DocNoteRow` value type (5 scalar fields); `DocClip` growing a stable-address ordered `note_rows` collection; the per-note-row binding lifecycle (create/remove/load hooks maintaining lockstep order); **dual-write** of the scalars from the note-row creation/load path and the funneled note-row Edits. **Out:** the read-flip (Slice 2a — trivial once binding is proven), the note **vector** (Slice 2b — the hot data), `drum_id`/kit linkage (rides the vector slice), save/load flip (rides read-flip).

## Design

### 1. `DocNoteRow` value type

`src/deluge-project/doc_note_row.h` (new), value-typed, engine-light (enum stored as underlying):
```cpp
struct DocNoteRow {
	int16_t y{0};
	int32_t loop_length_if_independent{0}; ///< 0 = obeys parent clip
	int8_t colour_offset{0};
	uint8_t sequence_direction{0}; ///< SequenceDirection underlying (0 = OBEY_PARENT)
	bool muted{false};
};
```
(Defaults must match the engine `NoteRow` ctor defaults — verified during implementation against `NoteRow`'s field inits.)

### 2. `DocClip` grows an ordered, stable-address note-row collection

`DocClip` (`doc_clip.h`) gains `note_rows`. **Stable element addresses are required** — the engine `NoteRow` holds a `DocNoteRow*`, so the container must not move elements on insert/remove. A plain `std::vector<DocNoteRow>` is wrong (reallocation dangles the pointers). Use a node-stable ordered container: **`std::list<DocNoteRow>`** (stable addresses, preserves order, O(n) positional insert — fine for ≤128 rows) — OR a pool/deque if the codebase prefers; the invariant is *stable addresses + insertion-order*. (Implementation picks per convention; the spec's contract is "stable-address ordered list of DocNoteRow, one per engine note-row, in the same order.")

### 3. The binding lifecycle (the real work)

Engine `NoteRow` gains `DocNoteRow* docNoteRow_{nullptr}` + `bindDocNoteRow(DocNoteRow*)`. The invariant: **`InstrumentClip`'s engine `noteRows[i]` ↔ its `DocClip.note_rows`'s i-th element, same order, 1:1.** Maintained at every lifecycle point:
- **Create** (`insertNoteRowAtIndex`/`insertNoteRowAtY` in `note_row_vector` / `InstrumentClip`): after the engine row is inserted at index `i`, insert a `DocNoteRow` at the matching position in the owning clip's `DocClip.note_rows` and `bindDocNoteRow` it. This needs the owning clip's `DocClip` — thread it (or resolve via the clip that owns the vector).
- **Remove**: erase the matching `DocNoteRow` from `DocClip.note_rows`.
- **Load** (`Song::readClipsFromFile` → `NoteRow::readFromTree` per row): the note-row is created (→ DocNoteRow created + bound) before its fields are read; the readFromTree scalar reads dual-write (below).
- **Clone / clip copy**: clone the note-row's `DocNoteRow` alongside.

Because the engine `noteRows` vector and `DocClip.note_rows` are maintained in lockstep, the engine row's own `docNoteRow_` pointer is all it needs — no index threading at read time.

### 4. Dual-write the scalars (no read-flip yet)

The engine `NoteRow` keeps its `y`/`muted`/`colourOffset`/`loopLengthIfIndependent`/`sequenceDirectionMode` members **authoritative**. Every writer of those fields also writes `docNoteRow_` (guarded on non-null):
- `NoteRow::readFromTree` scalar reads (`y`/`muted`/`colourOffset`/`length`/`sequenceDirection`) → also set the `DocNoteRow` field.
- The funneled note-row Edits that mutate these (e.g. `SetNoteRowLength`; mute/colour gestures as they funnel) → dual-write.
- Note-row creation defaults → seed the `DocNoteRow` to match.
Nothing reads `DocNoteRow` yet, so this is **purely additive and golden-neutral** — it lands the binding + shadow, de-risked. Slice 2a flips the accessors to read `docNoteRow_->field` (trivial once this is proven).

## Testing — the gate

Additive/golden-neutral: `song_roundtrip OK=16 REGRESS=0`, `edit_undo_redo OK=40`, `param_record OK=3`, `json_roundtrip OK=38`, `./dbt test` 15/15, firmware links. Corpus: `export DELUGE_SONG_CORPUS="/home/kate/Deluge Backup/SONGS"`. A REGRESS here means the shadow write perturbed engine state (it must not — engine fields stay authoritative) or a lifecycle hook corrupted the engine `noteRows`.

## Risks

1. **Lockstep invariant** — the hard part. Every engine note-row create/remove/reorder/clone must maintain `DocClip.note_rows` 1:1 in order. A missed site → the shadow drifts (harmless in Slice 1 since nothing reads it, but must be right before the read-flip). Mitigation: funnel all note-row lifecycle through the `note_row_vector` insert/erase primitives + a lockstep assert (ALPHA) that `ssize(noteRows) == size(DocClip.note_rows)`.
2. **Stable-address container** — a `vector<DocNoteRow>` would dangle the engine pointers; use a node-stable container. Verified by the ALPHA lockstep assert + goldens.
3. **Owning-DocClip access at note-row create** — the insert primitive needs the owning clip's `DocClip`. Thread the clip (the vector already lives inside an `InstrumentClip`; its `docClip_` is reachable).
4. **Reaching the Model from `NoteRow`** — sidestepped: `NoteRow` never resolves; it holds a bound `docNoteRow_` set at create by the clip that has the `DocClip`. (This is why binding-at-create, not resolve-at-use.)
