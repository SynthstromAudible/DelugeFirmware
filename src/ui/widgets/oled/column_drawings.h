#pragma once

/// @file
/// @brief Horizontal-menu column drawings that are neither a meter, a word nor an icon: transcriptions of legacy
///        `renderInHorizontalMenu` overrides and container renders, each drawn by `draw_column` from a `DrawColumn`.

#include "ui/gfx/oled/geometry.h"
#include "ui/widgets/oled/frame.h"

#include <cstdint>

namespace deluge::ui::widgets::oled {

/// @brief A filter's state as `FilterContainer::getFilterValues` reads it
/// (`origin/main:src/deluge/gui/menu_item/filter/filter_container.h`).
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
		/// `voice::Portamento::renderInHorizontalMenu` (`origin/main:src/deluge/gui/menu_item/voice/portamento.h`) at
		/// @ref norm.
		Glide,
		/// `unison::Detune::renderInHorizontalMenu` (`origin/main:src/deluge/gui/menu_item/unison/detune.h`) at @ref
		/// norm.
		Detune,
		/// `osc::PulseWidth::renderInHorizontalMenu` (`origin/main:src/deluge/gui/menu_item/osc/pulse_width.h`) at @ref
		/// raw, 0..50.
		PulseWidth,
		/// `delay::Amount::renderInHorizontalMenu` (`origin/main:src/deluge/gui/menu_item/delay/amount.h`) at @ref raw:
		/// the bar clamped to 0..24, and an inverted "!" above it.
		DelayAmount,
		/// `osc::AudioRecorder::renderInHorizontalMenu` (`origin/main:src/deluge/gui/menu_item/osc/audio_recorder.h`)
		/// for source @ref raw (0 or 1): its look follows the focus and `Style::highlighting`.
		Recorder,
		/// `sample::LoopPoint::renderInHorizontalMenu` (`origin/main:src/deluge/gui/menu_item/sample/loop_point.cpp`)
		/// for the start marker.
		LoopStart,
		/// The same for the end marker.
		LoopEnd,
		/// `audio_clip::SampleMarkerEditor::renderInHorizontalMenu`
		/// (`origin/main:src/deluge/gui/menu_item/audio_clip/sample_marker_editor.cpp`).
		ClipEnd,
	};
	Shape shape = Shape::Slider;
	FilterGraph filter;
	float norm = 0.0f;
	/// The value, for the drawings that read it rather than @ref norm.
	int32_t raw = 0;
	/// How many following columns the drawing also covers (`Column::span_next`).
	uint8_t span_next = 0;
};

/// @brief `Column::render` that draws `ctx` (a `DrawColumn`).
/// @param slot_frame Value-region frame to draw into.
/// @param focused    Whether this column currently has focus.
/// @param ctx        Must point at a `DrawColumn`.
void draw_column(OledFrame& slot_frame, bool focused, void* ctx);

/// @brief `filter::FilterContainer::render` (`origin/main:src/deluge/gui/menu_item/filter/filter_container.h`): the
///        filter's response, a rounded outline round a dotted-fill curve whose knee follows the frequency, whose peak
///        follows the resonance, and which a morphable filter shifts; HPF is drawn as a fully morphed LPF.
/// @param f    The frame.
/// @param slot The container's slot (`SlotPosition`: start, width, height), frame-local.
/// @param g    The filter's state.
void filter_graph(OledFrame& f, gfx::oled::Rect slot, const FilterGraph& g);

/// @brief What an envelope's graph draws.
struct EnvelopeGraph {
	enum class Mode : uint8_t { ADSR, AHD, AR, MPE };
	int32_t attack = 0;  ///< The four segments' values, 0..50.
	int32_t decay = 0;   ///< An AHD's hold.
	int32_t sustain = 0; ///< An AR's or MPE's level.
	int32_t release = 0; ///< An AHD's decay.
	Mode mode = Mode::ADSR;
	float attack_k = 4.f;  ///< Each stage's curve, the octaves it falls: 0 a straight line.
	float decay_k = 8.f;   ///< The decay's.
	float release_k = 8.f; ///< The release's.
};

/// @brief `envelope::EnvelopeMenu::renderMenuItems` (`origin/main:src/deluge/gui/menu_item/envelope/envelope_menu.h`):
///        the envelope's shape for its mode, its stages bent as its curve bends them, with dotted stage lines and a
///        square on each segment's point, the focused one inverted.
/// @param panel    A whole-panel frame (`panel_frame`): legacy draws in canvas coordinates.
/// @param envelope The envelope.
/// @param selected The focused segment, 0..3.
void envelope_graph(OledFrame& panel, const EnvelopeGraph& envelope, int32_t selected);

/// @brief `eq::EqMenu::renderMenuItems` (`origin/main:src/deluge/gui/menu_item/eq/eq_menu.h`): the shelving curve with
///        reference lines and a handle on each of bass, bass frequency, treble frequency and treble, the focused one
///        inverted.
/// @param panel       A whole-panel frame (`panel_frame`).
/// @param bass        Bass gain, 0..1 (value / 50).
/// @param treble      Treble gain, 0..1.
/// @param bass_freq   Bass frequency, 0..1.
/// @param treble_freq Treble frequency, 0..1 (value clamped to 0..32, / 32).
/// @param selected    The focused handle, 0..3 in that order.
void eq_graph(OledFrame& panel, float bass, float treble, float bass_freq, float treble_freq, int32_t selected);

} // namespace deluge::ui::widgets::oled
