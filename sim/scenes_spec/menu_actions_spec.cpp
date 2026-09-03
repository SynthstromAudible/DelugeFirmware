/// @file
/// @brief The **action seam** (`scenes/menu/action.h`) and the five items that use it, driven
///        through a real `Shell` over the real menu tables.
///
/// What is covered here, and what is not, matters more than usual for this spec, because the seam
/// deliberately splits one legacy statement in half:
///
///   * **Covered:** that pressing select on each of the five items files exactly one
///     `MenuActionRequest`, with the right *identity* and the right payload; that the four pure
///     verbs never descend into a screen of their own (legacy's `shouldEnterSubmenu() == false`);
///     that `PerformEditorItem` renders legacy's own option list, seeds its cursor from the mode,
///     and reproduces both of `selectButtonPress`'s two return values; that nothing files an action
///     without a press, across quiet ticks; and that the five item-to-action pairings are pinned by
///     identity, with a permutation proof that the pinning is not vacuous.
///   * **NOT covered, and it cannot be from inside this layer:** that anything *happens*.
///     `MenuScene::take_action()` is an outbox with no consumer in this repository. Nothing in this
///     file asserts that a stem export starts, that a clip's length changes, or that the
///     performance view opens, because performing those is the wiring plan's half of the seam. A
///     green run here means the menu asks correctly, not that the firmware answers.
///
/// Legacy is the oracle wherever it can be reached: `performEditorMenu`'s real option list and its
/// real `readCurrentValue` mapping are both run here, on the real `performanceView`, and compared
/// against the port. The four pure verbs have no comparable surface -- their legacy bodies are one
/// `openUI`/`startStemExportProcess`/`setClipLengthEqualToSampleLength` statement each, which is
/// exactly the half this layer does not own -- so they are pinned by identity instead, the same way
/// task 6's five reset descriptors are.

#include "scenes/l10n_bridge.h"
#include "scenes/menu/action.h"
#include "scenes/menu/context.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/menu/node.h"
#include "scenes/menu/node_id.h"
#include "scenes/menu/nodes/actions.h"
#include "scenes/menu/nodes/nodes.h"
#include "scenes/profile.h"
#include "scenes/registry.h"

#include "notify/notice_queue.h"
#include "shell/profiles/deluge.h"
#include "shell/shell.h"

#include "gui/l10n/l10n.h"
#include "gui/l10n/strings.h"
#include "gui/menu_item/performance_session_view/editing_mode.h"
#include "gui/views/performance_view.h"
#include "model/song/song.h"

#include "support/song_fixture.h"

#include "spec/shell/support/fake_surfaces.h"

#include "cppspec.hpp"

#include <array>
#include <cstdint>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "spec/support/spec_main.h"

/// The real `performance_session_view::EditingMode` singleton. Declared bare at global scope
/// because `gui/ui/menus.cpp:2005` defines it there (that file's `using namespace` lines are what
/// make its `menu_item::` spelling work) and `gui/ui/menus.h` does not export it -- the same
/// situation `tables_3_spec.cpp:589` records for `flashStatusMenu` and friends.
extern deluge::gui::menu_item::performance_session_view::EditingMode performEditorMenu;

using namespace deluge::scenes;
using deluge::l10n::String;
using deluge::scenes::menu::MenuAction;
using deluge::scenes::menu::MenuActionRequest;
using deluge::scenes::menu::MenuContext;
using deluge::scenes::menu::Node;
using deluge::ui::shell::BoardProfile;
using deluge::ui::shell::SceneId;
using deluge::ui::shell::SceneKind;
using deluge::ui::shell::Shell;
using deluge::ui::shell::ShellConfig;

