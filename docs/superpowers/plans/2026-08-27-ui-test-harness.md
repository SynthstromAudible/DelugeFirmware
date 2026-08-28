# UI test harness Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** One script format, one snapshot format, and harnesses at three levels — toolkit (exists), shell + scenes on a fake `SurfaceSet`, and the whole `deluge_host` app headless — so every UI layer is accepted on byte-identical frames and the migration can run the same script against legacy and new.

**Architecture:** A dependency-free script parser (`tests/harness/ui_script`, C++ with a C façade so the host BSP can compile it) and a snapshot writer (`.oled<N>.pbm` per surface + a sorted `.state` text). Level 2 drives a `Shell` (spec D plan) on `FakeSurfaceSet` from a script in-process; level 3 teaches the host BSP to inject script steps into its existing event queue on the deterministic frame clock and to write snapshots from its display/pad/LED shadows, then `quick_exit`. A Python differ compares `got/` to `golden/` and re-records on `GFX_GOLDEN_UPDATE=1`.

**Tech Stack:** C++26/C17, CMake, CppSpec, Python 3 (stdlib only), the host BSP (`src/bsp/host`), `sim/CMakeLists.txt`.

**Spec:** `docs/superpowers/specs/2026-08-26-ui-test-harness-design.md` (spec G). Depends on the gfx golden helpers (`tests/spec/gfx/support/golden.h`, `pbm.h`) and on the shell plan (`docs/superpowers/plans/2026-08-27-ui-shell.md`: `Shell`, `FakeSurfaceSet`, `Shell::state_lines()`, `deluge_profile()`, `test_board_profile()`).

## Global Constraints

- Branch: `ui/harness` off `ui/shell`. "Builds" = `./dbt build Debug` compiles to the known ThinLTO link failure, `deluge_host` links (`cmake --build build-sim-gfx --target deluge_host`), host suite green (`cmake --build build/tests --config Debug && ctest --test-dir build/tests -C Debug --output-on-failure`). Level 3 additionally: `cmake -B build-sim -S sim -G Ninja -DDELUGE_SIM_X64=ON && cmake --build build-sim --target deluge_host && ctest --test-dir build-sim -R ui_app_scripts`.
- Script format exactly spec G §2: `<at_ms> <command> <args…>`, `#` comments, commands `board`, `snapshot`, `button <Name> down|up`, `encoder <Role> <±n>`, `pad <x> <y> down [vel]|up`, `released`, `model <Dirty> [clip]`, `notice <Kind> <arg>`, `quit`. Unknown commands are errors.
- Snapshot format exactly spec G §3: `<name>.oled<N>.pbm` (P1 via `page_major_to_pbm`) and `<name>.state` with sorted lines `scene …` (top→bottom, unsorted among themselves), `pad x y r g b` (non-black only), `led <Name> on|blink <period>`, `ring upper|lower a b c d`, `lock <reason>`, `notice <Kind> <arg>`. Identical consecutive snapshots are still written.
- Determinism (spec G §5): no wall clock anywhere under test; level 2 clock = script `at_ms`; level 3 clock = `host_rendered_frames * 1000 / 44100`. Seeds are 0.
- Naming: `snake_case`; `PascalCase` types; `k` constants. Python: ruff-clean (pre-commit runs it).
- No new Python dependencies (`pbm2png.py` writes PNG with `zlib` + `struct`).
- Never commit `src/bsp/rust/Cargo.lock`; never touch `src/bsp/rza1`; commit per "Commit" step after `./dbt format`.

---

## File structure

```
tests/harness/ui_script.h / .cpp          parse_script() -> FixedVector<Step,256>; C façade ui_script_* for the host BSP
tests/harness/snapshot.h / .cpp           write_oled_pbm(), write_state(), state line builders (pad/led/ring)
tests/harness/README.md
tests/spec/harness/ui_script_spec.cpp     level-0 tests of the parser and the state formatter
tests/spec/shell/support/script_runner.h  run_script(): drives a Shell on FakeSurfaceSet; expect_script_goldens()
tests/spec/shell/scripts_spec.cpp         runs every tests/ui-scripts/shell/*.txt against tests/golden/ui/shell/<script>/
tests/ui-scripts/shell/boot.txt           first level-2 script
tests/ui-scripts/app/boot.txt             first level-3 script
tests/golden/ui/shell/<script>/*.pbm|.state
tests/golden/ui/app/<script>/*.pbm|.state
tests/fixtures/card/{SONGS,KITS,SYNTHS,SAMPLES}/.keep   minimal card root (empty song set)
scripts/ui_snapshot_diff.py               diff got/ vs golden/; GFX_GOLDEN_UPDATE=1 re-records; exit 1 on diff
scripts/pbm2png.py                        P1 PBM -> PNG (stdlib), side-by-side mode for PR artifacts
src/bsp/host/host_script.c                DELUGE_HOST_SCRIPT injection, shadows, snapshot, quit (C, calls the ui_script C façade)
```

