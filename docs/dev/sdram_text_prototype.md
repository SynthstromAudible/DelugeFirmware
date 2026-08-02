# Executing code from SDRAM (`.sdram_text`): prototype, history, and measurements

Status: hardware-validated (2026-07, Deluge OLED, release builds). The mechanism is proven, the
first production migration (menu/browser/settings UI code, +265KB fast heap) is included, and an
on-device A/B benchmark ships with it so further placement decisions can be made from data. This
document explains the mechanism, what kept the 2023 groundwork from being enabled, and the
measurements.

## Why: internal RAM pressure (the justification)

Everything on the Deluge — code, constants, globals, the fast heap, the stack — shares the RZ/A1L's
3MB on-chip SRAM (`0x20020000–0x20300000` usable). The general allocator's "internal" (fast) region
is simply whatever is left between the end of the image and the stack. The pressure is easiest to
see on a feature-heavy downstream fork — comparing release builds of upstream `main` and the
owlet-labs fork's `dev` (2026-07-27, same toolchain, `arm-none-eabi-size -A` and linker symbols):

| In internal SRAM      | upstream | owlet fork | delta       |
|-----------------------|----------|-----------|--------------|
| `.text`               | 1,087 KB | 1,437 KB  | +350 KB      |
| `.rodata`             |   407 KB |   441 KB  | +34 KB       |
| `.bss` + `.data`      |   199 KB |   216 KB  | +17 KB       |
| **fast heap left**    | **1,154 KB** | **752 KB** | **−402 KB (−35%)** |

(Heap = `program_stack_start` − `__heap_start`: upstream `0x202f8000 − 0x201d7994`, fork
`0x202f8000 − 0x2023bc14`.) The same arithmetic applies to upstream directly: its image also grows
release over release, and every KB of cold code moved to SDRAM is a KB returned to the fast heap.

The fork's large FX *buffers* are not the problem — the looper (~350KB), disperser delay lines and
retrospective buffer all allocate from SDRAM. The internal loss is almost entirely code and lookup
tables.

Consequence: `GeneralMemoryAllocator::alloc(…, mayUseOnChipRam=true)` (i.e. `allocMaxSpeed`, used
for voices, param managers, clips) tries the internal region first and **silently falls back to
SDRAM** when it is full (`general_memory_allocator.cpp:182-193`). A 35% smaller fast heap means
dense songs exhaust it sooner, and the overflow objects live in slower-on-miss memory. Binary size
itself does not slow execution (all code runs from internal SRAM today; the 32KB L1I / 128KB L2
caches care about working set, not image size) — the cost is heap headroom. Moving cold code to
SDRAM converts wasted fast RAM back into fast heap at a per-KB exchange rate this benchmark
quantifies.

## History (upstream)

- **Sept 2023 — Paul Freund, `0bcdd2da1` "Allow placing BSS and TEXT in SDRAM".** Built the whole
  mechanism this prototype runs on, essentially unchanged: the linker sections
  (`.sdram_text/.sdram_data/.sdram_rodata/.sdram_bss`), the boot-time `relocateSDRAMSection()`
  copies in `resetprg.c`, and the `PLACE_SDRAM_*` macros. When execution from SDRAM proved
  unstable, he made the right call for a shipping instrument: leave the text half disabled and
  record the observation in `definitions.h`
  (`// Paul: I had problems with execution from SDRAM, maybe timing?`). That candid note is what
  pointed this investigation in the right direction — and as the analysis below shows, the
  underlying bug was effectively undiagnosable with the tools available at the time.
- **Feb 2025 — Mark Adams' data migration series.** PR #3329 ("make all the big things that don't
  touch audio go in external ram", plus an SDRAM rodata section for strings), #3338 (section-flag
  mechanism for strings), #3353 (better frunk usage), #3355 (ARM exception tables to SDRAM). All
  data/rodata; all shipped and in production use — proof at scale that the 2023 relocation
  machinery is sound. Scoping to data was the sensible choice while the execution issue remained
  unexplained; it left code placement as the one unfinished piece, which is what this branch takes
  up.

## Root cause of the 2023 instability (analysis)

