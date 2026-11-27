// A stored choice that no longer applies, driven through the real menu scene: the list shows it
// marked, and turning off it moves among the choices that apply without the cursor slipping when the marked entry
// leaves the list.

#include "gui/ui/menu_view_of.h"
#include "ui/scenes/menu/bindings.h"
#include "ui/scenes/menu/context.h"
#include "ui/scenes/menu/menu_scene.h"
#include "ui/scenes/menu/node.h"
#include "ui/scenes/menu/node_id.h"
#include "ui/scenes/menu/nodes/nodes.h"
#include "ui/scenes/menu/nodes/sound_voice.h"
#include "ui/scenes/profile.h"
#include "ui/scenes/registry.h"

#include "support/mocks/fake_surfaces.h"

#include "instruments/classic/sound_state.h"
#include "model/edits/setting_edit.h"
#include "model/instrument/kit.h"
#include "processing/sound/sound_drum.h"
#include "ui/shell/profiles/deluge.h"
#include "ui/shell/shell.h"

#include "support/song_fixture.h"

#include "cppspec.hpp"
#include "support/record_state.h"

#include <string>

#include "support/menu_context.h"
#include "support/spec_main.h"

using namespace deluge::ui::scenes;
using deluge::instruments::classic::PolyphonyMode;
using deluge::ui::scenes::menu::MenuContext;
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
		menu_scene().reset_process_state(); // each example starts with the rows a boot has
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
	it("turns off the marked entry onto the choice before it, and the next turn onto the one before that", _ {
		// Choke shares a kit's voices among its lanes, so only a kit lane offers it; a synth track's stored choke is
		// shown marked, past the four choices that apply.
		spec::SongFixture f = spec::make_synth_song();
		Sound* sound = f.instrument->toModControllable()->effectsOwner()->sound;
		deluge::edits::settingFor(*sound, "polyphonic", deluge::serialization::Value::makeScalar("choke"));
		const MenuContext ctx = spec::ctx_of(*f.song, deluge::gui::menuViewOf(f.clip), 0, false, false, false, false);
		LeafRig r;
		r.open(menu::nodes::polyphony, ctx);
		const auto stored_mode = [&] { return static_cast<int>(deluge::instruments::classic::voicing(sound->state()).polyphony); };
		const int first = stored_mode();
		r.turn(-1, 20);
		const int second = stored_mode();
		r.turn(-1, 40);
		const int third = stored_mode();
		expect("stored " + std::to_string(first)).to_equal("stored " + std::to_string(static_cast<int>(PolyphonyMode::CHOKE)));
		expect("one turn " + std::to_string(second))
		    .to_equal("one turn " + std::to_string(static_cast<int>(PolyphonyMode::LEGATO)));
		expect("two turns " + std::to_string(third))
		    .to_equal("two turns " + std::to_string(static_cast<int>(PolyphonyMode::MONO)));
	});
});
// clang-format on

DELUGE_SPEC(stale_choice_scene_spec, stale_choice_scene)
