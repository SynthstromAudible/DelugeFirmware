/// @file
/// @brief `ContextMenuScene` -- the one scene that replaces legacy's `gui::ContextMenu` hierarchy
///        -- driven through a real `Shell` over `uitest::FakeSurfaceSet`, and compared pixel for
///        pixel against the *real* `gui::context_menu::resetSettings` instance.
///
/// Three halves, as `rename_scene_spec.cpp` has two: the `Descriptor` table (the five
/// `reset_settings::Reset` items' `getTitle()`/`getOptions()`/`acceptCurrentOption()` as data), the
/// scene's behaviour (`selectEncoderAction` / `buttonAction` / `padAction` /
/// `setupAndCheckAvailability` / `getGreyoutColsAndRows`), and the pixels.
///
/// **The pixel comparison here is against the legacy object itself**, not a transcription: this
/// executable links `deluge_app`, so `gui::context_menu::resetSettings.renderOLED(...)` can be
/// called on a real `oled_canvas::Canvas` and diffed against what the shell blitted.
/// `tests/spec/ui/option_window_spec.cpp` does the transcribed-oracle half, at option counts and
/// window positions no shipped descriptor can reach.
///
/// @note The spec may include `gui/`; the library may not. Same rule `menu_scene_spec.cpp` and
///       `rename_scene_spec.cpp` run under.

#include "scenes/context_menu/context_menu_scene.h"
#include "scenes/menu/context.h"
#include "scenes/profile.h"
#include "scenes/registry.h"

#include "notify/notice_queue.h"
#include "shell/profiles/deluge.h"
#include "shell/shell.h"

#include "gui/context_menu/reset_settings.h"
#include "gui/l10n/l10n.h"
#include "gui/l10n/strings.h"
#include "hid/display/oled_canvas/canvas.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h"
#include "storage/flash_storage.h"

#include "support/song_fixture.h"

#include "spec/gfx/support/golden.h"
#include "spec/gfx/support/pbm.h"
#include "spec/shell/support/fake_surfaces.h"

#include "cppspec.hpp"

#include <cstdint>
#include <string>
#include <string_view>

#include "spec/support/spec_main.h"

using namespace deluge::scenes;
using deluge::l10n::String;
using deluge::scenes::context_menu::context_menu_scene;
using deluge::scenes::context_menu::ContextMenuScene;
using deluge::scenes::context_menu::Descriptor;
using deluge::scenes::context_menu::DescriptorId;
using deluge::scenes::context_menu::Selection;
using deluge::scenes::menu::MenuContext;
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

/// `Action` unqualified is the model's own undo `Action`; the shell's is a different type with the
/// same name (`rename_scene_spec.cpp` makes the same alias for the same reason).
using ShellAction = deluge::ui::shell::Action;

