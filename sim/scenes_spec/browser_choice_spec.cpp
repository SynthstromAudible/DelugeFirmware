/// @file
/// @brief The **choice seam** (`scenes/browser/browser_scene.h`'s `Descriptor::accept` / `Defer`)
///        and its consumer, driven through a real `Shell` and a real `ShellHostUI` over the real
///        scene registry.
///
/// This is `menu_actions_spec.cpp`'s `menu_action_dispatch` group for the noun half of the same
/// wall, and what is covered here matters more than usual because one legacy statement is split in
/// two:
///
///   * **Covered:** that a real file press through the ported browser parks exactly one
///     `BrowserChoice`; that `gui::ui::dispatchShellChoices` is what takes it out of the outbox;
///     that the poll is reached **by the guest's own `timerCallback`**, on a tick where the browser
///     has already closed itself; that polling an empty outbox neither reports nor eats anything;
///     and that `sampleClaimTargetFor` splits a chosen path exactly the way
///     `SampleBrowser::getCurrentFilePath()` builds one.
///   * **The claim:** that the drain *points the legacy browser* at the chosen directory and
///     filename and then requests the claim of that file, which together are
///     `SampleBrowser::enterKeyPress`'s file arm with the one argument the guest has to change. The
///     effect is asserted on `Browser`'s own state and on the request itself.
///   * **NOT run, ever, in this file:** the claim itself. A real claim would mean a card read, an
///     `AudioFileManager` allocation and an `openUI(&soundEditor)` inside an assertion. `ClaimSpy`
///     installs a recording dispatcher through
///     `deluge::gui::ui::setSampleClaimDispatch`, which is why the examples can press a real file at
///     all -- see that function's own note.
///
/// @par Why the guest examples open the browser from the menu rather than as the initial scene
///
/// Because that is the only way it is ever open. `FileBrowserItem::on_focus`
/// (in `scenes/menu/nodes/sample.cpp`) opens the scene and files `MenuWalkState::pending_scene`
/// (`:104-105`), so the browser is always **above** a `MenuScene` on the shell stack. Pushed alone,
/// the stack would empty the moment `Defer` closed it, and `ShellHostUI::timerCallback` would take
/// its `closeGuest()` arm -- `closeUI` on a guest that is not
/// on the legacy UI stack, i.e. the index `-1` read `openPerformanceView`'s doc in
/// `gui/ui/shell_action_dispatch.cpp` describes. A rig that has to avoid a production path is a rig that is testing
/// something else.
///
/// @note The spec may include `gui/` (for the dispatcher and the guest); the library may not. Same
///       rule `sample_items_spec.cpp` and `browser_scene_spec.cpp` run under.

#include "gui/ui/menu_view_of.h"
#include "notify/dirty_set.h"
#include "scenes/browser/browser_scene.h"
#include "scenes/context_menu/context_menu_scene.h"
#include "scenes/menu/bindings.h"
#include "scenes/menu/context.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/menu/node.h"
#include "scenes/menu/node_id.h"
#include "scenes/menu/nodes/nodes.h"
#include "scenes/menu/nodes/sample.h"
#include "scenes/menu/walk.h"
#include "scenes/profile.h"
#include "scenes/registry.h"

#include "shell/profiles/deluge.h"
#include "shell/shell.h"
#include "ui/grid/keyboard.h"

#include "gui/l10n/l10n.h"
#include "gui/ui/browser/browser.h"
#include "gui/ui/browser/folder_import.h"
#include "gui/ui/browser/sample_browser.h"
#include "gui/ui/browser/sample_claim.h"
#include "gui/ui/load/instrument_load.h"
#include "gui/ui/shell_choice_dispatch.h"
#include "gui/ui/shell_guest_services.h"
#include "gui/ui/shell_host_ui.h"
#include "gui/views/instrument_clip_view.h"
#include "hid/button.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/led/pad_leds.h"
#include "model/instrument/midi_instrument.h"
#include "model/note/note_row.h"
#include "model/project_bridge/slot_bridge.h"
#include "model/sample/sample.h"
#include "model/song/song.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"
#include "processing/source.h"
#include "storage/cluster/cluster.h"
#include "storage/file_catalogue.h"
#include "storage/file_item.h"
#include "storage/multi_range/multisample_range.h"
#include "util/sample_preview_open.h"

#include "support/global_guest_menu.h"
#include "support/song_fixture.h"

// Before <cppspec.hpp>: it #defines `it`, which collides with every local named `it` in the project
// headers this drags in (`audio_engine.h` -> `util/container/ordered_pos_vector.h`).
#include "support/card_access.h"

#include "spec/shell/support/fake_surfaces.h"

#include "cppspec.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>

#include "spec/support/spec_main.h"

/// `Buttons::isButtonPressed` reads `buttonStates` (`hid/buttons.cpp`), which the header does not export.
namespace Buttons {
extern bool buttonStates[NUM_BUTTON_COLS + 1][NUM_BUTTON_ROWS];
} // namespace Buttons

using namespace deluge::scenes;
using deluge::gui::ui::SampleClaimTarget;
using deluge::scenes::menu::MenuContext;
using deluge::scenes::menu::Node;
using deluge::ui::shell::SceneId;
using deluge::ui::shell::SceneKind;
using deluge::ui::shell::Shell;
using deluge::ui::shell::ShellConfig;
namespace nodes = deluge::scenes::menu::nodes;

namespace {

// ---------------------------------------------------------------------------------------------
// Process-global scaffolding

/// @brief The card these examples list: `tests/fixtures/scenes_spec/browser_choice`, installed for this file before
/// boot
///        (`tests/integration/checks/scenes_spec_cards.txt`). Constructing one checks it is the installed card.
struct ScratchCard {
	std::filesystem::path dir = std::filesystem::path(DELUGE_REPO_ROOT) / "tests/fixtures/scenes_spec/browser_choice";
	spec::SdRoot root{dir.string()};
};

/// The path `SoundRig`'s holder names, and therefore the one a press on the landed selection files.
constexpr std::string_view kChosenPath = "SAMPLES/OSC_A/A1.WAV";

/// @brief The minimum `Display` the scenes need; `display` is null in a spec process
///        (`hid/display/display.cpp:4`). `sample_items_spec.cpp`'s, unchanged.
struct QuietDisplay final : deluge::hid::Display {
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

[[noreturn]] void spec_bail(const char* what) {
	std::fprintf(stderr, "browser_choice_spec: %s\n", what);
	std::abort();
}

/// @brief Refuse to start, or to finish, with the browser singleton still holding something.
///
/// **Assert-then-clear, not defensive-clear** -- `sample_items_spec.cpp`'s `CleanScenes`, plus the
/// outbox. The choice slot is the whole subject of this file: an example that left one parked would
/// let the *next* example's drain pass on a choice it never filed, which is precisely how a drain
/// test goes green while doing nothing. `before_each`/`after_each` take no `self`, so a named reason
/// on stderr plus `std::abort()` is what "loudly" means.
struct CleanBrowser {
	spec::QuietAudio quiet;

	CleanBrowser() {
		check("a browser singleton was still busy when an example started");
		// A file an earlier example previewed is the browsers' last file loaded, which a fresh browser opens beside, as
		// legacy's does; each example starts from none, and with no preview waiting.
		sampleBrowser.lastFilePathLoaded.clear();
		(void)browser::browser_scene().take_preview();
	}
	~CleanBrowser() { check("an example finished with a browser singleton still busy"); }
	CleanBrowser(const CleanBrowser&) = delete;
	CleanBrowser& operator=(const CleanBrowser&) = delete;

private:
	static void check(const char* what) {
		if (browser::browser_scene().listing_in_flight() || browser::browser_scene().take_choice().has_value()) {
			spec_bail(what);
		}
	}
};

/// @brief The claim seam, swapped for a recorder -- what lets these examples press a real file.
///
/// `deluge::gui::ui::setSampleClaimDispatch` exists for this and nothing else; its own note argues
/// why. **This is not `BrowserScene::set_dispatch`**,
/// which swaps the dispatcher on the scene's `Listing` -- the directory read, one layer down. There
/// is no claim seam on `BrowserScene`: the claim happens in the legacy-side dispatcher, so the
/// settable dispatcher is the dispatcher's own.
///
/// What it records is the `ClaimRequest` the drain hands `SampleClaim::request`: the path, the
/// entered text, the claim arguments and the completion.
///
/// @warning **Recording the request is as far as this can go, and that is a real limit on what the
///          examples below prove.** Honouring it means running the claim -- a card read and an
///          `AudioFileManager` allocation -- which is the thing the seam exists to keep out. So WHAT
///          is requested is pinned; what the claim then does is `SampleClaim`'s own.
/// @brief Records each file the drain asks the card to delete, in place of the card.
struct DeleteSpy {
	static inline std::vector<std::string> paths{};
	static void record(std::string_view path) { paths.emplace_back(path); }
	DeleteSpy() {
		paths.clear();
		deluge::gui::ui::setFileDeleteDispatch(&record);
	}
	~DeleteSpy() { deluge::gui::ui::setFileDeleteDispatch(nullptr); }
	DeleteSpy(const DeleteSpy&) = delete;
	DeleteSpy& operator=(const DeleteSpy&) = delete;
};

/// @brief Records each auto-load cancel the drain asks for, in place of `SampleClaim::cancelAutoLoad`.
struct CancelSpy {
	static inline int32_t cancels = 0;
	static void record() { cancels++; }
	CancelSpy() {
		cancels = 0;
		deluge::gui::ui::setSampleClaimCancel(&record);
	}
	~CancelSpy() { deluge::gui::ui::setSampleClaimCancel(nullptr); }
	CancelSpy(const CancelSpy&) = delete;
	CancelSpy& operator=(const CancelSpy&) = delete;
};

struct ClaimSpy {
	static inline int32_t dispatches = 0;
	static inline deluge::gui::ClaimRequest last{};

