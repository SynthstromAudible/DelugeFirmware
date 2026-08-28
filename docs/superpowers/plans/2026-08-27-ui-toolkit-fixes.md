# UI toolkit fixes (plan B.1) — audit follow-up before the shell

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close every finding of the 2026-08-27 audit of plans A/B/C (`docs/superpowers/audits/2026-08-27-abc-audit-reports.md`) that the user asked to fix before plan D: one real bug, the drain ordering, the missing input-consumption contract, the clip-rect and compositing gaps, the missing tweens and helpers the Rust SDK already had, compile-option/idiom hygiene, and the spec/plan reconciliation.

**Architecture:** No new libraries. `src/gfx` gains a canvas clip rect and grid compositing; `src/ui/core` gains the per-tick `FrameContext` (input claims + focus masking) that both frames share; `src/ui/oled` and `src/ui/grid` widgets move from reading `f.input.*` to the frame's masked accessors; the notify drain moves above the pad-space early-out. Specs B/D and the shell/scenes plans are amended to the decided contract (per-surface render virtuals, `after_render()`, per-surface `Style`).

**Tech Stack:** C++26, CMake, CppSpec; the existing goldens under `tests/golden/{gfx,ui}` are the regression net — **no existing golden may change** except where a task says a legacy quirk is being fixed (none in this plan: the six over-preserved quirks stay, listed for the user in Task 10).

**Spec:** the audit synthesis in the conversation record and `docs/superpowers/audits/2026-08-27-abc-audit-reports.md` (four reports: `audit-architecture`, `audit-cpp-idiom`, `audit-sdk-comparison`, `audit-spark-comparison` sections). Amends `docs/superpowers/specs/2026-08-26-ui-toolkit-design.md` and `docs/superpowers/plans/2026-08-27-ui-shell.md`.

## Global Constraints

- Branch: `ui/toolkit-fixes` off `ui/toolkit`. "Builds" = `./dbt build Debug` compiles to the known pre-existing ThinLTO link failure; `cmake --build build-sim-gfx --target deluge_host` links; `cmake --build build/tests --config Debug && ctest --test-dir build/tests -C Debug --output-on-failure` green (71 + new).
- `git diff <base> --stat -- tests/golden` must be empty at the end of every task unless the task lists the goldens it adds (new files only, never modified ones).
- Layering unchanged: `src/ui/**` includes only `ui/**`, `gfx/**`, std; `src/gfx` is a leaf; `src/notify` is a leaf; `python3 scripts/check_layer_rules.py` and `python3 scripts/check_gfx_layer.py` green after every task. Any new symbol rule the plan adds must start at baseline 0.
- No heap in gfx/ui/notify; POD state; no wall clock; `snake_case` for everything new (camelCase only on the legacy façade methods of moved types, which this plan does not rename).
- Never commit `src/bsp/rust/Cargo.lock`; never touch `src/bsp/rza1`; `./dbt format` before each commit; commit per "Commit" step.

---

## File structure

```
src/gfx/oled/canvas.h / .cpp            clip rect (set_clip/clear_clip/clip()) honoured by draw_char, draw_graphic_multi_line, draw_icon*, invert_area_rounded; memcpy fix; point callback as fn-ptr+ctx; BorderRadius enum class
src/gfx/grid/rect.h                      split_rows / split_cols / inset
src/gfx/grid/composite.h / .cpp          BlendMode, composite(base, overlay, mode, out)
src/gfx/grid/tween.h / .cpp              + smear_scroll_horizontal/vertical, zoom, explode, expand_collapse
src/gfx/grid/rgb.h                       drop redundant operator=; other -Wconversion fixes
src/gfx/grid/pad_grid.h / .cpp           at()/row() contract; tween dims asserts
src/gfx/CMakeLists.txt, src/ui/CMakeLists.txt, src/notify/CMakeLists.txt   DELUGE_COMMON_COMPILE_OPTIONS
src/ui/core/frame_context.h              FrameContext { const InputSnapshot& input; RepaintGate& gate; InputClaims claims; uint32_t tick; }, InputClaims
src/ui/core/input.h                      (unchanged shape) + doc of the edge contract
src/ui/oled/frame.h / .cpp               OledFrame over FrameContext; focused; masked accessors; bounds()/visible(); clip rect set while drawing
src/ui/grid/frame.h / .cpp               GridFrame over FrameContext; focused; interact(); bounds()/visible(); local_events without cache
src/ui/oled/{list,values,columns,scroll_text}.cpp   read input through accessors; claim; two-pass list
src/ui/grid/{effects,keyboard,rows,columns,sidebar}.cpp   read input through accessors; claim
src/ui/oled/meters.cpp, src/ui/grid/waveform.cpp, columns.h, scroll_text.cpp   idiom fixes
src/notify/notice_queue.h / .cpp, src/ui/core/led_frame.h   [[nodiscard]] / bool returns
src/deluge/gui/ui/ui.cpp                 drain above the pad-space early-out
src/deluge/processing/engines/audio_engine.h / .cpp   cull_alert_pending -> std::atomic<bool>
src/deluge/gui/menu_item/filter/filter_container.h   point-callback call site
tests/spec/support/spec_main.h           DELUGE_SPEC macro
tests/spec/**                            exporter boilerplate replaced; new specs per task
docs/superpowers/specs/*.md, docs/superpowers/plans/2026-08-27-ui-shell.md, 2026-08-27-ui-scenes-menus-1.md, src/ui/README.md, src/gfx/README.md
```

