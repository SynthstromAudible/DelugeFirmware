# UI toolkit (`ui-core`, `ui-oled`, `ui-grid`) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the immediate-mode toolkit — `deluge_ui_core` (input snapshot, response, repaint gate, POD state, style, LED frame), `deluge_ui_oled` (`OledFrame` + widgets) and `deluge_ui_grid` (`GridFrame` + components) — as globals-free, host-tested libraries whose widgets reproduce today's pixel output, ready for the shell and `MenuScene` to consume.

**Architecture:** Widgets are free functions over a frame; a frame is a borrowed, clipped, translated view of one surface for one tick carrying `Style`, `InputSnapshot` and a `RepaintGate`. State is POD owned by the caller. Every widget is a port of a named legacy routine (file:function listed per task) with `OLED::main`/`PadLEDs::image` replaced by the frame, `OLED_MAIN_*` by the frame's bounds, and the timer table by `request_repaint_after`. Not used by the firmware until the shell plan; verified by golden frames at 128×48 and 128×64.

**Tech Stack:** C++26, CMake, CppSpec, the gfx libraries (`gfx/oled`, `gfx/grid`) and golden helpers from the gfx plan.

**Spec:** `docs/superpowers/specs/2026-08-26-ui-toolkit-design.md` (spec B). Read it first. Depends on the gfx plan (Tasks 5–12 landed) and, for `LedId`, on nothing yet (an opaque `enum class LedId : uint8_t`).

## Global Constraints

- Branch `docs/ui-stack-redesign-spec`. Firmware verified with `./dbt build Debug` (the libraries link into `deluge_app` but are unused until the shell plan).
- `src/ui/**` may include only `ui/**`, `gfx/**` and the C++ standard library. `ui/oled` and `ui/grid` never include each other. No `notify`, `model`, `libdeluge`, `l10n`, `gui/`, `hid/`, `definitions*.h`, `board_config.h`. Task 15 enforces this in the layer checker.
- No mutable globals; no heap in any widget; widgets are not templates (only `region` bodies). Widgets take `std::string_view`, never `l10n::String`.
- Naming: `snake_case` functions/variables/members; `PascalCase` types; `k`-prefixed constants.
- Geometry parity: each OLED widget pins the legacy pixel positions listed in its task (derived from the legacy code) in a golden at **both** `Geometry{128,48,5}` and `Geometry{128,64,0}` and an explicit-pixel assertion; grid components pin colours from the legacy rules.
- Legacy constants reproduced as `ui` constants (`src/ui/core/metrics.h`): `kTextSpacingX = 6`, `kTextSpacingY = 9`, `kTextTitleSpacingX = 9`, `kTextTitleSizeY = 10`, `kTextHugeSpacingX = 18`, `kTextHugeSizeY = 20`, `kSubmenuIconSpacingX = 7`, `kScrollTime = 400`, `kFlashTime = 110`, `kHorizontalSlotYOffset = 2`, `kMenuBaseY48 = 14`, `kMenuBaseY64 = 15`, `kTitleSeparatorY = 12`, `kPopupMargin = 12`, `kPopupMaxCharsPerLine = 19`. (The gfx font metrics `kText*` are re-exported, not redefined.)
- Tests: `cmake -S tests -B build/tests -G "Ninja Multi-Config" && cmake --build build/tests --config Debug && ctest --test-dir build/tests -C Debug -R <name> --output-on-failure`. Goldens under `tests/golden/ui/widgets/`; re-record with `GFX_GOLDEN_UPDATE=1`.
- Commit per "Commit" step; `./dbt format` first.

---

## File structure

```
src/ui/CMakeLists.txt                     deluge_ui_core, deluge_ui_oled, deluge_ui_grid
src/ui/README.md
src/ui/core/metrics.h                     legacy layout constants
src/ui/core/input.h                       EncoderRole, PadEvent, InputSnapshot
src/ui/core/response.h                    Response
src/ui/core/repaint_gate.h / .cpp         RepaintGate
src/ui/core/state.h                       ListState, ColumnsState, ScrollText, Blink, Hold, Timer, WaveformState
src/ui/core/style.h                       Style, kDelugeStyle48, kDelugeStyle64
src/ui/core/led_frame.h / .cpp            LedId, IndicatorId, LedState, KnobIndicator, LedFrame
src/ui/core/text_wrap.h / .cpp            wrap_lines() (port of breakStringIntoLines)
src/ui/oled/frame.h / .cpp                OledFrame, region(), bounds()
src/ui/oled/text.h / .cpp                 TextStyle, Align, measure(), text(), title()
src/ui/oled/scroll_text.h / .cpp
src/ui/oled/list.h / .cpp                 ListRow, list(), scrollbar()
src/ui/oled/values.h / .cpp               int_value, decimal_value, enum_value, toggle
src/ui/oled/meters.h / .cpp               Hint, unipolar_bar, knob_arc, pan, percent, slider, length_slider, filter_curve, envelope_stage, sidechain_ducking, meter
src/ui/oled/columns.h / .cpp              Column, columns()
src/ui/oled/popup.h / .cpp                popup_frame, popup_text, working, progress
src/ui/oled/misc.h / .cpp                 envelope, vu, output_name, segment_digits, graphic, icon   (no `waveform`: deferred, see the spec's Amendments)
src/ui/grid/frame.h / .cpp                GridFrame, pad(), region(), local_events()
src/ui/grid/rows.h / .cpp                 row, clip_row, NoteCell, ClipRowData
src/ui/grid/sidebar.h / .cpp              StatusPad, status_pad, audition_pad, sidebar_columns
src/ui/grid/columns.h / .cpp              velocity_column, automation_column, macros, vu_meter, tick_squares
src/ui/grid/waveform.h / .cpp             waveform_row, waveform_full
src/ui/grid/keyboard.h / .cpp             text_keyboard, TextKeyboardState, KeyboardLayoutAdapter
src/ui/grid/effects.h / .cpp              greyout, gesture
src/gfx/grid/rect.h                       gfx::grid::Rect (pad units)
tests/spec/ui/support/frames.h            make_oled_frame(), make_grid_frame(), NullInput
tests/spec/ui/*_spec.cpp                  one per task
tests/golden/ui/widgets/*.pbm|*.state
```

Modified: `src/CMakeLists.txt`, `sim/CMakeLists.txt`, `tests/spec/CMakeLists.txt`, `tests/unit/CMakeLists.txt`, `src/deluge/CMakeLists.txt`, `scripts/check_gfx_layer.py` (rename/extend), `src/gfx/CMakeLists.txt` (rect.h is header-only).

---

### Task 1: Skeleton, metrics, `gfx::grid::Rect`, test support

**Files:**
- Create: `src/ui/CMakeLists.txt`, `src/ui/README.md`, `src/ui/core/metrics.h`, `src/gfx/grid/rect.h`, `tests/spec/ui/support/frames.h`
- Modify: `src/CMakeLists.txt` (`add_subdirectory(ui)` after `gfx`), `sim/CMakeLists.txt`, `tests/spec/CMakeLists.txt` (`add_subdirectory(../../src/ui ui)`, link the three targets into `deluge_spec`), `tests/unit/CMakeLists.txt`, `src/deluge/CMakeLists.txt` (link into `deluge_app`)
- Test: `tests/spec/ui/rect_spec.cpp`

**Interfaces:**
- Produces: `deluge::gfx::grid::Rect{int32_t x, y, w, h; contains(int,int); intersect(Rect); right(); bottom(); operator==}`; `deluge::ui::k*` metrics; CMake targets `deluge_ui_core` (STATIC once Task 2 adds `repaint_gate.cpp`; INTERFACE until then), `deluge_ui_oled`, `deluge_ui_grid` (INTERFACE until their first `.cpp`).

- [ ] **Step 1: Failing spec**

`tests/spec/ui/rect_spec.cpp`:

```cpp
#include "gfx/grid/rect.h"
#include "ui/core/metrics.h"
#include "cppspec.hpp"

using deluge::gfx::grid::Rect;

// clang-format off
describe grid_rect("gfx::grid::Rect", $ {
	it("contains and intersects in pad units", _ {
		Rect r{2, 1, 4, 3};
		expect(r.contains(2, 1)).to_be_true();
		expect(r.contains(6, 1)).to_be_false();
		expect(r.intersect(Rect{4, 2, 10, 10})).to_equal(Rect{4, 2, 2, 2});
		expect(deluge::ui::kTextSpacingY).to_equal(9);
	});
});

CPPSPEC_SPEC(grid_rect)
```

- [ ] **Step 2: Run to see it fail** — header not found.

- [ ] **Step 3: Implement**

`src/gfx/grid/rect.h`:

```cpp
#pragma once
#include <cstdint>

namespace deluge::gfx::grid {
/// Rectangle in pad units (columns, rows). Same shape as gfx::oled::Rect; kept separate so
/// the two gfx libraries stay independent.
struct Rect {
	int32_t x, y, w, h;
	constexpr bool operator==(const Rect&) const = default;
	constexpr int32_t right() const { return x + w; }
	constexpr int32_t bottom() const { return y + h; }
	constexpr bool contains(int32_t px, int32_t py) const { return px >= x && px < right() && py >= y && py < bottom(); }
	constexpr Rect intersect(Rect o) const {
		int32_t nx = x > o.x ? x : o.x, ny = y > o.y ? y : o.y;
		int32_t nr = right() < o.right() ? right() : o.right();
		int32_t nb = bottom() < o.bottom() ? bottom() : o.bottom();
		return {nx, ny, nr > nx ? nr - nx : 0, nb > ny ? nb - ny : 0};
	}
};
} // namespace deluge::gfx::grid
```

