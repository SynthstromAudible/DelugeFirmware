/// @file
/// @brief `RenameScene` -- the one scene that replaces `gui/ui/rename/`'s five classes -- driven
///        through a real `Shell` over `uitest::FakeSurfaceSet`, against the model-backed
///        `MenuContext` the song fixture resolves.
///
/// Two halves: the `Target` table (what each of the three reachable rename targets reads, writes
/// and refuses -- `RenameClipUI`/`RenameOutputUI`/`RenameDrumUI`'s three virtuals as data), and the
/// editor itself (`QwertyUI`'s typing rules and `RenameUI`'s buttons, over the already-ported
/// `text_keyboard` and `text_edit` widgets).
///
/// @note The spec may include `gui/` (for `l10n::String` ids); the library may not. Same rule
///       `menu_scene_spec.cpp` runs under.

#include "fakes.h"
#include "gui/ui/menu_view_of.h"
#include "project/device_settings.h"
#include "project/setting_group.h"
#include "ui/scenes/bind/ports/write_sink.h"
#include "ui/scenes/menu/context.h"
#include "ui/scenes/menu/menu_scene.h"
#include "ui/scenes/menu/node_id.h"
#include "ui/scenes/menu/nodes/nodes.h"
#include "ui/scenes/profile.h"
#include "ui/scenes/registry.h"
#include "ui/scenes/rename/rename_scene.h"
#include "unit/ui/widgets/support/frames.h"

#include "base/notify/notice_queue.h"
#include "ui/shell/profiles/deluge.h"
#include "ui/shell/shell.h"
#include "ui/widgets/core/metrics.h"
#include "ui/widgets/grid/keyboard.h"

#include "gui/l10n/l10n.h"
#include "gui/l10n/strings.h"
#include "model/clip/instrument_clip.h"
#include "model/drum/drum.h"
#include "model/edits/setting_edit.h"
#include "model/instrument/kit.h"
#include "model/output.h"
#include "model/song/song.h"
#include "processing/sound/sound_drum.h"

#include "support/shortcut_layout.h"
#include "support/song_fixture.h"

#include "support/mocks/fake_surfaces.h"
#include "unit/ui/gfx/support/golden.h"

#include "cppspec.hpp"
#include "support/matchers/value_matchers.h"

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>

#include "support/menu_context.h"
#include "support/spec_main.h"

using namespace deluge::ui::scenes;
using deluge::l10n::String;
using deluge::ui::scenes::menu::MenuContext;
using deluge::ui::scenes::rename::rename_scene;
using deluge::ui::scenes::rename::RenameScene;
using deluge::ui::scenes::rename::TargetId;
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

namespace nodes = deluge::ui::scenes::menu::nodes;

/// `Action` unqualified is the model's own undo `Action` (`model/action/action.h`, pulled in
/// through `menu_scene.h`'s model closure); the shell's is a different type with the same name.
using ShellAction = deluge::ui::shell::Action;

/// @brief Per-example reset of the two process-global things this scene touches -- the same two
///        `menu_scene_spec.cpp` resets, for the same reasons.
void select_language() {
	deluge::l10n::chosenLanguage = &deluge::l10n::built_in::english;
	deluge::base::notify::Notice discard;
	while (deluge::base::notify::notices().pop(discard)) {}
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
constexpr uint8_t kHorizontalEncoder = 1;
constexpr uint8_t kSelectEncButtonX = 4, kSelectEncButtonY = 3;
constexpr uint8_t kBackX = 7, kBackY = 1;

constexpr int32_t kHomeRow = deluge::ui::widgets::grid::kTextKeyboardHomeRow;

/// @brief The pad that types @p c on the QWERTY layout, or `{-1, -1}`.
///
/// Found by asking the toolkit's own lookup rather than transcribing the table a second time --
/// a spec that re-spelled `keyboardChars` would pass even if the table were wrong.
struct Pad {
	int32_t x = -1, y = -1;
};
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

/// @brief A `Shell` over `FakeSurfaceSet` with the application's own scene registry installed.
struct Rig {
	const BoardProfile& profile;
	deluge::ui::shell::SceneRegistry reg = scene_registry();
	uitest::FakeSurfaceSet fake;
	Shell shell{fake, profile, reg, deluge::ui::shell::Services{}, ShellConfig{}};
	uint32_t clock = 0;
	/// The song current before the example, put back after it: `context_of` makes the fixture's song current, so the
	/// app's sink binds it.
	Song* const saved_song = currentSong;

	Rig(const Rig&) = delete;
	Rig& operator=(const Rig&) = delete;

	/// @brief Tear the stack down, so every scene's `on_exit` always runs.
	///
	/// `RenameScene` is a process singleton (`ui/shell/scene.h`'s `SceneState` cache): an example that
	/// left it -- or anything pushed above it -- on the stack would make the NEXT example's
	/// `on_enter` see stale state instead of a fresh one, the cross-contamination
	/// `support/recorder_scene_fixtures.h`'s `~Rig` was written to stop (a leaked recorder there makes the next
	/// `on_enter` refuse outright and fails every later example). Popping
	/// unconditionally means every example starts from the same place whatever the one before it did.
	~Rig() {
		while (shell.stack().depth() > 0) {
			shell.stack().pop(SceneKind::Null);
		}
		currentSong = saved_song;
		menu_scene().reset_process_state(); // each example starts with the rows a boot has
	}

	explicit Rig(const DelugeBoard& board = deluge::ui::shell::kDelugeBoard, const BoardProfile& p = deluge_profile())
	    : profile(p), fake(board, p) {}

	/// @brief Open the rename editor directly on @p id, over @p ctx.
	void open_rename(const MenuContext& ctx, TargetId id) {
		rename_scene().open(ctx);
		shell.init(SceneId{rename::kRenameSceneKind, static_cast<uint16_t>(id)});
	}

	/// @brief Open the menu on @p root and land it straight on @p node.
	void open_menu(const menu::Node& root, const menu::Node& node, const MenuContext& ctx) {
		menu_scene().open(root, ctx);
		shell.init(SceneId{SceneKind::FirstApp, menu::node_id(node)});
	}

	void tick(uint32_t at) { shell.tick(at); }
	void button(uint8_t x, uint8_t y, bool on, uint32_t at) {
		shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_BUTTON, x, y, static_cast<int16_t>(on ? 255 : 0)}, at);
	}
	void pad(uint8_t x, uint8_t y, uint8_t velocity, uint32_t at) {
		shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_PAD, x, y, static_cast<int16_t>(velocity)}, at);
	}
	/// @brief One detent of the (detented, 2 edges per click) horizontal encoder.
	void scroll(int32_t detents, uint32_t at) {
		shell.on_board_event(
		    DelugeInputEvent{DELUGE_EVENT_ENCODER, kHorizontalEncoder, 0, static_cast<int16_t>(detents * 2)}, at);
	}

	/// @brief Press and release the pad that types @p c, one tick each.
	void type(char c) {
		const Pad p = pad_for(c);
		pad(static_cast<uint8_t>(p.x), static_cast<uint8_t>(p.y), 64, clock);
		tick(clock);
		clock += 10;
		pad(static_cast<uint8_t>(p.x), static_cast<uint8_t>(p.y), 0, clock);
		tick(clock);
		clock += 10;
	}
	void type(std::string_view s) {
		for (const char c : s) {
			type(c);
		}
	}
	/// @brief Press the backspace pad (row `home_row + 2`, x 14).
	void backspace() { type(deluge::ui::widgets::grid::kTextKeyboardBackspace); }
	/// @brief Press the pad that types @p c and leave it down. The shell's held-pad mask keeps it
	///        held until `release`, which is what `Response::held_index` reads.
	void press(char c, uint32_t at) {
		const Pad p = pad_for(c);
		pad(static_cast<uint8_t>(p.x), static_cast<uint8_t>(p.y), 64, at);
	}
	void release(char c, uint32_t at) {
		const Pad p = pad_for(c);
		pad(static_cast<uint8_t>(p.x), static_cast<uint8_t>(p.y), 0, at);
	}
	/// @brief Press the enter pad (row `home_row`, x 14) -- the commit, through the real input path.
	void enter() { type(deluge::ui::widgets::grid::kTextKeyboardEnter); }
};

