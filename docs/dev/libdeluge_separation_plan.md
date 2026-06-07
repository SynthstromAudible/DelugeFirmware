# libdeluge: Separating the Firmware from the Hardware

> **Status:** Proposal / planning document
> **Goal:** Split the codebase into a **portable Deluge application** (the synth /
> sampler / sequencer / UI / model / DSP) and **`libdeluge`** — the hardware
> support library (BSP) that the application runs on. `libdeluge` is the *only*
> consumer of the HAL. Porting to another ARM device means re-targeting
> `libdeluge` (+ its HAL), not touching the application.

> **Naming.** "Deluge" today names both the hardware and the software. We
> disambiguate: **`libdeluge`** is the hardware/board library (analogous to
> **libDaisy** for the Daisy platform, or an Arduino core/BSP). The portable
> product that links against it is referred to here as the **Deluge application**
> (the "app"). `libdeluge`'s public headers *are* the API boundary.

---

## 1. Objective

Today the application code, the board glue, and the Renesas RZ/A1L vendor HAL are
interleaved in one binary. The end state we want:

```
┌──────────────────────────────────────────────────────────┐
│ Deluge application  (portable: synth, sampler, sequencer, │
│   UI, model, DSP, song/storage logic, scheduling policy)  │
│                                                            │
│   depends ONLY on libdeluge's public API                   │
└───────────────────────────┬──────────────────────────────┘
                            │  #include <libdeluge/...>   (the API boundary)
┌───────────────────────────┴──────────────────────────────┐
│ libdeluge  (hardware support library / BSP — like libDaisy)│
│   provides: audio I/O, MIDI, control surface, display,     │
│   CV/gate, storage, clock, memory/cache services.          │
│   the ONLY consumer of the HAL.                            │
└───────────────────────────┬──────────────────────────────┘
                            │  HAL interface
┌───────────────────────────┴──────────────────────────────┐
│ HAL  (per-SoC register/peripheral drivers; e.g. RZA1)      │
│   wrapped/owned by libdeluge; swapped per board.           │
└────────────────────────────────────────────────────────────┘
```

A new device port = a new `libdeluge` target (new board implementation + its
HAL) exposing the same public API. The Deluge application is rebuilt unchanged
and linked against the new `libdeluge`.

---

## 2. Where we are today (findings from the current tree)

| Layer (today)            | Path                         | ~LOC    | Notes |
|--------------------------|------------------------------|---------|-------|
| Application              | `src/deluge/`                | 231k    | dsp, gui, model, processing, playback, modulation, storage, io, hid, memory, util |
| Partial driver façade    | `src/deluge/drivers/`        | 1.5k    | oled, ssi, uart, rspi, dmac, sd, pic, mtu, cache, usb — **already a seam; should become part of `libdeluge`** (today it includes RZA1 headers) |
| Vendor HAL               | `src/RZA1/`                  | 105k    | bsc, cache, gpio, intc, mtu, ostm, rspi, sdhi, spibsc, ssi, uart, usb, system/iodefines, compiler/start.S |
| OS-like services         | `src/OSLikeStuff/`           | 1.5k    | task scheduler (clean C API in `scheduler_api.h`), timers_interrupts, fault_handler |
| Entry / interrupt wiring | `src/main.c`                 | 142     | wires MTU timers + IRQ handlers, pokes RZA1/gpio, RZA1/uart directly |

**Key observations that make this tractable:**

1. **The coupling surface is small and countable.** Only **53 files** under
   `src/deluge/` directly `#include "RZA1/..."` — i.e. 53 places where the
   *application* reaches past `libdeluge` into the HAL. The dominant headers are:
   - `RZA1/uart/sio_char.h` (24) — MIDI + PIC serial
   - `RZA1/oled/oled_low_level.h` (10) — display
   - `RZA1/mtu/mtu.h` (7) — timers
   - `RZA1/gpio/gpio.h` (7) — CV/gate/pins
   - `RZA1/system/iodefines/dmac_iodefine.h` (4), `RZA1/intc/devdrv_intc.h` (4),
     `RZA1/rspi/rspi.h` (3), `RZA1/cpu_specific.h` (3), `RZA1/cache/cache.h` (2),
     plus USB, SDHI, SPIBSC singletons.

