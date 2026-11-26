#pragma once

/// @file
/// @brief The sample browser's fixtures, which `browser_scene_spec` and the menu oracle freezer share: the scratch
///        card, the per-example reset of the process's listing globals, a `Shell` over the scene with a dispatcher
///        that parks the listing op, and one side's answer (the navigation numbers, the text and the panel).

#include "gui/ui/menu_view_of.h"
#include "gui/ui/shell_host/host_ports.h"
#include "ui/scenes/browser/browser_scene.h"
#include "ui/scenes/menu/context.h"
#include "ui/scenes/profile.h"
#include "ui/scenes/registry.h"

#include "base/notify/notice_queue.h"
#include "ui/shell/profiles/deluge.h"
#include "ui/shell/shell.h"
#include "ui/widgets/grid/keyboard.h"

#include "base/notify/dirty_set.h"
#include "gui/l10n/l10n.h"
#include "hid/display/display.h"
#include "model/sample/last_file_path_loaded.h"
#include "storage/file_catalogue.h"
#include "storage/file_item.h"
#include "util/name_compare.h"

#include "support/song_fixture.h"

// Before <cppspec.hpp>: it #defines `it`, which collides with every local named `it` in the project headers this
// drags in.
#include "support/browser_host_fake.h"
#include "support/card_access.h"

#include "support/menu_context.h"
#include "support/mocks/fake_surfaces.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace deluge::ui::scenes::spec::browser_fx {

using deluge::storage::FileCatalogue;
using deluge::ui::scenes::browser::browser_scene;
using deluge::ui::scenes::browser::BrowserId;
using deluge::ui::scenes::browser::kBrowserSceneKind;
using deluge::ui::scenes::menu::MenuContext;
using deluge::ui::shell::BoardProfile;
using deluge::ui::shell::SceneId;
using deluge::ui::shell::SceneKind;
using deluge::ui::shell::Shell;
using deluge::ui::shell::ShellConfig;

// ---------------------------------------------------------------------------------------------
// Process-global scaffolding

/// @brief A dispatcher that stores the listing op instead of running it, so an example decides when the listing lands.
struct Deferred {
	static inline void (*fn)(void*) = nullptr;
	static inline void* ctx = nullptr;
	/// Set false to model `Owner::run` returning false -- the queue-full drop.
	static inline bool accept = true;

	static bool dispatch(void (*f)(void*), void* c) {
		if (!accept) {
			return false;
		}
		fn = f;
		ctx = c;
		return true;
	}
	static bool pending() { return fn != nullptr; }
	/// @brief Run the stored op, as the owner fiber eventually would.
	static void run() {
		void (*f)(void*) = fn;
		void* c = ctx;
		fn = nullptr;
		ctx = nullptr;
		if (f != nullptr) {
			f(c);
		}
	}
	static void reset() {
		fn = nullptr;
		ctx = nullptr;
		accept = true;
	}
};

/// @brief The minimum `Display` a browser run needs.
///
/// `display` is null in a spec process, and main's `Browser::renderOLED` and
/// `clampFileSelectionAndScroll` both read `display->getNumBrowserAndMenuLines()`. 3 is
/// `OLED::getNumBrowserAndMenuLines`'s own literal -- note that main hard-codes it on **every** panel
/// height, which is where the port deliberately diverges (see the row-count note in `render_oled`).
struct BrowserDisplay final : deluge::hid::Display {
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
	bool hasPopup() override { return false; }
	bool hasPopupOfType(PopupType) override { return false; }
	void consoleText(char const*) override {}
	void timerRoutine() override {}
};

/// @brief Per-example reset of everything process-global this spec touches.
///
/// The scene is a singleton and its `Listing` is a member of it, so an example that left a listing
/// dispatched would decide the next example's result by link order -- and `set_dispatch` asserts the
/// seam is `Idle`. Quiescing is therefore three steps in order: run whatever op is parked, let
/// `on_exit` collect the result (which is also what hands `FileCatalogue`'s claim back), then clear
/// the catalogue.
///
/// `QuietAudio` is declared first so the latch is held before anything erases: without it a spec
/// that drives a listing does not fail, it segfaults (`support/card_access.h`).
struct CleanBrowser {
	QuietAudio quiet;
	deluge::hid::Display* saved_display;
	/// The host installed before the example, which `reset` replaces with the recording one.
	deluge::ui::scenes::browser::BrowserHost* saved_host;
	BrowserDisplay display_impl;

