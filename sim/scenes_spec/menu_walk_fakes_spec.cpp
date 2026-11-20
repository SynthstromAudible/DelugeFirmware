/// @file
/// @brief The menu's walk -- opening the sound root, a descent and its Back, a horizontal chain's pages and columns, a
///        Back from three levels down -- driven through a real `MenuScene` and `Shell` over the ports' fakes, with a
///        model song of the spec's own and no legacy song behind the menu.

#include "scenes/menu/bindings.h"
#include "scenes/menu/context.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/menu/node.h"
#include "scenes/menu/node_id.h"
#include "scenes/menu/nodes/nodes.h"
#include "scenes/menu/present.h"
#include "scenes/profile.h"
#include "scenes/registry.h"

#include "gui/l10n/l10n.h"
#include "gui/l10n/strings.h"
#include "notify/dirty_set.h"
#include "notify/notice_queue.h"
#include "shell/profiles/deluge.h"
#include "shell/scene.h"
#include "shell/shell.h"

#include "support/fake_host_rig.h"
#include "support/mocks/fake_surfaces.h"

#include "cppspec.hpp"
#include "support/matchers/value_matchers.h"

#include <array>
#include <cstdint>
#include <string>

#include "support/spec_main.h"

using namespace deluge::scenes;
using deluge::l10n::String;
using deluge::scenes::menu::MenuContext;
using deluge::scenes::menu::Node;
using deluge::ui::shell::SceneId;
using deluge::ui::shell::SceneKind;
using deluge::ui::shell::Shell;
using deluge::ui::shell::ShellConfig;

namespace {

namespace nodes = deluge::scenes::menu::nodes;

/// Board encoder 3 is Select (`kDelugeEncoders`); the matrix buttons are `kMatrix`'s (`shell/profiles/deluge.cpp`).
constexpr uint8_t kSelectEncoder = 3;
constexpr uint8_t kSelectEncButtonX = 4, kSelectEncButtonY = 3;
constexpr uint8_t kBackX = 7, kBackY = 1;
constexpr uint8_t kShiftX = 8, kShiftY = 0;
constexpr uint8_t kSynthX = 5, kSynthY = 0;
constexpr uint8_t kKitX = 5, kKitY = 1;
constexpr uint8_t kCrossScreenX = 6, kCrossScreenY = 2;

/// @brief A `Shell` over fake surfaces with the app's scene registry, and the menu's synth context over the ports'
///        fakes.
struct Rig {
	spec::FakeHostRig fakes;
	deluge::ui::shell::SceneRegistry reg = scene_registry();
	uitest::FakeSurfaceSet surfaces{deluge::ui::shell::kDelugeBoard, deluge_profile()};
	Shell shell{surfaces, deluge_profile(), reg, deluge::ui::shell::Services{}, ShellConfig{}};

	Rig() {
		deluge::l10n::chosenLanguage = &deluge::l10n::built_in::english;
		deluge::notify::Notice discard;
		while (deluge::notify::notices().pop(discard)) {}
		(void)deluge::notify::dirty().drain();
		menu_scene().reset_process_state();
	}
	Rig(const Rig&) = delete;
	Rig& operator=(const Rig&) = delete;
	/// @brief Every scene popped, so the next example's menu starts as a boot leaves it.
	~Rig() {
		while (shell.stack().depth() > 0) {
			shell.stack().pop(SceneKind::Null);
		}
		menu_scene().reset_process_state();
	}

