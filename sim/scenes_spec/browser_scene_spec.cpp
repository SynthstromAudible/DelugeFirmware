/// @file
/// @brief `BrowserScene` -- the one scene that replaces the two `gui/ui/browser/` subclasses the
///        ported menu tree reaches -- driven through a real `Shell` over `uitest::FakeSurfaceSet`,
///        against a real card.
///
/// Three halves: the `Descriptor` table (what each browser is for, as data), the scene's behaviour
/// over the async listing seam, and its pixels compared **against the legacy renderer itself**.
///
/// @par Why every example installs a deferring dispatcher
///
/// **On the host `storage::Owner::run` is inline** (`storage/owner.h:55-59`), so with the shipped
/// dispatcher a listing has already finished by the time `request()` returns, and the in-flight
/// state -- which is most of this scene -- can never be observed at all. `BrowserScene::set_dispatch`
/// exists for that and nothing else; `Deferred` below stores the op and runs it when an example says
/// so. That reproduces the one property of the Embassy path the scene depends on -- *dispatch
/// returns before the op runs* -- and reproduces nothing else. Every "in flight" assertion here is
/// an assertion about ordering, not about a second execution context. See `listing.h`'s own honesty
/// note; it applies unchanged one layer up.
///
/// @par Card hygiene
///
/// Nothing here writes to `tests/fixtures/card`. Every example that lists builds a scratch card under
/// the host temp directory and removes it again (`ScratchCard`), because the committed fixture is
/// flat and nearly empty -- it cannot descend, ascend, or produce a folder with enough entries to
/// search. `git status --porcelain tests/fixtures` stays clean.
///
/// @note The spec may include `gui/` (for `l10n::String` ids and the legacy renderer); the library
///       may not. Same rule `menu_scene_spec.cpp` and `context_menu_scene_spec.cpp` run under.

#include "scenes/browser/browser_scene.h"
#include "scenes/menu/context.h"
#include "scenes/profile.h"
#include "scenes/registry.h"

#include "notify/notice_queue.h"
#include "shell/profiles/deluge.h"
#include "shell/shell.h"
#include "ui/core/metrics.h"
#include "ui/grid/keyboard.h"

#include "gfx/oled/icons.h"

#include "gui/l10n/l10n.h"
#include "gui/l10n/strings.h"
#include "gui/ui/browser/sample_browser.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "model/clip/audio_clip.h"
#include "model/instrument/midi_instrument.h"
#include "storage/file_catalogue.h"
#include "storage/file_item.h"
#include "util/name_compare.h"

#include "support/song_fixture.h"

// Before <cppspec.hpp>: it #defines `it`, which collides with every local named `it` in the project
// headers this drags in (`audio_engine.h` -> `util/container/ordered_pos_vector.h`).
#include "support/card_access.h"

#include "spec/gfx/support/golden.h"
#include "spec/shell/support/fake_surfaces.h"

#include "cppspec.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "spec/support/spec_main.h"

using namespace deluge::scenes;
using deluge::l10n::String;
using deluge::scenes::browser::AcceptOutcome;
using deluge::scenes::browser::browser_scene;
using deluge::scenes::browser::BrowserId;
using deluge::scenes::browser::BrowserScene;
using deluge::scenes::browser::kBrowserSceneKind;
using deluge::scenes::menu::MenuContext;
using deluge::scenes::spec::QuietAudio;
using deluge::scenes::spec::SdRoot;
using deluge::storage::FileCatalogue;
using deluge::ui::shell::BoardProfile;
using deluge::ui::shell::ButtonId;
using deluge::ui::shell::ButtonSet;
using deluge::ui::shell::Result;
using deluge::ui::shell::SceneId;
using deluge::ui::shell::SceneKind;
using deluge::ui::shell::Shell;
using deluge::ui::shell::ShellConfig;
using deluge::ui::shell::SurfaceRole;
using deluge::ui::shell::UiContext;
using deluge::ui::shell::UiEvent;

namespace {

/// `Action` unqualified is the model's own undo `Action` (`model/action/action.h`, pulled in through
/// the model closure); the shell's is a different type with the same name.
using ShellAction = deluge::ui::shell::Action;

// ---------------------------------------------------------------------------------------------
// Process-global scaffolding

/// @brief A dispatcher that stores the op instead of running it -- see the file docstring.
struct Deferred {
	static inline void (*fn)(void*) = nullptr;
	static inline void* ctx = nullptr;
	/// Set false to model `Owner::run` returning false -- the queue-full drop (`owner.h:43`).
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

/// @brief The minimum `Display` the legacy renderer needs.
///
/// `Browser::renderOLED` reads `display->getNumBrowserAndMenuLines()` (`browser.cpp:945`) and
/// `clampFileSelectionAndScroll` reads it again (`:1029`); `display` is null in a spec process
/// (`hid/display/display.cpp:4`). 3 is `OLED::getNumBrowserAndMenuLines`'s own literal
/// (`hid/display/oled.h:152`) -- note that legacy hard-codes it on **every** panel height, which is
/// where the port deliberately diverges (see the row-count note in `render_oled`).
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

/// @brief `Browser`'s protected statics, reachable.
///
/// `fileIndexSelected` / `scrollPosVertical` (`browser.h:228,230`) and `enteredTextEditPos` /
/// `scrollPosHorizontal` (`qwerty_ui.h:63-64`) are protected **statics**, so a derived class can
/// write them and the shipped `sampleBrowser` object then renders from them -- they are the same
/// objects, not copies. `SampleBrowser` itself is `final`, which is why this derives from `Browser`.
class SpecBrowser : public Browser {
public:
	std::string getCurrentFilePath() override { return {}; }
	UIType getUIType() override { return UIType::LOAD_SONG; }
	void enterKeyPress() override {}

	/// @brief Point the legacy renderer at exactly what the scene is showing.
	/// @param index  The selected row.
	/// @param scroll The top visible row.
	/// @param text   `QwertyUI::enteredText`.
	/// @param cursor `QwertyUI::enteredTextEditPos`.
	/// @param hscroll `QwertyUI::scrollPosHorizontal`.
	static void mirror(int32_t index, int32_t scroll, std::string_view text, int32_t cursor, int32_t hscroll) {
		fileIndexSelected = index;
		scrollPosVertical = scroll;
		QwertyUI::enteredText = std::string{text};
		enteredTextEditPos = static_cast<int16_t>(cursor);
		scrollPosHorizontal = hscroll;
	}
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
	BrowserDisplay display_impl;

	CleanBrowser() : saved_display(display) {
		display = &display_impl;
		reset();
	}
	~CleanBrowser() {
		reset();
		display = saved_display;
	}
	static void reset() {
		while (Deferred::pending()) {
			Deferred::run();
		}
		// Collects an uncollected result (releasing the catalogue claim) and empties the catalogue.
		browser_scene().on_exit();
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
		deluge::notify::Notice discard;
		while (deluge::notify::notices().pop(discard)) {}
		deluge::notify::dirty().drain();
	}
	CleanBrowser(const CleanBrowser&) = delete;
	CleanBrowser& operator=(const CleanBrowser&) = delete;
};

/// @brief A scratch card with the two trees the descriptors browse.
///
/// The committed fixture (`tests/fixtures/card`) is flat and holds four invisible `.keep` files plus
/// one XML -- it cannot descend, cannot ascend, and cannot produce a folder with enough entries to
/// search. Under the host temp directory, removed on the way out.
///
/// ```
/// SAMPLES/CLAP.WAV  SAMPLES/SNARE.WAV  SAMPLES/TOM.WAV
/// SAMPLES/KICKS/KICK1.WAV
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
	std::filesystem::path dir;
	SdRoot root;

