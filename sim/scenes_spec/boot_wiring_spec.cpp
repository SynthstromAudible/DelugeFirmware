/// @file
/// @brief The boot wiring `src/scenes/registry.h`'s note asked for, driven against the real
///        application globals.
///
/// Three settings the ported tree and the guest's `Shell` cannot read for themselves, plus the
/// three services the wiring installs. This is the spec level they
/// can be checked at: the host suite (`tests/unit/deluge/gui/shell_host_ui_lifecycle_spec.cpp`) deliberately never
/// builds the `deluge_scenes` closure, so it can see that `ShellHostUI::opened()` *calls* the wiring
/// but not what the wiring installs; this executable links the real `deluge_scenes` **and** the real
/// `deluge_app`, so `FlashStorage`, `view` and `shellHostUI` are all the shipped
/// objects.
///
/// @note **Every group restores what it changed.** These are process globals shared with every other
///       spec in this binary -- `shortcuts_spec.cpp`'s own `LegacyVersion1` guard exists for the
///       same reason -- so each example saves and puts back the setting it drives.

#include "scenes/browser/browser_scene.h"
#include "scenes/menu/node_id.h"
#include "scenes/menu/nodes/nodes.h"
#include "scenes/menu/shortcuts.h"
#include "scenes/registry.h"
#include "scenes/services/global_controls.h"
#include "scenes/services/midi_learn.h"
#include "scenes/services/mod_knobs.h"

#include "shell/router.h"
#include "shell/service.h"
#include "shell/shell.h"

#include "gui/ui/shell_guest_services.h"
#include "gui/ui/shell_host_ui.h"
#include "gui/views/view.h"

#include "gui/ui_timer_manager.h"
#include "hid/display/display.h"
#include "hid/display/screensaver.h"
#include "scenes/overlay/param_popup_scene.h"

#include "model/model_stack.h"
#include "model/song/song.h"
#include "processing/engines/audio_engine.h"
#include "storage/flash_storage.h"

#include "support/printers/knob_focus_printer.h"
#include "support/printers/project_address_printer.h"
#include "support/song_fixture.h"

#include "cppspec.hpp"
#include "support/matchers/value_matchers.h"

#include <cstdint>
#include <optional>
#include <string>

#include "support/spec_main.h"

using deluge::scenes::menu::ShortcutsVersion;
namespace menu = deluge::scenes::menu;
namespace services = deluge::scenes::services;

namespace {

/// @brief Save and restore the two screensaver settings around an example.
struct ScreensaverSettings {
	ScreensaverMode mode = FlashStorage::screensaverMode;
	uint8_t minutes = FlashStorage::screensaverTimeoutMinutes;
	uint32_t idle_ms = shellHostUI.shell().screensaver_idle_ms();

	~ScreensaverSettings() {
		FlashStorage::screensaverMode = mode;
		FlashStorage::screensaverTimeoutMinutes = minutes;
		shellHostUI.shell().set_screensaver_idle_ms(idle_ms);
	}
};

/// @brief Set both settings and run legacy's own notifier, which is where the port is re-supplied
///        from.
/// @return What the guest's shell is now running its screensaver service on.
uint32_t idle_after(ScreensaverMode mode, uint8_t minutes) {
	FlashStorage::screensaverMode = mode;
	FlashStorage::screensaverTimeoutMinutes = minutes;
	deluge::hid::display::Screensaver::settingsChanged();
	return shellHostUI.shell().screensaver_idle_ms();
}

/// @brief A display that keeps the notification last drawn, how many it drew, and that one is up once drawn.
struct NotificationDisplay final : deluge::hid::Display {
	std::string title;
	std::string value;
	int draws = 0;
	constexpr size_t getNumBrowserAndMenuLines() override { return 3; }
	void displayPopup(char const*, int8_t, bool, uint8_t, int32_t, PopupType) override {}
	void popupText(char const*, PopupType) override {}
	void popupTextTemporary(char const*, PopupType) override {}
	void cancelPopup() override {}
	void freezeWithError(char const*) override {}
	void displayError(Error) override {}
	void removeWorkingAnimation() override {}
	void displayLoadingAnimationText(char const*, bool, bool) override {}
	void removeLoadingAnimation() override {}
	bool hasPopup() override { return draws > 0; }
	bool hasPopupOfType(PopupType type) override { return draws > 0 && type == PopupType::NOTIFICATION; }
	void consoleText(char const*) override {}
	void timerRoutine() override {}
	void displayNotification(std::string_view t, std::optional<std::string_view> v) override {
		title = t;
		value = v.value_or("");
		draws++;
	}
};

} // namespace

