#pragma once
#include "gfx/grid/pad_grid.h"
#include "gfx/grid/tween.h"
#include "ui/grid/frame.h"

#include <cstdint>
#include <optional>

namespace deluge::ui::grid {

/// @brief How long a horizontal zoom slides, in ms: legacy's `kZoomSpeed` (`definitions_cxx.hpp:160`), which
///        `PadLEDs::recordTransitionBegin` scales to audio samples.
inline constexpr uint32_t kZoomSpeed = 110;

/// @brief How often a running zoom draws its next step, in ms: `PadLEDs::renderZoom` re-arms `MATRIX_DRIVER` at
///        `UI_MS_PER_REFRESH` (`board_config.h:53`). The settled frame is therefore the first step at or past
///        `kZoomSpeed`, not the moment the slide's time runs out.
inline constexpr uint32_t kZoomStepMs = 50;

/// @brief A horizontal zoom sliding between two frames of the same columns: legacy's `UI_MODE_HORIZONTAL_ZOOM`, as
///        `PadLEDs::renderZoom` draws it from `imageStore`.
///
/// The owner starts the slide with `begin_zoom` from what it showed, and paints the new view into the `to` it
/// answers; each render then hands its frame to `zoom_transition`, which paints the step and
/// answers whether the slide is still running. While it runs the owner paints nothing of its own over it.
///
/// The three grids are emplaced by `begin_zoom` at the dimensions of the frames it is given, so an owner that
/// never zooms holds none of them.
struct ZoomTransition {
	/// What was on the pads when the zoom happened, in the painting frame's local coordinates.
	std::optional<gfx::grid::PadGrid> from;
	/// The new view, as `from`.
	std::optional<gfx::grid::PadGrid> to;
	/// The step `gfx::grid::zoom` composes, copied onto the frame.
	std::optional<gfx::grid::PadGrid> out;
	/// The two levels, the pin square and the step; `t` is set per frame.
	gfx::grid::ZoomParams params{};
	/// When the slide began.
	uint32_t start_ms = 0;
	/// How long it slides.
	uint32_t length_ms = kZoomSpeed;
	/// Whether it is still sliding.
	bool active = false;
};

/// @brief Start a slide from @p from at @p now_ms (`WaveformBasicNavigator::zoom`'s `recordTransitionBegin`).
/// @param z The transition to (re)start; a running one is replaced.
/// @param from What the pads showed.
/// @param params The two levels, the pin square and the step; `t` is ignored.
/// @param now_ms The clock now.
/// @return The slide's `to`: black at `from`'s dimensions, for the caller to paint the new view into before the
///         first step.
gfx::grid::PadGrid& begin_zoom(ZoomTransition& z, const gfx::grid::PadGrid& from, gfx::grid::ZoomParams params,
                               uint32_t now_ms);

/// @brief Paint the running slide's step for @p f's clock over @p f's columns, as `PadLEDs::renderZoom` does.
///
/// The progress is `(now - start) * 65536 / length` (`getTransitionProgress`). Below 65536 the step is painted over
/// every pad of `f` that `from` covers and the next step is asked for `kZoomStepMs` on; at 65536 the slide ends,
/// nothing is painted and the caller paints its settled frame.
/// @param f The frame to paint, whose local corner is `from`'s (0, 0).
/// @param z The transition.
/// @return Whether the slide is still running (and painted).
bool zoom_transition(GridFrame& f, ZoomTransition& z);

} // namespace deluge::ui::grid
