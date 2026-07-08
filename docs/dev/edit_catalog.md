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
| `NOTE_TAIL_EXTEND` | `ExtendNoteTail` | note-row | M2c | pending |
| `NOTE_NUDGE` | `NudgeNotes` | note-row | M2c | pending |
| `NOTE_REPEAT_EDIT` | `RepeatNoteEdit` | note-row | M2c | pending |
| `NOTES_PASTE` | `PasteNotes` | note-row | M2c | pending |
| `EUCLIDEAN_NUM_EVENTS_EDIT` | `SetEuclideanEvents` | note-row | M2c | pending |
| `NOTEROW_ROTATE` | `RotateNoteRow` | note-row | M2c | pending |
| `NOTEROW_LENGTH_EDIT` | `SetNoteRowLength` | note-row | M2c | **landed (new-action branch)** — prevAction/secret-undo branch pending; edit_undo_redo-verified (apply+undo+redo, 9 songs) via the setCurrentClip harness coverage |
| `NOTEROW_HORIZONTAL_SHIFT` | `ShiftNoteRow` | note-row | M2c | pending |
| `CLIP_LENGTH_INCREASE` / `CLIP_LENGTH_DECREASE` | `SetClipLength` | clip | M2d | pending |
| `CLIP_CLEAR` | `ClearClip` | clip | M2d | pending |
| `CLIP_DELETE` | `DeleteClip` | clip | M2d | pending |
| `CLIP_MULTIPLY` | `MultiplyClip` | clip | M2d | pending |
| `CLIP_HORIZONTAL_SHIFT` | `ShiftClip` | clip | M2d | pending |
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