// clang-format off
describe boot_node_ids("scenes::assigned_node_ids at boot", $ {
	it("numbers the real tree, and every statically reachable root has an id", _ {
		// `menu::assign_node_ids` had no caller outside the specs until `assigned_node_ids` was
		// added, and three features no-op on an unnumbered tree -- `MenuScene::save_state`,
		// `restore_state` and the shortcut-pad jump (`scenes/registry.h`). `deluge_boot` calls this
		// explicitly; in this binary `shellHostUI`'s own construction has already called it through
		// `scene_registry()`, which is exactly why the boot call is spelled out rather than left to
		// static-initialisation order.
		const uint16_t assigned = deluge::scenes::assigned_node_ids();
		expect(assigned).to_be_greater_than(0);

		// Idempotent: a second call reports the same number rather than re-numbering.
		expect(deluge::scenes::assigned_node_ids()).to_equal(assigned);

		// And the ids are on the nodes, not merely counted. Two roots from different tables, so a
		// walk that numbered only the first would fail.
		expect(menu::node_id(deluge::scenes::menu::nodes::sound_root)).not_().to_equal(0);
		expect(menu::node_id(deluge::scenes::menu::nodes::root_settings)).not_().to_equal(0);
		expect(menu::node_id(deluge::scenes::menu::nodes::sound_root))
		    .not_().to_equal(menu::node_id(deluge::scenes::menu::nodes::root_settings));
	});
});

describe boot_shortcuts_version("the shortcut layout follows flash byte 60", $ {
	it("the ported tables select on FlashStorage::shortcutsVersion, which every path writes", _ {
		const uint8_t saved = FlashStorage::shortcutsVersion;

		// Boot (`FlashStorage::readSettings`, from byte 60), the factory resets (`FlashStorage::resetSettings`, reached
		// from the ported reset menu) and the ported settings item
		// (`menu::set_shortcuts_version`) each write this one byte, so the example drives the byte and the setter
		// rather than `resetSettings`, which rewrites every setting this process shares with the other specs in this
		// binary.
		//
		// Both values byte 60 can hold, in both directions -- a read that only ever followed one would pass a one-way
		// check.
		FlashStorage::shortcutsVersion = FlashStorage::kShortcutsVersion1;
		expect(menu::shortcuts_version()).to_equal(ShortcutsVersion::V1);
		FlashStorage::shortcutsVersion = FlashStorage::kShortcutsVersion3;
		expect(menu::shortcuts_version()).to_equal(ShortcutsVersion::V3);
		menu::set_shortcuts_version(ShortcutsVersion::V1);
		expect(menu::shortcuts_version()).to_equal(ShortcutsVersion::V1);

		// The layout the port actually resolves moves with it -- the setting is not just recorded.
		// `(0, 6)` of the sound table is interpolation under V3 and the sample end point under V1,
		// which `shortcuts_spec.cpp` establishes is one of the only two cells that genuinely move.
		expect(menu::shortcut_node(menu::MenuContext::Root::Sound, false, 0, 6))
		    .to_equal(&deluge::scenes::menu::nodes::sample0_end);
		menu::set_shortcuts_version(ShortcutsVersion::V3);
		expect(menu::shortcut_node(menu::MenuContext::Root::Sound, false, 0, 6))
		    .to_equal(&deluge::scenes::menu::nodes::sample0_interpolation);

		// And a byte that is neither enumerator takes the V3 layout, as legacy's own `default` does.
		FlashStorage::shortcutsVersion = 7;
		expect(menu::shortcuts_version()).to_equal(ShortcutsVersion::V3);

		FlashStorage::shortcutsVersion = saved;
	});
});

