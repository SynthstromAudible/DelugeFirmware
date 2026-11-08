/// @file
/// @brief The gate in front of the fifteen `openUI(&soundEditor)` sites, driven
///        against the real application globals.
///
/// `gui/ui/shell_menu_entry.cpp` carries the fifteen-site table; this file drives the gated
/// ones, one example per *argument shape*, plus the setting itself. It is at this level and not
/// in `tests/spec/gui/` for the reason `boot_wiring_spec.cpp` gives: the host suite deliberately
/// never builds the `deluge_scenes` closure, and everything under test here is the join between the
/// two layers -- `menu::context_for`'s root against `MenuScene::roots()`, a stable node id against
/// `SceneId::arg`, and `openUI(&shellHostUI)` against the real `uiNavigationHierarchy`.
///
/// @note **What this file cannot cover, stated rather than implied.** It drives `openShellMenu`,
///       which is what the gated sites call; it does **not** drive the sites themselves (site 15's column
///       aside), because
///       reaching `AudioClipView::buttonAction` here would need the whole legacy view stack. So
///       "the site passes the right arguments" is not covered here: like the `deluge.cpp` boot calls, it is
///       left to a level-3 A/B run. Each example names the site whose
///       argument shape it replays so the two can be compared by eye in the meantime.
///
/// @note **Every example puts the process back.** These are shared globals -- `numUIsOpen`,
///       `currentSong`, `runtimeFeatureSettings` -- and twenty other specs run in this binary.
///       `nullifyUIs()` is the teardown for the UI hierarchy: it is legacy's own "no UI is open"
///       and, unlike `closeUI`, it is well defined when the guest is the only thing on the stack.

#include "gui/ui/audio_recorder.h"
#include "gui/ui/menu_view_of.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "scenes/browser/browser_scene.h"
#include "scenes/menu/context.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/menu/node_id.h"
#include "scenes/menu/nodes/nodes.h"
#include "scenes/menu/shortcuts.h"
#include "scenes/profile.h"
#include "scenes/registry.h"

#include "shell/scene.h"
#include "shell/shell.h"

#include "gui/ui/shell_guest_services.h"
#include "gui/ui/shell_host_ui.h"
#include "gui/ui/shell_note_bridge.h"
#include "gui/ui/ui.h"
#include "util/shell_menu_entry.h"

#include "gui/l10n/l10n.h"
#include "gui/ui/keyboard/column_controls/dx.h"
#include "gui/ui/keyboard/keyboard_screen.h"
#include "gui/ui/sound_editor.h"
#include "gui/ui_timer_manager.h"
#include "gui/views/audio_clip_view.h"
#include "gui/views/automation_view.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/performance_view.h"
#include "gui/views/session_view.h"
#include "hid/button.h"
#include "hid/display/oled.h"
#include "hid/display/screensaver.h"
#include "io/midi/learned_midi.h"
#include "io/midi/midi_device.h"
#include "io/midi/midi_engine.h"
#include "model/action/action_logger.h"
#include "model/clip/instrument_clip.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound.h"
#include "storage/flash_storage.h"

#include "support/card_access.h"
#include "support/song_fixture.h"
#include "support/ui_mode_guard.h"

#include "cppspec.hpp"

#include <cstdint>
#include <optional>
#include <string>

#include "spec/support/spec_main.h"

/// @name Legacy's shift state, reached by bare `extern`: `Buttons::isShiftButtonPressed` reads
///       these (`hid/buttons.cpp`) and the header does not export them.
/// @{
namespace Buttons {
extern bool shiftCurrentlyPressed;
extern bool shiftCurrentlyStuck;
} // namespace Buttons
/// @}

namespace menu = deluge::scenes::menu;
namespace nodes = deluge::scenes::menu::nodes;
namespace shell = deluge::ui::shell;
namespace entry = deluge::gui::ui;
namespace spec = deluge::scenes::spec;

namespace {

/// @brief ShellMenu on (`On`) or off for the scope, through the Legacy Menu setting it is the inverse of; put back.
struct ToggleGuard {
	RuntimeFeatureStateToggle saved =
	    static_cast<RuntimeFeatureStateToggle>(runtimeFeatureSettings.get(RuntimeFeatureSettingType::LegacyMenu));

	explicit ToggleGuard(RuntimeFeatureStateToggle on) {
		runtimeFeatureSettings.set(RuntimeFeatureSettingType::LegacyMenu, on == RuntimeFeatureStateToggle::On
		                                                                      ? RuntimeFeatureStateToggle::Off
		                                                                      : RuntimeFeatureStateToggle::On);
	}
	~ToggleGuard() { runtimeFeatureSettings.set(RuntimeFeatureSettingType::LegacyMenu, saved); }
	ToggleGuard(const ToggleGuard&) = delete;
	ToggleGuard& operator=(const ToggleGuard&) = delete;
};

/// @brief Put the UI hierarchy back to "nothing open" after an example opened the guest.
///
/// `closeUI` would index `uiNavigationHierarchy[-1]` when the guest is the only entry
/// (`gui/ui/ui.cpp:213-223`, whose loop stops at `u >= 1`), so this is `nullifyUIs()`, which is what
/// legacy itself calls when it tears the hierarchy down (`gui/ui/ui.cpp:287-291`).
void closeGuest() {
	nullifyUIs();
}

/// @brief The `SceneId` the guest was told to open on.
shell::SceneId opened_on() {
	return shellHostUI.getInitialScene();
}

/// @brief A cable this spec owns outright -- `MIDICable` is abstract and no shipped subclass can be
///        built without a USB or DIN backend (`midi_learn_spec.cpp`'s `SpecCable`, same reason).
struct SpecCable final : ::MIDICable {
	void writeToFlash(uint8_t*) override {}
	char const* getDisplayName() override { return "entry"; }
	void sendMessage(MIDIMessage) override {}
	void sendSysex(const uint8_t*, int32_t) override {}
	[[nodiscard]] size_t sendBufferSpace() const override { return 0; }

protected:
	void writeReferenceAttributesToFile(Serializer&) override {}
};

SpecCable& cable() {
	static SpecCable instance;
	return instance;
}

/// @brief A `channelOrZone` neither the port nor legacy can produce
///        (`midi_learn_spec.cpp`'s `kPoisonChannel`, and for the identical reason: these
///        `LearnedMIDI`s are process singletons, so "unchanged" and "written correctly" are only
///        distinguishable if the slot starts at a value nothing writes).
constexpr uint8_t kPoisonChannel = 0xEE;

::LearnedMIDI& play_command_slot() {
	return midiEngine.globalMIDICommands[util::to_underlying(GlobalMIDICommand::PLAY)];
}

[[nodiscard]] bool poison(::LearnedMIDI& m) {
	m.cable = nullptr;
	m.channelOrZone = kPoisonChannel;
	m.noteOrCC = 0;
	return m.channelOrZone == kPoisonChannel && m.cable == nullptr;
}

/// @brief Open the guest focused on `nodes::midi_command_play`, the way `openShellMenu` opens the
///        menu -- `MenuScene::open`, then `setInitialScene`, then `openUI`
///        (`gui/ui/shell_menu_entry.cpp`).
///
/// Not through `openShellMenu` itself, because that resolves a *root* from the calling site's
/// arguments and `ShellMenuNode` names only two leaves, neither of them a MIDI command. What these
/// examples need is a focused item whose `learn_note_on` writes something -- `MidiCommandItem`
/// (in `scenes/menu/nodes/midi_command.cpp`) is the only ported one that does.
///
/// The node is named through `SceneId::arg`, not through `open()`'s root argument, and that is the
/// warning `MenuScene::open` carries: `on_enter` re-roots from `arg`, or from `ctx_.root` when
/// `arg == 0`, one call later -- so passing the leaf as the "root" and a zero `arg` lands on the
/// settings root instead, which is exactly how this helper failed the first time it was run.
/// @brief `spec::QuietAudio`, held from the first example that can leave a card listing behind.
///
/// **The site-8 example really opens the ported browser**, because
/// `MenuScene::on_enter` now performs the `Source::getOrCreateFirstRange()` legacy's permission
/// check performs (`SoundEditor::checkPermissionToBeginSessionForRangeSpecificParam`) -- so `browser::sample_can` stops
/// refusing and the scene reads `SAMPLES/` off the fixture card, which holds a file. The next `openUI(&shellHostUI)`
/// resets the stack, `BrowserScene::on_exit` calls `FileCatalogue::emptyFileItems`, and that pumps
/// `AudioEngine::routineWithClusterLoading()` between chunks (`storage/file_catalogue.cpp:94`) -- which **segfaults**
/// in a spec process, because nothing here ran `AudioEngine::init()` and so `inputRingPos` is still 0
/// (`AudioEngine::inputRingPos`; `support/card_access.h` has the whole diagnosis).
/// Measured: with the fix and without this latch, `menu_entry_spec` dies with SIGSEGV in
/// `fillInputRing`; with either one absent it passes.
///
/// Process-scoped, function-local rather than namespace-scope: this binary links every spec and
/// runs one per invocation, so a namespace-scope instance would silently disarm the audio routine
/// for all of them.
spec::QuietAudio& quiet_audio() {
	static spec::QuietAudio instance;
	return instance;
}

/// @brief Hold SHIFT for an example's lifetime.
struct HeldShift {
	HeldShift() {
		::Buttons::shiftCurrentlyPressed = true;
		::Buttons::shiftCurrentlyStuck = false;
	}
	~HeldShift() { ::Buttons::shiftCurrentlyPressed = false; }
	HeldShift(const HeldShift&) = delete;
	HeldShift& operator=(const HeldShift&) = delete;
};

/// @brief Restore the root UI an example changes with `setRootUILowLevel`.
struct RootUIGuard {
	RootUI* saved = getRootUI();
	RootUIGuard() = default;
	~RootUIGuard() {
		if (saved != nullptr) {
			setRootUILowLevel(saved);
		}
	}
	RootUIGuard(const RootUIGuard&) = delete;
	RootUIGuard& operator=(const RootUIGuard&) = delete;
};

/// @brief A legacy display that records the last popup and error: what the entry said when it refused.
struct PopupSpy final : deluge::hid::Display {
	std::string last_popup;
	std::optional<Error> last_error;

