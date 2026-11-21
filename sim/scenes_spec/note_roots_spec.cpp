/// @file
/// @brief `MenuContext::Root::NoteEditor`/`NoteRowEditor`, everything indexed by `Root`
///        that they wire (`MenuScene::roots()`, the two shortcut tables, `greyout_mask()`),
///        the note-row sequence-direction bridge arm, and `openMenu`'s `NoteMode`.
///
/// `MenuScene::grid_pad_claim()` for these two roots is `menu_grid_claim_spec.cpp`'s (extended,
/// not here) -- it already owns every other root's row of that same table.

#include "deluge-project/device_settings.h"
#include "gui/ui/menu_view_of.h"
#include "scenes/menu/bindings.h"
#include "scenes/menu/context.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/menu/nodes/nodes.h"
#include "scenes/menu/nodes/sound_actions.h"
#include "scenes/menu/shortcuts.h"
#include "scenes/registry.h"

#include "gui/l10n/setting_names.h"
#include "gui/ui/ui.h"
#include "scenes/l10n_bridge.h"
#include "util/menu_entry.h"

#include "definitions_cxx.hpp"
#include "deluge-project/sequence_direction.h"
#include "model/action/action_logger.h"
#include "model/clip/instrument_clip.h"
#include "model/edits/setting_edit.h"
#include "model/note/note_row.h"
#include "model/song/song.h"
#include "processing/sound/sound_drum.h"

#include "support/fake_note_bridge.h"
#include "support/song_fixture.h"

#include "unit/ui/support/frames.h"

#include "cppspec.hpp"

#include <cstdint>

#include "support/menu_context.h"
#include "support/spec_main.h"

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

/// @brief `menu_entry_spec.cpp`'s `closeGuest`: put the UI hierarchy back to "nothing open".
void closeGuest() {
	nullifyUIs();
}

} // namespace