	static void record(const deluge::gui::ClaimRequest& request) {
		dispatches++;
		last = request;
	}

	/// @brief Whether the last request is the guest's: @p path, its entered text, the guest's claim
	///        arguments (`kShellSampleClaimArgs`) and `shellSampleClaimDone` as the completion.
	static bool last_is_guest_claim_of(const std::string& path, const std::string& entered) {
		const auto& args = deluge::gui::ui::kShellSampleClaimArgs;
		return last.path == path && last.enteredText == entered && last.mayDoPitchDetection == args.mayDoPitchDetection
		       && last.mayDoSingleCycle == args.mayDoSingleCycle && last.mayDoWaveTable == args.mayDoWaveTable
		       && last.loadWithoutExiting == args.loadWithoutExiting
		       && last.completion == &deluge::gui::ui::shellSampleClaimDone;
	}

	ClaimSpy() {
		dispatches = 0;
		last = {};
		deluge::gui::ui::setSampleClaimDispatch(&record);
	}
	/// Put the shipped dispatcher back, so a later example that forgot a spy fails loudly rather
	/// than silently recording into a dead struct.
	~ClaimSpy() { deluge::gui::ui::setSampleClaimDispatch(nullptr); }
	ClaimSpy(const ClaimSpy&) = delete;
	ClaimSpy& operator=(const ClaimSpy&) = delete;
};

void select_language() {
	deluge::l10n::chosenLanguage = &deluge::l10n::built_in::english;
}

bool scene_on_stack(Shell& shell, SceneKind kind) {
	for (const deluge::ui::shell::Scene* s : shell.stack().scenes()) {
		if (s->kind() == kind) {
			return true;
		}
	}
	return false;
}

// ---------------------------------------------------------------------------------------------
// Typing on the grid keyboard, which is how the browser is committed

constexpr int32_t kHomeRow = deluge::ui::grid::kTextKeyboardHomeRow;

struct Pad {
	int32_t x = -1, y = -1;
};

/// @brief The pad that types @p c on the QWERTY layout, found by asking the toolkit's own lookup
///        rather than transcribing the table a second time (`browser_scene_spec.cpp`'s `pad_for`).
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

// ---------------------------------------------------------------------------------------------
// The song: one oscillator with a sample source, so `file0_selector` has a holder to open on

/// @brief A `Sample` with an investigated overview cache, so nothing reads the card for it.
///        `sample_items_spec.cpp`'s `make_sample`, at one size.
Sample* make_sample() {
	constexpr size_t kClusterSize = 32768;
	constexpr int32_t kNumClusters = 32;
	Cluster::set_size(kClusterSize);
	const uint64_t length = static_cast<uint64_t>(kClusterSize) * kNumClusters / 2;

	// Placed in the deluge heap and never freed -- `song_fixture.h`'s own rule, and for its reason.
	void* memory = deluge::memory::alloc_fast(sizeof(Sample));
	auto* s = new (memory) Sample();
	s->byteDepth = 2;
	s->numChannels = 1;
	s->sampleRate = 44100;
	s->audioDataStartPosBytes = 0;
	s->audioDataLengthBytes = length * 2;
	s->lengthInSamples = length;
	s->maxValueFound = 127 << 24;
	s->minValueFound = -128 << 24;
	s->resizeOverviewCache(kNumClusters);
	for (int32_t i = 0; i < kNumClusters; i++) {
		OverviewCacheEntry& e = s->overviewCacheEntry(static_cast<uint32_t>(i));
		e.min = static_cast<int8_t>(-(16 + i));
		e.max = static_cast<int8_t>(16 + i);
		e.investigated = true;
	}
	return s;
}

/// @brief The model half both rigs share: a synth song whose oscillator 0 is a sample source
///        pointing at `kChosenPath`, so `sample_start_at` lands the browser on that file.
struct SongRig {
	spec::SongFixture song = spec::make_synth_song();
	Source* source = nullptr;
	MultisampleRange* range = nullptr;

	SongRig(const SongRig&) = delete;
	SongRig& operator=(const SongRig&) = delete;

	SongRig() {
		auto* sound = static_cast<Sound*>(static_cast<ModControllableAudio*>(song.instrument->toModControllable()));
		source = &sound->sources[0];
		source->oscType = OscType::SAMPLE;
		source->repeatMode = SampleRepeatMode::ONCE;
		Sample* sample = make_sample();
		range = static_cast<MultisampleRange*>(source->getOrCreateFirstRange());
		range->sampleHolder.audioFile = sample;
		range->sampleHolder.filePath = std::string{kChosenPath};
		sample->filePath = std::string{kChosenPath}; // the record names the range's file by its sample
		range->sampleHolder.startPos = 0;
		range->sampleHolder.endPos = sample->lengthInSamples;
		deluge::bridge::recordLoadedSound(*sound, *song.song); // set field by field: recorded as a load does
	}

	/// @brief The context `SoundEditor::setup` would have resolved for source 0.
	MenuContext ctx() {
		MenuContext c = menu::context_for(*song.song, deluge::gui::menuViewOf(song.clip), 0, false, false, false,
		                                  /*kit_global_fx=*/false);
		c.range_index = 0;
		return c;
	}
};

/// @brief A bare `Shell` over `FakeSurfaceSet`, with the application's own registry -- the rig for
///        the examples that call the dispatcher by name.
struct ShellRig {
	SongRig model;
	deluge::ui::shell::SceneRegistry reg = scene_registry();
	uitest::FakeSurfaceSet fake{deluge::ui::shell::kDelugeBoard, deluge_profile()};
	Shell shell{fake, deluge_profile(), reg, deluge::ui::shell::Services{}, ShellConfig{}};
	uint32_t clock = 0;

	ShellRig() = default;
	ShellRig(const ShellRig&) = delete;
	ShellRig& operator=(const ShellRig&) = delete;

	/// Tear the stack down, so every scene's `on_exit` runs and the next example gets a fresh
	/// singleton -- the same rule as `sample_items_spec.cpp`'s `~SoundRig`.
	~ShellRig() {
		while (shell.stack().depth() > 0) {
			shell.stack().pop(SceneKind::Null);
		}
		menu_scene().reset_process_state(); // each example starts with the rows a boot has
	}

	/// @brief Open the menu at `file0_selector` and let the item push the browser over it.
	///
	/// Four ticks, not one, for `sample_items_spec.cpp`'s `focus_in` reasons: `after_render` files
	/// the `Push` at the end of the tick that rendered the item, and `BrowserScene` collects its
	/// listing in its own `after_render` a tick later again.
	void open_browser() { open_menu_at(nodes::file0_selector); }

	/// @brief `open_browser`'s body with the landing node as a parameter.
	///
	/// Split out for the re-rooting group's control leg, which needs a menu that was opened the same
	/// way and simply never pushed a scene. **Opening at a node is the shortcut-pad shape, not a
	/// contrivance**: `MenuScene::on_enter` re-roots from `SceneId::arg` unconditionally
	/// (`scenes/menu/menu_scene.cpp`'s `on_enter`), which is exactly what the jump's `Action::Kind::
	/// Replace` produces (`handle`'s pad arm) -- and today the pad is the only way to focus a file
	/// selector at all (see the group's own note).
	void open_menu_at(const Node& node) {
		select_language();
		(void)menu::assign_node_ids(nodes::kAllRoots);
		menu_scene().open(node, model.ctx());
		shell.init(SceneId{SceneKind::FirstApp, menu::node_id(node)});
		for (int32_t i = 0; i < 4; i++) {
			step();
		}
	}

	void step() {
		clock += 10;
		shell.tick(clock);
	}

	void pad(const Pad& p, uint8_t velocity) {
		clock += 10;
		shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_PAD, static_cast<uint8_t>(p.x), static_cast<uint8_t>(p.y),
		                                      static_cast<int16_t>(velocity)},
		                     clock);
		step();
	}

	/// @brief Press and release the enter pad, then settle -- the commit, through the real input path.
	void enter() {
		const Pad p = pad_for(deluge::ui::grid::kTextKeyboardEnter);
		pad(p, 64);
		pad(p, 0);
		step();
	}

	bool browser_on_stack() { return scene_on_stack(shell, browser::kBrowserSceneKind); }
};

/// @brief A real `ShellHostUI` over the real registry -- the rig for the one example that must not
///        name the dispatcher at all.
///
/// `menu_actions_spec.cpp`'s `GuestRig`, with the browser opened over the menu. **A local guest, not
/// the `shellHostUI` global**: `boot_wiring_spec.cpp` reads that global at 11+ sites and the driver
/// supports an all-in-one-process mode, so a scene left on its stack is a real contamination path.
///
/// @note `opened()` calls the real `wireShellGuestServices()`, which installs into the **global**
///       guest's shell rather than into this one (`wireShellGuestServices` in `gui/ui/shell_guest_services.cpp`).
///       That is documented as idempotent, so re-running it is a re-seed and not a second set.
struct GuestRig {
	SongRig model;
	deluge::ui::shell::SceneRegistry reg = scene_registry();
	deluge::gui::ui::ShellHostUI ui{reg, deluge::ui::shell::kDelugeBoard, deluge_profile()};
	uint32_t clock = 0;

