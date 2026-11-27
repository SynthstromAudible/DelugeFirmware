/// @file
/// @brief `ContextMenuScene` -- the one scene that replaces legacy's `gui::ContextMenu` hierarchy
///        -- driven through a real `Shell` over `uitest::FakeSurfaceSet`, and checked against main's
///        reset menus as `tests/golden/ui/menu_oracle/context_menu_scene.tsv` and its `pixels/` hold them.
///
/// Four parts: the `Descriptor` table (main's five reset confirmations and the other context menus as data),
/// the scene's behaviour (`selectEncoderAction` / `buttonAction` / `padAction` / `setupAndCheckAvailability` /
/// `getGreyoutColsAndRows`), and the pixels.
///
/// Each reset is checked by what it leaves behind: run from `kPerturbed`, it must leave the witness
/// (`support/reset_witness.h`) the table's `reset/<action>` rows say main's leaves. The confirmation window is
/// compared byte for byte, below the bezel, with main's panel for each action
/// (`pixels/context_menu_reset_<action>.pbm`). `tests/unit/ui/widgets/option_window_spec.cpp` checks the window at
/// option counts and positions no shipped descriptor reaches.
///
/// @note The spec may include `gui/`; the library may not. Same rule `menu_scene_spec.cpp` and
///       `rename_scene_spec.cpp` run under.

#include "gui/ui/menu_view_of.h"
#include "gui/ui/shell_action_dispatch.h"
#include "gui/ui/shell_host/host_ports.h"
#include "gui/ui/shell_host_ui.h"
#include "hid/display/screensaver.h"
#include "ui/scenes/context_menu/context_menu_scene.h"
#include "ui/scenes/menu/context.h"
#include "ui/scenes/menu/menu_scene.h"
#include "ui/scenes/profile.h"
#include "ui/scenes/registry.h"

#include "base/notify/notice_queue.h"
#include "ui/shell/profiles/deluge.h"
#include "ui/shell/shell.h"

#include "gui/l10n/l10n.h"
#include "gui/l10n/strings.h"
#include "hid/display/oled_canvas/canvas.h"
#include "io/midi/midi_engine.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h"
#include "playback/playback_handler.h"
#include "storage/flash_storage.h"

#include "support/host_flash.h"
#include "support/oracle_table.h"
#include "support/reset_witness.h"
#include "support/song_fixture.h"

#include "fakes.h"

#include "support/mocks/fake_surfaces.h"
#include "support/printers/context_menu_printer.h"
#include "unit/ui/gfx/support/golden.h"
#include "unit/ui/gfx/support/pbm.h"

#include "cppspec.hpp"
#include "support/matchers/address_of.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "support/menu_context.h"
#include "support/spec_main.h"

using namespace deluge::ui::scenes;
using deluge::l10n::String;
using deluge::ui::scenes::context_menu::context_menu_scene;
using deluge::ui::scenes::context_menu::ContextMenuScene;
using deluge::ui::scenes::context_menu::Descriptor;
using deluge::ui::scenes::context_menu::DescriptorId;
using deluge::ui::scenes::context_menu::Selection;
using deluge::ui::scenes::menu::MenuContext;
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

using deluge::test::matchers::address_of;

/// `Action` unqualified is the model's own undo `Action`; the shell's is a different type with the
/// same name (`rename_scene_spec.cpp` makes the same alias for the same reason).
using ShellAction = deluge::ui::shell::Action;

/// @brief Per-example reset of the process-global state this spec touches: the menu's rows, the notice queue, and the
/// panel globals main's confirmation window was drawn under (`support/reset_witness.h`).
void reset_globals() {
	menu_scene().reset_process_state(); // each example starts with the rows a boot has
	deluge::base::notify::Notice discard;
	while (deluge::base::notify::notices().pop(discard)) {}
	spec::reset::pin_panel_globals();
}

/// @brief A 128x64 single-panel board, so the same scene can be recorded at the other shipped
///        panel geometry.
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

/// Board encoder indices and matrix coordinates (`ui/shell/profiles/deluge.cpp`).
constexpr uint8_t kSelectEncoder = 3;
constexpr uint8_t kSelectEncButtonX = 4, kSelectEncButtonY = 3;
constexpr uint8_t kBackX = 7, kBackY = 1;

/// @brief An opaque scene that declares no LED and does nothing: what goes over a menu and comes off again.
constexpr SceneKind kBlinklessKind = static_cast<SceneKind>(static_cast<uint8_t>(SceneKind::FirstApp) + 30);

class BlinklessScene final : public deluge::ui::shell::Scene {
public:
	SceneKind kind() const override { return kBlinklessKind; }
	Result handle(const UiEvent&, const UiContext&) override { return Result::unhandled(); }
	void render_oled(deluge::ui::shell::SurfaceRole, deluge::ui::widgets::oled::OledFrame&) override {}
};

/// @brief The application's registry with the blinkless scene added.
deluge::ui::shell::SceneRegistry registry_with_blinkless() {
	static BlinklessScene scene;
	static const std::array<deluge::ui::shell::Scene*, deluge::ui::shell::kMaxSceneKinds> table = [] {
		std::array<deluge::ui::shell::Scene*, deluge::ui::shell::kMaxSceneKinds> t{};
		const auto app = scene_registry().by_kind;
		for (size_t i = 0; i < app.size() && i < t.size(); i++) {
			t[i] = app[i];
		}
		t[static_cast<size_t>(kBlinklessKind)] = &scene;
		return t;
	}();
	return deluge::ui::shell::SceneRegistry{std::span<deluge::ui::shell::Scene* const>{table}};
}

/// @brief A `Shell` over `FakeSurfaceSet` with the application's own scene registry installed.
struct Rig {
	const BoardProfile& profile;
	deluge::ui::shell::SceneRegistry reg = registry_with_blinkless();
	uitest::FakeSurfaceSet fake;
	Shell shell{fake, profile, reg, deluge::ui::shell::Services{}, ShellConfig{}};

	Rig(const Rig&) = delete;
	Rig& operator=(const Rig&) = delete;

