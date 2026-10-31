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

/// @brief `envelope::EnvelopeMenu::renderMenuItems` (`envelope/envelope_menu.h:34-91`): the ADSR shape with dotted
///        stage lines and a square on each stage, the focused one inverted.
/// @param panel    A whole-panel frame (`panel_frame`): legacy draws in canvas coordinates.
/// @param attack   The four segments' values, 0..50.
/// @param decay
/// @param sustain
/// @param release
/// @param selected The focused segment, 0..3.
void envelope_graph(OledFrame& panel, int32_t attack, int32_t decay, int32_t sustain, int32_t release,
                    int32_t selected);

/// @brief `eq::EqMenu::renderMenuItems` (`eq/eq_menu.h:32-111`): the shelving curve with reference lines and a handle
/// on
///        each of bass, bass frequency, treble frequency and treble, the focused one inverted.
/// @param panel       A whole-panel frame (`panel_frame`).
/// @param bass        Bass gain, 0..1 (value / 50).
/// @param treble      Treble gain, 0..1.
/// @param bass_freq   Bass frequency, 0..1.
/// @param treble_freq Treble frequency, 0..1 (value clamped to 0..32, / 32).
/// @param selected    The focused handle, 0..3 in that order.
void eq_graph(OledFrame& panel, float bass, float treble, float bass_freq, float treble_freq, int32_t selected);

} // namespace deluge::ui::oled