namespace {

namespace nodes = deluge::scenes::menu::nodes;

/// @brief Per-example reset, exactly `menu_scene_spec.cpp`'s: pick a language (nothing boots here,
///        so `l10n::chosenLanguage` would otherwise be null) and drain the process-global notice
///        queue so one example's notification cannot put an overlay on the next one's stack.
void select_language() {
	deluge::l10n::chosenLanguage = &deluge::l10n::built_in::english;
	deluge::notify::Notice discard;
	while (deluge::notify::notices().pop(discard)) {}
}

/// Board encoder index (`kDelugeEncoders`, `shell/profiles/deluge.cpp`): 3 = Select.
constexpr uint8_t kSelectEncoder = 3;
/// Matrix coordinates of the select-encoder button (`kMatrix`, `shell/profiles/deluge.cpp`).
constexpr uint8_t kSelectEncButtonX = 4, kSelectEncButtonY = 3;

/// @brief The real roots this spec navigates, so `assign_node_ids` numbers everything it touches.
constexpr const Node* kRoots[] = {&nodes::root_song, &nodes::root_audio_clip, &nodes::root_performance};

struct Rig {
	spec::SongFixture song = spec::make_synth_song();
	const BoardProfile& profile;
	deluge::ui::shell::SceneRegistry reg = scene_registry();
	uitest::FakeSurfaceSet fake;
	Shell shell{fake, profile, reg, deluge::ui::shell::Services{}, ShellConfig{}};

	Rig(const Rig&) = delete;
	Rig& operator=(const Rig&) = delete;

	explicit Rig(const DelugeBoard& board = deluge::ui::shell::kDelugeBoard, const BoardProfile& p = deluge_profile())
	    : profile(p), fake(board, p) {}

	MenuContext open(const Node& root) {
		// The song root is opened from a root UI, not over a clip -- and since E3 task 9 that matters:
		// `song_midi_learn` and `configure_song_macros` carry `relevance::is_session_view`, so a clip
		// context would list neither of the two rows this spec presses. `sound_editor.cpp:1764-1767`
		// is the arm being stood in for; `sessionView` of its three views, because that is the one
		// where both items are shown.
		const bool song_root = &root == &nodes::root_song;
		const MenuContext ctx = menu::context_for(*song.song, song_root ? nullptr : song.clip, 0, false,
		                                          /*session_or_arranger_root=*/song_root,
		                                          /*session_view_root=*/song_root, /*kit_global_fx=*/false);
		(void)menu::assign_node_ids(kRoots);
		menu_scene().open(root, ctx);
		shell.init(SceneId{SceneKind::FirstApp, menu::node_id(root)});
		return ctx;
	}

	void edges(uint8_t index, int16_t count, uint32_t at_ms) {
		shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_ENCODER, index, 0, count}, at_ms);
	}
	/// One detent of the (detented, 2 edges per detent) select encoder.
	void turn(int32_t detents, uint32_t at_ms) { edges(kSelectEncoder, static_cast<int16_t>(detents * 2), at_ms); }
	void button(uint8_t x, uint8_t y, bool on, uint32_t at_ms) {
		shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_BUTTON, x, y, static_cast<int16_t>(on ? 255 : 0)}, at_ms);
	}
	void enter(uint32_t at_ms) { button(kSelectEncButtonX, kSelectEncButtonY, true, at_ms); }
};

menu::MenuScene& scene() {
	return menu_scene();
}

/// @brief @p parent's visible children in the order the scene lists them.
///
/// The scene's own `visible_children` is private, and this must agree with it or every index below
/// is off. It is the same two-line rule (`menu_scene.cpp`: resolve `dynamic_children` first, then
/// admit on `relevant`), and none of the parents this spec walks has a `dynamic_children` hook --
/// asserted, rather than assumed, at the top of `visible_index_of`.
std::vector<const Node*> visible_children_of(const Node& parent, const MenuContext& ctx) {
	std::vector<const Node*> out;
	for (const Node* child : parent.children) {
		if (child->relevant == nullptr || child->relevant(ctx)) {
			out.push_back(child);
		}
	}
	return out;
}

/// @brief Where @p child sits in @p parent's visible list, or -1.
int32_t visible_index_of(const Node& parent, const Node& child, const MenuContext& ctx) {
	if (parent.dynamic_children != nullptr) {
		return -1; // none of this spec's parents has one; a table edit that adds one must be seen.
	}
	const std::vector<const Node*> kids = visible_children_of(parent, ctx);
	for (size_t i = 0; i < kids.size(); i++) {
		if (kids[i] == &child) {
			return static_cast<int32_t>(i);
		}
	}
	return -1;
}

