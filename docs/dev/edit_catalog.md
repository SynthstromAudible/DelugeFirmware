# Edit Catalog

The authoritative "what's left" checklist for the M2 structural write funnel (migration plan
`project_model_migration_plan.md`). Every persistent-state mutation becomes a `deluge::edits::Edit`;
**M2 is done when every row below is a landed Edit** (or explicitly out of scope). Each M2 phase
(M2b–M2g) checks off its rows. `RECORD`/`ARRANGEMENT_RECORD` are recorded-input, not UI Edits —
they stay on the reconcile path (formalised at M4), never reified as Edits.

One row per `ActionType` (`src/deluge/model/action/action.h`). A single ActionType may map to
several Edits (e.g. `NOTE_EDIT` → add / remove / nudge / …); those land across phases.

| ActionType | Edit(s) | Addressed target | Phase | Status |
|---|---|---|---|---|
| `PARAM_UNAUTOMATED_VALUE_CHANGE` | `SetParamValue` | param lane (`ParamAddress`) | M1b/M1c | **landed** |
| `AUTOMATION_DELETE` | `DeleteParamAutomation` | param lane | coda | **landed** |
| `AUTOMATION_PASTE` | `PasteParamAutomation` | param lane | coda | **landed** |
| `NOTE_EDIT` | `AddNote` / `RemoveNote` (melodic) | note-row (`NoteRowAddress`) | **M2b** | **landed (melodic add/remove)** — kit rows ADDRESSED (drum-id); other note-row edits pending (M2c) |
| `NOTE_TAIL_EXTEND` | (none) | note-row | — | **dead ActionType** — never created via getNewAction (only in the enum + name table, like `NOTEROW_ROTATE`). Note-tail changes are recorded under note-length paths. No Edit. |
| `NOTE_NUDGE` | `NudgeNote` | note-row | M2c | **landed.** Per-pad Edit: nudges notes across all screens + drags automation/MPE at note-row AND clip level under one shared Action; held pads coalesce via `getNewAction(NOTE_NUDGE, ALLOWED)`. The Edit builds a fresh clip stack (vs. the original's parent-stack reuse), so the caller's `ModelStackWithNoteRow` survives for the note-row-editor UI. View keeps the pad loop, collision detection, `newPos`, UI, display. edit_undo_redo-verified (13 songs). |
| `NOTE_REPEAT_EDIT` | `RepeatNoteEdit` | note-row | M2c | **landed** (secret-undo + new-action paths; edit_undo_redo-verified, 9 top-level songs) |
| `NOTES_PASTE` / `PATTERN_PASTE` | `PasteNotes` | **clip** (multi-row) | **M2d** | deferred to clip-structure — `paste()` is a whole-clip op (lengthen/shorten/clearArea/zoomToMax across all rows), not a single note-row edit |
| `EUCLIDEAN_NUM_EVENTS_EDIT` | `SetEuclideanEvents` | note-row | M2c | **landed** (partial-undo + rebuild paths; edit_undo_redo-verified, 40 songs) |
| `NOTEROW_ROTATE` | (via `ShiftNoteRow`) | note-row | M2c | **covered** — `NOTEROW_ROTATE` is a dead ActionType (never created via getNewAction); the rotate gesture funnels through `ShiftNoteRow` (`rotateNoteRowHorizontally` → `NOTEROW_HORIZONTAL_SHIFT`). No separate Edit. |
| `NOTEROW_LENGTH_EDIT` | `SetNoteRowLength` | note-row | M2c | **landed (new-action branch)** — prevAction/secret-undo branch pending; edit_undo_redo-verified (apply+undo+redo, 9 songs) via the setCurrentClip harness coverage |
| `NOTEROW_HORIZONTAL_SHIFT` | `ShiftNoteRow` | note-row | M2c | **landed** (mutation + coalescing undo; edit_undo_redo-verified, 9 songs) |
| `CLIP_LENGTH_INCREASE` / `CLIP_LENGTH_DECREASE` | `SetClipLength` | clip | M2d | pending |
| `CLIP_CLEAR` | `ClearClip` / `ClearNoteRow` | clip / note-row | M2d | **landed** — `ClearClip` (virtual `clip->clear`, funnels instrument + audio: instrument_clip_minder, automation_view audio-clip, audio_clip_view; edit_undo_redo clip-clear target, 13 songs). The automation-view note-editor "clear" is note-row-scoped → separate `ClearNoteRow` (funnels that 1 site). |
| `CLIP_DELETE` | `DeleteClip` | clip | M2d | pending |
| `CLIP_MULTIPLY` | `MultiplyClip` | clip | M2d | pending |
| `CLIP_HORIZONTAL_SHIFT` | `ShiftClip` | clip | M2d | **landed (M2d pilot)** — establishes `ClipAddress`/`clipAddressFor` (clip owner tier); mutation (`shiftHorizontally`) + coalesce/create `ConsequenceClipHorizontalShift`, goto-free; `shifted()` preserves the "nothing moved → bail" branch. edit_undo_redo clip-shift target, 13 songs. |
| `PATTERN_PASTE` | `PastePattern` | clip | M2d | pending |
| `SWING_CHANGE` | `SetSwing` | song-global | M2d | pending |
| `TEMPO_CHANGE` | (tempo value via `SetParamValue` M1c S5; `recordTempoChange` path) | song-global | M2d | partial — value edit landed; the `TEMPO_CHANGE` recorded path pending |
| `CLIP_INSTANCE_EDIT` | `EditClipInstance` | arrangement | M2f | pending |
| `ARRANGEMENT_TIME_EXPAND` / `ARRANGEMENT_TIME_CONTRACT` | `ExpandArrangementTime` / `ContractArrangementTime` | arrangement | M2f | pending |
| `ARRANGEMENT_CLEAR` | `ClearArrangement` | arrangement | M2f | pending |
| (kit row membership, per-drum descriptors — no single ActionType) | `AddKitRow` / `RemoveKitRow` / … | kit / drum | M2e | pending |
| (instrument swap — no single ActionType) | `SwapInstrument` | instrument | M2g | pending |
| `RECORD` | — (recorded input, reconcile path) | — | M4 | out of scope (not an Edit) |
| `ARRANGEMENT_RECORD` | — (recorded input, reconcile path) | — | M4 | out of scope (not an Edit) |
| `MISC` | — (uncategorised catch-all) | — | — | n/a |

**Phase legend** (owner-aligned; each feeds an M3 owner flip): M2b note pilot · M2c notes complete ·
M2d clip structure · M2e kit/drum · M2f sample/audio-clip + arrangement · M2g sound/instrument ·
M2h Action view-state shed (independent). See the migration plan's M2 ladder.
