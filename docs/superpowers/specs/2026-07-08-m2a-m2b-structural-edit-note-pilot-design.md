# M2a + M2b — Edit Catalog + Note-Add/Remove Structural Pilot — Design

**Status:** approved design, pre-plan.
**Predecessors:** M1 complete (Edit substrate + param-value + delete/paste Edits); S5-b id-addressing done. Migration plan `docs/dev/project_model_migration_plan.md` (M2); north-star `docs/dev/project_model_north_star.md`.

## Goal

Open M2 (the structural write funnel) with its first increment: **M2a** an Edit catalog that inventories the whole persistent-mutation surface, and **M2b** the note-add/remove pilot that cracks **structural id-addressing** — naming a note-row inside a clip, the pattern the rest of M2 follows. As in M2 generally, the Edits are id-addressed but still execute against engine objects (ownership inversion is M3).

## M2 Phase Ladder (this spec = M2a + M2b)

Owner-aligned: each phase funnels one owner-group's structural writes, directly enabling that owner's M3 flip ("an owner's M3 may start as soon as its writes funnel").

| Phase | Scope | M3 owner |
|---|---|---|
| **M2a** | **Edit catalog** — the 28 `ActionType`s → Edit → addressed target → engine sites → status. Defines M2-done; drives every phase. | — |
| **M2b** | **Note add/remove pilot (melodic)** — `NoteRowAddress` + `AddNote`/`RemoveNote`, the primary pad-tap sites. | owner 2 (partial) |
| **M2c** | **Notes complete** — kit note-rows (drum-id row-key via S5-b drum ids) + remaining note/note-row types (`NOTE_TAIL_EXTEND`, `NOTE_NUDGE`, `NOTE_REPEAT_EDIT`, `NOTES_PASTE`, `NOTEROW_ROTATE`, `NOTEROW_LENGTH_EDIT`, `NOTEROW_HORIZONTAL_SHIFT`, euclidean). | owner 2 |
| **M2d** | **Clip-structure Edits** — create/delete, length, loop points, section, colour, launch-style. | owner 1 |
| **M2e** | **Kit/drum structure Edits** — row membership, per-drum descriptors. | owner 3 |
| **M2f** | **Sample/audio-clip descriptor Edits** — file refs, start/end/pitch metadata. | owner 4 |
| **M2g** | **Sound/instrument structure Edits** — instrument swap; largest, guided by `docs/dev/synth_state_decoupling.md`. | owner 5 |
| **M2h** | **Action view-state shed** — move `xScroll`/zoom/mode view-state to a frontend side-record keyed by the Action. Mechanical, **independent track** (any time). | — |
| **exit** | Catalog fully checked; scattered setters retired or made private to Edit impls (stragglers = compile errors). | enables M3 completion |

Each of M2c–M2h gets its own spec→plan→execute, with M2a's catalog as the running checklist. M2a updates the plan doc's M2 section to this ladder.

## Key Facts (verified)

