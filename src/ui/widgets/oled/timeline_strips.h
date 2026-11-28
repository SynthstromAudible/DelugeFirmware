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
/// offsets from the box's top-left corner, clipped to @p r (an edge off the strip is not drawn); none when @p w <= 0.
/// The strips below keep a window of any length at least one column wide. Then the playhead column @p playhead_x
/// (relative to
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
/// Lane i starts on row `y + i * h / lanes` of @p r and runs to the next lane's row; its spans are one row shorter, at
/// least one row (so lanes that outnumber the rows share rows rather than vanish). A unit u is column
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

/// @brief Where a clip's notes were and where quantizing at @p amount will put them.
///
/// A tick t is column `x + t * w / length_ticks` of @p r. On the top fifth of @p r (rounded, at least a row) each
/// note is a 1-px tick at its current column; on the bottom quarter (rounded, at least a row) a dotted line (every 2nd
/// row) marks every `grid_ticks`, and each note is a 3-px-wide block at `x0 + round((snap - x0) * amount)`, where
/// snap is the column of the nearest grid line (a tie goes to the later one). A 1-px line joins each tick's foot to
/// the top of its block. @p amount is clamped to 0..1; columns are clipped to @p r.
void quantize_graph(OledFrame& f, gfx::oled::Rect r, int32_t length_ticks, int32_t grid_ticks,
                    std::span<const int32_t> note_ticks, float amount);

/// @brief One automation node: its position in ticks, its value 0..127, and whether the lane ramps into it (else it
///        holds the previous value and steps).
struct LaneNode {
	int32_t pos;
	int32_t value;
	bool interpolate;
};

/// @brief A parameter's whole lane over a clip, in @p r.
///
/// A tick t is column `x + t * w / length_ticks` of @p r; value 0 is the rect's bottom row and 127 its top (values
/// clamped). Into each node after the first, a line from the previous node if the node interpolates, else held flat
/// at the previous value then a vertical step; before the first node and after the last, held flat at the nearest
/// node's value. No nodes: a dotted line (every 2nd column) at @p empty_value. Then `window_marks` for the pads'
/// window and the playhead, in ticks (playhead < 0: none), as `note_strip` does. Nodes are in position order.
void automation_lane(OledFrame& f, gfx::oled::Rect r, int32_t length_ticks, std::span<const LaneNode> nodes,
                     int32_t empty_value, int32_t window_start, int32_t window_len, int32_t playhead);

} // namespace deluge::ui::widgets::oled