	CleanBrowser() : saved_display(display), saved_host(browser_scene().installed_host()) {
		display = &display_impl;
		reset();
	}
	~CleanBrowser() {
		reset();
		browser_scene().set_host(saved_host);
		display = saved_display;
	}
	static void reset() {
		// The scene's requests go to the recording host; an example reads them there and answers by hand.
		browser_scene().set_host(&fake_browser_host());
		while (Deferred::pending()) {
			Deferred::run();
		}
		// Collects an uncollected result (releasing the catalogue claim) and empties the catalogue.
		browser_scene().on_exit();
		fake_browser_host().clear();
		// Answers the stop that exit asked for, and drops the last file an example previewed, which a fresh browser
		// opens beside.
		browser_scene().preview_settled(false);
		deluge::model::lastFilePathLoaded().clear();
		Deferred::reset();
		FileCatalogue::emptyFileItems();
		FileCatalogue::numFileItemsDeletedAtStart = 0;
		FileCatalogue::numFileItemsDeletedAtEnd = 0;
		FileCatalogue::firstFileItemRemaining.clear();
		FileCatalogue::lastFileItemRemaining.clear();
		FileCatalogue::filenameToStartSearchAt = nullptr;
		FileCatalogue::currentDir.clear();
		// The comparator's globals decide what `strcmpspecial` *means*, so a stale `true` turns a
		// later example's expected order into a different valid order rather than into a crash.
		shouldInterpretNoteNames = false;
		octaveStartsFromA = false;
		deluge::l10n::chosenLanguage = &deluge::l10n::built_in::english;
		deluge::base::notify::Notice discard;
		while (deluge::base::notify::notices().pop(discard)) {}
		deluge::base::notify::dirty().drain();
		// Drop whatever ports the last example opened the scene with -- a fake on its stack among them -- for the
		// host's, which live as long as the program.
		MenuContext host{};
		host.ports = deluge::gui::shell_host::host_ports();
		browser_scene().open(host);
	}
	CleanBrowser(const CleanBrowser&) = delete;
	CleanBrowser& operator=(const CleanBrowser&) = delete;
};

/// @brief The card these examples list: `tests/fixtures/scenes_spec/browser_scene`, installed for this file before boot
///        (`tests/integration/checks/scenes_spec_cards.txt`). Constructing one checks it is the installed card.
///
/// ```
/// SAMPLES/CLAP.WAV  SAMPLES/SNARE.WAV  SAMPLES/TOM.WAV
/// SAMPLES/KICKS/KICK1.WAV  SAMPLES/KICKS/KICK2 WITH A LONG ROOM TAIL.WAV   (wider than the row: side-scrolls)
/// SAMPLES/CLIPS/TEMP/            (the folder SampleBrowser refuses to enter)
/// SAMPLES/CLIPS/{A3,C3,C4}.WAV   (note names -- see below)
/// MIDI_DEVICES/DEFINITION/DEVICE1.XML
/// ```
///
/// The three note-named files live one level down, in `CLIPS`, on purpose: the sample descriptor
/// interprets note names, so their order is the one thing on this card that **differs between the
/// two comparator pinnings** (`A3, C3, C4` with note names off; `C3, A3, C4` with them on, octave
/// from C), which is what the `repinComparator` example needs. Putting them at the top level would
/// have changed every recorded golden for a fixture detail.
struct ScratchCard {
	std::filesystem::path dir = std::filesystem::path(DELUGE_REPO_ROOT) / "tests/fixtures/scenes_spec/browser_scene";
	SdRoot root{dir.string()};
};

