# gfx extraction (UI redesign phases 1–2) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extend `libdeluge` for multiple displays, then extract the OLED and grid drawing primitives into two globals-free, host-testable libraries (`deluge_gfx_oled`, `deluge_gfx_grid`) with byte-exact golden tests — with the firmware's rendered frames unchanged.

**Architecture:** `src/gfx/oled` gets a `Canvas` that draws into caller-provided storage with a runtime `Geometry`, plus fonts, text metrics and icons; `src/gfx/grid` gets `RGB`, the palette, a dimension-parameterised `PadGrid` and pure tweens. The existing `deluge::hid::display::oled_canvas::Canvas` and `gui/colour` headers become thin façades over the new libraries so the ~140 existing call sites do not change. Goldens are recorded from the *current* renderer before any extraction and must still pass afterwards.

**Tech Stack:** C++26 / C23, CMake (Ninja Multi-Config for tests), CppSpec (`tests/spec`, `*_spec.cpp` auto-discovered), CppUTest (`tests/unit`), the `sim/` host build (`deluge_host`), `./dbt build Debug` for firmware, `./dbt rust --host` for the Rust BSP, `./dbt format` (clang-format).

**Spec:** `docs/superpowers/specs/2026-08-26-ui-stack-redesign-design.md` — §6 (gfx), §10 (libdeluge extension), §11 (testing), §12 phases 1–2. Read it first.

## Global Constraints

- Branch: work on `docs/ui-stack-redesign-spec` (off local `next`). Firmware is verified with `./dbt build Debug` only (never Release).
- **Byte-identical frames**: after every task the golden PBMs recorded in Task 4 must pass unchanged. Any diff is a bug in the task, not a reason to re-record.
- `src/gfx/**` may include only: its own headers, the C++ standard library, and `<etl/...>` is **not** allowed. No `definitions*.h`, no `board_config.h`, no `libdeluge/`, no `gui/`, no `hid/`, no `model/`, no `storage/`. Task 13 enforces this mechanically.
- No mutable globals in `src/gfx/**`. No heap allocation in drawing paths (`std::vector` in the current `drawGraphicMultiLine` is removed).
- Framebuffers stay **1-bpp page-major** (`bytes[page * width + col]`, bit `b` = panel row `page*8 + b`) — the `deluge_display_blit_oled` wire format.
- Public names in `src/gfx`: namespaces `deluge::gfx::oled` and `deluge::gfx::grid`; files, functions, methods, members and locals `snake_case` (the codebase is migrating to snake_case; new libraries start there); types `PascalCase`; constants keep the `k` prefix. The gfx `Canvas` API keeps the *shape* of the old one (same operations, same parameter order) but every method is renamed to snake_case; only the `hid/display` façade keeps the legacy camelCase names, as inline forwarders.
- Commit style: `type(scope): imperative summary` (see `git log`). One commit per task step that says "Commit".
- Format before every commit: `./dbt format`.
- Test commands: configure once with `cmake -S tests -B build/tests -G "Ninja Multi-Config"`; build with `cmake --build build/tests --config Debug`; run one with `ctest --test-dir build/tests -C Debug -R <name> --output-on-failure`. `./dbt test` does all three.
- Sim build: `cmake -B build-sim -S sim -G Ninja && cmake --build build-sim --target deluge_host deluge_host_boundary`.
- Forwarding façades carry a removal note: `// FACADE (remove after UI phase 5): forwards to gfx/...`.

---

## File structure

Created:

```
include/libdeluge/board.h            + DelugeDisplayDesc, display_count, displays
include/libdeluge/display.h          + deluge_display_blit_oled_n, deluge_display_busy_n
src/gfx/CMakeLists.txt               deluge_gfx_oled, deluge_gfx_grid STATIC targets
src/gfx/README.md                    layer rules
src/gfx/oled/geometry.h              Geometry, Point, Size, Rect, Insets, kEdge/kCentre
src/gfx/oled/font.h / font.cpp       Font, FontId, font_for_text_height, text metric constants
src/gfx/oled/fonts/fonts.c / fonts.h moved from src/deluge/gui/fonts (data only)
src/gfx/oled/icon.h                  Icon
src/gfx/oled/icons.h / icons.cpp     the DEFINE_ICON icons, namespace icons
src/gfx/oled/canvas.h / canvas.cpp   Canvas (non-owning), OwnedCanvas<W,H,TOP>
src/gfx/grid/rgb.h / rgb.cpp         moved from src/deluge/gui/colour
src/gfx/grid/palette.h               moved from src/deluge/gui/colour
src/gfx/grid/pad_grid.h / .cpp       GridDimensions, PadGrid
src/gfx/grid/tween.h / tween.cpp     fade, scroll_horizontal, scroll_vertical, grey_out
tests/spec/gfx/support/pbm.h         page-major → P1 PBM text
tests/spec/gfx/support/golden.h      golden compare/update helper
tests/spec/gfx/canvas_golden_spec.cpp
tests/spec/gfx/geometry_spec.cpp
tests/spec/gfx/font_spec.cpp
tests/spec/gfx/canvas_spec.cpp
tests/spec/gfx/pad_grid_spec.cpp
tests/spec/gfx/tween_spec.cpp
tests/golden/gfx/oled/*.pbm          committed goldens
tests/golden/gfx/grid/*.state        committed goldens
scripts/check_gfx_layer.py           include-rule checker (ctest)
```

Modified (façades and consumers) — note `src/bsp/rza1/**` is retired dead source and is never edited:

```
src/deluge/hid/display/oled_canvas/canvas.h / canvas.cpp   → façade subclass of OwnedCanvas<128,48,5>
src/deluge/hid/display/oled.h / oled_icons.cpp             → Icon from gfx; icons reference gfx::oled::icons
src/deluge/gui/fonts/fonts.h                               → façade
src/deluge/gui/colour/rgb.h, palette.h, colour.h            → façades
src/definitions_cxx.hpp                                     → kText* come from gfx/oled/font.h
src/deluge/CMakeLists.txt, src/CMakeLists.txt, sim/CMakeLists.txt, tests/spec/CMakeLists.txt, tests/unit/CMakeLists.txt
src/bsp/host/host_bsp.c, host_link.h, host_link.c; src/bsp/rust/src/board.rs, display.rs
docs/dev/host_emulator.md
```

---

### Task 1: libdeluge multi-display extension (C side)

**Files:**
- Modify: `include/libdeluge/board.h` (after `display_height`, ~line 54)
- Modify: `include/libdeluge/display.h` (after `deluge_display_busy`, ~line 40)
- Modify: `src/bsp/host/host_bsp.c:255-280` (board), `:328-340` (blit)
- Modify: `src/bsp/host/host_link.h:40-48`, `docs/dev/host_emulator.md` (wire table)
- Test: `sim` build of `deluge_host_boundary` + `./dbt build Debug`

**Interfaces:**
- Produces (C ABI):
  ```c
  typedef struct DelugeDisplayDesc { uint16_t width; uint16_t height; uint8_t visible_top; } DelugeDisplayDesc;
  /* in DelugeBoard, after display_height: */
  uint8_t display_count;               /* number of OLED panels; 0 or 1 on today's boards */
  const DelugeDisplayDesc* displays;   /* display_count entries; [0] mirrors display_width/height */
  DelugeStatus deluge_display_blit_oled_n(uint8_t index, const uint8_t* pixels, uint16_t width, uint16_t height);
  bool deluge_display_busy_n(uint8_t index);
  ```
- Host env override: `DELUGE_HOST_DISPLAYS="128x48+5,128x64+0"` (comma-separated `WxH+visible_top`, max 4).
- Host link message: `HOST_LINK_MSG_UPDATE_DISPLAY_N 0x2B`, payload `[index][pixels...]`; index 0 still uses `0x20`.

- [ ] **Step 1: Add the types and prototypes**

In `include/libdeluge/board.h`, before `typedef struct DelugeBoard`:

```c
/// Geometry of one OLED panel. `visible_top` is the number of top rows hidden by
/// the faceplate (5 on the Deluge); the application draws below it. Distinct
/// from any controller-side page offset, which stays inside the BSP.
typedef struct DelugeDisplayDesc {
	uint16_t width;      ///< pixels
	uint16_t height;     ///< pixels, multiple of 8
	uint8_t visible_top; ///< rows hidden at the top
} DelugeDisplayDesc;
```

Inside `DelugeBoard`, directly after `uint16_t display_height;`:

```c
	/// Multi-display extension. `display_count` OLED panels; `displays[0]` mirrors
	/// `display_width`/`display_height` so single-display callers need not change.
	uint8_t display_count;
	const DelugeDisplayDesc* displays;
```

In `include/libdeluge/display.h`, after `bool deluge_display_busy(void);`:

```c
/// Blit to panel `index` (0 .. deluge_board()->display_count-1). Index 0 is the
/// same panel as `deluge_display_blit_oled`. DELUGE_ERR_PARAM for an index the
/// board does not have. [task]
DelugeStatus deluge_display_blit_oled_n(uint8_t index, const uint8_t* pixels, uint16_t width, uint16_t height);
/// Per-panel `deluge_display_busy`. Always false for an index the board lacks. [task] [isr]
bool deluge_display_busy_n(uint8_t index);
```

- [ ] **Step 2: (removed) — the C RZ/A1 BSP is retired**