2. **`libdeluge` already half-exists** as `src/deluge/drivers/` — a thin façade
   (`getTxBufferStart()`, `ssiInit()`, `oled.*`, `pic.h`, …). That façade is the
   seed of `libdeluge`'s public API; today its *implementation* just happens to
   live in the app tree and pull in RZA1. We promote it to its own library.

3. **The scheduler is already a clean library boundary**
   (`OSLikeStuff/scheduler_api.h`): opaque types, plain C functions, no HAL types
   leaking. This is the template for every `libdeluge` API surface.

4. **The split is already proven in tests.** `tests/unit/` and
   `tests/32bit_unit_tests/` compile slices of `src/deluge` against `mocks/`
   (mock_display, mock_audio_engine, mock_encoder, timer mocks) — i.e. the
   application already builds against a *fake `libdeluge`* on a host. We are
   formalizing and completing what the test harness does ad hoc.

5. **Hardware facts are concentrated in two headers:** `RZA1/cpu_specific.h`
   (DMA channels, SSI channel, button rows/cols, SPI/UART channels, CV/gate
   counts, OLED) and `src/definitions.h` (timer assignments, SSI buffer sizes,
   `PLACE_SDRAM_*` section attributes). These belong inside `libdeluge`.

**The hard spots (called out early):**

- **Audio I/O.** `audio_engine.cpp` does not use a callback — it reads/writes the
  raw SSI/DMA double-buffer by pointer (`getTxBufferStart()`,
  `getTxBufferCurrentPlace()`, `i2sTXBufferPos`, `SSI_TX_BUFFER_NUM_SAMPLES`) and
  derives "how many samples can I render right now" from the DMA cursor. That
  buffer-ownership detail must move *into* `libdeluge`, with the app receiving a
  block to render. This is the **single biggest API to design.**
- **Control surface via PIC.** Pads/buttons/encoders/LEDs/7-seg all go through a
  PIC microcontroller over UART with a bespoke protocol (`drivers/pic/pic.h`).
  Other boards won't have a PIC, so `libdeluge` must expose a *logical*
  control-surface API and keep the PIC protocol entirely inside its RZA1
  implementation.
- **Memory & cache.** External SDRAM regions, `CACHE_LINE_SIZE` alignment,
  `.sdram_bss/.sdram_data` placement, manual cache clean/invalidate around DMA,
  and `linker_script_rz_a1l.ld` — all `libdeluge` concerns.
- **DSP intrinsics.** NEON shim (`src/arm_neon_shim.h`), NE10, inline asm.
  Portable across ARM-with-NEON; needs a scalar fallback for cores without it.
  (This lives in the *application's* DSP, not in `libdeluge`, but affects which
  targets the app can run on.)

---

## 3. Target layout

```
src/
  app/                       # the portable Deluge application (was src/deluge,
                             #   minus hardware includes). The product.
    dsp/ gui/ model/ processing/ playback/ modulation/ storage/ io/ hid/ ...
    # depends only on  #include <libdeluge/...>

  libdeluge/                 # the hardware support library (BSP) + public API
    include/libdeluge/       # THE API BOUNDARY — the only HW-facing headers the
                             #   app may include:
      audio_io.h             #   duplex block audio
      midi_io.h              #   raw MIDI byte streams (DIN + USB)
      control_surface.h      #   pads/buttons/encoders/LEDs (logical events)
      display.h              #   OLED framebuffer / 7-seg
      cv_gate.h              #   analog CV out, gate out, trigger clock in
      block_device.h         #   FatFS disk_read/write/ioctl
      clock.h                #   high-res time, sample clock
      scheduler.h            #   (move scheduler_api.h here)
      memory.h               #   region descriptors, cache maintenance, placement
      system.h               #   boot, irq registration, reset, fault hooks
      board.h                #   capability descriptor (grid size, #CV, rates, …)
    rza1/                    # reference implementation for the Synthstrom Deluge
      audio_io.cpp           #   wraps SSI+DMA double buffer -> block callback
      control_surface.cpp    #   speaks the PIC protocol
      display.cpp midi_io.cpp cv_gate.cpp block_device.cpp clock.cpp ...
      board_config.h         #   the numbers from cpu_specific.h + definitions.h
      hal/                   #   the RZA1 vendor HAL (was src/RZA1)
      linker/rz_a1l.ld
    host_sim/                # host implementation for dev/test (audio to file/
                             #   loopback, MIDI virtual, display to PNG, no PIC)
    template/                # stubbed implementation skeleton for new boards

  platform_libs/             # RTT, fatfs, NE10, lib (third-party, shared)
```

