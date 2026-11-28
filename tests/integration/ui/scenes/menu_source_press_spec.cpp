/// @file
/// @brief SHIFT and a patching-source pad under the menu: the press opens the page that patches that source to
///        what the menu shows, as `SoundEditor::potentialShortcutPadAction`'s source arm does in `main`.

#include "gui/ui/menu_view_of.h"
#include "ui/scenes/menu/context.h"
#include "ui/scenes/menu/menu_scene.h"
#include "ui/scenes/menu/node.h"
#include "ui/scenes/menu/node_id.h"
#include "ui/scenes/menu/nodes/nodes.h"
#include "ui/scenes/menu/nodes/patch_cables.h"
#include "ui/scenes/menu/pads.h"
#include "ui/scenes/menu/shortcuts.h"
#include "ui/scenes/profile.h"
#include "ui/scenes/registry.h"

#include "base/notify/notice_queue.h"
#include "ui/shell/profiles/deluge.h"
#include "ui/shell/shell.h"

#include "gui/l10n/l10n.h"
#include "modulation/params/param.h"
#include "modulation/patch/patch_cable.h"

#include "support/menu_rig.h"
#include "support/popup_capture.h"
#include "support/shortcut_layout.h"
#include "support/song_fixture.h"

#include "support/mocks/fake_surfaces.h"

#include "cppspec.hpp"

#include "support/menu_context.h"
#include "support/spec_main.h"
#include <algorithm>
#include <string>
#include <vector>

using namespace deluge::ui::scenes;
using deluge::ui::scenes::menu::MenuContext;
using deluge::ui::scenes::menu::MenuScene;
using deluge::ui::scenes::menu::Node;
using deluge::ui::shell::BoardProfile;
using deluge::ui::shell::SceneId;
using deluge::ui::shell::SceneKind;
using deluge::ui::shell::Shell;
using deluge::ui::shell::ShellConfig;