---

### Task 1: The two behaviour bugs — `local_events()` cache, drain ordering, atomic cull flag

**Files:**
- Modify: `src/ui/grid/frame.{h,cpp}` (delete the `local_events()` cache: filter the ≤32 events on every call into a caller-visible fixed buffer inside the frame — but *not* keyed; the audit measured filtering as free), `tests/spec/ui/grid_frame_spec.cpp` (+ case), `src/deluge/gui/ui/ui.cpp:375-396`, `src/deluge/processing/engines/audio_engine.{h,cpp}` (`inline bool cull_alert_pending` → `inline std::atomic<bool> cull_alert_pending{false}`; the drain does `exchange(false, std::memory_order_relaxed)`), `src/deluge/gui/ui/notify_drain.cpp`
- Sources: audit-cpp-idiom fix #1; audit-architecture §3 (drain behind the early-out at `ui.cpp:381-390`).

- [ ] **Step 1: Failing spec** — `grid_frame_spec.cpp`: one `PadEvent buf[2]` reused: tick 1 `buf = {{3,5,100,true},{9,9,1,true}}`, `input.pads = buf`, `local_events()` in region `{0,0,8,8}` has 1 entry; overwrite `buf[0] = {4,4,50,true}` (same pointer, same size), call again → the entry is now `(4,4)`. Today it returns the stale `(3,5)`.
- [ ] **Step 2: Run to see it fail.**
- [ ] **Step 3: Implement** — remove `cache_key_`/`cache_valid_`/the lazy fill; `local_events()` filters into `local_[kMaxLocalEvents]` every call (document: O(events), ≤32). Drain order in `ui.cpp`: 

```cpp
void doAnyPendingUIRendering() {
	if (pendingUIRenderingLock) { return; }
	pendingUIRenderingLock = true;
	drain_notifications();            // never starved by pad back-pressure: notices and hook edges must not wait for TX space
	if (deluge_control_pad_output_space() > (kNumBytesInMainPadRedraw + kNumBytesInSidebarRedraw) * 2) {
		doAnyPendingGridRendering();
		doAnyPendingOLEDRendering();
	}
	pendingUIRenderingLock = false;
}
```
(keep the existing "*2 trial" comment). `cull_alert_pending` atomic; drain uses `exchange`.
- [ ] **Step 4: Run** — spec PASS; full suite; builds; `git diff --stat -- tests/golden` empty.
- [ ] **Step 5: Commit** — `git commit -m "fix(ui,notify): local_events never caches; drain runs before the pad back-pressure gate; atomic cull flag"`

---

### Task 2: Compile options on the new targets and the idiom sweep

