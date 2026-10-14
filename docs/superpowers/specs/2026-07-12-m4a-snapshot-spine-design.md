# M4a — Snapshot spine + event-feed proof (param pilot)

**Status:** design (approved shape)
**Branch:** `deluge-project-param`
**Date:** 2026-07-12

## Where this sits

M3 save-authority is complete — the `deluge::project` model is the authoritative *writer* of
all persistent structure (no `syncDoc*` remains; save reads only the model). **M4** is the
read-side counterpart from the north star: seal the executor/audio thread behind a derived
**Snapshot** projected from the model, and make Edit-emitted events drive incremental snapshot
patches so the model is the authoritative *reader* too — not just at save.

M4 is several sub-milestones; this spec is only the first, **M4a**. The others are named here
for boundaries but designed separately:

- **M4a (this spec)** — carve the greenfield `Snapshot` type over one existing derived
  read-view (the param cursor), turn the observational `emitEvent` hook into a real
  event→snapshot-patch loop for that one group, and **measure** the RT cost with a host proxy.
- **M4b** — extend the snapshot to the hot Sound fields (currently `record + native cache`
  dual-writes); render reads the snapshot; retire those caches.
- **M4c** — the note VECTOR onto the snapshot (the biggest independence gap; RT-critical;
  `AddNote`/`RemoveNote` emit events that patch a note snapshot the playback path reads).
- **M4d** — LearnedMIDI onto the snapshot; make the engine-observed recorded-automation
  **reconcile** path explicit; split drum-MPE live-value vs recorded-automation → retire the
  `auto_param` interior undo log.

## Goal of M4a

Prove the event→snapshot-patch loop end-to-end on the lowest-risk group (params), establish the
`Snapshot` type + patch-protocol abstraction, and produce a **host-sim cost measurement** that
informs the north star's open granularity question (per-executor-unit vs per-song) — WITHOUT
touching the note vector, sealing the whole audio thread, or committing to snapshot-only reads.
M4a is behavior-preserving: goldens stay bit-identical.

## The existing seams M4a builds on

- **`Edit::emitEvent`** (`src/deluge/model/edits/edit.h:39`) — virtual, default no-op; dispatched
  in `deluge::edits::apply` (`edit.cpp`) after a successful `apply()`. Today only
  `SetParamValue` overrides it, and only observationally.
- **`SetParamValue::emitEvent`** (`set_param_value.cpp:47-57`) — resolves its durable
  `std::optional<project::ParamAddress> address_` via `ctx.model.resolveParam(*address_)` under
  `ALPHA_OR_BETA` and logs if it fails to round-trip. It patches nothing. `SetParamValue::apply`
  (`:14-45`) does the real mutation via `engineStack_->autoParam->setValuePossiblyForRegion(...)`.
- **The param cursor** — under Option I, `DocParamSet` owns the authoritative `current_value`;
  the engine projects a runtime cursor from the store (`projectFromStoreAfterRead`) and the
  patcher reads it every render to compute `paramFinalValues`. **This projected cursor is exactly
  "the derived read-view the engine already rebuilds" the north star tells M4 to name and carve.**
- No `Snapshot`/read-view type exists yet — greenfield (the only `Snapshot` in-tree is the
  unrelated MEM_GUARD epoch counter).

## Design

### 1. The `Snapshot` abstraction (greenfield, engine-side)
A minimal typed read-view with a patch API, living engine-side and depending on `deluge-project`
read-only (the model never references it; net dependency stays engine→model). Its **first
instance wraps the existing projected param cursor** for one owner — no parallel data structure;
the greenfield part is the *type + patch protocol + event wiring*, not new runtime data. The
abstraction answers: "what does the executor read, and how does an event patch it incrementally?"

Concretely for the pilot: a thin `ParamSnapshot` view over an owner's param cursor exposing
(a) the read the patcher already does, and (b) `applyPatch(ParamAddress, value/region)` — an
incremental update of one lane, equivalent to a targeted `setValuePossiblyForRegion` but sourced
from the model event rather than an in-line engine poke.

### 2. The event→patch loop
`SetParamValue::emitEvent` stops being observational: it resolves its `ParamAddress` (already
does) and calls `applyPatch` on the addressed owner's snapshot — the first real consumer of the
emission point. Full projection (`projectFromStoreAfterRead`) stays the *fallback* (load, or a
coarse invalidation), never the per-edit path.