namespace {

namespace nodes = deluge::ui::scenes::menu::nodes;
namespace params = deluge::modulation::params;

constexpr uint8_t kShiftX = 8, kShiftY = 0;
constexpr uint8_t kSelectEncoder = 3;
constexpr uint8_t kSelectEncButtonX = 4, kSelectEncButtonY = 3;
constexpr uint8_t kMidiX = 5, kMidiY = 2;
constexpr uint8_t kCvX = 5, kCvY = 3;
constexpr uint8_t kBackX = 7, kBackY = 1;
/// The MIDI and CV buttons' board LEDs on the Deluge profile (`ui/shell/profiles/deluge.cpp`).
constexpr uint8_t kMidiLed = 23, kCvLed = 32;

void select_language() {
	deluge::l10n::chosenLanguage = &deluge::l10n::built_in::english;
	deluge::base::notify::Notice discard;
	while (deluge::base::notify::notices().pop(discard)) {}
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
		const MenuContext ctx = spec::ctx_of(*song.song, deluge::gui::menuViewOf(song.clip), 0, false, false, false,
		                                     /*kit_global_fx=*/false);
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
	void button(uint8_t x, uint8_t y, bool down) {
		shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_BUTTON, x, y, static_cast<int16_t>(down ? 255 : 0)},
		                     now += 10);
		tick();
	}
	void press(uint8_t x, uint8_t y) {
		button(x, y, true);
		button(x, y, false);
		tick();
	}
	/// @brief @p detents of the select encoder, two edges each.
	void turn(int32_t detents) {
		shell.on_board_event(
		    DelugeInputEvent{DELUGE_EVENT_ENCODER, kSelectEncoder, 0, static_cast<int16_t>(detents * 2)}, now += 10);
		tick();
		tick();
	}
	/// @brief Whether board LED @p led is lit, as the surfaces were last told.
	bool led(uint8_t led) const {
		return ("\n" + fake.state()).find("\nled " + std::to_string(led) + " on\n") != std::string::npos;
	}
	/// @brief Open the VELOCITY -> volume cable's strength page (the fixture's synth patches it).
	void open_velocity_cable() {
		open(nodes::master_volume);
		shift_press(15, 1);
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
		expect(menu_scene().focused()).to_equal(&nodes::patch_cable_strength_regular);
		expect(int(r.st().depth)).to_equal(1);
		expect(int(r.st().walk.patch_source)).to_equal(int(PatchSource::VELOCITY));
	});

	it("opens the volume cable the source already drives, not a new POST_FX one", _ {
		// `Regular::checkPermissionToBeginSession`
		// (`origin/main:src/deluge/gui/menu_item/patch_cable_strength/regular.cpp`): a source POST_FX refuses is
		// retried on POST_REVERB_SEND, then LOCAL_VOLUME, each asked on its own. The fixture's synth patches VELOCITY
		// to LOCAL_VOLUME.
		select_language();
		Rig r;
		r.open(nodes::master_volume);
		r.shift_press(15, 1); // VELOCITY
		expect(int(r.st().walk.patching_param)).to_equal(int(params::LOCAL_VOLUME));
		expect(r.st().edit_int).not_().to_equal(0); // the existing cable's depth, not an empty one
	});

	it("a second press of the active source on a patch-cable page takes its second layer", _ {
		select_language();
		Rig r;
		r.open(nodes::lpf_freq); // ENV1 cannot patch to the volume triple
		r.shift_press(14, 6); // ENVELOPE_0
		expect(int(r.st().walk.patch_source)).to_equal(int(PatchSource::ENVELOPE_0));
		r.shift_press(14, 6); // the active source's pad again: ENVELOPE_2
		expect(menu_scene().focused()).to_equal(&nodes::patch_cable_strength_regular);
		expect(int(r.st().walk.patch_source)).to_equal(int(PatchSource::ENVELOPE_2));
	});

	it("opens the range page when another source pad is held", _ {
		select_language();
		Rig r;
		r.open(nodes::master_volume);
		r.shift(true);
		r.pad(15, 1, true); // VELOCITY, held
		r.pad(15, 2, true); // RANDOM, on that cable's depth
		expect(menu_scene().focused()).to_equal(&nodes::patch_cable_strength_range);
		expect(int(r.st().walk.patch_source)).to_equal(int(PatchSource::VELOCITY));
		expect(int(r.st().walk.patch_range_source)).to_equal(int(PatchSource::RANDOM));
	});

	it("closes the menu when nothing below depth 0 takes the press", _ {
		// A strength page at the root answers "ask the item above"; there is none, so the menu closes (`main`'s
		// `exitCompletely()`). Driven at the unit: the page opens only with its walk already chosen.
		select_language();
		Rig r;
		const MenuContext ctx = spec::ctx_of(*r.song.song, deluge::gui::menuViewOf(r.song.clip), 0, false, false,
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
		const deluge::ui::widgets::core::PadMask held_pads{};
		const deluge::ui::shell::UiContext ui{held, held_pads, true, false, 0, deluge_profile()};
		const std::optional<deluge::ui::shell::Result> got =
		    pads.source_press(menu::MenuCore{core_ctx, st}, walk, session, 15, 2, ui);
		expect(got.has_value()).to_be_true();
		if (got.has_value()) {
			expect(got->handled).to_be_true();
		}
		const bool removes = got.has_value() && !got->actions.empty()
		                     && got->actions[0].kind == deluge::ui::shell::Action::Kind::Remove;
		expect(removes).to_be_true();
	});

	it("falls through to the randomizer on (15,1) when no item takes it", _ {
		const spec::ShortcutLayout pinned_v3{deluge::project::ShortcutsVersion::V3}; // the layout the lookups name
		select_language();
		Rig r;
		r.open(nodes::synth_mode);
		r.shift_press(15, 1);
		const Node* randomizer = menu::shortcut_node(MenuContext::Root::Sound, deluge::project::ShortcutsVersion::V3, false, 15, 1);
		expect(randomizer).not_().to_be_null();
		expect(menu_scene().focused_value()).to_equal(randomizer);
	});

	it("swallows a source cell past the last patch source", _ {
		select_language();
		const spec::ShortcutLayout v1{deluge::project::ShortcutsVersion::V1}; // (15, 7) names no source
		Rig r;
		r.open(nodes::master_volume);
		deluge::ui::scenes::spec::PopupCapture popups;
		r.shift_press(15, 7);
		expect(popups.seen()).to_equal(std::vector<std::string>{}); // no PARAMETER NOT APPLICABLE for a non-source
		expect(r.shell.stack().depth()).to_equal(size_t(1));
		expect(menu_scene().focused()).to_equal(&nodes::master_volume);
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
		const spec::ShortcutLayout pinned_v3{deluge::project::ShortcutsVersion::V3}; // the layout the lookups name
		select_language();
		Rig r;
		r.open(nodes::vibrato);
		const menu::ShortcutPos own = menu::shortcut_pos(MenuContext::Root::Sound, deluge::project::ShortcutsVersion::V3, nodes::vibrato);
		expect(own.valid).to_be_true();
		r.shift_press(own.x, own.y);
		expect(r.st().second_layer).to_be_false();
	});

	it("lights MIDI on a bipolar cable's page and CV on a unipolar one, and the two buttons switch it", _ {
		// `PatchCableStrength::buttonAction` and `updatePolarityUI`
		// (`origin/main:src/deluge/gui/menu_item/patch_cable_strength.cpp`).
		select_language();
		Rig r;
		r.open_velocity_cable();
		r.press(kCvX, kCvY);
		expect(r.st().walk.patch_polarity).to_equal(Polarity::UNIPOLAR);
		expect(r.led(kCvLed)).to_be_true();
		expect(r.led(kMidiLed)).to_be_false();
		r.press(kMidiX, kMidiY);
		expect(r.st().walk.patch_polarity).to_equal(Polarity::BIPOLAR);
		expect(r.led(kMidiLed)).to_be_true();
		expect(r.led(kCvLed)).to_be_false();
		expect(menu_scene().focused()).to_equal(&nodes::patch_cable_strength_regular);
	});

	it("puts MIDI and CV out on BACK from the page", _ {
		// `endSession` (`origin/main:src/deluge/gui/menu_item/patch_cable_strength.cpp`); the MASTER page focused on
		// VOLUME lights KIT only.
		select_language();
		Rig r;
		r.open_velocity_cable();
		r.press(kMidiX, kMidiY);
		r.press(kBackX, kBackY);
		expect(menu_scene().focused()).not_().to_equal(&nodes::patch_cable_strength_regular);
		expect(r.led(kMidiLed)).to_be_false();
		expect(r.led(kCvLed)).to_be_false();
	});

	it("leaves MIDI and CV to the page buttons on a horizontal page focused on a cable column", _ {
		// Legacy's `beginSession` on a horizontal page's column lights nothing: only the strength page itself, the
		// focused screen, declares the pair. DUCK is the SIDECHAIN page's first column, so the page lights SYNTH and
		// neither MIDI nor CV; its source, SIDECHAIN, has a polarity the strength page would light.
		select_language();
		Rig r;
		r.open(nodes::sidechain);
		expect(menu_scene().focused()).to_equal(&nodes::sidechain);
		expect(menu_scene().focused_value()).to_equal(&nodes::sidechain_volume);
		expect(r.led(kMidiLed)).to_be_false();
		expect(r.led(kCvLed)).to_be_false();
	});

	it("a turn with Select held switches the polarity, leaves the strength, and its release opens nothing", _ {
		// `PatchCableStrength::selectEncoderAction` (`origin/main:src/deluge/gui/menu_item/patch_cable_strength.cpp`):
		// up unipolar, down bipolar, `selectButtonPressUsedUp`.
		select_language();
		Rig r;
		r.open_velocity_cable();
		const int32_t strength = r.st().edit_int;
		r.button(kSelectEncButtonX, kSelectEncButtonY, true);
		r.turn(2);
		expect(r.st().walk.patch_polarity).to_equal(Polarity::UNIPOLAR);
		expect(r.st().edit_int).to_equal(strength);
		r.button(kSelectEncButtonX, kSelectEncButtonY, false);
		r.tick();
		expect(menu_scene().focused()).to_equal(&nodes::patch_cable_strength_regular);
		r.button(kSelectEncButtonX, kSelectEncButtonY, true);
		r.turn(-2);
		r.button(kSelectEncButtonX, kSelectEncButtonY, false);
		r.tick();
		expect(r.st().walk.patch_polarity).to_equal(Polarity::BIPOLAR);
		expect(r.st().edit_int).to_equal(strength);
		expect(menu_scene().focused()).to_equal(&nodes::patch_cable_strength_regular);
	});

	it("SHIFT and a turn edits the strength five places a detent and leaves the polarity", _ {
		// `SoundEditor::selectEncoderAction`'s 5x under SHIFT (`origin/main:src/deluge/gui/ui/sound_editor.cpp`), then
		// `Decimal::selectEncoderAction`; no polarity arm without Select held.
		select_language();
		Rig r;
		r.open_velocity_cable();
		r.press(kCvX, kCvY); // unipolar, so a down turn that flipped it would show
		const int32_t strength = r.st().edit_int;
		r.shift(true);
		r.turn(-1);
		r.shift(false);
		expect(r.st().edit_int).to_equal(std::max(strength - 500, int32_t{kMinMenuPatchCableValue}));
		expect(r.st().walk.patch_polarity).to_equal(Polarity::UNIPOLAR);
	});
});
// clang-format on

DELUGE_SPEC(menu_source_press_spec, menu_source_press)
