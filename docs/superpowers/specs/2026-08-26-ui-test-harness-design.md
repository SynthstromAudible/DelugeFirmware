# UI test harness: scripts, snapshots, three levels

**Date:** 2026-08-26
**Branch target:** `next`
**Status:** design, pending review. Spec G of the UI stack redesign
(`2026-08-26-ui-stack-redesign-design.md` §11). Depends on the golden helpers from the
gfx plan (`page_major_to_pbm`, `expect_golden`) and on spec D (shell) for levels 2–3.

Naming: `snake_case` functions/variables; `PascalCase` types.

## 1. Why

UI rendering has zero tests today and the only off-device path is a socket to spark's
panel. Every layer of the redesign is accepted on **byte-identical frames**, and the
migration (spec F) is accepted on *the same script producing the same snapshots* against
the legacy UI and the new scene. That needs one script format, one snapshot format, and
harnesses at three levels that share them.

The host BSP already provides the hard parts: a deterministic clock
(`DELUGE_HOST_DETERMINISTIC` drives both clocks off the rendered-frame count), an input
event queue behind `deluge_control_poll_event`, env-driven pad injection
(`DELUGE_HOST_PAD`), a folder-backed card (`DELUGE_SD_ROOT`) and WAV capture.

## 2. Script format

deluge-sdk's headless format, one step per line: `<at_ms> <command> <args…>`; `#` comments.

```
0       board 128x48+5,128x64+0   # host-app level only: select the panel set
0       snapshot boot
500     button Clip down          # ButtonId names (spec D §2)
520     button Clip up
600     encoder Select +1         # EncoderRole names; signed detents
700     pad 3 5 down 100          # grid coords; velocity optional (default 255)
750     pad 3 5 up
800     released                  # DELUGE_EVENT_NO_PRESSES
900     model ClipContent 3       # shell level only: raise Dirty::ClipContent for clip 3
950     notice Error 248          # shell level only: push a Notice
1000    snapshot after-clip
1200    quit
```

- Times are milliseconds on the harness clock: the shell's `now_ms` argument at level 2;
  the deterministic frame-count clock at level 3 (`ms = frames * 1000 / sample_rate`).
- Parsing is one dependency-free unit, `tests/harness/ui_script.{h,cpp}`
  (`parse_script(string_view) -> FixedVector<Step, 256>`; `Step{at_ms, kind, args}`),
  shared by levels 1–2 and compiled into the host BSP for level 3.
- Unknown commands are errors, not warnings.

## 3. Snapshot format

Per `snapshot <name>`, written into the output directory:

- `<name>.oled<N>.pbm` — one **P1 PBM** per OLED surface, via `page_major_to_pbm`. A
  deliberate deviation from the SDK's PNG: no image codec in C, text diffs read in PRs.
  `scripts/pbm2png.py` converts for viewing.
- `<name>.state` — text, one item per line, sorted so diffs are stable:

```
scene Session
scene Popup/General
pad 0 0 255 0 0
pad 4 3 10 20 30
led Play on
led Record blink 500
ring upper 3 3 2 0
lock loading
notice Error 248
```

Only non-black pads, only lit/blinking LEDs, `scene` lines top→bottom, `lock` and
`notice` only when present.

`diff -r got/ golden/` is the assertion. `GFX_GOLDEN_UPDATE=1` re-records (the helper from
the gfx plan). A snapshot that is identical to the previous one in the same script is
still written — silence is not a pass.

## 4. Three levels

| Level | Under test | Driver | Location |
|---|---|---|---|
| 1 — toolkit | one widget/component | literal data + POD state → golden frames (toolkit spec §9); no script | `tests/spec/ui/*_spec.cpp`, goldens `tests/golden/ui/widgets/` |
| 2 — shell + scenes | `Shell` on a **fake `SurfaceSet`** (host canvases, no libdeluge) with a fake model, or the real model built for the host; scripts may use `model`/`notice` | `tests/spec/shell/*_spec.cpp` + scripts `tests/ui-scripts/shell/*.txt`, goldens `tests/golden/ui/shell/` |
| 3 — whole app | `deluge_host` headless | `DELUGE_HOST_SCRIPT=<file> DELUGE_HOST_SNAPSHOT_DIR=<dir> DELUGE_HOST_DETERMINISTIC=1 DELUGE_SD_ROOT=tests/fixtures/card` | scripts `tests/ui-scripts/app/*.txt`, goldens `tests/golden/ui/app/`, ctest `ui_app_scripts` |