	/// @brief Tear the stack down, so every scene's `on_exit` always runs.
	///
	/// `ContextMenuScene` is a process singleton (`ui/shell/scene.h`'s `SceneState` cache): an example
	/// that left it -- or anything pushed above it -- on the stack would make the NEXT example's
	/// `on_enter` see stale state instead of a fresh one, the cross-contamination
	/// `support/recorder_scene_fixtures.h`'s `~Rig` was written to stop (a leaked recorder there makes the next
	/// `on_enter` refuse outright and fails every later example). Popping
	/// unconditionally means every example starts from the same place whatever the one before it did.
	~Rig() {
		while (shell.stack().depth() > 0) {
			shell.stack().pop(SceneKind::Null);
		}
		menu_scene().reset_process_state(); // each example starts with the rows a boot has
	}

	explicit Rig(const DelugeBoard& board = deluge::ui::shell::kDelugeBoard, const BoardProfile& p = deluge_profile())
	    : profile(p), fake(board, p) {}

	/// @brief Open the context menu directly on @p id, over @p ctx.
	void open(const MenuContext& ctx, DescriptorId id) {
		context_menu_scene().open(ctx);
		shell.init(SceneId{context_menu::kContextMenuSceneKind, static_cast<uint16_t>(id)});
	}

	void tick(uint32_t at) { shell.tick(at); }
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
};

/// @brief A `UiContext` for a directly-invoked `handle()` call: no lowering, no held buttons.
struct DirectCtx {
	ButtonSet held{};
	deluge::ui::widgets::core::PadMask pads{};
	UiContext ctx;
	explicit DirectCtx(uint32_t now_ms = 0) : ctx{held, pads, false, false, now_ms, deluge_profile()} {}
};

const ShellAction* find_action(const Result& r, ShellAction::Kind k) {
	for (const ShellAction& a : r.actions) {
		if (a.kind == k) {
			return &a;
		}
	}
	return nullptr;
}

bool has_action(const Result& r, ShellAction::Kind k) {
	return find_action(r, k) != nullptr;
}

/// @brief A menu context with the host's ports and nothing else: what a reset needs to reach the app.
MenuContext host_context() {
	MenuContext ctx{};
	ctx.ports = deluge::gui::shell_host::host_ports();
	return ctx;
}

MenuContext context_of(spec::SongFixture& f) {
	return spec::ctx_of(*f.song, deluge::gui::menuViewOf(f.clip), 0, false, false, false,
	                    /*kit_global_fx=*/false);
}

/// @brief Drop the rows the faceplate hides from a PBM.
///
/// The 100x40 window's top border lands on canvas row `(48 - 40) >> 1 == 4`, i.e. under the
/// five-row bezel: legacy inks it into a framebuffer nobody can see, and the port's root frame
/// (origin `(0, visible_top)`) clips it away. Every popup in this toolkit is like that --
/// `RenameScene`'s recorded goldens have the same row blank -- so the comparison against the legacy
/// object is over exactly the rows that reach the glass.
///
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

using spec::reset::kPerturbed;
using spec::reset::ResetWitness;
using spec::reset::restore;
using spec::reset::witness;

spec::OracleTable& table() {
	return spec::OracleTable::named("context_menu_scene");
}

/// @brief The witness main's reset @p action left from `kPerturbed`, as the table's `reset/<action>` rows hold it;
///        nothing when a row is missing or is not one number.
std::optional<ResetWitness> frozen_witness(std::string_view action) {
	const std::string path = "reset/" + std::string{action};
	std::array<uint32_t, spec::reset::kWitnessColumns.size()> values{};
	for (size_t i = 0; i < values.size(); i++) {
		const std::vector<std::string>* row = table().find(path, spec::reset::kWitnessColumns[i]);
		if (row == nullptr || row->size() != 1) {
			return std::nullopt;
		}
		const std::optional<uint32_t> v = spec::reset::number_of(row->front());
		if (!v) {
			return std::nullopt;
		}
		values[i] = *v;
	}
	return spec::reset::witness_of(values);
}

/// @brief Main's confirmation window for @p action, the whole 128x48 panel as `pixels/context_menu_reset_<action>.pbm`
///        holds it; empty when the file is missing.
std::string frozen_panel(std::string_view action) {
	std::ifstream in(spec::oracle_dir() + "/pixels/context_menu_reset_" + std::string{action} + ".pbm");
	std::stringstream text;
	text << in.rdbuf();
	return text.str();
}

} // namespace