/// Board encoder indices and matrix coordinates (`ui/shell/profiles/deluge.cpp`).
inline constexpr uint8_t kSelectEncoder = 3;
inline constexpr uint8_t kSelectEncButtonX = 4, kSelectEncButtonY = 3;
/// SHIFT, at matrix (8, 0) (`ui/shell/profiles/deluge.cpp`).
inline constexpr uint8_t kShiftX = 8, kShiftY = 0;
inline constexpr uint8_t kBackX = 7, kBackY = 1;
/// X_ENC's push, LOAD and KEYBOARD, and the horizontal and vertical encoders' indices (`ui/shell/profiles/deluge.cpp`).
inline constexpr uint8_t kXEncX = 0, kXEncY = 1;
inline constexpr uint8_t kLoadX = 6, kLoadY = 1;
inline constexpr uint8_t kKeyboardX = 3, kKeyboardY = 3;
inline constexpr uint8_t kRecordX = 8, kRecordY = 2;
inline constexpr uint8_t kSaveX = 6, kSaveY = 3;
inline constexpr uint8_t kHorizontalEncoder = 1, kVerticalEncoder = 0;
inline constexpr int32_t kHomeRow = deluge::ui::widgets::grid::kTextKeyboardHomeRow;

struct Pad {
	int32_t x = -1, y = -1;
};

/// @brief The pad that types @p c on the QWERTY layout, found by asking the toolkit's own lookup
///        rather than transcribing the table a second time.
inline Pad pad_for(char c) {
	for (int32_t y = 0; y < 8; y++) {
		for (int32_t x = 0; x < deluge::ui::widgets::grid::kTextKeyboardCols; x++) {
			if (deluge::ui::widgets::grid::text_keyboard_char_at(deluge::ui::widgets::grid::KeyboardLayoutKind::Qwerty,
			                                                     x, y, kHomeRow, false)
			    == c) {
				return Pad{x, y};
			}
		}
	}
	return Pad{};
}

/// @brief A `Shell` over `FakeSurfaceSet` with the application's own scene registry installed.
struct Rig {
	const BoardProfile& profile;
	deluge::ui::shell::SceneRegistry reg = scene_registry();
	uitest::FakeSurfaceSet fake;
	Shell shell{fake, profile, reg, deluge::ui::shell::Services{}, ShellConfig{}};
	uint32_t clock = 0;

	Rig(const Rig&) = delete;
	Rig& operator=(const Rig&) = delete;

	/// @brief Tear the stack down, so every scene's `on_exit` always runs.
	///
	/// `BrowserScene` is a process singleton (`ui/shell/scene.h`'s `SceneState` cache): an example that
	/// left it -- or anything pushed above it -- on the stack would make the NEXT example's
	/// `on_enter` see stale state instead of a fresh one, the cross-contamination
	/// `support/recorder_scene_fixtures.h`'s `~Rig` was written to stop (a leaked recorder there makes the next
	/// `on_enter` refuse outright and fails every later example). Popping
	/// unconditionally means every example starts from the same place whatever the one before it did.
	~Rig() {
		while (shell.stack().depth() > 0) {
			shell.stack().pop(SceneKind::Null);
		}
	}

	explicit Rig(const DelugeBoard& board = deluge::ui::shell::kDelugeBoard, const BoardProfile& p = deluge_profile())
	    : profile(p), fake(board, p) {}

	/// @brief Open the browser on @p id over @p ctx, with the deferring dispatcher installed.
	/// @return What `Shell::init` reported.
	void open(const MenuContext& ctx, BrowserId id) {
		browser_scene().set_dispatch(&Deferred::dispatch);
		browser_scene().open(ctx);
		shell.init(SceneId{kBrowserSceneKind, static_cast<uint16_t>(id)});
	}

	void tick(uint32_t at) {
		clock = at;
		shell.tick(at);
	}
	/// @brief Advance one 10 ms tick.
	void step() { tick(clock + 10); }
	/// @brief Run the parked listing op and settle the scene on its result.
	///
	/// **Two ticks, and the second is not padding.** `Shell::tick` renders at steps 5-6 and calls
	/// `after_render()` at step 7 (`Shell::tick`), so the tick that *collects* the
	/// listing draws the state from before it landed; the `RepaintAll` that collection files is for
	/// the tick after. Anything comparing pixels needs both.
	void land() {
		Deferred::run();
		step();
		step();
	}
	/// @brief One more tick, so a change made after this tick's render reaches the framebuffer.
	///
	/// Needed after an encoder turn or a keystroke for the same reason `land()` takes two ticks: a
	/// turn is a widget-owned shape delivered at step 8, *after* the frame was built, and a typed
	/// character is acted on in `after_render` at step 7.
	void settle() { step(); }
	/// @brief Open, run the initial listing, and settle on the result.
	void open_and_land(const MenuContext& ctx, BrowserId id) {
		open(ctx, id);
		tick(0);
		land();
	}