`src/ui/core/metrics.h`:

```cpp
#pragma once
#include "gfx/oled/font.h"
#include <cstdint>

namespace deluge::ui {
using gfx::oled::kTextBigSizeY;
using gfx::oled::kTextHugeSizeY;
using gfx::oled::kTextHugeSpacingX;
using gfx::oled::kTextSpacingX;
using gfx::oled::kTextSpacingY;
using gfx::oled::kTextTitleSizeY;
using gfx::oled::kTextTitleSpacingX;

constexpr int32_t kSubmenuIconSpacingX = 7;
constexpr int32_t kScrollTime = 400;          // ms pause at each end of a scrolling label
constexpr int32_t kScrollStepForwardMs = 15;  // legacy: 15 ms per pixel forwards
constexpr int32_t kScrollStepBackMs = 5;      // 5 ms per pixel back
constexpr int32_t kFlashTime = 110;           // blink half-period
constexpr int32_t kHorizontalSlotYOffset = 2;
constexpr int32_t kMenuBaseY48 = 14;          // list top for a 48-row panel (+ visible_top)
constexpr int32_t kMenuBaseY64 = 15;
constexpr int32_t kTitleY = 1;                // title baseline offset (+ visible_top)
constexpr int32_t kTitleSeparatorY = 12;      // (+ visible_top)
constexpr int32_t kPopupMargin = 12;          // legacy doubleMargin
constexpr int32_t kPopupMaxCharsPerLine = 19;
constexpr int32_t kValueY = 18;               // huge-font value baseline (+ visible_top)
constexpr int32_t kHorizontalBoxHeight = 25;
} // namespace deluge::ui
```

`src/ui/CMakeLists.txt`:

```cmake
# deluge_ui_core / deluge_ui_oled / deluge_ui_grid — the immediate-mode toolkit (spec B).
# Include only ui/**, gfx/** and the standard library; oled and grid never include each other.
add_library(deluge_ui_core INTERFACE)
target_include_directories(deluge_ui_core INTERFACE ${SHARED_INCLUDE})
target_link_libraries(deluge_ui_core INTERFACE deluge_gfx_oled deluge_gfx_grid)
add_library(deluge_ui_oled INTERFACE)
target_link_libraries(deluge_ui_oled INTERFACE deluge_ui_core)
add_library(deluge_ui_grid INTERFACE)
target_link_libraries(deluge_ui_grid INTERFACE deluge_ui_core)
```

(Each becomes `STATIC` with `target_sources` when its first `.cpp` lands: core in Task 2, oled in Task 3, grid in Task 10.)

`tests/spec/ui/support/frames.h` — filled in by Tasks 3 and 10; create it now with the include guard and the `NullInput` helper:

```cpp
#pragma once
#include "ui/core/input.h"   // Task 2
#include <span>

namespace uitest {
/// An InputSnapshot with nothing pressed and no pad events.
inline deluge::ui::InputSnapshot null_input(uint32_t now_ms = 0) {
	deluge::ui::InputSnapshot in{};
	in.now_ms = now_ms;
	return in;
}
} // namespace uitest
```

(Since Task 2 defines `input.h`, write `frames.h` in Task 2 instead and keep this task to `rect.h` + metrics + CMake.)

`src/ui/README.md`: the four rules from spec B §2 and a pointer to the spec.

- [ ] **Step 4: Run** — `ctest -R grid_rect` PASS; `./dbt build Debug` builds.

- [ ] **Step 5: Commit**

```bash
./dbt format
git add src/ui src/gfx/grid/rect.h src/CMakeLists.txt sim/CMakeLists.txt tests/spec/CMakeLists.txt tests/unit/CMakeLists.txt src/deluge/CMakeLists.txt tests/spec/ui/rect_spec.cpp
git commit -m "feat(ui): toolkit skeleton, legacy layout metrics, grid Rect"
```

---

### Task 2: `ui-core` — input, response, repaint gate, state, style, LED frame

**Files:**
- Create: `src/ui/core/input.h`, `response.h`, `repaint_gate.{h,cpp}`, `state.h`, `style.h`, `led_frame.{h,cpp}`, `tests/spec/ui/support/frames.h`
- Modify: `src/ui/CMakeLists.txt` (core → STATIC with `repaint_gate.cpp`, `led_frame.cpp`)
- Test: `tests/spec/ui/repaint_gate_spec.cpp`, `tests/spec/ui/led_frame_spec.cpp`

**Interfaces:**
- Produces: exactly the `deluge::ui` declarations of spec B §3 (`EncoderRole`, `PadEvent`, `InputSnapshot`, `Response`, `RepaintGate`, `ListState`, `ColumnsState`, `ScrollText`, `Blink`, `Hold`, `Timer`, `WaveformState`, `Style`, `LedId`, `IndicatorId`, `LedState`, `KnobIndicator`, `LedFrame`), plus:
  ```cpp
  constexpr Style kDelugeStyle48{ .title_height = 12, .row_height = 9, .inset_x = 6, .scrollbar_w = 4, .menu_rows = 3,
                                  .rounded = true, .invert_highlight = true,
                                  .title_font = FontId::MetricBold9, .row_font = FontId::Apple7, .value_font = FontId::MetricBold20, .big_font = FontId::MetricBold20 };
  constexpr Style kDelugeStyle64{ /* same, menu_rows = 5 */ };
  // LedFrame
  class LedFrame { public: void set(LedId, LedState); void indicator(IndicatorId, KnobIndicator);
                   std::span<const std::pair<LedId, LedState>> entries() const;   // in call order, later wins
                   const KnobIndicator* indicator_or_null(IndicatorId) const; void clear(); };
  ```
  `LedFrame` stores up to 64 entries in a fixed array; a second `set` for the same id overwrites in place.

- [ ] **Step 1: Failing specs**

`tests/spec/ui/repaint_gate_spec.cpp`:

```cpp
#include "ui/core/repaint_gate.h"
#include "cppspec.hpp"

using deluge::ui::RepaintGate;

// clang-format off
describe repaint_gate("ui::RepaintGate", $ {
	it("fires on the first frame, then only when asked", _ {
		RepaintGate g;
		expect(g.needs_frame(0, false, false)).to_be_true();     // first frame
		expect(g.needs_frame(10, false, false)).to_be_false();   // clean
		expect(g.needs_frame(20, true, false)).to_be_true();     // input
		expect(g.needs_frame(30, false, true)).to_be_true();     // model dirty
		g.request_repaint();
		expect(g.needs_frame(40, false, false)).to_be_true();
		expect(g.needs_frame(41, false, false)).to_be_false();   // consumed
	});
	it("honours a timed request and reports the deadline", _ {
		RepaintGate g; g.needs_frame(0, false, false);
		g.request_repaint_after(100, 0);
		expect(g.next_deadline().has_value()).to_be_true();
		expect(*g.next_deadline()).to_equal(100u);
		expect(g.needs_frame(50, false, false)).to_be_false();
		expect(g.needs_frame(100, false, false)).to_be_true();
		expect(g.next_deadline().has_value()).to_be_false();
	});
	it("keeps the earliest of several timed requests", _ {
		RepaintGate g; g.needs_frame(0, false, false);
		g.request_repaint_after(300, 0); g.request_repaint_after(120, 0);
		expect(*g.next_deadline()).to_equal(120u);
	});
});

CPPSPEC_SPEC(repaint_gate)
```

`tests/spec/ui/led_frame_spec.cpp`:

```cpp
#include "ui/core/led_frame.h"
#include "cppspec.hpp"

using namespace deluge::ui;

// clang-format off
describe led_frame("ui::LedFrame", $ {
	it("records entries in order and lets a later set win", _ {
		LedFrame f;
		f.set(LedId{3}, LedState{LedState::Kind::On, 0, 0});
		f.set(LedId{3}, LedState{LedState::Kind::Blink, 200, 0});
		f.indicator(IndicatorId::Lower, KnobIndicator{{1, 2, 3, 0}});
		expect(f.entries().size()).to_equal(size_t(1));
		expect(f.entries()[0].second.kind == LedState::Kind::Blink).to_be_true();
		expect(f.indicator_or_null(IndicatorId::Lower)->levels[2]).to_equal(uint8_t(3));
		expect(f.indicator_or_null(IndicatorId::Upper) == nullptr).to_be_true();
	});
});

CPPSPEC_SPEC(led_frame)
```

- [ ] **Step 2: Run to see them fail** — headers not found.

- [ ] **Step 3: Implement**

`src/ui/core/input.h`, `response.h`, `state.h`, `style.h` are the spec B §3 blocks verbatim (snake_case as written there), each in its own header with `#pragma once`, `<cstdint>`, `<span>`, `<optional>` as needed; `style.h` includes `gfx/oled/font.h` and defines `kDelugeStyle48`/`kDelugeStyle64` as above.

`src/ui/core/repaint_gate.h`:

```cpp
#pragma once
#include <cstdint>
#include <optional>

namespace deluge::ui {
class RepaintGate {
public:
	void request_repaint() { requested_ = true; }
	void request_repaint_after(uint32_t ms, uint32_t now_ms);
	bool needs_frame(uint32_t now_ms, bool had_input, bool model_dirty);
	std::optional<uint32_t> next_deadline() const { return deadline_; }
private:
	bool first_ = true;
	bool requested_ = false;
	std::optional<uint32_t> deadline_;
};
} // namespace deluge::ui
```

`src/ui/core/repaint_gate.cpp`:

