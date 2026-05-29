# Harmonic Composition — Long-Term Architecture

> **Status:** Phase 5 design. No code, no implementation. Defines the abstractions so that
> **Chord Commit (Phase 4) is the first vertical slice of the final system**, not a throwaway proof.
> **Depends on:** `ARCHITECTURE_MAP.md`, `CHORD_PALETTE_FEASIBILITY.md`, `CHORD_COMMIT_POC_PLAN.md`.
> **Guiding tension:** preserve future flexibility *while keeping the Phase 1 footprint tiny.*

---

## 0. Design principles (and an honest anti-over-engineering stance)

1. **One spine, many plug-ins.** Every feature (commit, palette, packs, favorites, progressions)
   is either a new *source* of chords or a new *placement* of chords. If both ends are seams, the
   middle never needs to change.
2. **Separate WHAT from WHERE.** A chord (what) is independent of where it came from and where it
   lands. This single separation is what makes all later features additive.
3. **Resolve once, reuse everywhere.** The "chord → actual MIDI notes" computation must live in
   exactly one place, shared by audition, preview, and commit. It already exists informally inside
   the ChordLibrary audition loop — Phase 1 *extracts and names it*, nothing more.
4. **Interfaces follow the second implementation.** To respect upstream review and the "minimize
   complexity" rule, Phase 1 introduces **named boundaries and value objects**, but only **one
   concrete implementation** behind each. A formal `virtual` interface is extracted when the *second*
   source/placement actually arrives — a mechanical, non-breaking refactor *because* the boundary
   was clean from day one. We design the seams; we don't pre-build empty abstractions.

The net effect: Phase 1 is mostly *renaming and relocating logic that already exists*, plus one
small new entry point. But every name it introduces is a load-bearing part of the final system.

---

## 1. What abstractions should exist?

Six concepts, layered. Only the **bold** ones are *new* in Phase 1; the rest already exist or arrive later.

| Layer | Concept | Exists today? | Role |
|---|---|---|---|
| Data | `Chord` / `Voicing` / `ChordList` | ✅ `chords.{h,cpp}` | Immutable interval definitions (built-in 33×4) |
| Source | `ChordSource` | later | Where browseable chords come from (library, memory, packs, …) |
| Selection | **`ChordSelection`** | **new (Phase 1)** | A fully-specified chord instance: *what to voice* |
| Service | **`ChordService`** (facade) | **new (Phase 1, thin)** | UI-agnostic owner of current selection + optional pending; exposes `resolve()` and `commit()` |
| Resolve | **`resolveChordNotes()`** | **new (Phase 1, extracted)** | Pure function `ChordSelection → NoteList` |
| Placement | `ChordPlacement` strategy | playhead in Phase 1; step later | Where/how the notes are written into the clip |
| Staging | `PendingChord` | later (Phase 2) | Optional "capture now, place later" buffer |

## 2. What object represents a selected chord?

**`ChordSelection`** — a small, copyable value object (POD-style, like the existing `Voicing`):

```
struct ChordSelection {            // illustrative shape, not committed code
    int8_t   chordTypeId;          // index into ChordList (built-in) OR a source-qualified id
    uint8_t  rootNote;             // absolute MIDI base note (e.g. 60 = C4)
    int8_t   voicingIndex;         // which of the chord's voicings (0..kUniqueVoicings-1)
    int8_t   octaveShift;          // optional ± octaves, default 0
    ChordSourceId source;          // which source produced it (default = BuiltinLibrary)
};
```

Why this shape:
- It is exactly the data the ChordLibrary layout *already tracks* in `KeyboardStateChordLibrary`
  (chord index = `chordRowOffset + y`, root = `noteOffset + x`, voicing = `voicingOffset[chordNo]`).
  Phase 1 doesn't invent new state — it *names the tuple already living in the layout.*
- It is **source-tagged** so chord memory, packs, and favorites can all produce the same object.
- It carries **no absolute note list** — it is the *intent*; notes are derived on demand by the
  resolver. This keeps it tiny, serializable, and stable across views.

`ChordSelection` is the single currency that flows through the entire system.

## 3. Should a Pending Chord concept exist?

**Yes — but not in Phase 1.** `PendingChord` is simply `optional<ChordSelection>` held at a scope
both views can reach (on `ChordService`). It is the bridge across the keyboard/clip root-UI divide
(the core tension from the feasibility docs): select in keyboard view → stage as pending → switch to
clip view → place at a step.

