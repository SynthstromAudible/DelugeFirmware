/// @file
/// @brief The browser, the context menu and the rename scene over the ports' fakes, with no app: the browser lists and
///        walks a catalogue fake's entries, each context-menu reset lands on the device actions' recorder, and the
///        rename keyboard types by the text keyboard's DEVICE row.

#include "ui/scenes/browser/browser_scene.h"
#include "ui/scenes/context_menu/context_menu_scene.h"
#include "ui/scenes/menu/context.h"
#include "ui/scenes/profile.h"
#include "ui/scenes/registry.h"
#include "ui/scenes/rename/rename_scene.h"

#include "gui/l10n/l10n.h"
#include "libdeluge/control_surface.h"
#include "project/device_settings.h"
#include "ui/shell/profiles/deluge.h"
#include "ui/shell/shell.h"
#include "ui/views/oled/browser.h"
#include "ui/widgets/core/style.h"
#include "ui/widgets/grid/favourites.h"
#include "ui/widgets/grid/keyboard.h"
#include "ui/widgets/grid/shortcut_blink.h"
#include "unit/ui/widgets/support/frames.h"

#include "fakes.h"
#include "support/mocks/fake_surfaces.h"
#include "support/printers/rgb_printer.h"
#include "unit/ui/scenes/bind/support/model_fixture.h"

#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "support/spec_main.h"

using deluge::ui::scenes::browser::browser_scene;
using deluge::ui::scenes::browser::BrowserId;
using deluge::ui::scenes::browser::kBrowserSceneKind;
using deluge::ui::scenes::menu::MenuContext;
using deluge::ui::scenes::rename::kRenameSceneKind;
using deluge::ui::scenes::rename::rename_scene;
using deluge::ui::scenes::rename::TargetId;
using deluge::ui::shell::SceneId;
using deluge::ui::shell::SceneKind;
using deluge::ui::shell::Shell;
using deluge::ui::shell::ShellConfig;

namespace {

/// The select encoder's index on the board.
constexpr uint8_t kSelectEncoder = 3;

/// @brief The browser's listing work, parked until the spec runs it, as the host's card channel would.
struct Deferred {
	static inline void (*fn)(void*) = nullptr;
	static inline void* ctx = nullptr;
	static bool dispatch(void (*f)(void*), void* c) {
		fn = f;
		ctx = c;
		return true;
	}
	static void run() {
		if (fn != nullptr) {
			auto* f = fn;
			fn = nullptr;
			f(ctx);
		}
	}
};

/// @brief A `Shell` over fake surfaces with the scenes' own registry: the whole stack a scene runs on, with no app.
struct Rig {
	deluge::ui::shell::SceneRegistry reg = deluge::ui::scenes::scene_registry();
	uitest::FakeSurfaceSet fake{deluge::ui::shell::kDelugeBoard, deluge::ui::scenes::deluge_profile()};
	Shell shell{fake, deluge::ui::scenes::deluge_profile(), reg, deluge::ui::shell::Services{}, ShellConfig{}};
	uint32_t clock = 0;

	Rig() { deluge::l10n::chosenLanguage = &deluge::l10n::built_in::english; }
	Rig(const Rig&) = delete;
	Rig& operator=(const Rig&) = delete;
	~Rig() {
		while (shell.stack().depth() > 0) {
			shell.stack().pop(SceneKind::Null);
		}
	}

