/// @file
/// @brief The **choice seam** (`scenes/browser/browser_scene.h`'s `Descriptor::accept` / `Defer`)
///        and its consumer, driven through a real `Shell` and a real `ShellHostUI` over the real
///        scene registry.
///
/// This is `menu_actions_spec.cpp`'s `menu_action_dispatch` group for the noun half of the same
/// wall, and what is covered here matters more than usual because F3 splits one legacy statement in
/// two:
///
///   * **Covered:** that a real file press through the ported browser parks exactly one
///     `BrowserChoice`; that `gui::ui::dispatchShellChoices` is what takes it out of the outbox;
///     that the poll is reached **by the guest's own `timerCallback`**, on a tick where the browser
///     has already closed itself; that polling an empty outbox neither reports nor eats anything;
///     and that `sampleClaimTargetFor` splits a chosen path exactly the way
///     `SampleBrowser::getCurrentFilePath()` builds one.
///   * **Covered since F3 task 2:** the claim -- that the drain *points the legacy browser* at the
///     chosen directory and filename and then hands `deluge::gui::ui::runShellSampleClaimOp` to the
///     storage owner, which together are `SampleBrowser::enterKeyPress`'s file arm
///     (`gui/ui/browser/sample_browser.cpp:392`) with the one argument the guest has to change. The
///     effect is asserted on `Browser`'s own state, not on the dispatcher having been entered.
///   * **NOT run, ever, in this file:** `SampleBrowser::claimCurrentFile` itself. On this BSP
///     `Owner::run_or_inline` is inline on the caller's stack (`storage/owner.h:28-30`), so a real
///     claim would mean a card read, an `AudioFileManager` allocation and an `openUI(&soundEditor)`
///     inside an assertion. `ClaimSpy` installs a recording dispatcher through
///     `deluge::gui::ui::setSampleClaimDispatch`, which is why the examples can press a real file at
///     all -- see that function's own note.
///
/// @par Why the guest examples open the browser from the menu rather than as the initial scene
///
/// Because that is the only way it is ever open. `FileBrowserItem::on_focus`
/// (`scenes/menu/nodes/sample.cpp:77-102`) opens the scene and files `MenuState::pending_scene`
/// (`:100-101`), so the browser is always **above** a `MenuScene` on the shell stack. Pushed alone,
/// the stack would empty the moment `Defer` closed it, and `ShellHostUI::timerCallback` would take
/// its `closeGuest()` arm (`gui/ui/shell_host_ui.cpp:553-556`) -- `closeUI` on a guest that is not
/// on the legacy UI stack, i.e. the index `-1` read `gui/ui/shell_action_dispatch.cpp:128-134`
/// describes. A rig that has to avoid a production path is a rig that is testing something else.
///
/// @note The spec may include `gui/` (for the dispatcher and the guest); the library may not. Same
///       rule `sample_items_spec.cpp` and `browser_scene_spec.cpp` run under.

#include "scenes/browser/browser_scene.h"
#include "scenes/menu/context.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/menu/node.h"
#include "scenes/menu/node_id.h"
#include "scenes/menu/nodes/nodes.h"
#include "scenes/menu/nodes/sample.h"
#include "scenes/profile.h"
#include "scenes/registry.h"

#include "shell/profiles/deluge.h"
#include "shell/shell.h"
#include "ui/grid/keyboard.h"

#include "gui/l10n/l10n.h"
#include "gui/ui/browser/browser.h"
#include "gui/ui/browser/sample_browser.h"
#include "gui/ui/shell_choice_dispatch.h"
#include "gui/ui/shell_host_ui.h"
#include "hid/display/display.h"
#include "model/instrument/midi_instrument.h"
#include "model/sample/sample.h"
#include "model/song/song.h"
#include "processing/sound/sound.h"
#include "processing/source.h"
#include "storage/cluster/cluster.h"
#include "storage/file_item.h"
#include "storage/multi_range/multisample_range.h"
#include "storage/owner.h"

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

