/// @file
/// @brief The menu's entry, in place of the fifteen `openUI(&soundEditor)` sites, driven
///        against the real application globals.
///
/// `gui/ui/menu_entry.cpp` carries the fifteen-site table; this file drives the routed
/// ones, one example per *argument shape*. It is at this level and not
/// in `tests/unit/deluge/gui/` for the reason `boot_wiring_spec.cpp` gives: the host suite deliberately
/// never builds the `deluge_ui_scenes` closure, and everything under test here is the join between the
/// two layers -- `menu::context_for`'s root against `MenuScene::roots()`, a stable node id against
/// `SceneId::arg`, and `openUI(&shellHostUI)` against the real `uiNavigationHierarchy`.
///
/// @note **What this file cannot cover, stated rather than implied.** It drives `openMenu`,
///       which is what the gated sites call; it does **not** drive the sites themselves (site 15's column
///       aside), because
///       reaching `AudioClipView::buttonAction` here would need the whole `src/deluge/gui/views` stack. So
///       "the site passes the right arguments" is not covered here: like the `deluge/deluge.cpp` boot calls, it is
///       left to a level-3 A/B run. Each example names the site whose
///       argument shape it replays so the two can be compared by eye in the meantime.
///
/// @note **Every example puts the process back.** These are shared globals -- `numUIsOpen`,
///       `currentSong`, `runtimeFeatureSettings` -- and twenty other specs run in this binary.
///       `nullifyUIs()` is the teardown for the UI hierarchy: it is legacy's own "no UI is open"
///       and, unlike `closeUI`, it is well defined when the guest is the only thing on the stack.

#include "gui/ui/menu_view_of.h"
#include "instruments/classic/params.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "processing/sound/sound_drum.h"
#include "project/device_settings.h"
#include "support/peak_seed.h"
#include "support/sound_fixtures.h"
#include "ui/scenes/browser/browser_scene.h"
#include "ui/scenes/marker/marker_scene.h"
#include "ui/scenes/menu/chains.h"
#include "ui/scenes/menu/context.h"
#include "ui/scenes/menu/menu_scene.h"
#include "ui/scenes/menu/node_id.h"
#include "ui/scenes/menu/nodes/nodes.h"
#include "ui/scenes/menu/shortcuts.h"
#include "ui/scenes/profile.h"
#include "ui/scenes/registry.h"
#include "ui/scenes/rename/rename_scene.h"

#include "ui/shell/scene.h"
#include "ui/shell/shell.h"

#include "gui/ui/browser_host.h"
#include "gui/ui/shell_drum_creator.h"
#include "gui/ui/shell_guest_services.h"
#include "gui/ui/shell_host_ui.h"
#include "gui/ui/shell_note_bridge.h"
#include "gui/ui/ui.h"
#include "util/menu_entry.h"
#include "util/sound_menu_open.h"

#include "base/notify/dirty_set.h"
#include "gui/l10n/l10n.h"
#include "gui/ui/keyboard/column_controls/dx.h"
#include "gui/ui/keyboard/keyboard_screen.h"
#include "gui/ui_timer_manager.h"
#include "gui/views/audio_clip_view.h"
#include "gui/views/automation_view.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/performance_view.h"
#include "gui/views/session_view.h"
#include "gui/views/view.h"
#include "hid/button.h"
#include "hid/display/oled.h"
#include "hid/display/screensaver.h"
#include "io/midi/learned_midi.h"
#include "io/midi/midi_device.h"
#include "io/midi/midi_engine.h"
#include "memory/heaps.h"
#include "model/action/action_logger.h"
#include "model/clip/instrument_clip.h"
#include "model/edits/setting_edit.h"
#include "model/instrument/kit.h"
#include "model/project_bridge/slot_bridge.h"
#include "model/sample/sample.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound.h"
#include "processing/sound/source_resources.h"
#include "storage/cluster/cluster.h"
#include "storage/flash_storage.h"
#include "storage/multi_range/multisample_range.h"

#include "support/card_access.h"
#include "support/feature_setting.h"
#include "support/printers/scene_printer.h"
#include "support/shortcut_layout.h"
#include "support/song_fixture.h"
#include "support/ui_mode_guard.h"

#include "cppspec.hpp"
#include "support/matchers/value_matchers.h"
#include "support/record_state.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <optional>
#include <string>

#include "support/menu_context.h"
#include "support/spec_main.h"
#include "unit/ui/widgets/support/frames.h"

/// @name Legacy's shift state, reached by bare `extern`: `Buttons::isShiftButtonPressed` reads
///       these (`hid/buttons.cpp`) and the header does not export them.
/// @{
namespace Buttons {
extern bool shiftCurrentlyPressed;
extern bool shiftCurrentlyStuck;
} // namespace Buttons
/// @}

namespace menu = deluge::ui::scenes::menu;
namespace nodes = deluge::ui::scenes::menu::nodes;
namespace shell = deluge::ui::shell;
namespace entry = deluge::gui::ui;
namespace spec = deluge::ui::scenes::spec;

