# S5-b — session-id addressing: drop `doc()`'s engine fallback

**Status:** design, pending review
**Branch:** `deluge-project-param`
**Depends on:** S5-a (param `Kind` threaded onto the ModelStack; `addressFor()` extracted) — DONE.
**Task:** #66.

## Goal

Make `ModelStackWithAutoParam::doc()` resolve the DOM node store for **every** owner via a `ParamAddress`, so the
engine fallback `return paramCollection->docParamForId(paramId)` (auto_param.cpp:119) can be removed. That makes
`doc()` — the DOM read/edit seam — free of the `paramCollection` pointer, one concrete step toward the north-star
goal of shrinking the ModelStack (the `paramCollection` pointer stays on the stack for the ~260 engine-dispatch
uses; this decouples only the DOM-resolution seam from it).

## Current gap

`deluge::edits::addressFor()` (model/edits/param_address_for.cpp) returns `std::nullopt` — forcing the fallback —
whenever the owner is **not addressable**, i.e. has no `serialId`:
1. A **no-serialId clip** (`clip->serialId()` empty) — a clip that has never been saved. Real and common: a
   freshly-created clip before first save, or any clip in a freshly-loaded pre-serialId (old-format) song before
   it is re-saved.
2. A **no-serialId output** (synth/kit clip-level params) — same, for the instrument.
3. A **no-serialId drum** (per-drum params).
4. The **instrument-level default** (null-clip ParamBackup entry) of a no-serialId instrument.

`serialId`s are assigned lazily **on save** (never eagerly — that would change the values written and re-baseline
the golden). So the no-serialId state is a genuine, transient, pre-save runtime state; the fallback exists to
serve it. To drop the fallback, these owners must become addressable **without** a persisted id.

## Design: a transient session-id, parallel to `serialId`

Give every owner a **session id** — a `uint32_t` assigned in memory at load / creation, monotonic per Song,
**never written to file**. It is the runtime addressing identity for owners that don't (yet) have a `serialId`.
`serialId` keeps its two existing jobs (persistence + save-time addressing); the session id adds runtime
addressing for the not-yet-saved. They coexist; `serialId` wins when present so nothing about the saved path
changes → **golden stays byte-identical** (session ids are never serialized).

This is a **direct extension of S3/S4**, which already proved the load pattern: relocate an owner's store into a
Song-owned tree entry keyed by a stable id, `bindDomStoreExternal` the engine set to it, and the binding **rides**
`stealParamCollectionsFrom` / ParamBackup park+restore (only the collection *pointer* moves; the ParamSet object
— and its external binding — never relocates). S5-b reuses that machinery with a session id instead of a serialId.

### Pieces

- **Owner session id.** Add `uint32_t session_id_` (always valid, assigned at load/creation) to the owners that
  `addressFor` classifies: `Clip`, `Output`, `Drum`. A `Song` counter (`sessionIdCounter_`) mints them; assigned
  in the same post-load pass that assigns/reads serialIds, and at runtime creation (clone / new clip / setDrum).
- **Session-keyed tree stores.** Mirror the existing tree containers, keyed by session id:
  - `project.session_clip_params: fast_map<uint32, DocParamManager>` (parallels `clip_params`, for AudioClip +
    MIDI-clip owners).
  - `project.session_backed_params` (parallels `backed_params`, keyed `(owner-session-id, clip-session-id-or-null)`
    for synth/kit clip-level + per-drum + instrument-level-default owners).
  These are transient (cleared on song load, like the session ids).
- **`OwnerId` gains a `session` flavor.** `OwnerId` already carries a `serial_id`; add a bit / kind so a
  session-keyed owner is distinguishable from a serialId-keyed one, so `resolveParam` routes to the session maps.
  (Simplest: an `OwnerId::Kind::*_SESSION` variant per existing kind, or a `bool is_session` flag.)
- **`addressFor`:** when the owner lacks a `serialId`, build a **session** address from its `session_id_` instead
  of returning `nullopt`. Every non-null-tc owner is now addressable.
- **`resolveParam`:** the new session cases look up the session maps (same slot/cable/CC resolution as the serialId
  cases, just a different map).
