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

/// @brief A distortion page's four effects, each 0..1 (the menu index / 50).
struct DistortionGraph {
	float saturation = 0.f;
	float bitcrush = 0.f;
	float decimation = 0.f;
	float wavefold = 0.f;
};

/// @brief One cycle across the page body, clean (dotted, a pixel on every even column) and as it comes out (solid).
///
/// With `b = page_body(panel)`, the centre row `cy = b.center_y()`, the amplitude `amp = b.height() / 2 - 1` and
/// `u = (x - b.left) / (b.right - b.left)`, the output at column x is the sine at the column decimation holds,
/// `b.left + floor((x - b.left) / hold) * hold` with `hold = 1 + round(decimation * 10)`, then saturated
/// (`tanh(g v) / tanh(g)`, `g = 1 + 4 * saturation`), folded at `t = 1 - 0.6 * wavefold` (`|v| > t` becomes
/// `sign(v) * (2t - |v|)`), and crushed to `n = round(2 + (1 - bitcrush) * 14)` levels a side when bitcrush is above 0.
/// It is drawn as a horizontal run while it holds and a vertical at each change. Handles in column order:
/// SATURATION at u = 0.25, BITCRUSH at 0.10, DECIMATION at 0.58, WAVEFOLD at 0.75, each on the output's row. Values
/// outside 0..1 are clamped.
/// @param panel   The frame a menu hands a page drawing.
/// @param d       The four effects.
/// @param focused The focused column, 0..3; its handle is drawn inverted, on top.
void distortion_graph(OledFrame& panel, const DistortionGraph& d, int32_t focused);

/// @brief Eight steps of swing across the page body: each straight place a dotted guide, each step a 2-px-wide
///        solid line from `b.top + 5` to `b.bottom`, the odd steps pushed late.
///
/// With `b = page_body(panel)` and `pitch = (b.right - b.left) / 8`, step i's straight place is
/// `xs(i) = b.left + 2 + i * pitch`; an odd step sits `round((amount - 50) / 50 * pitch)` later. Handles in column
/// order: AMOUNT on step 1's top, INTERVAL on step 2's top.
/// @param panel   The frame a menu hands a page drawing.
/// @param amount  The song's swing, 1..99 with 50 straight; clamped.
/// @param focused The focused column, 0..1; its handle is drawn inverted, on top.
void swing_graph(OledFrame& panel, int32_t amount, int32_t focused);

} // namespace deluge::ui::widgets::oled