Boot order in `resetprg.c`: `R_CACHE_L1Init()` enables L1 I/D caches (and `L2CacheInit()` the
instruction-only L2) **before** `relocateSDRAMSection()` runs. The relocation is a plain word-copy
loop with no cache maintenance:

1. The copied code bytes go through the **D-cache** — they may not reach physical SDRAM at all
   before first execution.
2. Instruction fetch does not read the D-cache. Worse, the preceding whole-SDRAM `memset` plus
   speculative fetch can already have populated I/L2 lines (zeros) for those addresses.
3. Result: stale or garbage instruction fetch on first call — intermittent, load-dependent crashes
   that present exactly as "maybe timing?". The 2023 symptom was read correctly; only the
   mechanism was hidden.

Data/rodata relocation never hits this because every later reader goes through the same D-cache —
which is why the 2025 data migration works fine with the identical copy loop.

The MMU was ruled out as a cause: `ttb_init.S` maps the SDRAM area (Area01) with
`TTB_PARA_NORMAL_CACHE` = `0x1DEE` — write-back cacheable normal memory, **XN=0 (executable)** —
the same attributes as internal RAM. Execution from SDRAM is architecturally permitted and cached.

The same bug class was independently found and fixed in the chainloader in 2026 (upstream #4634:
flush D-cache before executing the copied MMU-off chainloader), which supports the diagnosis.