- **Relocation:** extend the post-load `relocateParamStoreToTree` pass (song.cpp) — and the runtime
  create/clone/setDrum sites — to relocate **no-serialId** owners into the session maps + `bindDomStoreExternal`.
  (Currently no-serialId owners keep `bindDomStore()` on the engine's own store; this replaces that with the
  session tree entry.)
- **Drop the fallback:** `doc()` becomes `return resolveParam(*addressFor(...))` with a nullptr guard that, in
  ALPHA/BETA, `FREEZE`s (a param op with no address is now a bug, not a fallback). `paramCollection` is no longer
  dereferenced in `doc()`.

### Lifetime

Session tree entries are Song-owned (stable address), so the external binding is dangle-safe exactly as the
serialId entries are (proven in S3c/C4). On clip/owner death the entry is a **bounded within-session leak, freed
on song reload** — the same posture S3 accepted for the serialId entries initially; a `remove`-on-death pass can
follow (as S4.5b did for serialId). The session id lives on the owner and travels with it; ParamBackup migration
needs **no key mirroring** — resolution rides the binding, and `addressFor` reads the owner's stable session id
wherever it is parked (the S3c validated insight).

### Golden-neutrality

Session ids are never serialized and `serialId` addressing is unchanged, so save output is identical → golden
`OK=15` byte-identical must hold. The new resolution is exercised + asserted by extending the `song_edit`
tree-residency + `scalarOk` checks to the no-serialId owners (load a pre-serialId song, or a freshly-created
clip), and by an ALPHA assert that `doc()` never nullptrs.

## Staging (each golden-gated, x86≡ARM)

1. **S5-b.1 — AudioClip only.** AudioClips never touch ParamBackup, so this proves the session-id mechanism with
   zero migration complexity: `Clip::session_id_`, `session_clip_params`, `OwnerId` session flavor, `addressFor`
   + `resolveParam` for the CLIP-session case, relocate no-serialId AudioClips. The fallback stays for the others.
   Gate: golden byte-identical + song_edit audio tree-residency/scalarOk on a no-serialId audio clip.
2. **S5-b.2 — MIDI clips** (session CLIP case, CC resolution).
3. **S5-b.3 — synth/kit clip-level + per-drum** (`Output`/`Drum` session ids, `session_backed_params`) — the
   ParamBackup-migrated owners; leans on the ride-the-binding insight.
4. **S5-b.4 — instrument-level default** (null-clip, no-serialId instrument).
5. **S5-b.5 — drop the fallback** once `addressFor` is total: replace with an ALPHA `FREEZE` nullptr guard; remove
   the `paramCollection->docParamForId` line. `doc()` is now `paramCollection`-free.
6. (later) **remove-on-death** for session entries (mirror S4.5b) to retire the bounded leak.

## Alternative considered — eager `serialId`

Assign a `serialId` to every owner at load (reuse the existing machinery) so there are no no-serialId owners.
Simpler code, but the written serialId values change (load-order vs save-order assignment) → the golden hashes
must be **re-baselined**, losing the byte-identity proof that has anchored this whole effort. Rejected in favour of
the transient session id, which preserves byte-identity.

## Honest payoff assessment (for review)

- **What it buys:** `doc()` (the DOM read/edit seam) resolves purely by id, never dereferencing `paramCollection`
  — and *every* owner's params become tree-resident (the project tree becomes the complete store, not just for
  saved owners), which is a real step for the "project owns all persistent structure" north star.
- **What it does NOT buy:** `paramCollection` stays on the ModelStack for its ~260 engine-dispatch uses (notify,
  summary, `processCurrentPos`, view rendering). Removing *those* — the actual ModelStack shrink — is a separate,
  much larger project (dissolving the `ParamCollection` hierarchy). S5-b is a bounded, safe, self-contained
  prerequisite step, not that project.
- **Cost:** ~5 staged slices touching the tree structures, `OwnerId`, `addressFor`/`resolveParam`, and the
  load/clone/setDrum relocation sites; the synth/kit/drum slice re-treads the S3 ParamBackup care.

**Decision for review:** is the "tree holds every owner + `doc()` is paramCollection-free" milestone worth the
~5-slice cost, given `paramCollection` stays on the stack regardless? If yes, proceed S5-b.1 (AudioClip). If the
bigger prize is the dispatch-role dissolution, that wants its own design and S5-b is optional scaffolding.