// clang-format off
describe cm_descriptors("scenes::context_menu descriptor table", $ {
	it("covers exactly the context menus the menu tree opens, and no others", _ {
		// The five `reset_settings::Reset` menu items (`origin/main:src/deluge/gui/ui/menus.cpp`) and
		// `audio_clip::AudioSourceSelector` -- and nothing else: every other legacy
		// `ContextMenu` subclass is opened from a view or a browser the menu does not open. See the
		// note on `DescriptorId`.
		expect(static_cast<int>(DescriptorId::Count)).to_equal(9);
	});

	it("gives the delete-file menu main's heading and option, as a saving menu", _ {
		// `context_menu::DeleteFile` (`gui/context_menu/delete_file.cpp`), a `ContextMenuForSaving`.
		const Descriptor& del = context_menu::descriptor(DescriptorId::DeleteFile);
		expect(static_cast<int>(del.title)).to_equal(static_cast<int>(String::STRING_FOR_DELETE_QMARK));
		expect(del.options.size()).to_equal(size_t(1));
		expect(static_cast<int>(del.options.empty() ? 0 : del.options[0])).to_equal(static_cast<int>(String::STRING_FOR_OK));
		expect(del.for_saving).to_be_true();
		expect(context_menu::descriptor(DescriptorId::SampleBrowserKit).for_saving).to_be_false();
	});

	it("gives the sample browser's two folder menus main's headings and options", _ {
		// `context_menu::sample_browser::Kit` and `::Synth` (`gui/context_menu/sample_browser/`).
		const Descriptor& kit = context_menu::descriptor(DescriptorId::SampleBrowserKit);
		expect(static_cast<int>(kit.title)).to_equal(static_cast<int>(String::STRING_FOR_SAMPLES));
		expect(kit.options.size()).to_equal(size_t(2));
		expect(static_cast<int>(kit.options[0])).to_equal(static_cast<int>(String::STRING_FOR_LOAD_ALL));
		expect(static_cast<int>(kit.options.size() > 1 ? kit.options[1] : 0))
		    .to_equal(static_cast<int>(String::STRING_FOR_SLICE));
		const Descriptor& synth = context_menu::descriptor(DescriptorId::SampleBrowserSynth);
		expect(static_cast<int>(synth.title)).to_equal(static_cast<int>(String::STRING_FOR_LOAD_FILES));
		const uint16_t want[] = {static_cast<uint16_t>(String::STRING_FOR_MULTISAMPLES),
		                         static_cast<uint16_t>(String::STRING_FOR_BASIC),
		                         static_cast<uint16_t>(String::STRING_FOR_SINGLE_CYCLE),
		                         static_cast<uint16_t>(String::STRING_FOR_WAVETABLE)};
		expect(synth.options.size()).to_equal(std::size(want));
		for (size_t i = 0; i < std::size(want) && i < synth.options.size(); i++) {
			expect(static_cast<int>(synth.options[i])).to_equal(static_cast<int>(want[i]));
		}
		// Both hand pads to the browser beneath, refuse input while the load they started runs, and offer what the
		// browser holds: the kit's SLICE only on a file.
		expect(kit.pad).not_().to_be_null();
		expect(kit.busy).not_().to_be_null();
		expect(kit.options_now).not_().to_be_null();
		expect(synth.pad).not_().to_be_null();
		expect(synth.busy).not_().to_be_null();
		expect(synth.options_now).not_().to_be_null();
	});

	it("gives every one of them ResetSettings' own heading and single OK option", _ {
		// `ResetSettings::getTitle()` is `STRING_FOR_ARE_YOU_SURE_QMARK` and `getOptions()` is a
		// one-element `{STRING_FOR_OK}` -- NOT a NO/YES pair. All five share both; only the action
		// differs, which is exactly what `ResetSettingsAction` selects.
		for (int i = 0; i <= static_cast<int>(DescriptorId::ResetAll); i++) {
			const Descriptor& d = context_menu::descriptor(static_cast<DescriptorId>(i));
			expect(static_cast<int>(d.title)).to_equal(static_cast<int>(String::STRING_FOR_ARE_YOU_SURE_QMARK));
			expect(d.options.size()).to_equal(size_t(1));
			expect(static_cast<int>(d.options[0])).to_equal(static_cast<int>(String::STRING_FOR_OK));
			expect(d.accept).not_().to_be_null();
			// The three hooks the sixth descriptor needs are null on all five of these,
			// which is what "the five reset descriptors are unchanged" means concretely. Asserted, so
			// a table edit that gave one of them a hook by accident is caught here rather than by a
			// surprising screen.
			expect(d.initial_option).to_be_null();
			expect(d.on_move).to_be_null();
			expect(d.footer).to_be_null();
			expect(d.options_now).to_be_null();
			expect(d.pad).to_be_null();
			expect(d.busy).to_be_null();
		}
	});

	it("runs each reset through the device actions port and closes", _ {
		using Kind = deluge::ui::scenes::bind::test::RecordingDeviceActions::Kind;
		const std::array<Kind, 5> kinds{Kind::RESET_SETTINGS, Kind::RESET_COMMUNITY_FEATURES, Kind::RESET_MIDI_FOLLOW,
		                                Kind::RESET_MIDI_DEVICES, Kind::FACTORY_RESET};
		for (int i = 0; i <= static_cast<int>(DescriptorId::ResetAll); i++) {
			deluge::ui::scenes::bind::test::RecordingDeviceActions actions;
			MenuContext ctx{};
			ctx.ports.device_actions = &actions;
			const Descriptor& d = context_menu::descriptor(static_cast<DescriptorId>(i));
			expect(d.accept(ctx, 0)).to_be_false();
			expect(actions.calls.size()).to_equal(std::size_t{1});
			if (!actions.calls.empty()) {
				expect(static_cast<int>(actions.calls.front())).to_equal(static_cast<int>(kinds.at(i)));
			}
		}
	});

	it("runs no reset with no device actions port, and still closes", _ {
		for (int i = 0; i <= static_cast<int>(DescriptorId::ResetAll); i++) {
			expect(context_menu::descriptor(static_cast<DescriptorId>(i)).accept(MenuContext{}, 0)).to_be_false();
		}
	});

	it("gives each one a distinct action", _ {
		// Five different `ResetSettingsAction` arms, so five different function pointers -- the
		// check that catches a copy-paste in the table, which sharing a heading and an option list
		// makes easy to make and impossible to see.
		for (int i = 0; i < static_cast<int>(DescriptorId::Count); i++) {
			for (int j = i + 1; j < static_cast<int>(DescriptorId::Count); j++) {
				expect(address_of(context_menu::descriptor(static_cast<DescriptorId>(i)).accept))
				    .not_().to_equal(address_of(context_menu::descriptor(static_cast<DescriptorId>(j)).accept));
			}
		}
	});

	it("falls back to entry 0 for an out-of-range id", _ {
		expect(address_of(context_menu::descriptor(DescriptorId::Count).accept))
		    .to_equal(address_of(context_menu::descriptor(DescriptorId::ResetFlash).accept));
	});
});

describe cm_window("scenes::context_menu window rule", $ {
	it("keeps the cursor on the bottom row while it walks down", _ {
		// `ContextMenu::selectEncoderAction`, which is NOT `list_window_start`'s centre-the-cursor
		// rule: `scrollPos` only follows once the cursor has already left it.
		Selection s;
		expect(static_cast<int>(s.option)).to_equal(0);
		expect(static_cast<int>(s.scroll)).to_equal(0);

		s = context_menu::step(s, +1, 5);   // was on scrollPos -> the window stays put
		expect(static_cast<int>(s.option)).to_equal(1);
		expect(static_cast<int>(s.scroll)).to_equal(0);

		s = context_menu::step(s, +1, 5);   // no longer on it -> the window follows
		expect(static_cast<int>(s.option)).to_equal(2);
		expect(static_cast<int>(s.scroll)).to_equal(1);

		s = context_menu::step(s, +1, 5);
		expect(static_cast<int>(s.option)).to_equal(3);
		expect(static_cast<int>(s.scroll)).to_equal(2);
	});

	it("pulls the window up as soon as the cursor passes its top", _ {
		Selection s{3, 2};
		s = context_menu::step(s, -1, 5);   // still inside the window
		expect(static_cast<int>(s.option)).to_equal(2);
		expect(static_cast<int>(s.scroll)).to_equal(2);
		s = context_menu::step(s, -1, 5);   // `if (currentOption < scrollPos) scrollPos = currentOption;`
		expect(static_cast<int>(s.option)).to_equal(1);
		expect(static_cast<int>(s.scroll)).to_equal(1);
	});

	it("refuses a turn off either end rather than clamping", _ {
		// Legacy restores `oldCurrentOption` and returns: neither the cursor nor the window moves,
		// and the cursor does NOT stick to the last option the way `list` would clamp it.
		expect(context_menu::step(Selection{0, 0}, -1, 5)).to_equal(Selection{0, 0});
		expect(context_menu::step(Selection{4, 3}, +1, 5)).to_equal(Selection{4, 3});
		// An accelerated turn that would overshoot is refused whole, not partly applied.
		expect(context_menu::step(Selection{0, 0}, +9, 5)).to_equal(Selection{0, 0});
		// And an empty list moves nothing.
		expect(context_menu::step(Selection{0, 0}, +1, 0)).to_equal(Selection{0, 0});
	});

	it("does not move a single-option menu, which is every shipped descriptor", _ {
		expect(context_menu::step(Selection{0, 0}, +1, 1)).to_equal(Selection{0, 0});
		expect(context_menu::step(Selection{0, 0}, -1, 1)).to_equal(Selection{0, 0});
	});
});