`src/bsp/rza1/` is dead source since `e58003257` ("build: drop the retired legacy RZA1
firmware image"); nothing compiles it. The device implementation of this extension is the
Rust BSP (Task 2). Do not edit `src/bsp/rza1`.

- [ ] **Step 3: Implement on the host BSP with the env override**

`src/bsp/host/host_link.h` — add after `HOST_LINK_MSG_CLEAR_ALL_LEDS 0x2A`:

```c
#define HOST_LINK_MSG_UPDATE_DISPLAY_N 0x2B // [index][page-major pixels]; host-only extension
```

`src/bsp/host/host_bsp.c` — replace the static `host_board` display fields with a mutable descriptor filled at first `deluge_board()` call:

```c
#define HOST_MAX_DISPLAYS 4
static DelugeDisplayDesc host_displays[HOST_MAX_DISPLAYS] = {{128, 48, 5}};
static DelugeBoard host_board = { /* existing fields unchanged ... */
    .display_kind = DELUGE_DISPLAY_OLED,
    .display_width = 128,
    .display_height = 48,
    .display_count = 1,
    .displays = host_displays,
    /* ... */ };

// DELUGE_HOST_DISPLAYS="128x48+5,128x64+0" overrides the panel set so the shell can be
// exercised against a future board's descriptor before that board's BSP exists.
static void host_board_apply_env(void) {
	static bool applied = false;
	if (applied) return;
	applied = true;
	const char* spec = getenv("DELUGE_HOST_DISPLAYS");
	if (!spec || !*spec) return;
	uint8_t n = 0;
	const char* p = spec;
	while (*p && n < HOST_MAX_DISPLAYS) {
		unsigned w, h, top = 0;
		int used = 0;
		if (sscanf(p, "%ux%u+%u%n", &w, &h, &top, &used) < 2) break;
		host_displays[n++] = (DelugeDisplayDesc){(uint16_t)w, (uint16_t)h, (uint8_t)top};
		p += used;
		if (*p == ',') p++;
	}
	if (n == 0) return;
	host_board.display_count = n;
	host_board.display_width = host_displays[0].width;
	host_board.display_height = host_displays[0].height;
}

const DelugeBoard* deluge_board(void) {
	host_board_apply_env();
	return &host_board;
}
```

(`#include <stdio.h>` and `<stdlib.h>` at the top if not present.) Then:

```c
DelugeStatus deluge_display_blit_oled_n(uint8_t index, const uint8_t* pixels, uint16_t width, uint16_t height) {
	host_board_apply_env();
	if (index >= host_board.display_count) return DELUGE_ERR_PARAM;
	if (index == 0) return deluge_display_blit_oled(pixels, width, height);
	if (host_link_active()) {
		uint16_t bytes = (uint16_t)((height >> 3) * width);
		uint8_t buf[1 + 128 * 8]; // index + up to 128x64
		if (bytes > sizeof(buf) - 1) return DELUGE_ERR_PARAM;
		buf[0] = index;
		memcpy(buf + 1, pixels, bytes);
		host_link_send(HOST_LINK_MSG_UPDATE_DISPLAY_N, buf, (uint16_t)(bytes + 1));
	}
	return DELUGE_OK;
}

bool deluge_display_busy_n(uint8_t index) {
	(void)index;
	return false;
}
```

- [ ] **Step 4: Document the wire extension**

In `docs/dev/host_emulator.md`, add a row to the "Firmware → GUI" table:

```
| `0x2B` | UpdateDisplayN | `index, 768/1024-byte page-major OLED` | `deluge_display_blit_oled_n(index, px, w, h)` — host-only extension for multi-display boards; index 0 always uses `0x20` |
```

and a sentence under "Running": `DELUGE_HOST_DISPLAYS="128x48+5,128x64+0"` selects the panel set the host board reports.

- [ ] **Step 5: Build both consumers**

Run: `cmake -B build-sim -S sim -G Ninja && cmake --build build-sim --target deluge_host_boundary deluge_host`
Expected: builds. Then `DELUGE_HOST_DISPLAYS="128x48+5,128x64+0" ./build-sim/deluge_host_boundary` runs (it prints the board name via `host_main.c:44`; no crash).
Run: `./dbt build Debug`
Expected: the app + Rust BSP image links (the app still calls only the 1-display entry points; the Rust BSP gains the new ones in Task 2).

- [ ] **Step 6: Commit**

```bash
./dbt format
git add include/libdeluge/board.h include/libdeluge/display.h src/bsp/host/host_bsp.c src/bsp/host/host_link.h docs/dev/host_emulator.md
git commit -m "feat(libdeluge): describe multiple OLED panels and blit by index"
```

---

### Task 2: libdeluge multi-display extension (Rust BSP)

**Files:**
- Modify: `src/bsp/rust/src/board.rs:38-55`, `src/bsp/rust/src/display.rs:65-95`
- Test: `./dbt rust --host` (bindgen regenerates from the headers)

**Interfaces:**
- Consumes: Task 1's header changes (bindgen picks up `DelugeDisplayDesc`, the two new fields, and the two new functions).

- [ ] **Step 1: Extend the board literal**

In `src/bsp/rust/src/board.rs`, above `static DELUGE_BOARD`:

```rust
static DELUGE_DISPLAYS: [DelugeDisplayDesc; 1] = [DelugeDisplayDesc { width: 128, height: 48, visible_top: 5 }];
```

and inside the `DelugeBoard { ... }` literal after `display_height: 48, // OLED_MAIN_HEIGHT_PIXELS`:

```rust
    display_count: 1,
    displays: DELUGE_DISPLAYS.as_ptr(),
```

(`BoardDescriptor` already carries the `unsafe impl Sync`; the raw pointer field is what needs it.)

- [ ] **Step 2: Add the indexed entry points**

In `src/bsp/rust/src/display.rs` after `deluge_display_busy`:

```rust
#[unsafe(no_mangle)]
pub extern "C" fn deluge_display_blit_oled_n(index: u8, pixels: *const u8, width: u16, height: u16) -> DelugeStatus {
    if index != 0 {
        return DELUGE_ERR_PARAM;
    }
    deluge_display_blit_oled(pixels, width, height)
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_display_busy_n(index: u8) -> bool {
    index == 0 && deluge_display_busy()
}
```

Import `DELUGE_ERR_PARAM` alongside `DELUGE_OK` at the top of the file.

- [ ] **Step 3: Build**

Run: `./dbt rust --host`
Expected: builds; bindgen emits `DelugeDisplayDesc` and the struct literal type-checks (a missing field would fail to compile here — that is the test).

- [ ] **Step 4: Commit**

```bash
git add src/bsp/rust/src/board.rs src/bsp/rust/src/display.rs
git commit -m "feat(bsp-rust): report the display set and blit by index"
```

---

### Task 3: Decouple the current Canvas from `oled.h` and `FlashStorage` (prep, behaviour-preserving)

Why: the golden harness (Task 4) must compile *today's* `canvas.cpp` on the host. It currently includes `hid/display/oled.h` (only for `Icon`) and `storage/flash_storage.h` (one read in `invertLeftEdgeForMenuHighlighting`). Both drag in the whole firmware.

**Files:**
- Modify: `src/deluge/hid/display/oled_canvas/canvas.h:29-31` (forward decls), `src/deluge/hid/display/oled.h:36-41` (Icon), `src/deluge/hid/display/oled_canvas/canvas.cpp:22,26,809-816`
- Modify: the two sites that assign `FlashStorage::accessibilityMenuHighlighting` (find with `grep -rn "accessibilityMenuHighlighting =" src/deluge`)
- Test: `./dbt build Debug`

**Interfaces:**
- Produces: `deluge::hid::display::Icon` now defined in `canvas.h`; `static inline bool Canvas::menuHighlightingInverts = true;` replaces the `FlashStorage` read.

- [ ] **Step 1: Move `Icon` into `canvas.h`**

In `canvas.h`, replace `struct Icon;` (line 31) with the full definition cut from `oled.h:36-41`:

```cpp
struct Icon {
	const uint8_t* data;
	uint8_t width;
	uint8_t height;
	uint8_t numBytesTall; // legacy field name, unchanged until Task 7
};
```

Delete the struct from `oled.h` (it includes `oled_canvas/canvas.h` already, so `Icon` stays visible there).

- [ ] **Step 2: Replace the FlashStorage read**

In `canvas.h`, next to `roundedCornersEnabled`:

```cpp
	/// Whether menu highlighting inverts (true) or draws an edge line (false). Mirrors
	/// FlashStorage::accessibilityMenuHighlighting; the settings layer keeps it in sync.
	static inline bool menuHighlightingInverts = true;
```

In `canvas.cpp`, delete `#include "storage/flash_storage.h"` and `#include <hid/display/oled.h>`, and change the body of `invertLeftEdgeForMenuHighlighting` to:

```cpp
	if (!menuHighlightingInverts) {
		drawVerticalLine(xMin, startY, endY);
	}
	else {
		invertAreaRounded(xMin, width, startY, endY);
	}
```

At each site that assigns `FlashStorage::accessibilityMenuHighlighting` (the settings loader in `storage/flash_storage.cpp` and the menu item under `gui/menu_item/runtime_feature/` or `gui/menu_item/`), add immediately after the assignment:

```cpp
	deluge::hid::display::oled_canvas::Canvas::menuHighlightingInverts =
	    (FlashStorage::accessibilityMenuHighlighting != MenuHighlighting::NO_INVERSION);
```

- [ ] **Step 3: Build and verify canvas.cpp no longer pulls the firmware**

Run: `./dbt build Debug`
Expected: builds.
Run: `grep -n "#include" src/deluge/hid/display/oled_canvas/canvas.cpp`
Expected: only `canvas.h`, `definitions_cxx.hpp`, `deluge/util/c_string.h`, `gui/fonts/fonts.h`, `util/etl_string.h`, `<algorithm>`, `<math.h>`, `<vector>`.

- [ ] **Step 4: Commit**

```bash
./dbt format
git add -A src/deluge
git commit -m "refactor(oled): free the canvas from oled.h and FlashStorage"
```

---

### Task 4: Golden harness for the current Canvas

Records what today's renderer draws, as human-readable P1 PBM files. Every later task must leave these passing.

**Files:**
- Create: `tests/spec/gfx/support/pbm.h`, `tests/spec/gfx/support/golden.h`, `tests/spec/gfx/canvas_golden_spec.cpp`, `tests/golden/gfx/oled/*.pbm`
- Modify: `tests/spec/CMakeLists.txt` (add sources to `deluge_spec`, define `GFX_GOLDEN_DIR`)
- Test: `ctest -R canvas_golden_spec`

**Interfaces:**
- Produces: `tests/spec/gfx/support/pbm.h`:
  `std::string page_major_to_pbm(const uint8_t* bytes, int width, int height);` — P1 text, one row per line.
  `tests/spec/gfx/support/golden.h`: `void expect_golden(auto& self, const std::string& name, const std::string& actual);` — `self` is the CppSpec example context (needed by its `expect` macro); compares to `GFX_GOLDEN_DIR/<name>`; with env `GFX_GOLDEN_UPDATE=1` writes it instead.
- Scenes (names are the golden file stems): `primitives`, `text_all_fonts`, `text_alignment`, `icons`, `rounded_and_invert`, `graphic_multiline`, `line_options`.

- [ ] **Step 1: Write the support headers**

`tests/spec/gfx/support/pbm.h`:

```cpp
#pragma once
#include <cstdint>
#include <string>

/// Convert a 1-bpp page-major framebuffer (bytes[page*width+col], bit b = row page*8+b)
/// to a P1 PBM: "P1\n<w> <h>\n" then one line per pixel row of '1'/'0' separated by spaces.
inline std::string page_major_to_pbm(const uint8_t* bytes, int width, int height) {
	std::string out = "P1\n" + std::to_string(width) + " " + std::to_string(height) + "\n";
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			bool on = bytes[(y >> 3) * width + x] & (1u << (y & 7));
			out += on ? '1' : '0';
			if (x + 1 < width) out += ' ';
		}
		out += '\n';
	}
	return out;
}
```

`tests/spec/gfx/support/golden.h`:

```cpp
#pragma once
#include "cppspec.hpp"
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#ifndef GFX_GOLDEN_DIR
#error "GFX_GOLDEN_DIR must be defined by CMake"
#endif

/// Compare `actual` with tests/golden/gfx/<name>. GFX_GOLDEN_UPDATE=1 rewrites the file.
inline void expect_golden(const std::string& name, const std::string& actual) {
	const std::string path = std::string(GFX_GOLDEN_DIR) + "/" + name;
	if (std::getenv("GFX_GOLDEN_UPDATE")) {
		std::ofstream(path) << actual;
	}
	std::ifstream in(path);
	std::stringstream want;
	want << in.rdbuf();
	expect(in.good()).to_be_true();       // golden exists
	expect(actual).to_equal(want.str());  // byte-identical
}
```

- [ ] **Step 2: Write the golden spec against today's Canvas**

`tests/spec/gfx/canvas_golden_spec.cpp`:

```cpp
#include "hid/display/oled_canvas/canvas.h"
#include "support/golden.h"
#include "support/pbm.h"

#include "cppspec.hpp"

using deluge::hid::display::Icon;
using deluge::hid::display::oled_canvas::Canvas;
using deluge::hid::display::oled_canvas::DrawLineOptions;

namespace {
constexpr int W = 128, H = 48;

std::string pbm(Canvas& c) {
	return page_major_to_pbm(&c.hackGetImageStore()[0][0], W, H);
}

// 8x8 arrow used by the icon scenes: one byte per column, bit 0 = top.
constexpr uint8_t kArrowData[8] = {0x18, 0x18, 0x18, 0x7E, 0x3C, 0x18, 0x00, 0x00};
constexpr Icon kArrow{kArrowData, 8, 8, 1};
} // namespace

// clang-format off
describe canvas_golden("Canvas golden frames", $ {
	it("primitives", _ {
		Canvas c; c.clear();
		c.drawPixel(0, 5); c.drawPixel(127, 47);
		c.drawHorizontalLine(10, 4, 60);
		c.drawVerticalLine(70, 6, 40);
		c.drawLine(80, 8, 120, 40);
		c.drawRectangle(2, 12, 30, 30);
		c.drawCircle(100, 20, 7, false);
		c.drawCircle(110, 38, 5, true);
		expect_golden(self, "oled/primitives.pbm", pbm(c));
	});

	it("text_all_fonts", _ {
		Canvas c; c.clear();
		c.drawString("Ag5", 0, 6, 6, 5);     // 5px
		c.drawString("Ag7", 24, 6, 6, 7);    // apple 7
		c.drawString("Ag9", 48, 6, 6, 9);    // apple, y+1
		c.drawString("Ag10", 0, 16, 9, 10);  // metric 9
		c.drawString("Ag13", 40, 16, 9, 13); // metric 13
		c.drawString("Ag20", 0, 28, 18, 20); // metric 20
		expect_golden(self, "oled/text_all_fonts.pbm", pbm(c));
	});

	it("text_alignment", _ {
		Canvas c; c.clear();
		c.drawStringCentred("CENTRE", 6, 6, 7);
		c.drawStringAlignRight("RIGHT", 16, 6, 7);
		c.drawStringCentered("MID", 10, 26, 6, 9, 60);
		c.drawStringCentredShrinkIfNecessary("THIS TITLE IS FAR TOO LONG TO FIT", 36, 9, 10);
		c.drawString("SCROLL", 0, 40, 6, 7, 3, 40, true);
		expect_golden(self, "oled/text_alignment.pbm", pbm(c));
	});

	it("icons", _ {
		Canvas c; c.clear();
		c.drawIcon(kArrow, 4, 6);
		c.drawIcon(kArrow, 20, 9, true);
		c.drawIconCentered(kArrow, 40, 40, 20);
		c.drawGraphicMultiLine(kArrowData, 100, 13, 8, 8, 1);
		expect_golden(self, "oled/icons.pbm", pbm(c));
	});

	it("rounded_and_invert", _ {
		Canvas c; c.clear();
		Canvas::roundedCornersEnabled = true;
		c.drawRectangleRounded(2, 6, 40, 20);
		c.drawRectangleRounded(50, 6, 90, 20, deluge::hid::display::oled_canvas::BIG);
		c.drawString("INV", 4, 26, 6, 7);
		c.invertArea(2, 30, 24, 34);
		c.invertAreaRounded(40, 30, 24, 34);
		Canvas::menuHighlightingInverts = true;
		c.invertLeftEdgeForMenuHighlighting(80, 20, 24, 34);
		Canvas::menuHighlightingInverts = false;
		c.invertLeftEdgeForMenuHighlighting(104, 20, 24, 34);
		c.clearAreaExact(10, 10, 14, 14);
		Canvas::roundedCornersEnabled = false;
		c.drawRectangleRounded(2, 38, 40, 46);
		Canvas::roundedCornersEnabled = true;
		expect_golden(self, "oled/rounded_and_invert.pbm", pbm(c));
	});

	it("graphic_multiline", _ {
		Canvas c; c.clear();
		// 16 columns x 2 bytes tall, drawn at three y offsets to exercise page straddling.
		uint8_t g[32];
		for (int i = 0; i < 32; i++) g[i] = (uint8_t)(0x81 + i);
		c.drawGraphicMultiLine(g, 0, 5, 16, 16, 2);
		c.drawGraphicMultiLine(g, 20, 9, 16, 16, 2);
		c.drawGraphicMultiLine(g, 40, 15, 16, 12, 2);
		c.drawGraphicMultiLine(g, 120, 20, 16, 16, 2); // clipped at right edge
		expect_golden(self, "oled/graphic_multiline.pbm", pbm(c));
	});

	it("line_options", _ {
		Canvas c; c.clear();
		c.drawLine(0, 6, 127, 47, DrawLineOptions{.thick = true});
		c.drawLine(0, 47, 127, 6, DrawLineOptions{.min_x = 20, .max_x = 100});
		c.drawScreenTitle("TITLE");
		expect_golden(self, "oled/line_options.pbm", pbm(c));
	});
});

CPPSPEC_SPEC(canvas_golden)
```

- [ ] **Step 3: Wire it into the spec build**

In `tests/spec/CMakeLists.txt`, add to the `deluge_spec` source list:

```cmake
../../src/deluge/hid/display/oled_canvas/canvas.cpp
../../src/deluge/gui/fonts/fonts.c
```

and after `target_include_directories(deluge_spec ...)`:

```cmake
target_compile_definitions(deluge_spec PUBLIC GFX_GOLDEN_DIR="${CMAKE_CURRENT_LIST_DIR}/../golden/gfx")
```

`create_specs_driver` globs `*_spec.cpp` recursively, so `gfx/canvas_golden_spec.cpp` is discovered and registered as ctest `canvas_golden_spec`. (`deluge_spec` already links `etl` transitively via `tests/CMakeLists.txt`; `canvas.cpp` needs `etl::istring` and `definitions_cxx.hpp`, both header-only.)

- [ ] **Step 4: Record the goldens, then verify they pass**

```bash
mkdir -p tests/golden/gfx/oled tests/golden/gfx/grid
cmake -S tests -B build/tests -G "Ninja Multi-Config" && cmake --build build/tests --config Debug
GFX_GOLDEN_UPDATE=1 ctest --test-dir build/tests -C Debug -R canvas_golden_spec --output-on-failure
ctest --test-dir build/tests -C Debug -R canvas_golden_spec --output-on-failure
```
Expected: second run PASS, 7 examples. Open one file (`cat tests/golden/gfx/oled/text_all_fonts.pbm`) and eyeball that glyphs are visible — a blank frame means the scene drew outside the canvas.

- [ ] **Step 5: Commit the harness and the goldens**

```bash
./dbt format
git add tests/spec/gfx tests/spec/CMakeLists.txt tests/golden/gfx
git commit -m "test(gfx): golden PBM frames for the current OLED canvas"
```

---

### Task 5: `src/gfx` skeleton and geometry

**Files:**
- Create: `src/gfx/CMakeLists.txt`, `src/gfx/README.md`, `src/gfx/oled/geometry.h`
- Modify: `src/CMakeLists.txt:6` (add `add_subdirectory(gfx)` before `deluge`), `sim/CMakeLists.txt:308` (add the gfx subdirectory), `tests/spec/CMakeLists.txt` (add subdirectory + link)
- Test: `tests/spec/gfx/geometry_spec.cpp`

**Interfaces:**
- Produces (`deluge::gfx::oled`):
  ```cpp
  struct Geometry { uint16_t width; uint16_t height; uint8_t visible_top;
                    constexpr uint32_t pages() const; constexpr uint32_t bytes() const;
                    constexpr int32_t visible_height() const; };
  constexpr Geometry kDelugeOled{128, 48, 5};
  constexpr int32_t kEdge = -1;    // "the right edge of the canvas" in endX/rightPos params
  constexpr int32_t kCentre = -1;  // "the horizontal centre" in centrePos params
  struct Point { int32_t x, y; bool operator==(const Point&) const = default; };
  struct Size { int32_t w, h; };
  struct Insets { int32_t top, right, bottom, left; };
  struct Rect { int32_t x, y, w, h;
                constexpr int32_t right() const; constexpr int32_t bottom() const;
                constexpr bool contains(Point) const; constexpr Point centre() const;
                constexpr Rect inset(Insets) const;
                constexpr std::pair<Rect, Rect> split_rows(int32_t at) const;   // top h=at, bottom rest
                constexpr std::pair<Rect, Rect> split_cols(int32_t at) const;   // left w=at, right rest
                constexpr Rect intersect(Rect) const; };
  ```

- [ ] **Step 1: Write the failing spec**

`tests/spec/gfx/geometry_spec.cpp`:

```cpp
#include "gfx/oled/geometry.h"
#include "cppspec.hpp"

using namespace deluge::gfx::oled;

// clang-format off
describe geometry("gfx::oled geometry", $ {
	it("derives pages and bytes from the panel size", _ {
		expect(kDelugeOled.pages()).to_equal(6u);
		expect(kDelugeOled.bytes()).to_equal(768u);
		expect(Geometry{128, 64, 0}.bytes()).to_equal(1024u);
		expect(kDelugeOled.visible_height()).to_equal(43);
	});

	it("splits rects without gaps or overlap", _ {
		Rect r{0, 5, 128, 43};
		auto [top, bottom] = r.split_rows(10);
		expect(top).to_equal(Rect{0, 5, 128, 10});
		expect(bottom).to_equal(Rect{0, 15, 128, 33});
		auto [left, right] = r.split_cols(32);
		expect(left.w + right.w).to_equal(128);
		expect(right.x).to_equal(32);
	});

	it("insets, contains and intersects", _ {
		Rect r{10, 10, 20, 20};
		expect(r.inset({1, 2, 3, 4})).to_equal(Rect{14, 11, 14, 16});
		expect(r.contains({10, 10})).to_be_true();
		expect(r.contains({30, 10})).to_be_false();
		expect(r.intersect(Rect{25, 25, 20, 20})).to_equal(Rect{25, 25, 5, 5});
		expect(r.intersect(Rect{40, 40, 5, 5}).w).to_equal(0);
		expect(r.centre()).to_equal(Point{20, 20});
	});
});

CPPSPEC_SPEC(geometry)
```

`Rect` and `Point` need `operator==` for `to_equal`; give both `= default`.

- [ ] **Step 2: Run it to see it fail**

Run: `cmake --build build/tests --config Debug 2>&1 | tail -3`
Expected: compile error, `gfx/oled/geometry.h` not found.

- [ ] **Step 3: Create the library and the header**

`src/gfx/CMakeLists.txt` — both libraries start as INTERFACE (header-only) targets; Task 6 turns `deluge_gfx_oled` STATIC when `font.cpp` lands and Task 10 does the same for `deluge_gfx_grid` with `rgb.cpp`:

```cmake
# deluge_gfx_oled / deluge_gfx_grid — pure drawing, no policy, no globals, no hardware.
# Depends on nothing but the C/C++ standard library. Included as <gfx/oled/...> and
# <gfx/grid/...> via the universal `-I src` path. Rules: src/gfx/README.md; enforced by
# scripts/check_gfx_layer.py (a ctest).
add_library(deluge_gfx_oled INTERFACE)
add_library(deluge_gfx_grid INTERFACE)
foreach(lib deluge_gfx_oled deluge_gfx_grid)
    target_include_directories(${lib} INTERFACE ${SHARED_INCLUDE})
endforeach()
```

`src/gfx/README.md`:

```markdown
# deluge_gfx_oled / deluge_gfx_grid

Pure drawing primitives for the Deluge UI stack (spec: docs/superpowers/specs/2026-08-26-ui-stack-redesign-design.md §6).

Rules (enforced by `scripts/check_gfx_layer.py`):
- May include only `gfx/**` and the C/C++ standard library.
- No mutable globals. No heap allocation in drawing paths.
- No knowledge of the board: geometry, dimensions and style arrive as values.
- `gfx/oled` and `gfx/grid` do not include each other.
```

`src/gfx/oled/geometry.h`:

```cpp
#pragma once
#include <cstdint>
#include <utility>

namespace deluge::gfx::oled {

/// Panel geometry. `visible_top` rows are hidden by the faceplate; content is laid out
/// below them. This is NOT the controller's page offset (that stays in the BSP).
struct Geometry {
	uint16_t width;
	uint16_t height; // multiple of 8
	uint8_t visible_top;
	constexpr uint32_t pages() const { return height >> 3; }
	constexpr uint32_t bytes() const { return pages() * width; }
	constexpr int32_t visible_height() const { return height - visible_top; }
};

constexpr Geometry kDelugeOled{128, 48, 5};

/// Sentinels for "the canvas edge" / "the canvas centre" in x parameters, so call sites
/// written against a fixed 128-wide panel keep their defaults on any geometry.
constexpr int32_t kEdge = -1;
constexpr int32_t kCentre = -1;

struct Point {
	int32_t x, y;
	constexpr bool operator==(const Point&) const = default;
};
struct Size {
	int32_t w, h;
};
struct Insets {
	int32_t top, right, bottom, left;
};

struct Rect {
	int32_t x, y, w, h;
	constexpr bool operator==(const Rect&) const = default;
	constexpr int32_t right() const { return x + w; }
	constexpr int32_t bottom() const { return y + h; }
	constexpr bool contains(Point p) const { return p.x >= x && p.x < right() && p.y >= y && p.y < bottom(); }
	constexpr Point centre() const { return {x + w / 2, y + h / 2}; }
	constexpr Rect inset(Insets i) const { return {x + i.left, y + i.top, w - i.left - i.right, h - i.top - i.bottom}; }
	constexpr std::pair<Rect, Rect> split_rows(int32_t at) const { return {{x, y, w, at}, {x, y + at, w, h - at}}; }
	constexpr std::pair<Rect, Rect> split_cols(int32_t at) const { return {{x, y, at, h}, {x + at, y, w - at, h}}; }
	constexpr Rect intersect(Rect o) const {
		int32_t nx = x > o.x ? x : o.x, ny = y > o.y ? y : o.y;
		int32_t nr = right() < o.right() ? right() : o.right();
		int32_t nb = bottom() < o.bottom() ? bottom() : o.bottom();
		return {nx, ny, nr > nx ? nr - nx : 0, nb > ny ? nb - ny : 0};
	}
};

} // namespace deluge::gfx::oled
```

Wire the subdirectory in: `src/CMakeLists.txt` — add `add_subdirectory(gfx)` after `add_subdirectory(midi)`; `sim/CMakeLists.txt` — add `add_subdirectory(${REPO_ROOT}/src/gfx ${CMAKE_BINARY_DIR}/gfx)` after the `src/midi` line; `tests/spec/CMakeLists.txt` — add `add_subdirectory(../../src/gfx gfx)` after the `src/midi` line and `deluge_gfx_oled deluge_gfx_grid` to `target_link_libraries(deluge_spec PUBLIC ...)`; `tests/unit/CMakeLists.txt` — add `add_subdirectory(../../src/gfx gfx)` after `FetchContent_MakeAvailable(CppUTest)` and `deluge_gfx_oled deluge_gfx_grid` to `target_link_libraries(UnitTests ...)`.

- [ ] **Step 4: Run the spec**

Run: `cmake --build build/tests --config Debug && ctest --test-dir build/tests -C Debug -R "geometry_spec|canvas_golden_spec" --output-on-failure`
Expected: both PASS.

- [ ] **Step 5: Commit**

```bash
./dbt format
git add src/gfx src/CMakeLists.txt sim/CMakeLists.txt tests/spec/CMakeLists.txt tests/unit/CMakeLists.txt tests/spec/gfx/geometry_spec.cpp
git commit -m "feat(gfx): add the gfx library skeleton and OLED geometry types"
```

---

### Task 6: Fonts and text metrics move to gfx

**Files:**
- Move: `src/deluge/gui/fonts/fonts.c` → `src/gfx/oled/fonts/fonts.c`; `fonts.h` → `src/gfx/oled/fonts/fonts.h` (`git mv`)
- Create: `src/gfx/oled/font.h`, `src/gfx/oled/font.cpp`, `src/deluge/gui/fonts/fonts.h` (façade)
- Modify: `src/definitions_cxx.hpp:968-983` (kText* constants), `src/gfx/CMakeLists.txt`, `src/deluge/hid/display/oled_canvas/canvas.cpp:464-510,562-580` (use `font_for_text_height`)
- Test: `tests/spec/gfx/font_spec.cpp`; `canvas_golden_spec` unchanged

**Interfaces:**
- Produces (`deluge::gfx::oled`):
  ```cpp
  // font.h
  constexpr int32_t kTextSpacingX = 6, kTextSpacingY = 9, kTextSizeYUpdated = 7,
                    kTextTitleSpacingX = 9, kTextTitleSizeY = 10, kTextBigSizeY = 13,
                    kTextHugeSpacingX = 18, kTextHugeSizeY = 20;
  enum class FontId : uint8_t { Px5, Apple7, MetricBold9, MetricBold13, MetricBold20 };
  struct Font { FontId id; const uint8_t* bitmap; const lv_font_glyph_dsc_t* glyphs;
                uint8_t native_height; uint8_t render_height; int8_t y_adjust; bool monospaced; };
  const Font& font_for_text_height(int32_t text_height);   // the switch from Canvas::drawChar, verbatim
  int32_t glyph_index(uint8_t c);                        // Canvas::getCharIndex, verbatim
  ```
  Mapping (from `canvas.cpp:466-509`): 5,6→Px5 (render 5, native 5); 7,8→Apple7 (render 7, native 8); 9→Apple7 with `y_adjust=1`; 10→MetricBold9 (render 9); 13→MetricBold13; 20 and default→MetricBold20. `monospaced` is true for Apple7 only.

- [ ] **Step 1: Write the failing spec**

`tests/spec/gfx/font_spec.cpp`:

```cpp
#include "gfx/oled/font.h"
#include "cppspec.hpp"

using namespace deluge::gfx::oled;

// clang-format off
describe font("gfx::oled fonts", $ {
	it("maps text heights to fonts exactly as the canvas did", _ {
		expect(font_for_text_height(5).id).to_equal(FontId::Px5);
		expect(font_for_text_height(6).id).to_equal(FontId::Px5);
		expect(font_for_text_height(7).id).to_equal(FontId::Apple7);
		expect(font_for_text_height(8).render_height).to_equal(7);
		expect(font_for_text_height(9).y_adjust).to_equal(1);
		expect(font_for_text_height(10).id).to_equal(FontId::MetricBold9);
		expect(font_for_text_height(13).id).to_equal(FontId::MetricBold13);
		expect(font_for_text_height(20).id).to_equal(FontId::MetricBold20);
		expect(font_for_text_height(99).id).to_equal(FontId::MetricBold20);
		expect(font_for_text_height(8).monospaced).to_be_true();
		expect(font_for_text_height(13).monospaced).to_be_false();
	});

	it("indexes glyphs with lowercase folded onto uppercase", _ {
		expect(glyph_index('A')).to_equal(glyph_index('a'));
		expect(glyph_index(' ')).to_equal(0);
		expect(glyph_index('A')).to_equal('A' - 0x20);
	});
});

CPPSPEC_SPEC(font)
```

- [ ] **Step 2: Run it to see it fail**

Run: `cmake --build build/tests --config Debug 2>&1 | tail -3`
Expected: `gfx/oled/font.h` not found.

- [ ] **Step 3: Move the data and write the font layer**

```bash
mkdir -p src/gfx/oled/fonts
git mv src/deluge/gui/fonts/fonts.c src/gfx/oled/fonts/fonts.c
git mv src/deluge/gui/fonts/fonts.h src/gfx/oled/fonts/fonts.h
```

Fix the include inside `src/gfx/oled/fonts/fonts.c` to `#include "fonts.h"`.

`src/gfx/oled/font.h`:

```cpp
#pragma once
#include "gfx/oled/fonts/fonts.h"
#include <cstdint>

namespace deluge::gfx::oled {

// Text metric constants (were in definitions_cxx.hpp).
constexpr int32_t kTextSpacingX = 6;    // 5px glyph + 1px gap
constexpr int32_t kTextSpacingY = 9;    // 7px glyph + 1px above and below
constexpr int32_t kTextSizeYUpdated = 7;
constexpr int32_t kTextTitleSpacingX = 9;
constexpr int32_t kTextTitleSizeY = 10;
constexpr int32_t kTextBigSizeY = 13;
constexpr int32_t kTextHugeSpacingX = 18;
constexpr int32_t kTextHugeSizeY = 20;

enum class FontId : uint8_t { Px5, Apple7, MetricBold9, MetricBold13, MetricBold20 };

struct Font {
	FontId id;
	const uint8_t* bitmap;
	const lv_font_glyph_dsc_t* glyphs;
	uint8_t native_height; // rows in the bitmap
	uint8_t render_height; // rows drawn (the old textHeight after normalisation)
	int8_t y_adjust;       // added to pixelY before drawing (textHeight 9 → +1)
	bool monospaced;      // Apple ][: every glyph advances kTextSpacingX
};

/// Select the font a text height selects. Same table as the original Canvas::drawChar.
const Font& font_for_text_height(int32_t textHeight);

/// Glyph table index for a character; <= 0 means "not drawable". Lowercase folds to
/// uppercase because lowercase glyphs were snipped from the tables.
int32_t glyph_index(uint8_t c);

} // namespace deluge::gfx::oled
```

`src/gfx/oled/font.cpp`:

```cpp
#include "gfx/oled/font.h"

namespace deluge::gfx::oled {

namespace {
constexpr Font kPx5{FontId::Px5, font_5px, font_5px_desc, 5, 5, 0, false};
constexpr Font kApple7{FontId::Apple7, font_apple, font_apple_desc, 8, 7, 0, true};
constexpr Font kApple7Up{FontId::Apple7, font_apple, font_apple_desc, 8, 7, 1, true};
constexpr Font kMetric9{FontId::MetricBold9, font_metric_bold_9px, font_metric_bold_9px_desc, 9, 9, 0, false};
constexpr Font kMetric13{FontId::MetricBold13, font_metric_bold_13px, font_metric_bold_13px_desc, 13, 13, 0, false};
constexpr Font kMetric20{FontId::MetricBold20, font_metric_bold_20px, font_metric_bold_20px_desc, 20, 20, 0, false};
} // namespace

const Font& font_for_text_height(int32_t textHeight) {
	switch (textHeight) {
	case 5:
	case 6:
		return kPx5;
	case 7:
	case 8:
		return kApple7;
	case 9:
		return kApple7Up;
	case 10:
		return kMetric9;
	case 13:
		return kMetric13;
	case 20:
	default:
		return kMetric20;
	}
}

int32_t glyph_index(uint8_t c) {
	// Copy the body of Canvas::getCharIndex (canvas.cpp) here verbatim, then delete it there.
	if (c >= 'a') {
		if (c <= 'z') {
			c -= 32;
		}
		else {
			c -= 26;
		}
	}
	return c - 0x20;
}

} // namespace deluge::gfx::oled
```

Check `Canvas::getCharIndex` in `canvas.cpp` (~line 530) and copy its exact body — the snippet above is the tail; keep any leading range checks it has.

Switch `deluge_gfx_oled` to STATIC in `src/gfx/CMakeLists.txt`:

```cmake
add_library(deluge_gfx_oled STATIC oled/font.cpp oled/fonts/fonts.c)
target_include_directories(deluge_gfx_oled PUBLIC ${SHARED_INCLUDE})
set_target_properties(deluge_gfx_oled PROPERTIES C_STANDARD 23 CXX_STANDARD 26 CXX_STANDARD_REQUIRED YES)
add_library(deluge_gfx_grid INTERFACE)
target_include_directories(deluge_gfx_grid INTERFACE ${SHARED_INCLUDE})
```

Façade `src/deluge/gui/fonts/fonts.h`:

```cpp
#pragma once
// FACADE (remove after UI phase 5): forwards to gfx/oled/fonts/fonts.h
#include "gfx/oled/fonts/fonts.h"
```

In `src/definitions_cxx.hpp`, replace the eight `kText*` constants (lines ~968-983) with:

```cpp
#include "gfx/oled/font.h"
using deluge::gfx::oled::kTextBigSizeY;
using deluge::gfx::oled::kTextHugeSizeY;
using deluge::gfx::oled::kTextHugeSpacingX;
using deluge::gfx::oled::kTextSizeYUpdated;
using deluge::gfx::oled::kTextSpacingX;
using deluge::gfx::oled::kTextSpacingY;
using deluge::gfx::oled::kTextTitleSizeY;
using deluge::gfx::oled::kTextTitleSpacingX;
```

(put the `#include` with the other includes at the top of the file; the `using` declarations where the constants were).

In `canvas.cpp`, replace the font switch in `drawChar` (lines 464-510) with:

```cpp
	const auto& f = deluge::gfx::oled::fontForTextHeight(textHeight);
	textHeight = f.render_height;
	pixelY += f.y_adjust;
	lv_font_glyph_dsc_t const* descriptor = f.glyphs;
	uint8_t const* font = f.bitmap;
	int32_t fontNativeHeight = f.native_height;
```

and the descriptor switch in `getCharWidthInPixels` (lines 562-580) with `lv_font_glyph_dsc_t const* descriptor = deluge::gfx::oled::fontForTextHeight(textHeight).glyphs;`. Make `Canvas::getCharIndex` call `deluge::gfx::oled::glyphIndex`. Remove `deluge_SOURCES` glob hits: `src/deluge/CMakeLists.txt` globs `*.c` under `src/deluge`, and `fonts.c` has moved out, so nothing to do there; add `deluge_gfx_oled` to `target_link_libraries(deluge_app PUBLIC ...)` in `src/deluge/CMakeLists.txt`.

- [ ] **Step 4: Run all three checks**

Run: `cmake --build build/tests --config Debug && ctest --test-dir build/tests -C Debug -R "font_spec|canvas_golden_spec|UnitTests" --output-on-failure`
Expected: PASS — the goldens are unchanged because the table is the same.
Run: `./dbt build Debug`
Expected: builds.

- [ ] **Step 5: Commit**

```bash
./dbt format
git add -A src/gfx src/deluge/gui/fonts src/definitions_cxx.hpp src/deluge/hid/display/oled_canvas/canvas.cpp src/deluge/CMakeLists.txt tests/spec/gfx/font_spec.cpp
git commit -m "refactor(gfx): move the bitmap fonts and text metrics into gfx/oled"
```

---

### Task 7: `Icon` and the icon registry move to gfx

**Files:**
- Create: `src/gfx/oled/icon.h`, `src/gfx/oled/icons.h`, `src/gfx/oled/icons.cpp`
- Modify: `src/deluge/hid/display/oled_canvas/canvas.h` (Icon becomes an alias), `src/deluge/hid/display/oled.h:101-155`, `src/deluge/hid/display/oled_icons.cpp`, `src/gfx/CMakeLists.txt`
- Test: `canvas_golden_spec` unchanged; `./dbt build Debug`

**Interfaces:**
- Produces: `deluge::gfx::oled::Icon { const uint8_t* data; uint8_t width; uint8_t height; uint8_t num_bytes_tall; }`; `namespace deluge::gfx::oled::icons { extern const Icon <name>; ... }` for every icon that `oled_icons.cpp` defines with `DEFINE_ICON`.
- `deluge::hid::display::Icon` becomes `using Icon = deluge::gfx::oled::Icon;`. `OLED::<name>Icon` members that were `Icon` become `static inline const Icon& <name>Icon = gfx::oled::icons::<name>;`. Raw `const uint8_t <name>Icon[]` arrays (drawn via `drawGraphicMultiLine`) stay in `OLED` for now; they are graphics, not icons, and move with the toolkit.

- [ ] **Step 1: Inventory**

Run: `grep -n "DEFINE_ICON(" src/deluge/hid/display/oled_icons.cpp | wc -l` and `grep -n "static const Icon " src/deluge/hid/display/oled.h`. Every name in the second list must appear in `icons.h`.

- [ ] **Step 2: Create the gfx icon files**

`src/gfx/oled/icon.h`:

```cpp
#pragma once
#include <cstdint>

namespace deluge::gfx::oled {
/// 1-bpp column-major bitmap: `width` columns of `num_bytes_tall` bytes, bit 0 = top row.
struct Icon {
	const uint8_t* data;
	uint8_t width;
	uint8_t height;
	uint8_t num_bytes_tall;
};
} // namespace deluge::gfx::oled
```

`src/gfx/oled/icons.h` — one `extern const Icon <camelName>;` per `DEFINE_ICON` name, dropping the `Icon` suffix (`OLED::folderIcon` → `icons::folder`). Use exactly the names from Step 1; e.g.

```cpp
#pragma once
#include "gfx/oled/icon.h"

namespace deluge::gfx::oled::icons {
extern const Icon sine;
extern const Icon arpModeUp;
// ... every DEFINE_ICON name from oled_icons.cpp, in file order
} // namespace deluge::gfx::oled::icons
```

`src/gfx/oled/icons.cpp` — move the `DEFINE_ICON` macro and every `DEFINE_ICON(...)` block out of `oled_icons.cpp` (cut, don't retype), with the macro rewritten for the new namespace:

```cpp
#include "gfx/oled/icons.h"

#define DEFINE_ICON(name, num_bytes_tall, ...)                                                                           \
	static const uint8_t name##_data[] = {__VA_ARGS__};                                                                \
	const Icon name = {name##_data, static_cast<uint8_t>(sizeof(name##_data) / (num_bytes_tall)),                        \
	                   static_cast<uint8_t>((num_bytes_tall) * 8), num_bytes_tall}

namespace deluge::gfx::oled::icons {
// DEFINE_ICON(sine, 2, ...); etc — pasted from oled_icons.cpp with the `Icon` suffix dropped
} // namespace deluge::gfx::oled::icons
```

Add `oled/icons.cpp` to `deluge_gfx_oled`'s sources.

- [ ] **Step 3: Re-point the firmware**

The gfx `Icon` field is `num_bytes_tall` (new library, snake_case) where the legacy struct had `numBytesTall`; because the legacy `Icon` becomes an alias of the gfx one, update every legacy reader of `.numBytesTall` (`grep -rn "numBytesTall" src/deluge` — expect `Canvas::drawIcon` in `hid/display/oled_canvas/canvas.cpp` and the `DEFINE_ICON` macro) to `.num_bytes_tall` in this task.

In `canvas.h`, replace the `struct Icon {...}` from Task 3 with:

```cpp
#include "gfx/oled/icon.h"
namespace deluge::hid::display {
using Icon = deluge::gfx::oled::Icon;
}
```

In `oled.h`, add `#include "gfx/oled/icons.h"` and replace each `static const Icon <name>Icon;` with `static inline const Icon& <name>Icon = deluge::gfx::oled::icons::<name>;`. Delete the moved `DEFINE_ICON` blocks (and the macro) from `oled_icons.cpp`; leave the raw `uint8_t OLED::xxxIcon[]` arrays.

- [ ] **Step 4: Build and test**

Run: `./dbt build Debug` — Expected: builds (every `OLED::xxxIcon` use resolves through the reference).
Run: `cmake --build build/tests --config Debug && ctest --test-dir build/tests -C Debug -R canvas_golden_spec --output-on-failure` — Expected: PASS.

- [ ] **Step 5: Commit**

```bash
./dbt format
git add -A src/gfx src/deluge/hid/display
git commit -m "refactor(gfx): move Icon and the icon bitmaps into gfx/oled"
```

---

### Task 8: The gfx `Canvas` with runtime geometry

This is the core extraction. `canvas.cpp` is copied to `src/gfx/oled/canvas.cpp` and transformed; the firmware keeps using the old class until Task 9 swaps it.

**Files:**
- Create: `src/gfx/oled/canvas.h`, `src/gfx/oled/canvas.cpp` (from a copy of `src/deluge/hid/display/oled_canvas/canvas.{h,cpp}`)
- Modify: `src/gfx/CMakeLists.txt`
- Test: `tests/spec/gfx/canvas_spec.cpp` (new, geometry-aware), plus a **parity** check inside it against the old class

**Interfaces:**
- Produces (`deluge::gfx::oled`):
  ```cpp
  enum BorderRadius : uint8_t { SMALL = 0, BIG = 1 };
  struct DrawLineOptions { bool thick{false}; std::optional<uint8_t> min_x, max_x;
                           std::optional<std::function<void(Point)>> point_callback; };  // verbatim
  class Canvas {                       // non-owning view over caller storage
  public:
      Canvas(std::span<uint8_t> storage, Geometry g);   // storage.size() >= g.bytes()
      Geometry geometry() const; std::span<uint8_t> bytes(); std::span<const uint8_t> bytes() const;
      uint8_t* page(uint32_t p);                        // &bytes()[p * width]
      // every drawing method of the old Canvas, same parameter order, renamed to snake_case
      // (draw_pixel, clear_pixel, invert_pixel, draw_horizontal_line, draw_vertical_line, draw_line,
      //  draw_rectangle, draw_rectangle_rounded, draw_circle, draw_string, draw_string_centred,
      //  draw_string_centered, draw_string_centred_shrink_if_necessary, draw_string_align_right, draw_char,
      //  get_char_width_in_pixels, get_char_spacing_in_pixels, get_string_width_in_pixels,
      //  draw_graphic_multi_line, draw_icon, draw_icon_centered, draw_screen_title, clear_area_exact,
      //  invert_area, invert_area_rounded), with:
      //   - default endX / rightPos = kEdge, default centrePos = kCentre
      //   - draw_rectangle_rounded(..., BorderRadius radius = SMALL, bool rounded = true)
      //   - invert_area_rounded(..., BorderRadius radius = SMALL, bool rounded = true)
      //   - NO invertLeftEdgeForMenuHighlighting (policy; stays in the façade)
      //   - NO etl::istring overload (façade)
      //   - NO hackGetImageStore / roundedCornersEnabled / menuHighlightingInverts
  };
  template <Geometry G, size_t Align = 32>
  class OwnedCanvas : public Canvas {   // owns G.bytes() of aligned storage
  public:
      OwnedCanvas() : Canvas(store_, G) {}
  private:
      alignas(Align) uint8_t store_[G.bytes()];
  };
  ```
  `kFramebufferAlignment = 32` is a gfx constant (was `CACHE_LINE_SIZE`).

- [ ] **Step 1: Write the failing spec**

`tests/spec/gfx/canvas_spec.cpp`:

```cpp
#include "gfx/oled/canvas.h"
#include "hid/display/oled_canvas/canvas.h" // the old class, for parity
#include "support/pbm.h"
#include "cppspec.hpp"
#include <cstring>

namespace g = deluge::gfx::oled;
using OldCanvas = deluge::hid::display::oled_canvas::Canvas;

// clang-format off
describe canvas("gfx::oled::Canvas", $ {
	it("draws into caller storage of the right size", _ {
		uint8_t store[768] = {};
		g::Canvas c(store, g::kDelugeOled);
		c.draw_pixel(3, 9);
		expect(store[1 * 128 + 3]).to_equal(uint8_t(1 << 1));
		expect(c.bytes().size()).to_equal(size_t(768));
	});

	it("respects a 128x64 geometry with no hidden rows", _ {
		g::OwnedCanvas<g::Geometry{128, 64, 0}> c;
		c.clear();
		c.draw_horizontal_line(63, 0, 127);        // last row exists on 64-tall
		expect(c.bytes()[7 * 128 + 5]).to_equal(uint8_t(0x80));
		c.draw_string("EDGE", 100, 50, 6, 7);      // clipped, no write past the buffer
		expect(c.bytes().size()).to_equal(size_t(1024));
	});

	it("resolves kEdge and kCentre against the geometry", _ {
		g::OwnedCanvas<g::Geometry{64, 16, 0}> narrow;
		narrow.clear();
		narrow.draw_string_align_right("R", 4, 6, 7); // right_pos defaults to kEdge == 64
		bool anyInLastCols = false;
		for (int x = 56; x < 64; x++) anyInLastCols |= (narrow.bytes()[x] | narrow.bytes()[64 + x]) != 0;
		expect(anyInLastCols).to_be_true();
	});

	it("is byte-identical to the legacy canvas on the Deluge geometry", _ {
		OldCanvas old; old.clear();
		g::OwnedCanvas<g::kDelugeOled> neu; neu.clear();
		old.drawRectangle(2, 12, 30, 30);            neu.draw_rectangle(2, 12, 30, 30);
		old.drawCircle(100, 20, 7, false);           neu.draw_circle(100, 20, 7, false);
		old.drawString("Ag13", 40, 16, 9, 13);       neu.draw_string("Ag13", 40, 16, 9, 13);
		old.drawStringCentred("CENTRE", 6, 6, 7);    neu.draw_string_centred("CENTRE", 6, 6, 7);
		old.drawLine(0, 6, 127, 47, {.thick = true}); neu.draw_line(0, 6, 127, 47, {.thick = true});
		old.drawRectangleRounded(50, 6, 90, 20, deluge::hid::display::oled_canvas::BIG);
		neu.draw_rectangle_rounded(50, 6, 90, 20, g::BIG, true);
		expect(std::memcmp(&old.hackGetImageStore()[0][0], neu.bytes().data(), 768)).to_equal(0);
	});
});

CPPSPEC_SPEC(canvas)
```

(The parity example calls the legacy and the gfx canvas side by side because their method names differ; `{.thick = true}` works for both `DrawLineOptions` types.)

- [ ] **Step 2: Run it to see it fail**

Run: `cmake --build build/tests --config Debug 2>&1 | tail -3`
Expected: `gfx/oled/canvas.h` not found.

- [ ] **Step 3: Copy and transform**

```bash
cp src/deluge/hid/display/oled_canvas/canvas.h src/gfx/oled/canvas.h
cp src/deluge/hid/display/oled_canvas/canvas.cpp src/gfx/oled/canvas.cpp
```

Apply these transformations to **both** new files (every occurrence):

| Before | After |
|---|---|
| `namespace deluge::hid::display {` … `namespace oled_canvas {` | `namespace deluge::gfx::oled {` (single namespace; remove the inner one and `class OLED;`) |
| `#include "definitions.h"`, `"deluge/util/c_string.h"`, `"util/etl_string.h"`, `"definitions_cxx.hpp"`, `"gui/fonts/fonts.h"`, `<vector>` | `#include "gfx/oled/geometry.h"`, `"gfx/oled/icon.h"`, `"gfx/oled/font.h"`, `<span>` |
| `OLED_MAIN_WIDTH_PIXELS` | `geometry_.width` (in `.cpp`); in `.h` defaults: `= kEdge` |
| `OLED_MAIN_WIDTH_PIXELS / 2` (default `centrePos`) | `= kCentre` |
| `OLED_MAIN_HEIGHT_PIXELS >> 3` / `kImageHeight` | `geometry_.pages()` |
| `OLED_MAIN_HEIGHT_PIXELS` | `geometry_.height` |
| `OLED_MAIN_TOPMOST_PIXEL` | `geometry_.visible_top` |
| `image_[row][x]` (2-D indexing) | `bytes_[row * geometry_.width + x]` |
| `&image_[rowOnDisplay][startX]` | `&bytes_[rowOnDisplay * geometry_.width + startX]` |
| `memset(image_, 0, sizeof(image_))` | `std::fill(bytes_.begin(), bytes_.end(), 0)` |
| `kTextSpacingX` etc. | unqualified is fine (same namespace as `font.h`) |
| `getCharIndex(` | `glyph_index(` (delete the member) |
| every method name | snake_case per the list in the Interfaces block (`drawPixel` → `draw_pixel`, `drawStringCentredShrinkIfNecessary` → `draw_string_centred_shrink_if_necessary`, …); parameters too (`pixelX` → `pixel_x`, `textHeight` → `text_height`, `endX` → `end_x`, `rightPos` → `right_pos`, `centrePos` → `centre_pos`, `numBytesTall` → `num_bytes_tall`) |
| `if (roundedCornersEnabled)` | `if (rounded)` — add `bool rounded = true` as the last parameter of `drawRectangleRounded` and `invertAreaRounded` |
| the `drawStringCentered(etl::istring& ...)` overload | delete |
| `invertLeftEdgeForMenuHighlighting` | delete |
| `roundedCornersEnabled`, `menuHighlightingInverts`, `kImageWidth`, `ImageStore`, `hackGetImageStore` | delete |
| `Canvas() = default;` and the private `image_` | replace per the block below |
| `std::vector<uint8_t> reversedGraphic(width * numBytesTall);` | fixed stack buffer, see below |

At the top of each method that uses `endX`, `rightPos` or `centrePos`, resolve the sentinels once:

```cpp
	if (end_x == kEdge) end_x = geometry_.width;
	if (right_pos == kEdge) right_pos = geometry_.width;
	if (centre_pos == kCentre) centre_pos = geometry_.width / 2;
```

Class storage block (replaces the constructor and `image_`):

```cpp
public:
	Canvas(std::span<uint8_t> storage, Geometry g) : bytes_(storage.first(g.bytes())), geometry_(g) {}
	Geometry geometry() const { return geometry_; }
	std::span<uint8_t> bytes() { return bytes_; }
	std::span<const uint8_t> bytes() const { return bytes_; }
	uint8_t* page(uint32_t p) { return &bytes_[p * geometry_.width]; }
private:
	std::span<uint8_t> bytes_;
	Geometry geometry_;
```

And after the class:

```cpp
constexpr size_t kFramebufferAlignment = 32;

template <Geometry G, size_t Align = kFramebufferAlignment>
class OwnedCanvas : public Canvas {
public:
	OwnedCanvas() : Canvas(store_, G) {}
	OwnedCanvas(const OwnedCanvas&) = delete;
	OwnedCanvas& operator=(const OwnedCanvas&) = delete;
private:
	alignas(Align) uint8_t store_[G.bytes()]{};
};
```

Reversed graphics without the heap (`drawGraphicMultiLine`):

```cpp
	if (reversed) {
		constexpr size_t kMaxReversed = 512;
		uint8_t reversed_graphic[kMaxReversed];
		const size_t n = static_cast<size_t>(width) * num_bytes_tall;
		if (n > kMaxReversed) {
			return; // larger reversed graphics do not exist; drawing nothing beats overflowing
		}
		for (int32_t col = 0; col < width; ++col) {
			/* same loop body as before */
		}
		return draw_graphic_multi_line(reversed_graphic, start_x, start_y, width, height, num_bytes_tall);
	}
