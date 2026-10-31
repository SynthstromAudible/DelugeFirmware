#pragma once

/// @file
/// @brief Horizontal-menu column drawings that are neither a meter, a word nor an icon: transcriptions of legacy
///        `renderInHorizontalMenu` overrides and container renders, each drawn by `draw_column` from a `DrawColumn`.

#include "gfx/oled/geometry.h"
#include "ui/oled/frame.h"

#include <cstdint>

namespace deluge::ui::oled {

/// @brief A filter's state as `FilterContainer::getFilterValues` reads it (`filter/filter_container.h:122-146`).
struct FilterGraph {
	int32_t freq_raw = 0;  ///< The frequency column's value, 0..50.
	int32_t reso_raw = 0;  ///< The resonance column's value, 0..50.
	int32_t morph_raw = 0; ///< The morph column's value, 0..50.
	bool morphable = false;
	bool hpf = false;
};

/// @brief `ctx` type for `draw_column`.
struct DrawColumn {
	enum class Shape : uint8_t {
		/// `filter::FilterContainer::render`: the response curve across this column and the next.
		FilterGraph,
		/// `Number::drawSlider` at @ref norm.
		Slider,
	};
	Shape shape = Shape::Slider;
	FilterGraph filter;
	float norm = 0.0f;
	/// How many following columns the drawing also covers (`Column::span_next`).
	uint8_t span_next = 0;
};

/// @brief `Column::render` that draws `ctx` (a `DrawColumn`).
/// @param slot_frame Value-region frame to draw into.
/// @param focused    Whether this column currently has focus.
/// @param ctx        Must point at a `DrawColumn`.
void draw_column(OledFrame& slot_frame, bool focused, void* ctx);

/// @brief `filter::FilterContainer::render` (`filter/filter_container.h:35-117`): the filter's response, a rounded
///        outline round a dotted-fill curve whose knee follows the frequency, whose peak follows the resonance, and
///        which a morphable filter shifts; HPF is drawn as a fully morphed LPF.
/// @param f    The frame.
/// @param slot The container's slot (`SlotPosition`: start, width, height), frame-local.
/// @param g    The filter's state.
void filter_graph(OledFrame& f, gfx::oled::Rect slot, const FilterGraph& g);

} // namespace deluge::ui::oled