```cpp
#include "ui/core/repaint_gate.h"

namespace deluge::ui {

void RepaintGate::request_repaint_after(uint32_t ms, uint32_t now_ms) {
	const uint32_t due = now_ms + ms;
	if (!deadline_ || static_cast<int32_t>(due - *deadline_) < 0) {
		deadline_ = due;
	}
}

bool RepaintGate::needs_frame(uint32_t now_ms, bool had_input, bool model_dirty) {
	bool due = deadline_ && static_cast<int32_t>(now_ms - *deadline_) >= 0;
	bool fire = first_ || requested_ || had_input || model_dirty || due;
	if (fire) {
		first_ = false;
		requested_ = false;
		if (due) deadline_.reset();
	}
	return fire;
}

} // namespace deluge::ui
```

`src/ui/core/led_frame.h`:

```cpp
#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

namespace deluge::ui {
enum class LedId : uint8_t;          // values supplied by the board profile (shell)
enum class IndicatorId : uint8_t { Upper, Lower };
struct LedState { enum class Kind : uint8_t { Off, On, Blink } kind; uint16_t period_ms; uint32_t until_ms; };
struct KnobIndicator { uint8_t levels[4]; };

class LedFrame {
public:
	static constexpr size_t kMaxEntries = 64;
	void set(LedId id, LedState s);
	void indicator(IndicatorId r, KnobIndicator k) { indicators_[static_cast<int>(r)] = k; has_indicator_[static_cast<int>(r)] = true; }
	std::span<const std::pair<LedId, LedState>> entries() const { return {entries_, count_}; }
	const KnobIndicator* indicator_or_null(IndicatorId r) const { return has_indicator_[static_cast<int>(r)] ? &indicators_[static_cast<int>(r)] : nullptr; }
	void clear() { count_ = 0; has_indicator_[0] = has_indicator_[1] = false; }
private:
	std::pair<LedId, LedState> entries_[kMaxEntries];
	size_t count_ = 0;
	KnobIndicator indicators_[2]{};
	bool has_indicator_[2]{};
};
} // namespace deluge::ui
```

`led_frame.cpp`: `set` searches `entries_` for `id`, overwrites if found, else appends when `count_ < kMaxEntries`.

`tests/spec/ui/support/frames.h` (the `null_input` helper shown in Task 1; the frame builders are added in Tasks 3 and 10).

Switch `deluge_ui_core` to `STATIC` with `core/repaint_gate.cpp core/led_frame.cpp`.

- [ ] **Step 4: Run** — both specs PASS; `./dbt build Debug`.

- [ ] **Step 5: Commit**

```bash
./dbt format
git add src/ui tests/spec/ui
git commit -m "feat(ui-core): input snapshot, response, repaint gate, POD state, style, LED frame"
```

---

### Task 3: `OledFrame`, `text`, `title`, `measure`

**Files:**
- Create: `src/ui/oled/frame.{h,cpp}`, `src/ui/oled/text.{h,cpp}`
- Modify: `src/ui/CMakeLists.txt` (oled → STATIC), `tests/spec/ui/support/frames.h` (add `make_oled_frame`)
- Test: `tests/spec/ui/oled_frame_spec.cpp`, `tests/spec/ui/text_spec.cpp`
- Ports: `Canvas::drawScreenTitle` (`hid/display/oled_canvas/canvas.cpp`), `Canvas::drawStringCentredShrinkIfNecessary`, `Canvas::drawStringCentered(startX…)`

**Interfaces:**
- Produces (`deluge::ui::oled`):
  ```cpp
  struct OledFrame {                              // spec B §4
      gfx::oled::Canvas& canvas; gfx::oled::Rect clip; gfx::oled::Point origin;
      const Style& style; const InputSnapshot& input; RepaintGate& gate;
      template <class F> void region(gfx::oled::Rect local, F&& body);   // body(OledFrame&)
      gfx::oled::Rect bounds() const;                                    // {0,0,clip.w,clip.h}
      // low-level helpers used by widgets (all clip to `clip`, translate by `origin`):
      void pixel(int32_t x, int32_t y); void hline(int32_t y, int32_t x0, int32_t x1); void vline(int32_t x, int32_t y0, int32_t y1);
      void line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, bool thick = false);
      void rect(gfx::oled::Rect r); void rect_rounded(gfx::oled::Rect r, gfx::oled::BorderRadius = gfx::oled::SMALL);
      void invert(gfx::oled::Rect r); void invert_rounded(gfx::oled::Rect r); void clear(gfx::oled::Rect r);
      void glyphs(std::string_view s, int32_t x, int32_t y, int32_t spacing_x, int32_t height, int32_t scroll_px = 0, int32_t end_x = kEdge, bool use_width = false);
      void graphic(const uint8_t* data, int32_t x, int32_t y, int32_t w, int32_t h, int32_t bytes_tall, bool reversed = false);
      void icon(const gfx::oled::Icon&, int32_t x, int32_t y);
  };
  OledFrame root_frame(gfx::oled::Canvas&, const Style&, const InputSnapshot&, RepaintGate&);   // origin (0, visible_top), clip = visible area
  // text.h
  enum class Align : uint8_t { Left, Centre, Right };
  struct TextStyle { gfx::oled::FontId font; Align align = Align::Left; bool shrink = false; };
  gfx::oled::Size measure(std::string_view, gfx::oled::FontId);       // width by glyph metrics, height = render height
  int32_t spacing_x_for(gfx::oled::FontId);                          // 6 / 9 / 18 as legacy textWidth
  void text(OledFrame&, gfx::oled::Rect, std::string_view, TextStyle);
  void title(OledFrame&, std::string_view, bool underline = true);   // drawScreenTitle: y = kTitleY, separator at kTitleSeparatorY
  ```
  Font ↔ legacy `textHeight`: `Px5`→5, `Apple7`→7, `MetricBold9`→10, `MetricBold13`→13, `MetricBold20`→20 (`text_height_for(FontId)`), so `glyphs` can call `canvas.draw_string(..., spacing_x, text_height_for(font), ...)`.

- [ ] **Step 1: Failing specs**

`tests/spec/ui/support/frames.h` additions:

```cpp
#include "gfx/oled/canvas.h"
#include "ui/core/repaint_gate.h"
#include "ui/core/style.h"
#include "ui/oled/frame.h"
#include "../../gfx/support/golden.h"
#include "../../gfx/support/pbm.h"

namespace uitest {
template <deluge::gfx::oled::Geometry G>
struct OledRig {
	deluge::gfx::oled::OwnedCanvas<G> canvas;
	deluge::ui::RepaintGate gate;
	deluge::ui::InputSnapshot input = null_input();
	const deluge::ui::Style& style = (G.height == 64) ? deluge::ui::kDelugeStyle64 : deluge::ui::kDelugeStyle48;
	deluge::ui::oled::OledFrame frame = deluge::ui::oled::root_frame(canvas, style, input, gate);
	OledRig() { canvas.clear(); }
	std::string pbm() { return page_major_to_pbm(canvas.bytes().data(), G.width, G.height); }
	bool lit(int x, int y) const { return canvas.bytes()[(y >> 3) * G.width + x] & (1u << (y & 7)); }
};
using Rig48 = OledRig<deluge::gfx::oled::kDelugeOled>;
using Rig64 = OledRig<deluge::gfx::oled::Geometry{128, 64, 0}>;
} // namespace uitest
```

`tests/spec/ui/oled_frame_spec.cpp`:

```cpp
#include "support/frames.h"
#include "cppspec.hpp"

using namespace deluge::ui::oled;
using deluge::gfx::oled::Rect;

// clang-format off
describe oled_frame("ui::oled::OledFrame", $ {
	it("translates by visible_top and clips to the visible area", _ {
		uitest::Rig48 r;
		r.frame.pixel(0, 0);                       // local (0,0) → canvas (0,5)
		expect(r.lit(0, 5)).to_be_true();
		expect(r.lit(0, 0)).to_be_false();
		r.frame.pixel(0, 43);                      // just past the 43 visible rows: clipped
		expect(r.lit(0, 48)).to_be_false();
		expect(r.frame.bounds()).to_equal(Rect{0, 0, 128, 43});
	});
	it("regions translate and clip, and nest", _ {
		uitest::Rig64 r;
		r.frame.region(Rect{10, 10, 20, 20}, [](OledFrame& f) {
			expect(f.bounds()).to_equal(Rect{0, 0, 20, 20});
			f.pixel(0, 0);                          // → (10,10)
			f.pixel(25, 0);                         // outside: clipped
			f.region(Rect{5, 5, 5, 5}, [](OledFrame& g) { g.pixel(0, 0); });   // → (15,15)
		});
		expect(r.lit(10, 10)).to_be_true();
		expect(r.lit(35, 10)).to_be_false();
		expect(r.lit(15, 15)).to_be_true();
	});
});

CPPSPEC_SPEC(oled_frame)
```

`tests/spec/ui/text_spec.cpp`:

