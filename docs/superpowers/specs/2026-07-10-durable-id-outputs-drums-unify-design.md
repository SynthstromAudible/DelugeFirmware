# durable-id — outputs/drums unify on durable id (Slice 1) — Design

**Status:** design, approved for planning
**Branch:** `deluge-project-param`
**Date:** 2026-07-10
**Parent:** [[durable-id-unification]]. Clips shed the transient `session_id_` (Step 3: id-first ctor-binding). Outputs/drums have not — they still carry `session_id_` and mint their durable `id` lazily at *save* time, so a runtime-created owner has no durable id and the param-automation addressing (`OwnerId`) falls back to `session_id_`. This slice makes outputs/drums always get a **durable** id at the points a session id is minted today, so every param-addressed owner is id-addressed and the session path goes dead. Slice 2 (Phase 4) then deletes the session machinery.

## Goal

Redirect the two session-id mint points to mint **durable** ids for outputs/drums, so `session_id_` is never assigned and `OwnerId`'s session branches become dead code. Behaviour-preserving for the persisted model; the only observable change is that a runtime owner's `id` (which *is* serialized) may take a different counter value than before. `songs` bit-identity is preserved (loaded owners keep their file ids); `param_record` is the gate and may re-baseline.

## Background — where session ids come from

`session_id_` (transient, never serialized) is minted only for owners that lack a durable `id`, at exactly two places:
1. `Song::assignSessionIds()` (song.cpp:1190), called once at load (song.cpp:1828) — mints for any clip/output/drum with `!id() && sessionId()==0`.
2. `param_address_for.cpp` lazy path (~169-181) — mints on first param-address for the owner chain of the param being edited.

Clips already always have a non-optional `id` (ctor-minted), so both clip branches are already dead. Only outputs/drums (optional `id`, minted lazily at save) still reach the session mints. `OwnerId` (param_address_for.cpp:78-103) already prefers the durable-id branch (`OwnerId::drum(*id)`, `OwnerId::instrument(*id)`) when `id()` is set, falling back to `…Session(sessionId())`.

## Design

Change the **output and drum** session-mints to durable-id mints; leave the clip mints (already dead) untouched for Slice 2.

### 1. `Song::assignSessionIds()` (song.cpp:1202-1212)
```cpp
	for (Output* o = project.media.first_output; o != nullptr; o = o->next) {
		if (!o->id()) {
			o->setId(generateNewOutputId());
		}
		if (o->type == OutputType::KIT) {
			for (Drum* d = static_cast<Kit*>(o)->firstDrum; d != nullptr; d = d->next) {
				if (!d->id()) {
					d->setId(generateNewDrumId());
				}
			}
		}
	}
```
(The `Song` has `generateNewOutputId()`/`generateNewDrumId()`. Drop the `&& sessionId()==0` guard — irrelevant once we mint a durable id. Loaded owners have file ids, so `!o->id()` is false and this is a no-op for them; it only fires for the rare id-less owner, which now gets a persistent id — strictly more correct.) The clip loop above stays as-is (dead).

### 2. `param_address_for.cpp` lazy path (~173-181)
The drum branch:
```cpp
		if (noteRow->drum != nullptr && noteRow->drum->type == DrumType::SOUND && !noteRow->drum->id().has_value()) {
			noteRow->drum->setId(modelStack.song->generateNewDrumId());
		}
```
The output branch:
```cpp
	else if (clip->type == ClipType::INSTRUMENT && clip->output != nullptr
	         && (clip->output->type == OutputType::SYNTH || clip->output->type == OutputType::KIT)
	         && !clip->output->id().has_value()) {
		clip->output->setId(modelStack.song->generateNewOutputId());
	}
```
(`modelStack.song` is the runtime `Song*` with the durable counters; `proj` was only for `mintSessionId`.) The clip lazy-mint at :169-170 stays (dead — `clip->id()==0` never true post-ctor).

### Result
After this, any output/drum that gets param-addressed has `id().has_value()` → `OwnerId` takes `OwnerId::instrument(*id)` / `OwnerId::drum(*id)`; `session_id_` is never set (always 0); the `…Session` branches and `note_address_for`'s session branch are unreachable. Nothing is deleted yet — Slice 2 removes the dead branches + `session_id_` + `assignSessionIds`/`mintSessionId`.

## What stays (Slice 2)
`session_id_`, `sessionId()`, `setSessionId`, `mintSessionId`, `next_session_id_`, `assignSessionIds` (as a whole), and the `OwnerId::…Session` branches in `param_address_for`/`note_address_for`. Deleting them is behavior-neutral once this slice makes them dead, so it's a separate clean commit.

## Scope

**In:** the two mint-point redirections (outputs/drums only). **Out:** deleting any session machinery (Slice 2); clips (already done); `id_` staying optional for outputs/drums (correct — instruments live outside songs; the invariant is "every param-addressed song-member owner has an id", not "every instrument has one").

## Testing — the gate

- `songs` `OK=16 REGRESS=0` — **must stay bit-identical.** Loaded owners keep their file `id`; this slice only affects owners with no durable id, of which a loaded song has none. A REGRESS here means an assumption is wrong — stop and diff.
- `edit_undo_redo` `OK=40`, `param_record` — runtime owners now get a durable (serialized) id at address-time instead of a session id. If `param_record` (or `edit_undo_redo`) hashes shift, it is because a runtime owner's serialized `id` counter value moved (mint at address-time vs the old save-time order). **Re-baseline only after confirming the driver still SETTLES** (gen1==gen2 — proves consistent keying, i.e. a value-neutral reordering, not a real regression). Record the re-baseline explicitly.
- `json_roundtrip OK=38`, `./dbt test` 15/15, firmware links.

## Risks

1. **`param_record`/`edit_undo_redo` id-order shift** — expected and acceptable per above; the settle-check distinguishes reordering from regression. If the driver does NOT settle, that's a real bug (inconsistent keying) — investigate, don't re-baseline.
2. **An owner param-addressed before it has an id AND before this mint fires** — the mint is at the top of `param_address_for` before `addressFor`, exactly where the session mint was, so timing is unchanged; only the id space changes.
3. **`assignSessionIds` minting a durable id for a genuinely id-less loaded owner** — would serialize an id it didn't before. Loaded owners have file ids so this shouldn't fire; if a corpus song trips it, `songs` REGRESSes and flags it.
