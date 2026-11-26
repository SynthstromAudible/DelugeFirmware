/// @file
/// @brief `BrowserScene` -- the one scene that stands in for main's `gui/ui/browser/` subclasses the
///        menu reaches -- driven through a real `Shell` over `uitest::FakeSurfaceSet`,
///        against a real card.
///
/// Three halves: the `Descriptor` table (what each browser is for, as data), the scene's behaviour
/// over the async listing seam, and its navigation and pixels compared **against what main's
/// `Browser` answers for the same inputs** -- see the `browser_main_pixels` group.
///
/// @par Why every example installs a deferring dispatcher
///
/// **The shipped dispatcher, `deluge::ui::scenes::run_inline` (`ui/scenes/run_inline.h`), runs the op on the
/// caller's stack**, so with it a listing has already finished by the time `request()` returns, and the in-flight
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
/// @note The spec may include `gui/` (for `l10n::String` ids); the library
///       may not. Same rule `menu_scene_spec.cpp` and `context_menu_scene_spec.cpp` run under.

#include "gui/ui/menu_view_of.h"
#include "gui/ui/shell_host/shell_host.h"
#include "gui/ui/shell_slice_ops.h"
#include "support/peak_seed.h"
#include "ui/scenes/browser/browser_scene.h"
#include "ui/scenes/context_menu/context_menu_scene.h"
#include "ui/scenes/menu/context.h"
#include "ui/scenes/profile.h"
#include "ui/scenes/recorder/recorder_scene.h"
#include "ui/scenes/registry.h"
#include "ui/scenes/slicing/slicer_scene.h"

#include "base/notify/notice_queue.h"
#include "ui/shell/notices.h"
#include "ui/shell/profiles/deluge.h"
#include "ui/shell/shell.h"
#include "ui/widgets/core/metrics.h"
#include "ui/widgets/grid/favourites.h"
#include "ui/widgets/grid/keyboard.h"

#include "ui/gfx/oled/icons.h"

#include "gui/l10n/l10n.h"
#include "gui/l10n/strings.h"
#include "gui/ui/ui.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/waveform/waveform_renderer.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "model/action/action.h"
#include "model/action/action_logger.h"
#include "model/clip/audio_clip.h"
#include "model/instrument/midi_instrument.h"
#include "model/sample/last_file_path_loaded.h"
#include "model/sample/sample.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/cluster/cluster.h"
#include "storage/file_catalogue.h"
#include "storage/file_item.h"
#include "storage/flash_storage.h"
#include "storage/multi_range/multisample_range.h"
#include "util/name_compare.h"

#include "support/browser_scene_fixtures.h"
#include "support/fake_ports.h"
#include "support/oracle_files.h"
#include "support/oracle_table.h"
#include "support/slicer_host_open.h"
#include "support/song_fixture.h"

// Before <cppspec.hpp>: it #defines `it`, which collides with every local named `it` in the project
// headers this drags in (`audio_engine.h` -> `util/container/ordered_pos_vector.h`).
#include "support/card_access.h"

#include "support/mocks/fake_surfaces.h"
#include "support/printers/browser_request_printer.h"
#include "support/printers/rgb_printer.h"
#include "unit/ui/gfx/support/golden.h"

#include "cppspec.hpp"
#include "support/matchers/value_matchers.h"
#include "support/record_state.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "support/spec_main.h"

using namespace deluge::ui::scenes;
using deluge::l10n::String;
using deluge::storage::FileCatalogue;
using deluge::ui::scenes::browser::AcceptOutcome;
using deluge::ui::scenes::browser::browser_scene;
using deluge::ui::scenes::browser::BrowserId;
using deluge::ui::scenes::browser::BrowserPads;
using deluge::ui::scenes::browser::BrowserScene;
using deluge::ui::scenes::browser::kBrowserSceneKind;
using deluge::ui::scenes::browser::kListingPollMs;
using deluge::ui::scenes::browser::PreviewRequest;
using deluge::ui::scenes::browser::ScrollRequest;
using deluge::ui::scenes::context_menu::context_menu_scene;
using deluge::ui::scenes::menu::MenuContext;
using deluge::ui::scenes::spec::fake_browser_host;
using deluge::ui::scenes::spec::QuietAudio;
using deluge::ui::scenes::spec::SdRoot;
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

namespace bf = deluge::ui::scenes::spec::browser_fx;
using bf::blank_start_path;
using bf::BrowserState;
using bf::CleanBrowser;
using bf::context_of;
using bf::Deferred;
using bf::kBackX;
using bf::kBackY;
using bf::kKeyboardX;
using bf::kKeyboardY;
using bf::kLoadX;
using bf::kLoadY;
using bf::kSaveX;
using bf::kSaveY;
using bf::kSelectEncButtonX;
using bf::kSelectEncButtonY;
using bf::kShiftX;
using bf::kShiftY;
using bf::kXEncX;
using bf::kXEncY;
using bf::Pad;
using bf::pad_for;
using bf::Rig;
using bf::ScratchCard;

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
		static constexpr const deluge::ui::widgets::core::Style* kStyles[] = {
		    &deluge::ui::widgets::core::kDelugeStyle64};
		BoardProfile q = deluge_profile();
		q.style_by_index = kStyles;
		return q;
	}();
	return p;
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

/// @brief What the scene currently holds, and what it currently shows.
BrowserState port_state(Rig& r) {
	const BrowserScene& s = browser_scene();
	return BrowserState{s.file_index(), s.scroll(), s.cursor(), s.text_scroll(), std::string{s.entered_text()},
	                    r.fake.pbm(0)};
}

/// @brief `browser_scene.tsv`: where main's browser lands after each input sequence below.
spec::OracleTable& table() {
	return spec::OracleTable::named("browser_scene");
}

/// @brief Assert the port's answer is main's for the same inputs, as `browse/<name> state` and
///        `pixels/browser_<name>.pbm` hold it, numbers first so a navigation divergence prints as a number.
///
/// Pixels last and deliberately: a frame diff is unreadable next to "index 4 vs index 3".
/// @param self The enclosing `it(...)` block's own lambda parameter -- `expect(...)` expands to
///             `self.expect(...)`, exactly as `expect_golden` documents.
template <typename Self>
void expect_same(Self& self, const BrowserState& port, std::string_view name) {
	const std::vector<std::string>* main = table().find("browse/" + std::string{name}, "state");
	expect(main).not_().to_be_null();
	if (main != nullptr) {
		expect(main->size()).to_equal(size_t{5});
	}
	if (main == nullptr || main->size() != size_t{5}) {
		return;
	}
	expect(std::to_string(port.index)).to_equal((*main)[0]);
	expect(std::to_string(port.scroll)).to_equal((*main)[1]);
	expect(port.text).to_equal((*main)[4]);
	expect(std::to_string(port.cursor)).to_equal((*main)[2]);
	expect(std::to_string(port.text_scroll)).to_equal((*main)[3]);
	// The FULL 128x48 panel, title rows included: `OLED_MAIN_TOPMOST_PIXEL` is 5 and main offsets its
	// own drawing by it (`Browser::renderOLED`; `Canvas::draw_screen_title`), so rows 0-4 are blank on
	// both sides and there is nothing to normalise away.
	expect(port.pbm).to_equal(spec::OracleFiles::pixels("browser_" + std::string{name}));
}

/// @brief The fixture's per-cluster peak at 8-bit scale: a widening shape.
std::pair<int8_t, int8_t> overview_peak(int32_t c) {
	return {static_cast<int8_t>(-(16 + 3 * c)), static_cast<int8_t>(16 + 3 * c)};
}

/// @brief A 16-bit mono sample whose peak index already holds the first `investigated_clusters` clusters' peaks, so a
///        read of them needs no card.
Sample* make_overview_sample(int32_t investigated_clusters = 32) {
	constexpr size_t kClusterBytes = 32768;
	constexpr int32_t kClusters = 32;
	constexpr uint64_t kLength = static_cast<uint64_t>(kClusterBytes) * kClusters / 2;
	Cluster::set_size(kClusterBytes);
	void* memory = deluge::memory::alloc_fast(sizeof(Sample));
	auto* s = new (memory) Sample();
	s->byteDepth = 2;
	s->numChannels = 1;
	s->sampleRate = 44100;
	s->audioDataStartPosBytes = 0;
	s->audioDataLengthBytes = kLength * 2;
	s->lengthInSamples = kLength;
	peak_seed::seed_peaks(*s, overview_peak, investigated_clusters);
	return s;
}

/// @brief Put @p sample in the preview drum's range for an example, as a landed preview does; the drum's range is
///        emptied after.
struct PreviewHeld {
	MultisampleRange* range;
	explicit PreviewHeld(Sample* sample)
	    : range(static_cast<MultisampleRange*>(AudioEngine::sampleForPreview->getOrCreateFirstRange(0))) {
		range->sampleHolder.audioFile = sample;
	}
	~PreviewHeld() { range->sampleHolder.audioFile = nullptr; }
	PreviewHeld(const PreviewHeld&) = delete;
	PreviewHeld& operator=(const PreviewHeld&) = delete;
};

/// @brief The pad flashes the rig's grid surface was sent since @p from.
std::vector<deluge::ui::widgets::grid::PadFlash> flashes_since(Rig& r, size_t from) {
	return {r.fake.flashes_sent.begin() + static_cast<std::ptrdiff_t>(from), r.fake.flashes_sent.end()};
}

/// @brief Whether the rig's LED for @p b is declared on.
bool led_on(Rig& r, ButtonId b) {
	return r.fake.leds().desired_on[static_cast<size_t>(b)];
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
	deluge::ui::widgets::core::PadMask pads{};
	UiContext ctx;
	explicit DirectCtx(uint32_t now_ms = 0) : ctx{held, pads, false, false, now_ms, deluge_profile()} {}
};

/// @brief The context the sample browser opens on for a sound: its first range, made if it has none.
MenuContext sound_context(spec::SongFixture& f) {
	MenuContext ctx = context_of(f);
	(void)menu::ensure_first_range(ctx);
	return ctx;
}

/// @brief Make @p sound a ring-mod synth before the browser opens on it: its state, and the config derived from it.
void ring_mod(Sound& sound) {
	deluge::test::set(sound.record(), "mode", "ringmod");
	sound.deriveConfig();
}

/// @brief Press Select on the next tick.
/// @return When it went down.
uint32_t press_select(Rig& r) {
	const uint32_t at = r.clock + 10;
	r.button(kSelectEncButtonX, kSelectEncButtonY, true, at);
	r.tick(at);
	return at;
}

/// @brief Release Select at @p at, on a tick of its own.
void release_select(Rig& r, uint32_t at) {
	r.button(kSelectEncButtonX, kSelectEncButtonY, false, at);
	r.tick(at);
}

/// @brief Tick every 10 ms up to and including @p at.
void run_until(Rig& r, uint32_t at) {
	while (r.clock + 10 <= at) {
		r.step();
	}
}

/// @brief The scene of kind @p k on @p r's stack, or null.
deluge::ui::shell::Scene* on_stack(Rig& r, SceneKind k) {
	for (deluge::ui::shell::Scene* scene : r.shell.stack().scenes()) {
		if (scene->kind() == k) {
			return scene;
		}
	}
	return nullptr;
}

/// @brief The popup's text, or empty with none showing.
std::string popup_text(Rig& r) {
	const auto* popup = static_cast<const deluge::ui::shell::PopupScene*>(on_stack(r, SceneKind::Popup));
	return popup != nullptr ? std::string{popup->text()} : std::string{};
}

/// @brief Walk the selection until the selected item is (or is not) a folder.
void select_folder(Rig& r, bool folder) {
	for (int i = 0; i < 8 && browser_scene().current_is_folder() != folder; i++) {
		r.turn(1, r.clock + 10);
		r.settle();
	}
}

/// @brief A favourites bank in memory, recording what the browser asks of it.
struct FakeFavourites final : deluge::ui::scenes::services::Favourites {
	std::array<std::optional<uint8_t>, 16> slots{};
	std::array<std::string, 16> paths{};
	std::optional<uint8_t> current{};
	uint8_t bank = 0;
	deluge::ui::scenes::services::FavouritesBank state = deluge::ui::scenes::services::FavouritesBank::Loaded;
	std::string category;
	int closes = 0;

	void set_category(std::string_view c) override { category = c; }
	void close() override { closes++; }
	[[nodiscard]] deluge::ui::scenes::services::FavouritesBank bank_state() const override { return state; }
	[[nodiscard]] std::array<std::optional<uint8_t>, 16> colours() const override { return slots; }
	[[nodiscard]] std::optional<uint8_t> current_favourite() const override { return current; }
	[[nodiscard]] uint8_t current_bank() const override { return bank; }
	[[nodiscard]] bool is_empty(uint8_t p) const override { return !slots[p].has_value(); }
	std::string recall(uint8_t p) override {
		current = p;
		return slots[p].has_value() ? paths[p] : std::string{};
	}
	void set(uint8_t p, std::string_view path) override {
		current = p;
		slots[p] = 4;
		paths[p] = path;
	}
	void unset(uint8_t p) override {
		current = p;
		slots[p].reset();
		paths[p].clear();
	}
	void select_bank(uint8_t b) override { bank = b; }
	void change_colour(uint8_t p, int32_t offset) override {
		if (slots[p].has_value()) {
			slots[p] = static_cast<uint8_t>(((*slots[p] + offset) % 16 + 16) % 16);
		}
	}
};

/// @brief Install @p fake as the browser's favourites for an example, in the default layout, and put the host's back.
struct UseFavourites {
	FavouritesDefaultLayout saved = FlashStorage::defaultFavouritesLayout;
	explicit UseFavourites(FakeFavourites& fake, FavouritesDefaultLayout layout = FavouritesDefaultLayoutFavorites) {
		FlashStorage::defaultFavouritesLayout = layout;
		browser_scene().set_favourites(&fake);
	}
	~UseFavourites() {
		browser_scene().set_favourites(nullptr);
		FlashStorage::defaultFavouritesLayout = saved;
	}
	UseFavourites(const UseFavourites&) = delete;
	UseFavourites& operator=(const UseFavourites&) = delete;
};

/// @brief Press and release main pad (@p x, @p y), a tick each.
void tap(Rig& r, uint8_t x, uint8_t y) {
	r.pad(x, y, 64, r.clock + 10);
	r.step();
	r.pad(x, y, 0, r.clock + 10);
	r.step();
}

} // namespace

// clang-format off
describe browser_descriptors("scenes::browser descriptor table", $ {
	it("covers exactly the three browsers the menu tree reaches", _ {
		// Legacy has nine `Browser` subclasses. Only `SampleBrowser`, `LoadMidiDeviceDefinitionUI` and
		// `DxSyxBrowser` are opened from a menu item; the load/save family is opened from root UIs and is not
		// ported. See `BrowserId`.
		expect(static_cast<int>(BrowserId::Count)).to_equal(3);
	});

	it("carries DxSyxBrowser's title, extension, folder and rules, opening on DX7's first entry", _ {
		const browser::Descriptor& syx = browser::descriptor(BrowserId::DxSyx);
		// `DxSyxBrowser::DxSyxBrowser` and `opened` (`origin/main:src/deluge/gui/ui/browser/dx_browser.cpp`).
		expect(std::string{syx.title}).to_equal(std::string{"DX7 syx files"});
		expect(std::string{syx.extensions[0]}).to_equal(std::string{"SYX"});
		expect(syx.extensions[1]).to_be_null();
		expect(std::string{syx.default_dir}).to_equal(std::string{"DX7"});
		expect(syx.interpret_note_names).to_be_false();
		expect(syx.allow_folders_sharing_name_with_file).to_be_true();
		expect(syx.folder_refusal).to_be_null();
		std::string dir = "ELSEWHERE";
		std::string name = "REMEMBERED";
		syx.start_at(menu::MenuContext{}, dir, name);
		expect(dir).to_equal(std::string{"DX7"});
		expect(name.empty()).to_be_true();
		Error error = Error::NONE;
		expect(syx.accept(menu::MenuContext{}, "DX7/A.SYX", error)).to_equal(browser::AcceptOutcome::DeferAndWait);
		expect(syx.can(menu::MenuContext{})).to_be_false(); // no sound to load a bank into
	});

	it("carries each subclass's own extensions, folder and note-name rule", _ {
		const browser::Descriptor& sample = browser::descriptor(BrowserId::Sample);
		const browser::Descriptor& midi = browser::descriptor(BrowserId::MidiDeviceDefinition);

		// `allowedFileExtensionsAudio` (`SampleBrowser`'s constructor) vs the base's
		// `allowedFileExtensionsXML` (`Browser::opened`).
		expect(std::string{sample.extensions[0]}).to_equal(std::string{"WAV"});
		expect(std::string{sample.extensions[1]}).to_equal(std::string{"AIFF"});
		expect(std::string{sample.extensions[2]}).to_equal(std::string{"AIF"});
		expect(sample.extensions[3]).to_be_null();
		expect(std::string{midi.extensions[0]}).to_equal(std::string{"XML"});

		expect(std::string{sample.default_dir}).to_equal(std::string{"SAMPLES"});
		expect(std::string{midi.default_dir}).to_equal(std::string{"MIDI_DEVICES/DEFINITION"});

		// `shouldInterpretNoteNamesForThisBrowser = true` (`SampleBrowser`'s constructor); the base leaves
		// it false.
		expect(sample.interpret_note_names).to_be_true();
		expect(midi.interpret_note_names).to_be_false();

		// `SampleBrowser::opened` sets `allowFoldersSharingNameWithFile = true`
		// (`SampleBrowser`'s constructor); `Browser::opened` leaves it false.
		expect(sample.allow_folders_sharing_name_with_file).to_be_true();
		expect(midi.allow_folders_sharing_name_with_file).to_be_false();

		expect(std::string{sample.title}).to_equal(std::string{"Audio files"});
		expect(std::string{midi.title}).to_equal(std::string{"Load midi device"});
	});

	it("gives each browser its own row icon, and only the MIDI one a second piece", _ {
		// `SampleBrowser`'s `fileIcon = OLED::waveIcon` (`SampleBrowser`'s constructor); `LoadMidiDeviceDefinitionUI`'s
		// `fileIcon = OLED::midiIcon` plus `fileIconPt2 = OLED::midiIconPt2` at width 1
		// (`LoadMidiDeviceDefinitionUI::setupForLoadingMidiDeviceDefinition` in `load_midi_device_definition_ui.cpp`).
		const browser::Descriptor& sample = browser::descriptor(BrowserId::Sample);
		const browser::Descriptor& midi = browser::descriptor(BrowserId::MidiDeviceDefinition);
		expect(sample.file_icon).to_equal(&deluge::ui::gfx::oled::icons::browserWave);
		expect(sample.file_icon_pt2).to_be_null();
		expect(midi.file_icon).to_equal(&deluge::ui::gfx::oled::icons::browserMidi);
		expect(midi.file_icon_pt2).to_equal(&deluge::ui::gfx::oled::icons::browserMidiEdge);
		expect(static_cast<int>(midi.file_icon_pt2->width)).to_equal(1);
		expect(static_cast<int>(sample.file_icon->width)).to_equal(8);
	});

	it("ports the legacy icon bitmaps byte for byte", _ {
		// The four `Icon`s added to `ui/gfx/oled/icons.cpp` are the legacy arrays (`OLED::folderIcon`, `OLED::waveIcon`,
		// `OLED::midiIcon` and `OLED::midiIconPt2` in `hid/display/oled_icons.cpp`), and this compares them against
		// those arrays rather than re-spelling the bits: a transcription typo would otherwise only show up as a golden
		// nobody could read.
		const deluge::ui::gfx::oled::Icon& folder = deluge::ui::gfx::oled::icons::browserFolder;
		const deluge::ui::gfx::oled::Icon& wave = deluge::ui::gfx::oled::icons::browserWave;
		const deluge::ui::gfx::oled::Icon& midi = deluge::ui::gfx::oled::icons::browserMidi;
		for (int i = 0; i < 8; i++) {
			expect(static_cast<int>(folder.data[i]))
			    .to_equal(static_cast<int>(deluge::hid::display::OLED::folderIcon[i]));
			expect(static_cast<int>(wave.data[i]))
			    .to_equal(static_cast<int>(deluge::hid::display::OLED::waveIcon[i]));
			expect(static_cast<int>(midi.data[i]))
			    .to_equal(static_cast<int>(deluge::hid::display::OLED::midiIcon[i]));
		}
		// Legacy blits only the first column of `midiIconPt2` (`fileIconPt2Width = 1`).
		expect(static_cast<int>(deluge::ui::gfx::oled::icons::browserMidiEdge.data[0]))
		    .to_equal(static_cast<int>(deluge::hid::display::OLED::midiIconPt2[0]));
	});
});

