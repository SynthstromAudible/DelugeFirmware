# Durable-ID Unification — Design

**Status:** design, approved for planning
**Branch:** `deluge-project-param`
**Date:** 2026-07-08
**Depends on / supersedes:** the S5-b transient session-id machinery and the M3 owner-1 `ownDoc_`/`treeDoc_` + `adoptClip` bind-at-creation slices (`e814d0ab4`, `abdd8ca8b`). Part of the [project-model north star](../../dev/project_model_north_star.md): *the Model owns persistent structure; identity precedes values; ids + resolve-at-use.*

## Goal

Give every persistent owner (`Clip`, `Output`, `Drum`) a single **durable identity — its `id` — minted at construction and used as the one key everywhere**: the runtime Model stores, the file, and address resolution. This lets a clip's structural record (`DocClip`) exist from the instant the clip is born, so the transient `ownDoc_` fallback can be deleted, and it retires the entire transient session-id apparatus (`sessionId`, `assignSessionIds`, `relocateAndResolve`, the `session_*` maps, every `is_session` flag).

End-state: identity precedes values end to end — file, loader, and runtime all lead with the id; the engine object holds only its `id` and a cached record pointer, never a private copy.

## Naming

Today the durable identity is called `serialId` (`serial_id_`), named to contrast with the transient `sessionId` and the positional `indexForSaving`. This program **deletes** `sessionId`, leaving a single identity — so the "serial" qualifier is vestigial. It is renamed to plain **`id`**, which matches the file attribute (literally `"id"`), matches `ClipAddress::clip_id` (already so named), and reads as the ordinary entity-identity term:

| before | after |
| --- | --- |
| `Clip/Output/Drum::serial_id_` | `id_` |
| `serialId()` / `setSerialId()` | `id()` / `setId()` |
| `OwnerId::serial_id` | `OwnerId::id` |
| `ParamAddress::clip_serial` | `ParamAddress::clip_id` |
| `ClipAddress::clip_id` | (unchanged) |

The counter methods (`generateNewClipId` / `generateNewOutputId` / `generateNewDrumId`) contain no "serial" and are unchanged. The rename is a pure mechanical, compiler-driven pre-slice (~207 occurrences across 32 files) landed **before** any semantic change — see *Phase −1*.

## Why not a transient runtime handle

An earlier draft introduced a process-global transient `handle` as the runtime key, keeping the durable id as a separate lazy-at-save file id. Rejected: it keeps **two** identities (a rename-and-relocate of `sessionId`, not a unification) and puts a transient key on the authoritative-persistent Model stores — an impedance layer exactly where the north star wants the Model's identity to *be* the persisted identity. Its only claimed advantage — golden-neutral file bytes — is **also** achieved by the durable id (see *Golden-neutrality*), so the handle bought nothing while adding a boundary translation we would later have to remove.

## Identity model

`id` is the durable identity, with three changes from today's `serialId`:

1. **Minted at construction, not lazily at first save.** A runtime-created owner calls the existing per-song counter — `Song::generateNewClipId` / `generateNewOutputId` / `generateNewDrumId` (backed by `clipIdCounter_` / `outputIdCounter_` / `drumIdCounter_`) — **in its constructor**, moving the call from today's lazy sites (`clip.cpp:656`, `output.cpp:240`, `kit.cpp:264`). A loaded owner is constructed *with* its file id (see *Identity-first loader*). The counters are reused unchanged; on load each is advanced to `max(loaded id) + 1` as owners are constructed.
2. **`id_` becomes non-`optional`** — always present from birth. (Today `serialId() == nullopt` = "never saved"; that signal moves to an explicit flag — see *Things to watch*.)
3. **It is the single key for every store and address.**

## Storage & addressing

