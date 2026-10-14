# Sequencer clone-idiom retirement (byte-copy → real C++ copy/clone) — Design Spec

**Date:** 2026-07-02
**Status:** Approved (brainstorm + investigation), pending implementation plan
**Relationship:** Prerequisite, standalone effort split out of
`2026-07-02-dom-authoritative-automation-single-store-design.md`. The DOM-automation store work
(single-copy unification, borrow pointers — that spec's Phases B+) is **deferred until this lands.**

---

## 1. Context & goal

The automation node store stays `deluge::OrderedPosVector<ParamNode>` (no facade, no alias — decided in
the DOM-automation brainstorm). But that container carries a **legacy byte-copy clone idiom** —
`init()` / `cloneFrom()` / `beenCloned()` — that is the one genuinely non-idiomatic wart, and it is
shared by the whole sequencer clone machinery. This effort **retires that idiom into proper C++
copy/clone semantics** across all `OrderedPosVector` users and the `ParamManager`/`ParamCollection`
clone chain, and does a light idiomatic pass on `OrderedPosVector` itself. It is done **first**, so the
subsequent DOM automation work builds on a clean, modern clone story.

**The idiom, precisely** (`src/deluge/util/container/ordered_pos_vector.h`): copy ctor/assign are
`= delete`d; `init()` (line 51) placement-news a fresh empty vector over the member *without freeing*
(detach from an aliased buffer after the owner was `memcpy`'d); `beenCloned()` (197–213) allocates a
fresh buffer and **per-element `memcpy`s** elements into it; `cloneFrom()` (182–189) is an explicit
`elements_ = other.elements_` deep copy. Copy is deleted specifically so byte-copy and real copy can't
be mixed.

## 2. Investigation findings (the map this plan is built on)

**Growth is already move-based; the memcpy is only in explicit clone.** `NoteRow`, `AutoParam` are
move-constructible (`= default` noexcept) with copy deleted, so `fast_vector` growth **move-constructs**
elements (buffers transfer correctly). The byte-copy idiom is used only for explicit *cloning*
(duplicate a clip / kit row / param, and ParamManager backup), never for vector growth.

**The four byte-copy sites:**
1. `model/note/note_row_vector.cpp:55` — `NoteRowVector::cloneFrom` memcpys each `NoteRow`.
2. `modulation/params/param_manager.cpp:217` — **polymorphic** `memcpy(dst, src, objectSize)` of a
   base-`ParamCollection*` (vtable and all), then virtual `beenCloned()`. **This is the keystone** —
   retiring it requires a virtual `clone()` across the `ParamCollection` hierarchy.
3. `util/container/ordered_pos_vector.h:209` — per-element `memcpy` in `beenCloned()`. Dangerous for
   non-trivially-copyable `T` (see `MIDIParam`).
4. `modulation/patch/patch_cable_set.cpp:789` — `Destination` structs shallow-copied (`*to = *from`)
   after allocating fresh `destinations[]` arrays.

**The recursive clone chain** (clip duplicate): `InstrumentClip` clone →
`NoteRowVector::cloneFrom` (memcpy NoteRow) → `NoteRow::beenCloned` → `paramManager.beenCloned()` →
`ParamManager::cloneParamCollectionsFrom` (memcpy each collection @217) → each collection's
`beenCloned()` → `AutoParam::beenCloned` → `nodes().beenCloned()` (per-element memcpy of ParamNodes).

**The undo path is already clean.** `ConsequenceParamChange` and `ConsequenceNoteArrayChange` use
`swap()` + `cloneFrom()`, not the byte-copy idiom. So "modernize undo" is largely already done; the
work is in the clone/duplicate path.

**Element trivial-copyability:** `ParamNode`, `Note`, `ClipInstance` are trivially copyable (safe
memcpy). **`MIDIParam` is NOT** — it owns an `AutoParam` by value (copy deleted); it is memcpy'd anyway
and relies on the subsequent `beenCloned()`. This is the fragile spot a real copy ctor must fix.

**Reversal complexity:** `AutoParam::beenCloned` and `NoteRow::beenCloned` have a
`reverseDirectionWithLength > 0` branch that clones-then-reverses element-by-element. This is a *reverse
clone*, a distinct operation from a plain copy — it stays a dedicated method, not folded into `operator=`.

## 3. Design

**Real copy semantics, bottom-up.** Give each owning type a proper copy constructor / clone that
deep-copies its members, replacing the memcpy+`beenCloned` pair:

- `OrderedPosVector<T>` gains a real copy constructor (element-wise copy construction, not memcpy) —
  valid for trivially- and non-trivially-copyable `T` alike. `init`/`cloneFrom`/`beenCloned` are
  removed once all callers are converted.
- `AutoParam` gains a copy ctor that deep-copies its `OrderedPosVector<ParamNode>`. The reverse-clone
  stays a named method.
- `MIDIParam` becomes properly copyable (its embedded `AutoParam` now copies).
- `NoteRow` gains a copy ctor deep-copying its `ParamManagerForTimeline` + `NoteVector` (reverse-clone
  stays a named method).
- **Keystone:** `ParamCollection` gets a pure virtual `clone()`; `ParamSet` (and its `Unpatched`/
  `Patched`/`Expression` subclasses), `MIDIParamCollection`, `PatchCableSet` implement it (deep-copying
  their `AutoParam`s / vectors / `Destination` arrays). `ParamManager::cloneParamCollectionsFrom`
  replaces the polymorphic `memcpy` (@217) with `src->clone()` virtual dispatch.
- `NoteRowVector::cloneFrom` replaces its memcpy (@55) with element-wise copy construction.
- `PatchCableSet` `Destination` deep-copy is audited for heap ownership and made a real copy.

Fallible allocation stays fallible: clone factories that can OOM return `std::expected<…, Error>`
(matching the existing `Error`-returning `beenCloned` contract), not throwing across the RT boundary.

## 4. Test strategy — the golden cannot see this

**The clone path is golden-blind.** The golden harness does load→save and never clones/duplicates/undoes,
so it will **not** catch a clone-semantics regression. Therefore:

- **Build a clone/undo sim test harness first** (task A0), before touching the idiom. It must: clone an
  `InstrumentClip` / `ParamManager` carrying automation + patch cables + MIDI params; duplicate a
  `NoteRow`; run undo/redo; and assert **deep-copy independence** — mutating the clone leaves the
  original unchanged, and freeing one doesn't corrupt the other. This is the real safety net.
- **The golden still runs** at every task boundary as a regression guard for the load/save path it
  *does* cover (`OK=15 REGRESS=0`): the refactor must not perturb serialization.
- **`param_projection_test`** continues to pass.
- **Hardware gate (mandatory):** clip duplicate, kit-row add/remove, undo/redo across clones,
  save→reload — the behaviours only hardware + the new harness can confirm.

## 5. Phasing

- **A0 — clone/undo test harness** (the golden-blind safety net; gates everything after).
- **A1 — light idiomatic pass** on `OrderedPosVector` (`requires`-constraint on `keyMember`,
  `std::span`/range accessors, `[[gnu::restrict]]` over `__restrict__`, `[[nodiscard]]`/doc
  completeness). Additive, low-risk.
- **A2 — bottom-up copy semantics:** `OrderedPosVector` real copy ctor → `AutoParam` copy ctor →
  `MIDIParam` → `NoteRow` copy ctor. Each converts its own `init/beenCloned/cloneFrom` usage; harness +
  golden green at each step.
- **A3 — the keystone:** virtual `ParamCollection::clone()` across the hierarchy; replace the
  polymorphic `memcpy` (`param_manager.cpp:217`) and `NoteRowVector::cloneFrom` memcpy with clone/copy
  dispatch.
- **A4 — remove** `init`/`cloneFrom`/`beenCloned` from `OrderedPosVector` and every override; audit +
  fix `PatchCableSet` `Destination` deep-copy.

## 6. Risks

- **Golden-blind** (§4): mitigated by the A0 harness + hardware; the golden only guards load/save.
- **Polymorphic clone keystone** (`param_manager.cpp:217`): the hardest change; a virtual `clone()`
  must exactly reproduce today's deep-copy (including the fallible/OOM behaviour and the reverse-clone
  variant) or clip-duplicate/undo silently corrupts state.
- **`MIDIParam` non-trivial copy:** the embedded `AutoParam` must deep-copy; today's memcpy+beenCloned
  hides the hazard.
- **Reverse-clone:** keep as a dedicated method; do not fold into `operator=`.
- **`Destination` ownership:** audit before trusting `*to = *from`.
- **Hardware-gated & undo-adjacent:** validate on device before expanding.

## 7. Out of scope / deferred

- **The DOM automation store work** (single-copy unification, DOM ownership, engine borrow pointers —
  the DOM-automation spec's Phases B+). Deferred until this clone modernization lands, per decision.
- **Spec correction (noted, deferred):** the DOM-automation spec §4.2 says `fast_vector` "relocates by
  memcpy, not move-construction." Growth actually **move-constructs**; the store-in-DOM conclusion still
  holds (the owner's control block relocates in *address*), but the reasoning needs correcting, and it
  *widens* the Phase B options (a `unique_ptr`-held store survives owner moves). Fix when Phase B is
  planned.

## 8. Global constraints

- New/modified C++ is idiomatic modern C++23/26; behaviour-preserving changes stay faithful but modern.
- Never add `Co-Authored-By` / "Generated with Claude" trailers.
- Commit freely at natural stopping points (branch `deluge-project-param`, not main).
- Golden `OK=15 REGRESS=0` at every task boundary; clone/undo harness green; `param_projection_test` green.
- clang-format pre-commit hook may reformat and abort (rc=1); re-run `git add -A && git commit`.
- Rebuild sim targets before running the golden.
