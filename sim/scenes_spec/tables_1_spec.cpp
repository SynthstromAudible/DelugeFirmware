/// @file
/// @brief The batch-1 menu tables, cross-checked against the legacy menu items they replace.
///
/// The six subtrees the retired Python generator produced are now `constexpr Node` literals
/// (`src/scenes/menu/nodes/*.cpp`). The legacy objects they were transcribed from are still linked
/// into this binary -- they are `gui/ui/menus.cpp`'s globals -- so every claim in that
/// transcription can be checked against the original rather than against a second hand-written
/// list:
///
///   * the children of each subtree are the same items, in the same order, with the same names;
///   * each node's `ParamRef` is the legacy item's own `getParamKind()` / `getParamIndex()`;
///   * `bindings::read(node, ctx)` equals the legacy item's `readCurrentValue()` + `getValue()` on
///     the same fixture sound;
///   * each `Kind::Enum`'s names are the legacy `Selection::getOptions()` strings;
///   * each node's `relevant` predicate agrees with the legacy `isRelevant()` override.
///
/// plus `validate_tree` over the seventeen roots, and one `MenuScene` render of the LPF page.
///
/// @note The SPEC may include `gui/menu_item/` and `gui/ui/menus.h`; the library may not
///       (`scripts/check_layer_rules.py`'s `scenes-includes-outside`). Same rule
///       `bindings_spec.cpp` and `menu_scene_spec.cpp` run under.

#include "scenes/menu/bindings.h"
#include "scenes/menu/context.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/menu/node.h"
#include "scenes/menu/node_id.h"
#include "scenes/menu/nodes/nodes.h"
#include "scenes/menu/relevance.h"
#include "scenes/menu/shortcuts.h"
#include "scenes/menu/validate.h"
#include "scenes/profile.h"
#include "scenes/registry.h"

#include "shell/profiles/deluge.h"
#include "shell/shell.h"

#include "gui/l10n/l10n.h"
#include "gui/l10n/strings.h"
#include "gui/menu_item/envelope/envelope_menu.h"
#include "gui/menu_item/horizontal_menu.h"
#include "gui/menu_item/menu_item_with_cc_learning.h"
#include "gui/menu_item/selection.h"
#include "gui/menu_item/submenu.h"
#include "gui/menu_item/submenu/actual_source.h"
#include "gui/menu_item/submenu/modulator.h"
#include "gui/menu_item/toggle.h"
#include "gui/ui/sound_editor.h"
#include "hid/display/oled.h"

// `gui/menu_item/filter_route.h` renders through an unqualified `OLED::main`, which resolves inside
// `gui/ui/menus.cpp` only because that file has a file-scope `using namespace deluge::hid::display`.
// This spec has no such directive, so name the type in the namespace the header is compiled in.
namespace deluge::gui::menu_item {
using deluge::hid::display::OLED;
} // namespace deluge::gui::menu_item
#include "gui/menu_item/filter_route.h"

#include "model/clip/instrument_clip.h"
#include "model/song/song.h"
#include "processing/sound/sound_instrument.h"
#include "processing/source.h"

#include "support/song_fixture.h"

#include "spec/gfx/support/golden.h"
#include "spec/shell/support/fake_surfaces.h"

#include "cppspec.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "spec/support/spec_main.h"

using namespace deluge::scenes;
using deluge::l10n::String;
using deluge::scenes::menu::MenuContext;
using deluge::scenes::menu::Node;
namespace legacy = deluge::gui::menu_item;

// ---------------------------------------------------------------------------------------------
// The legacy globals `g_menus.inc` declares (now pasted into `gui/ui/menus.cpp` verbatim). Only
// the horizontal submenus are named here: every leaf is reached through
// `HorizontalMenu::getItems()`. `soundFiltersMenu` itself is a plain vertical `Submenu` whose
// `items` are protected with no public accessor, so its three children are named directly: the two
// horizontal pages below, plus `filterRoutingMenu` (fix round 1, I4 -- it is a whole node with a
// bespoke enum, `get`/`set` and `slots = 4`, and was previously the one leaf outside the net).