namespace {

/// @brief Put the UI hierarchy back to "nothing open" after an example opened the guest.
///
/// main's `closeUI` (`origin/main:src/deluge/gui/ui/ui.cpp`, whose loop stops at `u >= 1`) would index
/// `uiNavigationHierarchy[-1]` when the guest is the only entry (ours, in `gui/ui/ui.cpp`, returns without closing),
/// so this is `nullifyUIs()`, which is what
/// legacy itself calls when it tears the hierarchy down (`nullifyUIs`). That skips the close, so the kit-global-FX
/// latch the close would clear is cleared here: the next example starts with no history.
void closeGuest() {
	nullifyUIs();
	entry::clearMenuKitGlobalFx();
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

/// @brief Open the guest focused on `nodes::midi_command_play`, the way `openMenu` opens the
///        menu -- `MenuScene::open`, then `setInitialScene`, then `openUI`
///        (`gui/ui/menu_entry.cpp`).
///
/// Not through `openMenu` itself, because that resolves a *root* from the calling site's
/// arguments and `MenuNode` names only two leaves, neither of them a MIDI command. What these
/// examples need is a focused item whose `learn_note_on` writes something -- `MidiCommandItem`
/// (in `ui/scenes/menu/nodes/midi_command.cpp`) is the only ported one that does.
///
/// The node is named through `SceneId::arg`, not through `open()`'s root argument, and that is the
/// warning `MenuScene::open` carries: `on_enter` re-roots from `arg`, or from `ctx_.root` when
/// `arg == 0`, one call later -- so passing the leaf as the "root" and a zero `arg` lands on the
/// settings root instead.
/// @brief `spec::QuietAudio`, held from the first example that can leave a card listing behind.
///
/// **The site-8 example really opens the browser scene**, because `MenuScene::on_enter` performs the
/// `Source::getOrCreateFirstRange()` legacy's permission check performs
/// (`SoundEditor::checkPermissionToBeginSessionForRangeSpecificParam`
/// (`origin/main:src/deluge/gui/ui/sound_editor.cpp`)) -- so `browser::sample_can` stops refusing and the scene reads
/// `SAMPLES/` off the fixture card, which holds a file. The latch keeps the audio routine still while that listing is
/// built and emptied (`support/card_access.h`).
///
/// Process-scoped, function-local rather than namespace-scope: this binary links every spec and
/// runs one per invocation, so a namespace-scope instance would silently hold the audio routine
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
	int32_t cancels = 0;

	using deluge::hid::Display::displayPopup;
	constexpr size_t getNumBrowserAndMenuLines() override { return 3; }
	void displayPopup(char const* text, int8_t, bool, uint8_t, int32_t, PopupType) override { last_popup = text; }
	void popupText(char const*, PopupType) override {}
	void popupTextTemporary(char const*, PopupType) override {}
	void cancelPopup() override { cancels++; }
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
/// @brief A sample the marker editor can open on, without a card: `marker_scene_spec.cpp`'s fixture, cut to what the
///        editor's open asks of it (a length, and a peak index every column reads from).
Sample* make_marker_sample() {
	constexpr size_t kClusterSize = 32768;
	constexpr int32_t kBytesPerFrame = 2;
	constexpr uint64_t kLength = static_cast<uint64_t>(kClusterSize) * 32 / kBytesPerFrame;
	constexpr int32_t kClusters = 32;
	Cluster::set_size(kClusterSize);
	// Placed in the deluge heap and never freed -- `song_fixture.h`'s own rule, and for its reason.
	auto* s = new (deluge::memory::alloc_fast(sizeof(Sample))) Sample();
	s->byteDepth = 2;
	s->numChannels = 1;
	s->sampleRate = 44100;
	s->audioDataStartPosBytes = 0;
	s->audioDataLengthBytes = kLength * kBytesPerFrame;
	s->lengthInSamples = kLength;
	peak_seed::seed_peaks(
	    *s, [&](int32_t i) { return std::pair<int8_t, int8_t>{static_cast<int8_t>(-16), static_cast<int8_t>(16)}; });
	return s;
}

/// @brief Give @p sound's source 0 one range holding @p sample, recorded as a load records it.
void give_drum_a_sample(deluge::ui::scenes::spec::SongFixture& f, Sound& sound, Sample* sample) {
	SourceResources& source = sound.sourceResources[0];
	deluge::test::set(*sound.state_, "osc1/type", "sample");
	deluge::test::set(*sound.state_, "osc1/loopMode", "1"); // ONCE
	sound.deriveConfig();
	auto* range = static_cast<MultisampleRange*>(sound.getOrCreateFirstRange(0));
	range->sampleHolder.audioFile = sample;
	range->sampleHolder.startPos = 0;
	range->sampleHolder.endPos = sample->lengthInSamples;
	deluge::bridge::recordLoadedSound(sound, *f.song);
}

/// @brief A kit clip with affect-entire on, one sound drum selected, and the latch set by a clip-view edit pad pressed
///        with the menu closed, as legacy's first statement sets `setupKitGlobalFXMenu`.
struct LatchedKit {
	deluge::ui::scenes::spec::SongFixture f = deluge::ui::scenes::spec::make_kit_song();
	SoundDrum* drum = deluge::ui::scenes::spec::add_drum<SoundDrum>(f);
	LatchedKit() {
		f.clip->affectEntire = true;
		setRootUILowLevel(&instrumentClipView);
		deluge::gui::ui::clearMenuKitGlobalFx();
		(void)instrumentClipView.padAction(0, 0, 64);
		(void)instrumentClipView.padAction(0, 0, 0);
		// An audition selects the drum's row again; the audition column never reaches the shortcut entry.
		static_cast<Kit*>(f.instrument)->selectedDrum = drum;
	}
};

/// @brief SHIFT held for the guest and for legacy, and a Shift + (@p x, @p y) press and release the guest takes.
void guest_shortcut(int32_t x, int32_t y) {
	(void)shellHostUI.buttonAction(deluge::hid::button::SHIFT, true);
	(void)shellHostUI.padAction(x, y, 64);
	for (int i = 0; i < 3; i++) {
		(void)shellHostUI.timerCallback();
	}
	(void)shellHostUI.padAction(x, y, 0);
	(void)shellHostUI.buttonAction(deluge::hid::button::SHIFT, false);
	(void)shellHostUI.timerCallback();
}

struct DrumRowStack {
	alignas(ModelStack) char memory[MODEL_STACK_MAX_SIZE];
	ModelStackWithNoteRow* stack = nullptr;
	explicit DrumRowStack(deluge::ui::scenes::spec::SongFixture& f) {
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
	const menu::MenuContext ctx = spec::ctx_of(song, deluge::gui::menuViewOf(nullptr), 0, false, false, false, false);
	const uint16_t arg = menu::node_id(nodes::midi_command_play);
	if (arg == 0) {
		return false;
	}
	deluge::ui::scenes::menu_scene().open(nodes::root_settings, ctx);
	shellHostUI.setInitialScene(shell::SceneId{.kind = shell::SceneKind::FirstApp, .arg = arg});
	return openUI(&shellHostUI);
}

/// @brief LEARN pressed or released on the guest's menu, with nothing else held.
void press_learn(bool on) {
	const shell::ButtonSet held{};
	const deluge::ui::widgets::core::PadMask pads{};
	const shell::UiContext ui{held, pads, false, false, 0, deluge::ui::scenes::deluge_profile()};
	(void)deluge::ui::scenes::menu_scene().handle(
	    shell::UiEvent{.kind = shell::UiEvent::Kind::Button, .on = on, .button = shell::ButtonId::Learn}, ui);
}

} // namespace

// clang-format off
describe menu_entry_roots("openMenu resolves the root each site's arguments name", $ {
	it("site 1 (View::buttonAction, SHIFT + SELECT) opens the settings root", _ {
		deluge::ui::scenes::spec::SongFixture f = deluge::ui::scenes::spec::make_synth_song();
		(void)f;

		// All four UI facts false -- SHIFT is held at that site, so `setup()`'s two view tests are
		// false and the root is `settingsRootMenu` (`origin/main:src/deluge/gui/ui/sound_editor.cpp`).
		expect(entry::openMenu(nullptr, 0, false, false, false, false)).to_be_true();
		expect(getCurrentUI()).to_equal(&shellHostUI);
		expect(deluge::ui::scenes::menu_scene().focused()).to_equal(&nodes::root_settings);
		// `arg == 0` is "the root the context chose" (`MenuScene::on_enter`'s `arg == 0` branch, `ui/scenes/menu/menu_scene.cpp`).
		expect(opened_on()).to_equal(shell::SceneId{.kind = shell::SceneKind::FirstApp, .arg = 0});
		closeGuest();
	});

	it("sites 2 and 3 (session and arranger) open the SAME song root, differing only in is_session_view", _ {
		deluge::ui::scenes::spec::SongFixture f = deluge::ui::scenes::spec::make_synth_song();
		(void)f;

		// The pair is one example on purpose: `sessionViewRoot` is the ONLY argument that differs
		// between the two sites, and `MenuContext::is_session_view` is the only thing it feeds. An
		// example that drove one site alone would pass with the field hard-coded either way.
		expect(entry::openMenu(nullptr, 0, false, /*sessionOrArrangerRoot=*/true,
		                            /*sessionViewRoot=*/true, false))
		    .to_be_true();
		expect(deluge::ui::scenes::menu_scene().focused()).to_equal(&nodes::root_song);
		expect(deluge::ui::scenes::menu_scene().menu_context().is_session_view).to_be_true();
		closeGuest();

		expect(entry::openMenu(nullptr, 0, false, /*sessionOrArrangerRoot=*/true,
		                            /*sessionViewRoot=*/false, false))
		    .to_be_true();
		expect(deluge::ui::scenes::menu_scene().focused()).to_equal(&nodes::root_song);
		expect(deluge::ui::scenes::menu_scene().menu_context().is_session_view).to_be_false();
		closeGuest();
	});

	it("site 4 (PerformanceView) opens the performance root", _ {
		deluge::ui::scenes::spec::SongFixture f = deluge::ui::scenes::spec::make_synth_song();
		(void)f;

		// Legacy names `&soundEditorRootMenuPerformanceView` by pointer; the port names the fact.
		// The example varies only `performanceView` against the settings-root call above, so a
		// mapping that ignored the argument would fail here rather than pass by coincidence.
		expect(entry::openMenu(nullptr, 0, /*performanceView=*/true, false, false, false)).to_be_true();
		expect(deluge::ui::scenes::menu_scene().focused()).to_equal(&nodes::root_performance);
		closeGuest();
	});

	it("site 5 (AudioClipView) opens the audio-clip root", _ {
		deluge::ui::scenes::spec::AudioSongFixture f = deluge::ui::scenes::spec::make_audio_clip_song();

		expect(entry::openMenu(f.clip, 0, false, false, false, false)).to_be_true();
		expect(deluge::ui::scenes::menu_scene().focused()).to_equal(&nodes::root_audio_clip);
		closeGuest();
	});

	it("site 6 (InstrumentClipMinder) picks the root from the clip, not from the site", _ {

		// **The clip is what varies.** Same call shape, three different songs -- so "always the
		// sound root" and "always whatever the last call resolved" both fail.
		deluge::ui::scenes::spec::SongFixture synth = deluge::ui::scenes::spec::make_synth_song();
		expect(entry::openMenu(synth.clip, 0, false, false, false, false)).to_be_true();
		expect(deluge::ui::scenes::menu_scene().focused()).to_equal(&nodes::sound_root);
		closeGuest();

		deluge::ui::scenes::spec::SongFixture midi = deluge::ui::scenes::spec::make_midi_song();
		expect(entry::openMenu(midi.clip, 0, false, false, false, false)).to_be_true();
		expect(deluge::ui::scenes::menu_scene().focused()).to_equal(&nodes::root_midi_or_cv);
		closeGuest();

		// And the kit-global-FX argument, which is the one fact the site computes for itself
		// (`KIT && affectEntire`).
		deluge::ui::scenes::spec::SongFixture kit = deluge::ui::scenes::spec::make_kit_song();
		static_cast<InstrumentClip*>(kit.clip)->affectEntire = true;
		expect(entry::openMenu(kit.clip, 0, false, false, false, /*kitGlobalFx=*/true)).to_be_true();
		expect(deluge::ui::scenes::menu_scene().focused()).to_equal(&nodes::root_kit_global_fx);
		closeGuest();
	});

	it("site 7 (AutomationView on the arranger) opens the song root from a null clip", _ {
		deluge::ui::scenes::spec::SongFixture f = deluge::ui::scenes::spec::make_synth_song();
		(void)f;

		// `onArrangerView` is both halves of that site's mapping: it is why the clip is null and it
		// is `!rootUIIsClipMinderScreen()`. `sessionViewRoot` stays false -- the automation view is
		// not the session view -- which is what separates this from site 2.
		expect(entry::openMenu(nullptr, 0, false, /*sessionOrArrangerRoot=*/true, false, false)).to_be_true();
		expect(deluge::ui::scenes::menu_scene().focused()).to_equal(&nodes::root_song);
		expect(deluge::ui::scenes::menu_scene().menu_context().is_session_view).to_be_false();
		closeGuest();
	});

	it("a UI teardown empties the guest and forgets the song the menu was open on", _ {
		deluge::ui::scenes::spec::SongFixture f = deluge::ui::scenes::spec::make_synth_song();
		expect(entry::openMenu(f.clip, 0, false, false, false, false)).to_be_true();
		expect(deluge::ui::scenes::menu_scene().menu_context().model).not_().to_be_null();
		// A song load or a clear takes every UI down (`nullifyUIs`) before the song the menu was open on goes.
		nullifyUIs();
		expect(shellHostUI.shell().stack().depth()).to_equal(size_t{0});
		expect(deluge::ui::scenes::menu_scene().menu_context().model).to_be_null();
		closeGuest();
	});

	it("a UI teardown with the guest closed still forgets the song the menu was last open on", _ {
		// The context outlives the stack (the close paths read its root), so a song load or a clear made after the
		// menu closed -- a MIDI NEXT_SONG from the clip view -- must not leave it naming the retired song.
		deluge::ui::scenes::spec::SongFixture f = deluge::ui::scenes::spec::make_synth_song();
		setRootUILowLevel(&instrumentClipView);
		expect(entry::openMenu(f.clip, 0, false, false, false, false)).to_be_true();
		shellHostUI.closeNow(); // the guest's own close, the clip view beneath
		expect(isUIOpen(&shellHostUI)).to_be_false();
		expect(deluge::ui::scenes::menu_scene().menu_context().model).to_equal(&f.song->project);
		nullifyUIs();
		expect(deluge::ui::scenes::menu_scene().menu_context().model).to_be_null();
		entry::clearMenuKitGlobalFx();
	});

	it("a UI teardown forgets the drum creator's drum, so no later close deletes it", _ {
		// The drum belongs to the kit a song load may be about to free: the guest's close arm, run after the teardown,
		// must find no drum of the creator's to delete.
		RootUIGuard root_guard;
		deluge::ui::scenes::spec::SongFixture f = deluge::ui::scenes::spec::make_kit_song();
		(void)deluge::ui::scenes::spec::add_drum<SoundDrum>(f);
		// The creator's new drum, which no row plays yet: the close arm deletes such a drum when no file was chosen.
		auto* const kit = static_cast<Kit*>(f.instrument);
		auto* const drum = deluge::ui::scenes::spec::detail::place<SoundDrum>();
		ParamManagerForTimeline params;
		params.setupWithPatching();
		Sound::initParams(&params);
		drum->setupAsSample(&params);
		f.song->live.param_backup.backUp(drum, f.clip, &params, true);
		kit->addDrum(drum);
		expect(kit->getFirstUnassignedDrum(static_cast<InstrumentClip*>(f.clip))).to_equal(drum);
		setRootUILowLevel(&instrumentClipView);
		expect(entry::openShellDrumCreator(static_cast<InstrumentClip*>(f.clip), drum)).to_be_true();
		nullifyUIs();
		entry::discardShellDrumCreatorBeforeClose();
		bool held = false;
		for (const Drum* d = kit->firstDrum; d != nullptr; d = d->next) {
			held = held || d == drum;
		}
		expect(held).to_be_true();
		closeGuest();
	});
});

describe menu_entry_node_arg("openMenu can name a node, not only a root", $ {
	it("site 8 opens on nodes::file0_selector through a non-zero SceneId::arg", _ {
		// The first example in this file that reads the card: the item's `on_focus` pushes
		// `BrowserScene`, which lists `SAMPLES/`. See `quiet_audio()` for why that needs the latch.
		(void)quiet_audio();
		deluge::ui::scenes::spec::SongFixture f = deluge::ui::scenes::spec::make_synth_song();
		(void)f;

		// Legacy's undo-log wipe is in `setup()`'s `item == nullptr` arm only, so a call that NAMES a
		// node must not wipe. Seeded with the guest open, because `getNewAction` refuses otherwise.
		expect(entry::openMenu(nullptr, 0, false, false, false, false)).to_be_true();
		actionLogger.deleteAllLogs();
		(void)actionLogger.getNewAction(ActionType::MISC, ActionAddition::NOT_ALLOWED);
		expect(actionLogger.firstAction[BEFORE]).not_().to_be_null();
		closeGuest();

		const uint16_t id = menu::node_id(nodes::file0_selector);
		// The id is what makes the mapping possible at all, and it is `ui::scenes::assigned_node_ids()`
		// that produces it. Asserted non-zero here because zero means "the context's
		// root" downstream, which would silently open the sound root instead.
		expect(id).not_().to_equal(0);

		expect(entry::openMenu(f.clip, 0, false, false, false, false,
		                            entry::MenuNode::Osc0FileSelector))
		    .to_be_true();
		expect(opened_on()).to_equal(shell::SceneId{.kind = shell::SceneKind::FirstApp, .arg = id});
		// And it landed there rather than merely being asked to: `MenuScene::on_enter` resolves the
		// arg through `node_by_id` and re-roots the path at it (`MenuScene::on_enter`).
		expect(deluge::ui::scenes::menu_scene().focused()).to_equal(&nodes::file0_selector);
		// ...and the log survived, which is the half a `clip != nullptr` test alone would get wrong.
		expect(actionLogger.firstAction[BEFORE]).not_().to_be_null();
		closeGuest();
	});

	it("site 14 never falls back to the legacy recorder or browser: a refused guest says setup()'s refusal", _ {
		// A boot numbers every node, so the only way to a refused guest is an id missing: numbered over the song root
		// alone, neither the recorder item nor the file selector has one.
		(void)quiet_audio();
		RootUIGuard root_guard;
		deluge::ui::scenes::spec::UiModeGuard mode;
		PopupSpy spy;
		DisplayGuard shown{spy};
		for (const bool take : {true, false}) {
			deluge::ui::scenes::spec::SongFixture f = deluge::ui::scenes::spec::make_kit_song();
			setRootUILowLevel(&instrumentClipView);
			DrumRowStack row{f};
			expect(row.stack).not_().to_be_null();
			if (row.stack == nullptr) {
				break;
			}
			const menu::Node* const neither[] = {&nodes::root_song};
			(void)menu::assign_node_ids(neither);
			spy.last_error.reset();
			instrumentClipView.enterDrumCreator(row.stack, take);
			(void)menu::assign_node_ids(nodes::kAllRoots);
			expect(isUIOpen(&shellHostUI)).to_be_false();
			expect(spy.last_error).to_equal(Error::UNSPECIFIED);
			nullifyUIs();
		}
	});

	it("cancels the popup legacy's display holds as it opens, as setup() does", _ {
		// An audition leaves the drum's name up as a persistent popup (`InstrumentClipView::drawDrumName`); RECORD then
		// opens the recorder over it, and `setup()`'s `display->cancelPopup()` takes it down.
		deluge::ui::scenes::spec::SongFixture f = deluge::ui::scenes::spec::make_synth_song();
		deluge::hid::display::OLED oled_display;
		deluge::hid::Display* const saved_display = display;
		display = &oled_display;
		display->popupText("KICK");
		expect(display->hasPopup()).to_be_true();
		expect(entry::openMenu(f.clip, 0, false, false, false, false)).to_be_true();
		expect(display->hasPopup()).to_be_false();
		closeGuest();
		display = saved_display;
	});

	it("site 11 opens DX7 source on the new FM synth, keeping the undo log", _ {
		// `InstrumentClipMinder::createNewInstrument`'s `soundEditor.setup(clip, &dxMenu, 0)`: a named item, so no wipe.
		deluge::ui::scenes::spec::SongFixture f = deluge::ui::scenes::spec::make_synth_song();
		Sound* sound = deluge::model::effects_owner_of(deluge::model::owner_of(*f.instrument))->sound;
		deluge::test::setOscType(*sound, 0, OscType::DX7);
		expect(entry::openMenu(nullptr, 0, false, false, false, false)).to_be_true();
		actionLogger.deleteAllLogs();
		(void)actionLogger.getNewAction(ActionType::MISC, ActionAddition::NOT_ALLOWED);
		closeGuest();

		expect(entry::openMenu(f.clip, 0, false, false, false, false, entry::MenuNode::Dx)).to_be_true();
		expect(opened_on())
		    .to_equal(shell::SceneId{.kind = shell::SceneKind::FirstApp, .arg = menu::node_id(nodes::dx_menu)});
		expect(deluge::ui::scenes::menu_scene().focused()).to_equal(&nodes::dx_menu);
		expect(actionLogger.firstAction[BEFORE]).not_().to_be_null();
		closeGuest();
	});

	it("site 15 opens the DX7 parameter page on the column's operator, its pad blinking, keeping the undo log", _ {
		// `DXColumn::handlePad`'s Shift arm: `openForOpOrGlobal`'s `setup(getCurrentClip(), &dxParam, 0)`.
		deluge::ui::scenes::spec::SongFixture f = deluge::ui::scenes::spec::make_synth_song();
		Sound* sound = deluge::model::effects_owner_of(deluge::model::owner_of(*f.instrument))->sound;
		deluge::test::setOscType(*sound, 0, OscType::DX7);
		f.song->setCurrentClip(f.clip);
		expect(entry::openMenu(nullptr, 0, false, false, false, false)).to_be_true();
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
		expect(opened_on())
		    .to_equal(shell::SceneId{.kind = shell::SceneKind::FirstApp, .arg = menu::node_id(nodes::dx_param)});
		expect(deluge::ui::scenes::menu_scene().focused()).to_equal(&nodes::dx_param);
		expect(static_cast<int32_t>(deluge::ui::scenes::menu_scene().state().dx.param)).to_equal(2 * 21 + 16);
		expect(deluge::ui::scenes::menu_scene().state().dx.pad_blink).to_be_true();
		expect(actionLogger.firstAction[BEFORE]).not_().to_be_null();
		// With the guest open the press is the menu's: the same operator again toggles to its coarse tuning.
		expect(entry::openShellDxColumn(2)).to_be_true();
		expect(static_cast<int32_t>(deluge::ui::scenes::menu_scene().state().dx.param)).to_equal(2 * 21 + 18);
		closeGuest();
	});

	it("the same arguments without the node open the sound root instead", _ {
		// The control for the example above: everything but `node` held fixed, so "the file
		// selector is where a synth clip always lands" fails.
		deluge::ui::scenes::spec::SongFixture f = deluge::ui::scenes::spec::make_synth_song();

		expect(entry::openMenu(f.clip, 0, false, false, false, false)).to_be_true();
		expect(opened_on()).to_equal(shell::SceneId{.kind = shell::SceneKind::FirstApp, .arg = 0});
		expect(deluge::ui::scenes::menu_scene().focused()).to_equal(&nodes::sound_root);
		closeGuest();
	});
});

describe menu_entry_shortcut("site 9: a shortcut pad from a clip view opens the menu on that pad's node", $ {
	it("Shift + (8,7) on a synth clip opens the filters group focused on nodes::lpf_freq through SceneId::arg", _ {
		const spec::ShortcutLayout pinned_v3{deluge::project::ShortcutsVersion::V3}; // the layout the lookups name
		// `SoundEditor::setup(clip, item, …)` lands a named item on the horizontal page that lists it, focused on it
		// (`maybeGetParentMenu` and `parent->focusChild`, `origin/main:src/deluge/gui/ui/sound_editor.cpp`).
		deluge::ui::scenes::spec::SongFixture f = deluge::ui::scenes::spec::make_synth_song();
		expect(menu::shortcut_node(menu::MenuContext::Root::Sound, deluge::project::ShortcutsVersion::V3, false, 8, 7)).to_equal(&nodes::lpf_freq);

		expect(entry::openMenuAtShortcut(f.clip, 8, 7, false, false, false, false)).to_be_true();
		expect(getCurrentUI()).to_equal(&shellHostUI);
		expect(opened_on())
		    .to_equal(shell::SceneId{.kind = shell::SceneKind::FirstApp, .arg = menu::node_id(nodes::filters_group)});
		expect(deluge::ui::scenes::menu_scene().focused()).to_equal(&nodes::filters_group);
		expect(deluge::ui::scenes::menu_scene().focused_value()).to_equal(&nodes::lpf_freq);
		expect(deluge::ui::scenes::menu_scene().menu_context().source_index).to_equal(int8_t{0});
		closeGuest();
	});

	it("an odd column addresses source 1", _ {
		const spec::ShortcutLayout pinned_v3{deluge::project::ShortcutsVersion::V3}; // the layout the lookups name
		deluge::ui::scenes::spec::SongFixture f = deluge::ui::scenes::spec::make_synth_song();
		const menu::Node* node = menu::shortcut_node(menu::MenuContext::Root::Sound, deluge::project::ShortcutsVersion::V3, false, 3, 2);
		expect(node).not_().to_be_null();

		expect(entry::openMenuAtShortcut(f.clip, 3, 2, false, false, false, false)).to_be_true();
		expect(is_horizontal_page(deluge::ui::scenes::menu_scene().focused())).to_be_true();
		expect(deluge::ui::scenes::menu_scene().focused_value()).to_equal(node);
		expect(deluge::ui::scenes::menu_scene().menu_context().source_index).to_equal(int8_t{1});
		closeGuest();
	});

	it("a cell with no shortcut opens nothing", _ {
		const spec::ShortcutLayout pinned_v3{deluge::project::ShortcutsVersion::V3}; // the layout the lookups name
		deluge::ui::scenes::spec::SongFixture f = deluge::ui::scenes::spec::make_synth_song();
		expect(menu::shortcut_node(menu::MenuContext::Root::Sound, deluge::project::ShortcutsVersion::V3, false, 15, 0)).to_be_null();

		expect(entry::openMenuAtShortcut(f.clip, 15, 0, false, false, false, false)).to_be_false();
		expect(isUIOpen(&shellHostUI)).to_be_false();
	});

	it("a node that does not apply opens nothing and says PARAMETER NOT APPLICABLE, as setup() does", _ {
		const spec::ShortcutLayout pinned_v3{deluge::project::ShortcutsVersion::V3}; // the layout the lookups name
		PopupSpy spy;
		DisplayGuard shown{spy};
		deluge::ui::scenes::spec::SongFixture f = deluge::ui::scenes::spec::make_synth_song();
		// The first even-column pad whose node does not apply to this synth.
		const menu::MenuContext ctx =
		    spec::ctx_of(*f.song, deluge::gui::menuViewOf(f.clip), 0, false, false, false, false);
		int32_t px = -1;
		int32_t py = -1;
		for (int32_t x = 0; x < 14 && px < 0; x += 2) {
			for (int32_t y = 0; y < 8 && px < 0; y++) {
				const menu::Node* n = menu::shortcut_node(menu::MenuContext::Root::Sound, deluge::project::ShortcutsVersion::V3, false, static_cast<uint8_t>(x),
				                                          static_cast<uint8_t>(y));
				if (n != nullptr && menu::node_id(*n) != 0 && !menu::shortcut_applies(*n, ctx)) {
					px = x;
					py = y;
				}
			}
		}
		to_be_at_least(expect(px), 0);

		expect(entry::openMenuAtShortcut(f.clip, px, py, false, false, false, false)).to_be_false();
		expect(isUIOpen(&shellHostUI)).to_be_false();
		expect(spy.last_popup).to_equal(text(deluge::l10n::String::STRING_FOR_PARAMETER_NOT_APPLICABLE));
	});

	it("does not wipe the undo log", _ {
		deluge::ui::scenes::spec::SongFixture f = deluge::ui::scenes::spec::make_synth_song();

		// Seeded with the guest open, because `getNewAction` refuses otherwise.
		expect(entry::openMenu(nullptr, 0, false, false, false, false)).to_be_true();
		actionLogger.deleteAllLogs();
		(void)actionLogger.getNewAction(ActionType::MISC, ActionAddition::NOT_ALLOWED);
		expect(actionLogger.firstAction[BEFORE]).not_().to_be_null();
		closeGuest();

		expect(entry::openMenuAtShortcut(f.clip, 8, 7, false, false, false, false)).to_be_true();
		expect(actionLogger.firstAction[BEFORE]).not_().to_be_null();
		closeGuest();
	});

	it("marks a keyboard-screen root", _ {
		RootUIGuard root_guard;
		deluge::ui::scenes::spec::SongFixture f = deluge::ui::scenes::spec::make_synth_song();

		setRootUILowLevel(&instrumentClipView);
		expect(entry::openMenuAtShortcut(f.clip, 8, 7, false, false, false, false)).to_be_true();
		expect(deluge::ui::scenes::menu_scene().menu_context().root_is_keyboard).to_be_false();
		closeGuest();

		setRootUILowLevel(&keyboardScreen);
		expect(entry::openMenuAtShortcut(f.clip, 8, 7, false, false, false, false)).to_be_true();
		expect(deluge::ui::scenes::menu_scene().menu_context().root_is_keyboard).to_be_true();
		closeGuest();

		setRootUILowLevel(&keyboardScreen);
		expect(entry::openMenu(f.clip, 0, false, false, false, false)).to_be_true();
		expect(deluge::ui::scenes::menu_scene().menu_context().root_is_keyboard).to_be_true();
		closeGuest();
	});

	it("Shift + (4,0) on a DX7 sound opens the DX7 parameter page alone, on the param it showed last", _ {
		const spec::ShortcutLayout pinned_v3{deluge::project::ShortcutsVersion::V3}; // the layout the lookups name
		// `potentialShortcutPadAction`'s fallback: modulator 1's volume does not apply to a sound that is not FM, so
		// `setup()` refuses it, and on a DX7 source `setup(clip, &dxParam, 0)` opens the page as `dxParam.param` stands.
		deluge::ui::scenes::spec::SongFixture f = deluge::ui::scenes::spec::make_synth_song();
		Sound* sound = deluge::model::effects_owner_of(deluge::model::owner_of(*f.instrument))->sound;
		deluge::test::setOscType(*sound, 0, OscType::DX7);
		deluge::ui::scenes::menu_scene().land_dx(menu::DxState{.param = 3 * 21 + 2});
		expect(menu::shortcut_node(menu::MenuContext::Root::Sound, deluge::project::ShortcutsVersion::V3, false, 4, 0)).to_equal(&nodes::modulator0_volume);

		expect(entry::openMenuAtShortcut(f.clip, 4, 0, false, false, false, false)).to_be_true();
		expect(opened_on())
		    .to_equal(shell::SceneId{.kind = shell::SceneKind::FirstApp, .arg = menu::node_id(nodes::dx_param)});
		expect(deluge::ui::scenes::menu_scene().focused()).to_equal(&nodes::dx_param);
		expect(static_cast<int32_t>(deluge::ui::scenes::menu_scene().state().depth)).to_equal(0);
		expect(static_cast<int32_t>(deluge::ui::scenes::menu_scene().state().dx.param)).to_equal(3 * 21 + 2);
		closeGuest();

		// The same pad on a sound that is not DX7 is refused, with setup()'s popup.
		deluge::test::setOscType(*sound, 0, OscType::SAW);
		expect(entry::openMenuAtShortcut(f.clip, 4, 0, false, false, false, false)).to_be_false();
		expect(isUIOpen(&shellHostUI)).to_be_false();
	});

	it("Shift + (8,7) from the clip view, menu closed, opens the menu through menuShortcutPadAction", _ {
		deluge::ui::scenes::spec::SongFixture f = deluge::ui::scenes::spec::make_synth_song();
		(void)f;
		deluge::hid::display::OLED oled_display;
		deluge::hid::Display* const saved_display = display;
		display = &oled_display;
		const int32_t saved_mode = currentUIMode;
		currentUIMode = UI_MODE_NONE;
		RootUIGuard root_guard;
		HeldShift shift;

		setRootUILowLevel(&instrumentClipView);
		expect(isUIOpen(&shellHostUI)).to_be_false();
		(void)entry::menuShortcutPadAction(8, 7, true);
		expect(getCurrentUI()).to_equal(&shellHostUI);
		expect(deluge::ui::scenes::menu_scene().focused_value()).to_equal(&nodes::lpf_freq);
		nullifyUIs();
		deluge::hid::display::OLED::removePopup();
		display = saved_display;
		currentUIMode = saved_mode;
	});
});

describe menu_entry_refusal("openMenu refuses where SoundEditor::setup refuses", $ {
	it("a kit with neither affect-entire nor a selected drum opens nothing", _ {
		deluge::ui::scenes::spec::SongFixture kit = deluge::ui::scenes::spec::make_kit_song();

		// `MenuContext::resolved` is `setup()`'s own `return false` at
		// `origin/main:src/deluge/gui/ui/sound_editor.cpp`, and the five gated sites that test a return value
		// are written against exactly that. **Nothing must be left half-open**: the assertion is
		// that the hierarchy did not move, not merely that the call said false.
		expect(entry::openMenu(kit.clip, 0, false, false, false, /*kitGlobalFx=*/false)).to_be_false();
		// `isUIOpen`, deliberately, and NOT `getCurrentUI()`: with an empty hierarchy `getCurrentUI()` answers
		// `emptyStackUI` (`gui/ui/ui.cpp`; main's answers `lastUIBeforeNullifying`,
		// `origin/main:src/deluge/gui/ui/ui.cpp`), which names no open UI, so only `isUIOpen` can say the guest is not
		// on the stack.
		expect(isUIOpen(&shellHostUI)).to_be_false();

		// And the same clip with the one argument flipped DOES open, so the refusal is about the
		// context this kit resolves to and not about kit clips, or about the fixture, or about the
		// setting being read twice.
		expect(entry::openMenu(kit.clip, 0, false, false, false, /*kitGlobalFx=*/true)).to_be_true();
		expect(isUIOpen(&shellHostUI)).to_be_true();
		closeGuest();
	});
});

describe menu_entry_undo_log("the undo-log wipe SoundEditor::setup does on every menu open", $ {
	it("is done for a clip-rooted open and NOT for a settings-rooted one", _ {
		deluge::ui::scenes::spec::SongFixture f = deluge::ui::scenes::spec::make_synth_song();

		// In `SoundEditor::setup` the undo-log wipe sits inside `if (item) ... else if (clip)`, so the wipe follows the
		// clip and not the open. the "Not owned by this file" note in `ui/scenes/menu/context.cpp` records it as owed to
		// whoever lands the port's entry point; this is that entry point.
		// The seed has to happen with the guest open, and that is not a detail:
		// `ActionLogger::getNewAction` refuses unless `getCurrentUI() == getRootUI()`
		// (`model/action/action_logger.cpp`), which in a spec process is only true while
		// something is actually on the hierarchy. So open first, seed, then close.
		expect(entry::openMenu(nullptr, 0, false, false, false, false)).to_be_true();
		actionLogger.deleteAllLogs();
		(void)actionLogger.getNewAction(ActionType::MISC, ActionAddition::NOT_ALLOWED);
		expect(actionLogger.firstAction[BEFORE]).not_().to_be_null();
		closeGuest();

		// Settings root, no clip: legacy's wipe is inside `if (item) ... else if (clip)`, so it does
		// not happen, and neither does this one.
		expect(entry::openMenu(nullptr, 0, false, false, false, false)).to_be_true();
		expect(actionLogger.firstAction[BEFORE]).not_().to_be_null();
		closeGuest();

		// Same call, same process, one argument different -- the clip -- and the log goes.
		expect(entry::openMenu(f.clip, 0, false, false, false, false)).to_be_true();
		expect(actionLogger.firstAction[BEFORE]).to_be_null();
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
		deluge::ui::scenes::spec::SongFixture f = deluge::ui::scenes::spec::make_synth_song();
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
		expect(entry::openMenu(nullptr, 0, false, false, false, false)).to_be_true();
		expect(getCurrentUI()).to_equal(&shellHostUI);
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
		deluge::ui::scenes::spec::SongFixture f = deluge::ui::scenes::spec::make_synth_song();

		// `Shell::next_wake()`'s own warning: a scene whose `next_timer()` answers 0 makes the
		// guest re-arm `UI_SPECIFIC` at its 1 ms floor for as long as it is stacked, i.e. ~15x
		// legacy's 15 ms `graphicsRoutine`. This group opens the guest, so the
		// property is asserted rather than assumed.
		//
		// `MenuScene::next_timer()` answers `std::nullopt`, `kSourceBlinkMs` (20) or
		// `kShortcutBlinkMs` (180) -- never 0 (`pad_next_timer`, `ui/scenes/menu/pads.cpp`).
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
			const bool opened = which == 0   ? entry::openMenu(nullptr, 0, false, false, false, false)
			                    : which == 1 ? entry::openMenu(f.clip, 0, false, false, false, false)
			                                 : entry::openMenu(f.clip, 0, false, false, false, false,
			                                                        entry::MenuNode::Osc0FileSelector);
			expect(opened).to_be_true();

			const std::optional<uint32_t> timer = deluge::ui::scenes::menu_scene().next_timer();
			if (timer.has_value()) {
				expect(*timer).to_be_greater_than(0);
			}

			shellHostUI.shell().tick(now);
			const std::optional<uint32_t> wake = shellHostUI.shell().next_wake();
			// The condition that would end the reprieve is a deadline AT `now`, which is what a
			// zero `next_timer()` produces. A deadline in the future is a normal blink.
			if (wake.has_value()) {
				expect(*wake).not_().to_equal(now);
			}
			closeGuest();
		}
	});
});
describe menu_entry_midi_learn("MIDI from PlaybackHandler learns into the menu", $ {
	// The whole route: `PlaybackHandler` offers the message to the current UI, `ShellHostUI` routes it to the menu, and
	// the focused MIDI command's `LearnedMIDI` is written. `midiEngine.globalMIDICommands` is a process singleton, so
	// the slot is poisoned first: a slot another example already learned would make a route that wrote nothing look
	// like one that worked.

	it("a note-on, a program change and a CC while LEARN is held write the focused command's slot", _ {
		deluge::ui::scenes::spec::SongFixture f = deluge::ui::scenes::spec::make_synth_song();
		Song* const saved_song = currentSong;
		currentSong = f.song;
		::LearnedMIDI& slot = play_command_slot();
		const ::LearnedMIDI saved_slot = slot;

		expect(openGuestOnPlayCommand(*f.song)).to_be_true();
		expect(deluge::ui::scenes::menu_scene().focused()).to_equal(&nodes::midi_command_play);
		press_learn(true);
		bool thru = false;

		expect(poison(slot)).to_be_true();
		playbackHandler.noteMessageReceived(cable(), true, 9, 64, 100, &thru);
		expect(slot.cable).to_equal(&cable());
		expect(slot.channelOrZone).to_equal(9);
		expect(slot.noteOrCC).to_equal(64);

		expect(poison(slot)).to_be_true();
		playbackHandler.programChangeReceived(cable(), 3, 7);
		expect(slot.channelOrZone).to_equal(3 + IS_A_PC);
		expect(slot.noteOrCC).to_equal(7);

		expect(poison(slot)).to_be_true();
		playbackHandler.midiCCReceived(cable(), 15, 20, 127, &thru);
		expect(slot.channelOrZone).to_equal(15 + IS_A_CC);
		expect(slot.noteOrCC).to_equal(20);

		// Released: the next message is the song's, not the menu's.
		press_learn(false);
		expect(poison(slot)).to_be_true();
		expect(shellHostUI.noteOnReceivedForMidiLearn(cable(), 9, 65, 100)).to_be_false();
		expect(slot.channelOrZone).to_equal(kPoisonChannel);

		closeGuest();
		slot = saved_slot;
		currentSong = saved_song;
	});

	it("a transparent overlay over the menu does not stop the learn; the menu not being there does", _ {
		// `input_top()`, not `top()`: a passive overlay is drawn over the menu without taking its input
		// (`Scene::input_transparent`). The negative half removes the menu rather than covering it, because every
		// opaque scene in the registry refuses `on_enter` without setup this file does not build.
		deluge::ui::scenes::spec::SongFixture f = deluge::ui::scenes::spec::make_synth_song();
		Song* const saved_song = currentSong;
		currentSong = f.song;
		::LearnedMIDI& slot = play_command_slot();
		const ::LearnedMIDI saved_slot = slot;

		expect(openGuestOnPlayCommand(*f.song)).to_be_true();
		press_learn(true);

		// `FirstApp + 6` is `overlay::param_popup_scene()` (`ui/scenes/registry.h`), which is input-transparent.
		const auto popup = static_cast<shell::SceneKind>(static_cast<uint8_t>(shell::SceneKind::FirstApp) + 6);
		expect(shellHostUI.shell().stack().push(shell::SceneId{.kind = popup, .arg = 0})).to_be_true();
		expect(shellHostUI.shell().stack().top().kind()).to_equal(popup);
		expect(poison(slot)).to_be_true();
		expect(shellHostUI.noteOnReceivedForMidiLearn(cable(), 3, 40, 100)).to_be_true();
		expect(slot.channelOrZone).to_equal(3);
		expect(slot.noteOrCC).to_equal(40);

		// The menu gone with LEARN still held: the note is not used, so it plays.
		expect(shellHostUI.shell().stack().remove(shell::SceneKind::FirstApp)).to_be_true();
		expect(poison(slot)).to_be_true();
		expect(shellHostUI.noteOnReceivedForMidiLearn(cable(), 3, 40, 100)).to_be_false();
		expect(slot.channelOrZone).to_equal(kPoisonChannel);

		press_learn(false);
		closeGuest();
		slot = saved_slot;
		currentSong = saved_song;
	});

	it("a menu closed with LEARN held and opened again is not learning", _ {
		deluge::ui::scenes::spec::SongFixture f = deluge::ui::scenes::spec::make_synth_song();
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
		expect(slot.channelOrZone).to_equal(kPoisonChannel);

		press_learn(false);
		closeGuest();
		slot = saved_slot;
		currentSong = saved_song;
	});
});
namespace {
/// @brief The automation view as the root, a pad held with pad selection on; all put back.
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
	// The note leaves read through the note bridge, which boot wires (`deluge/deluge.cpp`) and this binary does not.
	before_all([] { deluge::gui::ui::wireShellNoteBridge(); });

