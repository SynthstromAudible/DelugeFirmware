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

	it("both roots' shortcut layers are empty in both shortcut versions, and lights no pad", _ {
		const menu::ShortcutsVersion saved = menu::shortcuts_version();
		for (menu::ShortcutsVersion version : {menu::ShortcutsVersion::V1, menu::ShortcutsVersion::V3}) {
			menu::set_shortcuts_version(version);
			for (MenuContext::Root root : {MenuContext::Root::NoteEditor, MenuContext::Root::NoteRowEditor}) {
				expect(all_null(menu::shortcut_layer(root, /*second_layer=*/false))).to_be_true();
				expect(all_null(menu::shortcut_layer(root, /*second_layer=*/true))).to_be_true();
			}
		}
		menu::set_shortcuts_version(saved);

		// `sound_editor.cpp:1467`'s three predicates skip the shortcut arm on either root anyway
		// (`menu_grid_claim_spec.cpp` pins `grid_pad_claim()`), but the table itself is empty too:
		// opening the scene on the note-editor root and rendering the grid lights nothing.
		spec::SongFixture fx = spec::make_synth_song();
		MenuContext ctx = menu::context_for(*fx.song, fx.clip, 0, false, false, false, false);
		ctx.root = MenuContext::Root::NoteEditor;
		auto& scene = menu_scene();
		scene.open(nodes::note_editor_root, ctx);

		deluge::ui::test::GridRig grid;
		grid.begin_tick(0);
		scene.render_grid(grid.frame);
		for (int32_t y = 0; y < grid.grid.dims().rows; y++) {
			for (int32_t x = 0; x < grid.grid.dims().cols(); x++) {
				const RGB& c = grid.grid.at(static_cast<uint8_t>(x), static_cast<uint8_t>(y));
				expect(c.r == 0 && c.g == 0 && c.b == 0).to_be_true();
			}
		}
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

		spec::FakeNoteBridgeGuard bridge;
		bridge.fake.row_sequence_direction_value = true; // would answer wrongly if ever consulted

		bool from_note_row = false;
		const int32_t read = menu::bindings::read_sequence_direction(ctx, &from_note_row);
		if (row != nullptr) {
			expect(read).to_equal(static_cast<int32_t>(row->sequenceDirectionMode));
		}
		expect(from_note_row).to_be_true(); // the kit-row arm's own note row, not the bridge's
		expect(bridge.fake.row_sequence_direction_calls).to_equal(0);

		menu::bindings::write_sequence_direction(ctx, static_cast<int32_t>(SequenceDirection::PINGPONG));
		expect(bridge.fake.set_row_sequence_direction_calls).to_equal(0);
		if (row != nullptr) {
			expect(static_cast<int32_t>(row->sequenceDirectionMode))
			    .to_equal(static_cast<int32_t>(SequenceDirection::PINGPONG));
		}
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

	it("on a kit with affectEntire true, leaves affectEntire false and resolved the context before "
	   "the toggle", _ {
		// `instrument_clip_view.cpp:3590-3598`: a note is selected on a kit row (so `context_for`
		// resolves through the drum arm, not the kit-global-fx one -- `kitGlobalFx` here is false,
		// exactly as the kit-row shortcut path's own call is), while the clip's OWN `affectEntire`
		// is independently true. Both facts at once is the case `context.h`'s own note on
		// `affect_entire` names.
		ToggleGuard on{RuntimeFeatureStateToggle::On};
		spec::SongFixture fx = spec::make_kit_song();
		(void)spec::add_drum<SoundDrum>(fx);
		fx.clip->affectEntire = true;

		expect(entry::openShellMenu(fx.clip, 0, false, false, false, /*kitGlobalFx=*/false,
		                            entry::ShellMenuNode::ContextRoot, entry::NoteMode::NoteEditor))
		    .to_be_true();
		// The toggle happened...
		expect(fx.clip->affectEntire).to_be_false();
		// ...but the context installed still reports what it resolved BEFORE that toggle ran: the
		// clip's own `affectEntire`, read once by `context_for` ahead of the clear below it.
		expect(deluge::scenes::menu_scene().menu_context().affect_entire).to_be_true();
		expect(deluge::scenes::menu_scene().menu_context().root == MenuContext::Root::NoteEditor).to_be_true();
		closeGuest();
	});

	it("the same kit affectEntire clear applies to NoteRowEditor too", _ {
		// Resolution 1: legacy does the identical clear in `enterNoteRowEditor`
		// (`instrument_clip_view.cpp:3760-3765`), after `soundEditor.setup` and before `openUI`.
		ToggleGuard on{RuntimeFeatureStateToggle::On};
		spec::SongFixture fx = spec::make_kit_song();
		(void)spec::add_drum<SoundDrum>(fx);
		fx.clip->affectEntire = true;

		expect(entry::openShellMenu(fx.clip, 0, false, false, false, /*kitGlobalFx=*/false,
		                            entry::ShellMenuNode::ContextRoot, entry::NoteMode::NoteRowEditor))
		    .to_be_true();
		expect(fx.clip->affectEntire).to_be_false();
		expect(deluge::scenes::menu_scene().menu_context().affect_entire).to_be_true();
		expect(deluge::scenes::menu_scene().menu_context().root == MenuContext::Root::NoteRowEditor).to_be_true();
		closeGuest();
	});
});
// clang-format on

int note_roots_spec(int argc, char** const argv) {
	return CppSpec::parse(argc, argv)
	               .add_specs(note_roots_table, note_roots_sequence_direction, note_roots_open_shell_menu)
	               .exec()
	               .is_success()
	           ? EXIT_SUCCESS
	           : EXIT_FAILURE;
}