- **Phase 1 (Commit):** no pending needed — selection and placement happen in the same view/instant.
- **Phase 2 (Palette):** pending becomes the mechanism for "browse here, place there." It follows an
  existing precedent in the codebase: `CopiedNoteRow` (`copied_note_row.h:23`) and
  `CopiedParamAutomation` (`instrument_clip_view.h:236`) already stage data on a view for later paste.

Designing `ChordService` to *optionally* hold a pending selection now (even if Phase 1 never sets it)
means Phase 2 adds behavior, not structure.

## 4. How should placement be separated from chord selection?

Placement is a **strategy that consumes a resolved `NoteList` and a target**, with zero knowledge of
*which chord* it is or *where it came from*. This is the most important separation in the system,
and the codebase already forces it on us via the two distinct write paths:

| Placement strategy | Underlying call | Position source | Phase |
|---|---|---|---|
| `PlaceAtPlayhead` | `recordNoteOn()` (`instrument_clip.cpp:4390`) | `getLivePos()`, quantized | **1** |
| `PlaceAtStep` | `NoteRow::attemptNoteAdd(pos,…)` (via `editPadAction` pattern) | explicit grid `getPosFromSquare(x)` | 2 |
| `PlaceToPending` | (no clip write) | stages a `ChordSelection` | 2 |

The seam: **`ChordService::commit(const ChordSelection&, PlacementTarget)`**. The service resolves
the selection to notes, then hands the notes to the placement appropriate for the target. Selection
UI (ChordLibrary, future palette) never touches `recordNoteOn`/`attemptNoteAdd` directly — it only
expresses *intent* + *target*. That is what lets the same selection commit at a playhead today and a
step tomorrow with no change to the selection side.

## 5. How should future chord sources be integrated?

Every source produces `ChordSelection`s (or browseable entries that resolve to them). They share the
resolver and placement untouched. A `ChordSource` is "a thing you can browse and that yields a
selection":

| Source | Backing data (exists?) | Notes |
|---|---|---|
| **Built-in library** | `ChordList` / `Chord` (✅) | The Phase-1 default source |
| **Chord memory** | `chord_mem.{h,cpp}`, 8 slots, serialized (✅) | Stores absolute pitches → adapts to `ChordSelection` (or a raw NoteList variant) |
| **Custom chord packs** | `CHORD_PACKS/*.xml` (later) | Loaded lazily via the DX7-browser pattern; parsed into `Chord`-shaped entries |
| **User favorites** | new tiny store (later) | A pinned subset across sources; just stored `ChordSelection`s |
| **Progression templates** | new (later) | An *ordered sequence* of `ChordSelection`s; a sequencer over the same commit path |

Integration rule: a source's only obligation is to **emit `ChordSelection`** (and provide display
metadata: name, quality, short 4-char label for 7-seg). Two practicalities:
- **Chord memory stores absolute pitches, not intervals.** It joins via a `NoteList`-producing
  adapter or a `ChordSelection` variant flagged "literal notes" — the resolver short-circuits to the
  stored notes. This is the one place the *what* is pre-resolved; the seam absorbs it.
- **Progression templates are not a new placement** — they are a source that emits a *series*, driven
  through the same `commit()` once per chord. No generator, no AI; just sequenced selection.

## 6. What architecture lets Commit → Palette → Packs grow without refactoring?

Because each phase only **adds an implementation behind an existing seam**:

```
                      Phase 1: COMMIT        Phase 2: PALETTE          Phase 3: PACKS
                      ──────────────         ──────────────────        ──────────────
ChordSource           BuiltinLibrary  ──►    (+ ChordMemory adapter)   (+ ChordPackSource)   ← add sources
ChordSelection        (named tuple)          (unchanged)               (unchanged)           ← stable currency
resolveChordNotes()   extracted, pure   ──►  (unchanged)               (unchanged)           ← never changes
ChordService          commit(playhead)  ──►  (+ pending, + step)       (+ source switching)  ← extends, no rewrite
ChordPlacement        PlaceAtPlayhead   ──►  (+ PlaceAtStep/Pending)   (unchanged)           ← add strategies
Selection UI          ChordLibrary      ──►  (richer palette + cursor) (+ pack browser)      ← UI grows independently
```

