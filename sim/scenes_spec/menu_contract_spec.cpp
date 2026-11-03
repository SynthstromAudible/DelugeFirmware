/// @file
/// @brief The menu's contracts that its parts must keep however they are arranged: the order `after_render` drains
///        its mailboxes in, that `owned_input` predicts exactly what the render claims, and that a new visit starts
///        from defaults.

#include "gui/ui/menu_view_of.h"
#include "scenes/menu/action.h"
#include "scenes/menu/context.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/menu/node.h"
#include "scenes/menu/node_id.h"
#include "scenes/menu/nodes/nodes.h"
#include "scenes/profile.h"
#include "scenes/registry.h"
#include "scenes/slicing/slicer_scene.h"

#include "notify/notice_queue.h"
#include "shell/profiles/deluge.h"
#include "shell/shell.h"

#include "gui/l10n/l10n.h"

#include "support/menu_rig.h"
#include "support/song_fixture.h"

#include "spec/shell/support/fake_surfaces.h"
#include "spec/ui/support/frames.h"

#include "cppspec.hpp"

#include <span>
#include <string>
#include <vector>

#include "spec/support/spec_main.h"

using namespace deluge::scenes;
using deluge::scenes::menu::MenuContext;
using deluge::scenes::menu::MenuScene;
using deluge::scenes::menu::MenuState;
using deluge::scenes::menu::Node;
using deluge::ui::shell::BoardProfile;
using deluge::ui::shell::SceneId;
using deluge::ui::shell::SceneKind;
using deluge::ui::shell::Shell;
using deluge::ui::shell::ShellConfig;
using deluge::ui::shell::SurfaceRole;

namespace {

namespace nodes = deluge::scenes::menu::nodes;
using Band = MenuScene::DrainBand;

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

	Rig() = default;
	Rig(const Rig&) = delete;
	Rig& operator=(const Rig&) = delete;
	~Rig() { spec::reset_menu_singleton(shell); }

	MenuContext menu_context() const {
		return menu::context_for(*song.song, deluge::gui::menuViewOf(song.clip), 0, false, false, false,
		                         /*kit_global_fx=*/false);
	}
	/// @brief Open @p root, landing on @p at (the root itself when it is the root).
	void open(const Node& root, const Node& at) {
		(void)menu::assign_node_ids(MenuScene::roots());
		menu_scene().open(root, menu_context());
		shell.init(SceneId{SceneKind::FirstApp, menu::node_id(at)});
	}
};

} // namespace

// clang-format off
describe menu_contract_drain("scenes::MenuScene::after_render -- the drain order", $ {
	it("drains context changes, then the commit, navigation, the action, the repaint and the pushed scene", _ {
		select_language();
		Rig r;
		r.open(nodes::sound_root, nodes::sound_root);
		r.shell.tick(0);

		MenuState& st = menu_scene().state_for_spec();
		st.walk.range_choice = 0;
		st.walk.source_choice = 0;
		st.tick.value_changed = true;
		st.tick.value_node = &nodes::lpf_freq;
		st.tick.render_resp.entered = true;
		st.walk.pending_action = menu::MenuActionRequest{.action = menu::MenuAction::PadBrightnessChanged};
		st.walk.pending_scene = SceneId{slicing::kSlicerSceneKind, 0};
		(void)menu_scene().after_render();

		const std::vector<Band> want{Band::RangeChoice, Band::SourceChoice, Band::ValueCommit, Band::Navigation,
		                             Band::PendingAction, Band::Repaint,     Band::PendingScene};
		const std::span<const Band> got = menu_scene().drain_trace();
		expect(std::vector<Band>(got.begin(), got.end()) == want).to_be_true();
	});
});

describe menu_contract_claims("scenes::MenuScene::owned_input -- the render's claims, predicted", $ {
	it("predicts exactly what the render claims, on every root and every page directly under one", _ {
		select_language();
		std::vector<std::string> mismatches;
		for (const Node* root : MenuScene::roots()) {
			std::vector<const Node*> pages{root};
			for (const Node* child : root->children) {
				pages.push_back(child);
			}
			for (const Node* page : pages) {
				Rig r;
				r.open(*root, *page);
				deluge::ui::test::Rig48 rig;
				rig.begin_tick(0);
				const deluge::ui::InputClaims owned = menu_scene().owned_input();
				menu_scene().render_oled(SurfaceRole::Main, rig.frame);
				if (!spec::owns_matches_claims(owned, rig.ctx.claims)) {
					mismatches.push_back(std::to_string(menu::node_id(*root)) + "/" +
					                     std::to_string(menu::node_id(*page)));
				}
			}
		}
		std::string joined;
		for (const std::string& m : mismatches) {
			joined += m + " ";
		}
		expect(joined).to_equal(std::string{});
	});
});

describe menu_contract_visit("scenes::MenuScene -- a visit's state", $ {
	it("starts a reopened visit clean: a close asked for by the last visit does not close this one", _ {
		select_language();
		Rig r;
		r.open(nodes::sound_root, nodes::sound_root);
		r.shell.tick(0);
		// A scene opener's scene returning at the root goes up below depth 0, which asks the menu to close on its next
		// after_render.
		menu_scene().state_for_spec().walk.go_up_on_resume = true;
		menu_scene().on_resume();
		while (r.shell.stack().depth() > 0) {
			r.shell.stack().pop(SceneKind::Null); // closed some other way before it did
		}

		r.open(nodes::sound_root, nodes::sound_root);
		r.shell.tick(10);
		r.shell.tick(20);
		expect(r.shell.stack().depth()).to_equal(size_t(1));
	});
});
// clang-format on

DELUGE_SPEC(menu_contract_spec, menu_contract_drain, menu_contract_claims, menu_contract_visit)
