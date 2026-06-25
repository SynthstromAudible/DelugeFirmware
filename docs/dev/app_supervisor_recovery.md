# Application / system decoupling — crash isolation & warm app-restart

Status: **design sketch** (not started). Companion to the kernel-side design in the
deluge-sdk repo (`docs/system-supervisor.md`), which owns the supervisor, the MMU
SYS/app split, and the fault vectors. *This* doc is the **firmware (C++ app) side**:
what the application has to become so that the platform can tear it down and rebuild
it in place after a crash, without a full FSB→SSB→SDRAM→remount cold boot.

It builds directly on three pieces of in-flight work and is gated behind them:

- **libdeluge boundary** — the app↔system C ABI (the upcall allowlist; reached zero
  on the USB-MIDI boundary branch). The boundary is the fault-domain edge.
- **project-model extraction** — pulling project state out of the `Song` god-object
  into a live POD model (Phase 1 carved `RuntimeViewState` / `PersistentViewState`).
- **resource-manager / allocator redesign** — `deluge_alloc` + a residency-managed
  asset pool; the lever for giving the app its own arena.

---

## The problem, restated

A CPU fault in app code today lands in `handle_cpu_fault()` and spins forever; the
only recovery is a power cycle. The Tier 0+1 safety net (see
`docs/dev/`… fault-handler mute + watchdog) upgrades that to a **clean warm reboot**,
but a warm reboot re-runs the *entire* boot chain — FSB → (SSB) → firmware copy out
of SPI ROM → SDRAM/peripheral re-init → SD re-mount → app cold start. Seconds, and
all unsaved work is gone.

We want the **next** tier: keep the *system* resident (HAL, scheduler runtime, audio
core, drivers, allocator, SD mount, supervisor) and tear down + rebuild only the
*application* (song / sequencer / UI / synth runtime). Outcome: "brief silence + a
UI message" instead of a multi-second cold boot, with audio and storage never going
down. The give-up path (full warm reboot) becomes the crash-loop fallback, not the
first response.

There is no partial-reset shortcut on the RZ/A1: you cannot re-enter `main()` without
re-running `reset_handler` and leaving peripherals/stacks/C-runtime undefined. So
"restart the app" must be an explicit, designed teardown/rebuild — not a reset.

## What the application must become

The app becomes a **restartable unit** defined by three properties. The whole design
is downstream of these:

1. **It owns nothing the system needs.** All app mutable state lives in an **app
   arena** (its own allocator region) and on **its own stack(s)/executor**. Teardown
   = reset the arena + discard the executor — O(1), no walking of system stack frames.

2. **It talks to the system only through the libdeluge C ABI.** No shared mutable
   globals, no raw system pointers held across a restart, no direct peripheral pokes.
   Every such edge is a hole that survives the teardown and re-poisons the rebuild.

3. **It is reconstructible from durable inputs:** `app_main(model, sys_handles)`.
   Restart = re-run that function. The app is a *pure function of* (the project model,
   the live system handles).

If those hold, restart is mechanically:

```
fault in app code
  → supervisor catches it (kernel side; fault vector, not reboot)
  → fade audio to silence            (system stays up)
  → discard app executor / stack
  → reset app arena                  (frees all app memory at once)
  → re-bind / reload the project model
  → app_main(model, sys_handles)     (app re-attaches its voices)
```

## The four things this requires from the firmware

### A. A hard boundary (prerequisite, nearly there)

The libdeluge ABI must be the *only* app→system edge, **and** the app must hold no
raw system pointers it depends on across a restart, and make no system allocations
that outlive it. The upcall allowlist hitting zero is necessary but not sufficient:
the remaining audit is "what state does the app touch that survives it?" — globals,
cached pointers into driver/HAL structs, allocations on the system heap.

Deliverable: an **app-state ownership audit** — for every piece of app-reachable
mutable state, classify it `APP` (in the arena, disposable) or `SYS` (behind the ABI,
survives). Anything ambiguous is a bug to fix before the cut.

### B. State partition (the long pole — project-model extraction)

Every mutable byte lands on one side of the line. This *is* the project-model
extraction work; the flat `Song` god-object is the thing in the way. The app's state
must be (1) confined to the app arena and (2) reconstructible. Phase 1's
`RuntimeViewState` / `PersistentViewState` split is the first cut of this; the
remaining `Song` decomposition is the bulk of it.