extern legacy::HorizontalMenu lpfMenu;
extern legacy::HorizontalMenu hpfMenu;
extern legacy::FilterRouting filterRoutingMenu;
extern legacy::HorizontalMenu audioCompMenu;
extern legacy::HorizontalMenu unisonMenu;
extern legacy::HorizontalMenu recorderMenu;
extern legacy::submenu::ActualSource source0Menu;
extern legacy::submenu::ActualSource source1Menu;
extern legacy::submenu::Modulator modulator0Menu;
extern legacy::submenu::Modulator modulator1Menu;
extern legacy::HorizontalMenu oscMixerMenu;
extern legacy::envelope::EnvelopeMenu env1Menu;
extern legacy::envelope::EnvelopeMenu env2Menu;
extern legacy::envelope::EnvelopeMenu env3Menu;
extern legacy::envelope::EnvelopeMenu env4Menu;
extern legacy::HorizontalMenu lfo1Menu;
extern legacy::HorizontalMenu lfo2Menu;
extern legacy::HorizontalMenu lfo3Menu;
extern legacy::HorizontalMenu lfo4Menu;

namespace {

namespace nodes = deluge::scenes::menu::nodes;
namespace bindings = deluge::scenes::menu::bindings;

/// @brief The real app picks a language during boot; this spec renders text without booting.
void select_language() {
	deluge::l10n::chosenLanguage = &deluge::l10n::built_in::english;
}

/// @brief The fixture, plus the `soundEditor` globals the legacy items read instead of a context.
///
/// A legacy `readCurrentValue()` reaches `soundEditor.currentSound` / `currentModControllable` /
/// `currentParamManager` / `currentSourceIndex` / `currentMultiRange`
/// (`gui/ui/sound_editor.h:62-73`); `bindings::read` reaches the same objects through the
/// `MenuContext` `context_for` resolved. Pointing both at the same fixture is what makes the two
/// comparable.
struct Fixture {
	spec::SongFixture song = spec::make_synth_song();
	MenuContext ctx{};

