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
///   * **NOT covered, because it does not exist yet:** the claim. F3 task 1 ships the drain and the
///     choosing half; `claimSampleFile` (`gui/ui/shell_choice_dispatch.cpp`) is an empty body until
///     task 2 fills it in. That file's own `@warning` says what a spec would run if it were filled
///     in today -- a card read, an `AudioFileManager` allocation and an `openUI(&soundEditor)`,
///     inline on this BSP (`storage/owner.h:28-30`) -- and task 1's report names the two mechanisms
///     that can keep an example out of it once it is.
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
#include "gui/ui/shell_choice_dispatch.h"
#include "gui/ui/shell_host_ui.h"
#include "hid/display/display.h"
#include "model/sample/sample.h"
#include "model/song/song.h"
#include "processing/sound/sound.h"
#include "processing/source.h"
#include "storage/cluster/cluster.h"
#include "storage/multi_range/multisample_range.h"

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
	void open_browser() {
		select_language();
		(void)menu::assign_node_ids(nodes::kAllRoots);
		menu_scene().open(nodes::file0_selector, model.ctx());
		shell.init(SceneId{SceneKind::FirstApp, menu::node_id(nodes::file0_selector)});
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

/// @brief `sampleClaimTargetFor` as one comparable string, so a mismatch prints as a split.
///
/// `"<dir>|<name>"`, or `"-"` for `std::nullopt`. Read through a default rather than through
/// `->`: a mutant that answers `std::nullopt` everywhere must fail an example, not abort the
/// process inside `std::optional::operator->` before the rest of the file has run.
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
		        // `SampleBrowser::getCurrentFilePath` (`gui/ui/browser/sample_browser.cpp:503-514`) is
		        // `currentDir`, a separator, and `currentFileItem->filename` with its extension; this is that
		        // run backwards, and the extension has to survive it.
		        expect(split_of("SAMPLES/OSC_A/A1.WAV")).to_equal(std::string{"SAMPLES/OSC_A|A1.WAV"});
		        expect(split_of("SAMPLES/CLAP.WAV")).to_equal(std::string{"SAMPLES|CLAP.WAV"});
	        });

	    it(
	        "treats a path with no separator as a file at the card root", _ {
		        // `getCurrentFilePath` appends the separator only when `currentDir` is non-empty (`:505-507`),
		        // so the inverse of an empty `currentDir` is a path with no separator in it at all.
		        expect(split_of("A1.WAV")).to_equal(std::string{"|A1.WAV"});
	        });

	    it(
	        "answers nothing where the path names no file", _ {
		        // Neither of these can come out of `BrowserScene::enter`, which always appends a `FileItem`'s
		        // own filename (`scenes/browser/browser_scene.cpp:872-876`) -- they are the defence-in-depth
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
	    // Nothing here asserts that anything is *performed*. Nothing is, yet -- see the file docstring.

	    before_each([] {
		    static QuietDisplay d;
		    if (display == nullptr) {
			    display = &d;
		    }
	    });

	    it(
	        "takes the choice a real file press filed, and leaves the outbox empty", _ {
		        CleanBrowser clean;
		        ScratchCard card;
		        {
			        // The control, and it is not decoration: without it the example below is satisfied by a
			        // press that filed nothing at all, which is exactly how a drain test passes vacuously.
			        ShellRig control;
			        control.open_browser();
			        expect(control.browser_on_stack()).to_be_true();
			        control.enter();
			        // `Defer` parks AND closes in one statement (`scenes/browser/browser_scene.cpp:888-890`),
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

/// `browser_choice_target` first: it touches no scene, no card and no song, so it runs before
/// anything can have left a singleton in a state it would have to survive.
int browser_choice_spec(int argc, char** const argv) {
	return CppSpec::parse(argc, argv).add_specs(browser_choice_target, browser_choice_dispatch).exec().is_success()
	           ? EXIT_SUCCESS
	           : EXIT_FAILURE;
}