### C. App arena (the allocator lever)

Today app + system share one heap, so you *cannot* "free all app memory" without a
precise ownership map. Give the app a **private region**; teardown = arena reset.
System allocations must not live in it. The resource-manager / allocator-redesign work
is the enabler.

A nice consequence of the resource manager: leased assets (samples, wavetables) can be
**system-cached, not app-owned** — so they *survive* the restart and are re-leased,
rather than reloaded from SD. Decide this per asset class; default samples → survive.

### D. App on its own stack/executor (why this is a deluge-sdk-side job)

In the cooperative C++ scheduler the app and system share one stack, so a fault deep in
app code has system frames *below* it — you can't reset the app without unwinding
through them. This is the strongest reason the restartable-app machinery should be
built on the **Embassy / deluge-sdk** side: there the app's tasks are a respawnable
group on the main executor, while audio (separate `InterruptExecutor`) and drivers live
on executors that survive untouched. Teardown = drop the app task group, respawn it.

For the C++ app running on the Rust BSP via the libdeluge ABI, this means: the app's
entry must be a single `app_main`-style call the supervisor can (re)invoke, and the
app must not assume it runs exactly once.

## The key design decision: what does the app restart *from*?

Two options; the second is the interesting target and ties to project-model-in-SDRAM:

- **(a) Reload the project from SD.** Simple, definitely-clean, slowish, loses
  post-save edits.
- **(b) Keep the live project model resident in *system-owned* (protected) memory and
  re-bind to it.** If the model is a POD owned and protected by the system, and the app
  is just *code + transient runtime* operating on it, then restart = discard transient,
  re-attach to the still-live model. Fast, preserves unsaved work. Risk: the crash may
  have been *caused by* bad model data → validate the model on re-attach, fall back to
  (a) if it looks corrupt.

Option (b) splits "the data model" (system-owned, durable, protected) from "the
application" (code + transient, disposable) — a cleaner cut than app-vs-system, and the
natural end-state of the project-model extraction. **Recommended target: (b) with (a)
as the validated fallback.**

## Confidence levels (the MMU fork)

The *structure* is identical with or without memory protection; only the trust differs.
This is owned on the kernel side (deluge-sdk `system-supervisor.md`), but it dictates
how much the firmware can rely on a recovered state:

- **Best-effort (no protection):** a wild app pointer may have corrupted system memory
  *before* the fault → you recover into a possibly-poisoned system. `longjmp` +
  arena-reset + model-revalidate usually works; silent corruption (the E338 family)
  isn't even caught.
- **Guaranteed (MMU app-domain):** the wild write traps as a data abort *on the
  protection boundary*, before corrupting the system. The supervisor's memory is
  provably intact → restart is deterministic.

Build A–D first for best-effort restart; the MMU domain upgrades it to guaranteed.

## Sequencing

1. **App-state ownership audit** (A) — depends on the boundary work; produces the
   APP/SYS classification.
2. **Finish state partition + app arena** (B, C) — the long pole; already moving for
   other reasons (project-model, resource-manager).
3. **`app_main` entry + re-invocability** (D, firmware half) — make the app a function
   of (model, handles), safe to call more than once.
4. *(kernel side)* supervisor + app executor group + fault→recover path.
5. *(kernel side)* MMU app-domain → guaranteed restart; revisit option (b) with the
   model in the protected system domain.

Tier 0+1 ships first and independently as the safety net; `deluge_warm_reboot()`
becomes the crash-loop fallback underneath this app-restart path.

## Open questions

- Audio continuity: the app *produces* the audio graph, so teardown→rebuild has a
  no-app window. Confirm the system audio core fades and holds gracefully (reuse the
  Tier-1 liveness/mute net), then the rebuilt app re-attaches. Glitchless is not a
  goal; "brief silence" is.
- Crash-loop policy: how many restarts in what window before falling back to full warm
  reboot? Where is the counter kept (system-owned, survives app restart)?
- Model validation: what's the cheapest integrity check on re-attach for option (b)?
- Which leased asset classes survive vs. reload (resource-manager policy).