	void step() {
		clock += 10;
		shell.tick(clock);
	}
	void turn(int32_t detents) {
		shell.on_board_event(
		    DelugeInputEvent{DELUGE_EVENT_ENCODER, kSelectEncoder, 0, static_cast<int16_t>(detents * 2)}, clock + 5);
		step();
	}
	void pad(int32_t x, int32_t y, uint8_t velocity) {
		shell.on_board_event(
		    DelugeInputEvent{DELUGE_EVENT_PAD, static_cast<uint8_t>(x), static_cast<uint8_t>(y), velocity}, clock + 5);
		step();
	}
};

/// @brief The sample browser over the fixture's audio clip, its listing landed on @p entries.
void open_sample(Rig& r, deluge::ui::scenes::bind::test::ModelFixture& f) {
	f.host.catalogue.entries = {{"KICK.WAV", false}, {"SNARE.WAV", false}};
	f.host.catalogue.folder = "SAMPLES";
	MenuContext ctx{};
	ctx.model = &f.song;
	ctx.is_audio_clip = true;
	ctx.addresses.clip = deluge::ui::scenes::bind::test::kAudioClip;
	ctx.ports = f.ports();
	browser_scene().set_dispatch(&Deferred::dispatch);
	browser_scene().open(ctx);
	r.shell.init(SceneId{kBrowserSceneKind, static_cast<uint16_t>(BrowserId::Sample)});
	r.shell.tick(0);
	Deferred::run();
	r.step();
	r.step();
}

/// @brief The MIDI browser over the fixture's MIDI track, its listing landed on @p entries.
void open_midi(Rig& r, deluge::ui::scenes::bind::test::ModelFixture& f,
               std::vector<deluge::ui::scenes::bind::test::FakeCatalogue::Entry> entries) {
	f.host.catalogue.entries = std::move(entries);
	f.host.catalogue.folder = "MIDI_DEVICES/DEFINITION";
	MenuContext ctx{};
	ctx.model = &f.song;
	ctx.addresses.track = deluge::ui::scenes::bind::test::kMidiTrack;
	ctx.addresses.instrument = deluge::plugin::SettingContext::Instrument::MIDI;
	ctx.ports = f.ports();
	browser_scene().set_dispatch(&Deferred::dispatch);
	browser_scene().open(ctx);
	r.shell.init(SceneId{kBrowserSceneKind, static_cast<uint16_t>(BrowserId::MidiDeviceDefinition)});
	r.shell.tick(0);
	Deferred::run();
	r.step();
	r.step();
}

/// @brief Forty-five three-byte UTF-8 characters, none of whose bytes has a glyph in the row font.
std::string glyphless_run() {
	std::string s;
	for (int32_t i = 0; i < 45; i++) {
		s += "\xE6\xBC\xA2";
	}
	return s;
}

/// @brief Install @p fake as the browser's favourites for an example, and put the port's back.
struct UseFavourites {
	explicit UseFavourites(deluge::ui::scenes::bind::test::FakeFavourites& fake) {
		browser_scene().set_favourites(&fake);
	}
	~UseFavourites() { browser_scene().set_favourites(nullptr); }
	UseFavourites(const UseFavourites&) = delete;
	UseFavourites& operator=(const UseFavourites&) = delete;
};

/// @brief The QWERTY pad that types @p c, as the text keyboard lays it out.
std::array<int32_t, 2> qwerty_pad(char c) {
	for (int32_t y = 0; y < 8; y++) {
		for (int32_t x = 0; x < deluge::ui::widgets::grid::kTextKeyboardCols; x++) {
			if (deluge::ui::widgets::grid::text_keyboard_char_at(deluge::ui::widgets::grid::KeyboardLayoutKind::Qwerty,
			                                                     x, y, deluge::ui::widgets::grid::kTextKeyboardHomeRow,
			                                                     false)
			    == c) {
				return {x, y};
			}
		}
	}
	return {-1, -1};
}

} // namespace

