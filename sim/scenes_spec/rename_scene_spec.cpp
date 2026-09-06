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

#include "scenes/menu/context.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/menu/node_id.h"
#include "scenes/menu/nodes/nodes.h"
#include "scenes/profile.h"
#include "scenes/registry.h"
#include "scenes/rename/rename_scene.h"

#include "notify/notice_queue.h"
#include "shell/profiles/deluge.h"
#include "shell/shell.h"
#include "ui/core/metrics.h"
#include "ui/grid/keyboard.h"

#include "gui/l10n/l10n.h"
#include "gui/l10n/strings.h"
#include "model/clip/instrument_clip.h"
#include "model/drum/drum.h"
#include "model/instrument/kit.h"
#include "model/output.h"
#include "model/song/song.h"
#include "processing/sound/sound_drum.h"

#include "support/song_fixture.h"

#include "spec/gfx/support/golden.h"
#include "spec/shell/support/fake_surfaces.h"

#include "cppspec.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "spec/support/spec_main.h"

using namespace deluge::scenes;
using deluge::l10n::String;
using deluge::scenes::menu::MenuContext;
using deluge::scenes::rename::rename_scene;
using deluge::scenes::rename::RenameScene;
using deluge::scenes::rename::TargetId;
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

namespace nodes = deluge::scenes::menu::nodes;

/// `Action` unqualified is the model's own undo `Action` (`model/action/action.h`, pulled in
/// through `menu_scene.h`'s model closure); the shell's is a different type with the same name.
using ShellAction = deluge::ui::shell::Action;

/// @brief Per-example reset of the two process-global things this scene touches -- the same two
///        `menu_scene_spec.cpp` resets, for the same reasons.
void select_language() {
	deluge::l10n::chosenLanguage = &deluge::l10n::built_in::english;
	deluge::notify::Notice discard;
	while (deluge::notify::notices().pop(discard)) {}
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
constexpr uint8_t kHorizontalEncoder = 1;
constexpr uint8_t kSelectEncButtonX = 4, kSelectEncButtonY = 3;
constexpr uint8_t kBackX = 7, kBackY = 1;

constexpr int32_t kHomeRow = deluge::ui::grid::kTextKeyboardHomeRow;

/// @brief The pad that types @p c on the QWERTY layout, or `{-1, -1}`.
///
/// Found by asking the toolkit's own lookup rather than transcribing the table a second time --
/// a spec that re-spelled `keyboardChars` would pass even if the table were wrong.
struct Pad {
	int32_t x = -1, y = -1;
};
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
	/// `RenameScene` is a process singleton (`shell/scene.h`'s `SceneState` cache): an example that
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
	void backspace() { type(deluge::ui::grid::kTextKeyboardBackspace); }
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
	void enter() { type(deluge::ui::grid::kTextKeyboardEnter); }
};

/// @brief A `UiContext` for a directly-invoked `handle()` call: no lowering, no held buttons.
struct DirectCtx {
	ButtonSet held{};
	deluge::ui::PadMask pads{};
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
/// Fix round 1, Minor 4: found rather than assumed to be `actions[0]`, so prepending an unrelated
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
	return menu::context_for(*f.song, f.clip, 0, false, false, false, /*kit_global_fx=*/false);
}

Kit* kit_of(spec::SongFixture& f) {
	return static_cast<Kit*>(f.instrument);
}

/// @brief The text of the last notice raised, or "" -- the popup the shell turns
///        `ShellAction::Kind::Popup` into arrives here.
std::string last_notice() {
	deluge::notify::Notice n;
	std::string text;
	while (deluge::notify::notices().pop(n)) {
		text = n.text;
	}
	return text;
}

} // namespace