describe cm_composition("scenes::context_menu::ContextMenuScene composition", $ {
	it("registers at FirstApp + 2 and composes over the view underneath", _ {
		const deluge::ui::shell::SceneRegistry reg = scene_registry();
		expect(reg.find(context_menu::kContextMenuSceneKind)).to_equal(&context_menu_scene());
		expect(static_cast<int>(context_menu::kContextMenuSceneKind))
		    .to_equal(static_cast<int>(SceneKind::FirstApp) + 2);

		const deluge::ui::shell::Scene& s = context_menu_scene();
		// `ContextMenu`'s constructor sets `oledShowsUIUnderneath = true` (`context_menu.h`).
		expect(s.oled_transparent(SurfaceRole::Main)).to_be_true();
		expect(s.is_grid_overlay()).to_be_true();
		expect(s.occludes_grid()).to_be_false();
		// `greyout_mask()` is where the greyout lives: legacy's
		// `getGreyoutColsAndRows` sets `*cols = 0xFFFFFFFF` and leaves `*rows` at the caller's zero
		// (`gui/context_menu/context_menu.cpp`), which is a mask, not the shell's halve-everything dim. Same
		// split `MenuScene` and `RenameScene` make.
		expect(static_cast<uint64_t>(s.greyout_mask().cols)).to_equal(uint64_t{0xFFFFFFFFu});
		expect(static_cast<uint64_t>(s.greyout_mask().rows)).to_equal(uint64_t{0});
		expect(s.input_transparent()).to_be_false();
	});
});

describe cm_open("scenes::context_menu::ContextMenuScene opening", $ {
	it("seeds the cursor and window at zero, as setupAndCheckAvailability does", _ {
		reset_globals();
		spec::SongFixture f = spec::make_synth_song();
		Rig r;
		r.open(context_of(f), DescriptorId::ResetMidiFollow);
		r.tick(0);
		expect(static_cast<int>(context_menu_scene().descriptor_id()))
		    .to_equal(static_cast<int>(DescriptorId::ResetMidiFollow));
		expect(context_menu_scene().selection()).to_equal(Selection{0, 0});
		expect(r.shell.stack().depth()).to_equal(size_t(1));
	});

	it("refuses an argument past the end of the table", _ {
		reset_globals();
		expect(context_menu_scene().on_enter(SceneId{context_menu::kContextMenuSceneKind, 99})).to_be_false();
	});

	it("blinks the back LED, as focusRegained does", _ {
		reset_globals();
		spec::SongFixture f = spec::make_synth_song();
		Rig r;
		r.open(context_of(f), DescriptorId::ResetMidiFollow);
		r.tick(0);
		// `indicator_leds::blinkLed(IndicatorLED::BACK)`, declared rather than driven -- the shell
		// composes and flushes it. BACK is board LED 16 on the Deluge profile
		// (`ui/shell/profiles/deluge.cpp`, `hid/led/indicator_leds.h`).
		constexpr uint8_t kBackLed = 16;
		bool blinked = false;
		for (const auto& [led, on] : r.fake.led_writes) {
			blinked = blinked || (led == kBackLed && on);
		}
		expect(blinked).to_be_true();
	});

	it("blinks SAVE and turns LOAD off for the delete-file menu, as ContextMenuForSaving::focusRegained does", _ {
		reset_globals();
		spec::SongFixture f = spec::make_synth_song();
		Rig r;
		r.open(context_of(f), DescriptorId::DeleteFile);
		r.tick(0);
		// SAVE is board LED 33 and LOAD 15 on the Deluge profile (`ui/shell/profiles/deluge.cpp`).
		constexpr uint8_t kSaveLed = 33;
		constexpr uint8_t kLoadLed = 15;
		bool save = false;
		bool load = false;
		for (const auto& [led, on] : r.fake.led_writes) {
			save = save || (led == kSaveLed && on);
			load = load || (led == kLoadLed && on);
		}
		expect(save).to_be_true();
		expect(load).to_be_false();
		expect(r.fake.leds().desired_declared[static_cast<size_t>(deluge::ui::shell::ButtonId::Load)]).to_be_true();
	});

	it("starts the back LED's blink where the menu opens: lit 250 ms, then off", _ {
		reset_globals();
		spec::SongFixture f = spec::make_synth_song();
		Rig r;
		r.open(context_of(f), DescriptorId::ResetMidiFollow);
		constexpr uint8_t kBackLed = 16;
		const auto back_lit = [&r] {
			bool lit = false;
			for (const auto& [led, on] : r.fake.led_writes) {
				if (led == kBackLed) {
					lit = on;
				}
			}
			return lit;
		};
		for (uint32_t t = 1000; t <= 1240; t += 10) {
			r.tick(t);
		}
		expect(back_lit()).to_be_true();
		for (uint32_t t = 1250; t <= 1300; t += 10) {
			r.tick(t);
		}
		expect(back_lit()).to_be_false();
	});
});