	it("opens the note editor on an instrument clip, wiping the undo log as setup() does with no item", _ {
		deluge::ui::scenes::spec::SongFixture f = deluge::ui::scenes::spec::make_synth_song();
		HeldPadInAutomation held;
		// `ActionLogger::getNewAction` takes an action only while the root is the current UI, which it is here.
		actionLogger.deleteAllLogs();
		(void)actionLogger.getNewAction(ActionType::MISC, ActionAddition::NOT_ALLOWED);
		expect(actionLogger.firstAction[BEFORE]).not_().to_be_null();

		held.select();
		expect(getCurrentUI()).to_equal(&shellHostUI);
		expect(deluge::ui::scenes::menu_scene().menu_context().root).to_equal(menu::MenuContext::Root::NoteEditor);
		expect(deluge::ui::scenes::menu_scene().focused()).to_equal(&nodes::note_editor_root);
		// `setup(clip)` with no item wipes before it picks `noteEditorRootMenu`, unlike `enterNoteEditor`'s named root.
		expect(actionLogger.firstAction[BEFORE]).to_be_null();
		closeGuest();
	});

	it("leaves a kit's affect-entire on, where enterNoteEditor clears it", _ {
		deluge::ui::scenes::spec::SongFixture kit = deluge::ui::scenes::spec::make_kit_song();
		auto* clip = static_cast<InstrumentClip*>(kit.clip);
		clip->affectEntire = true;
		HeldPadInAutomation held;

		held.select();
		expect(getCurrentUI()).to_equal(&shellHostUI);
		expect(deluge::ui::scenes::menu_scene().menu_context().root).to_equal(menu::MenuContext::Root::NoteEditor);
		expect(clip->affectEntire).to_be_true();
		expect(deluge::ui::scenes::menu_scene().menu_context().affect_entire).to_be_true();
		closeGuest();
		clip->affectEntire = false;
	});

