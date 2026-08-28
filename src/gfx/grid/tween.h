#pragma once
#include "gfx/grid/pad_grid.h"
#include <span>

namespace deluge::gfx::grid {
// Pure frame→frame transitions. The caller owns timing; `out` must share `dims()` with the inputs
// -- a precondition, checked with DELUGE_GFX_CONTRACT in each function (gfx/contract.h), because a
// mismatch indexes one of the grids past its own extent.

/// @brief Occupancy for one grid, in the layout `ui::core::PadMask` uses: one `uint32_t` per
///        column, bit `y` = row `y`.
///
/// Spelled as a span rather than that struct because `gfx` is a leaf and cannot include `ui/**`; a
/// caller holding a `PadMask` passes `std::span{mask.cols}.first(cols())`.
///
/// @note **Occupancy vs. colour.** The tweens that accumulate several source pads into one
///       destination pad (explode(), expand_collapse()) need to distinguish "black" from "nothing
///       here" -- not the same thing on device: legacy's `View::renderMacros` can paint an armed
///       clip's pad black on the flash-off frame while still marking it occupied, and that
///       occupancy reaches both animation paths this file ports. Legacy blends that black in
///       through `drawSquare`, darkening what is underneath.
/// @note **Empty span.** An empty span means "derive occupancy from the pad: non-black is
///       occupied", which is right for most callers -- just not the flash-off case above. A
///       non-empty span must be exactly `cols()` long (contract) and is authoritative.
/// @note **Scale.** A bitset can't carry legacy's 0..64 occupancy scale, but only nonzero-ness
///       matters for a *source* pad, so the bitset is exact there. Where expand_collapse() seeds
///       the destination mask from `to`, an occupied pad is taken as fully occupied (64) --
///       legacy's only precedent for a pre-drawn destination pad.
using OccupancyMask = std::span<const uint32_t>;

/// @brief Blend `from` toward `to`.
///
/// @param from Source frame at `t == 0`.
/// @param to   Source frame at `t == 65535`.
/// @param t    Blend parameter, 0..65535 (0 = `from`, 65535 = `to`).
/// @param out  Destination; must share `dims()` with `from` and `to`. Left fully dirty.
void fade(const PadGrid& from, const PadGrid& to, uint16_t t, PadGrid& out);

/// @brief Shift `from` sideways by `offset` columns.
///
/// @param from     Source frame.
/// @param incoming Fills the columns vacated by the shift, from its far edge.
/// @param offset   Columns to shift; positive moves content right. Clamped to
///                 [-main_cols, main_cols] -- a scroll by the full main-axis extent is already
///                 all-`incoming`, so larger magnitudes clamp to that rather than reading out of
///                 range.
/// @param out      Destination; must share `dims()` with `from` and `incoming`. Sidebar columns
///                 are untouched. Left fully dirty.
void scroll_horizontal(const PadGrid& from, const PadGrid& incoming, int32_t offset, PadGrid& out);

/// @brief Shift `from` vertically by `offset` rows.
///
/// Unlike scroll_horizontal(), the sidebar columns move with the rows too -- the legacy vertical
/// scroll shifts the whole image, mute/audition columns included.
///
/// @param from     Source frame.
/// @param incoming Fills the rows vacated by the shift, from its far edge.
/// @param offset   Rows to shift; positive moves content down. Clamped to [-rows, rows] for the
///                 same reason scroll_horizontal() clamps to `main_cols`.
/// @param out      Destination; must share `dims()` with `from` and `incoming`. Left fully dirty.
void scroll_vertical(const PadGrid& from, const PadGrid& incoming, int32_t offset, PadGrid& out);

/// @brief Grey out every main-grid pad of `in`; sidebar pads pass through untouched.
///
/// @param in         Source frame.
/// @param proportion Grey-out proportion, as `RGB::greyOut` -- a ~0..6500000 scale for a full
///                   grey-out, NOT the 0..65535 scale fade()'s `t` and `RGB::blend`'s index use.
/// @param out        Destination; must share `dims()` with `in`. Left fully dirty.
void grey_out(const PadGrid& in, int32_t proportion, PadGrid& out);

// ---------------------------------------------------------------------------------------------
// Sub-pixel ("smear") scroll.
//
// The device has no software equivalent: legacy shifts whole squares and hands the incoming
// column to the PIC, which interpolates the intermediate positions in hardware. That leaves the
// host simulator and any second display without the smooth part of the scroll, so this is a port
// of the SDK's software version (`deluge-grid-toolkit`'s smear-scroll animations): sample the two
// neighbouring source columns/rows and blend by the fractional part of the offset.
//
// Deviations from that SDK code, both deliberate:
//  * the SDK's blend function weights its *first* argument, so at a zero fractional offset it
//    would return the *next* sample -- the whole image off by one pixel, and an integral offset
//    would not scroll at all. The arguments are swapped here so that an integral `offset_px_16`
//    reproduces scroll_horizontal() / scroll_vertical() exactly (the legacy whole-square
//    behaviour, and the parity authority).
//  * the SDK smears every column; `smear_scroll_horizontal` smears only the main columns and
//    passes the sidebar through from `from`, matching its own sibling `scroll_horizontal`.
//    `smear_scroll_vertical` moves the sidebar with the rows, matching `scroll_vertical`.
//
/// @brief Sub-pixel ("smear") horizontal scroll: like scroll_horizontal(), but samples and blends
///        between the two neighbouring source columns instead of jumping a whole pad at a time.
///
/// @param from         Source frame.
/// @param incoming     Fills the vacated columns from its far edge; a source that lands outside
///                     both images reads black.
/// @param offset_px_16 16.16 fixed point in pads (65536 = one pad, positive = content moves
///                     right), clamped to +/-`main_cols` for the reason scroll_horizontal()
///                     clamps.
/// @param out          Destination; must share `dims()` with `from` and `incoming`. Sidebar
///                     columns are untouched. Left fully dirty.
void smear_scroll_horizontal(const PadGrid& from, const PadGrid& incoming, int32_t offset_px_16, PadGrid& out);

/// @brief Sub-pixel ("smear") vertical scroll; rows, positive = content moves down, clamped to
///        +/-`rows`. Sidebar columns move with the rows, matching scroll_vertical().
///
/// @param from         Source frame.
/// @param incoming     Fills the vacated rows from its far edge.
/// @param offset_px_16 16.16 fixed point in pads, as smear_scroll_horizontal().
/// @param out          Destination; must share `dims()` with `from` and `incoming`. Left fully
///                     dirty.
void smear_scroll_vertical(const PadGrid& from, const PadGrid& incoming, int32_t offset_px_16, PadGrid& out);

// ---------------------------------------------------------------------------------------------
/// @brief Horizontal zoom cross-fade between two frames at different zoom levels.
///
/// @note Ports legacy `PadLEDs::renderZoom` and the `renderZoomWithProgress` /
///       `renderZoomedSquare` pair it drives.
///
/// Zoom direction is derived from the parameters: zooming in when `new_zoom < old_zoom`. The zoom
/// magnitude is `abs(bit_width(new_zoom) - bit_width(old_zoom))` -- zoom levels are ticks-per-square
/// and always differ by a power of two, so the bit-width difference is the log2 step. The
/// more-zoomed-in frame is `to` when zooming in, otherwise `from`; the other parameter is the
/// more-zoomed-out frame.
///
/// `pin_square_from` doubles as the animation's fixed on-screen pin for the whole transition;
/// `pin_square_to` is the pin square for `to`'s zoom level alone. Passing them equal is the common
/// case (one pin square for the whole transition); at `t == 0` the output is `from` at native
/// scale, so the on-screen pin and `from`'s pin necessarily coincide there regardless.
///
/// Main columns only; sidebar columns are copied from `from`. All rows dirty, and every row
/// renders unconditionally (legacy could skip a row that wasn't currently animating; this
/// doesn't).
///
/// @warning Precondition: the derived zoom magnitude must be at most kMaxZoomMagnitude. Past that,
///          an intermediate shift amount exceeds the 32-bit operand width (undefined behaviour),
///          and if it folded to zero the following unsigned divide would divide by zero and trap.
///          Legacy is saved only by ARM's shift-masking and non-trapping `udiv`. A caller whose
///          zoom range can exceed this must step the transition or clamp the magnitude itself.
///
/// @note Uses ~576 bytes of stack for its per-column scratch (four `kMaxGridCols` int32 arrays
///       plus the fade array); worth re-checking against `-Wstack-usage` if the device render path
///       ever calls this from a deep frame.
///
/// @param from   The more-zoomed-out (or, when zooming in, more-zoomed-in) frame -- see above.
/// @param to     The complementary frame -- see above.
/// @param params Zoom parameters; see ZoomParams.
/// @param out    Destination; must share `dims()` with `from` and `to`. Left fully dirty.
constexpr int32_t kMaxZoomMagnitude = 5;
struct ZoomParams {
	/// Zoom level (ticks per square) `from` is at.
	int32_t old_zoom;
	/// Zoom level `to` is at; `new_zoom < old_zoom` means zooming in.
	int32_t new_zoom;
	/// Pin square (16.16) for `from`'s zoom level; also the fixed on-screen pin for the whole
	/// transition.
	int32_t pin_square_from;
	/// Pin square (16.16) for `to`'s zoom level.
	int32_t pin_square_to;
	/// Transition progress, 0..65535 (65536 would end the animation).
	uint16_t t;
};
void zoom(const PadGrid& from, const PadGrid& to, ZoomParams params, PadGrid& out);

// ---------------------------------------------------------------------------------------------
/// @brief Explode / implode the lit pads of `from` outward from (or inward toward) an origin
///        point.
///
/// @note Ports legacy `PadLEDs::renderExplodeAnimation`.
///
/// Each lit source pad is spread bilinearly across the (up to) four destination pads its scaled
/// position straddles, accumulated with the same occupancy-weighted blend as legacy's
/// `drawSquare`. `out` starts black, so the sidebar ends up black too.
///
/// @warning Preconditions: `main_cols` must be a power of two (the scaling divides by it with a
///          shift, to keep exact floor arithmetic); `out` must not alias `from` (`out` is cleared
///          before `from` is read); a non-empty `from_occupancy` must be `cols()` long.
///
/// @note `t` is the caller's transition progress, 0..65535; legacy's terminal `explodedness` of
///       exactly 65536 is one step past `t`'s range -- the caller renders that end state directly
///       from the destination frame instead of reaching it through this function.
/// @note Uses ~768 bytes of stack (a `kMaxGridRows` x `kMaxGridCols` destination occupancy array
///       plus two per-column arrays); see the stack note on zoom() above.
///
/// @param from   Source frame; only its lit (or `from_occupancy`-occupied) pads are drawn.
/// @param params Explode parameters; see ExplodeParams.
/// @param out    Destination; must share `dims()` with `from`, and must not alias it. Starts
///               black; left fully dirty.
struct ExplodeParams {
	/// X origin of the explosion, 16.16.
	int32_t origin_x_big;
	/// Y origin of the explosion, 16.16.
	int32_t origin_y_big;
	/// Width the explosion scales across, 16.16.
	int32_t width_big;
	/// false = explode (`explodedness = t`); true = implode (`65536 - t`).
	bool implode;
	/// Transition progress, 0..65535.
	uint16_t t;
	/// Occupancy of `from` (see OccupancyMask); empty keeps the non-black-is-occupied rule, so it
	/// does not disturb a caller that has no mask.
	OccupancyMask from_occupancy{};
};
void explode(const PadGrid& from, ExplodeParams params, PadGrid& out);

// ---------------------------------------------------------------------------------------------
/// @brief Rows converging into (or fanning out of) a single destination row.
///
/// @note Ports the shared core of legacy `renderInstrumentClipCollapseAnimation`, stripped of
///       everything that reads global UI state -- the greyed-out region past the clip length, the
///       triplet columns, the mute-square colour morph, and the keyboard sidebar morph all stay
///       with the caller, since they need song/UI-mode state a leaf can't reach. What is left is
///       the part every caller shares: interpolate each source row's position, then draw it into
///       the one or two rows it straddles, weighted by the fractional part.
///
/// Row `i` of `from` sits at display row `row_from + i` when fully expanded, and at its
/// destination row when fully collapsed -- `row_to_by_row[i]` if that span is non-empty, else the
/// scalar `row_to` for every row. `to` is the destination frame the moving rows are composited
/// over (as if a cleared image already had the destination content drawn into it); its occupied
/// pads start at full occupancy.
///
/// `t` runs 0..65535 forwards through the animation. Legacy's progress argument is *expandedness*
/// (65536 = fully at the source row, 0 = fully at the destination), which is why each caller flips
/// it for a collapse; `collapsing` reproduces that flip.
///
/// | legacy caller | `collapsing` | `row_from` | destination |
/// |---|---|---|---|
/// | clip expand/collapse, expanding | false | 0 | `row_to` = clip's on-screen row |
/// | clip expand/collapse, collapsing | true | 0 | `row_to` = clip's on-screen row |
/// | note-row expand/collapse | true | 0 | `row_to_by_row[i]` = per-note-row destination |
/// | audio-clip expand/collapse | matches caller | -- | -- (waveform path; see below) |
///
/// `from` is the expanded frame and `to` the frame it collapses onto; `t` is the raw transition
/// progress in every row of the table above.
///
/// @note Coverage limits inherited from the callers above:
///       - a destination above the display can't be expressed (`uint8_t` can't carry a negative
///         row), though a note row is allowed an off-screen destination as long as its source row
///         is on-screen; such a row must be dropped by the caller, or the field widened.
///       - off-screen source rows aren't modelled: legacy animates two extra rows above and below
///         the display; a `PadGrid` has exactly `rows` rows and no off-screen margin, so those
///         contribute nothing and `row_from` is 0 for every current caller.
///       - the audio-clip case shares only the progress flip -- its pixels come from the waveform
///         renderer rather than a stored frame, so it cannot use a frame-to-frame tween at all.
///
/// @warning Preconditions: `row_to < rows` and `row_from < rows`; a non-empty `row_to_by_row` is
///          `rows` long with every entry `< rows`; a non-empty `from_occupancy` / `to_occupancy`
///          is `cols()` long.
///
/// @param from   The expanded frame.
/// @param to     The frame `from` collapses onto (or expands away from).
/// @param params Expand/collapse parameters; see ExpandCollapseParams.
/// @param out    Destination; must share `dims()` with `from` and `to`. Left fully dirty.
struct ExpandCollapseParams {
	/// Source row `from` starts at (currently always 0; see coverage limits).
	uint8_t row_from;
	/// Destination row, used when `row_to_by_row` is empty.
	uint8_t row_to;
	/// false = expanding; true = collapsing (flips `t`'s sense internally).
	bool collapsing;
	/// Transition progress, 0..65535.
	uint16_t t;
	/// Per-row destinations; overrides `row_to` when non-empty. Legacy `animatedRowGoingTo[]`.
	std::span<const uint8_t> row_to_by_row{};
	/// Occupancy of `from` (legacy `occupancyMaskStore`) and of `to` (the destination seed).
	OccupancyMask from_occupancy{};
	OccupancyMask to_occupancy{};
};
void expand_collapse(const PadGrid& from, const PadGrid& to, ExpandCollapseParams params, PadGrid& out);

} // namespace deluge::gfx::grid
