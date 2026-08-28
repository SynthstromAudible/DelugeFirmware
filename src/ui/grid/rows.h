#pragma once
#include "gfx/grid/pad_grid.h"
#include "gfx/grid/palette.h"
#include "gfx/grid/rgb.h"
#include "ui/core/response.h"
#include "ui/grid/frame.h"

#include <cstdint>
#include <span>

namespace deluge::ui::grid {

/// The widest row `row()` / `clip_row()` will paint in one call, and the size of `clip_row`'s
/// scratch buffers. Sized to `gfx::grid::kMaxGridCols` -- the widest grid the gfx layer supports at
/// all, where it caps `PadGrid`'s storage -- so the cap can never truncate a real frame:
/// `f.bounds().w` is at most the grid's column count, which is at most this. The `std::min` against
/// it in the implementation is belt-and-braces against a future wider `PadGrid`, not a live limit.
/// A Deluge row is 18 pads wide (16 main + 2 sidebar, one coordinate space -- see ui/grid/frame.h),
/// so the buffers cost 128 bytes of stack for a frame that uses 18 of them.
constexpr uint8_t kMaxRowCells = gfx::grid::kMaxGridCols;

/// @brief Paint one row of already-decided colours, hit-testing each pad in the same pass.
///
/// Stands in for the many `renderRow`-shaped legacy routines that each fill a run of `RGB`s into
/// one image row: the caller passes that same run as a span and the frame does the writing, so the
/// row is hit-tested in the same pass that paints it.
///
/// Cells past `f.bounds().w` -- or past `kMaxRowCells` -- are dropped, and a span shorter than the
/// frame leaves the remaining columns untouched (it does *not* black them out; legacy's clearing is
/// the caller's own `memset`, and `clip_row` below does it itself).
///
/// @note The `Response` is the row's, not one pad's, and encodes *which* column was pressed in
///       `Response::index`: the pressed column's x, or -1 for no press. `Response::velocity` is
///       that press's own pad velocity -- the two are separate fields, so a row reports both the
///       column and how hard it was hit. When more than one pad in the row is pressed in the same
///       tick the *leftmost* is reported; `pressed`, `released` and `held` are the row's OR, so a
///       caller can still tell "something in this row is down". A component that needs every press
///       of the tick reads `f.local_events()`, or `f.first_press_in()` for just the first inside a
///       rectangle.
///
/// @param f     Frame to paint into.
/// @param y     Row to paint, in frame coordinates.
/// @param cells Colours to paint, left to right.
/// @return The row's hit-test result; see the @note above.
Response row(GridFrame& f, int32_t y, std::span<const RGB> cells);

/// @brief Mirrors legacy `FillMode`, restated here because this layer may depend only on `ui/**`,
///        `gfx/**` and the standard library. Same three states in the same order.
enum class NoteFill : uint8_t { Off, NotFill, Fill };

/// @brief One note, windowed into columns by the caller.
///
/// @note Ports legacy `NoteRow::renderRow`, which walks squares, asks the note tree what note
///       covers each square's start position, and compares the note's `pos` with the square's
///       start. That search is model work; by the time a `NoteCell` exists the comparison has
///       already been made, and the legacy outcomes survive as the fields below.
struct NoteCell {
	/// The column the note starts in: legacy's `xDisplay` for the square whose start position the
	/// note's `pos` falls in or on.
	///
	/// @note **Signed, and routinely negative.** A note that began before the window -- the
	///       ordinary case for a scrolled row -- has a negative start column, and only its tail is
	///       visible; the start colour is not painted at all. That is legacy's continuing-note
	///       arm, which paints `rowTailColour` on any square the note's end is still beyond,
	///       whether or not its beginning is on screen.
	///
	/// @note A note whose tail *wraps* around the end of the loop is expressed as a **second cell**
	///       for the same note, with `start_col` shifted back by the loop's length in columns:
	///       `start_col - loop_cols`, matching legacy's own wrap arithmetic in column units. That
	///       cell is necessarily negative (a wrapped note started inside the loop), so it paints
	///       tail columns at the left of the row and never a start -- again, legacy's behaviour. No
	///       extra field is needed for the wrap: the subtraction is the caller's, because the
	///       caller is the one that knows the loop length and the zoom.
	int16_t start_col = 0;
	/// How many columns the note occupies *including* `start_col`, i.e. `len_cols - 1` tail
	/// columns follow the start. 0 paints nothing. Counted from `start_col`, so a note that began
	/// two columns left of the window with three visible tail columns is `{-2, 5}`.
	uint8_t len_cols = 0;
	/// The note's velocity, used only when `starts_within` is false.
	uint8_t velocity = 0;
	/// The note starts strictly *after* the square's start position rather than exactly on it. The
	/// start column then gets the blur colour and the velocity plays no part -- legacy's way of
	/// saying "this note begins somewhere inside this square, and one pad cannot show where".
	bool starts_within = false;
	/// Legacy's `allowNoteTails`, per note: false paints only the start column, however long the
	/// note is. Zoomed-out clip rows and non-tail instruments pass false.
	bool tail = false;
	/// The note's fill-mode state. Consulted only while `ClipRowData::fill_mode_active`.
	NoteFill fill = NoteFill::Off;
	/// True when the note has a non-default probability, iterance, or fill setting -- the
	/// settings the note editor marks. Consulted only while `ClipRowData::note_editor`.
	bool non_default_params = false;
};

/// @brief One clip/note row's worth of already-windowed model data.
///
/// The three colours are legacy's `rowColour` / `rowTailColour` / `rowBlurColour` triple, which
/// every caller of `NoteRow::renderRow` derives from the row's base colour with `RGB::forTail()`
/// and `RGB::forBlur()` before calling. They stay three separate fields, not one colour plus
/// derivations, because several views substitute their own: the performance view's pastel tail
/// colours, and `InstrumentClip::renderAsSingleRow`, whose blur colour is the *main* colour when
/// `allowBlur` is false.
struct ClipRowData {
	/// Legacy `rowColour`: scaled by velocity on a note that starts exactly on a square.
	RGB colour;
	/// Legacy `rowTailColour`: a note continuing through a square.
	RGB tail_colour;
	/// Legacy `rowBlurColour`: a note starting somewhere within a square.
	RGB blur_colour;
	/// The notes visible in this window.
	///
	/// @note Two notes that start in the *same* column resolve to whichever comes last in this
	///       span, so the caller must keep the model's order: legacy resolves that square with
	///       the note with the greatest `pos` below the square's end, i.e. the later one. Order is
	///       otherwise irrelevant -- a start always beats a tail, whichever cell each came from
	///       (see `clip_row`).
	std::span<const NoteCell> notes;
	/// The playhead column, or -1 for none. Painted last, over any note.
	int32_t playhead_col = -1;
	/// Legacy's `overwriteExisting`.
	///
	/// `true` (the default, and what a view rendering one note row per grid row passes) clears
	/// every column first, so the row shows this data and nothing else.
	///
	/// `false` paints *only* the columns this call's notes and playhead touch and leaves the rest
	/// exactly as they were -- which is how legacy composites many note rows into one grid row:
	/// `InstrumentClip::renderAsSingleRow` loops over the clip's note rows and calls `renderRow`
	/// once each with `overwriteExisting = false` and a **different** colour triple per row, which
	/// is the body of `SessionView::renderRow`. A scene reproduces that by calling `clip_row` once
	/// per note row, first call `overwrite_existing = true` (or a `row()` of black) and the rest
	/// `false`.
	///
	/// @note With `false`, the returned `Response` covers only the columns this call painted: an
	///       untouched column is not written *and not hit-tested*, because painting and
	///       hit-testing are the same call. The composited row's presses come from the pass that
	///       painted the column, or from `f.local_events()`.
	bool overwrite_existing = true;
	/// Whether fill mode is active for the song.
	bool fill_mode_active = false;
	/// Whether the sound editor's note editor is open.
	bool note_editor = false;
	/// The colour a fill note takes while fill mode is active. A field rather than a palette read
	/// so the component stays pure.
	RGB blue = gui::colours::blue;
	/// The colour a not-fill note takes while fill mode is active.
	RGB red = gui::colours::red;
	/// The colour a note with non-default parameters takes in the note editor.
	RGB yellow = gui::colours::yellow;
};

/// @brief The playhead colour, substituted for a square's own colour while the cursor sits on it
///        and the row is neither muted nor recording linearly (which get their own flash colours).
///
/// Legacy paints the playhead in the *driver*, after the view has rendered, which is why
/// `clip_row`'s colour rules have no playhead branch of their own; a separate `tick_squares`
/// component handles the overlay across a whole grid, and `ClipRowData::playhead_col` is the
/// one-row shorthand for the same pixel. The muted and recording variants belong to
/// `tick_squares`, which knows the per-row mute/record state; this call has only a column.
constexpr RGB kPlayheadColour = RGB{130, 120, 130};

/// @brief Paint one note row: legacy `NoteRow::renderRow`'s colour rules over a caller-windowed
///        `ClipRowData`.
///
/// Per column, in legacy's own order of precedence:
///
/// 1. black -- the `overwrite_existing` clear; skipped, along with the paint itself, when
///    `overwrite_existing` is false;
/// 2. a note continuing through the column, `tail == true` -> `tail_colour`. This is also the
///    whole of what a note with a negative `start_col` draws;
/// 3. a note starting in the column, `starts_within` -> `blur_colour`;
/// 4. a note starting in the column, exactly on it ->
///    `colour.adjustFractional((65 + v + v / 2) << 8, 255 << 8)` -- "colour * (0.25 + 0.75 *
///    velocity) in fixed point", as the legacy comment puts it;
/// 5. that start colour is then recoloured: while `fill_mode_active`, a `Fill` note becomes
///    `blue` and a `NotFill` note `red`; otherwise, while `note_editor`, a note with
///    `non_default_params` becomes `yellow`. Legacy gates this on the pixel being neither the
///    row's blur nor its tail colour, which is why blur and tail columns are never recoloured --
///    and why a start colour that *happens to equal* one of them is not either. Ported literally,
///    comparison and all;
/// 6. `playhead_col` -> `kPlayheadColour`.
///
/// Legacy gets this precedence from an if/else-if chain per square, where the note found at the
/// square is tested for "starts within" and "starts on" before the wrapped/continuing case. Two
/// passes reproduce it without a per-column search: tails first, then starts. So where one note's
/// tail crosses another's start, the start wins -- as it does in legacy, whose search returns the
/// *later* note for that square.
///
/// The frame's full width is painted, so a caller that wants only the main pads opens
/// `f.region({0, 0, 16, h})` first; the sidebar columns of a root frame would otherwise be
/// cleared to black. `y` is the row in frame coordinates.
///
/// @note Legacy's `occupancyMask` writes are deliberately dropped: the mask exists for
///       `drawUndefinedArea` and the greyout, which is instead a separate `greyout(f, GreyoutMask,
///       amount)` component, applied after painting. Nothing here needs to remember which pads a
///       note lit.
///
/// The `Response` encodes the pressed column in `index` and its velocity in `velocity`, exactly as
/// `row()` does, subject to `overwrite_existing` (see that field).
///
/// @param f    Frame to paint into.
/// @param y    Row to paint, in frame coordinates.
/// @param data The row's colours, notes and flags; see ClipRowData.
/// @return The row's hit-test result; see the @note above.
Response clip_row(GridFrame& f, int32_t y, const ClipRowData& data);

} // namespace deluge::ui::grid