/// @brief Open @p parent, walk the cursor onto @p child, and press select -- through the real
///        lowering, the real `list` widget and the real `enter_child`.
/// @return The action the scene filed as a result, drained exactly once.
MenuActionRequest press_child(Rig& r, const Node& parent, const Node& child) {
	const MenuContext ctx = r.open(parent);
	const int32_t index = visible_index_of(parent, child, ctx);
	if (index < 0) {
		return MenuActionRequest{};
	}
	// Settle the first frame, then walk. One detent per tick, which is what the widget's own
	// clamping walk expects.
	r.shell.tick(5);
	(void)scene().take_action(); // nothing may have been filed yet; assert that separately.
	for (int32_t i = 0; i < index; i++) {
		r.turn(1, static_cast<uint32_t>(10 + i * 10));
		r.shell.tick(static_cast<uint32_t>(15 + i * 10));
	}
	r.enter(static_cast<uint32_t>(100));
	r.shell.tick(static_cast<uint32_t>(110));
	return scene().take_action();
}

/// @brief The port's transcription of `EditingMode::readCurrentValue` (`editing_mode.h:34-45`),
///        which is the mapping the wiring plan owes a call to `set_performance_editing_mode` for.
///
/// Written here rather than in the library on purpose: the library holds a *mirror*, and this is the
/// rule that must keep it in step. The example below runs it against the real legacy item on the
/// real `performanceView`, which is the only place the two can be compared at all.
PerformanceEditingMode mode_from_view() {
	if (!performanceView.defaultEditingMode) {
		return PerformanceEditingMode::DISABLED;
	}
	if (!performanceView.editingParam) {
		return PerformanceEditingMode::VALUE;
	}
	return PerformanceEditingMode::PARAM;
}

/// @brief Save and restore the two `performanceView` bools around an example that moves them.
struct PerformViewGuard {
	bool saved_default = performanceView.defaultEditingMode;
	bool saved_param = performanceView.editingParam;
	PerformanceEditingMode saved_mirror = menu::performance_editing_mode();
	PerformViewGuard(const PerformViewGuard&) = delete;
	PerformViewGuard& operator=(const PerformViewGuard&) = delete;
	PerformViewGuard() = default;
	~PerformViewGuard() {
		performanceView.defaultEditingMode = saved_default;
		performanceView.editingParam = saved_param;
		menu::set_performance_editing_mode(saved_mirror);
	}
};

/// @brief Every item that files an action, paired with what it must file. Asserted by identity.
struct Pairing {
	const Node* node;
	const Node* parent;
	MenuAction action;
};

const std::array<Pairing, 4> kVerbs{{
    {&nodes::start_stem_export, &nodes::stem_export, MenuAction::StartStemExport},
    {&nodes::set_clip_length, &nodes::audio_clip_actions, MenuAction::SetClipLengthToSampleLength},
    {&nodes::song_midi_learn, &nodes::root_song, MenuAction::OpenSongMidiLearn},
    {&nodes::configure_song_macros, &nodes::root_song, MenuAction::OpenSongMacros},
}};

} // namespace