```

Every clip that compared against `OLED_MAIN_WIDTH_PIXELS - startX` or `(OLED_MAIN_HEIGHT_PIXELS >> 3)` now uses `geometry_` — double-check `draw_graphic_multi_line`, `draw_char`, `draw_string` (the `scroll_pos`/`end_x` path), `clear_area_exact`, `invert_area*`, `draw_circle`, `draw_line`.

Add `oled/canvas.cpp` to `deluge_gfx_oled` in `src/gfx/CMakeLists.txt`.

- [ ] **Step 4: Run the spec**

Run: `cmake --build build/tests --config Debug && ctest --test-dir build/tests -C Debug -R "canvas_spec|canvas_golden_spec" --output-on-failure`
Expected: PASS. If the parity example fails, diff the two buffers with `page_major_to_pbm` on each and fix the transformation — never adjust the expectation.

- [ ] **Step 5: Commit**

```bash
./dbt format
git add src/gfx tests/spec/gfx/canvas_spec.cpp
git commit -m "feat(gfx): OLED Canvas over caller storage with runtime geometry"
```

---

### Task 9: The firmware canvas becomes a façade over gfx

**Files:**
- Modify: `src/deluge/hid/display/oled_canvas/canvas.h` (rewrite), `src/deluge/hid/display/oled_canvas/canvas.cpp` (shrinks to the policy methods)
- Modify: `src/deluge/hid/display/oled.cpp` (`hackGetImageStore` sites keep working; `oledCurrentImage` type unchanged)
- Test: `canvas_golden_spec`, `canvas_spec`, `./dbt build Debug`, sim `deluge_host`

**Interfaces:**
- Produces: `deluge::hid::display::oled_canvas::Canvas : public deluge::gfx::oled::OwnedCanvas<deluge::gfx::oled::kDelugeOled>` exposing the **legacy camelCase method names as inline forwarders** to the snake_case base (`drawPixel` → `draw_pixel`, …, one line each, `[[deprecated]]`-free for now), plus `roundedCornersEnabled`, `menuHighlightingInverts`, `invertLeftEdgeForMenuHighlighting`, the `etl::istring` overload, `kImageHeight`, `kImageWidth`, `ImageStore`, `hackGetImageStore()`; `BorderRadius`, `Point`, `DrawLineOptions`, `SMALL`, `BIG` re-exported with `using`. New code must call the gfx names; the forwarders exist only so the ~140 legacy call sites compile unchanged and are deleted in phase 5.

- [ ] **Step 1: Rewrite the façade header**

`src/deluge/hid/display/oled_canvas/canvas.h`:

```cpp
#pragma once
// FACADE (remove after UI phase 5): the firmware's OLED canvas is gfx/oled/canvas.h plus
// three pieces of policy (rounded-corner and menu-highlighting settings, an etl overload)
// and the 2-D image-store accessor that hid/display/oled.cpp still composites with.
#include "gfx/oled/canvas.h"
#include "gfx/oled/icon.h"
#include "util/etl_string.h"
#include <string_view>

