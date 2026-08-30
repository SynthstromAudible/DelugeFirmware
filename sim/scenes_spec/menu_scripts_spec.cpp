/// @file
/// @brief Task 11: level-2 acceptance scripts, one per menu item kind, driven through the real
///        `Shell` over `uitest::FakeSurfaceSet` (`tests/spec/shell/support/script_runner.h`) against
///        the REAL declarative tables (`src/scenes/menu/nodes/`) -- not a synthetic tree.
///
/// Each script (`tests/ui-scripts/shell/menu_*.txt`) runs the same generic recipe -- a Select-encoder
/// detent, a select-encoder press/release, a GoldUpper detent, a Back press/release, snapshotting
/// after every step -- against a real node `RunOptions::initial_scene` opens directly
/// (`SceneId{FirstApp, node_id(target)}`). What that recipe visibly DOES depends entirely on the
/// target's own `Kind`, which is the point: one interpreter, seven different real screens.
///
/// **Why one node id resolves correctly across every script.** `MenuScene::on_enter` re-targets the
/// path from `SceneId::arg` (`node_by_id`) but never touches `ctx_` (`menu_scene.cpp:272-292`), so
/// each `it` block calls `menu_scene().open(target, ctx)` itself first -- purely to install `ctx_`
/// for its own fixture on the process-wide singleton -- before handing `expect_script_goldens` a
/// `RunOptions::initial_scene` that re-targets the path a second time via the id. `assign_node_ids`
/// is called fresh in every block, over exactly the roots that block's own target is reachable from
/// (ruling: idempotent over one root set, but a later call over a DIFFERENT set is what makes ids
/// valid for that set -- see `node_id.h`), so blocks never depend on execution order.
///
/// **The range-permission script is the one target with no real host.** Every `range_dependent` node
/// in the committed tables (`nodes::kRangeDependentArray`, `nodes/nodes.h`) is a value column of a
/// `Layout::Horizontal` page (`source0`/`source1`), and a horizontal page's `columns` widget claims
/// `entered` only for a `Kind::Submenu` column (`ui/oled/columns.cpp`) -- so `enter_child` (the only
/// caller of `permission_for`) is never reached through the real committed tree today; that is an
/// E2 gap (Custom-column navigation), not something this task can wire around. To still exercise the
/// REAL `source0_transpose` node's permission gate, this file declares one small vertical wrapper
/// submenu naming it as a real child alongside `nodes::lpf_mode` -- the wrapper is synthetic, the
/// leaf it gates is not.

#include "scenes/menu/bindings.h"
#include "scenes/menu/context.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/menu/node.h"
#include "scenes/menu/node_id.h"
#include "scenes/menu/nodes/nodes.h"
#include "scenes/profile.h"
#include "scenes/registry.h"

#include "notify/dirty_set.h"
#include "notify/notice_queue.h"
#include "shell/profiles/deluge.h"
#include "shell/shell.h"

#include "gui/l10n/l10n.h"
#include "gui/l10n/strings.h"
#include "processing/sound/sound_instrument.h"
#include "processing/source.h"

#include "support/song_fixture.h"

#include "spec/shell/support/script_runner.h"

#include "cppspec.hpp"

#include <algorithm>
#include <cstdint>

#include "spec/support/spec_main.h"

using namespace deluge::scenes;
using deluge::l10n::String;
using deluge::scenes::menu::MenuContext;
using deluge::scenes::menu::Node;
namespace bindings = deluge::scenes::menu::bindings;
namespace nodes = deluge::scenes::menu::nodes;

