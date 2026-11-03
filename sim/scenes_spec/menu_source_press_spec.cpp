/// @file
/// @brief SHIFT and a patching-source pad under the ported menu: the press opens the page that patches that source to
///        what the menu shows, as `SoundEditor::potentialShortcutPadAction`'s source arm does in `main`.

#include "gui/ui/menu_view_of.h"
#include "scenes/menu/context.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/menu/node.h"
#include "scenes/menu/node_id.h"
#include "scenes/menu/nodes/nodes.h"
#include "scenes/menu/nodes/patch_cables.h"
#include "scenes/menu/pads.h"
#include "scenes/menu/shortcuts.h"
#include "scenes/profile.h"
#include "scenes/registry.h"

#include "notify/notice_queue.h"
#include "shell/profiles/deluge.h"
#include "shell/shell.h"

#include "gui/l10n/l10n.h"
#include "modulation/params/param.h"

#include "support/menu_rig.h"
#include "support/song_fixture.h"

#include "spec/shell/support/fake_surfaces.h"

#include "cppspec.hpp"

#include "spec/support/spec_main.h"

using namespace deluge::scenes;
using deluge::scenes::menu::MenuContext;
using deluge::scenes::menu::MenuScene;
using deluge::scenes::menu::Node;
using deluge::ui::shell::BoardProfile;
using deluge::ui::shell::SceneId;
using deluge::ui::shell::SceneKind;
using deluge::ui::shell::Shell;
using deluge::ui::shell::ShellConfig;

namespace {

namespace nodes = deluge::scenes::menu::nodes;
namespace params = deluge::modulation::params;

constexpr uint8_t kShiftX = 8, kShiftY = 0;

void select_language() {
	deluge::l10n::chosenLanguage = &deluge::l10n::built_in::english;
	deluge::notify::Notice discard;
	while (deluge::notify::notices().pop(discard)) {}
}

struct Rig {
	spec::SongFixture song = spec::make_synth_song();
	const BoardProfile& profile = deluge_profile();
	deluge::ui::shell::SceneRegistry reg = scene_registry();
	uitest::FakeSurfaceSet fake{deluge::ui::shell::kDelugeBoard, deluge_profile()};
	Shell shell{fake, profile, reg, deluge::ui::shell::Services{}, ShellConfig{}};
	uint32_t now = 0;

	Rig() = default;
	Rig(const Rig&) = delete;
	Rig& operator=(const Rig&) = delete;
	~Rig() { spec::reset_menu_singleton(shell); }

	/// @brief Open the sound menu on @p at, as a shortcut landing would.
	void open(const Node& at) {
		const MenuContext ctx = menu::context_for(*song.song, deluge::gui::menuViewOf(song.clip), 0, false, false,
		                                          false, /*kit_global_fx=*/false);
		(void)menu::assign_node_ids(MenuScene::roots());
		menu_scene().open(at, ctx);
		shell.init(SceneId{SceneKind::FirstApp, menu::node_id(at)});
		tick();
	}
	void tick() { shell.tick(now += 10); }
	void shift(bool down) {
		shell.on_board_event(
		    DelugeInputEvent{DELUGE_EVENT_BUTTON, kShiftX, kShiftY, static_cast<int16_t>(down ? 255 : 0)}, now += 10);
		tick();
	}
	void pad(uint8_t x, uint8_t y, bool down) {
		shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_PAD, x, y, static_cast<int16_t>(down ? 64 : 0)}, now += 10);
		tick();
		tick();
	}
	/// @brief SHIFT held across one press and release of (@p x, @p y).
	void shift_press(uint8_t x, uint8_t y) {
		shift(true);
		pad(x, y, true);
		pad(x, y, false);
		shift(false);
	}
	const menu::MenuState& st() const { return menu_scene().state(); }
};

} // namespace

