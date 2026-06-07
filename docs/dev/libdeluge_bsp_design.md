# libdeluge BSP — language, boundary, and structure

> Companion to [`libdeluge_separation_plan.md`](libdeluge_separation_plan.md).
> That doc establishes *that* we put a hardware boundary under the application.
> This doc decides *what the boundary is made of* and *what the BSP looks like*,
> in light of the existing Rust/Embassy work in `~/GitHub/deluge-embassy`.

---

## 0. The decisive context: there is already a Rust BSP

`~/GitHub/deluge-embassy` is a Cargo workspace that already contains:

| Crate | What it is |
|-------|-----------|
| `rza1l-hal` | register-level HAL for the RZ/A1L (MMU, caches, GIC, timers, DMA, RSPI, SSI, SCUX, SDHI…) |
| `deluge-bsp` | a **Rust BSP** — `audio`, `controls`, `cv_gate`, `oled`, `pads`, `pic`, `sd`/`fat`, `uart`, `usb`, `midi_gate`, `trigger_clock`, `encoder`, `sdram`, `system` |
| `demo-firmware` | Embassy firmware that **owns `main`**, runs the executor, spawns tasks |
| `deluge-fft`, `crow-firmware`, `controller-firmware`, … | downstream consumers |

Two facts dominate every decision below:

1. **The BSP modules already match the port list 1:1.** We are not designing a
   BSP from scratch; we are deciding how the C++ application talks to *this* one
   (and to a near-term C/C++ stand-in, and to a host sim).
2. **Embassy is the intended task manager, and Embassy owns `main`.** In
   `demo-firmware`, `main` is Rust, the executor is Rust, and product behavior
   runs as Embassy tasks. The crow plan (`deluge-embassy/docs/crow-port-plan.md`)
   already runs a foreign C core *inside* an Embassy task, shimming its
   low-level layer to Rust via `extern "C"`. **That is exactly the shape the
   Deluge C++ application will take.**

So the real question isn't "C or C++ for the BSP" — it's "what ABI lets one C++
application run on top of (a) today's C/C++ drivers, (b) the Rust `deluge-bsp`,
and (c) a host sim, without rewrites?"

---

## 1. Decision: the boundary is a **C ABI**. The BSP *implementation* is per-target.

**The `libdeluge` API is hand-authored C headers (`extern "C"`).** Not C++, not
Rust. Rationale:

- **C is the only ABI all three implementers speak.** C++ has no stable ABI
  (name mangling, vtables, exceptions) and cannot be called from Rust without a C
  shim *anyway*. Rust can't be the boundary because the app is C++. C is the
  lingua franca — the same choice the crow port already made.
- **It's stable.** A frozen C header lets the implementation underneath change
  (C++ drivers → Rust/Embassy) with zero application churn.
- **It's already the proven pattern here.** crow's `ll/{system,timers,adda,…}.h`
  ↔ Rust `extern "C"` is the template; we are doing the same with the Deluge app
  as the C core.

**The BSP implementation language is NOT fixed — it's chosen per target:**

| Implementation | Language | When | Notes |
|----------------|----------|------|-------|
| `bsp-rza1-legacy` | C/C++ | now | A **thin adapter** over the *existing* `src/deluge/drivers` + `src/RZA1`. Ships immediately, lowest risk. Do **not** write new hand-rolled C++ drivers — wrap what exists. |
| `bsp-host-sim` | C++ or Rust | early (testability-first) | Desktop backend: audio to file/loopback, MIDI virtual, OLED→PNG. Proves the app is hardware-free. |
| `deluge-bsp` (Rust/Embassy) | Rust | strategic | The existing crate + a thin `extern "C"` shim. The destination. |

> **Bottom line for "C or C++?":** the *contract* is C; the *reference
> near-term implementation* is C/C++ wrapping existing drivers; the *strategic
> implementation* is the Rust `deluge-bsp`. Avoid investing in new C++ BSP code
> that the Rust BSP will replace — keep the C/C++ side a wrapper, put new
> engineering into `deluge-bsp` + its shim.

---

## 2. Control inversion: who owns `main`

This is the subtlety the boundary must get right, because it differs between
today and the Embassy endgame.

| | Owns `main` / runtime | App runs as | Audio driven by |
|---|---|---|---|
| **Today (C++)** | the C++ app | the superloop + OSLikeStuff cooperative scheduler | SSI/DMA cursor polled in the app |
| **Endgame (Embassy)** | the Rust firmware crate | one-or-more **Embassy tasks** | a BSP audio task / SCUX path calling into the app |

