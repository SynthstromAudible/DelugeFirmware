#include "../gfx/support/golden.h"
#include "support/frames.h"
#include "ui/grid/columns.h"

#include "cppspec.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <string>

using namespace deluge::ui;
using namespace deluge::ui::grid;
using deluge::gfx::grid::Rect;
namespace uitest = deluge::ui::test;
namespace palette = deluge::gui::colours;

// The five grid column components (spec B section 7): the VU meter, the macro column, the velocity
// and automation bars, and the tick-square overlay. Every one is a port of a legacy routine, so
// wherever legacy computes rather than tabulates -- the dBFS conversion, the macro brightnesses,
// the muted-tick transform -- this spec transcribes *legacy's* expression and compares the port
// against that, rather than against literals the port could drift from together with the spec.

namespace {

// --- View::getMaxYDisplayForVUMeter (gui/views/view.cpp:1815-1847), transcribed ---------------
const float legacy_dbfs_for_row[8] = {-30.8, -26.4, -22.0, -17.6, -13.2, -8.8, -4.4, -0.2};

int32_t legacy_vu_max_row(float level) {
	float dBFS = (level - 16.7) * 4;
	int32_t maxYDisplay = 255;
	for (int32_t y = 0; y < 8; y++) {
		if (dBFS >= legacy_dbfs_for_row[y]) {
			maxYDisplay = y;
		}
		else {
			break;
		}
	}
	return maxYDisplay;
}

// --- View::renderMacros' colour body (gui/views/view.cpp:2787-2834), transcribed --------------
RGB legacy_macro_colour(MacroKind kind, bool clip_active, bool clip_armed, bool arm_flash, bool held, int32_t selected,
                        int32_t y) {
	bool is_active = selected == y;
	bool is_other_active = selected >= 0 && !is_active;
	uint8_t dark = is_active ? 32 : 0;
	uint8_t light = is_other_active ? 208 : 255;

	RGB image{0, 0, 0};
	switch (kind) {
	case MacroKind::ClipLaunch:
		image = clip_active ? RGB{0, light, 0} : RGB{light, 0, 0};
		if (clip_armed && arm_flash) {
			image = RGB{0, 0, 0};
		}
		break;
	case MacroKind::OutputCycle:
		image = RGB{0, 64, light};
		break;
	case MacroKind::Section:
		image = RGB{light, 0, 128};
		break;
	case MacroKind::None:
		image = RGB{dark, dark, dark};
		break;
	}
	return image.adjust(255, (held ? 1 : 8));
}

// --- PadLEDs::setTickSquares' muted transform (hid/led/pad_leds.cpp:192-202), transcribed ------
RGB legacy_muted(RGB c) {
	auto transform = [](uint8_t& channel, size_t idx) {
		if (channel >= 64) {
			channel += (1 << idx);
		}
	};
	transform(c.r, 0);
	transform(c.g, 1);
	transform(c.b, 2);
	return c;
}

constexpr RGB kBlack = RGB{0, 0, 0};

} // namespace