namespace deluge::hid::display {
using Icon = deluge::gfx::oled::Icon;

namespace oled_canvas {
using deluge::gfx::oled::BIG;
using deluge::gfx::oled::BorderRadius;
using deluge::gfx::oled::DrawLineOptions;
using deluge::gfx::oled::Point;
using deluge::gfx::oled::SMALL;

class Canvas : public deluge::gfx::oled::OwnedCanvas<deluge::gfx::oled::kDelugeOled> {
	using Base = deluge::gfx::oled::OwnedCanvas<deluge::gfx::oled::kDelugeOled>;

public:
	Canvas() = default;

	/// Whether the *Rounded primitives round their corners. Kept in sync with the
	/// RoundedCorners community feature by the settings/menu layer.
	static inline bool roundedCornersEnabled = true;
	/// Mirrors FlashStorage::accessibilityMenuHighlighting (true = invert, false = edge line).
	static inline bool menuHighlightingInverts = true;

	// Legacy names → gfx names. One forwarder per Canvas method; the full list is the
	// snake_case list in Task 8's Interfaces block. Representative entries:
	void drawPixel(int32_t x, int32_t y) { draw_pixel(x, y); }
	void clearPixel(int32_t x, int32_t y) { clear_pixel(x, y); }
	void drawHorizontalLine(int32_t y, int32_t x0, int32_t x1) { draw_horizontal_line(y, x0, x1); }
	void drawString(std::string_view s, int32_t x, int32_t y, int32_t w, int32_t h, int32_t scroll = 0,
	                int32_t endX = deluge::gfx::oled::kEdge, bool useTextWidth = false) {
		draw_string(s, x, y, w, h, scroll, endX, useTextWidth);
	}
	// ... (drawVerticalLine, drawLine, drawRectangle, drawCircle, drawStringCentred, drawStringCentered,
	//      drawStringCentredShrinkIfNecessary, drawStringAlignRight, drawChar, getCharWidthInPixels,
	//      getCharSpacingInPixels, getStringWidthInPixels, drawGraphicMultiLine, drawIcon, drawIconCentered,
	//      drawScreenTitle, clearAreaExact, invertArea — same pattern, same defaults as before)
	void drawRectangleRounded(int32_t minX, int32_t minY, int32_t maxX, int32_t maxY, BorderRadius radius = SMALL) {
		draw_rectangle_rounded(minX, minY, maxX, maxY, radius, roundedCornersEnabled);
	}
	void invertAreaRounded(int32_t xMin, int32_t width, int32_t startY, int32_t endY, BorderRadius radius = SMALL) {
		invert_area_rounded(xMin, width, startY, endY, radius, roundedCornersEnabled);
	}
	void invertLeftEdgeForMenuHighlighting(int32_t xMin, int32_t width, int32_t startY, int32_t endY);