/// @brief A `UiContext` for a directly-invoked `handle()` call: no lowering, no held buttons.
struct DirectCtx {
	ButtonSet held{};
	deluge::ui::widgets::core::PadMask pads{};
	UiContext ctx;
	explicit DirectCtx(uint32_t now_ms = 0) : ctx{held, pads, false, false, now_ms, deluge_profile()} {}
};

/// @brief The select-encoder press `RenameUI::buttonAction` commits on, delivered straight to the
///        scene so the `Result` can be inspected.
Result commit(RenameScene& scene) {
	DirectCtx d;
	return scene.handle(UiEvent{.kind = UiEvent::Kind::Button, .on = true, .button = ButtonId::SelectEnc}, d.ctx);
}

/// @brief The first action of kind @p k, or null.
///
/// Found rather than assumed to be `actions[0]`, so prepending an unrelated
/// action (a `Repaint`, say) does not silently retarget every assertion below onto the wrong one.
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

/// @brief A kit song with one drum, already `kit->selectedDrum`.
spec::SongFixture kit_fixture() {
	spec::SongFixture f = spec::make_kit_song();
	(void)spec::add_drum<SoundDrum>(f);
	return f;
}

MenuContext context_of(spec::SongFixture& f) {
	currentSong = f.song;
	return spec::ctx_of(*f.song, deluge::gui::menuViewOf(f.clip), 0, false, false, false,
	                    /*kit_global_fx=*/false);
}

/// The name the @p group record holds for the owner @p ctx names, or empty.
std::string recorded_name(const MenuContext& ctx, deluge::project::SettingGroupId group) {
	const std::optional<deluge::project::SettingAddress> address = deluge::ui::scenes::bind::setting_address(
	    {.kind = deluge::ui::scenes::bind::Target::Kind::MODEL, .path = "name", .group = group}, ctx.addresses);
	const std::optional<deluge::serialization::Value> value =
	    address ? deluge::project::read_setting(*ctx.model, *address, "name") : std::nullopt;
	return value ? value->str() : std::string{};
}

Kit* kit_of(spec::SongFixture& f) {
	return static_cast<Kit*>(f.instrument);
}

/// @brief The text of the last notice raised, or "" -- the popup the shell turns
///        `ShellAction::Kind::Popup` into arrives here.
std::string last_notice() {
	deluge::base::notify::Notice n;
	std::string text;
	while (deluge::base::notify::notices().pop(n)) {
		text = n.text;
	}
	return text;
}

/// @brief Is the pixel at (@p x, @p y) lit in the P1 PBM @p pbm?
///
/// Reads the header (comment lines skipped, then the magic, width and height) and the pixel digits that follow, so
/// the answer does not depend on how the text is laid out.
/// @return the pixel, or nullopt when the text is not a P1 PBM or (@p x, @p y) lies outside it
std::optional<bool> pbm_lit(const std::string& pbm, int32_t x, int32_t y) {
	size_t pos = 0;
	auto skip_blank = [&] {
		while (pos < pbm.size()) {
			if (pbm[pos] == '#') {
				while (pos < pbm.size() && pbm[pos] != '\n') {
					pos++;
				}
			}
			else if (pbm[pos] == ' ' || pbm[pos] == '\n' || pbm[pos] == '\r' || pbm[pos] == '\t') {
				pos++;
			}
			else {
				break;
			}
		}
	};
	auto token = [&] {
		skip_blank();
		const size_t start = pos;
		while (pos < pbm.size() && pbm[pos] != ' ' && pbm[pos] != '\n' && pbm[pos] != '\r' && pbm[pos] != '\t') {
			pos++;
		}
		return pbm.substr(start, pos - start);
	};
	if (token() != "P1") {
		return std::nullopt;
	}
	const int32_t width = std::atoi(token().c_str());
	const int32_t height = std::atoi(token().c_str());
	if (x < 0 || y < 0 || x >= width || y >= height) {
		return std::nullopt;
	}
	skip_blank();
	// P1 pixels are single digits, whitespace between them optional.
	size_t seen = 0;
	const size_t target = static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
	for (; pos < pbm.size(); pos++) {
		if (pbm[pos] == '0' || pbm[pos] == '1') {
			if (seen++ == target) {
				return pbm[pos] == '1';
			}
		}
	}
	return std::nullopt;
}

/// The 48-row panel's top bezel: rows the frame's origin carries as `visible_top` (`OLED_MAIN_TOPMOST_PIXEL`).
constexpr int32_t kVisibleTop48 = 5;

} // namespace

// clang-format off
describe rename_targets("scenes::rename target table", $ {
	it("covers exactly the three targets the menu tree reaches", _ {
		// Legacy has four `RenameUI` subclasses. `renameMidiCCUI` is deliberately absent: its only
		// opener is `AutomationView::padAction`, and no node in `nodes::kAllRoots` reaches it.
		expect(static_cast<int>(TargetId::Count)).to_equal(3);
	});

	it("carries each subclass's own allowEmpty()", _ {
		// Only `RenameDrumUI` overrides it (`origin/main:src/deluge/gui/ui/rename/rename_drum_ui.h`).
		expect(rename::target(TargetId::Clip).allow_empty).to_be_true();
		expect(rename::target(TargetId::Output).allow_empty).to_be_true();
		expect(rename::target(TargetId::Drum).allow_empty).to_be_false();
	});

	it("titles each target the way its legacy constructor did", _ {
		expect(static_cast<int>(rename::target(TargetId::Clip).title))
		    .to_equal(static_cast<int>(String::STRING_FOR_CLIP_NAME));   // "Clip Name"
		expect(static_cast<int>(rename::target(TargetId::Output).title))
		    .to_equal(static_cast<int>(String::STRING_FOR_TRACK_NAME));  // "Track name"
		expect(static_cast<int>(rename::target(TargetId::Drum).title))
		    .to_equal(static_cast<int>(String::STRING_FOR_DRUM_NAME));   // "Drum Name"
	});
});