/// @brief Per-example reset of the process-global state this spec touches.
///
/// The language and the notice queue, as every scenes spec resets; plus the two accessibility
/// globals the *legacy* renderer reads, pinned to their defaults so the oracle below cannot be
/// steered by whatever ran before it. `Canvas::menuHighlightingInverts` is the flag
/// `invertLeftEdgeForMenuHighlighting` branches on and `FlashStorage::accessibilityMenuHighlighting`
/// is what `ContextMenu::renderOLED` reads for the label's x -- the pair `style.invert_highlight`
/// stands in for on the port's side.
void reset_globals() {
	deluge::l10n::chosenLanguage = &deluge::l10n::built_in::english;
	deluge::notify::Notice discard;
	while (deluge::notify::notices().pop(discard)) {}
	FlashStorage::accessibilityMenuHighlighting = MenuHighlighting::FULL_INVERSION;
	deluge::hid::display::oled_canvas::Canvas::menuHighlightingInverts = true;
	deluge::hid::display::oled_canvas::Canvas::roundedCornersEnabled = true;
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

/// @brief A `Shell` over `FakeSurfaceSet` with the application's own scene registry installed.
struct Rig {
	const BoardProfile& profile;
	deluge::ui::shell::SceneRegistry reg = scene_registry();
	uitest::FakeSurfaceSet fake;
	Shell shell{fake, profile, reg, deluge::ui::shell::Services{}, ShellConfig{}};

	Rig(const Rig&) = delete;
	Rig& operator=(const Rig&) = delete;

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
	deluge::ui::PadMask pads{};
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

MenuContext context_of(spec::SongFixture& f) {
	return menu::context_for(*f.song, f.clip, 0, false, false, /*kit_global_fx=*/false);
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

/// @brief The REAL legacy renderer's output for one reset action, as a PBM.
///
/// Not a transcription: `gui::context_menu::resetSettings` is the shipped object, and this is the
/// `setAction` / `setupAndCheckAvailability` / `renderOLED` sequence
/// `reset_settings::Reset::selectButtonPress` (`gui/menu_item/reset_settings/reset.h:29-33`) runs,
/// minus the `openUI`.
///
/// @param action Which reset the menu is confirming.
/// @return The 128x48 canvas legacy would have shown, as a PBM.
std::string legacy_pbm(deluge::gui::context_menu::ResetSettingsAction action) {
	static deluge::hid::display::oled_canvas::Canvas canvas;
	canvas.clear();
	deluge::gui::context_menu::resetSettings.setAction(action);
	(void)deluge::gui::context_menu::resetSettings.setupAndCheckAvailability();
	deluge::gui::context_menu::resetSettings.renderOLED(canvas);
	return page_major_to_pbm(canvas.bytes().data(), 128, 48);
}

} // namespace

// clang-format off
describe cm_descriptors("scenes::context_menu descriptor table", $ {
	it("covers exactly the five reset items the ported menu tree reaches", _ {
		// The five `reset_settings::Reset` menu items (`gui/ui/menus.cpp:2106-2115`), and nothing
		// else: every other legacy `ContextMenu` subclass is opened from a view or a browser this
		// plan does not port. See the note on `DescriptorId`.
		expect(static_cast<int>(DescriptorId::Count)).to_equal(5);
	});

	it("gives every one of them ResetSettings' own heading and single OK option", _ {
		// `ResetSettings::getTitle()` is `STRING_FOR_ARE_YOU_SURE_QMARK` and `getOptions()` is a
		// one-element `{STRING_FOR_OK}` -- NOT a NO/YES pair. All five share both; only the action
		// differs, which is exactly what `ResetSettingsAction` selects.
		for (int i = 0; i < static_cast<int>(DescriptorId::Count); i++) {
			const Descriptor& d = context_menu::descriptor(static_cast<DescriptorId>(i));
			expect(static_cast<int>(d.title)).to_equal(static_cast<int>(String::STRING_FOR_ARE_YOU_SURE_QMARK));
			expect(d.options.size()).to_equal(size_t(1));
			expect(static_cast<int>(d.options[0])).to_equal(static_cast<int>(String::STRING_FOR_OK));
			expect(d.accept != nullptr).to_be_true();
		}
	});

	it("gives each one a distinct action", _ {
		// Five different `ResetSettingsAction` arms, so five different function pointers -- the
		// check that catches a copy-paste in the table, which sharing a heading and an option list
		// makes easy to make and impossible to see.
		for (int i = 0; i < static_cast<int>(DescriptorId::Count); i++) {
			for (int j = i + 1; j < static_cast<int>(DescriptorId::Count); j++) {
				expect(context_menu::descriptor(static_cast<DescriptorId>(i)).accept
				       == context_menu::descriptor(static_cast<DescriptorId>(j)).accept)
				    .to_be_false();
			}
		}
	});

	it("falls back to entry 0 for an out-of-range id", _ {
		expect(context_menu::descriptor(DescriptorId::Count).accept
		       == context_menu::descriptor(DescriptorId::ResetFlash).accept)
		    .to_be_true();
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
		expect(context_menu::step(Selection{0, 0}, -1, 5) == Selection{0, 0}).to_be_true();
		expect(context_menu::step(Selection{4, 3}, +1, 5) == Selection{4, 3}).to_be_true();
		// An accelerated turn that would overshoot is refused whole, not partly applied.
		expect(context_menu::step(Selection{0, 0}, +9, 5) == Selection{0, 0}).to_be_true();
		// And an empty list moves nothing.
		expect(context_menu::step(Selection{0, 0}, +1, 0) == Selection{0, 0}).to_be_true();
	});

	it("does not move a single-option menu, which is every shipped descriptor", _ {
		expect(context_menu::step(Selection{0, 0}, +1, 1) == Selection{0, 0}).to_be_true();
		expect(context_menu::step(Selection{0, 0}, -1, 1) == Selection{0, 0}).to_be_true();
	});
});

describe cm_composition("scenes::context_menu::ContextMenuScene composition", $ {
	it("registers at FirstApp + 2 and composes over the view underneath", _ {
		const deluge::ui::shell::SceneRegistry reg = scene_registry();
		expect(reg.find(context_menu::kContextMenuSceneKind) == &context_menu_scene()).to_be_true();
		expect(static_cast<int>(context_menu::kContextMenuSceneKind))
		    .to_equal(static_cast<int>(SceneKind::FirstApp) + 2);

		const deluge::ui::shell::Scene& s = context_menu_scene();
		// `ContextMenu`'s constructor sets `oledShowsUIUnderneath = true` (`context_menu.h:26`).
		expect(s.oled_transparent(SurfaceRole::Main)).to_be_true();
		expect(s.is_grid_overlay()).to_be_true();
		expect(s.occludes_grid()).to_be_false();
		// `dims_grid()` is FALSE and `greyout_mask()` is where the greyout lives: legacy's
		// `getGreyoutColsAndRows` sets `*cols = 0xFFFFFFFF` and leaves `*rows` at the caller's zero
		// (`gui/ui/ui.cpp:74-83`), which is a mask, not the shell's halve-everything dim. Same
		// split `MenuScene` and `RenameScene` make.
		expect(s.dims_grid()).to_be_false();
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
		expect(context_menu_scene().selection() == Selection{0, 0}).to_be_true();
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
		// (`shell/profiles/deluge.cpp:36`, `hid/led/indicator_leds.h:47`).
		constexpr uint8_t kBackLed = 16;
		bool blinked = false;
		for (const auto& [led, on] : r.fake.led_writes) {
			blinked = blinked || (led == kBackLed && on);
		}
		expect(blinked).to_be_true();
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
		expect(removal != nullptr).to_be_true();
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
		// `option_window` is render-only, so the turn falls through to `handle()` post-render
		// rather than being consumed during the render pass the way `list` consumes it for
		// `MenuScene`. With one option the cursor cannot move -- but the turn must still be
		// swallowed, or it would reach the view underneath and scroll it.
		r.turn(+1, 100);
		r.tick(100);
		expect(context_menu_scene().selection() == Selection{0, 0}).to_be_true();
		expect(r.shell.stack().depth()).to_equal(size_t(1));

		// And the swallow itself, where the `Result` can be read: `handle` reports handled for a
		// select turn whether or not the cursor could move.
		DirectCtx d;
		const Result turned = context_menu_scene().handle(
		    UiEvent{.kind = UiEvent::Kind::Encoder, .encoder = deluge::ui::EncoderRole::Select, .delta = 1}, d.ctx);
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

describe cm_pixels("scenes::context_menu::ContextMenuScene pixels", $ {
	it("draws exactly what the real legacy ResetSettings menu draws", _ {
		reset_globals();
		spec::SongFixture f = spec::make_synth_song();

		// One pair per `ResetSettingsAction`: all five draw the same window, and asserting that
		// against the legacy object for each is what pins the *table* as well as the renderer.
		using deluge::gui::context_menu::ResetSettingsAction;
		const std::pair<DescriptorId, ResetSettingsAction> kPairs[] = {
		    {DescriptorId::ResetFlash, ResetSettingsAction::Flash},
		    {DescriptorId::ResetCommunityFeatures, ResetSettingsAction::CommunityFeatures},
		    {DescriptorId::ResetMidiFollow, ResetSettingsAction::MidiFollow},
		    {DescriptorId::ResetMidiDevices, ResetSettingsAction::MidiDevices},
		    {DescriptorId::ResetAll, ResetSettingsAction::All},
		};
		for (const auto& [id, action] : kPairs) {
			Rig r;
			r.open(context_of(f), id);
			r.tick(0);
			// The five bezel rows legacy inks and the port clips are dropped from both sides; see
			// `below_bezel`. Everything the glass shows is compared byte for byte.
			expect(below_bezel(r.fake.pbm(0), 5)).to_equal(below_bezel(legacy_pbm(action), 5));
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
			expect_golden(self, "../ui/scenes/context_menu_confirm_48.pbm", r.fake.pbm(0));
		}
		{
			Rig r64{board_64(), profile_64()};
			r64.open(context_of(f), DescriptorId::ResetAll);
			r64.tick(0);
			expect_golden(self, "../ui/scenes/context_menu_confirm_64.pbm", r64.fake.pbm(0));
		}
		// The *scrolled* window -- two rows with the cursor on the bottom -- has no shipped
		// descriptor that can reach it (every one has a single option), so it is recorded from the
		// widget instead: `tests/golden/ui/widgets/option_window_scrolled_{48,64}.pbm`.
	});
});
// clang-format on

/// Six `describe` groups in one TU, so `DELUGE_SPEC` (one spec per entry point) is spelled out here
/// with `add_specs` instead -- as `rename_scene_spec.cpp` does.
int context_menu_scene_spec(int argc, char** const argv) {
	return CppSpec::parse(argc, argv)
	               .add_specs(cm_descriptors, cm_window, cm_composition, cm_open, cm_input, cm_pixels)
	               .exec()
	               .is_success()
	           ? EXIT_SUCCESS
	           : EXIT_FAILURE;
}
