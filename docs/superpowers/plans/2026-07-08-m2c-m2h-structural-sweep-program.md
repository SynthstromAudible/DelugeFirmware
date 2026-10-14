# M2c–M2h — Structural Write-Funnel Sweep — Program Plan

> **Program plan, not a single implementation plan.** Each phase below is executed as its own focused
> spec→plan→execute cycle (the M2b recipe), driven by `docs/dev/edit_catalog.md`. This document is the
> roadmap: per phase it fixes the *address*, the *Edits*, the *funnel-site cluster*, the *tests*, and the
> *dependencies* — enough to lift each phase into a concrete plan when reached, without over-specifying
> phases whose design depends on what earlier ones reveal.

**Goal:** funnel the entire remaining persistent-mutation surface into Edits (M2 exit: every `edit_catalog.md`
row a landed Edit; scattered setters retired or made private to Edit impls). Owner-aligned so each phase
enables its M3 owner flip.

## The proven recipe (from M2a/M2b)

Each mutation site becomes: `Context ctx{*currentSong, currentSong->project}` + construct the Edit from
`<address>For(*engineStack)` + payload + `apply(ctx, edit)`, replacing the inline `getNewAction(TYPE)` + engine
mutation. The engine mutation self-records its undo Consequence into the Action (param snapshot / note-array /
clip snapshot). The address is carried (funnel phase), resolved to a model target only at M3. Primary net is
`edit_undo_redo` extended per owner; golden `songs`/`param_record` + firmware guard collateral; ARM + on-device
smoke at each phase boundary.

## Shared prerequisite — **P0: Universal structural ids** (do first, or interleave with kate's S5-b)