Modified: `tests/spec/CMakeLists.txt` (add `tests/harness/*.cpp` to `deluge_spec`), `sim/CMakeLists.txt` (compile `tests/harness/ui_script.cpp` + `src/bsp/host/host_script.c` into the host BSP; `ui_app_scripts` ctest), `src/bsp/host/host_bsp.c` (call `host_script_pump()` from `host_input_pump()`; shadows in `deluge_display_blit_oled_n`, `deluge_control_set_led`, `deluge_control_set_indicator`; `board` override before `deluge_board()`), `src/bsp/host/host_main.c` (init/quit hooks), `.github/workflows/tests.yml` (level-3 step + artifact upload), `scripts/check_layer_rules.py` (`shell-no-clock` extended to `src/ui/**`), `docs/dev/host_emulator.md` (script env vars).

---

### Task 1: Script parser and `.state` formatter (level 0)

**Files:**
- Create: `tests/harness/ui_script.{h,cpp}`, `tests/harness/snapshot.{h,cpp}`, `tests/spec/harness/ui_script_spec.cpp`, `tests/harness/README.md` (stub: format tables copied from spec G §2–3)
- Modify: `tests/spec/CMakeLists.txt` (add the two `.cpp` to `deluge_spec`; the specs driver glob already includes `harness/*_spec.cpp` — verify `create_specs_driver` recurses; register the nested spec name `harness/ui_script_spec` the same way the ui specs are)

**Interfaces:**
- Produces (`deluge::harness`, header usable from C++; C façade below):
  ```cpp
  enum class StepKind : uint8_t { Board, Snapshot, Button, Encoder, Pad, Released, Model, Notice, Quit };
  struct Step { uint32_t at_ms; StepKind kind; char name[32]; /* snapshot name / button name / role name / dirty name / notice kind */
                int32_t a = 0, b = 0, c = 0; bool down = false; char text[64]; /* board spec, notice arg */ };
  struct ParseError { uint16_t line; char message[64]; };
  struct ScriptResult { FixedVector<Step, 256> steps; std::optional<ParseError> error; };   // FixedVector: copy of src/shell/fixed_vector.h's shape into tests/harness/fixed_vector.h (tests/harness may not include src/shell — it is compiled into the host BSP)
  ScriptResult parse_script(std::string_view text);
  // snapshot.h
  std::string oled_pbm(std::span<const uint8_t> page_major, uint16_t w, uint16_t h);   // = page_major_to_pbm
  struct StateLines { FixedVector<std::array<char, 48>, 256> lines; void add(std::string_view); std::string sorted_text() const; };  // "scene" lines keep insertion order and come first
  void add_pad_line(StateLines&, uint8_t x, uint8_t y, uint8_t r, uint8_t g, uint8_t b);   // skipped when black
  void add_led_line(StateLines&, std::string_view name, bool blink, uint16_t period_ms);
  void add_ring_line(StateLines&, bool upper, const uint8_t levels[4]);
  bool write_file(std::string_view dir, std::string_view file, std::string_view contents);  // mkdir -p dir
  ```
- C façade (`extern "C"`, in `ui_script.h` under `#ifdef __cplusplus` guards):
  ```c
  typedef struct UiScriptStep { uint32_t at_ms; uint8_t kind; char name[32]; int32_t a, b, c; uint8_t down; char text[64]; } UiScriptStep;
  int ui_script_load(const char* path);                       // 0 ok; -1 open error; line number of a parse error otherwise
  int ui_script_next_due(uint32_t now_ms, UiScriptStep* out); // pops the next step whose at_ms <= now_ms; 0 = none
  ```

- [ ] **Step 1: Failing spec** — `tests/spec/harness/ui_script_spec.cpp`:

