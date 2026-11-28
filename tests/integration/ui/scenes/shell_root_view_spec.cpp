/// @file
/// @brief `ShellRootView`, a legacy root view whose body is a scene, over the firmware's one shell: the scene is the
///        root of the shared stack, a menu the guest opens over it goes on top and closes back to it, and the input
///        the menu leaves reaches the root scene.
///
/// The instance here is the spec's own, over `AudioClipScene`; the firmware does not push one yet.

#include "support/audio_clip_rig.h"

#include "gui/ui/menu_view_of.h"
#include "gui/ui/shell_host_ui.h"
#include "gui/ui/shell_root_view.h"
#include "gui/ui/shell_runtime.h"
#include "gui/ui/ui.h"
#include "gui/ui_timer_manager.h"
#include "ui/scenes/menu/menu_scene.h"
#include "ui/scenes/menu/node_id.h"
#include "ui/scenes/menu/nodes/nodes.h"

#include "support/menu_context.h"
#include "support/spec_main.h"

namespace menu = deluge::ui::scenes::menu;
using deluge::gui::ui::ShellRootView;
using deluge::gui::ui::shellRuntime;

namespace {

/// @brief How often the focus hook ran: the `View` half of the root view's focus.
int g_focus_hooks = 0;

ShellRootView<deluge::ui::scenes::audio_clip::AudioClipScene> testAudioClipRoot{
    &audio_clip_scene, deluge::ui::scenes::audio_clip::kAudioClipSceneKind, UIType::AUDIO_CLIP,
    [] { g_focus_hooks++; }};

/// @brief The runtime's stack, emptied and its root host forgotten, whichever way an example ends; the legacy UI
///        stack put back to nothing open; the guest's timer unset.
struct Guard {
	Guard(const Guard&) = delete;
	Guard& operator=(const Guard&) = delete;
	Guard() = default;
	~Guard() {
		nullifyUIs();
		shellRuntime().shell().stack().reset();
		shellRuntime().setRootHost(nullptr);
		uiTimerManager.unsetTimer(TimerName::UI_SPECIFIC);
	}
};

/// @brief Seat the spec's root view as legacy's root and open it.
bool open_root(Rig& r) {
	r.seed(Sentinel{});
	setRootUILowLevel(&testAudioClipRoot);
	return testAudioClipRoot.opened();
}

/// @brief Open the song's menu on the guest over the root view, as legacy's `openUI` does.
bool open_menu_over(Rig& r) {
	static const menu::Node* const roots[] = {&menu::nodes::root_song};
	(void)menu::assign_node_ids(roots);
	deluge::ui::scenes::menu_scene().open(
	    menu::nodes::root_song,
	    deluge::ui::scenes::spec::ctx_of(*r.f.song, deluge::gui::menuViewOf(r.f.clip), 0, false, false, false, false));
	shellHostUI.setInitialScene(SceneId{SceneKind::FirstApp, menu::node_id(menu::nodes::root_song)});
	return openUI(&shellHostUI);
}

deluge::ui::shell::SceneStack& stack() {
	return shellRuntime().shell().stack();
}

} // namespace

// clang-format off
describe shell_root_view_spec_body("gui::ui::ShellRootView over the shared shell", $ {
	it("opens its scene as the root of the shared shell, on the current clip, and runs the View half of its focus", _ {
		Guard guard;
		Rig r;
		const int hooks = g_focus_hooks;
		expect(open_root(r)).to_be_true();
		expect(stack().depth()).to_equal(size_t{1});
		expect(stack().top().kind()).to_equal(deluge::ui::scenes::audio_clip::kAudioClipSceneKind);
		expect(shellRuntime().rootHost() == &testAudioClipRoot).to_equal(true);
		expect(g_focus_hooks).to_equal(hooks + 1);
	});

	it("keeps its scene under a menu the guest opens over it, and the guest closes back to it", _ {
		Guard guard;
		Rig r;
		expect(open_root(r)).to_be_true();
		expect(open_menu_over(r)).to_be_true();
		expect(getCurrentUI() == &shellHostUI).to_equal(true);
		expect(stack().depth()).to_equal(size_t{2});
		expect(stack().root()->kind()).to_equal(deluge::ui::scenes::audio_clip::kAudioClipSceneKind);
		expect(stack().top().kind()).to_equal(menu::kMenuSceneKind);
		shellHostUI.closeNow();
		expect(stack().depth()).to_equal(size_t{1});
		expect(stack().top().kind()).to_equal(deluge::ui::scenes::audio_clip::kAudioClipSceneKind);
		expect(getCurrentUI() == &testAudioClipRoot).to_equal(true);
	});

	it("closes the guest when the menu goes and only the root scene is left", _ {
		Guard guard;
		Rig r;
		expect(open_root(r)).to_be_true();
		expect(open_menu_over(r)).to_be_true();
		// A long BACK takes the menu off; nothing above the root takes input any more.
		(void)shellHostUI.exitUI();
		expect(isUIOpen(&shellHostUI)).to_equal(false);
		expect(stack().depth()).to_equal(size_t{1});
		expect(stack().top().kind()).to_equal(deluge::ui::scenes::audio_clip::kAudioClipSceneKind);
	});

	it("leaves the guest only the menu's pad regions while the menu is open, and the root's when it is not", _ {
		Guard guard;
		Rig r;
		expect(open_root(r)).to_be_true();
		const deluge::ui::shell::PadRegions root_only = shellRuntime().surfaces().owned_regions();
		expect(root_only == audio_clip_scene().pad_regions()).to_equal(true);
		expect(open_menu_over(r)).to_be_true();
		expect(shellRuntime().surfaces().owned_regions() == deluge::ui::scenes::menu_scene().pad_regions()).to_equal(true);
		shellHostUI.closeNow();
		expect(shellRuntime().surfaces().owned_regions() == root_only).to_equal(true);
	});

	it("hands a pad the menu leaves to the root scene alone", _ {
		Guard guard;
		Rig r;
		expect(open_root(r)).to_be_true();
		expect(open_menu_over(r)).to_be_true();
		expect(audio_clip_scene().end_marker_visible()).to_equal(false);
		// The guest forwards a pad its scenes do not claim to the root view, which is not the current UI.
		(void)testAudioClipRoot.padAction(15, 4, 64);
		(void)testAudioClipRoot.padAction(15, 4, 0);
		expect(audio_clip_scene().end_marker_visible()).to_equal(true);
		expect(stack().top().kind()).to_equal(menu::kMenuSceneKind);
	});

	it("loses its root scene, and its root host, when the UIs are taken down for a new song", _ {
		Guard guard;
		Rig r;
		expect(open_root(r)).to_be_true();
		shellHostUI.uisTakenDown();
		expect(stack().depth()).to_equal(size_t{0});
		expect(shellRuntime().rootHost() == nullptr).to_equal(true);
	});
});
// clang-format on

DELUGE_SPEC(shell_root_view_spec, shell_root_view_spec_body)
