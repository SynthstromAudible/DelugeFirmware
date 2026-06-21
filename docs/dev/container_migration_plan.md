# Custom Container Audit & Standard-Container Migration Plan

*Audited at commit `9a74e162` (2026-06-12).*

> **Status (2026-06-12):** Phases 0–3 are implemented on `feat/std-container-migration`, one build-green
> commit per replacement for bisectability. All domain containers are migrated; high-traffic ones went
> through the `deluge::OrderedPosVector<T, keyMember>` compatibility template
> (`util/container/ordered_pos_vector.h`) so their call sites are unchanged. Still custom:
> `MemoryRegion::emptySpaces` (Phase 4.1, allocator core — keep until it gets its own carefully-tested
> carve-out onto `etl::vector_ext`) and `BidirectionalLinkedList` (Phase 4.2). `EnumStringMap` kept per plan.
> **No hardware testing has been done yet** — flash and run the smoke matrix in §5 before merging.
>
> **Modernization pass (also done):** the migrated containers expose a standard surface
> (`size()`/`empty()`/`clear()`/`operator[]`/iterators; `lower_bound(key)`/`find_key(key)` on
> `OrderedPosVector`), the legacy clear-as-`empty()` is gone, and call sites with genuinely range-shaped
> loops (MIDIParamCollection, audioFiles scans, WaveTable bands, browser sort/search) use range-for and
> `std::ranges` algorithms. **Deliberately not converted:** loops in note_row.cpp, auto_param.cpp,
> instrument_clip.cpp, song.cpp and session.cpp that mutate their container mid-iteration (or call into
> code that can) — range-for caches `end()` and would silently change behavior. Convert those only with
> per-loop review; prefer the iterator-returning APIs when doing so.

This document inventories every custom container in the firmware, analyzes each use
location, and proposes a phased migration to standard C++ containers (via the
`deluge::` aliases in `src/deluge/util/containers.h`). It also identifies the
containers that must **not** be migrated and why.

---

## 1. Executive summary

The firmware carries ~3,500 lines of custom container code under
`src/deluge/util/container/`, written before the codebase had C++23, exceptions,
and allocator-aware std containers. Today the prerequisites for migration exist:

- `util/containers.h` already aliases `std::vector`, `map`, `set`, etc. over two
  GMA-backed allocators (`deluge::vector<T>` → external 8 MiB non-stealable SDRAM
  via `allocExternal`; `deluge::fast_vector<T>` → internal SDRAM via `allocMaxSpeed`).
- Exceptions are enabled; both allocators throw `deluge::exception::BAD_ALLOC`,
  and 7 catch sites already exist as precedent.
- `deluge::vector` is already widely used in `gui/menu_item/`.

**What can migrate:** all `ResizeableArray`-family containers (12 domain types,
~1,600 call sites), `OpenAddressingHashTable` (2 stack-local uses), and
`NamedThingVector` (2 uses).

**What cannot (or should not) migrate:**

- `BidirectionalLinkedList` — *intrusive* list inside the memory-stealing core
  (`CacheManager` / `Stealable`). No std equivalent; `std::list` allocates nodes,
  which is unacceptable inside the allocator. However, ETL (already a pinned
  dependency, 20.40.0) provides `etl::intrusive_list` as a typed, non-allocating
  replacement — see §3.9.
- `MemoryRegion::emptySpaces` — lives *inside* `GeneralMemoryAllocator` and uses
  `setStaticMemory()`; it can never allocate through the allocator it implements.
  `etl::vector_ext` (runtime-sized external buffer) is the natural end-state here
  — see §4.2.
- `EnumStringMap` — constexpr, allocation-free, harmless. Optional tidy only.

**Strategic payoffs beyond code deletion:**

1. Removes the pointer-as-32-bit-key idiom (`Song::backedUpParamManagers`,
   `Kit::drumsWithRenderingActive`, `OpenAddressingHashTable` keyed on `Output*`),
   which is a known blocker for the 64-bit host simulator build.
2. Shrinks the surface that libdeluge / a future Rust port must reproduce.
3. Replaces unchecked `memmove`-of-objects semantics with proper move semantics
   (today e.g. vtable-bearing `MultiRange` objects are memmoved).

---

## 2. Cross-cutting constraints

These apply to every migration below; the per-container sections reference them
as **C1**–**C7**.