```cpp
#include "support/frames.h"
#include "ui/oled/text.h"
#include "cppspec.hpp"

using namespace deluge::ui;
using namespace deluge::ui::oled;
using deluge::gfx::oled::FontId;
using deluge::gfx::oled::Rect;

// clang-format off
describe text_widgets("ui::oled text & title", $ {
	it("draws a title at the legacy position with its separator", _ {
		uitest::Rig48 r;
		title(r.frame, "SOUND");
		// separator: canvas row kTitleSeparatorY + visible_top = 17, full width
		expect(r.lit(0, 17)).to_be_true(); expect(r.lit(127, 17)).to_be_true();
		expect(r.lit(0, 16)).to_be_false();
		expect_golden("../ui/widgets/title_48.pbm", r.pbm());
	});
	it("aligns text left, centre and right within a rect", _ {
		uitest::Rig48 r;
		text(r.frame, Rect{0, 0, 128, 9}, "L", {FontId::Apple7, Align::Left});
		text(r.frame, Rect{0, 10, 128, 9}, "C", {FontId::Apple7, Align::Centre});
		text(r.frame, Rect{0, 20, 128, 9}, "R", {FontId::Apple7, Align::Right});
		expect(measure("C", FontId::Apple7).w).to_equal(6);
		expect_golden("../ui/widgets/text_align_48.pbm", r.pbm());
	});
	it("shrinks a long centred string like drawStringCentredShrinkIfNecessary", _ {
		uitest::Rig48 r;
		text(r.frame, Rect{0, 20, 128, 20}, "THIS TITLE IS FAR TOO LONG TO FIT", {FontId::MetricBold20, Align::Centre, true});
		expect_golden("../ui/widgets/text_shrink_48.pbm", r.pbm());
	});
	it("renders the same widgets on a 128x64 panel", _ {
		uitest::Rig64 r;
		title(r.frame, "SOUND");
		text(r.frame, Rect{0, 20, 128, 9}, "C", {FontId::Apple7, Align::Centre});
		expect(r.lit(0, 12)).to_be_true();          // separator at 12 + 0
		expect_golden("../ui/widgets/title_text_64.pbm", r.pbm());
	});
});

CPPSPEC_SPEC(text_widgets)
```

(`expect_golden` resolves relative to `GFX_GOLDEN_DIR`, i.e. `tests/golden/gfx`; `../ui/widgets/...` lands in `tests/golden/ui/widgets/`. Create that directory.)

- [ ] **Step 2: Run to see them fail** — headers not found.

- [ ] **Step 3: Implement**

`frame.h`/`frame.cpp`: `OledFrame` as declared; every helper computes canvas coordinates `origin + local`, intersects with `clip`, and forwards to the gfx `Canvas` method (`draw_pixel`, `draw_horizontal_line`, `draw_line`, `draw_rectangle`, `draw_rectangle_rounded(..., style.rounded)`, `invert_area`, `invert_area_rounded(..., style.rounded)`, `clear_area_exact`, `draw_string`, `draw_graphic_multi_line`, `draw_icon`). `region` constructs a child `OledFrame{canvas, clip.intersect(translated local), origin + local.xy, style, input, gate}` and calls `body(child)`. `root_frame` sets `origin = {0, geometry.visible_top}` and `clip = {0, visible_top, width, visible_height}`.

`text.cpp`:

```cpp
int32_t text_height_for(FontId f) { switch (f) { case FontId::Px5: return 5; case FontId::Apple7: return 7; case FontId::MetricBold9: return 10; case FontId::MetricBold13: return 13; default: return 20; } }
int32_t spacing_x_for(FontId f)   { switch (f) { case FontId::Px5: return 6; case FontId::Apple7: return kTextSpacingX; case FontId::MetricBold9: return kTextTitleSpacingX; case FontId::MetricBold13: return kTextTitleSpacingX; default: return kTextHugeSpacingX; } }

Size measure(std::string_view s, FontId f) {
	return {canvas_free_string_width(s, text_height_for(f)), text_height_for(f)};   // uses gfx glyph metrics: get_string_width_in_pixels needs a Canvas today —
}
```

`get_string_width_in_pixels`/`get_char_width_in_pixels` are `Canvas` members in gfx; add free-function equivalents in `gfx/oled/font.h` (`string_width_px(string_view, int32_t text_height)`, `char_width_px`, `char_spacing_px`) by moving the bodies out of `canvas.cpp` (the Canvas members call them) — a small gfx change made in this task, with the gfx goldens as the regression net.

`text()`: for `Align::Centre` and `!shrink`, replicate `drawStringCentred`: `x = rect.x + (rect.w - measure(s).w) / 2`; for `Align::Right`, `x = rect.right() - measure(s).w`; `Align::Left`, `x = rect.x`. For `shrink`, port `drawStringCentredShrinkIfNecessary` (the `maxTextWidth = rect.w / len`, height ladder 20/13/10/7/5, `pixelY += heightDiff >> 1`, `use_width = true`) using `rect.w` where it used `OLED_MAIN_WIDTH_PIXELS`. `title()`: `glyphs(s, 0, kTitleY, kTextTitleSpacingX, 10)` and `hline(kTitleSeparatorY, 0, bounds().w - 1)` when `underline`.

Switch `deluge_ui_oled` to STATIC with `oled/frame.cpp oled/text.cpp`.

- [ ] **Step 4: Record goldens and run**

```bash
mkdir -p tests/golden/ui/widgets
cmake --build build/tests --config Debug && GFX_GOLDEN_UPDATE=1 ctest --test-dir build/tests -C Debug -R "oled_frame|text_widgets" --output-on-failure
ctest --test-dir build/tests -C Debug -R "oled_frame|text_widgets|canvas_golden_spec" --output-on-failure
```
Expected: PASS; `cat tests/golden/ui/widgets/title_48.pbm` shows "SOUND" in the top rows and a full-width line.

- [ ] **Step 5: Commit**

```bash
./dbt format
git add src/ui src/gfx tests/spec/ui tests/golden/ui
git commit -m "feat(ui-oled): OledFrame with regions, text/title/measure"
```

---

### Task 4: `scroll_text`

**Files:**
- Create: `src/ui/oled/scroll_text.{h,cpp}`
- Test: `tests/spec/ui/scroll_text_spec.cpp`
- Ports: `OLED::setupSideScroller`, the scroller branch of `OLED::scrollingAndBlinkingTimerEvent` (`hid/display/oled.cpp`)

**Interfaces:**
- Produces: `Response scroll_text(OledFrame&, gfx::oled::Rect, std::string_view, gfx::oled::FontId, ScrollText&, bool allow_motion = true, bool highlight = false);`
  Semantics (legacy): if the string fits, draw and return; else draw from `state.offset` px; when `allow_motion` (legacy: playback off) and `now_ms >= state.next_step_ms`, advance `offset += dir` by one pixel, `next_step_ms = now + (dir > 0 ? kScrollStepForwardMs : kScrollStepBackMs)`; at either end set `next_step_ms = now + kScrollTime` and flip `dir`; when `!allow_motion` reset `offset = 0`. Always `gate.request_repaint_after(next_step_ms - now, now)` while scrolling. `highlight` inverts the rect after drawing (legacy `doHighlight` with inversion on).

- [ ] **Step 1: Failing spec**

```cpp
#include "support/frames.h"
#include "ui/oled/scroll_text.h"
#include "cppspec.hpp"
using namespace deluge::ui; using namespace deluge::ui::oled; using deluge::gfx::oled::FontId; using deluge::gfx::oled::Rect;

// clang-format off
describe scroll_text_w("ui::oled::scroll_text", $ {
	it("does not move when the text fits", _ {
		uitest::Rig48 r; ScrollText st;
		scroll_text(r.frame, Rect{6, 14, 122, 9}, "SHORT", FontId::Apple7, st);
		expect(st.offset).to_equal(int16_t(0));
		expect(r.gate.next_deadline().has_value()).to_be_false();
	});
	it("steps one pixel per interval and ping-pongs", _ {
		uitest::Rig48 r; ScrollText st;
		const char* s = "A VERY LONG LABEL THAT DOES NOT FIT ON THE PANEL";
		r.input.now_ms = 0;   scroll_text(r.frame, Rect{6, 14, 122, 9}, s, FontId::Apple7, st);   // arms
		expect(*r.gate.next_deadline()).to_equal(uint32_t(kScrollStepForwardMs));
		r.input.now_ms = 15;  scroll_text(r.frame, Rect{6, 14, 122, 9}, s, FontId::Apple7, st);
		expect(st.offset).to_equal(int16_t(1));
		for (int t = 30; st.dir > 0; t += 15) { r.input.now_ms = t; scroll_text(r.frame, Rect{6, 14, 122, 9}, s, FontId::Apple7, st); }
		expect(st.dir).to_equal(int8_t(-1));                       // reached the end and paused
		expect(st.next_step_ms - r.input.now_ms).to_equal(uint32_t(kScrollTime));
	});
	it("resets to the start when motion is not allowed", _ {
		uitest::Rig48 r; ScrollText st; st.offset = 20;
		scroll_text(r.frame, Rect{6, 14, 122, 9}, "A VERY LONG LABEL THAT DOES NOT FIT ON THE PANEL", FontId::Apple7, st, false);
		expect(st.offset).to_equal(int16_t(0));
		expect_golden("../ui/widgets/scroll_text_start_48.pbm", r.pbm());
	});
});
CPPSPEC_SPEC(scroll_text_w)
```

- [ ] **Step 2: Run to see it fail.**

- [ ] **Step 3: Implement** per the semantics above; drawing = `f.clear(rect); f.glyphs(s, rect.x, rect.y, spacing, height, state.offset, rect.right(), false); if (highlight) f.invert(rect);`. The string width comes from `string_width_px`; the scrollable distance is `width - rect.w`.

- [ ] **Step 4: Record golden, run, commit**

```bash
GFX_GOLDEN_UPDATE=1 ctest --test-dir build/tests -C Debug -R scroll_text_w; ctest --test-dir build/tests -C Debug -R scroll_text_w --output-on-failure
./dbt format && git add src/ui tests/spec/ui tests/golden/ui && git commit -m "feat(ui-oled): scroll_text with legacy ping-pong timing"
```

