#pragma once
#include "gfx/oled/font.h"
#include <cstdint>
#include <string_view>

namespace deluge::ui {

/// The most lines `wrap_lines` will ever produce: legacy's `TEXT_MAX_NUM_LINES`
/// (hid/display/oled.cpp:387), the fixed array size of `TextLineBreakdown`.
inline constexpr int32_t kWrapMaxLines = 8;

/// `TextLineBreakdown` (hid/display/oled.cpp:388-397) as a return value.
///
/// `lines[i]` are views *into the string handed to `wrap_lines`* -- no copying, no allocation --
/// so they are valid exactly as long as that string is. `widths[i]` is the line's width in
/// pixels at the font it was measured with, and `longest` is the widest of them (legacy's
/// `longestLineWidth`, which is what `popupText` sizes its box from).
///
/// Legacy's `longestLineLength` (the longest line in *characters*) is not carried: nothing in
/// the tree ever read it -- `popupText`, `drawPermanentPopupLookingText` and `consoleText` all
/// size from the pixel width -- and neither `widths` nor `longest` can be derived from it.
struct WrappedLines {
	std::string_view lines[kWrapMaxLines];
	int32_t widths[kWrapMaxLines];
	uint8_t count;
	int32_t longest;
};

/// Break \p text into lines that each fit \p max_width_px, at the font's own glyph metrics.
///
/// A port of `breakStringIntoLines` + `findLineSplitPoint` + `addLine` + `addCharacterToLine`
/// (hid/display/oled.cpp:398-568), unchanged in behaviour and moved out of the display driver:
/// it draws nothing, touches no canvas and has no state, so it belongs in ui-core and is
/// testable on its own.
///
/// The rules, all legacy's:
///
///  - A "word" ends at `'\n'`, `' '`, `'_'` or the end of the string (oled.cpp:445-447). A
///    trailing `' '` or `'_'` is *added to the line it ended* (:459-461), so a wrapped line
///    keeps its own trailing space and `widths[i]` includes it.
///  - A word that would overrun the line is moved whole to the next line, and the line is
///    committed at the width it had *before* that word (:544-545). A `' '` at the break point is
///    dropped rather than starting the next line (:552-554); any other character starts it.
///  - `'\n'` ends a line unconditionally, and the trailing inter-character spacing is subtracted
///    off first (:519-521, :543) so a line's width does not include a gap after its last glyph.
///  - **Overflow is silent truncation.** At `kWrapMaxLines` committed lines the routine returns
///    (:526-528, :548-550) and the rest of \p text is simply not represented -- no ellipsis, no
///    error. `count` is then exactly `kWrapMaxLines` and the caller cannot tell truncation from a
///    string that happened to fit in eight lines.
///
/// @param max_chars_per_line legacy's `TextLineBreakdown::maxCharsPerLine`. **Read by nothing.**
///        Every caller sets it to 19 (`popupText` oled.cpp:608, `drawPermanentPopupLookingText`
///        :572, `consoleText` :800) but no line of `breakStringIntoLines`, `findLineSplitPoint`,
///        `addLine` or `addCharacterToLine` ever reads the field -- the only cap that acts is
///        `max_width_px`. Kept in the signature so call sites can still spell
///        `kPopupMaxCharsPerLine` where legacy did, and so that honouring it later is a visible
///        change rather than a silent one; passing a different value changes nothing today, and
///        `popup_spec.cpp` pins that.
/// @param font selects the metrics; `text_height_of(font)` is the legacy `textHeight` that
///        `char_width_px` / `char_spacing_px` are keyed on. Callers that wrap for a popup pass
///        `FontId::Apple7` -- legacy's `kTextSpacingY`.
WrappedLines wrap_lines(std::string_view text, int32_t max_width_px, int32_t max_chars_per_line,
                        gfx::oled::FontId font);

} // namespace deluge::ui
