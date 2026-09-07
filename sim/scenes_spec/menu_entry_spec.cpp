/// @file
/// @brief The gate F1 task 9 puts in front of the fourteen `openUI(&soundEditor)` sites, driven
///        against the real application globals.
///
/// `gui/ui/shell_menu_entry.cpp` carries the fourteen-site table; this file drives the eight that
/// are gated, one example per *argument shape*, plus the toggle itself. It is at this level and not
/// in `tests/spec/gui/` for the reason `boot_wiring_spec.cpp` gives: the host suite deliberately
/// never builds the `deluge_scenes` closure, and everything under test here is the join between the
/// two layers -- `menu::context_for`'s root against `MenuScene::roots()`, a stable node id against
/// `SceneId::arg`, and `openUI(&shellHostUI)` against the real `uiNavigationHierarchy`.
///
/// @note **What this file cannot cover, stated rather than implied.** It drives `openShellMenu`,
///       which is what the eight gated sites call; it does **not** drive the eight sites, because
///       reaching `AudioClipView::buttonAction` here would need the whole legacy view stack. So
///       "the site passes the right arguments" is uncovered until task 11's level-3 A/B run, in
///       exactly the way task 8's `deluge.cpp` boot calls were. Each example names the site whose
///       argument shape it replays so the two can be compared by eye in the meantime.
///
/// @note **Every example puts the process back.** These are shared globals -- `numUIsOpen`,
///       `currentSong`, `runtimeFeatureSettings` -- and twenty other specs run in this binary.
///       `nullifyUIs()` is the teardown for the UI hierarchy: it is legacy's own "no UI is open"
///       and, unlike `closeUI`, it is well defined when the guest is the only thing on the stack.

#include "scenes/menu/context.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/menu/node_id.h"
#include "scenes/menu/nodes/nodes.h"
#include "scenes/registry.h"

#include "shell/scene.h"
#include "shell/shell.h"

#include "gui/ui/shell_host_ui.h"
#include "gui/ui/shell_menu_entry.h"
#include "gui/ui/ui.h"

#include "gui/l10n/l10n.h"
#include "gui/ui_timer_manager.h"
#include "gui/views/session_view.h"
#include "hid/display/screensaver.h"
#include "model/action/action_logger.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h"
#include "processing/engines/audio_engine.h"
#include "storage/flash_storage.h"

#include "support/song_fixture.h"

#include "cppspec.hpp"

#include <cstdint>

#include "spec/support/spec_main.h"

namespace menu = deluge::scenes::menu;
namespace nodes = deluge::scenes::menu::nodes;
namespace shell = deluge::ui::shell;
namespace entry = deluge::gui::ui;

namespace {

/// @brief Save and restore the `ShellMenu` toggle around an example.
struct ToggleGuard {
	RuntimeFeatureStateToggle saved =
	    static_cast<RuntimeFeatureStateToggle>(runtimeFeatureSettings.get(RuntimeFeatureSettingType::ShellMenu));

	explicit ToggleGuard(RuntimeFeatureStateToggle on) {
		runtimeFeatureSettings.set(RuntimeFeatureSettingType::ShellMenu, on);
	}
	~ToggleGuard() { runtimeFeatureSettings.set(RuntimeFeatureSettingType::ShellMenu, saved); }
};

/// @brief Put the UI hierarchy back to "nothing open" after an example opened the guest.
///
/// `closeUI` would index `uiNavigationHierarchy[-1]` when the guest is the only entry
/// (`gui/ui/ui.cpp:183-194`, whose loop stops at `u >= 1`), so this is `nullifyUIs()`, which is what
/// legacy itself calls when it tears the hierarchy down (`gui/ui/ui.cpp:257-261`).
void closeGuest() {
	nullifyUIs();
}

/// @brief The `SceneId` the guest was told to open on.
shell::SceneId opened_on() {
	return shellHostUI.getInitialScene();
}

} // namespace

