/// @file
/// @brief Task 6: `MenuContext::Root::NoteEditor`/`NoteRowEditor`, everything indexed by `Root`
///        that this task wires (`MenuScene::roots()`, the two shortcut tables, `greyout_mask()`),
///        the note-row sequence-direction bridge arm, and `openShellMenu`'s `NoteMode`.
///
/// `MenuScene::grid_pad_claim()` for these two roots is `menu_grid_claim_spec.cpp`'s (extended,
/// not here) -- it already owns every other root's row of that same table.

#include "scenes/menu/bindings.h"
#include "scenes/menu/context.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/menu/nodes/nodes.h"
#include "scenes/menu/shortcuts.h"
#include "scenes/registry.h"

#include "gui/ui/ui.h"
#include "util/shell_menu_entry.h"

#include "definitions_cxx.hpp"
#include "model/action/action_logger.h"
#include "model/clip/instrument_clip.h"
#include "model/note/note_row.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h"
#include "processing/sound/sound_drum.h"

#include "support/fake_note_bridge.h"
#include "support/song_fixture.h"

#include "spec/ui/support/frames.h"

#include "cppspec.hpp"

#include <cstdint>

#include "spec/support/spec_main.h"

using namespace deluge::scenes;
using deluge::scenes::menu::MenuContext;
namespace menu = deluge::scenes::menu;
namespace nodes = deluge::scenes::menu::nodes;
namespace spec = deluge::scenes::spec;
namespace entry = deluge::gui::ui;

namespace {

/// @brief True when every pad in @p layer is null -- both shortcut layers of a root with no table.
bool all_null(std::span<const menu::Node* const> layer) {
	for (const menu::Node* cell : layer) {
		if (cell != nullptr) {
			return false;
		}
	}
	return true;
}

/// @brief Save and restore the `ShellMenu` toggle -- `menu_entry_spec.cpp`'s `ToggleGuard`.
struct ToggleGuard {
	RuntimeFeatureStateToggle saved =
	    static_cast<RuntimeFeatureStateToggle>(runtimeFeatureSettings.get(RuntimeFeatureSettingType::ShellMenu));
	explicit ToggleGuard(RuntimeFeatureStateToggle on) {
		runtimeFeatureSettings.set(RuntimeFeatureSettingType::ShellMenu, on);
	}
	~ToggleGuard() { runtimeFeatureSettings.set(RuntimeFeatureSettingType::ShellMenu, saved); }
};

/// @brief `menu_entry_spec.cpp`'s `closeGuest`: put the UI hierarchy back to "nothing open".
void closeGuest() {
	nullifyUIs();
}

} // namespace

