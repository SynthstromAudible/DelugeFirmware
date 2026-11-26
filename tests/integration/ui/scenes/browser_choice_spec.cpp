/// @file
/// @brief How a file press in the browser reaches the browser's host (`ui/scenes/browser/browser_scene.h`'s
///        `Descriptor::accept` / `DeferAndWait`) and the claim it requests, driven through a real `Shell` and a
///        real `ShellHostUI` over the real scene registry.
///
/// This is `menu_actions_spec.cpp`'s `menu_action_dispatch` group for the noun half of the same
/// wall, and what is covered here matters more than usual because one legacy statement is split in
/// two:
///
///   * **Covered:** that a real file press through the browser scene hands exactly one `BrowserChoice` to the
///     browser's host, which claims it on the press, through a bare `Shell` and through the guest's own input path;
///     that an answer the request produced before it returned waits for the guest's poll; that ticks that chose
///     nothing claim nothing; and that `sampleClaimTargetFor` splits a chosen path exactly the way
///     `SampleBrowser::getCurrentFilePath()` builds one.
///   * **The claim:** what the host requests -- the chosen file, its entered text, the guest's claim arguments and its
///     completion -- which together are `SampleBrowser::enterKeyPress`'s file arm.
///   * **NOT run, ever, in this file:** the claim itself. A real claim would mean a card read, an
///     `AudioFileManager` allocation and an `openUI(&soundEditor)` inside an assertion. `ClaimSpy`
///     installs a recording dispatcher through
///     `deluge::gui::ui::setSampleClaimDispatch`, which is why the examples can press a real file at
///     all -- see that function's own note.
///
/// @par Why the guest examples open the browser from the menu rather than as the initial scene
///
/// Because that is the only way it is ever open. `FileBrowserItem::on_focus`
/// (in `ui/scenes/menu/nodes/sample.cpp`) opens the scene and files `MenuWalkState::pending_scene`
/// (`FileBrowserItem::on_focus`), so the browser is always **above** a `MenuScene` on the shell stack. Pushed alone,
/// the stack would empty the moment the claim's completion closed it, and `ShellHostUI::timerCallback` would take
/// its `closeGuest()` arm -- `closeUI` on a guest that is not
/// on the legacy UI stack, i.e. the index `-1` read `openPerformanceView`'s doc in
/// `gui/ui/shell_action_dispatch.cpp` describes. A rig that has to avoid a production path is a rig that is testing
/// something else.
///
/// @note The spec may include `gui/` (for the browser host and the guest); the library may not. Same
///       rule `sample_items_spec.cpp` and `browser_scene_spec.cpp` run under.

#include "base/notify/dirty_set.h"
#include "gui/ui/menu_view_of.h"
#include "support/peak_seed.h"
#include "ui/scenes/browser/browser_scene.h"
#include "ui/scenes/context_menu/context_menu_scene.h"
#include "ui/scenes/menu/bindings.h"
#include "ui/scenes/menu/context.h"
#include "ui/scenes/menu/menu_scene.h"
#include "ui/scenes/menu/node.h"
#include "ui/scenes/menu/node_id.h"
#include "ui/scenes/menu/nodes/nodes.h"
#include "ui/scenes/menu/nodes/sample.h"
#include "ui/scenes/menu/walk.h"
#include "ui/scenes/profile.h"
#include "ui/scenes/recorder/recorder_scene.h"
#include "ui/scenes/registry.h"

#include "ui/shell/profiles/deluge.h"
#include "ui/shell/shell.h"
#include "ui/widgets/grid/keyboard.h"

#include "gui/l10n/l10n.h"
#include "gui/ui/browser/browser.h"
#include "gui/ui/browser/folder_import.h"
#include "gui/ui/browser/sample_claim.h"
#include "gui/ui/browser/sample_preview.h"
#include "gui/ui/browser_host.h"
#include "gui/ui/load/instrument_load.h"
#include "gui/ui/shell_guest_services.h"
#include "gui/ui/shell_host_ui.h"
#include "gui/ui/sound_target.h"
#include "gui/views/instrument_clip_view.h"
#include "hid/button.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/led/pad_leds.h"
#include "model/instrument/midi_instrument.h"
#include "model/note/note_row.h"
#include "model/project_bridge/slot_bridge.h"
#include "model/sample/last_file_path_loaded.h"
#include "model/sample/sample.h"
#include "model/song/song.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/source_resources.h"
#include "storage/cluster/cluster.h"
#include "storage/file_catalogue.h"
#include "storage/file_item.h"
#include "storage/multi_range/multisample_range.h"
#include "util/menu_entry.h"
#include "util/sample_preview_open.h"

#include "support/global_guest_menu.h"
#include "support/song_fixture.h"
#include "support/torn_down_take.h"

// Before <cppspec.hpp>: it #defines `it`, which collides with every local named `it` in the project
// headers this drags in (`audio_engine.h` -> `util/container/ordered_pos_vector.h`).
#include "support/card_access.h"

#include "support/mocks/fake_surfaces.h"

#include "cppspec.hpp"
#include "support/record_state.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>

#include "support/menu_context.h"
#include "support/spec_main.h"

/// `Buttons::isButtonPressed` reads `buttonStates` (`hid/buttons.cpp`), which the header does not export.
namespace Buttons {
extern bool buttonStates[NUM_BUTTON_COLS + 1][NUM_BUTTON_ROWS];
} // namespace Buttons

using namespace deluge::ui::scenes;
using deluge::gui::ui::SampleClaimTarget;
using deluge::ui::scenes::menu::MenuContext;
using deluge::ui::scenes::menu::Node;
using deluge::ui::shell::SceneId;
using deluge::ui::shell::SceneKind;
using deluge::ui::shell::Shell;
using deluge::ui::shell::ShellConfig;
namespace nodes = deluge::ui::scenes::menu::nodes;

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
///        (`hid/display/display.cpp`). `sample_items_spec.cpp`'s, unchanged.
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
/// **Assert-then-clear, not defensive-clear** -- `sample_items_spec.cpp`'s `CleanScenes`. A listing an example
/// left in flight would decide the next example's result, and an answer the browser host still holds is delivered
/// here, so it cannot reach the next example's browser. `before_each`/`after_each` take no `self`, so a named reason
/// on stderr plus `std::abort()` is what "loudly" means.
struct CleanBrowser {
	spec::QuietAudio quiet;

