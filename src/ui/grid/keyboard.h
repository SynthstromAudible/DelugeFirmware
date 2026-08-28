#pragma once
#include "gfx/grid/palette.h"
#include "gfx/grid/rgb.h"
#include "ui/core/response.h"
#include "ui/grid/frame.h"

#include <cstdint>
#include <optional>

namespace deluge::ui::grid {

/// The on-pad text keyboard (spec B section 7's `text_keyboard`), ported from `QwertyUI::drawKeys`
/// and `QwertyUI::padAction` (gui/ui/qwerty_ui.cpp), plus the two adapters that let a scene drive
/// the *existing* keyboard-layout and column-control classes through a `GridFrame` until phase 6
/// retires them.

// ---------------------------------------------------------------------------------------------
// Text keyboard
// ---------------------------------------------------------------------------------------------

/// Which row of `keyboardChars` (qwerty_ui.cpp:180-203) a lookup uses.
///
/// Legacy indexes that table with `util::to_underlying(FlashStorage::keyboardLayout)`
/// (qwerty_ui.cpp:268), i.e. `enum class KeyboardLayout : uint8_t { QWERTY, AZERTY, QWERTZ }`
/// (definitions_cxx.hpp:891-895). The brief spells only `{ Qwerty, Azerty }`; `Qwertz` is added
/// as an amendment, because the legacy table has three rows and dropping one would silently
/// retype a QWERTZ user's keyboard as QWERTY. The underlying values match legacy's so a scene can
/// cast between them.
enum class KeyboardLayoutKind : uint8_t { Qwerty = 0, Azerty = 1, Qwertz = 2 };
constexpr uint8_t kNumKeyboardLayoutKinds = 3;

/// Per-scene state the text keyboard keeps between frames.
///
/// `shift` is legacy's shift-pad *hold* test, `matrixDriver.isPadPressed(1, kQwertyHomeRow - 1)
/// || (2, …) || (13, …) || (14, …)` (qwerty_ui.cpp:284-287), lowered to a field: the component
/// recomputes it every frame from the pads it paints and writes it here, so a scene that needs
/// to know whether shift is down (e.g. to redraw something else) can read it without touching
/// the matrix driver.
struct TextKeyboardState {
	bool shift = false;
};

/// Legacy's `kQwertyHomeRow` (definitions_cxx.hpp:848). The keyboard occupies rows
/// `home_row - 2` through `home_row + 2`.
constexpr int32_t kTextKeyboardHomeRow = 3;

/// The number of columns the keyboard owns. `drawKeys` starts with
/// `PadLEDs::clearMainPadsWithoutSending()` (qwerty_ui.cpp:76), which clears columns
/// `[0, kDisplayWidth)` and leaves the sidebar alone (pad_leds.cpp:236-240).
constexpr int32_t kTextKeyboardCols = 16;

/// Characters `text_keyboard_char_at` returns for the two action pads, which have no entry in
/// legacy's `keyboardChars` table because legacy dispatches them by coordinate instead
/// (backspace at qwerty_ui.cpp:205, enter at :232).
///
/// Recorded as an amendment: the brief asks for a single pure `char` lookup covering "the
/// space/backspace/enter keys", so the two actions need char codes. ASCII backspace and carriage
/// return are used because neither can collide with a table entry (the table holds only
/// `0`, `' '`, digits, `A`-`Z` and `- ' , .`).
constexpr char kTextKeyboardBackspace = '\b';
constexpr char kTextKeyboardEnter = '\r';

/// The character pad (\p x, \p y) types, or 0 for a pad that types nothing.
///
/// Pure: legacy's `keyboardChars[layout][kQwertyHomeRow - y + 2][x - 3]` (qwerty_ui.cpp:267-268)
/// with legacy's own guards around it --
///
/// - backspace when `y == home_row + 2 && x >= 14 && x < 16` (:205),
/// - enter when `y == home_row && x >= 14 && x < 16` (:232),
/// - the table only for `x >= 3 && x < 14 && y >= home_row - 2 && y <= home_row + 2` (:255),
/// - a table entry of `0` types nothing (:269).
///
/// \p shift is legacy's shift-pad hold. Legacy applies it *only on the digit row*
/// (`if (y == kQwertyHomeRow + 2)`, :283) and only to four keys: `-` -> `_`, `1` -> `!`,
/// `3` -> `#`, `6` -> `^` (:290-301). Every other key is unchanged with shift down, including
/// the letters -- legacy's letter casing is decided later, by the surrounding word (:337-345),
/// not by the shift pads, and that logic belongs to the text model rather than the pad grid.
char text_keyboard_char_at(KeyboardLayoutKind layout, int32_t x, int32_t y, int32_t home_row, bool shift);

/// The colour `drawKeys` leaves on pad (\p x, \p y), or `nullopt` for a pad it never writes
/// (which `text_keyboard` paints black, per the `clearMainPadsWithoutSending` at
/// qwerty_ui.cpp:76).
///
/// `drawKeys` (qwerty_ui.cpp:75-121) writes some pads more than once and the last write wins;
/// the checks below are therefore ordered *last-write-first*. The full map, with legacy lines:
///
/// | pads | colour | line |
/// |---|---|---|
/// | row `hr-1`, x 1-2 and 13-14 (shift) | `colours::blue` | :117-118 |
/// | row `hr`, x 14-15 (enter) | `colours::green` | :114 |
/// | row `hr+2`, x 14-15 (backspace) | `colours::red` | :111 |
/// | row `hr-2`, x 5-10 (space bar) | `monochrome(160)` | :105-107 |
/// | row `hr`, x 6 and 9 (home keys) | `monochrome(160)` | :98-99 |
/// | row `hr`, x 3-5 and 10-12 (home keys) | `monochrome(64)` | :95-97, :100-102 |
/// | row `hr`, x 7, 8, 13 (rest of `asdf`) | `monochrome(10)` | :86 |
/// | row `hr+2`, x 13 (the `-` key) | `monochrome(10)` | :92 |
/// | row `hr+2`, x 3-12 (digits) | `monochrome(64)` | :83 |
/// | row `hr+1`, x 3-12 (`qwer`) | `monochrome(10)` | :84 |
/// | row `hr-1`, x 3-11 (`zxcv`) | `monochrome(10)` | :88 |
std::optional<RGB> text_keyboard_key_colour(int32_t x, int32_t y, int32_t home_row);

/// Is (\p x, \p y) one of the four shift pads? `matrixDriver.isPadPressed(1|2|13|14,
/// kQwertyHomeRow - 1)` (qwerty_ui.cpp:284-287), which is exactly the set `drawKeys` paints blue
/// (:117-118).
bool text_keyboard_is_shift_pad(int32_t x, int32_t y, int32_t home_row);

/// Paint the on-pad keyboard and report this tick's key press -- spec B section 7's
/// `text_keyboard(f, TextKeyboardState&)`.
///
/// Paints columns `[0, min(f.bounds().w, kTextKeyboardCols))` of every row, black where
/// `text_keyboard_key_colour` has nothing, which is legacy's clear-then-draw
/// (`clearMainPadsWithoutSending`, qwerty_ui.cpp:76) collapsed into one pass -- the immediate-mode
/// equivalent, and the reason the sidebar is untouched.
///
/// `Response::index` carries the typed character code, -1 for none (ui/core/response.h); the
/// brief's "in `velocity`" spelling is gone, because `Response::velocity` is the *pad* velocity
/// everywhere in the toolkit and a character code is not one. The shift pads are read from the
/// same pass that paints them (`Response::held`, plus `pressed` so a shift pad pressed on this
/// very tick already counts), written to `state.shift`, and applied to the lookup -- so a press on
/// `1` while shift is down returns `'!'` on the tick it happens.
///
/// The flags describe the keyboard as a whole: `pressed`, `released` and `held` are true if *any*
/// keyboard pad was pressed, released or is down this tick. `index` and `velocity` both come from
/// the first press **that types a character** -- not merely the first press in scan order. Legacy runs `padAction`
/// once per event (`qwerty_ui.cpp:205-330`), so a shift pad (which sits at `home_row - 1`, ahead of
/// the digit row in a bottom-up sweep, and falls outside the key rectangle at `:255`) does nothing
/// while the digit pressed in the same tick still types; taking the first press unconditionally
/// would have swallowed that keystroke. A scene that needs per-pad detail reads `f.local_events()`
/// for the same tick.
Response text_keyboard(GridFrame& f, TextKeyboardState& state, KeyboardLayoutKind layout,
                       int32_t home_row = kTextKeyboardHomeRow);

// ---------------------------------------------------------------------------------------------
// Legacy adapters -- bridges, not components
// ---------------------------------------------------------------------------------------------

/// The shape of legacy's pad image: `RGB image[kDisplayHeight][kDisplayWidth + kSideBarWidth]`
/// (`KeyboardLayout::renderPads`, gui/ui/keyboard/layout.h:62; `ControlColumn::renderColumn`,
/// gui/ui/keyboard/column_controls/control_column.h:34). Spelled out here as literals because
/// `ui/grid` must not include `gui/` -- that is the entire point of these two adapters.
constexpr int32_t kLegacyImageRows = 8;
constexpr int32_t kLegacyImageCols = 18;
using LegacyImage = RGB[kLegacyImageRows][kLegacyImageCols];

/// Copy the whole scratch image into the frame, clipped to `f.bounds()`.
void copy_legacy_image(GridFrame& f, const LegacyImage& image);

/// Copy one column of the scratch image into the frame, clipped to `f.bounds()`.
void copy_legacy_column(GridFrame& f, int32_t x, const LegacyImage& image);

/// Adapter over the legacy `KeyboardLayout` interface -- spec B section 7's
/// `keyboard(f, KeyboardLayout&)`.
///
/// **Why this is a template.** `ui/grid` may include only `ui/**`, `gfx/**` and the standard
/// library (spec B section 2), and `KeyboardLayout` lives in `gui/ui/keyboard/layout.h` behind a
/// deep include chain. Templating on the concrete type lets `deluge-scenes` supply it at the call
/// site with no `gui/` include here. Together with `control_column` below and `GridFrame::region`
/// these are the only templates in the toolkit; both adapters are deleted in phase 6, when the
/// layouts are rewritten as components that paint through the frame directly and the `RGB[][]`
/// signatures go away.
///
/// **Required interface of `Layout`** -- exactly `KeyboardLayout`'s (layout.h:62-72):
///
///     void renderPads(RGB image[][18]);         // may be a no-op; legacy's base class is
///     void renderSidebarPads(RGB image[][18]);  // `virtual … {}` / draws the two sidebar cols
///
/// A `RGB (&)[8][18]` argument decays to `RGB (*)[18]`, so the real virtuals bind unchanged.
///
/// The scratch image is zero-initialised each call (legacy's callers clear before rendering) and
/// then copied wholesale, sidebar included -- `renderSidebarPads` writes columns 16 and 17, and
/// the root frame addresses those in the same coordinate space (ui/grid/frame.h).
template <class Layout>
void keyboard(GridFrame& f, Layout& layout) {
	LegacyImage image = {};
	layout.renderPads(image);
	layout.renderSidebarPads(image);
	copy_legacy_image(f, image);
}

/// Adapter over the legacy `ControlColumn` interface -- spec B section 7's
/// `control_column(f, x, ControlColumn&)`. Same rationale, lifetime and phase-6 fate as
/// `keyboard` above.
///
/// **Required interface of `Column`**:
///
///     void renderColumn(RGB image[][18], int32_t column, void* layout);
///
/// Legacy's virtual is `renderColumn(RGB image[][kDisplayWidth + kSideBarWidth], int32_t column,
/// KeyboardLayout* layout)` (control_column.h:34). The third parameter is opaque to this layer --
/// naming `KeyboardLayout` would mean including `gui/` -- so it is passed through as `void*` and
/// the concrete `Column` the scene supplies is expected to be a one-line shim that casts it back
/// before forwarding to the real virtual. That shim lives in `deluge-scenes`, which already knows
/// both types; putting the cast there rather than here is what keeps the layer rule intact.
///
/// Only column \p x is copied out. Legacy's `renderColumn` writes just that column and the caller
/// paints the rest of the grid separately, so copying the whole scratch image would blacken
/// everything the caller had already drawn.
template <class Column>
void control_column(GridFrame& f, int32_t x, Column& c, void* layout) {
	LegacyImage image = {};
	c.renderColumn(image, x, layout);
	copy_legacy_column(f, x, image);
}

} // namespace deluge::ui::grid
