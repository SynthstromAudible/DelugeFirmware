# M4b — the SoundSnapshot: the engine's first derived read-view of hot Sound fields

**Status:** design (for review)
**Branch:** `deluge-project-param`
**Date:** 2026-07-12
**Follows:** M4a (`docs/dev/m4a_snapshot_measurement.md`) — the param-write commit-flip is complete; M4b is the
read-side counterpart for hot Sound fields.

## Where this sits

M3 made the `deluge::project` DOM model the authoritative WRITER of persistent Sound/instrument state (save
serializes from `DocSound`/`DocSource`/`DocModControllable`). For fields read on the per-render / per-voice audio
path, the engine kept a NATIVE cache: the model is save-authoritative, but a `Doc*` record is re-seeded into a
scattered native `Sound` field at cold hooks (`Sound::doneReadingFromFile()`, `bindDocSound`) and dual-written on
every menu-edit, and the RENDER reads the native field, not the model.

M4b introduces the engine's **first derived read-view** — `SoundSnapshot` — the north-star's "engine plays derived
Snapshots" made real for Sound. It GATHERS today's scattered native caches into one cohesive snapshot, PROJECTED
from the model at the existing reseed hooks; the render reads the snapshot; the per-edit dual-write is eliminated
(the setter writes only the Doc, then reprojects — the snapshot becomes a *derivation*, not an independent write).

M4a's `ParamSnapshot` is a param-WRITE protocol (incremental model patch), not a read-view. `SoundSnapshot` is the
first read-view; it establishes the abstraction M4c (notes) extends.

## The field map (from reconnaissance)

**Group A — flat scalar caches → live verbatim in the snapshot** (Doc stores the value bit-for-bit; read
per-voice-start / per-render-block, never per-sample):
`synthMode` (`sound.h:157`↔`doc_sound.h:42`), `transpose` (`:139`↔`:43`, read `voice.cpp:227`), `oscillatorSync`
(`:164`↔`:37`, read `sound.cpp:2195`), `voicePriority` (`:169`↔`:32`, read `voice.cpp:2574`),
`modulatorTranspose[2]` (`:149`↔`:48`, read `voice.cpp:529`), `modulator1ToModulator0` (`:158`↔`:47`),
`oscRetriggerPhase[2]` (`:201`↔`:50`, read `voice_unison_part_source.cpp:79`), `modulatorRetriggerPhase[2]`
(`:202`↔`:51`), and the `lfoConfig[LFO_COUNT]` triplet (`sound.h:115` ↔ `doc_sound.h:54-56`; read `voice.cpp:149`,
`sound.cpp:2467`). Reseed today: `Sound::seedDocSoundScalars()` (`sound.cpp:3895`) + direct lfo seed (`:3918`).

**Group B — computed caches → the computed object lives IN the snapshot, derived at projection** (Doc stores the
*source scalars*; the native cache is a precomputed `PhaseIncrementFineTuner`):
`unisonDetuners[kMaxNumVoicesUnison]` (`sound.h:154`, from `num_unison`/`unison_detune`/`unison_stereo_spread`, read
`voice.cpp:515,559`; derive `setupUnisonDetuners()` `sound.cpp:3066`) and `modulatorTransposers[kNumModulators]`
(`sound.h:152`, from `modulator_cents[2]`, read `voice.cpp:549`; derive `recalculateModulatorTransposer()`
`sound.cpp:3182`). The `LFO` runtime objects (`globalLFO1/3`, per-voice `lfo2/4`) hold only phase state — NOT
persistent, NOT in the snapshot, stay engine-owned.

**Group C — OUT of scope** (Doc is a save-projection, not a render source): `Source::dxPatch` (`DxPatch*`, rich
runtime object read per-sample) and `Source::ranges` (`MultiRangeArray` — holds *loaded* `SampleHolder`/`WaveTable`
engine resources that cannot live in the portable `DocRange`). These stay engine-owned; M4b does not touch them.

**Reclassify:** `compressor.threshold` is param-derived-every-render (`mod_controllable_audio.cpp:651-657`), not a
reseed cache; its `DocModControllable::compressor.threshold` shadow is vestigial (seeded, never save-authoritative).
M4b leaves threshold as a live param-tier read and (optionally) notes the shadow for removal — it is NOT a snapshot
field.

