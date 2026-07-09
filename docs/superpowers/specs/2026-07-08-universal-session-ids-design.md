# Universal session ids (Plan A) — design

**Goal:** Mint a transient session id for **every** clip / output / drum at load, unconditionally, so every entity of
those tiers is id-addressable — closing the gap where only param-bearing entities got ids. Golden-neutral (session ids
are never serialized). This makes the clip-structure flip *total* (every clip bindable at load) and directly unblocks
retiring the engine `Clip::loopLength_` mirror.

**Context:** M3 owner-1 substrate. Session ids are the transient runtime-addressing bridge (S5-b): `serialId` is the
durable identity written on save; `sessionId` addresses not-yet-saved entities so `doc()`/`resolveClip` resolve by id.
This plan universalizes the `sessionId` side. Kate's steer: (1) remove the redundant conditional mints outright; (2)
durable DOM identity gets serialized — so `ClipInstance`'s serialized `serialId` is a **separate plan (B)**, not here.

## Current state (the gap)

Session ids are minted **conditionally, woven into the param relocation** in `Song::readFromFile`:
- audio clip (song.cpp:1817), synth/kit clip (:1849) + its output (:1852), MIDI clip (:2177), drum (:1906) — each only
  when that entity has params to relocate *and* lacks a `serialId`.
- `relocateAndResolve` (param_address_for.cpp:170/175/181) mints lazily for entities created **after** load (runtime).

So a clip that doesn't hit a relocation branch stays unaddressable → the M3 load-binding pass can't bind it → it keeps
reading the engine field, blocking field retirement.

## Design

### `Song::assignSessionIds()` — one unconditional pass
A new method on `deluge::project::Song`, called **early in `Song::readFromFile`, before the param-relocation loop**
(once all clips/outputs/drums are loaded). For every entity **without a `serialId()`**, assign a session id if it
doesn't already have one (idempotent):
```cpp
void Song::assignSessionIds() {
  for (Clip* c : session_clips)             if (!c->serialId() && c->sessionId() == 0) c->setSessionId(mintSessionId());
  for (Clip* c : arrangement_only_clips)    ... same ...
  for (Output* o : first_output-list)       if (!o->serialId() && o->sessionId() == 0) o->setSessionId(mintSessionId());
  for (Drum* d : each kit output's drums)   if (!d->serialId() && d->sessionId() == 0) d->setSessionId(mintSessionId());
}
```
(Exact iteration mirrors the existing relocation's entity walk. Outputs minted once — shared across their clips, as the
current `mint-once` logic guarantees. `relocateAndResolve`'s runtime minting stays; the shared `next_session_id_`
counter continues, so no collision.)

### Remove the 5 redundant conditional mints
With ids pre-assigned, the inline mints in `readFromFile` are dead: delete the `setSessionId(mintSessionId())` lines at
song.cpp:1817-1818, 1848-1850, 1851-1853, 1905-1906, 2154/2177 (keep the surrounding relocation, which now *reads* the
pre-assigned `sessionId()`). The relocation's serialId-vs-session branch decisions are unchanged — the pass only
guarantees a session id exists; it does not alter keying (mixed serial/session cases still fall through as today).

### `relocateAndResolve` unchanged
It remains the runtime path for entities created after load. Kept minting off the same counter.

## Why golden-neutral

Session ids are transient and never serialized. Minting more of them (and in a different order — the pass assigns all
upfront rather than per-relocation-branch) changes no saved bytes → golden roundtrip stays REGRESS=0. Each entity and
its DOM store use the same id, so runtime resolution/behaviour is preserved. The id ordering/values are not observable.

## Testing

- **Golden songs OK=15 REGRESS=0** (neutrality — the load path now runs `assignSessionIds` on every song).
- `edit_undo_redo` **universal-addressability target**: after load, assert that **every** session + arrangement clip
  resolves — `deluge::edits::clipAddressFor(*clip).has_value()` — and is **bound** to a DocClip
  (`song->project.resolveClip(*addr) != nullptr`, seeded by the M3 load-binding pass now that all clips are
  addressable). This proves the universalization (previously some clips were unaddressable/unbound).
- edit_test OK, param_record OK=3, firmware clean.

## Unblocks (separate follow-ups)

- **Retire `Clip::loopLength_`** — with every loaded clip bound and all writes through `setLoopLength`, the field
  fallback is (at load) never used; retirement becomes tractable (still gated on runtime-created clips being bound via
  `relocateAndResolve`, which mints + would need to bind — a small extension).
- **Plan B** — `ClipInstance` serialized `serialId` (the M2f close), a file-format change with a golden re-baseline.

## Out of scope

`ClipInstance` identity (Plan B); retiring `loopLength_`; wiring session ids into any new resolver; the runtime-created
clip binding extension.
