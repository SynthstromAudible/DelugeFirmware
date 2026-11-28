#include "ui/gfx/grid/palette.h"
#include "ui/views/grid/keyboard.h"
#include "unit/ui/gfx/support/golden.h"
#include "unit/ui/widgets/support/frames.h"

#include "cppspec.hpp"

#include "support/printers/rgb_printer.h"
#include "support/spec_main.h"
#include <cstdint>

using namespace deluge::ui::views::grid;
using deluge::ui::widgets::grid::KeyboardLayoutKind;
using deluge::ui::widgets::grid::text_keyboard_char_at;
using deluge::ui::widgets::grid::TextKeyboardState;
namespace uitest = deluge::ui::test;

namespace {

constexpr KeyboardPads kQwerty{.layout = KeyboardLayoutKind::Qwerty, .home_row = 2, .dark = false};
constexpr KeyboardPads kDark{.layout = KeyboardLayoutKind::Qwerty, .home_row = 2, .dark = true};

} // namespace

// clang-format off
describe keyboard_v("ui::views::grid::keyboard", $ {
	it("draws the QWERTY keys on home row 2, and matches its golden", _ {
		uitest::GridRig r;
		TextKeyboardState kb;
		(void)keyboard(r.frame, kQwerty, kb);
		expect_golden(self, "ui/views/grid/keyboard_qwerty.state", r.state());
	});

	it("paints every main pad black when dark", _ {
		uitest::GridRig r;
		TextKeyboardState kb;
		(void)keyboard(r.frame, kDark, kb);
		for (uint8_t y = 0; y < 8; y++) {
			for (uint8_t x = 0; x < 16; x++) {
				expect(r.grid.at(x, y)).to_equal(deluge::gui::colours::black);
			}
		}
	});

	it("leaves the sidebar alone", _ {
		uitest::GridRig lit;
		lit.grid.set(16, 3, deluge::gui::colours::red);
		TextKeyboardState kb;
		(void)keyboard(lit.frame, kDark, kb);
		expect(lit.grid.at(16, 3)).to_equal(deluge::gui::colours::red);
	});

	it("returns a pressed key's character whether or not it is drawn", _ {
		// The pad that types 'Q' on home row 2, found through the toolkit's own lookup.
		int32_t qx = -1;
		int32_t qy = -1;
		for (int32_t y = 0; y < 8; y++) {
			for (int32_t x = 0; x < 16; x++) {
				if (text_keyboard_char_at(KeyboardLayoutKind::Qwerty, x, y, 2, false) == 'Q') {
					qx = x;
					qy = y;
				}
			}
		}
		expect(qx).to_be_greater_than(-1);
		for (const KeyboardPads& d : {kQwerty, kDark}) {
			uitest::GridRig r;
			const deluge::ui::widgets::core::PadEvent press{
			    .x = static_cast<uint8_t>(qx), .y = static_cast<uint8_t>(qy), .velocity = 64, .pressed = true};
			r.input.pads = std::span<const deluge::ui::widgets::core::PadEvent>(&press, 1);
			TextKeyboardState kb;
			const deluge::ui::widgets::core::Response resp = keyboard(r.frame, d, kb);
			expect(resp.pressed).to_be_true();
			expect(static_cast<int>(resp.index)).to_equal(static_cast<int>('Q'));
		}
	});
});
// clang-format on

DELUGE_SPEC(views_grid_keyboard_spec, keyboard_v)