// clang-format off
describe menu_entry_toggle("the ShellMenu toggle", $ {
	it("is Off by default, and shellMenuEnabled() follows it in both directions", _ {
		// Read from the settings object rather than asserted on the enum: F1 task 3's own rule, and
		// the reason is that a default-On toggle would ship the guest to every user.
		expect(runtimeFeatureSettings.get(RuntimeFeatureSettingType::ShellMenu)
		       == RuntimeFeatureStateToggle::Off)
		    .to_be_true();
		expect(entry::shellMenuEnabled()).to_be_false();

		{
			ToggleGuard on{RuntimeFeatureStateToggle::On};
			expect(entry::shellMenuEnabled()).to_be_true();
		}
		// And the guard put it back, which is what the other examples depend on.
		expect(entry::shellMenuEnabled()).to_be_false();
	});
});

describe menu_entry_roots("openShellMenu resolves the root each site's arguments name", $ {
	it("site 1 (View::buttonAction, SHIFT + SELECT) opens the settings root", _ {
		ToggleGuard on{RuntimeFeatureStateToggle::On};
		deluge::scenes::spec::SongFixture f = deluge::scenes::spec::make_synth_song();
		(void)f;

		// All four UI facts false -- SHIFT is held at that site, so `setup()`'s two view tests are
		// false and the root is `settingsRootMenu` (`gui/ui/sound_editor.cpp:1799-1808`).
		expect(entry::openShellMenu(nullptr, 0, false, false, false, false)).to_be_true();
		expect(getCurrentUI() == &shellHostUI).to_be_true();
		expect(deluge::scenes::menu_scene().focused() == &nodes::root_settings).to_be_true();
		// `arg == 0` is "the root the context chose" (`scenes/menu/menu_scene.cpp:310-319`).
		expect(opened_on() == shell::SceneId{.kind = shell::SceneKind::FirstApp, .arg = 0}).to_be_true();
		closeGuest();
	});

	it("sites 2 and 3 (session and arranger) open the SAME song root, differing only in is_session_view", _ {
		ToggleGuard on{RuntimeFeatureStateToggle::On};
		deluge::scenes::spec::SongFixture f = deluge::scenes::spec::make_synth_song();
		(void)f;

		// The pair is one example on purpose: `sessionViewRoot` is the ONLY argument that differs
		// between the two sites, and `MenuContext::is_session_view` is the only thing it feeds. An
		// example that drove one site alone would pass with the field hard-coded either way.
		expect(entry::openShellMenu(nullptr, 0, false, /*sessionOrArrangerRoot=*/true,
		                            /*sessionViewRoot=*/true, false))
		    .to_be_true();
		expect(deluge::scenes::menu_scene().focused() == &nodes::root_song).to_be_true();
		expect(deluge::scenes::menu_scene().menu_context().is_session_view).to_be_true();
		closeGuest();

		expect(entry::openShellMenu(nullptr, 0, false, /*sessionOrArrangerRoot=*/true,
		                            /*sessionViewRoot=*/false, false))
		    .to_be_true();
		expect(deluge::scenes::menu_scene().focused() == &nodes::root_song).to_be_true();
		expect(deluge::scenes::menu_scene().menu_context().is_session_view).to_be_false();
		closeGuest();
	});

	it("site 4 (PerformanceView) opens the performance root", _ {
		ToggleGuard on{RuntimeFeatureStateToggle::On};
		deluge::scenes::spec::SongFixture f = deluge::scenes::spec::make_synth_song();
		(void)f;

		// Legacy names `&soundEditorRootMenuPerformanceView` by pointer; the port names the fact.
		// The example varies only `performanceView` against the settings-root call above, so a
		// mapping that ignored the argument would fail here rather than pass by coincidence.
		expect(entry::openShellMenu(nullptr, 0, /*performanceView=*/true, false, false, false)).to_be_true();
		expect(deluge::scenes::menu_scene().focused() == &nodes::root_performance).to_be_true();
		closeGuest();
	});

	it("site 5 (AudioClipView) opens the audio-clip root", _ {
		ToggleGuard on{RuntimeFeatureStateToggle::On};
		deluge::scenes::spec::AudioSongFixture f = deluge::scenes::spec::make_audio_clip_song();

		expect(entry::openShellMenu(f.clip, 0, false, false, false, false)).to_be_true();
		expect(deluge::scenes::menu_scene().focused() == &nodes::root_audio_clip).to_be_true();
		closeGuest();
	});

	it("site 6 (InstrumentClipMinder) picks the root from the clip, not from the site", _ {
		ToggleGuard on{RuntimeFeatureStateToggle::On};

		// **The clip is what varies.** Same call shape, three different songs -- so "always the
		// sound root" and "always whatever the last call resolved" both fail.
		deluge::scenes::spec::SongFixture synth = deluge::scenes::spec::make_synth_song();
		expect(entry::openShellMenu(synth.clip, 0, false, false, false, false)).to_be_true();
		expect(deluge::scenes::menu_scene().focused() == &nodes::sound_root).to_be_true();
		closeGuest();

		deluge::scenes::spec::SongFixture midi = deluge::scenes::spec::make_midi_song();
		expect(entry::openShellMenu(midi.clip, 0, false, false, false, false)).to_be_true();
		expect(deluge::scenes::menu_scene().focused() == &nodes::root_midi_or_cv).to_be_true();
		closeGuest();

		// And the kit-global-FX argument, which is the one fact the site computes for itself
		// (`KIT && affectEntire`). Same clip, both values, so the argument is what is under test.
		deluge::scenes::spec::SongFixture kit = deluge::scenes::spec::make_kit_song();
		expect(entry::openShellMenu(kit.clip, 0, false, false, false, /*kitGlobalFx=*/true)).to_be_true();
		expect(deluge::scenes::menu_scene().focused() == &nodes::root_kit_global_fx).to_be_true();
		closeGuest();
	});

	it("site 7 (AutomationView on the arranger) opens the song root from a null clip", _ {
		ToggleGuard on{RuntimeFeatureStateToggle::On};
		deluge::scenes::spec::SongFixture f = deluge::scenes::spec::make_synth_song();
		(void)f;

		// `onArrangerView` is both halves of that site's mapping: it is why the clip is null and it
		// is `!rootUIIsClipMinderScreen()`. `sessionViewRoot` stays false -- the automation view is
		// not the session view -- which is what separates this from site 2.
		expect(entry::openShellMenu(nullptr, 0, false, /*sessionOrArrangerRoot=*/true, false, false)).to_be_true();
		expect(deluge::scenes::menu_scene().focused() == &nodes::root_song).to_be_true();
		expect(deluge::scenes::menu_scene().menu_context().is_session_view).to_be_false();
		closeGuest();
	});
});