describe rename_composition("scenes::rename::RenameScene composition", $ {
	it("registers at FirstApp + 1 and composes over the view underneath", _ {
		const deluge::ui::shell::SceneRegistry reg = scene_registry();
		expect(reg.find(rename::kRenameSceneKind)).to_equal(&rename_scene());
		expect(static_cast<int>(rename::kRenameSceneKind))
		    .to_equal(static_cast<int>(SceneKind::FirstApp) + 1);

		const deluge::ui::shell::Scene& s = rename_scene();
		// `RenameUI`'s constructor sets `oledShowsUIUnderneath = true` (`rename_ui.cpp`).
		expect(s.oled_transparent(SurfaceRole::Main)).to_be_true();
		expect(s.is_grid_overlay()).to_be_true();
		expect(s.occludes_grid()).to_be_false();
		expect(s.input_transparent()).to_be_false();
		// `RenameUI::getGreyoutColsAndRows` (`rename_ui.cpp`): `*cols = 0b11`.
		expect(static_cast<uint64_t>(s.greyout_mask().cols)).to_equal(uint64_t{0b11});
		expect(static_cast<uint64_t>(s.greyout_mask().rows)).to_equal(uint64_t{0});
	});

	it("draws the main pads only, leaving the sidebar to the view beneath", _ {
		// `QwertyUI::drawKeys` clears and draws the main pads only (`gui/ui/qwerty_ui.cpp`);
		// `RenameUI::getGreyoutColsAndRows` greys the two sidebar columns (`rename/rename_ui.cpp`).
		const deluge::ui::shell::Scene& s = rename_scene();
		expect(s.pad_regions()).to_equal(deluge::ui::shell::PadRegions::Main);
		expect(static_cast<uint64_t>(s.greyout_mask().cols)).to_equal(uint64_t{0b11});
		expect(static_cast<uint64_t>(s.greyout_mask().rows)).to_equal(uint64_t{0});
	});
});

describe rename_open("scenes::rename::RenameScene opening", $ {
	it("seeds the editor with the target's current name, cursor at the start", _ {
		select_language();
		spec::SongFixture f = kit_fixture();
		kit_of(f)->selectedDrum->setName("Snare");
		Rig r;
		r.open_rename(context_of(f), TargetId::Drum);

		// `RenameUI::opened`: `enteredText = getCurrentName()`; `QwertyUI::opened`:
		// `enteredTextEditPos = 0`. The whole existing name therefore sits to the cursor's right.
		expect(std::string{rename_scene().entered_text()}).to_equal(std::string{"Snare"});
		expect(rename_scene().cursor()).to_equal(0);
	});

	it("lays its keyboard out by the text keyboard's DEVICE row, read through the device port", _ {
		// `keyboardChars[FlashStorage::keyboardLayout]`: the QWERTY layout's Q pad types A on AZERTY.
		deluge::ui::scenes::bind::test::FakeDeviceSettings device; // declared first: it outlives the rig
		(void)device.write(deluge::project::device_path::kTextKeyboardLayout, 1); // AZERTY
		select_language();
		spec::SongFixture f = kit_fixture();
		kit_of(f)->selectedDrum->setName("");
		MenuContext ctx = context_of(f);
		ctx.ports.device = &device;
		Rig r;
		r.open_rename(ctx, TargetId::Drum);
		r.tick(0);
		r.clock = 10;
		r.type('Q');
		const std::string typed{rename_scene().entered_text()};
		expect(typed.size()).to_equal(std::size_t{1});
		if (!typed.empty()) {
			expect(static_cast<char>(std::tolower(static_cast<unsigned char>(typed.front())))).to_equal('a');
		}
	});

	it("blinks BACK when nothing beneath does", _ {
		// Opened straight over a view that declares no BACK blink (the clip view's rename shortcut).
		select_language();
		spec::SongFixture f = kit_fixture();
		kit_of(f)->selectedDrum->setName("Snare");
		Rig r;
		r.open_rename(context_of(f), TargetId::Drum);
		constexpr uint8_t kBackLed = 16;
		bool lit = false;
		for (uint32_t t = 1000; t <= 1240; t += 10) {
			r.tick(t);
		}
		for (const auto& [led, on] : r.fake.led_writes) {
			if (led == kBackLed) {
				lit = on;
			}
		}
		expect(lit).to_be_true();
		expect(r.fake.leds().desired_declared[static_cast<size_t>(deluge::ui::shell::ButtonId::Back)]).to_be_true();
	});

	it("restarts BACK's blink where it opens, as QwertyUI::opened does: lit 250 ms, then off", _ {
		// `indicator_leds::blinkLed(IndicatorLED::BACK)`: the normal blink, restarted at the open even when the menu
		// beneath already blinks BACK. BACK is board LED 16 on the Deluge profile.
		select_language();
		spec::SongFixture f = kit_fixture();
		kit_of(f)->selectedDrum->setName("Snare");
		Rig r;
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
		r.open_menu(nodes::drum_actions, nodes::drum_actions, context_of(f));
		for (uint32_t t = 1000; t <= 1300; t += 10) {
			r.tick(t);
		}
		expect(back_lit()).to_be_false(); // the menu's blink, past its first 250 ms lit
		r.button(kSelectEncButtonX, kSelectEncButtonY, true, 1310);
		r.button(kSelectEncButtonX, kSelectEncButtonY, false, 1310);
		for (uint32_t t = 1310; t <= 1540; t += 10) {
			r.tick(t);
		}
		expect(&r.shell.stack().input_top()).to_equal(&rename_scene());
		expect(back_lit()).to_be_true(); // restarted at the open, inside its lit 250 ms
		for (uint32_t t = 1570; t <= 1600; t += 10) {
			r.tick(t);
		}
		expect(back_lit()).to_be_false();
	});

	it("forgets its context on exit, so a re-entry without open() refuses", _ {
		select_language();
		spec::SongFixture f = kit_fixture();
		kit_of(f)->selectedDrum->setName("Snare");
		Rig r;
		r.open_rename(context_of(f), TargetId::Drum);
		r.tick(0);
		expect(std::string{rename_scene().entered_text()}).to_equal(std::string{"Snare"});

		r.button(kBackX, kBackY, true, 100);
		r.tick(100);
		expect(r.shell.stack().depth()).to_equal(size_t(0));

		// `open()` is the only writer of `ctx_` and this scene is a singleton
		// that outlives every visit, so `on_exit` zeroes it: an entry that forgot its `open()` --
		// a `SceneId` restored from somewhere, a caller that only pushed -- must not validate
		// against the previous song's pointers. A zeroed context has a null clip/kit/output, every
		// target's `can` rejects that, and `on_enter` refuses. Fails closed.
		expect(rename_scene().on_enter(SceneId{rename::kRenameSceneKind,
		                                       static_cast<uint16_t>(TargetId::Drum)})).to_be_false();
	});

	it("the window's frame covers main's rows, its top edge included", _ {
		// `RenameUI::renderOLED` draws `drawRectangle(4, 4, 124, 44)` on the 48-row panel: inclusive corners,
		// 121 x 41 pixels (`rename_ui.cpp`).
		select_language();
		spec::SongFixture f = spec::make_synth_song();
		rename_scene().open(context_of(f));
		expect(rename_scene().on_enter(SceneId{rename::kRenameSceneKind, static_cast<uint16_t>(TargetId::Clip)}))
		    .to_be_true();
		deluge::ui::test::Rig48 rig;
		rename_scene().render_oled(SurfaceRole::Main, rig.frame);
		expect(rig.lit(60, 4)).to_be_true();
		expect(rig.lit(60, 3)).to_be_false();
		expect(rig.lit(4, 4)).to_be_true();
		expect(rig.lit(124, 44)).to_be_true();
		expect(rig.lit(60, 45)).to_be_false();
		rename_scene().on_exit();
	});

	it("refuses to open when the target cannot resolve its model", _ {
		select_language();
		// A kit with no drums at all: legacy's `RenameDrumUI::getCurrentName` would
		// `FREEZE_WITH_ERROR("RN01")` here, so the port declines to open instead.
		spec::SongFixture f = spec::make_kit_song();
		// The predicate itself, so removing `canRename`'s port fails as an assertion rather than as
		// the null dereference `RenameDrumUI::getCurrentName` would then hit.
		expect(rename::target(TargetId::Drum).can(context_of(f))).to_be_false();
		rename_scene().open(context_of(f));
		expect(rename_scene().on_enter(SceneId{rename::kRenameSceneKind,
		                                       static_cast<uint16_t>(TargetId::Drum)})).to_be_false();
		// And an argument past the end of the table is refused too.
		expect(rename_scene().on_enter(SceneId{rename::kRenameSceneKind, 99})).to_be_false();
	});
});