	void drawStringCentered(etl::istring& stringBuf, int32_t startX, int32_t startY, int32_t textSpacingX,
	                        int32_t textSpacingY, int32_t totalWidth) {
		draw_string_centered(stringBuf.c_str(), startX, startY, textSpacingX, textSpacingY, totalWidth);
	}

	static constexpr uint32_t kImageHeight = deluge::gfx::oled::kDelugeOled.pages();
	static constexpr uint32_t kImageWidth = deluge::gfx::oled::kDelugeOled.width;
	using ImageStore = uint8_t[kImageHeight][kImageWidth];
	/// XXX: only hid/display/oled.cpp may use this; it disappears with the shell (phase 5).
	ImageStore& hackGetImageStore() { return *reinterpret_cast<ImageStore*>(bytes().data()); }
};

} // namespace oled_canvas
} // namespace deluge::hid::display
```

`src/deluge/hid/display/oled_canvas/canvas.cpp` shrinks to:

```cpp
#include "canvas.h"

namespace deluge::hid::display::oled_canvas {

void Canvas::invertLeftEdgeForMenuHighlighting(int32_t xMin, int32_t width, int32_t startY, int32_t endY) {
	if (!menuHighlightingInverts) {
		draw_vertical_line(xMin, startY, endY);
	}
	else {
		invertAreaRounded(xMin, width, startY, endY);
	}
}

} // namespace deluge::hid::display::oled_canvas
```

- [ ] **Step 2: Fix the fallout**

Run: `./dbt build Debug 2>&1 | grep -E "error" | head -20`. Expected fallout and fixes:
- Callers that named `deluge::hid::display::oled_canvas::Point` or `DrawLineOptions` compile via the `using`s.
- `oled.cpp` uses `Canvas::kImageHeight` and `hackGetImageStore()` — provided.
- Anything that used `OLED_MAIN_WIDTH_PIXELS` as an *argument* still works (it equals `kDelugeOled.width`).
- If a caller passed `OLED_MAIN_WIDTH_PIXELS / 2` explicitly for `centrePos`, it still works (64 ≠ `kCentre`).

- [ ] **Step 3: Verify byte-identity and both builds**

Run: `cmake --build build/tests --config Debug && ctest --test-dir build/tests -C Debug --output-on-failure`
Expected: all specs and `UnitTests` PASS — `canvas_golden_spec` now exercises the façade over gfx, so this is the acceptance test for the extraction.
Run: `./dbt build Debug` — Expected: builds.
Run: `cmake --build build-sim --target deluge_host` — Expected: builds (add `deluge_gfx_oled deluge_gfx_grid` to `target_link_libraries(deluge_host ...)` in `sim/CMakeLists.txt` if the link fails on `deluge::gfx::oled::` symbols).
Optional on-device check: flash the Debug build and confirm the boot screen, a menu, and a popup look unchanged.

- [ ] **Step 4: Commit**

```bash
./dbt format
git add -A src/deluge/hid/display src/gfx sim/CMakeLists.txt
git commit -m "refactor(oled): make the firmware canvas a facade over gfx::oled::Canvas"
```

---

### Task 10: `RGB` and the palette move to gfx/grid

**Files:**
- Move: `src/deluge/gui/colour/rgb.h` → `src/gfx/grid/rgb.h`, `rgb.cpp` → `src/gfx/grid/rgb.cpp`, `palette.h` → `src/gfx/grid/palette.h` (`git mv`)
- Create façades: `src/deluge/gui/colour/rgb.h`, `palette.h` (keep `colour.h` as is — it includes the two façades)
- Modify: `src/gfx/CMakeLists.txt` (grid becomes STATIC), `src/gfx/grid/rgb.h:2-3` (drop `util/const_functions.h`, `util/fixedpoint.h` — see step 2), `src/deluge/CMakeLists.txt` (link `deluge_gfx_grid`), `tests/unit/CMakeLists.txt` (remove `gui/colour/rgb.cpp` from the glob if listed)
- Test: `UnitTests` (`rgb_tests.cpp` unchanged), `./dbt build Debug`

**Interfaces:**
- Produces: `RGB` and the palette at their new paths, **still in the global namespace** (moving them into `deluge::gfx::grid` touches ~600 call sites and is not part of this phase — record it in the README's TODO list).

- [ ] **Step 1: Move the files**

```bash
git mv src/deluge/gui/colour/rgb.h src/gfx/grid/rgb.h
git mv src/deluge/gui/colour/rgb.cpp src/gfx/grid/rgb.cpp
git mv src/deluge/gui/colour/palette.h src/gfx/grid/palette.h
```

- [ ] **Step 2: Remove the two `util/` includes**

`rgb.h` includes `util/const_functions.h` and `util/fixedpoint.h`. Run `grep -n "const_functions\|fixedpoint\|q31\|ONE_Q" src/gfx/grid/rgb.h src/gfx/grid/rgb.cpp` to see what is used. For each used helper, copy its definition (they are tiny `constexpr` functions) into an anonymous namespace in `rgb.h` under a `// from util/const_functions.h` comment, so gfx has no `util/` dependency. If nothing is used, just delete the includes.