**Files:**
- Modify: `src/gfx/CMakeLists.txt`, `src/ui/CMakeLists.txt`, `src/notify/CMakeLists.txt` (`target_compile_options(<tgt> PRIVATE ${DELUGE_COMMON_COMPILE_OPTIONS})` on all six targets — find how `src/deluge/CMakeLists.txt:70` gets the variable and whether it is visible to these subdirectories in the `tests/` and `sim/` projects; if not, define it once in a shared `cmake/deluge_common_options.cmake` included by all three projects), then fix every warning that surfaces plus the listed idiom items: `src/gfx/oled/canvas.cpp:606` (`memcpy` for the unaligned `uint16_t` read), `src/gfx/oled/canvas.h:43` + `src/deluge/gui/menu_item/filter/filter_container.h:101` (`std::optional<std::function<void(Point)>>` → `void (*point_callback)(Point, void*) = nullptr; void* callback_ctx = nullptr;`), `src/gfx/oled/canvas.h:34-37` (`enum class BorderRadius : uint8_t { Small, Big }` + call sites `canvas.h:142,281`, `canvas.cpp:241,688`, `ui/oled/frame.h:96-97`, `frame.cpp:147`), `src/gfx/grid/rgb.h:66` (delete the defaulted `operator=`), `src/gfx/grid/pad_grid.h:39-42` + `tween.cpp:8-40` (checked `at()`/`row()` or `_unchecked` names; assert dims in tweens with `uint8_t` loops), `src/ui/oled/meters.cpp` (`int32_t` slot geometry, explicit `static_cast` where legacy truncation is intended — pixels unchanged, goldens prove it), `src/ui/grid/waveform.cpp:19-54` (snake_case locals, init at declaration), `src/ui/grid/columns.h:50,211,216,224-229,276-293` (`float` literals / `std::array`), `src/ui/oled/scroll_text.cpp:53,64` (wrap-safe `static_cast<int32_t>(now - next) >= 0`), `src/notify/dirty_set.h:61,66`, `dirty_set.cpp`, `src/ui/core/led_frame.h:33-46` (`std::to_underlying`), `src/notify/notice_queue.h:34` + `.cpp` + `led_frame.h:25` (`[[nodiscard]] bool`; the four `notice_*` helpers count drops in a `dropped()` counter exposed for the shell's diagnostics), `src/gfx/oled/canvas.h:63` + `src/gfx/grid/pad_grid.cpp:13-15` (contract checks that survive `-DNDEBUG`: route through the firmware's `FREEZE_WITH_ERROR`-equivalent if reachable from a leaf library — it is not; so keep an unconditional `if (!cond) { __builtin_trap(); }` behind a `DELUGE_GFX_CONTRACT(cond)` macro in `gfx/contract.h`, documented).
- Sources: audit-cpp-idiom fixes #2–#14 and §6 tooling.

- [ ] **Step 1:** add the compile options; build `tests/`, `sim/` and `./dbt build Debug`; record the warning count per target before (the audit measured 378 `-Wconversion`-class with an extra flag; the common options will show their own set).
- [ ] **Step 2:** fix warnings and idiom items; the goldens must stay byte-identical (`meters.cpp` and `rgb.h` are the risky ones — run `ctest -R "meters|rows|columns|sidebar|tween|pad_grid|canvas"` after each file).
- [ ] **Step 3:** add specs for the contract macro (a short-span `Canvas` traps — test via `EXPECT_DEATH`-style if CppSpec supports it, else a unit that checks the macro expands to a trap under `NDEBUG` by compiling a probe TU), and for `NoticeQueue::push` returning false when full + `dropped()` incrementing.
- [ ] **Step 4: Run** — everything green; `git diff --stat -- tests/golden` empty; `pre-commit run --all-files` clean for the touched files (clang-tidy naming).
- [ ] **Step 5: Commit** — `git commit -m "chore(gfx,ui,notify): common compile options on the new targets; idiom sweep (aliasing, enum class, to_underlying, nodiscard, float literals, wrap-safe time compares, contracts)"`

---

### Task 3: `DELUGE_SPEC` macro

**Files:** Create `tests/spec/support/spec_main.h`; Modify the 28 spec files under `tests/spec/{gfx,ui,notify}` (and any under `tests/spec/*` that use the same hand-spelled exporter — grep `CppSpec::parse(argc, argv).add_spec`).