describe menu_entry_node_arg("openShellMenu can name a node, not only a root", $ {
	it("site 8 opens on nodes::file0_selector through a non-zero SceneId::arg", _ {
		ToggleGuard on{RuntimeFeatureStateToggle::On};
		deluge::scenes::spec::SongFixture f = deluge::scenes::spec::make_synth_song();
		(void)f;

		// Legacy's undo-log wipe is in `setup()`'s `item == nullptr` arm only, so a call that NAMES a
		// node must not wipe. Seeded with the guest open, because `getNewAction` refuses otherwise.
		expect(entry::openShellMenu(nullptr, 0, false, false, false, false)).to_be_true();
		actionLogger.deleteAllLogs();
		(void)actionLogger.getNewAction(ActionType::MISC, ActionAddition::NOT_ALLOWED);
		expect(actionLogger.firstAction[BEFORE] != nullptr).to_be_true();
		closeGuest();

		const uint16_t id = menu::node_id(nodes::file0_selector);
		// The id is what makes the mapping possible at all, and it is `scenes::assigned_node_ids()`
		// (F1 task 8) that produces it. Asserted non-zero here because zero means "the context's
		// root" downstream, which would silently open the sound root instead.
		expect(id != 0).to_be_true();

		expect(entry::openShellMenu(f.clip, 0, false, false, false, false,
		                            entry::ShellMenuNode::Osc0FileSelector))
		    .to_be_true();
		expect(opened_on() == shell::SceneId{.kind = shell::SceneKind::FirstApp, .arg = id}).to_be_true();
		// And it landed there rather than merely being asked to: `MenuScene::on_enter` resolves the
		// arg through `node_by_id` and re-roots the path at it (`menu_scene.cpp:305-325`).
		expect(deluge::scenes::menu_scene().focused() == &nodes::file0_selector).to_be_true();
		// ...and the log survived, which is the half a `clip != nullptr` test alone would get wrong.
		expect(actionLogger.firstAction[BEFORE] != nullptr).to_be_true();
		closeGuest();
	});

	it("the same arguments without the node open the sound root instead", _ {
		// The control for the example above: everything but `node` held fixed, so "the file
		// selector is where a synth clip always lands" fails.
		ToggleGuard on{RuntimeFeatureStateToggle::On};
		deluge::scenes::spec::SongFixture f = deluge::scenes::spec::make_synth_song();

		expect(entry::openShellMenu(f.clip, 0, false, false, false, false)).to_be_true();
		expect(opened_on() == shell::SceneId{.kind = shell::SceneKind::FirstApp, .arg = 0}).to_be_true();
		expect(deluge::scenes::menu_scene().focused() == &nodes::sound_root).to_be_true();
		closeGuest();
	});
});

