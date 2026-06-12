# Legacy Container API Removal Plan

*Audited 2026-06-12 on `feat/std-container-migration` (after the std-container migration and the
modernization pass — see `container_migration_plan.md`).*

> **Status: executed (R0–R8 complete, same day).** All legacy methods listed below are deleted from
> OrderedPosVector / ClipArray / MultiRangeArray / AudioFileVector; NoteVector, ParamNodeVector and
> ClipInstanceVector are now plain using-aliases of the template. Naming refinements made during
> execution: the nullable bounds-checked lookup became `tryGet(i)` / `tryGetLast()` (kept — 9+ call
> sites use the null contract as logic), `firstAtOrAfter(key)` is the index-returning lower bound,
> and fallible ops return `std::expected` (with `.error_or(Error::NONE)` / `.value_or(-1)` bridging
> the old Error/sentinel flows exactly). Phase L (clone-idiom removal) remains open, as planned.
> The legacy `OrderedResizeableArray` hierarchy survives only under `memory/` (main plan, Phase 4).

The std-container migration kept the legacy method names (`getNumElements`, `getElement`,
`deleteAtIndex`, `search` + `GREATER_OR_EQUAL`/`LESS`, Error-returning inserts, …) as a compatibility
layer so each container could migrate in one bisectable commit. This plan removes that layer, leaving
only the standard surface plus genuinely domain-specific operations.

**Method:** for every removal, *delete the method from the wrapper first* and let the compiler
enumerate the call sites (this caught all 10 `empty()`-as-clear sites and both byte-copy aliasing bugs
during the migration). One commit per container per method family, build-green each, same
bisectability contract as before.

---

## 1. Containers in scope and their legacy surface

Call-site counts are receiver-based greps (modest noise: a few `notes` hits are scale-code `NoteSet`).

### 1.1 `OrderedPosVector` subclasses

| Method | `notes` (NoteVector) | `nodes` (ParamNodeVector) | `clipInstances` | `noteRows` | `params` (MIDI) | Total |
|---|---:|---:|---:|---:|---:|---:|
| `getNumElements()` | 71 | 87 | 41 | 125 | 1 | ~325 |
| `getElement(i)` | 85 | 70 | 88 | 116 | 1 | ~360 |
| `getElementAddress(i)` | 17 | 24 | — | — | — | 41 |
| `search(key, cmp, …)` | 25 | 24 | 28 | 3 | — | 80 |
| `searchExact(key)` | — | 1 | — | — | — | 1 |
| `insertAtIndex(i[, n])` | 6 | 18 | 5 | — | — | 29 |
| `deleteAtIndex(i[, n])` | 8 | 24 | 8 | — | — | 40 |
| `insertAtKey(key)` | 5 | 5 | 5 | — | — | 15 |
| `deleteAtKey(key)` | — | — | — | — | 1 | 1 |
| `swapStateWith(other)` | 7 | 3 | 2 | — | — | 12 |
| `swapElements(i, j)` | 2 | — | — | 1 | — | 3 |
| `ensureEnoughSpaceAllocated(n)` | 1 | — | 3 | — | — | 4 |
| `getLast()` / `getFirst()` | 7 / — | 1 / 5 | — | — | — | 13 |
| `init()` / `cloneFrom()` / `beenCloned()` | 2/—/1 | 4/2/1 | — | —/1/— | —/—/1 | 12 |
| `searchMultiple` / `searchDual` | 6 / — | — / 5 | 1 / — | — | — | 12 |
| `shiftHorizontal` / `generateRepeats` / `repositionElement` / `testSequentiality` | kept (domain) | | | | | ~30 |

NoteRowVector additionally has `insertNoteRowAtIndex` (5), `insertNoteRowAtY` (3),
`deleteNoteRowAtIndex` (8) — domain helpers that stay, but their *signatures* modernize (§3).

### 1.2 `ClipArray` (`sessionClips` + `arrangementOnlyClips` + `ClipArray*` params)

| Method | Count | Replacement |
|---|---:|---|
| `getNumElements()` | 112 | `size()` / `std::ssize()` / `!empty()` |
| `getClipAtIndex(i)` | 93 | `operator[]` |
| `getIndexForClip(clip)` | 11 | keep (thin `ranges::find` already) or inline at call sites |
| `insertClipAtIndex(clip, i)` | 12 | `std::expected<void, Error> insertClipAtIndex` (§2.1) |
| `deleteAtIndex(i[, n])` | 5 | `erase(it[, last])` |
| `ensureEnoughSpaceAllocated(n)` | 6 | `reserveExtra(n)` → `std::expected<void, Error>` (§2.1) |
| `swapElements` / `setPointerAtIndex` | 1 / 1 | `std::swap(v[i], v[j])` / `v[i] = clip` |