---

### Task 5: `list` and `scrollbar`

**Files:**
- Create: `src/ui/oled/list.{h,cpp}`
- Test: `tests/spec/ui/list_spec.cpp`
- Ports: `MenuItem::drawItemsForOled` (`gui/menu_item/menu_item.cpp:47-66`), `Selection::drawPixelsForOled` (window logic), `MenuItem::renderSubmenuItemTypeForOled` (icon at `width - kSubmenuIconSpacingX - 3`), `Toggle::renderSubmenuItemTypeForOled`

**Interfaces:**
- Produces:
  ```cpp
  struct ListRow { std::string_view label; std::string_view value = {}; const gfx::oled::Icon* icon = nullptr; bool selectable = true; };
  /// Legacy geometry: rows start at (128×48: kMenuBaseY48, 128×64: kMenuBaseY64) in local y, kTextSpacingY apart,
  /// label at x = kTextSpacingX, highlight = full width, rows y..y+8, style.menu_rows visible. Applies input.turn
  /// (clamped against rows_last), keeps the cursor visible (Selection's window rule), sets rows_last, draws the
  /// icon/value right-aligned, and scrolls the selected label via scroll_text(state.scroll_label).
  Response list(OledFrame&, ListState&, std::span<const ListRow>, ScrollText& scroll_label, bool allow_motion = true);
  void scrollbar(OledFrame&, gfx::oled::Rect, int32_t first, int32_t visible, int32_t total);
  ```
  `Response.changed` when the cursor moved; `entered` when `input.select_pressed` on a selectable row.

- [ ] **Step 1: Failing spec**

```cpp
#include "support/frames.h"
#include "ui/oled/list.h"
#include "cppspec.hpp"
using namespace deluge::ui; using namespace deluge::ui::oled;

// clang-format off
describe list_w("ui::oled::list", $ {
	const ListRow rows[] = {{"ONE"}, {"TWO"}, {"THREE"}, {"FOUR"}, {"FIVE"}};
	it("draws three rows at the legacy positions with the first highlighted", _ {
		uitest::Rig48 r; ListState st; ScrollText sc;
		list(r.frame, st, rows, sc);
		expect(st.rows_last).to_equal(int16_t(5));
		// highlight covers local rows 14..22 → canvas 19..27, full width
		expect(r.lit(0, 19)).to_be_true(); expect(r.lit(127, 27)).to_be_true();
		expect(r.lit(0, 28)).to_be_false();
		expect_golden("../ui/widgets/list_first_48.pbm", r.pbm());
	});
	it("moves the cursor with turn, clamps, and keeps it visible", _ {
		uitest::Rig48 r; ListState st; ScrollText sc;
		list(r.frame, st, rows, sc);
		r.input.turn = 4; Response resp = list(r.frame, st, rows, sc);
		expect(resp.changed).to_be_true(); expect(st.cursor).to_equal(int16_t(4));
		expect(st.scroll).to_equal(int16_t(2));     // rows 2,3,4 visible; cursor on the last row
		r.input.turn = 3; list(r.frame, st, rows, sc);
		expect(st.cursor).to_equal(int16_t(4));     // clamped against rows_last
		expect_golden("../ui/widgets/list_last_48.pbm", r.pbm());
	});
	it("reports entered on select press", _ {
		uitest::Rig48 r; ListState st; ScrollText sc;
		r.input.select_pressed = true;
		expect(list(r.frame, st, rows, sc).entered).to_be_true();
	});
	it("shows five rows on 128x64 with an icon column", _ {
		uitest::Rig64 r; ListState st; ScrollText sc;
		const ListRow with_icon[] = {{"A", {}, &deluge::gfx::oled::icons::checkedBox}, {"B"}, {"C"}, {"D"}, {"E"}, {"F"}};
		list(r.frame, st, with_icon, sc);
		expect_golden("../ui/widgets/list_icons_64.pbm", r.pbm());
	});
});
CPPSPEC_SPEC(list_w)
```