	void button(uint8_t x, uint8_t y, bool on, uint32_t at) {
		shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_BUTTON, x, y, static_cast<int16_t>(on ? 255 : 0)}, at);
	}
	void pad(uint8_t x, uint8_t y, uint8_t velocity, uint32_t at) {
		shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_PAD, x, y, static_cast<int16_t>(velocity)}, at);
	}
	/// @brief One detent of the (detented, 2 edges per click) select encoder.
	void turn(int32_t detents, uint32_t at) {
		shell.on_board_event(
		    DelugeInputEvent{DELUGE_EVENT_ENCODER, kSelectEncoder, 0, static_cast<int16_t>(detents * 2)}, at);
	}

	/// @brief One detent of the horizontal encoder, then a tick.
	void horizontal(int32_t detents) {
		shell.on_board_event(
		    DelugeInputEvent{DELUGE_EVENT_ENCODER, kHorizontalEncoder, 0, static_cast<int16_t>(detents * 2)},
		    clock + 10);
		step();
	}
	/// @brief Press and release a button, one tick each.
	void click(uint8_t x, uint8_t y) {
		button(x, y, true, clock + 10);
		step();
		button(x, y, false, clock + 10);
		step();
	}

	/// @brief Press and release the pad that types @p c, one tick each.
	void type(char c) {
		const Pad p = pad_for(c);
		pad(static_cast<uint8_t>(p.x), static_cast<uint8_t>(p.y), 64, clock + 10);
		step();
		pad(static_cast<uint8_t>(p.x), static_cast<uint8_t>(p.y), 0, clock + 10);
		step();
	}
	void type(std::string_view s) {
		for (const char c : s) {
			type(c);
		}
	}
	/// @brief Press the enter pad -- the commit, through the real input path.
	void enter() { type(deluge::ui::widgets::grid::kTextKeyboardEnter); }
	void backspace() { type(deluge::ui::widgets::grid::kTextKeyboardBackspace); }

	/// @brief Press or release the backspace pad at @p at, without ticking.
	void pad_backspace(bool on, uint32_t at) {
		const Pad p = pad_for(deluge::ui::widgets::grid::kTextKeyboardBackspace);
		pad(static_cast<uint8_t>(p.x), static_cast<uint8_t>(p.y), on ? 64 : 0, at);
	}

	/// @brief Is a `SceneKind::Working` scene on the stack right now?
	bool working_shown() {
		for (const deluge::ui::shell::Scene* scene : shell.stack().scenes()) {
			if (scene->kind() == SceneKind::Working) {
				return true;
			}
		}
		return false;
	}
	bool browser_on_stack() {
		for (const deluge::ui::shell::Scene* scene : shell.stack().scenes()) {
			if (scene->kind() == kBrowserSceneKind) {
				return true;
			}
		}
		return false;
	}
};

inline MenuContext context_of(spec::SongFixture& f) {
	return spec::ctx_of(*f.song, deluge::gui::menuViewOf(f.clip), 0, false, false, false,
	                    /*kit_global_fx=*/false);
}

inline MenuContext context_of(spec::AudioSongFixture& f) {
	return spec::ctx_of(*f.song, deluge::gui::menuViewOf(f.clip), 0, false, false, false,
	                    /*kit_global_fx=*/false);
}

/// @brief One run's answer: the four navigation numbers, the text, and the panel.
struct BrowserState {
	int32_t index = 0;
	int32_t scroll = 0;
	int32_t cursor = 0;
	int32_t text_scroll = 0;
	std::string text;
	std::string pbm;
};

/// @brief Hold the fixture to the arm of main's `SampleBrowser::opened` the reference rows were taken on: a holder
///        that names no file, with no last-loaded path, enters `SAMPLES` with no file to search for.
///
/// If a future fixture gave the audio clip a real sample path, main would enter on that file's folder and the rows
/// would stop describing it. This fails first. Read off the model, not off either side's dissection of it --
/// `SoundEditor::getCurrentAudioFileHolder` (`origin/main:src/deluge/gui/ui/sound_editor.cpp`) resolves an audio clip
/// to its own `sampleHolder`.
inline bool blank_start_path(spec::AudioSongFixture& f) {
	return f.clip->sampleHolder.filePath.empty();
}

} // namespace deluge::ui::scenes::spec::browser_fx