// clang-format off
describe note_roots_table("scenes::menu MenuContext::Root::NoteEditor / NoteRowEditor: the tables", $ {
	it("MenuScene::roots() maps both roots to their nodes", _ {
		const std::span<const menu::Node* const> r = menu::MenuScene::roots();
		expect(r[static_cast<size_t>(MenuContext::Root::NoteEditor)] == &nodes::note_editor_root).to_be_true();
		expect(r[static_cast<size_t>(MenuContext::Root::NoteRowEditor)] == &nodes::note_row_editor_root).to_be_true();
	});

	it("both roots' shortcut layers are empty in both shortcut versions, and light no pad even for "
	   "a node with a real shortcut elsewhere", _ {
		const menu::ShortcutsVersion saved = menu::shortcuts_version();
		for (menu::ShortcutsVersion version : {menu::ShortcutsVersion::V1, menu::ShortcutsVersion::V3}) {
			menu::set_shortcuts_version(version);
			for (MenuContext::Root root : {MenuContext::Root::NoteEditor, MenuContext::Root::NoteRowEditor}) {
				expect(all_null(menu::shortcut_layer(root, /*second_layer=*/false))).to_be_true();
				expect(all_null(menu::shortcut_layer(root, /*second_layer=*/true))).to_be_true();
			}
		}
		menu::set_shortcuts_version(saved);

		// Fix round 1, IMPORTANT 3: `render_grid` only ever paints `shortcut_pos(ctx_.root,
		// *focused())` and that node's source pads (`menu_scene.cpp:1268-1310`). `note_editor_root`
		// itself is in NO root's table, so opening the scene on it (the original shape of this
		// example) could never observe a leak -- it would pass even if `kByRootV3`/`kByRootV1`'s
		// note-root slots pointed at a real table by mistake. `nodes::lpf_freq` is a real cell under
		// `Root::Sound` (`shortcuts_spec.cpp` pins it at (8, 7)), so focusing on IT under each note
		// root is what actually exercises the table lookup this claim is about.
		spec::SongFixture fx = spec::make_synth_song();
		MenuContext ctx = menu::context_for(*fx.song, fx.clip, 0, false, false, false, false);
		auto& scene = menu_scene();

		// A render at tick 0 falls in the cycle's SOURCE phase (`phase < kSourceBlinkMs`), which
		// suppresses the focused-param pad regardless of the table (`render_grid`'s `!source_phase`
		// guard) -- so it could never light anything and would pass vacuously. But the cycle's own
		// anchor is (re-)zeroed at `now` the first time a newly-focused node renders at all
		// (`render_grid`'s `st_.blink_node != node` arm), so the FIRST render after `open()` is
		// always phase 0 no matter which tick it is asked for. A second render, `kSourceBlinkMs + 1`
		// after that same anchor, is what actually reaches the param-flash phase.
		auto any_pad_lit = [&](const menu::Node& node, MenuContext::Root root) {
			ctx.root = root;
			scene.open(node, ctx);
			deluge::ui::test::GridRig warmup;
			warmup.begin_tick(0);
			scene.render_grid(warmup.frame); // anchors the cycle at t = 0
			const uint32_t anchor = scene.state().blink_anchor_ms;

			deluge::ui::test::GridRig grid;
			grid.begin_tick(anchor + menu::kSourceBlinkMs + 1);
			scene.render_grid(grid.frame);
			for (int32_t y = 0; y < grid.grid.dims().rows; y++) {
				for (int32_t x = 0; x < grid.grid.dims().cols(); x++) {
					const RGB& c = grid.grid.at(static_cast<uint8_t>(x), static_cast<uint8_t>(y));
					if (c.r != 0 || c.g != 0 || c.b != 0) {
						return true;
					}
				}
			}
			return false;
		};

		// Positive control: the identical node and tick, under the root it REALLY belongs to --
		// proves the harness lights a pad when the table says to, so the two `false`s below are not
		// vacuous (a broken `GridRig`/timing setup would fail this one too).
		expect(any_pad_lit(nodes::lpf_freq, MenuContext::Root::Sound)).to_be_true();

		expect(any_pad_lit(nodes::lpf_freq, MenuContext::Root::NoteEditor)).to_be_false();
		expect(any_pad_lit(nodes::lpf_freq, MenuContext::Root::NoteRowEditor)).to_be_false();
	});

	it("greyout_mask is {0x03, 0} on the note editor and empty on the note-row editor", _ {
		// `gui/ui/sound_editor.cpp:274-281`: `inNoteEditor()` greys the sidebar only (`cols = 0x03`,
		// `rows = 0`); `inNoteRowEditor()` greys nothing (`doGreyout = false`).
		spec::SongFixture fx = spec::make_synth_song();
		MenuContext ctx = menu::context_for(*fx.song, fx.clip, 0, false, false, false, false);
		auto& scene = menu_scene();

		ctx.root = MenuContext::Root::NoteEditor;
		scene.open(nodes::note_editor_root, ctx);
		const deluge::ui::grid::GreyoutMask editor_mask = scene.greyout_mask();
		expect(editor_mask.cols).to_equal(uint32_t{0x03});
		expect(editor_mask.rows).to_equal(uint32_t{0});

		ctx.root = MenuContext::Root::NoteRowEditor;
		scene.open(nodes::note_row_editor_root, ctx);
		const deluge::ui::grid::GreyoutMask row_mask = scene.greyout_mask();
		expect(row_mask.cols).to_equal(uint32_t{0});
		expect(row_mask.rows).to_equal(uint32_t{0});
	});
});