describe rename_typing("scenes::rename::RenameScene typing", $ {
	it("keeps a long existing name whole and types a long one, as QwertyUI's unbounded enteredText", _ {
		// A drum named from a long sample file name: `QwertyUI::enteredText` is a `std::string`, so neither the open
		// nor the commit cuts it, and a key typed at its end appends.
		select_language();
		spec::SongFixture f = kit_fixture();
		const std::string long_name(200, 'a');
		kit_of(f)->selectedDrum->setName(long_name.c_str());
		Rig r;
		r.open_rename(context_of(f), TargetId::Drum);
		r.tick(0);
		r.clock = 10;
		expect(std::string{rename_scene().entered_text()}).to_equal(long_name);
		(void)commit(rename_scene());
		expect(std::string{kit_of(f)->selectedDrum->name()}).to_equal(long_name);

		// And a typed name runs past 64 characters too.
		kit_of(f)->selectedDrum->setName("");
		Rig typed;
		typed.open_rename(context_of(f), TargetId::Drum);
		typed.tick(0);
		typed.clock = 10;
		std::string digits;
		for (int i = 0; i < 7; i++) {
			digits += "1234567890";
		}
		typed.type(std::string_view{digits});
		expect(std::string{rename_scene().entered_text()}).to_equal(digits);
	});

	it("types a name on the grid and commits it on enter", _ {
		select_language();
		spec::SongFixture f = kit_fixture();
		Rig r;
		r.open_rename(context_of(f), TargetId::Drum);
		r.tick(0);
		r.clock = 10;

		r.type(std::string_view{"KICK"});
		// `QwertyUI::padAction` (`qwerty_ui.cpp`): a letter following another letter is lowercased.
		expect(std::string{rename_scene().entered_text()}).to_equal(std::string{"Kick"});

		const Result done = commit(rename_scene());
		expect(std::string{kit_of(f)->selectedDrum->name()}).to_equal(std::string{"Kick"});
		// `RenameUI::exitUI` -> `close()`: the editor leaves, and it names itself rather than
		// blindly popping whatever is on top.
		const ShellAction* removal = find_action(done, ShellAction::Kind::Remove);
		expect(removal).not_().to_be_null();
		expect(static_cast<int>(removal->pop_to)).to_equal(static_cast<int>(rename::kRenameSceneKind));
	});

	it("commits a track name into the output's record, which a save writes", _ {
		select_language();
		spec::SongFixture f = spec::make_synth_song();
		Rig r;
		r.open_rename(context_of(f), TargetId::Output);
		r.tick(0);
		r.clock = 10;
		r.type(std::string_view{"PAD"});
		(void)commit(rename_scene());
		expect(std::string{f.instrument->name}).to_equal(std::string{"Pad"});
		expect(f.instrument->docOutput_->name).to_equal(std::string{"Pad"});
	});

	it("commits a clip name and a drum name into the record", _ {
		select_language();
		spec::SongFixture synth = spec::make_synth_song();
		{
			Rig r;
			const MenuContext ctx = context_of(synth);
			r.open_rename(ctx, TargetId::Clip);
			r.tick(0);
			r.clock = 10;
			r.type(std::string_view{"VERSE"});
			(void)commit(rename_scene());
			expect(recorded_name(ctx, deluge::project::SettingGroupId::CLIP)).to_equal(std::string{"Verse"});
		}
		spec::SongFixture kit = kit_fixture();
		{
			Rig r;
			const MenuContext ctx = context_of(kit);
			r.open_rename(ctx, TargetId::Drum);
			r.tick(0);
			r.clock = 10;
			r.type(std::string_view{"KICK"});
			(void)commit(rename_scene());
			expect(recorded_name(ctx, deluge::project::SettingGroupId::LANE)).to_equal(std::string{"Kick"});
		}
	});

	it("hands its name to the menu's sink as a text request", _ {
		select_language();
		spec::SongFixture f = spec::make_synth_song();
		Rig r;
		MenuContext ctx = context_of(f);
		deluge::ui::scenes::bind::test::RecordingWrites recording;
		ctx.ports.writes = &recording;
		r.open_rename(ctx, TargetId::Output);
		r.tick(0);
		r.clock = 10;
		r.type(std::string_view{"PAD"});
		(void)commit(rename_scene());
		expect(recording.gestures.size()).to_equal(size_t(1));
		const bool named = !recording.gestures.empty()
		                   && recording.gestures[0].kind == deluge::ui::scenes::bind::test::RecordingWrites::Gesture::Kind::TEXT
		                   && recording.gestures[0].text == "Pad"
		                   && recording.gestures[0].target.group == deluge::project::SettingGroupId::TRACK;
		expect(named).to_be_true();
	});

	it("steps over a character that already matches the one under the cursor", _ {
		select_language();
		spec::SongFixture f = kit_fixture();
		kit_of(f)->selectedDrum->setName("Snare");
		Rig r;
		r.open_rename(context_of(f), TargetId::Drum);
		r.tick(0);
		r.clock = 10;

		// `QwertyUI::padAction` (`qwerty_ui.cpp`) -- `charCaseEqual`, so the case of the typed key does not matter.
		r.type('S');
		expect(std::string{rename_scene().entered_text()}).to_equal(std::string{"Snare"});
		expect(rename_scene().cursor()).to_equal(1);

		// A character that does NOT match truncates the tail and appends (`QwertyUI::padAction`'s new-character arm).
		r.type('T');
		expect(std::string{rename_scene().entered_text()}).to_equal(std::string{"St"});
		expect(rename_scene().cursor()).to_equal(2);
	});

	it("refuses a non-alphanumeric first character", _ {
		select_language();
		spec::SongFixture f = kit_fixture();
		Rig r;
		r.open_rename(context_of(f), TargetId::Drum);
		r.tick(0);
		r.clock = 10;

		// `QwertyUI::padAction` (`qwerty_ui.cpp`): "First character must be alphanumerical".
		r.type('.');
		expect(std::string{rename_scene().entered_text()}).to_equal(std::string{""});
		r.type(' ');
		expect(std::string{rename_scene().entered_text()}).to_equal(std::string{""});
		r.type('4');
		expect(std::string{rename_scene().entered_text()}).to_equal(std::string{"4"});
		// ...and only the FIRST one: a space is fine once something is there.
		r.type(' ');
		expect(std::string{rename_scene().entered_text()}).to_equal(std::string{"4 "});
	});

	it("backspaces the character before the cursor", _ {
		select_language();
		spec::SongFixture f = kit_fixture();
		Rig r;
		r.open_rename(context_of(f), TargetId::Drum);
		r.tick(0);
		r.clock = 10;

		r.type(std::string_view{"AB"});
		expect(std::string{rename_scene().entered_text()}).to_equal(std::string{"Ab"});
		r.backspace();
		expect(std::string{rename_scene().entered_text()}).to_equal(std::string{"A"});
		expect(rename_scene().cursor()).to_equal(1);
		r.backspace();
		expect(std::string{rename_scene().entered_text()}).to_equal(std::string{""});
		// `processBackspace` on an empty string still steps the position, which is already 0.
		r.backspace();
		expect(rename_scene().cursor()).to_equal(0);
	});

	it("repeats a held backspace at legacy's 500 ms delay and 80 ms cadence", _ {
		select_language();
		spec::SongFixture f = kit_fixture();
		Rig r;
		r.open_rename(context_of(f), TargetId::Drum);
		r.tick(0);
		r.clock = 10;
		r.type(std::string_view{"ABCDE"});
		expect(std::string{rename_scene().entered_text()}).to_equal(std::string{"Abcde"});

		// The press itself deletes one character and arms the hold
		// (`QwertyUI::padAction`: `processBackspace()` then
		// `setTimer(UI_SPECIFIC, 500)`).
		r.press(deluge::ui::widgets::grid::kTextKeyboardBackspace, 1000);
		r.tick(1000);
		expect(std::string{rename_scene().entered_text()}).to_equal(std::string{"Abcd"});
		// The scene must ask to be woken with no input pending, or nothing would run `render_grid`
		// and the hold would never be seen again. (The value is the nearer of the repeat deadline
		// and the cursor blink, so only the bound is asserted here; the cadence itself is pinned by
		// the ticks below, which is the assertion a wrong constant fails.)
		const std::optional<uint32_t> armed = rename_scene().next_timer();
		expect(armed.has_value()).to_be_true();
		to_be_at_most(expect(armed.value()), deluge::ui::widgets::grid::kBackspaceRepeatDelayMs);

		// One millisecond short of the delay: still four characters.
		r.tick(1000 + deluge::ui::widgets::grid::kBackspaceRepeatDelayMs - 1);
		expect(std::string{rename_scene().entered_text()}).to_equal(std::string{"Abcd"});
		// On the delay: the first repeat.
		r.tick(1000 + deluge::ui::widgets::grid::kBackspaceRepeatDelayMs);
		expect(std::string{rename_scene().entered_text()}).to_equal(std::string{"Abc"});

		// Then every 80 ms, not every 500.
		const uint32_t first = 1000 + deluge::ui::widgets::grid::kBackspaceRepeatDelayMs;
		r.tick(first + deluge::ui::widgets::grid::kBackspaceRepeatMs - 1);
		expect(std::string{rename_scene().entered_text()}).to_equal(std::string{"Abc"});
		r.tick(first + deluge::ui::widgets::grid::kBackspaceRepeatMs);
		expect(std::string{rename_scene().entered_text()}).to_equal(std::string{"Ab"});
		r.tick(first + 2 * deluge::ui::widgets::grid::kBackspaceRepeatMs);
		expect(std::string{rename_scene().entered_text()}).to_equal(std::string{"A"});
	});

	it("leaves the text and the repeat alone on a tick that repaints only the OLED while a key pad is held", _ {
		select_language();
		spec::SongFixture f = kit_fixture();
		Rig r;
		r.open_rename(context_of(f), TargetId::Drum);
		r.tick(0);
		r.clock = 10;
		r.type(std::string_view{"ABCDE"});
		r.press(deluge::ui::widgets::grid::kTextKeyboardBackspace, 1000);
		r.tick(1000);
		expect(std::string{rename_scene().entered_text()}).to_equal(std::string{"Abcd"});

		// The cursor blink's own flips fall between the press and the repeat's deadline: each repaints the OLED, and the
		// pads are not read.
		const uint32_t flip = 110;
		for (uint32_t at = 1100; at < 1000 + deluge::ui::widgets::grid::kBackspaceRepeatDelayMs; at += flip) {
			r.tick(at);
			expect(std::string{rename_scene().entered_text()}).to_equal(std::string{"Abcd"});
		}
		// The repeat still runs on its own schedule, from the press.
		r.tick(1000 + deluge::ui::widgets::grid::kBackspaceRepeatDelayMs);
		expect(std::string{rename_scene().entered_text()}).to_equal(std::string{"Abc"});
	});

	it("ignores a key, Enter, BACK, SELECT and the sidebar while backspace is held, and keeps repeating", _ {
		select_language();
		spec::SongFixture f = kit_fixture();
		Rig r;
		r.open_rename(context_of(f), TargetId::Drum);
		r.tick(0);
		r.clock = 10;
		r.type(std::string_view{"ABCDEFG"});
		expect(std::string{rename_scene().entered_text()}).to_equal(std::string{"Abcdefg"});
		r.press(deluge::ui::widgets::grid::kTextKeyboardBackspace, 1000);
		r.tick(1000);
		expect(std::string{rename_scene().entered_text()}).to_equal(std::string{"Abcdef"});
		// A letter pressed beside the hold: swallowed, and the hold carries on (`QwertyUI::padAction`).
		r.press('A', 1100);
		r.tick(1100);
		expect(std::string{rename_scene().entered_text()}).to_equal(std::string{"Abcdef"});
		r.press(deluge::ui::widgets::grid::kTextKeyboardEnter, 1150);
		r.tick(1150);
		expect(r.shell.stack().depth()).to_equal(size_t(1));
		r.button(kBackX, kBackY, true, 1200);
		r.tick(1200);
		r.button(kSelectEncButtonX, kSelectEncButtonY, true, 1250);
		r.tick(1250);
		r.pad(16, 3, 64, 1300);
		r.tick(1300);
		expect(r.shell.stack().depth()).to_equal(size_t(1));
		expect(std::string{rename_scene().entered_text()}).to_equal(std::string{"Abcdef"});
		r.tick(1000 + deluge::ui::widgets::grid::kBackspaceRepeatDelayMs);
		expect(std::string{rename_scene().entered_text()}).to_equal(std::string{"Abcde"});
		// Once the pad lifts, BACK closes again.
		r.release(deluge::ui::widgets::grid::kTextKeyboardBackspace, 1600);
		r.tick(1600);
		r.button(kBackX, kBackY, true, 1700);
		r.tick(1700);
		expect(r.shell.stack().depth()).to_equal(size_t(0));
	});

	it("re-arms the repeat 80 ms after the callback that ran it, not after the schedule", _ {
		select_language();
		spec::SongFixture f = kit_fixture();
		Rig r;
		r.open_rename(context_of(f), TargetId::Drum);
		r.tick(0);
		r.clock = 10;
		r.type(std::string_view{"ABCDE"});
		r.press(deluge::ui::widgets::grid::kTextKeyboardBackspace, 1000);
		r.tick(1000);
		r.tick(1530); // 30 ms late
		expect(std::string{rename_scene().entered_text()}).to_equal(std::string{"Abc"});
		r.tick(1580);
		expect(std::string{rename_scene().entered_text()}).to_equal(std::string{"Abc"});
		r.tick(1610);
		expect(std::string{rename_scene().entered_text()}).to_equal(std::string{"Ab"});
		r.release(deluge::ui::widgets::grid::kTextKeyboardBackspace, 1620);
		r.tick(1620);
	});

	it("stops repeating the moment the backspace pad goes up", _ {
		select_language();
		spec::SongFixture f = kit_fixture();
		Rig r;
		r.open_rename(context_of(f), TargetId::Drum);
		r.tick(0);
		r.clock = 10;
		r.type(std::string_view{"ABCDE"});

		r.press(deluge::ui::widgets::grid::kTextKeyboardBackspace, 1000);
		r.tick(1000);
		r.tick(1000 + deluge::ui::widgets::grid::kBackspaceRepeatDelayMs); // one repeat
		expect(std::string{rename_scene().entered_text()}).to_equal(std::string{"Abc"});

		// `padAction`'s release arm unsets the timer (`qwerty_ui.cpp`); here the hold is a
		// level, so letting go simply stops `held_index` naming the key.
		r.release(deluge::ui::widgets::grid::kTextKeyboardBackspace, 1600);
		r.tick(1600);
		r.tick(2000);
		r.tick(3000);
		expect(std::string{rename_scene().entered_text()}).to_equal(std::string{"Abc"});
		// ...and the scene stops asking to be woken for a repeat that will never come. (The cursor
		// blink is still armed, so `next_timer` is engaged; what matters is that no further
		// deletion happened above.)
	});

	it("does not repeat a held character key -- only backspace repeats in legacy", _ {
		select_language();
		spec::SongFixture f = kit_fixture();
		Rig r;
		r.open_rename(context_of(f), TargetId::Drum);
		r.tick(0);

		// Legacy arms `UI_MODE_HOLDING_BACKSPACE` from the backspace arm only
		// (`QwertyUI::padAction`); the "normal keys" arm arms no timer at all.
		r.press('A', 1000);
		r.tick(1000);
		expect(std::string{rename_scene().entered_text()}).to_equal(std::string{"A"});
		r.tick(1600);
		r.tick(2200);
		expect(std::string{rename_scene().entered_text()}).to_equal(std::string{"A"});
		r.release('A', 2300);
		r.tick(2300);
		expect(std::string{rename_scene().entered_text()}).to_equal(std::string{"A"});
	});

	it("repaints the panel on a tick with no input at all, so the cursor actually blinks", _ {
		// The regression test for the gate-arm in `render_oled`, and it has to drive
		// *quiet* ticks to be one. `Shell::tick` renders an OLED surface only when
		// `s.gate.needs_frame(now, had_input_, false)`, the shell-wide widgets gate, or
		// `router_.repaint_requested(role)` says so (`Shell::tick`). On a tick with no
		// input none of those fire on their own here: nothing in this scene arms the widgets gate
		// (`text_edit` is render-only and owns no clock, `ui/widgets/oled/text_edit.h`), and no `Repaint`
		// action is in flight. So the scene's own `request_repaint_after` is the *only* thing that
		// brings the panel back -- and a spec that supplies input on every tick would pass with it
		// deleted, which is exactly the shape of the bug it fixes.
		select_language();
		spec::SongFixture f = kit_fixture();
		Rig r;
		r.open_rename(context_of(f), TargetId::Drum);

		// The cursor is at the end of an empty name, so the blinking bar is what is on screen --
		// `text_edit`'s `cursor_lit` gates that arm and nothing else.
		r.tick(0);
		const std::string lit = r.fake.pbm(0);
		expect(rename_scene().cursor()).to_equal(0);
		expect(static_cast<int>(rename_scene().entered_text().size())).to_equal(0);

		// One half-period later, with NOTHING queued: the panel must have been redrawn, dark.
		r.tick(deluge::ui::widgets::core::kFlashTime);
		const std::string dark = r.fake.pbm(0);
		expect(dark).not_().to_equal(lit);

		// And back again on the next one -- a square wave, not a one-shot.
		r.tick(2 * deluge::ui::widgets::core::kFlashTime);
		expect(r.fake.pbm(0)).to_equal(lit);
	});

	it("blinks the end-of-string cursor and asks to be woken for it", _ {
		select_language();
		spec::SongFixture f = kit_fixture();
		kit_of(f)->selectedDrum->setName("Snare");
		Rig r;
		r.open_rename(context_of(f), TargetId::Drum);
		r.tick(0);

		// `setupBlink(..., shouldBlinkImmediately = true)` (`QwertyUI::drawTextForOLEDEditing` in `qwerty_ui.cpp`): the
		// bar is lit on the frame the editor opens, and `kFlashTime` (110 ms) later it is dark. The cursor starts at 0
		// -- mid-string -- where `text_edit` draws the inversion instead and there is nothing to flash, so nothing to
		// wake for.
		expect(rename_scene().next_timer().has_value()).to_be_false();

		// Walk it to the end: now the bar exists, and the scene asks for the flip.
		uint32_t at = 10;
		for (int i = 0; i < 5; i++) {
			r.scroll(1, at);
			r.tick(at);
			at += 10;
		}
		expect(rename_scene().cursor()).to_equal(5);
		const std::optional<uint32_t> next = rename_scene().next_timer();
		expect(next.has_value()).to_be_true();
		to_be_at_most(expect(next.value()), 110u);
	});

	it("walks the cursor with the horizontal encoder and stops at both ends", _ {
		select_language();
		spec::SongFixture f = kit_fixture();
		kit_of(f)->selectedDrum->setName("Snare");
		Rig r;
		r.open_rename(context_of(f), TargetId::Drum);
		r.tick(0);

		// `QwertyUI::horizontalEncoderAction` (`gui/ui/qwerty_ui.cpp`). The cursor starts at 0.
		r.scroll(-1, 10);
		r.tick(10);
		expect(rename_scene().cursor()).to_equal(0); // "if (enteredTextEditPos == 0) return"

		uint32_t at = 20;
		for (int i = 0; i < 5; i++) {
			r.scroll(1, at);
			r.tick(at);
			at += 10;
		}
		expect(rename_scene().cursor()).to_equal(5); // == the string's length
		r.scroll(1, at);
		r.tick(at);
		expect(rename_scene().cursor()).to_equal(5); // already at the far right end
	});
});

