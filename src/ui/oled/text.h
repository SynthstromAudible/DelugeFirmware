#pragma once
#include "gfx/oled/font.h"
#include "gfx/oled/geometry.h"
#include "ui/oled/frame.h"
#include <cstdint>
#include <string_view>

namespace deluge::ui::oled {

enum class Align : uint8_t { Left, Centre, Right };

/// How a run of text is drawn. Replaces the legacy `textWidth`/`textHeight` pair of
/// magic numbers: the font is named, and the two numbers are derived from it.
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

/// The legacy `textHeight` a font is selected by -- the number to lay out with.
///
/// Not always the number of rows the glyphs occupy: `MetricBold9` is selected by height 10
/// (the legacy `kTextTitleSizeY`) but draws 9 rows, the tenth being the gap under the line.
int32_t text_height_for(gfx::oled::FontId font);

/// The legacy `textWidth`: the nominal per-character advance a font is drawn with.
int32_t spacing_x_for(gfx::oled::FontId font);

/// Measure a string, in the legacy *advance* metrics -- not ink bounds.
///
/// `w` is `gfx::oled::string_width_px`, the sum of per-character advances including the
/// trailing spacing after the last character (an off-by-one the gfx code keeps deliberately);
/// `h` is `text_height_for`, the height the font is selected by rather than the number of rows
/// it inks. Spec B section 6 describes `measure` as "ink bounds so centring is visually
/// correct"; advance metrics are used instead because every legacy centring and right-aligning
/// call site was laid out against them, and ink bounds would shift all of them. See the
/// task-3 report; spec section 6 needs amending to match.
///
/// Needs no canvas, so layout code can call it before drawing.
gfx::oled::Size measure(std::string_view s, gfx::oled::FontId font);

/// Draw \p s inside \p rect (frame-local coordinates), aligned per \p ts.
///
/// The alignment arithmetic is the legacy arithmetic, generalised from "the whole panel" to
/// "this rect": `Align::Left` is `Canvas::drawString` at the rect's left edge,
/// `Align::Centre` is `drawStringCentred` about the rect's centre, `Align::Right` is
/// `drawStringAlignRight` against the rect's right edge, `ts.shrink` is
/// `drawStringCentredShrinkIfNecessary` and `ts.truncate` is `drawStringCentered(startX ...)`,
/// both over the rect's width.
void text(OledFrame& f, gfx::oled::Rect rect, std::string_view s, TextStyle ts);

/// Draw a screen title across the top of the frame, with the separator rule under it.
/// `Canvas::drawScreenTitle`, minus the bezel arithmetic -- the frame's origin already
/// accounts for `visible_top`, so the two constants are plain layout numbers here.
///
/// The face is `f.style.title_font` (spec B section 6), not a fixed font: both shipped
/// profiles set `MetricBold9`, which is the legacy `kTextTitleSpacingX`/`kTextTitleSizeY`
/// pair, so today's pixels are unchanged, but a profile with a bigger panel can choose.
void title(OledFrame& f, std::string_view s, bool underline = true);

} // namespace deluge::ui::oled
