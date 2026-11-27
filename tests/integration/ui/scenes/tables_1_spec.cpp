/// @file
/// @brief The first menu tables (`tables_1.tsv`), checked against the reference rows of upstream `main`'s menu
///        (`tests/golden/ui/menu_oracle/tables_1.tsv`).
///
/// The six subtrees are `constexpr Node` literals
/// (`src/ui/scenes/menu/nodes/*.cpp`). The rows hold what main's items answer on a synth clip, so every claim in that
/// transcription is checked against the original rather than against a second hand-written list:
///
///   * the children of each of the seventeen roots are the same items, in the same order, with the same names and
///     titles;
///   * each node's `ParamRef` index and kind, AFFECT ENTIRE use, edit marking, slot count, range dependence and learn
///     target are main's;
///   * `bindings::read(node, ctx)` equals what main's item reads on the same fixture sound;
///   * each `Kind::Enum`'s names are main's option strings;
///   * each node's `relevant` predicate agrees with main's `isRelevant()`;
///   * the note-range picker labels, edits, inserts, deletes and scrolls its ranges as main's `MultiRange` does;
///
/// plus `validate_tree` over the seventeen roots, and one `MenuScene` render of the LPF page.
///
/// @note The answers are rows of a reference table, not live calls: the spec includes no main-menu header, and the
///       last group asks that it asked every row (`OracleTable::unasked`).

#include "gui/ui/menu_view_of.h"
#include "support/fake_ports.h"
#include "ui/scenes/menu/bindings.h"
#include "ui/scenes/menu/context.h"
#include "ui/scenes/menu/menu_scene.h"
#include "ui/scenes/menu/node.h"
#include "ui/scenes/menu/node_id.h"
#include "ui/scenes/menu/nodes/nodes.h"
#include "ui/scenes/menu/relevance.h"
#include "ui/scenes/menu/shortcuts.h"
#include "ui/scenes/menu/validate.h"
#include "ui/scenes/profile.h"
#include "ui/scenes/registry.h"
#include "ui/scenes/services/view_state.h"
#include "unit/ui/widgets/support/frames.h"

#include "ui/shell/profiles/deluge.h"
#include "ui/shell/shell.h"

#include "gui/l10n/l10n.h"
#include "gui/l10n/strings.h"
#include "hid/display/display.h"
#include "storage/flash_storage.h"
#include "ui/gfx/oled/glyphs.h"

#include "model/clip/instrument_clip.h"
#include "model/song/song.h"
#include "processing/sound/sound_instrument.h"
#include "processing/sound/source_resources.h"

#include "support/known_differences.h"
#include "support/menu_engine.h"
#include "support/oracle_table.h"
#include "support/oracle_tree.h"
#include "support/song_fixture.h"
#include "support/tables_1_fixtures.h"

#include "support/mocks/fake_surfaces.h"
#include "support/printers/controller_mapping_printer.h"
#include "unit/ui/gfx/support/golden.h"

#include "cppspec.hpp"
#include "support/matchers/address_of.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "support/menu_context.h"
#include "support/spec_main.h"

using namespace deluge::ui::scenes;
using deluge::l10n::String;
using deluge::ui::scenes::menu::MenuContext;
using deluge::ui::scenes::menu::Node;

namespace t1 = deluge::ui::scenes::spec::tables1;

namespace {

using deluge::test::matchers::address_of;

namespace nodes = deluge::ui::scenes::menu::nodes;
namespace bindings = deluge::ui::scenes::menu::bindings;

using deluge::synth::SourceRange;
using t1::Fixture;
using t1::fixture;
using t1::RangeFixture;
using t1::shown;
using t1::text_of;

spec::OracleTable& table() {
	return spec::OracleTable::named("tables_1");
}

/// @brief The rows' text for a list of values, `|`-separated, so a mismatch prints the whole list.
std::string joined(const std::vector<std::string>& values) {
	std::string out;
	for (const std::string& v : values) {
		out += v + "|";
	}
	return out;
}

/// @brief The values of the row (@p path, @p column), joined; `<no row>` when there is none.
std::string frozen(const std::string& path, const std::string& column) {
	const std::vector<std::string>* values = table().find(path, column);
	return values == nullptr ? std::string{"<no row>"} : joined(*values);
}

/// @brief The top notes of @p ranges.
std::vector<int32_t> tops_of(const std::vector<SourceRange>& ranges) {
	std::vector<int32_t> tops;
	for (const SourceRange& r : ranges) {
		tops.push_back(r.top_note);
	}
	return tops;
}

/// @brief The ranges as the picker rows record them.
std::string spans(const std::vector<SourceRange>& ranges) {
	return joined(t1::spans_of(tops_of(ranges)));
}

/// @brief One picker row's label, as the port builds it.
std::string label_of(const MenuContext& ctx, int32_t index) {
	char buf[32];
	deluge::ui::shell::TextScratch scratch;
	return std::string{nodes::range_row_label(ctx, index, buf, sizeof(buf), scratch)};
}

/// @brief A `Shell` over `FakeSurfaceSet` with the application's scene registry, for the golden.
struct Rig {
	const deluge::ui::shell::BoardProfile& profile = deluge_profile();
	deluge::ui::shell::SceneRegistry reg = scene_registry();
	uitest::FakeSurfaceSet fake{deluge::ui::shell::kDelugeBoard, deluge_profile()};
	deluge::ui::shell::Shell shell{fake, profile, reg, deluge::ui::shell::Services{}, deluge::ui::shell::ShellConfig{}};

	Rig(const Rig&) = delete;
	Rig& operator=(const Rig&) = delete;
	Rig() = default;