	it("opens an audio clip's root, and the arranger's song root, as setup() does under the same mode", _ {
		deluge::ui::scenes::spec::AudioSongFixture f = deluge::ui::scenes::spec::make_audio_clip_song();
		{
			HeldPadInAutomation held;
			// The press's LED refresh asks the root UI for affect-entire, and `AutomationView::getAffectEntire` reads
			// `getCurrentInstrumentClip()->affectEntire` with no audio-clip arm: on the device a read of a low address,
			// here a fault. The gate reads only the clip, so this half keeps the audio clip view as the root.
			setRootUILowLevel(&audioClipView);
			held.select();
			expect(getCurrentUI()).to_equal(&shellHostUI);
			expect(deluge::ui::scenes::menu_scene().focused()).to_equal(&nodes::root_audio_clip);
			closeGuest();
		}
		{
			HeldPadInAutomation held{/*arranger=*/true};
			held.select();
			expect(getCurrentUI()).to_equal(&shellHostUI);
			expect(deluge::ui::scenes::menu_scene().focused()).to_equal(&nodes::root_song);
			closeGuest();
		}
	});
});

describe menu_entry_close_mode("leaving the menu ends the UI mode it was left under, as exitCompletely does", $ {
	it("clears AFFECT ENTIRE held on a kit row's menu when the long-press exit closes it", _ {
		RootUIGuard root_guard;
		deluge::ui::scenes::spec::UiModeGuard mode;
		deluge::ui::scenes::spec::SongFixture kit = deluge::ui::scenes::spec::make_kit_song();
		(void)deluge::ui::scenes::spec::add_drum<SoundDrum>(kit);
		setRootUILowLevel(&instrumentClipView);
		expect(entry::openMenuAtShortcut(kit.clip, 8, 7, false, false, false, /*kitGlobalFx=*/false)).to_be_true();
		expect(deluge::ui::scenes::menu_scene().takes_affect_entire()).to_be_true();
		expect(entry::forwardMenuAffectEntire(deluge::hid::button::AFFECT_ENTIRE, true).has_value()).to_be_true();
		expect(currentUIMode).to_equal(uint32_t{UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR});

		expect(shellHostUI.exitUI()).to_equal(ActionResult::ACTIONED_AND_CAUSED_CHANGE);
		expect(isUIOpen(&shellHostUI)).to_be_false();
		expect(currentUIMode).to_equal(uint32_t{UI_MODE_NONE});
	});

	it("clears it when a short BACK at the menu's root closes it, which legacy accepts with AFFECT ENTIRE held", _ {
		RootUIGuard root_guard;
		deluge::ui::scenes::spec::UiModeGuard mode;
		deluge::ui::scenes::spec::SongFixture kit = deluge::ui::scenes::spec::make_kit_song();
		(void)deluge::ui::scenes::spec::add_drum<SoundDrum>(kit);
		setRootUILowLevel(&instrumentClipView);
		expect(entry::openMenuAtShortcut(kit.clip, 8, 7, false, false, false, /*kitGlobalFx=*/false)).to_be_true();
		expect(static_cast<int32_t>(deluge::ui::scenes::menu_scene().state().depth)).to_equal(0);
		expect(entry::forwardMenuAffectEntire(deluge::hid::button::AFFECT_ENTIRE, true).has_value()).to_be_true();

		(void)shellHostUI.buttonAction(deluge::hid::button::BACK, true);
		(void)shellHostUI.timerCallback();
		expect(isUIOpen(&shellHostUI)).to_be_false();
		expect(currentUIMode).to_equal(uint32_t{UI_MODE_NONE});
	});

	it("clears the non-exclusive modes too: an audition the menu was opened under", _ {
		RootUIGuard root_guard;
		deluge::ui::scenes::spec::UiModeGuard mode;
		deluge::ui::scenes::spec::SongFixture f = deluge::ui::scenes::spec::make_synth_song();
		setRootUILowLevel(&instrumentClipView);
		expect(entry::openMenu(f.clip, 0, false, false, false, false)).to_be_true();
		currentUIMode = UI_MODE_AUDITIONING;

		(void)shellHostUI.exitUI();
		expect(isUIOpen(&shellHostUI)).to_be_false();
		expect(currentUIMode).to_equal(uint32_t{UI_MODE_NONE});
	});

	it("keeps a stem export the menu started, and only its mode: legacy resets first and the export engages after", _ {
		RootUIGuard root_guard;
		deluge::ui::scenes::spec::UiModeGuard mode;
		deluge::ui::scenes::spec::SongFixture f = deluge::ui::scenes::spec::make_synth_song();
		setRootUILowLevel(&instrumentClipView);
		expect(entry::openMenu(f.clip, 0, false, false, false, false)).to_be_true();
		// What `startStemExport` leaves before the tick's close: the export engaged over a latched audition.
		currentUIMode = UI_MODE_STEM_EXPORT | UI_MODE_AUDITIONING;

		(void)shellHostUI.exitUI();
		expect(isUIOpen(&shellHostUI)).to_be_false();
		expect(currentUIMode).to_equal(uint32_t{UI_MODE_STEM_EXPORT});
	});

	it("ends the mode after a refused take, where legacy falls back to its editor on the file selector", _ {
		// A take already running: the recorder refuses, and the menu lands on the file selector, whose hook pushes the
		// browser, as main's `openUI(&soundEditor)` does. That is an editor visit: its exit is `exitCompletely`.
		(void)quiet_audio();
		RootUIGuard root_guard;
		deluge::ui::scenes::spec::UiModeGuard mode;
		deluge::ui::scenes::spec::SongFixture kit = deluge::ui::scenes::spec::make_kit_song();
		setRootUILowLevel(&instrumentClipView);
		DrumRowStack row{kit};
		expect(row.stack).not_().to_be_null();
		if (row.stack == nullptr) {
			return;
		}
		AudioEngine::setForegroundRecording(nullptr, AudioInputChannel::OUTPUT);
		instrumentClipView.enterDrumCreator(row.stack, /*doRecording=*/true);
		expect(isUIOpen(&shellHostUI)).to_be_true();
		(void)shellHostUI.timerCallback(); // the recorder refuses; the menu lands on the file selector
		(void)shellHostUI.timerCallback(); // whose hook pushes the browser
		AudioEngine::setForegroundRecording(nullptr, AudioInputChannel::NONE);
		expect(deluge::ui::scenes::menu_scene().pushed_scene_refused()).to_be_true();
		currentUIMode = UI_MODE_AUDITIONING;

		while (isUIOpen(&shellHostUI) && shellHostUI.shell().stack().has_input_scene()
		       && shellHostUI.shell().stack().input_top().kind() != shell::SceneKind::FirstApp) {
			(void)shellHostUI.exitUI(); // the browser goes
		}
		for (int32_t i = 0; i < 3 && isUIOpen(&shellHostUI); i++) {
			(void)shellHostUI.timerCallback();
		}
		if (isUIOpen(&shellHostUI)) {
			(void)shellHostUI.exitUI(); // the editor, on the file selector, if its resume kept it
		}
		expect(isUIOpen(&shellHostUI)).to_be_false();
		expect(currentUIMode).to_equal(uint32_t{UI_MODE_NONE});
	});

	it("leaves the mode alone when the drum creator's browser ends the visit: legacy opened no editor there", _ {
		(void)quiet_audio();
		RootUIGuard root_guard;
		deluge::ui::scenes::spec::UiModeGuard mode;
		deluge::ui::scenes::spec::SongFixture kit = deluge::ui::scenes::spec::make_kit_song();
		setRootUILowLevel(&instrumentClipView);
		DrumRowStack row{kit};
		expect(row.stack).not_().to_be_null();
		if (row.stack == nullptr) {
			return;
		}
		instrumentClipView.enterDrumCreator(row.stack, /*doRecording=*/false);
		expect(isUIOpen(&shellHostUI)).to_be_true();
		(void)shellHostUI.timerCallback(); // the file selector's hook pushes the browser
		expect(shellHostUI.shell().stack().input_top().kind()).to_equal(deluge::ui::scenes::browser::kBrowserSceneKind);
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
		RootUIGuard root_guard;
		deluge::ui::scenes::spec::UiModeGuard mode;
		deluge::ui::scenes::spec::SongFixture kit = deluge::ui::scenes::spec::make_kit_song();
		setRootUILowLevel(&instrumentClipView);
		DrumRowStack row{kit};
		expect(row.stack).not_().to_be_null();
		if (row.stack == nullptr) {
			return;
		}
		instrumentClipView.enterDrumCreator(row.stack, /*doRecording=*/false);
		expect(isUIOpen(&shellHostUI)).to_be_true();
		nullifyUIs(); // a song load takes every UI down with no close

		deluge::ui::scenes::spec::SongFixture f = deluge::ui::scenes::spec::make_synth_song();
		setRootUILowLevel(&instrumentClipView);
		expect(entry::openMenu(f.clip, 0, false, false, false, false)).to_be_true();
		currentUIMode = UI_MODE_AUDITIONING;
		(void)shellHostUI.exitUI();
		expect(currentUIMode).to_equal(uint32_t{UI_MODE_NONE});
	});
});