```cpp
#include "harness/ui_script.h"
#include "harness/snapshot.h"
#include "cppspec.hpp"
using namespace deluge::harness;
// clang-format off
describe ui_script("harness::ui_script", $ {
	it("parses every command with its arguments", _ {
		auto r = parse_script("0 board 128x48+5,128x64+0\n0 snapshot boot   # comment\n500 button Clip down\n520 button Clip up\n"
		                      "600 encoder Select +1\n700 pad 3 5 down 100\n750 pad 3 5 up\n800 released\n900 model ClipContent 3\n"
		                      "950 notice Error 248\n1000 snapshot after-clip\n1200 quit\n");
		expect(r.error.has_value()).to_be_false();
		expect(r.steps.size()).to_equal(size_t(12));
		expect(r.steps[2].kind).to_equal(StepKind::Button); expect(std::string_view(r.steps[2].name)).to_equal("Clip"); expect(r.steps[2].down).to_be_true();
		expect(r.steps[4].a).to_equal(1); expect(r.steps[5].a).to_equal(3); expect(r.steps[5].b).to_equal(5); expect(r.steps[5].c).to_equal(100);
		expect(r.steps[6].c).to_equal(0);                       // pad up -> velocity 0
		expect(r.steps[8].a).to_equal(3); expect(r.steps[9].a).to_equal(248);
		expect(r.steps[11].kind).to_equal(StepKind::Quit); expect(r.steps[11].at_ms).to_equal(uint32_t(1200));
	});
	it("defaults pad velocity to 255 and rejects unknown commands with the line number", _ {
		expect(parse_script("0 pad 1 1 down\n").steps[0].c).to_equal(255);
		auto bad = parse_script("0 snapshot a\n10 frobnicate\n");
		expect(bad.error.has_value()).to_be_true(); expect(bad.error->line).to_equal(uint16_t(2));
	});
	it("rejects non-monotonic times", _ { expect(parse_script("10 released\n5 released\n").error.has_value()).to_be_true(); });
	it("formats state lines sorted with scenes first", _ {
		StateLines s; add_pad_line(s, 4, 3, 10, 20, 30); add_pad_line(s, 0, 0, 0, 0, 0); s.add("scene Session"); s.add("scene Popup/General");
		add_led_line(s, "Play", false, 0); add_led_line(s, "Record", true, 500); uint8_t lv[4] = {3, 3, 2, 0}; add_ring_line(s, true, lv);
		expect(s.sorted_text()).to_equal("scene Session\nscene Popup/General\nled Play on\nled Record blink 500\npad 4 3 10 20 30\nring upper 3 3 2 0\n");
	});
});
// clang-format on
int harness_ui_script_spec(int argc, char** const argv) { return CppSpec::parse(argc, argv).add_spec(ui_script).exec().is_success() ? EXIT_SUCCESS : EXIT_FAILURE; }
```

- [ ] **Step 2: Run to see it fail.**

- [ ] **Step 3: Implement.** Parser: line by line; strip `#…`; skip blank; `at_ms` integer; command word switch; `button` requires `down|up`; `encoder` accepts `+1`/`-3`; `pad` `x y down [vel]|up`; `model` `<Dirty> [clip]`; `notice` `<Kind> <arg>` (arg stored as text and, if numeric, in `a`); times must be non-decreasing; `board` stores the spec in `text`. `sorted_text()`: scene lines first in insertion order, then the rest `std::sort`ed (ASCII), each line `\n`-terminated. C façade keeps a static `ScriptResult` and cursor.

- [ ] **Step 4: Run** — PASS; builds.

- [ ] **Step 5: Commit** — `git add tests/harness tests/spec/harness tests/spec/CMakeLists.txt && git commit -m "test(harness): ui script parser, C façade and .state formatter"`

---

### Task 2: Snapshot differ and PBM→PNG (Python)

