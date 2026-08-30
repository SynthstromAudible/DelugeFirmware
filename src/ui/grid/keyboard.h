#pragma once
#include "gfx/grid/palette.h"
#include "gfx/grid/rgb.h"
#include "ui/core/response.h"
#include "ui/grid/frame.h"

#include <cstdint>
#include <optional>

namespace deluge::ui::grid {

/// @brief On-pad text keyboard, plus two adapters that let a scene drive the existing
///        keyboard-layout and column-control classes through a GridFrame.
///
/// @note Ports legacy `QwertyUI::drawKeys` / `QwertyUI::padAction`.
/// @note The two adapters below are temporary bridges, retired once the legacy layout classes are
///       rewritten as components that paint directly through the frame.

// ---------------------------------------------------------------------------------------------
// Text keyboard
// ---------------------------------------------------------------------------------------------

/// @brief Keyboard layout selector; picks which row of the character table
///        `text_keyboard_char_at` uses.
///
/// Values match legacy's `KeyboardLayout` enum (`QWERTY`, `AZERTY`, `QWERTZ`), so a scene can cast
/// between them. All three layouts are represented -- dropping one would silently retype a QWERTZ
/// user's keyboard as QWERTY.
enum class KeyboardLayoutKind : uint8_t { Qwerty = 0, Azerty = 1, Qwertz = 2 };
constexpr uint8_t kNumKeyboardLayoutKinds = 3;

/// @brief Per-scene state the text keyboard keeps between frames.
struct TextKeyboardState {
	/// True if any shift pad is held. Recomputed every frame from the pads `text_keyboard` paints,
	/// so a scene that needs to know whether shift is down (e.g. to redraw something else) can
	/// read it without touching the matrix driver directly.
	bool shift = false;
};

/// @brief Default home row for the text keyboard; it occupies rows `home_row - 2` through
///        `home_row + 2`.
constexpr int32_t kTextKeyboardHomeRow = 3;

/// @brief Number of columns the keyboard owns (the main pad area; the sidebar is left untouched).
constexpr int32_t kTextKeyboardCols = 16;

/// @brief Character codes `text_keyboard_char_at` returns for the backspace and enter pads.
///
/// These two action pads are dispatched by coordinate rather than through the character table.
/// ASCII backspace and carriage return are used because neither can collide with a table entry
/// (the table holds only `0`, `' '`, digits, `A`-`Z` and `- ' , .`).
constexpr char kTextKeyboardBackspace = '\b';
constexpr char kTextKeyboardEnter = '\r';

/// @brief The character pad (@p x, @p y) types, or 0 for a pad that types nothing.
///
/// A pure lookup into the layout's character table, with guards around it:
///
/// - backspace when `y == home_row + 2 && x >= 14 && x < 16`,
/// - enter when `y == home_row && x >= 14 && x < 16`,
/// - the table is consulted only for `x >= 3 && x < 14 && y >= home_row - 2 && y <= home_row + 2`,
/// - a table entry of `0` types nothing.
///
/// @note @p shift applies *only on the digit row* (`y == home_row + 2`), and only to four keys:
///       `-` -> `_`, `1` -> `!`, `3` -> `#`, `6` -> `^`. Every other key, including the letters, is
///       unchanged with shift down -- letter casing is decided later by the surrounding word, and
///       belongs to the text model rather than the pad grid.
///
/// @param layout   Which keyboard layout's character table to consult.
/// @param x        Pad column.
/// @param y        Pad row.
/// @param home_row Row the keyboard is centred on.
/// @param shift    Whether a shift pad is currently held.
/// @return The character typed by this pad, or 0 if it types nothing.
char text_keyboard_char_at(KeyboardLayoutKind layout, int32_t x, int32_t y, int32_t home_row, bool shift);

/// @brief The colour (@p x, @p y) is painted, or `nullopt` for a pad the keyboard never writes
///        (painted black by `text_keyboard`).
///
/// Some pads are covered by more than one rule below; the checks are ordered *last-write-first*,
/// matching draw order. The full map (`hr` = @p home_row):
///
/// | pads | colour |
/// |---|---|
/// | row `hr-1`, x 1-2 and 13-14 (shift) | `colours::blue` |
/// | row `hr`, x 14-15 (enter) | `colours::green` |
/// | row `hr+2`, x 14-15 (backspace) | `colours::red` |
/// | row `hr-2`, x 5-10 (space bar) | `monochrome(160)` |
/// | row `hr`, x 6 and 9 (home keys) | `monochrome(160)` |
/// | row `hr`, x 3-5 and 10-12 (home keys) | `monochrome(64)` |
/// | row `hr`, x 7, 8, 13 (rest of `asdf`) | `monochrome(10)` |
/// | row `hr+2`, x 13 (the `-` key) | `monochrome(10)` |
/// | row `hr+2`, x 3-12 (digits) | `monochrome(64)` |
/// | row `hr+1`, x 3-12 (`qwer`) | `monochrome(10)` |
/// | row `hr-1`, x 3-11 (`zxcv`) | `monochrome(10)` |
///
/// @param x        Pad column.
/// @param y        Pad row.
/// @param home_row Row the keyboard is centred on.
/// @return The pad's colour, or `nullopt` if the keyboard leaves it unpainted.
std::optional<RGB> text_keyboard_key_colour(int32_t x, int32_t y, int32_t home_row);

/// @brief Is (@p x, @p y) one of the four shift pads? Exactly the set `text_keyboard_key_colour`
///        paints blue.
///
/// @param x        Pad column.
/// @param y        Pad row.
/// @param home_row Row the keyboard is centred on.
/// @return True if the pad is a shift pad.
bool text_keyboard_is_shift_pad(int32_t x, int32_t y, int32_t home_row);

/// @brief Paint the on-pad keyboard for one frame and report this tick's key press.
///
/// Paints columns `[0, min(f.bounds().w, kTextKeyboardCols))` of every row, black where
/// `text_keyboard_key_colour` has nothing -- a clear-then-draw collapsed into one pass, which is
/// also why the sidebar is left untouched.
///
/// @note `Response::index` carries the typed character code, or -1 for none, and
///       `Response::held_index` the same for the character *held* this tick -- the level under
///       `index`'s edge, which is what a press-and-hold repeat re-reads (legacy's
///       `UI_MODE_HOLDING_BACKSPACE`, `gui/ui/qwerty_ui.cpp:218-227,449-453`). Both are filled by
///       the same "first pad that types a character" rule, one over `pressed` and one over `held`,
///       and both are resolved with this tick's shift state. `held_index` is set on the tick of the
///       press too, since `GridFrame::pad` answers `held` from a mask that already includes the
///       event being delivered. `Response::velocity`
///       stays the *pad's* press velocity everywhere in the toolkit, so it is never repurposed to
///       carry a character code. Shift pads are read from the same pass that paints them
///       (`Response::held`, plus `pressed` so a shift pad pressed this very tick already counts),
///       written to @p state's `shift` field, and applied to the lookup -- so a press on `1` while
///       shift is down returns `'!'` on the tick it happens.
///
/// The flags describe the keyboard as a whole: `pressed`, `released` and `held` are true if *any*
/// keyboard pad was pressed, released or is down this tick. `index` and `velocity` both come from
/// the first press **that types a character**, not merely the first press in scan order: a shift
/// pad sits ahead of the digit row in a bottom-up sweep and falls outside the key rectangle, so it
/// types nothing, but the digit pressed in the same tick still must -- taking the first press
/// unconditionally would have swallowed that keystroke. A scene that needs per-pad detail reads
/// `f.local_events()` for the same tick.
///
/// @param f        Frame to paint into.
/// @param state    Per-scene keyboard state, updated in place.
/// @param layout   Which keyboard layout to use.
/// @param home_row Row the keyboard is centred on.
/// @return This tick's aggregate response: `pressed`/`released`/`held` are true if any keyboard pad
///         was pressed/released/held; `index` is the typed character (or -1) and `velocity` its
///         press velocity; `held_index` is the held character (or -1).
Response text_keyboard(GridFrame& f, TextKeyboardState& state, KeyboardLayoutKind layout,
                       int32_t home_row = kTextKeyboardHomeRow);

// ---------------------------------------------------------------------------------------------
// Legacy adapters -- bridges, not components
// ---------------------------------------------------------------------------------------------

/// @brief The shape of legacy's pad image: `RGB image[rows][cols]`, matching
///        `KeyboardLayout::renderPads` and `ControlColumn::renderColumn`.
///
/// Spelled out here as literals because `ui/grid` must not include `gui/` -- that is the entire
/// point of these two adapters.
constexpr int32_t kLegacyImageRows = 8;
constexpr int32_t kLegacyImageCols = 18;
using LegacyImage = RGB[kLegacyImageRows][kLegacyImageCols];

/// @brief Copy the whole scratch image into the frame, clipped to `f.bounds()`.
/// @param f     Frame to paint into.
/// @param image Scratch image to copy from.
void copy_legacy_image(GridFrame& f, const LegacyImage& image);

/// @brief Copy one column of the scratch image into the frame, clipped to `f.bounds()`.
/// @param f     Frame to paint into.
/// @param x     Column to copy.
/// @param image Scratch image to copy from.
void copy_legacy_column(GridFrame& f, int32_t x, const LegacyImage& image);

/// @brief Adapter over the legacy `KeyboardLayout` interface.
///
/// @note Templated on the concrete layout type because `ui/grid` may not include `gui/`, and
///       `KeyboardLayout` lives behind a deep `gui/` include chain; templating lets the caller
///       supply the concrete type with no `gui/` include here. Together with `control_column`
///       below and `GridFrame::region`, these are the only templates in the toolkit.
/// @note This is a temporary bridge, retired once the legacy layout classes are rewritten as
///       components that paint through the frame directly.
///
/// Required interface of `Layout`, matching `KeyboardLayout` exactly:
///
///     void renderPads(RGB image[][18]);         // may be a no-op
///     void renderSidebarPads(RGB image[][18]);  // draws the two sidebar columns
///
/// A `RGB (&)[8][18]` argument decays to `RGB (*)[18]`, so the real virtuals bind unchanged.
///
/// The scratch image is zero-initialised each call and then copied wholesale, sidebar included --
/// `renderSidebarPads` writes columns 16 and 17, and the root frame addresses those in the same
/// coordinate space.
///
/// @tparam Layout Concrete legacy layout type; must implement the interface above.
/// @param f      Frame to paint into.
/// @param layout Legacy layout instance to render.
template <class Layout>
void keyboard(GridFrame& f, Layout& layout) {
	LegacyImage image = {};
	layout.renderPads(image);
	layout.renderSidebarPads(image);
	copy_legacy_image(f, image);
}

/// @brief Adapter over the legacy `ControlColumn` interface. Same rationale, lifetime and
///        eventual fate as `keyboard` above.
///
/// Required interface of `Column`:
///
///     void renderColumn(RGB image[][18], int32_t column, void* layout);
///
/// Legacy's virtual takes a `KeyboardLayout*` third parameter; naming that type here would mean
/// including `gui/`, so it is passed through as `void*`. The concrete `Column` the scene supplies
/// is expected to be a one-line shim that casts it back before forwarding to the real virtual --
/// that shim lives in `deluge-scenes`, which already knows both types, keeping the layer rule
/// intact.
///
/// @note Only column @p x is copied out. Legacy's `renderColumn` writes just that column and the
///       caller paints the rest of the grid separately, so copying the whole scratch image would
///       blacken everything already drawn.
///
/// @tparam Column Concrete legacy control-column type; must implement the interface above.
/// @param f      Frame to paint into.
/// @param x      Column to render.
/// @param c      Legacy control-column instance to render.
/// @param layout Opaque pointer forwarded to @p c's `renderColumn`, cast back by the caller's shim.
template <class Column>
void control_column(GridFrame& f, int32_t x, Column& c, void* layout) {
	LegacyImage image = {};
	c.renderColumn(image, x, layout);
	copy_legacy_column(f, x, image);
}

} // namespace deluge::ui::grid