	GuestRig(const GuestRig&) = delete;
	GuestRig& operator=(const GuestRig&) = delete;
	GuestRig() = default;

	~GuestRig() {
		while (ui.shell().stack().depth() > 0) {
			ui.shell().stack().pop(SceneKind::Null);
		}
		menu_scene().reset_process_state(); // each example starts with the rows a boot has
	}

	/// `ShellRig::open_browser`'s two halves through the guest's own door.
	bool open() {
		select_language();
		(void)menu::assign_node_ids(nodes::kAllRoots);
		menu_scene().open(nodes::file0_selector, model.ctx());
		ui.setInitialScene(SceneId{SceneKind::FirstApp, menu::node_id(nodes::file0_selector)});
		return ui.opened();
	}

	/// One tick of the SHELL, which routes and renders but is not the guest's tick and so polls
	/// nothing. The control leg's only clock.
	void shell_step() {
		clock += 10;
		ui.shell().tick(clock);
	}
	/// One tick of the GUEST, which does the same and then drains. The measured leg's only clock.
	void guest_step() { (void)ui.timerCallback(); }

	/// @brief Press and release the enter pad through the guest's own hook -- which is the point:
	///        `ShellHostUI::padAction` only queues, and a tick is what routes.
	void press_enter(void (GuestRig::*tick)()) {
		const Pad p = pad_for(deluge::ui::grid::kTextKeyboardEnter);
		(void)ui.padAction(p.x, p.y, 64);
		(this->*tick)();
		(void)ui.padAction(p.x, p.y, 0);
		(this->*tick)();
		(this->*tick)();
	}

	bool browser_on_stack() { return scene_on_stack(ui.shell(), browser::kBrowserSceneKind); }
};

/// @brief A `Shell` over a MIDI song, on the MIDI device definition browser.
///
/// `BrowserId::MidiDeviceDefinition` defers and waits for its load, and the drain hands its choice
/// to the instrument load rather than the sample claim. This rig exists to pin that the claim
/// wiring leaves it there.
///
/// It pushes the browser's `SceneId` directly, the way `browser_scene_spec.cpp`'s own rig does,
/// rather than re-deriving `nodes::MidiDeviceLinkedItem` (`scenes/menu/nodes/output_items.h`): what
/// is under test here is what the dispatcher does with a descriptor's answer, not how that item's
/// toggle opens the scene. And it is a bare `Shell`, not the guest, so the empty stack that `Close`
/// leaves behind reaches no `closeGuest()` arm (`ShellHostUI::timerCallback`'s).
struct MidiRig {
	spec::SongFixture song = spec::make_midi_song();
	deluge::ui::shell::SceneRegistry reg = scene_registry();
	uitest::FakeSurfaceSet fake{deluge::ui::shell::kDelugeBoard, deluge_profile()};
	Shell shell{fake, deluge_profile(), reg, deluge::ui::shell::Services{}, ShellConfig{}};
	uint32_t clock = 0;

	MidiRig(const MidiRig&) = delete;
	MidiRig& operator=(const MidiRig&) = delete;
	MidiRig() = default;
	~MidiRig() {
		while (shell.stack().depth() > 0) {
			shell.stack().pop(SceneKind::Null);
		}
		menu_scene().reset_process_state(); // each example starts with the rows a boot has
	}

	MIDIInstrument* instrument() { return static_cast<MIDIInstrument*>(song.instrument); }

	void open_browser() {
		select_language();
		instrument()->deviceDefinitionFileName.clear();
		browser::browser_scene().open(menu::context_for(*song.song, deluge::gui::menuViewOf(song.clip), 0, false, false,
		                                                false, /*kit_global_fx=*/false));
		shell.init(
		    SceneId{browser::kBrowserSceneKind, static_cast<uint16_t>(browser::BrowserId::MidiDeviceDefinition)});
		for (int32_t i = 0; i < 4; i++) {
			step();
		}
	}

	void step() {
		clock += 10;
		shell.tick(clock);
	}

	void pad(const Pad& p, uint8_t velocity) {
		clock += 10;
		shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_PAD, static_cast<uint8_t>(p.x), static_cast<uint8_t>(p.y),
		                                      static_cast<int16_t>(velocity)},
		                     clock);
		step();
	}

	void enter() {
		const Pad p = pad_for(deluge::ui::grid::kTextKeyboardEnter);
		pad(p, 64);
		pad(p, 0);
		step();
	}

	bool browser_on_stack() { return scene_on_stack(shell, browser::kBrowserSceneKind); }
};

/// @brief `sampleClaimTargetFor` as one comparable string, so a mismatch prints as a split.
///
/// `"<dir>|<name>"`, or `"-"` for `std::nullopt`. Read through a default rather than through
/// `->`: a mutant that answers `std::nullopt` everywhere must fail an example, not abort the
/// process inside `std::optional::operator->` before the rest of the file has run.
/// @brief Which column @p item is on @p page, counting only the children @p ctx admits; -1 if none.
///
/// `MenuScene::visible_children`'s filter (`scenes/menu/menu_scene.cpp`) re-derived, deliberately:
/// hard-coding "the file selector is column 1" would make the focus assertion agree with a mutant
/// that renumbered the page, and reading the index back off the scene would make it agree with
/// itself. `Node::relevant` is public data (`scenes/menu/node.h`), so this is the same rule the
/// interpreter applies, spelled independently of it.
int32_t visible_index_of(const MenuContext& ctx, const Node& page, const Node& item) {
	// A group's columns are its member pages' children, in order.
	int32_t index = 0;
	const auto walk = [&](const Node& p) {
		for (const Node* kid : p.children) {
			if (kid == nullptr || !menu::bindings::relevant(*kid, ctx)) {
				continue;
			}
			if (kid == &item) {
				return true;
			}
			index++;
		}
		return false;
	};
	if (page.is_group) {
		for (const Node* member : page.children) {
			if (member != nullptr && walk(*member)) {
				return index;
			}
		}
		return -1;
	}
	return walk(page) ? index : -1;
}

std::string split_of(std::string_view path) {
	const std::optional<SampleClaimTarget> t = deluge::gui::ui::sampleClaimTargetFor(path);
	if (!t.has_value()) {
		return "-";
	}
	return std::string{t->dir} + "|" + std::string{t->name};
}

/// @brief The folder-import seam, swapped for a recorder, as `ClaimSpy` swaps the claim's.
struct ImportSpy {
	static inline int32_t dispatches = 0;
	static inline deluge::gui::FolderImportRequest last{};

	static void record(deluge::gui::FolderImportRequest request) {
		dispatches++;
		last = std::move(request);
	}

	ImportSpy() {
		dispatches = 0;
		last = {};
		deluge::gui::ui::setFolderImportDispatch(&record);
	}
	~ImportSpy() { deluge::gui::ui::setFolderImportDispatch(nullptr); }
	ImportSpy(const ImportSpy&) = delete;
	ImportSpy& operator=(const ImportSpy&) = delete;
};

constexpr uint8_t kSelectEncButtonX = 4, kSelectEncButtonY = 3;
constexpr uint8_t kSelectEncoder = 3;

/// @brief Hold Select on @p shell until the long press answers, then let go.
void long_press(Shell& shell, uint32_t& clock) {
	clock += 10;
	shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_BUTTON, kSelectEncButtonX, kSelectEncButtonY, 255}, clock);
	shell.tick(clock);
	const uint32_t up = clock + browser::kLongPressMs + 20;
	while (clock < up) {
		clock += 10;
		shell.tick(clock);
	}
	shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_BUTTON, kSelectEncButtonX, kSelectEncButtonY, 0}, clock);
	clock += 10;
	shell.tick(clock);
}

/// @brief Turn the select encoder @p detents, then press and release Select: the menu's accept.
void pick(Shell& shell, uint32_t& clock, int32_t detents) {
	if (detents != 0) {
		clock += 10;
		shell.on_board_event(
		    DelugeInputEvent{DELUGE_EVENT_ENCODER, kSelectEncoder, 0, static_cast<int16_t>(detents * 2)}, clock);
		shell.tick(clock);
	}
	for (const int16_t v : {int16_t{255}, int16_t{0}}) {
		clock += 10;
		shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_BUTTON, kSelectEncButtonX, kSelectEncButtonY, v}, clock);
		shell.tick(clock);
	}
}

/// @brief A kit whose one drum is on row 0, with the sample browser opened over it directly, as the long press
///        needs it.
struct KitRig {
	spec::SongFixture song = spec::make_kit_song();
	deluge::ui::shell::SceneRegistry reg = scene_registry();
	uitest::FakeSurfaceSet fake{deluge::ui::shell::kDelugeBoard, deluge_profile()};
	Shell shell{fake, deluge_profile(), reg, deluge::ui::shell::Services{}, ShellConfig{}};
	uint32_t clock = 0;

	KitRig(const KitRig&) = delete;
	KitRig& operator=(const KitRig&) = delete;
	KitRig() { (void)spec::add_drum<SoundDrum>(song); }
	~KitRig() {
		while (shell.stack().depth() > 0) {
			shell.stack().pop(SceneKind::Null);
		}
	}