The two columns that **never change** — `ChordSelection` and `resolveChordNotes()` — are the spine.
Everything else is additive: new rows in the Source list, new placement strategies, new UI. No phase
modifies a prior phase's core. That is the definition of "grows without refactoring."

The only refactor ever required is the **extract-interface** step (turn `PlaceAtPlayhead` into a
`ChordPlacement` interface when `PlaceAtStep` arrives, same for `ChordSource`). That is deliberately
deferred per principle #4 and is mechanical because the boundary is clean from Phase 1.

## 7. Which components should be reusable?

| Component | Reused by | Why it's the reuse point |
|---|---|---|
| **`ChordSelection`** | every source, every placement, audition, preview, undo, serialization | The universal currency |
| **`resolveChordNotes()`** | audition, commit, palette preview, progression playback | One voicing truth; prevents heard≠written drift |
| **`ChordService`** | `KeyboardScreen`, `InstrumentClipView`, future palette/pack UIs | UI-agnostic facade that crosses the keyboard/clip root-UI divide |
| **`ChordPlacement` strategies** | commit, palette, progression templates | Decouples "where" from "what" |
| **`ChordSource` (concept)** | library, memory, packs, favorites, progressions | Decouples "from where" from "what" |
| Existing `ChordList`/`Chord`/`Voicing` | resolver + library source | Already the canonical data; do not duplicate |
| Existing write path (`recordNoteOn`/`attemptNoteAdd`) | placement strategies | Reuse, never reimplement |
| Existing `display->displayPopup()` | all confirmations | Dual-display abstraction for free |

---

## Component diagram

```
┌──────────────────────────────────────────────────────────────────────────┐
│ UI LAYER (root UIs — display-specific, NOT reusable across features)       │
│                                                                            │
│   KeyboardScreen / ChordLibrary layout        InstrumentClipView           │
│   (browse + select + audition)                (placement target: cursor)   │
│            │  expresses intent                         │  expresses target │
└────────────┼───────────────────────────────────────────┼──────────────────┘
             │                                            │
             ▼                                            ▼
┌──────────────────────────────────────────────────────────────────────────┐
│ ChordService  (facade — UI-agnostic, the cross-view seam)   ★ reusable     │
│   • activeSelection : ChordSelection                                       │
│   • pending         : optional<ChordSelection>     (Phase 2)               │
│   • commit(selection, target) ──► resolve ──► place                        │
└───────┬───────────────────────────┬────────────────────────┬──────────────┘
        │                           │                         │
        ▼                           ▼                         ▼
┌───────────────┐         ┌───────────────────┐     ┌────────────────────────┐
│ ChordSource   │ emits   │ resolveChordNotes()│     │ ChordPlacement         │
│ (concept) ★   │ Sel ──► │  pure fn  ★        │ ──► │  strategy ★            │
│ • Library     │         │  Selection→NoteList│     │ • PlaceAtPlayhead (P1) │
│ • Memory      │         └─────────┬─────────┘      │ • PlaceAtStep    (P2)  │
│ • Packs (P3)  │                   │                │ • PlaceToPending (P2)  │
│ • Favorites   │                   │                └───────────┬────────────┘
│ • Progressions│                   ▼                            ▼
└───────┬───────┘         ┌───────────────────┐      ┌────────────────────────┐
        │ reads           │ Chord/Voicing/    │      │ InstrumentClip write    │
        └────────────────►│ ChordList (data)  │      │ recordNoteOn /          │
                          └───────────────────┘      │ attemptNoteAdd  (exists)│
                                                      └────────────┬────────────┘
                                                                   ▼
                                                          ┌──────────────────┐
                                                          │ Action / undo     │
                                                          │ (one grouped step)│
                                                          └──────────────────┘
   ★ = reusable spine component
```

## Data flow diagram

```
 SELECT                 RESOLVE                PLACE                 PERSIST
 ──────                 ───────                ─────                 ───────
 Source ──ChordSelection──► resolveChordNotes ──NoteList──► Placement ──► NoteRows in Clip
   │            │                  │              │            │              │
   │            │                  uses           │            chooses        registers
   │            │              Chord/Voicing      │          playhead|step    one Action
   │            │                                 │                            (undoable
   │            └── (Phase 2) may park as ─────────┘                            as a unit)
   │                PendingChord, replayed later
   │
   └── display metadata (name, 4-char label) ──► display->displayPopup (OLED + 7-seg)
```