- [ ] **Step 3: Façades and CMake**

`src/deluge/gui/colour/rgb.h`:

```cpp
#pragma once
// FACADE (remove after UI phase 5): forwards to gfx/grid/rgb.h
#include "gfx/grid/rgb.h" // IWYU pragma: export
```

`src/deluge/gui/colour/palette.h`:

```cpp
#pragma once
// FACADE (remove after UI phase 5): forwards to gfx/grid/palette.h
#include "gfx/grid/palette.h" // IWYU pragma: export
```

`src/gfx/CMakeLists.txt`: make `deluge_gfx_grid` STATIC with `grid/rgb.cpp`, same properties as the oled target. `src/deluge/CMakeLists.txt`: add `deluge_gfx_grid` next to `deluge_gfx_oled` in `target_link_libraries(deluge_app PUBLIC ...)`. `tests/unit/CMakeLists.txt`: `deluge_SOURCES` lists explicit files; if `../../src/deluge/gui/colour/rgb.cpp` appears, delete that line (the library provides it now).

- [ ] **Step 4: Test and build**

Run: `cmake --build build/tests --config Debug && ctest --test-dir build/tests -C Debug -R UnitTests --output-on-failure` — Expected: PASS including `RGBTests`.
Run: `./dbt build Debug` — Expected: builds.

