/// @file
/// @brief `deluge::ui::shell::Scene::grid_pad_claim` and `MenuScene`'s override of it -- the
///        declaration this task adds, and nothing that reads it yet.
///
/// What this pins: that `MenuScene::grid_pad_claim()` answers `ShortcutsOnly` on a sound root and
/// `None` on a settings root -- the port of `SoundEditor::padAction`'s gate at
/// `gui/ui/sound_editor.cpp:1467` -- and that a scene which never overrides the virtual (the
/// browser, stood in for every scene this task does not touch) answers the safe default, `All`.
///
/// What it deliberately cannot see: nothing reads `grid_pad_claim()` yet. There is no forwarding
/// path, no `ShellHostUI::claimsGridPad`, and no grid-pad test through a `Shell` -- those are later
/// tasks. This file only opens a real `MenuScene` on a real root (`MenuScene::open` writes `ctx_`
/// and resets `st_` without needing a `Shell` at all, `scenes/menu/menu_scene.cpp:331-335`) and
/// reads the new virtual straight off it.

#include "scenes/menu/context.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/menu/nodes/nodes.h"
#include "scenes/registry.h"
#include "shell/scene.h"

#include "scenes/browser/browser_scene.h"

#include "support/song_fixture.h"

#include "cppspec.hpp"

#include "spec/support/spec_main.h"

using namespace deluge::scenes;
using deluge::scenes::menu::MenuContext;
using deluge::ui::shell::GridPadClaim;
namespace nodes = deluge::scenes::menu::nodes;

namespace {

/// @brief The context `SoundEditor::setup` resolves for a synth instrument clip: no kit, no
///        performance view, no session/arranger root -- `context_for`'s branch that lands on
///        `MenuContext::Root::Sound` (`scenes/menu/context.cpp:357`).
MenuContext sound_context(spec::SongFixture& fx) {
	return menu::context_for(*fx.song, fx.clip, 0, false, false, false, /*kit_global_fx=*/false);
}

} // namespace

// clang-format off
describe menu_grid_claim("MenuScene::grid_pad_claim -- SoundEditor::padAction's gate at "
                         "`gui/ui/sound_editor.cpp:1467`, declared", $ {
	it("answers ShortcutsOnly on a sound root", _ {
		// `:1467`'s three predicates are all false on a sound root, so `:1468`'s
		// `potentialShortcutPadAction` runs and claims -- the shortcut-only half of the port.
		spec::SongFixture fx = spec::make_synth_song();
		auto& scene = menu_scene();
		MenuContext ctx = sound_context(fx);
		scene.open(nodes::sound_root, ctx);
		expect(scene.grid_pad_claim()).to_equal(GridPadClaim::ShortcutsOnly);
	});

	it("answers None on a settings root", _ {
		// `SoundEditor::inSettingsMenu()` (`:1912-1914`) is one of `:1467`'s three predicates, so
		// legacy skips the shortcut arm entirely here and every pad falls through.
		spec::SongFixture fx = spec::make_synth_song();
		auto& scene = menu_scene();
		MenuContext ctx = sound_context(fx);
		ctx.root = MenuContext::Root::Settings;
		scene.open(nodes::root_settings, ctx);
		expect(scene.grid_pad_claim()).to_equal(GridPadClaim::None);
	});

	it("answers None on the note editor and the note-row editor roots", _ {
		// Task 6: `:1467`'s other two predicates, `!inNoteEditor()` and `!inNoteRowEditor()`, are the
		// other half of the same gate `Root::Settings` above pins -- both roots skip the shortcut
		// arm exactly as the settings root does.
		spec::SongFixture fx = spec::make_synth_song();
		auto& scene = menu_scene();
		MenuContext ctx = sound_context(fx);

		ctx.root = MenuContext::Root::NoteEditor;
		scene.open(nodes::note_editor_root, ctx);
		expect(scene.grid_pad_claim()).to_equal(GridPadClaim::None);

		ctx.root = MenuContext::Root::NoteRowEditor;
		scene.open(nodes::note_row_editor_root, ctx);
		expect(scene.grid_pad_claim()).to_equal(GridPadClaim::None);
	});

	it("answers All for a scene that does not override it", _ {
		// The default is the safe direction: a scene that forgets to answer forwards nothing,
		// rather than forwarding something it wanted.
		expect(deluge::scenes::browser::browser_scene().grid_pad_claim()).to_equal(GridPadClaim::All);
	});
});
// clang-format on

DELUGE_SPEC(menu_grid_claim_spec, menu_grid_claim)
