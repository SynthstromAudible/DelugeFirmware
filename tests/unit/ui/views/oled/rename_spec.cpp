#include "ui/views/oled/rename.h"
#include "unit/ui/gfx/support/golden.h"
#include "unit/ui/views/oled/support/fixtures.h"

#include "cppspec.hpp"

#include "support/spec_main.h"

using namespace deluge::ui;
using namespace deluge::ui::views::oled;
using namespace deluge::ui::views::oled::test;

namespace {

constexpr RenameWindow kEmpty{.title = "RENAME", .text = ""};
constexpr RenameWindow kTyped{.title = "RENAME", .text = "KICK 2"};

/// The window with the cursor lit at the end of its text.
template <class Rig>
void draw(Rig& r, const RenameWindow& d) {
	widgets::oled::TextEditState edit{};
	edit.cursor = static_cast<int16_t>(d.text.size());
	edit.cursor_lit = true;
	rename_window(r.frame, d, edit);
}

} // namespace

// clang-format off
describe rename_window_v("ui::views::oled::rename_window", $ {
	it("draws the typed name where the empty window has none", _ {
		uitest::Rig48 empty;
		draw(empty, kEmpty);
		uitest::Rig48 typed;
		draw(typed, kTyped);
		expect(typed.pbm() == empty.pbm()).to_equal(false);
	});

	it("draws no cursor bar while the cursor is dark", _ {
		uitest::Rig48 lit;
		draw(lit, kEmpty);
		uitest::Rig48 dark;
		widgets::oled::TextEditState edit{};
		edit.cursor_lit = false;
		rename_window(dark.frame, kEmpty, edit);
		expect(dark.pbm() == lit.pbm()).to_equal(false);
	});

	it("matches its goldens", _ {
		uitest::Rig48 e48;
		draw(e48, kEmpty);
		expect_golden(self, "ui/views/oled/rename_empty_48.pbm", e48.pbm());
		uitest::Rig64 e64;
		draw(e64, kEmpty);
		expect_golden(self, "ui/views/oled/rename_empty_64.pbm", e64.pbm());
		uitest::Rig48 t48;
		draw(t48, kTyped);
		expect_golden(self, "ui/views/oled/rename_typed_48.pbm", t48.pbm());
		uitest::Rig64 t64;
		draw(t64, kTyped);
		expect_golden(self, "ui/views/oled/rename_typed_64.pbm", t64.pbm());
	});
});
// clang-format on

DELUGE_SPEC(rename_spec, rename_window_v)