```cpp
// tests/spec/support/spec_main.h
#pragma once
#include "cppspec.hpp"
#include <cstdlib>
/// Every spec TU exports one C-linkage-free entry point the driver calls (tests/spec/CMakeLists.txt's
/// create_specs_driver derives the name from the path: `<dir>_<stem>`); CppSpec's own CPPSPEC_SPEC macro
/// cannot spell that name for nested directories, so this macro does.
#define DELUGE_SPEC(fn, spec)                                                                                          \
	int fn(int argc, char** const argv) {                                                                              \
		return CppSpec::parse(argc, argv).add_spec(spec).exec().is_success() ? EXIT_SUCCESS : EXIT_FAILURE;             \
	}
```
- [ ] Replace each hand-spelled exporter + its comment with `DELUGE_SPEC(<dir>_<stem>_spec, <describe>)`; build; `ctest -N | wc -l` unchanged; all green. Commit — `git commit -m "test: DELUGE_SPEC macro replaces the copy-pasted nested-spec exporter"`

---

### Task 4: Canvas clip rect, honoured by every primitive; frames set it while drawing

**Files:**
- Modify: `src/gfx/oled/canvas.{h,cpp}` (`void set_clip(Rect)`, `void clear_clip()`, `std::optional<Rect> clip() const`; every write path — `draw_char`/`draw_string*`, `draw_graphic_multi_line`, `draw_icon`/`draw_icon_centered`, `invert_area_rounded`, and the already-exact ones for uniformity — intersects with the clip before touching bytes; no clip = today's behaviour), `src/ui/oled/frame.{h,cpp}` (`OledFrame` sets `canvas.set_clip(region)` on construction of the root/region frame's drawing calls — since frames are aggregates without ctors, do it inside each drawing method: `ClipScope s{canvas, clip}` RAII that sets and restores; remove the "all-or-nothing" caveats from the header and README), `tests/spec/gfx/canvas_golden_spec.cpp` (+ clip cases: char half outside, icon half outside, rounded invert half outside → byte-compare against the unclipped draw masked by the rect), `tests/spec/ui/oled_frame_spec.cpp` (+ region cases for `glyphs`/`icon`/`graphic`/`invert_rounded` partially outside).
- Sources: audit-sdk §1.2, audit-architecture D5, `src/ui/README.md` follow-ups.

- [ ] **Step 1: Failing specs** as listed (the masked-unclipped oracle is exact: draw into a scratch canvas without clip, zero every pixel outside the rect, compare).
- [ ] **Step 2–3:** implement; gfx goldens untouched (no clip by default); ui goldens untouched (no existing golden had a clipped blit — verify with `git diff --stat -- tests/golden`).
- [ ] **Step 4: Run** — green; both checkers.  - [ ] **Step 5: Commit** — `git commit -m "feat(gfx,ui-oled): canvas clip rect honoured by every primitive; frames clip all drawing to their region"`

---

### Task 5: The input-consumption contract — `FrameContext`, claims, focus masking, accessors

**Files:**
- Create: `src/ui/core/frame_context.h`, `tests/spec/ui/frame_context_spec.cpp`
- Modify: `src/ui/oled/frame.{h,cpp}`, `src/ui/grid/frame.{h,cpp}`, every widget that reads `f.input.turn/edit/select_pressed/back_pressed/horizontal/vertical` (`oled/list.cpp:107-108,123`, `columns.cpp:385-386,400`, `values.cpp:40-45 (delete without_turn),248,288,299,309`, `scroll_text.cpp`, `grid/effects.cpp`, `keyboard.cpp`, `rows.cpp`, `columns.cpp`, `sidebar.cpp` — grep `input\.` under `src/ui`), `tests/spec/ui/support/frames.h` (rigs build a `FrameContext`), all ui specs that construct frames, `src/ui/README.md`.