	static std::filesystem::path make() {
		std::filesystem::path d = std::filesystem::temp_directory_path() / "deluge_browser_scene_spec";
		std::filesystem::remove_all(d);
		std::filesystem::create_directories(d / "SAMPLES" / "KICKS");
		std::filesystem::create_directories(d / "SAMPLES" / "CLIPS" / "TEMP");
		std::filesystem::create_directories(d / "MIDI_DEVICES" / "DEFINITION");
		for (const char* name : {"CLAP.WAV", "SNARE.WAV", "TOM.WAV"}) {
			std::ofstream(d / "SAMPLES" / name) << "RIFF";
		}
		std::ofstream(d / "SAMPLES" / "KICKS" / "KICK1.WAV") << "RIFF";
		std::ofstream(d / "SAMPLES" / "CLIPS" / "TEMP" / "KEEP.WAV") << "RIFF";
		for (const char* name : {"A3.WAV", "C3.WAV", "C4.WAV"}) {
			std::ofstream(d / "SAMPLES" / "CLIPS" / name) << "RIFF";
		}
		// The shape `StorageManager::openMidiDeviceDefinitionFile` demands (root tag `midiDevice`,
		// `storage_manager.cpp:371`) with one tag `MIDIInstrument::readDeviceDefinitionFile` accepts
		// -- `hideUnlabeledCC` is the cheapest, and it is what turns `Error::FILE_UNREADABLE` into
		// `Error::NONE` (`midi_instrument.cpp:606-607`). Modelled on the shipped
		// `contrib/sd_card/MIDI_DEVICES/DEFINITION/Moog/Minitaur.XML`.
		std::ofstream(d / "MIDI_DEVICES" / "DEFINITION" / "DEVICE1.XML")
		    << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<midiDevice>\n"
		       "\t<hideUnlabeledCC\n\t\tvalue=\"0\" />\n</midiDevice>\n";
		return d;
	}