	Fixture() {
		select_language();
		ctx = menu::context_for(*song.song, song.clip, 0, false, false, /*kit_global_fx=*/false);
		soundEditor.currentSound = ctx.sound;
		soundEditor.currentModControllable = ctx.mod;
		soundEditor.currentParamManager = ctx.params;
		soundEditor.currentSourceIndex = ctx.source_index;
		soundEditor.currentSource = ctx.source;
		soundEditor.currentMultiRange = ctx.range;
		soundEditor.currentMultiRangeIndex = ctx.range_index;
	}
};

Fixture& fixture() {
	static Fixture f;
	return f;
}

std::string text_of(uint16_t id) {
	deluge::ui::shell::TextScratch scratch;
	return std::string{deluge_profile().text_or_digits(id, scratch)};
}

/// @brief Legacy items whose `getName()` is not the l10n string the constructor was given.
///
/// Five classes override it to build a name at render time -- something a `Node::name` (one static
/// string id) cannot express, and the reason these rows are exempt from the name comparison rather
/// than the comparison being weakened for everyone:
///
///   * `filter::FilterParam` on a MORPH item -> the filter mode's own morph name
///     (`gui/menu_item/filter/param.h:38`, `filter/info.h:62-69`);
///   * `submenu::ActualSource` -> "OSC1"/"OSC2" via `asterixToInt` (`submenu/actual_source.h:26-38`);
///   * `envelope::EnvelopeMenu`, `osc::source::Volume`, `source::patched_param::ModulatorLevel`,
///     `osc::AudioRecorder` -> a `FormattedTitle` with the index substituted in
///     (`envelope/envelope_menu.h:31`, `osc/source/volume.h:28`, `modulator_level.h:27-28`,
///     `osc/audio_recorder.h:35`).
bool has_dynamic_name(const Node& node) {
	switch (node.name) {
	case static_cast<uint16_t>(String::STRING_FOR_LPF_MORPH):
	case static_cast<uint16_t>(String::STRING_FOR_HPF_MORPH):
	case static_cast<uint16_t>(String::STRING_FOR_OSCILLATOR_1):
	case static_cast<uint16_t>(String::STRING_FOR_OSCILLATOR_2):
	case static_cast<uint16_t>(String::STRING_FOR_ENVELOPE_MENU_TITLE):
	case static_cast<uint16_t>(String::STRING_FOR_OSC_LEVEL_MENU_TITLE):
	case static_cast<uint16_t>(String::STRING_FOR_FM_MOD_LEVEL_MENU_TITLE):
	case static_cast<uint16_t>(String::STRING_FOR_RECORD_TO_SAMPLE):
		return true;
	default:
		return false;
	}
}

/// @brief One `(node, legacy item)` pair to compare.
struct Pair {
	const Node* node;
	::MenuItem* item;
};

/// @brief Pair a subtree's node children against a legacy horizontal menu's items, in order.
///
/// @return Empty when the two lists are not even the same length -- the caller asserts that first,
///         so a size mismatch is reported once rather than as N confusing per-item failures.
std::vector<Pair> pair_with(std::span<const Node* const> kids, legacy::HorizontalMenu& menu) {
	std::vector<Pair> out;
	auto& items = menu.getItems();
	if (kids.size() != items.size()) {
		return out;
	}
	for (size_t i = 0; i < kids.size(); i++) {
		out.push_back(Pair{kids[i], items[i]});
	}
	return out;
}

/// @brief The batch-1 tree, one subtree at a time: `{node subtree root, legacy horizontal menu}`.
struct Subtree {
	const char* name;
	const Node* root;
	legacy::HorizontalMenu* legacy;
};

/// @brief Every horizontal page in batch 1, in `main.py`'s order (the filters submenu's own two
///        pages first, since it is the only *vertical* root and its children are the pages).
std::vector<Subtree> subtrees() {
	const Node* filters = nodes::kSoundFiltersChildren[0];
	return {
	    {"lpf", filters->children[0], &lpfMenu},
	    {"hpf", filters->children[1], &hpfMenu},
	    {"compressor", nodes::kSoundCompressorChildren[0], &audioCompMenu},
	    {"unison", nodes::kSoundUnisonChildren[0], &unisonMenu},
	    {"recorder", nodes::kSoundOscChildren[0], &recorderMenu},
	    {"source0", nodes::kSoundOscChildren[1], &source0Menu},
	    {"source1", nodes::kSoundOscChildren[2], &source1Menu},
	    {"modulator0", nodes::kSoundOscChildren[3], &modulator0Menu},
	    {"modulator1", nodes::kSoundOscChildren[4], &modulator1Menu},
	    {"osc mixer", nodes::kSoundOscChildren[5], &oscMixerMenu},
	    {"env1", nodes::kSoundEnvChildren[0], &env1Menu},
	    {"env2", nodes::kSoundEnvChildren[1], &env2Menu},
	    {"env3", nodes::kSoundEnvChildren[2], &env3Menu},
	    {"env4", nodes::kSoundEnvChildren[3], &env4Menu},
	    {"lfo1", nodes::kSoundLfoChildren[0], &lfo1Menu},
	    {"lfo2", nodes::kSoundLfoChildren[1], &lfo2Menu},
	    {"lfo3", nodes::kSoundLfoChildren[2], &lfo3Menu},
	    {"lfo4", nodes::kSoundLfoChildren[3], &lfo4Menu},
	};
}

std::vector<Pair> all_pairs() {
	std::vector<Pair> out;
	for (const Subtree& s : subtrees()) {
		for (const Pair& p : pair_with(s.root->children, *s.legacy)) {
			out.push_back(p);
		}
	}
	// `soundFiltersMenu`'s third child, which no horizontal page contains (fix round 1, I4).
	out.push_back(Pair{nodes::kSoundFiltersChildren[0]->children[2], &filterRoutingMenu});
	return out;
}

/// @brief The legacy item's current value, read the way legacy reads it, or `std::nullopt` for an
///        item that holds no value at all (the four bespoke `Kind::Custom` ones).
std::optional<int32_t> legacy_value(::MenuItem* item) {
	if (auto* v = dynamic_cast<legacy::Value<int32_t>*>(item); v != nullptr) {
		v->readCurrentValue();
		return v->getValue();
	}
	if (auto* v = dynamic_cast<legacy::Value<bool>*>(item); v != nullptr) {
		v->readCurrentValue();
		return v->getValue() ? 1 : 0;
	}
	return std::nullopt;
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

	void open(const Node& root, const MenuContext& ctx) {
		(void)menu::assign_node_ids(nodes::kBatch1Roots);
		menu_scene().open(root, ctx);
		shell.init(deluge::ui::shell::SceneId{deluge::ui::shell::SceneKind::FirstApp, menu::node_id(root)});
	}
};

} // namespace