The Deluge **application** becomes a CMake library/target with **no** `RZA1/` or
any HAL path on its include path — it sees only `include/libdeluge/`. Each board
target = the app + one `libdeluge` implementation (which owns its HAL).

> **Open naming question.** Renaming `src/deluge/` → `src/app/` disambiguates
> hardware vs. software but is cosmetic churn over 231k LOC. We can defer the
> physical rename and enforce only the *dependency direction* first; the rename
> can land later as a pure move. Working label in this doc: "the application".

---

## 4. The `libdeluge` public API

`libdeluge`'s public headers are the contract. Each is small, documented, and
C-compatible (C++ allowed where the app already uses it, but no STL/exception
requirements crossing the boundary). The scheduler (`scheduler_api.h`) is the
existing model: opaque types, plain functions, no HAL types leaking.

| API surface | Responsibility | Current code it absorbs |
|-------------|----------------|--------------------------|
| **audio_io** | Open a duplex stream at a sample rate/block size; `libdeluge` calls the app's render callback with `in[]`/`out[]` (interleaved stereo, existing fixed-point Q format). App stops poking DMA buffers. | `drivers/ssi`, the buffer-cursor logic in `audio_engine.cpp`, `SSI_*` defines |
| **midi_io** | Enumerate ports; non-blocking byte read/write per port (DIN serial + USB MIDI). | `drivers/uart` (MIDI channel), `RZA1/usb` MIDI |
| **control_surface** | Logical events: pad pressed (x,y,velocity), button (id,state), encoder delta; outputs: set pad RGB, set LED, set indicator. Hides PIC/UART. | `drivers/pic`, `hid/matrix`, `hid/led`, encoders |
| **display** | Push an OLED framebuffer region; or write 7-seg digits. | `drivers/oled`, `RZA1/oled` |
| **cv_gate** | Set CV channel value, set gate, register trigger-clock-in callback. | `RZA1/gpio`, `main.c` trigger clock IRQ, RSPI DAC |
| **block_device** | Sector read/write/ioctl for FatFS. | `drivers/sd`, `RZA1/sdhi`, `diskio.c` |
| **clock** | Monotonic high-res time; sample-accurate counters (the MTU timers behind the scheduler). | `OSLikeStuff/timers_interrupts`, `drivers/mtu`, `RZA1/ostm` |
| **scheduler** | Already defined; relocate header into `libdeluge`. | `OSLikeStuff/scheduler_api.h` |
| **memory** | Describe regions (fast internal vs. large external), placement attributes, cache clean/invalidate ops for DMA coherency. | `definitions.h` `PLACE_SDRAM_*`, `CACHE_LINE_SIZE`, `drivers/cache`, allocator region ends |
| **system** | Boot/init order, IRQ registration, reset, fault hooks. | `main.c`, `resetprg.c`, `fault_handler`, `RZA1/intc` |
| **board** | Capability descriptor the app queries: pad grid size, #CV/#gate, #encoders, audio channels, OLED vs 7-seg, etc. | `cpu_specific.h` model defines |

Design rules:
- **Direction is one-way:** the app includes `<libdeluge/...>`; `libdeluge`
  includes its own API *and* the HAL; the HAL includes nothing upward.
- **No HAL types leak through the API.** No `RZA1/...` types in any
  `include/libdeluge/*.h`.
- **Callbacks for realtime, polling for cold paths.** Audio, MIDI-in, and
  trigger-clock are callback/ISR-driven; storage and UI refresh are polled by the
  scheduler.

---

## 5. Phased migration

The strategy is **strangler-fig**: keep the RZA1 board building and bootable at
every step; stand up the boundary, then move code behind it incrementally. No
"big bang" rewrite.

> **Prioritization (driver: testability / CI first).** The near-term goal is an
> application that builds against `libdeluge`'s API and runs in CI, *not* a
> specific second board. So the critical path is **Phase 0 (lint guardrail) →
> Phase 1 (`libdeluge` library target + app target) → Phase 7 (host_sim
> implementation), pulled early**, with Phases 2–4 done only as far as needed to
> make `host_sim` link and the unit/QEMU suites widen. The audio redesign
> (Phase 5) and full memory/linker work (Phase 6) can lag until a real second
> board is on the table — `host_sim` can use a trivial audio backend meanwhile.
> Every phase still leaves the RZA1 board bootable.