- `clip_structure`, `clip_params` → **one map each, keyed by clip `id`**. `session_clip_structure` and `session_clip_params` are **deleted**.
- `backed_params` (instrument/drum-owned automation) → keyed by **owner `id`**; its clip dimension (`ParamAddress::clip_serial` / `clip_is_session`) becomes just `clip_id` (a plain `id`).
- `OwnerId` collapses from `{kind, serial_id, is_session}` to **`{kind, id}`**. The `*Session` constructors and every `is_session` / `clip_is_session` flag are removed from `OwnerId`, `ParamAddress`, and `ClipAddress`. `ClipAddress` collapses to `{clip_id}`.
- `clipAddressFor` / `addressFor` return the owner's `id` directly (no serial-else-session branch).

## Constructor binding (kills `ownDoc_`)

Only **`Clip`** binds a store record at construction, because only clips have a structure store (`DocClip`). The base `Clip` ctor gains a `Song&` (and an optional pre-assigned id for the load path):

```cpp
Clip(ClipType type, Song& song, std::optional<uint32_t> loadedId = std::nullopt) {
    id_      = loadedId.value_or(song.generateNewClipId());
    treeDoc_ = &song.project.clip_structure.getOrCreate(id_);
    // bound. impossible to exist unbound. no ownDoc_.
}
```

- `ownDoc_` and the `treeDoc_`-nullable fallback are **deleted**; accessors become `treeDoc_->field`, setters `treeDoc_->field = …`. `DocClip`'s own member initializers (`colour_offset{-60}`, `section{0}`, `launch_style{0}`, `loop_length{0}`) supply the ctor's former structural defaults, so the ctor sets no structural fields and there is no write-before-bind hazard.
- `Output` / `Drum` ctors mint their `id` from the per-song counter too (they need `Song&` to reach the counter) but bind no structure record — they are param owners only.
- `~Clip` still calls `removeDocClip(id)` (the leak-plug from `abdd8ca8b`), now keyed by the durable id.
- Every construction site (~17 `new Clip` + the output/drum sites) threads a `Song&`. Misses are compile errors — self-catching.

## Identity-first loader

Today the loader does `new InstrumentClip()` then `readFromFile`, so the id arrives mid-parse — incompatible with ctor-binding. Fix: read the clip's `id` at element-open and construct with it.

`id`, `section`, `colourOffset`, etc. are genuine **opening-tag attributes** (written between `writeOpeningTagBeginning` and `writeOpeningTagEnd` in `XMLSerializer`), so a clip's identity is available the instant its element opens. The loader reads the `id` attribute first, then `new InstrumentClip(song, /*loadedId=*/id)`, then populates the remaining attributes into the now-bound record.

This is north-star-aligned: a serialized record leads with its key; the loader binds identity before values, matching the runtime invariant. Applies to clip, output, and drum elements.

**Fallback:** if any old file orders `id` after a structural attribute in a way the reader cannot hoist, a one-time provisional-id-then-rekey bridges it: construct with a provisional id, and when the real `id` is read, move the record `provisional → real` and update `id_`. Because `id` is a real opening-tag attribute, the clean path is expected to cover the corpus; the bridge is defensive.

## Load / save behaviour

- **Save:** unchanged. `id` is written exactly as today's `serialId` (it is already set, so the lazy-mint guard is a no-op). File bytes for loaded songs are byte-identical.
- **Load collapses.** The four post-parse passes — `assignSessionIds`, param `relocateAndResolve`, the owner-1 load-binding pass, and the session-id conditional mints — are **deleted**. Every store is keyed by `id` from the moment its owner is constructed, so parsing a clip's structural attributes writes straight through its bound record, and parsing params writes directly into `clip_params[id]` / `backed_params[owner id, clip id]`.

## Deletions

`session_id_` on `Clip`/`Output`/`Drum`; `mintSessionId` / `next_session_id_`; `assignSessionIds`; `relocateAndResolve` and the param-load relocation; `session_clip_params`; `session_clip_structure`; every `is_session` / `clip_is_session` flag in `OwnerId` / `ParamAddress` / `ClipAddress`; `ownDoc_` and the `treeDoc_` fallback; `adoptClip` (subsumed by ctor-binding).

## Sequencing

Each slice is behaviour-preserving and gates on all three golden nets before the next. Map rekeys are golden-neutral (none of these stores are serialized).