The chord the user **hears** (audition runs the *same* `resolveChordNotes`) is byte-for-byte the
chord that gets **written** — there is no second voicing path to drift.

## State model

State of a chord moving through the service:

```
        ┌─────────┐  source selects / encoder moves   ┌──────────┐
        │  IDLE   │ ────────────────────────────────► │ SELECTED │ ◄─┐ adjust root/
        │(no sel) │                                    │ (active) │   │ voicing/chord
        └─────────┘ ◄──────────── clear ───────────────└────┬─────┘ ──┘ (re-resolve,
             ▲                                              │            re-audition)
             │                              ┌───────────────┴───────────────┐
             │                              │                               │
             │                       commit(target=                  commit(target=
             │                        Playhead|Step)                   Pending)         [Phase 2]
             │                              │                               │
             │                              ▼                               ▼
             │                        ┌───────────┐                   ┌───────────┐
             └──── (auto) ────────────│ COMMITTED │                   │  PENDING  │
                                      │ (in clip, │                   │ (staged,  │
                                      │ undoable) │                   │ cross-view)│
                                      └───────────┘                   └─────┬─────┘
                                                                            │ place in clip view
                                                                            ▼
                                                                      ┌───────────┐
                                                                      │ COMMITTED │
                                                                      └───────────┘
```

- **Phase 1 uses only:** IDLE → SELECTED → COMMITTED (target = Playhead). The PENDING branch is
  designed-in but dormant.
- Undo returns a COMMITTED chord's effect to the prior clip state in a single step (the grouped
  `RECORD` action); the *selection* itself is unaffected, so the user can re-commit.

---

## Recommended architecture (and the minimal Phase 1 footprint)

**Adopt the spine; implement the thinnest slice.** Concretely, Phase 1 (Chord Commit) introduces
only:

1. `ChordSelection` — a value struct wrapping the tuple ChordLibrary *already* tracks.
2. `resolveChordNotes(ChordSelection) → NoteList` — **extracted** from the existing audition loop
   (`chord_library.cpp:48–54`), so audition and commit share it immediately.
3. A thin `ChordService::commit(selection, PlacementTarget::Playhead)` that runs the known-good
   write loop (`getOrCreateNoteRowForYNote` → `recordNoteOn`, one grouped action) from
   `CHORD_COMMIT_POC_PLAN.md`.
4. The commit gesture wired in `KeyboardScreen` (SHIFT + select-encoder), per the PoC plan.

That is *it* — small enough to be the PoC, but every artifact is a permanent spine component. No
`ChordSource` interface, no `PlaceAtStep`, no `PendingChord` is *built* yet; their **boundaries are
respected** so they slot in later as additions.

**Deferred deliberately (YAGNI, extract when the 2nd implementation lands):**
- `ChordSource` as a formal interface — defer until chord memory / packs need it.
- `ChordPlacement` as a formal interface — defer until `PlaceAtStep` (Palette) lands.
- `PendingChord` behavior — structure-ready on `ChordService`, behavior added in Palette.

### Why this satisfies the brief
- **Future flexibility:** the WHAT/WHERE/FROM-WHERE separations mean every roadmap feature is purely
  additive (§6 table).
- **Small Phase 1:** the only genuinely new code is a struct, one extracted function, a thin facade
  method, and a gesture — most of it relocating logic that already exists.
- **No throwaway:** Chord Commit *is* `ChordService.commit()` with the built-in source and the
  playhead placement — i.e. the final system, exercised end-to-end on its narrowest path.

---

## Risks / cautions

- **Over-abstraction vs upstream taste.** Don't ship empty interfaces. Ship the value object + pure
  function + thin facade; let interfaces emerge. (Principle #4.) Flagged so a reviewer sees the
  restraint is intentional.
- **Chord memory's literal-notes shape** is the one impedance mismatch in the source model; resolve
  it with an adapter, not by bending `ChordSelection`.
- **Cross-view facade ownership:** decide where `ChordService` lives (a singleton like other managers,
  or hung off `Song`). It must outlive a root-UI switch to carry a `PendingChord` — pick a scope
  above the views.
- **Resolver purity:** keep `resolveChordNotes` free of UI/audition side effects so it is safe to
  call from preview, commit, and tests alike.

*No code is implemented here. With this spine accepted, the Phase 4 PoC can be built as `ChordService.commit()` — the first real slice — on your go.*