- [ ] **Step 5: Commit**

```bash
./dbt format
git add -A src/gfx src/deluge/gui/colour src/deluge/CMakeLists.txt tests/unit/CMakeLists.txt
git commit -m "refactor(gfx): move RGB and the palette into gfx/grid"
```

---

### Task 11: `PadGrid`

**Files:**
- Create: `src/gfx/grid/pad_grid.h`, `src/gfx/grid/pad_grid.cpp`, `tests/spec/gfx/support/grid_state.h`
- Modify: `src/gfx/CMakeLists.txt`
- Test: `tests/spec/gfx/pad_grid_spec.cpp`, golden `tests/golden/gfx/grid/pad_grid_basic.state`

**Interfaces:**
- Produces (`deluge::gfx::grid`):
  ```cpp
  struct GridDimensions { uint8_t main_cols; uint8_t sidebar_cols; uint8_t rows;
                          constexpr uint8_t cols() const { return main_cols + sidebar_cols; } };
  constexpr GridDimensions kDelugeGrid{16, 2, 8};
  constexpr uint8_t kMaxGridCols = 32, kMaxGridRows = 16;
  class PadGrid {
  public:
      explicit PadGrid(GridDimensions d);
      GridDimensions dims() const;
      RGB& at(uint8_t x, uint8_t y);  const RGB& at(uint8_t x, uint8_t y) const;   // row-major [y][x]
      void set(uint8_t x, uint8_t y, RGB c);                      // marks row y dirty; ignores out-of-range
      void clear();  void clear_row(uint8_t y);
      void fill_rect(oled-free Rect-like: uint8_t x, uint8_t y, uint8_t w, uint8_t h, RGB c);
      uint32_t dirty_rows() const;  void mark_row_dirty(uint8_t y);  void mark_all_dirty();  void clear_dirty();
      RGB* row(uint8_t y);                                          // &cells_[y][0], cols() valid entries
  };
  ```
  Storage is `RGB cells_[kMaxGridRows][kMaxGridCols]` row-major so that a `kDelugeGrid` grid's first 8 rows × 18 columns have the **same layout as `PadLEDs::image[8][18]`** when `kMaxGridCols == 18`… it does not (32 ≠ 18). Therefore `row(y)` is the aliasing seam later phases use (copy per row), not the whole array. Record this in the header comment.