describe note_roots_sequence_direction("scenes::menu bindings::read/write_sequence_direction under Root::NoteRowEditor", $ {
	it("on a non-kit clip, reads and writes the auditioned row through the bridge, and offers NONE", _ {
		spec::SongFixture fx = spec::make_synth_song();
		MenuContext ctx = menu::context_for(*fx.song, fx.clip, 0, false, false, false, false);
		expect(ctx.is_kit).to_be_false();
		ctx.root = MenuContext::Root::NoteRowEditor;

		spec::FakeNoteBridgeGuard bridge;
		bridge.fake.row_sequence_direction_value = true;
		bridge.fake.row_sequence_direction_out_value = static_cast<int32_t>(SequenceDirection::PINGPONG);

		bool from_note_row = false;
		const int32_t read = menu::bindings::read_sequence_direction(ctx, &from_note_row);
		expect(read).to_equal(static_cast<int32_t>(SequenceDirection::PINGPONG));
		expect(from_note_row).to_be_true();
		expect(bridge.fake.row_sequence_direction_calls > 0).to_be_true();

		menu::bindings::write_sequence_direction(ctx, static_cast<int32_t>(SequenceDirection::REVERSE));
		expect(bridge.fake.set_row_sequence_direction_calls).to_equal(1);
		expect(bridge.fake.set_row_sequence_direction_last_value)
		    .to_equal(static_cast<int32_t>(SequenceDirection::REVERSE));
	});

	it("falls back to the clip's own mode when the bridge answers false (no auditioned row)", _ {
		spec::SongFixture fx = spec::make_synth_song();
		MenuContext ctx = menu::context_for(*fx.song, fx.clip, 0, false, false, false, false);
		ctx.root = MenuContext::Root::NoteRowEditor;
		fx.clip->sequenceDirectionMode = SequenceDirection::REVERSE;

		spec::FakeNoteBridgeGuard bridge;
		bridge.fake.row_sequence_direction_value = false;

		bool from_note_row = true; // seeded true so the fallback's `false` is asserted, not assumed
		const int32_t read = menu::bindings::read_sequence_direction(ctx, &from_note_row);
		expect(read).to_equal(static_cast<int32_t>(SequenceDirection::REVERSE));
		expect(from_note_row).to_be_false();
		expect(bridge.fake.row_sequence_direction_calls > 0).to_be_true();

		menu::bindings::write_sequence_direction(ctx, static_cast<int32_t>(SequenceDirection::PINGPONG));
		expect(bridge.fake.set_row_sequence_direction_calls).to_equal(0);
		expect(static_cast<int32_t>(fx.clip->sequenceDirectionMode))
		    .to_equal(static_cast<int32_t>(SequenceDirection::PINGPONG));
	});

	it("on a kit clip, never asks the bridge, and keeps the kit-row arm unchanged", _ {
		// Resolution 3: `Root::NoteRowEditor` on a kit falls back to today's arms unchanged --
		// `selectedNoteRow` is only ever latched by legacy's OWN non-kit else-if
		// (`sequence/direction.h:117-119`), so a kit clip has no equivalent to model here at all.
		spec::SongFixture fx = spec::make_kit_song();
		SoundDrum* drum = spec::add_drum<SoundDrum>(fx);
		MenuContext ctx = menu::context_for(*fx.song, fx.clip, 0, false, false, false, /*kit_global_fx=*/false);
		expect(ctx.resolved).to_be_true();
		expect(ctx.is_kit).to_be_true();
		ctx.root = MenuContext::Root::NoteRowEditor;

		NoteRow* row = fx.clip->getNoteRowForDrum(drum);
		expect(row != nullptr).to_be_true();
		if (row == nullptr) {
			return; // fix round 1, minor c: a broken fixture must fail loudly, not skip silently
		}

		spec::FakeNoteBridgeGuard bridge;
		bridge.fake.row_sequence_direction_value = true; // would answer wrongly if ever consulted

		bool from_note_row = false;
		const int32_t read = menu::bindings::read_sequence_direction(ctx, &from_note_row);
		expect(read).to_equal(static_cast<int32_t>(row->sequenceDirectionMode));
		expect(from_note_row).to_be_true(); // the kit-row arm's own note row, not the bridge's
		expect(bridge.fake.row_sequence_direction_calls).to_equal(0);

		menu::bindings::write_sequence_direction(ctx, static_cast<int32_t>(SequenceDirection::PINGPONG));
		expect(bridge.fake.set_row_sequence_direction_calls).to_equal(0);
		expect(static_cast<int32_t>(row->sequenceDirectionMode))
		    .to_equal(static_cast<int32_t>(SequenceDirection::PINGPONG));
	});

	it("under any other root, is unchanged: the bridge is never consulted", _ {
		spec::SongFixture fx = spec::make_synth_song();
		MenuContext ctx = menu::context_for(*fx.song, fx.clip, 0, false, false, false, false);
		expect(ctx.root == MenuContext::Root::NoteRowEditor).to_be_false();
		fx.clip->sequenceDirectionMode = SequenceDirection::REVERSE;

		spec::FakeNoteBridgeGuard bridge;
		bridge.fake.row_sequence_direction_value = true;
		bridge.fake.row_sequence_direction_out_value = static_cast<int32_t>(SequenceDirection::PINGPONG);

		const int32_t read = menu::bindings::read_sequence_direction(ctx, nullptr);
		expect(read).to_equal(static_cast<int32_t>(SequenceDirection::REVERSE));
		expect(bridge.fake.row_sequence_direction_calls).to_equal(0);

		menu::bindings::write_sequence_direction(ctx, static_cast<int32_t>(SequenceDirection::PINGPONG));
		expect(bridge.fake.set_row_sequence_direction_calls).to_equal(0);
		expect(static_cast<int32_t>(fx.clip->sequenceDirectionMode))
		    .to_equal(static_cast<int32_t>(SequenceDirection::PINGPONG));
	});
});