// clang-format off
describe rename_targets("scenes::rename target table", $ {
	it("covers exactly the three targets the ported menu tree reaches", _ {
		// Legacy has four `RenameUI` subclasses. `renameMidiCCUI` is deliberately absent: its only
		// opener is `automation_view.cpp:1641`, and no node in `nodes::kAllRoots` reaches it.
		expect(static_cast<int>(TargetId::Count)).to_equal(3);
	});

	it("carries each subclass's own allowEmpty()", _ {
		// Only `RenameDrumUI` overrides it (`rename_drum_ui.h:25`).
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
		expect(reg.find(rename::kRenameSceneKind) == &rename_scene()).to_be_true();
		expect(static_cast<int>(rename::kRenameSceneKind))
		    .to_equal(static_cast<int>(SceneKind::FirstApp) + 1);

		const deluge::ui::shell::Scene& s = rename_scene();
		// `RenameUI`'s constructor sets `oledShowsUIUnderneath = true` (`rename_ui.cpp:29`).
		expect(s.oled_transparent(SurfaceRole::Main)).to_be_true();
		expect(s.is_grid_overlay()).to_be_true();
		expect(s.occludes_grid()).to_be_false();
		expect(s.dims_grid()).to_be_false();
		expect(s.input_transparent()).to_be_false();
		// `RenameUI::getGreyoutColsAndRows` (`rename_ui.cpp:58-61`): `*cols = 0b11`.
		expect(static_cast<uint64_t>(s.greyout_mask().cols)).to_equal(uint64_t{0b11});
		expect(static_cast<uint64_t>(s.greyout_mask().rows)).to_equal(uint64_t{0});
	});
});

