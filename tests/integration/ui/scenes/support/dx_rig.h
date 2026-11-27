#pragma once

/// @file
/// @brief What the DX7 specs and the menu oracle freezer share: a synth in DX7 mode, the menu
///        over it on a `Shell` over the fake surfaces, and the panel readers the pixel examples compare.
///
/// For the spec files only: it names `gui/` objects, which the scenes library may not.

#include "gui/ui/browser_host.h"
#include "gui/ui/menu_view_of.h"
#include "support/sound_fixtures.h"
#include "ui/scenes/menu/context.h"
#include "ui/scenes/menu/menu_scene.h"
#include "ui/scenes/menu/node_id.h"
#include "ui/scenes/menu/nodes/nodes.h"
#include "ui/scenes/profile.h"
#include "ui/scenes/registry.h"

#include "ui/shell/profiles/deluge.h"
#include "ui/shell/shell.h"

#include "base/notify/notice_queue.h"
#include "gui/l10n/l10n.h"
#include "hid/display/display.h"
#include "processing/dx7/dx_patch.h"
#include "processing/sound/sound.h"

#include "support/menu_context.h"
#include "support/menu_engine.h"
#include "support/mocks/fake_surfaces.h"
#include "support/oracle_table.h"
#include "support/song_fixture.h"

#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace deluge::ui::scenes::spec::dx_rig {

using deluge::ui::scenes::menu::MenuContext;
using deluge::ui::scenes::menu::Node;
using deluge::ui::shell::BoardProfile;
using deluge::ui::shell::SceneId;
using deluge::ui::shell::SceneKind;
using deluge::ui::shell::Shell;
using deluge::ui::shell::ShellConfig;
namespace nodes = deluge::ui::scenes::menu::nodes;

/// @brief A synth in DX7 mode, its DX7 holding a voice.
struct DxRig {
	spec::SongFixture song = spec::make_synth_song();
	Song* saved = currentSong;
	Sound* sound = nullptr;

	DxRig() {
		deluge::ui::scenes::spec::make_current(song.song);
		sound = song.instrument->toModControllable()->effectsOwner()->sound;
		deluge::test::setSynthMode(*sound, deluge::instruments::classic::SynthMode::DX7);
		(void)sound->dx7Patch.ensure();
	}
	~DxRig() { deluge::ui::scenes::spec::make_current(saved); }
	DxRig(const DxRig&) = delete;
	DxRig& operator=(const DxRig&) = delete;

	DxPatch& patch() { return *sound->dx7Patch.ensure(); }

	deluge::project::SlotAddress slot() {
		return *spec::ctx_of(*song.song, deluge::gui::menuViewOf(song.clip), 0, false, false, false, false)
		            .addresses.slot;
	}
};

/// @brief The language the menu's text comes from, and no notice left over from another example.
inline void select_language() {
	deluge::l10n::chosenLanguage = &deluge::l10n::built_in::english;
	deluge::base::notify::Notice discard;
	while (deluge::base::notify::notices().pop(discard)) {}
}

/// @brief The panel `pixels/<name>.pbm` of the reference tables holds, or nothing where there is no such file.
inline std::string frozen_panel(std::string_view name) {
	std::ifstream in(oracle_dir() + "/pixels/" + std::string{name} + ".pbm");
	std::stringstream text;
	text << in.rdbuf();
	return text.str();
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

/// Matrix coordinates and encoder indices (`kMatrix`, `kDelugeEncoders`, `ui/shell/profiles/deluge.cpp`).
constexpr uint8_t kSelectEncoder = 3;
constexpr uint8_t kHorizontalEncoder = 1;
constexpr uint8_t kSelectEncButtonX = 4, kSelectEncButtonY = 3;
constexpr uint8_t kBackX = 7, kBackY = 1;
constexpr uint8_t kShiftX = 8, kShiftY = 0;

/// @brief A `Display` that keeps the popups legacy shows: `display` is null in a spec process.
struct PopupDisplay final : deluge::hid::Display {
	std::vector<std::string> popups;
	constexpr size_t getNumBrowserAndMenuLines() override { return 3; }
	void displayPopup(char const* text, int8_t, bool, uint8_t, int32_t, PopupType) override {
		popups.emplace_back(text);
	}
	void popupText(char const*, PopupType) override {}
	void popupTextTemporary(char const*, PopupType) override {}
	void cancelPopup() override {}
	void freezeWithError(char const*) override {}
	void displayError(Error) override {}
	void removeWorkingAnimation() override {}
	void displayLoadingAnimationText(char const*, bool, bool) override {}
	void removeLoadingAnimation() override {}
	bool hasPopup() override { return false; }
	bool hasPopupOfType(PopupType) override { return false; }
	void consoleText(char const*) override {}
	void timerRoutine() override {}
};

/// @brief The popups shown while it lives, in English.
struct Popups {
	PopupDisplay mine;
	deluge::hid::Display* saved = display;
	Popups() {
		display = &mine;
		deluge::l10n::chosenLanguage = &deluge::l10n::built_in::english;
	}
	~Popups() { display = saved; }
	Popups(const Popups&) = delete;
	Popups& operator=(const Popups&) = delete;
	static std::string read_error() { return deluge::l10n::get(deluge::l10n::String::STRING_FOR_DX_ERROR_READ_ERROR); }
};

/// @brief The DX7 synth with the menu over it, on a `Shell` over the fake surfaces. Node ids come from every
///        root, as the app numbers them.
struct MenuRig : DxRig {
	const BoardProfile& profile = deluge_profile();
	deluge::ui::shell::SceneRegistry reg = scene_registry();
	uitest::FakeSurfaceSet fake{deluge::ui::shell::kDelugeBoard, deluge_profile()};
	Shell shell{fake, profile, reg, deluge::ui::shell::Services{}, ShellConfig{}};
	uint32_t now = 0;

	/// The application's browser host, as the guest installs it: a `.syx` file chosen here is read for real.
	MenuRig() { deluge::gui::ui::wireShellBrowserHost(); }
	~MenuRig() {
		while (shell.stack().depth() > 0) {
			shell.stack().pop(SceneKind::Null);
		}
		menu_scene().reset_process_state();
	}
	MenuRig(const MenuRig&) = delete;
	MenuRig& operator=(const MenuRig&) = delete;

	MenuContext menu_context() {
		return spec::ctx_of(*song.song, deluge::gui::menuViewOf(song.clip), 0, false, false, false, false);
	}
	/// @brief Open the menu on @p root, its sound edits going to @p sound_writes (the app's when null), over the
	///        keyboard screen when @p keyboard_root, and draw it once.
	void open(const Node& root, bind::SoundWrites* sound_writes = nullptr, bool keyboard_root = false) {
		MenuContext ctx = menu_context();
		if (sound_writes != nullptr) {
			ctx.ports.sound_writes = sound_writes;
		}
		ctx.root_is_keyboard = keyboard_root;
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
	/// @brief A main-grid or sidebar pad, pressed (`on`) or released, then a tick.
	void pad(uint8_t x, uint8_t y, bool on) {
		shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_PAD, x, y, static_cast<int16_t>(on ? 64 : 0)}, now += 10);
		tick();
	}
	/// @brief Shift held, a tick, the pad pressed and released, Shift let go: a shortcut press.
	void shift_pad(uint8_t x, uint8_t y) {
		button(kShiftX, kShiftY, true);
		tick();
		pad(x, y, true);
		pad(x, y, false);
		button(kShiftX, kShiftY, false);
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

} // namespace deluge::ui::scenes::spec::dx_rig
