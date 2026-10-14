#pragma once
#include "gfx/grid/rgb.h"
#include "ui/grid/frame.h"

#include <cstdint>
#include <span>

namespace deluge::ui::grid {

/// @brief The marker overlay a sample-marker editor paints *on top of* a waveform.
///
/// The pad half of `SampleMarkerEditor`, as a component: `SampleMarkerEditor::renderMarkerInCol`
/// (`gui/ui/sample_marker_editor.cpp:1058`) and `SampleMarkerEditor::renderColumn` (`:1268`),
/// ported unchanged onto `GridFrame`.
///
/// @note **Rendering only, and no model vocabulary.** Which sample position each marker sits at,
///       which column that lands on, whether the sample is reversed (legacy's `reverseRemap`,
///       `:83`) and therefore what colour a marker takes are all the caller's -- they read
///       `SampleHolder`, `SampleControls` and `MarkerType`, none of which `src/ui` may include
///       (`check_layer_rules.py`, `ui-includes-outside`). What arrives here is a bitmask of which
///       markers land on the column and a colour per marker index.
///
/// Everything paints through `GridFrame::pad`/`peek`, so the component is region-local: give it a
/// frame over the waveform's own columns, not a sidebar-inclusive root frame.

/// @brief The most markers one column can stack.
///
/// Legacy's `kNumMarkerTypes` (`definitions_cxx.hpp:785`) -- start, loop start, loop end, end. Spelled
/// as its own constant because `src/ui` may not include `definitions_cxx.hpp`; the caller's
/// `static_assert` is what ties the two together.
inline constexpr int32_t kMaxMarkers = 4;

/// @brief Tint rows `[y_start, y_end)` of column @p x toward @p colour.
///
/// `SampleMarkerEditor::renderMarkerInCol` (`sample_marker_editor.cpp:1089-1095`) exactly: each
/// row keeps a quarter of *the pad's red channel* as its underlying-waveform contribution and takes
/// three quarters of the marker colour.
///
/// @note Reading the **red channel only** is legacy's, not a simplification: the waveform under a
///       marker is drawn monochrome (`WaveformRenderer::drawColBar`'s `RGB::monochrome` arm), so
///       all three channels are equal there and red stands for the lot. Over a *coloured* waveform
///       the blend would be tinted by red alone -- which is what legacy does, and what the port
///       must keep doing.
///
/// @param f       Frame to paint into.
/// @param x       Column, frame-local.
/// @param y_start First row to tint.
/// @param y_end   One past the last row to tint.
/// @param colour  The marker's colour, already resolved by the caller.
/// @param dim     Paint it at `RGB::dim(3)` -- legacy's "this marker is not the selected one".
void marker_tint(GridFrame& f, int32_t x, int32_t y_start, int32_t y_end, RGB colour, bool dim);

/// @brief Which markers land on one column, and how they are stacked there.
struct MarkerStack {
	/// @brief Bit `i` set when marker `i` lands on this column.
	uint32_t active = 0;
	/// @brief Bit `i` set to skip marker `i` entirely -- legacy's `supressMask`, the blink's off phase.
	uint32_t suppress = 0;
	/// @brief Which marker index is the selected one, or negative for none.
	///
	/// Only affects how much of the column a marker gets when three share it (legacy gives the
	/// selected one two rows and the others one) and whether it is drawn dim.
	int32_t selected = -1;
};

/// @brief Paint one column's stacked markers, mirrored about the frame's middle.
///
/// `SampleMarkerEditor::renderColumn` (`sample_marker_editor.cpp:1268`). Markers are drawn from the
/// lowest index inwards, each taking a slice of the column's top half, and every slice is mirrored
/// into the bottom half -- so a column with one marker is a full-height bar and a column with four
/// is four stacked pairs. A column no marker lands on is left exactly as the waveform painted it.
///
/// @note **Two of the three slice heights are legacy's literals and do not scale with @p f's
///       height.** Only the one-marker case is `h / 2`; two, three and four markers use 2, 1/2 and
///       1 rows as legacy wrote them (`:1301-1317`). Every shipped board is eight rows tall, where
///       the literals and the fractions agree; a taller grid would stack the markers at the top
///       rather than spreading them, which is a deliberate transcription rather than a port
///       decision this component is entitled to make.
///
/// @param f       Frame to paint into.
/// @param x       Column, frame-local.
/// @param stack   Which markers are here and which is selected.
/// @param colours One colour per marker index; entries past `stack.active`'s highest bit are unused.
void marker_column(GridFrame& f, int32_t x, const MarkerStack& stack, std::span<const RGB> colours);

} // namespace deluge::ui::grid
