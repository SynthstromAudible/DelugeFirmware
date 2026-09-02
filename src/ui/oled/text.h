#pragma once
#include "gfx/oled/font.h"
#include "gfx/oled/geometry.h"
#include "gfx/oled/icon.h"
#include "ui/oled/frame.h"
#include <cstdint>
#include <string_view>

namespace deluge::ui::oled {

enum class Align : uint8_t { Left, Centre, Right };

/// @brief How a run of text is drawn.
///
/// Replaces the legacy `textWidth`/`textHeight` pair of magic numbers: the font is named, and the
/// two numbers are derived from it.
struct TextStyle {
	gfx::oled::FontId font;
	Align align = Align::Left;
	/// Shrink the text to fit the rect when it is too wide, the way
	/// `Canvas::drawStringCentredShrinkIfNecessary` did. That routine centres, both when it
	/// shrinks and when it does not, so this overrides `align`.
	bool shrink = false;
	/// Trim the text from the end until it fits the rect, the way
	/// `Canvas::drawStringCentered(startX ... totalWidth)` did -- the horizontal-menu slot
	/// labels. Also centres unconditionally, so it too overrides `align`, and it caps the
	/// string at 24 characters before it starts trimming. Takes precedence over `shrink`;
	/// setting both is a caller error (the two are different answers to the same question).
	bool truncate = false;
};

/// @brief The legacy `textHeight` a font is selected by -- the number to lay out with.
///
/// Not always the number of rows the glyphs occupy: `MetricBold9` is selected by height 10
/// (the legacy `kTextTitleSizeY`) but draws 9 rows, the tenth being the gap under the line.
///
/// @param font the font to look up
/// @return The legacy layout height for @p font.
int32_t text_height_for(gfx::oled::FontId font);

/// @brief The legacy `textWidth`: the nominal per-character advance a font is drawn with.
/// @param font the font to look up
/// @return The legacy per-character advance for @p font.
int32_t spacing_x_for(gfx::oled::FontId font);

/// @brief Measure a string, in the legacy *advance* metrics -- not ink bounds.
///
/// `w` is `gfx::oled::string_width_px`, the sum of per-character advances including the
/// trailing spacing after the last character (an off-by-one the gfx code keeps deliberately);
/// `h` is `text_height_for`, the height the font is selected by rather than the number of rows
/// it inks.
///
/// @note Advance metrics, not ink bounds, are used because every legacy centring and
///       right-aligning call site was laid out against them; switching to ink bounds would shift
///       all of them.
///
/// Needs no canvas, so layout code can call it before drawing.
///
/// @param s    the string to measure
/// @param font the font to measure it in
/// @return The string's advance width and the font's layout height.
gfx::oled::Size measure(std::string_view s, gfx::oled::FontId font);

/// @brief Draw \p s inside \p rect (frame-local coordinates), aligned per \p ts.
///
/// The alignment arithmetic is the legacy arithmetic, generalised from "the whole panel" to
/// "this rect": `Align::Left` is `Canvas::drawString` at the rect's left edge,
/// `Align::Centre` is `drawStringCentred` about the rect's centre, `Align::Right` is
/// `drawStringAlignRight` against the rect's right edge, `ts.shrink` is
/// `drawStringCentredShrinkIfNecessary` and `ts.truncate` is `drawStringCentered(startX ...)`,
/// both over the rect's width.
///
/// @param f    the frame to draw into
/// @param rect the rectangle (frame-local) to draw and align within
/// @param s    the text to draw
/// @param ts   the font, alignment, and shrink/truncate behaviour to draw with
void text(OledFrame& f, gfx::oled::Rect rect, std::string_view s, TextStyle ts);

/// @brief Draw a screen title across the top of the frame, with the separator rule under it.
///
/// `Canvas::drawScreenTitle`, minus the bezel arithmetic -- the frame's origin already
/// accounts for `visible_top`, so the two constants are plain layout numbers here.
///
/// The face is `f.style.title_font`, not a fixed font: both shipped profiles set `MetricBold9`,
/// which is the legacy `kTextTitleSpacingX`/`kTextTitleSizeY` pair, so today's pixels are
/// unchanged, but a profile with a bigger panel can choose.
///
/// @param f         the frame to draw into
/// @param s         the title text
/// @param underline draw the separator rule under the title
void title(OledFrame& f, std::string_view s, bool underline = true);

/// @brief Draw a small icon in the title band's right-hand end, with the ninth row legacy's icons
///        cannot hold filled in for it.
///
/// `SampleMarkerEditor::renderOLED`'s loop-lock badge (`gui/ui/sample_marker_editor.cpp:1130-1133`):
/// `drawGraphicMultiLine(OLED::lockIcon, OLED_MAIN_WIDTH_PIXELS - 10, OLED_MAIN_TOPMOST_PIXEL + 1, 7)`
/// followed by `invertArea(OLED_MAIN_WIDTH_PIXELS - 10, 7, ..._PIXEL + 9, ..._PIXEL + 9)`.
///
/// The `invertArea` is not a highlight: `OLED::lockIcon` is **nine** pixels tall and a byte holds
/// eight, and legacy's own comment at `hid/display/oled_icons.cpp:118` says "remember to draw a
/// rectangle under it so it looks correct". Inverting the (blank) row one below the icon is how it
/// draws that rectangle, and doing it here means no caller has to remember.
///
/// Right-aligned by the frame's own width rather than `OLED_MAIN_WIDTH_PIXELS`, so a wider panel
/// puts the badge at its own right edge; the 10-pixel inset and the row offsets are legacy's.
///
/// @param f the frame to draw into
/// @param i the icon; its `height` decides how many rows the filled row sits below the top
void title_badge(OledFrame& f, const gfx::oled::Icon& i);

} // namespace deluge::ui::oled