**`DocModControllable` scalars** (`lpf_mode`/`hpf_mode`/`filter_route`/`mod_fx_type`/`clipping_amount` + the
`delay`/`sidechain`/`stutter` sub-structs + the 5 non-threshold compressor knobs) live on `ModControllableAudio`,
the base shared by `Sound` AND `GlobalEffectable` — a DIFFERENT owner than `DocSound`. They get a SEPARATE
`ModControllableSnapshot` (same pattern, its own owner), not folded into `SoundSnapshot`.

## Design

### 1. `SoundSnapshot` (engine-side read-view)
`src/deluge/model/snapshot/sound_snapshot.h` — a plain struct owning the hot Sound render fields (the Group A
scalars by value + the Group B `PhaseIncrementFineTuner` arrays). It depends on `deluge-project` types only for the
projection input; it is engine-owned (the model never references it). `Sound` holds one `SoundSnapshot snapshot_` in
place of the scattered native members those fields occupy today.

### 2. `Sound::projectSnapshot()` (the single projection)
Replaces the scattered per-field reseeds (`seedDocSoundScalars`, `setupUnisonDetuners`, `recalculateModulatorTransposer`,
the lfo seed). Re-derives the WHOLE `snapshot_` from `docSound_`: copies the Group A scalars verbatim; runs the
`PhaseIncrementFineTuner` setup for Group B from the Doc's source scalars. Called at `bindDocSound` (`sound.cpp:3936`),
`doneReadingFromFile` (`:3029`), and after each edit. A per-field `projectSnapshotX()` (or a small set) may be used at
setters to avoid re-deriving everything on a single-scalar edit — decided per slice (a whole-snapshot reproject is
cheap enough for cold edits; unison edits already re-run only `setupUnisonDetuners`).

### 3. The render reads the snapshot
Every hot read site (Group A + B, cited above) changes `sound.field` / native member → `snapshot_.field` (via a
getter, e.g. `getSynthMode()` returns `snapshot_.synthMode`). The scattered native members are RETIRED (they move
into the snapshot). `Voice::render` reaches the snapshot through its `Sound*` (`sound->snapshot_` / a getter); no new
plumbing — the Voice already holds the Sound.

### 4. Unbound sounds write the snapshot directly (no fallback branch)
Preview sounds and `GlobalEffectable` owners have no `DocSound`. The snapshot ALWAYS exists on the object, so the
render reads it unconditionally — **no `docSound_ ? … : native` ternary** (cleaner than the owner-2 note-scalar
fallback). Edit routing:
- **bound:** setter writes `docSound_->field` (save-authoritative), then reprojects the snapshot field from the Doc
  — a derivation, NOT an independent write (this is the dual-write elimination).
- **unbound:** setter writes `snapshot_.field` directly (no Doc; these sounds are transient / never saved). This is
  the only place the snapshot is written outside projection, and it is confined to the never-saved case.
Load fills the Doc (bound) or the snapshot (unbound preview) as today; `projectSnapshot()` runs post-load for bound.

### 5. `ModControllableSnapshot` (separate slice, own owner)
Same pattern on `ModControllableAudio`: a `ModControllableSnapshot` gathering the `DocModControllable` scalar caches,
`projectModControllableSnapshot()` from `docModControllable_` at `bindDocModControllable` + `doneReadingFromFile`,
render reads the snapshot. Shared by Sound + GlobalEffectable (both carry `docModControllable_`). Threshold excluded
(param-derived).

## Decomposition (slices — each bisectable, golden-gated bit-identical)

1. **Pilot** — create `SoundSnapshot` + `Sound::projectSnapshot()` + flip ONE simple Group A scalar (`transpose` —
   a single read site at `voice.cpp:227`, per-voice-start) end-to-end (snapshot member, projection, the read site,
   setter reprojects, native member retired, unbound direct write). Fewest read sites, so it proves the pattern +
   the unbound handling with minimal churn before the higher-read-count fields.
