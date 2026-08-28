#include "../gfx/support/golden.h"
#include "support/frames.h"
#include "ui/grid/keyboard.h"

#include "cppspec.hpp"

#include "../support/spec_main.h"
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

using namespace deluge::ui;
using namespace deluge::ui::grid;
using deluge::gfx::grid::Rect;
namespace uitest = deluge::ui::test;
namespace palette = deluge::gui::colours;

// The on-pad text keyboard (QwertyUI::drawKeys / padAction) and the two legacy adapters.
//
// The character table below is transcribed straight out of gui/ui/qwerty_ui.cpp:180-203 and every
// reachable pad is compared against it for all three layouts, so a typo in the port's copy of the
// table fails here rather than on a device.

namespace {

// --- keyboardChars (gui/ui/qwerty_ui.cpp:180-203), transcribed --------------------------------
const char legacy_keyboard_chars[][5][11] = {{
                                                 {'1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-'},
                                                 {'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', 0},
                                                 {'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', 0, '\''},
                                                 {'Z', 'X', 'C', 'V', 'B', 'N', 'M', ',', '.', 0, 0},
                                                 {0, 0, ' ', ' ', ' ', ' ', ' ', ' ', 0, 0, 0},
                                             },
                                             {
                                                 {'1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-'},
                                                 {'A', 'Z', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', 0},
                                                 {'Q', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', 'M', '\''},
                                                 {'W', 'X', 'C', 'V', 'B', 'N', ',', '.', 0, 0, 0},
                                                 {0, 0, ' ', ' ', ' ', ' ', ' ', ' ', 0, 0, 0},
                                             },
                                             {
                                                 {'1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-'},
                                                 {'Q', 'W', 'E', 'R', 'T', 'Z', 'U', 'I', 'O', 'P', 'U'},
                                                 {'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', 'O', 'A'},
                                                 {'Y', 'X', 'C', 'V', 'B', 'N', 'M', ',', '.', '\'', 0},
                                                 {0, 0, ' ', ' ', ' ', ' ', ' ', ' ', 0, 0, 0},
                                             }};

constexpr int32_t kHomeRow = kTextKeyboardHomeRow; // definitions_cxx.hpp:848

// A rig plus the storage its pad-event span points into, so a spec can hold shift and hit a key in
// the same tick the way a player does. The events live in the struct, not in a shared static, so
// several rigs can coexist in one example without reseating each other's spans.
struct KeyRig {
	std::vector<PadEvent> events;
	uitest::GridRig rig;

	KeyRig(std::vector<PadEvent> pads, const std::vector<std::pair<int, int>>& held) : events(std::move(pads)) {
		rig.input.pads = events;
		for (auto [x, y] : held) {
			rig.input.held_pads.set(static_cast<uint8_t>(x), static_cast<uint8_t>(y), true);
		}
	}
};

// --- Fakes for the two adapters ---------------------------------------------------------------
// Deliberately *not* the real gui/ classes: the adapters exist so ui/grid never includes gui/,
// and a spec that reached for KeyboardLayout would defeat the point. These carry the exact method
// shapes the header documents.
struct FakeLayout {
	bool rendered_pads = false;
	bool rendered_sidebar = false;

	void renderPads(RGB image[][kLegacyImageCols]) {
		rendered_pads = true;
		image[0][0] = RGB{10, 20, 30};
		image[7][15] = RGB{40, 50, 60};
	}
	void renderSidebarPads(RGB image[][kLegacyImageCols]) {
		rendered_sidebar = true;
		image[2][16] = RGB{70, 80, 90};
		image[2][17] = RGB{100, 110, 120};
	}
};

struct FakeColumn {
	int32_t seen_column = -1;
	void* seen_layout = nullptr;

	void renderColumn(RGB image[][kLegacyImageCols], int32_t column, void* layout) {
		seen_column = column;
		seen_layout = layout;
		for (int32_t y = 0; y < kLegacyImageRows; y++) {
			image[y][column] = RGB{static_cast<uint8_t>(y + 1), 0, 0};
		}
		// A stray write outside the column: control_column must not copy it out.
		image[0][0] = RGB{255, 255, 255};
	}
};

} // namespace

// clang-format off
describe keyboard_spec("ui/grid/keyboard", $ {

	// --- text_keyboard_char_at ----------------------------------------------------------------

	it("matches the brief's three named cases", _ {
		expect(text_keyboard_char_at(KeyboardLayoutKind::Qwerty, 3, kHomeRow + 2, kHomeRow, false)).to_equal('1');
		expect(text_keyboard_char_at(KeyboardLayoutKind::Qwerty, 3, kHomeRow + 1, kHomeRow, false)).to_equal('Q');
		expect(text_keyboard_char_at(KeyboardLayoutKind::Azerty, 3, kHomeRow + 1, kHomeRow, false)).to_equal('A');
	});

	it("reproduces the whole legacy table for every layout", _ {
		// qwerty_ui.cpp:255 bounds the lookup; :267-268 is the index; :269 makes a 0 entry type
		// nothing. Sweep the entire rectangle for all three layouts.
		const KeyboardLayoutKind kinds[] = {KeyboardLayoutKind::Qwerty, KeyboardLayoutKind::Azerty,
		                                    KeyboardLayoutKind::Qwertz};
		for (int32_t layout = 0; layout < 3; layout++) {
			for (int32_t y = kHomeRow - 2; y <= kHomeRow + 2; y++) {
				for (int32_t x = 3; x < 14; x++) {
					const char want = legacy_keyboard_chars[layout][kHomeRow - y + 2][x - 3];
					expect(text_keyboard_char_at(kinds[layout], x, y, kHomeRow, false)).to_equal(want);
				}
			}
		}
	});

	it("types nothing outside the key rectangle", _ {
		expect(text_keyboard_char_at(KeyboardLayoutKind::Qwerty, 2, kHomeRow, kHomeRow, false)).to_equal(0);
		expect(text_keyboard_char_at(KeyboardLayoutKind::Qwerty, 3, kHomeRow + 3, kHomeRow, false)).to_equal(0);
		expect(text_keyboard_char_at(KeyboardLayoutKind::Qwerty, 3, kHomeRow - 3, kHomeRow, false)).to_equal(0);
		// The shift pads themselves type nothing (x = 1, 2 is left of the rectangle at :255).
		expect(text_keyboard_char_at(KeyboardLayoutKind::Qwerty, 1, kHomeRow - 1, kHomeRow, false)).to_equal(0);
	});

	it("types spaces from the space bar and actions from backspace and enter", _ {
		for (int32_t x = 5; x < 11; x++) {
			expect(text_keyboard_char_at(KeyboardLayoutKind::Qwerty, x, kHomeRow - 2, kHomeRow, false))
			    .to_equal(' ');
		}
		// The space row's outer cells are 0 in the table.
		expect(text_keyboard_char_at(KeyboardLayoutKind::Qwerty, 3, kHomeRow - 2, kHomeRow, false)).to_equal(0);
		expect(text_keyboard_char_at(KeyboardLayoutKind::Qwerty, 11, kHomeRow - 2, kHomeRow, false)).to_equal(0);

		// Backspace (:205) and enter (:232), both two pads wide.
		expect(text_keyboard_char_at(KeyboardLayoutKind::Qwerty, 14, kHomeRow + 2, kHomeRow, false))
		    .to_equal(kTextKeyboardBackspace);
		expect(text_keyboard_char_at(KeyboardLayoutKind::Qwerty, 15, kHomeRow + 2, kHomeRow, false))
		    .to_equal(kTextKeyboardBackspace);
		expect(text_keyboard_char_at(KeyboardLayoutKind::Qwerty, 14, kHomeRow, kHomeRow, false))
		    .to_equal(kTextKeyboardEnter);
		expect(text_keyboard_char_at(KeyboardLayoutKind::Qwerty, 15, kHomeRow, kHomeRow, false))
		    .to_equal(kTextKeyboardEnter);
	});

	it("applies shift to legacy's four digit-row keys and nothing else", _ {
		// :283-302 -- only on y == home_row + 2, only these four.
		expect(text_keyboard_char_at(KeyboardLayoutKind::Qwerty, 3, kHomeRow + 2, kHomeRow, true)).to_equal('!');
		expect(text_keyboard_char_at(KeyboardLayoutKind::Qwerty, 5, kHomeRow + 2, kHomeRow, true)).to_equal('#');
		expect(text_keyboard_char_at(KeyboardLayoutKind::Qwerty, 8, kHomeRow + 2, kHomeRow, true)).to_equal('^');
		expect(text_keyboard_char_at(KeyboardLayoutKind::Qwerty, 13, kHomeRow + 2, kHomeRow, true)).to_equal('_');
		// Untouched digits, and the letters (legacy cases those from the text, :337-345).
		expect(text_keyboard_char_at(KeyboardLayoutKind::Qwerty, 4, kHomeRow + 2, kHomeRow, true)).to_equal('2');
		expect(text_keyboard_char_at(KeyboardLayoutKind::Qwerty, 3, kHomeRow + 1, kHomeRow, true)).to_equal('Q');
		// The '-' key only shifts on the digit row; AZERTY's row-2 apostrophe is not a shift key.
		expect(text_keyboard_char_at(KeyboardLayoutKind::Azerty, 13, kHomeRow, kHomeRow, true)).to_equal('\'');
	});

	// --- text_keyboard ------------------------------------------------------------------------

	it("paints the drawKeys colour map", _ {
		uitest::GridRig r;
		TextKeyboardState state;
		text_keyboard(r.frame, state, KeyboardLayoutKind::Qwerty);

		// Backspace red (:111) and enter green (:114), both pads of each.
		expect(r.grid.at(14, kHomeRow + 2)).to_equal(palette::red);
		expect(r.grid.at(15, kHomeRow + 2)).to_equal(palette::red);
		expect(r.grid.at(14, kHomeRow)).to_equal(palette::green);
		expect(r.grid.at(15, kHomeRow)).to_equal(palette::green);
		// Shift blue on both sides (:117-118).
		expect(r.grid.at(1, kHomeRow - 1)).to_equal(palette::blue);
		expect(r.grid.at(2, kHomeRow - 1)).to_equal(palette::blue);
		expect(r.grid.at(13, kHomeRow - 1)).to_equal(palette::blue);
		expect(r.grid.at(14, kHomeRow - 1)).to_equal(palette::blue);
		// Digits 64 (:83), the '-' key 10 (:92), qwer 10 (:84).
		expect(r.grid.at(3, kHomeRow + 2)).to_equal(RGB::monochrome(64));
		expect(r.grid.at(13, kHomeRow + 2)).to_equal(RGB::monochrome(10));
		expect(r.grid.at(3, kHomeRow + 1)).to_equal(RGB::monochrome(10));
		// Home keys (:95-102): 64 either side of the two 160s, 10 in the middle.
		expect(r.grid.at(5, kHomeRow)).to_equal(RGB::monochrome(64));
		expect(r.grid.at(6, kHomeRow)).to_equal(RGB::monochrome(160));
		expect(r.grid.at(7, kHomeRow)).to_equal(RGB::monochrome(10));
		expect(r.grid.at(9, kHomeRow)).to_equal(RGB::monochrome(160));
		expect(r.grid.at(10, kHomeRow)).to_equal(RGB::monochrome(64));
		expect(r.grid.at(13, kHomeRow)).to_equal(RGB::monochrome(10));
		// Space bar 160 (:105-107), and the pad beside it left clear.
		expect(r.grid.at(5, kHomeRow - 2)).to_equal(RGB::monochrome(160));
		expect(r.grid.at(10, kHomeRow - 2)).to_equal(RGB::monochrome(160));
		expect(r.grid.at(4, kHomeRow - 2)).to_equal(palette::black);
	});

	it("leaves the sidebar alone", _ {
		uitest::GridRig r;
		for (int32_t y = 0; y < 8; y++) {
			r.frame.pad(16, y, RGB{3, 3, 3});
			r.frame.pad(17, y, RGB{4, 4, 4});
		}
		TextKeyboardState state;
		text_keyboard(r.frame, state, KeyboardLayoutKind::Qwerty);
		// clearMainPadsWithoutSending (:76 -> pad_leds.cpp:236-240) stops at kDisplayWidth.
		expect(r.grid.at(16, 0)).to_equal(RGB{3, 3, 3});
		expect(r.grid.at(17, 7)).to_equal(RGB{4, 4, 4});
	});

	it("returns the typed character in the response index, and the pad velocity in velocity", _ {
		KeyRig k({{.x = 3, .y = kHomeRow + 1, .velocity = 64, .pressed = true}}, {});
		TextKeyboardState state;
		const Response resp = text_keyboard(k.rig.frame, state, KeyboardLayoutKind::Qwerty);

		expect(resp.pressed).to_be_true();
		expect((char)resp.index).to_equal('Q');
		expect((int32_t)resp.velocity).to_equal(64); // the key pad's own velocity, not the character
		expect(state.shift).to_be_false();
	});

	it("reports no character for a pad that types nothing", _ {
		KeyRig k({{.x = 0, .y = 0, .velocity = 64, .pressed = true}}, {});
		TextKeyboardState state;
		const Response resp = text_keyboard(k.rig.frame, state, KeyboardLayoutKind::Qwerty);
		expect(resp.pressed).to_be_true();
		expect((int32_t)resp.index).to_equal(-1);
	});

	it("shifts the digit row while a shift pad is held", _ {
		KeyRig k({{.x = 3, .y = kHomeRow + 2, .velocity = 64, .pressed = true}}, {{1, kHomeRow - 1}});
		TextKeyboardState state;
		const Response resp = text_keyboard(k.rig.frame, state, KeyboardLayoutKind::Qwerty);

		expect(state.shift).to_be_true();     // :284-287
		expect((char)resp.index).to_equal('!'); // :293-294
	});

	it("types the digit when shift and the digit are pressed in the same tick", _ {
		// The shift pad sits at home_row - 1 and the digit row at home_row + 2, so a bottom-up
		// sweep reaches shift first. Legacy runs padAction per event (qwerty_ui.cpp:205-330), where
		// the shift pad falls outside the key rectangle (:255) and types nothing while the digit
		// still types '!'. Taking the first press in scan order would have returned 0.
		KeyRig k({{.x = 1, .y = kHomeRow - 1, .velocity = 64, .pressed = true},
		          {.x = 3, .y = kHomeRow + 2, .velocity = 64, .pressed = true}},
		         {});
		TextKeyboardState state;
		const Response resp = text_keyboard(k.rig.frame, state, KeyboardLayoutKind::Qwerty);

		expect(state.shift).to_be_true();
		expect((char)resp.index).to_equal('!');
	});

	it("types the first character-producing key when two keys land in one tick", _ {
		KeyRig k({{.x = 0, .y = 0, .velocity = 64, .pressed = true},          // types nothing
		          {.x = 4, .y = kHomeRow + 1, .velocity = 64, .pressed = true}}, // 'W'
		         {});
		TextKeyboardState state;
		const Response resp = text_keyboard(k.rig.frame, state, KeyboardLayoutKind::Qwerty);
		expect((char)resp.index).to_equal('W');
	});

	it("records the keyboard's colour map", _ {
		uitest::GridRig r;
		TextKeyboardState state;
		text_keyboard(r.frame, state, KeyboardLayoutKind::Qwerty);
		expect_golden(self, "../ui/widgets/text_keyboard.state", r.state());
	});

	// --- adapters -----------------------------------------------------------------------------

	it("copies a legacy layout's whole image, sidebar included", _ {
		uitest::GridRig r;
		FakeLayout layout;
		keyboard(r.frame, layout);

		expect(layout.rendered_pads).to_be_true();
		expect(layout.rendered_sidebar).to_be_true();
		expect(r.grid.at(0, 0)).to_equal(RGB{10, 20, 30});
		expect(r.grid.at(15, 7)).to_equal(RGB{40, 50, 60});
		expect(r.grid.at(16, 2)).to_equal(RGB{70, 80, 90});
		expect(r.grid.at(17, 2)).to_equal(RGB{100, 110, 120});
	});

	it("copies only the requested column from a legacy control column", _ {
		uitest::GridRig r;
		for (int32_t y = 0; y < 8; y++) {
			r.frame.pad(0, y, RGB{5, 5, 5});
		}
		FakeColumn column;
		int32_t opaque_layout = 7;
		control_column(r.frame, 12, column, &opaque_layout);

		expect(column.seen_column).to_equal(12);
		expect(column.seen_layout == &opaque_layout).to_be_true();
		for (int32_t y = 0; y < 8; y++) {
			expect(r.grid.at(12, y)).to_equal(RGB{(uint8_t)(y + 1), 0, 0});
		}
		// The stray write at image[0][0] stays in the scratch buffer.
		expect(r.grid.at(0, 0)).to_equal(RGB{5, 5, 5});
	});

	it("clips a legacy image to a smaller region", _ {
		uitest::GridRig r;
		FakeLayout layout;
		r.frame.region(Rect{0, 0, 4, 4}, [&](GridFrame& small) { keyboard(small, layout); });
		expect(r.grid.at(0, 0)).to_equal(RGB{10, 20, 30});
		expect(r.grid.at(15, 7)).to_equal(palette::black); // outside the region
		expect(r.grid.at(16, 2)).to_equal(palette::black);
	});
});
DELUGE_SPEC(ui_keyboard_spec, keyboard_spec)