// clang-format off
describe note_roots_table("scenes::menu MenuContext::Root::NoteEditor / NoteRowEditor: the tables", $ {
	it("MenuScene::roots() maps both roots to their nodes", _ {
		const std::span<const menu::Node* const> r = menu::MenuScene::roots();
		expect(r[static_cast<size_t>(MenuContext::Root::NoteEditor)]).to_equal(&nodes::note_editor_root);
		expect(r[static_cast<size_t>(MenuContext::Root::NoteRowEditor)]).to_equal(&nodes::note_row_editor_root);
	});

	it("both roots' shortcut layers are empty in both shortcut versions, and light no pad even for "
	   "a node with a real shortcut elsewhere", _ {
		for (deluge::project::ShortcutsVersion version : {deluge::project::ShortcutsVersion::V1, deluge::project::ShortcutsVersion::V3}) {
			for (MenuContext::Root root : {MenuContext::Root::NoteEditor, MenuContext::Root::NoteRowEditor}) {
				expect(all_null(menu::shortcut_layer(root, version, /*second_layer=*/false))).to_be_true();
				expect(all_null(menu::shortcut_layer(root, version, /*second_layer=*/true))).to_be_true();
			}
		}

		// `render_grid` only ever flashes `shortcut_pos(ctx_.root, version, *focused())`. `note_editor_root`
		// itself is in NO root's table, so opening the scene on it could never observe a leak.
		// `nodes::lpf_freq` is a real cell under `Root::Sound` (`shortcuts_spec.cpp` pins it at
		// (8, 7)), so focusing on IT under each note root is what exercises the table lookup.
		spec::SongFixture fx = spec::make_synth_song();
		MenuContext ctx = spec::ctx_of(*fx.song, deluge::gui::menuViewOf(fx.clip), 0, false, false, false, false);
		auto& scene = menu_scene();

		// The first render after `open()` anchors the cycle and is its source step, which flashes no
		// param pad whatever the table says. A second render `kSourceBlinkMs` after that anchor is
		// the param step, where a table hit would flash.
		auto any_pad_flashed = [&](const menu::Node& node, MenuContext::Root root) {
			ctx.root = root;
			scene.open(node, ctx);
			deluge::ui::test::GridRig warmup;
			warmup.begin_tick(0);
			scene.render_grid(warmup.frame); // anchors the cycle at t = 0
			const uint32_t anchor = scene.state().blink_anchor_ms;

			deluge::ui::test::GridRig grid;
			grid.begin_tick(anchor + menu::kSourceBlinkMs);
			scene.render_grid(grid.frame);
			return !warmup.flashes.flashes().empty() || !grid.flashes.flashes().empty();
		};

		// Positive control: the identical node and tick, under the root it REALLY belongs to --
		// proves the harness flashes a pad when the table says to, so the two `false`s below are not
		// vacuous (a broken `GridRig`/timing setup would fail this one too).
		expect(any_pad_flashed(nodes::lpf_freq, MenuContext::Root::Sound)).to_be_true();

		expect(any_pad_flashed(nodes::lpf_freq, MenuContext::Root::NoteEditor)).to_be_false();
		expect(any_pad_flashed(nodes::lpf_freq, MenuContext::Root::NoteRowEditor)).to_be_false();
	});

	it("greyout_mask is {0x03, 0} on the note editor and empty on the note-row editor", _ {
		// `SoundEditor::getGreyoutColsAndRows` (`origin/main:src/deluge/gui/ui/sound_editor.cpp`): `inNoteEditor()`
		// greys the sidebar only (`cols = 0x03`, `rows = 0`); `inNoteRowEditor()` greys nothing (`doGreyout = false`).
		spec::SongFixture fx = spec::make_synth_song();
		MenuContext ctx = spec::ctx_of(*fx.song, deluge::gui::menuViewOf(fx.clip), 0, false, false, false, false);
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
		MenuContext ctx = spec::ctx_of(*fx.song, deluge::gui::menuViewOf(fx.clip), 0, false, false, false, false);
		expect(ctx.is_kit).to_be_false();
		ctx.root = MenuContext::Root::NoteRowEditor;

		spec::FakeNoteBridgeGuard bridge;
		bridge.fake.row_sequence_direction_value = true;
		bridge.fake.row_sequence_direction_out_value = static_cast<int32_t>(SequenceDirection::PINGPONG);

		bool from_note_row = false;
		const int32_t read = menu::bindings::read_sequence_direction(ctx, &from_note_row);
		expect(read).to_equal(static_cast<int32_t>(SequenceDirection::PINGPONG));
		expect(from_note_row).to_be_true();
		expect(bridge.fake.row_sequence_direction_calls).to_be_greater_than(0);

		menu::bindings::write_sequence_direction(ctx, static_cast<int32_t>(SequenceDirection::REVERSE));
		expect(bridge.fake.set_row_sequence_direction_calls).to_equal(1);
		expect(bridge.fake.set_row_sequence_direction_last_value)
		    .to_equal(static_cast<int32_t>(SequenceDirection::REVERSE));
	});

	it("falls back to the clip's own mode when the bridge answers false (no auditioned row)", _ {
		spec::SongFixture fx = spec::make_synth_song();
		MenuContext ctx = spec::ctx_of(*fx.song, deluge::gui::menuViewOf(fx.clip), 0, false, false, false, false);
		ctx.root = MenuContext::Root::NoteRowEditor;
		deluge::edits::clipSettingFor(*fx.clip, "sequenceDirection", deluge::serialization::Value::makeScalar(std::string{deluge::project::to_string(SequenceDirection::REVERSE)}));

		spec::FakeNoteBridgeGuard bridge;
		bridge.fake.row_sequence_direction_value = false;

		bool from_note_row = true; // seeded true so the fallback's `false` is asserted, not assumed
		const int32_t read = menu::bindings::read_sequence_direction(ctx, &from_note_row);
		expect(read).to_equal(static_cast<int32_t>(SequenceDirection::REVERSE));
		expect(from_note_row).to_be_false();
		expect(bridge.fake.row_sequence_direction_calls).to_be_greater_than(0);

		menu::bindings::write_sequence_direction(ctx, static_cast<int32_t>(SequenceDirection::PINGPONG));
		expect(bridge.fake.set_row_sequence_direction_calls).to_equal(0);
		expect(static_cast<int32_t>(fx.clip->sequenceDirectionMode))
		    .to_equal(static_cast<int32_t>(SequenceDirection::PINGPONG));
	});

	it("on a kit clip, never asks the bridge, and keeps the kit-row arm unchanged", _ {
		// `Root::NoteRowEditor` on a kit keeps its kit arms -- `selectedNoteRow` is only ever latched by main's OWN
		// non-kit else-if (`sequence::Direction::checkPermissionToBeginSession` in
		// `origin/main:src/deluge/gui/menu_item/sequence/direction.h`), so a kit clip has no equivalent to model here
		// at all.
		spec::SongFixture fx = spec::make_kit_song();
		SoundDrum* drum = spec::add_drum<SoundDrum>(fx);
		MenuContext ctx = spec::ctx_of(*fx.song, deluge::gui::menuViewOf(fx.clip), 0, false, false, false, /*kit_global_fx=*/false);
		expect(ctx.resolved).to_be_true();
		expect(ctx.is_kit).to_be_true();
		ctx.root = MenuContext::Root::NoteRowEditor;

		NoteRow* row = fx.clip->getNoteRowForDrum(drum);
		expect(row).not_().to_be_null();
		if (row == nullptr) {
			return; // a broken fixture must fail loudly, not skip silently
		}

		spec::FakeNoteBridgeGuard bridge;
		bridge.fake.row_sequence_direction_value = true; // would answer wrongly if ever consulted

		bool from_note_row = false;
		const int32_t read = menu::bindings::read_sequence_direction(ctx, &from_note_row);
		expect(read).to_equal(static_cast<int32_t>(row->sequenceDirectionMode()));
		expect(from_note_row).to_be_true(); // the kit-row arm's own note row, not the bridge's
		expect(bridge.fake.row_sequence_direction_calls).to_equal(0);

		menu::bindings::write_sequence_direction(ctx, static_cast<int32_t>(SequenceDirection::PINGPONG));
		expect(bridge.fake.set_row_sequence_direction_calls).to_equal(0);
		expect(static_cast<int32_t>(row->sequenceDirectionMode()))
		    .to_equal(static_cast<int32_t>(SequenceDirection::PINGPONG));
	});

	it("under any other root, is unchanged: the bridge is never consulted", _ {
		spec::SongFixture fx = spec::make_synth_song();
		MenuContext ctx = spec::ctx_of(*fx.song, deluge::gui::menuViewOf(fx.clip), 0, false, false, false, false);
		expect(ctx.root).not_().to_equal(MenuContext::Root::NoteRowEditor);
		deluge::edits::clipSettingFor(*fx.clip, "sequenceDirection", deluge::serialization::Value::makeScalar(std::string{deluge::project::to_string(SequenceDirection::REVERSE)}));

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

	it("names the play direction's options as the row's own names: three on a clip, NONE fourth on a note row", _ {
		const deluge::l10n::SettingNames* names =
		    deluge::l10n::setting_names(deluge::project::SettingGroupId::NOTE_ROW, "sequenceDirection");
		expect(names != nullptr).to_equal(true);
		if (names == nullptr) {
			return;
		}
		expect(names->choices.size()).to_equal(size_t{4});

		spec::SongFixture fx = spec::make_synth_song();
		MenuContext ctx = spec::ctx_of(*fx.song, deluge::gui::menuViewOf(fx.clip), 0, false, false, false, false);
		std::string_view out[5]{};

		spec::FakeNoteBridgeGuard bridge;
		bridge.fake.row_sequence_direction_value = false;
		expect(nodes::sequence_direction_names(ctx, out)).to_equal(size_t{3});
		for (size_t i = 0; i < 3; ++i) {
			expect(std::string(out[i])).to_equal(std::string(deluge::scenes::text_lookup(static_cast<uint16_t>(names->choices[i]))));
		}

		ctx.root = MenuContext::Root::NoteRowEditor;
		bridge.fake.row_sequence_direction_value = true;
		expect(nodes::sequence_direction_names(ctx, out)).to_equal(size_t{4});
		for (size_t i = 0; i < 4; ++i) {
			expect(std::string(out[i])).to_equal(std::string(deluge::scenes::text_lookup(static_cast<uint16_t>(names->choices[i]))));
		}
	});
});

describe note_roots_open_menu("openMenu's NoteMode", $ {
	it("opens with menu_context().root == NoteEditor", _ {
		spec::SongFixture fx = spec::make_synth_song();

		expect(entry::openMenu(fx.clip, 0, false, false, false, false, entry::MenuNode::ContextRoot,
		                            entry::NoteMode::NoteEditor))
		    .to_be_true();
		expect(deluge::scenes::menu_scene().focused()).to_equal(&nodes::note_editor_root);
		expect(deluge::scenes::menu_scene().menu_context().root).to_equal(MenuContext::Root::NoteEditor);
		closeGuest();
	});

	it("opens with menu_context().root == NoteRowEditor", _ {
		spec::SongFixture fx = spec::make_synth_song();

		expect(entry::openMenu(fx.clip, 0, false, false, false, false, entry::MenuNode::ContextRoot,
		                            entry::NoteMode::NoteRowEditor))
		    .to_be_true();
		expect(deluge::scenes::menu_scene().focused()).to_equal(&nodes::note_row_editor_root);
		expect(deluge::scenes::menu_scene().menu_context().root).to_equal(MenuContext::Root::NoteRowEditor);
		closeGuest();
	});

	it("on a kit with affectEntire true, leaves affectEntire false in both the clip and the "
	   "installed context", _ {
		// `InstrumentClipView::handleNoteEditorEditPadAction` (`instrument_clip_view.cpp`): a note is selected on a kit
		// row (so `context_for` resolves through the drum arm, not the kit-global-fx one -- `kitGlobalFx` here is
		// false, exactly as the kit-row shortcut path's own call is), while the clip's OWN `affectEntire` is
		// independently true. Both facts at once is the case `context.h`'s own note on `affect_entire` names.
		//
		// `ctx.affect_entire` must follow the clear, not merely the clip's
		// own member -- `sequence::Direction::getIndividualNoteRow` reads `clip->affectEntire` LIVE
		// (`origin/main:src/deluge/gui/menu_item/sequence/direction.h`), after main's identical clear, so a stale
		// `true` left in the installed context would make `bindings::direction_target`
		// (`scenes/menu/bindings.cpp`) keep reading/writing the kit's own mode instead of the selected
		// drum's row -- see the dedicated example below.
		spec::SongFixture fx = spec::make_kit_song();
		(void)spec::add_drum<SoundDrum>(fx);
		fx.clip->affectEntire = true;

		expect(entry::openMenu(fx.clip, 0, false, false, false, /*kitGlobalFx=*/false,
		                            entry::MenuNode::ContextRoot, entry::NoteMode::NoteEditor))
		    .to_be_true();
		expect(fx.clip->affectEntire).to_be_false();
		expect(deluge::scenes::menu_scene().menu_context().affect_entire).to_be_false();
		expect(deluge::scenes::menu_scene().menu_context().root).to_equal(MenuContext::Root::NoteEditor);
		closeGuest();
	});

	it("the same kit affectEntire clear applies to NoteRowEditor too", _ {
		// Legacy does the identical clear in `InstrumentClipView::enterNoteRowEditor`, after `soundEditor.setup` and before `openUI`.
		spec::SongFixture fx = spec::make_kit_song();
		(void)spec::add_drum<SoundDrum>(fx);
		fx.clip->affectEntire = true;

		expect(entry::openMenu(fx.clip, 0, false, false, false, /*kitGlobalFx=*/false,
		                            entry::MenuNode::ContextRoot, entry::NoteMode::NoteRowEditor))
		    .to_be_true();
		expect(fx.clip->affectEntire).to_be_false();
		expect(deluge::scenes::menu_scene().menu_context().affect_entire).to_be_false();
		expect(deluge::scenes::menu_scene().menu_context().root).to_equal(MenuContext::Root::NoteRowEditor);
		closeGuest();
	});

	it("a kit note-row editor edits the selected drum's row, not the kit's own mode, once "
	   "affectEntire is cleared", _ {
		// The load-bearing consequence of clearing `affectEntire`, driven through the real binding rather than
		// just read back off `MenuContext`: `bindings::read_sequence_direction`
		// tests `ctx.root == Root::NoteRowEditor && !ctx.is_kit` first (the
		// non-kit bridge arm), then falls through to `direction_target`'s kit-row arm
		// (`!ctx.affect_entire && ctx.is_kit`) -- which only takes the SELECTED
		// DRUM's row when `ctx.affect_entire` is false, exactly as legacy's live
		// `clip->affectEntire` read does after its own clear.
		spec::SongFixture fx = spec::make_kit_song();
		SoundDrum* drum = spec::add_drum<SoundDrum>(fx);
		fx.clip->affectEntire = true;
		NoteRow* row = fx.clip->getNoteRowForDrum(drum);
		expect(row).not_().to_be_null();
		if (row == nullptr) {
			closeGuest();
			return;
		}
		row->setSequenceDirectionModeRaw(SequenceDirection::PINGPONG);
		deluge::edits::clipSettingFor(*fx.clip, "sequenceDirection", deluge::serialization::Value::makeScalar(std::string{deluge::project::to_string(SequenceDirection::REVERSE)})); // must NOT be what gets read

		expect(entry::openMenu(fx.clip, 0, false, false, false, /*kitGlobalFx=*/false,
		                            entry::MenuNode::ContextRoot, entry::NoteMode::NoteRowEditor))
		    .to_be_true();

		bool from_note_row = false;
		const int32_t read =
		    menu::bindings::read_sequence_direction(deluge::scenes::menu_scene().menu_context(), &from_note_row);
		expect(from_note_row).to_be_true();
		expect(read).to_equal(static_cast<int32_t>(SequenceDirection::PINGPONG));
		closeGuest();
	});
});

describe note_roots_undo_log("openMenu's NoteMode must not wipe the undo log", $ {
	// `SoundEditor::setup`'s undo-log wipe (`origin/main:src/deluge/gui/ui/sound_editor.cpp`) is inside its
	// `item == nullptr` arm only. `enterNoteEditor`/`enterNoteRowEditor` never take that arm -- each calls
	// `soundEditor.setup(clip, &noteEditorRootMenu)` / `soundEditor.setup(clip, &noteRowEditorRootMenu)`
	// (`instrument_clip_view.cpp`), an explicit item -- so legacy never wipes on a
	// note-editor/note-row-editor open. `openMenu`'s own wipe (`gui/ui/menu_entry.cpp`)
	// is conditioned on `noteMode == NoteMode::None` as well as `node == MenuNode::ContextRoot
	// && clip != nullptr`: the first two hold for every `NoteMode` call too (neither note root has
	// its own `MenuNode`), so without the third it would wipe where legacy does not.

	it("NoteMode::NoteEditor leaves an existing undo log intact", _ {
		spec::SongFixture fx = spec::make_synth_song();

		// Seed a real logged action while SOME guest is open -- `ActionLogger::getNewAction` refuses
		// unless `getCurrentUI() == getRootUI()` (`model/action/action_logger.cpp`), exactly
		// `menu_entry_spec.cpp`'s own undo-log examples note -- then close it again.
		expect(entry::openMenu(nullptr, 0, false, false, false, false)).to_be_true();
		actionLogger.deleteAllLogs();
		(void)actionLogger.getNewAction(ActionType::MISC, ActionAddition::NOT_ALLOWED);
		expect(actionLogger.firstAction[BEFORE]).not_().to_be_null();
		closeGuest();

		// The note editor, opened on a real clip: the log must survive.
		expect(entry::openMenu(fx.clip, 0, false, false, false, false, entry::MenuNode::ContextRoot,
		                            entry::NoteMode::NoteEditor))
		    .to_be_true();
		expect(actionLogger.firstAction[BEFORE]).not_().to_be_null();
		closeGuest();
	});

	it("NoteMode::NoteRowEditor leaves an existing undo log intact", _ {
		spec::SongFixture fx = spec::make_synth_song();

		expect(entry::openMenu(nullptr, 0, false, false, false, false)).to_be_true();
		actionLogger.deleteAllLogs();
		(void)actionLogger.getNewAction(ActionType::MISC, ActionAddition::NOT_ALLOWED);
		expect(actionLogger.firstAction[BEFORE]).not_().to_be_null();
		closeGuest();

		expect(entry::openMenu(fx.clip, 0, false, false, false, false, entry::MenuNode::ContextRoot,
		                            entry::NoteMode::NoteRowEditor))
		    .to_be_true();
		expect(actionLogger.firstAction[BEFORE]).not_().to_be_null();
		closeGuest();
	});

	it("NoteMode::None on the same shape still wipes it -- the pre-existing behaviour, pinned", _ {
		// The control: identical seed, identical clip, only `noteMode` different. Without it, "the
		// two examples above pass because nothing ever wipes" would also pass.
		spec::SongFixture fx = spec::make_synth_song();

		expect(entry::openMenu(nullptr, 0, false, false, false, false)).to_be_true();
		actionLogger.deleteAllLogs();
		(void)actionLogger.getNewAction(ActionType::MISC, ActionAddition::NOT_ALLOWED);
		expect(actionLogger.firstAction[BEFORE]).not_().to_be_null();
		closeGuest();

		expect(entry::openMenu(fx.clip, 0, false, false, false, false)).to_be_true();
		expect(actionLogger.firstAction[BEFORE]).to_be_null();
		closeGuest();
	});
});
// clang-format on

DELUGE_SPEC(note_roots_spec, note_roots_table, note_roots_sequence_direction, note_roots_open_menu, note_roots_undo_log)
