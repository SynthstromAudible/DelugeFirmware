/// @file
/// @brief The settings pages' DEVICE rows, driven through a real `MenuScene` and `Shell` over the ports' fakes, with no
///        song behind the menu: what a page shows is the fake's value, and what a turn writes reaches the fake's
///        store.

#include "scenes/bind/ports/device_settings.h"
#include "scenes/menu/bindings.h"
#include "scenes/menu/context.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/menu/node_id.h"
#include "scenes/menu/nodes/nodes.h"
#include "scenes/menu/nodes/settings.h"
#include "scenes/profile.h"
#include "scenes/registry.h"

#include "deluge-project/device_settings.h"
#include "gui/l10n/l10n.h"
#include "notify/dirty_set.h"
#include "notify/notice_queue.h"
#include "shell/profiles/deluge.h"
#include "shell/shell.h"

#include "support/fake_host_rig.h"
#include "support/mocks/fake_surfaces.h"

#include "cppspec.hpp"
#include "support/matchers/value_matchers.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "support/spec_main.h"

using namespace deluge::scenes;
using deluge::scenes::menu::MenuContext;
using deluge::scenes::menu::Node;
using deluge::ui::shell::SceneId;
using deluge::ui::shell::SceneKind;
using deluge::ui::shell::Shell;
using deluge::ui::shell::ShellConfig;
namespace device_path = deluge::project::device_path;

namespace {

namespace nodes = deluge::scenes::menu::nodes;

constexpr uint8_t kSelectEncoder = 3;
constexpr uint8_t kSelectEncButtonX = 4, kSelectEncButtonY = 3;

/// @brief A `Shell` over fake surfaces with the app's scene registry, and the menu's context over the ports' fakes.
struct Rig {
	spec::FakeHostRig fakes;
	deluge::ui::shell::SceneRegistry reg = scene_registry();
	uitest::FakeSurfaceSet surfaces{deluge::ui::shell::kDelugeBoard, deluge_profile()};
	Shell shell{surfaces, deluge_profile(), reg, deluge::ui::shell::Services{}, ShellConfig{}};

	Rig() {
		deluge::l10n::chosenLanguage = &deluge::l10n::built_in::english;
		deluge::notify::Notice discard;
		while (deluge::notify::notices().pop(discard)) {}
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

	/// @brief Open @p page on the settings root's context, with no model behind it.
	MenuContext open(const Node& page) {
		const MenuContext ctx = fakes.settings_context(/*with_model=*/false);
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
	/// @brief Press and release the select encoder.
	void select(uint32_t at_ms) {
		shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_BUTTON, kSelectEncButtonX, kSelectEncButtonY, 255}, at_ms);
		shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_BUTTON, kSelectEncButtonX, kSelectEncButtonY, 0}, at_ms);
		shell.tick(at_ms + 5);
	}
};

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

/// @brief The names @p node lists in @p ctx, in menu order.
std::vector<std::string> names_of(const Node& node, const MenuContext& ctx) {
	std::array<std::string_view, menu::kMaxDynamicNames> out{};
	std::array<deluge::ui::shell::TextScratch, menu::kMaxDynamicNames> scratch{};
	const size_t count = menu::bindings::names(node, ctx, out, scratch, menu::bindings::NameForm::Full);
	return {out.begin(), out.begin() + static_cast<std::ptrdiff_t>(count)};
}

} // namespace