### 3. Sealing approach — A/B behind a flag (zero RT risk in M4a)
Build the event-patch path, but keep the current in-line poke (`SetParamValue::apply`'s
`setValuePossiblyForRegion`) as the **default**, with the event-patch path selectable behind a
compile/config flag. M4a:
- runs BOTH paths and **asserts they produce identical cursor state** (the correctness proof);
- keeps the in-line poke as the shipped default → goldens bit-identical, no hot-path change;
- leaves the *commit* (make event-patch the sole path, delete the in-line poke) as a small
  follow-up flip, gated on the measurement showing acceptable cost.

So M4a carries no RT risk and no behaviour change; it establishes the mechanism and the number.

### 4. Measurement — host-sim cost proxy (decided: host only)
A host-sim microbench (new `tests/` target, CI-able): apply N `SetParamValue` edits and render M
blocks, counting instructions/allocations/wall-time for (a) the in-line-poke path and (b) the
event-patch path, at ≥2 snapshot granularities (per-owner cursor vs a coarser grouping). Output a
comparison table. **This number is a proxy** — an on-device audio-callback confirmation is an
explicit later follow-up (out of M4a scope), not a blocker. The table is the artifact that feeds
the north star's granularity decision.

## Scope

**In:** the `Snapshot`/`ParamSnapshot` type + patch protocol; `SetParamValue::emitEvent` driving
an incremental cursor patch; the A/B flag + equivalence assertion; the host-sim cost microbench;
the measurement write-up.

**Out (deferred, named above):** the note vector (M4c); hot Sound fields (M4b); LearnedMIDI +
recorded-automation reconcile + interior-log retirement (M4d); committing to snapshot-only reads
/ deleting in-line pokes (post-measurement flip); on-device RT confirmation; any snapshot
granularity *commitment* (M4a measures alternatives, it does not lock one in).

**Constraint — recorded automation stays put.** M4a covers only user-intent param edits
(`SetParamValue`). Engine-observed recorded param automation (the `auto_param` interior log,
sole remaining producer = drum-MPE) stays on its current path; its reconcile onto the model is
M4d. M4a's `emitEvent` fires only for the reified user edit.

## Testing / gates

- **Goldens bit-identical** (song 16 / json 38 / edit 40 / param 3 / audio_clip 1, x86≡ARM) +
  firmware links — the A/B keeps the in-line poke as default, so behaviour is unchanged.
- **New equivalence spec** (host): after a representative set of `SetParamValue` edits, the
  event-patch cursor state == the in-line-poke cursor state, lane-for-lane. This is the
  correctness proof that the event feed reproduces the in-line mutation.
- **The microbench** is a measurement artifact, not a pass/fail gate (it reports numbers).

## Risks / open questions

- **The cursor abstraction may be leakier than "one owner's array."** Params are patched
  (patch cables combine sources into `paramFinalValues`); the snapshot is the *input* cursor, not
  the patcher output. Verify the event-patch touches only the current-value the patcher reads,
  and that automation/region semantics (`setValuePossiblyForRegion`'s region handling) are
  reproduced by `applyPatch` — the equivalence spec must exercise a region edit, not just a
  point edit.
- **Granularity is measured, not chosen.** M4a must build the snapshot so ≥2 granularities are
  benchmarkable without a rewrite (e.g. the patch API keyed by `ParamAddress` works whether the
  cursor is per-owner or shared) — otherwise the measurement can't compare.
- **Proxy fidelity.** The host number is a model of RT cost; the design must not let a decision
  become irreversible before the device confirmation follow-up. Keeping the in-line poke as
  default (the A/B) is what preserves that reversibility.

## Exit criteria

- The `Snapshot`/`ParamSnapshot` type + patch protocol exist, engine-side, model-read-only.
- `SetParamValue::emitEvent` drives an incremental param-cursor patch (the emission point's first
  real consumer); a host spec proves it is lane-for-lane equivalent to the in-line poke, incl. a
  region edit.
- The host-sim cost microbench runs in CI and emits a granularity comparison table.
- Goldens bit-identical; firmware links. The in-line poke remains the default (no commit-flip in
  M4a).

## References
- North star: `docs/dev/project_model_north_star.md` (§ Snapshot, § write API events→snapshot)
- Ladder: `docs/dev/project_model_migration_plan.md` (§ M4)
- Pilot seam: `src/deluge/model/edits/edit.{h,cpp}`, `src/deluge/model/edits/set_param_value.{h,cpp}`
- Param projection (Option I): `Sound::readParamsFromFile` / `projectFromStoreAfterRead`, `DocParamSet`
- Interior log / drum-MPE (M4d dependency): `src/deluge/modulation/automation/auto_param.cpp`