### C1. Memory region selection

`ResizeableArray` allocates with `GeneralMemoryAllocator::allocMaxSpeed()`
(internal stealable-region SDRAM, though the allocations themselves are not
stealable). Replacing one with `deluge::vector` silently moves its storage to the
external 8 MiB region; `deluge::fast_vector` preserves the current region.

**Rule for this migration: use `fast_vector`/`fast_set`/`fast_map` everywhere by
default (preserves today's placement), except where the external region is
specifically advantageous (see `AudioFileVector`, C5).**

### C2. Growth policy and slack

`ResizeableArray` grows by `numExtraSpacesToAllocate` (default 15) elements,
shrinks slack beyond `maxNumEmptySpacesToKeep` (default 16), and can grow
**in place** via `GeneralMemoryAllocator::extend()`. `std::vector` doubles
capacity, never shrinks without `shrink_to_fit()`, and the std allocator API has
no realloc/extend, so every growth is alloc + copy + free.

Consequences:
- Per-container slack rises from ≤16 elements to up to 100% of size. The song
  model holds *thousands* of small containers (one `NoteVector` per `NoteRow`,
  one `ParamNodeVector` per automated param). Mitigate with `reserve()` at the
  existing `ensureEnoughSpaceAllocated()` sites and `shrink_to_fit()` at
  quiescent points (song load complete, clip clear).
- Growth copies get larger. For multi-thousand-note rows this is a one-off
  memcpy of tens of KB on the audio/sequencer path — measure during Phase 3.

### C3. Error returns vs exceptions

Custom containers return `Error::INSUFFICIENT_RAM`; std containers throw
`deluge::exception::BAD_ALLOC` from the allocators. Every mutation site keeps its
failure handling, but the mechanism changes. Two idioms, both already present in
the codebase:

```cpp
// (a) try/catch at the operation boundary, mapping back to Error
try { notes.insert(it, note); }
catch (deluge::exception&) { return Error::INSUFFICIENT_RAM; }

// (b) pre-reserve, then mutate noexcept
try { notes.reserve(notes.size() + n); }
catch (deluge::exception&) { return Error::INSUFFICIENT_RAM; }
notes.insert(...);  // cannot throw now (trivial types)
```

Idiom (b) is preferred in hot paths and multi-step edits, because it keeps the
strong exception guarantee trivially.

### C4. Element semantics: memmove → move construction

`ResizeableArray` memmoves raw bytes; element types are implicitly required to be
trivially relocatable. Migration imposes real C++ semantics:

- `Note`, `ParamNode`, `ClipInstance`, `SamplePercCacheZone`, `MIDIParam`,
  `MIDIKnob`, `ArpNote`, `Drum*`, `Clip*` — trivially copyable, no work needed.
- `NoteRow`, `FileItem`, `UnknownSetting`, `NamedThingVectorElement` — contain
  `String` (refcounted) or nested containers; need correct move ctors (mostly
  `= default` once members are std types).
- `MultiRange`/`MultisampleRange`/`MultiWaveTableRange` — **polymorphic objects
  stored by value and memmoved, vtables and all**. This is UB that happens to
  work; the fix is `std::variant` (see §3.6), not a mechanical swap.

### C5. The memory-stealing reentrancy hazard

`ResizeableArray::insertAtIndex(i, n, thingNotToStealFrom)` exists because
growing an array in the stealable region can trigger GMA *stealing*, and stealing
a `Cluster`/`AudioFile` can re-enter the very container being grown
(`AudioFile::mayBeStolen()` at `storage/audio/audio_file.cpp:512` explicitly
checks `thingNotToStealFrom != &audioFileManager.audioFiles`). The only firmware
caller that passes a non-null value is `NamedThingVector::insertElement`
(`util/container/vector/named_thing_vector.cpp:89`, passing `this`).

`fast_allocator` does **not** plumb `thingNotToStealFrom` through. Therefore:
containers whose elements can be deallocated *by stealing* must either move to
`external_allocator` (the external region never steals — this dissolves the
hazard) or `fast_allocator` must grow an optional steal-exclusion hook. The plan
chooses the external region for the one affected container (`audioFiles`).

### C6. Circular gap buffer

`ResizeableArray` is a wrap-around buffer (`memoryStart`): insertion/deletion at
*either* end is cheap, and `shiftHorizontal()` (rotate) is nearly free.
`std::vector` front-insertion is O(n). Affected hot-ish patterns:

- `Song::sessionClips` inserts at index 0 when a new clip is created — n is
  ≤ a few hundred clips; O(n) pointer moves are negligible.
- `shiftHorizontal()` on `NoteVector`/`ParamNodeVector`/automation (user-triggered
  "shift" operations) → becomes `std::rotate` + key rebase. Rare, user-paced; fine.

No site needs a `deque`.

### C7. Code size

One type-erased `ResizeableArray` implementation currently serves all element
types. Templates instantiate per element type (~15 instantiations). This is
partially offset by deleting ~3,500 lines of custom container code and by the
sorted-array algorithms being shared free-function templates. Expect a modest net
code-size increase; the firmware runs from SDRAM, so this is acceptable but worth
tracking in CI size reports.

---

## 3. Per-container analysis and replacement at each use location

### 3.1 `ResizeableArray` (`util/container/array/resizeable_array.h`)

Type-erased dynamic array (runtime `elementSize`). Base of everything in §3.2–3.7.
Direct (non-subclassed) uses:

| Use location | Element / ops used | Replacement |
|---|---|---|
| `Arpeggiator::notesAsPlayed` (`modulation/arpeggiator.h:366`); used in `arpeggiator.cpp` (insertAtIndex/deleteAtIndex/getElementAddress, ~13 calls) | `ArpNote`, insertion-ordered | `deluge::fast_vector<ArpNote>`; `getElementAddress(i)` → `&v[i]`; `emptyingShouldFreeMemory=false` → `clear()` (which already keeps capacity) |
| `Arpeggiator::notesByPattern` (`modulation/arpeggiator.h:368`); `arpeggiator.cpp` (~6 calls) | `ArpNote`, pattern order | `deluge::fast_vector<ArpNote>`, same notes as above |
| `RuntimeFeatureSettings::unknownSettings` (`model/settings/runtime_feature_settings.h:116`); `runtime_feature_settings.cpp:271-311` (append + linear scan only) | `UnknownSetting{String, value}` | `deluge::vector<UnknownSetting>` (cold path, external region fine); needs `UnknownSetting` move ctor (holds `String`) |
| `MidiKnobArray` (`modulation/midi/midi_knob_array.h`, append/iterate wrappers; used via `ModControllableAudio::midiKnobArray`) | `MIDIKnob` | `deluge::fast_vector<MIDIKnob>`; delete the wrapper class |
| `SampleClusterArray` (`model/sample/sample_cluster_array.h`; `Sample::clusters`) — preallocated once per sample to cluster count, `getElement`, no reordering | `SampleCluster` | `deluge::fast_vector<SampleCluster>` with `reserve()`/`resize()` at load; consider `std::unique_ptr<SampleCluster[]>`-style fixed array since size is fixed after load |

API mapping used throughout: `getNumElements()` → `size()`,
`getElementAddress(i)` → `&v[i]` / `std::span`, `empty()` → `clear()`,
`swapStateWith()` → `std::swap`/`swap()`, `cloneFrom()`+`beenCloned()` → copy
construction, `deleteAtIndex(i,n)` → `erase(it, it+n)`,
`insertAtIndex(i,n)` → `insert(it, n, T{})` (with C3 idiom),
`ensureEnoughSpaceAllocated(n)` → `reserve(size()+n)`,
`repositionElement/swapElements/moveElements*` → `std::rotate`/`std::swap_ranges`.

### 3.2 `OrderedResizeableArray` (sorted, bitfield key at byte offset)

A sorted vector whose sort key is an N-bit field at a byte offset inside the
element. Replacement pattern for all subclasses: **`deluge::fast_vector<T>` kept
sorted with `std::lower_bound` on an explicit key projection** (the key becomes a
real struct member instead of a masked bitfield — clearer and 64-bit-safe).
Batch/range algorithms (`searchMultiple`, `searchDual`, `shiftHorizontal`,
`generateRepeats`) get ported once as free-function templates in
`util/algorithm/sorted.h` (see Phase 0).

| Subclass | Use locations | Key | Replacement |
|---|---|---|---|
| `NoteRowVector` (`model/note/note_row_vector.h`) | `InstrumentClip::noteRows` (`model/clip/instrument_clip.h`); ~253 call sites, dominated by `instrument_clip.cpp` (getNumElements ×89, getElement ×77) and `kit.cpp` | `NoteRow::y` (16-bit) | `deluge::fast_vector<NoteRow>` sorted by `y`; `insertNoteRowAtY` → `lower_bound` + `insert`. **Must come after NoteVector migration** so `NoteRow` is cheaply movable (C4). Kit clips use y = index semantics — preserve the existing two access modes (`getNoteRowFromId` vs index) |
| `MIDIParamVector` (`modulation/midi/midi_param_vector.h`) | `MIDIParamCollection::params` (`modulation/midi/midi_param_collection.cpp`, ~37 calls) | CC number (8-bit) | `deluge::fast_vector<MIDIParam>` sorted by `cc`; `getOrCreateParamFromCC` → `lower_bound` + insert-if-missing. Tiny n (≤128); could even be `fast_map<uint8_t, MIDIParam>` but sorted vector is leaner |
| `MultiRangeArray` (`storage/multi_range/multi_range_array.h`) | `Source::ranges` (`processing/source.cpp` ~19 calls, `processing/sound/sound.cpp` ~12 calls) | `MultiRange::topNote` (16-bit at offsetof) | **Special case (C4):** stores polymorphic `MultisampleRange`/`MultiWaveTableRange` by value, memmoved with vtables. Replace with `deluge::fast_vector<std::variant<MultisampleRange, MultiWaveTableRange>>` sorted by `topNote`, or a sealed non-virtual `MultiRange` with a discriminant. The `ranges.elementSize` pokes in `sound.cpp` go away |
| `Arpeggiator::notes` (`modulation/arpeggiator.h:364`; `arpeggiator.cpp` ~23 calls) | note code (16-bit) | `deluge::fast_vector<ArpNote>` sorted by `noteCode`, `lower_bound` search |

### 3.3 `OrderedResizeableArrayWith32bitKey` (key = first 4 bytes)

Adds `searchDual`, `searchMultiple`, `shiftHorizontal`, `generateRepeats`.

| Subclass / instance | Use locations | Replacement |
|---|---|---|
| `NoteVector` (`model/note/note_vector.h`) | `NoteRow::notes` — ~301 call sites: `note_row.cpp` (~190), `instrument_clip_view.cpp`, `action.cpp`/`consequence_note_array_change.*` (undo snapshots via `swapStateWith`), `deluge.cpp` | `deluge::fast_vector<Note>` sorted by `pos` + ported algorithms. The undo machinery's `swapStateWith` → member `swap()` (O(1), noexcept — same guarantee). Hot path: live recording inserts via `insertAtKey` → `lower_bound`+`insert`; mostly appends at end, so O(1) amortized. **Highest-traffic migration in the plan; do last with NoteRowVector** |
| `ParamNodeVector` (`modulation/params/param_node_vector.h`) | `AutoParam::nodes` — ~270 call sites: `auto_param.cpp` (~250), `consequence_param_change.cpp` (swapStateWith snapshot) | `deluge::fast_vector<ParamNode>` sorted by `pos`; `searchDual` (5 uses) ported as free function. Same hot-path profile as NoteVector |
| `ClipInstanceVector` (`model/clip/clip_instance_vector.h`) | `Output::clipInstances` (`model/output.h`); ~182 call sites across `song.cpp`, `arrangement_view.cpp`, output/instrument code | `deluge::fast_vector<ClipInstance>` sorted by `pos`. Straightforward; key is a real `int32_t pos`, no 64-bit issue |
| `Kit::drumsWithRenderingActive` (`model/instrument/kit.h:151`; kit.cpp:52 constructs with `sizeof(Drum*)`, uses in `kit.cpp`, `sound_drum.cpp`, `load_instrument_preset_ui.cpp` — 7 calls) | sorted set of `Drum*` **cast to int32 keys** | `deluge::fast_set<Drum*>` (or sorted `fast_vector<Drum*>` with `lower_bound`). Removes a 64-bit host-sim blocker. n = active drums, tiny |
| `Sample::percCacheZones[2]` (`model/sample/sample.h:145`; `sample.cpp` ~22 calls) | zones sorted by start pos | `deluge::fast_vector<SamplePercCacheZone>` ×2, sorted by `startPos` |
| `WaveTable::bands` (`storage/wave_table/wave_table.h:63`; `wave_table.cpp` ~32 calls) | bands sorted by phase-increment threshold | `deluge::fast_vector<WaveTableBand>`; note `WaveTableBandData` (the audio data) stays a `Stealable` — only the index array migrates |

### 3.4 `OrderedResizeableArrayWithMultiWordKey` (composite multi-word keys)

| Instance | Use locations | Key | Replacement |
|---|---|---|---|
| `MemoryRegion::emptySpaces` (`memory/memory_region.h:72`; `memory_region.cpp`, 28 calls incl. `setStaticMemory`) | inside GMA itself | {size, address} | **Do not migrate to an allocating container.** See §4.2 |
| `Song::backedUpParamManagers` (`model/song/song.h:196`; `song.cpp` ~37 calls) | map keyed by {`ModControllable*`, `Clip*`} — **pointers as 32-bit words** | `deluge::fast_map<std::pair<ModControllable*, Clip*>, BackedUpParamManager>` or sorted `fast_vector` with a real struct key. Removes the main 64-bit host-sim blocker. Iteration patterns in `song.cpp` walk contiguous ranges per-ModControllable — `equal_range`/`lower_bound` reproduces this |
| `Sample::caches` (`model/sample/sample.h:142`; `sample.cpp` ~6 calls; note `sample.cpp:57` comments a bool widened to 32-bit purely to satisfy the key layout) | {sampleSource fields, reversed flag} | sorted `fast_vector<SampleCache-entry>` with a proper key struct + `std::lower_bound` on `std::tie` comparison; the widened-bool hack disappears |

### 3.5 `ResizeablePointerArray` → `ClipArray` (`model/clip/clip_array.h`)

| Use location | Ops | Replacement |
|---|---|---|
| `Song::sessionClips`, `Song::arrangementOnlyClips` (`model/song/song.h:189-190`) — ~136 call sites in `song.cpp`, `session.cpp` (28× `getNumElements`), `arranger_view.cpp`, etc. | index access, insert (often at 0), delete, swap | `deluge::fast_vector<Clip*>`; front-insert O(n) acceptable (C6) |
| `clip_iterators.{h,cpp}` (`model/song/`) — iterator adapters over `ClipArray` | custom iteration | becomes plain iterator/`std::span<Clip* const>` adapters; simplifies |
| `Action`/`ConsequenceClipExistence` (`model/action/action.{h,cpp}`, `model/consequence/consequence_clip_existence.*`) | records `ClipArray*` + index for undo | takes `deluge::fast_vector<Clip*>*`; mechanical |

`ClipArray`'s typed helpers (`getClipAtIndex`, `insertClipAtIndex`) can survive as
free functions or a thin subclass of `fast_vector<Clip*>` during transition.

### 3.6 `CStringArray` (`util/container/array/c_string_array.h`)

| Use location | Ops | Replacement |
|---|---|---|
| `Browser::fileItems` (static, `gui/ui/browser/browser.{h,cpp}`; ~109 call sites across browser/sample-browser/slot browser) | bulk fill from dir listing, `sortForStrings()` (quicksort on name via `strcmpspecial`), `search()` (binary search), delete | `deluge::vector<FileItem>` (cold UI path → external region relieves internal RAM during browsing); `std::sort` with a `strcmpspecial` comparator; `std::lower_bound` for search. `FileItem` holds `String` → needs default move ctor (C4). The existing low-memory fallback (browser truncates listings when `insertAtIndex` fails) maps to catch-and-truncate (C3) |

### 3.7 `NamedThingVector` (`util/container/vector/named_thing_vector.h`)

Sorted-by-name vector of `{void* thing, String name}`.

| Use location | Replacement |
|---|---|
| `MIDIDeviceManager::hostedMIDIDevices` (`io/midi/midi_device_manager.cpp:51`, ~14 calls) | `deluge::fast_vector<HostedMidiEntry{String, MIDICableUSBHosted*}>` sorted by name with `strcmpspecial`-style comparator, or thin template `NamedVector<T>` shared with audioFiles. Small n, cold path |
| `AudioFileManager::audioFiles` (`storage/audio/audio_file_manager.h:86` via `AudioFileVector`; lookups on sample load/playback) | template instance over `AudioFile*`, **allocated with `external_allocator`** to dissolve the steal-reentrancy hazard (C5). Then simplify `AudioFile::mayBeStolen` (`audio_file.cpp:512`) — its `thingNotToStealFrom` check against `&audioFileManager.audioFiles` becomes dead and the parameter can eventually be dropped from sites that only existed for this container |

Migrating both users kills `NamedThingVector` and the firmware's last
`thingNotToStealFrom` producer.

### 3.8 `OpenAddressingHashTable{With32/16/8bitKey}` (`util/container/hashtable/`)

| Use location | Pattern | Replacement |
|---|---|---|
| `session.cpp:287` `outputsLaunchedFor` (stack-local in clip launch evaluation) | set of `Output*` cast to uint32 keys | `deluge::fast_set<Output*>` (n = #outputs, tiny; tree-set fine). 64-bit-safe |
| `session.cpp:1639` `outputsWeHavePickedAClipFor` (stack-local, uses insert's `onlyIfNotAlreadyPresent` out-param) | set with "was it new?" check | `fast_set<Output*>::insert(...).second` — direct equivalent |
| `OpenAddressingHashTableWith16bitKey` / `8bitKey` | **no firmware users** — only `tests/32bit_unit_tests/container/open_addressing_hash_table.cpp` | delete with the class (Phase 0) |

### 3.9 `BidirectionalLinkedList` (`util/container/list/`) — replace with `etl::intrusive_list`

Intrusive doubly-linked list. `Stealable` (`memory/stealable.h:25`) *is a*
`BidirectionalLinkedListNode`; `CacheManager` (`memory/cache_manager.h:40`) holds
the reclamation queues; subclasses `Cluster`, `AudioFile`, `WaveTableBandData`,
`GrainBuffer` are queued/dequeued on the audio path with O(1) self-removal and
zero allocation. No *standard* container does intrusive linking — but ETL is
already a pinned dependency (20.40.0, `lib/CMakeLists.txt:12`, linked as
`etl::etl` at `CMakeLists.txt:201`), and `etl::intrusive_list<Stealable,
etl::bidirectional_link<0>>` covers this: non-allocating, typed (eliminates the
`static_cast<Stealable*>` walks in `cache_manager.cpp`), O(1) `size()`
(vs. `getNum()`'s O(n) walk), with `is_linked()`/`unlink()` for O(1)
self-removal.

Two feature-parity gaps need small redesigns:

- **Owning-list query**: `sample_cache.cpp:190` checks
  `cluster->list != &cache_manager.queue(q)` — ETL links don't record which list
  they're in. Fix: store a `StealableQueue currentQueue` id on `Stealable`
  (CacheManager already knows the destination queue at every `QueueForReclamation`
  call), or restructure the check.
- **`isLast()`**: same site; becomes a comparison against
  `std::prev(queue.end())` once the owning queue is known from the id above.

This migration is independent of the std-container work and lower priority
(scheduled Phase 4); the allocator core deserves its own careful test pass.

### 3.10 `EnumStringMap` (`util/container/enum_to_string_map.hpp`) — keep (optional tidy)

Constexpr enum↔string table; no allocation. Uses: `gui/ui/ui.cpp:460`
(`uiTypeMap`), `model/mod_controllable/filters/filter_config.cpp:8,15`
(`filterMap`, `routeMap`), `processing/audio_output.cpp:44` (`aoModeStringMap`).
Functionally fine. Known wart: the string→enum overload returns variant `N` on
miss and D_PRINTLNs into a dead buffer — worth a 5-line cleanup
(`std::optional<Enum>` return), independent of this plan.

### 3.11 Out of scope

`String`/`StringBuf` (`util/d_string.h`) are string types with GMA-aware
refcounting, not containers; they interlock with `NamedThingVector`/`FileItem`
element moves but are not migration targets here.

---

## 4. Non-std end-states

### 4.1 `BidirectionalLinkedList`
Replaced by `etl::intrusive_list` in Phase 4 (§3.9); stays as-is until then.

### 4.2 `MemoryRegion::emptySpaces`
Stays non-allocating, but once it is the *only* remaining user of the
`ResizeableArray` hierarchy, carve it out onto
`etl::vector_ext<EmptySpaceRecord>` (ETL's runtime-capacity vector over an
externally supplied buffer — a direct fit for the static buffer currently passed
to `setStaticMemory`) plus the Phase 0 sorted free functions. That lets us delete
`resizeable_array.cpp`, `ordered_resizeable_array.cpp`, and
`ordered_resizeable_array_with_multi_word_key.cpp` entirely. This is the final
step of the whole plan and is optional — keeping the trimmed-down legacy classes
solely for `MemoryRegion` is also acceptable.

---

## 5. Phased migration plan

Ordering principle: leaf/cold containers first to bed in the idioms (C1–C4),
core sequencer data last. Each phase is independently shippable and
hardware-testable. Every phase ends with: build firmware + run
`tests/unit` + `tests/32bit_unit_tests` + on-hardware smoke (song load/save
round-trip, live record, browser scroll, sample playback) + saved-XML diff
against pre-migration output for an identical session (serialization order must
not change — all replacements preserve sort order).

### Phase 0 — Infrastructure (no behavior change)
1. Add `util/algorithm/sorted.h`: free-function templates over contiguous
   ranges — `sorted_insert`, `sorted_erase_key`, `search_multiple` (batch binary
   search, port of `OrderedResizeableArrayWith32bitKey::searchMultiple`),
   `search_dual`, `shift_horizontal` (`std::rotate` + key rebase),
   `generate_repeats`. Port the `TEST_VECTOR*` ad-hoc tests into `tests/unit`.
2. Document the allocator-choice rule (C1) in `util/containers.h`.
3. Delete `OpenAddressingHashTableWith16bitKey`/`8bitKey` + their tests (dead code).
4. Add a `tests/32bit_unit_tests` suite exercising `deluge::fast_vector` against
   the mock GMA, including OOM-throw paths.

### Phase 1 — Leaf & cold containers (low risk, small diffs)
| # | Item | Size |
|---|---|---|
| 1.1 | `session.cpp` hash tables → `deluge::fast_set<Output*>` (§3.8) | ~30 lines |
| 1.2 | `unknownSettings` → `deluge::vector<UnknownSetting>` (§3.1) | ~20 lines |
| 1.3 | `MidiKnobArray` → `fast_vector<MIDIKnob>` (§3.1) | ~60 lines |
| 1.4 | `drumsWithRenderingActive` → `fast_set<Drum*>` (§3.3) — 64-bit win | ~25 lines |
| 1.5 | `hostedMIDIDevices` → named sorted vector (§3.7) | ~80 lines |
| 1.6 | `SampleClusterArray` → `fast_vector<SampleCluster>` (§3.1) | ~50 lines |

### Phase 2 — Medium-traffic model containers
| # | Item | Size / notes |
|---|---|---|
| 2.1 | `MIDIParamVector` → sorted `fast_vector<MIDIParam>` (§3.2) | ~90 call-site edits |
| 2.2 | `WaveTable::bands`, `Sample::percCacheZones` (§3.3) | ~55 call sites |
| 2.3 | `Sample::caches` → proper composite key (§3.4) | small; deletes the widened-bool hack |
| 2.4 | `Browser::fileItems` → `deluge::vector<FileItem>` + `std::sort` (§3.6) | ~110 call sites; verify low-memory truncation path |
| 2.5 | Arpeggiator `notes`/`notesAsPlayed`/`notesByPattern` (§3.1, §3.2) | ~45 call sites; audio-adjacent — profile arp under load |
| 2.6 | `AudioFileVector` → external-region named vector (§3.7) — retires `thingNotToStealFrom` plumbing | medium; coordinate with `AudioFile::mayBeStolen` |
| 2.7 | `backedUpParamManagers` → `fast_map`/sorted vector with struct key (§3.4) — main 64-bit win | ~40 call sites in `song.cpp` |
| 2.8 | `MultiRangeArray` → `std::variant` storage (§3.2) | **needs design care** (polymorphism-by-value); ~35 call sites |

After Phase 2: `NamedThingVector`, `CStringArray`, `OpenAddressingHashTable`
and `OrderedResizeableArrayWithMultiWordKey` (outside `MemoryRegion`) are deleted.

### Phase 3 — Core sequencer data (high traffic, do with profiling)
Order matters (C4: inner containers first so outer elements become movable):

| # | Item | Notes |
|---|---|---|
| 3.1 | `ClipInstanceVector` → sorted `fast_vector<ClipInstance>` | ~182 sites, mechanical |
| 3.2 | `ClipArray` → `fast_vector<Clip*>` + `clip_iterators` rework | ~160 sites incl. undo (`ConsequenceClipExistence`) |
| 3.3 | `ParamNodeVector` → sorted `fast_vector<ParamNode>` | ~270 sites; `searchDual` port; profile automation playback |
| 3.4 | `NoteVector` → sorted `fast_vector<Note>` | ~301 sites; `searchMultiple`/`generateRepeats`/`shiftHorizontal` ports; undo via `swap()`; profile live record + playback of dense rows |
| 3.5 | `NoteRowVector` → `fast_vector<NoteRow>` (after 3.4) | ~253 sites; `NoteRow` gains `= default` move ctor |

### Phase 4 — Teardown (optional)
4.1 Carve `MemoryRegion::emptySpaces` onto `etl::vector_ext<EmptySpaceRecord>`
(§4.2); delete the entire `ResizeableArray` hierarchy and `util/container/array/`.
4.2 `BidirectionalLinkedList` → `etl::intrusive_list<Stealable,
etl::bidirectional_link<0>>` (§3.9), including the `Stealable::currentQueue`
redesign for the owning-list query.

---

## 6. Risks & mitigations

| Risk | Where | Mitigation |
|---|---|---|
| OOM behavior change (Error → exception) misses a failure path | every mutation site | C3 idioms; grep-audit each migrated file for old `Error::NONE` checks; OOM unit tests per phase (Phase 0.4) |
| Memory slack growth across thousands of small vectors | NoteVector/ParamNodeVector (Phase 3) | `reserve()` at existing `ensureEnoughSpaceAllocated` sites; `shrink_to_fit()` on song-load completion; measure RAM headroom with large factory songs before/after |
| Loss of in-place `extend()` → larger growth copies | dense note rows, fileItems | profile; pre-reserve from serialized counts on song load |
| Audio-thread latency spikes from growth copy | live recording into long rows | measure worst-case insert under 16k-note rows; acceptable bound: one memcpy of `n*sizeof(Note)` ≈ 200 µs/16k notes at SDRAM bandwidth — verify |
| Stealing reentrancy regression | `audioFiles` (2.6) | external region per C5; assert-no-realloc test under simulated stealing in 32-bit unit tests |
| Polymorphism-by-value UB surfacing during `MultiRange` rework | 2.8 | treat as its own designed change with a spec test for multisample/wavetable range edit flows |
| Serialization order drift breaking song-file compatibility | all sorted containers | sort keys and comparators preserved exactly; XML round-trip diff in every phase gate |
| Code-size growth from template instantiation | global | track firmware size in CI per phase; offset by deleted container code |

---

## 7. Quick-reference inventory

| Custom container | Firmware users | Verdict |
|---|---|---|
| `ResizeableArray` | 5 direct + base of below | migrate → `fast_vector` family |
| `OrderedResizeableArray` | NoteRowVector, MIDIParamVector, MultiRangeArray, arp notes | migrate → sorted `fast_vector` |
| `OrderedResizeableArrayWith32bitKey` | NoteVector, ParamNodeVector, ClipInstanceVector, drumsWithRenderingActive, percCacheZones, wavetable bands | migrate → sorted `fast_vector` / `fast_set` |
| `OrderedResizeableArrayWithMultiWordKey` | emptySpaces (**keep**), backedUpParamManagers, Sample::caches | migrate 2 of 3 |
| `ResizeablePointerArray`/`ClipArray` | sessionClips, arrangementOnlyClips | migrate → `fast_vector<Clip*>` |
| `CStringArray` | Browser::fileItems | migrate → `deluge::vector<FileItem>` |
| `OpenAddressingHashTable` (32-bit) | 2 stack-locals in session.cpp | migrate → `fast_set<Output*>` |
| `OpenAddressingHashTable` (16/8-bit) | none (tests only) | delete |
| `NamedThingVector` | hostedMIDIDevices, audioFiles | migrate → sorted named vector |
| `BidirectionalLinkedList` | Stealable/CacheManager (allocator core) | migrate → `etl::intrusive_list` (Phase 4) |
| `EnumStringMap` | 3 constexpr tables | keep; optional tidy |