	/// @brief Tear the stack down, so every scene's `on_exit` always runs.
	///
	/// `MenuScene` is a process singleton (`ui/shell/scene.h`'s `SceneState` cache): an example that
	/// left it -- or anything pushed above it -- on the stack would make the NEXT example's
	/// `on_enter` see stale state instead of a fresh one, the cross-contamination
	/// `support/recorder_scene_fixtures.h`'s `~Rig` was written to stop (a leaked recorder there makes the next
	/// `on_enter` refuse outright and fails every later example). Popping
	/// unconditionally means every example starts from the same place whatever the one before it did.
	~Rig() {
		while (shell.stack().depth() > 0) {
			shell.stack().pop(deluge::ui::shell::SceneKind::Null);
		}
		menu_scene().reset_process_state(); // each example starts with the rows a boot has
	}

	void open(const Node& root, const MenuContext& ctx) {
		(void)menu::assign_node_ids(nodes::kBatch1Roots);
		menu_scene().open(root, ctx);
		shell.init(deluge::ui::shell::SceneId{deluge::ui::shell::SceneKind::FirstApp, menu::node_id(root)});
	}
};

} // namespace

// clang-format off
describe tables_1_shape("scenes::menu sound-subtree tables: shape", $ {
	it("declares seventeen roots, one per legacy top-level menu", _ {
		expect(nodes::kBatch1Roots.size()).to_equal(size_t(17));
	});

	it("lists, under every root, the children main's menu lists, and nothing it does not", _ {
		(void)fixture();
		size_t submenus = 0;
		for (const spec::TreeEntry& e : t1::entries()) {
			if (!t1::has_dynamic_name(*e.node)) {
				expect(e.path + (table().find(e.path, "name") != nullptr ? " has a name row" : " has none"))
				    .to_equal(e.path + " has a name row");
			}
			if (e.node->kind == menu::Kind::Submenu) {
				expect(e.path + " kids " + std::to_string(e.node->children.size()))
				    .to_equal(e.path + " kids " + table().value(e.path, "kids"));
				submenus++;
			}
		}
		expect(submenus).to_be_greater_than(size_t(15));
	});

	it("gives every node main's own name", _ {
		const Fixture& f = fixture();
		for (const spec::TreeEntry& e : t1::entries()) {
			if (t1::has_dynamic_name(*e.node)) {
				continue; // see has_dynamic_name's own note
			}
			expect(e.path + " " + shown(e.node->name, *e.node, f.ctx))
			    .to_equal(e.path + " " + table().value(e.path, "name"));
		}
	});

	it("gives every node main's own title", _ {
		const Fixture& f = fixture();
		size_t compared = 0;
		for (const spec::TreeEntry& e : t1::entries()) {
			if (t1::has_dynamic_name(*e.node)) {
				continue; // the same classes build their *title* from the name, at render time
			}
			const std::string title = shown(e.node->title, *e.node, f.ctx);
			expect(e.path + " " + title).to_equal(e.path + " " + table().value(e.path, "title"));
			compared++;
		}
		expect(compared).to_be_greater_than(size_t(80));
	});

	it("binds MIDI/CC learn to exactly the param main would have learned", _ {
		const Fixture& f = fixture();
		size_t learnable = 0;
		for (const spec::TreeEntry& e : t1::entries()) {
			const std::vector<std::string>* want = table().find(e.path, "learn");
			if (want == nullptr) {
				// A leaf with a `ParamRef` is one main's item learns.
				if (e.node->kind != menu::Kind::Submenu && e.node->kind != menu::Kind::Custom
				    && e.node->bind.param.valid()) {
					expect(e.path + " has none").to_equal(e.path + " has a learn row");
				}
				continue;
			}
			const std::optional<deluge::project::OwnerParamTarget> got = menu::bindings::learn_target(*e.node, f.ctx);
			const std::string at = e.path + " " + text_of(e.node->name) + " learns ";
			expect(at + (got ? std::to_string(got->param.id.value) : std::string{"none"}))
			    .to_equal(at + (want->empty() ? std::string{"<no value>"} : want->front()));
			if (got) {
				expect(got->owner).to_equal(menu::bindings::controls_owner(f.ctx));
			}
			learnable++;
		}
		// The four transposes plus every `ParamRef` leaf; a drop to 0 would make the loop vacuous.
		expect(learnable).to_be_greater_than(size_t(40));
	});

	it("gives every node main's own ParamRef, or none where main has none", _ {
		(void)fixture();
		size_t compared = 0;
		for (const spec::TreeEntry& e : t1::entries()) {
			// A submenu reports the kind of its first param child, not one of its own; a leaf with a hook or a
			// non-param target owns its value, and its learn-only `ParamRef` is the learn rows' to check.
			if (!spec::param_comparable(*e.node)) {
				expect(table().find(e.path, "param_index")).to_be_null();
				expect(table().find(e.path, "param_kind")).to_be_null();
				continue;
			}
			const std::optional<int32_t> index = spec::int_of(table().find(e.path, "param_index"));
			const std::string at = e.path + " " + text_of(e.node->name);
			expect(at + " index " + std::to_string(spec::runtime_of(e.node->bind.param).index))
			    .to_equal(at + " index " + (index ? std::to_string(*index) : std::string{"<no row>"}));
			const std::optional<int32_t> kind = spec::int_of(table().find(e.path, "param_kind"));
			expect(at + (kind ? " has a kind" : " has none")).to_equal(at + " has a kind");
			if (!kind || *kind == static_cast<int32_t>(deluge::modulation::params::Kind::NONE)) {
				continue;
			}
			// The *index* is the item's own; the *kind* is not, for an unpatched item: main's
			// `UnpatchedParam::getParamKind` reports whatever collection the editor's model stack lands on, and with
			// no root UI open that is the song's. A `ParamRef` states one kind statically, so both spellings are
			// accepted for an unpatched node.
			const auto want = static_cast<deluge::modulation::params::Kind>(*kind);
			const bool unpatched = want == deluge::modulation::params::Kind::UNPATCHED_SOUND
			                       || want == deluge::modulation::params::Kind::UNPATCHED_GLOBAL;
			if (unpatched) {
				expect(spec::runtime_of(e.node->bind.param).kind).to_satisfy([](auto kind) {
					using deluge::modulation::params::Kind;
					return kind == Kind::UNPATCHED_SOUND || kind == Kind::UNPATCHED_GLOBAL;
				});
			}
			else {
				expect(spec::runtime_of(e.node->bind.param).kind).to_equal(want);
			}
			compared++;
		}
		expect(compared).to_be_greater_than(size_t(30));
	});

	it("says a node uses AFFECT ENTIRE exactly where main's item does", _ {
		std::string wrong;
		size_t compared = 0;
		for (const spec::TreeEntry& e : t1::entries()) {
			if (e.node->kind == menu::Kind::Submenu) {
				continue; // a submenu's answer depends on its cursor (`menu::uses_affect_entire` ports that)
			}
			const std::string want = table().value(e.path, "ae");
			if (std::to_string(menu::node_uses_affect_entire(*e.node) ? 1 : 0) != want) {
				wrong += e.path + " " + text_of(e.node->name) + " main-" + want + "; ";
			}
			compared++;
		}
		expect(wrong).to_equal(std::string{});
		expect(compared).to_be_greater_than(size_t(80));
	});

	it("marks the select turn as editing the instrument exactly where main's item does", _ {
		const Fixture& f = fixture();
		size_t edits = 0;
		for (const spec::TreeEntry& e : t1::entries()) {
			if (e.node->kind == menu::Kind::Submenu) {
				continue;
			}
			menu::MenuState st{};
			st.path[0] = e.node;
			const bool port = menu::select_turn_edits_instrument(f.ctx, st);
			expect(e.path + " edits " + std::to_string(port ? 1 : 0))
			    .to_equal(e.path + " edits " + table().value(e.path, "edits"));
			edits += port ? 1 : 0;
		}
		expect(edits).to_be_greater_than(size_t(30));
	});

	it("marks exactly main's range-dependent items as range_dependent", _ {
		(void)fixture();
		for (const spec::TreeEntry& e : t1::entries()) {
			if (e.node->kind == menu::Kind::Submenu) {
				continue;
			}
			expect(e.path + " range_dep " + std::to_string(e.node->range_dependent ? 1 : 0))
			    .to_equal(e.path + " range_dep " + table().value(e.path, "range_dep"));
		}
		// The four classes that route through `checkPermissionToBeginSessionForRangeSpecificParam` are ten nodes here.
		expect(nodes::kRangeSpecificNodes.size()).to_equal(size_t(10));
		expect(nodes::kHalfPrecisionNodes.size()).to_equal(size_t(3));
	});
});