/// @brief A scratch card with one sample under one folder -- everything these examples need to
///        press a real file.
///
/// Under the host temp directory and removed again, `browser_scene_spec.cpp`'s reason: the committed
/// `tests/fixtures/card` is flat and holds no samples at all (`SAMPLES/` is one `.keep`), and
/// nothing here may write to it.
struct ScratchCard {
	std::filesystem::path dir;
	spec::SdRoot root;

	static std::filesystem::path make() {
		std::filesystem::path d = std::filesystem::temp_directory_path() / "deluge_browser_choice_spec";
		std::filesystem::remove_all(d);
		std::filesystem::create_directories(d / "SAMPLES" / "OSC_A");
		std::ofstream(d / "SAMPLES" / "OSC_A" / "A1.WAV") << "RIFF";
		// ...and the one file the OTHER descriptor browses, for the control that pins it unaffected.
		// The shape `StorageManager::openMidiDeviceDefinitionFile` demands (root tag `midiDevice`,
		// `storage/storage_manager.cpp:371`) with the one tag
		// `MIDIInstrument::readDeviceDefinitionFile` accepts, so the load answers `Error::NONE`
		// (`model/instrument/midi_instrument.cpp:606-607`). `browser_scene_spec.cpp`'s, unchanged.
		std::filesystem::create_directories(d / "MIDI_DEVICES" / "DEFINITION");
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

	CleanBrowser() { check("a browser singleton was still busy when an example started"); }
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
/// why. **This is not `BrowserScene::set_dispatch`** (`scenes/browser/browser_scene.h:373-381`),
/// which swaps the dispatcher on the scene's `Listing` -- the directory read, one layer down. There
/// is no claim seam on `BrowserScene`: the claim happens in the legacy-side dispatcher, so the
/// settable dispatcher is the dispatcher's own.
///
/// What it records is what `Owner::run_or_inline` would have been given (`storage/owner.h:68`): the
/// op and how many times. It answers `true`, which is that function's "it ran or was queued"
/// (`:67`) -- the caller discards it either way, as legacy's file arm does
/// (`gui/ui/browser/sample_browser.cpp:388-392`).
///
/// @warning **Recording the op is as far as this can go, and that is a real limit on what the
///          examples below prove.** Running it means running `SampleBrowser::claimCurrentFile` --
///          a card read and an `AudioFileManager` allocation -- which is the thing the seam exists to
///          keep out. So WHICH op is dispatched, and WHAT arguments that op will use
///          (`kShellSampleClaimArgs`), are pinned; the `closeUI` the wrong op would reach is not
///          reachable from a spec at all, and is argued at `runShellSampleClaimOp` instead.
struct ClaimSpy {
	static inline int32_t dispatches = 0;
	static inline void (*last_op)(void*) = nullptr;

	static bool record(void (*fn)(void*), void* ctx) {
		(void)ctx;
		dispatches++;
		last_op = fn;
		return true;
	}

	ClaimSpy() {
		dispatches = 0;
		last_op = nullptr;
		deluge::gui::ui::setSampleClaimDispatch(&record);
	}
	/// Put the shipped dispatcher back, so a later example that forgot a spy fails loudly rather
	/// than silently recording into a dead struct.
	~ClaimSpy() { deluge::gui::ui::setSampleClaimDispatch(&deluge::storage::Owner::run_or_inline); }
	ClaimSpy(const ClaimSpy&) = delete;
	ClaimSpy& operator=(const ClaimSpy&) = delete;
};

/// @brief `Browser::fileItems` as one comma-joined string, so a mismatch prints as a listing.
///
/// The claim's own witness: `BrowserScene` reads and writes the same shared `FileCatalogue`
/// (`Browser::fileItems` is a reference onto it, `gui/ui/browser/browser.h:106-107`), but
/// `BrowserScene::on_exit` empties it on the way out (`scenes/browser/browser_scene.cpp:401`). So
/// after a `Defer` closes the scene this is empty, and anything in it afterwards was put there by
/// `SampleBrowser::selectFileForClaim`.
std::string catalogue_order() {
	std::string out;
	for (const FileItem& item : Browser::fileItems) {
		if (!out.empty()) {
			out += ",";
		}
		out += item.filename;
	}
	return out;
}

/// @brief The filename `claimCurrentFile()` would read, or `"-"` when nothing is selected.
///
/// Guarded rather than dereferenced: `Browser::getCurrentFileItem()` indexes `fileItems` with
/// `fileIndexSelected` (`browser.cpp:1138-1143`), so asking it while the catalogue is empty is only
/// safe once something has selected an entry in it.
std::string selected_filename() {
	if (Browser::fileItems.empty()) {
		return "-";
	}
	const FileItem* item = Browser::getCurrentFileItem();
	return item != nullptr ? item->filename : std::string{"-"};
}

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
		range->sampleHolder.startPos = 0;
		range->sampleHolder.endPos = sample->lengthInSamples;
	}