- **Phase −1 — Rename `serialId` → `id`.** Pure mechanical, compiler-driven rename (`serial_id_`→`id_`, `serialId()`→`id()`, `setSerialId`→`setId`, `OwnerId::serial_id`→`id`, `ParamAddress::clip_serial`→`clip_id`), no behaviour change. Lands before any semantics move so later diffs are semantic-only. Golden bit-identical + firmware link.
- **Phase 0 — `id` at construction (additive).** Mint `id` in the ctor from the per-song counter; make `id_` non-optional; introduce the "never saved" flag replacing the old `nullopt` signal. Keep lazy-at-save as a no-op guard. Thread `Song&` into ctors + all construction sites. Session machinery still live in parallel. Golden-neutral.
- **Phase 1 — Identity-first loader.** Hoist `id` to construction for clip/output/drum. Proves ctor-binding on the load path. Golden-neutral (same records, established earlier).
- **Phase 2 — Structure tier → `id`; kill `ownDoc_`.** Rekey `clip_structure` to `id`, ctor binds it, delete `session_clip_structure`, delete `ownDoc_` + the `treeDoc_` fallback + `adoptClip`. `ClipAddress` → `{clip_id}`.
- **Phase 3 — Param tier → `id`.** Rekey `clip_params` + `backed_params` (owner key and clip dimension) to `id` across all owner kinds; `OwnerId` → `{kind, id}`; delete `session_clip_params` + the `is_session` flags; dismantle `relocateAndResolve` and the param-load relocation. **Highest-risk slice — `param_record` is the gate.** May subdivide clip-dimension vs owner-dimension.
- **Phase 4 — Delete the corpse.** Remove `session_id_` from all three owners, `mintSessionId` / `assignSessionIds`, and any remaining `is_session` plumbing.

## Testing & gates

Every slice, x86 (ARM-targeted at phase boundaries):

- `songs` (`song_roundtrip`): byte-identical save, `OK=15 REGRESS=0`.
- `param_record` (`param_record_diff`): `OK=3` — the Phase 3 gate.
- `edit_undo_redo`: `OK=40` — addressing + undo; the `universalIds` / address targets adapt from serial/session to `id`.
- Firmware `./dbt build Debug` links.

Corpus: `/home/kate/Deluge Backup/SONGS` via `DELUGE_SONG_CORPUS`.

## Golden-neutrality argument

The corpus test is load → save → byte-diff. Loaded owners keep their file ids (save already preserves a set id), and the test creates no new owners — the only case where minting shifts an id value from save-order to creation-order. So save bytes are identical. Param replay resolves by `id` = the loaded file id, consistent throughout.

## Risk register

1. **Hot-path perf** — unaffected. `treeDoc_` stays a cached pointer; `id` keying happens only at bind time (ctor / load), never per read.
2. **Counter wrap** — `id` is `uint32` per song, as today; unchanged risk.
3. **Ctor-threading misses** — compile errors, self-catching.
4. **Param-load rekey (Phase 3)** — the deep one; isolated to its phase, `param_record`-gated, may subdivide.
5. **Undo/Consequences that stash a serial/session id to re-resolve** — audit and switch to `id`.
6. **`id` losing `optional`** — see *Things to watch*.
7. **Old-file `id` ordering** — mitigated: `id` is an opening-tag attribute; provisional-rekey bridge is the defensive fallback.

## Things to watch

- **The "never saved" signal.** Today `serialId() == nullopt` doubles as "this owner has never been persisted." With `id_` always set, that meaning moves to an explicit boolean (e.g. `hasBeenSaved_` / an `unsaved` flag). Audit every `serialId().has_value()` read for this intent vs. mere "do I have an id."
- **Counter init on load.** Advance each per-song counter to `max(loaded id) + 1` during construction so post-load creations don't collide with loaded ids.
- **`adoptClip` retirement.** It is subsumed by ctor-binding (Phase 2); its runtime insert-site hooks are removed as the ctor becomes the single bind point.
