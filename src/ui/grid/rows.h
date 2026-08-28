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
/// all (gfx/grid/pad_grid.h:18, where it caps `PadGrid`'s storage) -- so the cap can never truncate
/// a real frame: `f.bounds().w` is at most the grid's column count, which is at most this. The
/// `std::min` against it in the implementation is belt-and-braces against a future wider `PadGrid`,
/// not a live limit. A Deluge row is 18 pads wide (16 main + 2 sidebar, one coordinate space -- see
/// ui/grid/frame.h), so the buffers cost 128 bytes of stack for a frame that uses 18 of them.
constexpr uint8_t kMaxRowCells = gfx::grid::kMaxGridCols;

/// Paint one row of already-decided colours -- spec B section 7's `row(f, y, span<RGB>)`, which
/// stands in for the ten `renderRow` implementations that all end up writing a run of `RGB`s into
/// one image row (`SessionView::renderRow` gui/views/session_view.cpp:2523,
/// `PerformanceView::renderRow` gui/views/performance_view.cpp:371, and their siblings).
///
/// Legacy hands those routines an `RGB thisImage[]` and they fill `xDisplay = 0 ..
/// kDisplayWidth`; here the caller passes the same run as a span and the frame does the writing,
/// so the row is also hit-tested in the same pass (spec B section 7: "the call that paints a pad
/// reports its press").
///
/// Cells past `f.bounds().w` -- or past `kMaxRowCells` -- are dropped, and a span shorter than
/// the frame leaves the remaining columns untouched (it does *not* black them out; legacy's
/// clearing is the caller's `memset`, note_row.cpp:1879, and `clip_row` below does it itself).
///
/// The `Response` is the row's, not one pad's, and encodes *which* column was pressed in
/// `Response::index`: the pressed column's x, or -1 for no press. `Response::velocity` is that
/// press's own pad velocity, as everywhere else in the toolkit (ui/core/response.h) -- the two are
/// separate fields, so a row reports both the column and how hard it was hit. When more than one
/// pad in the row is pressed in the same tick the *leftmost* is reported; `pressed`, `released`
/// and `held` are the row's OR, so a caller can still tell "something in this row is down". A
/// component that needs every press of the tick reads `f.local_events()`, or `f.first_press_in()`
/// for just the first inside a rectangle.
Response row(GridFrame& f, int32_t y, std::span<const RGB> cells);

/// `FillMode` (definitions_cxx.hpp:717-721) mirrored for this layer, which may include only
/// `ui/**`, `gfx/**` and the standard library. Same three states in the same order.
enum class NoteFill : uint8_t { Off, NotFill, Fill };