describe rename_rejection("scenes::rename::RenameScene rejection", $ {
	it("refuses a duplicate drum name, stays open, and says so", _ {
		select_language();
		// Two drums: the first named, the second selected and being renamed onto the first's name.
		spec::SongFixture f = spec::make_kit_song();
		Drum* other = spec::add_drum<SoundDrum>(f);
		Drum* being_renamed = spec::add_drum<SoundDrum>(f);
		other->setName("Snare");
		expect(kit_of(f)->selectedDrum).to_equal(being_renamed);

		Rig r;
		r.open_rename(context_of(f), TargetId::Drum);
		r.tick(0);
		r.clock = 10;
		r.type(std::string_view{"SNARE"});
		expect(std::string{rename_scene().entered_text()}).to_equal(std::string{"Snare"});

		const Result rejected = commit(rename_scene());

		// `RenameDrumUI::trySetName` returns false, so `enterKeyPress` does nothing at all: no
		// write, no close, and the typed text is still there to be edited.
		expect(std::string{being_renamed->name()}).to_equal(std::string{""});
		expect(has_action(rejected, ShellAction::Kind::Remove)).to_be_false();
		expect(has_action(rejected, ShellAction::Kind::Pop)).to_be_false();
		expect(std::string{rename_scene().entered_text()}).to_equal(std::string{"Snare"});

		// And the popup legacy raises from inside `trySetName`
		// (`display->displayPopup(STRING_FOR_DUPLICATE_NAMES)`).
		const ShellAction* popup = find_action(rejected, ShellAction::Kind::Popup);
		expect(popup).not_().to_be_null();
		expect(static_cast<int>(popup->popup_string))
		    .to_equal(static_cast<int>(String::STRING_FOR_DUPLICATE_NAMES));
	});

	it("tells the user about the rejection through the real input path", _ {
		select_language();
		spec::SongFixture f = spec::make_kit_song();
		Drum* other = spec::add_drum<SoundDrum>(f);
		Drum* being_renamed = spec::add_drum<SoundDrum>(f);
		other->setName("Snare");

		Rig r;
		r.open_rename(context_of(f), TargetId::Drum);
		r.tick(0);
		r.clock = 10;
		r.type(std::string_view{"SNARE"});
		(void)last_notice(); // drain

		// The enter PAD, not a direct `handle()` call: this is the whole path -- `text_keyboard`
		// reports the key, `after_render` commits, the target refuses, and the shell turns the
		// `Popup` action into a real notice.
		r.enter();
		r.tick(r.clock);

		// `Action::Kind::Popup` reaches `Notices::show_popup`, which stacks a `SceneKind::Popup`
		// over whatever is there (`Notices::show_popup`) rather than queueing a notice -- so
		// the visible proof is the popup ON the stack, not the notice ring.
		bool popup_stacked = false;
		for (const deluge::ui::shell::Scene* scene : r.shell.stack().scenes()) {
			popup_stacked = popup_stacked || scene->kind() == SceneKind::Popup;
		}
		expect(popup_stacked).to_be_true();
		expect(std::string{being_renamed->name()}).to_equal(std::string{""});
		// Still the scene taking input: a popup is drawn over it, never instead of it.
		expect(&r.shell.stack().input_top()).to_equal(&rename_scene());
	});

	it("accepts a name that only collides with the drum being renamed", _ {
		select_language();
		spec::SongFixture f = kit_fixture();
		Kit* kit = kit_of(f);
		kit->selectedDrum->setName("Snare");
		Rig r;
		r.open_rename(context_of(f), TargetId::Drum);
		r.tick(0);

		// `other != kit->selectedDrum` is the whole guard: re-committing a drum's own name is fine.
		const Result done = commit(rename_scene());
		expect(has_action(done, ShellAction::Kind::Remove)).to_be_true();
		expect(std::string{kit->selectedDrum->name()}).to_equal(std::string{"Snare"});
	});

	it("refuses an empty drum name silently, and accepts an empty clip name", _ {
		select_language();
		{
			// `RenameUI::enterKeyPress`: "if (enteredText.empty() && !allowEmpty()) return;" --
			// no popup, no close, nothing written.
			spec::SongFixture f = kit_fixture();
			kit_of(f)->selectedDrum->setName("Snare");
			Rig r;
			r.open_rename(context_of(f), TargetId::Drum);
			r.tick(0);
			r.clock = 10;
			r.backspace();
			r.backspace();
			r.backspace();
			r.backspace();
			r.backspace();
			expect(std::string{rename_scene().entered_text()}).to_equal(std::string{""});

			const Result refused = commit(rename_scene());
			expect(refused.actions.empty()).to_be_true();
			expect(std::string{kit_of(f)->selectedDrum->name()}).to_equal(std::string{"Snare"});
		}
		{
			// Every other target takes the base `allowEmpty() == true`.
			spec::SongFixture f = spec::make_synth_song();
			deluge::edits::clipSettingFor(*f.clip, "name", deluge::serialization::Value::makeScalar("Verse"));
			Rig r;
			r.open_rename(context_of(f), TargetId::Clip);
			r.tick(0);
			r.clock = 10;
			for (int i = 0; i < 5; i++) {
				r.backspace();
			}
			const Result done = commit(rename_scene());
			expect(has_action(done, ShellAction::Kind::Remove)).to_be_true();
			expect(std::string{f.clip->name}).to_equal(std::string{""});
		}
	});
});