**Files:**
- Create: `scripts/ui_snapshot_diff.py`, `scripts/pbm2png.py`, `tests/spec/harness/snapshot_tools_spec.cpp` (runs the two scripts through `std::system` on fixture dirs it writes under the test's temp dir — skip if `python3` is missing, with a printed reason)

**Interfaces:**
- `python3 scripts/ui_snapshot_diff.py <got_dir> <golden_dir>`: exit 0 when every file in `golden/` exists in `got/` byte-identical and `got/` has no extra `.pbm`/`.state` files; exit 1 otherwise printing a unified diff per `.state` and a per-row XOR summary per `.pbm` (`row 12: 3 pixels differ`); with `GFX_GOLDEN_UPDATE=1` in the environment it copies `got/` over `golden/` (creating it) and exits 0. `--list` prints the files it would compare.
- `python3 scripts/pbm2png.py <in.pbm> [<other.pbm>] -o <out.png> [--scale 4]`: P1 → PNG (1-bit → grayscale 8-bit, black-on-white), optional side-by-side with a 4-px red gutter, nearest-neighbour scale.

- [ ] **Step 1: Failing spec** — writes `golden/a.state` = "scene X\n", `got/a.state` = "scene X\n" → exit 0; modifies `got/a.state` → exit 1; sets `GFX_GOLDEN_UPDATE=1` → exit 0 and `golden/a.state` updated; writes a 4×2 P1 PBM and converts → PNG file exists with the PNG signature bytes.
- [ ] **Step 2: Run to see it fail.**  - [ ] **Step 3: Implement** (stdlib only: `argparse`, `difflib`, `pathlib`, `zlib`, `struct`).  - [ ] **Step 4: Run** — PASS; `pre-commit run --files scripts/ui_snapshot_diff.py scripts/pbm2png.py` (ruff) passes.
- [ ] **Step 5: Commit** — `git commit -m "test(harness): snapshot differ with re-record, pbm2png"`

---

### Task 3: Level 2 — script runner over `Shell` + `FakeSurfaceSet`, first script and goldens

**Files:**
- Create: `tests/spec/shell/support/script_runner.h`, `tests/spec/shell/scripts_spec.cpp`, `tests/ui-scripts/shell/boot.txt`, `tests/ui-scripts/shell/popup.txt`, goldens under `tests/golden/ui/shell/boot/`, `.../popup/`
- Consumes (shell plan): `Shell(SurfaceSet&, const BoardProfile&, const SceneRegistry&, Services, ShellConfig)`, `Shell::init`, `tick(now_ms)`, `on_board_event`, `on_encoder_edges`, `state_lines()`, `FakeSurfaceSet` (`pbm(index)`, `state()`), `deluge_profile()`, `kDelugeBoard`, `test_board_profile()`, `kTestBoard`, `notify::dirty().raise/raise_clip`, `notify::notices().push`.

**Interfaces:**
- Produces:
  ```cpp
  namespace uitest {
  struct RunOptions { uint32_t tick_ms = 10; const DelugeBoard* board = &kDelugeBoard; const BoardProfile* profile = &deluge_profile(); };
  struct RunResult { uint32_t snapshots = 0; std::optional<deluge::harness::ParseError> error; std::string log; };
  RunResult run_script(std::string_view script_text, const SceneRegistry&, Services, std::string_view out_dir, RunOptions = {});
  void expect_script_goldens(Self& self, std::string_view script_name, const SceneRegistry&, Services, RunOptions = {});   // runs tests/ui-scripts/shell/<name>.txt into <build>/got/shell/<name>/ and diffs against tests/golden/ui/shell/<name>/ (GFX_GOLDEN_UPDATE re-records) — the diff is done in C++ (byte compare per golden file, extra-file check) so the spec needs no Python
  }
  ```
  Runner semantics: build `FakeSurfaceSet` + `Shell`; `init("test")`; then for `t = 0; t <= last step's at_ms; t += tick_ms`: apply every step with `at_ms <= t` not yet applied (in order) — `board` (only at 0 ms: rebuild with `DELUGE_HOST_DISPLAYS`-style spec parsed into a `DelugeBoard` literal with a static `DelugeDisplayDesc[4]`), `button` (name → `ButtonId` via `button_name()` reverse lookup → matrix coords via the profile's `button_by_matrix` reverse lookup → `DelugeInputEvent{BUTTON, x, y, down}`), `encoder` (role name → index via `encoder_by_index` reverse → `on_encoder_edges(index, delta * edges_per_detent)`), `pad` (`DelugeInputEvent{PAD, x, y, vel}`), `released` (`NO_PRESSES`), `model` (`dirty().raise[_clip]`), `notice` (`notices().push(...)` built from Kind + arg), `snapshot` (after `tick(t)`: write `<name>.oled<i>.pbm` for every surface and `<name>.state` = shell `state_lines()` + fake pads/LEDs/rings via the Task 1 builders), `quit` (stop); then `tick(t)`.

- [ ] **Step 1: Failing spec** — `scripts_spec.cpp`: a registry with the shell's scenes plus a test `DemoScene` (kind `FirstApp`, renders `title("DEMO")` on Main and a 3-pad pattern on the grid, lights `Play` on, handles `Button Clip` by pushing itself with `arg+1` and drawing `arg`); scripts:

`boot.txt`:
```
0     snapshot boot
100   snapshot boot-again          # identical, still written
```
`popup.txt`:
```
0     snapshot start
50    button Clip down
70    button Clip up
100   snapshot after-clip          # DemoScene arg 1 on top
200   notice Text SAVED
210   snapshot popup               # Popup/General on top; state has "scene Popup/General" and "notice Text SAVED"
3000  snapshot popup-gone
3100  released
3200  quit
```
Assertions: `expect_script_goldens(self, "boot", …)`; `expect_script_goldens(self, "popup", …)`; plus explicit: after the run, `RunResult.snapshots == 4` for popup; a script with `frobnicate` reports the parse error and writes nothing.

- [ ] **Step 2: Run to see it fail.**  - [ ] **Step 3: Implement** the runner.  - [ ] **Step 4: Record** goldens (`GFX_GOLDEN_UPDATE=1 ctest -R scripts_spec`), inspect every `.state` and PBM (ASCII in the report), run again without the flag → PASS.
- [ ] **Step 5: Commit** — `git add tests/spec/shell tests/ui-scripts tests/golden/ui/shell && git commit -m "test(harness): level-2 script runner over Shell + FakeSurfaceSet; boot and popup scripts"`

---

### Task 4: Level 3 — host BSP script injection, shadows, snapshots, `quit`; `ui_app_scripts` ctest; CI

**Files:**
- Create: `src/bsp/host/host_script.c`, `tests/ui-scripts/app/boot.txt`, `tests/golden/ui/app/boot/`, `tests/fixtures/card/{SONGS,KITS,SYNTHS,SAMPLES}/.keep`
- Modify: `src/bsp/host/host_bsp.c` (`host_input_pump()` at `:488` calls `host_script_pump(now_ms)` first; `deluge_display_blit_oled_n` keeps a per-index shadow (`uint8_t oled_shadow[4][1024]` + geometry); `deluge_control_set_led`/`set_indicator` update `led_shadow[64]`/`ring_shadow[2][4]`; the pad shadow already maintained by `deluge_control_set_pad_columns` is exposed via `host_pad_shadow()`), `src/bsp/host/host_main.c` (`host_script_init()` before board init so `board` can apply; `quit` → `host_script_write_pending(); quick_exit(0)`), `sim/CMakeLists.txt` (compile `tests/harness/ui_script.cpp` into the host BSP target — it is C++ with the C façade; add the `ui_app_scripts` ctest), `.github/workflows/tests.yml`, `docs/dev/host_emulator.md`
- Ports/uses: `ev_push(kind, x, y, value)` (`host_bsp.c:462`), `encoder_edges[6]` + `kSparkEncoderToFwIndex` (`:474-484`, use the firmware index directly — the script's role names map through `deluge_profile()`'s `encoder_by_index` reverse table compiled into the host BSP? No: the BSP is C and must not depend on the app; embed a small static table `role name → fw index` in `host_script.c` matching `hid/encoder_input.cpp:56-63` (`Vertical=0, Horizontal=1, Tempo=2, Select=3, GoldLower=4, GoldUpper=5`) and `button name → (x,y)` matching `definitions_cxx.hpp:59-84` (copy the coordinate table; a spec in Task 5 cross-checks it against `deluge_profile()` so the two cannot drift), the deterministic clock (`host_bsp.c:104-153`: `ms = host_rendered_frames * 1000 / 44100`), `quick_exit` (`host_main.c:101`).

**Interfaces:**
- Env: `DELUGE_HOST_SCRIPT=<file>` (enables), `DELUGE_HOST_SNAPSHOT_DIR=<dir>` (default `./snapshots`), requires `DELUGE_HOST_DETERMINISTIC=1` (the pump asserts it and exits 2 with a message otherwise), `DELUGE_SD_ROOT` as today.
- Semantics: on each `host_input_pump()` call, `now_ms = host_rendered_frames * 1000 / 44100`; every step with `at_ms <= now_ms` is applied in order: `button` → `ev_push(BUTTON, x, y, down)`; `pad` → `ev_push(PAD, x, y, vel)`; `released` → `ev_push(NO_PRESSES, 0, 0, 0)`; `encoder` → `encoder_edges[idx] += delta * kEdgesPerDetent` (the legacy divisor from `hid/encoders.h:29-50`, cited) and wake the encoder task as an inbound rotation does; `model`/`notice` → not supported at level 3 (error at load: "shell-level command in an app script"); `snapshot` → write `<name>.oled<i>.pbm` from `oled_shadow[i]` for `i < display_count`, and `<name>.state` from the pad shadow (non-black), `led_shadow` (on only — the legacy blinker state is not visible to the BSP; document), `ring_shadow`; `board` → only at 0 ms and only before `deluge_board()` was first called (else error); `quit` → write pending, `quick_exit(0)`.
- `.state` at level 3 has no `scene` lines (the legacy UI has no scene names) — document; spec F's adapter adds them when the shell hosts.

`tests/ui-scripts/app/boot.txt`:
```
0      snapshot boot
2000   snapshot idle
2100   quit
```
(2 s of deterministic time ≈ 88 200 frames; the host loop renders blocks until then.)

`sim/CMakeLists.txt` ctest:
```cmake
file(GLOB UI_APP_SCRIPTS ${CMAKE_SOURCE_DIR}/../tests/ui-scripts/app/*.txt)
foreach(script IN LISTS UI_APP_SCRIPTS)
  get_filename_component(name ${script} NAME_WE)
  add_test(NAME ui_app_scripts_${name}
           COMMAND ${CMAKE_COMMAND} -E env DELUGE_HOST_DETERMINISTIC=1 DELUGE_HOST_SCRIPT=${script}
                   DELUGE_HOST_SNAPSHOT_DIR=${CMAKE_BINARY_DIR}/got/app/${name} DELUGE_SD_ROOT=${CMAKE_SOURCE_DIR}/../tests/fixtures/card
                   $<TARGET_FILE:deluge_host>)
  add_test(NAME ui_app_scripts_${name}_diff
           COMMAND python3 ${CMAKE_SOURCE_DIR}/../scripts/ui_snapshot_diff.py ${CMAKE_BINARY_DIR}/got/app/${name} ${CMAKE_SOURCE_DIR}/../tests/golden/ui/app/${name})
  set_tests_properties(ui_app_scripts_${name}_diff PROPERTIES DEPENDS ui_app_scripts_${name})
endforeach()
```
(Adjust `${CMAKE_SOURCE_DIR}/..` to however `sim/CMakeLists.txt` reaches the repo root today — it already references `../src`.) The pair is what `ctest -R ui_app_scripts` runs.

- [ ] **Step 1: Prove the failure mode** — run `deluge_host` with `DELUGE_HOST_SCRIPT` set before implementing: nothing happens (no snapshots). Then implement.
- [ ] **Step 2: Implement** `host_script.c` (parse via `ui_script_load`, pump via `ui_script_next_due`, writers with `mkdir -p`), the shadows, the `board` override, the ctest, and the CI step:

```yaml
      - name: UI app scripts (level 3)
        run: |
          cmake -B build-sim -S sim -G Ninja -DDELUGE_SIM_X64=ON
          cmake --build build-sim --target deluge_host
          ctest --test-dir build-sim -R ui_app_scripts --output-on-failure
      - name: Upload UI snapshots on failure
        if: failure()
        uses: actions/upload-artifact@v4
        with: { name: ui-app-snapshots, path: build-sim/got }
```
(Match the workflow's existing job layout and action versions; place it after the x86 tests step.)

- [ ] **Step 3: Record** — `GFX_GOLDEN_UPDATE=1 ctest --test-dir build-sim -R ui_app_scripts`; inspect `boot.oled0.pbm` as ASCII (`python3 scripts/pbm2png.py` for the report) — it must show the legacy boot/session screen, not blank; run again without the flag → PASS. Run twice more and confirm byte-identical output (determinism).
- [ ] **Step 4: Run** everything per Global Constraints; `docs/dev/host_emulator.md` gains the three env vars and the script format pointer.
- [ ] **Step 5: Commit** — `git add src/bsp/host sim/CMakeLists.txt tests/ui-scripts/app tests/golden/ui/app tests/fixtures .github/workflows/tests.yml docs/dev/host_emulator.md && git commit -m "test(harness): level-3 host scripts — injection on the deterministic clock, snapshots, ui_app_scripts ctest, CI"`

---

### Task 5: Determinism rule, table cross-check, migration hook, docs

**Files:**
- Create: `tests/spec/harness/tables_spec.cpp`; Modify: `scripts/check_layer_rules.py` (+ baseline), `tests/harness/README.md`, `tests/spec/shell/support/script_runner.h`, `docs/superpowers/specs/2026-08-26-ui-test-harness-design.md` (Amendments; `git add -f`)

- [ ] **Step 1: Ratchet** — extend the `shell-no-clock` symbol rule's `from_glob` to cover `src/ui/**` and `src/shell/**` (one row per glob if the checker takes one glob per row), regex `\bdeluge_clock_|\bstd::chrono\b|\bclock\(\)|\btime\(`; baseline 0 for both; probe with a `std::chrono::steady_clock::now()` call in a ui file → exit 1; remove.
- [ ] **Step 2: Table cross-check** — `tables_spec.cpp` compares the C table in `host_script.c` (expose it via `ui_script_button_xy(const char* name, uint8_t* x, uint8_t* y)` and `ui_script_encoder_index(const char* role)`) against `deluge_profile()` for every `ButtonId` with a matrix position and every `EncoderRole`; a mismatch fails.
- [ ] **Step 3: Migration hook** — `run_script` gains an optional `const SceneRegistry* second_registry`; when set, `expect_script_goldens` runs the script twice and diffs the two `got/` trees against each other (not against goldens) — the spec G §6 acceptance used by spec F; test it with the same registry twice (identical) and with a registry whose demo scene draws one extra pixel (differs, message names the file).
- [ ] **Step 4: Docs** — `tests/harness/README.md`: the three levels, how to add a script, how to re-record, how to read a diff, the level-3 limitations (`model`/`notice` unsupported, no `scene` lines, LEDs on-only). Spec G amendments: FixedVector copy in `tests/harness`, C façade, `.state` sorting rule (scenes first), level-3 limitations, `board` timing rule, the `ui_app_scripts_<name>` + `_diff` test pairing.
- [ ] **Step 5: Run** all suites (levels 0–3); commit — `git add scripts tests/spec/harness tests/harness tests/spec/shell/support && git add -f docs/superpowers/specs/2026-08-26-ui-test-harness-design.md && git commit -m "test(harness): no-clock rule, table cross-check, migration double-run, docs"`

---

## Self-review notes

- **Spec coverage.** §2 script format → Task 1 (parser, errors, monotonic times), §3 snapshot format → Tasks 1, 3, 4; §4 three levels → level 1 exists (toolkit plan), level 2 Task 3, level 3 Task 4; §4.1 fake surfaces → the shell plan's `FakeSurfaceSet` + the runner; §4.2 host additions (`DELUGE_HOST_SCRIPT`, snapshot from shadows, `board`, `quit`, fixtures card) → Task 4; §5 determinism → Tasks 4 (assert `DELUGE_HOST_DETERMINISTIC`), 5 (checker rule; seeds are the shell's `ShellConfig::seed`, already 0 in the runner); §6 migration acceptance → Task 5's double-run; §7 CI → Task 4; §8 existing tooling → untouched (documented in the README).
- **Placeholders:** none — every step has its code or its exact command; the two "verify how `sim/CMakeLists.txt` reaches the root" notes are instructions to read a file, not gaps.
- **Types consistent:** `Step`/`StepKind`/`ParseError`/`ScriptResult` (T1) used by T3/T4 (via the C façade); `StateLines` builders (T1) used by T3/T4; `run_script`/`expect_script_goldens`/`RunOptions` (T3) extended in T5; `ui_script_button_xy`/`ui_script_encoder_index` (T4) tested in T5.
- **Open questions carried:** spec G §9.1 (audio capture in the same run — not built; `DELUGE_HOST_WAV` still works independently), §9.2 (golden churn — accepted; the differ's per-row summary keeps big diffs readable).
