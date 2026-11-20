/// @file
/// @brief The menu's walk -- opening the sound root, a descent and its Back, a horizontal chain's pages and columns,
///        Backs from three levels down, and the menu removed whole from three levels down as a long BACK removes it --
///        driven through a real `MenuScene` and `Shell` over the ports' fakes, with a model song of the spec's own and
///        no legacy song behind the menu.

#include "scenes/menu/context.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/menu/node.h"
#include "scenes/menu/nodes/nodes.h"

#include "gui/l10n/strings.h"
#include "shell/scene.h"

#include "support/fake_menu_shell.h"

#include "cppspec.hpp"
#include "support/matchers/value_matchers.h"

#include <cstdint>
#include <optional>

#include "support/spec_main.h"

using namespace deluge::scenes;
using deluge::l10n::String;
using deluge::scenes::menu::MenuContext;
using deluge::scenes::spec::cursor_row;
using deluge::scenes::spec::FakeMenuShell;
using deluge::scenes::spec::listed_at;
using deluge::scenes::spec::page_title;
using deluge::scenes::spec::row_of;
using deluge::scenes::spec::text_of;
using deluge::scenes::spec::walk_depth;
using deluge::ui::shell::SceneKind;

namespace {

namespace nodes = deluge::scenes::menu::nodes;

/// MASTER's columns are SYNTH MODE, VOLUME, PAN, TRANSPOSE and VIBRATO.
constexpr int8_t kVibratoColumn = 4;

/// @brief The focused column of the horizontal page at walk depth @p depth.
int32_t focused_column(int32_t depth) {
	return menu_scene().state().columns[depth].focused;
}

/// @brief From the sound root, down MASTER's row to the VIBRATO column, its strength page and the range source picker
///        that page's select opens: walk depth 3.
/// @return MASTER's row on the sound root.
int32_t walk_to_depth_3(FakeMenuShell& r, const MenuContext& ctx) {
	const int32_t master = row_of(nodes::sound_root, nodes::sound_master, ctx).value_or(0);
	r.turn(master, 100);
	r.select(300);
	menu_scene().state_for_spec().columns[1].focused = kVibratoColumn;
	r.shell.tick(400);
	r.select(600);
	r.select(900);
	return master;
}

} // namespace