describe rename_exits("scenes::rename::RenameScene exits", $ {
	it("closes on BACK and on a sidebar pad, without writing", _ {
		select_language();
		{
			spec::SongFixture f = kit_fixture();
			kit_of(f)->selectedDrum->setName("Snare");
			Rig r;
			r.open_rename(context_of(f), TargetId::Drum);
			r.tick(0);
			r.clock = 10;
			r.type('K');
			expect(std::string{rename_scene().entered_text()}).to_equal(std::string{"K"});

			// `RenameUI::buttonAction`'s BACK arm -> `exitUI()`; the typed text is discarded.
			r.button(kBackX, kBackY, true, 100);
			r.tick(100);
			expect(std::string{kit_of(f)->selectedDrum->name()}).to_equal(std::string{"Snare"});
			expect(r.shell.stack().depth()).to_equal(size_t(0));
		}
		{
			spec::SongFixture f = kit_fixture();
			kit_of(f)->selectedDrum->setName("Snare");
			Rig r;
			r.open_rename(context_of(f), TargetId::Drum);
			r.tick(0);
			// `RenameUI::padAction`: a pad outside the keyboard's columns exits.
			r.pad(deluge::ui::widgets::grid::kTextKeyboardCols, 0, 64, 100);
			r.tick(100);
			expect(std::string{kit_of(f)->selectedDrum->name()}).to_equal(std::string{"Snare"});
			expect(r.shell.stack().depth()).to_equal(size_t(0));
		}
	});
});