2. **Group A scalars** — the remaining flat scalars (synthMode — the many-branch one, oscillatorSync, voicePriority,
   modulatorTranspose[], modulator1ToModulator0, retrigger phases), one field or a small batch per commit.
3. **`lfoConfig[]`** — the LFO config triplet (cheap flip, but many read sites; its own slice).
4. **Group B computed** — `unisonDetuners[]` + `modulatorTransposers[]` (the `PhaseIncrementFineTuner`s into the
   snapshot; projection runs the setup; this is the "genuine derived cache" proof).
5. **`ModControllableSnapshot`** — the `DocModControllable` scalar caches on `ModControllableAudio` (own type, own
   projection), Sound + GlobalEffectable.

(Group C untouched; threshold shadow noted, optional cleanup.)

## Testing / gates (every slice, x86≡ARM)

- **Goldens BIT-IDENTICAL:** song 16 / json 38 / edit 40 / param 3 / audio_clip 1; firmware links. **`param_record`
  is load-bearing** (it renders — a wrong snapshot read surfaces there); `song_roundtrip` carries save/load of the
  flipped fields. The synth golden-master audio harness (if run) is the deepest render check.
- **Behaviour-preserving:** the snapshot holds the same values the native cache did, projected at the same hooks —
  the render output is identical. Retiring a native member is a compile-checked move (misses become build errors).
- **Unbound sanity:** a preview/GlobalEffectable path (no Doc) must still render — covered by the golden song render
  (kits with affect-entire GlobalEffectable) + any preview-audition fixture.

## Risks / open questions

- **Unbound = direct snapshot write is a second write path.** It is confined to never-saved sounds (no Doc to
  reconcile), so it cannot desync anything persistent. But the setter grows a bound/unbound branch; keep it in ONE
  place (the setter), not scattered.
- **Projection granularity at setters.** A whole-snapshot reproject on every scalar edit is simplest and cold
  (edits aren't RT); if any edit path proves hot, add a per-field reproject. Start whole-snapshot, measure only if a
  gate flags it.
- **`Voice` caching.** Some hot reads happen at note-on and are cached on the `Voice` for the note's life
  (unisonDetuners feed `calculatePhaseIncrements` once). The read-flip changes only the SOURCE of that note-on read
  (snapshot vs native); the Voice's own caching is unchanged. Verify no read site reads the native field AFTER a
  mid-note reproject expecting the old value (edits mid-note already reproject the native cache today, so semantics
  are preserved).
- **`snapshot_` size / placement.** Gathering the fields into one struct changes `Sound`'s layout but not its size
  materially (same fields, relocated). The golden gate's pointer-independence guard covers layout sensitivity.
- **Not a full seal.** M4b makes the render read a derived snapshot but does NOT yet make the snapshot a separately
  projected buffer (per-executor-unit) or seal the whole audio thread — those are the granularity choices M4a
  measured, deferred until the read-view exists across owners (M4b/c/d). SoundSnapshot is a per-Sound engine member
  here, the simplest first form.

## Exit criteria

- `SoundSnapshot` exists (engine-side, model-read-only-input); `Sound::projectSnapshot()` derives it from `docSound_`
  at the reseed hooks; the render reads the snapshot for all Group A + Group B fields; the scattered native members
  are retired; the per-edit dual-write is gone for bound sounds (setter writes Doc + reprojects); unbound sounds
  write the snapshot directly with no render-side fallback branch.
- `ModControllableSnapshot` does the same for the `DocModControllable` scalar caches.
- Group C (dxPatch, ranges) untouched; threshold left param-tier.
- Goldens bit-identical (x86≡ARM), firmware links, per slice; `param_record` green.

## References
- North star: `docs/dev/project_model_north_star.md` (§ Snapshot); ladder: `docs/dev/project_model_migration_plan.md` (§ M4)
- Reconnaissance map: this session (field ↔ native ↔ Doc ↔ read-site ↔ reseed table)
- M4a read-view precedent: `src/deluge/model/snapshot/param_snapshot.{h,cpp}` (write protocol, not a read-view)
- Sound / Doc binding: `sound.h:84` (`docSound_`), `sound.cpp:3936` (`bindDocSound`), `:3029` (`doneReadingFromFile`)