describe cm_sidebar("scenes::context_menu::ContextMenuScene beneath a sample browser", $ {
	it("leaves the audition column to the clip view under a sample-browser menu", _ {
		// `Kit::padAction` and `Synth::padAction` hand the pad to the browser beneath, whose audition column is the clip
		// view's; every other menu's pad closes it and takes every pad.
		reset_globals();
		spec::SongFixture f = spec::make_synth_song();
		{
			Rig r;
			r.open(context_of(f), DescriptorId::SampleBrowserKit);
			expect(context_menu_scene().grid_pad_claim()).to_equal(deluge::ui::shell::GridPadClaim::AllButAudition);
		}
		{
			Rig r;
			r.open(context_of(f), DescriptorId::DeleteFile);
			expect(context_menu_scene().grid_pad_claim()).to_equal(deluge::ui::shell::GridPadClaim::All);
		}
	});

	it("restarts the BACK blink when a scene above it closes, as focusRegained does", _ {
		reset_globals();
		spec::SongFixture f = spec::make_synth_song();
		Rig r;
		r.open(context_of(f), DescriptorId::ResetMidiFollow);
		constexpr uint8_t kBackLed = 16;
		const auto back_lit = [&r] {
			bool lit = false;
			for (const auto& [led, on] : r.fake.led_writes) {
				if (led == kBackLed) {
					lit = on;
				}
			}
			return lit;
		};
		for (uint32_t t = 1000; t <= 1300; t += 10) {
			r.tick(t);
		}
		expect(back_lit()).to_be_false(); // past the first 250 ms lit phase
		// A scene that draws no BACK blink of its own goes over the menu and comes off: the stack fires `on_resume`.
		expect(r.shell.stack().push(SceneId{kBlinklessKind, 0})).to_be_true();
		r.tick(1310);
		expect(r.shell.stack().remove(kBlinklessKind)).to_be_true();
		for (uint32_t t = 1320; t <= 1540; t += 10) {
			r.tick(t);
		}
		expect(r.shell.stack().input_top().kind()).to_equal(context_menu::kContextMenuSceneKind);
		expect(back_lit()).to_be_true(); // restarted on the resume, inside its lit 250 ms
		for (uint32_t t = 1550; t <= 1600; t += 10) {
			r.tick(t);
		}
		expect(back_lit()).to_be_false();
	});
});

describe cm_input("scenes::context_menu::ContextMenuScene input", $ {
	it("closes on BACK without acting", _ {
		reset_globals();
		spec::SongFixture f = spec::make_synth_song();
		Rig r;
		r.open(context_of(f), DescriptorId::ResetCommunityFeatures);
		r.tick(0);

		// `ContextMenu::buttonAction`'s BACK arm -> `close()`. `acceptCurrentOption` is only ever
		// reached from the accept arm, so nothing is reset here.
		runtimeFeatureSettings.set(RuntimeFeatureSettingType::DrumRandomizer, RuntimeFeatureStateToggle::Off);
		r.button(kBackX, kBackY, true, 100);
		r.tick(100);
		expect(r.shell.stack().depth()).to_equal(size_t(0));
		expect(static_cast<int>(runtimeFeatureSettings.get(RuntimeFeatureSettingType::DrumRandomizer)))
		    .to_equal(static_cast<int>(RuntimeFeatureStateToggle::Off));
	});

	it("acts on the select-encoder press and then closes", _ {
		reset_globals();
		spec::SongFixture f = spec::make_synth_song();
		Rig r;
		r.open(context_of(f), DescriptorId::ResetCommunityFeatures);
		r.tick(0);

		// `RuntimeFeatureSettings::factoryReset` is the one shipped action that is pure in-memory:
		// the other four write flash or the card, which a host spec must not do. Moved off its
		// default first, so the reset is observable.
		runtimeFeatureSettings.set(RuntimeFeatureSettingType::DrumRandomizer, RuntimeFeatureStateToggle::Off);
		expect(static_cast<int>(runtimeFeatureSettings.get(RuntimeFeatureSettingType::DrumRandomizer)))
		    .to_equal(static_cast<int>(RuntimeFeatureStateToggle::Off));

		r.button(kSelectEncButtonX, kSelectEncButtonY, true, 100);
		r.button(kSelectEncButtonX, kSelectEncButtonY, false, 100);
		r.tick(100);

		expect(static_cast<int>(runtimeFeatureSettings.get(RuntimeFeatureSettingType::DrumRandomizer)))
		    .to_equal(static_cast<int>(RuntimeFeatureStateToggle::On));
		// `acceptCurrentOption` returns false -> "will cause UI to exit".
		expect(r.shell.stack().depth()).to_equal(size_t(0));
	});

	it("names itself when it closes, rather than popping whatever is on top", _ {
		reset_globals();
		spec::SongFixture f = spec::make_synth_song();
		context_menu_scene().open(context_of(f));
		expect(context_menu_scene().on_enter(
		           SceneId{context_menu::kContextMenuSceneKind, static_cast<uint16_t>(DescriptorId::ResetMidiFollow)}))
		    .to_be_true();

		DirectCtx d;
		const Result back = context_menu_scene().handle(
		    UiEvent{.kind = UiEvent::Kind::Button, .on = true, .button = ButtonId::Back}, d.ctx);
		expect(back.handled).to_be_true();
		const ShellAction* removal = find_action(back, ShellAction::Kind::Remove);
		expect(removal).not_().to_be_null();
		expect(static_cast<int>(removal->pop_to))
		    .to_equal(static_cast<int>(context_menu::kContextMenuSceneKind));
		// `Remove`, not `Pop`: the accept hooks raise notices, so an overlay can legitimately be
		// pushed above this scene between the decision and the router applying it.
		expect(has_action(back, ShellAction::Kind::Pop)).to_be_false();
		context_menu_scene().on_exit();
	});

	it("closes on a pad press and swallows the release", _ {
		reset_globals();
		spec::SongFixture f = spec::make_synth_song();
		{
			Rig r;
			r.open(context_of(f), DescriptorId::ResetMidiFollow);
			r.tick(0);
			// `ContextMenu::padAction`: any pad press closes the menu.
			r.pad(3, 3, 64, 100);
			r.tick(100);
			expect(r.shell.stack().depth()).to_equal(size_t(0));
		}
		{
			// ...and the release is `DEALT_WITH` too, so it never reaches the view underneath.
			context_menu_scene().open(context_of(f));
			(void)context_menu_scene().on_enter(SceneId{context_menu::kContextMenuSceneKind, 0});
			DirectCtx d;
			const Result up = context_menu_scene().handle(
			    UiEvent{.kind = UiEvent::Kind::Pad, .x = 3, .y = 3, .velocity = 0, .on = false}, d.ctx);
			expect(up.handled).to_be_true();
			expect(up.actions.empty()).to_be_true();
			context_menu_scene().on_exit();
		}
	});

	it("takes the select encoder's turn even though the widget claims nothing", _ {
		reset_globals();
		spec::SongFixture f = spec::make_synth_song();
		Rig r;
		r.open(context_of(f), DescriptorId::ResetMidiFollow);
		r.tick(0);
		// `option_window` is render-only, so the turn goes to `handle()` rather than being consumed during the render pass the way `list` consumes it for
		// `MenuScene`. With one option the cursor cannot move -- but the turn must still be
		// swallowed, or it would reach the view underneath and scroll it.
		r.turn(+1, 100);
		r.tick(100);
		expect(context_menu_scene().selection()).to_equal(Selection{0, 0});
		expect(r.shell.stack().depth()).to_equal(size_t(1));

		// And the swallow itself, where the `Result` can be read: `handle` reports handled for a
		// select turn whether or not the cursor could move.
		DirectCtx d;
		const Result turned = context_menu_scene().handle(
		    UiEvent{.kind = UiEvent::Kind::Encoder, .encoder = deluge::ui::widgets::core::EncoderRole::Select, .delta = 1}, d.ctx);
		expect(turned.handled).to_be_true();
		expect(turned.actions.empty()).to_be_true(); // nothing moved, so no repaint is asked for
	});

	it("forgets its context on exit", _ {
		reset_globals();
		spec::SongFixture f = spec::make_synth_song();
		Rig r;
		r.open(context_of(f), DescriptorId::ResetMidiFollow);
		r.tick(0);
		r.button(kBackX, kBackY, true, 100);
		r.tick(100);
		// Same fail-closed reasoning as `RenameScene::on_exit`. None of the five shipped hooks
		// reads the context, so this cannot be observed through them -- it is asserted structurally
		// instead: re-entering still works (the descriptor is all the reset actions need), and the
		// context it would hand a future descriptor is the zeroed one, not the previous song's.
		expect(context_menu_scene().on_enter(
		           SceneId{context_menu::kContextMenuSceneKind, static_cast<uint16_t>(DescriptorId::ResetMidiFollow)}))
		    .to_be_true();
		context_menu_scene().on_exit();
	});
});