	CleanBrowser() {
		check("a browser singleton was still busy when an example started");
		// A file an earlier example previewed is the browsers' last file loaded, which a fresh browser opens beside, as
		// legacy's does; each example starts from none, and with no preview wanted.
		deluge::model::lastFilePathLoaded().clear();
		deluge::gui::ui::shellPreviewer().cancel();
	}
	~CleanBrowser() { check("an example finished with a browser singleton still busy"); }
	CleanBrowser(const CleanBrowser&) = delete;
	CleanBrowser& operator=(const CleanBrowser&) = delete;

private:
	static void check(const char* what) {
		// An answer an example's last request left held is delivered here, where no shell ticks, so it cannot reach
		// the next example's browser.
		deluge::gui::ui::deliverShellBrowserAnswers();
		if (browser::browser_scene().listing_in_flight()) {
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
/// What it records is the `ClaimRequest` the browser host hands `SampleClaim::request`: the path, the
/// entered text, the claim arguments and the completion.
///
/// @warning **Recording the request is as far as this can go, and that is a real limit on what the
///          examples below prove.** Honouring it means running the claim -- a card read and an
///          `AudioFileManager` allocation -- which is the thing the seam exists to keep out. So WHAT
///          is requested is pinned; what the claim then does is `SampleClaim`'s own.
/// @brief Records each file the browser host asks the card to delete, in place of the card.
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

/// @brief Records each auto-load cancel the browser host asks for, in place of `SampleClaim::cancelAutoLoad`.
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

/// @brief A claim dispatcher that refuses inside the request, calling the completion before `request` returns.
///
/// `SampleClaim::request` runs a first pass inline, so a claim can end inside the press that asked for it. This spy is
/// that case at its plainest.
struct RefusingClaimSpy {
	static inline int32_t dispatches = 0;

	static void refuse(const deluge::gui::ClaimRequest& request) {
		dispatches++;
		if (request.completion != nullptr) {
			request.completion(false);
		}
	}

	RefusingClaimSpy() {
		dispatches = 0;
		deluge::gui::ui::setSampleClaimDispatch(&refuse);
	}
	~RefusingClaimSpy() { deluge::gui::ui::setSampleClaimDispatch(nullptr); }
	RefusingClaimSpy(const RefusingClaimSpy&) = delete;
	RefusingClaimSpy& operator=(const RefusingClaimSpy&) = delete;
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

constexpr int32_t kHomeRow = deluge::ui::widgets::grid::kTextKeyboardHomeRow;

struct Pad {
	int32_t x = -1, y = -1;
};

/// @brief The pad that types @p c on the QWERTY layout, found by asking the toolkit's own lookup
///        rather than transcribing the table a second time (`browser_scene_spec.cpp`'s `pad_for`).
Pad pad_for(char c) {
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
	peak_seed::seed_peaks(*s, [&](int32_t i) {
		return std::pair<int8_t, int8_t>{static_cast<int8_t>(static_cast<int8_t>(-(16 + i))),
		                                 static_cast<int8_t>(static_cast<int8_t>(16 + i))};
	});
	return s;
}

/// @brief The model half both rigs share: a synth song whose oscillator 0 is a sample source
///        pointing at `kChosenPath`, so `sample_start_at` lands the browser on that file.
struct SongRig {
	spec::SongFixture song = spec::make_synth_song();
	SourceResources* source = nullptr;
	MultisampleRange* range = nullptr;

	SongRig(const SongRig&) = delete;
	SongRig& operator=(const SongRig&) = delete;

	SongRig() {
		auto* sound = song.instrument->toModControllable()->effectsOwner()->sound;
		source = &sound->sourceResources[0];
		deluge::test::set(*sound->state_, "osc1/type", "sample");
		deluge::test::set(*sound->state_, "osc1/loopMode", "1"); // ONCE
		sound->deriveConfig();
		Sample* sample = make_sample();
		range = static_cast<MultisampleRange*>(sound->getOrCreateFirstRange(0));
		range->sampleHolder.audioFile = sample;
		range->sampleHolder.filePath = std::string{kChosenPath};
		sample->filePath = std::string{kChosenPath}; // the record names the range's file by its sample
		range->sampleHolder.startPos = 0;
		range->sampleHolder.endPos = sample->lengthInSamples;
		deluge::bridge::recordLoadedSound(*sound, *song.song); // set field by field: recorded as a load does
	}

	/// @brief The context `SoundEditor::setup` would have resolved for source 0.
	MenuContext ctx() {
		MenuContext c = spec::ctx_of(*song.song, deluge::gui::menuViewOf(song.clip), 0, false, false, false,
		                             /*kit_global_fx=*/false);
		c.range_index = 0;
		return c;
	}
};

/// @brief A bare `Shell` over `FakeSurfaceSet`, with the application's own registry -- the rig for
///        the examples that drive the browser host without the guest.
struct ShellRig {
	SongRig model;
	deluge::ui::shell::SceneRegistry reg = scene_registry();
	uitest::FakeSurfaceSet fake{deluge::ui::shell::kDelugeBoard, deluge_profile()};
	Shell shell{fake, deluge_profile(), reg, deluge::ui::shell::Services{}, ShellConfig{}};
	uint32_t clock = 0;

	/// The application's browser host, as the guest installs it.
	ShellRig() { deluge::gui::ui::wireShellBrowserHost(); }
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
	/// (`ui/scenes/menu/menu_scene.cpp`'s `on_enter`), which is exactly what the jump's `Action::Kind::
	/// Replace` produces (`handle`'s pad arm) -- and today the pad is the only way to focus a file
	/// selector at all (see the group's own note).
	void open_menu_at(const Node& node) { open_menu_at(node, model.ctx()); }

	/// @brief `open_menu_at` on @p ctx instead of the model's single-range context.
	void open_menu_at(const Node& node, const MenuContext& ctx) {
		select_language();
		(void)menu::assign_node_ids(nodes::kAllRoots);
		menu_scene().open(node, ctx);
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
		const Pad p = pad_for(deluge::ui::widgets::grid::kTextKeyboardEnter);
		pad(p, 64);
		pad(p, 0);
		step();
	}

	bool browser_on_stack() { return scene_on_stack(shell, browser::kBrowserSceneKind); }
};

/// @brief A real `ShellHostUI` over the real registry -- the rig for the example that reaches the browser host
///        through the guest's own input path and tick.
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

	/// One tick of the GUEST: the shell's tick, then the guest's polls.
	void guest_step() { (void)ui.timerCallback(); }

	/// @brief Press and release the enter pad through the guest's own hook -- which is the point:
	///        `ShellHostUI::padAction` only queues, and a tick is what routes.
	void press_enter(void (GuestRig::*tick)()) {
		const Pad p = pad_for(deluge::ui::widgets::grid::kTextKeyboardEnter);
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
/// `BrowserId::MidiDeviceDefinition` defers and waits for its load, and the browser host hands its choice
/// to the instrument load rather than the sample claim. This rig exists to pin that the claim
/// wiring leaves it there.
///
/// It pushes the browser's `SceneId` directly, the way `browser_scene_spec.cpp`'s own rig does,
/// rather than re-deriving `nodes::MidiDeviceLinkedItem` (`ui/scenes/menu/nodes/output_items.h`): what
/// is under test here is what the browser host does with a descriptor's answer, not how that item's
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
	MidiRig() { deluge::gui::ui::wireShellBrowserHost(); }
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
		browser::browser_scene().open(spec::ctx_of(*song.song, deluge::gui::menuViewOf(song.clip), 0, false, false,
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
		const Pad p = pad_for(deluge::ui::widgets::grid::kTextKeyboardEnter);
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
/// `menu::visible_children`'s filter (`ui/scenes/menu/menu_core.cpp`) re-derived, deliberately:
/// hard-coding "the file selector is column 1" would make the focus assertion agree with a mutant
/// that renumbered the page, and reading the index back off the scene would make it agree with
/// itself. `Node::relevant` is public data (`ui/scenes/menu/node.h`), so this is the same rule the
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
constexpr uint8_t kBackButtonX = 7, kBackButtonY = 1;
constexpr uint8_t kShiftButtonX = 8, kShiftButtonY = 0;
constexpr uint8_t kCrossScreenButtonX = 6, kCrossScreenButtonY = 2;
constexpr uint8_t kKitButtonX = 5, kKitButtonY = 1;

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

/// @brief Turn the select encoder @p detents one detent at a time, each its own event and tick, as a hand turns it.
void turn_detents(Shell& shell, uint32_t& clock, int32_t detents) {
	const int16_t edges = detents < 0 ? -2 : 2;
	for (int32_t i = 0; i < std::abs(detents); i++) {
		clock += 10;
		shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_ENCODER, kSelectEncoder, 0, edges}, clock);
		shell.tick(clock);
	}
}

/// @brief Press and release BACK, a tick after each.
void press_back(Shell& shell, uint32_t& clock) {
	for (const int16_t v : {int16_t{255}, int16_t{0}}) {
		clock += 10;
		shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_BUTTON, kBackButtonX, kBackButtonY, v}, clock);
		shell.tick(clock);
	}
}

/// @brief BACK out of the browser the menu opened inside `SAMPLES/OSC_A`: up to `SAMPLES`, then out
///        (`BrowserScene::back`), letting each listing land.
void leave_browser(ShellRig& r) {
	for (int32_t press = 0; press < 2; press++) {
		press_back(r.shell, r.clock);
		for (int32_t i = 0; i < 4; i++) {
			r.step();
		}
	}
}

/// @brief Give @p model's oscillator 0 three ranges, each on a sample at `kChosenPath`, recorded as a load records
///        them: the shape a MULTISAMPLES import of three pitches leaves.
void give_three_ranges(SongRig& model) {
	auto* sound = model.song.instrument->toModControllable()->effectsOwner()->sound;
	model.range->topNote = 59;
	for (int32_t i = 1; i < 3; i++) {
		auto* range = static_cast<MultisampleRange*>(model.source->ranges.insertMultiRange(i));
		if (range == nullptr) {
			spec_bail("no memory for a range");
		}
		Sample* sample = make_sample();
		sample->filePath = std::string{kChosenPath};
		range->topNote = i == 1 ? 71 : 32767;
		range->sampleHolder.audioFile = sample;
		range->sampleHolder.filePath = std::string{kChosenPath};
		range->sampleHolder.startPos = 0;
		range->sampleHolder.endPos = sample->lengthInSamples;
	}
	model.range = static_cast<MultisampleRange*>(model.source->ranges.getElement(0));
	deluge::bridge::recordLoadedSound(*sound, *model.song.song);
}

/// @brief Shift + (0,5) on @p r's several ranges: the note-range picker, standing in for the file selector. Turn it to
///        the bottom range, SELECT to open the browser from it, and ask for a MULTISAMPLES import of the browser's
///        file's folder from its long-press menu.
void import_from_picker(ShellRig& r) {
	MenuContext ctx = r.model.ctx();
	ctx.range_index = -1; // several ranges and none chosen, as `context_for` leaves them
	r.open_menu_at(nodes::file0_selector, ctx);
	turn_detents(r.shell, r.clock, -3);
	pick(r.shell, r.clock, 0); // the picker heads to the file selector, which pushes the browser
	for (int32_t i = 0; i < 4; i++) {
		r.step();
	}
	long_press(r.shell, r.clock);
	pick(r.shell, r.clock, 0); // MULTISAMPLES
	deluge::gui::ui::pollShellBrowserHost();
}

/// @brief A walk at depth 0 on @p submenu, with its column and its row both at @p focused.
menu::MenuState state_on(const Node& submenu, int32_t focused) {
	menu::MenuState st{};
	st.path[0] = &submenu;
	st.columns[0].focused = static_cast<int8_t>(focused);
	st.list[0].cursor = static_cast<int16_t>(focused);
	return st;
}

/// @brief Press and release the button at (@p x, @p y), past the page buttons' shared 140 ms debounce, with SHIFT held
///        around it when @p shift.
void press_button(Shell& shell, uint32_t& clock, uint8_t x, uint8_t y, bool shift) {
	clock += 200;
	const auto send = [&](uint8_t bx, uint8_t by, int16_t v) {
		clock += 10;
		shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_BUTTON, bx, by, v}, clock);
		shell.tick(clock);
	};
	if (shift) {
		send(kShiftButtonX, kShiftButtonY, 255);
	}
	send(x, y, 255);
	send(x, y, 0);
	if (shift) {
		send(kShiftButtonX, kShiftButtonY, 0);
	}
}

/// @brief Open the menu on @p page with @p column focused, as a shortcut pad's page landing does, on @p ctx.
void open_page_at(ShellRig& r, const Node& page, const Node& column, const MenuContext& ctx) {
	select_language();
	(void)menu::assign_node_ids(nodes::kAllRoots);
	menu_scene().open(page, ctx);
	const uint16_t arg = menu_scene().arm_page_landing(page, column);
	r.shell.init(SceneId{SceneKind::FirstApp, arg});
	for (int32_t i = 0; i < 4; i++) {
		r.step();
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
	KitRig() {
		(void)spec::add_drum<SoundDrum>(song);
		deluge::gui::ui::wireShellBrowserHost();
	}
	~KitRig() {
		while (shell.stack().depth() > 0) {
			shell.stack().pop(SceneKind::Null);
		}
	}

	void open_browser() {
		select_language();
		MenuContext ctx = spec::ctx_of(*song.song, deluge::gui::menuViewOf(song.clip), 0, false, false, false,
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
	    // The choosing half, driven by name. It is declared in `browser_host.h` rather than left
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
		        // `getCurrentFilePath` appends the separator only when `currentDir` is non-empty
		        // (`SampleBrowser::getCurrentFilePath` in `origin/main:src/deluge/gui/ui/browser/sample_browser.cpp`),
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

// clang-format off
describe browser_choice_host("gui::ui's browser host, performing on the press", $ {
	// The press hands the chosen file to the host, which claims it then: there is no outbox for a later poll to read.
	// Every example installs a `ClaimSpy` or `RefusingClaimSpy`, because a real file press reaches the claim, and the
	// shipped dispatcher would run it.

	before_each([] {
		static QuietDisplay d;
		if (display == nullptr) {
			display = &d;
		}
	});

	it("claims the file a real press chose while the press is routed, with the browser up and waiting", _ {
		CleanBrowser clean;
		ClaimSpy spy;
		ScratchCard card;
		ShellRig r;
		r.open_browser();
		expect(spy.dispatches).to_equal(0);
		r.enter();
		expect(spy.dispatches).to_equal(1);
		expect(ClaimSpy::last_is_guest_claim_of("SAMPLES/OSC_A/A1.WAV", "A1")).to_be_true();
		expect(r.browser_on_stack()).to_be_true();
		expect(browser::browser_scene().awaiting_deferred_load()).to_be_true();
	});

	it("claims nothing on ticks that chose nothing, and still claims the next press", _ {
		CleanBrowser clean;
		ClaimSpy spy;
		ScratchCard card;
		ShellRig r;
		r.open_browser();
		for (int32_t i = 0; i < 20; i++) {
			r.step();
			deluge::gui::ui::pollShellBrowserHost();
		}
		expect(r.browser_on_stack()).to_be_true();
		expect(spy.dispatches).to_equal(0);
		r.enter();
		expect(spy.dispatches).to_equal(1);
	});

	it("claims once, on the press, through the guest's own input path and tick", _ {
		CleanBrowser clean;
		ClaimSpy spy;
		ScratchCard card;
		GuestRig r;
		expect(r.open()).to_be_true();
		for (int32_t i = 0; i < 4; i++) {
			r.guest_step();
		}
		expect(r.browser_on_stack()).to_be_true();
		expect(spy.dispatches).to_equal(0);
		r.press_enter(&GuestRig::guest_step);
		expect(r.browser_on_stack()).to_be_true();
		expect(spy.dispatches).to_equal(1);
		expect(ClaimSpy::last_is_guest_claim_of("SAMPLES/OSC_A/A1.WAV", "A1")).to_be_true();
	});

	it("answers a claim that ended inside its request after the tick that asked, not inside the press", _ {
		// Answered inside the request, the completion would reach the browser from inside its own `handle`. The host
		// holds it, and the guest's after-tick poll delivers it before the next tick reads input.
		CleanBrowser clean;
		RefusingClaimSpy spy;
		ScratchCard card;
		ShellRig r;
		r.open_browser();
		r.enter();
		expect(RefusingClaimSpy::dispatches).to_equal(1);
		expect(browser::browser_scene().awaiting_deferred_load()).to_be_true();
		expect(deluge::gui::ui::shellBrowserAnswersHeld()).to_be_true();
		deluge::gui::ui::pollShellBrowserHost();
		expect(deluge::gui::ui::shellBrowserAnswersHeld()).to_be_false();
		expect(browser::browser_scene().awaiting_deferred_load()).to_be_false();
		r.step();
		r.step();
		expect(r.browser_on_stack()).to_be_true();
		expect(menu_scene().state().path[0]).to_equal(&nodes::file0_selector);
	});
});
// clang-format on

describe browser_choice_claim(
    "the browser host performing the claim", $ {
	    // What the host *does*: `SampleBrowser::enterKeyPress`'s file arm, once the browser it reads
	    // is gone, is a claim request naming the chosen file -- which `ClaimSpy` records whole.

	    before_each([] {
		    static QuietDisplay d;
		    if (display == nullptr) {
			    display = &d;
		    }
	    });

	    it(
	        "requests the guest's claim of the chosen file on the press, carrying its own path", _ {
		        // The claim carries its file: the browser is up, waiting, with its listing still in the scenes'
		        // catalogue, which the claim reads none of.
		        CleanBrowser clean;
		        ClaimSpy spy;
		        ScratchCard card;
		        ShellRig r;
		        r.open_browser();
		        r.enter();
		        expect(spy.dispatches).to_equal(1);
		        expect(ClaimSpy::last_is_guest_claim_of("SAMPLES/OSC_A/A1.WAV", "A1")).to_be_true();
		        expect(deluge::storage::FileCatalogue::fileItems.empty()).to_be_false();
		        expect(browser::browser_scene().awaiting_deferred_load()).to_be_true();
	        });

	    it(
	        "names the menu's sound, source and range in the request", _ {
		        CleanBrowser clean;
		        ClaimSpy spy;
		        ScratchCard card;
		        ShellRig r;
		        r.open_browser();
		        r.enter();
		        Sound* const sound = r.model.song.instrument->toModControllable()->effectsOwner()->sound;
		        deluge::gui::ui::pollShellBrowserHost();

		        expect(spy.dispatches).to_equal(1);
		        expect(ClaimSpy::last.target.sound).to_equal(sound);
		        expect(ClaimSpy::last.target.source).to_equal(r.model.source);
		        expect(ClaimSpy::last.target.range).to_equal(static_cast<::MultiRange*>(r.model.range));
		        expect(static_cast<int32_t>(ClaimSpy::last.target.rangeIndex)).to_equal(0);
		        expect(static_cast<int32_t>(ClaimSpy::last.target.sourceIndex)).to_equal(0);
	        });

	    it(
	        "hands a MIDI device definition to its own load, and claims nothing", _ {
		        // `BrowserId::MidiDeviceDefinition` defers and waits, as `LoadMidiDeviceDefinitionUI` stays open until
		        // `definitionLoaded` closes it, and the host hands its path to the instrument load
		        // (`requestMidiDefinitionLoad`), not to the sample claim -- so the `Sample` arm's body must not be
		        // reachable from it.
		        CleanBrowser clean;
		        ClaimSpy spy;
		        ScratchCard card;
		        MidiRig r;
		        r.open_browser();
		        expect(r.browser_on_stack()).to_be_true();

		        // The host requests the load on the press. Before boot finishes the file opens directly, inside the
		        // request, so its answer is held and the browser still waits.
		        r.enter();
		        expect(deluge::gui::ui::shellBrowserAnswersHeld()).to_be_true();
		        expect(r.browser_on_stack()).to_be_true();
		        expect(browser::browser_scene().awaiting_deferred_load()).to_be_true();

		        // Polled the same way a quiet guest tick polls, which is unconditionally; then the
		        // load's own job, run until it finishes.
		        deluge::gui::ui::pollShellBrowserHost();
		        expect(spy.dispatches).to_equal(0);
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
	        "claims with the browser's three detection defaults, as the auto-load does", _ {
		        // The values the auto-load feature's claim uses (`SampleBrowser::previewIfPossible`,
		        // `origin/main:src/deluge/gui/ui/browser/sample_browser.cpp`): "auto-loaded into the current instrument
		        // as if you had confirmed".
		        expect(deluge::gui::ui::kShellSampleClaimArgs.mayDoPitchDetection).to_equal(1);
		        expect(deluge::gui::ui::kShellSampleClaimArgs.mayDoSingleCycle).to_equal(1);
		        expect(deluge::gui::ui::kShellSampleClaimArgs.mayDoWaveTable).to_equal(1);
	        });

	    it(
	        "keeps the browser up until the claim lands, then closes it over the source page", _ {
		        CleanBrowser clean;
		        ClaimSpy spy;
		        ScratchCard card;
		        ShellRig r;
		        r.open_browser();
		        r.enter();
		        deluge::gui::ui::pollShellBrowserHost();
		        expect(spy.dispatches).to_equal(1);

		        // `claimCurrentFile` loads with the browser up; the claim here has not landed yet.
		        r.step();
		        r.step();
		        expect(r.browser_on_stack()).to_be_true();
		        expect(browser::browser_scene().awaiting_deferred_load()).to_be_true();

		        // The completion the request carries, called as the claim calls it once the file is in.
		        expect(ClaimSpy::last.completion).not_().to_be_null();
		        if (ClaimSpy::last.completion != nullptr) {
			        ClaimSpy::last.completion(true);
		        }
		        r.step();
		        r.step();
		        expect(r.browser_on_stack()).to_be_false();
		        // The menu beneath was moved onto the source page while the browser covered it, and resuming did not
		        // step it back up off that page.
		        expect(menu_scene().state().path[0]).to_equal(&nodes::source_group);
		        expect(menu_scene().state().depth).to_equal(0);
	        });

	    it(
	        "leaves the browser up for another file, and the menu where it was, when the claim fails", _ {
		        CleanBrowser clean;
		        ClaimSpy spy;
		        ScratchCard card;
		        ShellRig r;
		        r.open_browser();
		        r.enter();
		        deluge::gui::ui::pollShellBrowserHost();
		        expect(ClaimSpy::last.completion).not_().to_be_null();
		        if (ClaimSpy::last.completion != nullptr) {
			        ClaimSpy::last.completion(false);
		        }
		        r.step();
		        r.step();
		        expect(r.browser_on_stack()).to_be_true();
		        expect(browser::browser_scene().awaiting_deferred_load()).to_be_false();
		        expect(menu_scene().state().path[0]).to_equal(&nodes::file0_selector);
	        });

	    it(
	        "with horizontal menus off, leaves the menu once the claim lands, as main's editor goes up from the file "
	        "selector",
	        _ {
		        // No page lists the file selector, so the claim re-roots nothing (`reroot_to_scene_opener_page`), and
		        // `main`'s editor regains focus with `shouldGoUpOneLevelOnBegin` set: going up from depth 0 exits it.
		        CleanBrowser clean;
		        ClaimSpy spy;
		        ScratchCard card;
		        ShellRig r;
		        select_language();
		        (void)menu::assign_node_ids(nodes::kAllRoots);
		        MenuContext ctx = r.model.ctx();
		        ctx.horizontal_menus = false;
		        menu_scene().open(nodes::file0_selector, ctx);
		        r.shell.init(SceneId{SceneKind::FirstApp, menu::node_id(nodes::file0_selector)});
		        for (int32_t i = 0; i < 4; i++) {
			        r.step();
		        }
		        expect(r.browser_on_stack()).to_be_true();
		        r.enter();
		        deluge::gui::ui::pollShellBrowserHost();
		        expect(ClaimSpy::last.completion).not_().to_be_null();
		        if (ClaimSpy::last.completion != nullptr) {
			        ClaimSpy::last.completion(true);
		        }
		        for (int32_t i = 0; i < 4; i++) {
			        r.step();
		        }
		        expect(r.browser_on_stack()).to_be_false();
		        expect(scene_on_stack(r.shell, menu_scene().kind())).to_be_false();
	        });
    });

describe browser_choice_reroot(
    "MenuScene::reroot_to_scene_opener_page", $ {
	    // Main's return affordance, in the port's own terms: the `!loadWithoutExiting` arm of
	    // `SampleBrowser::claimCurrentFile` (`origin/main:src/deluge/gui/ui/browser/sample_browser.cpp`) focuses
	    // `menuItemHeadingTo` on `parentMenuHeadingTo` and sets the sound editor up on that page. The page is
	    // `sourceMenuGroup` focused on `source0Menu`'s file selector (`origin/main:src/deluge/gui/ui/menus.cpp`):
	    // `nodes::source_group`, whose columns run across its five member pages.
	    //
	    // @par Why the entry point is called by name here, and the claim is not run
	    //
	    // Because the claim cannot be run: it is a card read and an `AudioFileManager` allocation,
	    // which is the whole reason `ClaimSpy` exists. So the split is the one the group above
	    // already lives with: **what** the browser host requests is pinned there, and what the claim's
	    // completion does on success (`shellSampleClaimDone`) is driven by name here. The state
	    // these examples re-root is not a fixture, though: it is the real state a real file press
	    // leaves behind, all four ticks and the press's claim included.
	    //
	    // @note The menu here is at **depth 0** on the file selector, and that is not the rig taking a
	    //       shortcut -- it is the only shape a user can produce today. The port has no port of
	    //       `MenuItem::allowToBeginSessionFromHorizontalMenu` (
	    //       overridden true by `FileSelector`, `origin/main:src/deluge/gui/menu_item/file_selector.h`), so a select
	    //       press on the file selector's column of the horizontal `source0` page raises the
	    //       notification instead of opening the browser (`MenuView::render_horizontal`'s
	    //       `column_press`, and `ui/widgets/oled/columns.cpp` behind it). The shortcut pad is what is
	    //       left, and it re-roots to depth 0 (`handle`'s `Replace` arm).

	    before_each([] {
		    static QuietDisplay d;
		    if (display == nullptr) {
			    display = &d;
		    }
	    });

	    it(
	        "lands the menu on the source page, at the root, with the file selector focused", _ {
		        CleanBrowser clean;
		        ClaimSpy spy;
		        ScratchCard card;
		        ShellRig r;
		        r.open_browser();

		        // Before the press: the menu IS the file selector, and it is the whole path.
		        expect(menu_scene().state().depth).to_equal(0);
		        expect(menu_scene().state().path[0]).to_equal(&nodes::file0_selector);

		        r.enter();
		        deluge::gui::ui::pollShellBrowserHost();
		        // The claim was requested on the press -- the group above's subject, re-checked
		        // here only so a failure below cannot be blamed on the press not having landed.
		        expect(spy.dispatches).to_equal(1);

		        // The browser is up for the claim and the menu beneath is where the press left it: on the file
		        // selector, at the root.
		        expect(menu_scene().state().path[0]).to_equal(&nodes::file0_selector);

		        expect(menu_scene().reroot_to_scene_opener_page()).to_be_true();

		        // Main's `SampleBrowser::claimCurrentFile` (`origin/main:src/deluge/gui/ui/browser/sample_browser.cpp`)
		        // sets the sound editor up on `parentMenuHeadingTo`, the chain's `sourceMenuGroup` ...
		        expect(menu_scene().state().path[0]).to_equal(&nodes::source_group);
		        // ... at depth 0 (`soundEditor.setup(getCurrentClip(), parentMenu)`). This is the assertion the "one
		        // extra BACK press" was: a re-root that left the page one level in would still satisfy the line above.
		        expect(menu_scene().state().depth).to_equal(0);
		        // ... and `parentMenu->focusChild(menuItem)` in the same function, which is a
		        // separate failure mode: the right page with the wrong column focused is exactly what
		        // legacy's `focusChild` exists to prevent.
		        const int32_t column = visible_index_of(r.model.ctx(), nodes::source_group, nodes::file0_selector);
		        // Pinned as a number too, so "the focus was never moved" cannot pass by agreeing with a
		        // page whose selector happens to sit first. `osc0_type` is column 0 on a subtractive
		        // source (`nodes/sound_osc.h`'s `kSource0Children`).
		        expect(column).to_equal(1);
		        expect(static_cast<int32_t>(menu_scene().state().columns[0].focused)).to_equal(column);
		        // The group pages across its members, so the counter reads `main`'s 1/6, not the member page's 1/3.
		        const deluge::ui::widgets::oled::Paging paging =
		            menu::horizontal_paging(menu_scene().menu_context(), menu_scene().state());
		        expect(static_cast<int32_t>(paging.page)).to_equal(0);
		        expect(static_cast<int32_t>(paging.total_pages)).to_equal(6);
	        });

	    it(
	        "spends the recorded opener, so a second claim does not re-root a menu that has moved on", _ {
		        // The control leg, varying exactly one thing against the example above: the entry point is called a
		        // second time, with everything else identical. Legacy clears its pair at the same point
		        // (`SampleBrowser::exitAndNeverDeleteDrum` in
		        // `origin/main:src/deluge/gui/ui/browser/sample_browser.cpp`), and the reason is the same -- the pair
		        // belongs to one trip through the browser, and a stale one re-roots a menu the user has since navigated
		        // somewhere else entirely.
		        CleanBrowser clean;
		        ClaimSpy spy;
		        ScratchCard card;
		        ShellRig r;
		        r.open_browser();
		        r.enter();
		        deluge::gui::ui::pollShellBrowserHost();

		        expect(menu_scene().reroot_to_scene_opener_page()).to_be_true();
		        expect(menu_scene().state().path[0]).to_equal(&nodes::source_group);

		        expect(menu_scene().reroot_to_scene_opener_page()).to_be_false();
		        // And a refusal is a no-op, not a reset: the menu is exactly where the first call left it.
		        expect(menu_scene().state().path[0]).to_equal(&nodes::source_group);
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
		        deluge::gui::ui::pollShellBrowserHost();
		        r.step();
		        (void)deluge::base::notify::dirty().drain(deluge::base::notify::DirtyConsumer::Shell);

		        expect(menu_scene().reroot_to_scene_opener_page()).to_be_true();
		        expect(deluge::base::notify::dirty()
		                   .drain(deluge::base::notify::DirtyConsumer::Shell)
		                   .has(deluge::base::notify::Dirty::Oled))
		            .to_be_true();

		        // A refusal moves nothing and asks for nothing.
		        expect(menu_scene().reroot_to_scene_opener_page()).to_be_false();
		        expect(deluge::base::notify::dirty()
		                   .drain(deluge::base::notify::DirtyConsumer::Shell)
		                   .has(deluge::base::notify::Dirty::Oled))
		            .to_be_false();
	        });

	    it(
	        "refuses a menu that never opened a scene, and leaves its path alone", _ {
		        // The second control, varying one thing again: the same rig, the same root, the same
		        // four ticks -- but the menu is opened on `sound_root` rather than on the item that
		        // pushes the browser, so nothing ever recorded an opener. Main's guard is the same
		        // shape: the re-rooting is inside `if (menuItem != nullptr && parentMenu != nullptr ...)`, the two
		        // copied from `menuItemHeadingTo` and `parentMenuHeadingTo` (`SampleBrowser::claimCurrentFile`,
		        // `origin/main:src/deluge/gui/ui/browser/sample_browser.cpp`).
		        CleanBrowser clean;
		        ClaimSpy spy;
		        ScratchCard card;
		        ShellRig r;
		        r.open_menu_at(nodes::sound_root);
		        expect(r.browser_on_stack()).to_be_false();

		        expect(menu_scene().reroot_to_scene_opener_page()).to_be_false();
		        expect(menu_scene().state().path[0]).to_equal(&nodes::sound_root);
		        expect(menu_scene().state().depth).to_equal(0);
		        expect(spy.dispatches).to_equal(0);
	        });
    });

// clang-format off
describe browser_choice_preview("the browser host and the browser scene's preview", $ {
	before_each([] {
		static QuietDisplay d;
		if (display == nullptr) {
			display = &d;
		}
	});

	it("starts the file's load as the last file loaded once the browser asks for its preview", _ {
		CleanBrowser clean;
		ClaimSpy spy;
		ScratchCard card;
		ShellRig r;
		r.open_browser();
		expect(browser::browser_scene().preview_outstanding()).to_be_true();
		expect(deluge::model::lastFilePathLoaded().empty()).to_be_true();
		deluge::gui::ui::pollShellBrowserHost();
		expect(deluge::model::lastFilePathLoaded()).to_equal(std::string{kChosenPath});
		expect(spy.dispatches).to_equal(0);
		// No load ends in a spec process: the browser still waits for the answer.
		expect(browser::browser_scene().preview_outstanding()).to_be_true();
	});

	it("stops previewing for an empty path at once, and answers after the tick that asked", _ {
		// A browser leaving asks for the stop from `on_exit`, inside the shell's tick: the previewer stops then, and the
		// answer waits for the guest's poll, so it cannot reach the browser from inside its own hook.
		CleanBrowser clean;
		ClaimSpy spy;
		ScratchCard card;
		ShellRig r;
		r.open_browser();
		deluge::gui::ui::pollShellBrowserHost();
		browser::browser_scene().on_exit();
		expect(deluge::gui::ui::shellPreviewer().pending()).to_be_false();
		expect(browser::browser_scene().preview_outstanding()).to_be_true();
		expect(deluge::gui::ui::shellBrowserAnswersHeld()).to_be_true();
		deluge::gui::ui::pollShellBrowserHost();
		expect(browser::browser_scene().preview_outstanding()).to_be_false();
	});

	it("drops a leaving browser's held stop answer once the next visit files its own preview", _ {
		// The stop `on_exit` asked for answers about a visit that is over: delivered after the next visit's first
		// preview, it would end that preview's wait and spend its spare blink.
		CleanBrowser clean;
		ClaimSpy spy;
		ScratchCard card;
		ShellRig r;
		r.open_browser();
		deluge::gui::ui::pollShellBrowserHost();
		while (r.shell.stack().depth() > 0) {
			r.shell.stack().pop(SceneKind::Null);
		}
		expect(deluge::gui::ui::shellBrowserAnswersHeld()).to_be_true();
		r.open_browser();
		expect(r.browser_on_stack()).to_be_true();
		expect(browser::browser_scene().preview_outstanding()).to_be_true();
		expect(deluge::gui::ui::shellBrowserAnswersHeld()).to_be_false();
		deluge::gui::ui::pollShellBrowserHost();
		expect(browser::browser_scene().preview_outstanding()).to_be_true();
		browser::browser_scene().preview_settled(true);
		expect(browser::browser_scene().browse_blink()).to_equal(browser::BrowseBlink::Menu);
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
		expect(&shellHostUI.shell().stack().input_top()).to_equal(&browser::browser_scene());
		expect(deluge::gui::ui::samplePreviewUIOpen()).to_be_true();
		// The audio tier reads what the guest's tick published, so the answer changes at the next tick.
		shellHostUI.shell().stack().pop();
		expect(&shellHostUI.shell().stack().input_top()).to_equal(&menu_scene());
		deluge::gui::ui::refreshShellSamplePreviewSounds();
		expect(deluge::gui::ui::samplePreviewUIOpen()).to_be_false();
	});

	it("claims an auto-loaded preview keeping the browser open and the menu where it was", _ {
		// The auto-load of main's `SampleBrowser::previewIfPossible`
		// (`origin/main:src/deluge/gui/ui/browser/sample_browser.cpp`): `ClaimOrigin::AutoLoad`, and no completion to
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
		deluge::gui::ui::pollShellBrowserHost();
		expect(spy.dispatches).to_equal(1);
		expect(ClaimSpy::last.path).to_equal(std::string{kChosenPath});
		expect(ClaimSpy::last.origin).to_equal(deluge::gui::ClaimOrigin::AutoLoad);
		expect(ClaimSpy::last.completion).to_be_null();
		r.step();
		r.step();
		expect(r.browser_on_stack()).to_be_true();
		expect(menu_scene().state().depth).to_equal(before.depth);
		expect(menu_scene().state().path[before.depth]).to_equal(before.path[before.depth]);
	});

	it("cancels an auto-load claim whenever the selection asks for another preview", _ {
		// `ShellBrowserHost::preview` (`gui/ui/browser_host.cpp`): a move supersedes the auto-load of the file
		// it left.
		CleanBrowser clean;
		ClaimSpy spy;
		CancelSpy cancel;
		ScratchCard card;
		ShellRig r;
		r.open_browser();
		expect(browser::browser_scene().preview_outstanding()).to_be_true();
		expect(CancelSpy::cancels).to_equal(1); // the file the browser opened on, as the browser asked for it
		deluge::gui::ui::pollShellBrowserHost(); // nothing asked
		expect(CancelSpy::cancels).to_equal(1);
		browser::browser_scene().on_exit(); // a stop
		expect(CancelSpy::cancels).to_equal(2);
	});

	it("starts legacy's horizontal scroll with the waveform in the image store, the main columns left to it", _ {
		// Main's `SampleBrowser::previewIfPossible` (`origin/main:src/deluge/gui/ui/browser/sample_browser.cpp`) with a
		// move: `PadLEDs::horizontal::setupScroll` from the image store, in `UI_MODE_HORIZONTAL_SCROLL`; the guest
		// sends no main column until `scrollFinished`.
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
		auto* preview = static_cast<MultisampleRange*>(AudioEngine::sampleForPreview->getOrCreateFirstRange(0));
		Sample* sample = make_sample();
		sample->addReason();
		preview->sampleHolder.audioFile = sample;
		browser::browser_scene().preview_settled(true);
		for (int32_t i = 0; i < 4 && currentUIMode != UI_MODE_HORIZONTAL_SCROLL; i++) {
			(void)shellHostUI.timerCallback();
		}
		expect(currentUIMode).to_equal(static_cast<uint32_t>(UI_MODE_HORIZONTAL_SCROLL));
		expect(shellHostUI.surfaces().main_columns_held()).to_be_true();
		const deluge::ui::gfx::grid::PadGrid& grid = shellHostUI.surfaces().grid().grid();
		int32_t differ = 0;
		int32_t lit = 0;
		for (int32_t y = 0; y < kDisplayHeight; y++) {
			for (int32_t x = 0; x < kDisplayWidth; x++) {
				const RGB c = grid.at(static_cast<uint8_t>(x), static_cast<uint8_t>(y));
				differ += PadLEDs::imageStore[y][x] == c ? 0 : 1;
				lit += c == RGB{} ? 0 : 1;
			}
		}
		expect(lit).to_be_greater_than(0);
		expect(differ).to_equal(0);
		shellHostUI.scrollFinished();
		expect(shellHostUI.surfaces().main_columns_held()).to_be_false();
		expect(currentUIMode).not_().to_equal(UI_MODE_HORIZONTAL_SCROLL);
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
		deluge::gui::ui::pollShellBrowserHost();
		expect(shellHostUI.surfaces().main_columns_held()).to_be_false();
		currentUIMode = UI_MODE_HORIZONTAL_SCROLL; // legacy's scroll still running keeps them
		shellHostUI.surfaces().hold_main_columns(true);
		deluge::gui::ui::pollShellBrowserHost();
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
		deluge::gui::ui::pollShellBrowserHost();
		expect(DeleteSpy::paths.size()).to_equal(size_t(1));
		expect(DeleteSpy::paths.empty() ? std::string{} : DeleteSpy::paths[0]).to_equal(std::string{kChosenPath});
		const std::string before{browser::browser_scene().entered_text()};
		deluge::gui::ui::shellFileDeleteDone(kChosenPath, deluge::gui::ui::FileDeleteOutcome::Failed);
		expect(std::string{browser::browser_scene().entered_text()}).to_equal(before);
		deluge::gui::ui::shellFileDeleteDone(kChosenPath, deluge::gui::ui::FileDeleteOutcome::Removed);
		expect(std::string{browser::browser_scene().entered_text()}).not_().to_equal(before);
		for (const FileItem& item : deluge::storage::FileCatalogue::fileItems) {
			expect(std::string{"SAMPLES/OSC_A/"} + item.filename).not_().to_equal(std::string{kChosenPath});
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
		expect(shellHostUI.buttonAction(deluge::hid::button::RECORD, false)).to_equal(ActionResult::DEALT_WITH);
		shellHostUI.shell().stack().pop();
		expect(shellHostUI.buttonAction(deluge::hid::button::RECORD, false))
		    .to_equal(ActionResult::NOT_DEALT_WITH); // the menu beneath leaves it to the global RECORD
	});

	it("claims RECORD's press, and only its press, from the global RECORD while the recorder is frontmost", _ {
		// `AudioRecorder::buttonAction` ends the take on the press and answers `DEALT_WITH`, so `Buttons`' RECORD tail
		// stamps nothing; the release is `NOT_DEALT_WITH`, and the tail judges it by its last stamp, which toggles no
		// recording.
		CleanBrowser clean;
		ClaimSpy spy;
		ScratchCard card;
		spec::UiStackGuard ui_guard;
		spec::GlobalGuestGuard guest_guard;
		SongRig model;
		select_language();
		(void)menu::assign_node_ids(nodes::kAllRoots);
		menu_scene().open(nodes::sample0_recorder, model.ctx());
		shellHostUI.setInitialScene(SceneId{SceneKind::FirstApp, menu::node_id(nodes::sample0_recorder)});
		setRootUILowLevel(&instrumentClipView);
		currentUIMode = UI_MODE_NONE;
		expect(openUI(&shellHostUI)).to_be_true();
		for (int32_t i = 0; i < 4; i++) {
			(void)shellHostUI.timerCallback();
		}
		expect(deluge::ui::scenes::recorder::recorder_scene().recording()).to_be_true();
		expect(shellHostUI.buttonAction(deluge::hid::button::RECORD, true)).to_equal(ActionResult::DEALT_WITH);
		expect(shellHostUI.buttonAction(deluge::hid::button::RECORD, false)).to_equal(ActionResult::NOT_DEALT_WITH);
		// The press ended the take; tear the stack down so the recorder is ended rather than left armed, and settle it.
		while (shellHostUI.shell().stack().depth() > 0) {
			shellHostUI.shell().stack().pop(SceneKind::Null);
		}
		expect(deluge::ui::scenes::recorder::recorder_scene().recording()).to_be_false();
		spec::settle_torn_down_take();
		expect(AudioEngine::foregroundRecorder()).to_be_null();
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
		expect(&shellHostUI.shell().stack().input_top()).to_equal(&browser::browser_scene());
		const int32_t before = model.song.clip->yScroll;
		(void)shellHostUI.verticalEncoderAction(1);
		expect(model.song.clip->yScroll).not_().to_equal(before);
		const int32_t moved = model.song.clip->yScroll;
		// With X_ENC held the clip view would transpose the notes on screen; the browser keeps the turn.
		NoteRow* row = model.song.clip->createNewNoteRowForYVisual(moved + 2, model.song.song);
		expect(row).not_().to_be_null();
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
		expect(still).not_().to_be_null();
		if (still != nullptr) {
			expect(still->hasNoNotes()).to_be_false();
		}
	});
});
// clang-format on

describe browser_choice_long_press(
    "the browser host performing the sample browser's folder menus", $ {
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
			        deluge::gui::ui::pollShellBrowserHost();
			        expect(spy.dispatches).to_equal(1);
			        expect(ClaimSpy::last.path).to_equal(std::string{kChosenPath});
			        expect(ClaimSpy::last.enteredText).to_equal(std::string{"A1"});
			        expect(ClaimSpy::last.mayDoPitchDetection).to_equal(w.pitch);
			        expect(ClaimSpy::last.mayDoSingleCycle).to_equal(w.single_cycle);
			        expect(ClaimSpy::last.mayDoWaveTable).to_equal(w.wavetable);
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
		        deluge::gui::ui::pollShellBrowserHost();
		        expect(spy.dispatches).to_equal(1);
		        expect(static_cast<int>(ImportSpy::last.kind))
		            .to_equal(static_cast<int>(deluge::gui::FolderImportKind::Multisamples));
		        expect(ImportSpy::last.folder).to_equal(std::string{"SAMPLES/OSC_A"});
		        expect(ImportSpy::last.previouslyViewedFilename).to_equal(std::string{"A1.WAV"});
	        });

	    it(
	        "names the menu's source in a MULTISAMPLES import, and hands the ranges it makes current to the menu", _ {
		        CleanBrowser clean;
		        ImportSpy spy;
		        ScratchCard card;
		        ShellRig r;
		        r.open_browser();
		        long_press(r.shell, r.clock);
		        pick(r.shell, r.clock, 0);
		        deluge::gui::ui::pollShellBrowserHost();
		        expect(spy.dispatches).to_equal(1);
		        expect(ImportSpy::last.target.source).to_equal(r.model.source);
		        expect(ImportSpy::last.rangeChosen).not_().to_be_null();
	        });

	    it(
	        "lands the picker the import's browser closes onto on the middle range the import left current", _ {
		        CleanBrowser clean;
		        ImportSpy spy;
		        ScratchCard card;
		        ShellRig r;
		        give_three_ranges(r.model);
		        import_from_picker(r);
		        expect(spy.dispatches).to_equal(1);
		        expect(menu_scene().state().path[0]).to_equal(&nodes::multi_range);
		        expect(static_cast<int32_t>(menu_scene().state().depth)).to_equal(1);
		        expect(static_cast<int32_t>(menu_scene().menu_context().range_index)).to_equal(0);

		        // `FolderImport::finishMulti`: range 0 current while it builds, then `numSamples >> 1`.
		        expect(ImportSpy::last.rangeChosen).not_().to_be_null();
		        if (ImportSpy::last.rangeChosen != nullptr) {
			        ImportSpy::last.rangeChosen(0);
			        ImportSpy::last.rangeChosen(1);
		        }
		        ImportSpy::last.completion(true);
		        for (int32_t i = 0; i < 4; i++) {
			        r.step();
		        }

		        expect(r.browser_on_stack()).to_be_false();
		        expect(menu_scene().focused()).to_equal(&nodes::multi_range);
		        expect(static_cast<int32_t>(menu_scene().menu_context().range_index)).to_equal(1);
		        expect(static_cast<int32_t>(menu_scene().state().list[0].cursor)).to_equal(1);
	        });

	    it(
	        "drops the menu's range when the import's browser closes onto a page, as Submenu::beginSession drops "
	        "main's",
	        _ {
		        CleanBrowser clean;
		        ImportSpy spy;
		        ScratchCard card;
		        ShellRig r;
		        r.open_menu_at(nodes::sound_root);
		        menu_scene().enter_submenu(nodes::source_group);
		        menu_scene().enter_submenu(nodes::file0_selector);
		        // `enter_submenu` runs outside a tick and asks for no frame, so the item's push waits for the shell's
		        // next one: tick until the browser is up, then twice more for its listing.
		        for (int32_t i = 0; i < 40 && !r.browser_on_stack(); i++) {
			        r.step();
		        }
		        r.step();
		        r.step();
		        expect(r.browser_on_stack()).to_be_true();
		        long_press(r.shell, r.clock);
		        pick(r.shell, r.clock, 0); // MULTISAMPLES
		        deluge::gui::ui::pollShellBrowserHost();
		        expect(spy.dispatches).to_equal(1);

		        give_three_ranges(r.model); // what the import built
		        if (ImportSpy::last.rangeChosen != nullptr) {
			        ImportSpy::last.rangeChosen(0);
			        ImportSpy::last.rangeChosen(1);
		        }
		        ImportSpy::last.completion(true);
		        for (int32_t i = 0; i < 4; i++) {
			        r.step();
		        }

		        expect(r.browser_on_stack()).to_be_false();
		        expect(static_cast<int32_t>(menu_scene().state().depth)).to_equal(1);
		        expect(menu_scene().state().path[1]).to_equal(&nodes::source_group);
		        expect(static_cast<int32_t>(menu_scene().menu_context().range_index)).to_equal(-1);
		        // The transpose column's button then opens the picker first, as main's does.
		        expect(menu::permission_for(nodes::source0_transpose, menu_scene().menu_context()))
		            .to_equal(menu::Permission::MustSelectRange);
	        });

	    it(
	        "drops a range the picker chose when the walk enters a submenu", _ {
		        CleanBrowser clean;
		        ScratchCard card;
		        ShellRig r;
		        give_three_ranges(r.model);
		        MenuContext ctx = r.model.ctx();
		        ctx.range_index = 1;
		        r.open_menu_at(nodes::sound_root, ctx);
		        expect(static_cast<int32_t>(menu_scene().menu_context().range_index)).to_equal(1);
		        menu_scene().enter_submenu(nodes::source_group);
		        expect(static_cast<int32_t>(menu_scene().menu_context().range_index)).to_equal(-1);
	        });

	    it(
	        "spends the import's range on the resume it lands: the picker's next trip to the browser keeps its own", _ {
		        CleanBrowser clean;
		        ImportSpy spy;
		        ScratchCard card;
		        ShellRig r;
		        give_three_ranges(r.model);
		        import_from_picker(r);
		        if (ImportSpy::last.rangeChosen != nullptr) {
			        ImportSpy::last.rangeChosen(0);
			        ImportSpy::last.rangeChosen(1);
		        }
		        ImportSpy::last.completion(true);
		        for (int32_t i = 0; i < 4; i++) {
			        r.step();
		        }

		        turn_detents(r.shell, r.clock, -2);
		        pick(r.shell, r.clock, 0); // the browser again, from the bottom range
		        for (int32_t i = 0; i < 4; i++) {
			        r.step();
		        }
		        expect(r.browser_on_stack()).to_be_true();
		        leave_browser(r);

		        expect(r.browser_on_stack()).to_be_false();
		        expect(menu_scene().focused()).to_equal(&nodes::multi_range);
		        expect(static_cast<int32_t>(menu_scene().menu_context().range_index)).to_equal(0);
		        expect(static_cast<int32_t>(menu_scene().state().list[0].cursor)).to_equal(0);
	        });

	    it(
	        "applies no range an earlier visit was told, when a failed import's browser is left with BACK", _ {
		        CleanBrowser clean;
		        ImportSpy spy;
		        ScratchCard card;
		        ShellRig r;
		        give_three_ranges(r.model);
		        menu_scene().import_chose_range(2); // told to a visit that closed before its browser returned
		        import_from_picker(r);
		        ImportSpy::last.completion(false); // the listing failed: no range was made current
		        for (int32_t i = 0; i < 3; i++) {
			        r.step();
		        }
		        expect(r.browser_on_stack()).to_be_true();
		        leave_browser(r);

		        expect(r.browser_on_stack()).to_be_false();
		        expect(menu_scene().focused()).to_equal(&nodes::multi_range);
		        expect(static_cast<int32_t>(menu_scene().menu_context().range_index)).to_equal(0);
	        });

	    it(
	        "asks for a range when SELECT lands on a focused file column of a multisample, as main's select arm does",
	        _ {
		        CleanBrowser clean;
		        ScratchCard card;
		        ShellRig r;
		        give_three_ranges(r.model);
		        MenuContext ctx = r.model.ctx();
		        ctx.range_index = -1; // several ranges, none chosen
		        // Shift + (2,2), then KIT: the file column takes the focus with no picker
		        // (`handleInstrumentButtonPress`'s `allowToBeginSessionFromHorizontalMenu` suppression).
		        open_page_at(r, nodes::source_group, nodes::osc0_type, ctx);
		        press_button(r.shell, r.clock, kKitButtonX, kKitButtonY, /*shift=*/false);
		        r.step();
		        expect(r.browser_on_stack()).to_be_false();
		        expect(menu_scene().focused()).to_equal(&nodes::source_group);
		        expect(static_cast<int32_t>(menu_scene().state().columns[0].focused))
		            .to_equal(visible_index_of(ctx, nodes::source_group, nodes::file0_selector));

		        pick(r.shell, r.clock, 0); // SELECT
		        for (int32_t i = 0; i < 4; i++) {
			        r.step();
		        }

		        expect(r.browser_on_stack()).to_be_false();
		        expect(menu_scene().focused()).to_equal(&nodes::multi_range);
		        expect(menu_scene().state().walk.pending_after_range).to_equal(&nodes::file0_selector);
	        });

	    it(
	        "opens the browser when SELECT lands on a focused file column of a single sample", _ {
		        CleanBrowser clean;
		        ScratchCard card;
		        ShellRig r;
		        open_page_at(r, nodes::source_group, nodes::osc0_type, r.model.ctx());
		        press_button(r.shell, r.clock, kKitButtonX, kKitButtonY, /*shift=*/false);
		        r.step();
		        expect(r.browser_on_stack()).to_be_false();
		        expect(static_cast<int32_t>(menu_scene().state().columns[0].focused))
		            .to_equal(visible_index_of(r.model.ctx(), nodes::source_group, nodes::file0_selector));

		        pick(r.shell, r.clock, 0); // SELECT
		        for (int32_t i = 0; i < 4; i++) {
			        r.step();
		        }

		        expect(r.browser_on_stack()).to_be_true();
		        expect(menu_scene().state().path[1]).to_equal(&nodes::file0_selector);
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
		        deluge::gui::ui::pollShellBrowserHost();
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
			        deluge::gui::ui::pollShellBrowserHost();
			        expect(ImportSpy::last.completion).not_().to_be_null();
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

// clang-format off
describe browser_choice_submenu_range("menu::begin_submenu_range, Submenu::beginSession's drop of the range", $ {
	it("keeps it on a page whose focused column is range-dependent", _ {
		SongRig model;
		give_three_ranges(model);
		MenuContext ctx = model.ctx();
		ctx.range_index = 1;
		const menu::MenuState st =
		    state_on(nodes::source_group, visible_index_of(ctx, nodes::source_group, nodes::source0_transpose));
		menu::begin_submenu_range(ctx, st, nullptr);
		expect(static_cast<int32_t>(ctx.range_index)).to_equal(1);
	});

	it("keeps it on a page come back to from a range-dependent item", _ {
		SongRig model;
		give_three_ranges(model);
		MenuContext ctx = model.ctx();
		ctx.range_index = 1;
		menu::begin_submenu_range(ctx, state_on(nodes::source_group, 0), &nodes::source0_transpose);
		expect(static_cast<int32_t>(ctx.range_index)).to_equal(1);
	});

	it("keeps it on a page come back to from the picker heading to a range-dependent item", _ {
		SongRig model;
		give_three_ranges(model);
		MenuContext ctx = model.ctx();
		ctx.range_index = 1;
		menu::MenuState st = state_on(nodes::source_group, 0);
		st.walk.pending_after_range = &nodes::source0_transpose;
		menu::begin_submenu_range(ctx, st, &nodes::multi_range);
		expect(static_cast<int32_t>(ctx.range_index)).to_equal(1);
	});

	it("drops it on a page come back to from the picker heading to the file selector", _ {
		SongRig model;
		give_three_ranges(model);
		MenuContext ctx = model.ctx();
		ctx.range_index = 1;
		menu::MenuState st = state_on(nodes::source_group, 0);
		st.walk.pending_after_range = &nodes::file0_selector;
		menu::begin_submenu_range(ctx, st, &nodes::multi_range);
		expect(static_cast<int32_t>(ctx.range_index)).to_equal(-1);
	});

	it("drops it on a page come back to from the file selector, which main does not count as range-dependent", _ {
		SongRig model;
		give_three_ranges(model);
		MenuContext ctx = model.ctx();
		ctx.range_index = 1;
		menu::begin_submenu_range(ctx, state_on(nodes::source_group, 0), &nodes::file0_selector);
		expect(static_cast<int32_t>(ctx.range_index)).to_equal(-1);
	});

	it("drops it on a page come back to from the other file selector", _ {
		SongRig model;
		give_three_ranges(model);
		MenuContext ctx = model.ctx();
		ctx.range_index = 1;
		menu::begin_submenu_range(ctx, state_on(nodes::source_group, 0), &nodes::file1_selector);
		expect(static_cast<int32_t>(ctx.range_index)).to_equal(-1);
	});

	it("drops it on a page come back to from a recorder, which main does not count as range-dependent", _ {
		SongRig model;
		give_three_ranges(model);
		MenuContext ctx = model.ctx();
		ctx.range_index = 1;
		menu::begin_submenu_range(ctx, state_on(nodes::source_group, 0), &nodes::sample0_recorder);
		expect(static_cast<int32_t>(ctx.range_index)).to_equal(-1);
		ctx.range_index = 1;
		menu::begin_submenu_range(ctx, state_on(nodes::source_group, 0), &nodes::sample1_recorder);
		expect(static_cast<int32_t>(ctx.range_index)).to_equal(-1);
	});

	it("tests the item a page is entered for, not the column it remembers, as focusChild runs before enterSubmenu",
	   _ {
		   SongRig model;
		   give_three_ranges(model);
		   MenuContext ctx = model.ctx();
		   ctx.range_index = 1;
		   menu::MenuWalk walk;
		   menu::ValueSession session;
		   // The page remembers transpose: leaving it from there records the column.
		   menu::MenuState st = state_on(nodes::source_group,
		                                 visible_index_of(ctx, nodes::source_group, nodes::source0_transpose));
		   walk.ascend(menu::MenuCore{ctx, st}, session);
		   expect(static_cast<int32_t>(ctx.range_index)).to_equal(1);

		   // `enter_child`'s shortcut arm enters the page for the oscillator type, which is not range-dependent.
		   st = state_on(nodes::sound_root, 0);
		   walk.descend_to(menu::MenuCore{ctx, st}, session, nodes::source_group, &nodes::osc0_type);
		   expect(menu::selected_child(ctx, st, nodes::source_group)).to_equal(&nodes::osc0_type);
		   expect(static_cast<int32_t>(ctx.range_index)).to_equal(-1);
	   });

	it("tests the level directly above the landing when the step up skips a level it may no longer enter", _ {
		SongRig model;
		give_three_ranges(model);
		MenuContext ctx = model.ctx();
		ctx.range_index = 1;
		menu::MenuWalk walk;
		menu::ValueSession session;
		// The FM modulator page is not relevant on this subtractive sound, so the step up from transpose skips it and
		// lands on the source page, as `goUpOneLevel`'s permission loop does; main's `oldItem` is the modulator page.
		menu::MenuState st = state_on(nodes::source_group, 0);
		st.path[1] = &nodes::modulator0;
		st.path[2] = &nodes::source0_transpose;
		st.depth = 2;
		expect(menu::permission_for(nodes::modulator0, ctx)).to_equal(menu::Permission::No);
		walk.ascend(menu::MenuCore{ctx, st}, session);
		expect(static_cast<int32_t>(st.depth)).to_equal(0);
		expect(static_cast<int32_t>(ctx.range_index)).to_equal(-1);
	});

	it("drops it on a list whatever it came back from", _ {
		SongRig model;
		give_three_ranges(model);
		MenuContext ctx = model.ctx();
		ctx.range_index = 1;
		menu::begin_submenu_range(ctx, state_on(nodes::sound_root, 0), &nodes::source0_transpose);
		expect(static_cast<int32_t>(ctx.range_index)).to_equal(-1);
	});

	it("keeps a single range, which main's range check selects again before anything reads it", _ {
		SongRig model;
		MenuContext ctx = model.ctx(); // one range, range_index 0
		menu::begin_submenu_range(ctx, state_on(nodes::source_group, 0), nullptr);
		expect(static_cast<int32_t>(ctx.range_index)).to_equal(0);
	});

	it("drops it when SHIFT + CROSS SCREEN switches to the next chained page, as switchHorizontalMenu begins it", _ {
		CleanBrowser clean;
		ScratchCard card;
		ShellRig r;
		give_three_ranges(r.model);
		MenuContext ctx = r.model.ctx();
		ctx.range_index = 1; // picked through the transpose column's picker
		open_page_at(r, nodes::source_group, nodes::source0_transpose, ctx);
		expect(menu_scene().state().path[0]).to_equal(&nodes::source_group);
		expect(static_cast<int32_t>(menu_scene().menu_context().range_index)).to_equal(1);

		press_button(r.shell, r.clock, kCrossScreenButtonX, kCrossScreenButtonY, /*shift=*/true);
		r.step();

		expect(menu_scene().state().path[0]).not_().to_equal(&nodes::source_group);
		expect(static_cast<int32_t>(menu_scene().menu_context().range_index)).to_equal(-1);
	});

	it("keeps the picked range when the picker's SELECT enters its item on the item's page, as main's select arm does", _ {
		CleanBrowser clean;
		ScratchCard card;
		ShellRig r;
		give_three_ranges(r.model);
		MenuContext ctx = r.model.ctx();
		ctx.range_index = -1; // several ranges and none chosen: the shortcut opens the picker heading to transpose
		r.open_menu_at(nodes::source0_transpose, ctx);
		expect(menu_scene().focused()).to_equal(&nodes::multi_range);
		turn_detents(r.shell, r.clock, 3);
		pick(r.shell, r.clock, 0);
		r.step();

		// `maybeGetParentMenu` -> `focusChild` -> `enterSubmenu(parent, newItem)`: the page begins having come back
		// from transpose, so `HorizontalMenu::beginSession` keeps the range the picker chose.
		expect(menu_scene().focused()).to_equal(&nodes::source_group);
		expect(menu::selected_child(menu_scene().menu_context(), menu_scene().state(), nodes::source_group))
		    .to_equal(&nodes::source0_transpose);
		expect(static_cast<int32_t>(menu_scene().menu_context().range_index)).to_equal(2);
	});
});
// clang-format on

DELUGE_SPEC(browser_choice_spec, browser_choice_target, browser_choice_host, browser_choice_claim,
            browser_choice_reroot, browser_choice_long_press, browser_choice_preview, browser_choice_submenu_range)