describe menu_entry_reopen("an entry that reaches the open guest re-roots it in place, as setup() re-sets the open editor", $ {
	it("re-roots the Settings menu on the pad's node when Shift + a pad is forwarded to the keyboard screen", _ {
		RootUIGuard root_guard;
		deluge::ui::scenes::spec::UiModeGuard mode;
		deluge::ui::scenes::spec::SongFixture f = deluge::ui::scenes::spec::make_synth_song();
		(void)f;
		setRootUILowLevel(&keyboardScreen);
		expect(entry::openMenu(nullptr, 0, false, false, false, false)).to_be_true();
		expect(deluge::ui::scenes::menu_scene().menu_context().root).to_equal(menu::MenuContext::Root::Settings);
		{
			// `GridPadClaim::None` on the settings root: the guest forwards the pad to `keyboardScreen.padAction`, whose
			// first statement is the shortcut entry.
			HeldShift shift;
			(void)shellHostUI.padAction(8, 7, 64);
		}
		(void)shellHostUI.timerCallback(); // the `Replace` the re-root filed
		expect(getCurrentUI()).to_equal(&shellHostUI);
		expect(deluge::ui::scenes::menu_scene().menu_context().root).to_equal(menu::MenuContext::Root::Sound);
		expect(deluge::ui::scenes::menu_scene().focused_value()).to_equal(&nodes::lpf_freq);

		// One guest on the hierarchy: one exit leaves the keyboard screen current.
		(void)shellHostUI.exitUI();
		expect(isUIOpen(&shellHostUI)).to_be_false();
		expect(getCurrentUI()).to_equal(&keyboardScreen);
	});

	it("keeps the kit-global-FX fact the menu was opened with, so Shift + a pad edits the selected drum", _ {
		RootUIGuard root_guard;
		deluge::ui::scenes::spec::UiModeGuard mode;
		deluge::ui::scenes::spec::SongFixture f = deluge::ui::scenes::spec::make_kit_song();
		(void)deluge::ui::scenes::spec::add_drum<SoundDrum>(f);
		f.clip->affectEntire = true;
		setRootUILowLevel(&keyboardScreen);
		// The history: the last menu closed, and no pad pressed since -- legacy's `setupKitGlobalFXMenu` is false.
		entry::clearMenuKitGlobalFx();
		// The Settings menu over a kit clip with affect-entire on: it was opened with the fact false.
		expect(entry::openMenu(nullptr, 0, false, false, false, false)).to_be_true();
		expect(deluge::ui::scenes::menu_scene().menu_context().is_kit_global_fx).to_be_false();
		{
			HeldShift shift;
			(void)shellHostUI.padAction(8, 7, 64);
		}
		(void)shellHostUI.timerCallback();
		const menu::MenuContext& ctx = deluge::ui::scenes::menu_scene().menu_context();
		expect(ctx.is_kit_global_fx).to_be_false();
		expect(ctx.is_drum).to_be_true();
		expect(deluge::ui::scenes::menu_scene().focused_value()).to_equal(&nodes::lpf_freq);
		(void)shellHostUI.exitUI();
	});

	it("edits the kit's own effects when a pad was played since the last close, as legacy's sticky fact does", _ {
		(void)quiet_audio();
		RootUIGuard root_guard;
		deluge::ui::scenes::spec::UiModeGuard mode;
		deluge::ui::scenes::spec::SongFixture f = deluge::ui::scenes::spec::make_kit_song();
		(void)deluge::ui::scenes::spec::add_drum<SoundDrum>(f);
		f.clip->affectEntire = true;
		setRootUILowLevel(&keyboardScreen);
		entry::clearMenuKitGlobalFx();
		// A pad played on the keyboard screen with the menu closed: the shortcut entry is its first statement.
		(void)keyboardScreen.padAction(0, 0, 64);
		(void)keyboardScreen.padAction(0, 0, 0);
		expect(entry::menuKitGlobalFx()).to_be_true();
		// Shift + SELECT's Settings leaves the fact alone, and the forwarded Shift + pad reads it.
		expect(entry::openMenu(nullptr, 0, false, false, false, false)).to_be_true();
		{
			HeldShift shift;
			(void)shellHostUI.padAction(8, 7, 64);
		}
		(void)shellHostUI.timerCallback();
		const menu::MenuContext& ctx = deluge::ui::scenes::menu_scene().menu_context();
		expect(ctx.root).to_equal(menu::MenuContext::Root::KitGlobalFx);
		expect(ctx.is_kit_global_fx).to_be_true();
		expect(deluge::ui::scenes::menu_scene().focused_value()).to_equal(&nodes::global_lpf_freq);
		(void)shellHostUI.exitUI();
	});

	it("edits the selected drum when the menu closed after that pad: the close clears the fact", _ {
		(void)quiet_audio();
		RootUIGuard root_guard;
		deluge::ui::scenes::spec::UiModeGuard mode;
		deluge::ui::scenes::spec::SongFixture f = deluge::ui::scenes::spec::make_kit_song();
		(void)deluge::ui::scenes::spec::add_drum<SoundDrum>(f);
		f.clip->affectEntire = true;
		setRootUILowLevel(&keyboardScreen);
		(void)keyboardScreen.padAction(0, 0, 64);
		(void)keyboardScreen.padAction(0, 0, 0);
		expect(entry::menuKitGlobalFx()).to_be_true();
		// A menu opened and exited after the pad.
		expect(entry::openMenu(nullptr, 0, false, false, false, false)).to_be_true();
		(void)shellHostUI.exitUI();
		expect(isUIOpen(&shellHostUI)).to_be_false();
		expect(entry::menuKitGlobalFx()).to_be_false();
		// Then the same Settings and Shift + pad.
		expect(entry::openMenu(nullptr, 0, false, false, false, false)).to_be_true();
		{
			HeldShift shift;
			(void)shellHostUI.padAction(8, 7, 64);
		}
		(void)shellHostUI.timerCallback();
		const menu::MenuContext& ctx = deluge::ui::scenes::menu_scene().menu_context();
		expect(ctx.is_kit_global_fx).to_be_false();
		expect(ctx.is_drum).to_be_true();
		expect(deluge::ui::scenes::menu_scene().focused_value()).to_equal(&nodes::lpf_freq);
		(void)shellHostUI.exitUI();
	});

	it("SELECT with affect-entire off opens the drum root on the kit's own effects while a pad left the fact set", _ {
		(void)quiet_audio();
		RootUIGuard root_guard;
		deluge::ui::scenes::spec::UiModeGuard mode;
		deluge::ui::scenes::spec::SongFixture f = deluge::ui::scenes::spec::make_kit_song();
		SoundDrum* const drum = deluge::ui::scenes::spec::add_drum<SoundDrum>(f);
		f.clip->affectEntire = true;
		setRootUILowLevel(&instrumentClipView);
		entry::clearMenuKitGlobalFx();
		// A clip-view edit pad with the menu closed and no SHIFT: not a shortcut, but legacy's write runs first.
		(void)instrumentClipView.padAction(0, 0, 64);
		(void)instrumentClipView.padAction(0, 0, 0);
		expect(entry::menuKitGlobalFx()).to_be_true();
		f.clip->affectEntire = false; // AFFECT ENTIRE turned off, which writes nothing
		// An audition selects the drum's row again; the audition column never reaches the shortcut entry.
		static_cast<Kit*>(f.instrument)->selectedDrum = drum;
		(void)instrumentClipView.buttonAction(deluge::hid::button::SELECT_ENC, true);
		(void)instrumentClipView.buttonAction(deluge::hid::button::SELECT_ENC, false);
		expect(isUIOpen(&shellHostUI)).to_be_true();
		// `SoundEditor::setup`: the root from the clip's affect-entire, what it edits from `setupKitGlobalFXMenu`.
		const menu::MenuContext& ctx = deluge::ui::scenes::menu_scene().menu_context();
		expect(ctx.root).to_equal(menu::MenuContext::Root::Drum);
		expect(ctx.is_kit_global_fx).to_be_true();
		expect(ctx.is_drum).to_be_false();
		expect(ctx.addresses.slot.has_value()).to_be_false();
		expect(ctx.addresses.chain.has_value()).to_be_true();
		expect(std::get<Kit*>(entry::sound_menu_owner())).to_equal(static_cast<Kit*>(f.instrument));
		// The automation view under it agrees: the kit's own context.
		expect(automationView.getAffectEntire()).to_be_true();
		(void)shellHostUI.exitUI();
		expect(entry::menuKitGlobalFx()).to_be_false();
	});

	it("a Shift + pad on the drum menu SELECT opened takes the kit's own item while the latch is set", _ {
		(void)quiet_audio();
		RootUIGuard root_guard;
		deluge::ui::scenes::spec::UiModeGuard mode;
		LatchedKit k;
		expect(entry::menuKitGlobalFx()).to_be_true();
		k.f.clip->affectEntire = false; // SELECT then opens the drum's root on the kit's own effects
		(void)instrumentClipView.buttonAction(deluge::hid::button::SELECT_ENC, true);
		(void)instrumentClipView.buttonAction(deluge::hid::button::SELECT_ENC, false);
		expect(deluge::ui::scenes::menu_scene().menu_context().root).to_equal(menu::MenuContext::Root::Drum);
		HeldShift shift;
		guest_shortcut(8, 7);
		// `get_basic_shortcut_action(x, y, editingKitAffectEntire())` reads the latch: the kit's own LPF.
		expect(deluge::ui::scenes::menu_scene().focused_value()).to_equal(&nodes::global_lpf_freq);
		expect(deluge::ui::scenes::menu_scene().menu_context().is_kit_global_fx).to_be_true();
		// With affect-entire off `updatePadLightsFor` scans the sound table, which has no kit LPF: nothing blinks.
		(void)shellHostUI.timerCallback();
		expect(deluge::ui::scenes::menu_scene().state().blink_plan.param.valid).to_be_false();
		closeGuest();
	});

	it("the note editor opened after edit pads takes no shortcut press, as legacy's padAction skips it there", _ {
		(void)quiet_audio();
		RootUIGuard root_guard;
		deluge::ui::scenes::spec::UiModeGuard mode;
		LatchedKit k;
		expect(entry::menuKitGlobalFx()).to_be_true();
		expect(entry::openMenu(k.f.clip, 0, false, false, false, /*kitGlobalFx=*/false,
		                            entry::MenuNode::ContextRoot, entry::NoteMode::NoteEditor))
		    .to_be_true();
		expect(deluge::ui::scenes::menu_scene().menu_context().root).to_equal(menu::MenuContext::Root::NoteEditor);
		// `setup()` reads the latch for the note editor too: it opens on the kit's own effects.
		expect(deluge::ui::scenes::menu_scene().menu_context().is_kit_global_fx).to_be_true();
		const menu::Node* const before = deluge::ui::scenes::menu_scene().focused();
		HeldShift shift;
		guest_shortcut(8, 7);
		expect(deluge::ui::scenes::menu_scene().menu_context().root).to_equal(menu::MenuContext::Root::NoteEditor);
		expect(deluge::ui::scenes::menu_scene().focused()).to_equal(before);
		expect(deluge::ui::scenes::menu_scene().focused_value()).not_().to_equal(&nodes::global_lpf_freq);
		closeGuest();
	});

	it("the automation view reads the latch under the drum menu, and the kit's context before it follows a jump", _ {
		(void)quiet_audio();
		RootUIGuard root_guard;
		deluge::ui::scenes::spec::UiModeGuard mode;
		LatchedKit k;
		k.f.clip->affectEntire = false;
		setRootUILowLevel(&automationView);
		expect(entry::openMenu(k.f.clip, 0, false, false, false, /*kitGlobalFx=*/false)).to_be_true();
		expect(deluge::ui::scenes::menu_scene().menu_context().root).to_equal(menu::MenuContext::Root::Drum);
		// `getAffectEntire` reads `setupKitGlobalFXMenu` under the sound menu, which the latch is.
		expect(automationView.getAffectEntire()).to_be_true();
		HeldShift shift;
		(void)shellHostUI.buttonAction(deluge::hid::button::SHIFT, true);
		(void)shellHostUI.padAction(8, 7, 64);
		// One tick: the press, then the drains that hand the move to the automation view. The editor is on the kit's
		// own effects by then, as legacy's `setup()` re-points it before the view refreshes.
		(void)shellHostUI.timerCallback();
		expect(std::get<Kit*>(entry::sound_menu_owner())).to_equal(static_cast<Kit*>(k.f.instrument));
		expect(automationView.getAffectEntire()).to_be_true();
		(void)shellHostUI.padAction(8, 7, 0);
		(void)shellHostUI.buttonAction(deluge::hid::button::SHIFT, false);
		closeGuest();
	});

	it("SHIFT + CROSS SCREEN on a drum page steps the kit's own chain while the latch is set", _ {
		(void)quiet_audio();
		RootUIGuard root_guard;
		deluge::ui::scenes::spec::UiModeGuard mode;
		deluge::ui::scenes::spec::FeatureSetting horizontal{RuntimeFeatureSettingType::HorizontalMenus,
		                                                util::to_underlying(RuntimeFeatureStateToggle::On)};
		LatchedKit k;
		k.f.clip->affectEntire = false;
		expect(entry::openMenu(k.f.clip, 0, false, false, false, /*kitGlobalFx=*/false)).to_be_true();
		const menu::MenuContext drum_ctx = deluge::ui::scenes::menu_scene().menu_context();
		deluge::ui::scenes::menu_scene().reroot_at(*menu::MenuScene::roots()[static_cast<size_t>(drum_ctx.root)],
		                                       drum_ctx, menu::node_id(nodes::filters_group), nullptr);
		(void)shellHostUI.timerCallback();
		expect(deluge::ui::scenes::menu_scene().state().path[0]).to_equal(&nodes::filters_group);
		// Past the page buttons' 140 ms debounce, which runs on the guest's clock.
		AudioEngine::audioSampleTimer += 44'100;
		HeldShift shift;
		(void)shellHostUI.buttonAction(deluge::hid::button::SHIFT, true);
		(void)shellHostUI.buttonAction(deluge::hid::button::CROSS_SCREEN_EDIT, true);
		(void)shellHostUI.timerCallback();
		(void)shellHostUI.buttonAction(deluge::hid::button::CROSS_SCREEN_EDIT, false);
		(void)shellHostUI.buttonAction(deluge::hid::button::SHIFT, false);
		(void)shellHostUI.timerCallback();
		const menu::Node* const page = deluge::ui::scenes::menu_scene().state().path[0];
		const std::span<const menu::Node* const> kit_chain = menu::chain_for(menu::MenuContext::Root::KitGlobalFx);
		const std::span<const menu::Node* const> sound_chain = menu::chain_for(menu::MenuContext::Root::Drum);
		expect(kit_chain).to_contain(page);
		expect(sound_chain).not_().to_contain(page);
		expect(deluge::ui::scenes::menu_scene().menu_context().is_kit_global_fx).to_be_true();
		closeGuest();
	});

	it("the SHIFT overlay on the drum menu shows the kit's own map when it opened with the latch set, else the drum's",
	   _ {
		(void)quiet_audio();
		RootUIGuard root_guard;
		deluge::ui::scenes::spec::UiModeGuard mode;
		deluge::ui::scenes::spec::FeatureSetting overlay{
		    RuntimeFeatureSettingType::ShortcutOverlay,
		    util::to_underlying(RuntimeFeatureStateShortcutOverlay::MenuOnly)};
		LatchedKit k;
		k.f.clip->affectEntire = false;
		expect(entry::openMenu(k.f.clip, 0, false, false, false, /*kitGlobalFx=*/false)).to_be_true();
		expect(deluge::ui::scenes::menu_scene().menu_context().root).to_equal(menu::MenuContext::Root::Drum);
		HeldShift shift;
		(void)shellHostUI.buttonAction(deluge::hid::button::SHIFT, true);
		(void)shellHostUI.timerCallback();
		const auto shown = [] {
			deluge::ui::test::GridRig grid;
			grid.input.shift = true; // SHIFT held: the overlay is up
			grid.begin_tick(0);
			deluge::ui::scenes::menu_scene().render_grid(grid.frame);
			return grid.state();
		};
		const auto kit_map = shown();
		(void)shellHostUI.buttonAction(deluge::hid::button::SHIFT, false);
		closeGuest(); // the close clears the latch
		expect(entry::openMenu(k.f.clip, 0, false, false, false, /*kitGlobalFx=*/false)).to_be_true();
		expect(deluge::ui::scenes::menu_scene().menu_context().is_kit_global_fx).to_be_false();
		(void)shellHostUI.buttonAction(deluge::hid::button::SHIFT, true);
		(void)shellHostUI.timerCallback();
		const auto drum_map = shown();
		expect(kit_map).not_().to_equal(deluge::ui::test::GridRig{}.state());
		expect(kit_map).not_().to_equal(drum_map);
		(void)shellHostUI.buttonAction(deluge::hid::button::SHIFT, false);
		closeGuest();
	});

	it("opens the drum creator on the new drum, whatever a pad left the fact at", _ {
		(void)quiet_audio();
		RootUIGuard root_guard;
		deluge::ui::scenes::spec::UiModeGuard mode;
		deluge::ui::scenes::spec::SongFixture kit = deluge::ui::scenes::spec::make_kit_song();
		(void)deluge::ui::scenes::spec::add_drum<SoundDrum>(kit);
		kit.clip->affectEntire = true;
		setRootUILowLevel(&keyboardScreen);
		(void)keyboardScreen.padAction(0, 0, 64);
		(void)keyboardScreen.padAction(0, 0, 0);
		expect(entry::menuKitGlobalFx()).to_be_true();
		setRootUILowLevel(&instrumentClipView);
		DrumRowStack row{kit};
		expect(row.stack).not_().to_be_null();
		if (row.stack == nullptr) {
			return;
		}
		instrumentClipView.enterDrumCreator(row.stack, /*doRecording=*/false);
		expect(isUIOpen(&shellHostUI)).to_be_true();
		expect(entry::menuKitGlobalFx()).to_be_false();
		const menu::MenuContext& ctx = deluge::ui::scenes::menu_scene().menu_context();
		expect(ctx.is_kit_global_fx).to_be_false();
		expect(ctx.is_drum).to_be_true();
		closeGuest();
	});

	it("sets the fact on the browser's return to the menu only with SHIFT held and no audition, as legacy's re-entry",
	   _ {
		   deluge::ui::scenes::spec::UiModeGuard mode;
		   deluge::ui::scenes::spec::SongFixture f = deluge::ui::scenes::spec::make_kit_song();
		   (void)deluge::ui::scenes::spec::add_drum<SoundDrum>(f);
		   f.clip->affectEntire = true;
		   entry::clearMenuKitGlobalFx();
		   entry::latchMenuKitGlobalFxOnReentry();
		   expect(entry::menuKitGlobalFx()).to_be_false(); // no SHIFT
		   {
			   HeldShift shift;
			   currentUIMode = UI_MODE_AUDITIONING;
			   entry::latchMenuKitGlobalFxOnReentry();
			   expect(entry::menuKitGlobalFx()).to_be_false(); // an audition
			   currentUIMode = UI_MODE_NONE;
			   f.clip->affectEntire = false;
			   entry::latchMenuKitGlobalFxOnReentry();
			   expect(entry::menuKitGlobalFx()).to_be_false(); // affect-entire off
			   f.clip->affectEntire = true;
			   entry::latchMenuKitGlobalFxOnReentry();
			   expect(entry::menuKitGlobalFx()).to_be_true();
		   }
		   // It only ever sets: what was set stays.
		   entry::latchMenuKitGlobalFxOnReentry();
		   expect(entry::menuKitGlobalFx()).to_be_true();
		   entry::clearMenuKitGlobalFx();
	   });

	it("a second Shift + pad after the marker editor's jump opens the kit's own item, as legacy's re-entry latched",
	   _ {
		   (void)quiet_audio();
		   RootUIGuard root_guard;
		   deluge::ui::scenes::spec::UiModeGuard mode;
		   deluge::ui::scenes::spec::SongFixture f = deluge::ui::scenes::spec::make_kit_song();
		   SoundDrum* const drum = deluge::ui::scenes::spec::add_drum<SoundDrum>(f);
		   give_drum_a_sample(f, *drum, make_marker_sample());
		   f.clip->affectEntire = true;
		   setRootUILowLevel(&instrumentClipView);
		   // The drum creator's leg: the latch cleared, the menu on the new drum with affect-entire on.
		   entry::clearMenuKitGlobalFx();
		   expect(entry::openMenu(f.clip, 0, false, false, false, /*kitGlobalFx=*/false)).to_be_true();
		   const menu::MenuContext drum_ctx = deluge::ui::scenes::menu_scene().menu_context();
		   expect(drum_ctx.root).to_equal(menu::MenuContext::Root::Drum);
		   // SAMPLE > START: the marker editor over the menu.
		   deluge::ui::scenes::menu_scene().reroot_at(*menu::MenuScene::roots()[static_cast<size_t>(drum_ctx.root)],
		                                          drum_ctx, menu::node_id(nodes::sample0_start), nullptr);
		   for (int i = 0; i < 3; i++) {
			   (void)shellHostUI.timerCallback();
		   }
		   expect(shellHostUI.shell().stack().input_top().kind()).to_equal(deluge::ui::scenes::marker::kMarkerSceneKind);
		   HeldShift shift;
		   (void)shellHostUI.buttonAction(deluge::hid::button::SHIFT, true);
		   const auto shortcut = [] {
			   (void)shellHostUI.padAction(8, 7, 64);
			   for (int i = 0; i < 3; i++) {
				   (void)shellHostUI.timerCallback();
			   }
			   (void)shellHostUI.padAction(8, 7, 0);
			   (void)shellHostUI.timerCallback();
		   };
		   // Both firmwares land on the drum's LPF; legacy's re-entry from the marker editor latches the fact.
		   shortcut();
		   expect(shellHostUI.shell().stack().input_top().kind()).to_equal(shell::SceneKind::FirstApp);
		   expect(deluge::ui::scenes::menu_scene().focused_value()).to_equal(&nodes::lpf_freq);
		   expect(entry::menuKitGlobalFx()).to_be_true();
		   // `updatePadLightsFor` takes the kit's own table from now on, which has no drum LPF: nothing blinks.
		   (void)shellHostUI.timerCallback();
		   expect(deluge::ui::scenes::menu_scene().state().blink_plan.param.valid).to_be_false();
		   // The next press reads it: the kit's own LPF.
		   shortcut();
		   expect(deluge::ui::scenes::menu_scene().focused_value()).to_equal(&nodes::global_lpf_freq);
		   expect(deluge::ui::scenes::menu_scene().menu_context().is_kit_global_fx).to_be_true();
		   (void)shellHostUI.buttonAction(deluge::hid::button::SHIFT, false);
		   closeGuest();
	   });

	it("a Shift + pad after a browser claim made with SHIFT held opens the kit's own item", _ {
		(void)quiet_audio();
		RootUIGuard root_guard;
		deluge::ui::scenes::spec::UiModeGuard mode;
		deluge::ui::scenes::spec::SongFixture f = deluge::ui::scenes::spec::make_kit_song();
		(void)deluge::ui::scenes::spec::add_drum<SoundDrum>(f);
		f.clip->affectEntire = true;
		setRootUILowLevel(&instrumentClipView);
		// Legacy's claim re-enters its editor only where a page lists the file selector, which takes HorizontalMenus.
		deluge::ui::scenes::spec::FeatureSetting horizontal{RuntimeFeatureSettingType::HorizontalMenus,
		                                                util::to_underlying(RuntimeFeatureStateToggle::On)};
		entry::clearMenuKitGlobalFx();
		// The file selector on the drum, whose hook pushes the browser.
		expect(entry::openMenu(f.clip, 0, false, false, false, /*kitGlobalFx=*/false,
		                            entry::MenuNode::Osc0FileSelector))
		    .to_be_true();
		(void)shellHostUI.timerCallback();
		expect(shellHostUI.shell().stack().input_top().kind()).to_equal(deluge::ui::scenes::browser::kBrowserSceneKind);
		HeldShift shift;
		(void)shellHostUI.buttonAction(deluge::hid::button::SHIFT, true);
		// The claim lands with SHIFT held: legacy's `claimCurrentFile` re-enters its editor and latches the fact.
		entry::shellSampleClaimDone(true);
		// The browser leaves on its claim's success; no file was pressed here, so it is taken off as it takes itself.
		(void)shellHostUI.shell().stack().remove(deluge::ui::scenes::browser::kBrowserSceneKind);
		for (int i = 0; i < 3; i++) {
			(void)shellHostUI.timerCallback();
		}
		expect(entry::menuKitGlobalFx()).to_be_true();
		expect(shellHostUI.shell().stack().input_top().kind()).to_equal(shell::SceneKind::FirstApp);
		(void)shellHostUI.padAction(8, 7, 64);
		for (int i = 0; i < 3; i++) {
			(void)shellHostUI.timerCallback();
		}
		(void)shellHostUI.padAction(8, 7, 0);
		expect(deluge::ui::scenes::menu_scene().focused_value()).to_equal(&nodes::global_lpf_freq);
		expect(deluge::ui::scenes::menu_scene().menu_context().is_kit_global_fx).to_be_true();
		(void)shellHostUI.buttonAction(deluge::hid::button::SHIFT, false);
		closeGuest();
	});

	it("a browser claim made without SHIFT while the fact is latched leaves the page the file selector was pressed on",
	   _ {
		   (void)quiet_audio();
		   RootUIGuard root_guard;
		   deluge::ui::scenes::spec::UiModeGuard mode;
		   deluge::ui::scenes::spec::SongFixture f = deluge::ui::scenes::spec::make_kit_song();
		   (void)deluge::ui::scenes::spec::add_drum<SoundDrum>(f);
		   f.clip->affectEntire = true;
		   setRootUILowLevel(&instrumentClipView);
		   deluge::ui::scenes::spec::FeatureSetting horizontal{RuntimeFeatureSettingType::HorizontalMenus,
		                                                   util::to_underlying(RuntimeFeatureStateToggle::On)};
		   entry::clearMenuKitGlobalFx();
		   expect(entry::openMenu(f.clip, 0, false, false, false, /*kitGlobalFx=*/false,
		                               entry::MenuNode::Osc0FileSelector))
		       .to_be_true();
		   (void)shellHostUI.timerCallback();
		   {
			   HeldShift shift;
			   (void)shellHostUI.buttonAction(deluge::hid::button::SHIFT, true);
			   entry::shellSampleClaimDone(true);
			   (void)shellHostUI.shell().stack().remove(deluge::ui::scenes::browser::kBrowserSceneKind);
			   for (int i = 0; i < 3; i++) {
				   (void)shellHostUI.timerCallback();
			   }
			   (void)shellHostUI.buttonAction(deluge::hid::button::SHIFT, false);
		   }
		   expect(entry::menuKitGlobalFx()).to_be_true();
		   const menu::Node* const group = deluge::ui::scenes::menu_scene().focused();
		   expect(is_horizontal_page(group)).to_be_true();
		   expect(deluge::ui::scenes::menu_scene().focused_value()).to_equal(&nodes::file0_selector);
		   const auto column = deluge::ui::scenes::menu_scene().state().columns[0].focused;
		   // SELECT on the file selector opens the browser; a second claim, SHIFT released, goes back up one level.
		   (void)shellHostUI.buttonAction(deluge::hid::button::SELECT_ENC, true);
		   (void)shellHostUI.buttonAction(deluge::hid::button::SELECT_ENC, false);
		   for (int i = 0; i < 3; i++) {
			   (void)shellHostUI.timerCallback();
		   }
		   expect(shellHostUI.shell().stack().input_top().kind()).to_equal(deluge::ui::scenes::browser::kBrowserSceneKind);
		   entry::shellSampleClaimDone(true);
		   (void)shellHostUI.shell().stack().remove(deluge::ui::scenes::browser::kBrowserSceneKind);
		   for (int i = 0; i < 3; i++) {
			   (void)shellHostUI.timerCallback();
		   }
		   expect(deluge::ui::scenes::menu_scene().focused()).to_equal(group);
		   expect(deluge::ui::scenes::menu_scene().state().columns[0].focused).to_equal(column);
		   closeGuest();
	   });

	it("re-roots for a direct shortcut open too, keeping one guest", _ {
		RootUIGuard root_guard;
		deluge::ui::scenes::spec::UiModeGuard mode;
		deluge::ui::scenes::spec::SongFixture f = deluge::ui::scenes::spec::make_synth_song();
		setRootUILowLevel(&instrumentClipView);
		expect(entry::openMenu(f.clip, 0, false, false, false, false)).to_be_true();
		expect(entry::openMenuAtShortcut(f.clip, 9, 7, false, false, false, false)).to_be_true();
		(void)shellHostUI.timerCallback();
		expect(deluge::ui::scenes::menu_scene().focused_value()).to_equal(&nodes::hpf_freq);
		(void)shellHostUI.exitUI();
		expect(isUIOpen(&shellHostUI)).to_be_false();
		expect(getCurrentUI()).to_equal(&instrumentClipView);
	});
});

