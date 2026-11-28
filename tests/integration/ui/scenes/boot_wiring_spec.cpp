/// @file
/// @brief The boot wiring `src/ui/scenes/registry.h`'s note asked for, driven against the real
///        application globals.
///
/// Three settings the scene library and the guest's `Shell` cannot read for themselves, plus the
/// three services the wiring installs. This is the spec level they
/// can be checked at: the host suite (`tests/unit/deluge/gui/shell_host_ui_lifecycle_spec.cpp`) deliberately never
/// builds the `deluge_ui_scenes` closure, so it can see that `ShellHostUI::opened()` *calls* the wiring
/// but not what the wiring installs; this executable links the real `deluge_ui_scenes` **and** the real
/// `deluge_app`, so `FlashStorage`, `view` and `shellHostUI` are all the shipped
/// objects.
///
/// @note **Every group restores what it changed.** These are process globals shared with every other
///       spec in this binary -- `shortcuts_spec.cpp`'s own `LegacyVersion1` guard exists for the
///       same reason -- so each example saves and puts back the setting it drives.

#include "ui/scenes/browser/browser_scene.h"
#include "ui/scenes/menu/node_id.h"
#include "ui/scenes/menu/nodes/nodes.h"
#include "ui/scenes/menu/shortcuts.h"
#include "ui/scenes/registry.h"
#include "ui/scenes/services/global_controls.h"
#include "ui/scenes/services/midi_learn.h"
#include "ui/scenes/services/mod_knobs.h"

#include "ui/shell/router.h"
#include "ui/shell/service.h"
#include "ui/shell/shell.h"

#include "gui/ui/shell_guest_services.h"
#include "gui/ui/shell_host_ui.h"
#include "gui/ui/shell_runtime.h"
#include "gui/views/view.h"

#include "gui/ui_timer_manager.h"
#include "hid/display/display.h"
#include "hid/display/screensaver.h"
#include "ui/scenes/audio_clip/audio_clip_scene.h"
#include "ui/scenes/overlay/count_in_scene.h"
#include "ui/scenes/overlay/param_popup_scene.h"
#include "ui/scenes/overlay/screensaver_scene.h"
#include "ui/scenes/slicing/slicer_scene.h"

#include "memory/heaps.h"
#include "model/model_stack.h"
#include "model/sample/sample.h"
#include "model/song/song.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "storage/flash_storage.h"
#include "ui/views/waveform/view.h"

#include "support/popup_capture.h"
#include "support/printers/knob_focus_printer.h"
#include "support/printers/project_address_printer.h"
#include "support/shortcut_layout.h"
#include "support/slicer_host_open.h"
#include "support/song_fixture.h"

#include "support/plugin_param.h"

#include "cppspec.hpp"
#include "support/matchers/value_matchers.h"

#include <cstdint>
#include <optional>
#include <string>

#include "support/spec_main.h"

using deluge::project::ShortcutsVersion;
namespace menu = deluge::ui::scenes::menu;
namespace spec = deluge::ui::scenes::spec;
namespace services = deluge::ui::scenes::services;

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
		// `restore_state` and the shortcut-pad jump (`ui/scenes/registry.h`). `deluge_boot` calls this
		// explicitly; in this binary `shellHostUI`'s own construction has already called it through
		// `scene_registry()`, which is exactly why the boot call is spelled out rather than left to
		// static-initialisation order.
		const uint16_t assigned = deluge::ui::scenes::assigned_node_ids();
		expect(assigned).to_be_greater_than(0);

		// Idempotent: a second call reports the same number rather than re-numbering.
		expect(deluge::ui::scenes::assigned_node_ids()).to_equal(assigned);

		// And the ids are on the nodes, not merely counted. Two roots from different tables, so a
		// walk that numbered only the first would fail.
		expect(menu::node_id(deluge::ui::scenes::menu::nodes::sound_root)).not_().to_equal(0);
		expect(menu::node_id(deluge::ui::scenes::menu::nodes::root_settings)).not_().to_equal(0);
		expect(menu::node_id(deluge::ui::scenes::menu::nodes::sound_root))
		    .not_().to_equal(menu::node_id(deluge::ui::scenes::menu::nodes::root_settings));
	});
});