### 1.3 `MultiRangeArray` (`ranges`)

| Method | Count | Replacement |
|---|---:|---|
| `getNumElements()` | 43 | `size()` etc. |
| `getElement(i)` | 39 | keep — it's the variant→`MultiRange*` accessor, not a legacy shim. Optionally rename `rangeAt(i)` |
| `search(key, cmp)` | 3 | `lowerBound(key)` iterator-ish or keep index search (3 sites) |
| `insertMultiRange` / `insertVariant` / `changeType` / `currentElementSize` | 3/1/1/2 | domain API — keep; `currentElementSize` (a sizeof token) should become an enum/type query |
| `deleteAtIndex` / `ensureEnoughSpaceAllocated` | 1 / 1 | `erase(it)` / `reserveExtra` |

### 1.4 `AudioFileVector` (`audioFiles`)

Already mostly std (it *is* a `deluge::vector`). Remaining: `search` (3, index-returning),
`searchForExactObject` (2, index-returning), `insertElement` (3, Error-returning). Modernize shapes:
iterator-returning `lookup(path)` / `findExact(file)`, `std::expected<iterator, Error> insertElement`.

### 1.5 Not in scope

`MemoryRegion::emptySpaces` (still the legacy `OrderedResizeableArrayWithMultiWordKey`; its API dies
with the Phase 4 carve-out in the main plan) and `EnumStringMap` (no legacy surface).

---

## 2. Design decisions (settle before starting)

### 2.1 Fallible operations → `std::expected`

The codebase already uses `std::expected` + the `D_TRY`/`D_TRY_CATCH` macros (`util/try.h`, ~50 uses).
Replace the three legacy failure shapes — `Error` returns, `-1` index returns, `nullptr` returns,
`bool` returns — with one idiom:

```cpp
std::expected<iterator, Error> insertAt(iterator at, int32_t n = 1);  // was insertAtIndex → Error
std::expected<iterator, Error> insertSorted(int32_t key);            // was insertAtKey → index/-1
std::expected<void, Error> reserveExtra(int32_t n);                  // was ensureEnoughSpaceAllocated → bool
```

Internally these keep the existing try/catch on `deluge::exception`; call sites become
`D_TRY_CATCH(...)` or explicit `if (!result)`. The OOM-handling *logic* at each site is preserved
verbatim — only the plumbing changes. (~50 call sites across all containers.)

### 2.2 Indices → iterators, and signedness

- `search(key, GREATER_OR_EQUAL)` → `lowerBound(key)` (already exists on `OrderedPosVector`).
  `search(key, LESS)` (the `-1` offset) → `std::prev(lowerBound(key))` with the same
  begin-boundary caveat the old `-1` index had; the ~6 `LESS` sites each need eyes.
- `searchExact` → `find_key` (exists).
- The `GREATER_OR_EQUAL` / `LESS` macros are deleted at the end — a good "done" indicator.
- Index loops that must stay index-based (mutation during iteration) use
  `for (int32_t i = 0; i < std::ssize(v); i++)` — C++23 `std::ssize` keeps the loop variable signed
  and kills the `static_cast<int32_t>(v.size())` noise. The ~35 `if (x.getNumElements())`
  truthiness sites become `!x.empty()`.

### 2.3 The `getElement` nullptr question ← *the* main risk

`NoteVector`/`ParamNodeVector`/`ClipInstanceVector`/`MIDIParamVector::getElement` bounds-check and
return `nullptr`; `NoteRowVector`/`MultiRangeArray::getElement` don't. Of ~360 `getElement` calls,
**~43 are followed by a null/validity check** — those sites *use* the bounds check as logic
(typically "is this index past the end"). A blind `&v[i]` sweep would turn them into UB.

Policy:
- Sites with a preceding bounds guarantee (`i < getNumElements()` loop bounds, post-`search` checks):
  `&v[i]` / `v[i].member` — mechanical, scriptable, ~90% of sites.
- Null-reliant sites: rewrite the *condition* explicitly (`if (i < std::ssize(v))` /
  `find_key(...) != end()`), then index unconditionally. Each needs human review; budget the bulk of
  the per-container time here. Same for `getLast()` (8 sites, nullptr-when-empty) → guard + `back()`.

### 2.4 What is *not* legacy (stays, possibly renamed)