	void open_browser() {
		select_language();
		MenuContext ctx = menu::context_for(*song.song, deluge::gui::menuViewOf(song.clip), 0, false, false, false,
		                                    /*kit_global_fx=*/false);
		(void)menu::ensure_first_range(ctx);
		browser::browser_scene().open(ctx);
		shell.init(SceneId{browser::kBrowserSceneKind, static_cast<uint16_t>(browser::BrowserId::Sample)});
		for (int32_t i = 0; i < 4; i++) {
			clock += 10;
			shell.tick(clock);
		}
	}
};

} // namespace

describe browser_choice_target(
    "gui::ui::sampleClaimTargetFor", $ {
	    // The choosing half, driven by name. It is declared in `shell_choice_dispatch.h` rather than left
	    // in the `.cpp`'s anonymous namespace for the reason `stemExportTypeForRootUI` is
	    // (`gui/ui/shell_action_dispatch.h`): the acting half cannot be run in a spec, and this half must
	    // be. Nothing in this group touches a scene, a card or a song.

	    it(
	        "splits a chosen path the way getCurrentFilePath builds one", _ {
		        // `SampleBrowser::getCurrentFilePath` is
		        // `currentDir`, a separator, and `currentFileItem->filename` with its extension; this is that
		        // run backwards, and the extension has to survive it.
		        expect(split_of("SAMPLES/OSC_A/A1.WAV")).to_equal(std::string{"SAMPLES/OSC_A|A1.WAV"});
		        expect(split_of("SAMPLES/CLAP.WAV")).to_equal(std::string{"SAMPLES|CLAP.WAV"});
	        });

	    it(
	        "treats a path with no separator as a file at the card root", _ {
		        // `getCurrentFilePath` appends the separator only when `currentDir` is non-empty (`:541-543`),
		        // so the inverse of an empty `currentDir` is a path with no separator in it at all.
		        expect(split_of("A1.WAV")).to_equal(std::string{"|A1.WAV"});
	        });

	    it(
	        "answers nothing where the path names no file", _ {
		        // Neither of these can come out of `BrowserScene::enter_key_press`, which always appends a
		        // `FileItem`'s own filename -- they are the defence-in-depth
		        // arms, pinned so a later caller that is not the scene gets the documented answer.
		        expect(split_of("")).to_equal(std::string{"-"});
		        expect(split_of("SAMPLES/OSC_A/")).to_equal(std::string{"-"});
	        });

	    it(
	        "re-joins to what it was given, which is what makes the split the inverse and not a guess", _ {
		        // The property, rather than three more literals: dir + separator-if-any + name must be the
		        // path again, by `getCurrentFilePath`'s own three statements. An implementation that dropped
		        // the extension, kept the separator on the directory, or split at the FIRST slash passes at
		        // least one literal above and fails this.
		        for (const std::string_view path : {std::string_view{"SAMPLES/OSC_A/A1.WAV"},
		                                            std::string_view{"SAMPLES/X.WAV"}, std::string_view{"A1.WAV"}}) {
			        const std::optional<SampleClaimTarget> t = deluge::gui::ui::sampleClaimTargetFor(path);
			        expect(t.has_value()).to_be_true();
			        std::string rejoined{t.has_value() ? t->dir : std::string_view{}};
			        if (!rejoined.empty()) {
				        rejoined += "/";
			        }
			        rejoined += std::string{t.has_value() ? t->name : std::string_view{}};
			        expect(rejoined).to_equal(std::string{path});
		        }
	        });
    });

describe browser_choice_dispatch(
    "gui::ui::dispatchShellChoices over the real outbox", $ {
	    // The drain, in the binary that has every half: `deluge_scenes_spec` links the real
	    // `browser_scene()`, the real `dispatchShellChoices` AND the real `ShellHostUI::timerCallback`.
	    // The host suite (`tests/spec/gui/`) links only the last of those -- it stubs this dispatcher,
	    // because the real one belongs to the `deluge_scenes` closure that suite never builds -- so what
	    // it could pin is the CALL SITE in isolation.
	    //
	    // Two isolated halves are not the composition, and the third example is the one that says so: a
	    // stub at the call site plus a separate proof that the real function reads the outbox does not
	    // prove the real call site calls the real function. That example drives a real guest's own
	    // `timerCallback()` and asserts the real outbox is empty afterwards, without naming
	    // `dispatchShellChoices` at all.
	    //
	    // Nothing in THIS group asserts that anything is performed -- that is `browser_choice_claim`'s
	    // subject below. Every example here still installs a `ClaimSpy`, because a real file press
	    // followed by a drain now reaches the claim, and the shipped dispatcher would run it.

	    before_each([] {
		    static QuietDisplay d;
		    if (display == nullptr) {
			    display = &d;
		    }
	    });

	    it(
	        "takes the choice a real file press filed, and leaves the outbox empty", _ {
		        CleanBrowser clean;
		        ClaimSpy spy;
		        ScratchCard card;
		        {
			        // The control, and it is not decoration: without it the example below is satisfied by a
			        // press that filed nothing at all, which is exactly how a drain test passes vacuously.
			        ShellRig control;
			        control.open_browser();
			        expect(control.browser_on_stack()).to_be_true();
			        control.enter();
			        // `Defer` parks AND closes in one statement (`BrowserScene::enter_key_press`'s `Defer` arm),
			        // so both halves of it are asserted here, on the leg that is allowed to look.
			        expect(control.browser_on_stack()).to_be_false();
			        const std::optional<browser::BrowserChoice> choice = browser::browser_scene().take_choice();
			        expect(choice.has_value()).to_be_true();
			        expect(static_cast<int>(choice.has_value() ? choice->id : browser::BrowserId::Count))
			            .to_equal(static_cast<int>(browser::BrowserId::Sample));
			        expect(choice.has_value() ? choice->path : std::string{}).to_equal(std::string{kChosenPath});
		        }

		        ShellRig r;
		        r.open_browser();
		        r.enter();
		        // Deliberately NOT drained by hand here -- the dispatcher is the only thing that reads it.
		        deluge::gui::ui::dispatchShellChoices();
		        expect(browser::browser_scene().take_choice().has_value()).to_be_false();
	        });

	    it(
	        "takes nothing when nothing was chosen, and does not eat the next choice", _ {
		        CleanBrowser clean;
		        ClaimSpy spy;
		        ScratchCard card;
		        ShellRig r;
		        r.open_browser();
		        // Twenty polls against an empty outbox -- which is what every quiet tick of the guest is,
		        // including every tick the browser is up and the user is still scrolling.
		        for (int32_t i = 0; i < 20; i++) {
			        r.step();
			        deluge::gui::ui::dispatchShellChoices();
		        }
		        expect(r.browser_on_stack()).to_be_true();
		        // And the press after all of them still delivers, so the polling above left the seam intact
		        // rather than jamming it.
		        r.enter();
		        const std::optional<browser::BrowserChoice> choice = browser::browser_scene().take_choice();
		        expect(choice.has_value()).to_be_true();
		        expect(choice.has_value() ? choice->path : std::string{}).to_equal(std::string{kChosenPath});
	        });

	    it(
	        "is reached BY the guest's own tick, on a tick where the browser has already closed itself", _ {
		        // The composition, end to end and with nothing stubbed: a real `ShellHostUI` over the real
		        // `scene_registry()`, a real enter press through the guest's own `padAction`, and the guest's
		        // own `timerCallback()` -- which is the only thing this example calls that could drain
		        // anything. `dispatchShellChoices` is never named here.
		        //
		        // **The two legs differ in ONE call** and share everything else: the same guest type over the
		        // same registry, the same scene singletons, the same node, the same press through the same
		        // input queue. Leg 1 ticks the SHELL (`ui.shell().tick`), which routes the press, applies the
		        // `Remove` and runs `after_render` -- but is not the guest's tick and so does not poll. Leg 2
		        // ticks the GUEST (`timerCallback`), which does all of that and then polls. So the difference
		        // between "outbox full" and "outbox empty" isolates `timerCallback`'s own extra work rather
		        // than comparing two different rigs.
		        CleanBrowser clean;
		        ClaimSpy spy;
		        ScratchCard card;
		        {
			        GuestRig control;
			        expect(control.open()).to_be_true();
			        for (int32_t i = 0; i < 4; i++) {
				        control.shell_step();
			        }
			        expect(control.browser_on_stack()).to_be_true();
			        control.press_enter(&GuestRig::shell_step);
			        // The state the drain actually runs in, asserted on the leg that may look: the browser is
			        // already gone, and the menu it was opened from is still underneath it -- so the guest's
			        // own `closeGuest()` arm (`ShellHostUI::timerCallback`'s) is not what happens here.
			        expect(control.browser_on_stack()).to_be_false();
			        expect(control.ui.shell().stack().depth() > 0).to_be_true();
			        // The press really files, on this exact path, through this exact guest. Without this the
			        // example below passes on a press that never filed anything.
			        expect(browser::browser_scene().take_choice().has_value()).to_be_true();
		        }

		        GuestRig r;
		        expect(r.open()).to_be_true();
		        for (int32_t i = 0; i < 4; i++) {
			        r.guest_step();
		        }
		        expect(r.browser_on_stack()).to_be_true();
		        // ...and ticking the guest over an open browser files nothing on its own.
		        expect(browser::browser_scene().take_choice().has_value()).to_be_false();

		        r.press_enter(&GuestRig::guest_step);
		        expect(r.browser_on_stack()).to_be_false();
		        expect(browser::browser_scene().take_choice().has_value()).to_be_false();
	        });
    });