	using deluge::hid::Display::displayPopup;
	constexpr size_t getNumBrowserAndMenuLines() override { return 3; }
	void displayPopup(char const* text, int8_t, bool, uint8_t, int32_t, PopupType) override { last_popup = text; }
	void popupText(char const*, PopupType) override {}
	void popupTextTemporary(char const*, PopupType) override {}
	void cancelPopup() override {}
	void freezeWithError(char const*) override {}
	void displayError(Error error) override { last_error = error; }
	void removeWorkingAnimation() override {}
	void displayLoadingAnimationText(char const*, bool, bool) override {}
	void removeLoadingAnimation() override {}
	bool hasPopup() override { return false; }
	bool hasPopupOfType(PopupType) override { return false; }
	void consoleText(char const*) override {}
	void timerRoutine() override {}
};

/// @brief Point legacy's `display` at @p shown for the scope.
struct DisplayGuard {
	deluge::hid::Display* saved = display;
	explicit DisplayGuard(deluge::hid::Display& shown) { display = &shown; }
	~DisplayGuard() { display = saved; }
	DisplayGuard(const DisplayGuard&) = delete;
	DisplayGuard& operator=(const DisplayGuard&) = delete;
};

/// @brief `l10n::get` as a `std::string`, for `to_equal`.
std::string text(deluge::l10n::String s) {
	return std::string{deluge::l10n::get(s)};
}

/// @brief A kit clip with a fresh note row at index 0, and the model stack `enterDrumCreator` takes for it.
struct DrumRowStack {
	alignas(ModelStack) char memory[MODEL_STACK_MAX_SIZE];
	ModelStackWithNoteRow* stack = nullptr;
	explicit DrumRowStack(deluge::scenes::spec::SongFixture& f) {
		NoteRow* row = static_cast<InstrumentClip*>(f.clip)->noteRows.insertNoteRowAtIndex(0);
		if (row != nullptr) {
			stack = setupModelStackWithSong(memory, f.song)->addTimelineCounter(f.clip)->addNoteRow(0, row);
		}
	}
};

/// @brief Whether @p node is a horizontal page.
bool is_horizontal_page(const menu::Node* node) {
	return node != nullptr && node->kind == menu::Kind::Submenu && node->layout == menu::Layout::Horizontal;
}

bool openGuestOnPlayCommand(Song& song) {
	(void)quiet_audio();
	const menu::MenuContext ctx =
	    menu::context_for(song, deluge::gui::menuViewOf(nullptr), 0, false, false, false, false);
	const uint16_t arg = menu::node_id(nodes::midi_command_play);
	if (arg == 0) {
		return false;
	}
	deluge::scenes::menu_scene().open(nodes::root_settings, ctx);
	shellHostUI.setInitialScene(shell::SceneId{.kind = shell::SceneKind::FirstApp, .arg = arg});
	return openUI(&shellHostUI);
}

/// @brief LEARN pressed or released on the guest's menu, with nothing else held.
void press_learn(bool on) {
	const shell::ButtonSet held{};
	const deluge::ui::PadMask pads{};
	const shell::UiContext ui{held, pads, false, false, 0, deluge::scenes::deluge_profile()};
	(void)deluge::scenes::menu_scene().handle(
	    shell::UiEvent{.kind = shell::UiEvent::Kind::Button, .on = on, .button = shell::ButtonId::Learn}, ui);
}

} // namespace

// clang-format off
describe menu_entry_toggle("the Legacy Menu setting", $ {
	it("opens the ported menu by default (Legacy Menu Off), and shellMenuEnabled() follows the setting both ways", _ {
		expect(runtimeFeatureSettings.get(RuntimeFeatureSettingType::LegacyMenu)
		       == RuntimeFeatureStateToggle::Off)
		    .to_be_true();
		expect(entry::shellMenuEnabled()).to_be_true();
		{
			ToggleGuard off{RuntimeFeatureStateToggle::Off};
			expect(entry::shellMenuEnabled()).to_be_false();
			expect(runtimeFeatureSettings.get(RuntimeFeatureSettingType::LegacyMenu)
			       == RuntimeFeatureStateToggle::On)
			    .to_be_true();
		}
		// And the guard put it back, which is what the other examples depend on.
		expect(entry::shellMenuEnabled()).to_be_true();
	});
});

