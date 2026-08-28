#pragma once
#include "gfx/grid/pad_grid.h"

namespace deluge::gfx::grid {
// Pure frame→frame transitions. The caller owns timing; `out` must share `dims()` with the inputs
// -- a precondition, checked with DELUGE_GFX_CONTRACT in each function (gfx/contract.h), because a
// mismatch indexes one of the grids past its own extent.

/// Blend `from` toward `to` at parameter `t` (0..65535: 0 = from, 65535 = to). Leaves out.dirty_rows()
/// fully set.
void fade(const PadGrid& from, const PadGrid& to, uint16_t t, PadGrid& out);

/// Shift `from` by `offset` columns (positive = content moves right); `incoming` fills the
/// vacated columns from its far edge. Sidebar columns are untouched. All rows dirty.
/// `offset` is clamped to [-main_cols, main_cols]: a scroll by the full main-axis extent is
/// already all-`incoming`, so larger magnitudes clamp to that rather than reading out of range.
void scroll_horizontal(const PadGrid& from, const PadGrid& incoming, int32_t offset, PadGrid& out);

/// Same for rows (positive = content moves down), but the sidebar columns move with the rows too
/// (the legacy vertical scroll shifts the whole image, mute/audition columns included) -- unlike
/// scroll_horizontal, which leaves the sidebar untouched. `offset` is clamped to [-rows, rows] for
/// the same reason scroll_horizontal clamps to main_cols.
void scroll_vertical(const PadGrid& from, const PadGrid& incoming, int32_t offset, PadGrid& out);

/// RGB::greyOut applied to every main-grid pad; proportion as in RGB::greyOut (a ~0..6500000
/// scale for a full grey-out, NOT the 0..65535 scale `fade`'s `t` and `RGB::blend`'s index use --
/// see existing callers in pad_leds.cpp/session_view.cpp/load_song_ui.cpp). Sidebar pads pass
/// through untouched.
void grey_out(const PadGrid& in, int32_t proportion, PadGrid& out);

// ---------------------------------------------------------------------------------------------
// Sub-pixel ("smear") scroll.
//
// The device has no software equivalent: legacy `PadLEDs::horizontal::renderScroll`
// (pad_leds.cpp:1375) shifts whole squares and then hands the incoming column to the PIC
// (`deluge_control_scroll_row`), which interpolates the intermediate positions in hardware. That
// leaves the host simulator and any second display without the smooth part of the scroll, so this
// is a port of the SDK's software version -- `deluge-grid-toolkit`
// `animations/smear_scroll.rs:52-77` (horizontal) and `:160-186` (vertical): sample the two
// neighbouring source columns/rows and blend by the fractional part of the offset.
//
// Deviations from that SDK code, both deliberate:
//  * the SDK passes `(color_a = floor, color_b = floor + 1, frac)` to its `blend_static`, whose
//    index weights the *first* argument -- so at `frac == 0` it returns `floor + 1`, i.e. the whole
//    image is off by one pixel and an integral offset does not scroll at all. The arguments are
//    swapped here so that an integral `offset_px_16` reproduces `scroll_horizontal` /
//    `scroll_vertical` exactly (which is the legacy whole-square behaviour, the parity authority).
//  * the SDK smears all 18 columns; `smear_scroll_horizontal` smears only the main columns and
//    passes the sidebar through from `from`, matching its own sibling `scroll_horizontal`.
//    `smear_scroll_vertical` moves the sidebar with the rows, matching `scroll_vertical`.
//
/// `offset_px_16` is 16.16 fixed point in pads (65536 = one pad, positive = content moves right),
/// clamped to +/-`main_cols` for the reason `scroll_horizontal` clamps. `incoming` fills the
/// vacated columns from its far edge; a source that lands outside both images reads black (the
/// SDK's `get_source_color` fallback). All rows dirty.
void smear_scroll_horizontal(const PadGrid& from, const PadGrid& incoming, int32_t offset_px_16, PadGrid& out);

/// Rows, positive = content moves down; clamped to +/-`rows`. Sidebar columns move with the rows.
void smear_scroll_vertical(const PadGrid& from, const PadGrid& incoming, int32_t offset_px_16, PadGrid& out);

// ---------------------------------------------------------------------------------------------
/// Horizontal zoom cross-fade -- port of `PadLEDs::renderZoom` (pad_leds.cpp:1173) *and* the
/// `renderZoomWithProgress` (:1216) / `renderZoomedSquare` (:1338) pair it drives, which legacy
/// splits only because two callers share the latter.
///
/// The legacy globals become parameters:
///
/// | legacy | here |
/// |---|---|
/// | `zoomingIn` (timeline_view.cpp:323) | `new_zoom < old_zoom` |
/// | `zoomMagnitude` (same line; the abs log2 step) | `abs(bit_width(new) - bit_width(old))` |
/// | `imageStore[0]` -- inner, the more-zoomed-*in* frame | `to` when zooming in, else `from` |
/// | `imageStore[kDisplayHeight]` -- outer | `from` when zooming in, else `to` |
/// | `zoomPinSquare[y]` (timeline_view.cpp:54; one value per frame) | `pin_square_from` |
/// | `zoomPinSquareInner` (:1238, aliased to the above) | the inner image's pin, per the swap |
/// | `zoomPinSquareOuter` (:1239, likewise) | the outer image's pin, per the swap |
/// | `transitionProgress` (0..65535; 65536 ends the animation) | `t` |
/// | `transitionTakingPlaceOnRow[]` | absent -- every row renders |
///
/// Legacy passes one pin square for all three roles, so passing `pin_square_from ==
/// pin_square_to` is the parity case; they are separated here because the on-screen pin and the
/// two image-local pins are conceptually distinct. The on-screen pin is `pin_square_from` (at
/// `t == 0` the output is `from` at native scale, so the two coincide). Pin squares are 16.16.
///
/// Main columns only, as in legacy (`innerImageRightEdge == kDisplayWidth`); sidebar columns are
/// copied from `from`. All rows dirty.
struct ZoomParams {
	int32_t old_zoom;
	int32_t new_zoom;
	int32_t pin_square_from;
	int32_t pin_square_to;
	uint16_t t;
};
void zoom(const PadGrid& from, const PadGrid& to, ZoomParams params, PadGrid& out);

// ---------------------------------------------------------------------------------------------
/// Explode / implode about an origin -- port of `PadLEDs::renderExplodeAnimation`
/// (pad_leds.cpp:645). Each lit source pad is spread bilinearly across the (up to) four
/// destination pads its scaled position straddles, accumulated with the legacy `drawSquare`
/// (util/functions.h:337) occupancy blend. `out` starts black, as legacy's `memset(image, ...)`
/// does; the sidebar therefore ends black too.
///
/// `origin_x_big`/`width_big` are legacy's `explodeAnimationXStartBig`/`explodeAnimationXWidthBig`
/// and `origin_y_big` is `explodeAnimationYOriginBig` -- all 16.16. `implode` distinguishes the
/// two branches of the caller at pad_leds.cpp:956-957 (`explodeAnimationDirection`): explode uses
/// `explodedness = t`, implode uses `65536 - t`. `t` is that caller's `progress`, 0..65535;
/// legacy's terminal `explodedness` of exactly 65536 is one step past `t`'s range, and the caller
/// renders that end state from the destination frame anyway (pad_leds.cpp:872-880).
///
/// Preconditions: `main_cols` is a power of two -- legacy divides by the main width with
/// `>> kDisplayWidthMagnitude` and this keeps that exact (floor) arithmetic -- and `out` does not
/// alias `from`, since `out` is cleared before `from` is read.
struct ExplodeParams {
	int32_t origin_x_big;
	int32_t origin_y_big;
	int32_t width_big;
	bool implode;
	uint16_t t;
};
void explode(const PadGrid& from, ExplodeParams params, PadGrid& out);

// ---------------------------------------------------------------------------------------------
/// Rows converging into (or fanning out of) a single destination row -- the shared core of
/// `renderInstrumentClipCollapseAnimation` (pad_leds.cpp:344), stripped of everything that reads a
/// global: the greyed-out region past the clip length, the triplet columns, the mute-square colour
/// morph and the keyboard sidebar morph all stay in `pad_leds.cpp`, because they need
/// `currentSong` / `instrumentClipView` / the UI mode. What is left is the part the three callers
/// share: interpolate every source row's position, then draw it into the one or two rows it
/// straddles with `drawSquare`, weighted by the fractional part.
///
/// Row `i` of `from` sits at display row `row_from + i` when fully expanded and at `row_to` when
/// fully collapsed. `to` is the destination frame the moving rows are composited over (legacy
/// cleared `image` and drew the Session pads into it first, which is the same thing); pads of `to`
/// that are lit start at full occupancy, exactly as legacy's `occupancyMask[row][kDisplayWidth] =
/// 64` (pad_leds.cpp:354) did.
///
/// `t` runs 0..65535 forwards through the animation. Legacy's `progress` argument is
/// *expandedness* (65536 = at `animatedRowGoingFrom`, 0 = at `animatedRowGoingTo`), which is why
/// each caller flips it; `collapsing` reproduces that flip.
///
/// | legacy caller | `collapsing` | `row_from` | `row_to` |
/// |---|---|---|---|
/// | `renderClipExpandOrCollapse`, expanding (:1101) | false | 0 | `clipPlaceOnScreen` |
/// | `renderClipExpandOrCollapse`, collapsing (:1101) | true | 0 | `clipPlaceOnScreen` |
/// | `renderNoteRowExpandOrCollapse` (:1155) | true | 0 | the shared `yDisplayTo` |
/// | `renderAudioClipExpandOrCollapse` (:1069) | matches | -- | -- (waveform path) |
///
/// `from` is the expanded frame and `to` the frame it collapses onto; `t` is the raw transition
/// progress in every row of that table. `clipPlaceOnScreen` is set at `session_view.cpp:2625`; the
/// two legacy callers that flip `progress` themselves do so at :1096 and :1163.
///
/// Coverage limits -- the first two inherent to the scalar parameters this signature specifies:
///  * per-row destinations. `renderNoteRowExpandOrCollapse` sets `animatedRowGoingTo[i]`
///    per note row (`instrument_clip_view.cpp:5781`); one `row_to` covers it only when every
///    animated row shares a destination, which is the clip-collapse case.
///  * off-screen source rows. Legacy animates `kDisplayHeight + 2` rows, the extra two being the
///    off-screen rows above and below (`session_view.cpp:2623-2626`, `animatedRowGoingFrom[y] =
///    y - 1`); a `PadGrid` has exactly `rows` rows and no off-screen margin, so `row_from` is 0
///    for all three callers and those two rows contribute nothing.
///  * `renderAudioClipExpandOrCollapse` shares only the progress flip. Its pixels come from
///    `waveformRenderer.renderOneColForCollapseAnimation`, not from a stored frame, so it cannot
///    use a frame-to-frame tween at all.
struct ExpandCollapseParams {
	uint8_t row_from;
	uint8_t row_to;
	bool collapsing;
	uint16_t t;
};
void expand_collapse(const PadGrid& from, const PadGrid& to, ExpandCollapseParams params, PadGrid& out);

} // namespace deluge::gfx::grid