describe browser_choice_claim(
    "gui::ui::dispatchShellChoices performing the claim", $ {
	    // What the drain *does*: `SampleBrowser::enterKeyPress`'s file arm, once the browser it reads
	    // is gone, is a claim request naming the chosen file -- which `ClaimSpy` records whole.

	    before_each([] {
		    static QuietDisplay d;
		    if (display == nullptr) {
			    display = &d;
		    }
	    });

	    it(
	        "requests the guest's claim of the chosen file, leaving the legacy browser's state alone", _ {
		        CleanBrowser clean;
		        ClaimSpy spy;
		        ScratchCard card;
		        ShellRig r;
		        r.open_browser();
		        r.enter();

		        // `BrowserScene::on_exit` empties the scenes' catalogue as it closes, and nothing has
		        // been requested yet.
		        expect(deluge::storage::FileCatalogue::fileItems.empty()).to_be_true();
		        expect(spy.dispatches).to_equal(0);

		        // The legacy browser's own state is pointed somewhere else entirely -- as under the
		        // guest it genuinely is, since the legacy `sampleBrowser` was never opened. The claim
		        // carries its file, so none of this is read or written.
		        Browser::currentDir = "SOMEWHERE/ELSE";
		        QwertyUI::enteredText = "STALE";

		        deluge::gui::ui::dispatchShellChoices();

		        expect(std::string{Browser::currentDir}).to_equal(std::string{"SOMEWHERE/ELSE"});
		        expect(std::string{QwertyUI::enteredText}).to_equal(std::string{"STALE"});

		        // Exactly one request, and it is the guest's own: legacy's default arguments claim
		        // with `loadWithoutExiting = false`, which ends in `exitAndNeverDeleteDrum()` ->
		        // `closeUI(&sampleBrowser)` on a browser the guest never opened, i.e. an index `-1`
		        // read of `uiNavigationHierarchy` and `numUIsOpen = 0` from inside the running
		        // `timerCallback` (`gui/ui/ui.cpp:213-227`). See the example below.
		        expect(spy.dispatches).to_equal(1);
		        expect(ClaimSpy::last_is_guest_claim_of("SAMPLES/OSC_A/A1.WAV", "A1")).to_be_true();
		        // ...and the drain emptied the outbox on the way, so a mutant that stops reading it
		        // fails by comparison here rather than by `CleanBrowser`'s abort.
		        expect(browser::browser_scene().take_choice().has_value()).to_be_false();
	        });

	    it(
	        "claims from the guest's own tick, with nothing in the example naming the dispatcher", _ {
		        // The composition, as the drain examples above do it: the difference between the two
		        // legs is one call, `ui.shell().tick` versus `ui.timerCallback()`. So a claim on the
		        // second leg and none on the first isolates `timerCallback`'s own extra work.
		        CleanBrowser clean;
		        ClaimSpy spy;
		        ScratchCard card;
		        {
			        GuestRig control;
			        expect(control.open()).to_be_true();
			        for (int32_t i = 0; i < 4; i++) {
				        control.shell_step();
			        }
			        expect(control.browser_on_stack()).to_be_true();
			        control.press_enter(&GuestRig::shell_step);
			        // The press filed, the browser closed -- and nothing claimed, because the shell's
			        // tick is not the guest's.
			        expect(control.browser_on_stack()).to_be_false();
			        expect(spy.dispatches).to_equal(0);
			        expect(deluge::storage::FileCatalogue::fileItems.empty()).to_be_true();
			        expect(browser::browser_scene().take_choice().has_value()).to_be_true();
		        }

		        GuestRig r;
		        expect(r.open()).to_be_true();
		        for (int32_t i = 0; i < 4; i++) {
			        r.guest_step();
		        }
		        expect(r.browser_on_stack()).to_be_true();
		        // Ticking the guest over an open browser claims nothing on its own.
		        expect(spy.dispatches).to_equal(0);

		        r.press_enter(&GuestRig::guest_step);
		        expect(r.browser_on_stack()).to_be_false();
		        // Three guest ticks, one choice, one claim: the drain is not re-entered on the ticks
		        // after the outbox is empty.
		        expect(spy.dispatches).to_equal(1);
		        expect(ClaimSpy::last_is_guest_claim_of("SAMPLES/OSC_A/A1.WAV", "A1")).to_be_true();
		        // The outbox is empty because the guest's own tick emptied it. Asserted here as well as
		        // in the group above so a mutant that deletes the call site fails this example by
		        // comparison, rather than by `CleanBrowser`'s abort -- which stops the file on its first
		        // casualty and prints one reason for all of them.
		        expect(browser::browser_scene().take_choice().has_value()).to_be_false();
	        });

	    it(
	        "drains a MIDI device definition into its own load, and claims nothing", _ {
		        // `BrowserId::MidiDeviceDefinition` defers and waits, as `LoadMidiDeviceDefinitionUI` stays open until
		        // `definitionLoaded` closes it, and the drain hands its path to the instrument load
		        // (`requestMidiDefinitionLoad`), not to the sample claim -- so the `Sample` arm's body must not be
		        // reachable from it.
		        CleanBrowser clean;
		        ClaimSpy spy;
		        ScratchCard card;
		        MidiRig r;
		        r.open_browser();
		        expect(r.browser_on_stack()).to_be_true();

		        r.enter();
		        expect(r.browser_on_stack()).to_be_true();
		        expect(browser::browser_scene().awaiting_deferred_load()).to_be_true();
		        expect(r.instrument()->deviceDefinitionFileName).to_equal(std::string{});

		        // Polled the same way a quiet guest tick polls, which is unconditionally; then the
		        // load's own job, run until it finishes. Before boot finishes the file opens directly.
		        deluge::gui::ui::dispatchShellChoices();
		        expect(spy.dispatches).to_equal(0);
		        expect(browser::browser_scene().take_choice().has_value()).to_be_false();
		        for (int32_t pass = 0; pass < 64 && deluge::gui::instrumentLoad.currentTicket().generation != 0;
		             pass++) {
			        deluge::gui::instrumentLoad.routine();
		        }
		        expect(deluge::gui::instrumentLoad.currentTicket().generation).to_equal(0U);
		        // The proof the load ran: the instrument's own remembered filename, which the
		        // definition load writes.
		        expect(r.instrument()->deviceDefinitionFileName)
		            .to_equal(std::string{"MIDI_DEVICES/DEFINITION/DEVICE1.XML"});
		        // The load's completion answered the browser, which closes on the next tick.
		        expect(browser::browser_scene().awaiting_deferred_load()).to_be_false();
		        r.step();
		        r.step();
		        expect(r.browser_on_stack()).to_be_false();
	        });

	    it(
	        "claims without exiting, because there is no legacy browser open to close", _ {
		        // **The device defect this pins.** `claimCurrentFile`'s success tail is gated on
		        // `!loadWithoutExiting` (`gui/ui/browser/sample_browser.cpp:966`), and its first
		        // statement is `exitAndNeverDeleteDrum()` (`:1109`) -- which is `close()` (`:258-260`),
		        // `Browser::close()`, `UI::close()` and finally
		        // `closeUI(this)` (`gui/ui/ui.cpp:87-89`). Under the guest `sampleBrowser` was never
		        // opened, so `closeUI`'s search falls out at `u == 0` and reads
		        // `uiNavigationHierarchy[u - 1]` (`gui/ui/ui.cpp:213-223`), then sets `numUIsOpen = 0`
		        // and unsets `TimerName::UI_SPECIFIC` (`:195-197`) -- from inside the very
		        // `ShellHostUI::timerCallback` that is running the drain.
		        //
		        // **This is the strongest form the pin can take, and the limit is the seam's own.**
		        // Reaching `closeUI` means running a real `claimCurrentFile`: a card read and an
		        // `AudioFileManager` allocation inside the assertion. So what an example can say is
		        // which op the drain dispatches -- above -- and what that op will claim with, which is
		        // here. Flip `loadWithoutExiting` and this reddens.
		        //
		        // The values are legacy's own, from the one call site that already needed this: the
		        // auto-load feature's claim in `SampleBrowser::tickPreview` (`sample_browser.cpp:654-659`),
		        // "load it into the instrument as if you had confirmed, but keep the browser open".
		        expect(deluge::gui::ui::kShellSampleClaimArgs.mayDoPitchDetection).to_equal(1);
		        expect(deluge::gui::ui::kShellSampleClaimArgs.mayDoSingleCycle).to_equal(1);
		        expect(deluge::gui::ui::kShellSampleClaimArgs.mayDoWaveTable).to_equal(1);
		        expect(deluge::gui::ui::kShellSampleClaimArgs.loadWithoutExiting).to_be_true();
	        });
    });

