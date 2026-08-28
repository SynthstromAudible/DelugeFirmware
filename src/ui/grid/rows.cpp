#include "ui/grid/rows.h"

#include <algorithm>

namespace deluge::ui::grid {

namespace {

/// Fold one pad's answer into a row's: the pressed column travels in `Response::index` (-1 = no
/// press) and its velocity in `Response::velocity`, with the leftmost press winning when a tick
/// brings more than one.
void accumulate(Response& out, const Response& pad, int32_t x) {
	out.released = out.released || pad.released;
	out.held = out.held || pad.held;
	if (pad.pressed && !out.pressed) {
		out.pressed = true;
		out.index = static_cast<int16_t>(x);
		out.velocity = pad.velocity;
	}
}

/// The fill-mode / note-editor recolouring (model/note/note_row.cpp:1985-2004).
///
/// Legacy runs this after every square, but its outer test is "the pixel is neither the row's blur
/// colour nor its tail colour" (:1986) and both inner arms need `drewNote`, so the only pixel it
/// can ever change is the one the exact-start arm just wrote. Called from there, with legacy's
/// colour comparison kept: a start colour that happens to equal the blur or tail colour is left
/// alone, exactly as legacy leaves it (`InstrumentClip::renderAsSingleRow` really does pass
/// `blurColour == mainColour` when `allowBlur` is false, model/clip/instrument_clip.cpp:1467).
RGB recoloured(RGB pixel, const NoteCell& note, const ClipRowData& data) {
	if (pixel == data.blur_colour || pixel == data.tail_colour) {
		return pixel; // :1986
	}
	if (data.fill_mode_active) { // :1988
		if (note.fill == NoteFill::Fill) {
			return data.blue; // :1989-1990
		}
		if (note.fill == NoteFill::NotFill) {
			return data.red; // :1992-1993
		}
		// Legacy's arms are `if (fill mode) ... else if (note editor) ...`, so an ordinary note
		// while fill mode is active is never given the note editor's colour.
		return pixel;
	}
	if (data.note_editor && note.non_default_params) {
		return data.yellow; // :1996-2002
	}
	return pixel;
}

} // namespace

Response row(GridFrame& f, int32_t y, std::span<const RGB> cells) {
	const int32_t w = std::min({f.bounds().w, static_cast<int32_t>(cells.size()), static_cast<int32_t>(kMaxRowCells)});

	Response out;
	for (int32_t x = 0; x < w; x++) {
		accumulate(out, f.pad(x, y, cells[x]), x);
	}
	return out;
}

Response clip_row(GridFrame& f, int32_t y, const ClipRowData& data) {
	const int32_t w = std::min(f.bounds().w, static_cast<int32_t>(kMaxRowCells));

	// `overwriteExisting`'s clear (model/note/note_row.cpp:1878-1883): every column starts black,
	// and a row with no notes stays that way (legacy's early `if (notes.empty()) return;`, :1885).
	// When `overwrite_existing` is false these cells are never written to the grid -- `touched`
	// records which columns this call actually has something to say about.
	RGB cells[kMaxRowCells] = {};
	bool touched[kMaxRowCells] = {};

	// Pass 1 -- continuing notes (note_row.cpp:1967-1984). A note contributes tail columns only
	// when `allowNoteTails` is set for it, which is legacy's `noteEnd > squareStartPos &&
	// allowNoteTails` at :1977. A note that began left of the window (negative `start_col`, whether
	// scrolled off or wrapped around the loop as at :1969-1975) contributes nothing else, and the
	// clamp to 0 below is what makes its visible columns start at the left edge.
	for (const NoteCell& note : data.notes) {
		if (!note.tail || note.len_cols == 0) {
			continue;
		}
		const int32_t first = static_cast<int32_t>(note.start_col) + 1; // the start column is a start, not a tail
		const int32_t last = static_cast<int32_t>(note.start_col) + static_cast<int32_t>(note.len_cols); // exclusive
		for (int32_t x = std::max<int32_t>(first, 0); x < std::min(last, w); x++) {
			cells[x] = data.tail_colour;
			touched[x] = true;
		}
	}

	// Pass 2 -- note starts, after the tails so a start always wins the column it shares with
	// another note's tail (legacy's per-square if/else-if reaches the "starts within"/"starts on"
	// arms before the continuing-note arm: note_row.cpp:1947, :1956, :1967).
	for (const NoteCell& note : data.notes) {
		if (note.len_cols == 0) {
			continue;
		}
		const int32_t x = static_cast<int32_t>(note.start_col);
		if (x < 0 || x >= w) {
			continue; // began before the window (or past it): only its tail is visible
		}
		if (note.starts_within) {
			cells[x] = data.blur_colour; // note_row.cpp:1947-1949
		}
		else {
			// note_row.cpp:1956-1960, verbatim: "colour * (0.25 + 0.75 * velocity) but in fixed
			// point, i.e. it'll go linearly from 0.25 to max brightness with velocity". The cast
			// to the parameter's uint16_t is legacy's own implicit narrowing -- for the 0..127
			// velocities a Deluge produces, (65 + v + v/2) << 8 tops out at 255 << 8 and fits.
			const auto numerator = static_cast<uint16_t>((65 + note.velocity + (note.velocity / 2)) << 8);
			cells[x] = recoloured(data.colour.adjustFractional(numerator, static_cast<uint16_t>(255 << 8)), note, data);
		}
		touched[x] = true;
	}

	// The playhead last, over whatever the notes left: legacy substitutes the flash colour for the
	// square's own colour in the driver, after the view has rendered (hid/led/pad_leds.cpp:283-292).
	if (data.playhead_col >= 0 && data.playhead_col < w) {
		cells[data.playhead_col] = kPlayheadColour;
		touched[data.playhead_col] = true;
	}

	Response out;
	for (int32_t x = 0; x < w; x++) {
		if (!data.overwrite_existing && !touched[x]) {
			continue; // composited row: this column belongs to some other note row
		}
		accumulate(out, f.pad(x, y, cells[x]), x);
	}
	return out;
}

} // namespace deluge::ui::grid