namespace {
/// @brief `HorizontalMenus` set for an example and put back.
struct HorizontalMenusGuard {
	RuntimeFeatureStateToggle saved =
	    static_cast<RuntimeFeatureStateToggle>(runtimeFeatureSettings.get(RuntimeFeatureSettingType::HorizontalMenus));
	explicit HorizontalMenusGuard(RuntimeFeatureStateToggle v) {
		runtimeFeatureSettings.set(RuntimeFeatureSettingType::HorizontalMenus, v);
	}
	~HorizontalMenusGuard() { runtimeFeatureSettings.set(RuntimeFeatureSettingType::HorizontalMenus, saved); }
	HorizontalMenusGuard(const HorizontalMenusGuard&) = delete;
	HorizontalMenusGuard& operator=(const HorizontalMenusGuard&) = delete;
};

/// @brief One audition pad held on the instrument clip view, silently, and the state put back.
struct HeldAudition {
	uint32_t saved_mode = currentUIMode;
	uint8_t saved_pressed[kDisplayHeight];
	uint8_t saved_y = instrumentClipView.lastAuditionedYDisplay;
	bool saved_silent = instrumentClipView.auditioningSilently;
	explicit HeldAudition(int32_t y) {
		std::copy(std::begin(instrumentClipView.auditionPadIsPressed), std::end(instrumentClipView.auditionPadIsPressed),
		          saved_pressed);
		std::fill(std::begin(instrumentClipView.auditionPadIsPressed), std::end(instrumentClipView.auditionPadIsPressed),
		          uint8_t{0});
		instrumentClipView.auditionPadIsPressed[y] = 1;
		instrumentClipView.lastAuditionedYDisplay = y;
		instrumentClipView.auditioningSilently = true;
		currentUIMode = UI_MODE_AUDITIONING;
	}
	~HeldAudition() {
		std::copy(std::begin(saved_pressed), std::end(saved_pressed), instrumentClipView.auditionPadIsPressed);
		instrumentClipView.lastAuditionedYDisplay = saved_y;
		instrumentClipView.auditioningSilently = saved_silent;
		currentUIMode = saved_mode;
	}
	HeldAudition(const HeldAudition&) = delete;
	HeldAudition& operator=(const HeldAudition&) = delete;
};
} // namespace