describe menu_entry_refusal("openShellMenu refuses where SoundEditor::setup refuses", $ {
	it("a kit with neither affect-entire nor a selected drum opens nothing", _ {
		ToggleGuard on{RuntimeFeatureStateToggle::On};
		deluge::scenes::spec::SongFixture kit = deluge::scenes::spec::make_kit_song();

		// `MenuContext::resolved` is `setup()`'s own `return false` at
		// `gui/ui/sound_editor.cpp:1716-1722`, and the five gated sites that test a return value
		// are written against exactly that. **Nothing must be left half-open**: the assertion is
		// that the hierarchy did not move, not merely that the call said false.
		expect(entry::openShellMenu(kit.clip, 0, false, false, false, /*kitGlobalFx=*/false)).to_be_false();
		// `isUIOpen`, deliberately, and NOT `getCurrentUI()`: with an empty hierarchy `getCurrentUI()`
		// answers `lastUIBeforeNullifying` (`gui/ui/ui.cpp:136-141`), which the previous example's
		// teardown left pointing at the guest. Task 8 tripped on the same accessor from the other
		// side (its `currentUIIsClipMinderScreen()` null guard), which is why it is named here.
		expect(isUIOpen(&shellHostUI)).to_be_false();

		// And the same clip with the one argument flipped DOES open, so the refusal is about the
		// context this kit resolves to and not about kit clips, or about the fixture, or about the
		// toggle being read twice.
		expect(entry::openShellMenu(kit.clip, 0, false, false, false, /*kitGlobalFx=*/true)).to_be_true();
		expect(isUIOpen(&shellHostUI)).to_be_true();
		closeGuest();
	});
});

describe menu_entry_undo_log("the undo-log wipe SoundEditor::setup does on every menu open", $ {
	it("is done for a clip-rooted open and NOT for a settings-rooted one", _ {
		ToggleGuard on{RuntimeFeatureStateToggle::On};
		deluge::scenes::spec::SongFixture f = deluge::scenes::spec::make_synth_song();

		// `sound_editor.cpp:1754` is inside `if (item) ... else if (clip)`, so the wipe follows the
		// clip and not the open. `scenes/menu/context.cpp:44` records it as owed to whoever landed
		// the port's entry point; this is that entry point.
		// The seed has to happen with the guest open, and that is not a detail:
		// `ActionLogger::getNewAction` refuses unless `getCurrentUI() == getRootUI()`
		// (`model/action/action_logger.cpp:98-102`), which in a spec process is only true while
		// something is actually on the hierarchy. So open first, seed, then close.
		expect(entry::openShellMenu(nullptr, 0, false, false, false, false)).to_be_true();
		actionLogger.deleteAllLogs();
		(void)actionLogger.getNewAction(ActionType::MISC, ActionAddition::NOT_ALLOWED);
		expect(actionLogger.firstAction[BEFORE] != nullptr).to_be_true();
		closeGuest();

		// Settings root, no clip: legacy's wipe is inside `if (item) ... else if (clip)`, so it does
		// not happen, and neither does this one.
		expect(entry::openShellMenu(nullptr, 0, false, false, false, false)).to_be_true();
		expect(actionLogger.firstAction[BEFORE] != nullptr).to_be_true();
		closeGuest();

		// Same call, same process, one argument different -- the clip -- and the log goes.
		expect(entry::openShellMenu(f.clip, 0, false, false, false, false)).to_be_true();
		expect(actionLogger.firstAction[BEFORE] == nullptr).to_be_true();
		closeGuest();
	});
});