To support both, **the boundary must not assume the app owns `main`.** Model the
lifecycle as the *platform* owning startup and calling *into* the app:

```c
// libdeluge/app.h  — implemented by the APPLICATION, called by the platform
void  deluge_app_init(const DelugeBoard* board);   // one-time setup
void  deluge_app_render(const StereoSample* in,     // realtime audio callback
                        StereoSample* out, uint32_t frames);
void  deluge_app_tick(void);                        // cooperative slice (see §4)
void  deluge_app_on_event(const DelugeInputEvent*); // pad/button/encoder/midi in
```

- **Today:** a tiny C `main()` calls `deluge_platform_init()` (the C/C++ BSP),
  then `deluge_app_init()`, then loops calling `deluge_app_tick()` and lets the
  DMA path call `deluge_app_render()`. Minimal change from the current superloop.
- **Endgame:** the Rust `main` brings up Embassy, spawns a task that calls
  `deluge_app_init()` then `deluge_app_tick()` cooperatively (crow-style
  "superloop in a task"), and the BSP audio task calls `deluge_app_render()`.
  **The app code is identical.** Embassy is entirely below the boundary.

The app exports the `deluge_app_*` symbols; the BSP/runtime exports the
`deluge_*` service functions (§3). Direction stays one-way.

---

## 3. The service API (what the BSP exports to the app)

One C header per service, mirroring `scheduler_api.h`'s style: opaque handles,
plain functions, fixed-width/POD types only, **no STL, no exceptions, no panics,
no allocation across the boundary.** These correspond directly to existing
`deluge-bsp` modules.

```
include/libdeluge/
  audio_io.h        # block format, sample rate, latency query (BSP owns DMA buf)
  midi_io.h         # per-port byte rx/tx  (deluge-bsp::uart, ::midi_gate, ::usb)
  control_surface.h # pad/button/encoder events + LED/RGB out (::pads ::controls ::encoder ::pic)
  display.h         # OLED framebuffer blit / 7-seg              (::oled)
  cv_gate.h         # CV out, gate out, trigger-clock-in         (::cv_gate ::trigger_clock)
  block_device.h    # sector rd/wr/ioctl for FatFS               (::sd ::fat)
  clock.h           # monotonic hi-res time, sample clock        (::system, ostm/mtu)
  scheduler.h       # the existing scheduler_api.h (see §4)
  memory.h          # region descriptors, cache clean/invalidate (::sdram)
  system.h          # boot order, irq registration, reset, fault (::system)
  board.h           # capability descriptor (grid size, #CV, rates, OLED vs 7seg)
  app.h             # the inbound callbacks from §2
```

Cross-cutting rules:

- **Realtime contracts are explicit.** Each function's header documents its
  call context: *ISR*, *audio-callback*, or *task/cooperative*. `deluge_app_render`
  is realtime — no locks, no allocation, no blocking calls inside it.
