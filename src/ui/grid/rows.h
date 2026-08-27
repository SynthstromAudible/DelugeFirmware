#pragma once
#include "gfx/grid/rgb.h"
#include "ui/core/response.h"
#include "ui/grid/frame.h"

#include <cstdint>
#include <span>

namespace deluge::ui::grid {

/// The widest row `row()` / `clip_row()` will paint in one call, and the size of `clip_row`'s
/// scratch buffer. A Deluge row is 18 pads wide (16 main + 2 sidebar, one coordinate space -- see
/// ui/grid/frame.h); 32 leaves room for a wider future surface at a cost of 96 bytes of stack in
/// the one function that needs the buffer. Columns past the 32nd are not painted.
constexpr uint8_t kMaxRowCells = 32;

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
/// The `Response` is the row's, not one pad's, and encodes *which* column was pressed the way the
/// task 11 brief specifies: `velocity` carries `x + 1`, so 0 means "no press". The real press
/// velocity is therefore not reported by this call -- a component that needs it (velocity pads,
/// the note editor) calls `f.pad()` per column, or reads `f.local_events()`. When more than one
/// pad in the row is pressed in the same tick the *leftmost* is reported; `pressed`, `released`
/// and `held` are the row's OR, so a caller can still tell "something in this row is down".
Response row(GridFrame& f, int32_t y, std::span<const RGB> cells);

/// One note, windowed into columns by the caller -- spec B section 7's `NoteCell`.
///
/// `NoteRow::renderRow` (model/note/note_row.cpp:1873) walks squares, asks the note tree what
/// note covers each square's start position, and compares the note's `pos` with the square's
/// start. That search is model work; by the time a `NoteCell` exists the comparison has already
/// been made, and the three legacy outcomes survive as the fields below.
struct NoteCell {
	/// The column the note starts in: legacy's `xDisplay` for the square whose `squareStartPos`
	/// the note's `pos` falls in or on.
	uint8_t start_col = 0;
	/// How many columns the note occupies *including* `start_col`, i.e. `len_cols - 1` tail
	/// columns follow the start. 0 paints nothing.
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
};

/// One clip/note row's worth of already-windowed model data -- spec B section 7's `ClipRowData`.
///
/// The three colours are legacy's `rowColour` / `rowTailColour` / `rowBlurColour` triple, which
/// every caller of `NoteRow::renderRow` derives from the row's base colour with `RGB::forTail()`
/// and `RGB::forBlur()` (gfx/grid/rgb.h) before calling. They stay three separate fields, not one
/// colour plus derivations, because several views substitute their own (the performance view's
/// pastel tail colours, gui/views/performance_view.cpp:381-389).
struct ClipRowData {
	/// Legacy `rowColour`: scaled by velocity on a note that starts exactly on a square.
	RGB colour;
	/// Legacy `rowTailColour`: a note continuing through a square (note_row.cpp:1979).
	RGB tail_colour;
	/// Legacy `rowBlurColour`: a note starting somewhere within a square (note_row.cpp:1949).
	RGB blur_colour;
	/// The notes visible in this window. Order is irrelevant: starts always beat tails (see
	/// `clip_row`), so overlapping cells cannot depend on it.
	std::span<const NoteCell> notes;
	/// The playhead column, or -1 for none. Painted last, over any note.
	int32_t playhead_col = -1;
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

/// Paint one note row: `NoteRow::renderRow`'s colour rules (model/note/note_row.cpp:1873-2013)
/// over a caller-windowed `ClipRowData`.
///
/// Per column, in legacy's own order of precedence:
///
/// 1. black -- legacy's `overwriteExisting` clear (note_row.cpp:1878-1881), done unconditionally
///    here because a component that paints a row owns that row;
/// 2. a note continuing through the column, `tail == true` -> `tail_colour` (:1977-1979);
/// 3. a note starting in the column, `starts_within` -> `blur_colour` (:1947-1949);
/// 4. a note starting in the column, exactly on it ->
///    `colour.adjustFractional((65 + v + v / 2) << 8, 255 << 8)` (:1956-1960) -- "colour * (0.25 +
///    0.75 * velocity) in fixed point", as the legacy comment puts it;
/// 5. `playhead_col` -> `kPlayheadColour`.
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
/// The `Response` encodes the pressed column exactly as `row()` does.
Response clip_row(GridFrame& f, int32_t y, const ClipRowData& data);

} // namespace deluge::ui::grid