describe browser_choice_reroot(
    "MenuScene::reroot_to_scene_opener_page", $ {
	    // Legacy's return affordance, in the port's own terms:
	    // `parentMenuHeadingTo->focusChild(menuItemHeadingTo); menuItemNavigationRecord[0] =
	    // parentMenuHeadingTo; navigationDepth = 0` (`gui/ui/browser/sample_browser.cpp:974-976`).
	    // The page is `sourceMenuGroup` focused on `source0Menu`'s file selector (`gui/ui/menus.cpp:1748-1749`):
	    // `nodes::source_group`, whose columns run across its five member pages.
	    //
	    // @par Why the entry point is called by name here, and the claim is not run
	    //
	    // Because the claim cannot be run: it is a card read and an `AudioFileManager` allocation,
	    // which is the whole reason `ClaimSpy` exists. So the split is the one the group above
	    // already lives with: **what** the drain requests is pinned there, and what the claim's
	    // completion does on success (`shellSampleClaimDone`) is driven by name here. The state
	    // these examples re-root is not a fixture, though: it is the real state a real file press
	    // leaves behind, all four ticks and the drain included.
	    //
	    // @note The menu here is at **depth 0** on the file selector, and that is not the rig taking a
	    //       shortcut -- it is the only shape a user can produce today. The port has no port of
	    //       `MenuItem::allowToBeginSessionFromHorizontalMenu` (
	    //       overridden true by `FileSelector`, `gui/menu_item/file_selector.h:35`), so a select
	    //       press on the file selector's column of the horizontal `source0` page raises the
	    //       notification instead of opening the browser (`MenuScene::render_horizontal`'s
	    //       `column_press`, and `ui/oled/columns.cpp` behind it). The shortcut pad is what is
	    //       left, and it re-roots to depth 0 (`handle`'s `Replace` arm).

	    before_each([] {
		    static QuietDisplay d;
		    if (display == nullptr) {
			    display = &d;
		    }
	    });

	    it(
	        "lands the ported menu on the source page, at the root, with the file selector focused", _ {
		        CleanBrowser clean;
		        ClaimSpy spy;
		        ScratchCard card;
		        ShellRig r;
		        r.open_browser();

		        // Before the press: the menu IS the file selector, and it is the whole path.
		        expect(menu_scene().state().depth).to_equal(0);
		        expect(menu_scene().state().path[0] == &nodes::file0_selector).to_be_true();

		        r.enter();
		        deluge::gui::ui::dispatchShellChoices();
		        // The claim was requested and the outbox emptied -- the group above's subject, re-checked
		        // here only so a failure below cannot be blamed on the press not having landed.
		        expect(spy.dispatches).to_equal(1);

		        // The browser closed and the menu resumed, and `go_up_on_resume`'s `ascend()` had
		        // nowhere to go from depth 0: without the re-rooting the user is left staring at the
		        // file selector's own title screen.
		        expect(menu_scene().state().path[0] == &nodes::file0_selector).to_be_true();

		        expect(menu_scene().reroot_to_scene_opener_page()).to_be_true();

		        // `menuItemNavigationRecord[0] = parentMenuHeadingTo` (`:1117`), the chain's `sourceMenuGroup` ...
		        expect(menu_scene().state().path[0] == &nodes::source_group).to_be_true();
		        // ... and `navigationDepth = 0` (`:1118`). This is the assertion the "one extra BACK
		        // press" was: a re-root that left the page one level in would still satisfy the line
		        // above.
		        expect(menu_scene().state().depth).to_equal(0);
		        // ... and `parentMenuHeadingTo->focusChild(menuItemHeadingTo)` (`:1116`), which is a
		        // separate failure mode: the right page with the wrong column focused is exactly what
		        // legacy's `focusChild` exists to prevent.
		        const int32_t column = visible_index_of(r.model.ctx(), nodes::source_group, nodes::file0_selector);
		        // Pinned as a number too, so "the focus was never moved" cannot pass by agreeing with a
		        // page whose selector happens to sit first. `osc0_type` is column 0 on a subtractive
		        // source (`nodes/sound_osc.h`'s `kSource0Children`).
		        expect(column).to_equal(1);
		        expect(static_cast<int32_t>(menu_scene().state().columns[0].focused)).to_equal(column);
		        // The group pages across its members, so the counter reads `main`'s 1/6, not the member page's 1/3.
		        const deluge::ui::oled::Paging paging =
		            menu::horizontal_paging(menu_scene().menu_context(), menu_scene().state());
		        expect(static_cast<int32_t>(paging.page)).to_equal(0);
		        expect(static_cast<int32_t>(paging.total_pages)).to_equal(6);
	        });

	    it(
	        "spends the recorded opener, so a second claim does not re-root a menu that has moved on", _ {
		        // The control leg, varying exactly one thing against the example above: the entry point
		        // is called a second time, with everything else identical. Legacy clears its pair at the
		        // same point (`gui/ui/browser/sample_browser.cpp:979-980`), and the reason is the same
		        // -- the pair belongs to one trip through the browser, and a stale one re-roots a menu
		        // the user has since navigated somewhere else entirely.
		        CleanBrowser clean;
		        ClaimSpy spy;
		        ScratchCard card;
		        ShellRig r;
		        r.open_browser();
		        r.enter();
		        deluge::gui::ui::dispatchShellChoices();

		        expect(menu_scene().reroot_to_scene_opener_page()).to_be_true();
		        expect(menu_scene().state().path[0] == &nodes::source_group).to_be_true();

		        expect(menu_scene().reroot_to_scene_opener_page()).to_be_false();
		        // And a refusal is a no-op, not a reset: the menu is exactly where the first call left it.
		        expect(menu_scene().state().path[0] == &nodes::source_group).to_be_true();
		        expect(menu_scene().state().depth).to_equal(0);
	        });

	    it(
	        "asks for a frame when it moves the menu, because the claim ends between the shell's ticks", _ {
		        // `claimCurrentFile`'s tail ends in `soundEditor.enterOrUpdateSoundEditor(true)`, which draws the
		        // source page. Ours lands from the claim's own job, outside a tick, so without a repaint fact the
		        // item's page stays up until the next input.
		        CleanBrowser clean;
		        ClaimSpy spy;
		        ScratchCard card;
		        ShellRig r;
		        r.open_browser();
		        r.enter();
		        deluge::gui::ui::dispatchShellChoices();
		        r.step();
		        (void)deluge::notify::dirty().drain(deluge::notify::DirtyConsumer::Shell);

		        expect(menu_scene().reroot_to_scene_opener_page()).to_be_true();
		        expect(deluge::notify::dirty()
		                   .drain(deluge::notify::DirtyConsumer::Shell)
		                   .has(deluge::notify::Dirty::Oled))
		            .to_be_true();

		        // A refusal moves nothing and asks for nothing.
		        expect(menu_scene().reroot_to_scene_opener_page()).to_be_false();
		        expect(deluge::notify::dirty()
		                   .drain(deluge::notify::DirtyConsumer::Shell)
		                   .has(deluge::notify::Dirty::Oled))
		            .to_be_false();
	        });

	    it(
	        "refuses a menu that never opened a scene, and leaves its path alone", _ {
		        // The second control, varying one thing again: the same rig, the same root, the same
		        // four ticks -- but the menu is opened on `sound_root` rather than on the item that
		        // pushes the browser, so nothing ever recorded an opener. Legacy's guard is the same
		        // shape: the re-rooting is inside `if (menuItemHeadingTo != nullptr &&
		        // parentMenuHeadingTo != nullptr)` (`SampleClaim::stepFinish`).
		        CleanBrowser clean;
		        ClaimSpy spy;
		        ScratchCard card;
		        ShellRig r;
		        r.open_menu_at(nodes::sound_root);
		        expect(r.browser_on_stack()).to_be_false();

		        expect(menu_scene().reroot_to_scene_opener_page()).to_be_false();
		        expect(menu_scene().state().path[0] == &nodes::sound_root).to_be_true();
		        expect(menu_scene().state().depth).to_equal(0);
		        expect(spy.dispatches).to_equal(0);
	        });
    });