describe cm_actions("scenes::context_menu::ContextMenuScene actions", $ {
	/// @brief Drive @p id through the real input path from `kPerturbed` and report the witness afterwards.
	///
	/// The whole path main's reset item runs: open the scene on the descriptor, press the select encoder,
	/// and let the accept hook close it.
	static const auto run_ported = [](spec::SongFixture& f, DescriptorId id) {
		restore(kPerturbed);
		// Every reset raises a notice. One left queued by an earlier example would make the shell push a notice
		// overlay on this Rig's first tick, and the stack would still be one deep after the menu removed itself:
		// correct (it is why `close()` spells `Remove` rather than `Pop`), but not what these examples measure.
		deluge::base::notify::Notice discard;
		while (deluge::base::notify::notices().pop(discard)) {}
		Rig r;
		r.open(context_of(f), id);
		r.tick(0);
		r.button(kSelectEncButtonX, kSelectEncButtonY, true, 100);
		r.button(kSelectEncButtonX, kSelectEncButtonY, false, 100);
		r.tick(100);
		return std::pair<ResetWitness, size_t>{witness(), r.shell.stack().depth()};
	};

	it("runs the flash reset, and only the flash reset, exactly as main does", _ {
		reset_globals();
		spec::ErasedFlash flash; // this example programs the settings sector -- see `support/host_flash.h`
		const ResetWitness entry = witness();
		spec::SongFixture f = spec::make_synth_song();

		// `FlashStorage::factoryReset` is `resetSettings()` + `writeSettings()`. On the host the write goes to a
		// RAM-backed model of the settings sector, which is why this one can be driven; `ErasedFlash` keeps those
		// bytes from every example after this one. The C++ globals are saved and restored separately.
		const std::optional<ResetWitness> frozen = frozen_witness("flash");
		expect(frozen.has_value()).to_be_true();
		const ResetWitness main_after = frozen.value_or(kPerturbed);
		const auto [ported, depth] = run_ported(f, DescriptorId::ResetFlash);

		expect(depth).to_equal(size_t(0));              // `acceptCurrentOption` returns false -> exit
		expect(ported).to_equal(main_after);      // the same reset leaves the same globals
		expect(main_after).not_().to_equal(kPerturbed); // ...and it did something

		// Its own subsystem moved and no other did: a ResetFlash/ResetMidiFollow swap fails here rather than
		// factory-resetting the wrong thing on the device.
		expect(main_after.midi_thru).to_be_false();
		expect(static_cast<int>(main_after.analog_in_ppqn)).to_equal(24);
		expect(main_after.use_sharps).to_be_true();
		expect(static_cast<int>(main_after.follow_root_note)).to_equal(static_cast<int>(kPerturbed.follow_root_note));
		expect(main_after.follow_display_param).to_equal(kPerturbed.follow_display_param);
		expect(static_cast<int>(main_after.drum_randomizer)).to_equal(static_cast<int>(kPerturbed.drum_randomizer));

		restore(entry);
		reset_globals();
	});

	it("runs the MIDI-follow reset, and only that, exactly as main does", _ {
		reset_globals();
		const ResetWitness entry = witness();
		spec::SongFixture f = spec::make_synth_song();

		// `MidiFollow::factoryReset` is an `unlink` (of the in-memory card's copy -- it never reaches
		// `tests/fixtures`), five in-memory writes, and a re-read that finds nothing. All observable, all reversible.
		const std::optional<ResetWitness> frozen = frozen_witness("midi_follow");
		expect(frozen.has_value()).to_be_true();
		const ResetWitness main_after = frozen.value_or(kPerturbed);
		const auto [ported, depth] = run_ported(f, DescriptorId::ResetMidiFollow);

		expect(depth).to_equal(size_t(0));
		expect(ported).to_equal(main_after);
		expect(main_after).not_().to_equal(kPerturbed);

		expect(static_cast<int>(main_after.follow_root_note)).to_equal(36);
		expect(main_after.follow_display_param).to_be_false();
		expect(main_after.midi_thru).to_equal(kPerturbed.midi_thru); // flash untouched
		expect(static_cast<int>(main_after.analog_in_ppqn)).to_equal(static_cast<int>(kPerturbed.analog_in_ppqn));
		expect(static_cast<int>(main_after.drum_randomizer)).to_equal(static_cast<int>(kPerturbed.drum_randomizer));

		restore(entry);
		reset_globals();
	});

	it("runs the community-features reset, and only that, exactly as main does", _ {
		reset_globals();
		const ResetWitness entry = witness();
		spec::SongFixture f = spec::make_synth_song();

		const std::optional<ResetWitness> frozen = frozen_witness("community_features");
		expect(frozen.has_value()).to_be_true();
		const ResetWitness main_after = frozen.value_or(kPerturbed);
		const auto [ported, depth] = run_ported(f, DescriptorId::ResetCommunityFeatures);

		expect(depth).to_equal(size_t(0));
		expect(ported).to_equal(main_after);
		expect(main_after).not_().to_equal(kPerturbed);

		expect(static_cast<int>(main_after.drum_randomizer))
		    .to_equal(static_cast<int>(RuntimeFeatureStateToggle::On));
		expect(main_after.midi_thru).to_equal(kPerturbed.midi_thru);
		expect(static_cast<int>(main_after.follow_root_note)).to_equal(static_cast<int>(kPerturbed.follow_root_note));

		restore(entry);
		reset_globals();
	});

	it("runs the MIDI-devices reset, which touches none of the three above", _ {
		reset_globals();
		const ResetWitness entry = witness();
		spec::SongFixture f = spec::make_synth_song();

		// Driven for what it does NOT do. `MIDIDeviceManager::factoryReset` placement-news over the four global MIDI
		// cable objects and re-reads the card; none of that reaches a witness field. The unchanged witness still
		// discriminates: `Deluge::factoryReset`, the one undriven reset, runs `FlashStorage::factoryReset` among its
		// four and would move the flash fields, so a ResetMidiDevices/ResetAll swap fails here.
		const std::optional<ResetWitness> frozen = frozen_witness("midi_devices");
		expect(frozen.has_value()).to_be_true();
		const ResetWitness main_after = frozen.value_or(ResetWitness{});
		const auto [ported, depth] = run_ported(f, DescriptorId::ResetMidiDevices);

		expect(depth).to_equal(size_t(0));
		expect(ported).to_equal(main_after);
		expect(main_after).to_equal(kPerturbed); // nothing in the witness moved

		restore(entry);
		reset_globals();
	});

	it("cannot drive the full factory reset, and pins it by elimination instead", _ {
		// **The one exclusion.** `Deluge::factoryReset()` is the other four actions back to back
		// (`deluge/deluge.cpp`), so driving it would run `MIDIDeviceManager::factoryReset`'s
		// placement-new over the global MIDI cables AND `FlashStorage::resetSettings`' ~60 global
		// writes in one go -- far more than `ResetWitness` restores, and it would leave this
		// process' MIDI singletons rebuilt underneath every later example. Not driven.
		//
		// What pins it anyway: the four examples above identify their hooks behaviourally
		// (`ResetFlash`/`ResetMidiFollow`/`ResetCommunityFeatures` each move their own subsystem;
		// `ResetMidiDevices` moves none), and the table's five `accept` pointers are pairwise
		// distinct ("gives each one a distinct action"). One entry left, one hook left.
		//
		// The residual risk is therefore exactly: a `ResetAll` entry pointing at some SIXTH hook
		// that is not in the table at all. That is what this asserts is not the case.
		bool matches_a_known_hook = false;
		for (int i = 0; i < static_cast<int>(DescriptorId::Count); i++) {
			if (i == static_cast<int>(DescriptorId::ResetAll)) {
				continue;
			}
			matches_a_known_hook = matches_a_known_hook
			                       || context_menu::descriptor(DescriptorId::ResetAll).accept
			                              == context_menu::descriptor(static_cast<DescriptorId>(i)).accept;
		}
		expect(matches_a_known_hook).to_be_false();
		expect(context_menu::descriptor(DescriptorId::ResetAll).accept).not_().to_be_null();
	});
});