// clang-format off
describe menu_settings_pages_fakes("the settings pages' DEVICE rows, over the ports' fakes and no song", ${
	it("lists the patch cable polarity on the defaults page and shows the fake's value", _ {
		Rig r;
		const MenuContext ctx = r.open(nodes::defaults_settings);
		const int32_t row = row_of(nodes::defaults_settings, nodes::default_patch_cable_polarity, ctx);
		to_be_at_least(expect(row), 0);
		r.turn(row, 10);
		expect(static_cast<int32_t>(menu_scene().state().list[0].cursor)).to_equal(row);

		// The fake's default is bipolar, the row's second choice; the names are the cable's own words.
		expect(menu::bindings::read(nodes::default_patch_cable_polarity, ctx)).to_equal(1);
		expect(names_of(nodes::default_patch_cable_polarity, ctx))
			.to_equal(std::vector<std::string>{"unipolar", "bipolar"});
		r.fakes.host.device.values.insert_or_assign(std::string{device_path::kPatchCablePolarity},
		                                            deluge::serialization::Value::makeScalar("unipolar"));
		expect(menu::bindings::read(nodes::default_patch_cable_polarity, ctx)).to_equal(0);
		expect(r.fakes.host.device.stores.empty()).to_be_true();
	});

	it("stores a turn of the polarity through the device port, one write", _ {
		Rig r;
		const MenuContext ctx = r.open(nodes::defaults_settings);
		r.turn(row_of(nodes::defaults_settings, nodes::default_patch_cable_polarity, ctx), 10);
		r.select(30);
		expect(menu_scene().focused()).to_equal(&nodes::default_patch_cable_polarity);
		(void)deluge::notify::dirty().drain();
		r.turn(-1, 50);
		expect(r.fakes.host.device.stores.size()).to_equal(size_t{1});
		if (!r.fakes.host.device.stores.empty()) {
			expect(r.fakes.host.device.stores.front().first).to_equal(std::string{device_path::kPatchCablePolarity});
			expect(r.fakes.host.device.stores.front().second.str()).to_equal(std::string{"unipolar"});
		}
		expect(menu::bindings::read(nodes::default_patch_cable_polarity, ctx)).to_equal(0);
		// The song's write sink saw nothing: a device setting is no song edit.
		expect(r.fakes.host.writes.requests.empty()).to_be_true();
	});

	it("changes nothing and raises nothing for a write the row refuses", _ {
		Rig r;
		const MenuContext ctx = r.open(nodes::defaults_settings);
		// The row forced to a word it cannot name: it reads as the default, and a value past its list is refused.
		r.fakes.host.device.values.insert_or_assign(std::string{device_path::kPatchCablePolarity},
		                                            deluge::serialization::Value::makeScalar("tripolar"));
		expect(menu::bindings::read(nodes::default_patch_cable_polarity, ctx)).to_equal(1);
		(void)deluge::notify::dirty().drain();
		menu::bindings::write(nodes::default_patch_cable_polarity, ctx, 2);
		menu::bindings::write(nodes::default_patch_cable_polarity, ctx, -1);
		expect(r.fakes.host.device.stores.empty()).to_be_true();
		expect(r.fakes.host.device.values.at(std::string{device_path::kPatchCablePolarity}).str())
			.to_equal(std::string{"tripolar"});
		expect(deluge::notify::dirty().drain().has(deluge::notify::Dirty::Oled)).to_be_false();
	});

	it("says a setting the device applies after a restart, as the USB audio item does", _ {
		Rig r;
		const MenuContext ctx = r.open(nodes::defaults_settings);
		r.fakes.host.device.accept_restart.insert(std::string{device_path::kPlayCursor});
		menu::bindings::write(nodes::flash_status, ctx, 0);
		expect(r.fakes.host.device.stores.size()).to_equal(size_t{1});
		deluge::notify::Notice notice{};
		expect(deluge::notify::notices().pop(notice)).to_be_true();
		expect(static_cast<int>(notice.string_id))
			.to_equal(static_cast<int>(deluge::l10n::String::STRING_FOR_RESTART_TO_APPLY));
	});

	it("shows the play cursor's three forms in their value order, read from the fake", _ {
		Rig r;
		const MenuContext ctx = r.open(nodes::root_settings);
		expect(row_of(nodes::root_settings, nodes::flash_status, ctx)).not_().to_equal(-1);
		expect(menu::bindings::read(nodes::flash_status, ctx))
			.to_equal(static_cast<int32_t>(deluge::project::FlashCursor::Slow));
		expect(names_of(nodes::flash_status, ctx)).to_equal(std::vector<std::string>{"Fast", "Disabled", "Slow"});
		expect(menu::bindings::range(nodes::flash_status, ctx).max).to_equal(2);
	});
});
// clang-format on

DELUGE_SPEC(menu_settings_pages_fakes_spec, menu_settings_pages_fakes)
