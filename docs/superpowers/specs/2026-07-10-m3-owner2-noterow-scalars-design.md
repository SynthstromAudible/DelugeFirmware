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
(The struct's field defaults are placeholders — a new note-row's `DocNoteRow` is **seeded from the engine `NoteRow`'s actual values at bind time**, not from these. This matters: `NoteRow`'s `colourOffset` is `random(71)` at construction, so the shadow must copy that specific value, not a fixed default.)

### 2. `DocClip` grows a stable-address note-row POOL

`DocClip` (`doc_clip.h`) gains `note_rows` — a **stable-address owning pool** of `DocNoteRow` (NOT an index-aligned vector). The engine `NoteRow` is stored **by value** in an `OrderedPosVector<NoteRow,&y>` (contiguous, ordered by `y`, elements move on insert), and it carries its `DocNoteRow*` as a member — so on reorder the member moves with the note-row and the pairing is preserved **by pointer**. No index-lockstep is needed. The pool only needs **stable element addresses** (a `std::vector<DocNoteRow>` is wrong — reallocation dangles the pointers). Use a node-stable container (`std::list<DocNoteRow>` — stable addresses, O(1) splice; order within the pool is irrelevant). **Note-row order is a function of the data** (`y`), reproduced at save by iterating the engine's y-ordered `noteRows` (or, once model-authoritative, by sorting the pool on `y`) — so the pool carries no order itself.

### 3. The binding lifecycle (the real work)

Engine `NoteRow` gains `DocNoteRow* docNoteRow_{nullptr}` + `bindDocNoteRow(DocNoteRow*)`. `docNoteRow_` is a **non-owning cache**; the pool owns lifetime, and lifetime is managed **only at explicit hooks** (never in `NoteRow`'s copy/move/dtor — the by-value container's shifts would otherwise wrongly create/free pool entries). Hooks, all at the `InstrumentClip` level (which has `docClip_`):
- **Create** (`InstrumentClip`'s note-row insert sites, after `insertNoteRowAtIndex`/`AtY` returns the new row): `emplace` a `DocNoteRow` in `this->docClip_->note_rows` and `bindDocNoteRow` the new engine row to it.
- **Remove** (before `deleteNoteRowAtIndex`): erase the row's `docNoteRow_` from the pool.
- **Load** (`readFromTree` per row): the row is created (→ pool entry + bind) before its fields are read; the scalar reads dual-write.
- **Clone / clip copy** (deep-copy of a `NoteRowVector`): the copied engine rows' `docNoteRow_` would alias the ORIGINAL's pool entries — **wrong**. The clone path must, per copied row, `emplace` a fresh `DocNoteRow` (copied from the source) in the CLONE's `docClip_->note_rows` and rebind.

Because each engine row carries its own `docNoteRow_`, reads/dual-writes need no index threading — just `docNoteRow_->field`.

### 4. Dual-write the scalars (no read-flip yet)

The engine `NoteRow` keeps its `y`/`muted`/`colourOffset`/`loopLengthIfIndependent`/`sequenceDirectionMode` members **authoritative**. Every writer of those fields also writes `docNoteRow_` (guarded on non-null):
- `NoteRow::readFromTree` scalar reads (`y`/`muted`/`colourOffset`/`length`/`sequenceDirection`) → also set the `DocNoteRow` field.
- The funneled note-row Edits that mutate these (e.g. `SetNoteRowLength`; mute/colour gestures as they funnel) → dual-write.
- Note-row creation defaults → seed the `DocNoteRow` to match.
Nothing reads `DocNoteRow` yet, so this is **purely additive and golden-neutral** — it lands the binding + shadow, de-risked. Slice 2a flips the accessors to read `docNoteRow_->field` (trivial once this is proven).

## Testing — the gate

Additive/golden-neutral: `song_roundtrip OK=16 REGRESS=0`, `edit_undo_redo OK=40`, `param_record OK=3`, `json_roundtrip OK=38`, `./dbt test` 15/15, firmware links. Corpus: `export DELUGE_SONG_CORPUS="/home/kate/Deluge Backup/SONGS"`. A REGRESS here means the shadow write perturbed engine state (it must not — engine fields stay authoritative) or a lifecycle hook corrupted the engine `noteRows`.

## Risks

1. **Binding-hook completeness** — the hard part. Every engine note-row **create / remove / clone** must create/erase/rebind its pool entry, and this must live at explicit `InstrumentClip` hooks, NEVER in `NoteRow`'s copy/move/dtor (the by-value container's element shifts on insert/delete would otherwise wrongly touch the pool). A missed site → the shadow drifts. **Golden-neutral in Slice 1 since nothing reads `docNoteRow_`** — a drift bug is silent here and only bites at the read-flip. Mitigation: an ALPHA invariant check that every engine `noteRow.docNoteRow_` is non-null and points into its clip's pool, run after load + after edits; keep the read-flip (Slice 2a) small so drift surfaces immediately under the goldens.
2. **Stable-address pool** — a `vector<DocNoteRow>` would dangle the engine pointers on reallocation; use a node-stable container (`std::list`). No index-lockstep needed (pairing is by the note-row's own pointer, which survives reorder).
3. **Clone rebind** — deep-copying a `NoteRowVector` (clip clone) copies each `NoteRow` *including its `docNoteRow_`*, aliasing the source's pool entries. The clone path must emplace fresh pool entries in the clone's `DocClip` and rebind. Easy to miss → the highest-value site to get right.
4. **`colourOffset` = `random(71)`** — the shadow seeds from the engine's actual value at bind, not a fixed default (§1).