describe menu_entry_shortcut_whole("site 9: the port answers the press whole, and legacy never opens", $ {
	it("says PARAMETER NOT APPLICABLE for a pad that does not apply, and the DX7 test reads the pad's own source", _ {
		// A DX7 source beside the pad's own: the fallback tests the pad's source, not this one.
		RootUIGuard root_guard;
		deluge::ui::scenes::spec::UiModeGuard mode;
		PopupSpy spy;
		DisplayGuard shown{spy};
		deluge::ui::scenes::spec::SongFixture f = deluge::ui::scenes::spec::make_synth_song();
		Sound* sound = deluge::model::effects_owner_of(deluge::model::owner_of(*f.instrument))->sound;
		deluge::test::setOscType(*sound, 0, OscType::SAW);
		deluge::test::setOscType(*sound, 1, OscType::DX7);
		setRootUILowLevel(&instrumentClipView);
		{
			HeldShift shift;
			expect(entry::menuShortcutPadAction(4, 0, true)).to_equal(ActionResult::DEALT_WITH);
		}
		expect(isUIOpen(&shellHostUI)).to_be_false();
		expect(spy.last_popup).to_equal(text(deluge::l10n::String::STRING_FOR_PARAMETER_NOT_APPLICABLE));
		deluge::test::setOscType(*sound, 1, OscType::SAW);
		nullifyUIs();
	});

	it("says Feature not (yet?) implemented for a coming-soon cell and opens nothing", _ {
		RootUIGuard root_guard;
		deluge::ui::scenes::spec::UiModeGuard mode;
		PopupSpy spy;
		DisplayGuard shown{spy};
		deluge::ui::scenes::spec::SongFixture f = deluge::ui::scenes::spec::make_synth_song();
		(void)f;
		setRootUILowLevel(&instrumentClipView);
		{
			HeldShift shift;
			expect(entry::menuShortcutPadAction(4, 2, true)).to_equal(ActionResult::DEALT_WITH);
		}
		expect(isUIOpen(&shellHostUI)).to_be_false();
		expect(spy.last_popup).to_equal(text(deluge::l10n::String::STRING_FOR_UNIMPLEMENTED));
	});

	it("says nothing at all on a kit with neither affect-entire nor a selected row, even on a coming-soon cell", _ {
		RootUIGuard root_guard;
		deluge::ui::scenes::spec::UiModeGuard mode;
		PopupSpy spy;
		DisplayGuard shown{spy};
		deluge::ui::scenes::spec::SongFixture kit = deluge::ui::scenes::spec::make_kit_song();
		(void)kit;
		setRootUILowLevel(&instrumentClipView);
		{
			HeldShift shift;
			expect(entry::menuShortcutPadAction(4, 2, true)).to_equal(ActionResult::DEALT_WITH);
		}
		expect(isUIOpen(&shellHostUI)).to_be_false();
		expect(spy.last_popup).to_equal(std::string{});
	});

	it("reads the song view's table from the performance view and edits the song, as legacy's call does", _ {
		RootUIGuard root_guard;
		deluge::ui::scenes::spec::UiModeGuard mode;
		deluge::ui::scenes::spec::SongFixture f = deluge::ui::scenes::spec::make_synth_song();
		(void)f;
		setRootUILowLevel(&performanceView);
		{
			HeldShift shift;
			(void)entry::menuShortcutPadAction(8, 7, true);
		}
		expect(getCurrentUI()).to_equal(&shellHostUI);
		expect(deluge::ui::scenes::menu_scene().menu_context().root).to_equal(menu::MenuContext::Root::Performance);
		expect(deluge::ui::scenes::menu_scene().focused_value()).to_equal(&nodes::global_lpf_freq);
		closeGuest();
	});

	it("is the open guest's to answer: it re-roots, and nothing else opens", _ {
		RootUIGuard root_guard;
		deluge::ui::scenes::spec::UiModeGuard mode;
		deluge::ui::scenes::spec::SongFixture f = deluge::ui::scenes::spec::make_synth_song();
		(void)f;
		setRootUILowLevel(&keyboardScreen);
		expect(entry::openMenu(nullptr, 0, false, false, false, false)).to_be_true();
		{
			HeldShift shift;
			(void)shellHostUI.padAction(8, 7, 64);
		}
		expect(getCurrentUI()).to_equal(&shellHostUI);
		closeGuest();
	});

	it("an audition pad and the sequence-direction pad open the note-row editor on PLAY DIRECTION from a closed menu", _ {
		const spec::ShortcutLayout pinned_v3{deluge::project::ShortcutsVersion::V3}; // the layout the lookups name
		// `noteRowEditorRootMenu.focusChild(&sequenceDirectionMenu)`, then `enterNoteRowEditor`.
		deluge::gui::ui::wireShellNoteBridge();
		HorizontalMenusGuard horizontal{RuntimeFeatureStateToggle::On};
		RootUIGuard root_guard;
		deluge::ui::scenes::spec::UiModeGuard mode;
		PopupSpy spy;
		DisplayGuard shown{spy};
		deluge::ui::scenes::spec::SongFixture f = deluge::ui::scenes::spec::make_synth_song();
		setRootUILowLevel(&instrumentClipView);
		HeldAudition audition{2};
		expect(menu::shortcut_node(menu::MenuContext::Root::Sound, deluge::project::ShortcutsVersion::V3, false, 4, 7)).to_equal(&nodes::sequence_direction);

		expect(entry::menuShortcutPadAction(4, 7, true)).to_equal(ActionResult::DEALT_WITH);
		expect(getCurrentUI()).to_equal(&shellHostUI);
		expect(deluge::ui::scenes::menu_scene().menu_context().root).to_equal(menu::MenuContext::Root::NoteRowEditor);
		expect(deluge::ui::scenes::menu_scene().focused_value()).to_equal(&nodes::sequence_direction);
		// The page with the column focused, as `focusChild` leaves it: not PLAY DIRECTION's own screen.
		expect(deluge::ui::scenes::menu_scene().focused()).not_().to_equal(&nodes::sequence_direction);
		closeGuest();
	});

	it("lands on PLAY DIRECTION even when the note-row editor last sat on another row", _ {
		deluge::gui::ui::wireShellNoteBridge();
		HorizontalMenusGuard horizontal{RuntimeFeatureStateToggle::On};
		RootUIGuard root_guard;
		deluge::ui::scenes::spec::UiModeGuard mode;
		PopupSpy spy;
		DisplayGuard shown{spy};
		deluge::ui::scenes::spec::SongFixture f = deluge::ui::scenes::spec::make_synth_song();
		setRootUILowLevel(&instrumentClipView);
		HeldAudition audition{2};

		expect(instrumentClipView.enterNoteRowEditor()).to_be_true();
		const menu::MenuContext ctx = deluge::ui::scenes::menu_scene().menu_context();
		deluge::ui::scenes::menu_scene().reroot(*menu::MenuScene::roots()[static_cast<size_t>(ctx.root)], ctx,
		                                    nodes::note_row_probability);
		for (int32_t i = 0; i < 3; i++) {
			(void)shellHostUI.timerCallback();
		}
		expect(deluge::ui::scenes::menu_scene().focused_value()).to_equal(&nodes::note_row_probability);
		closeGuest();
		// The close ended the mode the audition pad is still holding, and the hierarchy it was closed over.
		setRootUILowLevel(&instrumentClipView);
		currentUIMode = UI_MODE_AUDITIONING;

		expect(entry::menuShortcutPadAction(4, 7, true)).to_equal(ActionResult::DEALT_WITH);
		expect(deluge::ui::scenes::menu_scene().focused_value()).to_equal(&nodes::sequence_direction);
		closeGuest();
	});

	it("re-roots the open note-row editor on PLAY DIRECTION's page and takes the popup down, as setup() does", _ {
		// `enterNoteRowEditor` with the sound editor open: `focusChild(&sequenceDirectionMenu)`, then `setup()`, which
		// ends with `display->cancelPopup()`.
		deluge::gui::ui::wireShellNoteBridge();
		HorizontalMenusGuard horizontal{RuntimeFeatureStateToggle::On};
		RootUIGuard root_guard;
		deluge::ui::scenes::spec::UiModeGuard mode;
		PopupSpy spy;
		DisplayGuard shown{spy};
		deluge::ui::scenes::spec::SongFixture f = deluge::ui::scenes::spec::make_synth_song();
		setRootUILowLevel(&instrumentClipView);
		HeldAudition audition{2};

		expect(instrumentClipView.enterNoteRowEditor()).to_be_true();
		const menu::MenuContext ctx = deluge::ui::scenes::menu_scene().menu_context();
		deluge::ui::scenes::menu_scene().reroot(*menu::MenuScene::roots()[static_cast<size_t>(ctx.root)], ctx,
		                                    nodes::note_row_probability);
		for (int32_t i = 0; i < 3; i++) {
			(void)shellHostUI.timerCallback();
		}
		expect(deluge::ui::scenes::menu_scene().focused_value()).to_equal(&nodes::note_row_probability);

		const int32_t cancels = spy.cancels;
		expect(entry::openShellNoteRowEditor(f.clip, true)).to_be_true();
		for (int32_t i = 0; i < 3; i++) {
			(void)shellHostUI.timerCallback();
		}
		expect(spy.cancels).to_be_greater_than(cancels);
		expect(deluge::ui::scenes::menu_scene().focused_value()).to_equal(&nodes::sequence_direction);
		expect(deluge::ui::scenes::menu_scene().focused()).not_().to_equal(&nodes::sequence_direction);
		closeGuest();
	});

	it("a range-dependent pad on a multisampled source opens the range picker first, as setup() does", _ {
		const spec::ShortcutLayout pinned_v3{deluge::project::ShortcutsVersion::V3}; // the layout the lookups name
		// `setup()`'s MUST_SELECT_RANGE arm: `multiRangeMenu.menuItemHeadingTo = item; newItem = &multiRangeMenu`.
		deluge::ui::scenes::spec::SongFixture f = deluge::ui::scenes::spec::make_synth_song();
		auto* sound = deluge::model::effects_owner_of(deluge::model::owner_of(*f.instrument))->sound;
		sound->sourceResources[0].ranges.insertMultiRange(0);
		sound->sourceResources[0].ranges.insertMultiRange(1);
		deluge::edits::recordRanges(*sound, 0);
		expect(menu::shortcut_node(menu::MenuContext::Root::Sound, deluge::project::ShortcutsVersion::V3, false, 2, 1)).to_equal(&nodes::source0_transpose);

		expect(entry::openMenuAtShortcut(f.clip, 2, 1, false, false, false, false)).to_be_true();
		expect(deluge::ui::scenes::menu_scene().focused()).to_equal(&nodes::multi_range);
		expect(deluge::ui::scenes::menu_scene().state().walk.pending_after_range).to_equal(&nodes::source0_transpose);
		closeGuest();
	});

	it("takes the sequence-direction pad whole over the automation view with the menu current, as main swallows it", _ {
		// `getCurrentUI() == &soundEditor` with the automation view as root and a kind-NONE item: DEALT_WITH, before
		// the sequence-direction arm.
		deluge::gui::ui::wireShellNoteBridge();
		HorizontalMenusGuard horizontal{RuntimeFeatureStateToggle::On};
		RootUIGuard root_guard;
		deluge::ui::scenes::spec::UiModeGuard mode;
		PopupSpy spy;
		DisplayGuard shown{spy};
		deluge::ui::scenes::spec::SongFixture f = deluge::ui::scenes::spec::make_synth_song();
		setRootUILowLevel(&automationView);
		expect(entry::openMenu(f.clip, 0, false, false, false, false)).to_be_true();
		const menu::MenuContext::Root before = deluge::ui::scenes::menu_scene().menu_context().root;
		HeldAudition audition{2};
		HeldShift shift;

		expect(entry::menuShortcutPadAction(4, 7, true)).to_equal(ActionResult::DEALT_WITH);
		expect(getCurrentUI()).to_equal(&shellHostUI);
		expect(deluge::ui::scenes::menu_scene().menu_context().root).to_equal(before);
		closeGuest();
	});

	it("passes a release, a sidebar pad and an ungated press back to the view", _ {
		RootUIGuard root_guard;
		deluge::ui::scenes::spec::UiModeGuard mode;
		deluge::ui::scenes::spec::SongFixture f = deluge::ui::scenes::spec::make_synth_song();
		(void)f;
		setRootUILowLevel(&instrumentClipView);
		expect(entry::menuShortcutPadAction(8, 7, true)).to_equal(ActionResult::NOT_DEALT_WITH);
		HeldShift shift;
		expect(entry::menuShortcutPadAction(8, 7, false)).to_equal(ActionResult::NOT_DEALT_WITH);
		expect(entry::menuShortcutPadAction(kDisplayWidth, 3, true)).to_equal(ActionResult::NOT_DEALT_WITH);
		expect(isUIOpen(&shellHostUI)).to_be_false();
	});
});

