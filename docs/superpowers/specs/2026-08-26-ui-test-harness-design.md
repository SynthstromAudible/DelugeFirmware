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
scene Popup/General
scene Session
pad 0 0 255 0 0
pad 4 3 10 20 30
led Play on
led Record blink 500
indicator upper 3 3 2 0
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
  one), and an LED/indicator shadow added to `deluge_control_set_led`/`set_indicator`.
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

## 10. Amendments during implementation (2026-08-28)

Recorded against `tests/harness/`, `tests/spec/shell/`, `tests/spec/harness/`, `src/bsp/host/` and
`src/ui/core/led_frame.h` as the harness was built (Tasks 1–5) and its review rounds. Each line
names the change and why it deviates from the draft above.

**§0 Level 0 (`tests/harness/`)**
- `tests/harness/` carries its **own copy** of `src/shell/fixed_vector.h`'s shape
  (`tests/harness/fixed_vector.h`), rather than including the shell's — level 0 is compiled
  straight into the host BSP (`sim/CMakeLists.txt`'s `deluge_host`), which must not depend on
  `src/shell` at all, so a shared header was not available without inverting that dependency.
- The parser (`parse_script`) is C++, and the BSP that consumes it is C, so `ui_script.h` gains an
  `extern "C"` façade (`ui_script_load`, `ui_script_next_due`, `ui_script_rewind`) plus a
  `UiScriptStepKind` C enum mirroring `StepKind`, pinned against it by `static_assert`s in the same
  header's C++ block — the two orderings cannot silently drift apart.

**§3 Snapshot format — `.state` category order and line width**
- `sorted_text()`'s ordering is a **fixed category order**, not a flat ASCII sort as the draft's
  prose implies: `scene` (insertion order — a nested-scene breadcrumb reads top-down as pushed),
  then `led`, `pad`, `indicator`, `lock`, `notice`, ASCII-sorted within each of those, then anything
  with an unrecognized first word, last. The draft's own worked example only reads correctly under
  this rule (`indicator` sorting `lower` before `upper` regardless of insertion order requires a
  category sort, not a plain one). `Shell::state_lines()` was reformatted to spec G §3's shape
  exactly (`scene <Kind>[/<qualifier>]`, `lock <reason>`, `notice <Kind> <arg>`) to match.
- `kStateLineBytes` is **96**, not the informal 48 the first pass used: `"notice "` (7) + the
  longest `Notice::Kind` name (`ClearWorking`, 12) + a space + a 47-character popup text
  (`Notice::text`/`PopupScene::text_` are both 48-byte buffers) is 67 characters, and a line that
  truncates silently is a golden that has stopped telling two screens apart. 96 leaves headroom
  for a longer kind name or a wider surface without another audit.

**§4.1 Level 2 — boot path, LED lines, run errors**
- `RunOptions` gained **`std::optional<SceneId> initial_scene`**: `Shell` has `init(SceneId)` and
  `boot(std::string_view)`, not the single `init("test")` an earlier draft assumed. Unset ->
  `shell.boot("test")` (the default path, ending on the `Boot` splash); set -> `shell.init(*id)`.
- Level 2's `led` lines are read from `LedSurface::frame`'s **declared** `(LedId, LedState)` pairs
  (`Shell::derive_leds`'s composed state, before it is flattened to a resolved boolean), not from
  `FakeSurfaceSet`'s `set_led` shadow. The shadow only ever sees the boolean `led_is_on(state,
  now_ms)` resolved to for that instant, which cannot distinguish a steady LED from a blinking one
  caught in its on phase — so spec G §3's `led <Name> blink <period>` is only representable by
  reading the declaration, one layer above where the first pass looked.
- `RunResult::error` is a single `std::optional<ParseError>` **reused** for run-time failures (an
  unknown button/encoder-role/notice-kind/`Dirty` name, an unparseable `board` spec) as well as
  parse failures — not a second `RunError` type. The two are the same fact to whoever reads the
  message ("a script line names something that does not exist"), and a second type would make
  every caller test two fields to ask one question. Since `Step` carries no line number, a run
  failure's line is recovered by re-counting *significant* source lines the same way the parser
  numbered them.
- An `encoder` step's edge count is computed **wide, then range-checked**, never narrowed on
  faith: at the default 2 edges/detent, `on_encoder_edges`'s `int8_t` parameter would silently wrap
  a 64-detent request to −128 and reverse the turn. Both levels (the level-2 runner and
  `host_script.c`) reject anything outside `[-128, 127]` with "too many detents for one
  `on_encoder_edges` call" / "too many detents for one encoder step" rather than truncate it.

**§4.2 Level 3 — encoder order, board timing, timeouts, boot snapshot, fixture**
- `BoardProfile::encoder_by_index`'s Deluge instance, and `host_script.c`'s copy of it, is
  `{Vertical, Horizontal, Tempo, Select, GoldUpper, GoldLower}` — firmware index **4 is the upper**
  gold knob (`mod1`), index **5 the lower** (`mod0`) — cited to `hid/encoder_input.cpp:56-63` (the
  order) and `hid/encoders.h:80-81` (which knob each `mod` is). A first pass had this swapped; Task
  5's `tests/spec/harness/tables_spec.cpp` pins both copies against `deluge_profile()` directly so
  a future edit cannot silently re-swap them.
- **`IndicatorId` is `{Lower = 0, Upper = 1}`** (`src/ui/core/led_frame.h`), not the reverse an
  earlier pass had: index 0/1 **is** the `which` argument `SurfaceSet::set_indicator` takes, which
  `BoardSurfaceSet` passes straight through to `deluge_control_set_indicator`, and on the wire that
  is legacy's mod-encoder number — mod encoder 0 being `mod0`, the **lower** gold encoder
  (`gui/views/view.cpp:1403-1468` -> `hid/led/indicator_leds.cpp:241`, `hid/encoders.h:80-81`).
  Getting this wrong sends every knob-indicator write to the physically wrong knob on hardware, a
  swap neither index would otherwise catch (both are in range and accept the same data) — pinned by
  a dedicated `static_assert`-plus-spec pair in `tests/spec/shell/surfaces_spec.cpp`.
- **`board` is applied before the surfaces/board descriptor exist**, not as an in-loop step, at
  both levels: it names the panel set the surfaces are *built* from (there is no way to resize a
  live surface pool or a latched `DelugeBoard` descriptor), so both the level-2 runner and
  `host_script_init` pre-scan for a leading `board` step and reject one at a non-zero `at_ms`, or
  one that arrives after any other step (level 3: the descriptor is latched by its first reader).
- **Level 3's build configuration is `-DDELUGE_SIM_X64=ON`**, in a directory separate from any
  pre-existing `build-sim` configured without it (re-running `cmake` with the flag over a
  differently-configured cache produces a half-migrated tree, not the CI configuration) — CI itself
  configures `build-sim` fresh on every run, so the directory name collision does not arise there.
- **Two timeout backstops, in different units, sized so the in-process one always wins the race**:
  the offline audio capture's own deadline is moved to the script's last `at_ms` **+ 5 audio-
  seconds** (`HOST_SCRIPT_DEADLINE_MARGIN_S`), and expiring it exits 2 with a diagnosis; the ctest
  `TIMEOUT` (`sim/CMakeLists.txt`, computed per script from its own last `at_ms`) is
  **`8 * (last_s + 5) + 30` wall seconds** — the `8×` is the in-process deadline converted to wall
  time at a render rate as slow as 0.125x (measured ~0.26x on a `-DDELUGE_SIM_X64=ON` build, so
  better than 2x headroom), `+ 30` covers process start-up and a cold CI runner. A flat wall margin
  (the first pass's `last_s + 30`) only dominates for a short script and silently inverts past
  roughly 4-5 seconds of script timeline, at which point ctest kills the process first and replaces
  the useful "the app stopped reaching its `quit`" message with a bare "Timeout" — scaling, not
  adding, is what keeps the ordering correct for scripts of any length.
- **`boot.txt`'s first snapshot is at 50 ms, not 0 ms.** The legacy app's first OLED blit happens
  from inside its main loop, *after* its first input poll — probing put the first blit between 5
  and 10 ms, so 50 ms is a 5x margin over the observed value and still the app's very first screen.
  A snapshot naming a panel that has never blitted is a hard failure (exit 2, "snapshot taken
  before the app's first frame"), not a black `.pbm` or a silently-absent file — either of those
  would be a golden that cannot tell "blank screen" from "never drawn".
- **The fixture card (`tests/fixtures/card/`) gained `SETTINGS/MIDIFollow.XML`**, beyond the
  four `.keep`-only directories: the app writes its own built-in MIDI-follow defaults there on
  first boot when the file is absent (4136 bytes, byte-identical on every run), so leaving it
  uncommitted would mean every `ctest` run dirties the working tree with a new untracked file.
  Committing it makes the card a fixed point and the run a read; it does not affect the snapshots
  either way.
- **`ui_app_scripts_<name>` / `ui_app_scripts_<name>_diff` is a globbed pair**, one per
  `tests/ui-scripts/app/*.txt` (`CONFIGURE_DEPENDS`, so a new script is picked up on the next build
  with no manual `cmake` re-run), rather than one test hand-listing every script — the runner writes
  `got/app/<name>/`, the `_diff` test (`DEPENDS` on its runner) byte-compares that tree against
  `tests/golden/ui/app/<name>/` via `scripts/ui_snapshot_diff.py`.
- **Level 3's `.state` is a strict subset of level 2's**, forced by what the BSP can physically
  observe (`tests/harness/README.md`'s "Level 3 limitations" section is the canonical write-up;
  this bullet mirrors it): `model`/`notice` are shell-only channels (`notify::Dirty`,
  `notify::Notice`) the legacy app has none of, so a level-3 script that uses either is rejected at
  load with "shell-level command in an app script", **exit 2** — not silently ignored or dropped.
  There are **no `scene` lines** (the legacy UI has no scene names) and **no `lock`/`notice`
  lines** (neither the input lock nor the notice queue reaches the BSP). **LEDs are `on`-only,
  never `blink`**: legacy blinking is the *app* toggling `deluge_control_set_led` on a timer, so
  the BSP only ever sees the resolved boolean for the instant sampled and cannot tell a steady LED
  from a blinking one caught in its on phase — level 2, by contrast, reads the shell's own
  *declared* `LedFrame` (before it is flattened to that boolean), so only level 2 can report
  `led Record blink 500`. **`board` is accepted only at 0 ms and only before anything has read
  `deluge_board()`**: the descriptor is latched by its first reader, so a `board` step arriving any
  later — even at 0 ms, if a non-`board` step preceded it — is rejected at load rather than applied
  too late to matter.

**§5 Determinism — the `shell-no-clock` ratchet, and the level-3 table split**
- The `shell-no-clock` layer rule (`scripts/check_layer_rules.py`) is extended from
  `src/shell/**` alone to **also cover `src/ui/**`**, one row per glob (the checker matches a
  single glob per rule row), with the regex widened to
  `\bdeluge_clock_|\bstd::chrono\b|\bclock\(\)|\btime\(` — the scenes and widgets a shell drives
  must be exactly as clock-free as the shell itself, so a scene reaching for `std::chrono` directly
  (bypassing the shell's `now_ms`) is caught the same way. Baseline is 0 for both rows; probed with
  a `std::chrono::steady_clock::now()` call in a `src/ui` file (exit 1) and confirmed
  `--update` changes nothing once the probe is removed.
- **`host_script.c`'s button-matrix and encoder-order name tables (and their two lookups,
  `ui_script_button_xy`/`ui_script_encoder_index`) moved into a new, dependency-free
  `src/bsp/host/host_script_tables.c` (+ `host_script_tables.h`)**, compiled into both the host BSP
  targets (`sim/CMakeLists.txt`'s `deluge_host`/`deluge_loadcheck`) and `deluge_spec`
  (`tests/spec/CMakeLists.txt`) — `host_script.c` proper pulls in BSP statics (the OLED/pad/LED/knob
  shadows, `host_rendered_frames`) and so cannot be linked into the test suite, but the two lookups
  and their tables have no such dependency. `tests/spec/harness/tables_spec.cpp` iterates every
  `ButtonId` with a matrix position (found by reversing `deluge_profile().button_by_matrix`) and
  every `EncoderRole` (reversing `encoder_by_index`), calling `ui_script_button_xy`/
  `ui_script_encoder_index` and comparing the result against the profile — a mismatch names the
  entry. A third function, `ui_script_button_name_at(x, y)`, was added to the same pair so
  `host_script.c`'s indicator-LED-name reverse lookup (`led_name`) could read the one copy of the
  table instead of a second one duplicated back into `host_script.c` — duplication is exactly the
  drift this split exists to prevent. `scripts/check_platform_boundary.py` stays green: the new
  files include nothing from `src/deluge`.

**§6 Migration acceptance — the double-run hook**
- `RunOptions` gained **`const SceneRegistry* second_registry = nullptr`**. When set,
  `expect_script_goldens` runs the script **twice** — once against its own `registry` parameter,
  once against `*second_registry` — into sibling `got/<name>/` and `got/<name>-second/` trees, and
  diffs those two trees **against each other** (byte-for-byte, with an extra/missing-file check),
  never against a recorded golden: there is nothing to record ahead of time when the whole point is
  that two independent scene tables must agree with each other. A mismatch names the first
  differing (or missing-on-one-side) file in sorted order. This is the mechanism spec F's migration
  acceptance needs ("the same script produces the same snapshots against the legacy UI and the new
  scene") without inventing a second golden format for it.
