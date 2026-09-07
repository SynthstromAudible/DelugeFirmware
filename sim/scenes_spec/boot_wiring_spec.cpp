/// @file
/// @brief The boot wiring `src/scenes/registry.h`'s note asked for, driven against the real
///        application globals.
///
/// Three settings the ported tree and the guest's `Shell` cannot read for themselves, plus the
/// three services that could not be installed at all until F1 task 2. This is the spec level they
/// can be checked at: the host suite (`tests/spec/gui/shell_host_ui_spec.cpp`) deliberately never
/// builds the `deluge_scenes` closure, so it can see that `ShellHostUI::opened()` *calls* the wiring
/// but not what the wiring installs; this executable links the real `deluge_scenes` **and** the real
/// `deluge_app`, so `soundEditor`, `FlashStorage`, `view` and `shellHostUI` are all the shipped
/// objects.
///
/// @note **Every group restores what it changed.** These are process globals shared with every other
///       spec in this binary -- `shortcuts_spec.cpp`'s own `LegacyVersion1` guard exists for the
///       same reason -- so each example saves and puts back the setting it drives.

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

#include "gui/ui/menus.h"
#include "gui/ui/shell_guest_services.h"
#include "gui/ui/shell_host_ui.h"
#include "gui/ui/sound_editor.h"
#include "gui/views/view.h"

#include "hid/display/screensaver.h"

#include "model/model_stack.h"
#include "model/song/song.h"
#include "storage/flash_storage.h"

#include "support/song_fixture.h"

#include "cppspec.hpp"

#include <cstdint>

#include "spec/support/spec_main.h"

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
		expect(assigned > 0).to_be_true();

		// Idempotent: a second call reports the same number rather than re-numbering.
		expect(deluge::scenes::assigned_node_ids()).to_equal(assigned);

		// And the ids are on the nodes, not merely counted. Two roots from different tables, so a
		// walk that numbered only the first would fail.
		expect(menu::node_id(deluge::scenes::menu::nodes::sound_root) != 0).to_be_true();
		expect(menu::node_id(deluge::scenes::menu::nodes::root_settings) != 0).to_be_true();
		expect(menu::node_id(deluge::scenes::menu::nodes::sound_root)
		       != menu::node_id(deluge::scenes::menu::nodes::root_settings))
		    .to_be_true();
	});
});