describe menu_entry_published_target("the menu's owner and params, which the automation view and the kit's stacks read", $ {
	it("names a kit row's drum and its note row's params", _ {
		RootUIGuard root_guard;
		deluge::ui::scenes::spec::UiModeGuard mode;
		deluge::ui::scenes::spec::SongFixture kit = deluge::ui::scenes::spec::make_kit_song();
		SoundDrum* drum = deluge::ui::scenes::spec::add_drum<SoundDrum>(kit);
		NoteRow* row = static_cast<InstrumentClip*>(kit.clip)->getNoteRowForDrum(drum);
		setRootUILowLevel(&instrumentClipView);
		expect(entry::openMenu(kit.clip, 0, false, false, false, /*kitGlobalFx=*/false)).to_be_true();
		expect(std::get<Sound*>(entry::sound_menu_owner())).to_equal(static_cast<Sound*>(drum));
		expect(entry::soundMenuParamManager()).to_equal(&row->paramManager);
		closeGuest();
	});

	it("builds the kit's automation stack under the menu on the published row", _ {
		RootUIGuard root_guard;
		deluge::ui::scenes::spec::UiModeGuard mode;
		deluge::ui::scenes::spec::SongFixture kit = deluge::ui::scenes::spec::make_kit_song();
		SoundDrum* drum = deluge::ui::scenes::spec::add_drum<SoundDrum>(kit);
		NoteRow* row = static_cast<InstrumentClip*>(kit.clip)->getNoteRowForDrum(drum);
		setRootUILowLevel(&instrumentClipView);
		expect(entry::openMenu(kit.clip, 0, false, false, false, /*kitGlobalFx=*/false)).to_be_true();
		alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* stack = setupModelStackWithTimelineCounter(memory, kit.song, kit.clip);
		ModelStackWithAutoParam* param = kit.instrument->getModelStackWithParam(
		    stack, kit.clip, deluge::project::engine_target(deluge::instruments::classic::param::kLpfFrequency.value),
		    /*affectEntire=*/false, /*useMenuStack=*/true, /*forEdit=*/false);
		expect(param).not_().to_be_null();
		if (param != nullptr) {
			expect(param->paramManager).to_equal(&row->paramManager);
			Sound* const sound = drum;
			expect(std::get<Sound*>(param->owner)).to_equal(sound);
		}
		closeGuest();
	});

	it("with the latch set under the drum menu, the automation view's kit-wide stack is the kit's own", _ {
		// `SoundEditor::setup` tests `setupKitGlobalFXMenu` ahead of the selected drum: the kit's own effects and the
		// clip's params, under the drum root the clip's affect-entire picks.
		(void)quiet_audio();
		RootUIGuard root_guard;
		deluge::ui::scenes::spec::UiModeGuard mode;
		LatchedKit k;
		NoteRow* row = static_cast<InstrumentClip*>(k.f.clip)->getNoteRowForDrum(k.drum);
		k.f.clip->affectEntire = false;
		setRootUILowLevel(&automationView);
		expect(entry::openMenu(k.f.clip, 0, false, false, false, /*kitGlobalFx=*/false)).to_be_true();
		expect(deluge::ui::scenes::menu_scene().menu_context().root).to_equal(menu::MenuContext::Root::Drum);
		expect(automationView.getAffectEntire()).to_be_true();
		alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* stack = setupModelStackWithTimelineCounter(memory, k.f.song, k.f.clip);
		ModelStackWithAutoParam* param = automationView.getModelStackWithParamForClip(
		    stack, k.f.clip, /*forEdit=*/false, deluge::modulation::params::UNPATCHED_STUTTER_RATE,
		    deluge::modulation::params::Kind::UNPATCHED_GLOBAL);
		expect(param).not_().to_be_null();
		if (param != nullptr) {
			expect(param->paramManager).to_equal(&static_cast<InstrumentClip*>(k.f.clip)->paramManager);
			expect(param->paramManager).not_().to_equal(&row->paramManager);
			expect(std::get<Kit*>(param->owner)).to_equal(static_cast<Kit*>(k.f.instrument));
		}
		closeGuest();
	});

	it("arms the automation display when playback automates the published params", _ {
		RootUIGuard root_guard;
		deluge::ui::scenes::spec::UiModeGuard mode;
		deluge::ui::scenes::spec::SongFixture kit = deluge::ui::scenes::spec::make_kit_song();
		SoundDrum* drum = deluge::ui::scenes::spec::add_drum<SoundDrum>(kit);
		NoteRow* row = static_cast<InstrumentClip*>(kit.clip)->getNoteRowForDrum(drum);
		setRootUILowLevel(&instrumentClipView);
		expect(entry::openMenu(kit.clip, 0, false, false, false, /*kitGlobalFx=*/false)).to_be_true();
		// The knobs' own params are the other arm of the test; with them released only the menu's can match.
		const deluge::ui::controls::KnobFocus savedFocus = view.knobFocus();
		view.releaseActiveModControllable();
		uiTimerManager.unsetTimer(TimerName::DISPLAY_AUTOMATION);
		view.notifyParamAutomationOccurred(&row->paramManager);
		expect(uiTimerManager.isTimerSet(TimerName::DISPLAY_AUTOMATION)).to_be_true();
		uiTimerManager.unsetTimer(TimerName::DISPLAY_AUTOMATION);
		view.knobFocus() = savedFocus;
		view.knobFocusChanged();
		closeGuest();
	});
	it("clears the published owner and params when a song swap resets the menu's context", _ {
		// `MenuScene::on_model_changed`'s `SongSwapped` arm drops the context, whose addresses named the old song; the
		// published pair goes with it rather than outliving the song.
		RootUIGuard root_guard;
		deluge::ui::scenes::spec::UiModeGuard mode;
		deluge::ui::scenes::spec::SongFixture kit = deluge::ui::scenes::spec::make_kit_song();
		SoundDrum* drum = deluge::ui::scenes::spec::add_drum<SoundDrum>(kit);
		setRootUILowLevel(&instrumentClipView);
		expect(entry::openMenu(kit.clip, 0, false, false, false, /*kitGlobalFx=*/false)).to_be_true();
		expect(std::get<Sound*>(entry::sound_menu_owner())).to_equal(static_cast<Sound*>(drum));

		deluge::base::notify::DirtySet dirty;
		dirty.raise(deluge::base::notify::Dirty::SongSwapped);
		deluge::ui::scenes::menu_scene().on_model_changed(dirty);
		expect(deluge::model::is_none(entry::sound_menu_owner())).to_be_true();
		expect(entry::soundMenuParamManager()).to_be_null();
		closeGuest();
	});

	it("clears the published owner and params when the menu forgets its context for a teardown", _ {
		RootUIGuard root_guard;
		deluge::ui::scenes::spec::UiModeGuard mode;
		deluge::ui::scenes::spec::SongFixture kit = deluge::ui::scenes::spec::make_kit_song();
		SoundDrum* drum = deluge::ui::scenes::spec::add_drum<SoundDrum>(kit);
		setRootUILowLevel(&instrumentClipView);
		expect(entry::openMenu(kit.clip, 0, false, false, false, /*kitGlobalFx=*/false)).to_be_true();
		expect(std::get<Sound*>(entry::sound_menu_owner())).to_equal(static_cast<Sound*>(drum));

		entry::forgetMenuContext();
		expect(deluge::model::is_none(entry::sound_menu_owner())).to_be_true();
		expect(entry::soundMenuParamManager()).to_be_null();
		closeGuest();
	});

	it("publishes what an open made on the scene itself edits, as every open does", _ {
		// `MenuScene::open` installs its context through the same hook as a re-root and an adopt, so an open that does
		// not come through `openMenu` publishes too. The guest stays open, so no teardown publishes in between.
		RootUIGuard root_guard;
		deluge::ui::scenes::spec::UiModeGuard mode;
		deluge::ui::scenes::spec::SongFixture kit = deluge::ui::scenes::spec::make_kit_song();
		SoundDrum* drum = deluge::ui::scenes::spec::add_drum<SoundDrum>(kit);
		setRootUILowLevel(&instrumentClipView);
		expect(entry::openMenu(kit.clip, 0, false, false, false, /*kitGlobalFx=*/false)).to_be_true();
		expect(std::get<Sound*>(entry::sound_menu_owner())).to_equal(static_cast<Sound*>(drum));

		deluge::ui::scenes::menu_scene().open(nodes::root_song, menu::MenuContext{});
		expect(deluge::model::is_none(entry::sound_menu_owner())).to_be_true();
		expect(entry::soundMenuParamManager()).to_be_null();
		closeGuest();
	});
});

describe menu_entry_kit_affect_entire("editKitAffectEntire: a menu opened now edits the kit's own effects", $ {
	it("is true for a kit clip with affect-entire on, and false with it off", _ {
		deluge::ui::scenes::spec::SongFixture kit = deluge::ui::scenes::spec::make_kit_song();
		static_cast<InstrumentClip*>(kit.clip)->affectEntire = true;
		expect(entry::editKitAffectEntire()).to_be_true();
		static_cast<InstrumentClip*>(kit.clip)->affectEntire = false;
		expect(entry::editKitAffectEntire()).to_be_false();
	});

	it("is false for a synth clip with affect-entire set, and for an audio clip", _ {
		deluge::ui::scenes::spec::SongFixture synth = deluge::ui::scenes::spec::make_synth_song();
		static_cast<InstrumentClip*>(synth.clip)->affectEntire = true;
		expect(entry::editKitAffectEntire()).to_be_false();
		deluge::ui::scenes::spec::AudioSongFixture audio = deluge::ui::scenes::spec::make_audio_clip_song();
		(void)audio;
		expect(entry::editKitAffectEntire()).to_be_false();
	});
});

describe menu_entry_audition_end("a scene opened over an audition ends it, as main's opener does", $ {
	it("ends the clip view's audition as the recorder opens", _ {
		RootUIGuard root_guard;
		deluge::ui::scenes::spec::UiModeGuard mode;
		(void)quiet_audio();
		deluge::ui::scenes::spec::SongFixture f = deluge::ui::scenes::spec::make_synth_song();
		setRootUILowLevel(&instrumentClipView);
		currentUIMode = UI_MODE_AUDITIONING; // a row held as SELECT reaches the recorder item
		expect(entry::openMenu(f.clip, 0, false, false, false, false, entry::MenuNode::Osc0Recorder))
		    .to_be_true();
		(void)shellHostUI.timerCallback();
		expect(currentUIMode).to_equal(uint32_t{UI_MODE_NONE});
	});

	it("ends it as the sample browser opens", _ {
		RootUIGuard root_guard;
		deluge::ui::scenes::spec::UiModeGuard mode;
		(void)quiet_audio();
		deluge::ui::scenes::spec::SongFixture f = deluge::ui::scenes::spec::make_synth_song();
		setRootUILowLevel(&instrumentClipView);
		currentUIMode = UI_MODE_AUDITIONING;
		expect(entry::openMenuAtShortcut(f.clip, 0, 5, false, false, false, false)).to_be_true(); // BROWSE
		(void)shellHostUI.timerCallback();
		expect(currentUIMode).to_equal(uint32_t{UI_MODE_NONE});
	});

	it("ends it as the drum rename opens on an instrument clip, held", _ {
		RootUIGuard root_guard;
		deluge::ui::scenes::spec::UiModeGuard mode;
		deluge::ui::scenes::spec::SongFixture kit = deluge::ui::scenes::spec::make_kit_song();
		(void)deluge::ui::scenes::spec::add_drum<SoundDrum>(kit);
		setRootUILowLevel(&instrumentClipView);
		currentUIMode = UI_MODE_AUDITIONING;
		expect(entry::openMenuAtShortcut(kit.clip, 11, 5, false, false, false, false)).to_be_true(); // drum name
		(void)shellHostUI.timerCallback();
		expect(currentUIMode).to_equal(uint32_t{UI_MODE_NONE});
	});

	it("leaves a pad's audition state alone as the drum rename opens with nothing held", _ {
		RootUIGuard root_guard;
		deluge::ui::scenes::spec::UiModeGuard mode;
		deluge::ui::scenes::spec::SongFixture kit = deluge::ui::scenes::spec::make_kit_song();
		(void)deluge::ui::scenes::spec::add_drum<SoundDrum>(kit);
		setRootUILowLevel(&instrumentClipView);
		instrumentClipView.auditionPadIsPressed[0] = 1; // `cancelAllAuditioning` clears it only in the mode
		expect(entry::openMenuAtShortcut(kit.clip, 11, 5, false, false, false, false)).to_be_true();
		(void)shellHostUI.timerCallback();
		expect(currentUIMode).to_equal(uint32_t{UI_MODE_NONE});
		expect(instrumentClipView.auditionPadIsPressed[0]).to_equal(uint8_t{1});
		instrumentClipView.auditionPadIsPressed[0] = 0;
	});

	it("leaves an audition alone as the clip rename opens on an audio clip", _ {
		RootUIGuard root_guard;
		deluge::ui::scenes::spec::UiModeGuard mode;
		deluge::ui::scenes::spec::AudioSongFixture audio = deluge::ui::scenes::spec::make_audio_clip_song();
		setRootUILowLevel(&instrumentClipView);
		currentUIMode = UI_MODE_AUDITIONING;
		expect(entry::openMenuAtShortcut(audio.clip, 11, 5, false, false, false, false)).to_be_true();
		for (int i = 0; i < 3; i++) {
			(void)shellHostUI.timerCallback();
		}
		expect(deluge::ui::scenes::menu_scene().focused()).to_equal(&nodes::edit_name);
		(void)shellHostUI.buttonAction(deluge::hid::button::SELECT_ENC, true);
		(void)shellHostUI.buttonAction(deluge::hid::button::SELECT_ENC, false);
		for (int i = 0; i < 3; i++) {
			(void)shellHostUI.timerCallback();
		}
		expect(shellHostUI.shell().stack().input_top().kind()).to_equal(deluge::ui::scenes::rename::kRenameSceneKind);
		expect(currentUIMode).to_equal(uint32_t{UI_MODE_AUDITIONING});
	});

	it("ends the keyboard's audition as a sound's marker editor opens over the keyboard", _ {
		RootUIGuard root_guard;
		deluge::ui::scenes::spec::UiModeGuard mode;
		(void)quiet_audio();
		deluge::ui::scenes::spec::SongFixture f = deluge::ui::scenes::spec::make_synth_song();
		Sound* sound = deluge::model::effects_owner_of(deluge::model::owner_of(*f.instrument))->sound;
		give_drum_a_sample(f, *sound, make_marker_sample());
		setRootUILowLevel(&keyboardScreen);
		currentUIMode = UI_MODE_AUDITIONING;
		expect(entry::openMenuAtShortcut(f.clip, 0, 7, false, false, false, false)).to_be_true(); // START
		(void)shellHostUI.timerCallback();
		expect(currentUIMode).to_equal(uint32_t{UI_MODE_NONE});
	});

	it("leaves an audition alone when the marker editor opens over the clip view", _ {
		RootUIGuard root_guard;
		deluge::ui::scenes::spec::UiModeGuard mode;
		(void)quiet_audio();
		deluge::ui::scenes::spec::SongFixture f = deluge::ui::scenes::spec::make_synth_song();
		Sound* sound = deluge::model::effects_owner_of(deluge::model::owner_of(*f.instrument))->sound;
		give_drum_a_sample(f, *sound, make_marker_sample());
		setRootUILowLevel(&instrumentClipView);
		currentUIMode = UI_MODE_AUDITIONING;
		expect(entry::openMenuAtShortcut(f.clip, 0, 7, false, false, false, false)).to_be_true();
		(void)shellHostUI.timerCallback();
		expect(currentUIMode).to_equal(uint32_t{UI_MODE_AUDITIONING});
	});
});

// clang-format on

DELUGE_SPEC(menu_entry_spec, menu_entry_roots, menu_entry_node_arg, menu_entry_shortcut, menu_entry_refusal,
            menu_entry_undo_log, menu_entry_screensaver, menu_entry_tick_rate, menu_entry_midi_learn,
            menu_entry_automation_notes, menu_entry_close_mode, menu_entry_reopen, menu_entry_shortcut_whole,
            menu_entry_published_target, menu_entry_kit_affect_entire, menu_entry_audition_end)