describe tables_1_values("scenes::menu sound-subtree tables: values", $ {
	it("reads what main's menu reads, for every node that holds a value", _ {
		const Fixture& f = fixture();
		size_t compared = 0;
		for (const spec::TreeEntry& e : t1::entries()) {
			// Every leaf main's item holds a value for has a row, but a target-bound one the fixture does not show
			// (which answers nothing readable), a bespoke `Kind::Custom` one and a two-ended range.
			const bool shown_here = (e.parent == nullptr || menu::bindings::relevant(*e.parent, f.ctx))
			                        && menu::bindings::relevant(*e.node, f.ctx);
			const bool asked = e.node->kind != menu::Kind::Submenu && e.node->kind != menu::Kind::Custom
			                   && e.node->kind != menu::Kind::Range && !(e.node->bind.target.valid() && !shown_here);
			const std::vector<std::string>* want = table().find(e.path, "read");
			if (want == nullptr) {
				expect(e.path + (asked ? " has none" : " has a row")).to_equal(e.path + " has a row");
				continue;
			}
			const std::string at = e.path + " " + text_of(e.node->name) + " reads ";
			expect(at + std::to_string(menu::bindings::read(*e.node, f.ctx)))
			    .to_equal(at + (want->empty() ? std::string{"<no value>"} : want->front()));
			compared++;
		}
		expect(compared).to_be_greater_than(size_t(50));
	});

	it("names every enum option exactly as main's list does", _ {
		const Fixture& f = fixture();
		size_t compared = 0;
		for (const spec::TreeEntry& e : t1::entries()) {
			if (e.node->kind != menu::Kind::Enum) {
				continue;
			}
			const std::vector<std::string>* want = table().find(e.path, "options");
			if (want == nullptr) {
				// Every enum but the lane-only ones has a row, shown on this fixture or not.
				const bool lane_only = e.node->bind.target.shown_on == deluge::ui::scenes::bind::Target::ShownOn::ON_LANE;
				expect(e.path + (lane_only ? " has options" : " has none")).to_equal(e.path + " has options");
				continue;
			}
			std::string_view got[menu::kMaxDynamicNames];
			deluge::ui::shell::TextScratch scratch[menu::kMaxDynamicNames];
			const size_t n = menu::bindings::names(*e.node, f.ctx, got, scratch);
			std::string port;
			for (size_t i = 0; i < n; i++) {
				port += std::string{got[i]} + "|";
			}
			expect(e.path + " " + port).to_equal(e.path + " " + joined(*want));
			compared++;
		}
		expect(compared).to_be_greater_than(size_t(10));
	});

	it("shows each node exactly where main's menu shows it, on the fixture sound", _ {
		const Fixture& f = fixture();
		size_t ever_true = 0;
		for (const spec::TreeEntry& e : t1::entries()) {
			const bool got = menu::bindings::relevant(*e.node, f.ctx);
			const std::string at = e.path + " " + text_of(e.node->name) + " ";
			expect(at + (got ? "shows" : "hides")).to_equal(at + (table().value(e.path, "rel") == "1" ? "shows" : "hides"));
			ever_true += got ? 1 : 0;
		}
		expect(ever_true).to_be_greater_than(size_t(60));
	});

	it("carries main's occupied-slot counts for every leaf", _ {
		(void)fixture();
		// The wide items, by their sources: `FilterRouting` draws the whole four-slot page and the audio
		// recorder takes two.
		expect(int(nodes::kSoundFiltersChildren[0]->children[2]->slots)).to_equal(4);
		expect(int(nodes::kSoundOscChildren[0]->children[0]->slots)).to_equal(2);
		expect(int(nodes::kSoundOscChildren[0]->children[1]->slots)).to_equal(2);
		for (const spec::TreeEntry& e : t1::entries()) {
			if (e.node->kind == menu::Kind::Submenu) {
				continue;
			}
			expect(e.path + " slots " + std::to_string(e.node->slots))
			    .to_equal(e.path + " slots " + table().value(e.path, "slots"));
		}
	});

	it("has a registered name for every predicate the tables use", _ {
		for (const spec::TreeEntry& e : t1::entries()) {
			if (e.node->relevant == nullptr) {
				continue;
			}
			expect(menu::relevance::name_of(e.node->relevant)).not_().to_equal(std::string_view{"?"});
		}
		// The registry is hand-maintained (see `relevance.cpp`'s note): C++ cannot enumerate a
		// header's declarations, so what is checkable is that it has the documented number of rows,
		// that no row is null, and that no two rows share a name or a function.
		const auto registry = menu::relevance::registry();
		// The 21 predicates `relevance.h` declares plus the sixteen `nodes::relevant_*_iterance_step_*`
		// rows (`relevance.cpp`'s `static_assert` is the authority; this asserts the count is
		// deliberate). 21 + 16 = 37.
		expect(registry.size()).to_equal(size_t(37));
		for (size_t i = 0; i < registry.size(); i++) {
			expect(registry[i].fn).not_().to_be_null();
			expect(registry[i].name.empty()).to_be_false();
			for (size_t j = i + 1; j < registry.size(); j++) {
				expect(address_of(registry[i].fn)).not_().to_equal(address_of(registry[j].fn));
				expect(registry[i].name).not_().to_equal(registry[j].name);
			}
		}
	});
});