**Interfaces:**
```cpp
namespace deluge::ui {
struct InputClaims { bool turn = false, edit0 = false, edit1 = false, horizontal = false, vertical = false, select = false, back = false;
	bool any() const; };
struct FrameContext {                           // one per tick per surface set, owned by the shell (or the rig)
	const InputSnapshot& input;
	RepaintGate& gate;
	InputClaims claims{};                       // what widgets consumed this tick (the shell delivers unclaimed widget-owned inputs to Scene::handle() after render)
	uint32_t tick = 0;                          // monotonic; lets frames/widgets detect a new tick without now_ms
};
}
// OledFrame / GridFrame (both): replace `const InputSnapshot& input; RepaintGate& gate;` with `FrameContext& ctx; bool focused = true;`
//   accessors (masked when !focused): int8_t turn() const; int8_t edit(int i) const; int8_t horizontal() const; int8_t vertical() const; bool select_pressed() const; bool back_pressed() const; bool shift() const; uint32_t now_ms() const;
//   claiming (widgets call after consuming): void claim_turn(); void claim_edit(int i); void claim_select(); void claim_back(); (no-ops when !focused)
//   pads (GridFrame): local_events(), pad(), first_press_in(), interact(Rect) unchanged — pad input is positional, never masked
//   region(rect, focused = this->focused): child inherits or narrows focus (never widens)
//   OledFrame::bounds() = requested local rect (unchanged); GridFrame::bounds() = requested local rect (CHANGED from clipped); both gain visible() = clipped extent
//   root_frame(canvas, style, ctx) / root_grid_frame(grid, ctx)
```
Contract (documented in `frame_context.h` and spec B §4/§5): *a widget that renders a control consumes the inputs that control owns and records a claim; unfocused frames present zero input; pad events are positional and never masked; `select_pressed`/`back_pressed` are edges the caller supplies once per press.*

- [ ] **Step 1: Failing specs** — `frame_context_spec.cpp`: a root frame with `turn = 1` → `f.turn() == 1`; `f.region(r, /*focused*/ false)` → child `turn() == 0`, `select_pressed() == false`; a widget rendered in the unfocused child does not move (`list` cursor unchanged); after rendering `list` in the focused frame, `ctx.claims.turn == true`; two `list`s in two regions, one focused → only the focused one moves; grid: `pad()` still reports presses in an unfocused region (positional); `bounds()` vs `visible()` on a partially clipped grid region.
- [ ] **Step 2: Run to see them fail.**  - [ ] **Step 3: Implement**; migrate widgets; delete `without_turn`; existing ui goldens byte-identical (rigs default to focused).
- [ ] **Step 4: Run** — green; layer checkers.  - [ ] **Step 5: Commit** — `git commit -m "feat(ui-core): FrameContext with input claims and focus masking; widgets read input through the frame"`

---

### Task 6: Grid helpers from the SDK — `Rect::split_rows/split_cols/inset`, `GridFrame::interact(Rect)`, `BlendMode` compositing

