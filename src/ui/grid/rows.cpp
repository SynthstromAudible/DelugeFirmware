#include "ui/grid/rows.h"

#include <algorithm>

namespace deluge::ui::grid {

Response row(GridFrame& f, int32_t y, std::span<const RGB> cells) {
	const int32_t w = std::min({f.bounds().w, static_cast<int32_t>(cells.size()), static_cast<int32_t>(kMaxRowCells)});

	Response out;
	for (int32_t x = 0; x < w; x++) {
		const Response r = f.pad(x, y, cells[x]);
		out.released = out.released || r.released;
		out.held = out.held || r.held;
		if (r.pressed && !out.pressed) {
			// The brief's encoding: the pressed column travels in `velocity` as x + 1, so 0 is
			// "no press". The leftmost press wins when a tick brings more than one.
			out.pressed = true;
			out.velocity = static_cast<uint8_t>(x + 1);
		}
	}
	return out;
}

Response clip_row(GridFrame& f, int32_t y, const ClipRowData& data) {
	const int32_t w = std::min(f.bounds().w, static_cast<int32_t>(kMaxRowCells));

	// `overwriteExisting`'s clear (model/note/note_row.cpp:1878-1881): every column starts black,
	// and a row with no notes stays that way (legacy's early `if (notes.empty()) return;`, :1884).
	RGB cells[kMaxRowCells] = {};

	// Pass 1 -- continuing notes (note_row.cpp:1967-1984). A note contributes tail columns only
	// when `allowNoteTails` is set for it, which is legacy's `noteEnd > squareStartPos &&
	// allowNoteTails` at :1977.
	for (const NoteCell& n : data.notes) {
		if (!n.tail || n.len_cols == 0) {
			continue;
		}
		const int32_t first = static_cast<int32_t>(n.start_col) + 1; // the start column is a start, not a tail
		const int32_t last = static_cast<int32_t>(n.start_col) + static_cast<int32_t>(n.len_cols); // exclusive
		for (int32_t x = std::max<int32_t>(first, 0); x < std::min(last, w); x++) {
			cells[x] = data.tail_colour;
		}
	}

	// Pass 2 -- note starts, after the tails so a start always wins the column it shares with
	// another note's tail (legacy's per-square if/else-if reaches the "starts within"/"starts on"
	// arms before the continuing-note arm: note_row.cpp:1947, :1956, :1967).
	for (const NoteCell& n : data.notes) {
		if (n.len_cols == 0) {
			continue;
		}
		const int32_t x = static_cast<int32_t>(n.start_col);
		if (x >= w) {
			continue;
		}
		if (n.starts_within) {
			cells[x] = data.blur_colour; // note_row.cpp:1947-1949
		}
		else {
			// note_row.cpp:1956-1960, verbatim: "colour * (0.25 + 0.75 * velocity) but in fixed
			// point, i.e. it'll go linearly from 0.25 to max brightness with velocity". The cast
			// to the parameter's uint16_t is legacy's own implicit narrowing -- for the 0..127
			// velocities a Deluge produces, (65 + v + v/2) << 8 tops out at 255 << 8 and fits.
			const auto numerator = static_cast<uint16_t>((65 + n.velocity + (n.velocity / 2)) << 8);
			cells[x] = data.colour.adjustFractional(numerator, static_cast<uint16_t>(255 << 8));
		}
	}

	// The playhead last, over whatever the notes left: legacy substitutes the flash colour for the
	// square's own colour in the driver, after the view has rendered (hid/led/pad_leds.cpp:283-292).
	if (data.playhead_col >= 0 && data.playhead_col < w) {
		cells[data.playhead_col] = kPlayheadColour;
	}

	return row(f, y, std::span<const RGB>(cells, static_cast<size_t>(w)));
}

} // namespace deluge::ui::grid