(`icons::checkedBox` is whatever name the gfx plan's Task 7 gave the checked-box icon; use that name.)

- [ ] **Step 2: Run to see it fail.**

- [ ] **Step 3: Implement** — port `drawItemsForOled` with `base_y = (bounds().h + visible_top == 64 ? kMenuBaseY64 : kMenuBaseY48)` (i.e. pick by the canvas height: `f.canvas.geometry().height`), `visible = style.menu_rows`, window = `Selection::drawPixelsForOled`'s `pos/tail` rule; highlight = `f.invert(Rect{0, y, bounds().w, 9})` when `style.invert_highlight`, else `f.vline(0, y, y + 8)` (legacy `invertLeftEdgeForMenuHighlighting`); icon at `x = bounds().w - kSubmenuIconSpacingX - 3`; value right-aligned before the icon. `turn` applied first: `cursor = clamp(cursor + turn, 0, rows_last - 1)` (with `rows_last` from the previous frame; on the first frame use `rows.size()`), then `rows_last = rows.size()`. `scrollbar` draws a 1-px track at `rect.right()-1` and a thumb of `max(2, rect.h * visible / total)` at `rect.y + rect.h * first / total`.

- [ ] **Step 4: Record, run, commit** — `git commit -m "feat(ui-oled): list with legacy menu geometry and Selection windowing; scrollbar"`.

---

### Task 6: Value widgets — `int_value`, `decimal_value`, `enum_value`, `toggle`

**Files:**
- Create: `src/ui/oled/values.{h,cpp}`
- Test: `tests/spec/ui/values_spec.cpp`
- Ports: `Integer::drawInteger`/`drawPixelsForOled`, `IntegerWithOff::drawPixelsForOled` (`gui/menu_item/integer.cpp`), `Decimal::drawPixelsForOled` + `OLED::setupBlink` (`decimal.cpp`, `oled.cpp`), `Toggle::drawPixelsForOled` (`toggle.cpp`), `Selection::drawPixelsForOled` (via `list`)

**Interfaces:**
- Produces:
  ```cpp
  struct Range { int32_t min, max, step = 1; };
  struct ValueStyle { std::string_view unit = {}; bool off_at_zero = false; };
  /// Huge centred number at local y = kValueY (canvas 18 + visible_top); applies input.edit[0] (+edit[1]) when focused.
  Response int_value(OledFrame&, int32_t& value, Range, ValueStyle = {}, bool focused = true);
  /// Legacy Decimal: digits huge at canvas y 20 (absolute, not + visible_top — legacy quirk, reproduced), period half-width,
  /// cursor blink under the digit at edit_pos via Blink (kFlashTime), horizontal moves edit_pos.
  Response decimal_value(OledFrame&, int32_t& value, Range, uint8_t places, int8_t& edit_pos, Blink&, bool focused = true);
  /// Windowed name list (Selection window rule) via list(); turn changes the index; wraps when `wrap`.
  Response enum_value(OledFrame&, int32_t& index, std::span<const std::string_view> names, ListState&, ScrollText&, bool wrap = false);
  /// Two rows (off, on) with the current one highlighted; turn or select toggles.
  Response toggle(OledFrame&, bool& value, std::string_view off_label, std::string_view on_label, ScrollText&);
  ```

- [ ] **Step 1: Failing spec** — one `it` per widget with a golden at 48 and explicit checks: `int_value(42)` lights pixels in canvas rows 23..42 (18+5 .. +20) and none in row 22; `off_at_zero` with 0 draws "OFF"; `decimal_value(1234, places 2)` → "12.34" with the cursor blink area at canvas y 41..42 under the edited digit (`Blink` armed, `gate.next_deadline == kFlashTime`); `enum_value` with 6 names and index 4 shows names 3,4,5 with 4 highlighted; `toggle(true)` highlights the second row; `edit[0] = +1` on a focused `int_value` at max stays at max and `changed == false`; at 41 → 42 `changed == true`.

- [ ] **Step 2: Run to see it fail.**

- [ ] **Step 3: Implement** by porting each named routine with `OLED::main` → `f`, `OLED_MAIN_TOPMOST_PIXEL` → `0` (the frame already sits at `visible_top`; **except** `decimal_value`, whose legacy y=20 is canvas-absolute: use `f.region` with a temporary origin of `-visible_top`… simpler: draw at local `y = 20 - f.canvas.geometry().visible_top`), `intToString` → `std::to_chars` into a 12-byte buffer, `setupBlink` → `Blink` state + `f.invert` when the phase is on + `gate.request_repaint_after(kFlashTime, now)`.

- [ ] **Step 4: Record, run, commit** — `git commit -m "feat(ui-oled): int/decimal/enum/toggle value widgets"`.

---

### Task 7: Meters — bars, knob arc, pan, percent, sliders, filter and envelope curves

**Files:**
- Create: `src/ui/oled/meters.{h,cpp}`
- Test: `tests/spec/ui/meters_spec.cpp`
- Ports: `Number::drawPercent/drawKnob/drawBar/drawSlider/drawLengthSlider/drawPan/drawHpf/drawLpf/drawAttack/drawRelease/drawSidechainDucking` (`gui/menu_item/number.cpp:107-430`)

**Interfaces:**
- Produces (all `void name(OledFrame&, gfx::oled::Rect slot, float norm /*0..1*/ …)`, `slot` = legacy `SlotPosition{start_x, start_y, width, height}` as a `Rect`):
  `percent(f, slot, int32_t value)`, `knob_arc(f, slot, float norm)`, `unipolar_bar(f, slot, float norm)`, `pan(f, slot, int32_t value /*-25..25*/)`, `slider(f, slot, float norm)`, `length_slider(f, slot, float norm, bool min_pos)`, `filter_curve(f, slot, float norm, FilterKind{Lpf, Hpf})`, `envelope_stage(f, slot, float norm, StageKind{Attack, Release})`, `sidechain_ducking(f, slot, int32_t value, int32_t max)`, plus `enum class Hint { None, Percent, Knob, Bar, Slider, LengthSlider, Pan, Lpf, Hpf, Attack, Release, Ducking, Enum, Toggle }` and `void meter(f, slot, Hint, float norm, int32_t raw)` dispatching like `Number::renderInHorizontalMenu`.
  Geometry constants are the legacy ones verbatim: knob radius 10, angles 165°+210°·norm, inner 3.5/outer 9; bar 21×5 at `(slot.x+5, slot.y+kHorizontalSlotYOffset+2)` with outline padding 2; slider 23×11 at `(slot.x+4, slot.y+1)`; lpf/hpf 21×11 at `(slot.x+5, slot.y+1)` slope width 5; attack/release 19×11; pan centre notches. `knob_arc` uses `gfx::oled::icons::knobArc` (the gfx icon registry name for `knobArcIcon`).

- [ ] **Step 1: Failing spec** — for each meter: golden at 48 in a `Rect{0, 14, 31, 25}` slot with `norm ∈ {0, 0.5, 1}` (three slots side by side: x = 0, 32, 64), plus explicit checks: `unipolar_bar(0.5)` inverts exactly columns `bar_start_x .. bar_start_x + 10`; `pan(0)` draws only the midline; `knob_arc(1.0)` lights the pixel at the end of the 375° line (compute with the legacy formula in the test).

- [ ] **Step 2: Run to see it fail.**

- [ ] **Step 3: Implement** by porting each routine body verbatim with `image.` → `f.` helpers and `slot.start_x/start_y/width` → `slot.x/y/w`; `normalize(getValue())` → the `norm` parameter; `getValue()*2` for percent → the `value` parameter. `std::lerp`, `cos/sin` from `<cmath>` (host and target both have libm; the legacy already uses them).

- [ ] **Step 4: Record, run, commit** — `git commit -m "feat(ui-oled): meters — bars, knob arc, pan, sliders, filter/envelope curves"`.

---

### Task 8: `columns` (the horizontal four-slot page)

**Files:**
- Create: `src/ui/oled/columns.{h,cpp}`
- Test: `tests/spec/ui/columns_spec.cpp`
- Ports: `HorizontalMenu::renderMenuItems/renderColumnLabel/renderPageCounters/renderTitle` (`gui/menu_item/horizontal_menu.cpp`), `Submenu/Toggle/Enumeration/Decimal::renderInHorizontalMenu`

**Interfaces:**
- Produces:
  ```cpp
  struct Column { std::string_view label; uint8_t slots = 1; bool show_label = true; bool relevant = true; bool is_submenu = false;
                  void (*render)(OledFrame& slot_frame, bool focused, void* ctx); void* ctx; };
  struct Paging { uint8_t page, total_pages, focused_on_page; };
  /// Legacy geometry: base_y = kMenuBaseY48/64, column_width = w/4, box_height = kHorizontalBoxHeight, label at the
  /// bottom (kTextSpacingY tall, centred, truncated until width+4 < slot width, side ticks for wide non-submenu slots),
  /// value region {x, base_y, box_width-1, box_height-label_height} handed to Column::render via f.region; focused slot
  /// highlighted per style.invert_highlight (label only, or whole slot for icon/submenu columns); page counters top-right
  /// "n/N" as digits + a slanted separator when total_pages > 1; title drawn by the caller via title().
  Response columns(OledFrame&, ColumnsState&, std::span<const Column>, std::string_view title_text);
  ```
  `turn` moves `state.focused` across relevant columns and pages (`state.page`); `Response.changed` on focus move; `entered` on select press over a submenu column.

- [ ] **Step 1: Failing spec** — four columns (`"FREQ"` knob 0.5, `"RES"` bar 0.25, `"MODE"` enum text `"12dB"`, `"MORPH"` submenu arrow) at 48: golden; label row at canvas y = 5 + 14 + 25 − 9 = 35..43; focused column 0 label inverted; `turn = +1` moves focus to column 1 (`changed`); a page of 6 columns shows "1/2" counters at the top right (pixels lit in rows 6..14 near x ≥ 110); `turn` past column 3 flips to page 2 and focus column 0.

- [ ] **Step 2: Run to see it fail.**

- [ ] **Step 3: Implement** — port `renderMenuItems` (without the container map: a `Column` with `slots > 1` occupies that many slots), `renderColumnLabel` (truncation loop, side ticks), `renderPageCounters` (digits + `line(x, y+7, x+2, y+1)`), the highlight switch (`invert_highlight` → invert label rect or whole slot; else a 1-px underline). Title shortening for paged titles (`"A. WORD"`) goes in `title()` as an optional `max_width` — add `void title_paged(OledFrame&, std::string_view, bool paged)` that shortens two-word titles as `renderTitle` does when `paged` and the width exceeds `w - (kTextSpacingX*2 + 10)`.

- [ ] **Step 4: Record, run, commit** — `git commit -m "feat(ui-oled): columns — the four-slot horizontal page with paging and labels"`.

---

### Task 9: Popups, working animation, progress; misc OLED widgets

**Files:**
- Create: `src/ui/core/text_wrap.{h,cpp}`, `src/ui/oled/popup.{h,cpp}`, `src/ui/oled/misc.{h,cpp}`
- Test: `tests/spec/ui/popup_spec.cpp`, `tests/spec/ui/misc_oled_spec.cpp`
- Ports: `breakStringIntoLines` (`oled.cpp`), `OLED::setupPopup`, `OLED::popupText`, `OLED::drawPermanentPopupLookingText`, `OLED::displayWorkingAnimation`, `View::drawOutputNameFromDetails` (drawing half), `OLED::renderEmulated7Seg`, the OLED VU, `Canvas::draw_graphic_multi_line`/`draw_icon` pass-throughs

**Interfaces:**
- Produces:
  ```cpp
  // text_wrap.h (ui-core; pure)
  struct WrappedLines { std::string_view lines[8]; int32_t widths[8]; uint8_t count; int32_t longest; };
  WrappedLines wrap_lines(std::string_view text, int32_t max_width_px, int32_t max_chars_per_line, gfx::oled::FontId);
  // popup.h
  enum class PopupStyle : uint8_t { Framed, Notification /* no border */, Permanent /* square rect */ };
  gfx::oled::Rect popup_rect_for(OledFrame&, gfx::oled::Size content);            // centred, content + kPopupMargin
  void popup_frame(OledFrame&, gfx::oled::Rect, PopupStyle);                       // clear + rounded/square border
  void popup_text(OledFrame&, std::string_view, PopupStyle = PopupStyle::Framed);  // wrap + frame + centred lines (legacy popupText)
  void working(OledFrame&, std::string_view word, Blink&);                         // "Loading" + animated dots
  void progress(OledFrame&, gfx::oled::Rect, int32_t num, int32_t den);
  // misc.h
  struct OutputNameData { std::string_view type_text; std::string_view name; int32_t slot, sub_slot; bool name_empty, edited; };
  void output_name(OledFrame&, const OutputNameData&);
  void segment_digits(OledFrame&, std::span<const uint8_t, 4> segs);
  void vu(OledFrame&, gfx::oled::Rect, uint8_t l, uint8_t r);
  void envelope(OledFrame&, gfx::oled::Rect, float a, float d, float s, float r);
  void graphic(OledFrame&, int32_t x, int32_t y, const uint8_t* data, int32_t w, int32_t h, int32_t bytes_tall, bool reversed = false);
  void icon(OledFrame&, int32_t x, int32_t y, const gfx::oled::Icon&);
  ```

- [ ] **Step 1: Failing specs** — `wrap_lines("HELLO WORLD THIS WRAPS", 116, 19, Apple7)` yields 2 lines with the split at a space; `popup_text("SAVED")` golden at 48 with the rounded border centred (`popup_rect_for` for a 30×9 content = `{(128-42)/2, (43-21)/2, 42, 21}` in local coords); `popup_text` with `PopupStyle::Notification` has no border pixels at the rect edge; `output_name({"SYNTH", "BASS 1", 12, -1, false, false})` golden at 48 and 64 (type text centred at local y 3 on 48, 12 on 64 — legacy `#if` — take `style.title_height`-independent constants `kOutputTypeY48 = 3`, `kOutputTypeY64 = 12` in metrics); `segment_digits({0x3F,0x06,0x5B,0x4F})` golden; `envelope(0.2,0.3,0.7,0.4)` golden.

- [ ] **Step 2: Run to see it fail.**

- [ ] **Step 3: Implement** — `wrap_lines` is a port of `breakStringIntoLines` + `findLineSplitPoint` + `addLine` using `string_width_px`; `popup_text` ports `popupText` (`doubleMargin = kPopupMargin`, `maxCharsPerLine = kPopupMaxCharsPerLine`, lines centred, `textPixelY = (h - textHeight) >> 1`); `Permanent` ports `drawPermanentPopupLookingText` (square `rect`, not rounded); `working` draws `word` centred and `state`-driven dots (`Blink` period 200 ms); `output_name` ports the pixel half of `drawOutputNameFromDetails` (type text centred at the y above; name in the big font, or `slot`/`sub_slot` formatted `"12b"` when `name_empty`; `edited` adds the legacy asterisk); `segment_digits` ports `renderEmulated7Seg` (segments at `33*i+1`, `dy = 17`); `envelope` draws A/D/S/R as four line segments across the rect (a new widget — no legacy pixel parity required; golden only).

- [ ] **Step 4: Record, run, commit** — `git commit -m "feat(ui-oled): popups, text wrapping, working/progress, output name, segment digits, envelope"`.

---

### Task 10: `GridFrame`, `pad`, regions

**Files:**
- Create: `src/ui/grid/frame.{h,cpp}`
- Modify: `src/ui/CMakeLists.txt` (grid → STATIC), `tests/spec/ui/support/frames.h` (`GridRig`)
- Test: `tests/spec/ui/grid_frame_spec.cpp`

**Interfaces:**
- Produces (`deluge::ui::grid`): `GridFrame` per spec B §4 with `pad(x, y, RGB) -> Response` (paints if in region, reports the matching events: `pressed` for a press event at that pad this tick, `released` for a release, `held` if the pad is in `input`'s held set — add `std::span<const PadEvent> held` to `InputSnapshot`? No: `held` derives from `PadMask held_pads` — add `struct PadMask { uint32_t cols[32]; bool test(x,y) const; }` to `ui/core/input.h` and a `held_pads` member to `InputSnapshot`), `local_events()`, `region()`, `bounds()`; `GridFrame root_grid_frame(PadGrid&, const InputSnapshot&, RepaintGate&)`.

- [ ] **Step 1: Failing spec**

```cpp
#include "support/frames.h"
#include "ui/grid/frame.h"
#include "cppspec.hpp"
using namespace deluge::ui; using namespace deluge::ui::grid; using deluge::gfx::grid::Rect;

// clang-format off
describe grid_frame("ui::grid::GridFrame", $ {
	it("paints and hit-tests in one call", _ {
		uitest::GridRig r;
		PadEvent ev[] = {{3, 5, 100, true}};
		r.input.pads = ev;
		Response a = r.frame.pad(3, 5, RGB{255, 0, 0});
		Response b = r.frame.pad(4, 5, RGB{0, 255, 0});
		expect(a.pressed).to_be_true(); expect(a.velocity).to_equal(uint8_t(100));
		expect(b.pressed).to_be_false();
		expect(r.grid.at(3, 5)).to_equal(RGB{255, 0, 0});
		expect(r.grid.dirty_rows() & (1u << 5)).to_be_true();
	});
	it("routes events to exactly one region and translates them", _ {
		uitest::GridRig r;
		PadEvent ev[] = {{10, 2, 255, true}};
		r.input.pads = ev;
		int hits = 0;
		r.frame.region(Rect{0, 0, 8, 8}, [&](GridFrame& left) { if (left.pad(2, 2, RGB{1,1,1}).pressed) hits++; expect(left.local_events().size()).to_equal(size_t(0)); });
		r.frame.region(Rect{8, 0, 8, 8}, [&](GridFrame& right) {
			expect(right.local_events().size()).to_equal(size_t(1));
			expect(int(right.local_events()[0].x)).to_equal(2);
			if (right.pad(2, 2, RGB{2,2,2}).pressed) hits++;
			right.pad(9, 0, RGB{9,9,9});    // outside the region: not painted
		});
		expect(hits).to_equal(1);
		expect(r.grid.at(10, 2)).to_equal(RGB{2, 2, 2});
		expect(r.grid.at(17, 0)).to_equal(RGB{0, 0, 0});
	});
});
CPPSPEC_SPEC(grid_frame)
```

`GridRig` in `frames.h`: `PadGrid grid{kDelugeGrid}; RepaintGate gate; InputSnapshot input = null_input(); GridFrame frame = root_grid_frame(grid, input, gate); std::string state() { return grid_to_state(grid); }`.

- [ ] **Step 2: Run to see it fail.**

- [ ] **Step 3: Implement** — `pad`: `if (!bounds().contains(x, y)) return {}; grid.set(region.x + x, region.y + y, colour); Response r; for (e : events) if (e.x == region.x + x && e.y == region.y + y) { e.pressed ? r.pressed = true, r.velocity = e.velocity : r.released = true; } r.held = input.held_pads.test(...); return r;`. `local_events()` filters `events` by `region` and returns a translated copy in a fixed 32-entry member buffer (filled lazily once per frame). `region` builds a child with `region.intersect(translated)` and the same `events` span.

- [ ] **Step 4: Run, commit** — `git commit -m "feat(ui-grid): GridFrame — paint and hit-test in one call, regions route input"`.

---

### Task 11: Rows — `row`, `clip_row`

**Files:**
- Create: `src/ui/grid/rows.{h,cpp}`
- Test: `tests/spec/ui/rows_spec.cpp`
- Ports: `NoteRow::renderRow` colour rules (`model/note/note_row.cpp:1872-`), `SessionView::renderRow`/`PerformanceView::renderRow` shapes

**Interfaces:**
- Produces:
  ```cpp
  Response row(GridFrame&, int32_t y, std::span<const RGB> cells);                       // paints cells[0..w); pressed x in Response (velocity carries x+1; 0 = none)
  struct NoteCell { uint8_t start_col; uint8_t len_cols; uint8_t velocity; bool starts_within; bool tail; };   // caller windows the model
  struct ClipRowData { RGB colour, tail_colour, blur_colour; std::span<const NoteCell> notes; int32_t playhead_col = -1; };
  /// Legacy NoteRow::renderRow colours: note starting exactly on a square → colour.adjust_fractional((65 + v + v/2) << 8, 255 << 8);
  /// starting within a square → blur_colour; continuing (tail allowed) → tail_colour; nothing → black.
  Response clip_row(GridFrame&, int32_t y, const ClipRowData&);
  ```

- [ ] **Step 1: Failing spec** — `clip_row` with notes `{start 0, len 3, vel 127}`, `{start 5, len 1, vel 64, starts_within}`: col 0 = `colour.adjustFractional((65+127+63)<<8, 255<<8)`, cols 1–2 = tail colour, col 5 = blur colour, col 3 black; golden `.state`; `row` reports the pressed column.

- [ ] **Step 2–4:** implement per the rules (use `RGB::adjustFractional` — existing name on the legacy `RGB`), record, commit `"feat(ui-grid): row and clip_row with NoteRow colour rules"`.

---

### Task 12: Sidebar — `status_pad`, `audition_pad`, `sidebar_columns`

**Files:**
- Create: `src/ui/grid/sidebar.{h,cpp}`
- Test: `tests/spec/ui/sidebar_spec.cpp`
- Ports: `View::getClipMuteSquareColour` (`gui/views/view.cpp`), `SessionView::drawMuteSquare`, `InstrumentClipView::drawAuditionSquare`

**Interfaces:**
- Produces:
  ```cpp
  enum class LaunchStyle : uint8_t { Default, Fill, Once };
  struct StatusColours { RGB active, stopped, solo, fill, once, midi_command, magenta, magenta_dull, red, red_dull; };  // from the colour menus/palette
  struct StatusPad { bool active, soloing_or_armed_solo, armed, wants_linear_record, overdub_clone, any_soloing, arm_flash_on, midi_flash_on, viewing_record_arming, blink_on;
                     bool has_midi_command; LaunchStyle launch; };
  RGB status_colour(const StatusPad&, const StatusColours&);            // pure port of getClipMuteSquareColour
  Response status_pad(GridFrame&, int32_t x, int32_t y, const StatusPad&, const StatusColours&);
  struct AuditionPad { RGB row_colour; bool pressed, midi_flash_on, midi_assigned, holding_for_learn, note_row_flash, selected_drum; };
  Response audition_pad(GridFrame&, int32_t x, int32_t y, const AuditionPad&);
  void sidebar_columns(GridFrame&, std::span<const RGB> left, std::span<const RGB> right);   // two rightmost columns of the frame
  ```

- [ ] **Step 1: Failing spec** — table-driven over `status_colour`: (record-arming, armed, blink on, wants-record) → red; (… overdub clone) → magenta; (arm flash on, armed) → black; soloing → solo colour; stopped + Fill → fill colour; active → active colour; any_soloing dulls; midi flash + has command → midi_command. `audition_pad`: pressed → row colour; midi flash + assigned → midi_command; note_row_flash → `row_colour.forBlur()`.

- [ ] **Step 2–4:** implement as a direct port (the function is already almost pure — the reads of `currentUIMode`, `view.blinkOn`, `midiLearnFlashOn`, `clipArmFlashOn`, `currentSong->getAnyClipsSoloing()`, the colour menus become `StatusPad`/`StatusColours` fields), commit `"feat(ui-grid): status and audition pads with the legacy colour rules"`.

---

### Task 13: Columns — `velocity_column`, `automation_column`, `macros`, `vu_meter`, `tick_squares`

**Files:**
- Create: `src/ui/grid/columns.{h,cpp}`
- Test: `tests/spec/ui/grid_columns_spec.cpp`
- Ports: `View::renderMacros` (`view.cpp`), `View::renderVUMeter`/`getMaxYDisplayForVUMeter` (dBFS table `{-30.8, -26.4, -22.0, -17.6, -13.2, -8.8, -4.4, -0.2}`), `PadLEDs::setTickSquares` (the colour part), automation/velocity bars from `automation_view.cpp`

**Interfaces:**
- Produces:
  ```cpp
  void vu_meter(GridFrame&, int32_t x, float level_l, float level_r);       // rows 0-4 green, 5-6 orange, 7 red; row from dBFS = (level - 16.7) * 4 vs the table
  struct MacroCell { enum class Kind : uint8_t { None, ClipLaunch, OutputCycle, Section } kind; bool clip_active, clip_armed, arm_flash, held; };
  void macros(GridFrame&, int32_t x, std::span<const MacroCell> rows, int32_t selected);   // renderMacros colours + adjust(255, held ? 1 : 8)
  void velocity_column(GridFrame&, int32_t x, uint8_t velocity /*0..127*/, RGB colour);   // rows from bottom, height = velocity*8/128
  void automation_column(GridFrame&, int32_t x, int32_t value, int32_t min, int32_t max, bool bipolar, RGB colour);
  struct TickSquare { uint8_t col; uint8_t colour_kind; };   // colour_kind 0 = normal, 1 = muted, 2 = red (legacy)
  void tick_squares(GridFrame&, std::span<const TickSquare> per_row, RGB muted_colour);   // draws over painted rows
  ```

- [ ] **Step 1: Failing spec** — `vu_meter(x, 16.7, 9)` lights rows 0..7 on the left (clipping) and none on the right; `macros` colours per kind; `tick_squares` overlays column 4 on row 2 with the legacy "muted" transform (channels ≥ 64 get `+ (1 << idx)`); `.state` goldens.

- [ ] **Step 2–4:** implement, record, commit `"feat(ui-grid): vu meter, macros, velocity/automation columns, tick squares"`.

---

### Task 14: Waveform on pads; keyboard adapters; text keyboard; greyout; gestures

**Files:**
- Create: `src/ui/grid/waveform.{h,cpp}`, `src/ui/grid/keyboard.{h,cpp}`, `src/ui/grid/effects.{h,cpp}`
- Test: `tests/spec/ui/waveform_grid_spec.cpp`, `tests/spec/ui/keyboard_spec.cpp`, `tests/spec/ui/effects_spec.cpp`
- Ports: `WaveformRenderer::renderAsSingleRow` (colour math) and `drawColBar` (`gui/waveform/waveform_renderer.cpp`), `QwertyUI::drawKeys` + `keyboardChars` (`gui/ui/qwerty_ui.cpp`), `PadLEDs` greyout amount application, per-view tap/hold FSMs

**Interfaces:**
- Produces:
  ```cpp
  // waveform.h — rendering only; peak scanning stays with the model
  struct PeakCol { int32_t min24, max24; bool valid; };                 // legacy 8.24 fixed-point peaks
  void waveform_row(GridFrame&, int32_t y, std::span<const uint8_t> brightness /*0..255 per col*/, RGB base, bool reversed);   // renderAsSingleRow colour math: v = b*b; channel = clamp(((v*ch)>>16 + 6) & ~15)
  void waveform_full(GridFrame&, std::span<const PeakCol>, int32_t brightness, std::optional<RGB>);                            // drawColBar per column
  // keyboard.h
  enum class KeyboardLayoutKind : uint8_t { Qwerty, Azerty };
  struct TextKeyboardState { bool shift; };
  /// drawKeys colours: digits row monochrome(64), letter rows monochrome(10), home keys 64/160, space 160, backspace red, enter green, shift blue.
  /// Returns the typed character in Response.velocity (0 = none), with pressed/released for the pad.
  Response text_keyboard(GridFrame&, TextKeyboardState&, KeyboardLayoutKind, int32_t home_row = 3);
  char text_keyboard_char_at(KeyboardLayoutKind, int32_t x, int32_t y, int32_t home_row, bool shift);   // pure; keyboardChars table
  /// Adapters over the legacy interfaces (kept until phase 6): render into a scratch RGB image[8][18] then copy rows into the frame.
  template <class Layout> void keyboard(GridFrame&, Layout& layout);          // calls layout.renderPads(image) / renderSidebarPads(image)
  template <class Column> void control_column(GridFrame&, int32_t x, Column& c, void* layout);  // c.renderColumn(image, x, layout)
  // effects.h
  struct GreyoutMask { uint32_t cols, rows; };
  void greyout(GridFrame&, GreyoutMask, int32_t amount /*RGB::greyOut proportion*/);
  enum class GestureEvent : uint8_t { None, Tap, Hold, Release, SecondPad };
  struct HoldPolicy { uint32_t short_press_ms = 500; };
  GestureEvent gesture(GridFrame&, Hold&, HoldPolicy, int32_t& second_x, int32_t& second_y);
  ```
  The two adapter templates live in the header (they are the only templates in the toolkit and exist so `ui-grid` never includes `gui/ui/keyboard/layout.h`; the concrete types are supplied by `deluge-scenes`).

- [ ] **Step 1: Failing specs** — `waveform_row` with brightness 255 on col 0 and 0 on col 1 → col 0 = base clamped by the legacy formula, col 1 black; `text_keyboard_char_at(Qwerty, 3, home_row+2, 3, false) == '1'`, `(…, 3, home_row+1) == 'Q'`, `(Azerty, 3, home_row+1) == 'A'`; `text_keyboard` golden `.state` matches the `drawKeys` colour map (assert the backspace pads are red and the enter pads green); `greyout` with cols mask bit 0 dims column 0 only; `gesture`: press at t=0, release at t=100 → `Tap`; press at t=0, still held at t=600 → `Hold` once; a second pad press while held → `SecondPad` with its coordinates.

- [ ] **Step 2–4:** implement, record, commit `"feat(ui-grid): waveform rendering, text keyboard, layout adapters, greyout, gestures"`.

---

### Task 15: Layer rules for `src/ui`, README, size check

**Files:**
- Modify: `scripts/check_gfx_layer.py` → generalise the allowed-prefix rule per root (`src/gfx` → `gfx/`; `src/ui` → `ui/`, `gfx/`; `src/ui/oled` may not include `ui/grid/` and vice versa) or, if the notify plan's `check_layer_rules.py` has landed, add rows there and delete `check_gfx_layer.py`
- Modify: `src/ui/README.md`, `tests/CMakeLists.txt` (ctest for the ui rules)
- Test: a deliberate `#include "ui/grid/frame.h"` in `src/ui/oled/text.h` fails the check; the tree passes

- [ ] **Step 1: Extend the checker** (whichever exists) with:
  - `ui-includes-outside`: files under `src/ui/**` may include only `ui/`, `gfx/` prefixes;
  - `ui-oled-grid-cross`: `src/ui/oled/**` may not include `ui/grid/`; `src/ui/grid/**` may not include `ui/oled/`;
  - and no `<` system-header exceptions needed beyond the standard library.

- [ ] **Step 2: Prove it** — add the cross include, run the check (exit 1), remove it, run again (exit 0), run the whole suite: `ctest --test-dir build/tests -C Debug --output-on-failure` all PASS; `./dbt build Debug` builds; `./dbt sizediff` (or the link-map diff) shows the three libraries add code only for what is referenced — nothing yet, since no firmware code calls them.

- [ ] **Step 3: README** — add the widget/component catalogue table from spec B §6–7 with the file each lives in, and the parity note ("every OLED widget pins legacy geometry in `tests/spec/ui`").

- [ ] **Step 4: Commit**

```bash
git add scripts tests/CMakeLists.txt src/ui/README.md
git commit -m "test(ui): enforce toolkit include rules; document the widget catalogue"
```

---

## Self-review notes

- **Spec coverage.** §3 core types (Task 2, incl. `LedFrame`; `PadMask`/`held_pads` added to `InputSnapshot` in Task 10 — update spec B §3 when Task 10 lands). §4 frames (Tasks 3, 10; `Frames` struct is the shell's and is not built here). §5 focus rules (`list` clamps against `rows_last`, navigation calls `request_repaint` — Tasks 5, 8). §6 OLED catalogue: `title`/`text` (3), `scroll_text` (4), `list`/`scrollbar` (5), `int_value`/`decimal_value`/`enum_value`/`toggle` (6), bars/`knob_arc`/`pan`/`filter_curve`/envelope stages (7), `columns` (8), `popup_frame`/`popup_text`/`progress`/`working`/`vu`/`output_name`/`segment_digits`/`envelope`/`graphic`/`icon` (9); `waveform` on OLED is deferred to the sample-marker scene (its data shape depends on `WaveformRenderData`, which is model-side) — noted here, not claimed. §7 grid catalogue: `pad` (10), `row`/`clip_row` (11), `status_pad`/`audition_pad`/`sidebar_columns` (12), `velocity_column`/`automation_column`/`macros`/`vu_meter`/`tick_squares` (13), `waveform_row`/`waveform_full`/`keyboard`/`control_column`/`text_keyboard`/`greyout`/`gesture` (14). §8 text/l10n: widgets take `string_view` throughout. §9 tests: goldens at 48 and 64 per task; null-input state tests; region routing (10).
- **Types consistent:** `OledFrame` helpers (`pixel`, `hline`, `vline`, `line`, `rect`, `rect_rounded`, `invert`, `invert_rounded`, `clear`, `glyphs`, `graphic`, `icon`, `region`, `bounds`), `TextStyle{font, align, shrink}`, `measure`, `string_width_px`, `ListRow`, `ListState`, `ScrollText`, `Blink`, `Response{changed, focused, entered, pressed, released, held, velocity}`, `GridFrame::pad/local_events/region/bounds`, `PadEvent{x, y, velocity, pressed}`, `GreyoutMask{cols, rows}` — same spelling everywhere.
- **Not placeholders:** each "port" step names the legacy function, the substitutions, and the constants; the geometry the tests assert is derived from the quoted legacy code in this plan's source survey (`kMenuBaseY48 = 14`, separator `12 + visible_top`, value `18 + visible_top`, decimal `y = 20` absolute, slot geometry per meter).