/// One note, windowed into columns by the caller -- spec B section 7's `NoteCell`.
///
/// `NoteRow::renderRow` (model/note/note_row.cpp:1873) walks squares, asks the note tree what
/// note covers each square's start position, and compares the note's `pos` with the square's
/// start. That search is model work; by the time a `NoteCell` exists the comparison has already
/// been made, and the legacy outcomes survive as the fields below.
struct NoteCell {
	/// The column the note starts in: legacy's `xDisplay` for the square whose `squareStartPos`
	/// the note's `pos` falls in or on.
	///
	/// **Signed, and routinely negative.** A note that began before the window -- the ordinary
	/// case for a scrolled row -- has a negative start column, and only its tail is visible; the
	/// start colour is not painted at all. That is legacy's continuing-note arm
	/// (note_row.cpp:1967-1984), which paints `rowTailColour` on any square the note's end is
	/// still beyond, whether or not its beginning is on screen.
	///
	/// A note whose tail *wraps* around the end of the loop (legacy's `wrapping = (i == 0)` /
	/// `notes.tryGetLast()` / `noteEnd -= effectiveRowLength`, :1969-1975) is expressed as a
	/// **second cell** for the same note, with `start_col` shifted back by the loop's length in
	/// columns: `start_col - loop_cols`, which is exactly legacy's `-= effectiveRowLength` in
	/// column units. That cell is necessarily negative (a wrapped note started inside the loop),
	/// so it paints tail columns at the left of the row and never a start -- again, legacy's
	/// behaviour. No extra field is needed for the wrap: the subtraction is the caller's, because
	/// the caller is the one that knows `effectiveRowLength` and the zoom.
	int16_t start_col = 0;
	/// How many columns the note occupies *including* `start_col`, i.e. `len_cols - 1` tail
	/// columns follow the start. 0 paints nothing. Counted from `start_col`, so a note that began
	/// two columns left of the window with three visible tail columns is `{-2, 5}`.
	uint8_t len_cols = 0;
	/// The note's velocity, used only when `starts_within` is false (note_row.cpp:1960).
	uint8_t velocity = 0;
	/// The note starts strictly *after* the square's start position (`note->pos > squareStartPos`,
	/// note_row.cpp:1947) rather than exactly on it (`==`, :1956). The start column then gets the
	/// blur colour and the velocity plays no part -- legacy's way of saying "this note begins
	/// somewhere inside this square, and one pad cannot show where".
	bool starts_within = false;
	/// Legacy's `allowNoteTails` (note_row.cpp:1977), per note: false paints only the start
	/// column, however long the note is. Zoomed-out clip rows and non-tail instruments pass false.
	bool tail = false;
	/// `note->fill` (note_row.cpp:1989-1993). Consulted only while `ClipRowData::fill_mode_active`.
	NoteFill fill = NoteFill::Off;
	/// `note->getProbability() != kNumProbabilityValues || note->getIterance() !=
	/// kDefaultIteranceValue || note->getFill() != FillMode::OFF` (note_row.cpp:1998-2000): the
	/// note has settings the note editor marks. Consulted only while `ClipRowData::note_editor`.
	bool non_default_params = false;
};

/// One clip/note row's worth of already-windowed model data -- spec B section 7's `ClipRowData`.
///
/// The three colours are legacy's `rowColour` / `rowTailColour` / `rowBlurColour` triple, which
/// every caller of `NoteRow::renderRow` derives from the row's base colour with `RGB::forTail()`
/// and `RGB::forBlur()` (gfx/grid/rgb.h) before calling. They stay three separate fields, not one
/// colour plus derivations, because several views substitute their own: the performance view's
/// pastel tail colours (gui/views/performance_view.cpp:381-389), and
/// `InstrumentClip::renderAsSingleRow` (model/clip/instrument_clip.cpp:1467), whose blur colour is
/// the *main* colour when `allowBlur` is false.
struct ClipRowData {
	/// Legacy `rowColour`: scaled by velocity on a note that starts exactly on a square.
	RGB colour;
	/// Legacy `rowTailColour`: a note continuing through a square (note_row.cpp:1979).
	RGB tail_colour;
	/// Legacy `rowBlurColour`: a note starting somewhere within a square (note_row.cpp:1949).
	RGB blur_colour;
	/// The notes visible in this window.
	///
	/// Two notes that start in the *same* column resolve to whichever comes last in this span, so
	/// the caller must keep the model's order: legacy resolves that square with
	/// `notes.tryGet(i - 1)` (note_row.cpp:1940-1942), the note with the greatest `pos` below the
	/// square's end, i.e. the later one. Order is otherwise irrelevant -- a start always beats a
	/// tail, whichever cell each came from (see `clip_row`).
	std::span<const NoteCell> notes;
	/// The playhead column, or -1 for none. Painted last, over any note.
	int32_t playhead_col = -1;
	/// Legacy's `overwriteExisting` (note_row.cpp:1874, tested :1878-1883).
	///
	/// `true` (the default, and what a view rendering one note row per grid row passes) clears
	/// every column first, so the row shows this data and nothing else.
	///
	/// `false` paints *only* the columns this call's notes and playhead touch and leaves the rest
	/// exactly as they were -- which is how legacy composites many note rows into one grid row:
	/// `InstrumentClip::renderAsSingleRow` (model/clip/instrument_clip.cpp:1449-1477) loops over
	/// the clip's note rows and calls `renderRow` once each with `overwriteExisting = false` and a
	/// **different** colour triple per row (:1465-1467), which is the body of
	/// `SessionView::renderRow` (gui/views/session_view.cpp:2523, via `renderAsSingleRow` at
	/// :2554). A scene reproduces that by calling `clip_row` once per note row, first call
	/// `overwrite_existing = true` (or a `row()` of black) and the rest `false`.
	///
	/// With `false`, the returned `Response` covers only the columns this call painted: an
	/// untouched column is not written *and not hit-tested*, because painting and hit-testing are
	/// the same call. The composited row's presses come from the pass that painted the column, or
	/// from `f.local_events()`.
	bool overwrite_existing = true;
	/// `currentSong->isFillModeActive()` (note_row.cpp:1988).
	bool fill_mode_active = false;
	/// `getCurrentUI() == &soundEditor && soundEditor.inNoteEditor()` (note_row.cpp:1997).
	bool note_editor = false;
	/// The colour a fill note takes while fill mode is active: legacy `colours::blue`
	/// (note_row.cpp:1990). A field rather than a palette read so the component stays pure.
	RGB blue = gui::colours::blue;
	/// The colour a not-fill note takes while fill mode is active: legacy `colours::red` (:1993).
	RGB red = gui::colours::red;
	/// The colour a note with non-default parameters takes in the note editor: legacy
	/// `colours::yellow` (:2001).
	RGB yellow = gui::colours::yellow;
};

