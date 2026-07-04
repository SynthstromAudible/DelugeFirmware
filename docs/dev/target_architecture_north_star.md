# Deluge target architecture — the north star

Status: **integrating overview** (living doc). This is the single picture that ties the
in-flight architectural efforts together: what the end-state is, what the stable
interfaces are, what depends on what, and in what order it lands. It does *not* re-specify
the individual subsystems — each has (or will have) its own doc; this one is the map.

Scope spans **two repos**: `DelugeFirmware` (the application and its components, today C++)
and `deluge-sdk` (the Rust BSP/HAL/platform — the strategic hardware target). See
"The two repos" below for how they relate.

---

## The thesis

The Deluge today is a monolith: one flat address space, one heap, the `Song` god-object,
the application fused to the hardware. Almost every architectural effort in flight is, at
bottom, the **same move** —

> **Decompose the monolith into components separated by stable, language-neutral
> interfaces.**

That single move is worth doing because it *simultaneously* unlocks four things we
otherwise have to chase separately:

1. **Per-component language choice.** Once a component sits behind a stable interface, it
   can be C++ or Rust independently — and migrate when (and only when) Rust earns it.
2. **The CLAP boundary.** The synth becomes a CLAP plugin — a component with a
   well-known plugin interface — usable on-device *and* in a desktop host.
3. **Fault isolation.** A component with a defined interface and its own memory can be an
   MMU protection domain: a crash is contained, not fatal (see the supervisor design).
4. **Independent restartability.** A contained component can be torn down and rebuilt in
   place — warm app-restart without a full reboot.

Language, plugin-ization, isolation, and restartability are all **per-component decisions
you can only make once the components exist.** So componentization is the spine; the rest
hang off it.

## Hard constraints (decided — design *around* these)

- **The synth is a CLAP plugin and stays C++ forever.** The Deluge sound must be
  sample-accurate and bit-reproducible — "that Deluge sound." It is never rewritten in
  Rust. It plugs in at the **L3 whole-instrument boundary** (CLAP); foreign synths plug in
  at the same altitude. (See `synth_state_decoupling.md`, and the L1/L3 two-boundary
  decision below.)
- **Rust owns the HAL and BSP.** `deluge-sdk` (`rza1l-hal`, `deluge-bsp`) is the strategic
  platform; the C++ application runs on it via the libdeluge C ABI (already proven on
  hardware). New low-level work goes to Rust.
- **Language follows benefit, component by component.** No big-bang rewrite. A component
  migrates to Rust only where Rust pays (safety, isolation, reuse). Engine and UI are
  *candidates*; the synth is *pinned C++*; the platform is *Rust*.
- **C ABI is the inter-language boundary.** Where a C++ component talks to a Rust component
  (or vice-versa), the contract is a C ABI — language-neutral, stable, the same shape the
  libdeluge boundary already established.

## The two extensibility altitudes (decided)

Foreign code plugs into the Deluge at exactly two nesting levels (`extensibility-two-
boundaries`):

- **L1 — engine API** ("bring a source, inherit the Deluge"): a sound *source* slots into
  the native voice/patcher machinery. Lean, native, the registry *is* the substrate.
- **L3 — whole-instrument CLAP** ("bring a synth, inherit the sequencer"): a complete
  instrument plugs in where the Deluge's own synth does. **CLAP lives only at L3.**

The Deluge's own synth is itself an L3 CLAP citizen — it is not privileged over a
third-party plugin except that it ships in-tree and defines the reference sound.

## The component model

```
  ┌───────────────────────────────────────────────────────────────────────┐
  │  APPLICATION COMPONENTS  (DelugeFirmware; C++ today, mixed future)    │
  │                                                                       │
  │   ┌──────────┐   ┌──────────┐   ┌──────────────────────────────────┐  │
  │   │   UI     │   │ Sequencer│   │  Synth  (L3 CLAP, PINNED C++)    │  │
  │   │ (cand.)  │   │ /engine  │   │   ├ L1 source engines            │  │
  │   │          │   │ (cand.)  │   │   └ voice / patcher / DSP        │  │
  │   └────┬─────┘   └────┬─────┘   └─────────────────┬────────────────┘  │
  │        │   stable interfaces (C ABI at lang seams)│                   │
  │   ┌────┴──────────────┴───────────┐   ┌───────────┴───────────────┐   │
  │   │  Project Model (POD, SDRAM)   │   │  Resource / asset manager │   │
  │   │  serialization swappable      │   │  (cache + leases + arena) │   │
  │   └────────────────┬──────────────┘   └────────────┬──────────────┘   │
  └────────────────────┼───────────────────────────────┼──────────────────┘
                       │   libdeluge C ABI (the platform boundary)
  ┌────────────────────┴───────────────────────────────┴──────────────────┐
  │  PLATFORM  (deluge-sdk; Rust)                                         │
  │   supervisor / app-loader · Embassy runtime · deluge_alloc arenas     │
  │   deluge-bsp drivers · rza1l-hal (MMU, GIC, cache, SSI, SDHI, …)      │
  └───────────────────────────────────────────────────────────────────────┘
```

Each box is a candidate fault domain / restart unit / language unit, *once* its interface
is stable. The dashed seams are where C ABIs live.

## The two repos

