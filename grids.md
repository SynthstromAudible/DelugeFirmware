# Topo Drums — porting the MI Grids engine into the Deluge

Design & progress tracker for integrating the generative drum-pattern engine from the
original Mutable Instruments **Grids** (V1, 2011, `~/Code/eurorack/grids`) into the Deluge
community firmware as a per-kit generative mode.

> **Naming / licensing:** "Grids" is a registered trademark of Mutable Instruments and must
> not be used for this feature — the UI name is **"Topo Drums"**. The engine is ported from
> GPLv3 code (Copyright 2011/2012 Emilie Gillet); the Deluge firmware is GPLv3, so the port
> is license-compatible. Original copyright headers are retained on all ported code, and the
> port is credited in docs.

## Feature summary (agreed design)

- **What**: A generative drum mode for **Kit tracks**. The engine's 3 banks (BD/SD/HH)
  drive 3 kit lanes, which default to **GateDrums on gate outputs 1/2/3** but can be any
  drum (internal sound / MIDI / gate).
- **Engine** (from Grids V1):
  - *Topographic mode*: 25 hand-made reference patterns (3 banks × 32 steps, values 0–255)
    arranged on a 5×5 map. X/Y knobs bilinearly morph between the 4 surrounding nodes.
    Per-bank **density** knobs are thresholds against the interpolated level; **randomness**
    adds a per-loop perturbation to levels; level > 192 ⇒ **accent**.
  - *Euclidean mode*: X/Y/randomness become the 3 banks' pattern lengths (1–32), densities
    become note counts (Björklund bitmask LUT).
  - 32-step loop = 1 bar, clocked at 24 PPQN granularity (3 pulses per step).
  - Swing reinterprets the randomness knob (drums mode); ±(randomness·43)>>8 offset.
- **Knobs** (Deluge has 2 mod encoders + 8 mod buttons) — mod buttons select param banks
  while a Topo clip is open:
  - **A**: knob1 = X, knob2 = Y
  - **B**: knob1 = Randomness, knob2 = Swing depth
  - **C**: knob1 = BD density, knob2 = SD density
  - **D**: knob1 = HH density, knob2 = accent threshold
- **Enable**: per-kit menu page ("TOPO DRUMS") gated behind a community-feature toggle
  (`RuntimeFeatureSettingType::EnableTopoDrums`, default off). Enabling on a kit with <3
  drums auto-creates GateDrums on the first 3 gate channels not set to SPECIAL
  (RUN/CLOCK), with conflict warning.
- **Display**: live 32-step pattern on the pads for the assigned lanes (accents brighter,
  playhead), plus **capture pattern to sequence** (writes current steps into note rows,
  disables engine on the clip, undo-safe).

## Integration architecture

| Concern | Approach / seam |
|---|---|
| Engine | `src/deluge/model/generative/topo/` — pure C++ port of `grids/pattern_generator.{h,cc}` + tables from `grids/resources.cc`. No avrlib: local LFSR + fixed-point helpers. |
| Tick hook | Sibling of `Kit::doTickForwardForArp()` (`src/deluge/model/instrument/kit.cpp:1136`), called from the same consumers (session/arrangement). Engine instance owned by `Kit`. |
| Firing | Per step boundary, banks with level > threshold call `drum->noteOnPostArp()` (arp bypassed, `arpNote = nullptr`); sample-accurate gate output via existing `cvEngine` window scheduling. Velocity: accent = 127, normal ≈ 100. |
| Settings | Per-clip `TopoDrumsSettings` on `InstrumentClip` (like `arpSettings`); bank→lane mapping as `bankRows[3]` (note-row indices) in the same struct. Forward-compatible XML tags (`exitTag()` skip pattern). |
| Knobs | `InstrumentClipView::modEncoderAction()` routes to engine when clip's Topo mode is on; mod buttons A–D select the param bank (LED feedback); value popups via `View::displayModEncoderValuePopup()`. |
| Menus | New `HorizontalMenu` appended to `horizontalMenusChainForKit` (`src/deluge/gui/ui/menus.cpp`): Enable, Mode (Topo/Euclidean), Gate/Trigger, Swing, Bank A/B/C lane assign, Reset pattern, Capture. |
| Feature gate | `RuntimeFeatureSettingType::EnableTopoDrums` in `SETTINGS/CommunityFeatures.XML`. |
| Clock | Engine clock unit = pulses (3/step, 96/bar); Phase 2 converts Deluge transport ticks → pulses. Loop follows clip transport; reset on loop wrap / re-launch. |