/// The playhead colour, from the pad driver's slow-flash table: `PadLEDs::flashColours[0]`
/// (hid/led/pad_leds.cpp:277-278), the entry `prepareColour` (:283-292) substitutes for a
/// square's own colour while the cursor sits on it and the row is neither muted (entry 1) nor
/// recording linearly (entry 2, red).
///
/// Legacy paints the playhead in the *driver*, after the view has rendered, which is why
/// `renderRow` itself has no playhead branch; spec B section 7 keeps a separate `tick_squares`
/// component for the overlay across a whole grid, and `ClipRowData::playhead_col` is the
/// one-row shorthand for the same pixel. The muted and recording variants belong to
/// `tick_squares`, which knows the per-row mute/record state; this call has only a column.
constexpr RGB kPlayheadColour = RGB{130, 120, 130};

/// Paint one note row: `NoteRow::renderRow`'s colour rules (model/note/note_row.cpp:1873-2005)
/// over a caller-windowed `ClipRowData`.
///
/// Per column, in legacy's own order of precedence:
///
/// 1. black -- legacy's `overwriteExisting` clear (note_row.cpp:1878-1883); skipped, along with
///    the paint itself, when `overwrite_existing` is false;
/// 2. a note continuing through the column, `tail == true` -> `tail_colour` (:1977-1979). This is
///    also the whole of what a note with a negative `start_col` draws;
/// 3. a note starting in the column, `starts_within` -> `blur_colour` (:1947-1949);
/// 4. a note starting in the column, exactly on it ->
///    `colour.adjustFractional((65 + v + v / 2) << 8, 255 << 8)` (:1956-1960) -- "colour * (0.25 +
///    0.75 * velocity) in fixed point", as the legacy comment puts it;
/// 5. that start colour is then recoloured (:1985-2004): while `fill_mode_active`, a `Fill` note
///    becomes `blue` and a `NotFill` note `red`; otherwise, while `note_editor`, a note with
///    `non_default_params` becomes `yellow`. Legacy gates this on the pixel being neither the row's
///    blur nor its tail colour (:1986), which is why blur and tail columns are never recoloured --
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
/// Legacy's `occupancyMask` writes (note_row.cpp:1879, :1950-1952, :1961-1963, :1980-1982) are
/// deliberately dropped: the mask exists for `drawUndefinedArea` and the greyout, which spec B
/// section 7 gives its own `greyout(f, GreyoutMask, amount)` component, applied after painting.
/// Nothing here needs to remember which pads a note lit.
///
/// The `Response` encodes the pressed column in `index` and its velocity in `velocity`, exactly as
/// `row()` does, subject to `overwrite_existing` (see that field).
Response clip_row(GridFrame& f, int32_t y, const ClipRowData& data);

} // namespace deluge::ui::grid
