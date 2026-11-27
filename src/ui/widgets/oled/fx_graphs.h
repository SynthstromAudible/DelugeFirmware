#pragma once
#include "ui/widgets/oled/frame.h"

#include <cstdint>

/// @file
/// @brief Page drawings for effect pages whose every column is a handle on the drawing: a sidechain duck, a
///        distortion, a swing. Drawn with the page kit (`ui/widgets/oled/page_drawing.h`) in the frame a menu hands a
///        page drawing, so they sit where the envelope and EQ pages draw.

namespace deluge::ui::widgets::oled {

/// @brief One sidechain duck, in menu terms.
struct DuckGraph {
	float volume;  ///< -1..1: negative ducks (the gain falls), positive pumps (it rises); the magnitude is the depth.
	float shape;   ///< 0..1: how hard the recovery bends; 0 is a straight line.
	float attack;  ///< 0..1: the fall's width (the menu index / 50).
	float release; ///< 0..1: the recovery's width (the menu index / 50).
};

/// @brief Which column each of the duck's handles stands for; -1 draws no handle for it. A page shows a handle only
///        for the columns it has (sidechain page 2 has ATTACK and RELEASE only).
struct DuckHandles {
	int8_t volume = -1;
	int8_t shape = -1;
	int8_t attack = -1;
	int8_t release = -1;
};

/// @brief A sidechain duck across the page body: the trigger as a dotted guide, the gain falling over the attack to
///        the depth (rising, for a positive volume), then recovering to rest over the release, bent by the shape.
///
/// Columns, from the body `b = page_body(panel)`: the trigger `xt = b.left + 4`; the attack's end
/// `xa = xt + round(attack * (b.right - b.left) / 4)`; the release's end `xr = xa + round(release * (b.right - xa -
/// 4))`. The rest level is 1 and the peak `1 - |volume|` when the volume ducks; the rest is `1 - |volume|` and the peak
/// 1 when it pumps. The peak row is dotted from the body's left to `xa`. Handles: volume on the peak at the body's
/// left, attack at `(xa, peak)`, shape on the recovery half way, release at `(xr, rest)`. Values outside their ranges
/// are clamped.
/// @param panel   The frame a menu hands a page drawing.
/// @param d       The duck.
/// @param h       The column each handle stands for.
/// @param focused The focused column; its handle is drawn inverted, on top.
/// @param phase   0..1 draws a 3x3 dot on the gain at that fraction of the body's width (the duck playing); < 0 none.
void duck_graph(OledFrame& panel, const DuckGraph& d, DuckHandles h, int32_t focused, float phase = -1.f);

} // namespace deluge::ui::widgets::oled