describe boot_shortcuts_version("the shortcut layout follows flash byte 60", $ {
	it("SoundEditor::setShortcutsVersion carries the choice into the ported tree, on every path", _ {
		const uint8_t saved_legacy = soundEditor.shortcutsVersion;
		const ShortcutsVersion saved_port = menu::shortcuts_version();
		// `setShortcutsVersion`'s V1 arm rewrites six cells of this array in place and its V3 arm is
		// a comment reading "Uses defaults" (`gui/ui/sound_editor.cpp`), so the round trip does NOT
		// restore them -- the same one-way write `menu::set_shortcuts_version`'s own note records.
		// Saved and put back by hand, exactly as `shortcuts_spec.cpp`'s `LegacyVersion1` guard does.
		::MenuItem* const saved_cells[2] = {paramShortcutsForSounds[0][6], paramShortcutsForSounds[1][6]};

		// **This function is the whole of the wiring, which is why the example drives it and not a
		// helper.** It is the sole writer of `shortcutsVersion`, and all four paths that change the
		// setting go through it: `FlashStorage::readSettings` at boot
		// (`storage/flash_storage.cpp:568`, from byte 60), `FlashStorage::resetSettings` (`:311`,
		// reached from the legacy reset menu AND from the ported one --
		// `scenes/context_menu/context_menu_scene.cpp`'s `accept_reset_flash` / `accept_reset_all`),
		// and `shortcuts::Version::writeCurrentValue` (`gui/menu_item/shortcuts/version.h:27`).
		// Each of those four is one call of exactly the shape below; the fix round put the mirror
		// here for that reason, after a version at three call sites missed the two ported reset
		// arms. Not driven from `resetSettings` itself: it rewrites every setting this process
		// shares with the other twenty specs in this binary.
		//
		// Both values byte 60 can hold, in both directions -- a mirror that only ever wrote V1
		// would pass a one-way check.
		soundEditor.setShortcutsVersion(SHORTCUTS_VERSION_1);
		expect(menu::shortcuts_version() == ShortcutsVersion::V1).to_be_true();
		soundEditor.setShortcutsVersion(SHORTCUTS_VERSION_3); // `resetSettings`' own line, verbatim
		expect(menu::shortcuts_version() == ShortcutsVersion::V3).to_be_true();
		soundEditor.setShortcutsVersion(SHORTCUTS_VERSION_1);
		expect(menu::shortcuts_version() == ShortcutsVersion::V1).to_be_true();

		// The layout the port actually resolves moves with it -- the setting is not just recorded.
		// `(0, 6)` of the sound table is interpolation under V3 and the sample end point under V1,
		// which `shortcuts_spec.cpp` establishes is one of the only two cells that genuinely move.
		expect(menu::shortcut_node(menu::MenuContext::Root::Sound, false, 0, 6)
		       == &deluge::scenes::menu::nodes::sample0_end)
		    .to_be_true();
		soundEditor.setShortcutsVersion(SHORTCUTS_VERSION_3);
		expect(menu::shortcut_node(menu::MenuContext::Root::Sound, false, 0, 6)
		       == &deluge::scenes::menu::nodes::sample0_interpolation)
		    .to_be_true();

		// And a value that is neither enumerator takes the V3 arm, as legacy's own `default` does.
		soundEditor.setShortcutsVersion(7);
		expect(menu::shortcuts_version() == ShortcutsVersion::V3).to_be_true();

		paramShortcutsForSounds[0][6] = saved_cells[0];
		paramShortcutsForSounds[1][6] = saved_cells[1];
		soundEditor.shortcutsVersion = saved_legacy;
		menu::set_shortcuts_version(saved_port);
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

		expect(slots.mod_knobs != nullptr).to_be_true();
		expect(slots.global_controls != nullptr).to_be_true();
		expect(slots.midi_learn != nullptr).to_be_true();
		// `Services`' own defaults are all three pointing at the one shared `null_service()`, which
		// is what an application could only ever pass before `Shell::set_services` existed
		// (migration §10.2 item 7). Naming it is what makes "installed" mean something.
		const deluge::ui::shell::Service* null_slot = &deluge::ui::shell::null_service();
		expect(slots.mod_knobs != null_slot).to_be_true();
		expect(slots.global_controls != null_slot).to_be_true();
		expect(slots.midi_learn != null_slot).to_be_true();
		expect(slots.mod_knobs != slots.global_controls).to_be_true();
		expect(slots.mod_knobs != slots.midi_learn).to_be_true();
		expect(slots.global_controls != slots.midi_learn).to_be_true();

		// The same three objects on a second call: the services are built once and re-installed,
		// so a `Shell::set_services` that leaked a new set per call would show up here.
		const deluge::ui::shell::Service* knobs = slots.mod_knobs;
		deluge::gui::ui::wireShellGuestServices();
		expect(shellHostUI.shell().services().mod_knobs == knobs).to_be_true();
	});

	it("none of the three asks to be woken at rest, so the guest's 1 ms floor stays latent", _ {
		// F1 task 7 recorded a 1 kHz cost that becomes live the moment a stacked scene or an
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
		// (`model/clip/instrument_clip_minder.cpp:286-289`); the port has no way to read it, and an
		// unseeded `ModKnobs` reports `mode() == -1`, which makes its `leds()` declare all eight mod
		// LEDs off and its indicators declare level 0 on both rings -- every tick the guest runs.
		view.setActiveModControllableTimelineCounter(f.clip);
		deluge::gui::ui::wireShellGuestServices();

		auto* knobs = static_cast<services::ModKnobs*>(shellHostUI.shell().services().mod_knobs);
		expect(knobs->target().song == f.song).to_be_true();
		expect(knobs->target().timeline_counter == static_cast<TimelineCounter*>(f.clip)).to_be_true();
		// The pointers legacy resolved, compared against legacy's own live stack rather than
		// against anything this spec computed -- the port is re-running
		// `setActiveModControllableTimelineCounter`'s resolution, so the two must agree.
		expect(knobs->target().mod_controllable == view.activeModControllableModelStack.modControllable).to_be_true();
		expect(knobs->target().param_manager == view.activeModControllableModelStack.paramManager).to_be_true();
		expect(knobs->target().mod_controllable != nullptr).to_be_true();
		// Which is what makes the LEDs legacy's: `mode()` is `ModControllable::getModKnobMode`, and
		// -1 is the "no target" answer that turns every mod LED off.
		expect(knobs->mode() >= 0).to_be_true();

		// The other two take the song and nothing else this layer can check from outside.
		expect(static_cast<services::GlobalControls*>(shellHostUI.shell().services().global_controls)->song()
		       == f.song)
		    .to_be_true();

		// Vary the thing the assertion is about: with no timeline counter at all, the port must land
		// in the *other* of legacy's two entry-point shapes rather than keep the old target.
		view.setActiveModControllableTimelineCounter(nullptr);
		deluge::gui::ui::wireShellGuestServices();
		expect(knobs->target().timeline_counter == nullptr).to_be_true();

		view.setActiveModControllableTimelineCounter(nullptr);
		currentSong = saved_song;
		deluge::gui::ui::wireShellGuestServices();
	});
});
// clang-format on

/// Four `describe` groups in one TU, so `DELUGE_SPEC` is spelled out here with `add_specs`.
int boot_wiring_spec(int argc, char** const argv) {
	return CppSpec::parse(argc, argv)
	               .add_specs(boot_node_ids, boot_shortcuts_version, boot_screensaver, boot_services)
	               .exec()
	               .is_success()
	           ? EXIT_SUCCESS
	           : EXIT_FAILURE;
}