- **`DelugeFirmware`** — the application and its components. Where the C++ lives today, and
  where component extraction / decoupling / project-model work happens. The app is being
  carved off the hardware (the libdeluge boundary) and off the `Song` monolith (project
  model).
- **`deluge-sdk`** — the Rust platform: `rza1l-hal` (registers, MMU, GIC, cache),
  `deluge-bsp` (drivers), `deluge-alloc` (arenas), the Embassy runtime, the app-loader, and
  (planned) the supervisor. Already runs the C++ app via the libdeluge C ABI on real
  hardware.

Relationship: **deluge-sdk is the platform the application targets.** The libdeluge C ABI
is the contract between them. A third BSP (host-sim) targets the same boundary for off-
target testing. As components migrate to Rust, some may *move repos* (app component → an
SDK crate) — the boundary is what stays fixed, not the file locations.

## How the in-flight efforts map onto the spine

Every active effort is a step toward componentization. Grouped by what they enable:

| Effort | Doc | Enables |
|---|---|---|
| **libdeluge boundary** (allowlist→0) | *(gap: boundary spec)* | The platform seam; the fault-domain edge |
| **Rust BSP/HAL bring-up** | deluge-sdk crate READMEs | The Rust platform under the boundary |
| **Embassy scheduler migration** | *(in deluge-sdk)* | Separable executors → restartable task groups |
| **Allocator redesign** (`deluge_alloc`) | `allocator_redesign.md` | The memory substrate; per-component arenas |
| **Resource / asset manager** | `resource_manager.md` | Residency-managed cache; arena split |
| **Project-model extraction** | *(gap: end-state doc)* | State off `Song`; restart-from-model; serialization swap |
| **Synth untangling / dsp-next** | `synth_state_decoupling.md`, `synth_phase4_voice_kernel.md` | The synth as an extractable L3 CLAP component (pinned C++) |
| **Supervisor + MMU SYS/APP** | `app_supervisor_recovery.md` (app) · deluge-sdk `system-supervisor.md` (kernel) | Fault isolation + warm restart |
| **Host-sim / emulator** | `host_emulator.md` | Off-target testability of every component |

## Dependency & sequencing graph

The integrating artifact — what blocks what. Read top-to-bottom as "must largely precede."

```
  Rust BSP/HAL  ─┐
  Embassy        ├─► libdeluge C ABI boundary (allowlist → 0)
  allocator      ┘            │
                              ├─► project-model extraction ──┐
                              │      (state off Song)        │
                              ├─► resource-mgr + app arena ──┤
                              │                              │
                              ▼                              ▼
                    component interfaces stable        app = f(model, handles)
                              │                              │
        ┌─────────────────────┼───────────────┐              │
        ▼                     ▼               ▼              ▼
   synth → L3 CLAP      engine extract    UI extract   supervisor + warm restart
   (pinned C++)         (lang TBD)        (lang TBD)         │
                                                             ▼
                                                  MMU SYS/APP split
                                                  (best-effort → guaranteed)
```

Cross-cutting, parallel to all of it: **host-sim** (test substrate) and the
**Tier 0+1 safety net** (audio-fade + WDT) — the latter ships *now*, independent of
everything above, as the interim crash behaviour while the real isolation matures.

Notable ordering facts this graph makes explicit:

- The **supervisor/restart** work can't be *trustworthy* until the boundary is a hard wall
  **and** project-model + app-arena exist (you need a clean teardown target). Tier 0+1
  ships first regardless; full restart waits.
- **Synth → CLAP** depends on component interfaces being stable but is otherwise
  independent of the supervisor track — it can proceed in parallel.
- **MMU isolation** is the *last* step, not the first: it upgrades an already-working
  best-effort restart to guaranteed. Don't front-load it.

## Gaps this reveals (candidate next docs)

Writing the north star surfaces the specific point docs that are missing or stale. In
rough priority:

1. **libdeluge boundary spec** — the C ABI surface as it stands today + the target shape.
   It's the spine seam and only exists implicitly (the allowlist, scattered headers).
2. **Project-model end-state** — the POD model, ownership (app vs SYS-resident), the
   serialization boundary (swappable C++ → Rust+serde), and how restart re-binds to it.
   Referenced everywhere; no doc exists.
3. **Component interface catalog** — what *are* the stable interfaces between UI /
   sequencer / synth / model / resource-mgr? Names the seams the whole thesis rests on.
4. **CLAP integration** — how the synth-as-CLAP boundary works on-device vs. in a desktop
   host; the L1/L3 altitudes made concrete. (May partly live in the synth docs already.)
5. **SVC / syscall ABI** — the app→platform call gate for the MMU user-mode split (an open
   question in `system-supervisor.md`).

## Open strategic questions

- **Per-component language triggers:** what concretely makes a component "earn" Rust?
  (Safety-critical state? Reuse across firmwares? A clean interface already in place?)
- **Repo location as components migrate:** when an app component goes Rust, does it move
  into a `deluge-sdk` crate, or does `DelugeFirmware` grow Rust crates? (Boundary stays
  fixed; physical home is open.)
- **"Process" semantics:** how far do we take the synth/engine/UI as *isolated processes*
  (MMU domains, restartable) vs. merely *modules*? The supervisor work makes full processes
  possible; how many do we actually want?
- **CLAP on-device cost:** does the L3 CLAP boundary impose real-time overhead that
  conflicts with the sample-accurate constraint, and where's the line?