// clang-format off
describe browser_fakes("the browser, the context menu and the rename scene over the fakes", ${
	it("lists a catalogue's entries and walks them with the select encoder", _ {
		// Declared first: the scene exits through the catalogue it was opened with as the rig tears down. The MIDI
		// browser opens on a MIDI track, which the fixture's song holds.
		deluge::ui::scenes::bind::test::ModelFixture f;
		f.host.catalogue.entries = {{"A.XML", false}, {"B", true}, {"C.XML", false}};
		f.host.catalogue.folder = "MIDI_DEVICES/DEFINITION";
		MenuContext ctx{};
		ctx.model = &f.song;
		ctx.addresses.track = deluge::ui::scenes::bind::test::kMidiTrack;
		ctx.addresses.instrument = deluge::plugin::SettingContext::Instrument::MIDI;
		ctx.ports = f.ports();
		Rig r;
		browser_scene().set_dispatch(&Deferred::dispatch);
		browser_scene().open(ctx);
		r.shell.init(SceneId{kBrowserSceneKind, static_cast<uint16_t>(BrowserId::MidiDeviceDefinition)});
		r.shell.tick(0);
		Deferred::run();
		r.step();
		r.step();
		expect(f.host.catalogue.listed.size()).to_equal(std::size_t{1});
		expect(std::string{browser_scene().entered_text()}).to_equal(std::string{"A"});
		r.turn(1);
		expect(browser_scene().file_index()).to_equal(1);
		expect(std::string{browser_scene().entered_text()}).to_equal(std::string{"B"});
	});

	it("presents the listed rows: each name as drawn, the folder, and the selection", _ {
		deluge::ui::scenes::bind::test::ModelFixture f;
		f.host.catalogue.entries = {{"A.XML", false}, {"B", true}, {"C.XML", false}, {"D.XML", false}};
		f.host.catalogue.folder = "MIDI_DEVICES/DEFINITION";
		MenuContext ctx{};
		ctx.model = &f.song;
		ctx.addresses.track = deluge::ui::scenes::bind::test::kMidiTrack;
		ctx.addresses.instrument = deluge::plugin::SettingContext::Instrument::MIDI;
		ctx.ports = f.ports();
		Rig r;
		browser_scene().set_dispatch(&Deferred::dispatch);
		browser_scene().open(ctx);
		r.shell.init(SceneId{kBrowserSceneKind, static_cast<uint16_t>(BrowserId::MidiDeviceDefinition)});
		r.shell.tick(0);
		Deferred::run();
		r.step();
		r.step();
		std::array<deluge::ui::views::oled::BrowserRow, deluge::ui::scenes::browser::kMaxBrowserRows> rows{};
		const deluge::ui::views::oled::BrowserList list =
		    browser_scene().present_list(deluge::ui::widgets::core::kDelugeStyle48, 128, rows);
		expect(std::string{list.title}).to_equal(std::string{"Load midi device"});
		expect(std::string{list.entered}).to_equal(std::string{"A"});
		expect(list.file_icon_pt2 != nullptr).to_equal(true);
		expect(list.rows.size()).to_equal(std::size_t{3});
		if (list.rows.size() == 3) {
			expect(std::string{list.rows[0].label}).to_equal(std::string{"A"});
			expect(list.rows[0].selected).to_be_true();
			expect(list.rows[0].scrolls).to_be_false();
			expect(std::string{list.rows[1].label}).to_equal(std::string{"B"});
			expect(list.rows[1].folder).to_be_true();
			expect(list.rows[1].selected).to_be_false();
			expect(std::string{list.rows[2].label}).to_equal(std::string{"C"});
		}
	});

	it("presents the DX7 browser's keys dark: they type, and show nothing", _ {
		deluge::ui::scenes::bind::test::ModelFixture f;
		f.host.catalogue.entries = {{"BANK.SYX", false}};
		f.host.catalogue.folder = "DX7";
		MenuContext ctx{};
		ctx.model = &f.song;
		ctx.addresses.slot = deluge::ui::scenes::bind::test::kSynth;
		ctx.ports = f.ports();
		Rig r;
		browser_scene().set_dispatch(&Deferred::dispatch);
		browser_scene().open(ctx);
		r.shell.init(SceneId{kBrowserSceneKind, static_cast<uint16_t>(BrowserId::DxSyx)});
		r.shell.tick(0);
		Deferred::run();
		r.step();
		r.step();
		expect(browser_scene().browser_id()).to_equal(BrowserId::DxSyx);
		const deluge::ui::views::grid::KeyboardPads keys = browser_scene().present_keys();
		expect(keys.dark).to_be_true();
		expect(keys.home_row).to_equal(deluge::ui::widgets::grid::kTextKeyboardHomeRow);
		expect(keys.layout).to_equal(deluge::ui::widgets::grid::KeyboardLayoutKind::Qwerty);
	});

	it("presents the preview's peaks in the main columns only", _ {
		deluge::ui::scenes::bind::test::ModelFixture f;
		Rig r;
		open_midi(r, f, {{"A.XML", false}});
		deluge::ui::scenes::services::WaveformFrame wf{.read = deluge::ui::views::waveform::PeakRead::all_past_end(),
		                                               .centre = 0,
		                                               .span = 1 << 24};
		for (int32_t c = 0; c < deluge::ui::views::waveform::kColumns; c++) {
			wf.read.set(c, deluge::ui::views::waveform::ColumnRead::of({.min = -(1 << 22), .max = 1 << 22}));
		}
		const auto cols = browser_scene().present_waveform(wf, 4);
		expect(cols[3].valid).to_be_true();
		expect(cols[3].max24).to_be_greater_than(0);
		expect(cols[3].min24).to_be_less_than(0);
		expect(cols[4].valid).to_be_false();
	});

	it("presents an unselected long name whole, so its tail past a run of bytes with no glyph inks", _ {
		const std::string run = glyphless_run();
		std::string with_tail;
		{
			deluge::ui::scenes::bind::test::ModelFixture f;
			Rig r;
			open_midi(r, f, {{"A.XML", false}, {run + "TAIL.XML", false}});
			std::array<deluge::ui::views::oled::BrowserRow, deluge::ui::scenes::browser::kMaxBrowserRows> rows{};
			const deluge::ui::views::oled::BrowserList list =
			    browser_scene().present_list(deluge::ui::widgets::core::kDelugeStyle48, 128, rows);
			expect(list.rows.size()).to_equal(std::size_t{2});
			if (list.rows.size() == 2) {
				expect(list.rows[1].selected).to_be_false();
				expect(std::string{list.rows[1].label}).to_equal(run + "TAIL");
			}
			with_tail = r.fake.pbm(0);
		}
		deluge::ui::scenes::bind::test::ModelFixture f;
		Rig r;
		open_midi(r, f, {{"A.XML", false}, {run + ".XML", false}});
		expect(r.fake.pbm(0) == with_tail).to_equal(false);
	});

	it("presents the favourites over the banks, the current favourite and bank flashing when a period turns", _ {
		deluge::ui::scenes::bind::test::ModelFixture f;
		static_cast<void>(
		    f.host.device.write(deluge::project::device_path::kFavouritesLayout,
		                        static_cast<int32_t>(deluge::project::FavouritesLayout::FAVOURITES_AND_BANKS)));
		deluge::ui::scenes::bind::test::FakeFavourites fake;
		fake.slots[2] = 4;
		fake.current = 2;
		fake.bank = 1;
		UseFavourites use(fake);
		Rig r;
		open_sample(r, f);
		expect(browser_scene().browser_id()).to_equal(BrowserId::Sample);
		r.pad(9, 7, 64); // reveals the keyboard
		r.pad(9, 7, 0);
		r.step();
		const std::optional<deluge::ui::views::grid::FavouritePads> now = browser_scene().present_favourites(r.clock);
		expect(now.has_value()).to_be_true();
		if (now.has_value()) {
			expect(now->row).to_equal(6);
			expect(now->colours).to_equal(deluge::ui::widgets::grid::favourite_row_colours(fake.slots));
			expect(now->banks).to_equal(std::optional{deluge::ui::widgets::grid::favourite_bank_colours()});
			expect(now->flash_favourite.has_value()).to_be_false();
		}
		const std::optional<deluge::ui::views::grid::FavouritePads> later =
		    browser_scene().present_favourites(r.clock + deluge::ui::scenes::browser::kFavouriteFlashMs);
		expect(later.has_value()).to_be_true();
		if (later.has_value()) {
			expect(later->flash_favourite).to_equal(std::optional<uint8_t>{2});
			expect(later->flash_bank).to_equal(std::optional<uint8_t>{1});
		}
	});

	it("presents the file selector's BROWSE blink on the audio clip's column, its step played on the pass that drew it",
	   _ {
		deluge::ui::scenes::bind::test::ModelFixture f;
		Rig r;
		open_sample(r, f);
		expect(browser_scene().browse_blink()).to_equal(deluge::ui::scenes::browser::BrowseBlink::Menu);
		const deluge::ui::views::grid::BlinkPads drawn = browser_scene().present_browse_blink(r.clock);
		expect(drawn.plan).to_equal(deluge::ui::widgets::grid::exclusive_blink(0, 5, 0));
		expect(drawn.fire).to_be_false();
		const deluge::ui::views::grid::BlinkPads next =
		    browser_scene().present_browse_blink(r.clock + deluge::ui::widgets::grid::kBlinkCycleMs);
		expect(next.step).to_equal(drawn.step + 2);
		expect(next.fire).to_be_true();
	});

	it("runs each reset on the device actions port", _ {
		using Kind = deluge::ui::scenes::bind::test::RecordingDeviceActions::Kind;
		using deluge::ui::scenes::context_menu::DescriptorId;
		const std::array<Kind, 5> kinds{Kind::RESET_SETTINGS, Kind::RESET_COMMUNITY_FEATURES, Kind::RESET_MIDI_FOLLOW,
		                                Kind::RESET_MIDI_DEVICES, Kind::FACTORY_RESET};
		for (int32_t i = 0; i <= static_cast<int32_t>(DescriptorId::ResetAll); i++) {
			deluge::ui::scenes::bind::test::RecordingDeviceActions actions;
			MenuContext ctx{};
			ctx.ports.device_actions = &actions;
			static_cast<void>(deluge::ui::scenes::context_menu::descriptor(static_cast<DescriptorId>(i)).accept(ctx, 0));
			expect(actions.calls.size()).to_equal(std::size_t{1});
			if (!actions.calls.empty()) {
				expect(static_cast<int32_t>(actions.calls.front()))
				    .to_equal(static_cast<int32_t>(kinds.at(static_cast<std::size_t>(i))));
			}
		}
	});

	it("types by the text keyboard's DEVICE row: the QWERTY Q pad types A on AZERTY", _ {
		deluge::ui::scenes::bind::test::ModelFixture f;
		static_cast<void>(f.host.device.write(deluge::project::device_path::kTextKeyboardLayout, 1)); // AZERTY
		MenuContext ctx{};
		ctx.model = &f.song;
		ctx.addresses.clip = deluge::ui::scenes::bind::test::kSynthClip;
		ctx.ports = f.ports();
		Rig r;
		rename_scene().open(ctx);
		r.shell.init(SceneId{kRenameSceneKind, static_cast<uint16_t>(TargetId::Clip)});
		r.shell.tick(0);
		const std::array<int32_t, 2> q = qwerty_pad('Q');
		r.pad(q[0], q[1], 64);
		r.pad(q[0], q[1], 0);
		const std::string typed{rename_scene().entered_text()};
		expect(typed.size()).to_be_greater_than(std::size_t{0});
		if (!typed.empty()) {
			expect(static_cast<char>(std::tolower(static_cast<unsigned char>(typed.back())))).to_equal('a');
		}
	});
});
// clang-format on

DELUGE_SPEC(browser_fakes_spec, browser_fakes)