describe boot_screensaver("ShellConfig::screensaver_idle_ms follows the two settings", $ {
	it("is the OFF-or-minutes formula, re-supplied by Screensaver::settingsChanged", _ {
		const ScreensaverSettings guard;

		// `screensaverMode == ScreensaverMode::OFF ? 0 : screensaverTimeoutMinutes * 60000`
		// (`hid/display/screensaver.cpp`'s `arm()` and its three OFF early-outs). Both settings
		// varied, and each varied while the other is held, so "always 0" and "always the minutes"
		// both fail -- and so does an implementation that reads only one of the two.
		expect(idle_after(ScreensaverMode::OFF, 5)).to_equal(uint32_t(0));
		expect(idle_after(ScreensaverMode::BLANK, 5)).to_equal(uint32_t(300000));
		expect(idle_after(ScreensaverMode::STARSCAPE, 5)).to_equal(uint32_t(300000));
		expect(idle_after(ScreensaverMode::DELUGE, 1)).to_equal(uint32_t(60000));
		expect(idle_after(ScreensaverMode::DELUGE, 60)).to_equal(uint32_t(3600000));
		// OFF wins over any timeout, which is the whole of what "0 is off" ports.
		expect(idle_after(ScreensaverMode::OFF, 60)).to_equal(uint32_t(0));
	});

	it("without the re-supply the shell keeps the number it was built with", _ {
		const ScreensaverSettings guard;
		// The failure this wiring exists to stop: `ShellConfig::screensaver_idle_ms` defaults to 0
		// and `shellHostUI` is a static-init global that cannot read flash, so a settings change
		// that nobody pushes in leaves the ported screensaver switched off for ever. Writing the
		// globals *without* calling the notifier must therefore change nothing.
		(void)idle_after(ScreensaverMode::DELUGE, 3);
		expect(shellHostUI.shell().screensaver_idle_ms()).to_equal(uint32_t(180000));

		FlashStorage::screensaverTimeoutMinutes = 30;
		expect(shellHostUI.shell().screensaver_idle_ms()).to_equal(uint32_t(180000));
		deluge::hid::display::Screensaver::settingsChanged();
		expect(shellHostUI.shell().screensaver_idle_ms()).to_equal(uint32_t(1800000));
	});
});