describe browser_composition("scenes::browser::BrowserScene composition", $ {
	it("registers at FirstApp + 3 and covers the screen it draws", _ {
		const deluge::ui::shell::SceneRegistry reg = scene_registry();
		expect(reg.find(kBrowserSceneKind)).to_equal(&browser_scene());
		expect(static_cast<int>(kBrowserSceneKind)).to_equal(static_cast<int>(SceneKind::FirstApp) + 3);

		const deluge::ui::shell::Scene& s = browser_scene();
		// Unlike `RenameScene`/`ContextMenuScene`, neither legacy browser sets
		// `oledShowsUIUnderneath`: `renderOLED` starts with `drawScreenTitle` and owns the panel.
		expect(s.oled_transparent(SurfaceRole::Main)).to_be_false();
		expect(s.is_grid_overlay()).to_be_true();
		expect(s.occludes_grid()).to_be_false();
		expect(s.input_transparent()).to_be_false();
	});

	it("draws the keyboard on the main pads only, greying the mute column, leaving the sidebar to the view beneath", _ {
		// The keyboard is `QwertyUI::drawKeys`'s, main pads only (`gui/ui/qwerty_ui.cpp`), and
		// `SampleBrowser::getGreyoutColsAndRows` greys the mute column while it shows
		// (`origin/main:src/deluge/gui/ui/browser/sample_browser.cpp`).
		CleanBrowser clean;
		ScratchCard card;
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		Rig r;
		r.open_and_land(context_of(f), BrowserId::Sample);
		r.pad(9, 7, 64, r.clock + 10); // the favourites row: reveals the keyboard and types nothing
		r.step();
		r.pad(9, 7, 0, r.clock + 10);
		r.step();
		const deluge::ui::shell::Scene& s = browser_scene();
		expect(browser_scene().pads()).to_equal(BrowserPads::Keyboard);
		expect(s.pad_regions()).to_equal(deluge::ui::shell::PadRegions::Main);
		expect(static_cast<uint64_t>(s.greyout_mask().cols)).to_equal(uint64_t{0b10});
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
		// (`origin/main:src/deluge/gui/ui/browser/sample_browser.cpp`).
		expect(std::string{browser_scene().current_dir()}).to_equal(std::string{"SAMPLES"});
		// Three files and two folders, sorted. `CLIPS` and `KICKS` are directories.
		expect(catalogue_order()).to_equal(std::string("CLAP.WAV,CLIPS,KICKS,SNARE.WAV,TOM.WAV"));
		// `arrivedInNewFolder` with no search string selects entry 0 and shows its name with the
		// extension stripped (`Browser::setEnteredTextFromCurrentFilename` in `browser.cpp`).
		expect(browser_scene().file_index()).to_equal(0);
		expect(browser_scene().scroll()).to_equal(0);
		expect(std::string{browser_scene().entered_text()}).to_equal(std::string{"CLAP"});
	});

	it("says its enter was not refused once it opens", _ {
		CleanBrowser clean;
		ScratchCard card;
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		Rig r;
		r.open_and_land(context_of(f), BrowserId::Sample);
		expect(browser_scene().last_enter_refused()).to_be_false();
	});

	it("says its enter was refused for an id past the table, which the menu that pushed it reads", _ {
		CleanBrowser clean;
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		browser_scene().open(context_of(f));
		expect(browser_scene().on_enter(SceneId{kBrowserSceneKind, static_cast<uint16_t>(BrowserId::Count)}))
		    .to_be_false();
		expect(browser_scene().last_enter_refused()).to_be_true();
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

	it("greys every column for the MIDI browser, as LoadMidiDeviceDefinitionUI does", _ {
		// `LoadMidiDeviceDefinitionUI::getGreyoutColsAndRows` sets `*cols = 0xFFFFFFFF`
		// (`load_midi_device_definition_ui.cpp`), keyboard and sidebar alike.
		CleanBrowser clean;
		ScratchCard card;
		spec::SongFixture f = spec::make_midi_song();
		Rig r;
		r.open_and_land(context_of(f), BrowserId::MidiDeviceDefinition);

		expect(static_cast<uint64_t>(browser_scene().greyout_mask().cols)).to_equal(uint64_t{0xFFFFFFFFu});
		expect(static_cast<uint64_t>(browser_scene().greyout_mask().rows)).to_equal(uint64_t{0});
	});

	it("refuses to open when the descriptor cannot resolve its model", _ {
		CleanBrowser clean;
		{
			// The sample browser over a MIDI clip: no audio-file holder at all, which legacy would
			// answer by dereferencing null (`SampleBrowser::opened`).
			spec::SongFixture f = spec::make_midi_song();
			expect(browser::descriptor(BrowserId::Sample).can(context_of(f))).to_be_false();
			browser_scene().open(context_of(f));
			expect(browser_scene().on_enter(SceneId{kBrowserSceneKind,
			                                        static_cast<uint16_t>(BrowserId::Sample)})).to_be_false();
		}
		{
			// The MIDI-definition browser over an audio clip: `LoadMidiDeviceDefinitionUI::opened`
			// (`load_midi_device_definition_ui.cpp`) returns false unless the output is `OutputType::MIDI_OUT`.
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

	it("creates DX7 when the card has none and opens on it, empty, as DxSyxBrowser does", _ {
		// `readFileItemsFromFolderAndMemory`: the folder `DxSyxBrowser::opened` lists is its default one, so a missing
		// one is created and listed, and the browser stays up on the empty folder. This card has no DX7.
		CleanBrowser clean;
		ScratchCard card;
		expect(deluge::io::Directory::open("DX7").has_value()).to_be_false();
		spec::SongFixture f = spec::make_synth_song();
		expect(browser::descriptor(BrowserId::DxSyx).can(context_of(f))).to_be_true();
		Rig r;
		r.open_and_land(context_of(f), BrowserId::DxSyx);

		expect(r.browser_on_stack()).to_be_true();
		expect(std::string{browser_scene().current_dir()}).to_equal(std::string{"DX7"});
		expect(catalogue_order()).to_equal(std::string(""));
		expect(browser_scene().file_index()).to_equal(-1);
		expect(std::string{browser_scene().entered_text()}).to_equal(std::string{""});
		expect(deluge::io::Directory::open("DX7").has_value()).to_be_true();
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

		// `Listing::request` raises `Notice::Kind::Working`, which `Notices` turns into a
		// pushed `SceneKind::Working` (`Notices::drain`). Nothing in this scene draws a
		// spinner; the busy indicator is the seam's.
		expect(browser_scene().listing_in_flight()).to_be_true();
		expect(r.working_shown()).to_be_true();
		expect(static_cast<int>(FileCatalogue::fileItems.size())).to_equal(0);

		// Legacy's `if (listingInProgress_) return;` at the top of `padAction`/`backButtonAction`
		// (`browser.cpp`): the press reaches the scene and does nothing at all -- it
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
		// This scene must not file `Action::Kind::LockInput{true}` while `in_flight()` and clear its own
		// flag in `on_exit()`: `on_exit()` returns `void`, so the matching release could never be
		// filed on the one path that needs it. `set_input_lock` is one bare global bool with no
		// owner (`Router`'s `LockInput` arm -> `InputLowering::set_input_lock`), so an external
		// `Pop` mid-listing would leave the ENTIRE device deaf until something else happened to clear
		// it. It is also the wrong lock: it *drops* events, including encoder edges
		// (`InputLowering::on_encoder_edges`), where legacy's `waitingForSDRoutineToEnd` defers and
		// retries.
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

		// The removal that would strand it: the scene leaves the stack with a listing dispatched
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
		// `pending_` is a single slot, and it is occupied in exactly the state the other two busy tests
		// miss: a `Busy` refusal leaves nothing in flight and no result owed. With an `Open` parked, a
		// BACK press must not reach `back()` and file an `UpLevel` straight over it -- that would lose
		// the `Open` and then list a directory derived from the FOREIGN holder's `currentDir`.
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
		// this tick (`Shell::tick`). With no input arriving, nothing else asks for a
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
		to_be_at_most(expect(armed.value()), browser::kListingPollMs);

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
		// `ClearWorking` down), and `Notices` pushing or popping `SceneKind::Working` is itself
		// enough to bring a frame back -- so neither can tell whether the scene asked to be woken.
		// A `Busy` refusal is different: `Listing::request` tests `FileCatalogue::claim` BEFORE it
		// raises anything (`Listing::request`), so the queue never moves. Nothing else in the shell
		// has any reason to render this scene again, and without the poll the retry never happens.
		CleanBrowser clean;
		ScratchCard card;
		spec::AudioSongFixture f = spec::make_audio_clip_song();

		// Something else holds the catalogue -- a second seam, or a legacy `Browser` mid-listing;
		// the claim is on the resource, so a raw token stands in for either.
		int other = 0;
		expect(FileCatalogue::claim(&other)).to_be_true();

		Rig r;
		r.open(context_of(f), BrowserId::Sample);
		r.tick(0);
		expect(browser_scene().listing_in_flight()).to_be_false();
		expect(Deferred::pending()).to_be_false();
		// Nothing was raised, so nothing in `Notices` will redraw this scene for us.
		deluge::base::notify::Notice n;
		int raised = 0;
		while (deluge::base::notify::notices().pop(n)) {
			raised++;
		}
		expect(raised).to_equal(0);
		// The scene owes a listing and says so.
		const std::optional<uint32_t> armed = browser_scene().next_timer();
		expect(armed.has_value()).to_be_true();
		to_be_at_most(expect(armed.value()), browser::kListingPollMs);

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
		// A dispatcher answering false is a drop, not a failure: `Listing::request` (`ui/scenes/browser/listing.cpp`) puts
		// the seam back to idle, so the request can be filed again. Here the request is kept and re-filed from
		// `after_render`, which is what makes the retry happen with no input at all.
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

describe browser_preview("scenes::browser::BrowserScene asking for previews", $ {
	it("files one on open for the selected file, direction 0", _ {
		CleanBrowser clean;
		ScratchCard card;
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		Rig r;
		r.open_and_land(context_of(f), BrowserId::Sample);
		const std::optional<PreviewRequest> request = fake_browser_host().take_preview();
		expect(request).to_equal(PreviewRequest{.id = BrowserId::Sample, .path = "SAMPLES/CLAP.WAV", .direction = 0});
		expect(browser_scene().preview_outstanding()).to_be_true();
		expect(fake_browser_host().take_preview().has_value()).to_be_false();
	});

	it("files a preview request when the selection moves, with its direction", _ {
		CleanBrowser clean;
		ScratchCard card;
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		Rig r;
		r.open_and_land(context_of(f), BrowserId::Sample);
		(void)fake_browser_host().take_preview();
		r.turn(3, r.clock + 10);
		r.step();
		expect(std::string{browser_scene().entered_text()}).to_equal(std::string{"SNARE"});
		expect(fake_browser_host().take_preview()).to_equal(PreviewRequest{.path = "SAMPLES/SNARE.WAV", .direction = 1});
		r.turn(-3, r.clock + 10);
		r.step();
		expect(fake_browser_host().take_preview()).to_equal(PreviewRequest{.path = "SAMPLES/CLAP.WAV", .direction = -1});
	});

	it("files an empty-path request for a folder", _ {
		CleanBrowser clean;
		ScratchCard card;
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		Rig r;
		r.open_and_land(context_of(f), BrowserId::Sample);
		(void)fake_browser_host().take_preview();
		r.turn(1, r.clock + 10);
		r.step();
		expect(std::string{browser_scene().entered_text()}).to_equal(std::string{"CLIPS"});
		expect(fake_browser_host().take_preview()).to_equal(PreviewRequest{.path = "", .direction = 1});
	});

	it("files a request when a typed search lands on another file, direction 0", _ {
		CleanBrowser clean;
		ScratchCard card;
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		Rig r;
		r.open_and_land(context_of(f), BrowserId::Sample);
		(void)fake_browser_host().take_preview();
		r.type('T');
		expect(fake_browser_host().take_preview()).to_equal(PreviewRequest{.path = "SAMPLES/TOM.WAV", .direction = 0});
	});

	it("files an empty-path request on exit", _ {
		CleanBrowser clean;
		ScratchCard card;
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		Rig r;
		r.open_and_land(context_of(f), BrowserId::Sample);
		(void)fake_browser_host().take_preview();
		browser_scene().on_exit();
		expect(fake_browser_host().take_preview()).to_equal(PreviewRequest{.path = "", .direction = 0});
	});

	it("files none for the MIDI device definition browser", _ {
		CleanBrowser clean;
		ScratchCard card;
		spec::SongFixture f = spec::make_midi_song();
		Rig r;
		r.open_and_land(context_of(f), BrowserId::MidiDeviceDefinition);
		expect(fake_browser_host().take_preview().has_value()).to_be_false();
		expect(browser_scene().preview_outstanding()).to_be_false();
	});

	it("polls while a preview is outstanding, and stops once it settles", _ {
		CleanBrowser clean;
		ScratchCard card;
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		Rig r;
		r.open_and_land(context_of(f), BrowserId::Sample);
		expect(browser_scene().next_timer()).to_equal(std::optional<uint32_t>{kListingPollMs});
		browser_scene().preview_settled(true);
		expect(browser_scene().preview_outstanding()).to_be_false();
		expect(browser_scene().next_timer()).not_().to_equal(std::optional<uint32_t>{kListingPollMs});
	});

	it("opens beside the last sample loaded when the holder names none", _ {
		CleanBrowser clean;
		ScratchCard card;
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		expect(blank_start_path(f)).to_be_true();
		deluge::model::lastFilePathLoaded() = "SAMPLES/KICKS/KICK1.WAV";
		Rig r;
		r.open_and_land(context_of(f), BrowserId::Sample);
		expect(std::string{browser_scene().current_dir()}).to_equal(std::string{"SAMPLES/KICKS"});
		expect(std::string{browser_scene().entered_text()}).to_equal(std::string{"KICK1"});
	});
});

describe browser_pads("scenes::browser::BrowserScene pads, greyout and LEDs", $ {
	it("opens in the root state: no pads, every column but the audition column greyed, the BROWSE pad flashing", _ {
		CleanBrowser clean;
		ScratchCard card;
		spec::SongFixture f = spec::make_synth_song();
		Rig r;
		r.open_and_land(sound_context(f), BrowserId::Sample);
		const deluge::ui::shell::Scene& s = browser_scene();
		expect(browser_scene().pads()).to_equal(BrowserPads::Root);
		expect(s.pad_regions()).to_equal(deluge::ui::shell::PadRegions::None);
		expect(static_cast<uint64_t>(s.greyout_mask().cols)).to_equal(uint64_t{0xFFFFFFFE});
		const size_t from = r.fake.flashes_sent.size();
		run_until(r, r.clock + 1000);
		const std::vector<deluge::ui::widgets::grid::PadFlash> seen = flashes_since(r, from);
		expect(seen.empty()).to_be_false();
		for (const deluge::ui::widgets::grid::PadFlash& fl : seen) {
			expect(fl.x).to_equal(0);
			expect(fl.y).to_equal(5);
		}
		// `updatePadLightsFor`'s `setupShortcutBlink(x, 5, 0)`, armed by `beginScreen` after the browser opened: every
		// 200 ms cycle, so four or five flashes in a second.
		expect(seen.size()).to_be_between(4, 5);
		expect(browser_scene().browse_blink()).to_equal(browser::BrowseBlink::Menu);
	});

	it("greys only the mute column over the keyboard screen", _ {
		CleanBrowser clean;
		ScratchCard card;
		spec::SongFixture f = spec::make_synth_song();
		MenuContext ctx = sound_context(f);
		ctx.root_is_keyboard = true;
		Rig r;
		r.open_and_land(ctx, BrowserId::Sample);
		expect(browser_scene().pads()).to_equal(BrowserPads::Root);
		expect(static_cast<uint64_t>(browser_scene().greyout_mask().cols)).to_equal(uint64_t{0b10});
	});

	it("shows the waveform once the preview settles with a sample", _ {
		CleanBrowser clean;
		ScratchCard card;
		spec::SongFixture f = spec::make_synth_song();
		Sample* sample = make_overview_sample();
		PreviewHeld held(sample);
		Rig r;
		r.open_and_land(sound_context(f), BrowserId::Sample);
		browser_scene().preview_settled(true);
		run_until(r, r.clock + 50); // the settle asks for the repaint on the next tick
		const deluge::ui::shell::Scene& s = browser_scene();
		expect(browser_scene().pads()).to_equal(BrowserPads::Waveform);
		expect(s.pad_regions()).to_equal(deluge::ui::shell::PadRegions::Main);
		expect(static_cast<uint64_t>(s.greyout_mask().cols)).to_equal(uint64_t{0b10});
		// The `renderFullScreen` of main's `SampleBrowser::previewIfPossible`
		// (`origin/main:src/deluge/gui/ui/browser/sample_browser.cpp`), zoomed all the way out.
		const deluge::ui::views::waveform::WaveformView view =
		    deluge::ui::views::waveform::WaveformView::open(sample->lengthInSamples, std::nullopt);
		RGB legacy[kDisplayHeight][kDisplayWidth + kSideBarWidth]{};
		(void)waveformRenderer.renderFullScreen(sample, static_cast<uint64_t>(view.scroll()),
		                                        static_cast<uint64_t>(view.zoom()), legacy);
		int32_t differ = 0;
		int32_t lit = 0;
		for (int32_t x = 0; x < kDisplayWidth; x++) {
			for (int32_t y = 0; y < kDisplayHeight; y++) {
				differ += r.fake.grid().grid().at(static_cast<uint8_t>(x), static_cast<uint8_t>(y)) == legacy[y][x] ? 0 : 1;
				lit += legacy[y][x] == RGB{} ? 0 : 1;
			}
		}
		expect(lit).to_be_greater_than(0);
		expect(differ).to_equal(0);
	});

	it("Q32: draws a fresh preview's first frame at the scale of its next: the range is the index's, not the reads'", _ {
		CleanBrowser clean;
		ScratchCard card;
		spec::SongFixture f = spec::make_synth_song();
		Sample* sample = make_overview_sample();
		PreviewHeld held(sample);
		Rig r;
		r.open_and_land(sound_context(f), BrowserId::Sample);
		const browser::WaveformDrawStats before = browser_scene().waveform_draw_stats();
		browser_scene().preview_settled(true);
		r.step(); // the settle asks for the repaint on the next tick
		expect(browser_scene().waveform_draw_stats().draws - before.draws).to_equal(1);
		const deluge::ui::gfx::grid::PadGrid first = r.fake.grid().grid();
		browser_scene().preview_settled(true); // the same sample again: a second frame
		r.step();
		expect(browser_scene().waveform_draw_stats().draws - before.draws).to_equal(2);
		int32_t differ = 0;
		int32_t lit = 0;
		for (uint8_t x = 0; x < kDisplayWidth; x++) {
			for (uint8_t y = 0; y < kDisplayHeight; y++) {
				differ += first.at(x, y) == r.fake.grid().grid().at(x, y) ? 0 : 1;
				lit += first.at(x, y) == RGB{} ? 0 : 1;
			}
		}
		expect(lit).to_be_greater_than(0);
		expect(differ).to_equal(0);
	});

	it("offers the slicer the view its preview drew, for that sample only", _ {
		// `main`'s slicer reads `waveformBasicNavigator`, which only `SampleBrowser::previewIfPossible` sets
		// (`origin/main:src/deluge/gui/ui/browser/sample_browser.cpp`): the previewed sample's view.
		CleanBrowser clean;
		ScratchCard card;
		spec::SongFixture f = spec::make_synth_song();
		Sample* sample = make_overview_sample();
		PreviewHeld held(sample);
		Rig r;
		r.open_and_land(sound_context(f), BrowserId::Sample);
		using deluge::gui::shell_host::waveform_identity;
		expect(browser_scene().shown_waveform(waveform_identity(*sample)).has_value()).to_be_false(); // nothing drawn
		browser_scene().preview_settled(true);
		run_until(r, r.clock + 50);
		const std::optional<deluge::ui::views::waveform::ShownWaveform> shown =
		    browser_scene().shown_waveform(waveform_identity(*sample));
		expect(shown.has_value()).to_be_true();
		if (shown) {
			expect(shown->view).to_equal(browser_scene().waveform_view());
		}
		// The columns the preview read are the waveforms port's, kept by file.
		const deluge::ui::views::waveform::PeakRead read = menu::host_ports().waveforms->peaks(
		    {.kind = deluge::ui::scenes::bind::WaveformAddress::Kind::PREVIEW}, browser_scene().waveform_view().window());
		expect(read.column(0).state()).to_equal(deluge::ui::views::waveform::ColumnState::PEAK);
		Sample* other = make_overview_sample();
		expect(browser_scene().shown_waveform(waveform_identity(*other)).has_value()).to_be_false();
		expect(browser_scene().shown_waveform(0).has_value()).to_be_false();
	});

	it("redraws a preview the card has not caught up with, and gives up 300 ms after its last progress", _ {
		// `tickPreviewRedraw`. No cluster the overview lacks ever arrives here (the spec process runs no loader), so
		// the render never completes: the browser asks for redraws, then stops.
		CleanBrowser clean;
		ScratchCard card;
		spec::SongFixture f = spec::make_synth_song();
		PreviewHeld held(make_overview_sample(16));
		Rig r;
		r.open_and_land(sound_context(f), BrowserId::Sample);
		browser_scene().preview_settled(true);
		run_until(r, r.clock + 50);
		expect(browser_scene().pads()).to_equal(BrowserPads::Waveform);
		expect(r.shell.next_wake().has_value()).to_be_true();
		if (r.shell.next_wake().has_value()) {
			to_be_at_most(expect(*r.shell.next_wake() - r.clock), kListingPollMs);
		}
		run_until(r, r.clock + 400);
		if (r.shell.next_wake().has_value()) {
			expect(*r.shell.next_wake() - r.clock).to_be_greater_than(kListingPollMs);
		}
	});

	it("counts one draw per previewed sample, a redraw per unfinished repaint, and the completed render", _ {
		// A render the card has not caught up with is repainted as its clusters land: one draw, then a redraw that
		// finishes it.
		CleanBrowser clean;
		ScratchCard card;
		spec::SongFixture f = spec::make_synth_song();
		Sample* sample = make_overview_sample(16);
		PreviewHeld held(sample);
		Rig r;
		r.open_and_land(sound_context(f), BrowserId::Sample);
		const browser::WaveformDrawStats before = browser_scene().waveform_draw_stats();
		browser_scene().preview_settled(true);
		run_until(r, r.clock + 50);
		expect(browser_scene().waveform_redraw_pending()).to_be_true();
		peak_seed::fold_peaks(*sample, overview_peak); // the scan finishes
		run_until(r, r.clock + 50);
		const browser::WaveformDrawStats& s = browser_scene().waveform_draw_stats();
		expect(s.draws - before.draws).to_equal(1);
		to_be_at_least(expect(s.redraws - before.redraws), 1);
		expect(s.complete - before.complete).to_equal(1);
		expect(s.give_ups - before.give_ups).to_equal(0);
		expect(browser_scene().waveform_redraw_pending()).to_be_false();
	});

	it("counts one give-up when the card stops resolving columns for the give-up time", _ {
		CleanBrowser clean;
		ScratchCard card;
		spec::SongFixture f = spec::make_synth_song();
		PreviewHeld held(make_overview_sample(16));
		Rig r;
		r.open_and_land(sound_context(f), BrowserId::Sample);
		const browser::WaveformDrawStats before = browser_scene().waveform_draw_stats();
		browser_scene().preview_settled(true);
		run_until(r, r.clock + 50);
		expect(browser_scene().waveform_redraw_pending()).to_be_true();
		run_until(r, r.clock + 400);
		const browser::WaveformDrawStats& s = browser_scene().waveform_draw_stats();
		expect(s.give_ups - before.give_ups).to_equal(1);
		expect(s.complete - before.complete).to_equal(0);
		expect(browser_scene().waveform_redraw_pending()).to_be_false();
	});

	it("asks again for a zoomed view after giving up on the last one", _ {
		// A new view counts its own columns: the give-up at the old window does not carry to the new one.
		CleanBrowser clean;
		ScratchCard card;
		spec::SongFixture f = spec::make_synth_song();
		PreviewHeld held(make_overview_sample(0));
		Rig r;
		r.open_and_land(sound_context(f), BrowserId::Sample);
		const int32_t give_ups = browser_scene().waveform_draw_stats().give_ups;
		browser_scene().preview_settled(true);
		run_until(r, r.clock + 50);
		run_until(r, r.clock + 400);
		expect(browser_scene().waveform_draw_stats().give_ups - give_ups).to_equal(1);
		expect(browser_scene().waveform_redraw_pending()).to_be_false();
		r.button(kXEncX, kXEncY, true, r.clock + 10);
		r.step();
		r.horizontal(1);
		r.button(kXEncX, kXEncY, false, r.clock + 10);
		r.step();
		expect(browser_scene().waveform_view().zoomed_in()).to_be_true();
		run_until(r, r.clock + 50);
		expect(browser_scene().waveform_redraw_pending()).to_be_true();
	});

	it("counts a zoomed view's first frame as a draw of that view, not a redraw of the last", _ {
		// The zoomed view's columns are finer than the overview's, so its read is pending too; a frame after its first
		// is its own redraw.
		CleanBrowser clean;
		ScratchCard card;
		spec::SongFixture f = spec::make_synth_song();
		PreviewHeld held(make_overview_sample(16));
		Rig r;
		r.open_and_land(sound_context(f), BrowserId::Sample);
		browser_scene().preview_settled(true);
		run_until(r, r.clock + 50);
		expect(browser_scene().waveform_redraw_pending()).to_be_true();
		r.button(kXEncX, kXEncY, true, r.clock + 10);
		const browser::WaveformDrawStats before = browser_scene().waveform_draw_stats();
		r.horizontal(1); // the zoomed view's first frame
		expect(browser_scene().waveform_view().zoomed_in()).to_be_true();
		expect(browser_scene().waveform_draw_stats().redraws - before.redraws).to_equal(0);
		expect(browser_scene().waveform_draw_stats().complete - before.complete).to_equal(0);
		r.button(kXEncX, kXEncY, false, r.clock + 10);
		r.step(); // the zoomed view drawn again
		to_be_at_least(expect(browser_scene().waveform_draw_stats().redraws - before.redraws), 1);
	});

	it("drops a pending redraw when the preview gives its sample up", _ {
		CleanBrowser clean;
		ScratchCard card;
		spec::SongFixture f = spec::make_synth_song();
		PreviewHeld held(make_overview_sample(16));
		Rig r;
		r.open_and_land(sound_context(f), BrowserId::Sample);
		browser_scene().preview_settled(true);
		run_until(r, r.clock + 50);
		expect(browser_scene().waveform_redraw_pending()).to_be_true();
		held.range->sampleHolder.audioFile = nullptr;
		deluge::base::notify::raise(deluge::base::notify::Dirty::GridMain);
		run_until(r, r.clock + 50);
		expect(browser_scene().waveform_redraw_pending()).to_be_false();
	});

	it("returns to the root state, blinking again, when the preview settles with no sample", _ {
		CleanBrowser clean;
		ScratchCard card;
		spec::SongFixture f = spec::make_synth_song();
		PreviewHeld held(make_overview_sample());
		Rig r;
		// The drum creator's door: no `beginScreen`, so the open's preview stops the browser's own blink.
		browser_scene().set_drum_creator_next(true);
		r.open_and_land(sound_context(f), BrowserId::Sample);
		browser_scene().preview_settled(true);
		run_until(r, r.clock + 500);
		const size_t from = r.fake.flashes_sent.size();
		run_until(r, r.clock + 1000);
		expect(flashes_since(r, from).empty()).to_be_true(); // no blink over the waveform
		browser_scene().preview_settled(false);
		expect(browser_scene().pads()).to_equal(BrowserPads::Root);
		run_until(r, r.clock + 1000);
		expect(flashes_since(r, from).empty()).to_be_false();
		expect(browser_scene().browse_blink()).to_equal(browser::BrowseBlink::Browser);
	});

	it("keeps the sound editor's 200 ms BROWSE blink over the open's own preview", _ {
		// `main` previews inside `openUI`, before `beginScreen` arms the file selector's blink: that preview stops nothing.
		CleanBrowser clean;
		ScratchCard card;
		spec::SongFixture f = spec::make_synth_song();
		PreviewHeld held(make_overview_sample());
		Rig r;
		r.open_and_land(sound_context(f), BrowserId::Sample);
		(void)fake_browser_host().take_preview();
		browser_scene().preview_settled(true);
		run_until(r, r.clock + 50);
		expect(browser_scene().pads()).to_equal(BrowserPads::Waveform);
		const size_t from = r.fake.flashes_sent.size();
		run_until(r, r.clock + 1000);
		const std::vector<deluge::ui::widgets::grid::PadFlash> seen = flashes_since(r, from);
		expect(seen.size()).to_be_between(4, 5);
		for (const deluge::ui::widgets::grid::PadFlash& fl : seen) {
			expect(fl.x).to_equal(0);
			expect(fl.y).to_equal(5);
		}
	});

	it("stops the BROWSE blink on a move off a shown preview, and a preview with no sample brings back the 400 ms one", _ {
		// `currentFileChanged`'s `unsetTimer` on a move while a preview shows; `clearPreviewDisplay`'s
		// `possiblySetUpBlinking` when the next item previews nothing.
		CleanBrowser clean;
		ScratchCard card;
		spec::SongFixture f = spec::make_synth_song();
		PreviewHeld held(make_overview_sample());
		Rig r;
		r.open_and_land(sound_context(f), BrowserId::Sample);
		(void)fake_browser_host().take_preview();
		browser_scene().preview_settled(true);
		run_until(r, r.clock + 50);
		r.turn(1, r.clock + 10);
		r.settle();
		expect(browser_scene().browse_blink()).to_equal(browser::BrowseBlink::Off);
		size_t from = r.fake.flashes_sent.size();
		run_until(r, r.clock + 1000);
		expect(flashes_since(r, from).empty()).to_be_true();
		(void)fake_browser_host().take_preview();
		browser_scene().preview_settled(false);
		expect(browser_scene().browse_blink()).to_equal(browser::BrowseBlink::Browser);
		from = r.fake.flashes_sent.size();
		run_until(r, r.clock + 1000);
		const size_t n = flashes_since(r, from).size();
		expect(n).to_be_between(2, 3);
	});

	it("stops the BROWSE blink when a later preview lands", _ {
		// The `unsetTimer` of main's `SampleBrowser::previewIfPossible`
		// (`origin/main:src/deluge/gui/ui/browser/sample_browser.cpp`): only the open's own first preview is spared.
		CleanBrowser clean;
		ScratchCard card;
		spec::SongFixture f = spec::make_synth_song();
		PreviewHeld held(make_overview_sample());
		Rig r;
		r.open_and_land(sound_context(f), BrowserId::Sample);
		select_folder(r, true); // a move onto a folder: no preview showed, so the blink runs on
		expect(browser_scene().browse_blink()).to_equal(browser::BrowseBlink::Menu);
		select_folder(r, false);
		(void)fake_browser_host().take_preview();
		browser_scene().preview_settled(true);
		expect(browser_scene().browse_blink()).to_equal(browser::BrowseBlink::Off);
	});

	it("stops the BROWSE blink when a pad reveals the keyboard, and KEYBOARD leaves it running", _ {
		// `padAction`'s and `horizontalEncoderAction`'s reveals unset the timer; `buttonAction`'s KEYBOARD arm does not.
		CleanBrowser clean;
		ScratchCard card;
		spec::SongFixture f = spec::make_synth_song();
		{
			Rig r;
			r.open_and_land(sound_context(f), BrowserId::Sample);
			r.click(kKeyboardX, kKeyboardY);
			r.settle();
			expect(browser_scene().keyboard_always_visible()).to_be_true();
			expect(browser_scene().browse_blink()).to_equal(browser::BrowseBlink::Menu);
			const size_t from = r.fake.flashes_sent.size();
			run_until(r, r.clock + 1000);
			expect(flashes_since(r, from).empty()).to_be_false();
		}
		CleanBrowser::reset();
		{
			Rig r;
			r.open_and_land(sound_context(f), BrowserId::Sample);
			tap(r, 3, 3);
			expect(browser_scene().browse_blink()).to_equal(browser::BrowseBlink::Off);
			const size_t from = r.fake.flashes_sent.size();
			run_until(r, r.clock + 1000);
			expect(flashes_since(r, from).empty()).to_be_true();
		}
		CleanBrowser::reset();
		{
			Rig r;
			r.open_and_land(sound_context(f), BrowserId::Sample);
			r.horizontal(1);
			expect(browser_scene().browse_blink()).to_equal(browser::BrowseBlink::Off);
		}
	});

	it("opens the drum creator's browser on the browser's own 400 ms blink", _ {
		// `enterDrumCreator` opens the browser with no `beginScreen` after it: `possiblySetUpBlinking` only.
		CleanBrowser clean;
		ScratchCard card;
		spec::SongFixture f = spec::make_synth_song();
		Rig r;
		browser_scene().set_drum_creator_next(true);
		r.open_and_land(sound_context(f), BrowserId::Sample);
		expect(browser_scene().browse_blink()).to_equal(browser::BrowseBlink::Browser);
		const size_t from = r.fake.flashes_sent.size();
		run_until(r, r.clock + 1000);
		const size_t n = flashes_since(r, from).size();
		expect(n).to_be_between(2, 3);
	});

	it("declares SYNTH and KIT from the context and turns CROSS_SCREEN, SESSION and SCALE off", _ {
		CleanBrowser clean;
		ScratchCard card;
		{
			spec::SongFixture f = spec::make_synth_song();
			Rig r;
			r.open_and_land(sound_context(f), BrowserId::Sample);
			expect(led_on(r, ButtonId::Synth)).to_be_true();
			expect(led_on(r, ButtonId::Kit)).to_be_false();
			expect(led_on(r, ButtonId::CrossScreen)).to_be_false();
			expect(led_on(r, ButtonId::Session)).to_be_false();
			expect(led_on(r, ButtonId::Scale)).to_be_false();
			expect(r.fake.leds().desired_declared[static_cast<size_t>(ButtonId::Scale)]).to_be_true();
		}
		CleanBrowser::reset();
		{
			spec::SongFixture f = spec::make_kit_song();
			(void)spec::add_drum<SoundDrum>(f);
			Rig r;
			r.open_and_land(sound_context(f), BrowserId::Sample);
			expect(led_on(r, ButtonId::Synth)).to_be_false();
			expect(led_on(r, ButtonId::Kit)).to_be_true();
		}
		CleanBrowser::reset();
		{
			spec::AudioSongFixture f = spec::make_audio_clip_song();
			Rig r;
			r.open_and_land(context_of(f), BrowserId::Sample);
			expect(led_on(r, ButtonId::Synth)).to_be_false();
			expect(led_on(r, ButtonId::Kit)).to_be_false();
		}
	});

	it("the mute column exits and stops the preview", _ {
		CleanBrowser clean;
		ScratchCard card;
		spec::SongFixture f = spec::make_synth_song();
		Rig r;
		r.open_and_land(sound_context(f), BrowserId::Sample);
		(void)fake_browser_host().take_preview();
		r.pad(kDisplayWidth, 0, 64, r.clock + 10);
		r.step();
		expect(r.browser_on_stack()).to_be_false();
		expect(fake_browser_host().take_preview()).to_equal(PreviewRequest{.path = "", .direction = 0});
	});

	it("keeps the MIDI definition browser on its keyboard", _ {
		CleanBrowser clean;
		ScratchCard card;
		spec::SongFixture f = spec::make_midi_song();
		Rig r;
		r.open_and_land(context_of(f), BrowserId::MidiDeviceDefinition);
		expect(browser_scene().pads()).to_equal(BrowserPads::Keyboard);
		expect(browser_scene().pad_regions()).to_equal(deluge::ui::shell::PadRegions::Main);
		browser_scene().preview_settled(false);
		expect(browser_scene().pads()).to_equal(BrowserPads::Keyboard);
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

		// `Browser::goIntoFolderImpl`: the folder name is appended to `currentDir`.
		expect(std::string{browser_scene().current_dir()}).to_equal(std::string{"SAMPLES/KICKS"});
		expect(catalogue_order()).to_equal(std::string("KICK1.WAV,KICK2 WITH A LONG ROOM TAIL.WAV"));
		expect(std::string{browser_scene().entered_text()}).to_equal(std::string{"KICK1"});

		// BACK ascends. `goUpOneDirectoryLevelImpl` assigns the segment it dropped to `enteredText`
		// and lists starting at it (`browser.cpp`), so the folder just left is selected.
		r.button(kBackX, kBackY, true, r.clock + 10);
		r.step();
		expect(browser_scene().listing_in_flight()).to_be_true();
		r.land();
		expect(std::string{browser_scene().current_dir()}).to_equal(std::string{"SAMPLES"});
		expect(std::string{browser_scene().entered_text()}).to_equal(std::string{"KICKS"});
		expect(browser_scene().file_index()).to_equal(2);
	});

	it("enters a folder on a SELECT press and release, whatever the menu does with Select", _ {
		// `Browser::mainButtonAction` acts on the release through `handle()`; the menu's own
		// release edge (`Scene::select_acts_on_release`) is not this scene's.
		CleanBrowser clean;
		ScratchCard card;
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		Rig r;
		r.open_and_land(context_of(f), BrowserId::Sample);
		r.turn(2, r.clock + 10);
		r.step();
		r.button(kSelectEncButtonX, kSelectEncButtonY, true, r.clock + 10);
		r.step();
		expect(browser_scene().listing_in_flight()).to_be_false();
		r.button(kSelectEncButtonX, kSelectEncButtonY, false, r.clock + 10);
		r.step();
		expect(browser_scene().listing_in_flight()).to_be_true();
		r.land();
		expect(std::string{browser_scene().current_dir()}).to_equal(std::string{"SAMPLES/KICKS"});
	});

	it("closes with the card's error when the folder it enters has gone, without falling back to SAMPLES", _ {
		// `goIntoFolder` lists with no default folder, so a folder that vanished under the browser fails the listing
		// and `onListingFailed` closes it; only the opening listing falls back.
		CleanBrowser clean;
		ScratchCard card;
		expect(deluge::io::mkdir("SAMPLES/GONE").has_value()).to_be_true();
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		Rig r;
		r.open_and_land(context_of(f), BrowserId::Sample);
		expect(catalogue_order()).to_equal(std::string("CLAP.WAV,CLIPS,GONE,KICKS,SNARE.WAV,TOM.WAV"));

		r.turn(2, r.clock + 10);
		r.step();
		expect(std::string{browser_scene().entered_text()}).to_equal(std::string{"GONE"});
		expect(deluge::io::unlink("SAMPLES/GONE").has_value()).to_be_true();
		r.enter();
		r.land();

		expect(r.browser_on_stack()).to_be_false();
		expect(deluge::io::Directory::open("SAMPLES/GONE").has_value()).to_be_false();
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

		// "Don't allow user to go into TEMP clips folder" (`SampleBrowser::enterKeyPress` in
		// `origin/main:src/deluge/gui/ui/browser/sample_browser.cpp`): the popup is raised, the directory does not
		// change, and no listing is dispatched.
		DirectCtx d(r.clock);
		(void)browser_scene().handle(UiEvent{.kind = UiEvent::Kind::Button, .on = true, .button = ButtonId::SelectEnc},
		                             d.ctx);
		const Result refused = browser_scene().handle(
		    UiEvent{.kind = UiEvent::Kind::Button, .on = false, .button = ButtonId::SelectEnc}, d.ctx);
		const ShellAction* popup = find_action(refused, ShellAction::Kind::Popup);
		expect(popup).not_().to_be_null();
		expect(popup != nullptr ? static_cast<int>(popup->popup_string) : -1)
		    .to_equal(static_cast<int>(String::STRING_FOR_TEMP_FOLDER_CANT_BE_BROWSED));
		expect(browser_scene().listing_in_flight()).to_be_false();
		expect(std::string{browser_scene().current_dir()}).to_equal(std::string{"SAMPLES/CLIPS"});

		// The MIDI descriptor has no folder refusal at all
		// (`LoadMidiDeviceDefinitionUI::enterKeyPress` in `load_midi_device_definition_ui.cpp`).
		expect(browser::descriptor(BrowserId::MidiDeviceDefinition).folder_refusal).to_be_null();
	});

	it("closes on BACK at the browser's own top level", _ {
		CleanBrowser clean;
		ScratchCard card;
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		Rig r;
		r.open_and_land(context_of(f), BrowserId::Sample);

		// `goUpOneDirectoryLevel`'s synchronous root test (`browser.cpp`): "SAMPLES" has
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
		// `Browser::clampFileSelectionAndScroll`: the selected item is below the
		// viewport, so it moves to the LAST visible row -- 4 - 3 + 1.
		expect(browser_scene().scroll()).to_equal(2);

		// Past the end with nothing culled: legacy wraps to 0 (`Browser::selectEncoderAction` in `browser.cpp`).
		r.turn(1, r.clock + 10);
		r.step();
		expect(browser_scene().file_index()).to_equal(0);
		expect(browser_scene().scroll()).to_equal(0);
		expect(std::string{browser_scene().entered_text()}).to_equal(std::string{"CLAP"});

		// ...and below 0 it wraps to the end (`Browser::selectEncoderAction`).
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

		// `QwertyUI::padAction`'s normal-key arm (`qwerty_ui.cpp`) appends the character and calls
		// `Browser::predictExtendedText`, which searches the cached window
		// and selects what it finds (`browser.cpp`).
		r.type('S');
		expect(browser_scene().file_index()).to_equal(3);
		// The found entry's name REPLACES the typed text, and the cursor stays where the typing left
		// it -- which is what `text_edit` draws as the prediction highlight.
		expect(std::string{browser_scene().entered_text()}).to_equal(std::string{"SNARE"});
		expect(browser_scene().cursor()).to_equal(1);

		// A second character that already matches the prediction just steps over it
		// (`QwertyUI::padAction` in `qwerty_ui.cpp`), leaving the selection alone.
		r.type('N');
		expect(std::string{browser_scene().entered_text()}).to_equal(std::string{"SNARE"});
		expect(browser_scene().cursor()).to_equal(2);
		expect(browser_scene().file_index()).to_equal(3);

		// Backspace shortens the search and re-runs it (`processBackspace`, `qwerty_ui.cpp`).
		r.backspace();
		expect(browser_scene().cursor()).to_equal(1);
		expect(browser_scene().file_index()).to_equal(3);
	});

	it("repeats backspace every 80 ms after a 500 ms hold, and refuses other input meanwhile", _ {
		CleanBrowser clean;
		ScratchCard card;
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		Rig r;
		r.open_and_land(context_of(f), BrowserId::Sample);
		r.type(std::string_view{"SNARE"});
		const int32_t start = browser_scene().cursor();
		expect(start).to_equal(5);
		// `QwertyUI::padAction` deletes at the press and arms a 500 ms timer; `timerCallback` repeats every 80 ms.
		r.pad_backspace(true, 1000);
		r.tick(1000);
		expect(browser_scene().cursor()).to_equal(start - 1);
		r.tick(1499);
		expect(browser_scene().cursor()).to_equal(start - 1);
		r.tick(1500);
		expect(browser_scene().cursor()).to_equal(start - 2);
		r.tick(1579);
		expect(browser_scene().cursor()).to_equal(start - 2);
		r.tick(1580);
		expect(browser_scene().cursor()).to_equal(start - 3); // the press, then 500 and 580 ms
		r.tick(1650);
		expect(browser_scene().cursor()).to_equal(start - 3);
		// UI_MODE_HOLDING_BACKSPACE refuses a key, Enter, BACK, SELECT and the sidebar.
		const std::string dir{browser_scene().current_dir()};
		DirectCtx d;
		const Result back = browser_scene().handle(
		    UiEvent{.kind = UiEvent::Kind::Button, .on = true, .button = ButtonId::Back}, d.ctx);
		const Result select = browser_scene().handle(
		    UiEvent{.kind = UiEvent::Kind::Button, .on = true, .button = ButtonId::SelectEnc}, d.ctx);
		const Result sidebar = browser_scene().handle(
		    UiEvent{.kind = UiEvent::Kind::Pad, .x = kDisplayWidth, .y = 3, .velocity = 64}, d.ctx);
		expect(back.handled).to_be_true();
		expect(back.actions.empty()).to_be_true();
		expect(select.handled).to_be_true();
		expect(select.actions.empty()).to_be_true();
		expect(sidebar.handled).to_be_true();
		expect(sidebar.actions.empty()).to_be_true();
		const Pad key = pad_for('A');
		r.pad(static_cast<uint8_t>(key.x), static_cast<uint8_t>(key.y), 64, 1651);
		r.tick(1651);
		r.pad(static_cast<uint8_t>(key.x), static_cast<uint8_t>(key.y), 0, 1652);
		r.tick(1652);
		const Pad enter = pad_for(deluge::ui::widgets::grid::kTextKeyboardEnter);
		r.pad(static_cast<uint8_t>(enter.x), static_cast<uint8_t>(enter.y), 64, 1653);
		r.tick(1653);
		r.pad(static_cast<uint8_t>(enter.x), static_cast<uint8_t>(enter.y), 0, 1654);
		r.tick(1654);
		expect(browser_scene().cursor()).to_equal(start - 3); // neither the key nor Enter acted
		r.button(kBackX, kBackY, true, 1655);
		r.tick(1655);
		r.button(kBackX, kBackY, false, 1656);
		r.tick(1656);
		expect(browser_scene().current_dir()).to_equal(dir);
		expect(r.browser_on_stack()).to_be_true();
		// The repeat stops when the pad lifts.
		r.pad_backspace(false, 1700);
		r.tick(1700);
		const int32_t stopped = browser_scene().cursor();
		r.tick(2500);
		expect(browser_scene().cursor()).to_equal(stopped);
		// ...and the keys type again.
		r.type('A');
		expect(browser_scene().cursor()).to_equal(stopped + 1);
	});

	it("neither deletes nor holds backspace while Select is held, as QwertyUI::padAction's mode gate", _ {
		// `QwertyUI::padAction`: `else if (!currentUIMode)` covers both `processBackspace` and the hold.
		CleanBrowser clean;
		ScratchCard card;
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		Rig r;
		r.open_and_land(context_of(f), BrowserId::Sample);
		r.type(std::string_view{"SNARE"});
		const int32_t start = browser_scene().cursor();
		const uint32_t down = press_select(r);
		expect(browser_scene().holding()).to_be_true();
		r.pad_backspace(true, down + 10);
		r.tick(down + 10);
		expect(browser_scene().cursor()).to_equal(start);
		// Past the hold's 500 ms, still nothing: no repeat was armed.
		r.tick(down + 400);
		expect(browser_scene().cursor()).to_equal(start);
		r.pad_backspace(false, down + 420);
		r.tick(down + 420);
	});

	it("asks to be woken for the next backspace repeat", _ {
		CleanBrowser clean;
		ScratchCard card;
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		Rig r;
		r.open_and_land(context_of(f), BrowserId::Sample);
		r.type(std::string_view{"SNARE"});
		r.pad_backspace(true, 1000);
		r.tick(1000);
		const std::optional<uint32_t> first = browser_scene().next_timer();
		expect(first.has_value()).to_be_true();
		to_be_at_most(expect(first.value()), deluge::ui::widgets::grid::kBackspaceRepeatDelayMs);
		r.tick(1500);
		const std::optional<uint32_t> next = browser_scene().next_timer();
		expect(next.has_value()).to_be_true();
		to_be_at_most(expect(next.value()), deluge::ui::widgets::grid::kBackspaceRepeatMs);
		r.pad_backspace(false, 1520);
		r.tick(1520);
	});

	it("keeps repeating when a preview lands mid-hold, until the pad lifts", _ {
		CleanBrowser clean;
		ScratchCard card;
		spec::SongFixture f = spec::make_synth_song();
		PreviewHeld held(make_overview_sample());
		Rig r;
		r.open_and_land(sound_context(f), BrowserId::Sample);
		r.type(std::string_view{"SNARE"});
		r.turn(1, r.clock + 10);
		r.step();
		r.horizontal(5);
		const int32_t start = browser_scene().cursor();
		expect(start).to_equal(3);
		r.pad_backspace(true, r.clock + 1000);
		const uint32_t at = r.clock + 1000;
		r.tick(at);
		browser_scene().preview_settled(true);
		r.tick(at + 10);
		expect(browser_scene().pads()).to_equal(BrowserPads::Waveform);
		r.tick(at + 500);
		expect(browser_scene().cursor()).to_equal(start - 2); // the waveform over the keys does not end the hold
		r.pad_backspace(false, at + 520);
		r.tick(at + 520);
		r.tick(at + 1200);
		expect(browser_scene().cursor()).to_equal(start - 2);
	});

	it("arms the hold from a blind backspace under a waveform", _ {
		CleanBrowser clean;
		ScratchCard card;
		spec::SongFixture f = spec::make_synth_song();
		PreviewHeld held(make_overview_sample());
		Rig r;
		r.open_and_land(sound_context(f), BrowserId::Sample);
		r.type(std::string_view{"SNARE"});
		r.turn(1, r.clock + 10);
		r.step();
		r.horizontal(5);
		browser_scene().preview_settled(true);
		r.step();
		expect(browser_scene().pads()).to_equal(BrowserPads::Waveform);
		const int32_t start = browser_scene().cursor();
		const uint32_t at = r.clock + 1000;
		r.pad_backspace(true, at);
		r.tick(at);
		expect(browser_scene().cursor()).to_equal(start - 1);
		r.tick(at + 500);
		expect(browser_scene().cursor()).to_equal(start - 2);
		r.pad_backspace(false, at + 520);
		r.tick(at + 520);

	});

	it("enters the hold from a first backspace press on keys that are not drawn", _ {
		CleanBrowser clean;
		ScratchCard card;
		spec::SongFixture f = spec::make_synth_song();
		Rig hidden;
		hidden.open_and_land(sound_context(f), BrowserId::Sample);
		hidden.pad_backspace(true, hidden.clock + 100);
		hidden.step();
		const std::string dir{browser_scene().current_dir()};
		hidden.button(kBackX, kBackY, true, hidden.clock + 10);
		hidden.step();
		hidden.button(kBackX, kBackY, false, hidden.clock + 10);
		hidden.step();
		expect(std::string{browser_scene().current_dir()}).to_equal(dir);
		expect(hidden.browser_on_stack()).to_be_true();
		hidden.pad_backspace(false, hidden.clock + 10);
		hidden.step();
	});

	it("re-arms the repeat 80 ms after the callback that ran it, not after the schedule", _ {
		CleanBrowser clean;
		ScratchCard card;
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		Rig r;
		r.open_and_land(context_of(f), BrowserId::Sample);
		r.type(std::string_view{"SNARE"});
		const int32_t start = browser_scene().cursor();
		const uint32_t at = r.clock + 1000;
		r.pad_backspace(true, at);
		r.tick(at);
		r.tick(at + 530); // 30 ms late
		expect(browser_scene().cursor()).to_equal(start - 2);
		r.tick(at + 580); // the schedule said now; the callback said 610
		expect(browser_scene().cursor()).to_equal(start - 2);
		r.tick(at + 610);
		expect(browser_scene().cursor()).to_equal(start - 3);
		r.pad_backspace(false, at + 620);
		r.tick(at + 620);
	});

	it("neither scrolls nor zooms a waveform while backspace is held", _ {
		CleanBrowser clean;
		ScratchCard card;
		spec::SongFixture f = spec::make_synth_song();
		PreviewHeld held(make_overview_sample());
		Rig r;
		r.open_and_land(sound_context(f), BrowserId::Sample);
		browser_scene().preview_settled(true);
		run_until(r, r.clock + 50);
		r.button(kXEncX, kXEncY, true, r.clock + 10);
		r.step();
		r.horizontal(1);
		r.button(kXEncX, kXEncY, false, r.clock + 10);
		r.step();
		expect(browser_scene().waveform_view().zoomed_in()).to_be_true();
		r.pad_backspace(true, r.clock + 10);
		r.step();
		const int64_t scroll = browser_scene().waveform_view().scroll();
		r.horizontal(1);
		expect(browser_scene().waveform_view().scroll()).to_equal(scroll);
		r.pad_backspace(false, r.clock + 10);
		r.step();
		browser_scene().preview_settled(true);
		run_until(r, r.clock + 50);
	});

	it("types across folders and files alike, and moves the viewport to the match", _ {
		CleanBrowser clean;
		ScratchCard card;
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		Rig r;
		r.open_and_land(context_of(f), BrowserId::Sample);

		// "KICKS" is a folder at index 2, past the first visible row: the search selects it AND
		// `clampFileSelectionAndScroll` brings it on screen (`browser.cpp`).
		r.type('K');
		expect(browser_scene().file_index()).to_equal(2);
		expect(std::string{browser_scene().entered_text()}).to_equal(std::string{"KICKS"});
		expect(browser_scene().scroll()).to_equal(0); // three rows fit 0..2 already

		// A character no entry starts with is refused: `predictExtendedText`'s `notFound` arm with
		// `mayDefaultToBrandNewNameOnEntry` false puts the selected file's name back
		// (`Browser::continuePrediction` in `browser.cpp`) and `padAction` undoes the cursor advance (`qwerty_ui.cpp`).
		r.type('Q');
		expect(std::string{browser_scene().entered_text()}).to_equal(std::string{"KICKS"});
		expect(browser_scene().file_index()).to_equal(2);
		expect(browser_scene().cursor()).to_equal(1);
	});

	it("reverts the cursor when a search that needed a re-read still finds nothing", _ {
		// Legacy decrements `enteredTextEditPos` whenever
		// `predictExtendedText()` answers false (`QwertyUI::padAction` in `qwerty_ui.cpp`), and that includes the
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
		// (`Browser::continuePrediction` in `browser.cpp`) and the scene dispatches a re-read rather than answering.
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
		// The sample descriptor interprets note names, so the listing
		// leaves `CLIPS` ordered C3, A3, C4 -- and `searchFileItems` is a binary search over exactly
		// that order. The comparator's globals live in `util/name_compare.h`, which this layer may
		// not include, so the scene calls `FileCatalogue::repinComparator()` before every search
		// instead of relying on nothing having disturbed them since the sort.
		//
		// The window is real, not hypothetical: the op is dispatched and the result is
		// collected a tick later, so anything else sharing these globals in between -- a legacy
		// browser, a save UI's default-name search -- re-pins them under the scene. The deferred
		// dispatcher lets that window be entered exactly.
		CleanBrowser clean;
		ScratchCard card;
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		// `SampleBrowser::opened`'s first arm: the holder's path decides where the browser opens and
		// which entry it lands on (`SampleBrowser::opened`).
		f.clip->setSampleFilePath("SAMPLES/CLIPS/A3.WAV");

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
		// `QwertyUI::padAction` (`qwerty_ui.cpp`): "First character must be alphanumerical". The cursor is at 0 on
		// arrival (`Browser::applyListingSelection` in `browser.cpp`), so this is the first character.
		expect(browser_scene().cursor()).to_equal(0);
		r.type('.');
		expect(browser_scene().cursor()).to_equal(0);
		expect(std::string{browser_scene().entered_text()}).to_equal(std::string{"CLAP"});
	});
});

describe browser_accept("scenes::browser::BrowserScene accepting a file", $ {
	it("hands the chosen sample to its host and waits for its load", _ {
		CleanBrowser clean;
		ScratchCard card;
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		Rig r;
		r.open_and_land(context_of(f), BrowserId::Sample);
		(void)fake_browser_host().take_choice(); // forget any choice recorded so far, so the one below is the press's

		// `SampleBrowser::enterKeyPress`'s file arm is `claimCurrentFile()`, which loads with the browser up under the
		// working animation and closes it once the file is in. The claim names `soundEditor`, `AudioEngine`, `slicer`,
		// `PadLEDs` and `instrumentClipView`, behind the `scenes-includes-outside` wall, so the descriptor hands the
		// path to the host and the browser waits for its answer.
		Error unused = Error::NONE;
		expect(static_cast<int>(
		           browser::descriptor(BrowserId::Sample).accept(context_of(f), "SAMPLES/CLAP.WAV", unused)))
		    .to_equal(static_cast<int>(AcceptOutcome::DeferAndWait));

		r.enter();
		expect(r.browser_on_stack()).to_be_true();
		expect(browser_scene().awaiting_deferred_load()).to_be_true();

		const std::optional<browser::BrowserChoice> choice = fake_browser_host().take_choice();
		expect(choice.has_value()).to_be_true();
		// Read through defaults rather than through `choice->`: a mutant that asks the host for nothing must fail this
		// example, not abort the process inside `std::optional::operator->`.
		expect(static_cast<int>(choice.has_value() ? choice->id : BrowserId::Count))
		    .to_equal(static_cast<int>(BrowserId::Sample));
		expect(choice.has_value() ? choice->path : std::string{}).to_equal(std::string{"SAMPLES/CLAP.WAV"});

		// One load at a time: a second Enter while it is out asks for nothing.
		r.enter();
		expect(fake_browser_host().take_choice().has_value()).to_be_false();

		browser_scene().deferred_load_done(true); // the claim's completion
		r.step();
		r.step();
		expect(r.browser_on_stack()).to_be_false();
	});

	it("stays up for another file when the sample's load fails", _ {
		CleanBrowser clean;
		ScratchCard card;
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		Rig r;
		r.open_and_land(context_of(f), BrowserId::Sample);
		(void)fake_browser_host().take_choice(); // forget any choice recorded so far, so the one below is the press's

		r.enter();
		expect(fake_browser_host().take_choice().has_value()).to_be_true();
		// `claimCurrentFile`'s error arm: the animation goes, the error shows, the browser stays.
		browser_scene().deferred_load_done(false);
		r.step();
		r.step();
		expect(r.browser_on_stack()).to_be_true();
		expect(browser_scene().awaiting_deferred_load()).to_be_false();

		// And it takes the next file.
		r.enter();
		expect(fake_browser_host().take_choice().has_value()).to_be_true();
	});

	it("hands the chosen MIDI device definition to its host, and waits for its load", _ {
		CleanBrowser clean;
		ScratchCard card;
		spec::SongFixture f = spec::make_midi_song();
		auto* instrument = static_cast<MIDIInstrument*>(f.instrument);
		instrument->deviceDefinitionFileName.clear();

		Rig r;
		r.open_and_land(context_of(f), BrowserId::MidiDeviceDefinition);
		expect(std::string{browser_scene().entered_text()}).to_equal(std::string{"DEVICE1"});

		// The load is an instrument-load request on the interaction tier (`browser_host.cpp`'s
		// `requestMidiDefinitionLoad`), which the host makes, and `LoadMidiDeviceDefinitionUI` stays open until
		// `definitionLoaded` closes it. So the press hands the path to the host, loads nothing itself and waits.
		Error unused = Error::NONE;
		expect(static_cast<int>(browser::descriptor(BrowserId::MidiDeviceDefinition)
		                            .accept(context_of(f), "MIDI_DEVICES/DEFINITION/DEVICE1.XML", unused)))
		    .to_equal(static_cast<int>(AcceptOutcome::DeferAndWait));

		r.enter();
		expect(r.browser_on_stack()).to_be_true();
		expect(browser_scene().awaiting_deferred_load()).to_be_true();
		const std::optional<browser::BrowserChoice> choice = fake_browser_host().take_choice();
		expect(choice.has_value()).to_be_true();
		if (choice.has_value()) {
			expect(choice->id).to_equal(BrowserId::MidiDeviceDefinition);
		}
		expect(choice.has_value() ? choice->path : std::string{})
		    .to_equal(std::string{"MIDI_DEVICES/DEFINITION/DEVICE1.XML"});
		// What the host then does with the choice is `browser_choice_spec.cpp`'s.
		expect(instrument->deviceDefinitionFileName).to_equal(std::string{});
		browser_scene().deferred_load_done(true); // the load's completion
		r.step();
		r.step();
		expect(r.browser_on_stack()).to_be_false();
	});

	it("searches for the remembered file with .XML appended again, as legacy does", _ {
		CleanBrowser clean;
		ScratchCard card;
		spec::SongFixture f = spec::make_midi_song();
		auto* instrument = static_cast<MIDIInstrument*>(f.instrument);
		// `setupForLoadingMidiDeviceDefinition` splits the remembered path into a directory and the
		// name to select (`load_midi_device_definition_ui.cpp`).
		instrument->setDeviceDefinitionFileName("MIDI_DEVICES/DEFINITION/DEVICE1.XML");

		std::string dir;
		std::string name;
		browser::descriptor(BrowserId::MidiDeviceDefinition).start_at(context_of(f), dir, name);
		expect(dir).to_equal(std::string{"MIDI_DEVICES/DEFINITION"});
		// Legacy appends ".XML" to a name that already carries it
		// (`LoadMidiDeviceDefinitionUI::setupForLoadingMidiDeviceDefinition` in `load_midi_device_definition_ui.cpp`),
		// so the search never matches and the browser opens on the folder's first entry, as on `main`. This folder
		// holds one file, so that entry is the same one.
		expect(name).to_equal(std::string{"DEVICE1.XML.XML"});

		Rig r;
		r.open_and_land(context_of(f), BrowserId::MidiDeviceDefinition);
		expect(browser_scene().file_index()).to_equal(0);
		expect(std::string{browser_scene().entered_text()}).to_equal(std::string{"DEVICE1"});
	});
});

/// @par Against main's own navigation
///
/// Every example here runs inputs through the shell into `BrowserScene` and compares where it lands
/// with where main's `Browser` landed after the same inputs on a freshly emptied catalogue over the
/// same card, as `browser_scene.tsv`'s `browse/<case>` rows and `pixels/browser_<case>.pbm` hold it:
/// `fileIndexSelected`, `scrollPosVertical`, `enteredText`, `enteredTextEditPos`,
/// `scrollPosHorizontal`, the directory where a case walks, and the 128x48 panel. That is what puts
/// main's `Browser::arrivedInNewFolder`, `selectEncoderAction`, `finishSelectEncoderAction`,
/// `clampFileSelectionAndScroll`, `goIntoFolderImpl` and `predictExtendedText` behind these examples.
describe browser_main_pixels("scenes::browser::BrowserScene against main's browser", $ {
	it("lists and selects the same entry as main's Browser::arrivedInNewFolder", _ {
		CleanBrowser clean;
		ScratchCard card;
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		expect(blank_start_path(f)).to_be_true();

		Rig r;
		r.open_and_land(context_of(f), BrowserId::Sample);
		const BrowserState port = port_state(r);
		expect(port.index).to_equal(0);

		// No input to replay: opening IS the input. Legacy lists the same folder for itself.
		expect_same(self, port, "open");
	});

	it("scrolls the same way as four turns of main's Browser::selectEncoderAction", _ {
		CleanBrowser clean;
		ScratchCard card;
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		expect(blank_start_path(f)).to_be_true();

		Rig r;
		r.open_and_land(context_of(f), BrowserId::Sample);
		for (int i = 0; i < 4; i++) {
			r.turn(1, r.clock + 10);
			r.step();
		}
		r.settle();
		const BrowserState port = port_state(r);
		// A different window (rows 2..4) and a different selected row, so the comparison is not
		// re-asserting the first example's frame.
		expect(port.scroll).to_equal(2);
		expect(port.index).to_equal(4);

		// Four detents, four `selectEncoderAction(+1)` calls -- the shell lowers one detent to one
		// select-encoder step, which is the mapping this comparison is also pinning.
		expect_same(self, port, "four_turns");
	});

	it("wraps the same way as main's Browser::selectEncoderAction past the ends", _ {
		CleanBrowser clean;
		ScratchCard card;
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		expect(blank_start_path(f)).to_be_true();

		Rig r;
		r.open_and_land(context_of(f), BrowserId::Sample);
		// One detent backwards from row 0: `selectEncoderAction`'s `newFileIndex < 0` arm
		// (`Browser::selectEncoderAction`). With nothing culled at either end it wraps to the last entry and
		// `clampFileSelectionAndScroll` re-derives the window -- the arm neither example above
		// reaches.
		r.turn(-1, r.clock + 10);
		r.step();
		r.settle();
		const BrowserState port = port_state(r);
		expect(port.index).to_equal(4);

		expect_same(self, port, "wrap_back");
	});

	it("predicts the same text as main's Browser::predictExtendedText after typing", _ {
		CleanBrowser clean;
		ScratchCard card;
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		expect(blank_start_path(f)).to_be_true();

		Rig r;
		r.open_and_land(context_of(f), BrowserId::Sample);
		r.type('S');
		r.settle();
		const BrowserState port = port_state(r);
		// Cursor mid-string is `drawTextForOLEDEditing`'s OTHER arm -- the inversion from the cursor
		// to the panel's right edge (`QwertyUI::drawTextForOLEDEditing`) -- so this compares a code path the
		// examples above do not reach.
		expect(port.cursor).to_equal(1);
		expect(static_cast<int>(port.text.size())).to_equal(5);

		// The SAME pad, by coordinate. Legacy maps it through `keyboardChars`
		// (`QwertyUI::padAction`) and the port through `text_keyboard_char_at`; feeding both the
		// coordinate rather than the character is what puts that mapping under the comparison too.
		expect_same(self, port, "type_s");
	});

	it("ascends the same way as main's Browser::goUpOneDirectoryLevelImpl, landing on the folder it left", _ {
		CleanBrowser clean;
		ScratchCard card;
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		expect(blank_start_path(f)).to_be_true();

		Rig r;
		r.open_and_land(context_of(f), BrowserId::Sample);
		for (int i = 0; i < 2; i++) {
			r.turn(1, r.clock + 10);
			r.step();
		}
		r.enter();
		r.land();
		r.button(kBackX, kBackY, true, r.clock + 10);
		r.step();
		r.button(kBackX, kBackY, false, r.clock + 10);
		r.land();
		const BrowserState port = port_state(r);
		expect(std::string{browser_scene().current_dir()}).to_equal(std::string{"SAMPLES"});
		// `goUpOneDirectoryLevelImpl` assigns the segment it dropped to `enteredText` and lists
		// starting at it (`Browser::goUpOneDirectoryLevelImpl`), so the folder just left is the selection.
		expect(port.text).to_equal(std::string{"KICKS"});

		expect_same(self, port, "ascend");
		expect(std::string{browser_scene().current_dir()}).to_equal(table().value("browse/ascend", "dir"));
	});

	it("descends into a folder the same way as main's Browser::goIntoFolderImpl", _ {
		CleanBrowser clean;
		ScratchCard card;
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		expect(blank_start_path(f)).to_be_true();

		Rig r;
		r.open_and_land(context_of(f), BrowserId::Sample);
		// Row 2 is KICKS/ (CLAP.WAV, CLIPS, KICKS, SNARE.WAV, TOM.WAV): two detents, then the enter
		// pad. Neither side is told the folder's name -- each reads it off its own selection.
		for (int i = 0; i < 2; i++) {
			r.turn(1, r.clock + 10);
			r.step();
		}
		r.enter();
		r.land();
		const BrowserState port = port_state(r);
		expect(std::string{browser_scene().current_dir()}).to_equal(std::string{"SAMPLES/KICKS"});
		expect(port.text).to_equal(std::string{"KICK1"});

		expect_same(self, port, "descend");
		// Main walked into the same folder for itself, through `Browser::goIntoFolderImpl`
		// rather than through a name this spec supplied.
		expect(std::string{browser_scene().current_dir()}).to_equal(table().value("browse/descend", "dir"));
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
			expect_golden(self, "ui/scenes/browser_sample_list_48.pbm", r.fake.pbm(0));
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
			expect_golden(self, "ui/scenes/browser_sample_list_64.pbm", r64.fake.pbm(0));
		}
	});

	it("draws the MIDI browser's own icon and title on both geometries", _ {
		{
			CleanBrowser clean;
			ScratchCard card;
			spec::SongFixture f = spec::make_midi_song();
			Rig r;
			r.open_and_land(context_of(f), BrowserId::MidiDeviceDefinition);
			expect_golden(self, "ui/scenes/browser_midi_list_48.pbm", r.fake.pbm(0));
		}
		{
			CleanBrowser clean;
			ScratchCard card;
			spec::SongFixture f = spec::make_midi_song();
			Rig r64{board_64(), profile_64()};
			r64.open_and_land(context_of(f), BrowserId::MidiDeviceDefinition);
			expect_golden(self, "ui/scenes/browser_midi_list_64.pbm", r64.fake.pbm(0));
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
			expect_golden(self, "ui/scenes/browser_typed_48.pbm", r.fake.pbm(0));
		}
		{
			CleanBrowser clean;
			ScratchCard card;
			spec::AudioSongFixture f = spec::make_audio_clip_song();
			Rig r64{board_64(), profile_64()};
			r64.open_and_land(context_of(f), BrowserId::Sample);
			r64.type('S');
			r64.settle();
			expect_golden(self, "ui/scenes/browser_typed_64.pbm", r64.fake.pbm(0));
		}
	});
});
describe browser_long_press("scenes::browser::BrowserScene holding Select", $ {
	it("enters on a release before 400 ms", _ {
		CleanBrowser clean;
		ScratchCard card;
		spec::SongFixture f = spec::make_synth_song();
		Rig r;
		r.open_and_land(sound_context(f), BrowserId::Sample);
		(void)fake_browser_host().take_choice();
		select_folder(r, false);
		const uint32_t down = press_select(r);
		run_until(r, down + 390);
		release_select(r, down + 399);
		expect(on_stack(r, context_menu::kContextMenuSceneKind)).to_be_null();
		expect(fake_browser_host().take_choice().has_value()).to_be_true();
	});

	it("keeps the BROWSE pad blinking under a folder menu opened in the root state", _ {
		// `main`'s blink timer runs on under the folder menu (`possiblySetUpBlinking`; the menu stops nothing).
		CleanBrowser clean;
		ScratchCard card;
		spec::SongFixture f = spec::make_synth_song();
		Rig r;
		r.open_and_land(sound_context(f), BrowserId::Sample);
		select_folder(r, true);
		browser_scene().preview_settled(false);
		const uint32_t down = press_select(r);
		run_until(r, down + 400);
		release_select(r, down + 450);
		expect(on_stack(r, context_menu::kContextMenuSceneKind)).not_().to_be_null();
		const size_t from = r.fake.flashes_sent.size();
		run_until(r, r.clock + 1000);
		const std::vector<deluge::ui::widgets::grid::PadFlash> seen = flashes_since(r, from);
		expect(seen.empty()).to_be_false();
		for (const deluge::ui::widgets::grid::PadFlash& fl : seen) {
			expect(fl.x).to_equal(0);
			expect(fl.y).to_equal(5);
		}
	});

	it("opens the synth's LOAD FILES menu at 400 ms, after which the release does nothing", _ {
		CleanBrowser clean;
		ScratchCard card;
		spec::SongFixture f = spec::make_synth_song();
		Rig r;
		r.open_and_land(sound_context(f), BrowserId::Sample);
		(void)fake_browser_host().take_choice();
		select_folder(r, false);
		const uint32_t down = press_select(r);
		run_until(r, down + 400);
		expect(on_stack(r, context_menu::kContextMenuSceneKind)).not_().to_be_null();
		expect(static_cast<int>(context_menu_scene().descriptor_id()))
		    .to_equal(static_cast<int>(context_menu::DescriptorId::SampleBrowserSynth));
		expect(browser_scene().holding()).to_be_false();
		release_select(r, down + 450);
		expect(on_stack(r, context_menu::kContextMenuSceneKind)).not_().to_be_null();
		expect(r.browser_on_stack()).to_be_true();
		expect(fake_browser_host().take_choice().has_value()).to_be_false();
	});

	it("exits the browser and its folder menu on a mute-column press, as Kit::padAction hands it to the browser", _ {
		CleanBrowser clean;
		ScratchCard card;
		spec::SongFixture f = spec::make_kit_song();
		(void)spec::add_drum<SoundDrum>(f);
		Rig r;
		r.open_and_land(sound_context(f), BrowserId::Sample);
		const uint32_t down = press_select(r);
		run_until(r, down + 410);
		expect(on_stack(r, context_menu::kContextMenuSceneKind)).not_().to_be_null();
		r.pad(kDisplayWidth, 2, 64, r.clock + 10);
		r.step();
		run_until(r, r.clock + 20);
		expect(on_stack(r, context_menu::kContextMenuSceneKind)).to_be_null();
		expect(r.browser_on_stack()).to_be_false();
	});

	it("refuses BACK and SELECT under the folder menu while an audition pad is held", _ {
		// `ContextMenu::buttonAction` acts only in `buttonAndPadActionUIModes`, which a held audition pad is not.
		CleanBrowser clean;
		ScratchCard card;
		spec::SongFixture f = spec::make_kit_song();
		(void)spec::add_drum<SoundDrum>(f);
		MenuContext ctx = sound_context(f);
		spec::HeldPorts held{ctx};
		Rig r;
		r.open_and_land(ctx, BrowserId::Sample);
		const uint32_t down = press_select(r);
		run_until(r, down + 410);
		expect(on_stack(r, context_menu::kContextMenuSceneKind)).not_().to_be_null();
		held.ui.auditioning_now = true;
		r.button(kBackX, kBackY, true, r.clock + 10);
		r.step();
		r.button(kBackX, kBackY, false, r.clock + 10);
		r.step();
		r.button(kSelectEncButtonX, kSelectEncButtonY, true, r.clock + 10);
		r.step();
		r.button(kSelectEncButtonX, kSelectEncButtonY, false, r.clock + 10);
		r.step();
		expect(on_stack(r, context_menu::kContextMenuSceneKind)).not_().to_be_null();
		expect(r.browser_on_stack()).to_be_true();
		expect(fake_browser_host().take_choice().has_value()).to_be_false(); // the refused SELECT started no import
		held.ui.auditioning_now = false;
	});

	it("leaves every other button to the UI beneath under the folder menu while an audition pad is held", _ {
		// `ContextMenu::buttonAction` refuses only BACK and SELECT; PLAY returns NOT_DEALT_WITH and falls through.
		CleanBrowser clean;
		ScratchCard card;
		spec::SongFixture f = spec::make_kit_song();
		(void)spec::add_drum<SoundDrum>(f);
		MenuContext ctx = sound_context(f);
		spec::HeldPorts held{ctx};
		Rig r;
		r.open_and_land(ctx, BrowserId::Sample);
		const uint32_t down = press_select(r);
		run_until(r, down + 410);
		expect(on_stack(r, context_menu::kContextMenuSceneKind)).not_().to_be_null();
		held.ui.auditioning_now = true;
		DirectCtx d;
		const Result play = context_menu_scene().handle(
		    UiEvent{.kind = UiEvent::Kind::Button, .on = true, .button = ButtonId::Play}, d.ctx);
		expect(play.handled).to_be_false();
		held.ui.auditioning_now = false;
	});

	it("ignores a Select turn while held", _ {
		CleanBrowser clean;
		ScratchCard card;
		spec::SongFixture f = spec::make_synth_song();
		Rig r;
		r.open_and_land(sound_context(f), BrowserId::Sample);
		const int32_t before = browser_scene().file_index();
		const uint32_t down = press_select(r);
		r.turn(1, down + 50);
		r.tick(down + 50);
		expect(browser_scene().file_index()).to_equal(before);
		expect(browser_scene().holding()).to_be_true();
	});

	it("opens nothing while a sample load runs, and refuses the release", _ {
		CleanBrowser clean;
		ScratchCard card;
		spec::SongFixture f = spec::make_synth_song();
		MenuContext ctx = sound_context(f);
		spec::HeldPorts loading{ctx};
		loading.card.sample_loading = true;
		Rig r;
		r.open_and_land(ctx, BrowserId::Sample);
		(void)fake_browser_host().take_choice();
		select_folder(r, false);
		const uint32_t down = press_select(r);
		run_until(r, down + 600);
		expect(on_stack(r, context_menu::kContextMenuSceneKind)).to_be_null();
		expect(browser_scene().holding()).to_be_false(); // the press was refused too
		release_select(r, down + 610);
		// `SampleBrowser::buttonAction`'s gate refuses every button while the load runs, the release included.
		expect(fake_browser_host().take_choice().has_value()).to_be_false();
		expect(r.browser_on_stack()).to_be_true();
	});

	it("refuses a key typed while a sample load runs", _ {
		CleanBrowser clean;
		ScratchCard card;
		spec::SongFixture f = spec::make_synth_song();
		MenuContext ctx = sound_context(f);
		spec::HeldPorts loading{ctx};
		loading.card.sample_loading = true;
		Rig r;
		r.open_and_land(ctx, BrowserId::Sample);
		const int32_t before = browser_scene().cursor();
		const std::string name{browser_scene().entered_text()};
		r.type(name.empty() ? 'C' : name[static_cast<size_t>(before)]);
		expect(browser_scene().cursor()).to_equal(before);
	});

	it("refuses a Select turn while a sample load runs", _ {
		CleanBrowser clean;
		ScratchCard card;
		spec::SongFixture f = spec::make_synth_song();
		MenuContext ctx = sound_context(f);
		spec::HeldPorts loading{ctx};
		loading.card.sample_loading = true;
		Rig r;
		r.open_and_land(ctx, BrowserId::Sample);
		const int32_t before = browser_scene().file_index();
		expect(static_cast<int32_t>(FileCatalogue::fileItems.size())).to_be_greater_than(1);
		// `Browser::selectEncoderAction` turns only with no UI mode set, and the load holds one.
		r.turn(1, r.clock + 10);
		r.settle();
		expect(browser_scene().file_index()).to_equal(before);
	});

	it("ignores a key typed while Select is held", _ {
		CleanBrowser clean;
		ScratchCard card;
		spec::SongFixture f = spec::make_synth_song();
		Rig r;
		r.open_and_land(sound_context(f), BrowserId::Sample);
		const int32_t before = browser_scene().cursor();
		const std::string name{browser_scene().entered_text()};
		(void)press_select(r);
		r.type(name.empty() ? 'C' : name[static_cast<size_t>(before)]);
		expect(browser_scene().cursor()).to_equal(before);
		expect(browser_scene().holding()).to_be_true();
	});

	it("enters when the release lands on the deadline's own tick", _ {
		CleanBrowser clean;
		ScratchCard card;
		spec::SongFixture f = spec::make_synth_song();
		Rig r;
		r.open_and_land(sound_context(f), BrowserId::Sample);
		(void)fake_browser_host().take_choice();
		select_folder(r, false);
		const uint32_t down = press_select(r);
		run_until(r, down + 390);
		// The tick routes its input before the scene answers the deadline, so the release unsets the hold first,
		// as main's release unsets its timer when the button task runs before the timer's.
		release_select(r, down + 400);
		expect(on_stack(r, context_menu::kContextMenuSceneKind)).to_be_null();
		expect(fake_browser_host().take_choice().has_value()).to_be_true();
	});

	it("closes a folder menu with the browser once Enter on the keyboard beneath has loaded the file", _ {
		CleanBrowser clean;
		ScratchCard card;
		spec::SongFixture f = spec::make_synth_song();
		Rig r;
		r.open_and_land(sound_context(f), BrowserId::Sample);
		(void)fake_browser_host().take_choice();
		select_folder(r, false);
		const uint32_t down = press_select(r);
		run_until(r, down + 410);
		release_select(r, down + 420);
		expect(on_stack(r, context_menu::kContextMenuSceneKind)).not_().to_be_null();
		r.enter();
		expect(fake_browser_host().take_choice().has_value()).to_be_true();
		// Both stay while the claim runs, as `claimCurrentFile` runs with them up ...
		expect(r.browser_on_stack()).to_be_true();
		expect(on_stack(r, context_menu::kContextMenuSceneKind)).not_().to_be_null();
		// ... and the closing browser takes the menu above it, as `close()` does.
		browser_scene().deferred_load_done(true);
		r.step();
		r.step();
		expect(r.browser_on_stack()).to_be_false();
		expect(on_stack(r, context_menu::kContextMenuSceneKind)).to_be_null();
	});

	it("ignores a load's end it did not start", _ {
		CleanBrowser clean;
		ScratchCard card;
		spec::SongFixture f = spec::make_synth_song();
		Rig r;
		r.open_and_land(sound_context(f), BrowserId::Sample);
		browser_scene().long_press_done(true);
		r.step();
		r.step();
		expect(r.browser_on_stack()).to_be_true();
	});

	it("tells an audio clip it cannot take a whole folder", _ {
		CleanBrowser clean;
		ScratchCard card;
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		Rig r;
		r.open_and_land(context_of(f), BrowserId::Sample);
		const uint32_t down = press_select(r);
		run_until(r, down + 410);
		expect(on_stack(r, context_menu::kContextMenuSceneKind)).to_be_null();
		expect(popup_text(r)).to_equal(
		    std::string{deluge::l10n::get(String::STRING_FOR_CANT_IMPORT_WHOLE_FOLDER_INTO_AUDIO_CLIP)});
	});

	it("tells a kit with more than one drum that only a brand-new kit takes a whole folder", _ {
		CleanBrowser clean;
		ScratchCard card;
		spec::SongFixture f = spec::make_kit_song();
		(void)spec::add_drum<SoundDrum>(f);
		(void)spec::add_drum<SoundDrum>(f);
		Rig r;
		r.open_and_land(sound_context(f), BrowserId::Sample);
		const uint32_t down = press_select(r);
		run_until(r, down + 410);
		expect(on_stack(r, context_menu::kContextMenuSceneKind)).to_be_null();
		expect(popup_text(r)).to_equal(
		    std::string{deluge::l10n::get(String::STRING_FOR_CAN_ONLY_IMPORT_WHOLE_FOLDER_INTO_BRAND_NEW_KIT)});
	});

	it("opens the SAMPLES menu on a kit's only drum, editing source 0", _ {
		CleanBrowser clean;
		ScratchCard card;
		spec::SongFixture f = spec::make_kit_song();
		(void)spec::add_drum<SoundDrum>(f);
		Rig r;
		r.open_and_land(sound_context(f), BrowserId::Sample);
		const uint32_t down = press_select(r);
		run_until(r, down + 410);
		expect(static_cast<int>(context_menu_scene().descriptor_id()))
		    .to_equal(static_cast<int>(context_menu::DescriptorId::SampleBrowserKit));
		expect(on_stack(r, context_menu::kContextMenuSceneKind)).not_().to_be_null();
	});

	it("tells a kit's only drum edited on source 1 that only a brand-new kit takes a whole folder", _ {
		CleanBrowser clean;
		ScratchCard card;
		spec::SongFixture f = spec::make_kit_song();
		(void)spec::add_drum<SoundDrum>(f);
		MenuContext ctx = context_of(f);
		menu::select_source(ctx, 1);
		(void)menu::ensure_first_range(ctx);
		Rig r;
		r.open_and_land(ctx, BrowserId::Sample);
		const uint32_t down = press_select(r);
		run_until(r, down + 410);
		expect(on_stack(r, context_menu::kContextMenuSceneKind)).to_be_null();
		expect(popup_text(r)).to_equal(
		    std::string{deluge::l10n::get(String::STRING_FOR_CAN_ONLY_IMPORT_WHOLE_FOLDER_INTO_BRAND_NEW_KIT)});
	});

	it("opens nothing for a ring-mod synth held on a folder, which has no option to offer", _ {
		CleanBrowser clean;
		ScratchCard card;
		spec::SongFixture f = spec::make_synth_song();
		ring_mod(*static_cast<SoundInstrument*>(f.instrument));
		Rig r;
		r.open_and_land(sound_context(f), BrowserId::Sample);
		select_folder(r, true);
		const uint32_t down = press_select(r);
		run_until(r, down + 410);
		expect(on_stack(r, context_menu::kContextMenuSceneKind)).to_be_null();
		expect(popup_text(r)).to_equal(std::string{});
		expect(browser_scene().holding()).to_be_false();
	});
	it("offers a ring-mod synth on a file only SINGLE-CYCLE and WAVETABLE, opening on the first", _ {
		CleanBrowser clean;
		ScratchCard card;
		spec::SongFixture f = spec::make_synth_song();
		ring_mod(*static_cast<SoundInstrument*>(f.instrument));
		Rig r;
		r.open_and_land(sound_context(f), BrowserId::Sample);
		select_folder(r, false);
		const uint32_t down = press_select(r);
		run_until(r, down + 410);
		expect(on_stack(r, context_menu::kContextMenuSceneKind)).not_().to_be_null();
		expect(context_menu_scene().available().size()).to_equal(size_t(2));
		expect(static_cast<int>(context_menu_scene().selected_option())).to_equal(2);
		release_select(r, down + 420);
		r.turn(1, r.clock + 10);
		r.settle();
		expect(static_cast<int>(context_menu_scene().selected_option())).to_equal(3);
	});

	it("types a pad pressed on the synth menu into the browser beneath, and the menu stays", _ {
		CleanBrowser clean;
		ScratchCard card;
		spec::SongFixture f = spec::make_synth_song();
		Rig r;
		r.open_and_land(sound_context(f), BrowserId::Sample);
		select_folder(r, false);
		const uint32_t down = press_select(r);
		run_until(r, down + 410);
		release_select(r, down + 420);
		// The cursor sits before the predicted name, so typing its next letter steps over it.
		const int32_t before = browser_scene().cursor();
		const std::string name{browser_scene().entered_text()};
		r.type(name.empty() ? 'C' : name[static_cast<size_t>(before)]);
		expect(on_stack(r, context_menu::kContextMenuSceneKind)).not_().to_be_null();
		expect(browser_scene().cursor()).to_equal(before + 1);
	});

	it("types nothing into the browser beneath a context menu that passes no pads, which the press closes", _ {
		CleanBrowser clean;
		ScratchCard card;
		spec::SongFixture f = spec::make_synth_song();
		Rig r;
		r.open_and_land(sound_context(f), BrowserId::Sample);
		context_menu_scene().open(sound_context(f));
		expect(r.shell.stack().push(SceneId{context_menu::kContextMenuSceneKind,
		                                    static_cast<uint16_t>(context_menu::DescriptorId::ResetMidiFollow)}))
		    .to_be_true();
		const int32_t before = browser_scene().cursor();
		const std::string text{browser_scene().entered_text()};
		r.type('C');
		// `ContextMenu::padAction` closes on the press and deals with it, so the browser it uncovers does not type it.
		expect(on_stack(r, context_menu::kContextMenuSceneKind)).to_be_null();
		expect(browser_scene().cursor()).to_equal(before);
		expect(std::string{browser_scene().entered_text()}).to_equal(text);
	});

	it("refuses BACK, Select and pads on a folder menu while a sample load runs", _ {
		spec::SongFixture f = spec::make_synth_song();
		MenuContext ctx = sound_context(f);
		spec::HeldPorts loading{ctx};
		loading.card.sample_loading = true;
		auto& menu = context_menu_scene();
		menu.open(ctx);
		expect(menu.on_enter(SceneId{context_menu::kContextMenuSceneKind,
		                             static_cast<uint16_t>(context_menu::DescriptorId::SampleBrowserSynth)}))
		    .to_be_true();
		DirectCtx d(0);
		for (const UiEvent& e : {UiEvent{.kind = UiEvent::Kind::Button, .on = true, .button = ButtonId::Back},
		                         UiEvent{.kind = UiEvent::Kind::Button, .on = true, .button = ButtonId::SelectEnc},
		                         UiEvent{.kind = UiEvent::Kind::Pad, .x = 3, .y = 3, .velocity = 64, .on = true}}) {
			const Result refused = menu.handle(e, d.ctx);
			expect(refused.handled).to_be_true();
			expect(refused.actions.empty()).to_be_true();
		}
		menu.on_exit();
	});
});

describe browser_slicer_doors("scenes::browser::BrowserScene the slicer's two doors", $ {
	it("the kit menu offers SLICE on a file and not on a folder", _ {
		CleanBrowser clean;
		ScratchCard card;
		spec::SongFixture f = spec::make_kit_song();
		(void)spec::add_drum<SoundDrum>(f);
		Rig r;
		r.open_and_land(sound_context(f), BrowserId::Sample);
		const context_menu::Descriptor& kit = context_menu::descriptor(context_menu::DescriptorId::SampleBrowserKit);
		expect(kit.options_now).not_().to_be_null();
		std::array<uint8_t, context_menu::kMaxOptions> out{};
		select_folder(r, false);
		expect(kit.options_now != nullptr ? kit.options_now(sound_context(f), out) : 0).to_equal(size_t(2));
		select_folder(r, true);
		expect(kit.options_now != nullptr ? kit.options_now(sound_context(f), out) : 0).to_equal(size_t(1));
		expect(static_cast<int>(out[0])).to_equal(0);
	});

	it("opens the slicer on the file the preview already holds without stopping it, as main's openUI(&slicer)", _ {
		// `SampleBrowser`'s SLICE door is `openUI(&slicer)` with the preview left sounding: nothing stops it.
		spec::SongFixture f = spec::make_kit_song();
		(void)spec::add_drum<SoundDrum>(f);
		// The spec's sample has no lease, so a stop's unbind would freeze on it (E220): the open must not unbind.
		Sample* const sample = make_overview_sample();
		const deluge::ui::scenes::spec::SliceHeld held{sample, "SAMPLES/HELD.WAV"};
		held.open();
		const auto* range = static_cast<const MultisampleRange*>(AudioEngine::sampleForPreview->getOrCreateFirstRange(0));
		expect(range->sampleHolder.audioFile).to_equal(sample);
		expect(deluge::gui::ui::shellSlicerBusy()).to_be_false();
		expect(deluge::ui::scenes::slicing::slicer_scene().has_sample()).to_be_true();
	});

	it("drops an open still loading when the slicer opens on the file the preview holds", _ {
		spec::SongFixture f = spec::make_kit_song();
		(void)spec::add_drum<SoundDrum>(f);
		// An open of another file, its load not yet run: the slicer waits on it.
		deluge::gui::ui::openShellSlice("SAMPLES/OTHER.WAV");
		expect(deluge::gui::ui::shellSlicerBusy()).to_be_true();
		const deluge::ui::scenes::spec::SliceHeld held{make_overview_sample(), "SAMPLES/HELD.WAV"};
		held.open();
		// The slicer is open on the held file, and nothing is left to land over it.
		expect(deluge::ui::scenes::slicing::slicer_scene().has_sample()).to_be_true();
		expect(deluge::gui::ui::shellSlicerBusy()).to_be_false();
	});

	it("SLICE asks the host for a slice of the selected file and keeps the menu", _ {
		CleanBrowser clean;
		ScratchCard card;
		spec::SongFixture f = spec::make_kit_song();
		(void)spec::add_drum<SoundDrum>(f);
		Rig r;
		r.open_and_land(sound_context(f), BrowserId::Sample);
		(void)fake_browser_host().take_choice();
		select_folder(r, false);
		const context_menu::Descriptor& kit = context_menu::descriptor(context_menu::DescriptorId::SampleBrowserKit);
		expect(kit.accept(sound_context(f), 1)).to_be_true();
		const std::optional<browser::BrowserChoice> choice = fake_browser_host().take_choice();
		expect(choice.has_value()).to_be_true();
		if (choice.has_value()) {
			expect(choice->op).to_equal(browser::BrowserChoice::Op::Slice);
			expect(choice->is_folder).to_be_false();
			expect(choice->path.empty()).to_be_false();
		}
	});

	it("Shift+Select on a file in a fresh kit asks the host for a slice and keeps the browser", _ {
		CleanBrowser clean;
		ScratchCard card;
		spec::SongFixture f = spec::make_kit_song();
		(void)spec::add_drum<SoundDrum>(f);
		Rig r;
		r.open_and_land(sound_context(f), BrowserId::Sample);
		(void)fake_browser_host().take_choice();
		select_folder(r, false);
		r.button(kShiftX, kShiftY, true, r.clock + 5);
		const uint32_t down = press_select(r);
		release_select(r, down + 20);
		r.settle();
		const std::optional<browser::BrowserChoice> choice = fake_browser_host().take_choice();
		expect(choice.has_value()).to_be_true();
		if (choice.has_value()) {
			expect(choice->op).to_equal(browser::BrowserChoice::Op::Slice);
		}
		expect(r.browser_on_stack()).to_be_true();
		expect(popup_text(r)).to_equal(std::string{});
	});

	it("Shift+Select in a kit that is not fresh says only a brand-new kit can use the slicer", _ {
		CleanBrowser clean;
		ScratchCard card;
		spec::SongFixture f = spec::make_kit_song();
		(void)spec::add_drum<SoundDrum>(f);
		(void)spec::add_drum<SoundDrum>(f);
		Rig r;
		r.open_and_land(sound_context(f), BrowserId::Sample);
		(void)fake_browser_host().take_choice();
		select_folder(r, false);
		r.button(kShiftX, kShiftY, true, r.clock + 5);
		const uint32_t down = press_select(r);
		release_select(r, down + 20);
		r.settle();
		expect(fake_browser_host().take_choice().has_value()).to_be_false();
		expect(popup_text(r)).to_equal(
		    std::string{deluge::l10n::get(String::STRING_FOR_CAN_ONLY_USE_SLICER_FOR_BRAND_NEW_KIT)});
	});

	it("Shift+Select on a synth claims the file as Select does", _ {
		CleanBrowser clean;
		ScratchCard card;
		spec::SongFixture f = spec::make_synth_song();
		Rig r;
		r.open_and_land(sound_context(f), BrowserId::Sample);
		(void)fake_browser_host().take_choice();
		select_folder(r, false);
		r.button(kShiftX, kShiftY, true, r.clock + 5);
		const uint32_t down = press_select(r);
		release_select(r, down + 20);
		r.settle();
		const std::optional<browser::BrowserChoice> choice = fake_browser_host().take_choice();
		expect(choice.has_value()).to_be_true();
		if (choice.has_value()) {
			expect(choice->op).to_equal(browser::BrowserChoice::Op::Enter);
		}
	});

	it("pushes the slicer the host opened, with SLICER on the Shift+Select door only", _ {
		CleanBrowser clean;
		ScratchCard card;
		spec::SongFixture f = spec::make_kit_song();
		(void)spec::add_drum<SoundDrum>(f);
		{
			Rig r;
			r.open_and_land(sound_context(f), BrowserId::Sample);
			(void)fake_browser_host().take_choice();
			select_folder(r, false);
			r.button(kShiftX, kShiftY, true, r.clock + 5);
			const uint32_t down = press_select(r);
			release_select(r, down + 20);
			r.settle();
			(void)fake_browser_host().take_choice();
			browser_scene().slice_ready(true);
			r.step(); // arms the browser's timer
			r.step(); // renders, and pushes
			expect(on_stack(r, slicing::kSlicerSceneKind)).not_().to_be_null();
			r.step(); // the popup the push filed
			expect(popup_text(r)).to_equal(std::string{"SLICER"});
		}
		{
			Rig r;
			r.open_and_land(sound_context(f), BrowserId::Sample);
			(void)fake_browser_host().take_choice();
			select_folder(r, false);
			(void)context_menu::descriptor(context_menu::DescriptorId::SampleBrowserKit).accept(sound_context(f), 1);
			(void)fake_browser_host().take_choice();
			browser_scene().slice_ready(true);
			r.step(); // arms the browser's timer
			r.step(); // renders, and pushes
			expect(on_stack(r, slicing::kSlicerSceneKind)).not_().to_be_null();
			expect(popup_text(r)).to_equal(std::string{});
		}
	});

	it("pushes nothing when the host could not open the slice, or had no slice out", _ {
		CleanBrowser clean;
		ScratchCard card;
		spec::SongFixture f = spec::make_kit_song();
		(void)spec::add_drum<SoundDrum>(f);
		Rig r;
		r.open_and_land(sound_context(f), BrowserId::Sample);
		(void)fake_browser_host().take_choice();
		browser_scene().slice_ready(true); // nothing out
		r.step();
		expect(on_stack(r, slicing::kSlicerSceneKind)).to_be_null();
		select_folder(r, false);
		(void)context_menu::descriptor(context_menu::DescriptorId::SampleBrowserKit).accept(sound_context(f), 1);
		(void)fake_browser_host().take_choice();
		browser_scene().slice_ready(false);
		r.step();
		expect(on_stack(r, slicing::kSlicerSceneKind)).to_be_null();
		expect(r.browser_on_stack()).to_be_true();
	});
});
describe browser_keyboard("scenes::browser::BrowserScene the keyboard on demand, the encoders, KEYBOARD and LOAD", $ {
	it("the first main-pad press reveals the keyboard and types, unless it is on the favourites row", _ {
		// `SampleBrowser::padAction`: the reveal puts the cursor at the start, and passes the press on only below
		// `favouriteRow`.
		CleanBrowser clean;
		ScratchCard card;
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		{
			Rig r;
			r.open_and_land(context_of(f), BrowserId::Sample);
			r.pad(9, 7, 64, r.clock + 10);
			r.step();
			expect(browser_scene().pads()).to_equal(BrowserPads::Keyboard);
			expect(std::string{browser_scene().entered_text()}).to_equal(std::string{"CLAP"});
			expect(browser_scene().cursor()).to_equal(0);
		}
		CleanBrowser::reset();
		{
			Rig r;
			r.open_and_land(context_of(f), BrowserId::Sample);
			const Pad t = pad_for('T');
			expect(t.y).to_be_less_than(browser::kFavouriteRow);
			r.pad(static_cast<uint8_t>(t.x), static_cast<uint8_t>(t.y), 64, r.clock + 10);
			r.step();
			expect(browser_scene().pads()).to_equal(BrowserPads::Keyboard);
			expect(std::string{browser_scene().entered_text()}).to_equal(std::string{"TOM"});
		}
	});

	it("a selection move hides the keyboard unless it is always visible", _ {
		// `SampleBrowser::currentFileChanged`: a move clears `qwertyVisible` unless `qwertyAlwaysVisible`; what the
		// preview then shows replaces the keys (`clearPreviewDisplay` here: the move lands on a folder).
		CleanBrowser clean;
		ScratchCard card;
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		{
			Rig r;
			r.open_and_land(context_of(f), BrowserId::Sample);
			r.pad(9, 7, 64, r.clock + 10);
			r.step();
			expect(browser_scene().pads()).to_equal(BrowserPads::Keyboard);
			r.turn(1, r.clock + 10);
			r.step();
			browser_scene().preview_settled(false);
			expect(browser_scene().pads()).to_equal(BrowserPads::Root);
			expect(static_cast<uint64_t>(browser_scene().greyout_mask().cols)).to_equal(uint64_t{0xFFFFFFFE});
		}
		CleanBrowser::reset();
		{
			Rig r;
			r.open_and_land(context_of(f), BrowserId::Sample);
			r.click(kKeyboardX, kKeyboardY);
			r.turn(1, r.clock + 10);
			r.step();
			browser_scene().preview_settled(false);
			expect(browser_scene().pads()).to_equal(BrowserPads::Keyboard);
		}
	});

	it("keeps the keyboard over a typed search, which moves the selection without a direction", _ {
		CleanBrowser clean;
		ScratchCard card;
		spec::SongFixture f = spec::make_synth_song();
		PreviewHeld held(make_overview_sample());
		Rig r;
		r.open_and_land(sound_context(f), BrowserId::Sample);
		r.type('T');
		browser_scene().preview_settled(true);
		expect(browser_scene().pads()).to_equal(BrowserPads::Keyboard);
	});

	it("KEYBOARD toggles always-visible and its LED, on the press", _ {
		CleanBrowser clean;
		ScratchCard card;
		spec::SongFixture f = spec::make_synth_song();
		Rig r;
		r.open_and_land(sound_context(f), BrowserId::Sample);
		expect(browser_scene().keyboard_always_visible()).to_be_false();
		r.button(kKeyboardX, kKeyboardY, true, r.clock + 10);
		r.step();
		expect(browser_scene().keyboard_always_visible()).to_be_true();
		expect(browser_scene().pads()).to_equal(BrowserPads::Keyboard);
		expect(led_on(r, ButtonId::Keyboard)).to_be_true();
		r.button(kKeyboardX, kKeyboardY, false, r.clock + 10);
		r.step();
		expect(browser_scene().keyboard_always_visible()).to_be_true();
		r.click(kKeyboardX, kKeyboardY);
		expect(browser_scene().keyboard_always_visible()).to_be_false();
		expect(led_on(r, ButtonId::Keyboard)).to_be_false();
	});

	it("LOAD toggles auto-load and its LED on the release, but not on an audio clip", _ {
		CleanBrowser clean;
		ScratchCard card;
		{
			spec::SongFixture f = spec::make_synth_song();
			Rig r;
			r.open_and_land(sound_context(f), BrowserId::Sample);
			r.button(kLoadX, kLoadY, true, r.clock + 10);
			r.step();
			expect(browser_scene().auto_load()).to_be_false();
			r.button(kLoadX, kLoadY, false, r.clock + 10);
			r.step();
			expect(browser_scene().auto_load()).to_be_true();
			expect(led_on(r, ButtonId::Load)).to_be_true();
			r.click(kLoadX, kLoadY);
			expect(browser_scene().auto_load()).to_be_false();
			expect(led_on(r, ButtonId::Load)).to_be_false();
		}
		CleanBrowser::reset();
		{
			spec::AudioSongFixture f = spec::make_audio_clip_song();
			Rig r;
			r.open_and_land(context_of(f), BrowserId::Sample);
			r.click(kLoadX, kLoadY);
			expect(browser_scene().auto_load()).to_be_false();
			expect(led_on(r, ButtonId::Load)).to_be_false();
		}
	});

	it("with auto-load on, a landed preview files a claim that keeps the browser open; a stop files none", _ {
		// The auto-load of main's `SampleBrowser::previewIfPossible`
		// (`origin/main:src/deluge/gui/ui/browser/sample_browser.cpp`): the landed file is claimed as Select would,
		// keeping the browser open.
		CleanBrowser clean;
		ScratchCard card;
		spec::SongFixture f = spec::make_synth_song();
		Rig r;
		r.open_and_land(sound_context(f), BrowserId::Sample);
		r.click(kLoadX, kLoadY);
		(void)fake_browser_host().take_preview();
		browser_scene().preview_settled(true);
		const std::optional<browser::BrowserChoice> choice = fake_browser_host().take_choice();
		expect(choice.has_value()).to_be_true();
		if (choice.has_value()) {
			expect(choice->keep_open).to_be_true();
			expect(choice->op).to_equal(browser::BrowserChoice::Op::Enter);
		}
		expect(choice.has_value() ? choice->path : std::string{}).to_equal(std::string{"SAMPLES/CLAP.WAV"});
		expect(r.browser_on_stack()).to_be_true();
		select_folder(r, true);
		browser_scene().preview_settled(false);
		expect(fake_browser_host().take_choice().has_value()).to_be_false();
	});

	it("zooms with X_ENC held, scrolls once zoomed in, and otherwise reveals the keyboard", _ {
		// `SampleBrowser::horizontalEncoderAction`.
		CleanBrowser clean;
		ScratchCard card;
		spec::SongFixture f = spec::make_synth_song();
		PreviewHeld held(make_overview_sample());
		Rig r;
		r.open_and_land(sound_context(f), BrowserId::Sample);
		browser_scene().preview_settled(true);
		run_until(r, r.clock + 50);
		const std::string zoomed_out = r.fake.state();
		r.button(kXEncX, kXEncY, true, r.clock + 10);
		r.step();
		r.horizontal(1);
		r.button(kXEncX, kXEncY, false, r.clock + 10);
		r.step();
		r.settle();
		expect(browser_scene().pads()).to_equal(BrowserPads::Waveform);
		expect(browser_scene().waveform_view().zoomed_in()).to_be_true();
		expect(r.fake.state()).not_().to_equal(zoomed_out);
		const int64_t scroll = browser_scene().waveform_view().scroll();
		r.horizontal(1);
		r.settle();
		expect(browser_scene().waveform_view().scroll()).not_().to_equal(scroll);
		expect(browser_scene().pads()).to_equal(BrowserPads::Waveform);
		r.button(kXEncX, kXEncY, true, r.clock + 10);
		r.step();
		r.horizontal(-8);
		r.button(kXEncX, kXEncY, false, r.clock + 10);
		r.step();
		expect(browser_scene().waveform_view().zoomed_in()).to_be_false();
		r.horizontal(1);
		expect(browser_scene().pads()).to_equal(BrowserPads::Keyboard);
		expect(browser_scene().cursor()).to_equal(1);
	});

	it("opens every landed preview zoomed out, even one whose sample sits where the last one did", _ {
		// The `waveformBasicNavigator.opened()` of main's `SampleBrowser::previewIfPossible`
		// (`origin/main:src/deluge/gui/ui/browser/sample_browser.cpp`): a freed sample's memory can hold the next.
		CleanBrowser clean;
		ScratchCard card;
		spec::SongFixture f = spec::make_synth_song();
		PreviewHeld held(make_overview_sample());
		Rig r;
		r.open_and_land(sound_context(f), BrowserId::Sample);
		browser_scene().preview_settled(true);
		run_until(r, r.clock + 50);
		r.button(kXEncX, kXEncY, true, r.clock + 10);
		r.step();
		r.horizontal(1);
		r.button(kXEncX, kXEncY, false, r.clock + 10);
		r.step();
		expect(browser_scene().waveform_view().zoomed_in()).to_be_true();
		browser_scene().preview_settled(true);
		run_until(r, r.clock + 50);
		expect(browser_scene().waveform_view().zoomed_in()).to_be_false();
	});

	it("redraws a zoomed view the card has not caught up with, after the last view's redraw gave up", _ {
		// `tickPreviewRedraw` counts a new render's columns afresh: the zoom's render is its own.
		CleanBrowser clean;
		ScratchCard card;
		spec::SongFixture f = spec::make_synth_song();
		PreviewHeld held(make_overview_sample(16));
		Rig r;
		r.open_and_land(sound_context(f), BrowserId::Sample);
		browser_scene().preview_settled(true);
		run_until(r, r.clock + 500);
		if (r.shell.next_wake().has_value()) {
			expect(*r.shell.next_wake() - r.clock).to_be_greater_than(kListingPollMs);
		}
		r.button(kXEncX, kXEncY, true, r.clock + 10);
		r.step();
		r.horizontal(1);
		r.settle();
		expect(r.shell.next_wake().has_value()).to_be_true();
		if (r.shell.next_wake().has_value()) {
			to_be_at_most(expect(*r.shell.next_wake() - r.clock), kListingPollMs);
		}
		r.button(kXEncX, kXEncY, false, r.clock + 10);
		r.step();
	});

	it("side-scrolls the selected row's name while the cursor is at its start", _ {
		// `Browser::renderOLED` arms `OLED::setupSideScroller` for the selected row when `enteredTextEditPos` is 0.
		CleanBrowser clean;
		ScratchCard card;
		spec::SongFixture f = spec::make_synth_song();
		deluge::model::lastFilePathLoaded() = "SAMPLES/KICKS/KICK2 WITH A LONG ROOM TAIL.WAV";
		Rig r;
		r.open_and_land(sound_context(f), BrowserId::Sample);
		expect(std::string{browser_scene().entered_text()}).to_equal(std::string{"KICK2 WITH A LONG ROOM TAIL"});
		const std::string still = r.fake.pbm(0);
		run_until(r, r.clock + 1000);
		expect(r.fake.pbm(0)).not_().to_equal(still);
		r.horizontal(1); // the cursor off the start: the row stops scrolling
		const std::string moved = r.fake.pbm(0);
		run_until(r, r.clock + 1000);
		expect(r.fake.pbm(0)).to_equal(moved);
	});
});
describe browser_scroll("scenes::browser::BrowserScene asking for the horizontal scroll-in", $ {
	it("files a scroll toward the waveform once a moved-to preview lands and the waveform is drawn", _ {
		// Main's `SampleBrowser::previewIfPossible` (`origin/main:src/deluge/gui/ui/browser/sample_browser.cpp`) with a
		// move: the waveform, rendered into the image store, scrolls in.
		CleanBrowser clean;
		ScratchCard card;
		spec::SongFixture f = spec::make_synth_song();
		PreviewHeld held(make_overview_sample());
		Rig r;
		r.open_and_land(sound_context(f), BrowserId::Sample);
		browser_scene().preview_settled(false); // the file it opened on: nothing to show
		r.turn(1, r.clock + 10);
		r.step();
		expect(browser_scene().take_scroll().has_value()).to_be_false(); // nothing showed: no scroll out
		browser_scene().preview_settled(true);
		expect(browser_scene().scroll_pending()).to_be_true();
		const Result oled_only = browser_scene().after_render(); // a tick that drew no pads
		expect(browser_scene().take_scroll().has_value()).to_be_false(); // the waveform is not on the grid yet
		expect(find_action(oled_only, ShellAction::Kind::RepaintAll)).not_().to_be_null();
		deluge::base::notify::raise(deluge::base::notify::Dirty::GridMain); // the repaint that after_render asked for
		run_until(r, r.clock + 30);
		expect(browser_scene().take_scroll()).to_equal(ScrollRequest{.direction = 1});
		expect(browser_scene().scroll_pending()).to_be_false();
	});

	it("files a scroll out at once when the selection moves off a waveform, and not off the keyboard alone", _ {
		// `finishSelectEncoderAction` hides the keyboard before `currentFileChanged` asks whether a preview or the
		// keyboard shows, so only a waveform scrolls out while the next preview loads.
		CleanBrowser clean;
		ScratchCard card;
		spec::SongFixture f = spec::make_synth_song();
		PreviewHeld held(make_overview_sample());
		Rig r;
		r.open_and_land(sound_context(f), BrowserId::Sample);
		browser_scene().preview_settled(false);
		r.pad(9, 7, 64, r.clock + 10);
		r.step();
		r.turn(1, r.clock + 10);
		r.step();
		expect(browser_scene().take_scroll().has_value()).to_be_false();
		browser_scene().preview_settled(true);
		run_until(r, r.clock + 30);
		(void)browser_scene().take_scroll();
		r.turn(-1, r.clock + 10);
		r.step();
		expect(browser_scene().take_scroll()).to_equal(ScrollRequest{.direction = -1, .into_nothing = true});
	});

	it("hides the keyboard on entering a folder unless it is always visible", _ {
		// `Browser::arrivedInNewFolder`.
		CleanBrowser clean;
		ScratchCard card;
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		Rig r;
		r.open_and_land(context_of(f), BrowserId::Sample);
		select_folder(r, true);
		r.pad(9, 7, 64, r.clock + 10);
		r.step();
		const uint32_t down = press_select(r);
		release_select(r, down + 20);
		r.land();
		browser_scene().preview_settled(false);
		expect(browser_scene().pads()).to_equal(BrowserPads::Root);
	});

	it("files a scroll to the root view when a move lands on nothing to show", _ {
		// `clearPreviewDisplay` with a move.
		CleanBrowser clean;
		ScratchCard card;
		spec::SongFixture f = spec::make_synth_song();
		PreviewHeld held(make_overview_sample());
		Rig r;
		r.open_and_land(sound_context(f), BrowserId::Sample);
		browser_scene().preview_settled(true);
		run_until(r, r.clock + 30);
		(void)browser_scene().take_scroll();
		select_folder(r, true);
		(void)browser_scene().take_scroll(); // the scroll out
		browser_scene().preview_settled(false);
		const std::optional<ScrollRequest> scroll = browser_scene().take_scroll();
		expect(scroll.has_value()).to_be_true();
		if (scroll.has_value()) {
			expect(scroll->to_root).to_be_true();
			expect(scroll->into_nothing).to_be_false();
		}
		expect(browser_scene().pads()).to_equal(BrowserPads::Root);
	});

	it("drops a waveform scroll the keyboard overtook before it was drawn", _ {
		// Otherwise the scroll stays owed, and the host keeps the main columns from the browser for good.
		CleanBrowser clean;
		ScratchCard card;
		spec::SongFixture f = spec::make_synth_song();
		PreviewHeld held(make_overview_sample());
		Rig r;
		r.open_and_land(sound_context(f), BrowserId::Sample);
		browser_scene().preview_settled(false);
		r.turn(1, r.clock + 10);
		r.step();
		browser_scene().preview_settled(true);
		expect(browser_scene().scroll_pending()).to_be_true();
		DirectCtx d;
		(void)browser_scene().handle(UiEvent{.kind = UiEvent::Kind::Button, .on = true, .button = ButtonId::Keyboard},
		                             d.ctx);
		expect(browser_scene().scroll_pending()).to_be_false();
		run_until(r, r.clock + 30);
		expect(browser_scene().take_scroll().has_value()).to_be_false();
	});

	it("drops a waveform scroll the root view overtook before it was drawn", _ {
		// A preview stopped with no move (the file deleted) returns the pads to the root view, leaving nothing to scroll.
		CleanBrowser clean;
		ScratchCard card;
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		PreviewHeld held(make_overview_sample());
		Rig r;
		r.open_and_land(context_of(f), BrowserId::Sample);
		browser_scene().preview_settled(false);
		r.turn(-1, r.clock + 10); // onto CLAP from wherever the turn wraps
		r.step();
		r.turn(1, r.clock + 10);
		r.step();
		browser_scene().preview_settled(true);
		expect(browser_scene().scroll_pending()).to_be_true();
		browser_scene().file_deleted(std::string{"SAMPLES/"} + std::string{browser_scene().entered_text()} + ".WAV");
		browser_scene().preview_settled(false);
		expect(browser_scene().pads()).to_equal(BrowserPads::Root);
		run_until(r, r.clock + 30);
		expect(browser_scene().scroll_pending()).to_be_false();
		expect(browser_scene().take_scroll().has_value()).to_be_false();
	});

	it("files none with the keyboard always visible, nor on open", _ {
		CleanBrowser clean;
		ScratchCard card;
		spec::SongFixture f = spec::make_synth_song();
		PreviewHeld held(make_overview_sample());
		Rig r;
		r.open_and_land(sound_context(f), BrowserId::Sample);
		browser_scene().preview_settled(true);
		run_until(r, r.clock + 30);
		expect(browser_scene().take_scroll().has_value()).to_be_false();
		r.click(kKeyboardX, kKeyboardY);
		r.turn(1, r.clock + 10);
		r.step();
		browser_scene().preview_settled(true);
		run_until(r, r.clock + 30);
		expect(browser_scene().take_scroll().has_value()).to_be_false();
		expect(browser_scene().scroll_pending()).to_be_false();
	});
});
describe browser_record_delete("scenes::browser::BrowserScene RECORD and Shift+SAVE", $ {
	it("RECORD replaces the browser with the recorder and is claimed; its release is claimed too", _ {
		// `SampleBrowser::buttonAction`'s RECORD arm: `changeUISideways(&audioRecorder)` on the press.
		CleanBrowser clean;
		ScratchCard card;
		spec::SongFixture f = spec::make_synth_song();
		Rig r;
		r.open_and_land(sound_context(f), BrowserId::Sample);
		DirectCtx d;
		const Result pressed = browser_scene().handle(
		    UiEvent{.kind = UiEvent::Kind::Button, .on = true, .button = ButtonId::Record}, d.ctx);
		expect(pressed.handled).to_be_true();
		const ShellAction* removed = find_action(pressed, ShellAction::Kind::Remove);
		const ShellAction* pushed = find_action(pressed, ShellAction::Kind::Push);
		expect(removed).not_().to_be_null();
		if (removed != nullptr) {
			expect(removed->pop_to).to_equal(kBrowserSceneKind);
		}
		expect(pushed).not_().to_be_null();
		if (pushed != nullptr) {
			expect(pushed->id.kind).to_equal(recorder::kRecorderSceneKind);
		}
		const Result released = browser_scene().handle(
		    UiEvent{.kind = UiEvent::Kind::Button, .on = false, .button = ButtonId::Record}, d.ctx);
		expect(released.handled).to_be_true();
		expect(released.actions.empty()).to_be_true();
	});

	it("takes RECORD and does nothing while an audition pad is held", _ {
		// `SampleBrowser::buttonAction`'s RECORD arm returns at once under any UI mode, and a held audition pad is
		// `UI_MODE_AUDITIONING`: the browser stays and no take starts. The audition column is the view's, so the mode
		// is legacy's fact, not a pad the guest holds.
		CleanBrowser clean;
		ScratchCard card;
		spec::SongFixture f = spec::make_synth_song();
		MenuContext ctx = sound_context(f);
		spec::HeldPorts held{ctx};
		held.ui.auditioning_now = true;
		Rig r;
		r.open_and_land(ctx, BrowserId::Sample);
		DirectCtx d;
		const Result pressed = browser_scene().handle(
		    UiEvent{.kind = UiEvent::Kind::Button, .on = true, .button = ButtonId::Record}, d.ctx);
		expect(pressed.handled).to_be_true();
		expect(pressed.actions.empty()).to_be_true();
		const Result released = browser_scene().handle(
		    UiEvent{.kind = UiEvent::Kind::Button, .on = false, .button = ButtonId::Record}, d.ctx);
		expect(released.handled).to_be_true();
		expect(released.actions.empty()).to_be_true();
		// The audition pad up, the same press opens the recorder.
		held.ui.auditioning_now = false;
		const Result opened = browser_scene().handle(
		    UiEvent{.kind = UiEvent::Kind::Button, .on = true, .button = ButtonId::Record}, d.ctx);
		expect(find_action(opened, ShellAction::Kind::Push)).not_().to_be_null();
	});

	it("takes no BACK, SELECT, Select turn, key or mute-column press while an audition pad is held", _ {
		// `Browser::buttonAction`, `Browser::selectEncoderAction`, `QwertyUI::padAction` and `SampleBrowser::padAction`
		// each test `currentUIMode`, and a held audition pad is `UI_MODE_AUDITIONING`.
		CleanBrowser clean;
		ScratchCard card;
		spec::SongFixture f = spec::make_synth_song();
		MenuContext ctx = sound_context(f);
		spec::HeldPorts held{ctx};
		Rig r;
		r.open_and_land(ctx, BrowserId::Sample);
		const std::string dir{browser_scene().current_dir()};
		const int32_t index = browser_scene().file_index();
		held.ui.auditioning_now = true;
		DirectCtx d;
		Result taken[4];
		taken[0] = browser_scene().handle(
		    UiEvent{.kind = UiEvent::Kind::Button, .on = true, .button = ButtonId::Back}, d.ctx);
		taken[1] = browser_scene().handle(
		    UiEvent{.kind = UiEvent::Kind::Button, .on = true, .button = ButtonId::SelectEnc}, d.ctx);
		taken[2] = browser_scene().handle(
		    UiEvent{.kind = UiEvent::Kind::Encoder, .encoder = deluge::ui::widgets::core::EncoderRole::Select, .delta = 1}, d.ctx);
		taken[3] = browser_scene().handle(
		    UiEvent{.kind = UiEvent::Kind::Pad, .x = kDisplayWidth, .y = 3, .velocity = 64}, d.ctx);
		// BACK, SELECT, the Select turn and the mute column: each is claimed and does nothing.
		expect(taken[0].handled).to_be_true();
		expect(taken[0].actions.empty()).to_be_true();
		expect(taken[1].handled).to_be_true();
		expect(taken[1].actions.empty()).to_be_true();
		expect(taken[2].handled).to_be_true();
		expect(taken[2].actions.empty()).to_be_true();
		expect(taken[3].handled).to_be_true();
		expect(taken[3].actions.empty()).to_be_true();
		expect(browser_scene().holding()).to_be_false();
		const Result released = browser_scene().handle(
		    UiEvent{.kind = UiEvent::Kind::Button, .on = false, .button = ButtonId::SelectEnc}, d.ctx);
		expect(released.actions.empty()).to_be_true();
		// A key on the keyboard pads is not typed either.
		r.click(kKeyboardX, kKeyboardY);
		const std::string typed{browser_scene().entered_text()};
		r.type('T');
		r.backspace();
		r.enter();
		expect(std::string{browser_scene().entered_text()}).to_equal(typed);
		expect(std::string{browser_scene().current_dir()}).to_equal(dir);
		expect(browser_scene().file_index()).to_equal(index);
		expect(r.browser_on_stack()).to_be_true();
		held.ui.auditioning_now = false;
	});

	it("walks nothing while X_ENC is held", _ {
		CleanBrowser clean;
		ScratchCard card;
		spec::SongFixture f = spec::make_synth_song();
		Rig r;
		r.open_and_land(sound_context(f), BrowserId::Sample);
		const int32_t index = browser_scene().file_index();
		r.button(kXEncX, kXEncY, true, r.clock + 10);
		r.step();
		r.turn(1, r.clock + 10);
		r.step();
		r.button(kXEncX, kXEncY, false, r.clock + 10);
		r.step();
		expect(browser_scene().file_index()).to_equal(index);
	});

	it("leaves RECORD to the global RECORD while a foreground recording is live, and stays up", _ {
		// `SampleBrowser::buttonAction`'s RECORD arm needs `audioRecorder.recordingSource == NONE`.
		CleanBrowser clean;
		ScratchCard card;
		spec::SongFixture f = spec::make_synth_song();
		Rig r;
		r.open_and_land(sound_context(f), BrowserId::Sample);
		AudioEngine::setForegroundRecording(nullptr, AudioInputChannel::OUTPUT);
		DirectCtx d;
		const Result pressed = browser_scene().handle(
		    UiEvent{.kind = UiEvent::Kind::Button, .on = true, .button = ButtonId::Record}, d.ctx);
		AudioEngine::setForegroundRecording(nullptr, AudioInputChannel::NONE);
		expect(pressed.handled).to_be_false();
		expect(pressed.actions.empty()).to_be_true();
		expect(r.browser_on_stack()).to_be_true();
		expect(recorder::recorder_scene().recording()).to_be_false();
	});

	it("forgets the undo log as it opens, as SampleBrowser::opened does", _ {
		CleanBrowser clean;
		ScratchCard card;
		spec::SongFixture f = spec::make_synth_song();
		// `getNewAction` refuses unless the current UI is the root, so a root is set for the seed and taken down again.
		{
			struct RootGuard {
				RootGuard() { setRootUILowLevel(&instrumentClipView); }
				~RootGuard() { nullifyUIs(); }
				RootGuard(const RootGuard&) = delete;
				RootGuard& operator=(const RootGuard&) = delete;
			} root;
			actionLogger.deleteAllLogs();
			(void)actionLogger.getNewAction(ActionType::MISC, ActionAddition::NOT_ALLOWED);
			expect(actionLogger.firstAction[BEFORE]).not_().to_be_null();
		}
		Rig r;
		r.open_and_land(sound_context(f), BrowserId::Sample);
		expect(actionLogger.firstAction[BEFORE]).to_be_null();
	});

	it("leaves KEYBOARD's and LOAD's LEDs as the view left them until each is pressed", _ {
		// `SampleBrowser::opened` sets neither: over the keyboard screen KEYBOARD stays lit until its first press.
		CleanBrowser clean;
		ScratchCard card;
		spec::SongFixture f = spec::make_synth_song();
		Rig r;
		r.open_and_land(sound_context(f), BrowserId::Sample);
		expect(r.fake.leds().desired_declared[static_cast<size_t>(ButtonId::Keyboard)]).to_be_false();
		expect(r.fake.leds().desired_declared[static_cast<size_t>(ButtonId::Load)]).to_be_false();
		r.click(kKeyboardX, kKeyboardY);
		expect(r.fake.leds().desired_declared[static_cast<size_t>(ButtonId::Keyboard)]).to_be_true();
		expect(r.fake.leds().desired_declared[static_cast<size_t>(ButtonId::Load)]).to_be_false();
		// `buttonAction` sets the LOAD LED on the release only: held, it is still the view's.
		r.button(kLoadX, kLoadY, true, r.clock + 10);
		r.step();
		expect(r.fake.leds().desired_declared[static_cast<size_t>(ButtonId::Load)]).to_be_false();
		r.button(kLoadX, kLoadY, false, r.clock + 10);
		r.step();
		expect(r.fake.leds().desired_declared[static_cast<size_t>(ButtonId::Load)]).to_be_true();
	});

	it("takes no BACK, RECORD or Shift+SAVE while Select is held, nor a Select press while X_ENC is", _ {
		// `currentUIMode` gates all of them in `main`: a Select hold and a held X_ENC are UI modes.
		CleanBrowser clean;
		ScratchCard card;
		spec::SongFixture f = spec::make_synth_song();
		Rig r;
		r.open_and_land(sound_context(f), BrowserId::Sample);
		const std::string dir{browser_scene().current_dir()};
		DirectCtx d;
		(void)browser_scene().handle(UiEvent{.kind = UiEvent::Kind::Button, .on = true, .button = ButtonId::SelectEnc},
		                             d.ctx);
		expect(browser_scene().holding()).to_be_true();
		for (const ButtonId b : {ButtonId::Back, ButtonId::Record}) {
			const Result pressed =
			    browser_scene().handle(UiEvent{.kind = UiEvent::Kind::Button, .on = true, .button = b}, d.ctx);
			expect(pressed.handled).to_be_true();
			expect(pressed.actions.empty()).to_be_true();
		}
		d.ctx.shift = true;
		const Result saved = browser_scene().handle(
		    UiEvent{.kind = UiEvent::Kind::Button, .on = true, .button = ButtonId::Save}, d.ctx);
		expect(saved.handled).to_be_true();
		expect(saved.actions.empty()).to_be_true();
		d.ctx.shift = false;
		expect(browser_scene().listing_in_flight()).to_be_false();
		expect(std::string{browser_scene().current_dir()}).to_equal(dir);
		(void)browser_scene().handle(UiEvent{.kind = UiEvent::Kind::Button, .on = false, .button = ButtonId::SelectEnc},
		                             d.ctx);
		expect(browser_scene().holding()).to_be_false();
		(void)browser_scene().handle(UiEvent{.kind = UiEvent::Kind::Button, .on = true, .button = ButtonId::XEnc},
		                             d.ctx);
		(void)browser_scene().handle(UiEvent{.kind = UiEvent::Kind::Button, .on = true, .button = ButtonId::SelectEnc},
		                             d.ctx);
		expect(browser_scene().holding()).to_be_false();
		(void)browser_scene().handle(UiEvent{.kind = UiEvent::Kind::Button, .on = false, .button = ButtonId::XEnc},
		                             d.ctx);
	});

	it("leaves RECORD to the global RECORD on an audio clip", _ {
		CleanBrowser clean;
		ScratchCard card;
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		Rig r;
		r.open_and_land(context_of(f), BrowserId::Sample);
		DirectCtx d;
		const Result pressed = browser_scene().handle(
		    UiEvent{.kind = UiEvent::Kind::Button, .on = true, .button = ButtonId::Record}, d.ctx);
		expect(pressed.handled).to_be_false();
	});

	it("Shift+SAVE on a file opens the delete menu, and on a folder does nothing", _ {
		// `Browser::buttonAction` -> `goIntoDeleteFileContextMenu`, on a file only (`SampleBrowser`'s own test).
		CleanBrowser clean;
		ScratchCard card;
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		Rig r;
		r.open_and_land(context_of(f), BrowserId::Sample);
		DirectCtx d; // SAVE alone is not the browser's
		expect(browser_scene()
		           .handle(UiEvent{.kind = UiEvent::Kind::Button, .on = true, .button = ButtonId::Save}, d.ctx)
		           .handled)
		    .to_be_false();
		r.button(kShiftX, kShiftY, true, r.clock + 5);
		r.button(kSaveX, kSaveY, true, r.clock + 10);
		r.step();
		r.button(kSaveX, kSaveY, false, r.clock + 10);
		r.step();
		r.step();
		const auto* menu = static_cast<const context_menu::ContextMenuScene*>(
		    on_stack(r, context_menu::kContextMenuSceneKind));
		expect(menu).not_().to_be_null();
		if (menu != nullptr) {
			expect(menu->descriptor_id()).to_equal(context_menu::DescriptorId::DeleteFile);
		}
		r.shell.stack().pop(context_menu::kContextMenuSceneKind);
		select_folder(r, true);
		r.button(kSaveX, kSaveY, true, r.clock + 10);
		r.step();
		r.button(kSaveX, kSaveY, false, r.clock + 10);
		r.step();
		r.step();
		expect(on_stack(r, context_menu::kContextMenuSceneKind)).to_be_null();
		r.button(kShiftX, kShiftY, false, r.clock + 10);
		r.step();
	});

	it("Shift+SAVE on a file the song uses refuses with main's popup", _ {
		CleanBrowser clean;
		ScratchCard card;
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		// Resident and leased, as a sample the song plays is (`AudioFileManager::finishSampleFromCard`).
		auto* sample = new (deluge::memory::alloc_external(sizeof(Sample), 16)) Sample;
		audioFileManager.adoptAudioFileObject(sample);
		sample->addReason();
		sample->filePath = "SAMPLES/CLAP.WAV";
		(void)audioFileManager.audioFiles.insertElement(sample);
		Rig r;
		r.open_and_land(context_of(f), BrowserId::Sample);
		r.button(kShiftX, kShiftY, true, r.clock + 5);
		r.button(kSaveX, kSaveY, true, r.clock + 10);
		r.step();
		r.step();
		expect(on_stack(r, context_menu::kContextMenuSceneKind)).to_be_null();
		expect(popup_text(r)).to_equal(std::string{deluge::l10n::get(String::STRING_FOR_AUDIO_FILE_IS_USED_IN_CURRENT_SONG)});
		r.button(kSaveX, kSaveY, false, r.clock + 10);
		r.button(kShiftX, kShiftY, false, r.clock + 10);
		r.step();
		const int32_t i = audioFileManager.audioFiles.searchForExactObject(sample);
		if (i >= 0) {
			audioFileManager.audioFiles.erase(audioFileManager.audioFiles.begin() + i);
		}
		sample->removeReason("spec");
	});

	it("keeps the waveform on the pads once the preview gives its sample up", _ {
		// Shift+SAVE stops the preview to free the file; `main`'s pads keep what they were last sent.
		CleanBrowser clean;
		ScratchCard card;
		spec::SongFixture f = spec::make_synth_song();
		PreviewHeld held(make_overview_sample());
		Rig r;
		r.open_and_land(sound_context(f), BrowserId::Sample);
		browser_scene().preview_settled(true);
		run_until(r, r.clock + 50);
		const std::string shown = r.fake.state();
		held.range->sampleHolder.audioFile = nullptr;
		deluge::base::notify::raise(deluge::base::notify::Dirty::GridMain);
		run_until(r, r.clock + 50);
		expect(browser_scene().pads()).to_equal(BrowserPads::Waveform);
		expect(r.fake.state()).to_equal(shown);
	});

	it("drops a deleted file from the list while it is still selected, and previews the next", _ {
		// `Browser::currentFileDeleted`.
		CleanBrowser clean;
		ScratchCard card;
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		Rig r;
		r.open_and_land(context_of(f), BrowserId::Sample);
		(void)fake_browser_host().take_preview();
		browser_scene().file_deleted("SAMPLES/SNARE.WAV"); // not the selection: nothing
		expect(std::string{browser_scene().entered_text()}).to_equal(std::string{"CLAP"});
		browser_scene().file_deleted("SAMPLES/CLAP.WAV");
		expect(std::string{browser_scene().entered_text()}).to_equal(std::string{"CLIPS"});
		expect(catalogue_order()).not_().to_contain("CLAP.WAV");
		expect(fake_browser_host().take_preview()).to_equal(PreviewRequest{.path = "", .direction = 0});
	});
});
describe browser_favourites("scenes::browser::BrowserScene the favourites row", $ {
	it("shows the SAMPLES favourites while the keyboard shows, and leaves them on exit", _ {
		CleanBrowser clean;
		ScratchCard card;
		FakeFavourites fake;
		fake.slots[2] = 4;
		UseFavourites use(fake);
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		Rig r;
		r.open_and_land(context_of(f), BrowserId::Sample);
		expect(fake.category).to_equal(std::string{"SAMPLES"});
		expect(r.fake.grid().grid().at(2, 7)).to_equal(RGB{}); // no keyboard yet: the root view's
		tap(r, 9, 7);
		r.settle();
		const std::array<RGB, 16> want = deluge::ui::widgets::grid::favourite_row_colours(fake.slots);
		for (uint8_t x = 0; x < 16; x++) {
			expect(r.fake.grid().grid().at(x, 7)).to_equal(want[x]);
		}
		browser_scene().on_exit();
		expect(fake.closes).to_equal(1);
	});

	it("moves the selection to a favourite's file on a press, and pops main's message for an empty one", _ {
		CleanBrowser clean;
		ScratchCard card;
		FakeFavourites fake;
		fake.slots[3] = 1;
		fake.paths[3] = "SAMPLES/TOM.WAV";
		UseFavourites use(fake);
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		Rig r;
		r.open_and_land(context_of(f), BrowserId::Sample);
		tap(r, 9, 7); // reveal
		tap(r, 3, 7);
		r.land();
		expect(std::string{browser_scene().entered_text()}).to_equal(std::string{"TOM"});
		expect(fake.current).to_equal(std::optional<uint8_t>{3});
		tap(r, 5, 7); // the recall's listing hid the keyboard: this reveals it
		tap(r, 5, 7);
		r.step();
		expect(popup_text(r)).to_equal(std::string{deluge::l10n::get(String::STRING_FOR_FAVOURITES_EMPTY)});
	});

	it("sets a favourite to the selected file with SHIFT, and unsets a set one", _ {
		CleanBrowser clean;
		ScratchCard card;
		FakeFavourites fake;
		UseFavourites use(fake);
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		Rig r;
		r.open_and_land(context_of(f), BrowserId::Sample);
		tap(r, 9, 7);
		r.button(kShiftX, kShiftY, true, r.clock + 5);
		tap(r, 6, 7);
		expect(fake.paths[6]).to_equal(std::string{"SAMPLES/CLAP.WAV"});
		tap(r, 6, 7);
		expect(fake.slots[6].has_value()).to_be_false();
		select_folder(r, true); // the move hides the keyboard
		tap(r, 6, 7);           // which this reveals
		tap(r, 6, 7);           // and a folder is not a favourite
		expect(fake.slots[6].has_value()).to_be_false();
		r.button(kShiftX, kShiftY, false, r.clock + 10);
		r.step();
	});

	it("recolours the current favourite with SHIFT and the vertical encoder", _ {
		CleanBrowser clean;
		ScratchCard card;
		FakeFavourites fake;
		fake.slots[1] = 4;
		fake.current = 1;
		UseFavourites use(fake);
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		Rig r;
		r.open_and_land(context_of(f), BrowserId::Sample);
		tap(r, 9, 7);
		DirectCtx d;
		d.ctx.shift = true;
		(void)browser_scene().handle(
		    UiEvent{.kind = UiEvent::Kind::Encoder, .encoder = deluge::ui::widgets::core::EncoderRole::Vertical, .delta = 2}, d.ctx);
		expect(fake.slots[1]).to_equal(std::optional<uint8_t>{6});
	});

	it("flashes the current favourite every 500 ms from the draw", _ {
		CleanBrowser clean;
		ScratchCard card;
		FakeFavourites fake;
		fake.slots[4] = 2;
		fake.current = 4;
		UseFavourites use(fake);
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		Rig r;
		r.open_and_land(context_of(f), BrowserId::Sample);
		tap(r, 9, 7);
		const size_t from = r.fake.flashes_sent.size();
		run_until(r, r.clock + 400);
		expect(flashes_since(r, from).empty()).to_be_true();
		run_until(r, r.clock + 700); // 1100 ms from the draw: the flashes at 500 and 1000
		const std::vector<deluge::ui::widgets::grid::PadFlash> seen = flashes_since(r, from);
		expect(seen.size()).to_equal(size_t(2));
		for (const deluge::ui::widgets::grid::PadFlash& fl : seen) {
			expect(fl.x).to_equal(4);
			expect(fl.y).to_equal(7);
		}
		r.turn(1, r.clock + 10); // a move hides the keyboard; the rows stay, the flash stops
		r.step();
		const size_t hidden = r.fake.flashes_sent.size();
		run_until(r, r.clock + 1100);
		expect(flashes_since(r, hidden).empty()).to_be_true();
	});

	it("does nothing while the bank loads, and says the card failed when it could not load", _ {
		CleanBrowser clean;
		ScratchCard card;
		FakeFavourites fake;
		fake.slots[3] = 1;
		fake.paths[3] = "SAMPLES/TOM.WAV";
		fake.state = deluge::ui::scenes::services::FavouritesBank::Loading;
		UseFavourites use(fake);
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		Rig r;
		r.open_and_land(context_of(f), BrowserId::Sample);
		tap(r, 9, 7);
		tap(r, 3, 7);
		expect(fake.current.has_value()).to_be_false();
		fake.state = deluge::ui::scenes::services::FavouritesBank::Unavailable;
		tap(r, 3, 7);
		r.step();
		expect(popup_text(r)).to_equal(std::string{deluge::l10n::get(String::STRING_FOR_SD_CARD_ERROR)});
	});

	it("puts the favourites on row 6 and the banks on row 7 in the banks layout, a bank press selecting it", _ {
		CleanBrowser clean;
		ScratchCard card;
		FakeFavourites fake;
		UseFavourites use(fake, FavouritesDefaultLayoutFavoritesAndBanks);
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		Rig r;
		r.open_and_land(context_of(f), BrowserId::Sample);
		expect(browser_scene().favourite_row()).to_equal(6);
		tap(r, 9, 7);
		r.settle();
		const std::array<RGB, 16> banks = deluge::ui::widgets::grid::favourite_bank_colours();
		expect(r.fake.grid().grid().at(5, 7)).to_equal(banks[5]);
		tap(r, 5, 7);
		expect(fake.bank).to_equal(uint8_t(5));
	});
});

describe browser_scene_reference_rows("browser_scene: the reference rows", $ {
	it("parses browser_scene.tsv clean and asks every row of it", _ {
		spec::OracleTable& t = spec::OracleTable::named("browser_scene");
		expect(t.problems().size()).to_equal(size_t{0});
		expect(t.size()).to_equal(size_t{8});
		expect(t.unasked()).to_equal(std::string{});
	});

	it("reads every panel frozen beside it", _ {
		expect(spec::OracleFiles::unread("browser_")).to_equal(std::string{});
	});
});
    // clang-format on

    DELUGE_SPEC(browser_scene_spec, browser_descriptors, browser_composition, browser_open, browser_in_flight,
                browser_navigation, browser_typing, browser_accept, browser_main_pixels, browser_goldens,
                browser_long_press, browser_slicer_doors, browser_preview, browser_pads, browser_keyboard,
                browser_scroll, browser_record_delete, browser_favourites, browser_scene_reference_rows)