A supporting discovery from this prototype: `src/RZA1/compiler/asm/l1_cache_operation.s` (which
implements `L1_I_CacheFlushAllAsm` / `L1_D_CacheOperationAsm`, the backing for every by-hand L1
maintenance helper in `cache.h`) was named with a lowercase `.s`, while `src/RZA1/CMakeLists.txt`
globs only `*.S` — so in the CMake-era build the file **was never compiled**, and LTO silently
eliminated the unreferenced `cache.h` wrappers. So through no fault of the original work, any
2023 attempt to add the missing cache maintenance using the official helpers would have hit
unexplained link errors — the deck was stacked against diagnosing this back then. Renamed to `.S`
on this branch, which is what makes the fix (and the benchmark's cold-cache flushes) linkable at
all.

## The fix (this branch)

After the `.sdram_text` relocation in `resetprg.c`, when the section is non-empty:
`invalidate_range_all_caches(start, end)` (clean+invalidate D by range, clean+invalidate L2 range)
followed by `L1_I_CacheFlushAll()` and `dsb; isb`. Data/rodata relocations are left untouched.

To **reproduce the 2023 instability** (historical evidence): build with
`SDRAM_TEXT_SKIP_CACHE_FIX` defined. The benchmark prints
`"calling sdram-placed code next"` before its first SDRAM call; a crash between that line and the
next — or garbage results — while the fixed
build runs clean, confirms stale-cache execution as the root cause. Note the instability is by nature
non-deterministic; a clean run on one boot does not falsify the diagnosis, but repeated clean runs
under load make it unlikely.

## The benchmark (`src/deluge/io/debug/sdram_text_bench.cpp`)

Two kernels, each compiled as **two byte-identical copies** — one in `.text` (internal SRAM), one
in `.sdram_text` — timed with the PMU cycle counter (`PMCCNTR`) in the same boot, so placement is
the only variable:

- **tight**: a few cache lines, loop-bound (4096 serially-dependent LCG steps). Warm numbers should
  be placement-invariant (both execute from L1I) — this is the control that validates the harness.
- **sprawl**: ~48KB of straight-line code, exceeding the 32KB L1I. Cold runs miss on every line, so
  the placement delta is the per-line instruction-fetch penalty of SDRAM vs internal SRAM — the
  number that bounds what moving cold code costs.

Protocol: 9 trials per (kernel, placement, cold/warm); cold trials fully flush L1D+L2+L1I between
runs; min and median reported (min suppresses interrupt interference; median shows typical). Serial
dependence in the kernels prevents compiler folding; `noinline` + `volatile` sink prevent
elision. Placements are interleaved so system drift cancels rather than biasing one side.

Running it: flash the build, re-enable the `sdramTextBenchRequest()` call in `sysex.cpp` and
attach the sysex debug console; results print once as JSON lines, starting with a `layout` line whose addresses prove where
each copy executes from (`0x0C……` = SDRAM, `0x20……` = internal).

Interpretation guide:

| Result | Meaning |
|---|---|
| `tight warm` differs between placements | Harness problem — investigate before trusting anything else |
| `sprawl cold` SDRAM ≈ internal | I-fetch penalty negligible; move cold code freely |
| `sprawl cold` SDRAM ≫ internal | Penalty is per-miss; still fine for menus/UI (human-speed), keep audio-path code internal |
| skip-fix build crashes at the marker | 2023 root cause confirmed |

## Measured results (2026-07-27, release build, chainloaded, Deluge OLED hardware)

The fixed build's first SDRAM-placed execution returned correctly ("calling sdram-placed code
next" → "sdram-placed code returned ok"), with the layout line confirming placements at runtime
(SDRAM kernels at 0x0C000001/0x0C000029 — Thumb bit set; internal at 0x200D…).

| kernel        | placement | cold_min | cold_med | warm_min | warm_med |
|---------------|-----------|----------|----------|----------|----------|
| tight         | internal  | 34,451   | 41,714   | 27,384   | 40,408   |
| tight         | SDRAM     | 34,772   | 42,209   | 27,389   | 40,411   |
| sprawl (33KB) | internal  | 38,289   | 41,687   | 27,437   | 40,424   |
| sprawl (33KB) | SDRAM     | 188,363  | 193,739  | 24,666   | 40,598   |

- **Warm execution is placement-invariant** (all four warm medians within 0.5%) — once cached,
  code home does not matter. The tight kernel is also cold-invariant, validating the harness.
- **Cold penalty**: +150,074 cycles for a fully-cold 33KB walk = **~145 extra cycles per 32-byte
  line**, ≈ 4.9× slower cold. At 400MHz that is ~375µs per fully-cold 33KB — a one-time cost per
  cache-cold visit. For human-speed UI code this is imperceptible (a 50KB fully-cold menu open
  would cost ~0.5ms once); for the per-buffer audio path (~2.9ms budget) it is prohibitive —
  which sets the migration rule: menus/UI/serialization may move, audio render and IRQ paths must
  not. (Medians carry ~13k cycles of interrupt noise vs mins, equally across placements.)

Hardware lessons encoded in this branch, found the hard way:
1. Linker `AT()` staging offsets must exactly match VMA offsets — a 4-byte alignment skew shifted
   every relocated SDRAM byte and garbled all OLED strings (fixed: ALIGN(32) section ends; the
   SDRAM sections now form one contiguous LOAD segment whose LMA is __heap_start).
2. Never send multiple sysex replies from inside the sysex RX handler (USB tx deadlock — device
   freeze, no fault recorded). The report runs from a scheduler task instead.
3. `sysexDebugPrint` shares one format buffer; rapid consecutive sends clobber in-flight lines.
   The report emits one line per task tick.
4. `Debug::print` is compiled out of release builds (ENABLE_TEXT_OUTPUT); anything that must
   report on release firmware uses sysexDebugPrint directly.

## What moved, and what moves next

First migration (landed with this work): menu system (`gui/menu_item`, `gui/context_menu`,
`gui/ui/menus.cpp`), browser/file UI and settings — +265KB returned to the fast heap.

Scope note: only executable sections (`.text*`) of those objects move. Vtables, typeinfo, string
literals and all other `.rodata`/data of the moved classes stay in internal SRAM (verified in the
built ELF: every vtable links at a `0x2…` internal address, none at `0x0C…`). Vtable *entries* for
the moved virtual functions point into SDRAM — resolved at link time and valid before `main()` via
the boot relocation — so virtual dispatch is mechanically unchanged; the only cost is the measured
cold instruction-fetch when a call lands on uncached SDRAM code.

Remaining
candidates in rough order of size-payoff and coldness: serialization, OLED rendering helpers.
Never: audio render path, interrupt handlers, SD/DMA-adjacent code. Per-file placement uses a
linker-script input pattern on object paths rather than per-function attributes. Each KB moved is
a KB returned to the fast heap; moving ~400KB of cold code would fully restore upstream's
internal-heap headroom.