// clang-format off
describe browser_choice_preview("gui::ui::dispatchShellChoices and the ported browser's preview", $ {
	before_each([] {
		static QuietDisplay d;
		if (display == nullptr) {
			display = &d;
		}
	});

	it("drains the preview outbox after the choice outbox, starting the file's load as the last file loaded", _ {
		CleanBrowser clean;
		ClaimSpy spy;
		ScratchCard card;
		ShellRig r;
		r.open_browser();
		expect(browser::browser_scene().preview_outstanding()).to_be_true();
		expect(sampleBrowser.lastFilePathLoaded.empty()).to_be_true();
		deluge::gui::ui::dispatchShellChoices();
		expect(browser::browser_scene().take_preview().has_value()).to_be_false();
		expect(sampleBrowser.lastFilePathLoaded).to_equal(std::string{kChosenPath});
		expect(spy.dispatches).to_equal(0);
		// No load ends in a spec process: the browser still waits for the answer.
		expect(browser::browser_scene().preview_outstanding()).to_be_true();
	});

	it("stops previewing for an empty path, and answers at once", _ {
		CleanBrowser clean;
		ClaimSpy spy;
		ScratchCard card;
		ShellRig r;
		r.open_browser();
		deluge::gui::ui::dispatchShellChoices();
		browser::browser_scene().on_exit();
		expect(browser::browser_scene().preview_outstanding()).to_be_true();
		deluge::gui::ui::dispatchShellChoices();
		expect(browser::browser_scene().preview_outstanding()).to_be_false();
		expect(browser::browser_scene().take_preview().has_value()).to_be_false();
	});

	it("sounds the preview while the guest's sample browser is frontmost, and not under the menu", _ {
		// `AudioEngine::renderSamplePreview` renders the preview drum only for the UIs `samplePreviewUIOpen` names;
		// under the guest the current UI is `shellHostUI`, so it reads which scene the guest's last tick left frontmost.
		CleanBrowser clean;
		ClaimSpy spy;
		ScratchCard card;
		spec::UiStackGuard ui_guard;
		spec::GlobalGuestGuard guest_guard;
		SongRig model;
		select_language();
		(void)menu::assign_node_ids(nodes::kAllRoots);
		menu_scene().open(nodes::file0_selector, model.ctx());
		shellHostUI.setInitialScene(SceneId{SceneKind::FirstApp, menu::node_id(nodes::file0_selector)});
		setRootUILowLevel(&instrumentClipView);
		expect(deluge::gui::ui::samplePreviewUIOpen()).to_be_false();
		expect(openUI(&shellHostUI)).to_be_true();
		for (int32_t i = 0; i < 4; i++) {
			(void)shellHostUI.timerCallback();
		}
		expect(&shellHostUI.shell().stack().input_top() == &browser::browser_scene()).to_be_true();
		expect(deluge::gui::ui::samplePreviewUIOpen()).to_be_true();
		// The audio tier reads what the guest's tick published, so the answer changes at the next tick.
		shellHostUI.shell().stack().pop();
		expect(&shellHostUI.shell().stack().input_top() == &menu_scene()).to_be_true();
		deluge::gui::ui::refreshShellSamplePreviewSounds();
		expect(deluge::gui::ui::samplePreviewUIOpen()).to_be_false();
	});

	it("claims an auto-loaded preview keeping the browser open and the menu where it was", _ {
		// `SampleBrowser::tickPreview`'s auto-load: `ClaimOrigin::AutoLoad`, `loadWithoutExiting`, and no completion to
		// close or re-root anything.
		CleanBrowser clean;
		ClaimSpy spy;
		ScratchCard card;
		ShellRig r;
		r.open_browser();
		const menu::MenuState before = menu_scene().state();
		r.shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_BUTTON, 6, 1, 255}, r.clock + 5); // LOAD
		r.shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_BUTTON, 6, 1, 0}, r.clock + 6);
		r.step();
		expect(browser::browser_scene().auto_load()).to_be_true();
		browser::browser_scene().preview_settled(true);
		deluge::gui::ui::dispatchShellChoices();
		expect(spy.dispatches).to_equal(1);
		expect(ClaimSpy::last.path).to_equal(std::string{kChosenPath});
		expect(ClaimSpy::last.origin == deluge::gui::ClaimOrigin::AutoLoad).to_be_true();
		expect(ClaimSpy::last.loadWithoutExiting).to_be_true();
		expect(ClaimSpy::last.completion == nullptr).to_be_true();
		r.step();
		r.step();
		expect(r.browser_on_stack()).to_be_true();
		expect(menu_scene().state().depth).to_equal(before.depth);
		expect(menu_scene().state().path[before.depth] == before.path[before.depth]).to_be_true();
	});

	it("cancels an auto-load claim whenever the selection asks for another preview", _ {
		// `SampleBrowser::previewFile` and `cancelPreview`: a move supersedes the auto-load of the file it left.
		CleanBrowser clean;
		ClaimSpy spy;
		CancelSpy cancel;
		ScratchCard card;
		ShellRig r;
		r.open_browser();
		expect(browser::browser_scene().preview_outstanding()).to_be_true();
		deluge::gui::ui::dispatchShellChoices(); // the file the browser opened on
		expect(CancelSpy::cancels).to_equal(1);
		deluge::gui::ui::dispatchShellChoices(); // nothing asked
		expect(CancelSpy::cancels).to_equal(1);
		browser::browser_scene().on_exit(); // a stop
		deluge::gui::ui::dispatchShellChoices();
		expect(CancelSpy::cancels).to_equal(2);
	});

	it("starts legacy's horizontal scroll with the waveform in the image store, the main columns left to it", _ {
		// `showPreview` with a move: `PadLEDs::horizontal::setupScroll` from the image store, in
		// `UI_MODE_HORIZONTAL_SCROLL`; the guest sends no main column until `scrollFinished`.
		CleanBrowser clean;
		ClaimSpy spy;
		ScratchCard card;
		spec::UiStackGuard ui_guard;
		spec::GlobalGuestGuard guest_guard;
		SongRig model;
		select_language();
		(void)menu::assign_node_ids(nodes::kAllRoots);
		menu_scene().open(nodes::file0_selector, model.ctx());
		shellHostUI.setInitialScene(SceneId{SceneKind::FirstApp, menu::node_id(nodes::file0_selector)});
		setRootUILowLevel(&instrumentClipView);
		currentUIMode = UI_MODE_NONE;
		expect(openUI(&shellHostUI)).to_be_true();
		for (int32_t i = 0; i < 4; i++) {
			(void)shellHostUI.timerCallback();
		}
		shellHostUI.selectEncoderAction(1);
		(void)shellHostUI.timerCallback();
		// The landed preview, as the previewer leaves it: its sample in the preview drum.
		auto* preview = static_cast<MultisampleRange*>(AudioEngine::sampleForPreview->sources[0].getOrCreateFirstRange());
		Sample* sample = make_sample();
		sample->addReason();
		preview->sampleHolder.audioFile = sample;
		browser::browser_scene().preview_settled(true);
		for (int32_t i = 0; i < 4 && currentUIMode != UI_MODE_HORIZONTAL_SCROLL; i++) {
			(void)shellHostUI.timerCallback();
		}
		expect(currentUIMode).to_equal(static_cast<uint32_t>(UI_MODE_HORIZONTAL_SCROLL));
		expect(shellHostUI.surfaces().main_columns_held()).to_be_true();
		const deluge::gfx::grid::PadGrid& grid = shellHostUI.surfaces().grid().grid();
		int32_t differ = 0;
		int32_t lit = 0;
		for (int32_t y = 0; y < kDisplayHeight; y++) {
			for (int32_t x = 0; x < kDisplayWidth; x++) {
				const RGB c = grid.at(static_cast<uint8_t>(x), static_cast<uint8_t>(y));
				differ += PadLEDs::imageStore[y][x] == c ? 0 : 1;
				lit += c == RGB{} ? 0 : 1;
			}
		}
		expect(lit > 0).to_be_true();
		expect(differ).to_equal(0);
		shellHostUI.scrollFinished();
		expect(shellHostUI.surfaces().main_columns_held()).to_be_false();
		expect(currentUIMode == UI_MODE_HORIZONTAL_SCROLL).to_be_false();
		preview->sampleHolder.audioFile = nullptr;
		currentUIMode = UI_MODE_NONE;
	});

	it("lets the menu declare no LEDs while the browser its entry opened covers it", _ {
		// `SoundEditor::opened` opens the browser from `beginScreen`, before `focusRegained` sets the editor's LEDs;
		// they are set when the editor regains focus. SAVE is the menu's alone to declare.
		CleanBrowser clean;
		ClaimSpy spy;
		ScratchCard card;
		ShellRig r;
		r.open_browser();
		expect(r.browser_on_stack()).to_be_true();
		const auto save = static_cast<size_t>(deluge::ui::shell::ButtonId::Save);
		const auto back = static_cast<size_t>(deluge::ui::shell::ButtonId::Back);
		expect(r.fake.leds().desired_declared[save]).to_be_false();
		expect(r.fake.leds().desired_declared[back]).to_be_true(); // the browser's own blink
	});

	it("hands the main columns back when nothing scrolls or is about to", _ {
		// The browser left between asking for a scroll and drawing it: no `scrollFinished` will come.
		CleanBrowser clean;
		ClaimSpy spy;
		ScratchCard card;
		currentUIMode = UI_MODE_NONE;
		shellHostUI.surfaces().hold_main_columns(true);
		deluge::gui::ui::dispatchShellChoices();
		expect(shellHostUI.surfaces().main_columns_held()).to_be_false();
		currentUIMode = UI_MODE_HORIZONTAL_SCROLL; // legacy's scroll still running keeps them
		shellHostUI.surfaces().hold_main_columns(true);
		deluge::gui::ui::dispatchShellChoices();
		expect(shellHostUI.surfaces().main_columns_held()).to_be_true();
		currentUIMode = UI_MODE_NONE;
		shellHostUI.surfaces().hold_main_columns(false);
	});

	it("deletes the file the delete menu accepts, and drops it from the list once the card has", _ {
		// `DeleteFile::acceptCurrentOption` then `Browser::currentFileDeleted`; a refusal leaves the list alone.
		CleanBrowser clean;
		ClaimSpy spy;
		DeleteSpy deletes;
		ScratchCard card;
		ShellRig r;
		r.open_browser();
		expect(context_menu::descriptor(context_menu::DescriptorId::DeleteFile).accept(menu_scene().menu_context(), 0))
		    .to_be_false(); // the menu closes
		deluge::gui::ui::dispatchShellChoices();
		expect(DeleteSpy::paths.size()).to_equal(size_t(1));
		expect(DeleteSpy::paths.empty() ? std::string{} : DeleteSpy::paths[0]).to_equal(std::string{kChosenPath});
		const std::string before{browser::browser_scene().entered_text()};
		deluge::gui::ui::shellFileDeleteDone(kChosenPath, deluge::gui::ui::FileDeleteOutcome::Failed);
		expect(std::string{browser::browser_scene().entered_text()}).to_equal(before);
		deluge::gui::ui::shellFileDeleteDone(kChosenPath, deluge::gui::ui::FileDeleteOutcome::Removed);
		expect(std::string{browser::browser_scene().entered_text()} != before).to_be_true();
		for (const FileItem& item : deluge::storage::FileCatalogue::fileItems) {
			expect(std::string{"SAMPLES/OSC_A/"} + item.filename != std::string{kChosenPath}).to_be_true();
		}
	});

	it("claims RECORD from the global RECORD while the sample browser is frontmost", _ {
		// `SampleBrowser::buttonAction` answers `DEALT_WITH`, so `Buttons`' RECORD tail never arms recording.
		CleanBrowser clean;
		ClaimSpy spy;
		ScratchCard card;
		spec::UiStackGuard ui_guard;
		spec::GlobalGuestGuard guest_guard;
		SongRig model;
		select_language();
		(void)menu::assign_node_ids(nodes::kAllRoots);
		menu_scene().open(nodes::file0_selector, model.ctx());
		shellHostUI.setInitialScene(SceneId{SceneKind::FirstApp, menu::node_id(nodes::file0_selector)});
		setRootUILowLevel(&instrumentClipView);
		currentUIMode = UI_MODE_NONE;
		expect(openUI(&shellHostUI)).to_be_true();
		for (int32_t i = 0; i < 4; i++) {
			(void)shellHostUI.timerCallback();
		}
		expect(shellHostUI.buttonAction(deluge::hid::button::RECORD, false) == ActionResult::DEALT_WITH).to_be_true();
		shellHostUI.shell().stack().pop();
		expect(shellHostUI.buttonAction(deluge::hid::button::RECORD, false) == ActionResult::NOT_DEALT_WITH)
		    .to_be_true(); // the menu beneath leaves it to the global RECORD
	});

	it("hands a vertical turn to the instrument clip view beneath, and keeps it with X_ENC held", _ {
		// `SampleBrowser::verticalEncoderAction`.
		CleanBrowser clean;
		ClaimSpy spy;
		ScratchCard card;
		spec::UiStackGuard ui_guard;
		spec::GlobalGuestGuard guest_guard;
		SongRig model;
		select_language();
		(void)menu::assign_node_ids(nodes::kAllRoots);
		menu_scene().open(nodes::file0_selector, model.ctx());
		shellHostUI.setInitialScene(SceneId{SceneKind::FirstApp, menu::node_id(nodes::file0_selector)});
		setRootUILowLevel(&instrumentClipView);
		currentUIMode = UI_MODE_NONE;
		expect(openUI(&shellHostUI)).to_be_true();
		for (int32_t i = 0; i < 4; i++) {
			(void)shellHostUI.timerCallback();
		}
		expect(&shellHostUI.shell().stack().input_top() == &browser::browser_scene()).to_be_true();
		const int32_t before = model.song.clip->yScroll;
		(void)shellHostUI.verticalEncoderAction(1);
		expect(model.song.clip->yScroll != before).to_be_true();
		const int32_t moved = model.song.clip->yScroll;
		// With X_ENC held the clip view would transpose the notes on screen; the browser keeps the turn.
		NoteRow* row = model.song.clip->createNewNoteRowForYVisual(moved + 2, model.song.song);
		expect(row != nullptr).to_be_true();
		if (row == nullptr) {
			return;
		}
		{
			alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
			ModelStackWithTimelineCounter* stack = model.song.song->setupModelStackWithCurrentClip(memory);
			ModelStackWithNoteRow* with_row = model.song.clip->getNoteRowForYNote(row->y(), stack);
			(void)row->attemptNoteAdd(0, 24, 64, kNumProbabilityValues, row->getDefaultIterance(), 0, with_row, nullptr);
		}
		expect(static_cast<int32_t>(row->notes.size())).to_equal(1);
		model.song.song->persistentView.xScroll[NAVIGATION_CLIP] = 0;
		model.song.song->persistentView.xZoom[NAVIGATION_CLIP] = 48;
		const int32_t note_y = row->y();
		const auto xy = deluge::hid::button::toXY(deluge::hid::button::X_ENC);
		Buttons::buttonStates[xy.x][xy.y] = true;
		(void)shellHostUI.verticalEncoderAction(1);
		Buttons::buttonStates[xy.x][xy.y] = false;
		expect(model.song.clip->yScroll).to_equal(moved);
		NoteRow* still = model.song.clip->getNoteRowForYNote(note_y);
		expect(still != nullptr && !still->hasNoNotes()).to_be_true();
	});
});
// clang-format on

