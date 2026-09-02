#include "ui/grid/markers.h"

#include <cstdint>

namespace deluge::ui::grid {

void marker_tint(GridFrame& f, int32_t x, int32_t y_start, int32_t y_end, RGB colour, bool dim) {
	if (dim) {
		colour = colour.dim(3);
	}

	for (int32_t y = y_start; y < y_end; ++y) {
		// The pad's own red channel, quartered -- see the note in the header on why red alone.
		const uint8_t value = static_cast<uint8_t>(f.peek(x, y).r / 4);
		f.pad(x, y, colour.transform([value](uint8_t a) {
			return static_cast<uint8_t>(value + static_cast<uint8_t>((uint32_t{a} * 3) / 4));
		}));
	}
}

void marker_column(GridFrame& f, int32_t x, const MarkerStack& stack, std::span<const RGB> colours) {
	const int32_t rows = f.bounds().h;

	int32_t num_markers = 0;
	for (int32_t i = 0; i < kMaxMarkers; ++i) {
		if ((stack.active & (uint32_t{1} << i)) != 0) {
			++num_markers;
		}
	}

	if (num_markers == 0) {
		return;
	}

	int32_t marker = 0;
	uint32_t marker_mask = 1;
	while ((marker_mask & stack.active) == 0) {
		marker_mask <<= 1;
		++marker;
	}

	const uint32_t selected_mask = stack.selected >= 0 ? (uint32_t{1} << stack.selected) : 0u;
	int32_t prev_y_end = rows;
	int32_t seen_markers = 0;

	int32_t base_increment = 0;
	int32_t selected_increment = 0;
	switch (num_markers) {
	case 1:
		base_increment = selected_increment = rows / 2;
		break;
	case 2:
		base_increment = selected_increment = 2;
		break;
	case 3:
		base_increment = 1;
		selected_increment = 2;
		break;
	default: // 4 -- `kMaxMarkers`, so `num_markers` can be nothing else
		base_increment = selected_increment = 1;
		break;
	}

	while (marker_mask != (uint32_t{1} << kMaxMarkers)) {
		// skip inactive markers
		if ((marker_mask & stack.active) == 0) {
			marker_mask <<= 1;
			++marker;
			continue;
		}

		const bool is_selected = (marker_mask & selected_mask) != 0;

		seen_markers++;

		const int32_t y_end = prev_y_end;
		int32_t y_start = 0;

		if (seen_markers == num_markers) {
			// This is the last marker we need to render in this column, fill the rest of the column
			y_start = rows / 2;
		}
		else {
			// otherwise, render just 1 pad for this marker
			y_start = y_end - (is_selected ? selected_increment : base_increment);
		}

		const bool allowed = (marker_mask & stack.suppress) == 0u;
		if (allowed && marker < static_cast<int32_t>(colours.size())) {
			const bool dim = !is_selected;
			marker_tint(f, x, y_start, y_end, colours[marker], dim);
			marker_tint(f, x, rows - y_end, rows - y_start, colours[marker], dim);
		}

		prev_y_end = y_start;
		marker_mask <<= 1;
		++marker;
	}
}

} // namespace deluge::ui::grid