namespace {

/// @brief An `l10n::String` as the plain `uint16_t` a `Node` carries -- the same helper every
///        `nodes/*.cpp` and `menu_scene_spec.cpp` define locally rather than export.
constexpr uint16_t sid(String s) {
	return static_cast<uint16_t>(s);
}

/// @brief Reset the two process-global things a menu touches, and the singleton's own state.
///
/// Mirrors `menu_scene_spec.cpp`'s `select_language`, plus draining `dirty()`: a script run reads
/// `after_render`'s repaint requests through it, and a fact an earlier `it` left behind must not
/// leak into this one's first tick.
void reset_globals() {
	deluge::l10n::chosenLanguage = &deluge::l10n::built_in::english;
	deluge::notify::Notice discard;
	while (deluge::notify::notices().pop(discard)) {}
	deluge::notify::dirty().drain();
}

/// @brief The synthetic vertical wrapper the range-permission script opens: two REAL leaves,
///        `lpf_mode` (an ordinary Enum, so `Select +1` off the default cursor lands on row 1) and
///        `source0_transpose` (the real range-dependent Decimal `permission_for` gates).
constexpr const Node* kRangePermChildren[] = {&nodes::lpf_mode, &nodes::source0_transpose};
constexpr Node range_perm_root = deluge::scenes::menu::submenu(sid(String::STRING_FOR_TRANSPOSE), kRangePermChildren);
constexpr const Node* kRangePermRoots[] = {&range_perm_root};

/// @brief `menu_scene().state().list[0].cursor` -- the same accessor `menu_scene_spec.cpp` uses.
int cursor0() {
	return menu_scene().state().list[0].cursor;
}

} // namespace

