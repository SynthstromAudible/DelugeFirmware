# Hunting the layout-dependent sample overread

## RESOLUTION (2026-06-24): FIXED — it was NOT an overread, but shared-PRNG coupling

**Root cause: the master output dither drew from the synth-wide PRNG.** `AudioEngine::outputStage` added
`getNoise()` (the global `jcong`/`CONG` stream, also used by every synth's RANDOM source / noise oscillator
/ time-stretch hop randomization) to *every output sample*. The output stage runs once per output sample —
**including a layout-/timing-dependent number of keep-alive "silence" samples rendered during song load**
(the scheduler keeps audio alive while loading; how many blocks it renders depends on load duration measured
by the deterministic clock, which is layout-sensitive). So a heap-layout change shifted the count of
load-phase dither draws (+128 at `DELUGE_SIM_HEAP_PAD=64`), which advanced the shared PRNG to a different
position before the real render began → every synth random draw diverged → the whole render diverged. It was
never an overread (hence guard pages never faulted); the `cost: u8` band-aid only worked by keeping the chunk
table the same size so load timing — and thus that draw count — happened to land the same.

**The fix (this change):**
1. **Decouple the dither PRNG** — `outputStage` now uses a private `outputDither()` LCG stream, so dithering
   can never perturb synth randomness (and the offline render no longer depends on output-sample count).
   This alone makes every fixture pad-invariant across the full `DELUGE_SIM_HEAP_PAD` sweep (0…4096).
2. **Voice-cull by priority, not pointer** — `Sound::getLowestPriorityVoice` / `AudioEngine::killOneVoice`
   compared `ActiveVoice == std::unique_ptr<Voice>` by *address*, never dereferencing to use
   `Voice::operator<=>` (`getPriorityRating`). A real correctness bug (culls an arbitrary voice, the literal
   "drum_stealing" path) that was also heap-layout-dependent. Now compares `*a < *b`.

With these, the `cost: u8` band-aid is reverted to `u32` (48-byte `ChunkSlot`) and highsiderr stays
pad-invariant — i.e. struct size no longer matters, the plan's completion oracle.

**Caveat:** both fixes change render output (different dither sequence; different voice culled), so all sim
goldens must be **re-baselined + ear-checked**, and the change alters synth-random behaviour on hardware too
(more correct: musical randomness no longer coupled to output-sample count). Add a pad-sweep CI guard
(render each fixture under two `DELUGE_SIM_HEAP_PAD` values, assert bit-exact) so this can't silently regress.

---

### How it was diagnosed (each from a sim-only diagnostic toggle, all since reverted)

- **Tunable reproducer.** A leaked `DELUGE_SIM_HEAP_PAD` byte-pad at SDRAM-heap init reproduces the exact
  struct-growth signature (rms 159180, first MIXDOWN diff at byte 192) with the 44-byte struct, for pad
  **≥ 64** (16/32/48 pass). Struct-size-independent oracle. Run-to-run determinism confirmed (pad0 == pad0).
- **Technique A (guard pages) — no fault.** Backing every SDRAM allocation with its own mmap + a trailing
  `PROT_NONE` guard page, *even right-abutted with alignment ignored* (`DELUGE_SIM_GUARD_TIGHT`), never
  faults. So there is **no out-of-bounds read at all** — over *or* under end. (Cluster reads are already
  guarded: `Cluster` has a 32-byte zero-init `dummy[]` *before* `data[]` and a declared `data[CACHE_LINE_SIZE]`
  acting as a trailing guard, absorbing the interpolation misalignment.)
- **Technique B (free-poison) — clean.** 0xA5-on-free changes nothing → not a read of freed memory.
- **Content is irrelevant.** Zeroing the rounded-up TLSF slack (`DELUGE_SIM_ZERO_USABLE`) *and* zeroing
  every `deluge_alloc` return to its full usable size (`DELUGE_SIM_ZERO_ALLOC`) both fail to make it
  pad-invariant — proven cleanly in fixed-length **TRACK** mode (no tail-silence confound), all tracks
  still differ. So the divergence is **not** any memory-content read.