- `tests/spec/gfx/support/grid_state.h`: `std::string grid_to_state(const PadGrid&)` — one line per non-black pad: `pad <x> <y> <r> <g> <b>\n`, sorted by y then x (same format as deluge-sdk's headless `.state`).

- [ ] **Step 1: Write the failing spec**

`tests/spec/gfx/pad_grid_spec.cpp`:

```cpp
#include "gfx/grid/pad_grid.h"
#include "support/golden.h"
#include "support/grid_state.h"
#include "cppspec.hpp"

using namespace deluge::gfx::grid;

// clang-format off
describe pad_grid("gfx::grid::PadGrid", $ {
	it("sizes itself from the dimensions and starts black and clean", _ {
		PadGrid g(kDelugeGrid);
		expect(g.dims().cols()).to_equal(uint8_t(18));
		expect(g.dirty_rows()).to_equal(0u);
		expect(g.at(17, 7)).to_equal(RGB{0, 0, 0});
	});

	it("tracks dirty rows on set and ignores out-of-range writes", _ {
		PadGrid g(kDelugeGrid);
		g.set(3, 5, RGB{255, 0, 0});
		expect(g.dirty_rows()).to_equal(1u << 5);
		g.set(18, 0, RGB{1, 1, 1}); // one past the sidebar
		g.set(0, 8, RGB{1, 1, 1});  // one past the last row
		expect(g.dirty_rows()).to_equal(1u << 5);
		g.clear_dirty();
		expect(g.dirty_rows()).to_equal(0u);
	});

	it("fills rectangles clipped to the grid", _ {
		PadGrid g(GridDimensions{4, 1, 3});
		g.fill_rect(3, 1, 10, 10, RGB{0, 0, 9});
		expect(g.at(3, 1)).to_equal(RGB{0, 0, 9});
		expect(g.at(4, 2)).to_equal(RGB{0, 0, 9});
		expect(g.at(2, 1)).to_equal(RGB{0, 0, 0});
		expect(g.dirty_rows()).to_equal((1u << 1) | (1u << 2));
	});

	it("dumps a golden state", _ {
		PadGrid g(kDelugeGrid);
		g.set(0, 0, RGB{255, 0, 0});
		g.set(17, 7, RGB{0, 255, 0});
		g.fill_rect(4, 3, 2, 2, RGB{10, 20, 30});
		expect_golden(self, "grid/pad_grid_basic.state", grid_to_state(g));
	});
});

CPPSPEC_SPEC(pad_grid)
```

`RGB` needs `operator==`; check `rgb.h` — if absent add `constexpr bool operator==(const RGB&) const = default;`.

- [ ] **Step 2: Run it to see it fail**

Run: `cmake --build build/tests --config Debug 2>&1 | tail -3` — Expected: `gfx/grid/pad_grid.h` not found.

- [ ] **Step 3: Implement**

`src/gfx/grid/pad_grid.h`:

```cpp
#pragma once
#include "gfx/grid/rgb.h"
#include <cstdint>

namespace deluge::gfx::grid {

struct GridDimensions {
	uint8_t main_cols;
	uint8_t sidebar_cols;
	uint8_t rows;
	constexpr uint8_t cols() const { return main_cols + sidebar_cols; }
};
constexpr GridDimensions kDelugeGrid{16, 2, 8};

constexpr uint8_t kMaxGridCols = 32;
constexpr uint8_t kMaxGridRows = 16;

/// RGB pad framebuffer with runtime dimensions and a per-row dirty mask (the protocol the
/// existing renderMainPads(whichRows) already uses). Storage is row-major [y][x] with a
/// compile-time capacity; `row(y)` is the seam for copying to/from legacy PadLEDs::image.
class PadGrid {
public:
	explicit PadGrid(GridDimensions d) : dims_(d) { clear(); }

	GridDimensions dims() const { return dims_; }
	bool in_range(uint8_t x, uint8_t y) const { return x < dims_.cols() && y < dims_.rows; }

	RGB& at(uint8_t x, uint8_t y) { return cells_[y][x]; }
	const RGB& at(uint8_t x, uint8_t y) const { return cells_[y][x]; }
	RGB* row(uint8_t y) { return cells_[y]; }
	const RGB* row(uint8_t y) const { return cells_[y]; }

	void set(uint8_t x, uint8_t y, RGB c);
	void clear();
	void clear_row(uint8_t y);
	void fill_rect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, RGB c);

	uint32_t dirty_rows() const { return dirty_; }
	void mark_row_dirty(uint8_t y) { if (y < dims_.rows) dirty_ |= 1u << y; }
	void mark_all_dirty() { dirty_ = (1u << dims_.rows) - 1; }
	void clear_dirty() { dirty_ = 0; }

private:
	GridDimensions dims_;
	uint32_t dirty_ = 0;
	RGB cells_[kMaxGridRows][kMaxGridCols];
};

} // namespace deluge::gfx::grid
```

`src/gfx/grid/pad_grid.cpp`:

```cpp
#include "gfx/grid/pad_grid.h"

namespace deluge::gfx::grid {

void PadGrid::set(uint8_t x, uint8_t y, RGB c) {
	if (!in_range(x, y)) {
		return;
	}
	cells_[y][x] = c;
	dirty_ |= 1u << y;
}

void PadGrid::clear() {
	for (uint8_t y = 0; y < kMaxGridRows; y++) {
		for (uint8_t x = 0; x < kMaxGridCols; x++) {
			cells_[y][x] = RGB{0, 0, 0};
		}
	}
	mark_all_dirty();
}

void PadGrid::clearRow(uint8_t y) {
	if (y >= dims_.rows) {
		return;
	}
	for (uint8_t x = 0; x < dims_.cols(); x++) {
		cells_[y][x] = RGB{0, 0, 0};
	}
	dirty_ |= 1u << y;
}

void PadGrid::fillRect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, RGB c) {
	const uint8_t x1 = (x + w < dims_.cols()) ? x + w : dims_.cols();
	const uint8_t y1 = (y + h < dims_.rows) ? y + h : dims_.rows;
	for (uint8_t yy = y; yy < y1; yy++) {
		for (uint8_t xx = x; xx < x1; xx++) {
			cells_[yy][xx] = c;
		}
		dirty_ |= 1u << yy;
	}
}

} // namespace deluge::gfx::grid
```

`tests/spec/gfx/support/grid_state.h`:

```cpp
#pragma once
#include "gfx/grid/pad_grid.h"
#include <string>

/// deluge-sdk headless `.state` format: one `pad x y r g b` line per non-black pad.
inline std::string grid_to_state(const deluge::gfx::grid::PadGrid& g) {
	std::string out;
	for (uint8_t y = 0; y < g.dims().rows; y++) {
		for (uint8_t x = 0; x < g.dims().cols(); x++) {
			const RGB& c = g.at(x, y);
			if (c.r | c.g | c.b) {
				out += "pad " + std::to_string(x) + " " + std::to_string(y) + " " + std::to_string(c.r) + " "
				       + std::to_string(c.g) + " " + std::to_string(c.b) + "\n";
			}
		}
	}
	return out;
}
```

Add `grid/pad_grid.cpp` to `deluge_gfx_grid`. Note the test's `fill_rect(3,1,10,10)` on a 5×3 grid relies on `x + w` not overflowing `uint8_t`; compute `x1`/`y1` in `int32_t` if the compiler warns.

- [ ] **Step 4: Record the golden and run**

```bash
cmake --build build/tests --config Debug
GFX_GOLDEN_UPDATE=1 ctest --test-dir build/tests -C Debug -R pad_grid_spec --output-on-failure
ctest --test-dir build/tests -C Debug -R pad_grid_spec --output-on-failure
cat tests/golden/gfx/grid/pad_grid_basic.state
```
Expected: PASS; the file lists 6 pads (0,0), (4,3), (5,3), (4,4), (5,4), (17,7).

- [ ] **Step 5: Commit**

```bash
./dbt format
git add src/gfx tests/spec/gfx tests/golden/gfx/grid
git commit -m "feat(gfx): dimension-parameterised PadGrid with row dirty masks"
```

---

### Task 12: Pure tweens — fade, scroll, grey-out

Scope note: `pad_leds.cpp`'s zoom, explode and expand/collapse animations read UI state (`explodeAnimationTargetUI`, clip geometry) and are ported when the shell owns timing (spec §12 phase 5). Phase 2 ships the three that are pure functions of two frames, so the shell has them ready and the taxonomy is established.

**Files:**
- Create: `src/gfx/grid/tween.h`, `src/gfx/grid/tween.cpp`
- Modify: `src/gfx/CMakeLists.txt`
- Test: `tests/spec/gfx/tween_spec.cpp`

**Interfaces:**
- Produces (`deluge::gfx::grid`):
  ```cpp
  /// t is 0..65535 (0 = from, 65535 = to), matching RGB::blend's index.
  void fade(const PadGrid& from, const PadGrid& to, uint16_t t, PadGrid& out);
  /// Shift `from` by `offset` columns (positive = content moves right); `incoming` fills the
  /// vacated columns from its far edge. Sidebar columns are untouched. All rows dirty.
  void scroll_horizontal(const PadGrid& from, const PadGrid& incoming, int32_t offset, PadGrid& out);
  /// Same for rows (positive = content moves down).
  void scroll_vertical(const PadGrid& from, const PadGrid& incoming, int32_t offset, PadGrid& out);
  /// RGB::greyOut applied to every main-grid pad; proportion as in RGB::greyOut.
  void grey_out(const PadGrid& in, int32_t proportion, PadGrid& out);
  ```
  All take `out` by reference (no allocation), require identical `dims()`, and leave `out.dirty_rows()` fully set.

- [ ] **Step 1: Write the failing spec**

`tests/spec/gfx/tween_spec.cpp`:

```cpp
#include "gfx/grid/tween.h"
#include "cppspec.hpp"

using namespace deluge::gfx::grid;

// clang-format off
describe tween("gfx::grid tweens", $ {
	it("fade hits both endpoints exactly", _ {
		PadGrid a(kDelugeGrid), b(kDelugeGrid), out(kDelugeGrid);
		a.set(2, 2, RGB{200, 0, 0});
		b.set(2, 2, RGB{0, 0, 200});
		fade(a, b, 0, out);
		expect(out.at(2, 2)).to_equal(RGB{200, 0, 0});
		fade(a, b, 65535, out);
		expect(out.at(2, 2)).to_equal(RGB{0, 0, 200});
		expect(out.dirty_rows()).to_equal(0xFFu);
	});

	it("scroll_horizontal moves main pads and pulls incoming columns in, leaving the sidebar alone", _ {
		PadGrid a(kDelugeGrid), in(kDelugeGrid), out(kDelugeGrid);
		a.set(0, 0, RGB{9, 9, 9});
		a.set(16, 0, RGB{7, 7, 7});      // sidebar
		in.set(15, 0, RGB{1, 2, 3});     // rightmost incoming main column
		scroll_horizontal(a, in, 1, out); // content moves right by one
		expect(out.at(1, 0)).to_equal(RGB{9, 9, 9});
		expect(out.at(0, 0)).to_equal(RGB{1, 2, 3});
		expect(out.at(16, 0)).to_equal(RGB{7, 7, 7});
		in.set(0, 0, RGB{4, 4, 4});
		scroll_horizontal(a, in, -1, out); // content moves left by one; column 15 comes from incoming's column 0
		expect(out.at(15, 0)).to_equal(RGB{4, 4, 4});
		expect(out.at(0, 0)).to_equal(RGB{0, 0, 0});
	});

	it("scroll_vertical moves rows", _ {
		PadGrid a(kDelugeGrid), in(kDelugeGrid), out(kDelugeGrid);
		a.set(3, 0, RGB{5, 5, 5});
		in.set(3, 7, RGB{6, 6, 6});
		scroll_vertical(a, in, 1, out);
		expect(out.at(3, 1)).to_equal(RGB{5, 5, 5});
		expect(out.at(3, 0)).to_equal(RGB{6, 6, 6});
	});

	it("grey_out dims main pads only", _ {
		PadGrid a(kDelugeGrid), out(kDelugeGrid);
		a.set(0, 0, RGB{255, 255, 255});
		a.set(17, 0, RGB{255, 255, 255});
		grey_out(a, 65536 / 2, out);
		expect(out.at(0, 0).r).to_be_less_than(uint8_t(255));
		expect(out.at(17, 0)).to_equal(RGB{255, 255, 255});
	});
});

CPPSPEC_SPEC(tween)
```

- [ ] **Step 2: Run it to see it fail**

Run: `cmake --build build/tests --config Debug 2>&1 | tail -3` — Expected: `gfx/grid/tween.h` not found.

- [ ] **Step 3: Implement**

`src/gfx/grid/tween.h`:

```cpp
#pragma once
#include "gfx/grid/pad_grid.h"

namespace deluge::gfx::grid {
// Pure frame→frame transitions. The caller owns timing; `out` must share `dims()` with the inputs.
void fade(const PadGrid& from, const PadGrid& to, uint16_t t, PadGrid& out);
void scroll_horizontal(const PadGrid& from, const PadGrid& incoming, int32_t offset, PadGrid& out);
void scroll_vertical(const PadGrid& from, const PadGrid& incoming, int32_t offset, PadGrid& out);
void grey_out(const PadGrid& in, int32_t proportion, PadGrid& out);
} // namespace deluge::gfx::grid
```

`src/gfx/grid/tween.cpp`:

```cpp
#include "gfx/grid/tween.h"

namespace deluge::gfx::grid {

void fade(const PadGrid& from, const PadGrid& to, uint16_t t, PadGrid& out) {
	const auto d = from.dims();
	for (uint8_t y = 0; y < d.rows; y++) {
		for (uint8_t x = 0; x < d.cols(); x++) {
			// RGB::blend(a, b, index): index 0 = b, 65535 = a.
			out.at(x, y) = RGB::blend(to.at(x, y), from.at(x, y), t);
		}
	}
	out.mark_all_dirty();
}

void scroll_horizontal(const PadGrid& from, const PadGrid& incoming, int32_t offset, PadGrid& out) {
	const auto d = from.dims();
	const int32_t main = d.main_cols;
	for (uint8_t y = 0; y < d.rows; y++) {
		for (int32_t x = 0; x < main; x++) {
			const int32_t src = x - offset;
			if (src >= 0 && src < main) {
				out.at(x, y) = from.at(src, y);
			}
			else if (offset > 0) {
				out.at(x, y) = incoming.at(main + src, y); // src < 0: incoming's right edge
			}
			else {
				out.at(x, y) = incoming.at(src - main, y); // src >= main: incoming's left edge
			}
		}
		for (int32_t x = main; x < d.cols(); x++) {
			out.at(x, y) = from.at(x, y);
		}
	}
	out.mark_all_dirty();
}

void scroll_vertical(const PadGrid& from, const PadGrid& incoming, int32_t offset, PadGrid& out) {
	const auto d = from.dims();
	const int32_t rows = d.rows;
	for (int32_t y = 0; y < rows; y++) {
		const int32_t src = y - offset;
		for (uint8_t x = 0; x < d.cols(); x++) {
			if (src >= 0 && src < rows) {
				out.at(x, y) = from.at(x, src);
			}
			else if (offset > 0) {
				out.at(x, y) = incoming.at(x, rows + src);
			}
			else {
				out.at(x, y) = incoming.at(x, src - rows);
			}
		}
	}
	out.mark_all_dirty();
}

void grey_out(const PadGrid& in, int32_t proportion, PadGrid& out) {
	const auto d = in.dims();
	for (uint8_t y = 0; y < d.rows; y++) {
		for (uint8_t x = 0; x < d.cols(); x++) {
			RGB c = in.at(x, y);
			out.at(x, y) = (x < d.main_cols) ? c.greyOut(proportion) : c;
		}
	}
	out.mark_all_dirty();
}

} // namespace deluge::gfx::grid
```

`RGB::greyOut` is non-const in `rgb.h` (`constexpr RGB greyOut(int32_t)`); calling it on the local copy `c` is fine. Add `grid/tween.cpp` to `deluge_gfx_grid`.

- [ ] **Step 4: Run**

Run: `cmake --build build/tests --config Debug && ctest --test-dir build/tests -C Debug -R tween_spec --output-on-failure` — Expected: PASS.

- [ ] **Step 5: Commit**

```bash
./dbt format
git add src/gfx tests/spec/gfx/tween_spec.cpp
git commit -m "feat(gfx): pure fade, scroll and grey-out tweens over PadGrid"
```

---

### Task 13: Layer-rule check for `src/gfx`

**Files:**
- Create: `scripts/check_gfx_layer.py`
- Modify: `tests/CMakeLists.txt` (register as a ctest), `src/gfx/README.md` (mention)
- Test: the script itself, run against the tree and against a deliberately bad temp file

**Interfaces:**
- Produces: `python3 scripts/check_gfx_layer.py [--root src/gfx]` exits 1 and prints `file:line: forbidden include "<x>"` for any `#include` in `src/gfx/**` that is not `gfx/...`, a relative `"..."` within `src/gfx`, or a `<...>` standard header; additionally fails if `gfx/oled/**` includes `gfx/grid/**` or vice versa.

- [ ] **Step 1: Write the checker**

`scripts/check_gfx_layer.py`:

```python
#!/usr/bin/env python3
"""Enforce the src/gfx include rules (src/gfx/README.md).

gfx may include: its own headers ("gfx/..." or a relative path that resolves inside
src/gfx) and standard headers (<...>). gfx/oled and gfx/grid may not include each other.
"""
import argparse
import re
import sys
from pathlib import Path

INCLUDE = re.compile(r'^\s*#\s*include\s+([<"])([^>"]+)[>"]')


def check(root: Path) -> int:
    bad = 0
    for path in sorted(root.rglob("*")):
        if path.suffix not in {".h", ".hpp", ".c", ".cpp"}:
            continue
        side = "oled" if "oled" in path.relative_to(root).parts else "grid"
        for lineno, line in enumerate(path.read_text().splitlines(), 1):
            m = INCLUDE.match(line)
            if not m:
                continue
            kind, target = m.groups()
            if kind == "<":
                continue  # standard headers only; nothing else in the tree is <...>-included from gfx
            if target.startswith("gfx/"):
                ok = True
                other = "grid" if side == "oled" else "oled"
                if target.startswith(f"gfx/{other}/"):
                    ok = False
            else:
                resolved = (path.parent / target).resolve()
                ok = root.resolve() in resolved.parents
            if not ok:
                print(f'{path}:{lineno}: forbidden include "{target}"')
                bad += 1
    return bad


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default="src/gfx")
    args = ap.parse_args()
    n = check(Path(args.root))
    if n:
        print(f"{n} forbidden include(s) in {args.root}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 2: Prove it fails on a violation, then passes on the tree**

```bash
mkdir -p /tmp/gfxcheck/gfx/oled && printf '#include "gfx/grid/rgb.h"\n#include "model/song/song.h"\n' > /tmp/gfxcheck/gfx/oled/bad.h
python3 scripts/check_gfx_layer.py --root /tmp/gfxcheck/gfx; echo "exit=$?"
python3 scripts/check_gfx_layer.py; echo "exit=$?"
```
Expected: first prints two `forbidden include` lines and `exit=1`; second prints nothing and `exit=0`. If the second fails, the offending include (probably a leftover `definitions` or `util/` include from Task 8/10) must be removed — that is the point.

- [ ] **Step 3: Register as a ctest and document**

In `tests/CMakeLists.txt`, after `enable_testing()`:

```cmake
find_package(Python3 COMPONENTS Interpreter)
if (Python3_Interpreter_FOUND)
    add_test(NAME gfx_layer_check
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/../scripts/check_gfx_layer.py
                     --root ${CMAKE_CURRENT_LIST_DIR}/../src/gfx)
endif ()
```

Append to `src/gfx/README.md`:

```markdown
Run the check locally with `python3 scripts/check_gfx_layer.py`; it is also the `gfx_layer_check` ctest.

Deferred to later phases (tracked in the UI redesign spec): `RGB` into the `deluge::gfx::grid`
namespace; the raw `uint8_t[]` graphics still on `OLED`; zoom/explode/expand-collapse tweens.
```

- [ ] **Step 4: Run the whole suite one last time**

Run: `cmake --build build/tests --config Debug && ctest --test-dir build/tests -C Debug --output-on-failure`
Expected: all PASS, including `gfx_layer_check`, `canvas_golden_spec`, `canvas_spec`, `geometry_spec`, `font_spec`, `pad_grid_spec`, `tween_spec`, `UnitTests`.
Run: `./dbt build Debug && cmake --build build-sim --target deluge_host` — Expected: both build.

- [ ] **Step 5: Commit**

```bash
./dbt format
git add scripts/check_gfx_layer.py tests/CMakeLists.txt src/gfx/README.md
git commit -m "test(gfx): enforce the gfx include rules as a ctest"
```

---

## Self-review notes

- **Spec coverage.** §10 (ABI): Tasks 1–2. §6.1 Canvas over span + geometry, sentinel defaults, `hackGetImageStore` → `bytes()`, `roundedCornersEnabled` out of the rasteriser, no `cpu_specific`/`board_config` dependency: Tasks 8–9. §6.2 geometry types, `Font` handle over the five fonts, one `Icon` type in a registry: Tasks 5–7 (the `TextStyle`/ink-bounds API is toolkit-phase and is not claimed here). §6.3 `PadGrid` with runtime dims and row mask, `RGB`/palette as-is, tweens as pure functions: Tasks 10–12 (three tweens now; zoom/explode/expand-collapse deferred with the reason stated). §6.4/§11 host-testable and golden-tested: Tasks 4, 8, 11, 13. §12 phase 2 acceptance "byte-identical frames": Task 4 records, Task 9 proves.
- **Not in this plan (by design):** `deluge_display_blit_oled_n` has no caller yet — the shell (phase 4) is its consumer; `PadLEDs::` keeps its own `image` until phase 5; l10n/`ui-tokens` untouched.
- **Type consistency:** `Geometry{width,height,visible_top}`, `kDelugeOled`, `kEdge`, `kCentre`, `OwnedCanvas<G>`, `bytes()`, `GridDimensions{main_cols,sidebar_cols,rows}`, `PadGrid::at/set/row/dirty_rows/mark_all_dirty`, `font_for_text_height`, `glyph_index`, `icons::<name>` are used with the same spelling in every task.