	/// @brief Open @p page on the synth clip's context, and render it once.
	MenuContext open(const Node& page) {
		const MenuContext ctx = fakes.synth_context();
		(void)menu::assign_node_ids(nodes::kAllRoots);
		menu_scene().open(page, ctx);
		shell.init(SceneId{SceneKind::FirstApp, menu::node_id(page)});
		shell.tick(0);
		return ctx;
	}
	/// @brief @p detents clicks of the select encoder: two edges a click.
	void turn(int32_t detents, uint32_t at_ms) {
		shell.on_board_event(
		    DelugeInputEvent{DELUGE_EVENT_ENCODER, kSelectEncoder, 0, static_cast<int16_t>(detents * 2)}, at_ms);
		shell.tick(at_ms + 5);
	}
	/// @brief A press and release of matrix button (@p x, @p y), settled by a tick.
	void press(uint8_t x, uint8_t y, uint32_t at_ms) {
		button(x, y, true, at_ms);
		shell.tick(at_ms + 5);
		button(x, y, false, at_ms + 10);
		shell.tick(at_ms + 15);
	}
	/// @brief One edge of matrix button (@p x, @p y).
	void button(uint8_t x, uint8_t y, bool on, uint32_t at_ms) {
		shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_BUTTON, x, y, static_cast<int16_t>(on ? 255 : 0)}, at_ms);
	}
	/// @brief Press and release the select encoder.
	void select(uint32_t at_ms) { press(kSelectEncButtonX, kSelectEncButtonY, at_ms); }
	/// @brief Press BACK.
	void back(uint32_t at_ms) { press(kBackX, kBackY, at_ms); }
};

/// @brief The focused page's title, as the page draws it.
std::string title() {
	menu::NodeText scratch;
	return std::string{menu_scene().page_title(scratch)};
}

/// @brief The English text of @p id.
std::string text(String id) {
	return std::string{deluge::l10n::get(id)};
}

/// @brief The row the cursor stands on at the walk's current depth.
int32_t cursor_row() {
	const auto& st = menu_scene().state();
	return st.list[st.depth].cursor;
}

int32_t depth() {
	return menu_scene().state().depth;
}

/// @brief Where @p node is listed on @p page in @p ctx, or -1.
int32_t row_of(const Node& page, const Node& node, const MenuContext& ctx) {
	std::array<const Node*, menu::kMaxDynamicChildren> listed{};
	const size_t count = menu::bindings::listed_children(page, ctx, listed, menu::bindings::ListFor::Rows);
	for (size_t i = 0; i < count; ++i) {
		if (listed[i] == &node) {
			return static_cast<int32_t>(i);
		}
	}
	return -1;
}

/// @brief The node listed at @p row of @p page in @p ctx, or null.
const Node* listed_at(const Node& page, int32_t row, const MenuContext& ctx) {
	std::array<const Node*, menu::kMaxDynamicChildren> listed{};
	const size_t count = menu::bindings::listed_children(page, ctx, listed, menu::bindings::ListFor::Rows);
	return row >= 0 && static_cast<size_t>(row) < count ? listed[static_cast<size_t>(row)] : nullptr;
}

} // namespace