describe boot_services("the three services, installed into the guest's own Shell", $ {
	it("fills all three slots with three distinct real services, not the null one", _ {
		deluge::gui::ui::wireShellGuestServices();
		const deluge::ui::shell::Services& slots = shellHostUI.shell().services();

		expect(slots.mod_knobs).not_().to_be_null();
		expect(slots.global_controls).not_().to_be_null();
		expect(slots.midi_learn).not_().to_be_null();
		// `Services`' own defaults are all three pointing at the one shared `null_service()`, which
		// is what an application passes without `Shell::set_services`.
		// Naming it is what makes "installed" mean something.
		const deluge::ui::shell::Service* null_slot = &deluge::ui::shell::null_service();
		expect(slots.mod_knobs).not_().to_equal(null_slot);
		expect(slots.global_controls).not_().to_equal(null_slot);
		expect(slots.midi_learn).not_().to_equal(null_slot);
		expect(slots.mod_knobs).not_().to_equal(slots.global_controls);
		expect(slots.mod_knobs).not_().to_equal(slots.midi_learn);
		expect(slots.global_controls).not_().to_equal(slots.midi_learn);

		// The same three objects on a second call: the services are built once and re-installed,
		// so a `Shell::set_services` that leaked a new set per call would show up here.
		const deluge::ui::shell::Service* knobs = slots.mod_knobs;
		deluge::gui::ui::wireShellGuestServices();
		expect(shellHostUI.shell().services().mod_knobs).to_equal(knobs);
	});

	it("none of the three asks to be woken at rest, so the guest's 1 ms floor stays latent", _ {
		// A 1 kHz cost becomes live the moment a stacked scene or an
		// installed service reports a **zero** timer: the guest converts `Shell::next_wake()` into a
		// `UI_SPECIFIC` duration with a 1 ms floor, so a zero answer runs legacy's whole 29-timer
		// sweep and a full grid render at up to 1 kHz where `graphicsRoutine` runs at ~66 Hz. It is
		// latent because `MenuScene::next_timer()` is `std::nullopt`; installing services is the
		// obvious way to make it live, so it is asserted rather than assumed.
		deluge::gui::ui::wireShellGuestServices();
		const deluge::ui::shell::Services& slots = shellHostUI.shell().services();
		expect(slots.mod_knobs->next_timer().has_value()).to_be_false();
		expect(slots.global_controls->next_timer().has_value()).to_be_false();
		expect(slots.midi_learn->next_timer().has_value()).to_be_false();
		// And the shell itself is not pinned to now: nothing here arms a deadline at all.
		expect(shellHostUI.shell().next_wake().has_value()).to_be_false();
	});

	it("seeds ModKnobs from View's own active mod controllable, so its LEDs are not blank", _ {
		deluge::scenes::spec::SongFixture f = deluge::scenes::spec::make_synth_song();
		Song* const saved_song = currentSong;
		currentSong = f.song;

		// Legacy's root views set this on `focusRegained`
		// (`InstrumentClipMinder::focusRegained`); the port has no way to read it, and an
		// unseeded `ModKnobs` reports `mode() == -1`, which makes its `leds()` declare all eight mod
		// LEDs off and its indicators declare level 0 on both rings -- every tick the guest runs.
		view.setActiveModControllableTimelineCounter(f.clip);
		deluge::gui::ui::wireShellGuestServices();

		auto* knobs = static_cast<services::ModKnobs*>(shellHostUI.shell().services().mod_knobs);
		// The focus is the synth on its clip: the owner legacy's stack resolved, as addresses.
		const auto* sound = std::get_if<deluge::scenes::bind::SoundFocus>(&knobs->focus().owner);
		expect(sound).not_().to_be_null();
		if (sound != nullptr) {
			expect(sound->slot.id).to_equal(*f.instrument->id());
			expect(sound->clip.has_value()).to_be_true();
			if (sound->clip.has_value()) {
				expect(sound->clip->clip_id).to_equal(f.clip->id());
			}
		}
		// Which is what makes the LEDs legacy's: -1 is the "no focus" answer that turns every mod LED off.
		to_be_at_least(expect(knobs->mode()), 0);

		// The other two take the song and nothing else this layer can check from outside.
		expect(static_cast<services::GlobalControls*>(shellHostUI.shell().services().global_controls)->song())
		    .to_equal(f.song);

		// Vary it: with no timeline counter, legacy's view points at nothing, and so does the focus.
		view.setActiveModControllableTimelineCounter(nullptr);
		deluge::gui::ui::wireShellGuestServices();
		expect(std::holds_alternative<std::monostate>(knobs->focus().owner)).to_be_true();

		view.setActiveModControllableTimelineCounter(nullptr);
		currentSong = saved_song;
		deluge::gui::ui::wireShellGuestServices();
	});
	it("hands a showing knob popup to legacy's display when the guest closes, with its remaining lifetime", _ {
		NotificationDisplay stub;
		deluge::hid::Display* const saved = display;
		display = &stub;
		auto& popup = deluge::scenes::overlay::param_popup_scene();
		popup.tick(100'000);
		uiTimerManager.unsetTimer(TimerName::DISPLAY);
		(void)popup.show(deluge::modulation::params::Kind::UNPATCHED_GLOBAL,
		                 deluge::modulation::params::UNPATCHED_VOLUME, 0, PatchSource::NONE, PatchSource::NONE,
		                 5'000'000, {});
		const std::string name{popup.shown_name()};
		const std::string value{popup.shown_value()};
		popup.tick(600);

		// Through the hook the wiring installs, as the guest's close runs it.
		deluge::gui::ui::wireShellGuestServices();
		const deluge::gui::ui::ShellHostUI::CloseHook hook = deluge::gui::ui::ShellHostUI::closeHook();
		expect(hook).not_().to_be_null();
		if (hook != nullptr) {
			hook(5'000'800); // 800 ms after the show: 1200 ms left
		}
		expect(stub.title).to_equal(name);
		expect(stub.value).to_equal(value);
		expect(uiTimerManager.isTimerSet(TimerName::DISPLAY)).to_be_true();
		expect(uiTimerManager.getTimer(TimerName::DISPLAY).triggerTime - AudioEngine::audioSampleTimer)
		    .to_equal(uint32_t(1200 * 44));
		expect(popup.remaining_ms()).to_equal(uint32_t(0));

		// Nothing showing: nothing handed.
		stub.title.clear();
		if (hook != nullptr) {
			hook(5'000'900);
		}
		expect(stub.title.empty()).to_be_true();
		uiTimerManager.unsetTimer(TimerName::DISPLAY);
		display = saved;
	});

	it("tells legacy's knob popup what the hand-off drew, so its next draw is not taken for a repeat", _ {
		NotificationDisplay stub;
		deluge::hid::Display* const saved = display;
		display = &stub;
		const uint32_t saved_timer = AudioEngine::audioSampleTimer;
		namespace params = deluge::modulation::params;

		AudioEngine::audioSampleTimer = 50'000'000;
		view.displayModEncoderValuePopup(params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_VOLUME, 10);
		const std::string legacy_value = stub.value;
		expect(stub.draws).to_equal(1);

		// The guest's popup moves the knob on, and its close hands that popup to legacy's display.
		auto& popup = deluge::scenes::overlay::param_popup_scene();
		popup.tick(100'000);
		(void)popup.show(params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_VOLUME, 30, PatchSource::NONE,
		                 PatchSource::NONE, 6'000'000, {});
		deluge::gui::ui::handOffGuestOverlays(6'000'000);
		expect(stub.value).not_().to_equal(legacy_value);

		// Legacy turns back to where it last drew: a change from what is showing, so it draws.
		AudioEngine::audioSampleTimer += 10 * 44'100;
		view.displayModEncoderValuePopup(params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_VOLUME, 10);
		expect(stub.value).to_equal(legacy_value);

		uiTimerManager.unsetTimer(TimerName::DISPLAY);
		AudioEngine::audioSampleTimer = saved_timer;
		display = saved;
	});

	it("tells the guest's knobs whether the UI mode lets a mod button act, as View answers it", _ {
		deluge::scenes::spec::SongFixture f = deluge::scenes::spec::make_synth_song();
		Song* const saved_song = currentSong;
		currentSong = f.song;
		auto* knobs = static_cast<services::ModKnobs*>(shellHostUI.shell().services().mod_knobs);
		const uint32_t saved_mode = currentUIMode;

		currentUIMode = UI_MODE_HORIZONTAL_SCROLL; // a mode outside View's list
		deluge::gui::ui::wireShellGuestServices();
		expect(view.modButtonUIModeInRange()).to_be_false();
		expect(knobs->view_state().mod_button_ui_mode_in_range).to_be_false();

		currentUIMode = UI_MODE_NONE;
		deluge::gui::ui::wireShellGuestServices();
		expect(knobs->view_state().mod_button_ui_mode_in_range).to_be_true();

		currentUIMode = saved_mode;
		currentSong = saved_song;
		deluge::gui::ui::wireShellGuestServices();
	});

	it("the guest's knobs are View's: a mod button pressed in the guest moves View's mode and VU flag", _ {
		deluge::scenes::spec::SongFixture f = deluge::scenes::spec::make_synth_song();
		Song* const saved_song = currentSong;
		currentSong = f.song;
		f.song->persistentView.affectEntire = true;
		view.setActiveModControllableTimelineCounter(f.song, false);
		view.knobFocus().vu_meter = false;
		deluge::gui::ui::wireShellGuestServices();
		auto* knobs = static_cast<services::ModKnobs*>(shellHostUI.shell().services().mod_knobs);
		expect(&knobs->focus()).to_equal(&view.knobFocus());

		knobs->mod_button_action(1, true);
		knobs->mod_button_action(1, false);
		expect(view.knobMode()).to_equal(1);
		knobs->mod_button_action(0, true);
		knobs->mod_button_action(0, false);
		knobs->mod_button_action(0, true);
		knobs->mod_button_action(0, false);
		expect(view.knobMode()).to_equal(0);
		expect(view.knobFocus().vu_meter).to_be_true();

		view.setModRegion(96, 48, 0);
		expect(knobs->focus().region).to_equal(deluge::controls::ModRegion{.pos = 96, .length = 48});

		view.setModRegion();
		view.knobFocus().vu_meter = false;
		view.releaseActiveModControllable();
		currentSong = saved_song;
		deluge::gui::ui::wireShellGuestServices();
	});
});

describe boot_browser_host("the browser's host, installed with the services", $ {
	it("gives the browser the application's host, not the one that performs nothing", _ {
		deluge::scenes::browser::browser_scene().set_host(nullptr);
		deluge::gui::ui::wireShellGuestServices();
		const deluge::scenes::browser::BrowserHost* host = deluge::scenes::browser::browser_scene().installed_host();
		expect(host).not_().to_be_null();
		expect(host).not_().to_equal(&deluge::scenes::browser::null_browser_host());
	});
});
// clang-format on

DELUGE_SPEC(boot_wiring_spec, boot_node_ids, boot_shortcuts_version, boot_screensaver, boot_services, boot_browser_host)
