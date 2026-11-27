#pragma once
#include "ui/gfx/oled/geometry.h"
#include "ui/widgets/oled/frame.h"

#include <cstdint>
#include <span>

/// @file
/// @brief Whole-timeline strips for the OLED: what the pads show only a window of (the arrangement, a clip's notes),
///        squashed to the panel's width, with the pads' window and the playhead marked on it.

namespace deluge::ui::widgets::oled {

/// @brief The pads' window and the playhead over a strip.
///
/// The window `[x, x + w)` (columns relative to @p r) as a dotted box over @p r's full height, its dots on even
/// offsets from the box's top-left corner; none when @p w <= 0. Then the playhead column @p playhead_x (relative to
/// @p r) inverted over @p r's height; none when negative. Shared by the strips here and by any scene marking a window
/// over `oled::waveform`.
void window_marks(OledFrame& f, gfx::oled::Rect r, int32_t x, int32_t w, int32_t playhead_x);

/// @brief One clip instance on an arrangement lane, in the caller's units (bars, say).
struct LaneSpan {
	int16_t lane;
	int32_t start;
	int32_t length;
	bool muted;
	bool selected = false;
};

/// @brief The whole arrangement in @p r.
///
/// Lane i is `r.h / lanes` rows tall from `r.y + i * (r.h / lanes)`, its spans one row shorter. A unit u is column
/// `r.x + u * r.w / units`; a span runs from its start's column to two before its end's (so back-to-back instances
/// keep a blank column between them), at least one column, clipped to @p r. Spans are filled; a muted span is a dotted
/// outline instead; when any span is selected it alone is filled and the others are solid outlines (the "clip held"
/// moment). Then `window_marks` for the window `[window_start, window_start + window_len)` and the playhead, in units
/// (playhead < 0: none).
void overview(OledFrame& f, gfx::oled::Rect r, int32_t lanes, int32_t units, std::span<const LaneSpan> spans,
              int32_t window_start, int32_t window_len, int32_t playhead);

/// @brief One note of a clip: its start and length in ticks, and its MIDI note.
struct StripNote {
	int32_t start;
	int32_t length;
	uint8_t pitch;
};

/// @brief A clip's notes squashed into @p r.
///
/// A tick t is column `r.x + t * r.w / length_ticks`; a pitch p is row `r.y + (high - p) * (r.h - 1) / (high - low)`
/// (the middle row when high == low), clamped to @p r. Each note is a 1-row line from its start's column to one
/// before its end's, at least one column, clipped to @p r. Then `window_marks` for the window and the playhead, in
/// ticks (playhead < 0: none).
void note_strip(OledFrame& f, gfx::oled::Rect r, int32_t length_ticks, std::span<const StripNote> notes, uint8_t low,
                uint8_t high, int32_t window_start, int32_t window_len, int32_t playhead);

} // namespace deluge::ui::widgets::oled