describe tables_1_validation("scenes::menu sound-subtree tables: validation", $ {
	it("validates clean, its FormattedTitle siblings told apart by their title_arg", _ {
		Fixture& f = fixture();
		const MenuContext contexts[] = {f.ctx};
		// Only the shortcut sweep is stood down, and only because this is a *subtree*: the rule is
		// quantified over every root's table, so pointed at these roots it would report every cell in
		// every other root's (`validate.h`'s `skip_shortcut_sweep`). Nothing else is: no
		// placeholder option is passed: every node in the tree has a real item.
		const auto issues =
		    menu::validate_tree(nodes::kBatch1Roots, contexts, nodes::kRangeSpecificNodes,
		                        nodes::kHalfPrecisionNodes, menu::ValidateOptions{.skip_shortcut_sweep = true});
		size_t duplicates = 0;
		size_t others = 0;
		for (const menu::Issue& i : issues) {
			if (i.kind == menu::Issue::Kind::DuplicateName) {
				duplicates++;
			}
			else {
				others++;
			}
		}
		// The sample recorders, oscillator levels and FM modulator levels share an l10n id in pairs;
		// legacy tells each pair apart with a `FormattedTitle` argument, which is their `title_arg`.
		expect(duplicates).to_equal(size_t(0));
		expect(others).to_equal(size_t(0));
	});

	it("reports no placeholder, even when strict", _ {
		Fixture& f = fixture();
		const MenuContext contexts[] = {f.ctx};
		// This tree is clean under the default (strict) options: both file selectors, the
		// four sample loop points and both audio recorders have real items (`nodes/sample.h`).
		// `tests/integration/ui/scenes/sample_items_spec.cpp` pins which item each of those nodes got, and this
		// is the same claim `output_items_spec.cpp` makes over `kAllRoots`, made here over `kBatch1Roots`.
		const auto issues =
		    menu::validate_tree(nodes::kBatch1Roots, contexts, nodes::kRangeSpecificNodes,
		                        nodes::kHalfPrecisionNodes, menu::ValidateOptions{.skip_shortcut_sweep = true});
		size_t placeholders = 0;
		for (const menu::Issue& i : issues) {
			if (i.kind == menu::Issue::Kind::PlaceholderCustom) {
				placeholders++;
			}
			expect(i.kind).not_().to_equal(menu::Issue::Kind::NullCustom);
		}
		expect(placeholders).to_equal(size_t(0));
	});

	it("assigns an id to every node under kBatch1Roots", _ {
		Fixture& f = fixture();
		const MenuContext contexts[] = {f.ctx};
		(void)menu::validate_tree(nodes::kBatch1Roots, contexts, {}, {},
		                          menu::ValidateOptions{.skip_shortcut_sweep = true});
		for (const Node* root : nodes::kBatch1Roots) {
			expect(menu::node_id(*root)).not_().to_equal(uint16_t(0));
			for (const Node* kid : root->children) {
				expect(menu::node_id(*kid)).not_().to_equal(uint16_t(0));
			}
		}
	});
});

describe tables_1_pixels("scenes::menu sound-subtree tables: pixels", $ {
	it("renders the LPF page", _ {
		Fixture& f = fixture();
		deluge::base::notify::dirty().drain();
		Rig r;
		const Node* filters = nodes::kSoundFiltersChildren[0];
		r.open(*filters->children[0], f.ctx);
		r.shell.tick(0);
		expect_golden(self, "ui/menu/tables1_lpf_48.pbm", r.fake.pbm(0));
	});
});