describe rename_from_menu("scenes::rename::RenameScene from the menu", $ {
	it("opens on the drum target when drum_name_edit is focused, and steps back out after", _ {
		select_language();
		deluge::base::notify::dirty().drain();
		spec::SongFixture f = kit_fixture();
		kit_of(f)->selectedDrum->setName("Snare");
		const MenuContext ctx = context_of(f);
		expect(static_cast<int>(ctx.root)).to_equal(static_cast<int>(MenuContext::Root::Drum));

		// Open the kit row's ACTIONS page, whose first row IS `drum_name_edit`
		// (`kDrumActionsChildren`, `roots.h`), and press select on it -- the real descent, so
		// `enter_child` -> `enter_leaf` -> `on_focus` is the path under test.
		Rig r;
		r.open_menu(nodes::drum_actions, nodes::drum_actions, ctx);
		r.tick(0);
		expect(static_cast<int>(menu_scene().state().depth)).to_equal(0);

		r.button(kSelectEncButtonX, kSelectEncButtonY, true, 10);
		r.button(kSelectEncButtonX, kSelectEncButtonY, false, 10);
		r.tick(10);
		expect(menu_scene().focused()).to_equal(&nodes::drum_name_edit);

		// `EditName::beginSession` -> `openUI(renameDrumUI)`: the editor is on top, seeded from the
		// drum, on the same tick the item was entered.
		expect(r.shell.stack().depth()).to_equal(size_t(2));
		expect(&r.shell.stack().input_top()).to_equal(&rename_scene());
		expect(static_cast<int>(rename_scene().target_id())).to_equal(static_cast<int>(TargetId::Drum));
		expect(std::string{rename_scene().entered_text()}).to_equal(std::string{"Snare"});

		// `soundEditor.shouldGoUpOneLevelOnBegin = true` (`origin/main:src/deluge/gui/menu_item/edit_name.cpp`
		// `EditName::beginSession`): closing the editor
		// lands one level ABOVE the item, not back on it -- otherwise `on_focus` would reopen it on
		// the very next tick.
		r.button(kBackX, kBackY, true, 100);
		r.tick(100);
		expect(r.shell.stack().depth()).to_equal(size_t(1));
		expect(static_cast<int>(menu_scene().state().depth)).to_equal(0);
		expect(menu_scene().focused()).to_equal(&nodes::drum_actions);
		// ...and it stays closed.
		r.tick(110);
		expect(r.shell.stack().depth()).to_equal(size_t(1));
	});

	it("draws no menu title beneath the drum rename opened from its shortcut pad", _ {
		const spec::ShortcutLayout pinned_v3{deluge::project::ShortcutsVersion::V3}; // the layout the lookups name
		select_language();
		deluge::base::notify::dirty().drain();
		spec::SongFixture f = kit_fixture();
		kit_of(f)->selectedDrum->setName("Snare");
		const MenuContext ctx = context_of(f);
		const menu::Node* node = menu::shortcut_node(MenuContext::Root::Drum, deluge::project::ShortcutsVersion::V3, false, 11, 5);
		expect(node).to_equal(&nodes::drum_name_edit);

		// Shift + (11,5) opens the menu ON the name item, so the item is the menu's root and
		// `SoundEditor::renderOLED` returns before drawing anything beneath the rename window.
		Rig r;
		r.open_menu(*node, *node, ctx);
		r.tick(0);
		r.tick(10);
		expect(&r.shell.stack().input_top()).to_equal(&rename_scene());
		expect(static_cast<int>(menu_scene().state().depth)).to_equal(0);

		// The title's rule runs the panel's width on its last title row; the rename window covers
		// only the middle, so its two ends show whatever the menu beneath drew.
		const std::string pbm = r.fake.pbm(0);
		constexpr int32_t kRuleRow = kVisibleTop48 + deluge::ui::widgets::core::kTitleSeparatorY;
		expect(pbm_lit(pbm, 1, kRuleRow)).to_equal(std::optional<bool>{false});
		expect(pbm_lit(pbm, 126, kRuleRow)).to_equal(std::optional<bool>{false});
	});

	it("reopens the editor when a menu parked on the item is restored", _ {
		select_language();
		deluge::base::notify::dirty().drain();
		spec::SongFixture f = kit_fixture();
		kit_of(f)->selectedDrum->setName("Snare");

		Rig r;
		r.open_menu(nodes::drum_actions, nodes::drum_actions, context_of(f));
		r.tick(0);
		r.button(kSelectEncButtonX, kSelectEncButtonY, true, 10);
		r.button(kSelectEncButtonX, kSelectEncButtonY, false, 10);
		r.tick(10);
		expect(&r.shell.stack().input_top()).to_equal(&rename_scene());

		// The state `replace_root` would carry across, taken while the menu is parked on the item.
		deluge::ui::shell::SceneState saved{};
		menu_scene().save_state(saved);

		// Get back out, so the restore is the only thing that could reopen anything.
		r.button(kBackX, kBackY, true, 100);
		r.tick(100);
		expect(r.shell.stack().depth()).to_equal(size_t(1));
		expect(menu_scene().focused()).to_equal(&nodes::drum_actions);

		// `restore_state` -> `enter_leaf` -> `CustomItem::on_focus` fires,
		// and the editor comes back -- legacy's `focusRegained` -> `beginScreen` -> `beginSession`
		// (`origin/main:src/deluge/gui/ui/sound_editor.cpp`), which `EditName` answers by opening `RenameUI`
		// unconditionally. Restoring the user's place means restoring the editor they were in.
		menu_scene().restore_state(saved);
		expect(menu_scene().focused()).to_equal(&nodes::drum_name_edit);
		// `SceneStack::replace_root` flags the stack changed, which redraws every surface; a bare restore asks for
		// that frame itself.
		deluge::base::notify::dirty().raise(deluge::base::notify::Dirty::Oled);
		r.tick(200);
		expect(r.shell.stack().depth()).to_equal(size_t(2));
		expect(&r.shell.stack().input_top()).to_equal(&rename_scene());
		expect(std::string{rename_scene().entered_text()}).to_equal(std::string{"Snare"});
	});

	it("opens the clip and track targets from the sound root's edit-name page", _ {
		select_language();
		deluge::base::notify::dirty().drain();
		{
			spec::SongFixture f = spec::make_synth_song();
			deluge::edits::clipSettingFor(*f.clip, "name", deluge::serialization::Value::makeScalar("Verse"));
			Rig r;
			r.open_menu(*menu::MenuScene::roots()[static_cast<size_t>(MenuContext::Root::Sound)],
			            nodes::edit_clip_name, context_of(f));
			r.tick(0);
			expect(&r.shell.stack().input_top()).to_equal(&rename_scene());
			expect(static_cast<int>(rename_scene().target_id())).to_equal(static_cast<int>(TargetId::Clip));
			expect(std::string{rename_scene().entered_text()}).to_equal(std::string{"Verse"});
		}
		{
			spec::SongFixture f = spec::make_synth_song();
			deluge::edits::trackSettingFor(*f.instrument, "name", deluge::serialization::Value::makeScalar("Lead"));
			Rig r;
			r.open_menu(*menu::MenuScene::roots()[static_cast<size_t>(MenuContext::Root::Sound)],
			            nodes::edit_track_name, context_of(f));
			r.tick(0);
			expect(&r.shell.stack().input_top()).to_equal(&rename_scene());
			expect(static_cast<int>(rename_scene().target_id())).to_equal(static_cast<int>(TargetId::Output));
			expect(std::string{rename_scene().entered_text()}).to_equal(std::string{"Lead"});
		}
	});
});

