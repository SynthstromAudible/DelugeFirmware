/// @file
/// @brief The second menu tables -- the whole sound-editor root -- checked against the reference rows of upstream
///        `main`'s menu (`tests/golden/ui/menu_oracle/tables_2.tsv`).
///
/// These tables cover the six sound subtrees (filters, compressor, unison, oscillators, envelopes, LFOs) and the root
/// itself with everything else hanging off it: actions, master, arpeggiator, randomizer, FX, sidechain, voice, bend,
/// the sequence direction and the output-MIDI page. The rows hold what main's items answer on a synth clip and on a kit
/// row, so the whole transcription is checked against the originals rather than against a second hand-written list:
///
///   * the tree lists the children main's menu lists, under each submenu, and nothing else;
///   * each node's name and title are main's own;
///   * each node's `ParamRef` index and kind, AFFECT ENTIRE use, edit marking, slot count, range dependence and learn
///     target are main's;
///   * `bindings::read(node, ctx)` equals what main's item reads on the same fixture;
///   * each `Kind::Enum`'s names are main's option strings;
///   * each node's `relevant` predicate agrees with main's `isRelevant()`;
///
/// plus `validate_tree` over the root (placeholders allowed, see the validation group) and two `MenuScene` renders of
/// the root list.
///
/// @note The answers are rows of a reference table, not live calls: the spec includes no main-menu header, and the
///       last group asks that it asked every row (`OracleTable::unasked`).

#include "gui/ui/menu_view_of.h"
#include "support/sound_fixtures.h"
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

#include "ui/shell/profiles/deluge.h"
#include "ui/shell/shell.h"

#include "gui/l10n/l10n.h"
#include "gui/l10n/strings.h"
#include "hid/display/oled.h"

#include "model/clip/instrument_clip.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/project_bridge/patch_cable_words.h"
#include "model/song/song.h"
#include "modulation/arpeggiator.h"
#include "modulation/params/param_set.h"
#include "modulation/patch/patch_cable.h"
#include "modulation/patch/patch_cable_set.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"
#include "processing/sound/source_resources.h"
#include "project/param_automation.h"

#include "support/known_differences.h"
#include "support/menu_engine.h"
#include "support/oracle_table.h"
#include "support/oracle_tree.h"
#include "support/song_fixture.h"
#include "support/tables_2_fixtures.h"

#include "support/mocks/fake_surfaces.h"
#include "support/printers/controller_mapping_printer.h"
#include "support/printers/param_id_printer.h"
#include "unit/ui/gfx/support/golden.h"

#include "cppspec.hpp"
#include "support/matchers/address_of.h"
#include "support/matchers/value_matchers.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <type_traits>
#include <vector>

#include "support/menu_context.h"
#include "support/spec_main.h"

using namespace deluge::ui::scenes;
using deluge::l10n::String;
using deluge::ui::scenes::menu::MenuContext;
using deluge::ui::scenes::menu::Node;

namespace t2 = deluge::ui::scenes::spec::tables2;

namespace {

using deluge::test::matchers::address_of;

namespace nodes = deluge::ui::scenes::menu::nodes;
namespace bindings = deluge::ui::scenes::menu::bindings;

using t2::chain_name;
using t2::chain_targets_contain;
using t2::fixture;
using t2::install;
using t2::kit_fixture;
using t2::KitRig;
using t2::Rigged;
using t2::seed_cable;
using t2::select_drum;
using t2::shown;
using t2::text_of;

/// @brief The sound root's nodes, keyed as the reference rows key them.
const std::vector<spec::TreeEntry>& entries() {
	static const std::vector<spec::TreeEntry> list = spec::tree_entries("sound", nodes::sound_root);
	return list;
}

spec::OracleTable& table() {
	return spec::OracleTable::named("tables_2");
}

/// @brief One fixture the sound root's answers are recorded on: a synth clip, and a kit row with its first drum
/// selected.
struct Fx {
	const char* name;
	MenuContext ctx;
	Song* song;
};

std::vector<Fx> fixtures() {
	Rigged& synth = fixture();
	KitRig& kit = kit_fixture();
	const MenuContext kit_ctx = select_drum(kit, kit.drum_a);
	return {Fx{"synth", synth.ctx, synth.song.song}, Fx{"kit row", kit_ctx, kit.song.song}};
}

/// @brief The page a chain case is recorded under: its tree path, or `chain/<name>` for a chain-only page.
std::string chain_path(const Node& node) {
	const std::string path = spec::first_path_of(entries(), node);
	return path.empty() ? "chain/" + chain_name(&node) : path;
}

/// @brief Reach `Output::activeClip`, which is `protected` (`model/output.h`) and whose only public setter also
/// re-sends
///        MIDI program changes. The [class.protected] route: the pointer-to-member is formed inside a derived class;
///        never instantiated.
struct OutputPeek : ::Output {
	static Clip*& active_clip_of(::Output& out) { return out.*(&OutputPeek::activeClip); }
};

/// @brief A `Shell` over `FakeSurfaceSet` with the application's scene registry, for the goldens.
struct Rig {
	const deluge::ui::shell::BoardProfile& profile;
	deluge::ui::shell::SceneRegistry reg = scene_registry();
	uitest::FakeSurfaceSet fake;
	deluge::ui::shell::Shell shell{fake, profile, reg, deluge::ui::shell::Services{}, deluge::ui::shell::ShellConfig{}};

	Rig(const Rig&) = delete;
	Rig& operator=(const Rig&) = delete;

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

	explicit Rig(const DelugeBoard& board = deluge::ui::shell::kDelugeBoard,
	             const deluge::ui::shell::BoardProfile& p = deluge_profile())
	    : profile(p), fake(board, p) {}

	void open(const Node& root, const MenuContext& ctx) {
		(void)menu::assign_node_ids(nodes::kSoundRootRoots);
		menu_scene().open(root, ctx);
		shell.init(deluge::ui::shell::SceneId{deluge::ui::shell::SceneKind::FirstApp, menu::node_id(root)});
	}

	/// @brief One select-encoder press edge, at its own matrix position (`kMatrix`,
	///        `ui/shell/profiles/deluge.cpp` -- the same (4, 3) `menu_scene_spec.cpp` names).
	///
	/// The patch-cable strength and source-selection pages are
	/// reached only through a select press, so a golden of either has to press one.
	void enter(uint32_t at_ms) {
		shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_BUTTON, 4, 3, 255}, at_ms);
		shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_BUTTON, 4, 3, 0}, at_ms);
	}
};

/// @brief A 128x64 single-panel board, so the same scene can be recorded at the other shipped
///        panel geometry (the shape `menu_scene_spec.cpp` established).
constexpr DelugeDisplayDesc kDisplays64[] = {{128, 64, 0}};

DelugeBoard board_64() {
	DelugeBoard b = deluge::ui::shell::kDelugeBoard;
	b.display_height = 64;
	b.display_count = 1;
	b.displays = kDisplays64;
	return b;
}

const deluge::ui::shell::BoardProfile& profile_64() {
	static const deluge::ui::shell::BoardProfile p = [] {
		static constexpr const deluge::ui::widgets::core::Style* kStyles[] = {
		    &deluge::ui::widgets::core::kDelugeStyle64};
		deluge::ui::shell::BoardProfile q = deluge_profile();
		q.style_by_index = kStyles;
		return q;
	}();
	return p;
}

/// @brief Is @p target named by some node's static `children` span, anywhere in the whole tree?
///
/// The point of the chain-only pages is that the answer is *no* for four of them: nothing but a
/// `chain_targets` span reaches them, in the port or in legacy.
bool reachable_by_children(const Node* target) {
	std::vector<const Node*> stack(nodes::kAllRoots.begin(), nodes::kAllRoots.end());
	std::set<const Node*> seen;
	while (!stack.empty()) {
		const Node* n = stack.back();
		stack.pop_back();
		if (n == nullptr || !seen.insert(n).second) {
			continue;
		}
		for (const Node* c : n->children) {
			if (c == target) {
				return true;
			}
			stack.push_back(c);
		}
		// Chain targets are traversed (so a page reached only *below* a chained page is still
		// searched) but never counted as a `children` hit themselves.
		for (const Node* t : n->chain_targets) {
			stack.push_back(t);
		}
	}
	return false;
}

/// Whether writing @p node's param makes the reverb recompute: the Edit raises it for a sidechain's shape (a sound's or
/// a global effectable's) and the global sidechain volume, the params legacy's two `UpdatingReverbParams` items edit.
bool raises_reverb(const Node& node) {
	namespace params = deluge::modulation::params;
	const auto& p = node.bind.param;
	return ((p.kind == params::Kind::UNPATCHED_SOUND || p.kind == params::Kind::UNPATCHED_GLOBAL)
	        && p.index == params::UNPATCHED_SIDECHAIN_SHAPE)
	       || (p.kind == params::Kind::UNPATCHED_GLOBAL && p.index == params::UNPATCHED_SIDECHAIN_VOLUME);
}

} // namespace