// clang-format off
describe grid_columns("ui::grid columns", $ {

	// -----------------------------------------------------------------------------------------
	// VU meter -- View::renderVUMeter / getMaxYDisplayForVUMeter (gui/views/view.cpp:1812-1865)
	// -----------------------------------------------------------------------------------------

	it("lights every row on a clipping channel, and the bottom row alone at the audibility floor", _ {
		uitest::GridRig r;
		// The task brief's case, `vu_meter(x, 16.7, 9)`, on the two sidebar columns legacy uses
		// (view.cpp:1788, :1793). 16.7 is "log(2^24) ... the approxRMSLevel at which clipping
		// begins" (:1817) and 9 is the level "at which the sound becomes inaudible" (:1823-1824).
		vu_meter(r.frame, 16, 16.7f, 9.0f);

		// Left: all eight rows, in legacy's three colour bands (:1853-1862).
		for (int32_t y = 0; y < 5; y++) {
			expect(r.grid.at(16, y)).to_equal(palette::green);
		}
		expect(r.grid.at(16, 5)).to_equal(palette::orange);
		expect(r.grid.at(16, 6)).to_equal(palette::orange);
		expect(r.grid.at(16, 7)).to_equal(palette::red);

		// Right: **brief amendment.** The brief expects "none on the right" at level 9. Legacy
		// lights row 0: `(9 - 16.7) * 4` is computed in `double` and narrowed to `float`, which
		// lands on exactly -30.8f, and the test at :1839 is `>=`. Legacy's own comment agrees --
		// "any approxRMSLevel value *below* 9 doesn't get rendered" (:1823-1824).
		expect(legacy_vu_max_row(9.0f)).to_equal(0);
		expect(r.grid.at(17, 0)).to_equal(palette::green);
		for (int32_t y = 1; y < 8; y++) {
			expect(r.grid.at(17, y)).to_equal(kBlack);
		}
	});

	it("puts the meter's top on the row the dBFS table names, one level per threshold", _ {
		// One level comfortably inside each of the eight bands (:1827-1836), plus one below the
		// bottom threshold. Levels convert as `level = dBFS / 4 + 16.7`.
		struct Case { float level; int32_t top; };
		constexpr Case cases[] = {
		    {8.0f, 255},  // below -30.8 dBFS: `maxYDisplay` stays 255, nothing is drawn (:1819)
		    {9.5f, 0},    // -30.8 .. -26.5
		    {10.6f, 1},   // -26.4 .. -22.1
		    {11.7f, 2},   // -22.0 .. -17.7
		    {12.8f, 3},   // -17.6 .. -13.3
		    {13.9f, 4},   // -13.2 ..  -8.9
		    {15.0f, 5},   //  -8.8 ..  -4.5
		    {16.0f, 6},   //  -4.4 ..  -0.3
		    {16.7f, 7},   // clipping (-0.2 or higher)
		};
		for (const Case& c : cases) {
			uitest::GridRig r;
			expect(vu_max_row(c.level)).to_equal(legacy_vu_max_row(c.level));
			expect(vu_max_row(c.level)).to_equal(c.top);
			vu_meter(r.frame, 16, c.level, 0.0f);
			for (int32_t y = 0; y < 8; y++) {
				// `kVuSilent` (255) is legacy's "nothing to draw" sentinel, not a row index.
				const bool lit = (c.top != kVuSilent) && (y <= c.top);
				expect(r.grid.at(16, y)).to_equal(lit ? vu_row_colour(y) : kBlack);
			}
			// A silent right channel is left black, whatever the left one does (:1792).
			for (int32_t y = 0; y < 8; y++) {
				expect(r.grid.at(17, y)).to_equal(kBlack);
			}
		}
	});

	it("erases both columns before drawing, so a falling meter loses its top pads", _ {
		uitest::GridRig r;
		vu_meter(r.frame, 16, 16.7f, 16.7f);
		expect(r.grid.at(16, 7)).to_equal(palette::red);
		// "erase current image as it will be refreshed" (view.cpp:1779-1783).
		vu_meter(r.frame, 16, 9.5f, 8.0f);
		expect(r.grid.at(16, 0)).to_equal(palette::green);
		for (int32_t y = 1; y < 8; y++) {
			expect(r.grid.at(16, y)).to_equal(kBlack);
		}
		for (int32_t y = 0; y < 8; y++) {
			expect(r.grid.at(17, y)).to_equal(kBlack);
		}
	});

	it("matches the legacy VU colour bands exactly", _ {
		// :1853-1862, spelled out: rows 0-4 green, 5-6 orange, 7 red.
		expect(vu_row_colour(0)).to_equal(palette::green);
		expect(vu_row_colour(4)).to_equal(palette::green);
		expect(vu_row_colour(5)).to_equal(palette::orange);
		expect(vu_row_colour(6)).to_equal(palette::orange);
		expect(vu_row_colour(7)).to_equal(palette::red);
	});

	// -----------------------------------------------------------------------------------------
	// Macro column -- View::renderMacros (gui/views/view.cpp:2785-2837)
	// -----------------------------------------------------------------------------------------

	it("paints every macro kind the colour renderMacros paints it", _ {
		uitest::GridRig r;
		// One kind per row, nothing selected, nothing armed or held: the plain palette.
		const MacroCell rows[] = {
		    {.kind = MacroKind::None},
		    {.kind = MacroKind::ClipLaunch, .clip_active = false},
		    {.kind = MacroKind::ClipLaunch, .clip_active = true},
		    {.kind = MacroKind::OutputCycle},
		    {.kind = MacroKind::Section},
		};
		macros(r.frame, 16, rows, -1);

		// `adjust(255, 8)` is an integer eighth of each channel (:2831-2834): 255 -> 31, 128 -> 16,
		// 64 -> 8, 0 -> 0. Spelled as literals here so the dimming is pinned, not just re-derived.
		expect(r.grid.at(16, 0)).to_equal(RGB{0, 0, 0});    // None, unselected: dark = 0
		expect(r.grid.at(16, 1)).to_equal(RGB{31, 0, 0});   // ClipLaunch stopped: {light, 0, 0}
		expect(r.grid.at(16, 2)).to_equal(RGB{0, 31, 0});   // ClipLaunch active:  {0, light, 0}
		expect(r.grid.at(16, 3)).to_equal(RGB{0, 8, 31});   // OutputCycle:        {0, 64, light}
		expect(r.grid.at(16, 4)).to_equal(RGB{31, 0, 16});  // Section:            {light, 0, 128}
	});

	it("blacks out an armed clip-launch macro on the flash's on phase, and only then", _ {
		uitest::GridRig r;
		const MacroCell rows[] = {
		    {.kind = MacroKind::ClipLaunch, .clip_active = true, .clip_armed = true, .arm_flash = true},
		    {.kind = MacroKind::ClipLaunch, .clip_active = true, .clip_armed = true, .arm_flash = false},
		    {.kind = MacroKind::ClipLaunch, .clip_active = true, .clip_armed = false, .arm_flash = true},
		    // The flash blacks out clip-launch macros only -- legacy's black assignment is inside
		    // that case (:2806-2811).
		    {.kind = MacroKind::Section, .clip_armed = true, .arm_flash = true},
		};
		macros(r.frame, 16, rows, -1);
		expect(r.grid.at(16, 0)).to_equal(kBlack);
		expect(r.grid.at(16, 1)).to_equal(RGB{0, 31, 0});
		expect(r.grid.at(16, 2)).to_equal(RGB{0, 31, 0});
		expect(r.grid.at(16, 3)).to_equal(RGB{31, 0, 16});

		// The `armed` legacy returns (:2795, :2806-2807, :2836).
		expect(macro_wants_arm_flash(rows[0])).to_be_true();
		expect(macro_wants_arm_flash(rows[1])).to_be_true();   // armed clip, flash phase off
		expect(macro_wants_arm_flash(rows[2])).to_be_true();   // flash already on
		expect(macro_wants_arm_flash(MacroCell{.kind = MacroKind::ClipLaunch})).to_be_false();
	});

	it("brightens a held macro to its full colour and dims the rest, per issue #3244", _ {
		uitest::GridRig r;
		const MacroCell rows[] = {
		    {.kind = MacroKind::Section, .held = true},
		    {.kind = MacroKind::Section, .held = false},
		};
		macros(r.frame, 16, rows, -1);
		// `adjust(255, 1)` is the colour itself; `adjust(255, 8)` an eighth of it (:2831-2834).
		expect(r.grid.at(16, 0)).to_equal(RGB{255, 0, 128});
		expect(r.grid.at(16, 1)).to_equal(RGB{31, 0, 16});
	});

	it("knocks the other macros back while one is selected, and greys the selected empty slot", _ {
		uitest::GridRig r;
		const MacroCell rows[] = {
		    {.kind = MacroKind::None},
		    {.kind = MacroKind::Section},
		};
		// Row 0 selected: `dark = 32` for it, `light = 208` for every other row (:2790-2793).
		macros(r.frame, 16, rows, 0);
		expect(r.grid.at(16, 0)).to_equal(RGB{4, 4, 4});     // {32,32,32} / 8
		expect(r.grid.at(16, 1)).to_equal(RGB{26, 0, 16});   // {208,0,128} / 8

		// Nothing selected: the empty slot is black and the section keeps full `light`.
		macros(r.frame, 16, rows, -1);
		expect(r.grid.at(16, 0)).to_equal(kBlack);
		expect(r.grid.at(16, 1)).to_equal(RGB{31, 0, 16});
	});

	it("agrees with renderMacros on every kind x active x armed x flash x held x selection", _ {
		constexpr MacroKind kinds[] = {MacroKind::None, MacroKind::ClipLaunch, MacroKind::OutputCycle,
		                               MacroKind::Section};
		constexpr int32_t selections[] = {-1, 0, 3};
		for (MacroKind kind : kinds) {
			for (bool active : {false, true}) {
				for (bool armed : {false, true}) {
					for (bool flash : {false, true}) {
						for (bool held : {false, true}) {
							for (int32_t selected : selections) {
								uitest::GridRig r;
								const MacroCell cell{.kind = kind, .clip_active = active, .clip_armed = armed,
								                     .arm_flash = flash, .held = held};
								const MacroCell rows[] = {cell, cell, cell, cell, cell, cell, cell, cell};
								macros(r.frame, 16, rows, selected);
								for (int32_t y = 0; y < 8; y++) {
									expect(r.grid.at(16, y))
									    .to_equal(legacy_macro_colour(kind, active, armed, flash, held, selected, y));
								}
							}
						}
					}
				}
			}
		}
	});

	// -----------------------------------------------------------------------------------------
	// Velocity column -- renderNoteSquare
	// (gui/views/automation/editor_layout/note/velocity.cpp:75-104)
	// -----------------------------------------------------------------------------------------

	it("grows the velocity bar by the minPadDisplayValues table, not by a division", _ {
		// **Brief amendment.** The brief says "height = velocity * 8 / 128"; legacy tests
		// `value >= minPadDisplayValues[yDisplay]` with {0, 17, 33, 49, 65, 81, 97, 113}
		// (velocity.cpp:44, :87). Velocity 1 would be no rows under the brief's rule and is one row
		// under legacy's, because row 0's minimum is 0.
		struct Case { uint8_t velocity; int32_t top; };
		constexpr Case cases[] = {{0, 0}, {1, 0}, {64, 3}, {127, 7}};
		for (const Case& c : cases) {
			uitest::GridRig r;
			velocity_column(r.frame, 4, VelocityData{.velocity = c.velocity, .square = NoteSquare::Head});
			for (int32_t y = 0; y < 8; y++) {
				expect(r.grid.at(4, y)).to_equal(y <= c.top ? kVelocityRow[y] : kBlack);
			}
		}
	});

	it("picks the head, tail and blur ramps by square type, and erases an empty square", _ {
		uitest::GridRig r;
		velocity_column(r.frame, 0, VelocityData{.velocity = 127, .square = NoteSquare::Head});
		velocity_column(r.frame, 1, VelocityData{.velocity = 127, .square = NoteSquare::Tail});
		velocity_column(r.frame, 2, VelocityData{.velocity = 127, .square = NoteSquare::Blurred});
		velocity_column(r.frame, 3, VelocityData{.velocity = 127, .square = NoteSquare::None});
		for (int32_t y = 0; y < 8; y++) {
			expect(r.grid.at(0, y)).to_equal(kVelocityRow[y]);      // velocity.cpp:89-91
			expect(r.grid.at(1, y)).to_equal(kVelocityRowTail[y]);  // :92-94
			expect(r.grid.at(2, y)).to_equal(kVelocityRowBlur[y]);  // :95-97
			expect(r.grid.at(3, y)).to_equal(kBlack);               // :82-84, "erase pad"
		}
		// The ramp runs blue at the bottom to red at the top (:31-32).
		expect(kVelocityRow[0]).to_equal(RGB{0, 0, 255});
		expect(kVelocityRow[7]).to_equal(RGB{255, 0, 0});
	});

	// -----------------------------------------------------------------------------------------
	// Automation column -- renderAutomationUnipolarSquare / renderAutomationBipolarSquare
	// (gui/views/automation/editor_layout/mod_controllable.cpp:130-253)
	// -----------------------------------------------------------------------------------------

	it("fills a unipolar column from the bottom, and leaves it empty at the parameter's floor", _ {
		// min / mid / max, where "min" is legacy's `if (knobPos)` guard at :224: a knob position of
		// 0 draws nothing even though row 0's minimum is also 0.
		struct Case { int32_t knob_pos; int32_t top; };
		constexpr Case cases[] = {{0, -1}, {64, 3}, {128, 7}};
		for (const Case& c : cases) {
			uitest::GridRig r;
			automation_column(r.frame, 5, AutomationData{.knob_pos = c.knob_pos, .automated = true});
			for (int32_t y = 0; y < 8; y++) {
				expect(r.grid.at(5, y)).to_equal(y <= c.top ? kAutomationRow[y] : kBlack);
			}
		}
	});

	it("uses the tail ramp when the parameter is not automated", _ {
		uitest::GridRig r;
		automation_column(r.frame, 5, AutomationData{.knob_pos = 128, .automated = false}); // :233
		automation_column(r.frame, 6, AutomationData{.knob_pos = 128, .automated = true});  // :228-230
		for (int32_t y = 0; y < 8; y++) {
			expect(r.grid.at(5, y)).to_equal(kAutomationRowTail[y]);
			expect(r.grid.at(6, y)).to_equal(kAutomationRow[y]);
		}
		// "VU meter style colours" (:41): green at the bottom, red at the top.
		expect(kAutomationRow[0]).to_equal(RGB{0, 255, 0});
		expect(kAutomationRow[7]).to_equal(RGB{255, 0, 0});
	});

	it("draws a bipolar column outward from the middle, in the four-entry ramp", _ {
		// A non-patch-cable bipolar parameter: middle 64 (:142-143).
		uitest::GridRig r;
		// Positive (above the middle): the top half only, ramp indexed `7 - y` (:176-191).
		automation_column(r.frame, 0, AutomationData{.knob_pos = 128, .bipolar = true, .automated = true});
		for (int32_t y = 0; y < 4; y++) {
			expect(r.grid.at(0, y)).to_equal(kBlack);                       // :146-150
		}
		for (int32_t y = 4; y < 8; y++) {
			expect(r.grid.at(0, y)).to_equal(kAutomationBipolar[7 - y]);
		}
		// Zero (at the middle): nothing at all (`doRender` never set, :152-172).
		automation_column(r.frame, 1, AutomationData{.knob_pos = 64, .bipolar = true, .automated = true});
		for (int32_t y = 0; y < 8; y++) {
			expect(r.grid.at(1, y)).to_equal(kBlack);
		}
		// Negative (below the middle): the bottom half only, ramp indexed `y`.
		automation_column(r.frame, 2, AutomationData{.knob_pos = 0, .bipolar = true, .automated = true});
		for (int32_t y = 0; y < 4; y++) {
			expect(r.grid.at(2, y)).to_equal(kAutomationBipolar[y]);
		}
		for (int32_t y = 4; y < 8; y++) {
			expect(r.grid.at(2, y)).to_equal(kBlack);
		}
		// Rows 3 and 4 both take the ramp's last entry -- green, nearest the middle (:52).
		expect(kAutomationBipolar[3]).to_equal(RGB{0, 255, 0});
		expect(kAutomationBipolar[0]).to_equal(RGB{255, 0, 0});
	});

	it("moves a patch cable's middle to zero and reads the patch-cable tables", _ {
		uitest::GridRig r;
		// -128 is the patch cable's floor: the whole bottom half lights (:64, :158-161).
		automation_column(r.frame, 0, AutomationData{.knob_pos = -128, .patch_cable = true, .bipolar = true,
		                                             .automated = true});
		for (int32_t y = 0; y < 4; y++) {
			expect(r.grid.at(0, y)).to_equal(kAutomationBipolar[y]);
		}
		// -30 sits in row 3's range (-32 .. -1, legacy's summary at :71-79): only the row nearest
		// the middle lights.
		automation_column(r.frame, 1, AutomationData{.knob_pos = -30, .patch_cable = true, .bipolar = true,
		                                             .automated = true});
		expect(r.grid.at(1, 3)).to_equal(kAutomationBipolar[3]);
		for (int32_t y = 0; y < 3; y++) {
			expect(r.grid.at(1, y)).to_equal(kBlack);
		}
		// 0 is the middle for a patch cable, so the column is empty (:138-139).
		automation_column(r.frame, 2, AutomationData{.knob_pos = 0, .patch_cable = true, .bipolar = true,
		                                             .automated = true});
		for (int32_t y = 0; y < 8; y++) {
			expect(r.grid.at(2, y)).to_equal(kBlack);
		}
	});

	it("draws the pad-selection cursor in the blur ramp, and greys the rows it did not light", _ {
		uitest::GridRig r;
		automation_column(r.frame, 5, AutomationData{.knob_pos = 64, .automated = true, .pad_selected = true});
		for (int32_t y = 0; y < 4; y++) {
			expect(r.grid.at(5, y)).to_equal(kAutomationRowBlur[y]); // :242-245
		}
		for (int32_t y = 4; y < 8; y++) {
			expect(r.grid.at(5, y)).to_equal(palette::grey);         // :246-247
		}
		// On a bipolar column the unused half returns before the cursor is considered (:149), so
		// those pads stay black rather than going grey.
		automation_column(r.frame, 6, AutomationData{.knob_pos = 128, .bipolar = true, .automated = true,
		                                             .pad_selected = true});
		for (int32_t y = 0; y < 4; y++) {
			expect(r.grid.at(6, y)).to_equal(kBlack);
		}
		expect(r.grid.at(6, 7)).to_equal(kAutomationBipolarBlur[0]);
	});

	// -----------------------------------------------------------------------------------------
	// Tick squares -- PadLEDs::setTickSquares' colour half (hid/led/pad_leds.cpp:165-229, :277-291)
	// -----------------------------------------------------------------------------------------

	it("overlays the playhead on the rows that have one, leaving the rest of the grid alone", _ {
		uitest::GridRig r;
		// Paint something first: tick squares substitute for the square's own colour rather than
		// clearing anything (`prepareColour`, pad_leds.cpp:283-291).
		for (int32_t y = 0; y < 8; y++) {
			for (int32_t x = 0; x < 16; x++) {
				r.frame.pad(x, y, RGB{9, 9, 9});
			}
		}
		const TickSquare per_row[] = {
		    {.col = kNoTickSquare},                                  // no playhead in this row
		    {.col = 2, .colour = TickColour::Normal},
		    {.col = 4, .colour = TickColour::Muted},                 // the brief's case
		    {.col = 6, .colour = TickColour::Red},
		};
		constexpr RGB kMuted = RGB{255, 160, 0}; // colours::yellow_orange, the factory muted colour
		tick_squares(r.frame, per_row, kMuted);

		expect(r.grid.at(0, 0)).to_equal(RGB{9, 9, 9});
		expect(r.grid.at(2, 1)).to_equal(kPlayheadColour);       // flashColours[0] (:278)
		expect(r.grid.at(2, 1)).to_equal(RGB{130, 120, 130});
		expect(r.grid.at(4, 2)).to_equal(legacy_muted(kMuted));  // :192-202
		expect(r.grid.at(6, 3)).to_equal(palette::red);          // flashColours[2] (:280)
		// Untouched columns keep what the view painted.
		expect(r.grid.at(3, 1)).to_equal(RGB{9, 9, 9});
		expect(r.grid.at(4, 4)).to_equal(RGB{9, 9, 9});
	});

	it("adds 1, 2 and 4 to a muted colour's channels of 64 or more, and wraps at 255", _ {
		// pad_leds.cpp:192-202: `if (channel >= 64) channel += (1 << idx)` for r, g, b, in `uint8_t`
		// and with no clamp.
		expect(tick_muted_colour(RGB{255, 160, 0})).to_equal(RGB{0, 162, 0}); // 255 wraps to 0
		expect(tick_muted_colour(RGB{64, 64, 64})).to_equal(RGB{65, 66, 68});
		expect(tick_muted_colour(RGB{63, 63, 63})).to_equal(RGB{63, 63, 63}); // below the threshold
		expect(tick_muted_colour(RGB{200, 63, 64})).to_equal(RGB{201, 63, 68});
		for (RGB c : {RGB{255, 160, 0}, RGB{64, 64, 64}, RGB{63, 63, 63}, RGB{200, 63, 64}}) {
			expect(tick_muted_colour(c)).to_equal(legacy_muted(c));
		}
		// The three kinds (:189-208, :277-291).
		expect(tick_colour(TickColour::Normal, RGB{255, 160, 0})).to_equal(kPlayheadColour);
		expect(tick_colour(TickColour::Red, RGB{255, 160, 0})).to_equal(palette::red);
		expect(tick_colour(TickColour::Muted, RGB{255, 160, 0})).to_equal(RGB{0, 162, 0});
	});

	// -----------------------------------------------------------------------------------------
	// Goldens
	// -----------------------------------------------------------------------------------------

	it("records the whole grid for each component", _ {
		uitest::GridRig r;
		vu_meter(r.frame, 16, 16.7f, 12.8f);
		expect_golden(self, "../ui/widgets/vu_meter.state", r.state());
	});

	it("records the macro column", _ {
		uitest::GridRig r;
		const MacroCell rows[] = {
		    {.kind = MacroKind::None},
		    {.kind = MacroKind::None},
		    {.kind = MacroKind::ClipLaunch, .clip_active = false},
		    {.kind = MacroKind::ClipLaunch, .clip_active = true},
		    {.kind = MacroKind::ClipLaunch, .clip_active = true, .clip_armed = true, .arm_flash = true},
		    {.kind = MacroKind::OutputCycle},
		    {.kind = MacroKind::Section},
		    {.kind = MacroKind::Section, .held = true},
		};
		macros(r.frame, 16, rows, 1);
		expect_golden(self, "../ui/widgets/macros.state", r.state());
	});

	it("records the velocity and automation bars side by side", _ {
		uitest::GridRig r;
		r.frame.region(Rect{0, 0, 16, 8}, [&](GridFrame& main) {
			velocity_column(main, 0, VelocityData{.velocity = 0, .square = NoteSquare::Head});
			velocity_column(main, 1, VelocityData{.velocity = 64, .square = NoteSquare::Head});
			velocity_column(main, 2, VelocityData{.velocity = 127, .square = NoteSquare::Head});
			velocity_column(main, 3, VelocityData{.velocity = 127, .square = NoteSquare::Tail});
			velocity_column(main, 4, VelocityData{.velocity = 127, .square = NoteSquare::Blurred});
			automation_column(main, 6, AutomationData{.knob_pos = 0, .automated = true});
			automation_column(main, 7, AutomationData{.knob_pos = 64, .automated = true});
			automation_column(main, 8, AutomationData{.knob_pos = 128, .automated = true});
			automation_column(main, 9, AutomationData{.knob_pos = 128, .automated = false});
			automation_column(main, 10, AutomationData{.knob_pos = 128, .bipolar = true, .automated = true});
			automation_column(main, 11, AutomationData{.knob_pos = 64, .bipolar = true, .automated = true});
			automation_column(main, 12, AutomationData{.knob_pos = 0, .bipolar = true, .automated = true});
			automation_column(main, 13, AutomationData{.knob_pos = -128, .patch_cable = true, .bipolar = true,
			                                           .automated = true});
			automation_column(main, 14, AutomationData{.knob_pos = 64, .automated = true, .pad_selected = true});
		});
		expect_golden(self, "../ui/widgets/columns_bars.state", r.state());
	});

	it("records the tick-square overlay over a painted grid", _ {
		uitest::GridRig r;
		for (int32_t y = 0; y < 8; y++) {
			for (int32_t x = 0; x < 16; x++) {
				r.frame.pad(x, y, RGB{9, 9, 9});
			}
		}
		const TickSquare per_row[] = {
		    {.col = kNoTickSquare},
		    {.col = 2, .colour = TickColour::Normal},
		    {.col = 4, .colour = TickColour::Muted},
		    {.col = 6, .colour = TickColour::Red},
		    {.col = 8, .colour = TickColour::Normal},
		    {.col = kNoTickSquare},
		    {.col = 12, .colour = TickColour::Muted},
		    {.col = 14, .colour = TickColour::Red},
		};
		tick_squares(r.frame, per_row, RGB{255, 160, 0});
		expect_golden(self, "../ui/widgets/tick_squares.state", r.state());
	});
});
// clang-format on

// Not CPPSPEC_SPEC(grid_columns): create_test_sourcelist derives the extern function name from this
// file's path relative to tests/spec (slashes become underscores), i.e. `ui_grid_columns_spec`,
// which does not match the `grid_columns` describe-block name the macro would assume. Spelled out
// by hand, as the other ui specs do, so the exported symbol matches what the generated driver
// declares `extern`.
int ui_grid_columns_spec(int argc, char** const argv) {
	return CppSpec::parse(argc, argv).add_spec(grid_columns).exec().is_success() ? EXIT_SUCCESS : EXIT_FAILURE;
}