describe note_roots_open_shell_menu("openShellMenu's NoteMode", $ {
	it("opens with menu_context().root == NoteEditor", _ {
		ToggleGuard on{RuntimeFeatureStateToggle::On};
		spec::SongFixture fx = spec::make_synth_song();

		expect(entry::openShellMenu(fx.clip, 0, false, false, false, false, entry::ShellMenuNode::ContextRoot,
		                            entry::NoteMode::NoteEditor))
		    .to_be_true();
		expect(deluge::scenes::menu_scene().focused() == &nodes::note_editor_root).to_be_true();
		expect(deluge::scenes::menu_scene().menu_context().root == MenuContext::Root::NoteEditor).to_be_true();
		closeGuest();
	});

	it("opens with menu_context().root == NoteRowEditor", _ {
		ToggleGuard on{RuntimeFeatureStateToggle::On};
		spec::SongFixture fx = spec::make_synth_song();

		expect(entry::openShellMenu(fx.clip, 0, false, false, false, false, entry::ShellMenuNode::ContextRoot,
		                            entry::NoteMode::NoteRowEditor))
		    .to_be_true();
		expect(deluge::scenes::menu_scene().focused() == &nodes::note_row_editor_root).to_be_true();
		expect(deluge::scenes::menu_scene().menu_context().root == MenuContext::Root::NoteRowEditor).to_be_true();
		closeGuest();
	});

	it("on a kit with affectEntire true, leaves affectEntire false in both the clip and the "
	   "installed context", _ {
		// `instrument_clip_view.cpp:3590-3598`: a note is selected on a kit row (so `context_for`
		// resolves through the drum arm, not the kit-global-fx one -- `kitGlobalFx` here is false,
		// exactly as the kit-row shortcut path's own call is), while the clip's OWN `affectEntire`
		// is independently true. Both facts at once is the case `context.h`'s own note on
		// `affect_entire` names.
		//
		// Fix round 1, IMPORTANT 1: `ctx.affect_entire` must follow the clear, not merely the clip's
		// own member -- `sequence::Direction::getIndividualNoteRow` reads `clip->affectEntire` LIVE
		// (`gui/menu_item/sequence/direction.h:44`), after legacy's identical clear, so a stale
		// `true` left in the installed context would make `bindings::individual_note_row`
		// (`bindings.cpp:832`) keep reading/writing the kit's own mode instead of the selected
		// drum's row -- see the dedicated example below.
		ToggleGuard on{RuntimeFeatureStateToggle::On};
		spec::SongFixture fx = spec::make_kit_song();
		(void)spec::add_drum<SoundDrum>(fx);
		fx.clip->affectEntire = true;

		expect(entry::openShellMenu(fx.clip, 0, false, false, false, /*kitGlobalFx=*/false,
		                            entry::ShellMenuNode::ContextRoot, entry::NoteMode::NoteEditor))
		    .to_be_true();
		expect(fx.clip->affectEntire).to_be_false();
		expect(deluge::scenes::menu_scene().menu_context().affect_entire).to_be_false();
		expect(deluge::scenes::menu_scene().menu_context().root == MenuContext::Root::NoteEditor).to_be_true();
		closeGuest();
	});

	it("the same kit affectEntire clear applies to NoteRowEditor too", _ {
		// Resolution 1: legacy does the identical clear in `enterNoteRowEditor`
		// (`instrument_clip_view.cpp:3813-3818`), after `soundEditor.setup` and before `openUI`.
		ToggleGuard on{RuntimeFeatureStateToggle::On};
		spec::SongFixture fx = spec::make_kit_song();
		(void)spec::add_drum<SoundDrum>(fx);
		fx.clip->affectEntire = true;

		expect(entry::openShellMenu(fx.clip, 0, false, false, false, /*kitGlobalFx=*/false,
		                            entry::ShellMenuNode::ContextRoot, entry::NoteMode::NoteRowEditor))
		    .to_be_true();
		expect(fx.clip->affectEntire).to_be_false();
		expect(deluge::scenes::menu_scene().menu_context().affect_entire).to_be_false();
		expect(deluge::scenes::menu_scene().menu_context().root == MenuContext::Root::NoteRowEditor).to_be_true();
		closeGuest();
	});

	it("a kit note-row editor edits the selected drum's row, not the kit's own mode, once "
	   "affectEntire is cleared", _ {
		// The load-bearing consequence of the fix above, driven through the real binding rather than
		// just read back off `MenuContext`: `bindings::read_sequence_direction`
		// (`bindings.cpp:892-915`) tests `ctx.root == Root::NoteRowEditor && !ctx.is_kit` first (the
		// non-kit bridge arm), then falls through to `individual_note_row`'s kit-row arm
		// (`bindings.cpp:832`, `!ctx.affect_entire && ctx.is_kit`) -- which only takes the SELECTED
		// DRUM's row when `ctx.affect_entire` is false, exactly as legacy's live
		// `clip->affectEntire` read does after its own clear.
		ToggleGuard on{RuntimeFeatureStateToggle::On};
		spec::SongFixture fx = spec::make_kit_song();
		SoundDrum* drum = spec::add_drum<SoundDrum>(fx);
		fx.clip->affectEntire = true;
		NoteRow* row = fx.clip->getNoteRowForDrum(drum);
		expect(row != nullptr).to_be_true();
		if (row == nullptr) {
			closeGuest();
			return;
		}
		row->sequenceDirectionMode = SequenceDirection::PINGPONG;
		fx.clip->sequenceDirectionMode = SequenceDirection::REVERSE; // must NOT be what gets read

		expect(entry::openShellMenu(fx.clip, 0, false, false, false, /*kitGlobalFx=*/false,
		                            entry::ShellMenuNode::ContextRoot, entry::NoteMode::NoteRowEditor))
		    .to_be_true();

		bool from_note_row = false;
		const int32_t read =
		    menu::bindings::read_sequence_direction(deluge::scenes::menu_scene().menu_context(), &from_note_row);
		expect(from_note_row).to_be_true();
		expect(read).to_equal(static_cast<int32_t>(SequenceDirection::PINGPONG));
		closeGuest();
	});
});