// clang-format off
describe menu_source_press("scenes::MenuScene -- SHIFT and a patching-source pad", $ {
	it("opens the cable strength page for a source pressed on a patched param", _ {
		select_language();
		Rig r;
		r.open(nodes::master_volume);
		r.shift_press(15, 1); // VELOCITY
		expect(menu_scene().focused() == &nodes::patch_cable_strength_regular).to_be_true();
		expect(int(r.st().depth)).to_equal(1);
		expect(int(r.st().walk.patching_param)).to_equal(int(params::GLOBAL_VOLUME_POST_FX));
		expect(int(r.st().walk.patch_source)).to_equal(int(PatchSource::VELOCITY));
	});

	it("a second press of the active source on a patch-cable page takes its second layer", _ {
		select_language();
		Rig r;
		r.open(nodes::lpf_freq); // ENV1 cannot patch to the volume triple
		r.shift_press(14, 6); // ENVELOPE_0
		expect(int(r.st().walk.patch_source)).to_equal(int(PatchSource::ENVELOPE_0));
		r.shift_press(14, 6); // the active source's pad again: ENVELOPE_2
		expect(menu_scene().focused() == &nodes::patch_cable_strength_regular).to_be_true();
		expect(int(r.st().walk.patch_source)).to_equal(int(PatchSource::ENVELOPE_2));
	});

	it("opens the range page when another source pad is held", _ {
		select_language();
		Rig r;
		r.open(nodes::master_volume);
		r.shift(true);
		r.pad(15, 1, true); // VELOCITY, held
		r.pad(15, 2, true); // RANDOM, on that cable's depth
		expect(menu_scene().focused() == &nodes::patch_cable_strength_range).to_be_true();
		expect(int(r.st().walk.patch_source)).to_equal(int(PatchSource::VELOCITY));
		expect(int(r.st().walk.patch_range_source)).to_equal(int(PatchSource::RANDOM));
	});

	it("closes the menu when nothing below depth 0 takes the press", _ {
		// A strength page at the root answers "ask the item above"; there is none, so the menu closes (`main`'s
		// `exitCompletely()`). Driven at the unit: the page opens only with its walk already chosen.
		select_language();
		Rig r;
		const MenuContext ctx = menu::context_for(*r.song.song, deluge::gui::menuViewOf(r.song.clip), 0, false, false,
		                                          false, /*kit_global_fx=*/false);
		menu::MenuContext core_ctx = ctx;
		menu::MenuState st{};
		st.path[0] = &nodes::patch_cable_strength_regular;
		st.walk.patching_param = params::LOCAL_LPF_FREQ;
		st.walk.patch_source = PatchSource::VELOCITY;
		menu::MenuPads pads;
		menu::MenuWalk walk;
		menu::ValueSession session;
		const deluge::ui::shell::ButtonSet held{};
		const deluge::ui::PadMask held_pads{};
		const deluge::ui::shell::UiContext ui{held, held_pads, true, false, 0, deluge_profile()};
		const std::optional<deluge::ui::shell::Result> got =
		    pads.source_press(menu::MenuCore{core_ctx, st}, walk, session, 15, 2, ui);
		expect(got.has_value() && got->handled).to_be_true();
		const bool removes = got.has_value() && !got->actions.empty()
		                     && got->actions[0].kind == deluge::ui::shell::Action::Kind::Remove;
		expect(removes).to_be_true();
	});

	it("falls through to the randomizer on (15,1) when no item takes it", _ {
		select_language();
		Rig r;
		r.open(nodes::synth_mode);
		r.shift_press(15, 1);
		const Node* randomizer = menu::shortcut_node(MenuContext::Root::Sound, false, 15, 1);
		expect(randomizer != nullptr).to_be_true();
		expect(menu_scene().focused_value() == randomizer).to_be_true();
	});

	it("swallows a source cell past the last patch source", _ {
		select_language();
		const menu::ShortcutsVersion saved = menu::shortcuts_version();
		menu::set_shortcuts_version(menu::ShortcutsVersion::V1); // (15, 7) names no source
		Rig r;
		r.open(nodes::master_volume);
		r.shift_press(15, 7);
		menu::set_shortcuts_version(saved);
		deluge::notify::Notice notice;
		expect(deluge::notify::notices().pop(notice)).to_be_false(); // no PARAMETER NOT APPLICABLE for a non-source
		expect(r.shell.stack().depth()).to_equal(size_t(1));
		expect(menu_scene().focused() == &nodes::master_volume).to_be_true();
	});

	it("keeps the second-layer flag when a source pad is pressed", _ {
		select_language();
		Rig r;
		r.open(nodes::synth_mode); // takes no source: the press reaches the shortcut path
		menu_scene().state_for_spec().second_layer = true;
		r.shift_press(15, 0); // AFTERTOUCH, not a shortcut
		expect(r.st().second_layer).to_be_true();
	});

	it("never toggles the second layer from a fixed cable's own pad", _ {
		select_language();
		Rig r;
		r.open(nodes::vibrato);
		const menu::ShortcutPos own = menu::shortcut_pos(MenuContext::Root::Sound, nodes::vibrato);
		expect(own.valid).to_be_true();
		r.shift_press(own.x, own.y);
		expect(r.st().second_layer).to_be_false();
	});
});
// clang-format on

DELUGE_SPEC(menu_source_press_spec, menu_source_press)