// clang-format off
describe tables_2_shape("scenes::menu sound-editor tables: shape", $ {
	it("declares the sound-editor root with the legacy child count and order", _ {
		(void)fixture();
		// `origin/main:src/deluge/gui/ui/menus.cpp`'s `soundEditorRootMenu` literal lists **27** children; the literal
		// is the authority.
		expect(nodes::sound_root.children.size()).to_equal(size_t(27));
		expect(std::to_string(nodes::sound_root.children.size())).to_equal(table().value("sound", "kids"));
	});

	it("lists, under every submenu of the root, the children main's menu lists, and nothing it does not", _ {
		size_t submenus = 0;
		for (const spec::TreeEntry& e : entries()) {
			if (!t2::has_dynamic_name(*e.node)) {
				expect(e.path + (table().find(e.path, "name") != nullptr ? " has a name row" : " has none"))
				    .to_equal(e.path + " has a name row");
			}
			if (e.node->kind == menu::Kind::Submenu) {
				expect(e.path + " kids " + std::to_string(e.node->children.size()))
				    .to_equal(e.path + " kids " + table().value(e.path, "kids"));
				submenus++;
			}
		}
		expect(submenus).to_be_greater_than(size_t(20));
	});

	it("gives every node main's own name", _ {
		Rigged& f = fixture();
		install(f.ctx, f.song.song);
		for (const spec::TreeEntry& e : entries()) {
			if (t2::has_dynamic_name(*e.node)) {
				continue; // see has_dynamic_name's own note
			}
			expect(e.path + " " + shown(e.node->name, *e.node, f.ctx))
			    .to_equal(e.path + " " + table().value(e.path, "name"));
		}
	});

	it("gives every node main's own ParamRef, or none where main has none", _ {
		size_t compared = 0;
		for (const spec::TreeEntry& e : entries()) {
			// A submenu reports the kind of its first param child, not one of its own. A `Kind::Custom` node has no
			// binding at all, and the two that stand in for `patch_cable_strength::Fixed` items (`vibrato`,
			// `sidechain_volume`) report a patch cable descriptor, not a param index. A leaf with a hook or a non-param
			// target owns its value, and its learn-only `ParamRef` is the learn rows' to check.
			if (!spec::param_comparable(*e.node)) {
				expect(table().find(e.path, "param_index")).to_be_null();
				continue;
			}
			const std::optional<int32_t> index = spec::int_of(table().find(e.path, "param_index"));
			const std::string at = e.path + " " + text_of(e.node->name);
			expect(at + " index " + std::to_string(e.node->bind.param.index))
			    .to_equal(at + " index " + (index ? std::to_string(*index) : std::string{"<no row>"}));
			for (const char* fixture : {"synth", "kit row"}) {
				const std::optional<int32_t> kind = spec::int_of(table().find(e.path, std::string{"param_kind@"} + fixture));
				if (!kind || *kind == static_cast<int32_t>(deluge::modulation::params::Kind::NONE)) {
					continue;
				}
				// The *index* is the item's own; the *kind* is not, for an unpatched item: main's
				// `UnpatchedParam::getParamKind` reports whatever collection the editor's model stack lands on.
				const auto want = static_cast<deluge::modulation::params::Kind>(*kind);
				const bool unpatched = want == deluge::modulation::params::Kind::UNPATCHED_SOUND
				                       || want == deluge::modulation::params::Kind::UNPATCHED_GLOBAL;
				if (unpatched) {
					expect(e.node->bind.param.kind).to_satisfy([](auto kind) {
						using deluge::modulation::params::Kind;
						return kind == Kind::UNPATCHED_SOUND || kind == Kind::UNPATCHED_GLOBAL;
					});
				}
				else {
					expect(e.node->bind.param.kind).to_equal(want);
				}
			}
			compared++;
		}
		expect(compared).to_be_greater_than(size_t(50));
	});

	it("says a node uses AFFECT ENTIRE exactly where main's item does", _ {
		// `MenuItem::usesAffectEntire` (`origin/main:src/deluge/gui/menu_item/menu_item.h`): what makes AFFECT ENTIRE a
		// hold in a kit row's menu rather than the clip's toggle. Leaves only: a submenu's answer depends on its cursor
		// (`menu::uses_affect_entire` ports that).
		std::string wrong;
		const auto compare = [&wrong](const std::string& path, const Node& node) {
			const std::string want = table().value(path, "ae");
			if (std::to_string(menu::node_uses_affect_entire(node) ? 1 : 0) != want) {
				wrong += path + " " + text_of(node.name) + " main-" + want + "; ";
			}
		};
		for (const spec::TreeEntry& e : entries()) {
			if (e.node->kind != menu::Kind::Submenu) {
				compare(e.path, *e.node);
			}
		}
		for (const Node* page : {&nodes::arp_octave_mode_to_note_mode, &nodes::arp_octave_mode_to_note_mode_drums,
		                         &nodes::arp_note_mode_from_octave_mode, &nodes::arp_note_mode_from_octave_mode_drums}) {
			compare("chain/" + chain_name(page), *page);
		}
		expect(wrong).to_equal(std::string{});
	});

	it("marks the select turn as editing the instrument exactly where main's item does", _ {
		Rigged& f = fixture();
		size_t edits = 0;
		for (const spec::TreeEntry& e : entries()) {
			if (e.node->kind == menu::Kind::Submenu) {
				continue;
			}
			install(f.ctx, f.song.song);
			menu::MenuState st{};
			st.path[0] = e.node;
			const bool port = menu::select_turn_edits_instrument(f.ctx, st);
			expect(e.path + " edits " + std::to_string(port ? 1 : 0))
			    .to_equal(e.path + " edits " + table().value(e.path, "edits"));
			edits += port ? 1 : 0;
		}
		expect(edits).to_be_greater_than(size_t(50));
	});

	it("binds MIDI/CC learn to exactly the param main would have learned", _ {
		size_t synth_learners = 0;
		for (const Fx& fx : fixtures()) {
			install(fx.ctx, fx.song);
			const bool synth = std::string{fx.name} == "synth";
			for (const spec::TreeEntry& e : entries()) {
				const std::vector<std::string>* want = table().find(e.path, std::string{"learn@"} + fx.name);
				const std::optional<deluge::project::OwnerParamTarget> got = menu::bindings::learn_target(*e.node, fx.ctx);
				if (synth && got) {
					// Every node that learns on the synth has main's answer frozen beside it.
					synth_learners++;
					expect(e.path + " learn row " + (want != nullptr ? "frozen" : "missing"))
					    .to_equal(e.path + " learn row frozen");
				}
				if (want == nullptr) {
					continue;
				}
				const std::string at = e.path + " " + text_of(e.node->name) + " learns ";
				expect(at + (got ? std::to_string(got->param.id.value) : std::string{"none"}))
				    .to_equal(at + (want->empty() ? std::string{"<no value>"} : want->front()));
				if (got) {
					expect(got->owner).to_equal(menu::bindings::controls_owner(fx.ctx));
				}
			}
		}
		// Every `ParamRef` leaf in the root tree plus the four transposes.
		expect(synth_learners).to_equal(size_t(81));
	});

	it("carries main's occupied-slot counts and range dependence for every leaf", _ {
		// The wide items, by their sources: `mod_fx::Type` draws the whole four-slot page, and both rhythm items
		// take two.
		expect(int(nodes::mod_fx_type.slots)).to_equal(4);
		expect(int(nodes::arp_rhythm.slots)).to_equal(2);
		expect(int(nodes::arp_rhythm_midi.slots)).to_equal(2);
		for (const spec::TreeEntry& e : entries()) {
			if (e.node->kind == menu::Kind::Submenu) {
				continue;
			}
			expect(e.path + " slots " + std::to_string(e.node->slots))
			    .to_equal(e.path + " slots " + table().value(e.path, "slots"));
			expect(e.path + " range_dep " + std::to_string(e.node->range_dependent ? 1 : 0))
			    .to_equal(e.path + " range_dep " + table().value(e.path, "range_dep"));
		}
	});
});