describe browser_choice_long_press(
    "gui::ui::dispatchShellChoices performing the sample browser's folder menus", $ {
	    before_each([] {
		    static QuietDisplay d;
		    if (display == nullptr) {
			    display = &d;
		    }
	    });

	    it(
	        "claims BASIC, SINGLE-CYCLE and WAVETABLE with main's detection arguments, keeping the menu up", _ {
		        struct Want {
			        int32_t detents;
			        int32_t pitch, single_cycle, wavetable;
		        };
		        for (const Want w : {Want{1, 0, 0, 0}, Want{2, 2, 2, 1}, Want{3, 1, 1, 2}}) {
			        CleanBrowser clean;
			        ClaimSpy spy;
			        ScratchCard card;
			        ShellRig r;
			        r.open_browser();
			        long_press(r.shell, r.clock);
			        expect(scene_on_stack(r.shell, context_menu::kContextMenuSceneKind)).to_be_true();
			        pick(r.shell, r.clock, w.detents);
			        expect(scene_on_stack(r.shell, context_menu::kContextMenuSceneKind)).to_be_true();
			        deluge::gui::ui::dispatchShellChoices();
			        expect(spy.dispatches).to_equal(1);
			        expect(ClaimSpy::last.path).to_equal(std::string{kChosenPath});
			        expect(ClaimSpy::last.enteredText).to_equal(std::string{"A1"});
			        expect(ClaimSpy::last.mayDoPitchDetection).to_equal(w.pitch);
			        expect(ClaimSpy::last.mayDoSingleCycle).to_equal(w.single_cycle);
			        expect(ClaimSpy::last.mayDoWaveTable).to_equal(w.wavetable);
			        expect(ClaimSpy::last.loadWithoutExiting).to_be_true();
		        }
	        });

	    it(
	        "imports MULTISAMPLES from a file's folder, starting from that file, and leaves the browser to the host",
	        _ {
		        CleanBrowser clean;
		        ImportSpy spy;
		        ScratchCard card;
		        ShellRig r;
		        r.open_browser();
		        long_press(r.shell, r.clock);
		        pick(r.shell, r.clock, 0);
		        deluge::gui::ui::dispatchShellChoices();
		        expect(spy.dispatches).to_equal(1);
		        expect(static_cast<int>(ImportSpy::last.kind))
		            .to_equal(static_cast<int>(deluge::gui::FolderImportKind::Multisamples));
		        expect(ImportSpy::last.folder).to_equal(std::string{"SAMPLES/OSC_A"});
		        expect(ImportSpy::last.previouslyViewedFilename).to_equal(std::string{"A1.WAV"});
		        expect(ImportSpy::last.exitBrowser).to_be_false();
	        });

	    it(
	        "imports a whole folder into a fresh kit with LOAD ALL", _ {
		        CleanBrowser clean;
		        ImportSpy spy;
		        ScratchCard card;
		        KitRig r;
		        r.open_browser();
		        expect(browser::browser_scene().current_is_folder()).to_be_true(); // SAMPLES/OSC_A
		        long_press(r.shell, r.clock);
		        expect(static_cast<int>(context_menu::context_menu_scene().descriptor_id()))
		            .to_equal(static_cast<int>(context_menu::DescriptorId::SampleBrowserKit));
		        pick(r.shell, r.clock, 0);
		        deluge::gui::ui::dispatchShellChoices();
		        expect(spy.dispatches).to_equal(1);
		        expect(static_cast<int>(ImportSpy::last.kind))
		            .to_equal(static_cast<int>(deluge::gui::FolderImportKind::Kit));
		        expect(ImportSpy::last.folder).to_equal(std::string{"SAMPLES/OSC_A"});
		        expect(ImportSpy::last.previouslyViewedFilename).to_equal(std::string{});
	        });

	    it(
	        "closes only the menu when the load fails, and the menu and the browser when it succeeds", _ {
		        for (const bool success : {false, true}) {
			        CleanBrowser clean;
			        ImportSpy spy;
			        ScratchCard card;
			        KitRig r;
			        r.open_browser();
			        long_press(r.shell, r.clock);
			        const int32_t item = browser::browser_scene().file_index();
			        pick(r.shell, r.clock, 0);
			        deluge::gui::ui::dispatchShellChoices();
			        expect(ImportSpy::last.completion != nullptr).to_be_true();
			        if (ImportSpy::last.completion != nullptr) {
				        ImportSpy::last.completion(success);
			        }
			        for (int32_t i = 0; i < 3; i++) {
				        r.clock += 10;
				        r.shell.tick(r.clock);
			        }
			        expect(scene_on_stack(r.shell, context_menu::kContextMenuSceneKind)).to_be_false();
			        expect(scene_on_stack(r.shell, browser::kBrowserSceneKind)).to_equal(!success);
			        if (!success) {
				        expect(browser::browser_scene().file_index()).to_equal(item); // on the same item
			        }
		        }
	        });
    });

DELUGE_SPEC(browser_choice_spec, browser_choice_target, browser_choice_dispatch, browser_choice_claim,
            browser_choice_reroot, browser_choice_long_press, browser_choice_preview)