### Phase 0 — Guardrails & inventory (no behavior change)
- Add a CI lint that **fails if any file in the application includes `RZA1/`, a
  HAL path, or a `libdeluge` *implementation* path** (the app may include only
  `libdeluge` public headers). Seed it with today's 53 offenders as an allowlist
  that only ever shrinks. Makes progress measurable, prevents regressions.
- Capture a baseline: flash/RAM size, audio latency, boot time, unit + QEMU
  results, so each phase can prove "no regression."

### Phase 1 — Carve directories & build the two targets
- Create `src/libdeluge/` and move `src/RZA1` → `src/libdeluge/rza1/hal/`, the
  `src/deluge/drivers/` façade → `src/libdeluge/`. Re-home the app under a
  clear identity (defer the physical `src/deluge`→`src/app` rename if desired).
- `add_library(libdeluge ...)` (RZA1 variant) with a public include dir of
  `include/libdeluge`, and an app target linking it. Current `deluge` binary
  stays byte-comparable as the regression oracle.

### Phase 2 — Define the public API (headers only)
- Write `include/libdeluge/*.h` for every surface in §4, modeled on
  `scheduler_api.h`. No new implementations yet; they describe the contract.
- Relocate `scheduler_api.h`/`clock_type.h` into `libdeluge` (already clean —
  proves the boundary compiles end-to-end).

### Phase 3 — Move the existing façade behind the API (the easy 80%)
Group the 53 includes by subsystem and convert one group per PR, smallest first:
1. **clock/timers** (`mtu`, `ostm`): app calls `<libdeluge/clock.h>`; RZA1 impl
   wraps MTU. (7 files)
2. **MIDI/serial** (`uart/sio_char.h`): app uses `<libdeluge/midi_io.h>`. (24
   files — largest, but mechanical)
3. **display** (`oled_low_level.h`): app uses `<libdeluge/display.h>`. (10 files)
4. **cv/gate + gpio** (`gpio.h`): app uses `<libdeluge/cv_gate.h>`. (7 files)
5. **storage** (`sdhi`, `diskio`): app uses `<libdeluge/block_device.h>`; FatFS
   sits in `platform_libs` and binds to the API.
6. **control surface** (`pic.h`): collapse the PIC protocol behind
   `<libdeluge/control_surface.h>`; the PIC enum stays inside the RZA1 impl.
7. **DMA/cache/intc** leftovers fold into `memory.h` + `system.h`.

After each group, re-run the Phase-0 lint (allowlist shrinks) + tests.

### Phase 4 — Hardware constants → board config
- Split `cpu_specific.h`/`definitions.h`: portable constants stay with the app;
  hardware numbers move into `libdeluge/rza1/board_config.h` and surface to the
  app through `<libdeluge/board.h>` (a capability struct).
- Replace `#if DELUGE_MODEL ...` hardware branches with capability queries.

### Phase 5 — Audio I/O redesign (the centerpiece)
- Move SSI/DMA double-buffer ownership into `libdeluge`; expose a **block-based
  duplex callback** the app implements (`render(in, out, frames)`). The
  "samples available from the DMA cursor" logic becomes `libdeluge`'s and is
  expressed as the block size handed to the callback.
- Riskiest change for **latency and glitch-free audio**. Do it on RZA1 behind a
  feature flag, A/B against the current path, and measure worst-case render time
  vs. the DMA deadline before removing the old path.
- Keep the app's existing fixed-point sample format; the API just moves bytes.

### Phase 6 — Memory, cache & linker
- `<libdeluge/memory.h>`: region descriptors (fast/internal, large/external),
  `PLACE_SDRAM_*`-style placement supplied by `libdeluge`, and explicit
  `cache_clean()/cache_invalidate()` hooks the app calls around DMA regions.
- Per-board linker scripts inside each `libdeluge` impl; the app stops
  referencing `linker_script_rz_a1l.ld`.
- The general memory allocator takes region bounds from the API, not
  `EXTERNAL_MEMORY_END`.