	ScratchCard() : dir(make()), root(dir.string()) {}
	~ScratchCard() { std::filesystem::remove_all(dir); }
	ScratchCard(const ScratchCard&) = delete;
	ScratchCard& operator=(const ScratchCard&) = delete;
};

/// @brief A 128x64 single-panel board, so the same scene can be recorded at the other shipped panel
///        geometry.
constexpr DelugeDisplayDesc kDisplays64[] = {{128, 64, 0}};

DelugeBoard board_64() {
	DelugeBoard b = deluge::ui::shell::kDelugeBoard;
	b.display_height = 64;
	b.display_count = 1;
	b.displays = kDisplays64;
	return b;
}

const BoardProfile& profile_64() {
	static const BoardProfile p = [] {
		static constexpr const deluge::ui::Style* kStyles[] = {&deluge::ui::kDelugeStyle64};
		BoardProfile q = deluge_profile();
		q.style_by_index = kStyles;
		return q;
	}();
	return p;
}

/// Board encoder indices and matrix coordinates (`shell/profiles/deluge.cpp`).
constexpr uint8_t kSelectEncoder = 3;
constexpr uint8_t kSelectEncButtonX = 4, kSelectEncButtonY = 3;
constexpr uint8_t kBackX = 7, kBackY = 1;
constexpr int32_t kHomeRow = deluge::ui::grid::kTextKeyboardHomeRow;

struct Pad {
	int32_t x = -1, y = -1;
};

/// @brief The pad that types @p c on the QWERTY layout, found by asking the toolkit's own lookup
///        rather than transcribing the table a second time.
Pad pad_for(char c) {
	for (int32_t y = 0; y < 8; y++) {
		for (int32_t x = 0; x < deluge::ui::grid::kTextKeyboardCols; x++) {
			if (deluge::ui::grid::text_keyboard_char_at(deluge::ui::grid::KeyboardLayoutKind::Qwerty, x, y, kHomeRow,
			                                            false)
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

	/// @brief Tear the stack down, so every scene's `on_exit` always runs (E3 task 10).
	///
	/// `BrowserScene` is a process singleton (`shell/scene.h`'s `SceneState` cache): an example that
	/// left it -- or anything pushed above it -- on the stack would make the NEXT example's
	/// `on_enter` see stale state instead of a fresh one, the cross-contamination
	/// `recorder_scene_spec.cpp`'s `~Rig` was written to stop (a leaked recorder there made the next
	/// `on_enter` refuse outright and killed 13 of 16 examples on one mutant's first run). Popping
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
	/// `after_render()` at step 7 (`shell/shell.cpp:334-364`), so the tick that *collects* the
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
	void enter() { type(deluge::ui::grid::kTextKeyboardEnter); }
	void backspace() { type(deluge::ui::grid::kTextKeyboardBackspace); }

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

MenuContext context_of(spec::SongFixture& f) {
	return menu::context_for(*f.song, f.clip, 0, false, false, false, /*kit_global_fx=*/false);
}

MenuContext context_of(spec::AudioSongFixture& f) {
	return menu::context_for(*f.song, f.clip, 0, false, false, false, /*kit_global_fx=*/false);
}

/// @brief The catalogue's contents as one comma-joined string, so an order mismatch prints as an
///        order rather than as a container diff.
std::string catalogue_order() {
	std::string out;
	for (const FileItem& item : FileCatalogue::fileItems) {
		if (!out.empty()) {
			out += ",";
		}
		out += item.displayName();
	}
	return out;
}

/// @brief Drop the rows the faceplate hides from a PBM.
///
/// The port's root frame has origin `(0, visible_top)` and cannot ink above it; legacy draws into a
/// raw canvas with no such bound. Comparing below the bezel is comparing exactly the rows that reach
/// the glass -- the same normalisation `context_menu_scene_spec.cpp` makes for the same reason.
/// @param pbm         A P1 PBM as `page_major_to_pbm` writes it.
/// @param visible_top How many leading pixel rows the faceplate hides.
/// @return The same PBM with those rows removed from the body.
std::string below_bezel(const std::string& pbm, int visible_top) {
	size_t at = 0;
	for (int i = 0; i < 2 + visible_top; i++) {
		at = pbm.find('\n', at) + 1;
	}
	return pbm.substr(at);
}

/// @brief The REAL legacy renderer's output for whatever the scene is currently showing.
///
/// Not a transcription: `sampleBrowser` is the shipped object (its constructor set `fileIcon` to
/// `OLED::waveIcon` and `title` to "Audio files", `sample_browser.cpp:88-89`), the catalogue it reads
/// is the same `FileCatalogue::fileItems` the scene just listed, and the selection is mirrored onto
/// legacy's own statics first. This is `Browser::renderOLED` (`browser.cpp:925`) itself.
///
/// @return The 128x48 canvas legacy would have shown, as a PBM.
std::string legacy_pbm() {
	const BrowserScene& s = browser_scene();
	SpecBrowser::mirror(s.file_index(), s.scroll(), s.entered_text(), s.cursor(), s.text_scroll());
	static deluge::hid::display::oled_canvas::Canvas canvas;
	canvas.clear();
	sampleBrowser.renderOLED(canvas);
	return page_major_to_pbm(canvas.bytes().data(), 128, 48);
}

/// @brief The first action of kind @p k in @p r, or null.
const ShellAction* find_action(const Result& r, ShellAction::Kind k) {
	for (const ShellAction& a : r.actions) {
		if (a.kind == k) {
			return &a;
		}
	}
	return nullptr;
}

/// @brief A `UiContext` for a directly-invoked `handle()` call: no lowering, no held buttons.
struct DirectCtx {
	ButtonSet held{};
	deluge::ui::PadMask pads{};
	UiContext ctx;
	explicit DirectCtx(uint32_t now_ms = 0) : ctx{held, pads, false, false, now_ms, deluge_profile()} {}
};

} // namespace

// clang-format off
describe browser_descriptors("scenes::browser descriptor table", $ {
	it("covers exactly the two browsers the ported menu tree reaches", _ {
		// Legacy has nine `Browser` subclasses. Only `SampleBrowser` and
		// `LoadMidiDeviceDefinitionUI` are opened from a menu item; the load/save family is opened
		// from root UIs and is a later plan's. See `BrowserId`.
		expect(static_cast<int>(BrowserId::Count)).to_equal(2);
	});

	it("carries each subclass's own extensions, folder and note-name rule", _ {
		const browser::Descriptor& sample = browser::descriptor(BrowserId::Sample);
		const browser::Descriptor& midi = browser::descriptor(BrowserId::MidiDeviceDefinition);

		// `allowedFileExtensionsAudio` (`sample_browser.cpp:85`) vs the base's
		// `allowedFileExtensionsXML` (`browser.cpp:181`).
		expect(std::string{sample.extensions[0]}).to_equal(std::string{"WAV"});
		expect(std::string{sample.extensions[1]}).to_equal(std::string{"AIFF"});
		expect(std::string{sample.extensions[2]}).to_equal(std::string{"AIF"});
		expect(sample.extensions[3] == nullptr).to_be_true();
		expect(std::string{midi.extensions[0]}).to_equal(std::string{"XML"});

		expect(std::string{sample.default_dir}).to_equal(std::string{"SAMPLES"});
		expect(std::string{midi.default_dir}).to_equal(std::string{"MIDI_DEVICES/DEFINITION"});

		// `shouldInterpretNoteNamesForThisBrowser = true` (`sample_browser.cpp:92`); the base leaves
		// it false (`browser.cpp:92`).
		expect(sample.interpret_note_names).to_be_true();
		expect(midi.interpret_note_names).to_be_false();

		// `SampleBrowser::opened` sets `allowFoldersSharingNameWithFile = true`
		// (`sample_browser.cpp:112`); `Browser::opened` leaves it false (`browser.cpp:182`).
		expect(sample.allow_folders_sharing_name_with_file).to_be_true();
		expect(midi.allow_folders_sharing_name_with_file).to_be_false();

		expect(std::string{sample.title}).to_equal(std::string{"Audio files"});
		expect(std::string{midi.title}).to_equal(std::string{"Load midi device"});
	});

	it("gives each browser its own row icon, and only the MIDI one a second piece", _ {
		// `SampleBrowser`'s `fileIcon = OLED::waveIcon` (`sample_browser.cpp:88`);
		// `LoadMidiDeviceDefinitionUI`'s `fileIcon = OLED::midiIcon` plus `fileIconPt2 =
		// OLED::midiIconPt2` at width 1 (`load_midi_device_definition_ui.cpp:87-90`).
		const browser::Descriptor& sample = browser::descriptor(BrowserId::Sample);
		const browser::Descriptor& midi = browser::descriptor(BrowserId::MidiDeviceDefinition);
		expect(sample.file_icon == &deluge::gfx::oled::icons::browserWave).to_be_true();
		expect(sample.file_icon_pt2 == nullptr).to_be_true();
		expect(midi.file_icon == &deluge::gfx::oled::icons::browserMidi).to_be_true();
		expect(midi.file_icon_pt2 == &deluge::gfx::oled::icons::browserMidiEdge).to_be_true();
		expect(static_cast<int>(midi.file_icon_pt2->width)).to_equal(1);
		expect(static_cast<int>(sample.file_icon->width)).to_equal(8);
	});

	it("ports the legacy icon bitmaps byte for byte", _ {
		// The four `Icon`s added to `gfx/oled/icons.cpp` are the legacy arrays
		// (`hid/display/oled_icons.cpp:23,34,82,91`), and this compares them against those arrays
		// rather than re-spelling the bits: a transcription typo would otherwise only show up as a
		// golden nobody could read.
		const deluge::gfx::oled::Icon& folder = deluge::gfx::oled::icons::browserFolder;
		const deluge::gfx::oled::Icon& wave = deluge::gfx::oled::icons::browserWave;
		const deluge::gfx::oled::Icon& midi = deluge::gfx::oled::icons::browserMidi;
		for (int i = 0; i < 8; i++) {
			expect(static_cast<int>(folder.data[i]))
			    .to_equal(static_cast<int>(deluge::hid::display::OLED::folderIcon[i]));
			expect(static_cast<int>(wave.data[i]))
			    .to_equal(static_cast<int>(deluge::hid::display::OLED::waveIcon[i]));
			expect(static_cast<int>(midi.data[i]))
			    .to_equal(static_cast<int>(deluge::hid::display::OLED::midiIcon[i]));
		}
		// Legacy blits only the first column of `midiIconPt2` (`fileIconPt2Width = 1`).
		expect(static_cast<int>(deluge::gfx::oled::icons::browserMidiEdge.data[0]))
		    .to_equal(static_cast<int>(deluge::hid::display::OLED::midiIconPt2[0]));
	});
});

describe browser_composition("scenes::browser::BrowserScene composition", $ {
	it("registers at FirstApp + 3 and covers the screen it draws", _ {
		const deluge::ui::shell::SceneRegistry reg = scene_registry();
		expect(reg.find(kBrowserSceneKind) == &browser_scene()).to_be_true();
		expect(static_cast<int>(kBrowserSceneKind)).to_equal(static_cast<int>(SceneKind::FirstApp) + 3);

		const deluge::ui::shell::Scene& s = browser_scene();
		// Unlike `RenameScene`/`ContextMenuScene`, neither legacy browser sets
		// `oledShowsUIUnderneath`: `renderOLED` starts with `drawScreenTitle` and owns the panel.
		expect(s.oled_transparent(SurfaceRole::Main)).to_be_false();
		expect(s.is_grid_overlay()).to_be_true();
		expect(s.occludes_grid()).to_be_false();
		expect(s.dims_grid()).to_be_false();
		expect(s.input_transparent()).to_be_false();
		// Both subclasses' `getGreyoutColsAndRows` set `*cols = 0xFFFFFFFF`.
		expect(static_cast<uint64_t>(s.greyout_mask().cols)).to_equal(uint64_t{0xFFFFFFFFu});
		expect(static_cast<uint64_t>(s.greyout_mask().rows)).to_equal(uint64_t{0});
	});
});

describe browser_open("scenes::browser::BrowserScene opening", $ {
	it("lists the descriptor's default folder and lands on the first entry", _ {
		CleanBrowser clean;
		ScratchCard card;
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		Rig r;
		r.open_and_land(context_of(f), BrowserId::Sample);

		// `SampleBrowser::opened` with a blank holder path defaults to the SAMPLES folder
		// (`sample_browser.cpp:141-143`).
		expect(std::string{browser_scene().current_dir()}).to_equal(std::string{"SAMPLES"});
		// Three files and two folders, sorted. `CLIPS` and `KICKS` are directories.
		expect(catalogue_order()).to_equal(std::string("CLAP.WAV,CLIPS,KICKS,SNARE.WAV,TOM.WAV"));
		// `arrivedInNewFolder` with no search string selects entry 0 and shows its name with the
		// extension stripped (`browser.cpp:394`, `:1316 setEnteredTextFromCurrentFilename`).
		expect(browser_scene().file_index()).to_equal(0);
		expect(browser_scene().scroll()).to_equal(0);
		expect(std::string{browser_scene().entered_text()}).to_equal(std::string{"CLAP"});
	});

	it("opens the MIDI browser on its own folder and extension", _ {
		CleanBrowser clean;
		ScratchCard card;
		spec::SongFixture f = spec::make_midi_song();
		Rig r;
		r.open_and_land(context_of(f), BrowserId::MidiDeviceDefinition);

		expect(std::string{browser_scene().current_dir()})
		    .to_equal(std::string{"MIDI_DEVICES/DEFINITION"});
		expect(catalogue_order()).to_equal(std::string("DEVICE1.XML"));
		expect(std::string{browser_scene().entered_text()}).to_equal(std::string{"DEVICE1"});
	});

	it("refuses to open when the descriptor cannot resolve its model", _ {
		CleanBrowser clean;
		{
			// The sample browser over a MIDI clip: no audio-file holder at all, which legacy would
			// answer by dereferencing null (`sample_browser.cpp:131`).
			spec::SongFixture f = spec::make_midi_song();
			expect(browser::descriptor(BrowserId::Sample).can(context_of(f))).to_be_false();
			browser_scene().open(context_of(f));
			expect(browser_scene().on_enter(SceneId{kBrowserSceneKind,
			                                        static_cast<uint16_t>(BrowserId::Sample)})).to_be_false();
		}
		{
			// The MIDI-definition browser over an audio clip: `LoadMidiDeviceDefinitionUI::opened`
			// returns false unless the output is `OutputType::MIDI_OUT` (`:45-47`).
			spec::AudioSongFixture f = spec::make_audio_clip_song();
			expect(browser::descriptor(BrowserId::MidiDeviceDefinition).can(context_of(f))).to_be_false();
			browser_scene().open(context_of(f));
			expect(browser_scene().on_enter(
			    SceneId{kBrowserSceneKind,
			            static_cast<uint16_t>(BrowserId::MidiDeviceDefinition)})).to_be_false();
		}
		// ...and an argument past the end of the table is refused too.
		expect(browser_scene().on_enter(SceneId{kBrowserSceneKind, 99})).to_be_false();
	});

	it("forgets its context on exit, so a re-entry without open() refuses", _ {
		CleanBrowser clean;
		ScratchCard card;
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		Rig r;
		r.open_and_land(context_of(f), BrowserId::Sample);
		expect(browser_scene().file_index()).to_equal(0);

		browser_scene().on_exit();
		// Same fail-closed rule `RenameScene::on_exit` records: a zeroed context has no audio file
		// holder, every descriptor's `can` rejects that, and `on_enter` refuses rather than opening
		// on the previous song's pointers.
		expect(browser_scene().on_enter(SceneId{kBrowserSceneKind,
		                                        static_cast<uint16_t>(BrowserId::Sample)})).to_be_false();
	});
});

describe browser_in_flight("scenes::browser::BrowserScene while a listing is in flight", $ {
	it("shows the Working scene, locks input, and refuses presses until the listing lands", _ {
		CleanBrowser clean;
		ScratchCard card;
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		Rig r;
		r.open(context_of(f), BrowserId::Sample);
		r.tick(0);

		// `Listing::request` raises `Notice::Kind::Working`, which E1's `Notices` turns into a
		// pushed `SceneKind::Working` (`shell/notices.cpp:260-265`). Nothing in this scene draws a
		// spinner; the busy indicator is the seam's.
		expect(browser_scene().listing_in_flight()).to_be_true();
		expect(r.working_shown()).to_be_true();
		expect(static_cast<int>(FileCatalogue::fileItems.size())).to_equal(0);

		// Legacy's `if (listingInProgress_) return;` at the top of `padAction`/`backButtonAction`
		// (`browser.cpp:1187`, `:1284`): the press reaches the scene and does nothing at all -- it
		// does not close the browser and does not start a second listing.
		r.button(kBackX, kBackY, true, 10);
		r.tick(10);
		expect(r.browser_on_stack()).to_be_true();
		expect(browser_scene().listing_in_flight()).to_be_true();

		// And now it lands: the animation comes down and the entries are there.
		r.land();
		expect(browser_scene().listing_in_flight()).to_be_false();
		expect(r.working_shown()).to_be_false();
		expect(static_cast<int>(FileCatalogue::fileItems.size())).to_equal(5);
	});

	it("never takes the shell's input lock, and leaves it clear across a whole listing", _ {
		// **Fix round 1, Critical 1.** This scene used to file `Action::Kind::LockInput{true}` while
		// `in_flight()` and clear its own flag in `on_exit()` -- which returns `void`, so the
		// matching release could never be filed on the one path that needs it. `set_input_lock` is
		// one bare global bool with no owner (`shell/router.cpp:139` ->
		// `shell/input_lowering.h:293`), so an external `Pop` mid-listing left the ENTIRE device
		// deaf until something else happened to clear it. It is also the wrong lock: it *drops*
		// events, including encoder edges (`shell/input_lowering.cpp:159`), where legacy's
		// `waitingForSDRoutineToEnd` defers and retries.
		CleanBrowser clean;
		ScratchCard card;
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		Rig r;

		expect(r.shell.lowering().input_locked()).to_be_false();
		r.open(context_of(f), BrowserId::Sample);
		r.tick(0);
		expect(browser_scene().listing_in_flight()).to_be_true();
		// In flight, and the lock is still clear.
		expect(r.shell.lowering().input_locked()).to_be_false();

		// The removal that used to strand it: the scene leaves the stack with a listing dispatched
		// and no `Result` of its own. Nothing can have been leaked, because nothing was taken.
		browser_scene().on_exit();
		expect(r.shell.lowering().input_locked()).to_be_false();

		// And the op still lands harmlessly -- `on_exit` collected nothing because nothing had
		// landed, so the seam is left holding the catalogue until the next collect, which is the
		// documented jam rather than a corrupted vector (`browser_scene.cpp`'s `on_exit`).
		Deferred::run();
		expect(r.shell.lowering().input_locked()).to_be_false();
	});

	it("refuses input while a refused request is still parked, so it cannot be overwritten", _ {
		// **Fix round 1, Critical 2.** `pending_` is a single slot, and it is created in exactly the
		// state the other two busy tests miss: a `Busy` refusal leaves nothing in flight and no
		// result owed. With an `Open` parked, a BACK press used to reach `back()` and file an
		// `UpLevel` straight over it -- losing the `Open` and then listing a directory derived from
		// the FOREIGN holder's `currentDir`.
		CleanBrowser clean;
		ScratchCard card;
		spec::AudioSongFixture f = spec::make_audio_clip_song();

		// Someone else holds the catalogue, and has left `currentDir` somewhere of their own.
		int other = 0;
		expect(FileCatalogue::claim(&other)).to_be_true();
		FileCatalogue::currentDir = "MIDI_DEVICES/DEFINITION";

		Rig r;
		r.open(context_of(f), BrowserId::Sample);
		r.tick(0);
		expect(browser_scene().listing_in_flight()).to_be_false();

		// BACK, and the select-encoder release: both are swallowed, and neither files a listing.
		DirectCtx d(r.clock);
		const Result back_result = browser_scene().handle(
		    UiEvent{.kind = UiEvent::Kind::Button, .on = true, .button = ButtonId::Back}, d.ctx);
		expect(back_result.handled).to_be_true();
		expect(back_result.actions.empty()).to_be_true();
		const Result enter_result = browser_scene().handle(
		    UiEvent{.kind = UiEvent::Kind::Button, .on = false, .button = ButtonId::SelectEnc}, d.ctx);
		expect(enter_result.handled).to_be_true();
		expect(enter_result.actions.empty()).to_be_true();

		// The parked `Open` survives, and when the claim frees it lists what it always meant to.
		FileCatalogue::release(&other);
		r.step();
		expect(browser_scene().listing_in_flight()).to_be_true();
		r.land();
		expect(std::string{browser_scene().current_dir()}).to_equal(std::string{"SAMPLES"});
		expect(catalogue_order()).to_equal(std::string("CLAP.WAV,CLIPS,KICKS,SNARE.WAV,TOM.WAV"));
	});

	it("collects a listing that lands on a tick with NO input at all", _ {
		// The quiet-tick example, and it has to be one: `after_render()` -- the only hook that can
		// collect a result or file an action -- runs only for scenes the shell actually *rendered*
		// this tick (`shell/shell.cpp:360-364`). With no input arriving, nothing else asks for a
		// frame, so `render_oled`'s `kListingPollMs` gate arm and `next_timer()`'s matching answer
		// are the ONLY things that bring the browser back. A spec that supplied input on every tick
		// would pass with both deleted.
		CleanBrowser clean;
		ScratchCard card;
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		Rig r;
		r.open(context_of(f), BrowserId::Sample);
		r.tick(0);
		expect(browser_scene().listing_in_flight()).to_be_true();

		// The scene must ask to be woken with nothing pending, or the landed listing is never seen.
		const std::optional<uint32_t> armed = browser_scene().next_timer();
		expect(armed.has_value()).to_be_true();
		expect(armed.value() <= browser::kListingPollMs).to_be_true();

		// Run the op the way the owner fiber would -- from outside the UI entirely, with no event
		// queued -- and then tick without touching a single control.
		Deferred::run();
		expect(browser_scene().file_index()).to_equal(-1); // not collected yet
		r.tick(50);
		expect(browser_scene().file_index()).to_equal(0);
		expect(std::string{browser_scene().entered_text()}).to_equal(std::string{"CLAP"});
	});

	it("retries a listing refused as Busy on a quiet tick, with no notice to wake it", _ {
		// **This is the example `kListingPollMs` exists for, and the only one that can be.** A
		// `Dropped` dispatch and a completed listing both move the notice queue (`Working` up,
		// `ClearWorking` down), and E1's `Notices` pushing or popping `SceneKind::Working` is itself
		// enough to bring a frame back -- so neither can tell whether the scene asked to be woken.
		// A `Busy` refusal is different: `Listing::request` tests `FileCatalogue::claim` BEFORE it
		// raises anything (`listing.cpp:105`), so the queue never moves. Nothing else in the shell
		// has any reason to render this scene again, and without the poll the retry never happens.
		CleanBrowser clean;
		ScratchCard card;
		spec::AudioSongFixture f = spec::make_audio_clip_song();

		// Something else holds the catalogue -- a second seam, or a legacy `Browser` mid-listing;
		// the claim is on the resource, so a raw token stands in for either (task 2, fix round 2).
		int other = 0;
		expect(FileCatalogue::claim(&other)).to_be_true();

		Rig r;
		r.open(context_of(f), BrowserId::Sample);
		r.tick(0);
		expect(browser_scene().listing_in_flight()).to_be_false();
		expect(Deferred::pending()).to_be_false();
		// Nothing was raised, so nothing in `Notices` will redraw this scene for us.
		deluge::notify::Notice n;
		int raised = 0;
		while (deluge::notify::notices().pop(n)) {
			raised++;
		}
		expect(raised).to_equal(0);
		// The scene owes a listing and says so.
		const std::optional<uint32_t> armed = browser_scene().next_timer();
		expect(armed.has_value()).to_be_true();
		expect(armed.value() <= browser::kListingPollMs).to_be_true();

		// The other holder finishes. Now tick with no input and no notice at all: the scene's own
		// gate arm is the only thing that can bring it back to retry.
		FileCatalogue::release(&other);
		r.tick(browser::kListingPollMs);
		expect(browser_scene().listing_in_flight()).to_be_true();
		r.land();
		expect(browser_scene().file_index()).to_equal(0);
		expect(std::string{browser_scene().entered_text()}).to_equal(std::string{"CLAP"});
	});

	it("keeps a dropped dispatch and retries it on a later tick", _ {
		// `Owner::run` returning false is a queue-full drop, not a failure: legacy clears its flag
		// so the next HID event retries (`browser.cpp:113-117`). Here the request is kept and
		// re-filed from `after_render`, which is what makes the retry happen with no input at all.
		CleanBrowser clean;
		ScratchCard card;
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		Rig r;
		Deferred::accept = false;
		r.open(context_of(f), BrowserId::Sample);
		r.tick(0);

		expect(browser_scene().listing_in_flight()).to_be_false();
		expect(Deferred::pending()).to_be_false();
		// It did not give up: the scene still asks to be woken, because it still owes a listing.
		expect(browser_scene().next_timer().has_value()).to_be_true();

		Deferred::accept = true;
		r.step();
		expect(browser_scene().listing_in_flight()).to_be_true();
		r.land();
		expect(static_cast<int>(FileCatalogue::fileItems.size())).to_equal(5);
	});
});

describe browser_navigation("scenes::browser::BrowserScene navigation", $ {
	it("descends into a folder and ascends back out, landing on the folder it left", _ {
		CleanBrowser clean;
		ScratchCard card;
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		Rig r;
		r.open_and_land(context_of(f), BrowserId::Sample);

		// Walk to KICKS (index 2) and press enter on it.
		r.turn(2, r.clock + 10);
		r.step();
		expect(browser_scene().file_index()).to_equal(2);
		expect(std::string{browser_scene().entered_text()}).to_equal(std::string{"KICKS"});

		r.enter();
		expect(browser_scene().listing_in_flight()).to_be_true();
		r.land();

		// `goIntoFolderImpl` (`browser.cpp:1339`): the folder name is appended to `currentDir`.
		expect(std::string{browser_scene().current_dir()}).to_equal(std::string{"SAMPLES/KICKS"});
		expect(catalogue_order()).to_equal(std::string("KICK1.WAV"));
		expect(std::string{browser_scene().entered_text()}).to_equal(std::string{"KICK1"});

		// BACK ascends. `goUpOneDirectoryLevelImpl` assigns the segment it dropped to `enteredText`
		// and lists starting at it (`browser.cpp:1388-1392`), so the folder just left is selected.
		r.button(kBackX, kBackY, true, r.clock + 10);
		r.step();
		expect(browser_scene().listing_in_flight()).to_be_true();
		r.land();
		expect(std::string{browser_scene().current_dir()}).to_equal(std::string{"SAMPLES"});
		expect(std::string{browser_scene().entered_text()}).to_equal(std::string{"KICKS"});
		expect(browser_scene().file_index()).to_equal(2);
	});

	it("refuses to enter SAMPLES/CLIPS/TEMP, and says why", _ {
		CleanBrowser clean;
		ScratchCard card;
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		Rig r;
		r.open_and_land(context_of(f), BrowserId::Sample);

		// Into CLIPS first (index 1).
		r.turn(1, r.clock + 10);
		r.step();
		expect(std::string{browser_scene().entered_text()}).to_equal(std::string{"CLIPS"});
		r.enter();
		r.land();
		expect(std::string{browser_scene().current_dir()}).to_equal(std::string{"SAMPLES/CLIPS"});
		// Note-name order, because the sample descriptor sets `interpret_note_names`: C3 before A3
		// before C4 (`file_catalogue_spec.cpp` characterises the same comparator).
		expect(catalogue_order()).to_equal(std::string("C3.WAV,A3.WAV,C4.WAV,TEMP"));
		// ...and TEMP is the last of the four, so walk onto it.
		expect(browser_scene().file_index()).to_equal(0);
		for (int i = 0; i < 3; i++) {
			r.turn(1, r.clock + 10);
			r.settle();
		}
		expect(std::string{browser_scene().entered_text()}).to_equal(std::string{"TEMP"});

		// "Don't allow user to go into TEMP clips folder" (`sample_browser.cpp:345-350`): the popup
		// is raised, the directory does not change, and no listing is dispatched.
		DirectCtx d(r.clock);
		const Result refused = browser_scene().handle(
		    UiEvent{.kind = UiEvent::Kind::Button, .on = false, .button = ButtonId::SelectEnc}, d.ctx);
		const ShellAction* popup = find_action(refused, ShellAction::Kind::Popup);
		expect(popup != nullptr).to_be_true();
		expect(popup != nullptr ? static_cast<int>(popup->popup_string) : -1)
		    .to_equal(static_cast<int>(String::STRING_FOR_TEMP_FOLDER_CANT_BE_BROWSED));
		expect(browser_scene().listing_in_flight()).to_be_false();
		expect(std::string{browser_scene().current_dir()}).to_equal(std::string{"SAMPLES/CLIPS"});

		// The MIDI descriptor has no folder refusal at all
		// (`load_midi_device_definition_ui.cpp:150-154`).
		expect(browser::descriptor(BrowserId::MidiDeviceDefinition).folder_refusal == nullptr).to_be_true();
	});

	it("closes on BACK at the browser's own top level", _ {
		CleanBrowser clean;
		ScratchCard card;
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		Rig r;
		r.open_and_land(context_of(f), BrowserId::Sample);

		// `goUpOneDirectoryLevel`'s synchronous root test (`browser.cpp:1364-1368`): "SAMPLES" has
		// no slash, so there is no level to ascend and `backButtonAction` exits instead.
		expect(std::string{browser_scene().current_dir()}).to_equal(std::string{"SAMPLES"});
		r.button(kBackX, kBackY, true, r.clock + 10);
		r.step();
		expect(r.browser_on_stack()).to_be_false();
		expect(browser_scene().listing_in_flight()).to_be_false();
	});

	it("walks the selection with the select encoder and stops at both ends", _ {
		CleanBrowser clean;
		ScratchCard card;
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		Rig r;
		r.open_and_land(context_of(f), BrowserId::Sample);

		// Five entries, three visible rows: the window follows the cursor down and stops.
		for (int i = 0; i < 4; i++) {
			r.turn(1, r.clock + 10);
			r.step();
		}
		expect(browser_scene().file_index()).to_equal(4);
		expect(std::string{browser_scene().entered_text()}).to_equal(std::string{"TOM"});
		// `clampFileSelectionAndScroll` (`browser.cpp:1050-1052`): the selected item is below the
		// viewport, so it moves to the LAST visible row -- 4 - 3 + 1.
		expect(browser_scene().scroll()).to_equal(2);

		// Past the end with nothing culled: legacy wraps to 0 (`browser.cpp:688`).
		r.turn(1, r.clock + 10);
		r.step();
		expect(browser_scene().file_index()).to_equal(0);
		expect(browser_scene().scroll()).to_equal(0);
		expect(std::string{browser_scene().entered_text()}).to_equal(std::string{"CLAP"});

		// ...and below 0 it wraps to the end (`:662`).
		r.turn(-1, r.clock + 10);
		r.step();
		expect(browser_scene().file_index()).to_equal(4);
	});
});

describe browser_typing("scenes::browser::BrowserScene typed search", $ {
	it("types to search and lands on the matching entry", _ {
		CleanBrowser clean;
		ScratchCard card;
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		Rig r;
		r.open_and_land(context_of(f), BrowserId::Sample);
		expect(browser_scene().file_index()).to_equal(0);

		// `QwertyUI::padAction`'s normal-key arm appends the character and calls
		// `Browser::predictExtendedText` (`qwerty_ui.cpp:350-356`), which searches the cached window
		// and selects what it finds (`browser.cpp:880-885`).
		r.type('S');
		expect(browser_scene().file_index()).to_equal(3);
		// The found entry's name REPLACES the typed text, and the cursor stays where the typing left
		// it -- which is what `text_edit` draws as the prediction highlight.
		expect(std::string{browser_scene().entered_text()}).to_equal(std::string{"SNARE"});
		expect(browser_scene().cursor()).to_equal(1);

		// A second character that already matches the prediction just steps over it
		// (`qwerty_ui.cpp:305-317`), leaving the selection alone.
		r.type('N');
		expect(std::string{browser_scene().entered_text()}).to_equal(std::string{"SNARE"});
		expect(browser_scene().cursor()).to_equal(2);
		expect(browser_scene().file_index()).to_equal(3);

		// Backspace shortens the search and re-runs it (`processBackspace`, `qwerty_ui.cpp:408-418`).
		r.backspace();
		expect(browser_scene().cursor()).to_equal(1);
		expect(browser_scene().file_index()).to_equal(3);
	});

	it("types across folders and files alike, and moves the viewport to the match", _ {
		CleanBrowser clean;
		ScratchCard card;
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		Rig r;
		r.open_and_land(context_of(f), BrowserId::Sample);

		// "KICKS" is a folder at index 2, past the first visible row: the search selects it AND
		// `clampFileSelectionAndScroll` brings it on screen (`browser.cpp:882-883`).
		r.type('K');
		expect(browser_scene().file_index()).to_equal(2);
		expect(std::string{browser_scene().entered_text()}).to_equal(std::string{"KICKS"});
		expect(browser_scene().scroll()).to_equal(0); // three rows fit 0..2 already

		// A character no entry starts with is refused: `predictExtendedText`'s `notFound` arm with
		// `mayDefaultToBrandNewNameOnEntry` false puts the selected file's name back
		// (`browser.cpp:842-846`) and `padAction` undoes the cursor advance (`qwerty_ui.cpp:357-359`).
		r.type('Q');
		expect(std::string{browser_scene().entered_text()}).to_equal(std::string{"KICKS"});
		expect(browser_scene().file_index()).to_equal(2);
		expect(browser_scene().cursor()).to_equal(1);
	});

	it("reverts the cursor when a search that needed a re-read still finds nothing", _ {
		// **Fix round 1, Important 3.** Legacy decrements `enteredTextEditPos` whenever
		// `predictExtendedText()` answers false (`qwerty_ui.cpp:357-359`), and that includes the
		// `doNewRead -> doSearch -> notFound` route. Here the re-read lands a whole tick later, so
		// the cursor to fall back to has to travel with the request; without that the cursor ends
		// one column past legacy's, the prediction highlight starts a column late, and the next
		// keystroke truncates at the wrong index.
		CleanBrowser clean;
		ScratchCard card;
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		Rig r;
		r.open_and_land(context_of(f), BrowserId::Sample);
		expect(browser_scene().cursor()).to_equal(0);

		// 'Z' sorts past every entry, so the cached-window search runs off the right-hand end
		// (`browser.cpp:822`) and the scene dispatches a re-read rather than answering.
		r.type('Z');
		expect(browser_scene().listing_in_flight()).to_be_true();
		r.land();

		// The re-read finds nothing either. Legacy's `notFound` arm puts the selected file's name
		// back AND undoes the cursor advance -- both, not just the first.
		expect(std::string{browser_scene().entered_text()}).to_equal(std::string{"CLAP"});
		expect(browser_scene().cursor()).to_equal(0);
		expect(browser_scene().file_index()).to_equal(0);

		// Proof that it matters rather than just differs: with the cursor back at 0, the next
		// keystroke is a first character again and truncates from 0, as legacy's would.
		r.type('S');
		expect(std::string{browser_scene().entered_text()}).to_equal(std::string{"SNARE"});
		expect(browser_scene().cursor()).to_equal(1);
	});

	it("searches under the comparator the listing sorted with, not whatever is pinned now", _ {
		// **Fix round 1, Important 4.** The sample descriptor interprets note names, so the listing
		// leaves `CLIPS` ordered C3, A3, C4 -- and `searchFileItems` is a binary search over exactly
		// that order. The comparator's globals live in `util/name_compare.h`, which this layer may
		// not include, so the scene calls `FileCatalogue::repinComparator()` before every search
		// instead of relying on nothing having disturbed them since the sort.
		//
		// The window is real, not hypothetical: the op runs on the storage owner and the result is
		// collected a tick later, so anything else sharing these globals in between -- a legacy
		// browser, a save UI's default-name search -- re-pins them under the scene. The deferred
		// dispatcher lets that window be entered exactly.
		CleanBrowser clean;
		ScratchCard card;
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		// `SampleBrowser::opened`'s first arm: the holder's path decides where the browser opens and
		// which entry it lands on (`sample_browser.cpp:131`).
		f.clip->sampleHolder.filePath = "SAMPLES/CLIPS/A3.WAV";

		Rig r;
		r.open(context_of(f), BrowserId::Sample);
		r.tick(0);
		Deferred::run(); // the listing has now read and sorted, pinning note names on

		bool sorted_with = false;
		expect(FileCatalogue::sortedComparator(sorted_with)).to_be_true();
		expect(sorted_with).to_be_true();
		expect(catalogue_order()).to_equal(std::string("C3.WAV,A3.WAV,C4.WAV,TEMP"));

		// Something else re-pins the comparator in the window before the collect.
		shouldInterpretNoteNames = false;
		octaveStartsFromA = false;

		// The collect searches for "A3.WAV". Under the order the folder was actually sorted into it
		// is at index 1; under the plain comparator the search stops at index 0 (C3) instead, which
		// is what this example fails with if the re-pin is removed.
		r.step();
		expect(std::string{browser_scene().current_dir()}).to_equal(std::string{"SAMPLES/CLIPS"});
		expect(browser_scene().file_index()).to_equal(1);
		expect(std::string{browser_scene().entered_text()}).to_equal(std::string{"A3"});
	});

	it("refuses a non-alphanumeric first character", _ {
		CleanBrowser clean;
		ScratchCard card;
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		Rig r;
		r.open_and_land(context_of(f), BrowserId::Sample);
		// `qwerty_ui.cpp:273-280`: "First character must be alphanumerical". The cursor is at 0 on
		// arrival (`browser.cpp:389`), so this is the first character.
		expect(browser_scene().cursor()).to_equal(0);
		r.type('.');
		expect(browser_scene().cursor()).to_equal(0);
		expect(std::string{browser_scene().entered_text()}).to_equal(std::string{"CLAP"});
	});
});

describe browser_accept("scenes::browser::BrowserScene accepting a file", $ {
	it("parks the chosen sample for the wiring plan and closes", _ {
		CleanBrowser clean;
		ScratchCard card;
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		Rig r;
		r.open_and_land(context_of(f), BrowserId::Sample);
		(void)browser_scene().take_choice(); // drain

		// `SampleBrowser::enterKeyPress`'s file arm is `claimCurrentFile()`
		// (`sample_browser.cpp:398`), which names `soundEditor`, `AudioEngine`, `slicer`, `PadLEDs`
		// and `instrumentClipView` -- five symbols behind the `scenes-includes-outside` wall. So the
		// descriptor answers `Defer` and the scene parks the path, exactly as
		// `MenuScene::take_action()` parks a verb it cannot perform.
		Error unused = Error::NONE;
		expect(static_cast<int>(
		           browser::descriptor(BrowserId::Sample).accept(context_of(f), "SAMPLES/CLAP.WAV", unused)))
		    .to_equal(static_cast<int>(AcceptOutcome::Defer));

		r.enter();
		expect(r.browser_on_stack()).to_be_false();

		const std::optional<browser::BrowserChoice> choice = browser_scene().take_choice();
		expect(choice.has_value()).to_be_true();
		// Read through defaults rather than through `choice->`: a mutant that leaves the outbox empty
		// must fail this example, not abort the process inside `std::optional::operator->` before
		// any other example has run.
		expect(static_cast<int>(choice.has_value() ? choice->id : BrowserId::Count))
		    .to_equal(static_cast<int>(BrowserId::Sample));
		// `getCurrentFilePath()`'s own spelling: the directory, a separator, the filename WITH its
		// extension.
		expect(choice.has_value() ? choice->path : std::string{}).to_equal(std::string{"SAMPLES/CLAP.WAV"});
		// Exactly once.
		expect(browser_scene().take_choice().has_value()).to_be_false();
	});

	it("loads the MIDI device definition itself, and closes", _ {
		CleanBrowser clean;
		ScratchCard card;
		spec::SongFixture f = spec::make_midi_song();
		auto* instrument = static_cast<MIDIInstrument*>(f.instrument);
		instrument->deviceDefinitionFileName.clear();

		Rig r;
		r.open_and_land(context_of(f), BrowserId::MidiDeviceDefinition);
		expect(std::string{browser_scene().entered_text()}).to_equal(std::string{"DEVICE1"});

		// Unlike the sample descriptor, this one performs its own action:
		// `StorageManager::loadMidiDeviceDefinitionFile` is in `storage/`, which this layer may
		// include (`load_midi_device_definition_ui.cpp:216-217`). The proof it ran is the
		// instrument's own remembered filename, which `loadMidiDeviceDefinitionFile` writes.
		r.enter();
		expect(r.browser_on_stack()).to_be_false();
		expect(instrument->deviceDefinitionFileName)
		    .to_equal(std::string{"MIDI_DEVICES/DEFINITION/DEVICE1.XML"});
		// ...and nothing was parked: this descriptor never defers.
		expect(browser_scene().take_choice().has_value()).to_be_false();
	});

	it("reopens on the file it remembers", _ {
		CleanBrowser clean;
		ScratchCard card;
		spec::SongFixture f = spec::make_midi_song();
		auto* instrument = static_cast<MIDIInstrument*>(f.instrument);
		// `setupForLoadingMidiDeviceDefinition` splits the remembered path into a directory and the
		// name to select (`load_midi_device_definition_ui.cpp:100-121`).
		instrument->deviceDefinitionFileName = "MIDI_DEVICES/DEFINITION/DEVICE1.XML";

		std::string dir;
		std::string name;
		browser::descriptor(BrowserId::MidiDeviceDefinition).start_at(context_of(f), dir, name);
		expect(dir).to_equal(std::string{"MIDI_DEVICES/DEFINITION"});
		expect(name).to_equal(std::string{"DEVICE1.XML"});

		Rig r;
		r.open_and_land(context_of(f), BrowserId::MidiDeviceDefinition);
		expect(browser_scene().file_index()).to_equal(0);
		expect(std::string{browser_scene().entered_text()}).to_equal(std::string{"DEVICE1"});
	});
});

describe browser_legacy_pixels("scenes::browser::BrowserScene against the legacy renderer", $ {
	it("draws the same rows as Browser::renderOLED on the same catalogue", _ {
		CleanBrowser clean;
		ScratchCard card;
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		Rig r;
		r.open_and_land(context_of(f), BrowserId::Sample);
		expect(browser_scene().file_index()).to_equal(0);

		// The oracle runs SECOND and from its own state, mirrored across just before it draws --
		// E2's lesson: a comparison that reads state the port left behind can pass with the port
		// wrong. Here the shared thing is `FileCatalogue::fileItems`, which is the *input* to both
		// renderers rather than either one's output.
		expect(below_bezel(r.fake.pbm(0), 5)).to_equal(below_bezel(legacy_pbm(), 5));
	});

	it("draws the same rows after the viewport has scrolled", _ {
		CleanBrowser clean;
		ScratchCard card;
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		Rig r;
		r.open_and_land(context_of(f), BrowserId::Sample);
		for (int i = 0; i < 4; i++) {
			r.turn(1, r.clock + 10);
			r.step();
		}
		// A different window (rows 2..4) and a different selected row, so the comparison is not
		// re-asserting the first example's frame.
		expect(browser_scene().scroll()).to_equal(2);
		expect(browser_scene().file_index()).to_equal(4);
		r.settle();
		expect(below_bezel(r.fake.pbm(0), 5)).to_equal(below_bezel(legacy_pbm(), 5));
	});

	it("draws the same prediction highlight after typing", _ {
		CleanBrowser clean;
		ScratchCard card;
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		Rig r;
		r.open_and_land(context_of(f), BrowserId::Sample);
		r.type('S');
		r.settle();
		// Cursor mid-string is `drawTextForOLEDEditing`'s OTHER arm -- the inversion from the cursor
		// to the panel's right edge (`qwerty_ui.cpp:156-170`) -- so this compares a code path the
		// two examples above do not reach.
		expect(browser_scene().cursor()).to_equal(1);
		expect(static_cast<int>(browser_scene().entered_text().size())).to_equal(5);
		expect(below_bezel(r.fake.pbm(0), 5)).to_equal(below_bezel(legacy_pbm(), 5));
	});
});

describe browser_goldens("scenes::browser::BrowserScene pixels", $ {
	it("draws the listed folder the same on both shipped panel geometries", _ {
		{
			CleanBrowser clean;
			ScratchCard card;
			spec::AudioSongFixture f = spec::make_audio_clip_song();
			Rig r;
			r.open_and_land(context_of(f), BrowserId::Sample);
			expect_golden(self, "../ui/scenes/browser_sample_list_48.pbm", r.fake.pbm(0));
		}
		{
			CleanBrowser clean;
			ScratchCard card;
			spec::AudioSongFixture f = spec::make_audio_clip_song();
			Rig r64{board_64(), profile_64()};
			r64.open_and_land(context_of(f), BrowserId::Sample);
			// Five rows on the taller panel where legacy's hard-coded
			// `getNumBrowserAndMenuLines()` would still have drawn three -- the port follows
			// `Style::menu_rows`, as `list` already does.
			expect_golden(self, "../ui/scenes/browser_sample_list_64.pbm", r64.fake.pbm(0));
		}
	});

	it("draws the MIDI browser's own icon and title on both geometries", _ {
		{
			CleanBrowser clean;
			ScratchCard card;
			spec::SongFixture f = spec::make_midi_song();
			Rig r;
			r.open_and_land(context_of(f), BrowserId::MidiDeviceDefinition);
			expect_golden(self, "../ui/scenes/browser_midi_list_48.pbm", r.fake.pbm(0));
		}
		{
			CleanBrowser clean;
			ScratchCard card;
			spec::SongFixture f = spec::make_midi_song();
			Rig r64{board_64(), profile_64()};
			r64.open_and_land(context_of(f), BrowserId::MidiDeviceDefinition);
			expect_golden(self, "../ui/scenes/browser_midi_list_64.pbm", r64.fake.pbm(0));
		}
	});

	it("draws the prediction highlight the same on both geometries", _ {
		{
			CleanBrowser clean;
			ScratchCard card;
			spec::AudioSongFixture f = spec::make_audio_clip_song();
			Rig r;
			r.open_and_land(context_of(f), BrowserId::Sample);
			r.type('S');
			r.settle();
			expect_golden(self, "../ui/scenes/browser_typed_48.pbm", r.fake.pbm(0));
		}
		{
			CleanBrowser clean;
			ScratchCard card;
			spec::AudioSongFixture f = spec::make_audio_clip_song();
			Rig r64{board_64(), profile_64()};
			r64.open_and_land(context_of(f), BrowserId::Sample);
			r64.type('S');
			r64.settle();
			expect_golden(self, "../ui/scenes/browser_typed_64.pbm", r64.fake.pbm(0));
		}
	});
});
    // clang-format on

    /// Eight `describe` groups in one TU, so `DELUGE_SPEC` (one spec per entry point) is spelled out
    /// here with `add_specs` instead -- as `rename_scene_spec.cpp` does.
    int browser_scene_spec(int argc, char** const argv) {
	    return CppSpec::parse(argc, argv)
	                   .add_specs(browser_descriptors, browser_composition, browser_open, browser_in_flight,
	                              browser_navigation, browser_typing, browser_accept, browser_legacy_pixels,
	                              browser_goldens)
	                   .exec()
	                   .is_success()
	               ? EXIT_SUCCESS
	               : EXIT_FAILURE;
    }