**Files:**
- Modify: `src/gfx/grid/rect.h` (+ `constexpr Rect split_rows(int32_t n, int32_t i) const` / `split_cols` / `inset(int32_t)`, matching the OLED `Rect`'s existing helpers at `gfx/oled/geometry.h:50-51`), `src/ui/grid/frame.{h,cpp}` (`Response interact(gfx::grid::Rect) const` — pressed/released/held/velocity/index for the first event in the rect, mirroring the SDK `imode.rs:315` semantics — cite), `tests/spec/gfx/rect_spec.cpp`, `tests/spec/ui/grid_frame_spec.cpp`
- Create: `src/gfx/grid/composite.{h,cpp}`, `tests/spec/gfx/composite_spec.cpp`
- Sources: SDK `crates/deluge-grid-toolkit/src/imode.rs:315,367-398`, `layer.rs:19-112` (`BlendMode`, `composite`), spark report §compositing.

```cpp
namespace deluge::gfx::grid {
enum class BlendMode : uint8_t { Replace /* non-black overlay pads replace */, Overlay /* additive-saturating per channel */, Tint /* multiply base by overlay/255 */ };
void composite(const PadGrid& base, const PadGrid& overlay, BlendMode, PadGrid& out);   // out may alias base; dims must match (contract macro)
}
```
- [ ] **Step 1: Failing specs** — split/inset arithmetic; `interact` on a rect containing a press vs. not; `composite` per mode with hand-computed pads (read the SDK's exact formulas and cite; where the SDK's Overlay semantics differ from "additive-saturating", follow the SDK and document).
- [ ] **Step 2–4:** implement, run.  - [ ] **Step 5: Commit** — `git commit -m "feat(gfx-grid,ui-grid): rect splits, interact(), BlendMode compositing"`

---

### Task 7: The four missing tweens — smear scroll, zoom, explode, expand/collapse

**Files:**
- Modify: `src/gfx/grid/tween.{h,cpp}`, `tests/spec/gfx/tween_spec.cpp`, goldens `tests/golden/gfx/grid/tween_*.state` (new files only)
- Sources (legacy, the parity authority): `src/deluge/hid/led/pad_leds.cpp` `renderZoomWithProgress`/`renderZoom` (dual-buffer cross-fade with pin squares), `renderExplodeAnimation` (bilinear explode/implode about an origin), `renderClipExpandOrCollapse`/`renderInstrumentClipCollapseAnimation`/`renderNoteRowExpandOrCollapse` (the expand/collapse family — port the common core and expose the parameters the three callers vary), plus `horizontal::renderScroll`/`vertical::renderScroll` for the smear variant; SDK ports for cross-checking: `crates/deluge-grid-toolkit/src/animations/{smear_scroll.rs:52-77, zoom.rs:1-50, explode.rs:1-60, expand_collapse.rs}`.

```cpp
void smear_scroll_horizontal(const PadGrid& from, const PadGrid& incoming, int32_t offset_px_16 /* 16.16 fixed */, PadGrid& out);   // sub-pixel: blend by the fractional part (SDK smear_scroll.rs:52-77 — legacy's PIC did this in hardware; document that this is the host/second-device path)
void smear_scroll_vertical(...);
struct ZoomParams { int32_t old_zoom, new_zoom; int32_t pin_square_from, pin_square_to; uint16_t t; };
void zoom(const PadGrid& from, const PadGrid& to, ZoomParams, PadGrid& out);                       // renderZoomWithProgress port
struct ExplodeParams { int32_t origin_x_big, origin_y_big; int32_t width_big; bool implode; uint16_t t; };
void explode(const PadGrid& from, ExplodeParams, PadGrid& out);                                    // renderExplodeAnimation port
struct ExpandCollapseParams { uint8_t row_from, row_to; bool collapsing; uint16_t t; };
void expand_collapse(const PadGrid& from, const PadGrid& to, ExpandCollapseParams, PadGrid& out);  // the shared core of the three legacy routines
```
- [ ] **Step 1: Failing specs** — for zoom/explode/expand-collapse: a legacy-transcription oracle in the spec (copy the legacy routine's arithmetic, `image`→`PadGrid`) at t ∈ {0, 0.5, 1} on a 3-colour test grid, byte-compared; smear: hand-computed blend at offset 0.5 pads; `.state` goldens after the oracles pass.
- [ ] **Step 2–4:** implement, run.  - [ ] **Step 5: Commit** — `git commit -m "feat(gfx-grid): smear scroll, zoom, explode and expand/collapse tweens ported from PadLEDs"`

---

### Task 8: Two-pass `list` (count, then draw)

**Files:** Modify `src/ui/oled/list.{h,cpp}`, `tests/spec/ui/list_spec.cpp`
- Source: SDK `crates/deluge-ui-toolkit/src/menu.rs:346-415`; audit-sdk §5.1 — today the cursor clamp uses the previous frame's `rows_last` (`list.h:96-99`, `list.cpp:107-108`), so a list whose row count shrinks lags one frame.
- [ ] **Step 1: Failing spec** — rows shrink from 5 to 2 while `cursor == 4` and `turn == 0` → cursor is 1 on the *same* frame (today: 4 clamped next frame); rows grow with `turn = +1` at the old end → cursor advances into the new row on the same frame. `rows_last` becomes a pure cache (documented) — keep the field (spec §3 shape), stop treating it as the authority.
- [ ] **Step 2–4:** implement (`visible = count(rows)` first, then clamp, then draw), run; `list_*` goldens byte-identical.  - [ ] **Step 5: Commit** — `git commit -m "fix(ui-oled): list clamps against this frame's row count (two-pass), not last frame's"`

---

### Task 9: Shell-plan and scenes-plan reconciliation (docs + plan text), per-surface `Style`

**Files:** Modify `docs/superpowers/plans/2026-08-27-ui-shell.md`, `2026-08-27-ui-scenes-menus-1.md`, `docs/superpowers/specs/2026-08-26-ui-shell-design.md` (`git add -f` for docs under `docs/superpowers`).
- Shell plan Task 3 (`scene.h`): replace `Frames` + `render(Frames&)` with `virtual void render_oled(SurfaceRole, OledFrame&) {}` and `virtual void render_grid(GridFrame&) {}`, add `virtual Result after_render() { return {}; }`; Task 7's tick: render each OLED surface via `render_oled`, the grid via `render_grid`, then `router_.apply(scene.after_render())` for every scene rendered this tick, then the **post-render input delivery**: widget-owned inputs (`Encoder{Select/GoldUpper/GoldLower/Horizontal/Vertical}`, `EncoderButton{Select}`, `Button{Back}` edges) are held back from `pump_input`, exposed to widgets through the `InputSnapshot`, and delivered to `Scene::handle()` after render **only if `ctx.claims` did not claim them**; Task 2 (`OledSurface`): `+ const Style* style` chosen per index from `BoardProfile::style_by_index` (replaces the single `style`); Task 4: the lowering builds one `FrameContext` per tick; `InputSnapshot` unchanged. Scenes plan Task 5: `MenuScene::handle` no longer applies `turn`/select/back itself — it renders `list`/`columns` which claim them, and navigates from `Response` in `after_render()`; `Task 12`'s `scenes-no-canvas` row unchanged.
- Spec D: §3 `Scene` signature and §7 tick updated the same way; §2 `BoardProfile::style_by_index`.
- [ ] Apply; re-run each plan's self-review checklist (type consistency across tasks); commit — `git commit -m "docs(ui): shell/scenes plans and spec D adopt per-surface render virtuals, after_render(), input claims and per-surface Style"`

---

### Task 10: Spec B / umbrella / README amendments, attribution fix, quirk list

**Files:** Modify `docs/superpowers/specs/2026-08-26-ui-toolkit-design.md` (§4: `FrameContext`, accessors, claims, focus, `bounds()`/`visible()`, drop `Frames`; §5 focus rule now has a mechanism; §3 amendments list), `docs/superpowers/specs/2026-08-26-ui-stack-redesign-design.md` (§D5/§8: the OLED-vs-grid split and imode design came from **deluge-sdk** (`deluge-ui-toolkit`/`deluge-grid-toolkit`), not spark; the host-link `0x2B` opcode has a sender but no reader in `src/bsp/host/host_link.c` — note as a harness-plan item), `src/ui/README.md`, `src/gfx/README.md` (clip rect, compositing, tweens, contract macro), and a new section in `src/ui/README.md` **"Legacy quirks reproduced on purpose"** listing the six the audit named (popups centred on bezel-hidden rows; `decimal_value`'s canvas-absolute y; `kHorizontalMenuBaseY = 14` on every panel; mirrored `GreyoutMask` bits; dead `max_chars_per_line`; the length-slider default) each with the golden that pins it and the one-line change that would fix it — the user decides per item later.
- [ ] Apply; commit — `git add -f docs/superpowers/specs/... && git add src/ui/README.md src/gfx/README.md && git commit -m "docs(ui): spec B/umbrella amendments for the contract, clipping, compositing; SDK attribution; reproduced-quirk list"`

---

## Self-review notes

- **Coverage of the eight audit items:** 1 → Task 1; 2 → Tasks 5 + 9; 3 → Tasks 4 + 6; 4 → Task 9 (per-surface `Style`) + Task 10 (base-y rule documented once); 5 → Task 1; 6 → Task 2; 7 → Tasks 6, 7, 8 (ink-bounds centring deliberately **not** harvested: it changes pixels vs legacy — noted in Task 10); 8 → Task 3. Spark-report extras: `interact(Rect)` → Task 6; structural `FrameOutput` gate → not adopted (needs closures; the advisory gate + `needs_frame` stays, documented in Task 10); variant-exhaustiveness tests for profile tables → shell plan Task 10 already has them.
- **Placeholders:** none; every task has its files, its failing spec, and its commit.
- **Type consistency:** `FrameContext`/`InputClaims` (T5) referenced by T9's plan text; `BlendMode`/`composite` (T6) and the tweens (T7) referenced by T9's shell-plan `GridSurface::tween` and `render_grid` text; `bounds()/visible()` (T5) referenced in T10's spec B text.
- **Order matters:** T1–T3 first (cheap, unblock warnings), T4 before T5 (frames set clips; T5 rewrites frames), T5 before T8 (list reads through accessors), T9/T10 last (docs describe the final shapes).