// clang-format off
describe tables_1_shape("scenes::menu batch-1 tables: shape", $ {
	it("declares seventeen roots, one per legacy top-level menu", _ {
		expect(nodes::kBatch1Roots.size()).to_equal(size_t(17));
	});

	it("gives every subtree the same children, in the same order, as its legacy menu", _ {
		(void)fixture();
		for (const Subtree& s : subtrees()) {
			expect(s.root->children.size() == s.legacy->getItems().size()).to_be_true();
		}
	});

	it("gives every node the legacy item's own name", _ {
		(void)fixture();
		for (const Pair& p : all_pairs()) {
			if (has_dynamic_name(*p.node)) {
				continue; // see has_dynamic_name's own note
			}
			expect(text_of(p.node->name) == std::string{p.item->getName()}).to_be_true();
		}
	});

	it("binds MIDI/CC learn to exactly the param legacy would have learned", _ {
		Fixture& f = fixture();
		size_t learnable = 0;
		for (const Pair& p : all_pairs()) {
			// `MenuItemWithCCLearning::getLearningThing()` (`menu_item_with_cc_learning.h:27`) is
			// the authority, and it is what catches the four `menu_item::Transpose` derivatives
			// (fix round 1, I1): they are a `Decimal` with their own read/write AND a
			// `PatchedParam` with a real id, so `getParamKind()` reports NONE for them
			// (`transpose.h:26-51` bridges only `getParamIndex`) while their learn target is real.
			// The nodes carry that id as a learn-only `ParamRef` beside their `get`/`set`
			// (`detail.h`'s `with_learn_param`, `node.h`'s precedence note).
			auto* learner = dynamic_cast<::MenuItemWithCCLearning*>(p.item);
			if (learner == nullptr) {
				continue;
			}
			learnable++;
			expect(menu::bindings::learn_target(*p.node, f.ctx) == learner->getLearningThing()).to_be_true();
		}
		// The four transposes plus every `ParamRef` leaf; a drop to 0 would make the loop vacuous.
		expect(learnable > size_t(40)).to_be_true();
	});

	it("gives every node the legacy item's own ParamRef, or none where legacy has none", _ {
		(void)fixture();
		for (const Pair& p : all_pairs()) {
			const auto kind = p.item->getParamKind();
			if (kind == deluge::modulation::params::Kind::NONE) {
				continue;
			}
			// A `Submenu` reports the kind of its first param child, not one of its own
			// (`gui/menu_item/submenu.cpp`'s getParamKind); only leaves are compared.
			if (p.node->kind == menu::Kind::Submenu) {
				continue;
			}
			expect(uint32_t(p.node->bind.param.index) == p.item->getParamIndex()).to_be_true();
			// The *index* is the item's own; the *kind* is not. `UnpatchedParam::getParamKind`
			// (`gui/menu_item/unpatched_param.cpp:98-101`) reports whatever param collection
			// `SoundEditor::getCurrentModelStack` lands on, and with no root UI open that function
			// takes its `!rootUIIsClipMinderScreen()` arm (`gui/ui/sound_editor.cpp:1986`) and
			// returns the *song's* stack -- so an unpatched item reports UNPATCHED_GLOBAL here and
			// UNPATCHED_SOUND on a real sound-editor screen. A `ParamRef` states one kind
			// statically, and `bindings.cpp` treats the two identically (`serves_param`,
			// `add_param`), so both spellings are accepted for an unpatched node.
			const bool unpatched = kind == deluge::modulation::params::Kind::UNPATCHED_SOUND
			                       || kind == deluge::modulation::params::Kind::UNPATCHED_GLOBAL;
			if (unpatched) {
				expect(p.node->bind.param.kind == deluge::modulation::params::Kind::UNPATCHED_SOUND
				       || p.node->bind.param.kind == deluge::modulation::params::Kind::UNPATCHED_GLOBAL)
				    .to_be_true();
			}
			else {
				expect(p.node->bind.param.kind == kind).to_be_true();
			}
		}
	});

	it("marks exactly the legacy range-dependent items as range_dependent", _ {
		(void)fixture();
		for (const Pair& p : all_pairs()) {
			// One direction only, deliberately: a node that is NOT flagged must not correspond to a
			// legacy item that overrides `isRangeDependent()`. The converse does not hold, because
			// two of the four legacy classes reach the same rule *without* the override -- through
			// `checkPermissionToBeginSessionForRangeSpecificParam` in their own
			// `checkPermissionToBeginSession` (`file_selector.cpp:68-86`,
			// `osc/audio_recorder.h:65-74`) -- which is exactly what `node.h`'s own list says.
			if (!p.node->range_dependent) {
				expect(p.item->isRangeDependent()).to_be_false();
			}
		}
		// The four legacy classes that route through
		// `checkPermissionToBeginSessionForRangeSpecificParam` are ten nodes here.
		expect(nodes::kRangeDependentNodes.size()).to_equal(size_t(10));
		expect(nodes::kHalfPrecisionNodes.size()).to_equal(size_t(3));
	});
});