- **It is the PRNG.** Forcing `getNoise`/`getRandom255` to a constant (`DELUGE_SIM_CONST_NOISE`) makes the
  render **bit-identical across pads, every track**. Pinning only the *seed* (`DELUGE_SIM_FIXED_SEED`)
  does *not* — so it is the PRNG **call count / sequence position** that diverges, not the seed.
- **Localized to load-phase.** A `getNoise` call counter shows the counts already diverge *before export*
  (pad0 122272 vs pad64 122400, **+128**) — i.e. in the audio the scheduler renders *during* song load.
  Because the PRNG is global and shared (master dither consumes 2/sample, plus voice noise/RANDOM source,
  time-stretch hop randomization), any layout-dependent extra/fewer consumption shifts *every* later draw.
- **Not cluster eviction** (`evictions=0` at both pads, via `DELUGE_RESOURCE_STATS`). **Not CPU-load voice
  culling** either: no-op'ing `cullVoices` (`DELUGE_SIM_NO_CULL`) does not restore pad-invariance. The
  residual consumer(s) remain unpinned — a deeper systemic issue (multiple layout-sensitive paths feed the
  one global RNG). `requests` differs slightly (12678 vs 12694) — a *downstream* symptom (cache build extent).

### Real bug found & fixed along the way (committed in this change)
`Sound::getLowestPriorityVoice()` and `AudioEngine::killOneVoice()` selected a voice with
`std::ranges::max_element(voices_)` where `voices_` holds `ActiveVoice == std::unique_ptr<Voice>` — so it
ordered by **pointer address**, never dereferencing to use `Voice::operator<=>` (which compares
`getPriorityRating()`). The names (`getLowestPriorityVoice`, `lowest_priority_voices`) were a lie: culling
selected the highest-*addressed* voice, which is both semantically wrong on hardware (culls an arbitrary
voice, not the lowest-priority one) and heap-layout-dependent. Fixed both to compare `*a < *b`. This is a
genuine correctness fix but **changes render output → all sim goldens need re-baselining + an ear-check**,
and it is *not* sufficient on its own (the +128 load-phase divergence persists from another RNG consumer).

### Recommended next step
Find the remaining layout-dependent RNG consumer in the load-phase render: instrument the `getNoise` call
count per render routine (or bisect by `const`-ing individual consumers) to locate the address-gated branch
/ loop that draws a layout-dependent number of times. Candidate vectors to rule in/out: voice
acquisition/pool ordering, time-stretch hop setup count, any per-object loop bounded by a pointer-derived
count. Once pinned, the principled fix is likely to make the affected draw layout-invariant (or give that
subsystem its own PRNG stream). Then re-baseline goldens and add a pad-sweep CI guard (render each fixture
under two `DELUGE_SIM_HEAP_PAD` values, assert bit-exactness).

---

## (original plan below — retained for the technique catalogue; hypothesis since disproven)

## Goal

Find (and then fix) the latent **overread** in the sample read/interpolation path that makes the offline
golden render depend on heap **allocation layout**. Right now correct-looking audio silently depends on
what bytes sit *past* a buffer — a real bug (reads garbage past valid samples; exists on hardware too, it's
just usually masked). The proper fix is to stop reading past valid data; the current `ChunkSlot.cost` u8
packing only keeps the struct byte-stable so the golden doesn't notice.

## What we already know (don't re-derive)

- **Symptom:** growing `ChunkSlot` 44→48 bytes (adding `size: u32`) flipped the **highsiderr** golden
  deterministically — first diff at ~sample 37 (byte 192), `rms 159180` — with **0 evictions**. Bisected:
  vtable-innocent, `COST_OBJECT`-value-innocent, eviction logic never runs. Pure layout.
- **Mechanism class is confirmed by the code itself.** `src/deluge/memory/general_memory_allocator.cpp:64-68`
  documents it: the slab zeroes each cluster slot on acquire *specifically* because "a SampleCache reading
  interpolation-overhang bytes **past its write position**" would otherwise read layout-dependent recycled
  content. So the overhang-overread is known; the slab case was masked by `deluge_slab_set_zero_on_acquire`.