- **Sorted-insert/delete and batch algorithms:** `insertSorted` (née `insertAtKey`), `deleteAtKey`,
  `searchMultiple`/`searchDual` (modernize signatures to `std::span<int32_t>`), `shiftHorizontal`,
  `generateRepeats`, `repositionElement`, `testSequentiality` (debug assert). These have no std
  equivalent; they're the container's reason to exist.
- **Domain helpers:** NoteRowVector's row creation/deletion trio, `getOrCreateParamFromCC`,
  `MultiRangeArray`'s type-change machinery, `AudioFileVector::searchForExactObject` (neighbour
  logic for Sample/WaveTable path duplicates).
- **The clone idiom** (`init`, `cloneFrom`, `beenCloned`, and `swapStateWith`→ rename to `swap`):
  `swapStateWith` is a trivial rename (12 sites). The other three can only die when the *byte-copy
  cloning idiom itself* is replaced with real fallible clone factories on NoteRow / AutoParam /
  MIDIParamCollection (`static std::expected<T, Error> clonedFrom(T const&)`). That is a separate,
  deeper refactor — listed here as the optional final phase L, not a prerequisite for anything else.

---

## 3. Phases

Order: smallest/leaf containers first to bed in the idioms, the two giant sweeps last. Every phase is
one or more build-green commits; firmware + unit tests after each.

| Phase | Scope | Sites | Risk / notes |
|---|---|---:|---|
| **R0** | Decisions in §2 ratified; add `std::expected` variants alongside legacy methods (no call-site changes); add `swap()` alias | — | enables incremental porting |
| **R1** | `AudioFileVector`: iterator-returning `lookup`/`findExact`, expected `insertElement`; delete old | ~8 | trivial |
| **R2** | `MultiRangeArray`: std surface sweep + `currentElementSize` → type enum | ~50 | small; `getElement` stays |
| **R3** | `ClipArray`: `getClipAtIndex`→`[]`, `getNumElements`→`ssize`/`empty`, expected inserts, erase | ~230 | mechanical-heavy; clip loops often mutate → keep index loops, do NOT convert to range-for |
| **R4** | `MIDIParamVector` + `ClipInstanceVector`: full sweep incl. `search`→`lowerBound` | ~180 | medium; arrangement-view `search` sites use both cmp modes |
| **R5** | `NoteRowVector`: `getElement`→`&v[i]` (unchecked today, so purely mechanical), `getNumElements` sweep, domain trio gets expected signatures | ~250 | mechanical-heavy |
| **R6** | `ParamNodeVector` (auto_param.cpp): the densest mutation-during-iteration code; null-reliant `getElement` sites rewritten per §2.3 | ~250 | **highest care**; hot path — verify no behavior drift, profile automation playback |
| **R7** | `NoteVector` (note_row.cpp + views + undo): same treatment | ~230 | **highest care**; live-record hot path |
| **R8** | Delete `GREATER_OR_EQUAL`/`LESS` macros, `getNumElements`/`getElement*`/`search`/`searchExact`/`insertAtIndex`/`deleteAtIndex`/`insertAtKey`/`ensureEnoughSpaceAllocated`/`swapStateWith`/`getLast`/`getFirst` from `OrderedPosVector`; compiler confirms zero stragglers | — | the "done" gate |
| **L** *(optional, later)* | Replace byte-copy clone idiom with fallible clone factories; delete `init`/`cloneFrom`/`beenCloned` and the deleted-copy-ctor workarounds | ~20 | deep refactor across NoteRow/AutoParam/ParamManager cloning; own plan when attempted |

Estimated total: ~1,200 call-site edits, of which roughly 85% are scriptable renames and 50–60 need
individual review (null-reliance, `LESS` arithmetic, OOM paths).

## 4. Gotchas checklist (apply at every phase)

1. **Null-reliant `getElement`/`getLast`** — grep `getElement` followed by null tests before sweeping a file (§2.3).
2. **Mutation during iteration** — keep index/iterator loops; never auto-convert to range-for (per the boundary documented in the main plan).
3. **`getNumElements()` in arithmetic** — `getNumElements() - 1` underflows differently with unsigned `size()`; use `std::ssize`.
4. **`search(..., LESS)`** — returns `-1` when key ≤ first element; verify each site's handling of that sentinel before iterator conversion.
5. **OOM paths** — every `Error::NONE` / `-1` / `nullptr` / `bool` check must map onto the `std::expected` result; no path silently dropped.
6. **`insertAtIndex` multi-insert** (`insertAtIndex(i, n)`) sites (beenCloned reversals) need `insertAt(it, n)` to stay a single reserve + batch op for the O(n²) reasons noted in the migration.
7. After each phase, the deleted methods must not exist anywhere — `grep -rn '<method>' src/` returns only this document.
