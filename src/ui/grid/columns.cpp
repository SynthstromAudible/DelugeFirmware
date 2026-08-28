#include "ui/grid/columns.h"

#include <algorithm>

namespace deluge::ui::grid {

namespace {

/// How many rows of a frame a table-driven column can paint. The six lookup tables in columns.h are
/// `kColumnRows` long, exactly as legacy's are `kDisplayHeight` long, so a taller frame than the
/// Deluge's eight rows simply leaves the extra rows alone rather than indexing past them.
int32_t table_rows(const GridFrame& f) {
	return std::min(f.bounds().h, kColumnRows);
}

} // namespace

// --- VU meter -------------------------------------------------------------------------------

int32_t vu_max_row(float level) {
	// gui/views/view.cpp:1818-1819, verbatim -- including the `double` literal and the narrowing to
	// `float`, both of which are observable at a threshold (see the header).
	float dBFS = (level - 16.7) * 4;
	int32_t maxYDisplay = kVuSilent;

	for (int32_t yDisplay = 0; yDisplay < kColumnRows; yDisplay++) {
		if (dBFS >= kVuDbfsForRow[yDisplay]) { // :1839
			maxYDisplay = yDisplay;
		}
		else {
			break; // :1843 -- the table is ascending, so the first miss ends it
		}
	}
	return maxYDisplay;
}

RGB vu_row_colour(int32_t y) {
	if (y < 5) {
		return kVuGreen; // gui/views/view.cpp:1853-1854
	}
	if (y < 7) {
		return kVuOrange; // :1857-1858
	}
	return kVuRed; // :1861-1862
}

void vu_meter(GridFrame& f, int32_t x, float level_l, float level_r) {
	const int32_t rows = table_rows(f);

	// "erase current image as it will be refreshed" (gui/views/view.cpp:1779-1783): both columns,
	// every row, before either meter is drawn. Without it a falling meter never loses its top pads.
	for (int32_t y = 0; y < rows; y++) {
		f.pad(x, y, gui::colours::black);
		f.pad(x + 1, y, gui::colours::black);
	}

	// Left in column x, right in column x + 1 (view.cpp:1788, :1793). `kVuSilent` is *greater* than
	// every row index, not past the end of the loop, so it needs legacy's own explicit guard
	// (`if (maxYDisplayForVUMeterL != 255)`, :1787 and :1792) -- without it a silent channel would
	// light the whole column.
	const int32_t max_l = vu_max_row(level_l);
	const int32_t max_r = vu_max_row(level_r);

	// `renderVUMeter` fills row 0 up to and including maxYDisplay (view.cpp:1851-1852).
	if (max_l != kVuSilent) {
		for (int32_t y = 0; y < rows && y <= max_l; y++) {
			f.pad(x, y, vu_row_colour(y));
		}
	}
	if (max_r != kVuSilent) {
		for (int32_t y = 0; y < rows && y <= max_r; y++) {
			f.pad(x + 1, y, vu_row_colour(y));
		}
	}
}

// --- Macro column ---------------------------------------------------------------------------

RGB macro_colour(const MacroCell& cell, bool selected_here, bool other_selected) {
	// gui/views/view.cpp:2792-2793.
	const uint8_t dark = selected_here ? 32 : 0;
	const uint8_t light = other_selected ? 208 : 255;

	RGB colour = gui::colours::black;
	switch (cell.kind) {
	case MacroKind::ClipLaunch:
		// :2799-2804
		colour = cell.clip_active ? RGB{0, light, 0} : RGB{light, 0, 0};
		// :2806-2811 -- an armed clip blacks out on the flash's on phase.
		if (cell.clip_armed && cell.arm_flash) {
			colour = gui::colours::black;
		}
		break;
	case MacroKind::OutputCycle:
		colour = RGB{0, 64, light}; // :2814-2816
		break;
	case MacroKind::Section:
		colour = RGB{light, 0, 128}; // :2817-2819
		break;
	case MacroKind::None:
		colour = RGB{dark, dark, dark}; // :2820-2822
		break;
	}

	// :2831-2834 -- "Dim macros at rest so that pressing one visibly brightens it to its full
	// colour, giving feedback that the press registered (issue #3244)."
	return colour.adjust(255, cell.held ? 1 : 8);
}

bool macro_wants_arm_flash(const MacroCell& cell) {
	// `bool armed = view.clipArmFlashOn;` (gui/views/view.cpp:2795), forced true by an armed
	// clip-launch macro (:2806-2807) whatever the flash phase.
	return cell.arm_flash || (cell.kind == MacroKind::ClipLaunch && cell.clip_armed);
}

void macros(GridFrame& f, int32_t x, std::span<const MacroCell> rows, int32_t selected) {
	const int32_t count = std::min(f.bounds().h, static_cast<int32_t>(rows.size()));
	for (int32_t y = 0; y < count; y++) {
		// gui/views/view.cpp:2790-2791.
		const bool selected_here = (selected == y);
		const bool other_selected = (selected >= 0) && !selected_here;
		f.pad(x, y, macro_colour(rows[y], selected_here, other_selected));
	}
}

// --- Velocity column ------------------------------------------------------------------------

RGB velocity_colour(const VelocityData& data, int32_t y) {
	if (data.square == NoteSquare::None) {
		return gui::colours::black; // velocity.cpp:82-84, legacy's "erase pad"
	}
	if (static_cast<int32_t>(data.velocity) < kUnipolarRowMin[y]) {
		return gui::colours::black; // :100-102
	}
	switch (data.square) { // :89-97
	case NoteSquare::Head:
		return kVelocityRow[y];
	case NoteSquare::Tail:
		return kVelocityRowTail[y];
	case NoteSquare::Blurred:
		return kVelocityRowBlur[y];
	case NoteSquare::None:
		break; // unreachable: handled above
	}
	return gui::colours::black;
}

void velocity_column(GridFrame& f, int32_t x, const VelocityData& data) {
	const int32_t rows = table_rows(f);
	for (int32_t y = 0; y < rows; y++) { // velocity.cpp:68-71
		f.pad(x, y, velocity_colour(data, y));
	}
}

// --- Automation column ----------------------------------------------------------------------

namespace {

/// `renderAutomationUnipolarSquare` (mod_controllable.cpp:216-251).
RGB automation_unipolar_colour(const AutomationData& data, int32_t y) {
	// :222-224. `if (knobPos)` first: a parameter at 0 draws nothing at all, even in row 0, whose
	// minimum is also 0.
	const bool do_render = (data.knob_pos != 0) && (data.knob_pos >= kUnipolarRowMin[y]);

	// :242-247 -- the pad-selection cursor replaces whatever the value chose.
	if (data.pad_selected) {
		return do_render ? kAutomationRowBlur[y] : gui::colours::grey;
	}
	if (!do_render) {
		return gui::colours::black; // :238
	}
	return data.automated ? kAutomationRow[y] : kAutomationRowTail[y]; // :228-233
}

/// `renderAutomationBipolarSquare` (mod_controllable.cpp:130-213).
RGB automation_bipolar_colour(const AutomationData& data, int32_t y) {
	// :137-144 -- a patch cable's range is -128..128 about 0; every other bipolar parameter's is
	// 0..128 about 64.
	const int32_t middle = data.patch_cable ? 0 : 64;
	const bool above = data.knob_pos > middle;
	const bool below = data.knob_pos < middle;

	// :146-150 -- the half of the column the value does not point into is erased and *returns*, so
	// the pad-selection cursor below never reaches it.
	if ((above && y < 4) || (below && y > 3)) {
		return gui::colours::black;
	}

	// :152-172
	bool do_render = false;
	if (data.knob_pos != middle) {
		if (data.patch_cable) {
			do_render = above ? (data.knob_pos >= kPatchCableRowMin[y]) : (data.knob_pos <= kPatchCableRowMax[y]);
		}
		else {
			do_render = above ? (data.knob_pos >= kUnipolarRowMin[y]) : (data.knob_pos <= kUnipolarRowMax[y]);
		}
	}

	// The four-entry ramps are indexed outward from the middle: `[-yDisplay + 7]` above it,
	// `[yDisplay]` below (:176-191).
	const int32_t ramp = above ? (7 - y) : y;

	if (data.pad_selected) { // :198-212
		return do_render ? kAutomationBipolarBlur[ramp] : gui::colours::grey;
	}
	if (!do_render) {
		return gui::colours::black; // :194-196
	}
	return data.automated ? kAutomationBipolar[ramp] : kAutomationBipolarTail[ramp]; // :176-191
}

} // namespace

RGB automation_colour(const AutomationData& data, int32_t y) {
	// mod_controllable.cpp:119-126.
	return data.bipolar ? automation_bipolar_colour(data, y) : automation_unipolar_colour(data, y);
}

void automation_column(GridFrame& f, int32_t x, const AutomationData& data) {
	const int32_t rows = table_rows(f);
	for (int32_t y = 0; y < rows; y++) { // mod_controllable.cpp:118-126
		f.pad(x, y, automation_colour(data, y));
	}
}

// --- Tick squares ---------------------------------------------------------------------------

RGB tick_colour(TickColour kind, RGB muted) {
	switch (kind) {
	case TickColour::Muted:
		// `prepareColour`, hid/led/pad_leds.cpp:285-286: the menu colour, untransformed. The
		// channel transform at :192-202 is never read -- see `tick_colour`'s declaration.
		return muted;
	case TickColour::Red:
		return gui::colours::red; // flashColours[2], pad_leds.cpp:280
	case TickColour::Normal:
		break;
	}
	return kPlayheadColour; // flashColours[0], pad_leds.cpp:278
}

void tick_squares(GridFrame& f, std::span<const TickSquare> per_row, RGB muted_colour) {
	const int32_t count = std::min(f.bounds().h, static_cast<int32_t>(per_row.size()));
	for (int32_t y = 0; y < count; y++) {
		const TickSquare& square = per_row[y];
		if (square.col == kNoTickSquare) {
			continue; // hid/led/pad_leds.cpp:189 -- no playhead in this row
		}
		f.pad(square.col, y, tick_colour(square.colour, muted_colour));
	}
}

} // namespace deluge::ui::grid
