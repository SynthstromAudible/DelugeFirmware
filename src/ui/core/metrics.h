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

// Text metrics the gfx font tables do not carry, reproduced from the legacy
// definitions_cxx.hpp so the FontId tables in ui/oled/text.cpp can name every arm rather than
// spelling a number.
constexpr int32_t kTextSmallSpacingX = 4; ///< Legacy value: definitions_cxx.hpp's 5px font textWidth.
constexpr int32_t kTextSmallSizeY = 5;    ///< Legacy value: definitions_cxx.hpp's 5px font textHeight.
constexpr int32_t kTextBigSpacingX = 11;  ///< Legacy value: definitions_cxx.hpp's 13px font textWidth.

constexpr int32_t kSubmenuIconSpacingX = 7;
constexpr int32_t kScrollTime = 400;         // ms pause at each end of a scrolling label
constexpr int32_t kScrollStepForwardMs = 15; // legacy: 15 ms per pixel forwards
constexpr int32_t kScrollStepBackMs = 5;     // 5 ms per pixel back
constexpr int32_t kFlashTime = 110;          // blink half-period
constexpr int32_t kHorizontalSlotYOffset = 2;
constexpr int32_t kMenuBaseY48 = 14; // list top for a 48-row panel (+ visible_top)
constexpr int32_t kMenuBaseY64 = 15;
/// The horizontal menu's box top (+ visible_top).
///
/// @note Deliberately distinct from kMenuBaseY48/64 above, which are the *list* constants.
///       Legacy hard-codes this one as a literal `14 + OLED_MAIN_TOPMOST_PIXEL`
///       (`HorizontalMenu::renderMenuItems`) with no panel-height branch, so it is 14 on every
///       geometry, whereas the list top (`MenuItem::drawItemsForOled`) picks 14 or 15 depending on
///       panel height. The two coincide on the shipped 48-row panel and diverge by one row on a
///       64-row one; `columns` follows this constant, `list`/`menu_rect` the pair above.
constexpr int32_t kHorizontalMenuBaseY = 14;
constexpr int32_t kTitleY = 1;           // title baseline offset (+ visible_top)
constexpr int32_t kTitleSeparatorY = 12; // (+ visible_top)
constexpr int32_t kPopupMargin = 12;     // legacy doubleMargin
constexpr int32_t kPopupMaxCharsPerLine = 19;
constexpr int32_t kValueY = 18; // huge-font value baseline (+ visible_top)
constexpr int32_t kHorizontalBoxHeight = 25;

/// @brief Output type/name label baselines (+ visible_top).
///
/// Reproduces `View::drawOutputNameFromDetails`'s `#if OLED_MAIN_HEIGHT_PIXELS == 64` branches as
/// values rather than a preprocessor fork. Legacy's numbers are all `OLED_MAIN_TOPMOST_PIXEL + n`,
/// and the frame's origin already carries that term, so `n` is what survives.
///
/// @note Deliberately *not* derived from Style::title_height: legacy's numbers are neither
///       title_height nor a multiple of row_height, and rounding them into the style would move
///       pixels.
constexpr int32_t kOutputTypeY48 = 3;
constexpr int32_t kOutputTypeY64 = 12;
constexpr int32_t kOutputNameY48 = 17;
constexpr int32_t kOutputNameY64 = 30;

/// @brief Loading-animation timing.
///
/// Reproduces legacy's `updateWorkingAnimation` intervals: the step between animation frames, and
/// the pause both before the sequence starts and after it completes a cycle.
constexpr int32_t kWorkingStepMs = 70;
constexpr int32_t kWorkingPauseMs = 350;

/// @brief Convert a FontId to the legacy `textHeight` value it was selected by.
///
/// This is the number every gfx metric call (char_width_px, char_spacing_px, string_width_px,
/// font_for_text_height) is keyed on.
///
/// Lives here, in ui-core, rather than in `ui/oled/text.h` (where `text_height_for` declares the
/// same mapping and forwards to this) because `ui/core/text_wrap.cpp` measures text and ui-core
/// cannot include ui-oled. One definition, two layers.
///
/// @note Not always the number of rows the glyphs occupy: MetricBold9 is selected by height 10
///       (kTextTitleSizeY) but draws 9 rows, the tenth being the gap under the line.
/// @note `Apple7` is 9 (kTextSpacingY), not 7. The Apple ][ face has two legacy heights that are
///       NOT interchangeable: `font_for_text_height` returns `kApple7` for heights 7-8 and
///       `kApple7Up` for 9, and `kApple7Up` draws the same glyphs one row lower. Height 9 is the
///       dominant legacy call shape, so it is the one FontId::Apple7 means.
/// @param font Font to look up.
/// @return The legacy textHeight value.
constexpr int32_t text_height_of(gfx::oled::FontId font) {
	switch (font) {
	case gfx::oled::FontId::Px5:
		return kTextSmallSizeY; // 5
	case gfx::oled::FontId::Apple7:
		return kTextSpacingY; // 9
	case gfx::oled::FontId::MetricBold9:
		return kTextTitleSizeY; // 10
	case gfx::oled::FontId::MetricBold13:
		return kTextBigSizeY; // 13
	case gfx::oled::FontId::MetricBold20:
	default:
		return kTextHugeSizeY; // 20
	}
}
} // namespace deluge::ui