describe menu_entry_roots("openShellMenu resolves the root each site's arguments name", $ {
	it("site 1 (View::buttonAction, SHIFT + SELECT) opens the settings root", _ {
		ToggleGuard on{RuntimeFeatureStateToggle::On};
		deluge::scenes::spec::SongFixture f = deluge::scenes::spec::make_synth_song();
		(void)f;

		// All four UI facts false -- SHIFT is held at that site, so `setup()`'s two view tests are
		// false and the root is `settingsRootMenu` (`gui/ui/sound_editor.cpp:1816-1825`).
		expect(entry::openShellMenu(nullptr, 0, false, false, false, false)).to_be_true();
		expect(getCurrentUI() == &shellHostUI).to_be_true();
		expect(deluge::scenes::menu_scene().focused() == &nodes::root_settings).to_be_true();
		// `arg == 0` is "the root the context chose" (`MenuScene::on_enter`'s `arg == 0` branch, `scenes/menu/menu_scene.cpp`).
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
		// The first example in this file that reads the card: the item's `on_focus` pushes
		// `BrowserScene`, which lists `SAMPLES/`. See `quiet_audio()` for why that needs the latch.
		(void)quiet_audio();
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
		// that produces it. Asserted non-zero here because zero means "the context's
		// root" downstream, which would silently open the sound root instead.
		expect(id != 0).to_be_true();

		expect(entry::openShellMenu(f.clip, 0, false, false, false, false,
		                            entry::ShellMenuNode::Osc0FileSelector))
		    .to_be_true();
		expect(opened_on() == shell::SceneId{.kind = shell::SceneKind::FirstApp, .arg = id}).to_be_true();
		// And it landed there rather than merely being asked to: `MenuScene::on_enter` resolves the
		// arg through `node_by_id` and re-roots the path at it (`MenuScene::on_enter`).
		expect(deluge::scenes::menu_scene().focused() == &nodes::file0_selector).to_be_true();
		// ...and the log survived, which is the half a `clip != nullptr` test alone would get wrong.
		expect(actionLogger.firstAction[BEFORE] != nullptr).to_be_true();
		closeGuest();
	});

	it("site 14's legacy fallback opens the guest on the file selector, never the legacy editor", _ {
		// Reached with ShellMenu on only when the guest's own open refused and legacy's `setup()` did not. A boot
		// numbers every node, so the only way there is an id missing: numbered over the file selector alone, the
		// recorder item has none and `openShellDrumRecorder` refuses; legacy's `openUI(&audioRecorder)` then refuses a
		// take already running.
		(void)quiet_audio();
		ToggleGuard on{RuntimeFeatureStateToggle::On};
		deluge::hid::display::OLED oled_display;
		deluge::hid::Display* const saved_display = display;
		display = &oled_display;
		RootUIGuard root_guard;
		const auto drum_take = [&](deluge::scenes::spec::SongFixture& f) {
			NoteRow* row = static_cast<InstrumentClip*>(f.clip)->noteRows.insertNoteRowAtIndex(0);
			expect(row != nullptr).to_be_true();
			if (row == nullptr) {
				return;
			}
			alignas(ModelStack) char memory[MODEL_STACK_MAX_SIZE];
			ModelStackWithNoteRow* stack =
			    setupModelStackWithSong(memory, f.song)->addTimelineCounter(f.clip)->addNoteRow(0, row);
			AudioEngine::setForegroundRecording(nullptr, AudioInputChannel::OUTPUT);
			instrumentClipView.enterDrumCreator(stack, /*doRecording=*/true);
			AudioEngine::setForegroundRecording(nullptr, AudioInputChannel::NONE);
		};

		{
			deluge::scenes::spec::SongFixture f = deluge::scenes::spec::make_kit_song();
			setRootUILowLevel(&instrumentClipView);
			const menu::Node* const just_the_selector[] = {&nodes::file0_selector};
			(void)menu::assign_node_ids(just_the_selector);
			drum_take(f);
			expect(getCurrentUI() == &shellHostUI).to_be_true();
			expect(isUIOpen(&soundEditor)).to_be_false();
			expect(deluge::scenes::menu_scene().focused() == &nodes::file0_selector).to_be_true();
			closeGuest();
		}
		{
			// Neither node numbered: the guest cannot open either, and the legacy editor still does not.
			deluge::scenes::spec::SongFixture f = deluge::scenes::spec::make_kit_song();
			setRootUILowLevel(&instrumentClipView);
			const menu::Node* const neither[] = {&nodes::root_song};
			(void)menu::assign_node_ids(neither);
			drum_take(f);
			expect(isUIOpen(&shellHostUI)).to_be_false();
			expect(isUIOpen(&soundEditor)).to_be_false();
			nullifyUIs();
		}
		(void)menu::assign_node_ids(nodes::kAllRoots);
		display = saved_display;
	});

	it("cancels the popup legacy's display holds as it opens, as setup() does", _ {
		// An audition leaves the drum's name up as a persistent popup (`InstrumentClipView::drawDrumName`); RECORD then
		// opens the recorder over it, and `setup()`'s `display->cancelPopup()` takes it down.
		ToggleGuard on{RuntimeFeatureStateToggle::On};
		deluge::scenes::spec::SongFixture f = deluge::scenes::spec::make_synth_song();
		deluge::hid::display::OLED oled_display;
		deluge::hid::Display* const saved_display = display;
		display = &oled_display;
		display->popupText("KICK");
		expect(display->hasPopup()).to_be_true();
		expect(entry::openShellMenu(f.clip, 0, false, false, false, false)).to_be_true();
		expect(display->hasPopup()).to_be_false();
		closeGuest();
		display = saved_display;
	});

	it("site 11 opens DX7 source on the new FM synth, keeping the undo log", _ {
		// `InstrumentClipMinder::createNewInstrument`'s `soundEditor.setup(clip, &dxMenu, 0)`: a named item, so no wipe.
		ToggleGuard on{RuntimeFeatureStateToggle::On};
		deluge::scenes::spec::SongFixture f = deluge::scenes::spec::make_synth_song();
		Sound* sound = static_cast<Sound*>(static_cast<ModControllableAudio*>(f.instrument->toModControllable()));
		sound->sources[0].setOscType(OscType::DX7);
		expect(entry::openShellMenu(nullptr, 0, false, false, false, false)).to_be_true();
		actionLogger.deleteAllLogs();
		(void)actionLogger.getNewAction(ActionType::MISC, ActionAddition::NOT_ALLOWED);
		closeGuest();

		expect(entry::openShellMenu(f.clip, 0, false, false, false, false, entry::ShellMenuNode::Dx)).to_be_true();
		expect(opened_on() == shell::SceneId{.kind = shell::SceneKind::FirstApp, .arg = menu::node_id(nodes::dx_menu)})
		    .to_be_true();
		expect(deluge::scenes::menu_scene().focused() == &nodes::dx_menu).to_be_true();
		expect(actionLogger.firstAction[BEFORE] != nullptr).to_be_true();
		closeGuest();
	});

	it("site 15 opens the DX7 parameter page on the column's operator, its pad blinking, keeping the undo log", _ {
		// `DXColumn::handlePad`'s Shift arm: `openForOpOrGlobal`'s `setup(getCurrentClip(), &dxParam, 0)`.
		ToggleGuard on{RuntimeFeatureStateToggle::On};
		deluge::scenes::spec::SongFixture f = deluge::scenes::spec::make_synth_song();
		Sound* sound = static_cast<Sound*>(static_cast<ModControllableAudio*>(f.instrument->toModControllable()));
		sound->sources[0].setOscType(OscType::DX7);
		f.song->setCurrentClip(f.clip);
		expect(entry::openShellMenu(nullptr, 0, false, false, false, false)).to_be_true();
		actionLogger.deleteAllLogs();
		(void)actionLogger.getNewAction(ActionType::MISC, ActionAddition::NOT_ALLOWED);
		closeGuest();

		// Through the column itself, its Shift arm gated: row 5 is operator index 2 (`8 - y - 1`), acted on release.
		{
			HeldShift shift;
			deluge::gui::ui::keyboard::controls::DXColumn column;
			deluge::gui::ui::keyboard::PressedPad release{};
			release.x = 17;
			release.y = 5;
			release.active = false;
			column.handlePad(nullptr, release, nullptr);
		}
		expect(isUIOpen(&shellHostUI)).to_be_true();
		expect(opened_on() == shell::SceneId{.kind = shell::SceneKind::FirstApp, .arg = menu::node_id(nodes::dx_param)})
		    .to_be_true();
		expect(deluge::scenes::menu_scene().focused() == &nodes::dx_param).to_be_true();
		expect(static_cast<int32_t>(deluge::scenes::menu_scene().state().dx.param)).to_equal(2 * 21 + 16);
		expect(deluge::scenes::menu_scene().state().dx.pad_blink).to_be_true();
		expect(actionLogger.firstAction[BEFORE] != nullptr).to_be_true();
		// With the guest open the press is the menu's: the same operator again toggles to its coarse tuning.
		expect(entry::openShellDxColumn(2)).to_be_true();
		expect(static_cast<int32_t>(deluge::scenes::menu_scene().state().dx.param)).to_equal(2 * 21 + 18);
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

describe menu_entry_shortcut("site 9: a shortcut pad from a clip view opens the ported menu on that pad's node", $ {
	it("Shift + (8,7) on a synth clip opens the filters group focused on nodes::lpf_freq through SceneId::arg", _ {
		// `SoundEditor::setup(clip, item, …)` lands a named item on the horizontal page that lists it,
		// focused on it (`maybeGetParentMenu` and `parent->focusChild`, `gui/ui/sound_editor.cpp`).
		ToggleGuard on{RuntimeFeatureStateToggle::On};
		deluge::scenes::spec::SongFixture f = deluge::scenes::spec::make_synth_song();
		expect(menu::shortcut_node(menu::MenuContext::Root::Sound, false, 8, 7) == &nodes::lpf_freq).to_be_true();

		expect(entry::openShellMenuAtShortcut(f.clip, 8, 7, false, false, false, false)).to_be_true();
		expect(getCurrentUI() == &shellHostUI).to_be_true();
		expect(opened_on() == shell::SceneId{.kind = shell::SceneKind::FirstApp, .arg = menu::node_id(nodes::filters_group)})
		    .to_be_true();
		expect(deluge::scenes::menu_scene().focused() == &nodes::filters_group).to_be_true();
		expect(deluge::scenes::menu_scene().focused_value() == &nodes::lpf_freq).to_be_true();
		expect(deluge::scenes::menu_scene().menu_context().source_index).to_equal(int8_t{0});
		closeGuest();
	});

	it("an odd column addresses source 1", _ {
		ToggleGuard on{RuntimeFeatureStateToggle::On};
		deluge::scenes::spec::SongFixture f = deluge::scenes::spec::make_synth_song();
		const menu::Node* node = menu::shortcut_node(menu::MenuContext::Root::Sound, false, 3, 2);
		expect(node != nullptr).to_be_true();

		expect(entry::openShellMenuAtShortcut(f.clip, 3, 2, false, false, false, false)).to_be_true();
		expect(is_horizontal_page(deluge::scenes::menu_scene().focused())).to_be_true();
		expect(deluge::scenes::menu_scene().focused_value() == node).to_be_true();
		expect(deluge::scenes::menu_scene().menu_context().source_index).to_equal(int8_t{1});
		closeGuest();
	});

	it("a cell with no shortcut opens nothing", _ {
		ToggleGuard on{RuntimeFeatureStateToggle::On};
		deluge::scenes::spec::SongFixture f = deluge::scenes::spec::make_synth_song();
		expect(menu::shortcut_node(menu::MenuContext::Root::Sound, false, 15, 0) == nullptr).to_be_true();

		expect(entry::openShellMenuAtShortcut(f.clip, 15, 0, false, false, false, false)).to_be_false();
		expect(isUIOpen(&shellHostUI)).to_be_false();
	});

	it("a node that does not apply opens nothing and says PARAMETER NOT APPLICABLE, as setup() does", _ {
		ToggleGuard on{RuntimeFeatureStateToggle::On};
		PopupSpy spy;
		DisplayGuard shown{spy};
		deluge::scenes::spec::SongFixture f = deluge::scenes::spec::make_synth_song();
		// The first even-column pad whose node does not apply to this synth.
		const menu::MenuContext ctx =
		    menu::context_for(*f.song, deluge::gui::menuViewOf(f.clip), 0, false, false, false, false);
		int32_t px = -1;
		int32_t py = -1;
		for (int32_t x = 0; x < 14 && px < 0; x += 2) {
			for (int32_t y = 0; y < 8 && px < 0; y++) {
				const menu::Node* n = menu::shortcut_node(menu::MenuContext::Root::Sound, false, static_cast<uint8_t>(x),
				                                          static_cast<uint8_t>(y));
				if (n != nullptr && menu::node_id(*n) != 0 && !menu::shortcut_applies(*n, ctx)) {
					px = x;
					py = y;
				}
			}
		}
		expect(px >= 0).to_be_true();

		expect(entry::openShellMenuAtShortcut(f.clip, px, py, false, false, false, false)).to_be_false();
		expect(isUIOpen(&shellHostUI)).to_be_false();
		expect(spy.last_popup).to_equal(text(deluge::l10n::String::STRING_FOR_PARAMETER_NOT_APPLICABLE));
	});

	it("does not wipe the undo log", _ {
		ToggleGuard on{RuntimeFeatureStateToggle::On};
		deluge::scenes::spec::SongFixture f = deluge::scenes::spec::make_synth_song();

		// Seeded with the guest open, because `getNewAction` refuses otherwise.
		expect(entry::openShellMenu(nullptr, 0, false, false, false, false)).to_be_true();
		actionLogger.deleteAllLogs();
		(void)actionLogger.getNewAction(ActionType::MISC, ActionAddition::NOT_ALLOWED);
		expect(actionLogger.firstAction[BEFORE] != nullptr).to_be_true();
		closeGuest();

		expect(entry::openShellMenuAtShortcut(f.clip, 8, 7, false, false, false, false)).to_be_true();
		expect(actionLogger.firstAction[BEFORE] != nullptr).to_be_true();
		closeGuest();
	});

	it("marks a keyboard-screen root", _ {
		ToggleGuard on{RuntimeFeatureStateToggle::On};
		RootUIGuard root_guard;
		deluge::scenes::spec::SongFixture f = deluge::scenes::spec::make_synth_song();

		setRootUILowLevel(&instrumentClipView);
		expect(entry::openShellMenuAtShortcut(f.clip, 8, 7, false, false, false, false)).to_be_true();
		expect(deluge::scenes::menu_scene().menu_context().root_is_keyboard).to_be_false();
		closeGuest();

		setRootUILowLevel(&keyboardScreen);
		expect(entry::openShellMenuAtShortcut(f.clip, 8, 7, false, false, false, false)).to_be_true();
		expect(deluge::scenes::menu_scene().menu_context().root_is_keyboard).to_be_true();
		closeGuest();

		setRootUILowLevel(&keyboardScreen);
		expect(entry::openShellMenu(f.clip, 0, false, false, false, false)).to_be_true();
		expect(deluge::scenes::menu_scene().menu_context().root_is_keyboard).to_be_true();
		closeGuest();
	});

	it("Shift + (4,0) on a DX7 sound opens the DX7 parameter page alone, on the param it showed last", _ {
		// `potentialShortcutPadAction`'s fallback: modulator 1's volume does not apply to a sound that is not FM, so
		// `setup()` refuses it, and on a DX7 source `setup(clip, &dxParam, 0)` opens the page as `dxParam.param` stands.
		ToggleGuard on{RuntimeFeatureStateToggle::On};
		deluge::scenes::spec::SongFixture f = deluge::scenes::spec::make_synth_song();
		Sound* sound = static_cast<Sound*>(static_cast<ModControllableAudio*>(f.instrument->toModControllable()));
		sound->sources[0].setOscType(OscType::DX7);
		deluge::scenes::menu_scene().land_dx(menu::DxState{.param = 3 * 21 + 2});
		expect(menu::shortcut_node(menu::MenuContext::Root::Sound, false, 4, 0) == &nodes::modulator0_volume).to_be_true();

		expect(entry::openShellMenuAtShortcut(f.clip, 4, 0, false, false, false, false)).to_be_true();
		expect(opened_on() == shell::SceneId{.kind = shell::SceneKind::FirstApp, .arg = menu::node_id(nodes::dx_param)})
		    .to_be_true();
		expect(deluge::scenes::menu_scene().focused() == &nodes::dx_param).to_be_true();
		expect(static_cast<int32_t>(deluge::scenes::menu_scene().state().depth)).to_equal(0);
		expect(static_cast<int32_t>(deluge::scenes::menu_scene().state().dx.param)).to_equal(3 * 21 + 2);
		closeGuest();

		// The same pad on a sound that is not DX7 is refused, with setup()'s popup.
		sound->sources[0].setOscType(OscType::SAW);
		expect(entry::openShellMenuAtShortcut(f.clip, 4, 0, false, false, false, false)).to_be_false();
		expect(isUIOpen(&shellHostUI)).to_be_false();
	});

	it("with ShellMenu off, the pad opens the legacy SoundEditor", _ {
		deluge::scenes::spec::SongFixture f = deluge::scenes::spec::make_synth_song();
		(void)f;
		deluge::hid::display::OLED oled_display;
		deluge::hid::Display* const saved_display = display;
		display = &oled_display;
		const int32_t saved_mode = currentUIMode;
		currentUIMode = UI_MODE_NONE;
		RootUIGuard root_guard;
		HeldShift shift;

		{
			ToggleGuard off{RuntimeFeatureStateToggle::Off};
			setRootUILowLevel(&instrumentClipView);
			(void)soundEditor.potentialShortcutPadAction(8, 7, true);
			expect(getCurrentUI() == &soundEditor).to_be_true();
			// Legacy's own close, which ends the item's session and clears the navigation record.
			soundEditor.exitCompletely();
			nullifyUIs();
		}
		{
			ToggleGuard on{RuntimeFeatureStateToggle::On};
			setRootUILowLevel(&instrumentClipView);
			(void)soundEditor.potentialShortcutPadAction(8, 7, true);
			expect(getCurrentUI() == &shellHostUI).to_be_true();
			expect(deluge::scenes::menu_scene().focused_value() == &nodes::lpf_freq).to_be_true();
			nullifyUIs();
		}
		deluge::hid::display::OLED::removePopup();
		display = saved_display;
		currentUIMode = saved_mode;
	});
});

describe menu_entry_refusal("openShellMenu refuses where SoundEditor::setup refuses", $ {
	it("a kit with neither affect-entire nor a selected drum opens nothing", _ {
		ToggleGuard on{RuntimeFeatureStateToggle::On};
		deluge::scenes::spec::SongFixture kit = deluge::scenes::spec::make_kit_song();

		// `MenuContext::resolved` is `setup()`'s own `return false` at
		// `gui/ui/sound_editor.cpp:1733-1739`, and the five gated sites that test a return value
		// are written against exactly that. **Nothing must be left half-open**: the assertion is
		// that the hierarchy did not move, not merely that the call said false.
		expect(entry::openShellMenu(kit.clip, 0, false, false, false, /*kitGlobalFx=*/false)).to_be_false();
		// `isUIOpen`, deliberately, and NOT `getCurrentUI()`: with an empty hierarchy `getCurrentUI()`
		// answers `lastUIBeforeNullifying` (`gui/ui/ui.cpp:166-171`), which the previous example's
		// teardown left pointing at the guest. `currentUIIsClipMinderScreen()`'s null guard trips on the
		// same accessor from the other side, which is why it is named here.
		expect(isUIOpen(&shellHostUI)).to_be_false();

		// And the same clip with the one argument flipped DOES open, so the refusal is about the
		// context this kit resolves to and not about kit clips, or about the fixture, or about the
		// setting being read twice.
		expect(entry::openShellMenu(kit.clip, 0, false, false, false, /*kitGlobalFx=*/true)).to_be_true();
		expect(isUIOpen(&shellHostUI)).to_be_true();
		closeGuest();
	});
});

describe menu_entry_undo_log("the undo-log wipe SoundEditor::setup does on every menu open", $ {
	it("is done for a clip-rooted open and NOT for a settings-rooted one", _ {
		ToggleGuard on{RuntimeFeatureStateToggle::On};
		deluge::scenes::spec::SongFixture f = deluge::scenes::spec::make_synth_song();

		// In `SoundEditor::setup` the undo-log wipe sits inside `if (item) ... else if (clip)`, so the wipe follows the
		// clip and not the open. the "Not owned by this file" note in `scenes/menu/context.cpp` records it as owed to
		// whoever lands the port's entry point; this is that entry point.
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
		// The collision at `hid/display/screensaver.cpp`:
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
		// legacy's 15 ms `graphicsRoutine`. This group opens the guest, so the
		// property is asserted rather than assumed.
		//
		// `MenuScene::next_timer()` answers `std::nullopt`, `kSourceBlinkMs` (20) or
		// `kShortcutBlinkMs` (180) -- never 0 (`pad_next_timer`, `scenes/menu/pads.cpp`).
		//
		// **This is a sample, not a sweep, and the distinction matters.** The eight gated sites open
		// ten different roots between them; what runs below is the settings root, the sound root and
		// one leaf inside the sound root -- **three of ten**. Song, Performance, AudioClip,
		// KitGlobalFx, MidiOrCv, MidiDrum and GateDrum are undriven here. The property is safe for
		// all ten by *reading* -- `next_timer()` has exactly three returns and none of them is 0 --
		// and the sample exists because the answer depends on the focused node's shortcut pads, so
		// one root would not even sample the two branches. Do not read it as coverage of the roots
		// the sites open.
		// `Shell::tick` renders, and rendering resolves l10n strings -- `l10n::chosenLanguage` is
		// null in a spec process because nothing constructed a `Display` (`l10n_bridge_spec.cpp`
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
describe menu_entry_midi_learn("MIDI from PlaybackHandler learns into the ported menu", $ {
	// The whole route: `PlaybackHandler` offers the message to the current UI, `ShellHostUI` routes it to the menu, and
	// the focused MIDI command's `LearnedMIDI` is written. `midiEngine.globalMIDICommands` is a process singleton, so
	// the slot is poisoned first: a slot another example already learned would make a route that wrote nothing look
	// like one that worked.

	it("a note-on, a program change and a CC while LEARN is held write the focused command's slot", _ {
		ToggleGuard on{RuntimeFeatureStateToggle::On};
		deluge::scenes::spec::SongFixture f = deluge::scenes::spec::make_synth_song();
		Song* const saved_song = currentSong;
		currentSong = f.song;
		::LearnedMIDI& slot = play_command_slot();
		const ::LearnedMIDI saved_slot = slot;

		expect(openGuestOnPlayCommand(*f.song)).to_be_true();
		expect(deluge::scenes::menu_scene().focused() == &nodes::midi_command_play).to_be_true();
		press_learn(true);
		bool thru = false;

		expect(poison(slot)).to_be_true();
		playbackHandler.noteMessageReceived(cable(), true, 9, 64, 100, &thru);
		expect(slot.cable == &cable()).to_be_true();
		expect(slot.channelOrZone == 9).to_be_true();
		expect(slot.noteOrCC == 64).to_be_true();

		expect(poison(slot)).to_be_true();
		playbackHandler.programChangeReceived(cable(), 3, 7);
		expect(slot.channelOrZone == 3 + IS_A_PC).to_be_true();
		expect(slot.noteOrCC == 7).to_be_true();

		expect(poison(slot)).to_be_true();
		playbackHandler.midiCCReceived(cable(), 15, 20, 127, &thru);
		expect(slot.channelOrZone == 15 + IS_A_CC).to_be_true();
		expect(slot.noteOrCC == 20).to_be_true();

		// Released: the next message is the song's, not the menu's.
		press_learn(false);
		expect(poison(slot)).to_be_true();
		expect(shellHostUI.noteOnReceivedForMidiLearn(cable(), 9, 65, 100)).to_be_false();
		expect(slot.channelOrZone == kPoisonChannel).to_be_true();

		closeGuest();
		slot = saved_slot;
		currentSong = saved_song;
	});

	it("a transparent overlay over the menu does not stop the learn; the menu not being there does", _ {
		// `input_top()`, not `top()`: a passive overlay is drawn over the menu without taking its input
		// (`Scene::input_transparent`). The negative half removes the menu rather than covering it, because every
		// opaque scene in the registry refuses `on_enter` without setup this file does not build.
		ToggleGuard on{RuntimeFeatureStateToggle::On};
		deluge::scenes::spec::SongFixture f = deluge::scenes::spec::make_synth_song();
		Song* const saved_song = currentSong;
		currentSong = f.song;
		::LearnedMIDI& slot = play_command_slot();
		const ::LearnedMIDI saved_slot = slot;

		expect(openGuestOnPlayCommand(*f.song)).to_be_true();
		press_learn(true);

		// `FirstApp + 6` is `overlay::param_popup_scene()` (`scenes/registry.h`), which is input-transparent.
		const auto popup = static_cast<shell::SceneKind>(static_cast<uint8_t>(shell::SceneKind::FirstApp) + 6);
		expect(shellHostUI.shell().stack().push(shell::SceneId{.kind = popup, .arg = 0})).to_be_true();
		expect(shellHostUI.shell().stack().top().kind() == popup).to_be_true();
		expect(poison(slot)).to_be_true();
		expect(shellHostUI.noteOnReceivedForMidiLearn(cable(), 3, 40, 100)).to_be_true();
		expect(slot.channelOrZone == 3).to_be_true();
		expect(slot.noteOrCC == 40).to_be_true();

		// The menu gone with LEARN still held: the note is not used, so it plays.
		expect(shellHostUI.shell().stack().remove(shell::SceneKind::FirstApp)).to_be_true();
		expect(poison(slot)).to_be_true();
		expect(shellHostUI.noteOnReceivedForMidiLearn(cable(), 3, 40, 100)).to_be_false();
		expect(slot.channelOrZone == kPoisonChannel).to_be_true();

		press_learn(false);
		closeGuest();
		slot = saved_slot;
		currentSong = saved_song;
	});

	it("a menu closed with LEARN held and opened again is not learning", _ {
		ToggleGuard on{RuntimeFeatureStateToggle::On};
		deluge::scenes::spec::SongFixture f = deluge::scenes::spec::make_synth_song();
		Song* const saved_song = currentSong;
		currentSong = f.song;
		::LearnedMIDI& slot = play_command_slot();
		const ::LearnedMIDI saved_slot = slot;

		expect(openGuestOnPlayCommand(*f.song)).to_be_true();
		press_learn(true);
		closeGuest(); // a song load tears the UIs down the same way
		expect(openGuestOnPlayCommand(*f.song)).to_be_true();
		expect(poison(slot)).to_be_true();
		expect(shellHostUI.noteOnReceivedForMidiLearn(cable(), 3, 40, 100)).to_be_false();
		expect(slot.channelOrZone == kPoisonChannel).to_be_true();

		press_learn(false);
		closeGuest();
		slot = saved_slot;
		currentSong = saved_song;
	});
});
namespace {
/// @brief The automation view as the root, a pad held with pad selection on, and ShellMenu on; all put back.
struct HeldPadInAutomation {
	deluge::hid::display::OLED oled_display;
	deluge::hid::Display* saved_display = display;
	uint32_t saved_mode = currentUIMode;
	bool saved_pad_selection = automationView.padSelectionOn;
	bool saved_arranger = automationView.onArrangerView;
	RootUIGuard root_guard;
	explicit HeldPadInAutomation(bool arranger = false) {
		display = &oled_display;
		setRootUILowLevel(&automationView);
		automationView.onArrangerView = arranger;
		automationView.padSelectionOn = true;
		currentUIMode = UI_MODE_NOTES_PRESSED;
	}
	~HeldPadInAutomation() {
		currentUIMode = saved_mode;
		automationView.padSelectionOn = saved_pad_selection;
		automationView.onArrangerView = saved_arranger;
		display = saved_display;
	}
	HeldPadInAutomation(const HeldPadInAutomation&) = delete;
	HeldPadInAutomation& operator=(const HeldPadInAutomation&) = delete;
	/// The press `AutomationView::buttonAction` routes to `handleSelectEncoderButtonAction`.
	void select() { (void)automationView.buttonAction(deluge::hid::button::SELECT_ENC, true); }
};
} // namespace

describe menu_entry_automation_notes("site 7 under UI_MODE_NOTES_PRESSED: what setup(clip) infers", $ {
	// The note leaves read through the note bridge, which boot wires (`deluge.cpp`) and this binary does not.
	before_all([] { deluge::gui::ui::wireShellNoteBridge(); });

	it("opens the note editor on an instrument clip, wiping the undo log as setup() does with no item", _ {
		ToggleGuard on{RuntimeFeatureStateToggle::On};
		deluge::scenes::spec::SongFixture f = deluge::scenes::spec::make_synth_song();
		HeldPadInAutomation held;
		// `ActionLogger::getNewAction` takes an action only while the root is the current UI, which it is here.
		actionLogger.deleteAllLogs();
		(void)actionLogger.getNewAction(ActionType::MISC, ActionAddition::NOT_ALLOWED);
		expect(actionLogger.firstAction[BEFORE] != nullptr).to_be_true();

		held.select();
		expect(getCurrentUI() == &shellHostUI).to_be_true();
		expect(isUIOpen(&soundEditor)).to_be_false();
		expect(deluge::scenes::menu_scene().menu_context().root == menu::MenuContext::Root::NoteEditor).to_be_true();
		expect(deluge::scenes::menu_scene().focused() == &nodes::note_editor_root).to_be_true();
		// `setup(clip)` with no item wipes before it picks `noteEditorRootMenu`, unlike `enterNoteEditor`'s named root.
		expect(actionLogger.firstAction[BEFORE] == nullptr).to_be_true();
		closeGuest();
	});

	it("leaves a kit's affect-entire on, where enterNoteEditor clears it", _ {
		ToggleGuard on{RuntimeFeatureStateToggle::On};
		deluge::scenes::spec::SongFixture kit = deluge::scenes::spec::make_kit_song();
		auto* clip = static_cast<InstrumentClip*>(kit.clip);
		clip->affectEntire = true;
		HeldPadInAutomation held;

		held.select();
		expect(getCurrentUI() == &shellHostUI).to_be_true();
		expect(deluge::scenes::menu_scene().menu_context().root == menu::MenuContext::Root::NoteEditor).to_be_true();
		expect(clip->affectEntire).to_be_true();
		expect(deluge::scenes::menu_scene().menu_context().affect_entire).to_be_true();
		closeGuest();
		clip->affectEntire = false;
	});

	it("opens an audio clip's root, and the arranger's song root, as setup() does under the same mode", _ {
		ToggleGuard on{RuntimeFeatureStateToggle::On};
		deluge::scenes::spec::AudioSongFixture f = deluge::scenes::spec::make_audio_clip_song();
		{
			HeldPadInAutomation held;
			// The press's LED refresh asks the root UI for affect-entire, and `AutomationView::getAffectEntire` reads
			// `getCurrentInstrumentClip()->affectEntire` with no audio-clip arm: on the device a read of a low address,
			// here a fault. The gate reads only the clip, so this half keeps the audio clip view as the root.
			setRootUILowLevel(&audioClipView);
			held.select();
			expect(getCurrentUI() == &shellHostUI).to_be_true();
			expect(deluge::scenes::menu_scene().focused() == &nodes::root_audio_clip).to_be_true();
			closeGuest();
		}
		{
			HeldPadInAutomation held{/*arranger=*/true};
			held.select();
			expect(getCurrentUI() == &shellHostUI).to_be_true();
			expect(deluge::scenes::menu_scene().focused() == &nodes::root_song).to_be_true();
			closeGuest();
		}
	});
});

describe menu_entry_close_mode("leaving the ported menu ends the UI mode it was left under, as exitCompletely does", $ {
	it("clears AFFECT ENTIRE held on a kit row's menu when the long-press exit closes it", _ {
		ToggleGuard on{RuntimeFeatureStateToggle::On};
		RootUIGuard root_guard;
		deluge::scenes::spec::UiModeGuard mode;
		deluge::scenes::spec::SongFixture kit = deluge::scenes::spec::make_kit_song();
		(void)deluge::scenes::spec::add_drum<SoundDrum>(kit);
		setRootUILowLevel(&instrumentClipView);
		expect(entry::openShellMenuAtShortcut(kit.clip, 8, 7, false, false, false, /*kitGlobalFx=*/false)).to_be_true();
		expect(deluge::scenes::menu_scene().takes_affect_entire()).to_be_true();
		expect(entry::forwardMenuAffectEntire(deluge::hid::button::AFFECT_ENTIRE, true).has_value()).to_be_true();
		expect(currentUIMode).to_equal(uint32_t{UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR});

		expect(shellHostUI.exitUI() == ActionResult::ACTIONED_AND_CAUSED_CHANGE).to_be_true();
		expect(isUIOpen(&shellHostUI)).to_be_false();
		expect(currentUIMode).to_equal(uint32_t{UI_MODE_NONE});
	});

	it("clears it when a short BACK at the menu's root closes it, which legacy accepts with AFFECT ENTIRE held", _ {
		ToggleGuard on{RuntimeFeatureStateToggle::On};
		RootUIGuard root_guard;
		deluge::scenes::spec::UiModeGuard mode;
		deluge::scenes::spec::SongFixture kit = deluge::scenes::spec::make_kit_song();
		(void)deluge::scenes::spec::add_drum<SoundDrum>(kit);
		setRootUILowLevel(&instrumentClipView);
		expect(entry::openShellMenuAtShortcut(kit.clip, 8, 7, false, false, false, /*kitGlobalFx=*/false)).to_be_true();
		expect(static_cast<int32_t>(deluge::scenes::menu_scene().state().depth)).to_equal(0);
		expect(entry::forwardMenuAffectEntire(deluge::hid::button::AFFECT_ENTIRE, true).has_value()).to_be_true();

		(void)shellHostUI.buttonAction(deluge::hid::button::BACK, true);
		(void)shellHostUI.timerCallback();
		expect(isUIOpen(&shellHostUI)).to_be_false();
		expect(currentUIMode).to_equal(uint32_t{UI_MODE_NONE});
	});

	it("clears the non-exclusive modes too: an audition the menu was opened under", _ {
		ToggleGuard on{RuntimeFeatureStateToggle::On};
		RootUIGuard root_guard;
		deluge::scenes::spec::UiModeGuard mode;
		deluge::scenes::spec::SongFixture f = deluge::scenes::spec::make_synth_song();
		setRootUILowLevel(&instrumentClipView);
		expect(entry::openShellMenu(f.clip, 0, false, false, false, false)).to_be_true();
		currentUIMode = UI_MODE_AUDITIONING;

		(void)shellHostUI.exitUI();
		expect(isUIOpen(&shellHostUI)).to_be_false();
		expect(currentUIMode).to_equal(uint32_t{UI_MODE_NONE});
	});

	it("keeps a stem export the menu started, and only its mode: legacy resets first and the export engages after", _ {
		ToggleGuard on{RuntimeFeatureStateToggle::On};
		RootUIGuard root_guard;
		deluge::scenes::spec::UiModeGuard mode;
		deluge::scenes::spec::SongFixture f = deluge::scenes::spec::make_synth_song();
		setRootUILowLevel(&instrumentClipView);
		expect(entry::openShellMenu(f.clip, 0, false, false, false, false)).to_be_true();
		// What `startStemExport` leaves before the tick's close: the export engaged over a latched audition.
		currentUIMode = UI_MODE_STEM_EXPORT | UI_MODE_AUDITIONING;

		(void)shellHostUI.exitUI();
		expect(isUIOpen(&shellHostUI)).to_be_false();
		expect(currentUIMode).to_equal(uint32_t{UI_MODE_STEM_EXPORT});
	});

	it("leaves the mode alone when the drum creator's browser ends the visit: legacy opened no editor there", _ {
		(void)quiet_audio();
		ToggleGuard on{RuntimeFeatureStateToggle::On};
		RootUIGuard root_guard;
		deluge::scenes::spec::UiModeGuard mode;
		deluge::scenes::spec::SongFixture kit = deluge::scenes::spec::make_kit_song();
		setRootUILowLevel(&instrumentClipView);
		DrumRowStack row{kit};
		expect(row.stack != nullptr).to_be_true();
		if (row.stack == nullptr) {
			return;
		}
		instrumentClipView.enterDrumCreator(row.stack, /*doRecording=*/false);
		expect(isUIOpen(&shellHostUI)).to_be_true();
		(void)shellHostUI.timerCallback(); // the file selector's hook pushes the browser
		expect(shellHostUI.shell().stack().input_top().kind() == deluge::scenes::browser::kBrowserSceneKind)
		    .to_be_true();
		currentUIMode = UI_MODE_AUDITIONING; // an audition pad held over the browser

		(void)shellHostUI.exitUI(); // the browser goes; the menu goes up from its root on the resume
		for (int32_t i = 0; i < 3 && isUIOpen(&shellHostUI); i++) {
			(void)shellHostUI.timerCallback();
		}
		expect(isUIOpen(&shellHostUI)).to_be_false();
		expect(currentUIMode).to_equal(uint32_t{UI_MODE_AUDITIONING});
	});

	it("forgets that mark when the visit is torn down without its close: the next visit's close resets", _ {
		(void)quiet_audio();
		ToggleGuard on{RuntimeFeatureStateToggle::On};
		RootUIGuard root_guard;
		deluge::scenes::spec::UiModeGuard mode;
		deluge::scenes::spec::SongFixture kit = deluge::scenes::spec::make_kit_song();
		setRootUILowLevel(&instrumentClipView);
		DrumRowStack row{kit};
		expect(row.stack != nullptr).to_be_true();
		if (row.stack == nullptr) {
			return;
		}
		instrumentClipView.enterDrumCreator(row.stack, /*doRecording=*/false);
		expect(isUIOpen(&shellHostUI)).to_be_true();
		nullifyUIs(); // a song load takes every UI down with no close

		deluge::scenes::spec::SongFixture f = deluge::scenes::spec::make_synth_song();
		setRootUILowLevel(&instrumentClipView);
		expect(entry::openShellMenu(f.clip, 0, false, false, false, false)).to_be_true();
		currentUIMode = UI_MODE_AUDITIONING;
		(void)shellHostUI.exitUI();
		expect(currentUIMode).to_equal(uint32_t{UI_MODE_NONE});
	});
});

describe menu_entry_reopen("an entry that reaches the open guest re-roots it in place, as setup() re-sets the open editor", $ {
	it("re-roots the Settings menu on the pad's node when Shift + a pad is forwarded to the keyboard screen", _ {
		ToggleGuard on{RuntimeFeatureStateToggle::On};
		RootUIGuard root_guard;
		deluge::scenes::spec::UiModeGuard mode;
		deluge::scenes::spec::SongFixture f = deluge::scenes::spec::make_synth_song();
		(void)f;
		setRootUILowLevel(&keyboardScreen);
		expect(entry::openShellMenu(nullptr, 0, false, false, false, false)).to_be_true();
		expect(deluge::scenes::menu_scene().menu_context().root == menu::MenuContext::Root::Settings).to_be_true();
		{
			// `GridPadClaim::None` on the settings root: the guest forwards the pad to `keyboardScreen.padAction`, whose
			// first statement is the shortcut entry.
			HeldShift shift;
			(void)shellHostUI.padAction(8, 7, 64);
		}
		(void)shellHostUI.timerCallback(); // the `Replace` the re-root filed
		expect(getCurrentUI() == &shellHostUI).to_be_true();
		expect(deluge::scenes::menu_scene().menu_context().root == menu::MenuContext::Root::Sound).to_be_true();
		expect(deluge::scenes::menu_scene().focused_value() == &nodes::lpf_freq).to_be_true();

		// One guest on the hierarchy: one exit leaves the keyboard screen current.
		(void)shellHostUI.exitUI();
		expect(isUIOpen(&shellHostUI)).to_be_false();
		expect(getCurrentUI() == &keyboardScreen).to_be_true();
	});

	it("re-roots for a direct shortcut open too, keeping one guest", _ {
		ToggleGuard on{RuntimeFeatureStateToggle::On};
		RootUIGuard root_guard;
		deluge::scenes::spec::UiModeGuard mode;
		deluge::scenes::spec::SongFixture f = deluge::scenes::spec::make_synth_song();
		setRootUILowLevel(&instrumentClipView);
		expect(entry::openShellMenu(f.clip, 0, false, false, false, false)).to_be_true();
		expect(entry::openShellMenuAtShortcut(f.clip, 9, 7, false, false, false, false)).to_be_true();
		(void)shellHostUI.timerCallback();
		expect(deluge::scenes::menu_scene().focused_value() == &nodes::hpf_freq).to_be_true();
		(void)shellHostUI.exitUI();
		expect(isUIOpen(&shellHostUI)).to_be_false();
		expect(getCurrentUI() == &instrumentClipView).to_be_true();
	});
});

describe menu_entry_shortcut_whole("site 9 with ShellMenu on: the port answers the press whole, and legacy never opens", $ {
	it("says PARAMETER NOT APPLICABLE for a pad that does not apply, and the DX7 test reads the pad's own source", _ {
		// A DX7 source an earlier menu left in legacy's mirror: legacy's fallback tests it and opens its `dxParam`.
		ToggleGuard on{RuntimeFeatureStateToggle::On};
		RootUIGuard root_guard;
		deluge::scenes::spec::UiModeGuard mode;
		PopupSpy spy;
		DisplayGuard shown{spy};
		deluge::scenes::spec::SongFixture f = deluge::scenes::spec::make_synth_song();
		Sound* sound = static_cast<Sound*>(static_cast<ModControllableAudio*>(f.instrument->toModControllable()));
		sound->sources[0].setOscType(OscType::SAW);
		sound->sources[1].setOscType(OscType::DX7);
		Source* const saved_source = soundEditor.currentSource;
		soundEditor.currentSource = &sound->sources[1];
		setRootUILowLevel(&instrumentClipView);
		{
			HeldShift shift;
			expect(soundEditor.potentialShortcutPadAction(4, 0, true) == ActionResult::DEALT_WITH).to_be_true();
		}
		expect(isUIOpen(&soundEditor)).to_be_false();
		expect(isUIOpen(&shellHostUI)).to_be_false();
		expect(spy.last_popup).to_equal(text(deluge::l10n::String::STRING_FOR_PARAMETER_NOT_APPLICABLE));
		soundEditor.currentSource = saved_source;
		sound->sources[1].setOscType(OscType::SAW);
		nullifyUIs();
	});

	it("says Feature not (yet?) implemented for a coming-soon cell and opens nothing", _ {
		ToggleGuard on{RuntimeFeatureStateToggle::On};
		RootUIGuard root_guard;
		deluge::scenes::spec::UiModeGuard mode;
		PopupSpy spy;
		DisplayGuard shown{spy};
		deluge::scenes::spec::SongFixture f = deluge::scenes::spec::make_synth_song();
		(void)f;
		setRootUILowLevel(&instrumentClipView);
		{
			HeldShift shift;
			expect(soundEditor.potentialShortcutPadAction(4, 2, true) == ActionResult::DEALT_WITH).to_be_true();
		}
		expect(isUIOpen(&soundEditor)).to_be_false();
		expect(isUIOpen(&shellHostUI)).to_be_false();
		expect(spy.last_popup).to_equal(text(deluge::l10n::String::STRING_FOR_UNIMPLEMENTED));
	});

	it("says nothing at all on a kit with neither affect-entire nor a selected row, even on a coming-soon cell", _ {
		ToggleGuard on{RuntimeFeatureStateToggle::On};
		RootUIGuard root_guard;
		deluge::scenes::spec::UiModeGuard mode;
		PopupSpy spy;
		DisplayGuard shown{spy};
		deluge::scenes::spec::SongFixture kit = deluge::scenes::spec::make_kit_song();
		(void)kit;
		setRootUILowLevel(&instrumentClipView);
		{
			HeldShift shift;
			expect(soundEditor.potentialShortcutPadAction(4, 2, true) == ActionResult::DEALT_WITH).to_be_true();
		}
		expect(isUIOpen(&soundEditor)).to_be_false();
		expect(isUIOpen(&shellHostUI)).to_be_false();
		expect(spy.last_popup).to_equal(std::string{});
	});

	it("reads the song view's table from the performance view and edits the song, as legacy's call does", _ {
		ToggleGuard on{RuntimeFeatureStateToggle::On};
		RootUIGuard root_guard;
		deluge::scenes::spec::UiModeGuard mode;
		deluge::scenes::spec::SongFixture f = deluge::scenes::spec::make_synth_song();
		(void)f;
		setRootUILowLevel(&performanceView);
		{
			HeldShift shift;
			(void)soundEditor.potentialShortcutPadAction(8, 7, true);
		}
		expect(isUIOpen(&soundEditor)).to_be_false();
		expect(getCurrentUI() == &shellHostUI).to_be_true();
		expect(deluge::scenes::menu_scene().menu_context().root == menu::MenuContext::Root::Performance).to_be_true();
		expect(deluge::scenes::menu_scene().focused_value() == &nodes::global_lpf_freq).to_be_true();
		closeGuest();
	});

	it("is the open guest's to answer even once the setting selects the legacy menu: it re-roots, legacy stays shut", _ {
		ToggleGuard on{RuntimeFeatureStateToggle::On};
		RootUIGuard root_guard;
		deluge::scenes::spec::UiModeGuard mode;
		deluge::scenes::spec::SongFixture f = deluge::scenes::spec::make_synth_song();
		(void)f;
		setRootUILowLevel(&keyboardScreen);
		expect(entry::openShellMenu(nullptr, 0, false, false, false, false)).to_be_true();
		{
			ToggleGuard off{RuntimeFeatureStateToggle::Off};
			HeldShift shift;
			(void)shellHostUI.padAction(8, 7, 64);
		}
		expect(isUIOpen(&soundEditor)).to_be_false();
		expect(getCurrentUI() == &shellHostUI).to_be_true();
		closeGuest();
	});

	it("passes a release, a sidebar pad and an ungated press back to the view", _ {
		ToggleGuard on{RuntimeFeatureStateToggle::On};
		RootUIGuard root_guard;
		deluge::scenes::spec::UiModeGuard mode;
		deluge::scenes::spec::SongFixture f = deluge::scenes::spec::make_synth_song();
		(void)f;
		setRootUILowLevel(&instrumentClipView);
		expect(soundEditor.potentialShortcutPadAction(8, 7, true) == ActionResult::NOT_DEALT_WITH).to_be_true();
		HeldShift shift;
		expect(soundEditor.potentialShortcutPadAction(8, 7, false) == ActionResult::NOT_DEALT_WITH).to_be_true();
		expect(soundEditor.potentialShortcutPadAction(kDisplayWidth, 3, true) == ActionResult::NOT_DEALT_WITH).to_be_true();
		expect(isUIOpen(&shellHostUI)).to_be_false();
	});
});
// clang-format on

DELUGE_SPEC(menu_entry_spec, menu_entry_toggle, menu_entry_roots, menu_entry_node_arg, menu_entry_shortcut,
            menu_entry_refusal, menu_entry_undo_log, menu_entry_screensaver, menu_entry_tick_rate,
            menu_entry_midi_learn, menu_entry_automation_notes, menu_entry_close_mode, menu_entry_reopen,
            menu_entry_shortcut_whole)
