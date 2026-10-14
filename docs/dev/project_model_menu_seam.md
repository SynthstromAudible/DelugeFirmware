# Menu ↔ model access seam (owner-4/5 retirement prerequisite)

Part of the project-model migration (see `project_model_north_star.md`,
`project_model_migration_plan.md`). This note fixes the *shape* by which the SoundEditor
menus reach a persistent descriptor field, so those fields can be made model-authoritative
and their native engine copies retired.

## The problem it solves

The SoundEditor edits persistent fields (voice priority, sample controls, attack, …) that
live on `AudioClip`, `Sound`, and `Drum`. Today the menus reach those fields two ways, and
**both hand the menu a raw mutable reference to the native engine field**:

- **Bound pointers** — `sound_editor.h` holds `current*` pointers (`currentSampleControls`,
  `currentPriority`, …) assigned on menu-entry in `SoundEditor::setup()`; a menu does
  `*soundEditor.currentPriority = v`.
- **Resolve-at-use helpers returning `T&`** — e.g. `getCurrentSampleControls(whichThing)`
  (`menu_item/sample/utils.cpp`) returns `SampleControls&`, branching audio-clip-vs-sound.

A returned `T&` (or `T*`) is fine while the native field is authoritative. It is fatal to
retirement: once the truth moves into the `Doc*` record, a menu holding a reference to the
native field has **no seam** to (a) write the Doc instead, (b) re-derive a hot render cache,
or (c) later reify the write as an `edits::Edit`. You cannot delete a field that 100+ menu
files hold a live reference into.

## The shape (approach C)

**Route every persistent-field access through a get/set accessor on the owning class; never
expose a mutable reference to the native field.**

1. **Accessor pair on the owning class**, forwarding to the bound `Doc*` — the exact template
   already proven on owners 1–3 (`Clip`/`NoteRow`/`Drum`):

   ```cpp
   // AudioClip
   VoicePriority voicePriority() const { return static_cast<VoicePriority>(docAudioClip_->voice_priority); }
   void setVoicePriority(VoicePriority v) { docAudioClip_->voice_priority = static_cast<uint8_t>(v); }
   ```

   The native field is renamed away / deleted; the accessor is the single routing point,
   reused by **all four consumers** — load, save, render-derive, and menu-edit — so there is
   no parallel menu-side mirror to keep in sync. It is also the choke point where the
   write-funnel milestone later drops `emitEdit(...)`.

2. **Menus that hold the object** call the accessor directly:
   `getCurrentAudioClip()->setVoicePriority(v)`.

3. **Genuinely-polymorphic menus** (the handful that serve both audio-clip and sound owners —
   today only *priority* and the *sample-controls* cluster) use a thin **get/set-by-value**
   resolver that dispatches to the per-class accessor. It **must not** return a `T&`:

   ```cpp
   VoicePriority getCurrentVoicePriority();          // audioclip? sound? drum?
   void setCurrentVoicePriority(VoicePriority v);     // routes to the owner's accessor
   ```

   This is the only place the `getCurrentSampleControls` idiom survives, and it is by-value.

## Why not the alternatives

- **Central `EditTarget` object** (one bound abstraction mirroring every field): premature
  centralization. It front-loads a god-object-shaped mirror before the pattern is proven, it
  only sees *menu* writes (load/consequence/gesture writes bypass it) so it cannot be the
  write-funnel, and it duplicates the routing the per-class accessor already owns. Keep it as
  a graduation path *only* if the by-value resolver surface ever grows past comfort — the
  Explore map shows just 2 of 12 `current*` pointers are dual-owner, so it should not.
- **Per-field free-function pairs everywhere** (extend `getCurrentSampleControls` for every
  field): a second routing layer that drifts from the load/save path and repeats the
  owner-branch per field. C uses this idiom *only* for the 2 dual-owner menus.

## Migration order (each slice behavior-preserving, golden bit-identical)

1. **Delete dead `currentSampleControls`** — write-only, zero reads; drop field + 3 binds.
2. **Seam `priority`** — migrate `menu_item/voice/priority.h` off `currentPriority` to a
   by-value resolver; delete `currentPriority` + its 2 binds. No retirement yet.
3. **Retire per field, owner by owner** — flip the owning class's accessor to the `Doc*`,
   delete the native field, redirect the ≤5 engine touch-sites. Owner-4 (`AudioClip`) first
   (`voicePriority`, `attack`, then the `sampleControls` hot group), then owner-5
   (`Sound`/`ModControllableAudio`) reuses the identical recipe.

## Owner-4 outcome (as of this pass)

- **Retired cold scalars** — `voicePriority`, `attack`: native field deleted, accessor reads
  the record, save reads the record. No cache.
- **DOM-owned hot group with derived cache** — `sampleControls` (interpolation /
  pitch-speed-independence / reversed): record authoritative, `sampleControls` kept as the
  render cache, writes funnel through `AudioClip::set*` (+ the `sample/utils` by-value
  dispatchers for the shared source/clip menus), save reads the record.
- **Deferred, still engine-authoritative** (`syncDocAudioClip` retained as the residual
  engine→record sync for exactly these):
  - **`sampleHolder` group** (`file_path` / `start_pos` / `end_pos` / `transpose` / `cents`).
    Blocked, not skipped: the sample window is authored by `SampleHolder::loadFile` at load
    time and **reverted raw by two undo Consequences** — `ConsequenceClipLength` writes
    `sampleHolder.endPos`/`startPos` through a stored `uint64_t*`
    (`audio_clip_view.cpp` sets `pointerToMarkerValue = &clip.sampleHolder.endPos`), and
    `ConsequenceAudioClipSetSample::revert` writes `filePath`/`endPos` then `loadFile()`s.
    A retired/cache field written by a raw-pointer undo silently desyncs from the record.
    Inverting it means teaching the generic Consequence/undo machinery about the model — the
    edit→model+undo funnel that **M4** generalizes — not an owner-4 setter. Do it with M4.
  - **`overdubsShouldCloneOutput`** — a polymorphic `Clip`-base field (`clip.h`), read/written
    across session/view/playback/song; belongs to a `DocClip`-ownership slice, not owner-4.

So `syncDocAudioClip` shrinks to (sampleHolder + overdubs) and cannot be fully deleted until
those two are done; the save path already reads every *inverted* field straight from the record.

## Invariant (the exit bar)

`grep` for a raw mutable reference to any retired persistent field (`->field =`, `&obj->field`)
returns zero outside the owning class; every menu reaches it through the accessor or the
by-value resolver.