// clang-format off
describe menu_action_seam("scenes::menu action seam", $ {
	it("files nothing at all until something is pressed, across twenty quiet ticks", _ {
		select_language();
		Rig r;
		(void)r.open(nodes::stem_export);
		for (uint32_t t = 0; t < 20; t++) {
			r.shell.tick(10 + t * 10);
			expect(scene().take_action().engaged()).to_be_false();
		}
	});

	it("empties the outbox on the poll, so one press is not delivered twice", _ {
		select_language();
		Rig r;
		const MenuActionRequest first = press_child(r, nodes::stem_export, nodes::start_stem_export);
		expect(first.action == MenuAction::StartStemExport).to_be_true();
		// Reading empties the slot -- a dispatcher that polls every tick must not see the same export
		// request four times.
		expect(scene().take_action().engaged()).to_be_false();
		r.shell.tick(200);
		expect(scene().take_action().engaged()).to_be_false();
		// **This is not an exactly-once guarantee, and the example is not claiming one.**
		// `take_action()` is a one-slot mailbox with a silent overwrite (see its warning): a caller
		// that skips a poll gets the request late, and two filed between polls would lose the first.
		// What is guaranteed, and what is checked here, is that a delivered request is delivered once.
	});

	it("survives a re-root: a request already accepted is still delivered", _ {
		select_language();
		Rig r;
		const MenuContext ctx = r.open(nodes::stem_export);
		r.shell.tick(5);
		r.enter(10);
		r.shell.tick(20);
		// `retarget` drops `MenuState::pending_action`, but the drain has already moved this one into
		// the scene's own outbox, which it must not drop -- see `MenuScene::taken_action_`.
		menu_scene().open(nodes::root_song, ctx);
		expect(scene().take_action().action == MenuAction::StartStemExport).to_be_true();
	});
});

describe menu_action_items("scenes::menu the four pure verbs", $ {
	it("each files its own action, and only its own", _ {
		select_language();
		for (const Pairing& p : kVerbs) {
			Rig r;
			const MenuActionRequest got = press_child(r, *p.parent, *p.node);
			expect(static_cast<int>(got.action)).to_equal(static_cast<int>(p.action));
			expect(static_cast<int>(got.arg)).to_equal(0);
		}
	});

	it("opened as a root, files NOTHING -- there was no press", _ {
		// **E3 task 7 fix round 1 reversed this example, and the reversal is the point.** It used to
		// assert that a `SceneId::arg` deep link onto one of these items filed its action; that was
		// the port firing `selectButtonPress` with no press behind it. Legacy cannot: it never
		// navigates into these items at all (`shouldEnterSubmenu() == false`,
		// `gui/ui/sound_editor.cpp:382-385`), so the only way to reach the body is a press on the
		// parent's row, and `Toggle` -- the same shape -- guards the other way in explicitly
		// (`gui/menu_item/toggle.h:35-37`). `MenuScene::enter_leaf`'s `FocusCause` is that
		// discriminator and `CustomItem::on_focus_is_press` is its scope.
		//
		// A deep link onto one of these therefore shows the item's own title and does nothing --
		// which is still the one path `ActionItem::render` is reached by, so it is still pinned
		// here. Starting a stem export because a scene id was restored is not a defensible reading
		// of "put the user back where they were".
		select_language();
		Rig r;
		(void)r.open(nodes::start_stem_export);
		r.shell.tick(10);
		expect(scene().take_action().engaged()).to_be_false();
		expect(scene().focused() == &nodes::start_stem_export).to_be_true();
		expect(static_cast<int>(scene().state().depth)).to_equal(0);
		// And it stays quiet: nothing re-fires on later ticks either.
		for (uint32_t t = 0; t < 5; t++) {
			r.shell.tick(20 + t * 10);
			expect(scene().take_action().engaged()).to_be_false();
		}
	});

	it("still files on a real press, so the guard is narrow rather than a mute button", _ {
		// The other side of the same discriminator, over all four items: a descent from the parent
		// list is `FocusCause::Descended` and still acts. Without this the example above could be
		// satisfied by an item that never fires at all.
		select_language();
		for (const Pairing& p : kVerbs) {
			Rig r;
			const MenuActionRequest got = press_child(r, *p.parent, *p.node);
			expect(got.engaged()).to_be_true();
			expect(static_cast<int>(got.action)).to_equal(static_cast<int>(p.action));
		}
	});

	it("pins the item-to-action mapping by identity, and the pinning is not vacuous", _ {
		select_language();
		// The mapping under test, read off the shipped items rather than re-derived.
		std::vector<int> observed;
		for (const Pairing& p : kVerbs) {
			const auto* item = dynamic_cast<const nodes::ActionItem*>(p.node->custom);
			expect(item != nullptr).to_be_true();
			observed.push_back(item != nullptr ? static_cast<int>(item->action()) : -1);
		}
		std::vector<int> expected;
		for (const Pairing& p : kVerbs) {
			expected.push_back(static_cast<int>(p.action));
		}
		expect(observed).to_equal(expected);

		// ... and a single transposition of it must NOT compare equal, or the assertion above could
		// be passing on an all-`None` list. Task 11's pad-colour example carries the same proof.
		std::vector<int> swapped = observed;
		std::swap(swapped[0], swapped[2]);
		expect(swapped == expected).to_be_false();

		// The four actions are pairwise distinct, so "files the right one" is a real discriminator.
		std::set<int> distinct(observed.begin(), observed.end());
		expect(static_cast<int>(distinct.size())).to_equal(4);
	});

	it("never shows a screen of its own: the press leaves the parent list up", _ {
		select_language();
		for (const Pairing& p : kVerbs) {
			Rig r;
			const MenuActionRequest got = press_child(r, *p.parent, *p.node);
			expect(got.engaged()).to_be_true();
			// Legacy's `shouldEnterSubmenu() == false` (`gui/ui/sound_editor.cpp:382-385`): the item
			// is never navigated into. The port descends for the width of one `after_render` and
			// ascends inside it, so by the time the tick is over the parent is focused again.
			expect(scene().focused() == p.parent).to_be_true();
			expect(static_cast<int>(scene().state().depth)).to_equal(0);
		}
	});
});