describe menu_entry_screensaver("legacy's screensaver stands down while the guest is current", $ {
	it("re-arms at the full idle period instead of animating, and wakes if it was showing", _ {
		// The collision F1 task 8 recorded at `hid/display/screensaver.cpp` and left for this task:
		// with the guest open, `shell::Screensaver` pushes `overlay::ScreensaverScene` through the
		// guest's surfaces while this class draws its own canvas over the same panel. The fix is an
		// early return in `Screensaver::timerEvent()` while the guest is the current UI.
		//
		// **What is asserted is the ARM, because that is what distinguishes the two paths.** An
		// animated mode that ran re-arms at `kFrameIntervalMS` (50 ms); the stand-down re-arms at
		// `arm()`'s full `screensaverTimeoutMinutes * 60000`. A silent `return` would arm neither,
		// which is the third outcome and the one the note warned against, so the timer is cleared
		// before each call and the arm is read back rather than assumed.
		ToggleGuard on{RuntimeFeatureStateToggle::On};
		deluge::scenes::spec::SongFixture f = deluge::scenes::spec::make_synth_song();
		(void)f;

		const ScreensaverMode saved_mode = FlashStorage::screensaverMode;
		const uint8_t saved_minutes = FlashStorage::screensaverTimeoutMinutes;
		FlashStorage::screensaverMode = ScreensaverMode::STARSCAPE;
		FlashStorage::screensaverTimeoutMinutes = 2;

		auto armed_ms = []() -> int32_t {
			const Timer& t = uiTimerManager.getTimer(TimerName::SCREENSAVER);
			return t.active ? static_cast<int32_t>(t.triggerTime - AudioEngine::audioSampleTimer) / 44 : -1;
		};

		// ---- the control: a legacy root view is current, so this class owns the panel ----
		setRootUILowLevel(&sessionView);
		uiTimerManager.unsetTimer(TimerName::SCREENSAVER);
		(void)deluge::hid::display::Screensaver::consumeFrameDirty();
		deluge::hid::display::Screensaver::timerEvent();
		// It activated, drew a frame, and is animating: `kFrameIntervalMS`, not the idle period.
		expect(deluge::hid::display::Screensaver::isActive()).to_be_true();
		expect(deluge::hid::display::Screensaver::consumeFrameDirty()).to_be_true();
		expect(armed_ms()).to_equal(int32_t(50));

		// ---- the guest on top: same settings, same call, same already-showing screensaver ----
		expect(entry::openShellMenu(nullptr, 0, false, false, false, false)).to_be_true();
		expect(getCurrentUI() == &shellHostUI).to_be_true();
		uiTimerManager.unsetTimer(TimerName::SCREENSAVER);
		deluge::hid::display::Screensaver::timerEvent();
		// Re-armed at the full idle period (2 minutes), so it is ready the instant the guest closes,
		// and NOT disarmed.
		expect(armed_ms()).to_equal(int32_t(120000));
		// And it stood down rather than merely skipping a frame: it was SHOWING from the control
		// above, so the guest branch had to take the panel back.
		expect(deluge::hid::display::Screensaver::isActive()).to_be_false();
		expect(deluge::hid::display::Screensaver::consumeFrameDirty()).to_be_false();

		// A second tick under the guest is idempotent -- nothing to wake this time, still armed full.
		uiTimerManager.unsetTimer(TimerName::SCREENSAVER);
		deluge::hid::display::Screensaver::timerEvent();
		expect(armed_ms()).to_equal(int32_t(120000));
		expect(deluge::hid::display::Screensaver::isActive()).to_be_false();

		// **The OFF early-out still comes first**, and it is asserted with the guest OPEN as well as
		// without it -- that pair is what pins the ORDER of the two tests. A stand-down placed above
		// the OFF check would arm the timer here, where legacy leaves it unarmed.
		FlashStorage::screensaverMode = ScreensaverMode::OFF;
		uiTimerManager.unsetTimer(TimerName::SCREENSAVER);
		deluge::hid::display::Screensaver::timerEvent();
		expect(armed_ms()).to_equal(int32_t(-1));

		closeGuest();
		setRootUILowLevel(&sessionView);
		uiTimerManager.unsetTimer(TimerName::SCREENSAVER);
		deluge::hid::display::Screensaver::timerEvent();
		expect(armed_ms()).to_equal(int32_t(-1));

		FlashStorage::screensaverMode = saved_mode;
		FlashStorage::screensaverTimeoutMinutes = saved_minutes;
		uiTimerManager.unsetTimer(TimerName::SCREENSAVER);
		nullifyUIs();
	});
});