	/// @brief The context `SoundEditor::setup` would have resolved for source 0.
	MenuContext ctx() {
		MenuContext c = menu::context_for(*song.song, song.clip, 0, false, false, false, /*kit_global_fx=*/false);
		c.source = source;
		c.range = range;
		c.sample_controls = &source->sampleControls;
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
	/// singleton -- E3 task 10's rule, and `sample_items_spec.cpp`'s `~SoundRig`.
	~ShellRig() {
		while (shell.stack().depth() > 0) {
			shell.stack().pop(SceneKind::Null);
		}
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
///       guest's shell rather than into this one (`gui/ui/shell_guest_services.cpp:66`, `:173`).
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
	///        `padAction` only queues (`gui/ui/shell_host_ui.cpp:747-775`), and a tick is what routes.
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

/// @brief A `Shell` over a MIDI song, on the browser that does **not** defer.
///
/// `BrowserId::MidiDeviceDefinition`'s accept hook really loads the file --
/// `StorageManager::loadMidiDeviceDefinitionFile` is in `storage/`, which the scenes layer may
/// include (`scenes/browser/browser_scene.h:203-209`) -- so it answers `Close` and files nothing.
/// This rig exists to pin that the claim wiring leaves it exactly as it was.
///
/// It pushes the browser's `SceneId` directly, the way `browser_scene_spec.cpp`'s own rig does,
/// rather than re-deriving `nodes::MidiDeviceLinkedItem` (`scenes/menu/nodes/output_items.h`): what
/// is under test here is what the dispatcher does with a descriptor's answer, not how that item's
/// toggle opens the scene. And it is a bare `Shell`, not the guest, so the empty stack that `Close`
/// leaves behind reaches no `closeGuest()` arm (`gui/ui/shell_host_ui.cpp:553-556`).
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
	}

	MIDIInstrument* instrument() { return static_cast<MIDIInstrument*>(song.instrument); }

	void open_browser() {
		select_language();
		instrument()->deviceDefinitionFileName.clear();
		browser::browser_scene().open(
		    menu::context_for(*song.song, song.clip, 0, false, false, false, /*kit_global_fx=*/false));
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
	int32_t index = 0;
	for (const Node* kid : page.children) {
		if (kid == nullptr || (kid->relevant != nullptr && !kid->relevant(ctx))) {
			continue;
		}
		if (kid == &item) {
			return index;
		}
		index++;
	}
	return -1;
}

std::string split_of(std::string_view path) {
	const std::optional<SampleClaimTarget> t = deluge::gui::ui::sampleClaimTargetFor(path);
	if (!t.has_value()) {
		return "-";
	}
	return std::string{t->dir} + "|" + std::string{t->name};
}

} // namespace

describe browser_choice_target(
    "gui::ui::sampleClaimTargetFor", $ {
	    // The choosing half, driven by name. It is declared in `shell_choice_dispatch.h` rather than left
	    // in the `.cpp`'s anonymous namespace for the reason `stemExportTypeForRootUI` is
	    // (`gui/ui/shell_action_dispatch.h`): the acting half cannot be run in a spec, and this half must
	    // be. Nothing in this group touches a scene, a card or a song.

	    it(
	        "splits a chosen path the way getCurrentFilePath builds one", _ {
		        // `SampleBrowser::getCurrentFilePath` (`gui/ui/browser/sample_browser.cpp:539-550`) is
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
		        // Neither of these can come out of `BrowserScene::enter`, which always appends a `FileItem`'s
		        // own filename (`scenes/browser/browser_scene.cpp:879-883`) -- they are the defence-in-depth
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
			        // `Defer` parks AND closes in one statement (`scenes/browser/browser_scene.cpp:895-897`),
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
			        // own `closeGuest()` arm (`gui/ui/shell_host_ui.cpp:553-556`) is not what happens here.
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
	    // What the drain *does*, asserted on `Browser`'s own state rather than on the dispatcher
	    // having been entered. `SampleBrowser::enterKeyPress`'s file arm is two things once the
	    // browser it reads is gone: point the browser (`selectFileForClaim`) and dispatch the claim
	    // onto the storage owner (`gui/ui/browser/sample_browser.cpp:392`). Both are visible from
	    // outside -- `Browser::currentDir`, `Browser::fileItems` and `Browser::getCurrentFileItem()`
	    // are public statics (`gui/ui/browser/browser.h:63`, `:106-107`), and `ClaimSpy` records the
	    // dispatch.

	    before_each([] {
		    static QuietDisplay d;
		    if (display == nullptr) {
			    display = &d;
		    }
	    });

	    it(
	        "points the legacy browser at the chosen file and hands the guest's claim op to the owner", _ {
		        CleanBrowser clean;
		        ClaimSpy spy;
		        ScratchCard card;
		        ShellRig r;
		        r.open_browser();
		        r.enter();

		        // The catalogue is EMPTY here, and that is what makes the assertions below witnesses
		        // rather than residue: the scene and the legacy browser share one `FileCatalogue`, and
		        // `BrowserScene::on_exit` empties it as it closes
		        // (`scenes/browser/browser_scene.cpp:401`). So the listing that landed the selection
		        // cannot be what puts an entry back.
		        expect(Browser::fileItems.empty()).to_be_true();
		        expect(spy.dispatches).to_equal(0);

		        // And the two pieces of state the claim reads are pointed somewhere else entirely --
		        // which under the guest they genuinely are, since the legacy `sampleBrowser` was never
		        // opened and whatever wrote them last was some other browser.
		        Browser::currentDir = "SOMEWHERE/ELSE";
		        QwertyUI::enteredText = "STALE";

		        deluge::gui::ui::dispatchShellChoices();

		        // `getCurrentFilePath()` (`sample_browser.cpp:539-550`) read forwards again: the
		        // directory, and the selected item's filename WITH its extension.
		        expect(std::string{Browser::currentDir}).to_equal(std::string{"SAMPLES/OSC_A"});
		        expect(catalogue_order()).to_equal(std::string{"A1.WAV"});
		        expect(selected_filename()).to_equal(std::string{"A1.WAV"});
		        expect(Browser::fileItems.empty() ? true : Browser::fileItems[0].isFolder).to_be_false();
		        // `setEnteredTextFromCurrentFilename` (`browser.cpp:1316-1330`), which browsing would
		        // have run: the name without its extension. `claimAudioFileForInstrument` names a new
		        // Drum from it (`sample_browser.cpp:1034`, `:1040`).
		        expect(std::string{QwertyUI::enteredText}).to_equal(std::string{"A1"});

		        // Exactly one dispatch, and it is the guest's own op rather than
		        // `SampleBrowser::runClaimCurrentFileOp`. That is not bookkeeping: legacy's op claims
		        // with default arguments, and `loadWithoutExiting = false` ends in
		        // `exitAndNeverDeleteDrum()` -> `closeUI(&sampleBrowser)` on a browser the guest never
		        // opened, i.e. an index `-1` read of `uiNavigationHierarchy` and `numUIsOpen = 0` from
		        // inside the running `timerCallback` (`gui/ui/ui.cpp:183-197`). See the example below.
		        expect(spy.dispatches).to_equal(1);
		        expect(ClaimSpy::last_op == &deluge::gui::ui::runShellSampleClaimOp).to_be_true();
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
			        expect(Browser::fileItems.empty()).to_be_true();
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
		        expect(ClaimSpy::last_op == &deluge::gui::ui::runShellSampleClaimOp).to_be_true();
		        expect(std::string{Browser::currentDir}).to_equal(std::string{"SAMPLES/OSC_A"});
		        expect(selected_filename()).to_equal(std::string{"A1.WAV"});
		        // The outbox is empty because the guest's own tick emptied it. Asserted here as well as
		        // in the group above so a mutant that deletes the call site fails this example by
		        // comparison, rather than by `CleanBrowser`'s abort -- which stops the file on its first
		        // casualty and prints one reason for all of them.
		        expect(browser::browser_scene().take_choice().has_value()).to_be_false();
	        });

	    it(
	        "leaves the descriptor that answers Close alone: it loads its own file and claims nothing", _ {
		        // `BrowserId::MidiDeviceDefinition` does not defer -- its accept hook really loads the
		        // file, because `StorageManager::loadMidiDeviceDefinitionFile` is in `storage/`, which
		        // the scenes layer may include (`scenes/browser/browser_scene.h:203-209`). So the drain
		        // must see nothing at all for it, and the `Sample` arm's new body must not be reachable
		        // from it.
		        CleanBrowser clean;
		        ClaimSpy spy;
		        ScratchCard card;
		        MidiRig r;
		        r.open_browser();
		        expect(r.browser_on_stack()).to_be_true();

		        r.enter();
		        expect(r.browser_on_stack()).to_be_false();
		        // The proof the accept hook really ran and answered `Close`: the instrument's own
		        // remembered filename, which `loadMidiDeviceDefinitionFile` writes.
		        expect(r.instrument()->deviceDefinitionFileName)
		            .to_equal(std::string{"MIDI_DEVICES/DEFINITION/DEVICE1.XML"});
		        // Nothing parked...
		        expect(browser::browser_scene().take_choice().has_value()).to_be_false();

		        // ...so the drain has nothing to take, and nothing is claimed. Polled the same way a
		        // quiet guest tick polls, which is unconditionally.
		        deluge::gui::ui::dispatchShellChoices();
		        expect(spy.dispatches).to_equal(0);
	        });

	    it(
	        "claims without exiting, because there is no legacy browser open to close", _ {
		        // **The device defect this pins.** `claimCurrentFile`'s success tail is gated on
		        // `!loadWithoutExiting` (`gui/ui/browser/sample_browser.cpp:1108`), and its first
		        // statement is `exitAndNeverDeleteDrum()` (`:1109`) -- which is `close()` (`:258-260`),
		        // `Browser::close()` (`gui/ui/browser/browser.cpp:189-193`), `UI::close()` and finally
		        // `closeUI(this)` (`gui/ui/ui.cpp:57-59`). Under the guest `sampleBrowser` was never
		        // opened, so `closeUI`'s search falls out at `u == 0` and reads
		        // `uiNavigationHierarchy[u - 1]` (`gui/ui/ui.cpp:183-194`), then sets `numUIsOpen = 0`
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
		        // auto-load feature's `claimCurrentFile(1, 1, 1, true)` (`sample_browser.cpp:640`),
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
	    // parentMenuHeadingTo; navigationDepth = 0` (`gui/ui/browser/sample_browser.cpp:1116-1118`).
	    // The page is `sourceMenuGroup` focused on `source0Menu` (`gui/ui/menus.cpp:1748-1749`), which
	    // this port models as the ordinary submenu `nodes::source0`.
	    //
	    // @par Why the entry point is called by name here, and the claim op is not
	    //
	    // Because the op cannot be run: `runShellSampleClaimOp`'s success arm is behind a real
	    // `SampleBrowser::claimCurrentFile` -- a card read and an `AudioFileManager` allocation --
	    // which is the whole reason `ClaimSpy` exists. So the split is the one the group above
	    // already lives with: **which** op the drain hands the owner is pinned there, and what that
	    // op does on success is argued at `runShellSampleClaimOp` and driven by name here. The state
	    // these examples re-root is not a fixture, though: it is the real state a real file press
	    // leaves behind, all four ticks and the drain included.
	    //
	    // @note The menu here is at **depth 0** on the file selector, and that is not the rig taking a
	    //       shortcut -- it is the only shape a user can produce today. The port has no port of
	    //       `MenuItem::allowToBeginSessionFromHorizontalMenu` (`gui/menu_item/menu_item.h:314`,
	    //       overridden true by `FileSelector`, `gui/menu_item/file_selector.h:35`), so a select
	    //       press on the file selector's column of the horizontal `source0` page raises the
	    //       notification instead of opening the browser (`MenuScene::render_horizontal`'s
	    //       `column_press`, and `ui/oled/columns.cpp:419` behind it). The shortcut pad is what is
	    //       left, and it re-roots to depth 0 (`handle`'s `Replace` arm). Reported, not fixed here.

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

		        // `menuItemNavigationRecord[0] = parentMenuHeadingTo` (`:1117`) ...
		        expect(menu_scene().state().path[0] == &nodes::source0).to_be_true();
		        // ... and `navigationDepth = 0` (`:1118`). This is the assertion the "one extra BACK
		        // press" was: a re-root that left the page one level in would still satisfy the line
		        // above.
		        expect(menu_scene().state().depth).to_equal(0);
		        // ... and `parentMenuHeadingTo->focusChild(menuItemHeadingTo)` (`:1116`), which is a
		        // separate failure mode: the right page with the wrong column focused is exactly what
		        // legacy's `focusChild` exists to prevent.
		        const int32_t column = visible_index_of(r.model.ctx(), nodes::source0, nodes::file0_selector);
		        // Pinned as a number too, so "the focus was never moved" cannot pass by agreeing with a
		        // page whose selector happens to sit first. `osc0_type` is column 0 on a subtractive
		        // source (`nodes/sound_osc.h`'s `kSource0Children`).
		        expect(column).to_equal(1);
		        expect(static_cast<int32_t>(menu_scene().state().columns[0].focused)).to_equal(column);
	        });

	    it(
	        "spends the recorded opener, so a second claim does not re-root a menu that has moved on", _ {
		        // The control leg, varying exactly one thing against the example above: the entry point
		        // is called a second time, with everything else identical. Legacy clears its pair at the
		        // same point (`gui/ui/browser/sample_browser.cpp:1121-1122`), and the reason is the same
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
		        expect(menu_scene().state().path[0] == &nodes::source0).to_be_true();

		        expect(menu_scene().reroot_to_scene_opener_page()).to_be_false();
		        // And a refusal is a no-op, not a reset: the menu is exactly where the first call left it.
		        expect(menu_scene().state().path[0] == &nodes::source0).to_be_true();
		        expect(menu_scene().state().depth).to_equal(0);
	        });

	    it(
	        "refuses a menu that never opened a scene, and leaves its path alone", _ {
		        // The second control, varying one thing again: the same rig, the same root, the same
		        // four ticks -- but the menu is opened on `sound_root` rather than on the item that
		        // pushes the browser, so nothing ever recorded an opener. Legacy's guard is the same
		        // shape: the re-rooting is inside `if (menuItemHeadingTo != nullptr &&
		        // parentMenuHeadingTo != nullptr)` (`gui/ui/browser/sample_browser.cpp:1111`).
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

/// `browser_choice_target` first: it touches no scene, no card and no song, so it runs before
/// anything can have left a singleton in a state it would have to survive.
int browser_choice_spec(int argc, char** const argv) {
	return CppSpec::parse(argc, argv)
	               .add_specs(browser_choice_target, browser_choice_dispatch, browser_choice_claim,
	                          browser_choice_reroot)
	               .exec()
	               .is_success()
	           ? EXIT_SUCCESS
	           : EXIT_FAILURE;
}