## Port notes (fidelity)

- RNG: avrlib 16-bit Galois LFSR, polynomial `0xB400` (x^16+x^14+x^13+x^11), initial seed
  `0x21`, updated **once per pulse** (as in `PatternGenerator::Evaluate()`). Perturbations
  drawn at step 0 of each loop: `perturbation = (rand_byte · (randomness>>2)) >> 8`.
- `U8Mix(a, b, bal)` reproduces the AVR-ASM semantics exactly:
  `(b·bal + a·(255−bal)) >> 8` (note: *not* the buggy portable-C version).
- `U8U8MulShift8(a,b) = (a·b) >> 8`.
- Node tables: 25 × 96 bytes (instrument-major, then step). 5×5 `drum_map` node index
  table preserved verbatim. Euclidean LUT: 1024 × uint32 bitmasks.
- Deliberate divergences: instance-based (not static/global); no EEPROM save/load; the
  8-bit hardware output word (`state_`) is replaced by explicit fire/accent masks;
  clock-resolution/tap-tempo/output-clock options dropped (Deluge transport owns tempo;
  MIDI clock handled by the Deluge).
- Golden-value tests are generated from the **original** Grids code compiled on-host with
  pgm_read/avrlib shims, so the port is bit-exact against the hardware behavior.

## Progress

### Phase 1 — Engine port (pure, testable) — COMPLETE
- [x] Research both codebases; agree UX (banks→kit lanes, topo+euclidean, mod-button param
      banks, pads display + capture, name "Topo Drums")
- [x] `topo_rng.h` — faithful LFSR port, seedable
- [x] `topo_pattern_data.{h,cpp}` — node_0..24 + euclidean LUT + drum_map (generated from
      `grids/resources.cc`)
- [x] `topo_engine.{h,cpp}` — settings, `advance()` step/pulse engine with event output,
      `levelAt()`, loop simulation for UI, swing amount
- [x] Unit tests `tests/unit/topo_engine_tests.cpp` (interp, thresholds, accents, euclidean
      vs Björklund reference, RNG determinism, golden streams from original code)
- [x] Unit test build green (`UnitTests` target)
- [x] Firmware build green: full `Release` build in the CI toolchain container
      (`docker run ghcr.io/synthstromaudible/dbt-toolchain:v22 … build --type dev Release`)
      compiles + links both new TUs into `deluge.elf`; `git diff --exit-code` (genfile
      check) clean
- [x] CI style checks green locally: clang-format **19** (`.clang-format-ignore` is unused —
      generated data arrays wrapped in `// clang-format off/on` instead), Ruff 0.16.0
      format+lint on the generator script
- Note: local unit-test CMake build needs gcc-multilib (`-m32`); without sudo, run the
      topo tests standalone, e.g.
      `g++ -std=gnu++20 -DCPPUTEST_MEM_LEAK_DETECTION_DISABLED -I src/deluge -I <cpputest>/include -I <cpputest>/src tests/unit/topo_engine_tests.cpp src/deluge/model/generative/topo/*.cpp <cpputest>/src/CppUTest/*.cpp <cpputest>/src/Platforms/Gcc/UtestPlatform.cpp`
      (cpputest source kept at `/tmp/opencode/cpputest-src` after `dbt.py nuke`)

### Phase 2 — Model, enable flow, triggers out — IN PROGRESS
- [x] `TopoDrumsSettings` (`model/generative/topo/topo_drums_settings.h`) on `InstrumentClip`
      + XML `<topoDrums ...>` (forward-compatible: unknown attrs `exitTag()`; copied in
      `copyBasicsFrom`)
- [x] Lane mapping design change: instead of a `topoBank` field on `Drum`, the clip stores
      `bankRows[3]` (note-row index per bank, -1 = unassigned) — same drum can serve
      different banks in different clips, and no Drum/XML changes were needed