describe tables_1_values("scenes::menu batch-1 tables: values", $ {
	it("reads what the legacy item reads, for every node that holds a value", _ {
		Fixture& f = fixture();
		for (const Pair& p : all_pairs()) {
			const std::optional<int32_t> want = legacy_value(p.item);
			if (!want.has_value()) {
				continue; // a bespoke Kind::Custom item -- nothing to read
			}
			expect(menu::bindings::read(*p.node, f.ctx) == *want).to_be_true();
		}
	});

	it("names every enum option exactly as the legacy Selection does", _ {
		Fixture& f = fixture();
		for (const Pair& p : all_pairs()) {
			if (p.node->kind != menu::Kind::Enum) {
				continue;
			}
			auto* selection = dynamic_cast<legacy::Selection*>(p.item);
			expect(selection != nullptr).to_be_true();
			if (selection == nullptr) {
				continue;
			}
			const auto want = selection->getOptions(legacy::Selection::OptType::FULL);

			std::string_view got[menu::kMaxDynamicNames];
			deluge::ui::shell::TextScratch scratch[menu::kMaxDynamicNames];
			const size_t n = menu::bindings::names(*p.node, f.ctx, got, scratch);
			expect(n == want.size()).to_be_true();
			for (size_t i = 0; i < n && i < want.size(); i++) {
				expect(std::string{got[i]} == std::string{want[i]}).to_be_true();
			}
		}
	});

	it("agrees with the legacy isRelevant on the fixture sound", _ {
		Fixture& f = fixture();
		for (const Pair& p : all_pairs()) {
			const bool want = p.item->isRelevant(f.ctx.mod, f.ctx.source_index);
			const bool got = p.node->relevant == nullptr || p.node->relevant(f.ctx);
			expect(got == want).to_be_true();
		}
	});

	it("carries the legacy occupied-slot counts", _ {
		(void)fixture();
		// `MenuItem::getOccupiedSlots()` is protected, so the two overridden counts are asserted
		// against the values their sources state: `FilterRouting` draws the whole four-slot page
		// (`filter_route.h:62`) and `osc::AudioRecorder` takes two (`osc/audio_recorder.h:77`).
		// Everything else keeps `menu_item.h:304`'s default of one.
		expect(int(nodes::kSoundFiltersChildren[0]->children[2]->slots)).to_equal(4);
		expect(int(nodes::kSoundOscChildren[0]->children[0]->slots)).to_equal(2);
		expect(int(nodes::kSoundOscChildren[0]->children[1]->slots)).to_equal(2);
		for (const Pair& p : all_pairs()) {
			const bool wide = p.node == nodes::kSoundFiltersChildren[0]->children[2]
			                  || p.node == nodes::kSoundOscChildren[0]->children[0]
			                  || p.node == nodes::kSoundOscChildren[0]->children[1];
			if (!wide) {
				expect(int(p.node->slots)).to_equal(1);
			}
		}
	});

	it("has a registered name for every predicate the tables use", _ {
		for (const Pair& p : all_pairs()) {
			if (p.node->relevant == nullptr) {
				continue;
			}
			expect(menu::relevance::name_of(p.node->relevant) != std::string_view{"?"}).to_be_true();
		}
		// The registry is hand-maintained (see `relevance.cpp`'s note): C++ cannot enumerate a
		// header's declarations, so what is checkable is that it has the documented number of rows,
		// that no row is null, and that no two rows share a name or a function.
		const auto registry = menu::relevance::registry();
		expect(registry.size()).to_equal(size_t(27));
		for (size_t i = 0; i < registry.size(); i++) {
			expect(registry[i].fn != nullptr).to_be_true();
			expect(registry[i].name.empty()).to_be_false();
			for (size_t j = i + 1; j < registry.size(); j++) {
				expect(registry[i].fn == registry[j].fn).to_be_false();
				expect(registry[i].name == registry[j].name).to_be_false();
			}
		}
	});
});