describe rename_open("scenes::rename::RenameScene opening", $ {
	it("seeds the editor with the target's current name, cursor at the start", _ {
		select_language();
		spec::SongFixture f = kit_fixture();
		kit_of(f)->selectedDrum->drumName = "Snare";
		Rig r;
		r.open_rename(context_of(f), TargetId::Drum);

		// `RenameUI::opened`: `enteredText = getCurrentName()`; `QwertyUI::opened`:
		// `enteredTextEditPos = 0`. The whole existing name therefore sits to the cursor's right.
		expect(std::string{rename_scene().entered_text()}).to_equal(std::string{"Snare"});
		expect(rename_scene().cursor()).to_equal(0);
	});

	it("forgets its context on exit, so a re-entry without open() refuses", _ {
		select_language();
		spec::SongFixture f = kit_fixture();
		kit_of(f)->selectedDrum->drumName = "Snare";
		Rig r;
		r.open_rename(context_of(f), TargetId::Drum);
		r.tick(0);
		expect(std::string{rename_scene().entered_text()}).to_equal(std::string{"Snare"});

		r.button(kBackX, kBackY, true, 100);
		r.tick(100);
		expect(r.shell.stack().depth()).to_equal(size_t(0));

		// Fix round 1, Minor 1. `open()` is the only writer of `ctx_` and this scene is a singleton
		// that outlives every visit, so `on_exit` zeroes it: an entry that forgot its `open()` --
		// a `SceneId` restored from somewhere, a caller that only pushed -- must not validate
		// against the previous song's pointers. A zeroed context has a null clip/kit/output, every
		// target's `can` rejects that, and `on_enter` refuses. Fails closed.
		expect(rename_scene().on_enter(SceneId{rename::kRenameSceneKind,
		                                       static_cast<uint16_t>(TargetId::Drum)})).to_be_false();
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
	it("types a name on the grid and commits it on enter", _ {
		select_language();
		spec::SongFixture f = kit_fixture();
		Rig r;
		r.open_rename(context_of(f), TargetId::Drum);
		r.tick(0);
		r.clock = 10;

		r.type(std::string_view{"KICK"});
		// `qwerty_ui.cpp:334-343`: a letter following another letter is lowercased.
		expect(std::string{rename_scene().entered_text()}).to_equal(std::string{"Kick"});

		const Result done = commit(rename_scene());
		expect(std::string{kit_of(f)->selectedDrum->drumName}).to_equal(std::string{"Kick"});
		// `RenameUI::exitUI` -> `close()`: the editor leaves, and it names itself rather than
		// blindly popping whatever is on top.
		const ShellAction* removal = find_action(done, ShellAction::Kind::Remove);
		expect(removal != nullptr).to_be_true();
		expect(static_cast<int>(removal->pop_to)).to_equal(static_cast<int>(rename::kRenameSceneKind));
	});

	it("steps over a character that already matches the one under the cursor", _ {
		select_language();
		spec::SongFixture f = kit_fixture();
		kit_of(f)->selectedDrum->drumName = "Snare";
		Rig r;
		r.open_rename(context_of(f), TargetId::Drum);
		r.tick(0);
		r.clock = 10;

		// `qwerty_ui.cpp:305-317` -- `charCaseEqual`, so the case of the typed key does not matter.
		r.type('S');
		expect(std::string{rename_scene().entered_text()}).to_equal(std::string{"Snare"});
		expect(rename_scene().cursor()).to_equal(1);

		// A character that does NOT match truncates the tail and appends (`:350-353`).
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

		// `qwerty_ui.cpp:273-280`: "First character must be alphanumerical".
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
		// (`QwertyUI::padAction`, `qwerty_ui.cpp:219-220`: `processBackspace()` then
		// `setTimer(UI_SPECIFIC, 500)`).
		r.press(deluge::ui::grid::kTextKeyboardBackspace, 1000);
		r.tick(1000);
		expect(std::string{rename_scene().entered_text()}).to_equal(std::string{"Abcd"});
		// The scene must ask to be woken with no input pending, or nothing would run `render_grid`
		// and the hold would never be seen again. (The value is the nearer of the repeat deadline
		// and the cursor blink, so only the bound is asserted here; the cadence itself is pinned by
		// the ticks below, which is the assertion a wrong constant fails.)
		const std::optional<uint32_t> armed = rename_scene().next_timer();
		expect(armed.has_value()).to_be_true();
		expect(armed.value() <= rename::kBackspaceRepeatDelayMs).to_be_true();

		// One millisecond short of the delay: still four characters.
		r.tick(1000 + rename::kBackspaceRepeatDelayMs - 1);
		expect(std::string{rename_scene().entered_text()}).to_equal(std::string{"Abcd"});
		// On the delay: the first repeat.
		r.tick(1000 + rename::kBackspaceRepeatDelayMs);
		expect(std::string{rename_scene().entered_text()}).to_equal(std::string{"Abc"});

		// Then every 80 ms, not every 500.
		const uint32_t first = 1000 + rename::kBackspaceRepeatDelayMs;
		r.tick(first + rename::kBackspaceRepeatMs - 1);
		expect(std::string{rename_scene().entered_text()}).to_equal(std::string{"Abc"});
		r.tick(first + rename::kBackspaceRepeatMs);
		expect(std::string{rename_scene().entered_text()}).to_equal(std::string{"Ab"});
		r.tick(first + 2 * rename::kBackspaceRepeatMs);
		expect(std::string{rename_scene().entered_text()}).to_equal(std::string{"A"});
	});

	it("stops repeating the moment the backspace pad goes up", _ {
		select_language();
		spec::SongFixture f = kit_fixture();
		Rig r;
		r.open_rename(context_of(f), TargetId::Drum);
		r.tick(0);
		r.clock = 10;
		r.type(std::string_view{"ABCDE"});

		r.press(deluge::ui::grid::kTextKeyboardBackspace, 1000);
		r.tick(1000);
		r.tick(1000 + rename::kBackspaceRepeatDelayMs); // one repeat
		expect(std::string{rename_scene().entered_text()}).to_equal(std::string{"Abc"});

		// `padAction`'s release arm unsets the timer (`qwerty_ui.cpp:223-228`); here the hold is a
		// level, so letting go simply stops `held_index` naming the key.
		r.release(deluge::ui::grid::kTextKeyboardBackspace, 1600);
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
		// (`qwerty_ui.cpp:218`); the "normal keys" arm at `:254` arms no timer at all.
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
		// Fix round 2. The regression test for the gate-arm in `render_oled`, and it has to drive
		// *quiet* ticks to be one. `Shell::tick` renders an OLED surface only when
		// `s.gate.needs_frame(now, had_input_, false)`, the shell-wide widgets gate, or
		// `router_.repaint_requested(role)` says so (`shell/shell.cpp:334-343`). On a tick with no
		// input none of those fire on their own here: nothing in this scene arms the widgets gate
		// (`text_edit` is render-only and owns no clock, `ui/oled/text_edit.h`), and no `Repaint`
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
		r.tick(deluge::ui::kFlashTime);
		const std::string dark = r.fake.pbm(0);
		expect(dark != lit).to_be_true();

		// And back again on the next one -- a square wave, not a one-shot.
		r.tick(2 * deluge::ui::kFlashTime);
		expect(r.fake.pbm(0)).to_equal(lit);
	});

	it("blinks the end-of-string cursor and asks to be woken for it", _ {
		select_language();
		spec::SongFixture f = kit_fixture();
		kit_of(f)->selectedDrum->drumName = "Snare";
		Rig r;
		r.open_rename(context_of(f), TargetId::Drum);
		r.tick(0);

		// `setupBlink(..., shouldBlinkImmediately = true)` (`qwerty_ui.cpp:153`): the bar is lit on
		// the frame the editor opens, and `kFlashTime` (110 ms) later it is dark.
		// The cursor starts at 0 -- mid-string -- where `text_edit` draws the inversion instead and
		// there is nothing to flash, so nothing to wake for.
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
		expect(next.value() <= 110u).to_be_true();
	});

	it("walks the cursor with the horizontal encoder and stops at both ends", _ {
		select_language();
		spec::SongFixture f = kit_fixture();
		kit_of(f)->selectedDrum->drumName = "Snare";
		Rig r;
		r.open_rename(context_of(f), TargetId::Drum);
		r.tick(0);

		// `QwertyUI::horizontalEncoderAction` (`qwerty_ui.cpp:420-447`). The cursor starts at 0.
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
		other->drumName = "Snare";
		expect(kit_of(f)->selectedDrum == being_renamed).to_be_true();

		Rig r;
		r.open_rename(context_of(f), TargetId::Drum);
		r.tick(0);
		r.clock = 10;
		r.type(std::string_view{"SNARE"});
		expect(std::string{rename_scene().entered_text()}).to_equal(std::string{"Snare"});

		const Result rejected = commit(rename_scene());

		// `RenameDrumUI::trySetName` returns false, so `enterKeyPress` does nothing at all: no
		// write, no close, and the typed text is still there to be edited.
		expect(std::string{being_renamed->drumName}).to_equal(std::string{""});
		expect(has_action(rejected, ShellAction::Kind::Remove)).to_be_false();
		expect(has_action(rejected, ShellAction::Kind::Pop)).to_be_false();
		expect(std::string{rename_scene().entered_text()}).to_equal(std::string{"Snare"});

		// And the popup legacy raises from inside `trySetName`
		// (`display->displayPopup(STRING_FOR_DUPLICATE_NAMES)`).
		const ShellAction* popup = find_action(rejected, ShellAction::Kind::Popup);
		expect(popup != nullptr).to_be_true();
		expect(static_cast<int>(popup->popup_string))
		    .to_equal(static_cast<int>(String::STRING_FOR_DUPLICATE_NAMES));
	});

	it("tells the user about the rejection through the real input path", _ {
		select_language();
		spec::SongFixture f = spec::make_kit_song();
		Drum* other = spec::add_drum<SoundDrum>(f);
		Drum* being_renamed = spec::add_drum<SoundDrum>(f);
		other->drumName = "Snare";

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
		// over whatever is there (`shell/notices.cpp:283-312`) rather than queueing a notice -- so
		// the visible proof is the popup ON the stack, not the notice ring.
		bool popup_stacked = false;
		for (const deluge::ui::shell::Scene* scene : r.shell.stack().scenes()) {
			popup_stacked = popup_stacked || scene->kind() == SceneKind::Popup;
		}
		expect(popup_stacked).to_be_true();
		expect(std::string{being_renamed->drumName}).to_equal(std::string{""});
		// Still the scene taking input: a popup is drawn over it, never instead of it.
		expect(&r.shell.stack().input_top() == &rename_scene()).to_be_true();
	});

	it("accepts a name that only collides with the drum being renamed", _ {
		select_language();
		spec::SongFixture f = kit_fixture();
		Kit* kit = kit_of(f);
		kit->selectedDrum->drumName = "Snare";
		Rig r;
		r.open_rename(context_of(f), TargetId::Drum);
		r.tick(0);

		// `other != kit->selectedDrum` is the whole guard: re-committing a drum's own name is fine.
		const Result done = commit(rename_scene());
		expect(has_action(done, ShellAction::Kind::Remove)).to_be_true();
		expect(std::string{kit->selectedDrum->drumName}).to_equal(std::string{"Snare"});
	});

	it("refuses an empty drum name silently, and accepts an empty clip name", _ {
		select_language();
		{
			// `RenameUI::enterKeyPress`: "if (enteredText.empty() && !allowEmpty()) return;" --
			// no popup, no close, nothing written.
			spec::SongFixture f = kit_fixture();
			kit_of(f)->selectedDrum->drumName = "Snare";
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
			expect(std::string{kit_of(f)->selectedDrum->drumName}).to_equal(std::string{"Snare"});
		}
		{
			// Every other target takes the base `allowEmpty() == true`.
			spec::SongFixture f = spec::make_synth_song();
			f.clip->name = "Verse";
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
			kit_of(f)->selectedDrum->drumName = "Snare";
			Rig r;
			r.open_rename(context_of(f), TargetId::Drum);
			r.tick(0);
			r.clock = 10;
			r.type('K');
			expect(std::string{rename_scene().entered_text()}).to_equal(std::string{"K"});

			// `RenameUI::buttonAction`'s BACK arm -> `exitUI()`; the typed text is discarded.
			r.button(kBackX, kBackY, true, 100);
			r.tick(100);
			expect(std::string{kit_of(f)->selectedDrum->drumName}).to_equal(std::string{"Snare"});
			expect(r.shell.stack().depth()).to_equal(size_t(0));
		}
		{
			spec::SongFixture f = kit_fixture();
			kit_of(f)->selectedDrum->drumName = "Snare";
			Rig r;
			r.open_rename(context_of(f), TargetId::Drum);
			r.tick(0);
			// `RenameUI::padAction`: a pad outside the keyboard's columns exits.
			r.pad(deluge::ui::grid::kTextKeyboardCols, 0, 64, 100);
			r.tick(100);
			expect(std::string{kit_of(f)->selectedDrum->drumName}).to_equal(std::string{"Snare"});
			expect(r.shell.stack().depth()).to_equal(size_t(0));
		}
	});
});

describe rename_from_menu("scenes::rename::RenameScene from the menu", $ {
	it("opens on the drum target when drum_name_edit is focused, and steps back out after", _ {
		select_language();
		deluge::notify::dirty().drain();
		spec::SongFixture f = kit_fixture();
		kit_of(f)->selectedDrum->drumName = "Snare";
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
		r.tick(10);
		expect(menu_scene().focused() == &nodes::drum_name_edit).to_be_true();

		// `EditName::beginSession` -> `openUI(renameDrumUI)`: the editor is on top, seeded from the
		// drum, on the same tick the item was entered.
		expect(r.shell.stack().depth()).to_equal(size_t(2));
		expect(&r.shell.stack().input_top() == &rename_scene()).to_be_true();
		expect(static_cast<int>(rename_scene().target_id())).to_equal(static_cast<int>(TargetId::Drum));
		expect(std::string{rename_scene().entered_text()}).to_equal(std::string{"Snare"});

		// `soundEditor.shouldGoUpOneLevelOnBegin = true` (`edit_name.cpp:50`): closing the editor
		// lands one level ABOVE the item, not back on it -- otherwise `on_focus` would reopen it on
		// the very next tick.
		r.button(kBackX, kBackY, true, 100);
		r.tick(100);
		expect(r.shell.stack().depth()).to_equal(size_t(1));
		expect(static_cast<int>(menu_scene().state().depth)).to_equal(0);
		expect(menu_scene().focused() == &nodes::drum_actions).to_be_true();
		// ...and it stays closed.
		r.tick(110);
		expect(r.shell.stack().depth()).to_equal(size_t(1));
	});

	it("reopens the editor when a menu parked on the item is restored", _ {
		select_language();
		deluge::notify::dirty().drain();
		spec::SongFixture f = kit_fixture();
		kit_of(f)->selectedDrum->drumName = "Snare";

		Rig r;
		r.open_menu(nodes::drum_actions, nodes::drum_actions, context_of(f));
		r.tick(0);
		r.button(kSelectEncButtonX, kSelectEncButtonY, true, 10);
		r.tick(10);
		expect(&r.shell.stack().input_top() == &rename_scene()).to_be_true();

		// The state `replace_root` would carry across, taken while the menu is parked on the item.
		deluge::ui::shell::SceneState saved{};
		menu_scene().save_state(saved);

		// Get back out, so the restore is the only thing that could reopen anything.
		r.button(kBackX, kBackY, true, 100);
		r.tick(100);
		expect(r.shell.stack().depth()).to_equal(size_t(1));
		expect(menu_scene().focused() == &nodes::drum_actions).to_be_true();

		// Fix round 1, Minor 2. `restore_state` -> `enter_leaf` -> `CustomItem::on_focus` fires,
		// and the editor comes back -- legacy's `focusRegained` -> `beginScreen` -> `beginSession`
		// (`sound_editor.cpp:292,1015`), which `EditName` answers by opening `RenameUI`
		// unconditionally. Restoring the user's place means restoring the editor they were in.
		menu_scene().restore_state(saved);
		expect(menu_scene().focused() == &nodes::drum_name_edit).to_be_true();
		r.tick(200);
		expect(r.shell.stack().depth()).to_equal(size_t(2));
		expect(&r.shell.stack().input_top() == &rename_scene()).to_be_true();
		expect(std::string{rename_scene().entered_text()}).to_equal(std::string{"Snare"});
	});

	it("opens the clip and track targets from the sound root's edit-name page", _ {
		select_language();
		deluge::notify::dirty().drain();
		{
			spec::SongFixture f = spec::make_synth_song();
			f.clip->name = "Verse";
			Rig r;
			r.open_menu(*menu::MenuScene::roots()[static_cast<size_t>(MenuContext::Root::Sound)],
			            nodes::edit_clip_name, context_of(f));
			r.tick(0);
			expect(&r.shell.stack().input_top() == &rename_scene()).to_be_true();
			expect(static_cast<int>(rename_scene().target_id())).to_equal(static_cast<int>(TargetId::Clip));
			expect(std::string{rename_scene().entered_text()}).to_equal(std::string{"Verse"});
		}
		{
			spec::SongFixture f = spec::make_synth_song();
			f.instrument->name = "Lead";
			Rig r;
			r.open_menu(*menu::MenuScene::roots()[static_cast<size_t>(MenuContext::Root::Sound)],
			            nodes::edit_track_name, context_of(f));
			r.tick(0);
			expect(&r.shell.stack().input_top() == &rename_scene()).to_be_true();
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
			expect_golden(self, "../ui/scenes/rename_empty_48.pbm", r.fake.pbm(0));
		}
		{
			spec::SongFixture f = kit_fixture();
			Rig r64{board_64(), profile_64()};
			draw(r64, f, "");
			expect_golden(self, "../ui/scenes/rename_empty_64.pbm", r64.fake.pbm(0));
		}
	});

	it("draws a typed name the same on both shipped panel geometries", _ {
		select_language();
		{
			spec::SongFixture f = kit_fixture();
			Rig r;
			draw(r, f, "KICK");
			expect(std::string{rename_scene().entered_text()}).to_equal(std::string{"Kick"});
			expect_golden(self, "../ui/scenes/rename_typed_48.pbm", r.fake.pbm(0));
		}
		{
			spec::SongFixture f = kit_fixture();
			Rig r64{board_64(), profile_64()};
			draw(r64, f, "KICK");
			expect_golden(self, "../ui/scenes/rename_typed_64.pbm", r64.fake.pbm(0));
		}
	});
});
// clang-format on

/// Seven `describe` groups in one TU, so `DELUGE_SPEC` (one spec per entry point) is spelled out
/// here with `add_specs` instead -- as `menu_scene_spec.cpp` does.
int rename_scene_spec(int argc, char** const argv) {
	return CppSpec::parse(argc, argv)
	               .add_specs(rename_targets, rename_composition, rename_open, rename_typing, rename_rejection,
	                          rename_exits, rename_from_menu, rename_goldens)
	               .exec()
	               .is_success()
	           ? EXIT_SUCCESS
	           : EXIT_FAILURE;
}