- [x] Kit tick hook: `Kit::doTopoTick()` called from `doTickForwardForArp()`; stateless
      position→pulse mapping `pulseAt(pos) = pos * 96 / loopLength` (one engine cycle per
      clip loop; robust to any loop length / tick magnitude; restarts on clip change,
      seek-back or loop wrap; transport swing is inherited since clip pos advances in
      swung time — engine's own `swing` flag kept for a later refinement)
- [x] Fire via `fireTopoBank()`: SOUND drums `noteOn()` w/ neutral MPE, GATE/MIDI drums
      `noteOnPostArp()` w/ synthesized `ArpNote` (velocity 127 accent / 100 normal);
      gate-hold (whole step) vs trigger (1 pulse) close via `silenceTopoBank()`
- [x] `RuntimeFeatureSettingType::EnableTopoDrums` (default off) + toggle in community
      features menu + l10n string "Topo Drums" (`STRING_FOR_COMMUNITY_FEATURE_TOPO_DRUMS`)
- [x] Enable flow: "TOPO DRUMS" `HorizontalMenu` in a kit chain variant
      (`horizontalMenusChainForKitWithTopo`, chosen in `SoundEditor::getCurrentHorizontalMenusChain()`
      when the feature is on) with items: Enable (auto-assigns lanes via
      `InstrumentClip::ensureTopoLanes()` — reuses GateDrum rows, else creates GateDrums on the
      first non-SPECIAL gate channels, popup warning if not enough), Mode (Topographic /
      Euclidean), Output (Trigger / Gate), Bank A/B/C lane assign ("None" / "Row n")
      — items in `src/deluge/gui/menu_item/topo/`, new l10n strings added
- [ ] Test on hardware: pattern triggers on outputs from a live song
- Note: the horizontal-menu chain requires OLED + the HorizontalMenus community feature (chain
      entry point); a classic Submenu entry (7-seg compatible) can come later if wanted

### Phase 3 — Knobs & menus
- [x] Mod-button param banks A–D (LED feedback) + knob routing in `InstrumentClipView`
      — `modEncoderAction` intercept when topo active; `modButtonAction` override picks bank
      (buttons A–D; E–H inactive); encoder-press = jump LED to value; LEDs synced on view entry.
      Banks: A = X/Y, B = Randomness/Swing (turn CW/CCW = on/off), C = Density A/B,
      D = Density C/Accent threshold
- [x] Value popups; Euclidean relabeling of the same knobs (Len A/B/C, Notes A/B/C);
      14 new l10n strings
- [x] "TOPO DRUMS" `HorizontalMenu` in kit menu chain (enable, mode, gate/trigger,
      bank assign) — done in Phase 2 (swing lives on the knob bank; reset deferred)
- [ ] Test on hardware: knob feel, LED behaviour vs. standard mod system

### Phase 4 — Pad display & capture
- [x] Live pattern rendering on assigned lanes — `InstrumentClipView::renderTopoPatternOverlay()`
      draws `engine.fireAt()/accentAt()` for the current loop on the bank lane rows
      (accents full row colour, normal hits dimmed, never overwrites existing note pixels);
      re-render requested on loop wrap / restart from `Kit::doTopoTick`
- [x] "Capture" item in the Topo menu (`topo::Capture` Selection, springs back to Off) —
      `InstrumentClip::captureTopoPattern()` simulates the next 32 steps
      (`TopoEngine::simulateNextSteps`), clears the lane rows, writes notes
      (accent 127 / normal 100; gate-mode gets step-length notes), disables the engine,
      pops up the note count
      — note: no undo (this firmware's ActionLogger only tracks param changes; deviation
      from plan), mitigation: rows can be cleared manually and topo re-enabled

### Phase 5 — Docs & polish
- [x] Docs: `website/src/content/docs/features/community_features.mdx` §4.6.5 "Topo Drums"
      (prettier-clean with repo config)
- [x] Credit: engine ported from Mutable Instruments Grids (GPLv3, Emilie Gillet); no
      trademark use — header comments on all topo engine files + docs section
- [ ] CHANGELOG: repo CHANGELOG.md is a stub pointing at delugecommunity.com; website
      changelog is generated per release — nothing to add here
- [ ] 7-seg: topo menu is only reachable via horizontal menus (OLED + HorizontalMenus
      feature); consider classic submenu later