describe tables_2_values("scenes::menu sound-editor tables: values", $ {
	it("reads what main's menu reads, for every node that holds a value", _ {
		size_t compared = 0;
		for (const Fx& fx : fixtures()) {
			for (const spec::TreeEntry& e : entries()) {
				const std::vector<std::string>* want = table().find(e.path, std::string{"read@"} + fx.name);
				if (want == nullptr) {
					continue;
				}
				install(fx.ctx, fx.song);
				const std::string at = fx.name + std::string{" "} + text_of(e.node->name) + " reads ";
				expect(at + std::to_string(menu::bindings::read(*e.node, fx.ctx)))
				    .to_equal(at + (want->empty() ? std::string{"<no value>"} : want->front()));
				compared++;
			}
		}
		expect(compared).to_be_greater_than(size_t(150));
	});

	it("names every enum option exactly as main's list does", _ {
		// A page main keeps only for kit rows (the drums' note mode) shows on a kit row alone: read there.
		size_t compared = 0;
		for (const Fx& fx : fixtures()) {
			for (const spec::TreeEntry& e : entries()) {
				if (e.node->kind != menu::Kind::Enum) {
					continue;
				}
				install(fx.ctx, fx.song);
				const std::vector<std::string>* want = table().find(e.path, std::string{"options@"} + fx.name);
				if (want == nullptr) {
					// The synth clip is asked of every enum but the lane-only ones; the kit row, of those it shows.
					const bool lane_only =
					    e.node->bind.target.shown_on == deluge::ui::scenes::bind::Target::ShownOn::ON_LANE;
					const bool shown_here = (e.parent == nullptr || menu::bindings::relevant(*e.parent, fx.ctx))
					                        && menu::bindings::relevant(*e.node, fx.ctx);
					const bool required = std::string{fx.name} == "synth" ? !lane_only : shown_here;
					expect(e.path + (required ? " has none" : " has options")).to_equal(e.path + " has options");
					continue;
				}
				std::string_view got[menu::kMaxDynamicNames];
				deluge::ui::shell::TextScratch scratch[menu::kMaxDynamicNames];
				const size_t n = menu::bindings::names(*e.node, fx.ctx, got, scratch);
				std::string port;
				for (size_t i = 0; i < n; i++) {
					port += std::string{got[i]} + "|";
				}
				std::string frozen;
				for (const std::string& o : *want) {
					frozen += o + "|";
				}
				expect(fx.name + std::string{" "} + e.path + " " + port)
				    .to_equal(fx.name + std::string{" "} + e.path + " " + frozen);
				compared++;
			}
		}
		expect(compared).to_be_greater_than(size_t(30));
		install(fixture().ctx, fixture().song.song);
	});

	it("shows each node exactly where main's menu shows it, under a synth clip and a kit row", _ {
		// Two contexts, not one: against a synth clip alone every kit/MIDI/gate-drum predicate is false on both sides,
		// so most of the kit-only predicates would agree vacuously. A kit row turns on `is_kit`/`is_kit_row`/
		// `kit_row_not_gate`/`per_finger_bend`/`randomizer_non_kit`.
		size_t ever_true = 0;
		for (const Fx& fx : fixtures()) {
			install(fx.ctx, fx.song);
			for (const spec::TreeEntry& e : entries()) {
				const std::vector<std::string>* want = table().find(e.path, std::string{"rel@"} + fx.name);
				if (want == nullptr) {
					continue; // a page this context never opens (the bend ranges on a kit row)
				}
				const bool got = menu::bindings::relevant(*e.node, fx.ctx);
				const std::string at = std::string{fx.name} + " " + text_of(e.node->name) + " ";
				expect(at + (got ? "shows" : "hides")).to_equal(at + (want->front() == "1" ? "shows" : "hides"));
				ever_true += got ? 1 : 0;
			}
		}
		expect(ever_true).to_be_greater_than(size_t(150));
		install(fixture().ctx, fixture().song.song);
	});

	it("shows the per-finger bend range wherever legacy shows it", _ {
		// `perFingerBendRangeMenu` lives only inside `bendMenu`, i.e. at
		// `navigationDepth == 1`, where `bend_range::PerFinger::isRelevant`'s first arm short-circuits to true --
		// so legacy always draws it on the Bend Range page, including on a synth where
		// `editingKit()` is false. The node must agree, or the page loses one of its two items.
		Rigged& synth = fixture();
		expect(nodes::bend_range_per_finger.relevant).to_be_null();
		expect(menu::bindings::relevant(nodes::bend_range_per_finger, synth.ctx)).to_be_true(); // a melodic track's row
		expect(nodes::bend.relevant).not_().to_be_null();
		if (nodes::bend.relevant != nullptr) {
			expect(nodes::bend.relevant(synth.ctx)).to_be_true();
		}
		// ... while `drumBendRangeMenu`, the same class as a *root child* at depth 0, is kit-only.
		// Its row applies on a kit row's sound drum alone (`kOnSoundDrum`).
		expect(menu::bindings::relevant(nodes::drum_bend_range, synth.ctx)).to_be_false();
		KitRig& kit = kit_fixture();
		const MenuContext kit_ctx = select_drum(kit, kit.drum_a);
		expect(menu::bindings::relevant(nodes::drum_bend_range, kit_ctx)).to_be_true();
		install(synth.ctx, synth.song.song);
	});

	it("gives every node main's own title", _ {
		// ~40 `with_title(...)` transcriptions and every "title == name" decision would otherwise rest on reading alone.
		Rigged& f = fixture();
		install(f.ctx, f.song.song);
		size_t compared = 0;
		for (const spec::TreeEntry& e : entries()) {
			if (t2::has_dynamic_name(*e.node)) {
				continue; // the same classes build their *title* from the name, at render time
			}
			const std::string title = shown(e.node->title, *e.node, f.ctx);
			// A placeholder left unfilled is `sample::Repeat`'s, a `FormattedTitle` main gives no argument, so its title
			// is empty until something formats it.
			if (title.find('*') != std::string::npos) {
				expect(e.node->title_arg).to_equal(uint8_t{0});
				(void)table().find(e.path, "title");
				continue;
			}
			expect(e.path + " " + title).to_equal(e.path + " " + table().value(e.path, "title"));
			compared++;
		}
		expect(compared).to_be_greater_than(size_t(120));
	});

	it("has a registered name for every predicate the tables use", _ {
		for (const spec::TreeEntry& e : entries()) {
			if (e.node->relevant == nullptr) {
				continue;
			}
			expect(menu::relevance::name_of(e.node->relevant)).not_().to_equal(std::string_view{"?"});
		}
		const auto registry = menu::relevance::registry();
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

describe tables_2_validation("scenes::menu sound-editor tables: validation", $ {
	it("validates the sound root clean", _ {
		Rigged& f = fixture();
		const MenuContext contexts[] = {f.ctx};
		const auto issues = menu::validate_tree(
		    nodes::kSoundRootRoots, contexts, nodes::kRangeSpecificNodes, nodes::kHalfPrecisionNodes,
		    menu::ValidateOptions{.skip_shortcut_sweep = true});
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
		// Everything legacy names twice under one parent is told apart: the `FormattedTitle` siblings
		// (the sample recorders, oscillator and FM modulator levels, `env1Menu`..`env4Menu`) by their
		// `title_arg`, and the rest by `validate.cpp`'s `co_visible` -- the randomizer's eleven
		// sound/MIDI pairs, the arpeggiator's rhythm, sequence-length and note-mode twins, and
		// `bendMenu` beside `drumBendRangeMenu` -- because no context shows both.
		expect(duplicates).to_equal(size_t(0));
		expect(others).to_equal(size_t(0));
	});

	it("assigns an id to every node in the sound root tree", _ {
		Rigged& f = fixture();
		const MenuContext contexts[] = {f.ctx};
		(void)menu::validate_tree(
		    nodes::kSoundRootRoots, contexts, {}, {},
		    menu::ValidateOptions{.skip_shortcut_sweep = true});
		for (const spec::TreeEntry& e : entries()) {
			expect(menu::node_id(*e.node)).not_().to_equal(uint16_t(0));
		}
	});
});

describe tables_2_chaining("scenes::menu sound-editor tables: selectButtonPress chaining", $ {
	it("answers what main's item selectButtonPress answers, value by value", _ {
		// The rows are `MenuItem::selectButtonPress()` on the live items, mapped to the page each opens: a
		// wrong condition, a wrong target, or a missing `select_chain` shows up as a mismatch here.
		Rigged& f = fixture();

		struct Case {
			const Node* node;
			int32_t value; ///< written before the press
		};
		const Case cases[] = {
		    // CUSTOM opens a page, every other preset ascends. Both ends of that are probed, not just the interesting one.
		    {&nodes::arp_preset, int32_t(ArpPreset::CUSTOM)},
		    {&nodes::arp_preset, int32_t(ArpPreset::UP)},
		    {&nodes::arp_preset, int32_t(ArpPreset::OFF)},
		    // POLY opens the voice count, everything else falls through to `Selection::selectButtonPress`, which for the
		    // open menu is "go up".
		    {&nodes::polyphony, int32_t(PolyphonyMode::POLY)},
		    {&nodes::polyphony, int32_t(PolyphonyMode::MONO)},
		    {&nodes::polyphony, int32_t(PolyphonyMode::AUTO)},
		    // Unconditional, so two different counts must give the same answer.
		    {&nodes::unison_count, 1},
		    {&nodes::unison_count, 4},
		    // The octave-mode page and its drum twin: also unconditional.
		    {&nodes::arp_octave_mode_to_note_mode, 0},
		    {&nodes::arp_octave_mode_to_note_mode_drums, 0},
		};

		size_t compared = 0;
		for (const Case& c : cases) {
			install(f.ctx, f.song.song);
			menu::bindings::write(*c.node, f.ctx, c.value);
			const Node* got = c.node->select_chain != nullptr ? c.node->select_chain(f.ctx, menu_scene().state())
			                                                  : nullptr;
			expect(chain_name(got)).to_equal(table().value(chain_path(*c.node), "chain@synth/" + std::to_string(c.value)));
			// Every answer the hook actually gives must be in its own `chain_targets` span -- otherwise the page it
			// names is unnumbered, unaddressable by `SceneId::arg` and absent from the dump, silently. `validate_tree`
			// can only check both-set-or-neither (the hook is opaque to a static walk), so the containment half is
			// checked on exercised answers.
			if (got != nullptr) {
				expect(chain_targets_contain(*c.node, got)).to_be_true();
			}
			compared++;
		}
		expect(compared).to_equal(std::size(cases));
	});

	it("opens the drum twin of the octave-mode page when a kit row is being edited", _ {
		// The other half of the preset page's press: a kit row picks the drum twin of the octave-mode page. The synth
		// fixture above can never show it, so it needs the kit fixture.
		KitRig& k = kit_fixture();
		const MenuContext ctx = select_drum(k, k.drum_a);
		menu::bindings::write(nodes::arp_preset, ctx, int32_t(ArpPreset::CUSTOM));

		const Node* got = nodes::arp_preset.select_chain(ctx, menu_scene().state());
		expect(chain_name(got)).to_equal(
		    table().value(chain_path(nodes::arp_preset), "chain@kit row/" + std::to_string(int32_t(ArpPreset::CUSTOM))));
		expect(got).to_equal(&nodes::arp_octave_mode_to_note_mode_drums);

		// And the drum twin chains on to the drum note mode, not the plain one.
		expect(nodes::arp_octave_mode_to_note_mode_drums.select_chain(ctx, menu_scene().state()))
		    .to_equal(&nodes::arp_note_mode_from_octave_mode_drums);
		expect(nodes::arp_octave_mode_to_note_mode.select_chain(ctx, menu_scene().state()))
		    .to_equal(&nodes::arp_note_mode_from_octave_mode);
		install(fixture().ctx, fixture().song.song);
	});

	it("declares the four chain-only arp pages exactly as their legacy base class reads and writes", _ {
		// `arpOctaveModeToNoteModeMenu` and its three relatives add only the chain to the reads and writes of their
		// base classes (`origin/main:src/deluge/gui/menu_item/arpeggiator/octave_mode.h`,
		// `origin/main:src/deluge/gui/menu_item/arpeggiator/note_mode.h` and
		// `origin/main:src/deluge/gui/menu_item/arpeggiator/note_mode_for_drums.h`; only
		// `origin/main:src/deluge/gui/menu_item/arpeggiator/octave_mode.h` overrides `selectButtonPress`), so each must
		// be indistinguishable from the page already in `arpPatternMenu` apart from the chain itself. Comparing against
		// the sibling node -- not against a copy of the field values -- is what makes this fail if one drifts.
		struct Twin {
			const Node* chained;
			const Node* plain;
		};
		const Twin twins[] = {
		    {&nodes::arp_octave_mode_to_note_mode, &nodes::arp_octave_mode},
		    {&nodes::arp_octave_mode_to_note_mode_drums, &nodes::arp_octave_mode},
		    {&nodes::arp_note_mode_from_octave_mode, &nodes::arp_note_mode},
		    {&nodes::arp_note_mode_from_octave_mode_drums, &nodes::arp_note_mode_drums},
		};
		for (const Twin& t : twins) {
			expect(t.chained->name).to_equal(t.plain->name);
			expect(t.chained->title).to_equal(t.plain->title);
			expect(static_cast<int>(t.chained->kind)).to_equal(static_cast<int>(t.plain->kind));
			expect(address_of(t.chained->relevant)).to_equal(address_of(t.plain->relevant));
			expect(address_of(t.chained->bind.get)).to_equal(address_of(t.plain->bind.get));
			expect(address_of(t.chained->bind.set)).to_equal(address_of(t.plain->bind.set));
			expect(static_cast<const void*>(t.chained->bind.names.data()))
			    .to_equal(static_cast<const void*>(t.plain->bind.names.data()));
			// ... and is a page in its own right, not one of the plain ones under another name.
			expect(t.chained).not_().to_equal(t.plain);
		}
	});

	it("numbers every chain target, including the four pages no children span reaches", _ {
		// The reachability rule: `assign_node_ids` follows `chain_targets` as well as `children`,
		// or `SceneId::arg` could not name a chained page and `MenuScene::on_enter` could not
		// restore one. Asserted over the WHOLE tree, since that is the walk the app performs.
		(void)menu::assign_node_ids(nodes::kAllRoots);
		const Node* chained[] = {
		    &nodes::arp_octave_mode_to_note_mode,       &nodes::arp_octave_mode_to_note_mode_drums,
		    &nodes::arp_note_mode_from_octave_mode,     &nodes::arp_note_mode_from_octave_mode_drums,
		    &nodes::unison_spread,                      &nodes::voice_count,
		};
		for (const Node* n : chained) {
			const uint16_t id = menu::node_id(*n);
			expect(id).not_().to_equal(uint16_t(0));
			expect(menu::node_by_id(id)).to_equal(n);
		}
		// The four arp pages are reachable ONLY through a chain -- no span in the tree names them.
		const Node* chain_only[] = {&nodes::arp_octave_mode_to_note_mode,
		                            &nodes::arp_octave_mode_to_note_mode_drums,
		                            &nodes::arp_note_mode_from_octave_mode,
		                            &nodes::arp_note_mode_from_octave_mode_drums};
		for (const Node* n : chain_only) {
			expect(reachable_by_children(n)).to_be_false();
		}
		// ... while `unison_spread` and `voice_count` are ordinary columns as well as chain targets.
		expect(reachable_by_children(&nodes::unison_spread)).to_be_true();
		expect(reachable_by_children(&nodes::voice_count)).to_be_true();
	});

	it("sets select_chain and chain_targets together, everywhere in the tree", _ {
		// `Flag::ChainTargetsMismatch`: a hook with no targets is a page nothing can number; targets
		// with no hook is a page nothing can open. Checked over `kAllRoots` through the validator
		// itself, so the rule is exercised and not merely present.
		Rigged& f = fixture();
		const MenuContext contexts[] = {f.ctx};
		const auto issues =
		    menu::validate_tree(nodes::kAllRoots, contexts, {}, {},
		                        menu::ValidateOptions{.skip_shortcut_sweep = true});
		size_t mismatches = 0;
		for (const menu::Issue& i : issues) {
			if (i.kind == menu::Issue::Kind::FlagMismatch
			    && i.detail == static_cast<uint16_t>(menu::Flag::ChainTargetsMismatch)) {
				mismatches++;
			}
		}
		expect(mismatches).to_equal(size_t(0));
	});
});

describe tables_2_patch_cables("scenes::menu sound-editor tables: the patch-cable items", $ {
	it("declares the three items with the legacy globals' own names, and none of them a placeholder", _ {
		Rigged& f = fixture();
		struct Case {
			const char* row;
			const Node* node;
			const menu::CustomItem* expected;
		};
		const Case cases[] = {
		    {"cables/patch_cables", &nodes::patch_cables, &nodes::kPatchCablesItem},
		    {"cables/vibrato", &nodes::vibrato, &nodes::kVibratoItem},
		    {"cables/sidechain_volume", &nodes::sidechain_volume, &nodes::kSidechainVolumeItem},
		};
		for (const Case& c : cases) {
			expect(text_of(c.node->name)).to_equal(table().value(c.row, "name"));
			expect(c.node->kind).to_equal(menu::Kind::Custom);
			expect(c.node->custom).to_equal(c.expected);
			expect(c.node->custom->is_placeholder()).to_be_false();
		}
		// The two `Fixed` items differ ONLY in their baked-in param and source, so a swapped pair would be invisible to
		// every check above. This is what makes them distinguishable: each item's own `on_focus` installs its pair, and
		// the two pairs are the ones `origin/main:src/deluge/gui/ui/menus.cpp` constructs the globals with.
		menu::MenuState vib{};
		nodes::kVibratoItem.on_focus(f.ctx, vib);
		expect(int(vib.walk.patching_param)).to_equal(int(deluge::modulation::params::LOCAL_PITCH_ADJUST));
		expect(int(vib.walk.patch_source)).to_equal(int(PatchSource::LFO_GLOBAL_1));
		menu::MenuState duck{};
		nodes::kSidechainVolumeItem.on_focus(f.ctx, duck);
		expect(int(duck.walk.patching_param)).to_equal(int(deluge::modulation::params::GLOBAL_VOLUME_POST_REVERB_SEND));
		expect(int(duck.walk.patch_source)).to_equal(int(PatchSource::SIDECHAIN));
	});

	it("chains exactly where the legacy items' own selectButtonPress chains", _ {
		// The port answers from a `MenuState` this example fills in itself, and the row is what main's items answered
		// from the globals they keep for the same choice.
		Rigged& f = fixture();
		install(f.ctx, f.song.song);

		menu::MenuState st{};
		st.walk.patching_param = deluge::modulation::params::LOCAL_LPF_FREQ;
		st.walk.patch_source = PatchSource::ENVELOPE_1;
		st.walk.patch_range_source = PatchSource::LFO_GLOBAL_1;

		struct Case {
			const char* row;
			const Node* node;
		};
		const Case cases[] = {
		    {"cables/source_selection_regular", &nodes::source_selection_regular},
		    {"cables/patch_cable_strength_regular", &nodes::patch_cable_strength_regular},
		    {"cables/source_selection_range", &nodes::source_selection_range},
		    {"cables/patch_cable_strength_range", &nodes::patch_cable_strength_range},
		    // The two `Fixed` items are `PatchCableStrength`s, so they chain the same way.
		    {"cables/vibrato", &nodes::vibrato},
		    {"cables/sidechain_volume", &nodes::sidechain_volume},
		};
		size_t compared = 0;
		for (const Case& c : cases) {
			const Node* got = c.node->select_chain != nullptr ? c.node->select_chain(f.ctx, st) : nullptr;
			expect(chain_name(got)).to_equal(table().value(c.row, "chain"));
			expect(got).not_().to_be_null();
			if (got != nullptr) {
				expect(chain_targets_contain(*c.node, got)).to_be_true();
			}
			compared++;
		}
		expect(compared).to_equal(std::size(cases));
		// The 53 `PatchedParam` nodes all answer the regular source picker, which is what a `PatchedParam`'s non-shift
		// press answers; its shift arm deletes the param's automation, which the interpreter does for every automation
		// item (see "deletes automation on shift+select ..." below). One representative item: `lpf_freq`.
		expect(chain_name(nodes::lpf_freq.select_chain(f.ctx, st))).to_equal(table().value("cables/lpf_freq", "chain"));
		expect(table().value("cables/lpf_freq", "chain")).to_equal(std::string{"source_selection_regular"});
	});

	it("chains every PATCHED node, and only those", _ {
		// The affected set, measured by `dynamic_cast<PatchedParam*>` over a lockstep walk, is
		// **exactly** the nodes with `bind.param.kind == PATCHED`, 53 of them.
		// That equality is what `with_patch_chain` is applied on, so it is what is pinned here:
		// every PATCHED node chains to `source_selection_regular`, and no node that chains there is
		// anything else.
		size_t patched = 0;
		size_t chained_to_picker = 0;
		std::vector<const Node*> stack(nodes::kAllRoots.begin(), nodes::kAllRoots.end());
		std::set<const Node*> seen;
		while (!stack.empty()) {
			const Node* n = stack.back();
			stack.pop_back();
			if (n == nullptr || !seen.insert(n).second) {
				continue;
			}
			for (const Node* c : n->children) {
				stack.push_back(c);
			}
			for (const Node* t : n->chain_targets) {
				stack.push_back(t);
			}
			const bool is_patched = n->bind.param.kind == deluge::modulation::params::Kind::PATCHED;
			const bool to_picker = chain_targets_contain(*n, &nodes::source_selection_regular);
			if (is_patched) {
				patched++;
			}
			if (to_picker) {
				chained_to_picker++;
			}
			// Named rather than counted, so a failure says which node lost (or gained) its chain.
			expect(text_of(n->name) + (is_patched ? " PATCHED" : " -")).to_equal(text_of(n->name)
			                                                                    + (to_picker ? " PATCHED" : " -"));
		}
		expect(patched).to_equal(size_t(53));
		expect(chained_to_picker).to_equal(size_t(53));
	});

	it("reads and writes a patch cable's strength where the legacy page reads and writes it", _ {
		Rigged& f = fixture();
		install(f.ctx, f.song.song);

		// A cable from ENVELOPE_1 to LPF frequency, created by the port's own write path.
		deluge::synth::CableTarget learn = deluge::synth::CableTarget::depth_of(deluge::modulation::params::LOCAL_LPF_FREQ, PatchSource::ENVELOPE_1);
		expect(seed_cable(f.ctx, learn, 1234)).to_be_true();

		// The port reads it through a `MenuState` filled in independently of main's globals; the row is what main's page
		// reads off the same cable.
		menu::MenuState st{};
		st.walk.patching_param = deluge::modulation::params::LOCAL_LPF_FREQ;
		st.walk.patch_source = PatchSource::ENVELOPE_1;
		nodes::kPatchCableStrengthRegularItem.on_focus(f.ctx, st);
		expect(st.edit_int).to_equal(spec::int_of(table().find("strength/seed", "read")).value_or(-1));
		expect(st.edit_int).to_equal(int32_t(1234));
		expect(int(st.decimal_pos)).to_equal(2); // `getDefaultEditPos()`

		// The learn descriptor is the strength page's own learning thing: the cable's depth.
		expect(std::to_string(deluge::modulation::knobWord(deluge::modulation::knobTargetOf(
		                          nodes::kPatchCableStrengthRegularItem.learn_descriptor(f.ctx, st)))
		                          .word))
		    .to_equal(table().value("strength/seed", "learn"));

		// And the write, from an independent sentinel: the cable is moved somewhere that is NOT the value about to be
		// written, the port's `commit` writes, and the cable is read back. A `commit` that did nothing would leave the
		// sentinel behind.
		expect(seed_cable(f.ctx, learn, -2500)).to_be_true(); // sentinel
		nodes::kPatchCableStrengthRegularItem.on_focus(f.ctx, st);
		expect(st.edit_int).to_equal(spec::int_of(table().find("strength/sentinel", "read")).value_or(-1));

		st.edit_int = 4321;
		nodes::kPatchCableStrengthRegularItem.commit(f.ctx, st);
		menu::MenuState reread{};
		reread.walk.patching_param = deluge::modulation::params::LOCAL_LPF_FREQ;
		reread.walk.patch_source = PatchSource::ENVELOPE_1;
		nodes::kPatchCableStrengthRegularItem.on_focus(f.ctx, reread);
		expect(reread.edit_int).to_equal(spec::int_of(table().find("strength/commit", "write/4321")).value_or(-1));
	});

	it("gives a cable it creates the polarity the page was showing, not the source's default", _ {
		// The half of main's write path that is not the value: `getModelStack(..., allowCreation = true)` calls
		// `setPatchCablePolarity(polarity_in_the_ui_)` on the `!patch_cable_exists_` arm
		// (`origin/main:src/deluge/gui/menu_item/patch_cable_strength.cpp`), so a polarity the user chose *before* the
		// cable existed survives its creation.
		//
		// `PatchCableSet::getPatchCableIndex` (`modulation/patch/patch_cable_set.cpp`) already
		// gives a new cable `setDefaultPolarity()`, so the *default* needs no help -- what would be lost is a chosen
		// one. Hence the sentinel below: the page is put in the polarity that is NOT the source's default, and the
		// created cable must come out in it.
		Rigged& f = fixture();
		install(f.ctx, f.song.song);

		constexpr PatchSource kSource = PatchSource::ENVELOPE_3;
		deluge::synth::CableTarget learn = deluge::synth::CableTarget::depth_of(deluge::modulation::params::LOCAL_HPF_RESONANCE, kSource);
		PatchCableSet* set = spec::menu_engine(f.ctx, *f.song.song).params->getPatchCableSetAllowJibberish();
		expect(set).not_().to_be_null();
		expect(set->getPatchCableIndex(kSource, [] {
			deluge::synth::CableTarget d = deluge::synth::CableTarget::param(deluge::modulation::params::LOCAL_HPF_RESONANCE);
			return d;
		}())).to_equal(kNoSelection); // no such cable yet

		// The page, in the polarity opposite to what a fresh cable would be given.
		const Polarity fresh = PatchCable::getDefaultPolarity(kSource);
		const Polarity chosen = fresh == Polarity::BIPOLAR ? Polarity::UNIPOLAR : Polarity::BIPOLAR;
		expect(deluge::instruments::classic::has_polarity(kSource)).to_be_true();

		menu::MenuState st{};
		st.walk.patching_param = deluge::modulation::params::LOCAL_HPF_RESONANCE;
		st.walk.patch_source = kSource;
		st.walk.patch_polarity = chosen;
		st.edit_int = 2000;
		nodes::kPatchCableStrengthRegularItem.commit(f.ctx, st);

		deluge::synth::CableTarget destination = deluge::synth::CableTarget::param(deluge::modulation::params::LOCAL_HPF_RESONANCE);
		const uint8_t created = set->getPatchCableIndex(kSource, destination);
		expect(created).not_().to_equal(kNoSelection); // the write created it
		expect(static_cast<int>(set->patchCables[created].polarity)).to_equal(static_cast<int>(chosen));
		// The cable's record, which a save writes, holds the same polarity.
		const std::optional<size_t> record = set->records().find(kSource, destination);
		expect(record.has_value()).to_be_true();
		expect(static_cast<int>(set->records()[*record].polarity)).to_equal(static_cast<int>(chosen));
		// ... and that really is a different answer from the one creation alone would have given.
		expect(static_cast<int>(chosen)).not_().to_equal(static_cast<int>(fresh));

		// `on_focus` reads it back off the cable, which is `PatchCableStrength::beginSession`'s own arm
		// (`origin/main:src/deluge/gui/menu_item/patch_cable_strength.cpp`).
		menu::MenuState reopened{};
		reopened.walk.patching_param = deluge::modulation::params::LOCAL_HPF_RESONANCE;
		reopened.walk.patch_source = kSource;
		nodes::kPatchCableStrengthRegularItem.on_focus(f.ctx, reopened);
		expect(static_cast<int>(reopened.walk.patch_polarity)).to_equal(static_cast<int>(chosen));
	});

	it("deletes automation on shift+select exactly where the legacy item is an Automation", _ {
		size_t automation = 0;
		for (const spec::TreeEntry& e : entries()) {
			const bool legacy = table().find(e.path, "automation") != nullptr;
			const bool port = menu::is_automation_item(*e.node);
			if (legacy != port) {
				expect(e.path + (port ? " deletes" : " keeps")).to_equal(e.path + " as legacy");
			}
			automation += legacy ? 1 : 0;
		}
		expect(automation).to_be_greater_than(size_t(50));
	});

	it("names its own cable's address as the strength page's automation", _ {
		Rigged& f = fixture();
		install(f.ctx, f.song.song);
		menu::MenuState st{};
		st.walk.patching_param = deluge::modulation::params::LOCAL_LPF_FREQ;
		st.walk.patch_source = PatchSource::ENVELOPE_1;
		deluge::synth::CableTarget destination = deluge::synth::CableTarget::param(deluge::modulation::params::LOCAL_LPF_FREQ);
		const auto address = nodes::kPatchCableStrengthRegularItem.automation_address(f.ctx, st);
		expect(nodes::kPatchCableStrengthRegularItem.is_automation_item()).to_be_true();
		expect(address.has_value()).to_be_true();
		if (address.has_value()) {
			expect(address->id).to_equal(deluge::bridge::cable_depth(PatchSource::ENVELOPE_1, destination));
		}
		expect(nodes::kPatchCablesItem.is_automation_item()).to_be_false();
	});

	it("gives a cable it creates from a horizontal page's column the source's default polarity", _ {
		// `getModelStack(..., allowCreation = true)` applies the shown polarity only `!isInHorizontalMenu()`
		// (`origin/main:src/deluge/gui/menu_item/patch_cable_strength.cpp`): a column's new cable takes its source's
		// default.
		Rigged& f = fixture();
		install(f.ctx, f.song.song);
		constexpr PatchSource kSource = PatchSource::LFO_GLOBAL_1;
		const Polarity fresh = PatchCable::getDefaultPolarity(kSource);
		menu::MenuState st{};
		st.depth = 0;
		st.path[0] = &nodes::sidechain; // a horizontal page, its columns custom items among them
		st.walk.patching_param = deluge::modulation::params::LOCAL_HPF_MORPH;
		st.walk.patch_source = kSource;
		st.walk.patch_polarity = fresh == Polarity::BIPOLAR ? Polarity::UNIPOLAR : Polarity::BIPOLAR;
		st.edit_int = 1200;
		nodes::kPatchCableStrengthRegularItem.commit(f.ctx, st);
		PatchCableSet* set = spec::menu_engine(f.ctx, *f.song.song).params->getPatchCableSetAllowJibberish();
		deluge::synth::CableTarget destination = deluge::synth::CableTarget::param(deluge::modulation::params::LOCAL_HPF_MORPH);
		const uint8_t created = set->getPatchCableIndex(kSource, destination);
		expect(created).not_().to_equal(kNoSelection);
		expect(static_cast<int>(set->patchCables[created].polarity)).to_equal(static_cast<int>(fresh));
	});

	it("flips an existing cable's polarity in the engine and the record", _ {
		Rigged& f = fixture();
		install(f.ctx, f.song.song);
		constexpr PatchSource kSource = PatchSource::LFO_GLOBAL_1;
		deluge::synth::CableTarget learn = deluge::synth::CableTarget::depth_of(deluge::modulation::params::LOCAL_OSC_A_PITCH_ADJUST, kSource);
		expect(seed_cable(f.ctx, learn, 1500)).to_be_true();
		PatchCableSet* set = spec::menu_engine(f.ctx, *f.song.song).params->getPatchCableSetAllowJibberish();
		deluge::synth::CableTarget destination = deluge::synth::CableTarget::param(deluge::modulation::params::LOCAL_OSC_A_PITCH_ADJUST);
		const uint8_t index = set->getPatchCableIndex(kSource, destination);
		expect(index).not_().to_equal(kNoSelection);
		const Polarity flipped = set->patchCables[index].polarity == Polarity::BIPOLAR ? Polarity::UNIPOLAR
		                                                                              : Polarity::BIPOLAR;

		menu::MenuState st{};
		st.walk.patching_param = deluge::modulation::params::LOCAL_OSC_A_PITCH_ADJUST;
		st.walk.patch_source = kSource;
		nodes::kPatchCableStrengthRegularItem.set_polarity(f.ctx, st, flipped);
		expect(static_cast<int>(st.walk.patch_polarity)).to_equal(static_cast<int>(flipped));
		const uint8_t after = set->getPatchCableIndex(kSource, destination);
		expect(static_cast<int>(set->patchCables[after].polarity)).to_equal(static_cast<int>(flipped));
		expect(static_cast<int>(set->records()[after].polarity)).to_equal(static_cast<int>(flipped));
		expect(set->patchCables[after].param.getCurrentValue()).not_().to_equal(0); // the strength stays
	});

	it("refuses a strength write on a full cable set, creating nothing", _ {
		spec::SongFixture song = spec::make_synth_song();
		menu::MenuContext ctx =
		    spec::ctx_of(*song.song, deluge::gui::menuViewOf(song.clip), 0, false, false, false, /*kit_global_fx=*/false);
		install(ctx, song.song);
		PatchCableSet* set = spec::menu_engine(ctx, *song.song).params->getPatchCableSetAllowJibberish();
		for (const PatchSource source : {PatchSource::RANDOM, PatchSource::VELOCITY, PatchSource::NOTE}) {
			for (int32_t p = 0; set->numPatchCables < kMaxNumPatchCables && p < deluge::modulation::params::kNumParams;
			     ++p) {
				deluge::synth::CableTarget learn = deluge::synth::CableTarget::depth_of(p, source);
				(void)seed_cable(ctx, learn, 100);
			}
		}
		expect(set->numPatchCables).to_equal(static_cast<uint8_t>(kMaxNumPatchCables));
		menu::MenuState st{};
		st.walk.patching_param = deluge::modulation::params::LOCAL_LPF_FREQ;
		st.walk.patch_source = PatchSource::AFTERTOUCH;
		st.edit_int = 2000;
		nodes::kPatchCableStrengthRegularItem.commit(ctx, st);
		deluge::synth::CableTarget destination = deluge::synth::CableTarget::param(deluge::modulation::params::LOCAL_LPF_FREQ);
		expect(set->getPatchCableIndex(PatchSource::AFTERTOUCH, destination)).to_equal(kNoSelection);
		expect(set->numPatchCables).to_equal(static_cast<uint8_t>(kMaxNumPatchCables));
		install(fixture().ctx, fixture().song.song);
	});

	it("opens the strength page the selected cable needs, and installs that cable's own sources", _ {
		Rigged& f = fixture();
		install(f.ctx, f.song.song);
		PatchCableSet* set = spec::menu_engine(f.ctx, *f.song.song).params->getPatchCableSetAllowJibberish();
		expect(set).not_().to_be_null();

		// Two cables: one to a bare param, one to another cable's depth. The page picks a different strength page for
		// each.
		deluge::synth::CableTarget plain = deluge::synth::CableTarget::depth_of(deluge::modulation::params::LOCAL_LPF_FREQ, PatchSource::ENVELOPE_1);
		expect(seed_cable(f.ctx, plain, 1000)).to_be_true();
		const deluge::synth::CableTarget ranged = deluge::synth::CableTarget::depth_of_depth(
		    deluge::modulation::params::LOCAL_LPF_FREQ, PatchSource::ENVELOPE_1, PatchSource::LFO_GLOBAL_1);
		expect(seed_cable(f.ctx, ranged, 900)).to_be_true();
		to_be_at_least(expect(set->numPatchCables), 2);

		size_t compared = 0;
		for (int32_t i = 0; i < set->numPatchCables; i++) {
			const std::string row = "cables/row/" + std::to_string(i);
			menu::MenuState st{};
			st.depth = 0;
			st.path[0] = &nodes::patch_cables;
			st.list[0].cursor = static_cast<int16_t>(i);
			const Node* got = nodes::patch_cables.select_chain(f.ctx, st);
			expect(chain_name(got)).to_equal(table().value(row, "page"));
			expect(got).not_().to_be_null();
			expect(chain_targets_contain(nodes::patch_cables, got)).to_be_true();

			// And the state main's own press installed on the way out -- which the port installs on the way *in*, from
			// the strength page's `on_focus`: the param, the source, and (for a depth-of-depth cable) the range source.
			menu::MenuState arrived = st;
			arrived.depth = 1;
			arrived.path[1] = got;
			const auto* item = static_cast<const nodes::PatchCableStrengthItem*>(got->custom);
			item->on_focus(f.ctx, arrived);
			const std::vector<std::string>* sources = table().find(row, "sources");
			expect(sources).not_().to_be_null();
			if (sources != nullptr) {
				expect(std::to_string(int(arrived.walk.patching_param))).to_equal(sources->at(0));
				expect(std::to_string(int(arrived.walk.patch_source))).to_equal(sources->at(1));
				if (!set->patchCables[i].destination.is_param()) {
					expect(std::to_string(int(arrived.walk.patch_range_source))).to_equal(sources->at(2));
				}
				else {
					expect(sources->size()).to_equal(size_t(2));
				}
			}
			compared++;
		}
		to_be_at_least(expect(compared), size_t(2));
		// The rows are exactly the stored cables: the next row is the end of the list.
		expect(table().find("cables/row/" + std::to_string(set->numPatchCables), "page")).to_be_null();

		// With no cable under the cursor at all, the page falls through to `MenuItem::selectButtonPress` -- "there were
		// no items ... take them back there" -- which is `nullptr`, i.e. ascend.
		menu::MenuState empty{};
		empty.list[0].cursor = static_cast<int16_t>(set->numPatchCables);
		expect(chain_name(nodes::patch_cables.select_chain(f.ctx, empty))).to_equal(table().value("cables/row/end", "page"));
	});
});

describe tables_2_pixels("scenes::menu sound-editor tables: pixels", $ {
	it("renders the sound-editor root list", _ {
		Rigged& f = fixture();
		deluge::base::notify::dirty().drain();
		Rig r;
		r.open(nodes::sound_root, f.ctx);
		r.shell.tick(0);
		expect_golden(self, "ui/menu/menu_sound_root_48.pbm", r.fake.pbm(0));
	});

	it("renders the sound-editor root list on a 64-row panel", _ {
		Rigged& f = fixture();
		deluge::base::notify::dirty().drain();
		Rig r{board_64(), profile_64()};
		r.open(nodes::sound_root, f.ctx);
		r.shell.tick(0);
		expect_golden(self, "ui/menu/menu_sound_root_64.pbm", r.fake.pbm(0));
	});

	it("renders the mod-matrix list and the cable-strength page, on both shipped panel geometries", _ {
		// Both geometries for both pages, because neither screen has a golden anywhere else and the
		// row arithmetic behind them (`menu_rect`, `title`, `decimal_value`) is panel-dependent --
		// a 64-row bug once shipped for exactly this reason.
		Rigged& f = fixture();
		install(f.ctx, f.song.song);
		deluge::synth::CableTarget plain = deluge::synth::CableTarget::depth_of(deluge::modulation::params::LOCAL_LPF_FREQ, PatchSource::ENVELOPE_1);
		expect(seed_cable(f.ctx, plain, 1234)).to_be_true();

		deluge::base::notify::dirty().drain();
		{
			Rig r;
			r.open(nodes::patch_cables, f.ctx);
			r.shell.tick(0);
			expect_golden(self, "ui/menu/menu_patch_cables_48.pbm", r.fake.pbm(0));
		}
		{
			Rig r{board_64(), profile_64()};
			r.open(nodes::patch_cables, f.ctx);
			r.shell.tick(0);
			expect_golden(self, "ui/menu/menu_patch_cables_64.pbm", r.fake.pbm(0));
		}
		// The strength page, reached the way a user reaches it: a select press on the cable list.
		{
			Rig r;
			r.open(nodes::patch_cables, f.ctx);
			r.shell.tick(0);
			r.enter(20);
			r.shell.tick(30);
			r.shell.tick(40);
			expect(menu_scene().focused()).to_equal(&nodes::patch_cable_strength_regular);
			expect_golden(self, "ui/menu/menu_cable_strength_48.pbm", r.fake.pbm(0));
		}
		{
			Rig r{board_64(), profile_64()};
			r.open(nodes::patch_cables, f.ctx);
			r.shell.tick(0);
			r.enter(20);
			r.shell.tick(30);
			r.shell.tick(40);
			expect_golden(self, "ui/menu/menu_cable_strength_64.pbm", r.fake.pbm(0));
		}
	});

	it("renders the three-stage routing diagram, reached the way a user reaches it, on both panels", _ {
		// `vibrato` -> `source_selection_range` -> `patch_cable_strength_range`: the page whose
		// diagram has three stages, because the cable it edits modulates another cable's depth.
		// Its own widget is pinned byte-for-byte against legacy in `tests/unit/ui/widgets/routing_spec.cpp`;
		// this records what the whole page composes to.
		Rigged& f = fixture();
		install(f.ctx, f.song.song);
		deluge::base::notify::dirty().drain();
		for (bool tall : {false, true}) {
			Rig r = tall ? Rig{board_64(), profile_64()} : Rig{};
			r.open(nodes::vibrato, f.ctx);
			r.shell.tick(0);
			r.enter(20);
			r.shell.tick(30);
			r.shell.tick(40);
			r.enter(50);
			r.shell.tick(60);
			r.shell.tick(70);
			expect(menu_scene().focused()).to_equal(&nodes::patch_cable_strength_range);
			expect_golden(self, tall ? "ui/menu/menu_cable_strength_range_64.pbm"
			                         : "ui/menu/menu_cable_strength_range_48.pbm",
			              r.fake.pbm(0));
		}
	});

	it("renders the source picker the 53 PatchedParam nodes chain to, on both panels", _ {
		Rigged& f = fixture();
		install(f.ctx, f.song.song);
		deluge::base::notify::dirty().drain();
		{
			Rig r;
			r.open(nodes::lpf_freq, f.ctx);
			r.shell.tick(0);
			r.enter(20);
			r.shell.tick(30);
			r.shell.tick(40);
			expect(menu_scene().focused()).to_equal(&nodes::source_selection_regular);
			expect_golden(self, "ui/menu/menu_source_selection_48.pbm", r.fake.pbm(0));
		}
		{
			Rig r{board_64(), profile_64()};
			r.open(nodes::lpf_freq, f.ctx);
			r.shell.tick(0);
			r.enter(20);
			r.shell.tick(30);
			r.shell.tick(40);
			expect_golden(self, "ui/menu/menu_source_selection_64.pbm", r.fake.pbm(0));
		}
	});
});
describe tables_2_writes("scenes::menu sound-editor tables: writes", $ {
	it("leaves the model where main's menu leaves it, for every set hook", _ {
		// Every `set` hook the rows name is driven for the values they name: each starts from a sentinel that is
		// not the value, so a hook that stores nothing shows; the port's read after its own write is compared with the
		// frozen read after main's write.
		//
		// `ParamRef` nodes are deliberately not here: main's `UnpatchedParam::writeCurrentValue` resolves its model
		// stack through the editor's, which with no root UI open writes the *song's* param collection where the port
		// writes the clip's. That is a property of the harness, not of the tables, and `bindings_spec` already covers
		// the param write path end to end. The next example covers what is specific to the param nodes of these tables
		// instead. The ratchet is the set of (fixture, path) pairs with a `set` hook, shown in that fixture, not left
		// out for needing a booted UI, and wide enough to start from a sentinel: it must be exactly the set the rows
		// cover.
		std::set<std::string> covered;
		std::set<std::string> hooked;
		for (const Fx& fx : fixtures()) {
			for (const spec::TreeEntry& e : entries()) {
				install(fx.ctx, fx.song);
				if (e.node->bind.set != nullptr && !t2::write_needs_booted_ui(*e.node) && e.node->kind != menu::Kind::Range
				    && menu::bindings::relevant(*e.node, fx.ctx)) {
					const menu::IntRange wide = menu::bindings::range(*e.node, fx.ctx);
					if (wide.min != wide.max) {
						hooked.insert(std::string{fx.name} + " " + e.path);
					}
				}
				const std::vector<const spec::OracleRow*> rows = table().columns(e.path, std::string{"write@"} + fx.name + "/");
				if (rows.empty()) {
					continue;
				}
				const menu::IntRange r = menu::bindings::range(*e.node, fx.ctx);
				std::string probed;
				std::string expected;
				for (const int32_t v : spec::distinct_probe_values(r)) {
					if (spec::sentinel_for(r, v)) {
						expected += std::to_string(v) + ",";
					}
				}
				const std::string prefix = std::string{"write@"} + fx.name + "/";
				for (const spec::OracleRow* row : rows) {
					const int32_t v = std::stoi(row->column.substr(prefix.size()));
					menu::bindings::write(*e.node, fx.ctx, *spec::sentinel_for(r, v));
					menu::bindings::write(*e.node, fx.ctx, v);
					const std::string at = std::string{fx.name} + " " + e.path + " write " + std::to_string(v) + " reads ";
					expect(at + std::to_string(menu::bindings::read(*e.node, fx.ctx))).to_equal(at + row->values.at(0));
					probed += std::to_string(v) + ",";
				}
				covered.insert(std::string{fx.name} + " " + e.path);
				expect(e.path + " probed " + probed).to_equal(e.path + " probed " + expected);
			}
		}
		// The other nodes are bound to targets, which the binding library's specs drive.
		expect(covered).to_equal(hooked);
		expect(covered.empty()).to_be_false();
		install(fixture().ctx, fixture().song.song);
	});

	it("round-trips every param node, and raises the reverb flag on exactly the one that should", _ {
		// The param half of the coverage above, port-only for the reason that example explains, plus one more:
		// `sidechain::Shape` is an `unpatched_param::UpdatingReverbParams`
		// (`origin/main:src/deluge/gui/menu_item/sidechain/shape.h`), so its write must also set
		// `AudioEngine::mustUpdateReverbParamsBeforeNextRender` (`writeCurrentValue` in
		// `origin/main:src/deluge/gui/menu_item/unpatched_param/updating_reverb_params.h`).
		Rigged& f = fixture();
		install(f.ctx, f.song.song);
		size_t exercised = 0;
		// The distinct set of param nodes round-tripped, sized from the walk rather than from a hand-written count.
		std::set<const Node*> covered;
		std::set<const Node*> param_nodes;
		// Distinct nodes are what the coverage claim is about; `pair_probes` counts the *tree
		// positions* they were driven from, since a node reachable from two parents appears twice.
		size_t pair_probes = 0;
		for (const spec::TreeEntry& p : entries()) {
			// A setting target's `ParamRef` is learn-only: its value is its setting's, which descriptor_coherence_spec and
			// sound_settings_spec drive. A param target's value is its param's.
			const bool setting_target = p.node->bind.target.valid()
			                            && p.node->bind.target.kind != deluge::ui::scenes::bind::Target::Kind::PARAM;
			if (!p.node->bind.param.valid() || setting_target || p.node->bind.set != nullptr
			    || p.node->kind == menu::Kind::Custom) {
				continue;
			}
			param_nodes.insert(p.node);
			pair_probes++;
			const menu::IntRange r = menu::bindings::range(*p.node, f.ctx);
			for (int32_t v : spec::probe_values(r)) {
				AudioEngine::mustUpdateReverbParamsBeforeNextRender = false;
				menu::bindings::write(*p.node, f.ctx, v);
				expect(menu::bindings::read(*p.node, f.ctx)).to_equal(v);
				expect(AudioEngine::mustUpdateReverbParamsBeforeNextRender).to_equal(raises_reverb(*p.node));
				exercised++;
				covered.insert(p.node);
			}
		}
		expect(covered.size()).to_equal(param_nodes.size());
		for (const Node* n : param_nodes) {
			expect(covered.contains(n)).to_be_true();
		}
		expect(exercised).to_equal(size_t(3) * pair_probes);
		// Exactly one node in this tree edits such a param; the other `UpdatingReverbParams` item,
		// `sidechain::GlobalVolume` (`origin/main:src/deluge/gui/menu_item/sidechain/volume_global.h`), belongs to
		// `tables_3_spec.cpp`'s roots.
		std::set<const Node*> flagged;
		for (const spec::TreeEntry& p : entries()) {
			if (raises_reverb(*p.node)) {
				flagged.insert(p.node);
			}
		}
		expect(flagged.size()).to_equal(size_t(1));
		expect(flagged.contains(&nodes::sidechain_shape)).to_be_true();
	});

	it("sweeps both drums of a kit exactly as legacy's affect-entire loop does", _ {
		// With AFFECT-ENTIRE held on a kit row, main's ~20 hand-written sweeps write every drum in the kit; the port's
		// the generic fan-out and the arp items' kit sweep (`SetKitLaneSetting` in `shell_host/edit_sink.cpp`, every lane's
		// arp) must write the same set. Observing "the same set" generically is what the two-drum fixture buys: select
		// each drum in turn and read the node back.
		//
		// Each sweep starts from a **sentinel seeded on both drums through a single-target write with the hold off**, so
		// a no-op port sweep leaves the drums at the sentinel while the rows are at `v`.
		KitRig& k = kit_fixture();
		const auto observe = [&](const Node& node) {
			std::array<int32_t, 2> out{};
			out[0] = menu::bindings::read(node, select_drum(k, k.drum_a));
			out[1] = menu::bindings::read(node, select_drum(k, k.drum_b));
			return out;
		};
		// The ratchet: every node with a `set` hook, not left out for needing a booted UI, and wide enough to seed from a
		// sentinel is swept, so the rows cover exactly that set.
		std::set<std::string> swept;
		std::set<std::string> sweepable;
		for (const spec::TreeEntry& e : entries()) {
			const std::vector<std::string>* drum_a = table().find("sweep/" + e.path, "drum_a");
			const std::vector<std::string>* drum_b = table().find("sweep/" + e.path, "drum_b");
			if (e.node->bind.set != nullptr && !t2::write_needs_booted_ui(*e.node)) {
				const menu::IntRange wide = menu::bindings::range(*e.node, select_drum(k, k.drum_a));
				if (wide.min != wide.max) {
					sweepable.insert(e.path);
				}
			}
			if (drum_a == nullptr || drum_b == nullptr) {
				continue;
			}
			const menu::IntRange r = menu::bindings::range(*e.node, select_drum(k, k.drum_a));
			const int32_t v = r.max;
			const std::optional<int32_t> sentinel = spec::sentinel_for(r, v);
			expect(sentinel.has_value()).to_be_true();
			if (!sentinel.has_value()) {
				continue;
			}
			menu::bindings::write(*e.node, select_drum(k, k.drum_a), *sentinel);
			menu::bindings::write(*e.node, select_drum(k, k.drum_b), *sentinel);
			const MenuContext held = select_drum(k, k.drum_a, /*affect_entire_held=*/true);
			menu::bindings::write(*e.node, held, v);
			const std::array<int32_t, 2> after = observe(*e.node);
			expect(e.path + " drum a " + std::to_string(after[0])).to_equal(e.path + " drum a " + drum_a->front());
			expect(e.path + " drum b " + std::to_string(after[1])).to_equal(e.path + " drum b " + drum_b->front());
			swept.insert(e.path);
		}
		expect(swept).to_equal(sweepable);
		expect(swept.empty()).to_be_false();
		install(fixture().ctx, fixture().song.song);
	});

	it("asks each drum its own allowNoteTails when sweeping the kit-arp toggle", _ {
		// Main's sweep (`IncludeInKitArp::writeCurrentValue` in
		// `origin/main:src/deluge/gui/menu_item/arpeggiator/include_in_kit_arp.h`) builds a model stack per `SoundDrum`
		// and skips the ones that cannot hold a note tail; the port must too, or the two end up with different
		// `includeInKitArp` across the kit. Made observable by giving drum A an arpeggiator -- `Sound::allowNoteTails`
		// returns true outright when the arp is on (`sound.cpp`) -- while drum B keeps its sample setup's answer.
		KitRig& k = kit_fixture();
		k.drum_a->arpSettings.mode = ArpMode::ARP; // `Sound::allowNoteTails` (`sound.cpp`) -- arp on, tails allowed
		k.drum_b->arpSettings.mode = ArpMode::OFF;
		k.drum_a->arpSettings.includeInKitArp = false;
		k.drum_b->arpSettings.includeInKitArp = false;

		// ... and make drum B refuse: `Sound::envelopeHasSustainEver` is
		// false when envelope 0's sustain is at the bottom of its range and its decay does not
		// exceed its release, and `allowNoteTails` returns false outright when it is.
		NoteRow* row_b = k.song.clip->getNoteRowForDrum(k.drum_b);
		expect(row_b).not_().to_be_null();
		if (row_b != nullptr) {
			PatchedParamSet* params_b = row_b->paramManager.getPatchedParamSet();
			namespace mp = deluge::modulation::params;
			params_b->params[mp::LOCAL_ENV_0_SUSTAIN].setPlayed(-2147483648);
			params_b->params[mp::LOCAL_ENV_0_DECAY].setPlayed(-2147483648);
			params_b->params[mp::LOCAL_ENV_0_RELEASE].setPlayed(2147483647);
		}

		const MenuContext ctx_a = select_drum(k, k.drum_a);
		expect(spec::drum_allows_note_tails(k.drum_a)).to_be_true();
		// The whole point: the two drums must disagree, or the sweep test proves nothing.
		expect(spec::drum_allows_note_tails(k.drum_b)).to_be_false();

		const MenuContext held = select_drum(k, k.drum_a, /*affect_entire_held=*/true);
		menu::bindings::write(nodes::arp_include_in_kit_arp, held, 1);

		// Drum A takes the write; drum B, which cannot hold a tail, is left alone -- exactly what
		// `IncludeInKitArp::writeCurrentValue`
		// (`origin/main:src/deluge/gui/menu_item/arpeggiator/include_in_kit_arp.h`) does, and what the context-wide
		// test this replaces did not.
		expect(k.drum_a->arpSettings.includeInKitArp).to_be_true();
		expect(k.drum_b->arpSettings.includeInKitArp).to_be_false();

		k.drum_a->arpSettings.mode = ArpMode::OFF;
		install(fixture().ctx, fixture().song.song);
	});

	it("writes the play direction to the note row on a kit row and to the clip otherwise", _ {
		// `sequence::Direction::writeCurrentValue` (`origin/main:src/deluge/gui/menu_item/sequence/direction.h`) writes
		// the *resolved* note row when there is one and the clip when there is not; `bindings::direction_target` and
		// `direction_where` (`ui/scenes/menu/bindings.cpp`) resolve the kit row's.
		Rigged& f = fixture();
		install(f.ctx, f.song.song);
		const auto want_clip = static_cast<int32_t>(SequenceDirection::REVERSE);
		menu::bindings::write(nodes::sequence_direction, f.ctx, want_clip);
		expect(static_cast<int32_t>(f.song.clip->sequenceDirectionMode)).to_equal(want_clip);

		KitRig& k = kit_fixture();
		const MenuContext kit_ctx = select_drum(k, k.drum_a);
		expect(k.song.clip->affectEntire).to_be_false(); // the precondition of the kit-row arm
		NoteRow* row = k.song.clip->getNoteRowForDrum(k.drum_a);
		expect(row).not_().to_be_null();
		const auto want_row = static_cast<int32_t>(SequenceDirection::PINGPONG);
		const auto clip_before = static_cast<int32_t>(k.song.clip->sequenceDirectionMode);
		menu::bindings::write(nodes::sequence_direction, kit_ctx, want_row);
		if (row != nullptr) {
			expect(static_cast<int32_t>(row->sequenceDirectionMode())).to_equal(want_row);
		}
		expect(static_cast<int32_t>(k.song.clip->sequenceDirectionMode)).to_equal(clip_before);
		install(f.ctx, f.song.song);
	});

	it("drives the port hook of every node the write rows leave out", _ {
		// `t2::kWritesNeedingBootedUi` above stands the **legacy** call down; it must not
		// stand the port's down with it. Each of the four hooks is driven here on its own and its
		// persisted effect asserted -- including the two arpeggiator ones, whose real work
		// (`silenceArp` on a toggle, `updatePresetFromCurrentSettings`,
		// `updateSettingsFromCurrentPreset`, `flagForceArpRestart`, and the kit-wide sweep over
		// every `Drum` rather than every `SoundDrum`) is invisible to `bindings::read`.
		Rigged& f = fixture();
		install(f.ctx, f.song.song);
		size_t covered = 0;

		{ // --- synth_mode --------------------------------------------------------------------
			// Ring mod takes two ring-moddable oscillators (the model refuses it otherwise, as main's menu does), so
			// both sources are saws while the hook is driven.
			const OscType osc_before[2] = {spec::menu_engine(f.ctx, *f.song.song).sound->oscType(0), spec::menu_engine(f.ctx, *f.song.song).sound->oscType(1)};
			deluge::test::setOscType(*spec::menu_engine(f.ctx, *f.song.song).sound, 0, OscType::SAW);
			deluge::test::setOscType(*spec::menu_engine(f.ctx, *f.song.song).sound, 1, OscType::SAW);
			const SynthMode before = spec::menu_engine(f.ctx, *f.song.song).sound->getSynthMode();
			menu::bindings::write(nodes::synth_mode, f.ctx, static_cast<int32_t>(SynthMode::RINGMOD));
			expect(spec::menu_engine(f.ctx, *f.song.song).sound->getSynthMode()).to_equal(SynthMode::RINGMOD);
			menu::bindings::write(nodes::synth_mode, f.ctx, static_cast<int32_t>(SynthMode::SUBTRACTIVE));
			expect(spec::menu_engine(f.ctx, *f.song.song).sound->getSynthMode()).to_equal(SynthMode::SUBTRACTIVE);
			menu::bindings::write(nodes::synth_mode, f.ctx, static_cast<int32_t>(before));
			deluge::test::setOscType(*spec::menu_engine(f.ctx, *f.song.song).sound, 0, osc_before[0]);
			deluge::test::setOscType(*spec::menu_engine(f.ctx, *f.song.song).sound, 1, osc_before[1]);
			covered++;
		}

		{ // --- mod_fx_type -------------------------------------------------------------------
			const ModFXType before = deluge::model::effects_owner_of(deluge::ui::scenes::spec::menu_engine(f.ctx, *f.song.song).mod)->binding->renderEffects().mod_fx.type;
			menu::bindings::write(nodes::mod_fx_type, f.ctx, static_cast<int32_t>(ModFXType::NONE));
			expect(deluge::model::effects_owner_of(deluge::ui::scenes::spec::menu_engine(f.ctx, *f.song.song).mod)->binding->renderEffects().mod_fx.type)
			    .to_equal(ModFXType::NONE);
			menu::bindings::write(nodes::mod_fx_type, f.ctx, static_cast<int32_t>(ModFXType::FLANGER));
			expect(deluge::model::effects_owner_of(deluge::ui::scenes::spec::menu_engine(f.ctx, *f.song.song).mod)->binding->renderEffects().mod_fx.type)
			    .to_equal(ModFXType::FLANGER);
			deluge::model::changeEffects(*deluge::model::effects_owner_of(deluge::ui::scenes::spec::menu_engine(f.ctx, *f.song.song).mod), [&](deluge::effects::EffectStates& e) { e.mod_fx.type = before; });
			covered++;
		}

		// The toggle's silencing only fires on a clip that is actually sounding (main
		// `origin/main:src/deluge/gui/menu_item/arpeggiator/mode.h`), and `song_fixture.h` never makes its clip the
		// output's active one -- nothing in the fixture plays. Arranged here, and put back.
		Clip* const saved_active = OutputPeek::active_clip_of(*spec::menu_engine(f.ctx, *f.song.song).output);
		OutputPeek::active_clip_of(*spec::menu_engine(f.ctx, *f.song.song).output) = spec::menu_engine(f.ctx, *f.song.song).clip;
		expect(spec::menu_engine(f.ctx, *f.song.song).clip->isActiveOnOutput()).to_be_true();

		{ // --- arp_mode: the silence block and the preset re-derivation ----------------------
			ArpeggiatorSettings* arp = spec::menu_engine(f.ctx, *f.song.song).arp;
			expect(arp).not_().to_be_null();
			arp->mode = ArpMode::OFF;
			arp->preset = ArpPreset::OFF;
			arp->octaveMode = ArpOctaveMode::DOWN;
			arp->noteMode = ArpNoteMode::DOWN;
			// `Sound::allNotesOff` clears this first thing: the one effect
			// of `silenceArp`'s synth arm that lands in readable model state.
			spec::menu_engine(f.ctx, *f.song.song).sound->invertReversed = true;

			menu::bindings::write(nodes::arp_mode, f.ctx, static_cast<int32_t>(ArpMode::ARP));

			expect(arp->mode).to_equal(ArpMode::ARP);
			// DOWN/DOWN is `ArpPreset::DOWN` (`arpeggiator.cpp`) -- false unless
			// `updatePresetFromCurrentSettings()` ran.
			expect(arp->preset).to_equal(ArpPreset::DOWN);
			expect(spec::menu_engine(f.ctx, *f.song.song).sound->invertReversed).to_be_false();
			covered++;
		}

		{ // --- arp_preset: the settings re-derivation, the restart flag, the silence block ----
			ArpeggiatorSettings* arp = spec::menu_engine(f.ctx, *f.song.song).arp;
			arp->mode = ArpMode::OFF;
			arp->preset = ArpPreset::OFF;
			arp->octaveMode = ArpOctaveMode::RANDOM;
			arp->noteMode = ArpNoteMode::RANDOM;
			arp->flagForceArpRestart = false;
			spec::menu_engine(f.ctx, *f.song.song).sound->invertReversed = true;

			menu::bindings::write(nodes::arp_preset, f.ctx, static_cast<int32_t>(ArpPreset::DOWN));

			expect(arp->preset).to_equal(ArpPreset::DOWN);
			// `ArpeggiatorSettings::updateSettingsFromCurrentPreset()` is the only thing
			// that moves these three.
			expect(arp->mode).to_equal(ArpMode::ARP);
			expect(arp->octaveMode).to_equal(ArpOctaveMode::DOWN);
			expect(arp->noteMode).to_equal(ArpNoteMode::DOWN);
			expect(arp->flagForceArpRestart).to_be_true();
			expect(spec::menu_engine(f.ctx, *f.song.song).sound->invertReversed).to_be_false();

			arp->mode = ArpMode::OFF;
			arp->preset = ArpPreset::OFF;
			arp->flagForceArpRestart = false;
			covered++;
		}

		OutputPeek::active_clip_of(*spec::menu_engine(f.ctx, *f.song.song).output) = saved_active;
		expect(covered).to_equal(std::size(t2::kWritesNeedingBootedUi));

		// --- and the kit sweep the generic `*_writes` loop cannot reach for these two ----------
		// The arp items' kit sweep (`SetKitLaneSetting`) reaches every `Drum`'s lane, not every
		// `SoundDrum` the way the generic fan-out does -- so the affect-entire sweep for the arp
		// items needs its own check. Both drums must come out fully re-derived, not merely with
		// `preset` stored.
		KitRig& k = kit_fixture();
		const MenuContext held = select_drum(k, k.drum_a, /*affect_entire_held=*/true);
		for (SoundDrum* drum : {k.drum_a, k.drum_b}) {
			drum->arpSettings.mode = ArpMode::OFF;
			drum->arpSettings.preset = ArpPreset::OFF;
			drum->arpSettings.octaveMode = ArpOctaveMode::RANDOM;
			drum->arpSettings.noteMode = ArpNoteMode::RANDOM;
			drum->arpSettings.flagForceArpRestart = false;
		}
		menu::bindings::write(nodes::arp_preset, held, static_cast<int32_t>(ArpPreset::UP));
		for (SoundDrum* drum : {k.drum_a, k.drum_b}) {
			expect(drum->arpSettings.preset).to_equal(ArpPreset::UP);
			expect(drum->arpSettings.mode).to_equal(ArpMode::ARP);
			expect(drum->arpSettings.octaveMode).to_equal(ArpOctaveMode::UP);
			expect(drum->arpSettings.noteMode).to_equal(ArpNoteMode::UP);
			expect(drum->arpSettings.flagForceArpRestart).to_be_true();
			drum->arpSettings.mode = ArpMode::OFF;
			drum->arpSettings.preset = ArpPreset::OFF;
			drum->arpSettings.flagForceArpRestart = false;
		}
		install(f.ctx, f.song.song);
	});
});
// clang-format on

// clang-format off
describe tables_2_reference_rows("tables_2: the reference rows", $ {
	it("parses tables_2.tsv clean and asks every row of it", _ {
		spec::OracleTable& t = spec::OracleTable::named("tables_2");
		expect(t.problems().size()).to_equal(size_t{0});
		expect(t.size()).to_be_greater_than(size_t{2400});
		expect(t.unasked()).to_equal(std::string{});
	});
});
// clang-format on

DELUGE_SPEC(tables_2_spec, tables_2_shape, tables_2_values, tables_2_validation, tables_2_chaining,
            tables_2_patch_cables, tables_2_pixels, tables_2_writes, tables_2_reference_rows)