### 4.1 Level 2 fake surfaces

`FakeSurfaceSet` implements the `SurfaceSet` interface the shell consumes with host-side
`OwnedCanvas`es sized from a `DelugeBoard` literal (so the new device's descriptor can be
tested before its BSP exists), a `PadGrid`, an `LedSurface`, and recorders for what the
shell would have flushed (`blits`, `column_pairs_sent`, `led_writes`). Tests assert on
snapshots *and* on the recorders (e.g. "a clean frame produced no blit").

### 4.2 Level 3 host BSP additions

- `DELUGE_HOST_SCRIPT`: the host BSP parses the script at init and, on each rendered
  frame, injects every step whose `at_ms` has been reached into the existing event queue
  (`ev_queue`, `deluge_control_poll_event`). `button` names map through the Deluge profile's
  matrix table; `encoder` deltas go to `deluge_encoder_take_edges` as two edges per detent.
- `snapshot`: written from the last blitted frame per display (`deluge_display_blit_oled_n`
  keeps a shadow copy), the pad shadow (`deluge_control_set_pad_columns` already maintains
  one), and an LED/ring shadow added to `deluge_control_set_led`/`set_indicator`.
- `board`: `DELUGE_HOST_DISPLAYS` from the gfx plan, settable from the script for
  convenience.
- `quit`: `quick_exit(0)` after flushing snapshots (the app's main loop never returns).
- `tests/fixtures/card/` is a committed minimal card root (one song, one kit, one sample)
  so boot → `LoadingScene` → `SessionScene` is reproducible.

## 5. Determinism rules

- No wall clock in the UI. `now_ms` is the shell's tick argument; level 3 supplies the
  frame-count clock. Any `deluge_clock_*` use inside `ui-*`/`shell`/`scenes` is a layer
  violation (checker rule).
- Blink phase is `now_ms / period_ms`; scroll text and tweens advance on `now_ms` only.
- Scenes must not read uninitialised POD state; `Scene` registry instances are
  value-initialised.
- Random elements (screensaver starfield) take a seed from the shell; the harness seeds 0.

## 6. Migration acceptance (used by spec F)

The same level-2 script runs twice — once with the legacy `UI` behind `LegacySceneAdapter`,
once with the new scene — and the snapshots must match. Each migrated scene ships with at
least: boot-into-scene, one navigation, one edit gesture, one popup, all snapshotted.
Menus are covered per item kind: script enters the menu, turns, edits, snapshots.

## 7. CI

- Levels 1–2 ride the existing `tests.yml` x86 job (they are ctests under `tests/`).
- Level 3 adds a step: `cmake -B build-sim -S sim -G Ninja -DDELUGE_SIM_X64=ON`,
  `cmake --build build-sim --target deluge_host`, `ctest --test-dir build-sim -R ui_app_scripts`.
  The ctest runs every `tests/ui-scripts/app/*.txt` and diffs against `tests/golden/ui/app/`.
- On failure the job uploads `got/` as an artifact next to the golden so the diff is
  viewable; `scripts/pbm2png.py` renders a side-by-side for the PR.

## 8. Where existing tooling goes

| Today | Target |
|---|---|
| `DELUGE_HOST_PAD`, `DELUGE_HOST_PAD_AT_FRAME`, `DELUGE_HOST_PAD_RELEASE_FRAME` | superseded by `DELUGE_HOST_SCRIPT`; kept until their WAV-harness users move to scripts |
| `tests/spec/starfield_preview.cpp`, `rainfall_preview.cpp` (ASCII eyeballing) | remain as look-and-tell tools; the screensaver scene also gets level-2 goldens with seed 0 |
| spark panel over `DELUGE_HOST_LINK` | unchanged; interactive use only |

## 9. Open questions

1. Whether level 3 should also capture audio (`DELUGE_HOST_WAV`) in the same script run so
   a UI gesture's audible effect is asserted alongside the frame — useful for
   "press pad → note sounds" but couples two harnesses; defer until the WAV harness is
   script-driven.
2. Golden churn: a `Style` change re-records every OLED golden. Accept it (the diff is the
   review) or split goldens by widget so churn is local — decide after the first Style
   change.