describe cm_pixels("scenes::context_menu::ContextMenuScene pixels", $ {
	it("draws exactly what main's ResetSettings menu draws, for each of the five", _ {
		reset_globals();
		spec::SongFixture f = spec::make_synth_song();

		// All five draw the same window. Checking each against its own frozen panel pins the table as well as the
		// renderer.
		const std::pair<DescriptorId, std::string_view> kPanels[] = {
		    {DescriptorId::ResetFlash, "flash"},
		    {DescriptorId::ResetCommunityFeatures, "community_features"},
		    {DescriptorId::ResetMidiFollow, "midi_follow"},
		    {DescriptorId::ResetMidiDevices, "midi_devices"},
		    {DescriptorId::ResetAll, "all"},
		};
		for (const auto& [id, name] : kPanels) {
			Rig r;
			r.open(context_of(f), id);
			r.tick(0);
			const std::string frozen = frozen_panel(name);
			expect(frozen.empty()).to_be_false();
			// The five bezel rows main inks and the port clips are dropped from both sides; see `below_bezel`.
			// Everything the glass shows is compared byte for byte.
			expect(below_bezel(r.fake.pbm(0), 5)).to_equal(below_bezel(frozen, 5));
		}
	});

	it("stands still across quiet ticks", _ {
		reset_globals();
		spec::SongFixture f = spec::make_synth_song();
		Rig r;
		r.open(context_of(f), DescriptorId::ResetAll);
		r.tick(0);
		const std::string first = r.fake.pbm(0);

		// The only thing on this screen that can move without input is the focused row's side
		// scroller, and "OK" is 18px in a 78px box -- so it never arms, `next_timer()` answers
		// "no timer", and a thousand quiet ticks later the panel is byte-identical. A blink or
		// repaint a single-frame golden cannot see would show up here.
		expect(context_menu_scene().next_timer().has_value()).to_be_false();
		for (uint32_t t = 100; t <= 2000; t += 100) {
			r.tick(t);
		}
		expect(context_menu_scene().next_timer().has_value()).to_be_false();
		expect(r.fake.pbm(0)).to_equal(first);
	});

	it("records the confirmation window at both shipped panel geometries", _ {
		reset_globals();
		spec::SongFixture f = spec::make_synth_song();
		{
			Rig r;
			r.open(context_of(f), DescriptorId::ResetAll);
			r.tick(0);
			expect_golden(self, "ui/scenes/context_menu_confirm_48.pbm", r.fake.pbm(0));
		}
		{
			Rig r64{board_64(), profile_64()};
			r64.open(context_of(f), DescriptorId::ResetAll);
			r64.tick(0);
			expect_golden(self, "ui/scenes/context_menu_confirm_64.pbm", r64.fake.pbm(0));
		}
		// The *scrolled* window -- two rows with the cursor on the bottom -- has no shipped
		// descriptor that can reach it (every one has a single option), so it is recorded from the
		// widget instead: `tests/golden/ui/widgets/option_window_scrolled_{48,64}.pbm`.
	});
});