describe tables_1_writes("scenes::menu sound-subtree tables: writes", $ {
	it("leaves the model where main's menu leaves it, for every set hook", _ {
		// Reads, names, params, enums, learn targets and relevance are compared elsewhere; this drives the `set` hooks.
		// Every hook of these tables is driven here for the three values spanning the node's range: each write starts
		// from a sentinel that is not the value, so a hook that stores nothing shows, and the port's read after its own
		// write is compared with the frozen read after main's.
		//
		// `ParamRef` nodes are not here, for the reason `tables_2_spec` spells out: with no root UI open, main's
		// `UnpatchedParam::writeCurrentValue` resolves the *song's* param collection where the port resolves the
		// clip's. `bindings_spec` covers that path directly.
		const Fixture& f = fixture();
		size_t exercised = 0;
		// A probe count with slack cannot tell a node that left the table from one that never had a hook. What is
		// asserted is the **set of distinct nodes driven**, against a set derived from the table itself -- every node
		// in the walk carrying a `set` hook.
		std::set<const Node*> covered;
		std::set<const Node*> with_hook;
		for (const spec::TreeEntry& e : t1::entries()) {
			if (e.node->bind.set == nullptr) {
				continue;
			}
			with_hook.insert(e.node);
			const menu::IntRange r = menu::bindings::range(*e.node, f.ctx);
			std::string probed;
			for (const int32_t v : spec::probe_values(r)) {
				const std::optional<int32_t> sentinel = spec::sentinel_for(r, v);
				if (!sentinel.has_value()) {
					continue; // a one-value range has no state to start from that is not `v`
				}
				const std::string column = "write/" + std::to_string(v);
				const std::vector<std::string>* want = table().find(e.path, column);
				menu::bindings::write(*e.node, f.ctx, *sentinel);
				menu::bindings::write(*e.node, f.ctx, v);
				const std::string at = e.path + " " + text_of(e.node->name) + " write " + std::to_string(v) + " reads ";
				expect(at + std::to_string(menu::bindings::read(*e.node, f.ctx)))
				    .to_equal(at + (want == nullptr || want->empty() ? std::string{"<no row>"} : want->front()));
				exercised++;
				covered.insert(e.node);
			}
			expect(table().columns(e.path, "write/").size()).to_equal(spec::distinct_probe_values(r).size());
		}
		// Every node of these tables with a `set` hook is driven -- no exceptions.
		expect(covered.size()).to_equal(with_hook.size());
		for (const Node* n : with_hook) {
			expect(covered.contains(n)).to_be_true();
		}
		// ... and each was driven for all three values its range spans.
		expect(exercised).to_equal(size_t(3) * with_hook.size());
	});
});