describe note_roots_undo_log("pre-review fix: openShellMenu's NoteMode must not wipe the undo log", $ {
	// `SoundEditor::setup`'s undo-log wipe (`sound_editor.cpp:1769-1771`) is inside its `item ==
	// nullptr` arm only. `enterNoteEditor`/`enterNoteRowEditor` never take that arm -- each calls
	// `soundEditor.setup(clip, &noteEditorRootMenu)` / `soundEditor.setup(clip, &noteRowEditorRootMenu)`
	// (`instrument_clip_view.cpp:3590`, `:3758`), an explicit item -- so legacy never wipes on a
	// note-editor/note-row-editor open. `openShellMenu`'s own wipe (`gui/ui/shell_menu_entry.cpp`)
	// used to fire whenever `node == ShellMenuNode::ContextRoot && clip != nullptr`, which is true
	// for every `NoteMode` call too (neither note root has its own `ShellMenuNode`), so it wiped
	// where legacy does not. Fixed by adding `&& noteMode == NoteMode::None` to that condition.

	it("NoteMode::NoteEditor leaves an existing undo log intact", _ {
		ToggleGuard on{RuntimeFeatureStateToggle::On};
		spec::SongFixture fx = spec::make_synth_song();

		// Seed a real logged action while SOME guest is open -- `ActionLogger::getNewAction` refuses
		// unless `getCurrentUI() == getRootUI()` (`model/action/action_logger.cpp:98-102`), exactly
		// `menu_entry_spec.cpp`'s own undo-log examples note -- then close it again.
		expect(entry::openShellMenu(nullptr, 0, false, false, false, false)).to_be_true();
		actionLogger.deleteAllLogs();
		(void)actionLogger.getNewAction(ActionType::MISC, ActionAddition::NOT_ALLOWED);
		expect(actionLogger.firstAction[BEFORE] != nullptr).to_be_true();
		closeGuest();

		// The note editor, opened on a real clip: the log must survive.
		expect(entry::openShellMenu(fx.clip, 0, false, false, false, false, entry::ShellMenuNode::ContextRoot,
		                            entry::NoteMode::NoteEditor))
		    .to_be_true();
		expect(actionLogger.firstAction[BEFORE] != nullptr).to_be_true();
		closeGuest();
	});

	it("NoteMode::NoteRowEditor leaves an existing undo log intact", _ {
		ToggleGuard on{RuntimeFeatureStateToggle::On};
		spec::SongFixture fx = spec::make_synth_song();

		expect(entry::openShellMenu(nullptr, 0, false, false, false, false)).to_be_true();
		actionLogger.deleteAllLogs();
		(void)actionLogger.getNewAction(ActionType::MISC, ActionAddition::NOT_ALLOWED);
		expect(actionLogger.firstAction[BEFORE] != nullptr).to_be_true();
		closeGuest();

		expect(entry::openShellMenu(fx.clip, 0, false, false, false, false, entry::ShellMenuNode::ContextRoot,
		                            entry::NoteMode::NoteRowEditor))
		    .to_be_true();
		expect(actionLogger.firstAction[BEFORE] != nullptr).to_be_true();
		closeGuest();
	});

	it("NoteMode::None on the same shape still wipes it -- the pre-existing behaviour, pinned", _ {
		// The control: identical seed, identical clip, only `noteMode` different. Without it, "the
		// two examples above pass because nothing ever wipes" would also pass.
		ToggleGuard on{RuntimeFeatureStateToggle::On};
		spec::SongFixture fx = spec::make_synth_song();

		expect(entry::openShellMenu(nullptr, 0, false, false, false, false)).to_be_true();
		actionLogger.deleteAllLogs();
		(void)actionLogger.getNewAction(ActionType::MISC, ActionAddition::NOT_ALLOWED);
		expect(actionLogger.firstAction[BEFORE] != nullptr).to_be_true();
		closeGuest();

		expect(entry::openShellMenu(fx.clip, 0, false, false, false, false)).to_be_true();
		expect(actionLogger.firstAction[BEFORE] == nullptr).to_be_true();
		closeGuest();
	});
});
// clang-format on

int note_roots_spec(int argc, char** const argv) {
	return CppSpec::parse(argc, argv)
	               .add_specs(note_roots_table, note_roots_sequence_direction, note_roots_open_shell_menu,
	                          note_roots_undo_log)
	               .exec()
	               .is_success()
	           ? EXIT_SUCCESS
	           : EXIT_FAILURE;
}