**Finding (from M2b):** S5-b mints session ids only for owners **with param collections** (its relocation pass is
gated on `containsAnyMainParamCollections()`). Structural addressing needs a durable id for **every** clip /
output / drum / note-row — including note-only clips, param-less clips, empty kit rows. Today those are
unaddressable (`noteRowAddressFor` returns nullopt for a note-only clip — proven in M2b's harness skips).

**P0:** on load, mint a transient session id for **every** clip, output, and drum unconditionally (decouple id
minting from param relocation in `Song::readFromFile`). Never serialized (like existing session ids); golden-
neutral. This is the enabler for all of M2c–M2g. Coordinate with kate (owns S5-b) — it may already be in
progress; if so, P0 is just "confirm universal, add the const-assert gate." Verify with an `edit_undo_redo`-style
sweep asserting every clip/output/drum has a nonzero id after load.

---

## M2c — Notes complete → M3 owner 2

**Scope:** kit note-rows + the remaining note/note-row `ActionType`s. Counts: `NOTE_EDIT` (13 sites left of 15),
`NOTE_NUDGE` (2), `NOTES_PASTE` (1), `NOTE_REPEAT_EDIT` (1), `EUCLIDEAN_NUM_EVENTS_EDIT` (1), `NOTEROW_LENGTH_EDIT`
(1), `NOTEROW_HORIZONTAL_SHIFT` (1), `NOTEROW_ROTATE` (close-only, 2), `NOTE_TAIL_EXTEND`.

**Address:** extend `NoteRowAddress` with a **row-key variant** — `y` (melodic) or `drum_id` (kit, from P0/S5-b
drum ids). Add a `bool is_kit` + reuse the id fields. `noteRowAddressFor` drops its kit-→nullopt guard and
classifies the drum id (mirroring `addressFor`'s drum branch).

**Edits:** `ExtendNoteTail`, `NudgeNotes`, `PasteNotes`, `RepeatNoteEdit`, `SetEuclideanEvents`, `RotateNoteRow`,
`SetNoteRowLength`, `ShiftNoteRow`, plus kit `AddNote`/`RemoveNote` (reuse the melodic Edits once the address
covers kit). Each funnels its `getNewAction(TYPE)` + the `NoteRow`/`InstrumentClip` engine op.

**Funnel sites:** `instrument_clip_view.cpp` (the bulk), `note_row.cpp`, `automation_view.cpp`.

**Tests:** `edit_undo_redo` gains a **kit** note target (drum-id addressed) + a case per note-row op (nudge,
length, rotate, shift) — mutate→undo→redo over a real clip. Golden `songs` (note serialization) is the collateral
guard.

**Deps:** P0 (kit/drum ids). **Exit:** every note/note-row row in the catalog landed.

---

## M2d — Clip-structure Edits → M3 owner 1

**Scope:** `CLIP_LENGTH_INCREASE`/`DECREASE`, `CLIP_CLEAR` (5 sites), `CLIP_DELETE`, `CLIP_MULTIPLY`,
`CLIP_HORIZONTAL_SHIFT`, `PATTERN_PASTE` (6 sites), `SWING_CHANGE` (song-global), `TEMPO_CHANGE` (2; the value edit
already lands via `SetParamValue` M1c-S5 — this is the `recordTempoChange` structural path).

**Address:** `ClipAddress { clip_id, is_session }` (P0 ids). Song-global ops (swing, tempo) reuse `OwnerId::song`.

**Edits:** `SetClipLength`, `ClearClip`, `DeleteClip`, `MultiplyClip`, `ShiftClip`, `PastePattern`, `SetSwing`,
`SetTempo` (structural). Undo via the clip's existing snapshot Consequences.

**Funnel sites:** `session_view.cpp`, `arranger_view.cpp`, `instrument_clip_view.cpp`, `clip.cpp`, `song.cpp`.

**Tests:** `edit_undo_redo` clip target — set-length / clear / delete over a real clip, undo/redo; note
`DeleteClip`'s undo must restore the clip (existence Consequence). Golden `songs` guards clip serialization.

**Deps:** P0. Feeds M3 owner 1 (the plan's designated first owner flip). **Exit:** all clip rows landed.

---

## M2e — Kit/drum structure Edits → M3 owner 3

**Scope:** kit row membership (add/remove a kit row = add/remove a drum+note-row), per-drum descriptors (name,
output routing). No single ActionType — these ride `NOTE_EDIT`/existence Consequences + kit-specific paths.

**Address:** `KitRowAddress { kit_id (output), drum_id }` (P0 ids). Per-drum descriptor edits key on `drum_id`.

**Edits:** `AddKitRow`, `RemoveKitRow`, `SetDrumDescriptor…`. Funnel `Kit::addDrum`/row-delete + descriptor
setters.

**Funnel sites:** `kit.cpp`, `instrument_clip_view.cpp` (kit-row ops), `sound_drum.cpp`.

**Tests:** `edit_undo_redo` kit target — add/remove a row, undo/redo (existence Consequence). Golden `songs`.

**Deps:** P0, M2c (kit note addressing). **Exit:** kit/drum structural rows landed.

---

## M2f — Sample/audio-clip + arrangement Edits → M3 owner 4

**Scope:** audio-clip descriptors (file ref, start/end/pitch metadata); arrangement: `CLIP_INSTANCE_EDIT` (7
sites), `ARRANGEMENT_TIME_EXPAND`/`CONTRACT`, `ARRANGEMENT_CLEAR` (2). (`ARRANGEMENT_RECORD` is recorded input →
M4, not an Edit.)

**Address:** `AudioClipAddress` (clip id, P0) for descriptors; `ClipInstanceAddress { output_id, arrangement_pos
}` for arrangement clip-instances.

**Edits:** `SetSampleRef`, `SetSampleRegion`, `SetSamplePitch`; `EditClipInstance`, `ExpandArrangementTime`,
`ContractArrangementTime`, `ClearArrangement`.

**Funnel sites:** `audio_clip_view.cpp`, `arranger_view.cpp`, `audio_clip.cpp`, `clip_instance.cpp`.

**Tests:** `edit_undo_redo` audio-clip + arrangement targets. Golden `songs` (arrangement + sample refs
serialize). **Deps:** P0. **Exit:** audio-clip + arrangement rows landed.

---

## M2g — Sound/instrument structure Edits → M3 owner 5

**Scope:** instrument swap (synth↔kit↔MIDI↔CV on an Output/Clip); the largest, guided by
`docs/dev/synth_state_decoupling.md`. No single ActionType — swap goes through instrument-change paths.

**Address:** `InstrumentAddress` (output id, P0). Swap is addressed by the clip/output whose instrument changes.

**Edits:** `SwapInstrument` (+ any sound-structural descriptor edits the audit surfaces).

**Funnel sites:** the instrument-change paths (`view.cpp`/`session_view.cpp`/`instrument_clip.cpp` — grep
`changeInstrument`/`setInstrument`). Largest blast radius — slice narrowly, gate hard.

**Tests:** `edit_undo_redo` swap target (undo restores the prior instrument). Golden `songs` + hardware ear-check.
**Deps:** P0; benefits from the four easier owners drilled first. **Exit:** instrument-structural rows landed.

---

## M2h — Action view-state shed (independent track, any time)

**Scope:** move `Action`'s view-state (`xScrollClip`, `yScrollArranger`, zoom, mode fields, `view`, `currentClip`,
triplets, affectEntire) to a **frontend-owned side-record keyed by the Action**, leaving the core transaction
layer model-only. Mechanical; no address, no new Edits.

**Approach:** (1) introduce `ActionViewState` side-record (a map Action*→view-state, owned by the UI layer);
(2) move `Action::getNewAction`'s view-state capture + `revert`'s view-state restore into the frontend hook;
(3) delete the view-state fields from `Action`. Independently committable in that order.

**Tests:** `edit_undo_redo` + `edit_test` unchanged (they don't depend on Action view-state); golden `songs`;
manual UI check that undo still restores scroll/zoom. **Deps:** none. **Exit:** `Action` is view-state-free.

---

## Sequencing & exit

**Order:** P0 → M2c → M2d (owner 1 can then M3-flip) → M2e → M2f → M2g; M2h any time (parallel). Each phase:
its own spec→plan→execute; update `edit_catalog.md` rows to **landed** as they close; ARM + on-device smoke at
each boundary. **M2 exit:** every catalog row landed; scattered setters retired or made private to Edit impls so
stragglers are compile errors; `Action` view-state-free. That unblocks M3 completion (per-owner ownership
inversion) across all owners.

## Cross-cutting risks

| Risk | Mitigation |
|---|---|
| Structural addressing blocked by missing ids | **P0** mints universal session ids first; every phase's `edit_undo_redo` target asserts its address resolves. |
| Entangled UI sites (multi-op loops, shared Action) as in M2b | Funnel single engine-mutation calls; coalescing preserves the shared Action; expose engine return where a site branches on it (cf. `AddNote::added()`). |
| Instrument swap (M2g) blast radius | Slice narrowly, hardware ear-check, do last after the method is drilled on 4 owners. |
| A phase reveals the recipe doesn't fit | Each phase is its own spec — stop and redesign that phase, don't force. |