// clang-format off
describe menu_walk_fakes("the menu's walk over the ports' fakes and a model song", ${
	it("opens the sound root on its title, its cursor on the first row it lists", _ {
		Rig r;
		const MenuContext ctx = r.open(nodes::sound_root);
		expect(ctx.root).to_equal(MenuContext::Root::Sound);
		expect(r.shell.stack().depth()).to_equal(size_t{1});
		expect(menu_scene().focused()).to_equal(&nodes::sound_root);
		expect(title()).to_equal(text(String::STRING_FOR_SOUND));
		expect(depth()).to_equal(0);
		expect(cursor_row()).to_equal(0);
		expect(listed_at(nodes::sound_root, 0, ctx)).to_equal(&nodes::sound_actions);
	});

	it("descends into the row the cursor stands on, and BACK ascends to the same row", _ {
		Rig r;
		const MenuContext ctx = r.open(nodes::sound_root);
		const int32_t master = row_of(nodes::sound_root, nodes::sound_master, ctx);
		to_be_at_least(expect(master), 1);
		r.turn(master, 100);
		expect(cursor_row()).to_equal(master);

		r.select(300);
		expect(depth()).to_equal(1);
		expect(menu_scene().focused()).to_equal(&nodes::sound_master);
		expect(title()).to_equal(text(String::STRING_FOR_MASTER));

		r.back(600);
		expect(depth()).to_equal(0);
		expect(menu_scene().focused()).to_equal(&nodes::sound_root);
		expect(title()).to_equal(text(String::STRING_FOR_SOUND));
		expect(cursor_row()).to_equal(master);
		expect(r.shell.stack().depth()).to_equal(size_t{1});
	});

	it("pages the filters group LPF, HPF, FILTER ROUTE, and SHIFT+CROSS_SCREEN follows the chain to EQ", _ {
		Rig r;
		(void)r.open(nodes::filters_group);
		expect(title()).to_equal(text(String::STRING_FOR_LPF));
		expect(static_cast<int>(menu_scene().horizontal_paging().total_pages)).to_equal(3);
		r.press(kCrossScreenX, kCrossScreenY, 300);
		expect(title()).to_equal(text(String::STRING_FOR_HPF));
		expect(static_cast<int>(menu_scene().horizontal_paging().page)).to_equal(1);
		r.press(kCrossScreenX, kCrossScreenY, 600);
		expect(title()).to_equal(text(String::STRING_FOR_FILTER_ROUTE));
		r.press(kCrossScreenX, kCrossScreenY, 900);
		expect(title()).to_equal(text(String::STRING_FOR_LPF));

		r.button(kShiftX, kShiftY, true, 1200);
		r.press(kCrossScreenX, kCrossScreenY, 1250);
		r.button(kShiftX, kShiftY, false, 1300);
		r.shell.tick(1305);
		expect(menu_scene().focused()).to_equal(&nodes::eq);
		expect(depth()).to_equal(0);
	});

	it("focuses a horizontal page's columns by their instrument buttons", _ {
		Rig r;
		(void)r.open(nodes::lpf);
		expect(title()).to_equal(text(String::STRING_FOR_LPF));
		expect(static_cast<int>(menu_scene().state().columns[0].focused)).to_equal(0);
		r.press(kKitX, kKitY, 300);
		expect(static_cast<int>(menu_scene().state().columns[0].focused)).to_equal(1);
		r.press(kSynthX, kSynthY, 600);
		expect(static_cast<int>(menu_scene().state().columns[0].focused)).to_equal(0);
		expect(menu_scene().focused()).to_equal(&nodes::lpf);
	});

	it("climbs one level a BACK from three levels down, and the BACK at the root closes the menu", _ {
		Rig r;
		const MenuContext ctx = r.open(nodes::sound_root);
		r.turn(row_of(nodes::sound_root, nodes::sound_master, ctx), 100);
		r.select(300);
		expect(menu_scene().focused()).to_equal(&nodes::sound_master);
		menu_scene().state_for_spec().columns[1].focused = 4; // SYNTH MODE, VOLUME, PAN, TRANSPOSE, VIBRATO
		r.shell.tick(400);
		r.select(600);
		expect(menu_scene().focused()).to_equal(&nodes::vibrato);
		expect(depth()).to_equal(2);
		r.select(900);
		expect(menu_scene().focused()).to_equal(&nodes::source_selection_range);
		expect(depth()).to_equal(3);

		r.back(1200);
		expect(depth()).to_equal(2);
		expect(menu_scene().focused()).to_equal(&nodes::vibrato);
		r.back(1500);
		expect(depth()).to_equal(1);
		expect(title()).to_equal(text(String::STRING_FOR_MASTER));
		r.back(1800);
		expect(depth()).to_equal(0);
		expect(title()).to_equal(text(String::STRING_FOR_SOUND));
		expect(r.shell.stack().depth()).to_equal(size_t{1});
		r.back(2100);
		expect(r.shell.stack().depth()).to_equal(size_t{0});
	});
});
// clang-format on

DELUGE_SPEC(menu_walk_fakes_spec, menu_walk_fakes)