describe menu_entry_tick_rate("the guest's tick rate with MenuScene as its tenant", $ {
	it("never asks to be woken at now_ms, so the 1 kHz cost stays latent", _ {
		ToggleGuard on{RuntimeFeatureStateToggle::On};
		deluge::scenes::spec::SongFixture f = deluge::scenes::spec::make_synth_song();

		// `Shell::next_wake()`'s own warning: a scene whose `next_timer()` answers 0 makes the
		// guest re-arm `UI_SPECIFIC` at its 1 ms floor for as long as it is stacked, i.e. ~15x
		// legacy's 15 ms `graphicsRoutine`. F1 task 7 left that latent because nothing opened the
		// guest; this task opens it, so the property is asserted rather than assumed.
		//
		// `MenuScene::next_timer()` answers `std::nullopt`, `kSourceBlinkMs` (20) or
		// `kShortcutBlinkMs` (180) -- never 0 (`scenes/menu/menu_scene.cpp:1591-1606`). Driven over
		// three roots, because the answer depends on the focused node's shortcut pads and a single
		// root would only prove it for that one.
		// `Shell::tick` renders, and rendering resolves l10n strings -- `l10n::chosenLanguage` is
		// null in a spec process because nothing constructed a `Display` (`l10n_bridge_spec.cpp:29`
		// records the same trap, and five other specs in this binary do the same line).
		deluge::l10n::chosenLanguage = &deluge::l10n::built_in::english;

		const uint32_t now = 100000;
		for (int32_t which = 0; which < 3; which++) {
			const bool opened = which == 0   ? entry::openShellMenu(nullptr, 0, false, false, false, false)
			                    : which == 1 ? entry::openShellMenu(f.clip, 0, false, false, false, false)
			                                 : entry::openShellMenu(f.clip, 0, false, false, false, false,
			                                                        entry::ShellMenuNode::Osc0FileSelector);
			expect(opened).to_be_true();

			const std::optional<uint32_t> timer = deluge::scenes::menu_scene().next_timer();
			expect(!timer.has_value() || *timer > 0).to_be_true();

			shellHostUI.shell().tick(now);
			const std::optional<uint32_t> wake = shellHostUI.shell().next_wake();
			// The condition that would end the reprieve is a deadline AT `now`, which is what a
			// zero `next_timer()` produces. A deadline in the future is a normal blink.
			expect(!wake.has_value() || *wake != now).to_be_true();
			closeGuest();
		}
	});
});
// clang-format on

/// Seven `describe` groups in one TU, so `DELUGE_SPEC` is spelled out here with `add_specs`, the way
/// `boot_wiring_spec.cpp` does.
int menu_entry_spec(int argc, char** const argv) {
	return CppSpec::parse(argc, argv)
	               .add_specs(menu_entry_toggle, menu_entry_roots, menu_entry_node_arg, menu_entry_refusal,
	                          menu_entry_undo_log, menu_entry_screensaver, menu_entry_tick_rate)
	               .exec()
	               .is_success()
	           ? EXIT_SUCCESS
	           : EXIT_FAILURE;
}