- **Note-edit undo** is the structural twin of param undo: `Action::recordNoteArrayChangeIfNotAlreadySnapshotted(InstrumentClip* clip, int32_t noteRowId, NoteVector* notes, bool stealData)` snapshots the note-row's whole `NoteVector`, deduped by `(clip, noteRowId)`. Note edits use `ActionType::NOTE_EDIT` (coalescing `ActionAddition::ALLOWED`).
- **`noteRowId` semantics** (`InstrumentClip::getNoteRowId`): melodic (synth/MIDI) → `noteRow->y` (stable pitch id); kit → `noteRowIndex` (transient). **Melodic pilot uses `y`.**
- **Engine mutations**: add = `NoteRow::attemptNoteAdd(pos, length, velocity, probability, iterance, …)`; remove = `NoteRow::deleteNoteByIndex(index, action, noteRowId, clip)`. `Note : Positionable` (has `pos`).
- **Clip identity** (for the address): `Clip::serialId()` (optional, stable-when-saved) + `sessionId()` (transient, minted in S5-b's `readFromFile` relocation). Loaded synth/MIDI clips carry a session id.

## Key Decisions (resolved in brainstorming)

1. **Scope = M2a catalog + M2b melodic note pilot.** Kit rows and the other note/structural types are explicit later phases (ladder above), not a generic "deferred."
2. **Melodic-only pilot.** `y` is already a clean stable id; the pilot cracks the address + Edit recipe without the kit index→drum-id resolution (that's M2c).
3. **NoteRow-level address, note `pos` as payload.** The addressed target is the note-row (matching the coarse per-note-row-array Consequence); the note position is operation payload — parallel to `ParamAddress` (the lane) + value/pos payload.
4. **Two Edits, `AddNote` and `RemoveNote`** — distinct verbs (delete/paste precedent), each a focused `apply`.
5. **Catalog is a companion doc** `docs/dev/edit_catalog.md` (a living markdown table), not a header comment.
6. **`edit_undo_redo` is the pilot's primary test vehicle** — the song-less `edit_test` can't host a real clip/note-row; the loaded-song harness resolves a real `NoteRowAddress` and drives real undo/redo.

## M2a — Edit Catalog

`docs/dev/edit_catalog.md`: a table, one row per `ActionType` (and per structural mutation that shares one), columns:

| ActionType(s) | Edit (planned/landed) | Addressed target | Engine call-sites subsumed | Phase | Status |
|---|---|---|---|---|---|

Seeded from the M1 Edits (SetParamValue, Delete/PasteParamAutomation — mark their ActionTypes done) and the M2 note types. It is the authoritative "what's left" list; each later phase checks off rows. M2a also rewrites the migration plan's M2 section to the ladder above.

## M2b — The Note Pilot

### `NoteRowAddress` (new, `src/deluge-project/note_address.h`)

The durable structural id of a melodic note-row, parallel to `ParamAddress`:
```cpp
namespace deluge::project {
/// Names one melodic note-row: which clip, which pitch. (Kit rows — a drum-id row-key — added in M2c.)
struct NoteRowAddress {
	uint32_t clip_id{0};       ///< Clip::serialId() when saved, else sessionId().
	bool clip_is_session{false};
	int32_t y{0};              ///< noteRow->y — the stable melodic row id.
};
}
```
Plus `std::optional<NoteRowAddress> deluge::edits::noteRowAddressFor(const ModelStackWithNoteRow&)` — the structural analogue of `addressFor`: classify the clip identity (serial vs session) and read `y`; `nullopt` for a kit row (deferred) or an unaddressable clip. *(Resolution to a live model target is future — M3; for the pilot the address is carried, not yet resolved, mirroring M1b.)*

### `AddNote` / `RemoveNote` Edits (`src/deluge/model/edits/`)

```cpp
class AddNote final : public Edit {          // reifies "create a note in this melodic row"
	// { std::optional<NoteRowAddress> address; int32_t pos, length, velocity, probability; Iterance iterance;
	//   ModelStackWithNoteRow* engineStack; }
};
class RemoveNote final : public Edit {        // reifies "delete the note at pos in this melodic row"
	// { std::optional<NoteRowAddress> address; int32_t pos; ModelStackWithNoteRow* engineStack; }
};
```
`apply` funnels the proven pattern (as delete/paste did):
```
Action* action = getNewAction(ActionType::NOTE_EDIT, ActionAddition::ALLOWED);
if (action) action->recordNoteArrayChangeIfNotAlreadySnapshotted(clip, noteRowId, &noteRow->notes, /*steal=*/false);
// then the engine mutation:
//   AddNote:    noteRow->attemptNoteAdd(pos, length, velocity, probability, iterance, engineStack)
//   RemoveNote: noteRow->deleteNoteByIndex(indexOfNoteAtPos(pos), action, noteRowId, clip)
```
`clip`, `noteRow`, `noteRowId` come from `engineStack` (the `ModelStackWithNoteRow`), verbatim as the call sites do today. Funnel-phase fields (`engineStack`) documented removable at M3/M4, like `SetParamValue::engineStack_`.

### Pilot call-sites

Funnel the **single highest-traffic pad-tap add path and remove path** in `instrument_clip_view.cpp` (the `getNewAction(NOTE_EDIT)` + `recordNoteArrayChange` + `attemptNoteAdd`/`deleteNoteByIndex` sequence a pad-tap on an empty/occupied melodic square triggers). Not all ~10 `NOTE_EDIT` sites — those are M2c. Plan pins the exact lines.

## Testing / Verification

- **Primary net — `edit_undo_redo` extended:** find the first melodic `InstrumentClip` with a note-row in the loaded song; build its `ModelStackWithNoteRow`; assert `noteRowAddressFor` returns a real address; then `AddNote` → assert the note is present → `revert(BEFORE)` (undo) → assert absent → `revert(AFTER)` (redo) → assert present again; symmetrically a `RemoveNote` on a seeded note. Same crash-safety discipline (recording=OFF, no `deleteAllLogs`). Defensive skip if no melodic clip/row.
- **Golden:** `songs_run.py` OK=15 (notes serialize — untouched), `param_record` OK=3, existing `edit_test` OK, against the corpus.
- **Firmware:** `./dbt build Debug` clean; targeted ARM at the milestone.
- **On-device smoke** (milestone): add a note by pad-tap + undo/redo; remove a note + undo/redo — melodic clip.

## Risks

| Risk | Mitigation |
|---|---|
| `RemoveNote` must locate the note index from `pos` | Resolve index at `apply` time via the note-row's lookup (the same lookup the call site uses); carry `pos`, not a transient index. |
| Coalescing (`ALLOWED`) semantics of consecutive note edits altered | Edit calls `getNewAction(NOTE_EDIT, ALLOWED)` verbatim; the `recordNoteArrayChange` dedupe by `(clip, noteRowId)` is unchanged. |
| `noteRowAddressFor` clip-id classification wrong (serial vs session) | Mirror `addressFor`'s established serial-else-session logic; the `edit_undo_redo` case asserts a real address resolves. |
| Note payload (probability/iterance/length) dropped vs the call site | `AddNote` carries every arg `attemptNoteAdd` takes; passed verbatim. |
| `edit_test` can't host the pilot | Use `edit_undo_redo` (loaded song) as the net; `edit_test` unchanged. |

## Non-Goals

Kit note-rows and all non-pilot note/structural mutations (the explicit M2c–M2g phases); M2h Action view-state shed; resolving `NoteRowAddress` to a model target (M3); the engine kernels (`attemptNoteAdd`/`deleteNoteByIndex`) themselves.