describe menu_action_perform("scenes::menu PerformEditorItem", $ {
	it("offers exactly the options the real legacy EditingMode offers", _ {
		select_language();
		Rig r;
		(void)r.open(nodes::root_performance);

		// Both sides driven: legacy's own list, from the real singleton...
		const auto legacy = performEditorMenu.getOptions(deluge::gui::menu_item::Selection::OptType::FULL);
		std::vector<std::string> legacy_names;
		for (const std::string_view v : legacy) {
			legacy_names.emplace_back(v);
		}
		// ... and the port's, resolved through the same l10n table the item uses.
		const std::vector<std::string> port_names{
		    std::string(text_lookup(static_cast<uint16_t>(String::STRING_FOR_DISABLED))),
		    std::string(text_lookup(static_cast<uint16_t>(String::STRING_FOR_PERFORM_EDIT_VALUE))),
		    std::string(text_lookup(static_cast<uint16_t>(String::STRING_FOR_PERFORM_EDIT_PARAM))),
		};
		expect(port_names).to_equal(legacy_names);
	});

	it("maps the performance view's two bools onto a mode exactly as legacy's readCurrentValue does", _ {
		select_language();
		PerformViewGuard guard;

		// All three reachable states of the pair. Legacy is *driven* for each -- `readCurrentValue`
		// is what fills its value, and skipping the call would compare against whatever the previous
		// example left behind (the trap task 11 fix round 1 recorded).
		const std::array<std::pair<bool, bool>, 3> states{{{false, false}, {true, false}, {true, true}}};
		for (const auto& [default_mode, editing_param] : states) {
			performanceView.defaultEditingMode = default_mode;
			performanceView.editingParam = editing_param;

			performEditorMenu.readCurrentValue();
			const int32_t legacy_value = performEditorMenu.getValue();

			expect(static_cast<int>(mode_from_view())).to_equal(static_cast<int>(legacy_value));
		}
		// `{false, true}` is unreachable in legacy -- `readCurrentValue` tests `defaultEditingMode`
		// first -- and maps to DISABLED under the same rule, so it is asserted rather than skipped.
		performanceView.defaultEditingMode = false;
		performanceView.editingParam = true;
		performEditorMenu.readCurrentValue();
		expect(static_cast<int>(mode_from_view()))
		    .to_equal(static_cast<int>(performEditorMenu.getValue()));
	});

	it("seeds its cursor from the mirrored mode, the way beginSession seeds from the view", _ {
		select_language();
		PerformViewGuard guard;
		for (const auto mode :
		     {PerformanceEditingMode::DISABLED, PerformanceEditingMode::VALUE, PerformanceEditingMode::PARAM}) {
			Rig r;
			menu::set_performance_editing_mode(mode);
			(void)r.open(nodes::perform_editor);
			r.shell.tick(10);
			expect(static_cast<int>(scene().state().list[0].cursor)).to_equal(static_cast<int>(mode));
			expect(scene().take_action().engaged()).to_be_false(); // opening files nothing
		}
	});

	it("files OpenPerformanceView carrying the chosen mode, and stays on the page", _ {
		select_language();
		for (const auto mode : {PerformanceEditingMode::VALUE, PerformanceEditingMode::PARAM}) {
			PerformViewGuard guard;
			Rig r;
			menu::set_performance_editing_mode(PerformanceEditingMode::DISABLED);
			(void)r.open(nodes::perform_editor);
			r.shell.tick(5);
			for (int32_t i = 0; i < static_cast<int32_t>(mode); i++) {
				r.turn(1, static_cast<uint32_t>(10 + i * 10));
				r.shell.tick(static_cast<uint32_t>(15 + i * 10));
			}
			r.enter(100);
			r.shell.tick(110);

			const MenuActionRequest got = scene().take_action();
			expect(got.action == MenuAction::OpenPerformanceView).to_be_true();
			expect(static_cast<int>(got.arg)).to_equal(static_cast<int>(mode));
			// `:55-60` -- the press is what writes the two bools legacy keeps on the view.
			expect(static_cast<int>(menu::performance_editing_mode())).to_equal(static_cast<int>(mode));
			// `:73` -- `NO_NAVIGATION`: legacy does not leave this page.
			expect(scene().focused() == &nodes::perform_editor).to_be_true();
		}
	});

	it("goes up a level on DISABLED and files nothing, which is legacy's return nullptr", _ {
		select_language();
		PerformViewGuard guard;
		Rig r;
		menu::set_performance_editing_mode(PerformanceEditingMode::VALUE);
		const MenuContext ctx = r.open(nodes::root_performance);
		const int32_t index = visible_index_of(nodes::root_performance, nodes::perform_editor, ctx);
		expect(index).to_equal(0);
		r.shell.tick(5);
		r.enter(10);
		r.shell.tick(20);
		expect(scene().focused() == &nodes::perform_editor).to_be_true();
		expect(scene().take_action().engaged()).to_be_false();

		// Back to DISABLED, then press.
		r.turn(-1, 30);
		r.shell.tick(40);
		expect(static_cast<int>(scene().state().list[1].cursor))
		    .to_equal(static_cast<int>(PerformanceEditingMode::DISABLED));
		r.enter(50);
		r.shell.tick(60);

		expect(scene().take_action().engaged()).to_be_false();
		expect(scene().focused() == &nodes::root_performance).to_be_true();
		// And the mirror is untouched -- legacy's DISABLED arm writes nothing either.
		expect(static_cast<int>(menu::performance_editing_mode()))
		    .to_equal(static_cast<int>(PerformanceEditingMode::VALUE));
	});

	it("files nothing across twenty quiet ticks while sitting on a non-DISABLED option", _ {
		select_language();
		PerformViewGuard guard;
		Rig r;
		menu::set_performance_editing_mode(PerformanceEditingMode::PARAM);
		(void)r.open(nodes::perform_editor);
		for (uint32_t t = 0; t < 20; t++) {
			r.shell.tick(10 + t * 10);
			expect(scene().take_action().engaged()).to_be_false();
		}
	});
});
// clang-format on

/// Three `describe` groups in one TU, so `DELUGE_SPEC` (one spec per entry point) is spelled out
/// here with `add_specs` instead -- exactly as `context_spec.cpp` does.
int menu_actions_spec(int argc, char** const argv) {
	return CppSpec::parse(argc, argv)
	               .add_specs(menu_action_seam, menu_action_items, menu_action_perform)
	               .exec()
	               .is_success()
	           ? EXIT_SUCCESS
	           : EXIT_FAILURE;
}