- **Why the deterministic gate doesn't fully mask it.** The gate zeroes memory at *hand-out*
  (`finalizeAlloc` GMA:40, `operator new` `memory/operators.cpp:14`, `ObjectPool::acquire`
  `memory/object_pool.h:113`, slab zero-on-acquire GMA:68). It does **not** zero (a) **free** heap that was
  never handed out (TLSF keeps stale bytes), nor (b) a **different neighbor** when the overhang crosses a
  buffer boundary. So an overhang that reads past a *heap* buffer (or past the slab's end) lands in
  layout-dependent memory the gate never touched. Growing the chunk table shifted every later allocation →
  changed those neighbors → changed the overhang samples.
- **The masked path is slab/cluster; the tripped path is NOT.** Growing the chunk table shifts the slab's
  base address but not its internal slot order, so slab-internal neighbours are unchanged. The flip
  therefore comes from a **heap-backed** buffer's overhang (perc cache / a heap cache / a sample tail), or
  the overhang crossing **past the slab's end** into the shifted heap.
- **Tools that WON'T work:** ASAN / valgrind only trap reads into *their* redzones. Our buffers come from a
  **custom** allocator (TLSF over a fixed SDRAM region) with no redzones, and an overread into an adjacent
  *valid* allocation isn't a fault. Electric Fence / MALLOC_CHECK target `malloc`, not our SDRAM heap. So a
  **guard page on the custom heap** is the only reliable read-trap (technique A).

## Step 0 — a fast, tunable reproducer

The full highsiderr render is ~120 s; we don't need it — the divergence is in the **first render block**.

1. **Known trigger (no new code):** revert the band-aid so `ChunkSlot` is 48 bytes again — change
   `ChunkSlot.cost` back to `u32` in `crates/deluge_resource/src/manager.rs` (and the two casts:
   `evict_lowest` `(s.cost as u32, 0)` → `(s.cost, 0)`, `evict_slot` drop the `as u32`, adopt `cost: cost as u8`
   → `cost`). Rebuild the i686 lib (`cargo build -p deluge_rust --release --target i686-unknown-linux-gnu`)
   + `cmake --build build-sim-cpp --target deluge_render`. `FIXTURE=highsiderr scripts/golden_mixdown.sh check`
   now FAILs at byte 192. This is the oracle: any real fix must make highsiderr pass **with the 48-byte
   struct**.
2. **Better, tunable trigger (recommended):** add a sim-only boot pad so you can shift layout by an arbitrary
   amount *without* touching the struct. In the host BSP / GMA init, once, gated on an env var:
   `if (getenv("DELUGE_SIM_HEAP_PAD")) deluge::memory::alloc_external(atoi(...), 16); // leaked on purpose`.
   Then `DELUGE_SIM_HEAP_PAD=4 … check` should reproduce, `=0` should pass. Sweeping the pad size maps the
   sensitivity and, later, proves the fix (golden stays green for *all* pad sizes).
3. **Shrink the render** so iterations are seconds, not minutes: cap the export to the first ~2048 frames
   (a `--max-frames` flag on `deluge_render`, or temporarily clamp the stem-export length) and diff only
   that. The bug shows in the first block, so this loses nothing.

## Technique A — guard-page the custom heap (decisive; do this first)

Goal: turn the silent over-end read into a SIGSEGV with an exact backtrace.

1. Add a sim-only allocator mode (`DELUGE_SIM_GUARD_HEAP`) to the **external/SDRAM** allocation path
   (`alloc_external` / the `GeneralMemoryAllocator` external region — the allocations that shift; start with
   heap-backed, since slab is already masked). In guard mode, bypass TLSF and back each allocation with
   `mmap`: round the request up to a page, append one `PROT_NONE` guard page, and **place the block so its
   end abuts the guard page** (right-align within the rounded region). `free` → `munmap`.
2. Run the reproducer. The first read ≥1 byte past a block faults; the backtrace names the exact buffer and
   read site. That's the overread.
3. Notes: right-alignment is essential (catches over-*end* reads, the bug; left-alignment would only catch
   under-reads). Expect this mode to be slow and memory-hungry (a page per alloc) — fine for one render.
   If nothing faults on the heap path, extend the guard to the **slab** (over-allocate each slot by a page
   and guard it) to catch an overhang crossing the slab's end / a slot boundary.

## Technique B — free-poison differential (cheap; corroborates + localizes)

The gate zeroes on hand-out but leaves **freed** memory stale. Add a sim-only **poison on free** (memset the
block to a recognizable sentinel, e.g. `0xA5`, in the `free`/`dealloc` path under `DELUGE_DETERMINISTIC_ALLOC`).

- If the render **changes** vs zero-on-free (or the divergent output samples now carry a recognizable
  `0xA5`-derived value) → the overread reads **free** memory → confirmed, and the leaked value's magnitude
  /position helps localize the source buffer.
- If the render is **unchanged** by the free-poison → the overhang reads a **live neighbor** instead; switch
  focus to which live allocation sits past the suspect buffer.

This needs no guard pages and reuses the existing deterministic-alloc plumbing.

## Technique C — bisect the buffer *class* by toggling masking

The gate's four zeroing sites each mask a class. Turn them off one at a time (keep the others on) and see
which un-masking reproduces the layout sensitivity / changes highsiderr at full heap:

- slab zero-on-acquire (GMA:68) — cluster/cache slots
- `finalizeAlloc` (GMA:40) — GMA `alloc()` (incl. perc cache via `alloc_external`?)
- `operator new` (operators.cpp:14) — std-container buffers
- `ObjectPool::acquire` (object_pool.h:113) — Voice/VoiceSample/TimeStretcher pools

Whichever class, when un-masked, makes the render layout-sensitive is where the overhang lands. (We expect a
**heap** class, not the slab — see "what we know".)

## Technique D — read the overhang code directly

The class is "interpolation/cache overhang past write position". Inspect the read path for an index that can
exceed the validly-written length:

- `src/deluge/model/sample/sample_low_level_reader.cpp` — `considerUpcomingWindow` already got an overshoot
  clamp (the P4-D2 fix: `bytesLeftWhichMayBeRead < 0 → 0`). Look for **sibling** lookahead reads that weren't
  clamped (the interpolation taps, the window-fill loop).
- `src/deluge/dsp/timestretch/time_stretcher.cpp` and `src/deluge/model/sample/sample_cache.cpp` — the
  repitch/perc cache fill + read (highsiderr is repitch-cache-heavy). Look for reads of
  `[pos + INTERPOLATION_MAX_NUM_SAMPLES]`-style overhang past the cache's written cluster length.
- `src/deluge/model/voice/voice_sample.cpp` — interpolation buffer fill at cluster boundaries.
- Anchors: `sizeof(Cluster) + Cluster::size` is the slot size; the overhang is a small constant
  (interpolation lookahead). Find where a read can pass the last written sample without the cross-cluster /
  end-of-sample guard kicking in.

## Confirm & fix

1. **Fix:** clamp/pad the read so it never passes the validly-written length — either stop the read at the
   boundary, or guarantee a zeroed pad region of `overhang` samples *within* the same allocation (so the
   read stays in-bounds and reads defined 0). Prefer the latter if the DSP needs the lookahead samples.
2. **Prove it:** with the fix, highsiderr passes **with the 48-byte struct** AND for every `DELUGE_SIM_HEAP_PAD`
   value. Then the `cost` u8 band-aid is no longer load-bearing (you can keep it as a cheap win or revert).
3. **Regression guard:** add a golden/CI mode that renders each fixture twice under two different
   `DELUGE_SIM_HEAP_PAD` values and asserts bit-exactness. That turns "a struct-size change silently shifted
   a fixture" into an immediate, localized failure — the gap behind this whole episode (see the caveat added
   to the deterministic-alloc gate: a green golden does **not** prove a layout change is neutral).

## Pitfalls

- A green golden does not prove neutrality — it's layout-pinned. Use the pad-sweep guard above.
- Don't chase ASAN/valgrind on the custom heap; they won't see it.
- The overhang may read its **own** buffer's unwritten slack (in-bounds) in some paths — that's already
  masked by hand-out zeroing and is *not* this bug. The bug is the **past-end** read (guard pages catch
  exactly that).
- Keep `DELUGE_HOST_DETERMINISTIC=1` for all repros; without it other nondeterminism muddies the diff.
