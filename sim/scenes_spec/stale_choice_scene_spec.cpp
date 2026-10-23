// A stored choice that no longer applies (step 2's decision 2), driven through the real menu scene: the list shows it
// marked, and turning off it moves among the choices that apply without the cursor slipping when the marked entry
// leaves the list.

#include "gui/ui/menu_view_of.h"
#include "scenes/menu/bindings.h"
#include "scenes/menu/context.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/menu/node.h"
#include "scenes/menu/node_id.h"
#include "scenes/menu/nodes/nodes.h"
#include "scenes/menu/nodes/sound_osc.h"
#include "scenes/profile.h"
#include "scenes/registry.h"

#include "spec/shell/support/fake_surfaces.h"

#include "engines/deluge/sound_state.h"
#include "model/edits/setting_edit.h"
#include "model/instrument/kit.h"
#include "processing/sound/sound_drum.h"
#include "shell/profiles/deluge.h"
#include "shell/shell.h"

#include "support/song_fixture.h"

#include "cppspec.hpp"

#include <string>

#include "spec/support/spec_main.h"

using namespace deluge::scenes;
using deluge::instrument::OscType;
using deluge::scenes::menu::MenuContext;
using deluge::ui::shell::SceneKind;
using deluge::ui::shell::Shell;
using deluge::ui::shell::ShellConfig;

namespace {

/// The select encoder's index on the Deluge board.
constexpr uint8_t kSelectEncoder = 3;

/// A bare shell over fake surfaces with the menu scene opened on one leaf.
struct LeafRig {
	const deluge::ui::shell::BoardProfile& profile = deluge_profile();
	deluge::ui::shell::SceneRegistry reg = scene_registry();
	uitest::FakeSurfaceSet fake{deluge::ui::shell::kDelugeBoard, deluge_profile()};
	Shell shell{fake, profile, reg, deluge::ui::shell::Services{}, ShellConfig{}};

	LeafRig() = default;
	LeafRig(const LeafRig&) = delete;
	LeafRig& operator=(const LeafRig&) = delete;
	~LeafRig() {
		while (shell.stack().depth() > 0) {
			shell.stack().pop(SceneKind::Null);
		}
	}

	void open(const menu::Node& leaf, const MenuContext& ctx) {
		(void)menu::assign_node_ids(menu::nodes::kAllRoots);
		menu_scene().open(leaf, ctx);
		shell.init(deluge::ui::shell::SceneId{SceneKind::FirstApp, menu::node_id(leaf)});
		shell.tick(5);
	}
	void turn(int32_t detents, uint32_t at_ms) {
		shell.on_board_event(
		    DelugeInputEvent{DELUGE_EVENT_ENCODER, kSelectEncoder, 0, static_cast<int16_t>(detents * 2)}, at_ms);
		shell.tick(at_ms + 5);
	}
};

} // namespace

// clang-format off
describe stale_choice_scene("the menu scene over a stored choice that no longer applies", ${
	it("turns off the marked entry onto the next choice, and the next turn onto the one after it", _ {
		spec::SongFixture f = spec::make_kit_song();
		SoundDrum* drum = spec::add_drum<SoundDrum>(f);
		deluge::edits::settingFor(*drum, "osc1/type", deluge::serialization::Value::makeScalar("dx7"));
		const MenuContext ctx = menu::context_for(*f.song, deluge::gui::menuViewOf(f.clip), 0, false, false, false, false);
		LeafRig r;
		r.open(menu::nodes::osc0_type, ctx);
		const OscType first = drum->boundState()->sources[0].osc_type;
		r.turn(1, 20);
		const OscType second = drum->boundState()->sources[0].osc_type;
		r.turn(1, 40);
		const OscType third = drum->boundState()->sources[0].osc_type;
		expect("stored " + std::to_string(static_cast<int>(first))).to_equal("stored " + std::to_string(static_cast<int>(OscType::DX7)));
		expect("one turn " + std::to_string(static_cast<int>(second)))
		    .to_equal("one turn " + std::to_string(static_cast<int>(OscType::INPUT_L)));
		expect("two turns " + std::to_string(static_cast<int>(third)))
		    .to_equal("two turns " + std::to_string(static_cast<int>(OscType::INPUT_R)));
	});
});

DELUGE_SPEC(stale_choice_scene_spec, stale_choice_scene)