describe tables_1_validation("scenes::menu batch-1 tables: validation", $ {
	it("validates clean apart from the three FormattedTitle name collisions legacy has", _ {
		Fixture& f = fixture();
		menu::set_shortcut_table_for_test({}, 0, 0);
		const MenuContext contexts[] = {f.ctx};
		// `allow_placeholder_custom`: the four bespoke oscillator items point at `kPending` until E2
		// lands the real implementations, and `validate_tree` is strict about that by default
		// (task-7 ruling 3). Standing the rule down here rather than everywhere is the point --
		// the next example asserts strict mode still reports exactly those four.
		const auto issues =
		    menu::validate_tree(nodes::kBatch1Roots, contexts, nodes::kRangeDependentNodes,
		                        nodes::kHalfPrecisionNodes, menu::ValidateOptions{.allow_placeholder_custom = true});
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
		// Three sibling pairs share an l10n id because legacy disambiguates them only at render
		// time, through `FormattedTitle`, which one `uint16_t Node::name` cannot express:
		//
		//   * `sample0RecorderMenu` / `sample1RecorderMenu`, both `STRING_FOR_RECORD_TO_SAMPLE`
		//     (`g_menus.inc:55-56` at `ce4a79de0`; `osc/audio_recorder.h:34`);
		//   * `source0VolumeMenu` / `source1VolumeMenu`, both `STRING_FOR_OSC_LEVEL_MENU_TITLE`
		//     (`:144-145`; `osc/source/volume.h:29`);
		//   * `modulator0Volume` / `modulator1Volume`, both `STRING_FOR_FM_MOD_LEVEL_MENU_TITLE`
		//     (`:146-147`; `source/patched_param/modulator_level.h:29`).
		//
		// Spec E §4.4's rule ("no duplicate `(name, parent)`") is sibling-scoped and name-only, and
		// `validate.cpp` implements it exactly; the tables are a faithful transcription. So the
		// three reports are correct on both sides, and the controller blessed asserting the exact
		// count rather than weakening the rule (fix round 1, I2) -- a fourth collision still fails
		// loudly, which is what the rule is for. Task 12 records the tolerance as a spec E §4.4
		// amendment, and E2's formatted-name field on `Node` is what removes it.
		expect(duplicates).to_equal(size_t(3));
		expect(others).to_equal(size_t(0));
	});

	it("reports the four E2 placeholders when strict", _ {
		Fixture& f = fixture();
		menu::set_shortcut_table_for_test({}, 0, 0);
		const MenuContext contexts[] = {f.ctx};
		// Default `ValidateOptions` is strict, so the same tree now reports one
		// `PlaceholderCustom` per node pointing at `kPending`: both audio recorders, both file
		// selectors and the four sample loop points -- eight nodes over one shared instance
		// (`sound_osc.h`'s note; the *item* budget still counts 1).
		const auto issues = menu::validate_tree(nodes::kBatch1Roots, contexts, nodes::kRangeDependentNodes,
		                                        nodes::kHalfPrecisionNodes);
		size_t placeholders = 0;
		for (const menu::Issue& i : issues) {
			if (i.kind == menu::Issue::Kind::PlaceholderCustom) {
				placeholders++;
			}
			expect(i.kind != menu::Issue::Kind::NullCustom).to_be_true();
		}
		expect(placeholders).to_equal(size_t(8));
	});

	it("assigns an id to every node in the batch-1 tree", _ {
		Fixture& f = fixture();
		const MenuContext contexts[] = {f.ctx};
		(void)menu::validate_tree(nodes::kBatch1Roots, contexts);
		for (const Node* root : nodes::kBatch1Roots) {
			expect(menu::node_id(*root) != uint16_t(0)).to_be_true();
			for (const Node* kid : root->children) {
				expect(menu::node_id(*kid) != uint16_t(0)).to_be_true();
			}
		}
	});
});

describe tables_1_pixels("scenes::menu batch-1 tables: pixels", $ {
	it("renders the LPF page", _ {
		Fixture& f = fixture();
		deluge::notify::dirty().drain();
		Rig r;
		const Node* filters = nodes::kSoundFiltersChildren[0];
		r.open(*filters->children[0], f.ctx);
		r.shell.tick(0);
		expect_golden(self, "../ui/menu/tables1_lpf_48.pbm", r.fake.pbm(0));
	});
});
// clang-format on

/// Four `describe` groups in one TU, so `DELUGE_SPEC` (one spec per entry point) is spelled out
/// here with `add_specs` instead.
int tables_1_spec(int argc, char** const argv) {
	return CppSpec::parse(argc, argv)
	               .add_specs(tables_1_shape, tables_1_values, tables_1_validation, tables_1_pixels)
	               .exec()
	               .is_success()
	           ? EXIT_SUCCESS
	           : EXIT_FAILURE;
}
