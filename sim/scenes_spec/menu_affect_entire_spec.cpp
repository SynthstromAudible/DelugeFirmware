/// @file
/// @brief `MenuScene`'s AFFECT ENTIRE: the hold mode the host enters on a kit row's item that uses it
///        (`SoundEditor::buttonAction`'s AFFECT_ENTIRE arm), its fast LED blink, and the whole-kit writes it allows.

#include "gui/l10n/l10n.h"
#include "gui/ui/menu_view_of.h"
#include "model/instrument/kit.h"
#include "processing/sound/sound_drum.h"
#include "scenes/menu/bindings.h"
#include "scenes/menu/context.h"
#include "scenes/menu/menu_core.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/menu/node_id.h"
#include "scenes/menu/nodes/nodes.h"
#include "scenes/profile.h"
#include "scenes/registry.h"
#include "shell/profiles/deluge.h"
#include "shell/shell.h"
#include "ui/core/led_frame.h"

#include "support/song_fixture.h"

#include "spec/shell/support/fake_surfaces.h"

#include "cppspec.hpp"

#include "spec/support/spec_main.h"

using namespace deluge::scenes;
using deluge::scenes::menu::MenuContext;
using deluge::scenes::menu::Node;
using deluge::ui::shell::ButtonId;
using deluge::ui::shell::SceneId;
using deluge::ui::shell::SceneKind;
namespace nodes = deluge::scenes::menu::nodes;

namespace {

/// @brief A kit of two sound drums with the first selected, and the shell `menu_scene_spec.cpp`'s `Rig` builds.
struct KitMenu {
	spec::SongFixture song = spec::make_kit_song();
	SoundDrum* a = spec::add_drum<SoundDrum>(song);
	SoundDrum* b = spec::add_drum<SoundDrum>(song);
	uitest::FakeSurfaceSet fake{deluge::ui::shell::kDelugeBoard, deluge_profile()};
	deluge::ui::shell::SceneRegistry reg = scene_registry();
	deluge::ui::shell::Shell shell{fake, deluge_profile(), reg, deluge::ui::shell::Services{},
	                               deluge::ui::shell::ShellConfig{}};

	KitMenu(const KitMenu&) = delete;
	KitMenu& operator=(const KitMenu&) = delete;
	KitMenu() { static_cast<Kit*>(song.instrument)->selectedDrum = a; }
	~KitMenu() {
		while (shell.stack().depth() > 0) {
			shell.stack().pop(SceneKind::Null);
		}
		menu_scene().set_affect_entire_hold(false);
		menu_scene().reset_process_state();
	}
	/// @brief Open the drum's menu at @p node (a numbered node of the drum root).
	void open(const Node& node) {
		currentSong = song.song;
		MenuContext ctx = menu::context_for(*song.song, deluge::gui::menuViewOf(song.clip), 0, false, false, false,
		                                    /*kit_global_fx=*/false);
		(void)menu::assign_node_ids(nodes::kAllRoots);
		menu_scene().open(nodes::root_drum, ctx);
		shell.init(SceneId{SceneKind::FirstApp, menu::node_id(node)});
	}
	deluge::ui::LedState ae_led() const {
		deluge::ui::LedFrame f;
		menu_scene().leds(f);
		for (const auto& [id, state] : f.entries()) {
			if (id == static_cast<deluge::ui::LedId>(ButtonId::AffectEntire)) {
				return state;
			}
		}
		return deluge::ui::LedState{deluge::ui::LedState::Kind::Off, 0, 0};
	}
};

bool declared(const deluge::ui::LedState& s) {
	return s.kind == deluge::ui::LedState::Kind::Shared;
}

} // namespace

// clang-format off
describe menu_affect_entire("scenes::menu -- AFFECT ENTIRE in a kit row's menu", $ {
	before_each([] { deluge::l10n::chosenLanguage = &deluge::l10n::built_in::english; });

	it("takes AFFECT ENTIRE as a hold on a kit row's LPF frequency, and not on the root list's submenu row", _ {
		KitMenu k;
		k.open(nodes::lpf_freq);
		k.shell.tick(0);
		expect(menu_scene().takes_affect_entire()).to_be_true();
		KitMenu root;
		root.open(nodes::root_drum);
		root.shell.tick(0);
		expect(menu_scene().takes_affect_entire()).to_be_false();
	});

	it("blinks AFFECT ENTIRE fast while held and lets a write reach every drum, until the release", _ {
		KitMenu k;
		k.open(nodes::lpf_freq);
		k.shell.tick(0);
		expect(declared(k.ae_led())).to_be_false();
		menu_scene().set_affect_entire_hold(true);
		k.shell.tick(10);
		expect(declared(k.ae_led())).to_be_true();
		expect(k.ae_led().rate == deluge::ui::BlinkRate::Fast).to_be_true();
		expect(menu::bindings::writes_whole_kit(menu_scene().menu_context())).to_be_true();
		menu_scene().set_affect_entire_hold(false);
		k.shell.tick(20);
		expect(declared(k.ae_led())).to_be_false();
		expect(menu::bindings::writes_whole_kit(menu_scene().menu_context())).to_be_false();
	});

	it("does not take the button's level as the hold any more", _ {
		KitMenu k;
		k.open(nodes::lpf_freq);
		k.shell.tick(0);
		k.shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_BUTTON, 3, 0, 255}, 5); // AFFECT ENTIRE down
		k.shell.tick(10);
		expect(menu::bindings::writes_whole_kit(menu_scene().menu_context())).to_be_false();
		k.shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_BUTTON, 3, 0, 0}, 15);
		k.shell.tick(20);
	});

	it("answers for a horizontal page's focused column and a vertical list's non-entering row", _ {
		// `Submenu::usesAffectEntire`: the focused child on a horizontal page, or a vertical row that does not enter.
		KitMenu k;
		k.open(nodes::lpf_freq); // lands on the FILTER page, focused on LPF frequency
		k.shell.tick(0);
		expect(menu::uses_affect_entire(menu_scene().menu_context(), menu_scene().state())).to_be_true();
		KitMenu arp;
		arp.open(nodes::arp); // the ARPEGGIATOR list, its cursor on MODE, a row that flips rather than entering
		arp.shell.tick(0);
		expect(menu_scene().focused() == &nodes::arp).to_be_true();
		expect(menu::uses_affect_entire(menu_scene().menu_context(), menu_scene().state())).to_be_true();
	});

	it("does not take AFFECT ENTIRE as a hold in the kit's own FX menu", _ {
		// `editingKitRow()` is false for the kit's global FX, so its items toggle the clip's affect-entire.
		KitMenu k;
		currentSong = k.song.song;
		MenuContext ctx = menu::context_for(*k.song.song, deluge::gui::menuViewOf(k.song.clip), 0, false, false, false,
		                                    /*kit_global_fx=*/true);
		(void)menu::assign_node_ids(nodes::kAllRoots);
		menu_scene().open(nodes::root_kit_global_fx, ctx);
		k.shell.init(SceneId{SceneKind::FirstApp, menu::node_id(nodes::lpf_freq)});
		k.shell.tick(0);
		expect(menu_scene().focused_value() == &nodes::lpf_freq).to_be_true();
		expect(menu_scene().takes_affect_entire()).to_be_false();
	});
});
// clang-format on

DELUGE_SPEC(menu_affect_entire_spec, menu_affect_entire)