describe boot_shortcuts_version("the shortcut layout follows flash byte 60", $ {
	it("the scenes' shortcut tables select on FlashStorage::shortcutsVersion, which every path writes", _ {
		const uint8_t saved = FlashStorage::shortcutsVersion;

		// Boot (`FlashStorage::readSettings`, from byte 60), the factory resets (`FlashStorage::resetSettings`, reached
		// from the reset menu) and the settings item
		// (its `pads/shortcutsVersion` row) each write this one byte, so the example drives the byte and the row
		// rather than `resetSettings`, which rewrites every setting this process shares with the other specs in this
		// binary.
		//
		// Both values byte 60 can hold, in both directions -- a read that only ever followed one would pass a one-way
		// check.
		FlashStorage::shortcutsVersion = FlashStorage::kShortcutsVersion1;
		expect(spec::shortcut_layout()).to_equal(ShortcutsVersion::V1);
		FlashStorage::shortcutsVersion = FlashStorage::kShortcutsVersion3;
		expect(spec::shortcut_layout()).to_equal(ShortcutsVersion::V3);
		spec::set_shortcut_layout(ShortcutsVersion::V1);
		expect(spec::shortcut_layout()).to_equal(ShortcutsVersion::V1);

		// The layout the port actually resolves moves with it -- the setting is not just recorded.
		// `(0, 6)` of the sound table is interpolation under V3 and the sample end point under V1,
		// which `shortcuts_spec.cpp` establishes is one of the only two cells that genuinely move.
		expect(menu::shortcut_node(menu::MenuContext::Root::Sound, spec::shortcut_layout(), false, 0, 6))
		    .to_equal(&deluge::ui::scenes::menu::nodes::sample0_end);
		spec::set_shortcut_layout(ShortcutsVersion::V3);
		expect(menu::shortcut_node(menu::MenuContext::Root::Sound, spec::shortcut_layout(), false, 0, 6))
		    .to_equal(&deluge::ui::scenes::menu::nodes::sample0_interpolation);

		// And a byte that is neither enumerator takes the V3 layout, as legacy's own `default` does.
		FlashStorage::shortcutsVersion = 7;
		expect(spec::shortcut_layout()).to_equal(ShortcutsVersion::V3);

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

	it("installs them on the one runtime shell, which the guest drives and every other host shares", _ {
		deluge::gui::ui::wireShellGuestServices();
		deluge::ui::shell::Shell& runtime_shell = deluge::gui::ui::shellRuntime().shell();
		expect(&shellHostUI.shell()).to_equal(&runtime_shell);
		expect(&shellHostUI.surfaces()).to_equal(&deluge::gui::ui::shellRuntime().surfaces());
		expect(runtime_shell.services().mod_knobs).not_().to_equal(&deluge::ui::shell::null_service());
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
		deluge::ui::scenes::spec::SongFixture f = deluge::ui::scenes::spec::make_synth_song();
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
		const auto* sound = std::get_if<deluge::ui::scenes::bind::SoundFocus>(&knobs->focus().owner);
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

		// The global controls take the app's ports and the song's record.
		const auto* const controls = static_cast<services::GlobalControls*>(shellHostUI.shell().services().global_controls);
		expect(controls->ports()).to_equal(&deluge::gui::shell_host::host_ports());
		expect(controls->model()).to_equal(static_cast<const deluge::project::Song*>(&f.song->project));

		// Vary it: with no timeline counter, legacy's view points at nothing, and so does the focus.
		view.setActiveModControllableTimelineCounter(nullptr);
		deluge::gui::ui::wireShellGuestServices();
		expect(std::holds_alternative<std::monostate>(knobs->focus().owner)).to_be_true();

		view.setActiveModControllableTimelineCounter(nullptr);
		currentSong = saved_song;
		deluge::gui::ui::wireShellGuestServices();
	});
	it("hands a showing knob popup to legacy's display when the guest closes, with its remaining lifetime", _ {
		deluge::ui::scenes::spec::PopupCapture popups;
		auto& popup = deluge::ui::scenes::overlay::param_popup_scene();
		popup.tick(100'000);
		uiTimerManager.unsetTimer(TimerName::DISPLAY);
		(void)popup.show(deluge::ui::scenes::spec::plugin_param(deluge::modulation::params::Kind::UNPATCHED_GLOBAL, deluge::modulation::params::UNPATCHED_VOLUME), 0, PatchSource::NONE, PatchSource::NONE,
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
		expect(popups.last(deluge::ui::scenes::spec::PopupEvent::Kind::NOTIFICATION)).to_equal(std::optional<std::string>{name + "\n" + value});
		expect(uiTimerManager.isTimerSet(TimerName::DISPLAY)).to_be_true();
		expect(uiTimerManager.getTimer(TimerName::DISPLAY).triggerTime - AudioEngine::audioSampleTimer)
		    .to_equal(uint32_t(1200 * 44));
		expect(popup.remaining_ms()).to_equal(uint32_t(0));

		// Nothing showing: nothing handed.
		popups.clear();
		if (hook != nullptr) {
			hook(5'000'900);
		}
		expect(popups.count(deluge::ui::scenes::spec::PopupEvent::Kind::NOTIFICATION)).to_equal(size_t{0});
		uiTimerManager.unsetTimer(TimerName::DISPLAY);
	});

	it("tells legacy's knob popup what the hand-off drew, so its next draw is not taken for a repeat", _ {
		deluge::ui::scenes::spec::PopupCapture popups;
		const uint32_t saved_timer = AudioEngine::audioSampleTimer;
		namespace params = deluge::modulation::params;

		AudioEngine::audioSampleTimer = 50'000'000;
		view.displayModEncoderValuePopup(params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_VOLUME, 10);
		const std::optional<std::string> legacy_banner = popups.last(deluge::ui::scenes::spec::PopupEvent::Kind::NOTIFICATION);
		expect(popups.count(deluge::ui::scenes::spec::PopupEvent::Kind::NOTIFICATION)).to_equal(size_t{1});

		// The guest's popup moves the knob on, and its close hands that popup to legacy's display.
		auto& popup = deluge::ui::scenes::overlay::param_popup_scene();
		popup.tick(100'000);
		(void)popup.show(deluge::ui::scenes::spec::plugin_param(params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_VOLUME), 30, PatchSource::NONE,
		                 PatchSource::NONE, 6'000'000, {});
		deluge::gui::ui::handOffGuestOverlays(6'000'000);
		expect(popups.last(deluge::ui::scenes::spec::PopupEvent::Kind::NOTIFICATION)).not_().to_equal(legacy_banner);

		// Legacy turns back to where it last drew: a change from what is showing, so it draws.
		AudioEngine::audioSampleTimer += 10 * 44'100;
		view.displayModEncoderValuePopup(params::Kind::UNPATCHED_GLOBAL, params::UNPATCHED_VOLUME, 10);
		expect(popups.last(deluge::ui::scenes::spec::PopupEvent::Kind::NOTIFICATION)).to_equal(legacy_banner);

		uiTimerManager.unsetTimer(TimerName::DISPLAY);
		AudioEngine::audioSampleTimer = saved_timer;
	});

	it("tells the guest's knobs whether the UI mode lets a mod button act, as View answers it", _ {
		deluge::ui::scenes::spec::SongFixture f = deluge::ui::scenes::spec::make_synth_song();
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
		deluge::ui::scenes::spec::SongFixture f = deluge::ui::scenes::spec::make_synth_song();
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
		expect(knobs->focus().region).to_equal(deluge::ui::controls::ModRegion{.pos = 96, .length = 48});

		view.setModRegion();
		view.knobFocus().vu_meter = false;
		view.releaseActiveModControllable();
		currentSong = saved_song;
		deluge::gui::ui::wireShellGuestServices();
	});
});

describe boot_overlays("the overlays', the slicer's and the audio clip scene's ports, installed with the services", $ {
	it("gives the count-in the app's transport: a count-in the playback handler holds shows as its digit", _ {
		auto& scene = deluge::ui::scenes::overlay::count_in_scene();
		const int32_t saved = playbackHandler.currentVisualCountForCountIn;
		scene.set_transport(nullptr);
		deluge::gui::ui::wireShellGuestServices();
		playbackHandler.currentVisualCountForCountIn = 3;
		scene.tick(0);
		expect(scene.showing()).to_be_true();
		expect(std::string{scene.shown_text()}).to_equal("3");
		// Back to idle: the count ends, and the scene files its removal.
		playbackHandler.currentVisualCountForCountIn = 0;
		scene.tick(0);
		(void)scene.after_render();
		expect(scene.showing()).to_be_false();
		playbackHandler.currentVisualCountForCountIn = saved;
	});

	it("gives the screensaver the app's device settings: it shows the mode the flash storage holds", _ {
		auto& scene = deluge::ui::scenes::overlay::screensaver_scene();
		const ScreensaverMode saved = FlashStorage::screensaverMode;
		scene.set_device(nullptr);
		deluge::gui::ui::wireShellGuestServices();
		// BLANK scatters no field, so the scene's animations stay as the other specs expect them; with no port it
		// would read the row's default, DELUGE.
		FlashStorage::screensaverMode = ScreensaverMode::BLANK;
		expect(scene.on_enter(deluge::ui::shell::SceneId{deluge::ui::scenes::overlay::kScreensaverSceneKind, 0}))
		    .to_be_true();
		expect(scene.mode()).to_equal(deluge::project::ScreensaverMode::BLANK);
		FlashStorage::screensaverMode = saved;
	});

	it("gives the slicer the app's ports: it opens over the length the app's waveforms answer at SLICE", _ {
		auto& scene = deluge::ui::scenes::slicing::slicer_scene();
		scene.set_ports(nullptr);
		deluge::gui::ui::wireShellGuestServices();
		auto* sample = new (deluge::memory::alloc_fast(sizeof(Sample))) Sample();
		sample->byteDepth = 2;
		sample->numChannels = 1;
		sample->sampleRate = 44100;
		sample->lengthInSamples = 16000;
		{
			const deluge::ui::scenes::spec::SliceHeld held{sample, "SAMPLES/BOOT.WAV"};
			held.open();
			// With no ports the scene would read no length, and open at a zoom of 1.
			expect(scene.x_zoom()).to_equal(deluge::ui::views::waveform::WaveformView::open(16000, std::nullopt).zoom());
		}
		// The host and the preview drum have let it go.
		sample->~Sample();
		deluge::memory::dealloc(sample);
	});

	it("gives the audio clip scene the app's ports: it opens on the clip navigation the current song holds", _ {
		auto& scene = deluge::ui::scenes::audio_clip::audio_clip_scene();
		scene.set_ports(nullptr);
		deluge::gui::ui::wireShellGuestServices();
		Song* const saved = currentSong;
		spec::AudioSongFixture f = spec::make_audio_clip_song(); // made current
		f.song->persistentView.xScroll[NAVIGATION_CLIP] = 96;
		f.song->persistentView.xZoom[NAVIGATION_CLIP] = 6;
		scene.timeline().x_scroll = 0;
		scene.timeline().x_zoom = 1;
		scene.open(deluge::project::ClipAddress{f.clip->id()}, &f.song->project);
		// With no ports the scene would read no navigation, and keep the view it had.
		expect(scene.timeline().x_scroll).to_equal(96);
		expect(scene.timeline().x_zoom).to_equal(uint32_t{6});
		scene.open(std::nullopt, nullptr);
		f.song->setCurrentClip(nullptr);
		spec::make_current(saved);
	});
});

describe boot_browser_host("the browser's host, installed with the services", $ {
	it("gives the browser the application's host, not the one that performs nothing", _ {
		deluge::ui::scenes::browser::BrowserHost* const saved = deluge::ui::scenes::browser::browser_scene().installed_host();
		deluge::ui::scenes::browser::browser_scene().set_host(nullptr);
		deluge::gui::ui::wireShellGuestServices();
		const deluge::ui::scenes::browser::BrowserHost* host = deluge::ui::scenes::browser::browser_scene().installed_host();
		expect(host).not_().to_be_null();
		expect(host).not_().to_equal(&deluge::ui::scenes::browser::null_browser_host());
		deluge::ui::scenes::browser::browser_scene().set_host(saved);
	});
});
// clang-format on

DELUGE_SPEC(boot_wiring_spec, boot_node_ids, boot_shortcuts_version, boot_screensaver, boot_services, boot_overlays,
            boot_browser_host)
