#pragma once

/// @file
/// @brief What the DX7 specs share: a synth whose first oscillator is DX7 with legacy's DX7 items pointed at it, the
///        ported menu over it on a `Shell` over the fake surfaces, and the panel readers the pixel examples compare.
///
/// For the spec files only: it names `gui/` objects, which the scenes library may not.

#include "gui/ui/menu_view_of.h"
#include "scenes/menu/context.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/menu/node_id.h"
#include "scenes/menu/nodes/nodes.h"
#include "scenes/profile.h"
#include "scenes/registry.h"

#include "shell/profiles/deluge.h"
#include "shell/shell.h"

#include "gui/l10n/l10n.h"
#include "gui/ui/sound_editor.h"
#include "hid/display/oled.h"
#include "notify/notice_queue.h"
#include "processing/dx7/dx_patch.h"
#include "processing/sound/sound.h"
#include "processing/source.h"

#include "spec/gfx/support/pbm.h"
#include "spec/shell/support/fake_surfaces.h"
#include "support/song_fixture.h"

#include <cstdint>
#include <string>

/// @brief Legacy's held-Shift state, which `Buttons::isShiftButtonPressed` reads and `hid/buttons.h` does not declare.
namespace Buttons {
extern bool shiftCurrentlyPressed;
extern bool shiftCurrentlyStuck;
} // namespace Buttons

namespace deluge::scenes::spec::dx_rig {

using deluge::scenes::menu::MenuContext;
using deluge::scenes::menu::Node;
using deluge::ui::shell::BoardProfile;
using deluge::ui::shell::SceneId;
using deluge::ui::shell::SceneKind;
using deluge::ui::shell::Shell;
using deluge::ui::shell::ShellConfig;
namespace nodes = deluge::scenes::menu::nodes;

/// @brief A synth whose first oscillator is DX7, set up for legacy's DX7 items.
///
/// Legacy's items read `soundEditor.currentSound`; `setOscType` gives the source its patch, as it does on the device.
struct DxRig {
	spec::SongFixture song = spec::make_synth_song();
	Song* saved = currentSong;
	Sound* sound = nullptr;

	DxRig() {
		currentSong = song.song;
		sound = static_cast<Sound*>(static_cast<ModControllableAudio*>(song.instrument->toModControllable()));
		sound->sources[0].setOscType(OscType::DX7);
		soundEditor.currentSound = sound;
		soundEditor.currentSource = &sound->sources[0];
		soundEditor.currentSourceIndex = 0;
	}
	~DxRig() {
		set_shift(false);
		currentSong = saved;
	}
	DxRig(const DxRig&) = delete;
	DxRig& operator=(const DxRig&) = delete;

	DxPatch& patch() { return *sound->sources[0].ensureDxPatch(); }

	deluge::project::SlotAddress slot() {
		return *menu::context_for(*song.song, deluge::gui::menuViewOf(song.clip), 0, false, false, false, false)
		            .addresses.slot;
	}

	/// @brief Hold or release Shift on legacy's side, which is all `isShiftButtonPressed` reads.
	static void set_shift(bool on) {
		::Buttons::shiftCurrentlyPressed = on;
		::Buttons::shiftCurrentlyStuck = false;
	}
};

/// @brief The language the port's and legacy's text come from, and no notice left over from another example.
inline void select_language() {
	deluge::l10n::chosenLanguage = &deluge::l10n::built_in::english;
	deluge::notify::Notice discard;
	while (deluge::notify::notices().pop(discard)) {}
}

/// @brief `OLED::main`, into which every legacy `renderOLED()` draws, as a PBM.
inline std::string legacy_panel_pbm() {
	const auto& canvas = deluge::hid::display::OLED::main;
	return page_major_to_pbm(canvas.bytes().data(), OLED_MAIN_WIDTH_PIXELS, OLED_MAIN_HEIGHT_PIXELS);
}

/// @brief @p pbm without the five rows above the 48-row panel's visible top, which the port's root frame cannot ink.
inline std::string below_bezel(const std::string& pbm) {
	size_t pos = 0;
	for (int newlines = 0; pos < pbm.size() && newlines < 2; pos++) {
		newlines += pbm[pos] == '\n' ? 1 : 0;
	}
	std::string out = pbm.substr(0, pos);
	for (int i = 0; i < 5 && pos < pbm.size(); i++) {
		const size_t next = pbm.find('\n', pos);
		pos = next == std::string::npos ? pbm.size() : next + 1;
	}
	return out + pbm.substr(pos);
}

/// Matrix coordinates and encoder indices (`kMatrix`, `kDelugeEncoders`, `shell/profiles/deluge.cpp`).
constexpr uint8_t kSelectEncoder = 3;
constexpr uint8_t kHorizontalEncoder = 1;
constexpr uint8_t kSelectEncButtonX = 4, kSelectEncButtonY = 3;
constexpr uint8_t kBackX = 7, kBackY = 1;
constexpr uint8_t kShiftX = 8, kShiftY = 0;

/// @brief The DX7 synth with the ported menu over it, on a `Shell` over the fake surfaces. Node ids come from every
///        root, as the app numbers them.
struct MenuRig : DxRig {
	const BoardProfile& profile = deluge_profile();
	deluge::ui::shell::SceneRegistry reg = scene_registry();
	uitest::FakeSurfaceSet fake{deluge::ui::shell::kDelugeBoard, deluge_profile()};
	Shell shell{fake, profile, reg, deluge::ui::shell::Services{}, ShellConfig{}};
	uint32_t now = 0;

	MenuRig() = default;
	~MenuRig() {
		while (shell.stack().depth() > 0) {
			shell.stack().pop(SceneKind::Null);
		}
		menu_scene().reset_process_state();
	}
	MenuRig(const MenuRig&) = delete;
	MenuRig& operator=(const MenuRig&) = delete;

	MenuContext menu_context() {
		return menu::context_for(*song.song, deluge::gui::menuViewOf(song.clip), 0, false, false, false, false);
	}
	/// @brief Open the menu on @p root, writing to @p sink (the app's when null), and draw it once.
	void open(const Node& root, bind::WriteSink* sink = nullptr) {
		MenuContext ctx = menu_context();
		if (sink != nullptr) {
			ctx.sink = sink;
		}
		(void)menu::assign_node_ids(nodes::kAllRoots);
		while (shell.stack().depth() > 0) { // a fresh push, so the first frame is drawn whole
			shell.stack().pop(SceneKind::Null);
		}
		menu_scene().open(root, ctx);
		shell.init(SceneId{SceneKind::FirstApp, menu::node_id(root)});
		tick();
	}
	void tick() { shell.tick(now += 10); }
	void button(uint8_t x, uint8_t y, bool on) {
		shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_BUTTON, x, y, static_cast<int16_t>(on ? 255 : 0)},
		                     now += 10);
	}
	void select() {
		button(kSelectEncButtonX, kSelectEncButtonY, true);
		button(kSelectEncButtonX, kSelectEncButtonY, false);
		tick();
	}
	void back() {
		button(kBackX, kBackY, true);
		button(kBackX, kBackY, false);
		tick();
	}
	/// @brief @p detents clicks of a detented encoder: two A-pin edges each.
	void turn(int32_t detents, uint8_t encoder = kSelectEncoder) {
		shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_ENCODER, encoder, 0, static_cast<int16_t>(detents * 2)},
		                     now += 10);
		tick();
	}
	const Node* focused() const { return menu_scene().focused(); }
	const menu::MenuState& state() const { return menu_scene().state(); }
	int16_t cursor() const { return state().list[state().depth].cursor; }
};

} // namespace deluge::scenes::spec::dx_rig