describe tables_1_range_picker("scenes::menu sound-subtree tables: the multi-range picker", $ {
	it("labels every row exactly as MultiRange::getText labels it, on the same fixture", _ {
		// The port reads the ranges out of its own `MenuContext`; the rows are what main's `getText` wrote for the
		// same three ranges. A port label built from nothing (an empty string, say) fails against a label that is not
		// empty.
		RangeFixture f{3};
		// Distinct, realistic top notes, so the labels are not all the same string -- an all-"top" list would pass a
		// comparison that only ever looked at one arm.
		const int16_t tops[] = {47, 71, 32767}; // B2, B4, the top
		f.set_tops(tops);

		expect(menu::range_count(f.ctx)).to_equal(int32_t(3));
		size_t compared = 0;
		for (int32_t i = 0; i < 3; i++) {
			const std::string path = "picker/r3/" + std::to_string(i);
			expect(path + " " + label_of(f.ctx, i)).to_equal(path + " " + table().value(path, "label"));
			compared++;
		}
		expect(compared).to_equal(size_t(3));
		// And they really are three different strings -- so the comparison above is about the labels rather than about
		// one string repeated.
		const std::string l0 = label_of(f.ctx, 0);
		const std::string l1 = label_of(f.ctx, 1);
		const std::string l2 = label_of(f.ctx, 2);
		expect(l0).not_().to_equal(l1);
		expect(l1).not_().to_equal(l2);
		expect(l0).not_().to_equal(l2);
	});

	it("labels a note below C-2 in the octave under it, as MultiRange::getText does", _ {
		// A range's top note can sit below note code 0 once a clip is transposed down. The octave is the floor of
		// the twelfths, not their truncation: note -1 is B-3, a line below note 11's B-2.
		RangeFixture f{3};
		const int16_t tops[] = {-14, -1, 32767}; // A#-4, B-3, the top
		f.set_tops(tops);

		for (int32_t i = 0; i < 3; i++) {
			const std::string path = "picker/low/" + std::to_string(i);
			expect(path + " " + label_of(f.ctx, i)).to_equal(path + " " + table().value(path, "label"));
		}
		expect(label_of(f.ctx, 1)).to_equal(std::string("B-4    - B-3"));
	});

	it("names a black-key top note with the flat glyph when the device spells with flats", _ {
		// With the device spelling in flats, a black key's name is the next letter up and the font's flat glyph; the
		// spelling is a process-wide setting, so the guard puts it back however the example ends.
		struct SharpsGuard {
			bool saved = FlashStorage::defaultUseSharps;
			~SharpsGuard() { FlashStorage::defaultUseSharps = saved; }
		} guard;
		RangeFixture f{3};
		const int16_t tops[] = {46, 70, 32767}; // A#1/Bb1, A#3/Bb3, the top
		f.set_tops(tops);

		const std::string flat_b3 = std::string{"B"} + deluge::ui::gfx::oled::kFlatGlyph + "3";
		FlashStorage::defaultUseSharps = false;
		const std::string flats = label_of(f.ctx, 1);
		expect(flats.substr(flats.size() - flat_b3.size())).to_equal(flat_b3);
		FlashStorage::defaultUseSharps = true;
		const std::string sharps = label_of(f.ctx, 1);
		expect(sharps.substr(sharps.size() - 3)).to_equal(std::string{"A#3"});
		expect(flats.size()).to_equal(sharps.size());
	});

	it("chains to the node it stood in for, and its chain_targets are the range-specific set", _ {
		// `MultiRange::selectButtonPress` returns the item the picker was heading to; the port's equivalent pair is
		// `MenuWalkState::pending_after_range` and this hook.
		RangeFixture f{2};
		menu::MenuState st{};
		expect(nodes::multi_range_chain(f.ctx, st)).to_be_null(); // nothing pending -> ascend
		st.walk.pending_after_range = &nodes::source0_transpose;
		expect(nodes::multi_range_chain(f.ctx, st)).to_equal(&nodes::source0_transpose);

		// The span has to be the whole `range_specific` set, because any one of them can be what
		// `permission_for` refused. Asserted against `nodes::kRangeSpecificNodes` itself so the two
		// lists cannot drift (`nodes/multi_range.h` says why it is spelled twice).
		expect(nodes::kMultiRangeChain.size()).to_equal(nodes::kRangeSpecificNodes.size());
		for (const Node* n : nodes::kRangeSpecificNodes) {
			expect(nodes::kMultiRangeChain).to_contain(n);
		}
		// And the picker itself is NOT range-specific -- see `nodes/multi_range.h`: it would recurse.
		expect(nodes::multi_range.range_specific).to_be_false();
	});

	it("getOrCreateFirstRange is run for a zero-range source", _ {
		// `checkPermissionToBeginSessionForRangeSpecificParam` creates the source's first range
		// before answering anything; `permission_for` is a pure predicate and does not.
		// `menu::ensure_first_range` does it, and
		// `MenuScene::enter_child` is what calls it.
		RangeFixture f{0};
		expect(menu::range_count(f.ctx)).to_equal(int32_t(0));
		expect(spec::menu_engine(f.ctx, *f.song.song).range).to_be_null();

		expect(menu::ensure_first_range(f.ctx)).to_be_true();
		// One range now exists, and the context points at it, so a range-dependent item opens rather than
		// bouncing to a one-row picker.
		expect(menu::range_count(f.ctx)).to_equal(int32_t(1));
		expect(spec::menu_engine(f.ctx, *f.song.song).range).not_().to_be_null();
		expect(static_cast<int>(f.ctx.range_index)).to_equal(0);
		expect(menu::permission_for(nodes::source0_transpose, f.ctx)).to_equal(menu::Permission::Yes);
		// `SourceResources::getOrCreateFirstRange` gives the new range the top of the keyboard.
		expect(menu::range_top_note(f.ctx, 0)).to_equal(int32_t(32767));

		// Idempotent: a second call must not add a second range.
		expect(menu::ensure_first_range(f.ctx)).to_be_true();
		expect(menu::range_count(f.ctx)).to_equal(int32_t(1));
	});

	it("moves an edge one note per turn, clamped, exactly as MultiRange's edge arm does", _ {
		// Main's edge arm on four ranges, against the port's arithmetic: every range, both edges, both directions,
		// turned far enough to reach every clamp. The row holds all four ranges, since a left edge moves the range
		// below's top.
		using deluge::ui::widgets::oled::RangeEdge;
		RangeFixture f{4};
		const int16_t tops[] = {40, 41, 60, 32767};
		int32_t compared = 0;
		for (int32_t index = 0; index < 4; index++) {
			for (const RangeEdge edge : {RangeEdge::Left, RangeEdge::Right}) {
				const bool editable = nodes::may_edit_range_edge(f.ctx, index, edge);
				for (const int32_t offset : {1, -1}) {
					const std::string path = "picker/edge/" + std::to_string(index) + "/"
					                         + (edge == RangeEdge::Left ? "L" : "R") + "/" + std::to_string(offset);
					if (!editable) {
						expect(path + (table().find(path, "range") != nullptr ? " has a row" : " has none"))
						    .to_equal(path + " has none");
						continue;
					}
					f.set_tops(tops);
					std::vector<SourceRange> ported = menu::ranges_of(f.ctx);
					for (int32_t k = 0; k < 130; k++) {
						nodes::step_range_edge(ported, index, edge, offset);
					}
					expect(path + " " + spans(ported)).to_equal(path + " " + frozen(path, "range"));
					compared++;
				}
			}
		}
		expect(compared).to_equal(12); // three left edges and three right edges, each both ways
	});

	it("picks an edge on a horizontal turn exactly as Range::horizontalEncoderAction does", _ {
		using deluge::ui::widgets::oled::RangeEdge;
		RangeFixture f{3};
		const auto name_of = [](RangeEdge e) { return e == RangeEdge::Left ? "left" : e == RangeEdge::Right ? "right" : "off"; };
		size_t compared = 0;
		for (int32_t index = 0; index < 3; index++) {
			for (const RangeEdge start : {RangeEdge::Off, RangeEdge::Left, RangeEdge::Right}) {
				for (const int32_t offset : {1, -1}) {
					const std::string path = "picker/hedge/" + std::to_string(index) + "/" + name_of(start) + "/"
					                         + std::to_string(offset);
					const RangeEdge ported = nodes::next_range_edge(f.ctx, index, start, offset);
					expect(path + " " + name_of(ported)).to_equal(path + " " + table().value(path, "edge"));
					compared++;
				}
			}
		}
		expect(compared).to_equal(size_t(18));
	});

	it("inserts a range beside the current one exactly as MultiRange's Shift arm does, and refuses one of one note", _ {
		// Shift and a select turn on four ranges whose second holds a single note (41): the planner's answer applied by
		// hand, and the picker driven through the host, both against main's.
		const int16_t tops[] = {40, 41, 60, 32767};
		int32_t inserts = 0;
		int32_t refusals = 0;
		for (int32_t index = 0; index < 4; index++) {
			for (const int32_t offset : {1, -1}) {
				RangeFixture f{4};
				f.set_tops(tops);
				const std::string path = "picker/insert/" + std::to_string(index) + "/" + std::to_string(offset);
				const bool refused = table().value(path, "ranges") == "refused";
				std::vector<SourceRange> ported = menu::ranges_of(f.ctx);
				const std::optional<nodes::RangeInsert> plan = nodes::plan_range_insert(ported, index, offset);

				menu::MenuState picker{};
				nodes::kMultiRangeItem.on_focus(f.ctx, picker);
				picker.list[0].cursor = static_cast<int16_t>(index);
				{
					deluge::ui::test::Rig48 rig;
					rig.input.turn = static_cast<int8_t>(offset);
					rig.input.shift = true;
					nodes::kMultiRangeItem.render(rig.frame, f.ctx, picker);
				}
				display->cancelPopup();
				const std::vector<SourceRange> driven = menu::ranges_of(f.ctx);

				expect(path + (plan ? " inserts" : " refuses")).to_equal(path + (refused ? " refuses" : " inserts"));
				if (!plan) {
					refusals++;
					expect(path + " " + spans(driven)).to_equal(path + " " + spans(ported));
					continue;
				}
				inserts++;
				ported.insert(ported.begin() + plan->at, SourceRange{.top_note = plan->top_note});
				if (plan->previous_top) {
					ported[static_cast<size_t>(plan->at - 1)].top_note = *plan->previous_top;
				}
				expect(path + " " + spans(ported)).to_equal(path + " " + frozen(path, "ranges"));
				expect(path + " " + spans(driven)).to_equal(path + " " + frozen(path, "ranges"));
				expect(path + " cursor " + std::to_string(plan->at)).to_equal(path + " cursor " + table().value(path, "cursor"));
				expect(path + " cursor " + std::to_string(picker.list[0].cursor))
				    .to_equal(path + " cursor " + table().value(path, "cursor"));
			}
		}
		expect(inserts).to_equal(6);
		expect(refusals).to_equal(2);
	});

	it("deletes a range exactly as MultiRange::deletePress does, and refuses the last", _ {
		const int16_t tops[] = {40, 50, 60, 32767};
		for (int32_t index = 0; index < 4; index++) {
			RangeFixture f{4};
			f.set_tops(tops);
			const std::string path = "picker/delete/4/" + std::to_string(index);
			std::vector<SourceRange> ported = menu::ranges_of(f.ctx);
			const std::optional<nodes::RangeDelete> plan = nodes::plan_range_delete(ported, index);
			expect(path + (plan ? " deletes" : " refuses")).to_equal(path + " deletes");
			expect(table().value(path, "ranges")).not_().to_equal("refused");
			if (!plan) {
				continue;
			}
			ported.erase(ported.begin() + index);
			if (plan->lower_top) {
				ported[static_cast<size_t>(index - 1)].top_note = *plan->lower_top;
			}
			expect(path + " " + spans(ported)).to_equal(path + " " + frozen(path, "ranges"));
			expect(path + " cursor " + std::to_string(plan->cursor)).to_equal(path + " cursor " + table().value(path, "cursor"));

			// And through the host, as SAVE with Shift reaches it.
			menu::MenuState state{};
			state.list[0].cursor = static_cast<int16_t>(index);
			nodes::range_delete_press(f.ctx, state);
			display->cancelPopup();
			expect(path + " " + spans(menu::ranges_of(f.ctx))).to_equal(path + " " + frozen(path, "ranges"));
			expect(path + " cursor " + std::to_string(state.list[0].cursor))
			    .to_equal(path + " cursor " + table().value(path, "cursor"));
		}
		RangeFixture last{1};
		const std::string path = "picker/delete/1/0";
		expect(nodes::plan_range_delete(menu::ranges_of(last.ctx), 0).has_value()).to_be_false();
		menu::MenuState state{};
		nodes::range_delete_press(last.ctx, state);
		display->cancelPopup();
		expect(path + " " + std::to_string(menu::range_count(last.ctx))).to_equal(path + " 1");
		expect(table().value(path, "ranges")).to_equal(std::string{"refused"});
	});

	it("keeps its window where MultiRange's currentScroll keeps it: on opening, on a flick, and on an insert before", _ {
		// Each step: open, a flick of -1, a flick of -2 more, a flick of +1 (the range mid-window), and a Shift turn of
		// -1 (inserted before, which moves the window with the old range). The rows hold main's scroll after each.
		const char* const steps[] = {"open", "down1", "down3", "up1", "insert_before"};
		RangeFixture f{6};
		const int16_t tops[] = {20, 30, 40, 50, 60, 32767};
		f.set_tops(tops);
		static_cast<SoundInstrument*>(f.song.instrument)->sourceResources[0].defaultRangeI = 4;
		std::vector<int32_t> got;
		menu::MenuState picker{};
		picker.edit_edge = deluge::ui::widgets::oled::RangeEdge::Left; // opening ends in `Range::beginSession`
		nodes::kMultiRangeItem.on_focus(f.ctx, picker);
		expect(static_cast<int>(picker.edit_edge)).to_equal(static_cast<int>(deluge::ui::widgets::oled::RangeEdge::Off));
		const auto frame = [&](int8_t turn, bool shift) {
			deluge::ui::test::Rig48 rig;
			rig.input.turn = turn;
			rig.input.shift = shift;
			nodes::kMultiRangeItem.render(rig.frame, f.ctx, picker);
			got.push_back(picker.list[0].scroll);
		};
		frame(0, false);
		frame(-1, false);
		frame(-2, false);
		frame(1, false);
		frame(-1, true);
		display->cancelPopup();
		expect(got.size()).to_equal(std::size(steps));
		for (size_t i = 0; i < got.size() && i < std::size(steps); i++) {
			const std::string path = std::string{"picker/scroll/"} + steps[i];
			expect(path + " " + std::to_string(got[i])).to_equal(path + " " + table().value(path, "scroll"));
		}
	});

	it("deletes through the sound port, the cursor going to the range below, and not while a popup is up", _ {
		// `deletePress` (`origin/main:src/deluge/gui/menu_item/multi_range.cpp` `MultiRange::deletePress`): nothing
		// while an edge is edited or a popup is up.
		RangeFixture f{3};
		auto* sound = static_cast<SoundInstrument*>(f.song.instrument);
		menu::MenuState state{};
		state.list[0].cursor = 2;
		MenuContext gated = f.ctx;
		spec::HeldPorts popup{gated};
		popup.ui.popup = true;
		nodes::range_delete_press(gated, state);
		expect(static_cast<int32_t>(std::ssize(sound->sourceResources[0].ranges))).to_equal(3);
		state.edit_edge = deluge::ui::widgets::oled::RangeEdge::Left;
		nodes::range_delete_press(f.ctx, state);
		expect(static_cast<int32_t>(std::ssize(sound->sourceResources[0].ranges))).to_equal(3);
		state.edit_edge = deluge::ui::widgets::oled::RangeEdge::Off;
		nodes::range_delete_press(f.ctx, state);
		expect(static_cast<int32_t>(std::ssize(sound->sourceResources[0].ranges))).to_equal(2);
		expect(static_cast<int32_t>(state.list[0].cursor)).to_equal(1);
		expect(static_cast<int32_t>(state.walk.range_choice)).to_equal(1);
		display->cancelPopup();

		// `selectEncoderAction` is refused whole while a popup is up (`MultiRange::selectEncoderAction` in
		// `origin/main:src/deluge/gui/menu_item/multi_range.cpp`): the turn moves no cursor.
		menu::MenuState picker{};
		picker.list[0].cursor = 0;
		{
			deluge::ui::test::Rig48 rig;
			rig.input.turn = 1;
			nodes::kMultiRangeItem.render(rig.frame, gated, picker);
			expect(static_cast<int32_t>(picker.list[0].cursor)).to_equal(0);
		}
		{
			deluge::ui::test::Rig48 rig;
			rig.input.turn = 1;
			nodes::kMultiRangeItem.render(rig.frame, f.ctx, picker);
			expect(static_cast<int32_t>(picker.list[0].cursor)).to_equal(1);
		}
	});

	it("moves to a played note's range as noteOnToChangeRange does, a note at a range's top inside it, and not mid-edge", _ {
		// `MultiRange::noteOnToChangeRange` and `SourceResources::getRangeIndex`: the first range whose top is at or above the note.
		RangeFixture f{3};
		auto* sound = static_cast<SoundInstrument*>(f.song.instrument);
		const int16_t tops[] = {40, 60, 32767};
		for (int32_t i = 0; i < 3; i++) {
			sound->sourceResources[0].ranges.getElement(i)->topNote = tops[i];
		}
		deluge::edits::recordRanges(*sound, 0);
		MenuContext ctx = spec::ctx_of(*f.song.song, deluge::gui::menuViewOf(f.song.clip), 0, false, false, false,
		                               /*kit_global_fx=*/false);
		menu::MenuState picker{};
		picker.list[0].cursor = 2;
		expect(nodes::range_note_press(ctx, picker, 40)).to_be_true();
		expect(static_cast<int32_t>(picker.list[0].cursor)).to_equal(0);
		expect(static_cast<int32_t>(ctx.range_index)).to_equal(0);
		expect(static_cast<int32_t>(sound->sourceResources[0].getRangeIndex(40))).to_equal(0);
		picker.edit_edge = deluge::ui::widgets::oled::RangeEdge::Right;
		expect(nodes::range_note_press(ctx, picker, 70)).to_be_false();
		expect(static_cast<int32_t>(picker.list[0].cursor)).to_equal(0);
	});

	it("selects a range the way setCurrentMultiRange does", _ {
		RangeFixture f{3};
		menu::select_range(f.ctx, 2);
		expect(static_cast<int>(f.ctx.range_index)).to_equal(2);
		expect(spec::menu_engine(f.ctx, *f.song.song).range).to_equal(spec::menu_engine(f.ctx, *f.song.song).source->ranges.getElement(2));
		// Out of range is ignored rather than clamped -- every caller has clamped already.
		menu::select_range(f.ctx, 7);
		expect(static_cast<int>(f.ctx.range_index)).to_equal(2);
	});
});
// clang-format on

// clang-format off
describe tables_1_reference_rows("tables_1: the reference rows", $ {
	it("parses tables_1.tsv clean and asks every row of it", _ {
		spec::OracleTable& t = spec::OracleTable::named("tables_1");
		expect(t.problems().size()).to_equal(size_t{0});
		expect(t.size()).to_be_greater_than(size_t{800});
		expect(t.unasked()).to_equal(std::string{});
	});
});
// clang-format on

DELUGE_SPEC(tables_1_spec, tables_1_shape, tables_1_values, tables_1_validation, tables_1_pixels, tables_1_writes,
            tables_1_range_picker, tables_1_reference_rows)