describe cm_live_settings("scenes::context_menu resets re-apply on the guest what main reads live", $ {
	it("a community-features reset reaches the guest's shift machine and the open menu's layout", _ {
		reset_globals();
		const ResetWitness entry = witness();
		const uint32_t sticky = runtimeFeatureSettings.get(RuntimeFeatureSettingType::ShiftIsSticky);
		const uint32_t horizontal = runtimeFeatureSettings.get(RuntimeFeatureSettingType::HorizontalMenus);
		const MenuContext ctx = host_context(); // the app's menu context carries the host's ports
		runtimeFeatureSettings.set(RuntimeFeatureSettingType::ShiftIsSticky, RuntimeFeatureStateToggle::On);
		shellHostUI.shell().lowering().set_shift_is_sticky(true);
		// A SHIFT latched under the old setting: main's `shiftCurrentlyStuck`, which the reset leaves set.
		shellHostUI.shell().lowering().seed_shift(/*pressed=*/false, /*stuck=*/true, 0);
		menu_scene().set_horizontal_menus(false);

		(void)context_menu::descriptor(DescriptorId::ResetCommunityFeatures).accept(ctx, 0);
		expect(runtimeFeatureSettings.isOn(RuntimeFeatureSettingType::ShiftIsSticky)).to_be_false(); // the default
		expect(runtimeFeatureSettings.isOn(RuntimeFeatureSettingType::HorizontalMenus)).to_be_true(); // the default
		expect(shellHostUI.shell().lowering().shift_is_sticky()).to_be_false();
		expect(shellHostUI.shell().lowering().sticky()).to_be_true(); // the latch outlives the reset, as in main
		expect(shellHostUI.shell().lowering().shift()).to_be_true();
		expect(menu_scene().menu_context().horizontal_menus).to_be_true();

		runtimeFeatureSettings.set(RuntimeFeatureSettingType::ShiftIsSticky, sticky);
		runtimeFeatureSettings.set(RuntimeFeatureSettingType::HorizontalMenus, horizontal);
		shellHostUI.shell().lowering().set_shift_is_sticky(sticky == RuntimeFeatureStateToggle::On);
		shellHostUI.shell().lowering().clear_shift_sticky();
		restore(entry);
		reset_globals();
	});

	it("leaves rounded corners as main does after it, stale until the next boot", _ {
		reset_globals();
		const ResetWitness entry = witness();
		const uint32_t rounded = runtimeFeatureSettings.get(RuntimeFeatureSettingType::RoundedCorners);
		const MenuContext ctx = host_context(); // the app's menu context carries the host's ports
		runtimeFeatureSettings.set(RuntimeFeatureSettingType::RoundedCorners, RuntimeFeatureStateToggle::Off);
		deluge::gui::ui::pushShellRoundedCorners();

		(void)context_menu::descriptor(DescriptorId::ResetCommunityFeatures).accept(ctx, 0);
		expect(runtimeFeatureSettings.isOn(RuntimeFeatureSettingType::RoundedCorners)).to_be_true(); // the default
		expect(deluge::hid::display::oled_canvas::Canvas::roundedCornersEnabled).to_be_false();
		expect(shellHostUI.shell().surfaces().oled()[0].style->rounded).to_be_false();

		runtimeFeatureSettings.set(RuntimeFeatureSettingType::RoundedCorners, rounded);
		deluge::gui::ui::pushShellRoundedCorners();
		restore(entry);
		reset_globals();
	});

	it("a flash reset restyles the guest's highlighting and re-times its screensaver", _ {
		reset_globals();
		spec::ErasedFlash flash; // the reset programs the settings sector -- see `support/host_flash.h`
		const ResetWitness entry = witness();
		const MenuHighlighting highlighting = FlashStorage::accessibilityMenuHighlighting;
		const ScreensaverMode mode = FlashStorage::screensaverMode;
		const uint8_t minutes = FlashStorage::screensaverTimeoutMinutes;
		const MenuContext ctx = host_context(); // the app's menu context carries the host's ports
		FlashStorage::accessibilityMenuHighlighting = MenuHighlighting::NO_INVERSION;
		deluge::gui::ui::pushShellHighlighting();
		FlashStorage::screensaverMode = ScreensaverMode::OFF;
		deluge::hid::display::Screensaver::settingsChanged();
		expect(shellHostUI.shell().screensaver_idle_ms()).to_equal(uint32_t{0});

		(void)context_menu::descriptor(DescriptorId::ResetFlash).accept(ctx, 0);
		expect(shellHostUI.shell().surfaces().oled()[0].style->highlighting)
		    .to_equal(deluge::ui::widgets::core::Highlighting::PartialInversion); // `resetSettings`' default
		expect(shellHostUI.shell().screensaver_idle_ms())
		    .to_equal(static_cast<uint32_t>(kDefaultScreensaverTimeoutMinutes) * 60000u); // a minute, as `arm()` counts

		FlashStorage::accessibilityMenuHighlighting = highlighting;
		FlashStorage::screensaverMode = mode;
		FlashStorage::screensaverTimeoutMinutes = minutes;
		deluge::gui::ui::pushShellHighlighting();
		deluge::hid::display::Screensaver::settingsChanged();
		restore(entry);
		reset_globals();
	});
});

describe cm_reference_rows("scenes::context_menu: the reference rows", $ {
	it("parses context_menu_scene.tsv clean and asks every row of it", _ {
		spec::OracleTable& t = table();
		expect(t.problems().size()).to_equal(size_t{0});
		expect(t.size()).to_be_greater_than(size_t{23});
		expect(t.unasked()).to_equal(std::string{});
	});
});
// clang-format on

DELUGE_SPEC(context_menu_scene_spec, cm_descriptors, cm_window, cm_composition, cm_open, cm_sidebar, cm_input,
            cm_actions, cm_pixels, cm_live_settings, cm_reference_rows)