### Phase 7 — Host-sim `libdeluge` (`host_sim`)
- A second `libdeluge` implementation for the desktop: audio via file or a
  PortAudio/loopback backend, MIDI via a virtual port, display to a window/PNG,
  control surface from a config/script, storage on a disk image.
- Reuse the existing `tests/*/mocks` as the starting point — they already stub
  display/audio/encoders. This implementation is the proof the application is
  truly hardware-free, and it becomes the fast dev/test loop.

### Phase 8 — New-board bring-up template & docs
- `docs/dev/porting_guide.md`: the checklist of API surfaces to implement, the
  capability descriptor to fill in, and `libdeluge/template/` — a skeleton with
  every surface stubbed and `#error "implement me"`.
- Acceptance test for a port: links against the app, passes the QEMU/unit suites
  that don't need real peripherals, boots to UI, plays a note.

---

## 6. Build system changes

- `libdeluge`: `add_library(libdeluge_<board> STATIC)`, public include dir
  `include/`; **owns** its HAL; nothing above it on its interface include path.
- Application: `add_library(deluge_app ...)` (or executable) that links a chosen
  `libdeluge_<board>`; **no** HAL path on its include path.
- HAL: built inside `libdeluge`, no upward includes.
- Board executables: app + `libdeluge_<board>` + `platform_libs`.
- CMake presets per board (`rza1`, `host_sim`, future `<x>`).
- Keep `dbt` working by mapping current commands to the `rza1` preset.

---

## 7. Risks & mitigations

| Risk | Mitigation |
|------|------------|
| Audio latency/glitches from the callback redesign | Feature-flag the new path; A/B measure worst-case render vs. DMA deadline before switching; keep old path until proven |
| Realtime regressions from added indirection | API uses callbacks/ISRs (no virtual dispatch in the hot path); keep audio render monomorphic; measure cycles per phase |
| Cache coherency bugs around DMA | Explicit `cache_clean/invalidate` in `libdeluge`'s memory API; audit every former inline cache call |
| PIC protocol assumptions leaking into the app | Confine the entire PIC enum/protocol to the RZA1 `libdeluge` impl; app sees only logical events |
| Memory budget (3MB SRAM + 64MB SDRAM) | Region descriptors from `libdeluge`; allocator reads bounds from the API; track size each phase |
| DSP intrinsics (NEON/NE10/asm) non-portable | Keep portable C fallback paths in the app's DSP; gate NEON behind a capability; NE10 in `platform_libs` |
| Scope/merge churn over 231k LOC | Strangler-fig + per-subsystem PRs + the shrinking-allowlist lint; RZA1 board boots at every commit |
| Vendor HAL (105k LOC) is large to maintain | Unchanged and isolated inside `libdeluge/rza1/hal`; new ports provide their own |

---

## 8. Definition of done

1. No application file includes any HAL or `libdeluge`-implementation header (lint
   is green with an **empty** allowlist); the app sees only `<libdeluge/...>`.
2. The application builds against the `host_sim` `libdeluge` and passes the unit +
   QEMU suites.
3. The RZA1 board builds, boots, and is within baseline tolerance on flash/RAM
   size and audio latency.
4. A second `libdeluge` (host sim) runs the application on a desktop.
5. `porting_guide.md` + `libdeluge/template/` exist; a newcomer can stub the API
   surfaces and link the app.

---

## 9. Suggested first PRs (low-risk, high-signal)

Ordered for the **testability/CI-first** driver:

1. **Phase 0 lint** (dependency-direction check, seeded with the 53-file
   allowlist) + baseline metrics doc. No code moves, immediate guardrail.
2. **Phase 1** `add_library(libdeluge ...)` + app target with no HAL on the app's
   include path, plus the directory carve. RZA1 binary stays the regression
   oracle.
3. Relocate `scheduler_api.h`/`clock_type.h` into `include/libdeluge/` to prove
   the dependency-direction rule on the one already-clean surface.
4. Convert the **clock/timers** group (7 files) behind `<libdeluge/clock.h>` as
   the worked example the rest of the subsystems follow.
5. Stand up a **minimal `host_sim` `libdeluge`** (audio stub/file, MIDI virtual,
   display to PNG) reusing `tests/*/mocks`, and a CI job that builds the app +
   `host_sim` and runs the suites. This is the payoff: the application now builds
   and tests without hardware.