// clang-format off
describe menu_walk_fakes("the menu's walk over the ports' fakes and a model song", ${
	it("opens the sound root on its title, its cursor on the first row it lists", _ {
		FakeMenuShell r;
		const MenuContext ctx = r.open_synth(nodes::sound_root);
		expect(ctx.root).to_equal(MenuContext::Root::Sound);
		expect(r.shell.stack().depth()).to_equal(size_t{1});
		expect(menu_scene().focused()).to_equal(&nodes::sound_root);
		expect(page_title()).to_equal(text_of(String::STRING_FOR_SOUND));
		expect(walk_depth()).to_equal(0);
		expect(cursor_row()).to_equal(0);
		expect(listed_at(nodes::sound_root, 0, ctx)).to_equal(&nodes::sound_actions);
	});

	it("descends into the row the cursor stands on, and BACK ascends to the same row", _ {
		FakeMenuShell r;
		const MenuContext ctx = r.open_synth(nodes::sound_root);
		const std::optional<int32_t> master = row_of(nodes::sound_root, nodes::sound_master, ctx);
		expect(master.has_value()).to_be_true();
		to_be_at_least(expect(master.value_or(0)), 1);
		r.turn(master.value_or(0), 100);
		expect(cursor_row()).to_equal(master.value_or(0));

		r.select(300);
		expect(walk_depth()).to_equal(1);
		expect(menu_scene().focused()).to_equal(&nodes::sound_master);
		expect(page_title()).to_equal(text_of(String::STRING_FOR_MASTER));
		expect(focused_column(1)).to_equal(0);

		r.back(600);
		expect(walk_depth()).to_equal(0);
		expect(menu_scene().focused()).to_equal(&nodes::sound_root);
		expect(page_title()).to_equal(text_of(String::STRING_FOR_SOUND));
		expect(cursor_row()).to_equal(master.value_or(0));
		expect(r.shell.stack().depth()).to_equal(size_t{1});
	});

	it("pages the filters group LPF, HPF, FILTER ROUTE, and SHIFT+CROSS_SCREEN follows the chain to EQ", _ {
		FakeMenuShell r;
		(void)r.open_synth(nodes::filters_group);
		expect(page_title()).to_equal(text_of(String::STRING_FOR_LPF));
		expect(static_cast<int>(menu_scene().horizontal_paging().total_pages)).to_equal(3);
		expect(focused_column(0)).to_equal(0);
		r.press(FakeMenuShell::kCrossScreenX, FakeMenuShell::kCrossScreenY, 300);
		expect(page_title()).to_equal(text_of(String::STRING_FOR_HPF));
		expect(static_cast<int>(menu_scene().horizontal_paging().page)).to_equal(1);
		r.press(FakeMenuShell::kCrossScreenX, FakeMenuShell::kCrossScreenY, 600);
		expect(page_title()).to_equal(text_of(String::STRING_FOR_FILTER_ROUTE));
		r.press(FakeMenuShell::kCrossScreenX, FakeMenuShell::kCrossScreenY, 900);
		expect(page_title()).to_equal(text_of(String::STRING_FOR_LPF));

		r.button(FakeMenuShell::kShiftX, FakeMenuShell::kShiftY, true, 1200);
		r.press(FakeMenuShell::kCrossScreenX, FakeMenuShell::kCrossScreenY, 1250);
		r.button(FakeMenuShell::kShiftX, FakeMenuShell::kShiftY, false, 1300);
		r.shell.tick(1305);
		expect(menu_scene().focused()).to_equal(&nodes::eq);
		expect(page_title()).to_equal(text_of(String::STRING_FOR_EQ));
		expect(walk_depth()).to_equal(0);
		expect(focused_column(0)).to_equal(0);
	});

	it("focuses a horizontal page's columns by their instrument buttons", _ {
		FakeMenuShell r;
		(void)r.open_synth(nodes::lpf);
		expect(page_title()).to_equal(text_of(String::STRING_FOR_LPF));
		expect(focused_column(0)).to_equal(0);
		r.press(FakeMenuShell::kKitX, FakeMenuShell::kKitY, 300);
		expect(focused_column(0)).to_equal(1);
		r.press(FakeMenuShell::kSynthX, FakeMenuShell::kSynthY, 600);
		expect(focused_column(0)).to_equal(0);
		expect(menu_scene().focused()).to_equal(&nodes::lpf);
	});

	it("climbs one level for each short BACK from three levels down, and the BACK at the root closes the menu", _ {
		FakeMenuShell r;
		const MenuContext ctx = r.open_synth(nodes::sound_root);
		const int32_t master = walk_to_depth_3(r, ctx);
		expect(menu_scene().focused()).to_equal(&nodes::source_selection_range);
		expect(walk_depth()).to_equal(3);

		r.back(1200);
		expect(walk_depth()).to_equal(2);
		expect(menu_scene().focused()).to_equal(&nodes::vibrato);
		r.back(1500);
		expect(walk_depth()).to_equal(1);
		expect(page_title()).to_equal(text_of(String::STRING_FOR_MASTER));
		expect(focused_column(1)).to_equal(kVibratoColumn);
		r.back(1800);
		expect(walk_depth()).to_equal(0);
		expect(page_title()).to_equal(text_of(String::STRING_FOR_SOUND));
		expect(cursor_row()).to_equal(master);
		expect(r.shell.stack().depth()).to_equal(size_t{1});
		r.back(2100);
		expect(r.shell.stack().depth()).to_equal(size_t{0});
	});

	it("leaves the whole menu from three levels down as a long BACK does, and reopens at its root", _ {
		// A long BACK is the host's `exitUI`, which removes the input-top scene however deep its walk is
		// (`ShellHostUI::exitUI`, as `SoundEditor::exitCompletely`). The host half is the timer; the scene's half is
		// that removal, done here as the host does it.
		FakeMenuShell r;
		const MenuContext ctx = r.open_synth(nodes::sound_root);
		const int32_t master = walk_to_depth_3(r, ctx);
		expect(walk_depth()).to_equal(3);

		expect(r.shell.stack().remove(r.shell.stack().input_top().kind())).to_be_true();
		r.shell.tick(1200);
		expect(r.shell.stack().depth()).to_equal(size_t{0});

		(void)r.open(nodes::sound_root, ctx, 1500);
		expect(walk_depth()).to_equal(0);
		expect(menu_scene().focused()).to_equal(&nodes::sound_root);
		expect(page_title()).to_equal(text_of(String::STRING_FOR_SOUND));
		// The root keeps the row it was left on, as each of main's `Submenu`s keeps its `current_item_`.
		expect(cursor_row()).to_equal(master);
	});
});
// clang-format on

DELUGE_SPEC(menu_walk_fakes_spec, menu_walk_fakes)