describe rename_goldens("scenes::rename::RenameScene pixels", $ {
	/// @brief Open the drum editor and type @p text, on whichever geometry @p r is.
	static const auto draw = [](Rig& r, spec::SongFixture& f, std::string_view text) {
		r.open_rename(context_of(f), TargetId::Drum);
		r.tick(0);
		r.clock = 10;
		r.type(text);
		r.tick(r.clock);
	};

	it("draws the empty editor the same on both shipped panel geometries", _ {
		select_language();
		{
			spec::SongFixture f = kit_fixture();
			Rig r;
			draw(r, f, "");
			expect(std::string{rename_scene().entered_text()}).to_equal(std::string{""});
			expect_golden(self, "ui/scenes/rename_empty_48.pbm", r.fake.pbm(0));
		}
		{
			spec::SongFixture f = kit_fixture();
			Rig r64{board_64(), profile_64()};
			draw(r64, f, "");
			expect_golden(self, "ui/scenes/rename_empty_64.pbm", r64.fake.pbm(0));
		}
	});

	it("draws a typed name the same on both shipped panel geometries", _ {
		select_language();
		{
			spec::SongFixture f = kit_fixture();
			Rig r;
			draw(r, f, "KICK");
			expect(std::string{rename_scene().entered_text()}).to_equal(std::string{"Kick"});
			expect_golden(self, "ui/scenes/rename_typed_48.pbm", r.fake.pbm(0));
		}
		{
			spec::SongFixture f = kit_fixture();
			Rig r64{board_64(), profile_64()};
			draw(r64, f, "KICK");
			expect_golden(self, "ui/scenes/rename_typed_64.pbm", r64.fake.pbm(0));
		}
	});
});
describe rename_present("scenes::rename::RenameScene present", $ {
	it("presents the target's title and the name typed so far as the window", _ {
		select_language();
		spec::SongFixture f = kit_fixture();
		Rig r;
		r.open_rename(context_of(f), TargetId::Drum);
		r.tick(0);
		r.clock = 10;
		r.type(std::string_view{"KICK"});
		deluge::ui::shell::TextScratch scratch;
		const deluge::ui::views::oled::RenameWindow w = rename_scene().present_window(scratch);
		expect(std::string{w.title}).to_equal(std::string{"Drum Name"});
		expect(std::string{w.text}).to_equal(std::string{"Kick"});
	});

	it("presents the QWERTY keyboard on its home row, drawn", _ {
		select_language();
		spec::SongFixture f = kit_fixture();
		Rig r;
		r.open_rename(context_of(f), TargetId::Drum);
		const deluge::ui::views::grid::KeyboardPads k = rename_scene().present_keys();
		expect(static_cast<int>(k.layout)).to_equal(static_cast<int>(deluge::ui::widgets::grid::KeyboardLayoutKind::Qwerty));
		expect(k.home_row).to_equal(kHomeRow);
		expect(k.dark).to_be_false();
	});
});
// clang-format on

DELUGE_SPEC(rename_scene_spec, rename_targets, rename_composition, rename_open, rename_typing, rename_rejection,
            rename_exits, rename_from_menu, rename_goldens, rename_present)
