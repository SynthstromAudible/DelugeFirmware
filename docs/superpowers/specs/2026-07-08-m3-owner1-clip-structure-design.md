# M3 Owner-1 — clip-structure flip — design (+ shadow-store slice 1)

**Goal:** Begin the M3 ownership inversion for owner 1 (clip structure), mirroring the already-flipped param/automation
tier. **Slice 1 (this doc's implementation): the additive shadow-store front of the flip** — a `DocClip` model value
type + `resolveClip` resolver + dual-write from `SetClipLength`. No read/save/load flip yet; behaviour-preserving.

> ⚠️ **Coordination: this extends kate's DOM model** (`deluge::project`). Slice 1 is deliberately additive and
> invisible (nothing reads `DocClip`), so it is safe to land, but the `DocClip` shape / store keying / session-id
> reuse should get kate's review before the read/save/load flip slices proceed. See "Coordination constraints".

## The flip pattern being mirrored (param tier, already flipped)

`DocAutoParam` value type → stored in `ClipParams` (`fast_map<serialId, …>`) + a transient `session_clip_params`
twin → resolved by `Song::resolveParam(ParamAddress)` (`is_session ? session… : …`) → engine reaches it via
`doc()` (`addressFor` → `resolveParam`) → writes go to `doc()` (DOM-authoritative), engine field is an RT cursor →
save reads the store (`toDoc…FromStore`) → load relocates+binds+mints session ids.

Clip structure (`loopLength`, `section`, `colourOffset`, `launchStyle`) is 100% engine-owned; `ClipAddress` +
`clipAddressFor` (built in M2d) already provide the addressing.

## The full owner-1 flip roadmap (slices)

1. **Shadow store + dual-write (THIS SLICE).** `DocClip{loop_length}` + `ClipStructure` store + `resolveClip` +
   dual-write `loop_length` from `SetClipLength`. Additive; engine stays authoritative. Prove: `DocClip.loop_length ==
   engine loopLength` after every length edit.
2. **Grow the value type + funnel the other fields.** Add `section`/`colour_offset`/`launch_style` to `DocClip`;
   introduce `SetClipSection`/`SetClipColour`/`SetClipLaunchStyle` Edits (none exist today) and dual-write them.
3. **Read flip — the expensive one.** `loopLength` has **~330 raw `clip->loopLength` reads** (playback, rendering,
   every view), unlike params which route through `doc()`. Options: (a) keep the engine field as a bound write-through
   mirror indefinitely (like the param `currentValue` RT cursor) — cheap, never fully "retires"; (b) introduce a
   `clip->loopLength()` accessor reading `DocClip` and migrate 330 sites — large. Recommend (a) initially.
4. **Save flip.** `Clip::writeDataToFile` reads the `DocClip` store (a `toDocClipFromStore`-style projection) instead
   of the engine field. Gated on the golden round-trip harness + hardware.
5. **Load flip.** Post-load relocation (alongside the `clip_params` loop in `Song::readFromFile`) seeds the `DocClip`
   store from the file, reusing the clip's already-minted session id.
6. **Retire** the engine field where feasible (bounded by the read-flip choice).

## Slice 1 components

### `DocClip` — `src/deluge-project/doc_clip.h`
```cpp
namespace deluge::project {
/// The DOM-owned structural record for one clip (M3 owner-1). Slice 1 owns only loop_length (the only clip-structure
/// field with an Edit funnel today); section/colour/launch_style join as their Edits land. Keyed by the clip's stable
/// serialId (or a transient sessionId) in ClipStructure — never by Clip* or index.
struct DocClip {
	int32_t loop_length{0};
};
}
```

### `ClipStructure` — `src/deluge-project/clip_structure.h`
Exact mirror of `ClipParams`: `fast_map<uint32_t, DocClip> by_serial_id;` + `find`/`getOrCreate`/`remove`.

### `Song` additions — `song.h`/`song.cpp`
```cpp
ClipStructure clip_structure;          // per-clip structural records, keyed by Clip::serialId
ClipStructure session_clip_structure;  // twin for no-serialId clips, keyed by session id (rebuilt per load)

DocClip* resolveClip(const ClipAddress& a);          // find (nullptr if absent) — mirrors resolveParam CLIP branch
DocClip& getOrCreateDocClip(const ClipAddress& a);   // getOrCreate — the dual-write target
```
```cpp
DocClip* Song::resolveClip(const ClipAddress& a) {
	return a.is_session ? session_clip_structure.find(a.clip_id) : clip_structure.find(a.clip_id);
}
DocClip& Song::getOrCreateDocClip(const ClipAddress& a) {
	return a.is_session ? session_clip_structure.getOrCreate(a.clip_id) : clip_structure.getOrCreate(a.clip_id);
}
```

### Dual-write — `set_clip_length.cpp`
```cpp
Error SetClipLength::apply(Context& ctx) {
	ctx.song.setClipLength(clip_, newLength_, action_, mayReSyncClip_);      // engine (authoritative, as now)
	// M3 owner-1 slice 1: additive shadow write into the DOM clip-structure store. Nothing reads it yet; the engine
	// field stays authoritative until the read/save/load flip. Only shadow addressable clips — NEVER mint a session
	// id here (that would fork the shared id lifecycle); piggyback on the id the param tier already minted.
	if (auto address = deluge::edits::clipAddressFor(*clip_)) {
		ctx.model.getOrCreateDocClip(*address).loop_length = static_cast<int32_t>(newLength_);
	}
	return Error::NONE;
}
```

## Testing — dual-write consistency

Extend `edit_undo_redo`'s `clipLengthTarget`: after `apply(SetClipLength)`, assert
`song->project.resolveClip(*clipAddressFor(*clip))->loop_length == newLength` (the shadow matches the engine). Also
after undo, the engine `loopLength` restores — the shadow is intentionally NOT reverted (nothing reads it; undo of the
shadow is part of the later flip). Add a note so the asymmetry is explicit.

## Coordination constraints (for kate's review)

1. **Shared session-id lifecycle — mint-once.** `sessionId` is minted by the param relocation (`relocateAndResolve`).
   Slice 1 **never mints** — it only shadow-writes clips already addressable (serial or session id), so it is
   idempotent with the param tier. The load-flip slice (5) must reuse the *same* minted id, not a second one.
2. **`is_session` numeric-space collision.** `resolveClip` honors `is_session` exactly like `resolveParam`'s CLIP
   branch (session and serial share a numeric space).
3. **Keying.** `ClipStructure` mirrors `ClipParams`' `fast_map<serialId, …>` — consistent with the param stores, not
   the O(n) `clipForId`.
4. **`DocClip` growth + save/load** (slices 4–5) are the parts that most need kate's design sign-off (serialization
   shape, relocation placement).

## Gates (slice 1)

edit_undo_redo OK (+ shadow assertion), edit_test OK, **golden songs OK=15** (Song gains non-serialized members — must
not perturb serialization), param_record OK=3, firmware clean. Catalog: add an M3 owner-1 row (slice 1 shadow).

## Out of scope (slice 1)

Read flip, save flip, load flip, field retirement; `section`/`colour`/`launchStyle` Edits + dual-write; undo of the
shadow store; any session-id minting.