- **The BSP owns the audio buffers and the SCUX path.** `deluge-bsp::audio` runs
  samples through the SCUX **DVU block** for hardware volume/click-free fades.
  So the boundary passes *pre-volume digital samples*; **final volume/fade is a
  BSP concern**, and the app must not duplicate it. (A real behavioral decision —
  flag for the audio engine refactor in the separation plan's Phase 5.)
- **Events vs. polling.** `deluge-bsp` exposes *shared pad state* + IRQ-captured
  encoder detents; `demo-firmware` translates these to events. The C++ app today
  polls the PIC, so expose **both**: a poll-the-event-queue call (easy port now)
  and, optionally, the `deluge_app_on_event` push path for the Embassy world.

---

## 4. The hard part: the scheduler vs. Embassy

OSLikeStuff's cooperative scheduler currently lives *in the app* and is the one
place the app actively drives concurrency. Two ways it meets Embassy:

1. **Stage 1 — superloop in one task (least change).** Keep OSLikeStuff in the
   app. The BSP/runtime just calls `deluge_app_tick()` repeatedly. Under Embassy
   that tick runs inside a single Embassy task (exactly crow's model). Ship this
   first; the app is unaware Embassy exists.
2. **Stage 2 — scheduler becomes a port backed by Embassy.** Promote
   `scheduler_api.h` to a BSP-provided service: `addRepeatingTask` → spawn an
   Embassy task with an `embassy_time` ticker; `RESOURCE_*` → `embassy-sync`
   mutexes; once-tasks → `embassy_futures`. The app keeps the same C API; its
   "tasks" become real Embassy tasks underneath. Do this only when the payoff
   (true preemption-free async, USB/SD overlap) is wanted.

Recommend Stage 1 for the first Embassy bring-up, Stage 2 as a later, optional
step. Either way the scheduler is a *port*, not app-owned hardware knowledge.

---

## 5. Keeping the C header and the Rust BSP in sync

The hand-written C header is the **single source of truth**. The Rust side
conforms, checked mechanically:

- A small shim crate (e.g. `deluge-bsp-cabi`) in `deluge-embassy` `bindgen`s the
  canonical `include/libdeluge/*.h` and implements each `extern "C"` function as
  a thin wrapper over `deluge-bsp`. Because the signatures are *generated from*
  the header, the compiler enforces that Rust matches C — drift is a build error.
- The C/C++ legacy BSP and the host-sim BSP `#include` the same headers directly.
- CI builds all three against the same header set; a mismatch fails somewhere.
- Panic/error discipline: Rust shim functions are `extern "C"`, must not unwind
  across the boundary (abort or return an error code); the C++ app must not throw
  across it. (The crow work already solved the toolchain-linking, libc, and
  float-ABI issues this implies — reuse those findings.)

---

## 6. Build/repo topology

Two repos exist (`DelugeFirmware` C++, `deluge-embassy` Rust). Don't merge them
yet. Instead:

- **Own the contract in one place.** Put `include/libdeluge/` in `DelugeFirmware`
  (it's the app's requirement) and consume it from `deluge-embassy` via a git
  submodule or a vendored copy that CI diffs.
- **Near-term build (C++ on top):** CMake links app + `bsp-rza1-legacy` +
  existing RZA1. Nothing about Rust required to ship the separation.
- **Host build:** CMake links app + `bsp-host-sim`. This is the testability-first
  target from the separation plan.
- **Embassy build (Rust on top):** Cargo is the top-level. The Rust firmware
  crate builds the C++ app as a **staticlib** (via the `cc`/`cmake` crate, as
  crow already builds Lua), links `deluge-bsp-cabi` + `deluge-bsp` + `rza1l-hal`,
  and the Embassy `main` drives the `deluge_app_*` entry points. This is the
  inversion in §2, and the crow firmware is the working precedent.

---

## 7. Recommended sequence (folds into the separation plan's phases)

1. **Author the C header set** (`include/libdeluge/*.h`) as the contract,
   modeled on `scheduler_api.h`. (Separation-plan Phase 2.)
2. **`bsp-rza1-legacy`**: implement the headers as thin wrappers over existing
   `drivers/` + `RZA1`. App keeps OSLikeStuff and `main` for now. Ship — the
   firmware is unchanged behaviorally but now speaks only the C ABI.
   (Phases 1–4.)
3. **`bsp-host-sim`** + CI: app builds and tests on desktop. (Phase 7 — pulled
   early for the testability-first goal.)
4. **Audio boundary redesign** to the `deluge_app_render` callback, accounting
   for BSP-owned SCUX volume/fade. (Phase 5.)
5. **`deluge-bsp-cabi` shim** in `deluge-embassy`: implement the same C ABI over
   the Rust BSP; bring the C++ app up as a staticlib inside an Embassy task
   (Stage-1 scheduler). This is the first run of the real firmware on Embassy.
6. **Optional Stage-2 scheduler** on Embassy primitives, if/when wanted.

---

## 8. Decisions

- **Near-term path: ship `bsp-rza1-legacy` (C/C++) first.** Wrap the existing
  `src/deluge/drivers` + `src/RZA1` behind the C ABI before any Rust dependency.
  This de-risks the *boundary itself* independently of the Embassy integration.
  The Rust `deluge-bsp` shim follows as step 5 of §7, validated against the same
  headers.
- **Contract ownership: `include/libdeluge/` lives in `DelugeFirmware`** (it is
  the application's requirement) and is consumed by `deluge-embassy` via a git
  submodule / vendored copy, with a CI diff to catch drift. App-centric — the
  app declares the services it needs; implementations conform.
- **Scheduler endgame: deferred.** Start with Stage 1 (OSLikeStuff superloop
  running inside one Embassy task) and decide on Stage 2 (full migration onto
  Embassy tasks/timers/sync) *after* the Embassy bring-up shows what overlap
  (USB/SD/audio) is actually worth. The boundary supports both, so this choice
  costs nothing to postpone — keep `scheduler.h` a port either way.
```