// clang-format off
describe menu_scripts("scenes::menu per-kind level-2 acceptance scripts", $ {
	it("Int: nodes::saturation renders and edits through the real Shell", _ {
		reset_globals();
		spec::SongFixture song = spec::make_synth_song();
		const MenuContext ctx = menu::context_for(*song.song, song.clip, 0, false, false, false);
		(void)menu::assign_node_ids(nodes::kAllRoots);
		menu_scene().open(nodes::saturation, ctx);

		const int32_t before = bindings::read(nodes::saturation, ctx);

		uitest::RunOptions opts{
		    .tick_ms = 10,
		    .board = &deluge::ui::shell::kDelugeBoard,
		    .profile = &deluge::scenes::deluge_profile(),
		    .initial_scene = deluge::ui::shell::SceneId{deluge::ui::shell::SceneKind::FirstApp,
		                                               menu::node_id(nodes::saturation)},
		};
		const uitest::RunResult result =
		    uitest::expect_script_goldens(self, "menu_int", scenes::scene_registry(), deluge::ui::shell::Services{}, opts);
		expect(result.snapshots).to_equal(uint32_t{5});

		// Q7: the `.state` pins WHICH node is open; it says nothing about its label or value, so
		// both are asserted here directly. The name check is against the EXPECTED `l10n::String` id,
		// not `nodes::saturation.name` -- comparing a node's own field against itself cannot fail
		// once the pointer-identity check just above already holds (review round 1, I2); this is the
		// independent value Q7 actually wants pinned.
		expect(menu_scene().focused() == &nodes::saturation).to_be_true();
		expect(static_cast<uint32_t>(menu_scene().focused()->name)).to_equal(static_cast<uint32_t>(sid(String::STRING_FOR_SATURATION)));
		// GoldUpper +2 detents -> `edges_per_detent`(2) * 2 = 4 raw edges (an Accelerating knob's
		// magnitude IS the raw edge count -- `script_runner.h`'s uniform detent->edge conversion,
		// `menu_scene_spec.cpp`'s `Rig::gold` note); `int_value`'s coarse step is `range.step` (1),
		// so the value should have moved by exactly 4, clamped to the node's own `IntRange{0,15,1}`.
		expect(bindings::read(nodes::saturation, ctx)).to_equal(std::min(before + 4, 15));
	});

	it("Decimal: nodes::source0_transpose renders and edits through the real Shell", _ {
		reset_globals();
		spec::SongFixture song = spec::make_synth_song();
		const MenuContext ctx = menu::context_for(*song.song, song.clip, 0, false, false, false);
		(void)menu::assign_node_ids(nodes::kAllRoots);
		menu_scene().open(nodes::source0_transpose, ctx);

		const int32_t before = bindings::read(nodes::source0_transpose, ctx);

		uitest::RunOptions opts{
		    .tick_ms = 10,
		    .board = &deluge::ui::shell::kDelugeBoard,
		    .profile = &deluge::scenes::deluge_profile(),
		    .initial_scene = deluge::ui::shell::SceneId{deluge::ui::shell::SceneKind::FirstApp,
		                                               menu::node_id(nodes::source0_transpose)},
		};
		const uitest::RunResult result = uitest::expect_script_goldens(
		    self, "menu_decimal", scenes::scene_registry(), deluge::ui::shell::Services{}, opts);
		expect(result.snapshots).to_equal(uint32_t{5});

		expect(menu_scene().focused() == &nodes::source0_transpose).to_be_true();
		expect(static_cast<uint32_t>(menu_scene().focused()->name)).to_equal(static_cast<uint32_t>(sid(String::STRING_FOR_TRANSPOSE)));
		// `decimal_value`'s coarse amount is `10^edit_pos`, and `edit_pos` starts at 0, so the same
		// 4-raw-edge GoldUpper turn moves the value by 4 * 1, clamped to `kTransposeRange`.
		expect(bindings::read(nodes::source0_transpose, ctx)).to_equal(std::min(before + 4, 9600));
		// Review round 1, Minor 6: `after_select_turn`'s pixel diff against `idle` is the cursor's
		// own time-driven blink (`decimal_value`'s `Blink`), not an effect of the Select turn -- the
		// script's shared "Select +1" step is claimed-but-unused here (`render_leaf_decimal`'s
		// `f.claim_turn()`), so that snapshot alone verifies nothing about input. What DOES verify
		// something: `edit_pos` moves only on the "Horizontal" encoder, which this script never
		// touches, so it must still be 0 after Select turns, a select press/release and a GoldUpper
		// turn -- proving Select really is inert for a Decimal screen's cursor, not merely that its
		// pixels happen to look inert at the two moments a screenshot was taken.
		expect(static_cast<int>(menu_scene().state().decimal_pos)).to_equal(0);
	});

	it("Enum: nodes::lpf_mode renders and edits through the real Shell", _ {
		reset_globals();
		spec::SongFixture song = spec::make_synth_song();
		const MenuContext ctx = menu::context_for(*song.song, song.clip, 0, false, false, false);
		(void)menu::assign_node_ids(nodes::kAllRoots);
		menu_scene().open(nodes::lpf_mode, ctx);

		const int32_t before = bindings::read(nodes::lpf_mode, ctx); // TRANSISTOR_24DB == 1

		uitest::RunOptions opts{
		    .tick_ms = 10,
		    .board = &deluge::ui::shell::kDelugeBoard,
		    .profile = &deluge::scenes::deluge_profile(),
		    .initial_scene = deluge::ui::shell::SceneId{deluge::ui::shell::SceneKind::FirstApp,
		                                               menu::node_id(nodes::lpf_mode)},
		};
		const uitest::RunResult result =
		    uitest::expect_script_goldens(self, "menu_enum", scenes::scene_registry(), deluge::ui::shell::Services{}, opts);
		expect(result.snapshots).to_equal(uint32_t{5});

		expect(menu_scene().focused() == &nodes::lpf_mode).to_be_true();
		expect(static_cast<uint32_t>(menu_scene().focused()->name)).to_equal(static_cast<uint32_t>(sid(String::STRING_FOR_MODE)));
		// `enum_value` owns the Select turn itself: one detent moves the option index by one, and it
		// commits on the same tick (`Response::changed`). The later SelectEnc press/release and
		// GoldUpper turn (unclaimed by this widget) do not move it again.
		expect(bindings::read(nodes::lpf_mode, ctx)).to_equal(before + 1);
	});

	it("Toggle: nodes::osc_sync renders and edits through the real Shell", _ {
		reset_globals();
		spec::SongFixture song = spec::make_synth_song();
		const MenuContext ctx = menu::context_for(*song.song, song.clip, 0, false, false, false);
		(void)menu::assign_node_ids(nodes::kAllRoots);
		menu_scene().open(nodes::osc_sync, ctx);

		expect(bindings::read(nodes::osc_sync, ctx)).to_equal(int32_t{0}); // off by default

		uitest::RunOptions opts{
		    .tick_ms = 10,
		    .board = &deluge::ui::shell::kDelugeBoard,
		    .profile = &deluge::scenes::deluge_profile(),
		    .initial_scene = deluge::ui::shell::SceneId{deluge::ui::shell::SceneKind::FirstApp,
		                                               menu::node_id(nodes::osc_sync)},
		};
		const uitest::RunResult result = uitest::expect_script_goldens(
		    self, "menu_toggle", scenes::scene_registry(), deluge::ui::shell::Services{}, opts);
		expect(result.snapshots).to_equal(uint32_t{5});

		expect(menu_scene().focused() == &nodes::osc_sync).to_be_true();
		expect(static_cast<uint32_t>(menu_scene().focused()->name)).to_equal(static_cast<uint32_t>(sid(String::STRING_FOR_OSCILLATOR_SYNC)));
		// `toggle`'s turn selects the row in the turn's direction: one detent toward ON flips it.
		// The later SelectEnc press reports `entered` and leaves `changed` false (`ui/oled/values.h`),
		// and GoldUpper is unclaimed by this widget, so the value stays flipped, not flipped twice.
		expect(bindings::read(nodes::osc_sync, ctx)).to_equal(int32_t{1});
	});

	it("Submenu (horizontal): nodes::lpf renders and edits its focused column through the real Shell", _ {
		reset_globals();
		spec::SongFixture song = spec::make_synth_song();
		const MenuContext ctx = menu::context_for(*song.song, song.clip, 0, false, false, false);
		(void)menu::assign_node_ids(nodes::kAllRoots);
		menu_scene().open(nodes::lpf, ctx);

		const int32_t before = bindings::read(nodes::lpf_freq, ctx);

		uitest::RunOptions opts{
		    .tick_ms = 10,
		    .board = &deluge::ui::shell::kDelugeBoard,
		    .profile = &deluge::scenes::deluge_profile(),
		    .initial_scene = deluge::ui::shell::SceneId{deluge::ui::shell::SceneKind::FirstApp, menu::node_id(nodes::lpf)},
		};
		const uitest::RunResult result = uitest::expect_script_goldens(
		    self, "menu_submenu_horizontal", scenes::scene_registry(), deluge::ui::shell::Services{}, opts);
		expect(result.snapshots).to_equal(uint32_t{5});

		expect(menu_scene().focused() == &nodes::lpf).to_be_true();
		expect(static_cast<uint32_t>(menu_scene().focused()->name)).to_equal(static_cast<uint32_t>(sid(String::STRING_FOR_LPF)));
		// The Select detent moves the column focus from MODE (0) to FREQ (1); a select press on a
		// value column raises the notice popup rather than navigating (`horizontal_menu.cpp:657-661`
		// port), so GoldUpper's turn still lands on FREQ -- `render_horizontal`'s own coarse step is
		// `range.step` (1) too, so the same 4-raw-edge turn moves it by 4.
		expect(static_cast<int>(menu_scene().state().columns[0].focused)).to_equal(1);
		expect(bindings::read(nodes::lpf_freq, ctx)).to_equal(std::min(before + 4, 50));
	});

	it("Shortcut: a real shift+pad jump lands on nodes::lpf_freq, then the real Shell edits it", _ {
		reset_globals();
		spec::SongFixture song = spec::make_synth_song();
		const MenuContext ctx = menu::context_for(*song.song, song.clip, 0, false, false, false);
		(void)menu::assign_node_ids(nodes::kAllRoots);
		menu_scene().open(nodes::sound_root, ctx);

		const int32_t before = bindings::read(nodes::lpf_freq, ctx);

		uitest::RunOptions opts{
		    .tick_ms = 10,
		    .board = &deluge::ui::shell::kDelugeBoard,
		    .profile = &deluge::scenes::deluge_profile(),
		    .initial_scene = deluge::ui::shell::SceneId{deluge::ui::shell::SceneKind::FirstApp,
		                                               menu::node_id(nodes::sound_root)},
		};
		const uitest::RunResult result = uitest::expect_script_goldens(
		    self, "menu_shortcut", scenes::scene_registry(), deluge::ui::shell::Services{}, opts);
		expect(result.snapshots).to_equal(uint32_t{6});

		// The real pad (8, 7) of `paramShortcutsForSounds` -- `&lpfFreqMenu` -- proven in
		// `menu_scene_spec.cpp`'s own shortcut tests.
		expect(menu_scene().focused() == &nodes::lpf_freq).to_be_true();
		expect(static_cast<uint32_t>(menu_scene().focused()->name)).to_equal(static_cast<uint32_t>(sid(String::STRING_FOR_FREQUENCY)));
		expect(bindings::read(nodes::lpf_freq, ctx)).to_equal(std::min(before + 4, 50));
	});

	it("Range permission: entering the real source0_transpose is refused until a range is chosen", _ {
		reset_globals();
		spec::SongFixture song = spec::make_synth_song();
		auto* sound = static_cast<SoundInstrument*>(song.instrument);
		// `permission_for`'s `MustSelectRange` arm (`context.cpp`): more than one `MultiRange` on the
		// source and none selected yet -- the exact setup `menu_scene_spec.cpp`'s own range-permission
		// test uses, here against the REAL `nodes::source0_transpose` instead of a synthetic param.
		sound->sources[0].ranges.insertMultiRange(0);
		sound->sources[0].ranges.insertMultiRange(1);
		const MenuContext ctx = menu::context_for(*song.song, song.clip, 0, false, false, false);
		(void)menu::assign_node_ids(kRangePermRoots);
		menu_scene().open(range_perm_root, ctx);

		const int32_t before = bindings::read(nodes::source0_transpose, ctx);

		uitest::RunOptions opts{
		    .tick_ms = 10,
		    .board = &deluge::ui::shell::kDelugeBoard,
		    .profile = &deluge::scenes::deluge_profile(),
		    .initial_scene = deluge::ui::shell::SceneId{deluge::ui::shell::SceneKind::FirstApp,
		                                               menu::node_id(range_perm_root)},
		};
		const uitest::RunResult result = uitest::expect_script_goldens(
		    self, "menu_range_permission", scenes::scene_registry(), deluge::ui::shell::Services{}, opts);
		expect(result.snapshots).to_equal(uint32_t{5});

		// The wrapper itself is still on screen -- `source0_transpose` was never entered.
		expect(menu_scene().focused() == &range_perm_root).to_be_true();
		expect(static_cast<uint32_t>(menu_scene().focused()->name)).to_equal(static_cast<uint32_t>(sid(String::STRING_FOR_TRANSPOSE)));
		expect(cursor0()).to_equal(1); // the Select detent landed the cursor on row 1
		expect(menu_scene().state().range_picker_pending).to_be_true();
		expect(menu_scene().state().pending_after_range == &nodes::source0_transpose).to_be_true();
		// And the model value: refused entry means no edit ever reached it.
		expect(bindings::read(nodes::source0_transpose, ctx)).to_equal(before);
	});
});
// clang-format on

int menu_scripts_spec(int argc, char** const argv) {
	return CppSpec::parse(argc, argv).add_specs(menu_scripts).exec().is_success() ? EXIT_SUCCESS : EXIT_FAILURE;
}
