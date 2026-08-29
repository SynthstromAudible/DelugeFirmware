/// @file
/// @brief The batch-2 menu tables -- the whole sound-editor root -- cross-checked against the
///        legacy menu items they replace.
///
/// Task 6 declared six subtrees (filters, compressor, unison, oscillators, envelopes, LFOs) and
/// stopped below the root. Task 7 declares the root itself and everything else hanging off it:
/// actions, master, arpeggiator, randomizer, FX, sidechain, voice, bend, the sequence direction
/// and the output-MIDI page. The legacy objects are still linked into this binary (they are
/// `gui/ui/menus.cpp`'s globals), so the whole transcription is checked against the originals
/// rather than against a second hand-written list:
///
///   * the tree walks in lockstep with the legacy tree -- same children, same order, same count,
///     at every level from `soundEditorRootMenu` down;
///   * each node's name is the legacy item's own `getName()`;
///   * each node's `ParamRef` is the legacy item's own `getParamKind()` / `getParamIndex()`;
///   * `bindings::read(node, ctx)` equals the legacy item's `readCurrentValue()` + `getValue()` on
///     the same fixture sound;
///   * each `Kind::Enum`'s names are the legacy `Selection::getOptions(FULL)` strings;
///   * each node's `relevant` predicate agrees with the legacy `isRelevant()` override;
///
/// plus `validate_tree` over the root (placeholders allowed, see the validation group) and two
/// `MenuScene` renders of the root list.
///
/// @note The SPEC may include `gui/menu_item/` and `gui/ui/menus.h`; the library may not
///       (`scripts/check_layer_rules.py`'s `scenes-includes-outside`). Same rule
///       `bindings_spec.cpp`, `menu_scene_spec.cpp` and `tables_1_spec.cpp` run under.

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
#include "gui/menu_item/menu_item_with_cc_learning.h"
#include "gui/menu_item/selection.h"
#include "gui/menu_item/submenu.h"
#include "gui/ui/sound_editor.h"
#include "hid/display/oled.h"

#include "model/clip/instrument_clip.h"
#include "model/model_stack.h"
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

/// The one legacy global this spec needs by name: everything else is reached by walking its
/// children. `Submenu` rather than `HorizontalMenu` -- `soundEditorRootMenu` is a vertical list.
extern legacy::Submenu soundEditorRootMenu;

namespace {

namespace nodes = deluge::scenes::menu::nodes;
namespace bindings = deluge::scenes::menu::bindings;

/// @brief Read a `Submenu`'s child list.
///
/// `Submenu::items` is protected with no public accessor, and only `HorizontalMenu` exposes
/// `getItems()` -- but half of the root's children are plain vertical `Submenu`s, and the root
/// itself is one. Naming the members through a derived class is the standard (and standard-blessed,
/// [class.protected]) way to reach them: the pointer-to-member is formed inside a class derived
/// from `Submenu`, which is exactly the access the rule permits, and its type is still
/// `... Submenu::*` so it applies to the base objects `menus.cpp` actually defines.
///
/// The alternative -- re-listing every submenu's children in this file -- would make the spec a
/// second copy of the thing under test rather than a check of it.
struct SubmenuPeek : legacy::Submenu {
	static const deluge::vector<::MenuItem*>& items_of(legacy::Submenu& menu) { return menu.*(&SubmenuPeek::items); }
};

/// @brief The real app picks a language during boot; this spec renders text without booting.
void select_language() {
	deluge::l10n::chosenLanguage = &deluge::l10n::built_in::english;
}

/// @brief The fixture, plus the `soundEditor` globals the legacy items read instead of a context.
///
/// Identical in intent to `tables_1_spec.cpp`'s: a legacy `readCurrentValue()` reaches
/// `soundEditor.current*` (`gui/ui/sound_editor.h:62-73`) while `bindings::read` reaches the same
/// objects through the `MenuContext` `context_for` resolved, so both must point at one fixture.
/// Batch 2 adds two more of those globals -- `currentArpSettings` (`:69`), which the whole
/// arpeggiator and randomizer surface reads, and `currentPriority` (`:71`).
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
		soundEditor.currentArpSettings = ctx.arp;
		soundEditor.currentPriority = ctx.priority;
		soundEditor.currentSidechain = ctx.sidechain;
		soundEditor.currentSampleControls = ctx.sample_controls;
		// `SoundEditor::setup` caches this once per entry (`sound_editor.cpp:1811-1816`), and
		// `arpeggiator::IncludeInKitArp` reads the cached copy. Computed here the way legacy
		// computes it -- straight from `Sound::allowNoteTails` over the fixture's own model stack --
		// so the comparison against `bindings::allows_note_tails` stays a real one.
		soundEditor.allowsNoteTails = true;
		if (ctx.sound != nullptr) {
			char memory[MODEL_STACK_MAX_SIZE];
			ModelStackWithThreeMainThings* stack = menu::model_stack_for(ctx, memory);
			soundEditor.allowsNoteTails = ctx.sound->allowNoteTails(stack->addSoundFlags(), true);
		}
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
/// Same exemption `tables_1_spec` documents, extended with batch 2's own dynamic names. A
/// `Node::name` is one static string id and cannot express any of them:
///
///   * batch 1's, reachable again from the root: `filter::FilterParam`'s morph name
///     (`filter/param.h:38`), `submenu::ActualSource` (`submenu/actual_source.h:26-38`),
///     `envelope::EnvelopeMenu`, `osc::source::Volume`, `ModulatorLevel`, `osc::AudioRecorder`
///     (`FormattedTitle`);
///   * `mod_fx::Depth_Patched` / `Feedback` / `Offset`, whose names come from
///     `modfx::getParamName(type, ...)` and so change with the selected mod-FX type
///     (`mod_fx/depth_patched.h:29-31`, `feedback.h:26-28`, `offset.h:27-29`);
///   * `reverb::RoomSize` and `reverb::Width`, which read "TIME" / "DIFFUSION" on the two newer
///     reverb models (`reverb/room_size.h:29-38`, `width.h:28-37`).
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
	case static_cast<uint16_t>(String::STRING_FOR_DEPTH):
	case static_cast<uint16_t>(String::STRING_FOR_FEEDBACK):
	case static_cast<uint16_t>(String::STRING_FOR_OFFSET):
	case static_cast<uint16_t>(String::STRING_FOR_ROOM_SIZE):
	case static_cast<uint16_t>(String::STRING_FOR_WIDTH):
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

/// @brief Walk the node tree and the legacy tree together, pairing them level by level.
///
/// @return False as soon as the two disagree on a child count -- the caller asserts that first, so
///         a shape mismatch is one failure rather than N confusing per-item ones.
bool walk(const Node& node, ::MenuItem& item, std::vector<Pair>& out, std::string& where) {
	out.push_back(Pair{&node, &item});
	if (node.kind != menu::Kind::Submenu) {
		return true;
	}
	auto* submenu = dynamic_cast<legacy::Submenu*>(&item);
	if (submenu == nullptr) {
		where = "node is a submenu but legacy item is not: " + std::string{item.getName()};
		return false;
	}
	const auto& kids = SubmenuPeek::items_of(*submenu);
	if (kids.size() != node.children.size()) {
		where = "child count differs under " + std::string{item.getName()};
		return false;
	}
	for (size_t i = 0; i < kids.size(); i++) {
		if (node.children[i] == nullptr || kids[i] == nullptr) {
			where = "null child under " + std::string{item.getName()};
			return false;
		}
		if (!walk(*node.children[i], *kids[i], out, where)) {
			return false;
		}
	}
	return true;
}

/// @brief Every `(node, legacy item)` pair in the sound-editor root tree, or an empty list plus a
///        reason when the two trees do not have the same shape.
struct Walk {
	std::vector<Pair> pairs;
	std::string problem;
};

const Walk& tree() {
	static Walk w = [] {
		(void)fixture();
		Walk result;
		if (!walk(nodes::sound_root, soundEditorRootMenu, result.pairs, result.problem)) {
			result.pairs.clear();
		}
		return result;
	}();
	return w;
}

/// @brief The legacy item's current value, read the way legacy reads it, or `std::nullopt` for an
///        item that holds no value at all (the bespoke `Kind::Custom` ones and the submenus).
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

/// @brief A `Shell` over `FakeSurfaceSet` with the application's scene registry, for the goldens.
struct Rig {
	const deluge::ui::shell::BoardProfile& profile;
	deluge::ui::shell::SceneRegistry reg = scene_registry();
	uitest::FakeSurfaceSet fake;
	deluge::ui::shell::Shell shell{fake, profile, reg, deluge::ui::shell::Services{}, deluge::ui::shell::ShellConfig{}};

	Rig(const Rig&) = delete;
	Rig& operator=(const Rig&) = delete;
	explicit Rig(const DelugeBoard& board = deluge::ui::shell::kDelugeBoard,
	             const deluge::ui::shell::BoardProfile& p = deluge_profile())
	    : profile(p), fake(board, p) {}

	void open(const Node& root, const MenuContext& ctx) {
		(void)menu::assign_node_ids(nodes::kSoundRootRoots);
		menu_scene().open(root, ctx);
		shell.init(deluge::ui::shell::SceneId{deluge::ui::shell::SceneKind::FirstApp, menu::node_id(root)});
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
		static constexpr const deluge::ui::Style* kStyles[] = {&deluge::ui::kDelugeStyle64};
		deluge::ui::shell::BoardProfile q = deluge_profile();
		q.style_by_index = kStyles;
		return q;
	}();
	return p;
}

} // namespace

// clang-format off
describe tables_2_shape("scenes::menu batch-2 tables: shape", $ {
	it("declares the sound-editor root with the legacy child count and order", _ {
		(void)fixture();
		// `menus.cpp`'s `soundEditorRootMenu` literal (at `ce4a79de0`, `:1521-1551`; at HEAD,
		// `:1772-1802`) lists **27** children, not the 28 the task brief's prose says -- the
		// brief's own enumeration of the names is 27 long, so the count was a miscount and the
		// literal is the authority. Asserted against the live object, not a copy of the list.
		const auto& kids = SubmenuPeek::items_of(soundEditorRootMenu);
		expect(kids.size()).to_equal(size_t(27));
		expect(nodes::sound_root.children.size()).to_equal(size_t(27));
	});

	it("walks the whole root tree in lockstep with the legacy tree", _ {
		const Walk& w = tree();
		expect(w.problem).to_equal(std::string{});
		expect(w.pairs.empty()).to_be_false();
	});

	it("gives every node the legacy item's own name", _ {
		for (const Pair& p : tree().pairs) {
			if (has_dynamic_name(*p.node)) {
				continue; // see has_dynamic_name's own note
			}
			expect(text_of(p.node->name) == std::string{p.item->getName()}).to_be_true();
		}
	});

	it("gives every node the legacy item's own ParamRef, or none where legacy has none", _ {
		for (const Pair& p : tree().pairs) {
			const auto kind = p.item->getParamKind();
			if (kind == deluge::modulation::params::Kind::NONE) {
				continue;
			}
			// A `Submenu` reports the kind of its first param child, not one of its own
			// (`gui/menu_item/submenu.cpp`'s getParamKind); only leaves are compared. A
			// `Kind::Custom` node is an E2 placeholder with no binding at all -- and the two that
			// stand in for `patch_cable_strength::Fixed` items (`vibrato`, `sidechain_volume`)
			// report a *patch cable* descriptor, not a param index
			// (`patch_cable_strength.cpp`'s `getParamIndex`), so there is nothing to compare.
			if (p.node->kind == menu::Kind::Submenu || p.node->kind == menu::Kind::Custom) {
				continue;
			}
			expect(uint32_t(p.node->bind.param.index) == p.item->getParamIndex()).to_be_true();
			// The *index* is the item's own; the *kind* is not, for an unpatched item -- see
			// `tables_1_spec`'s long note on `UnpatchedParam::getParamKind` reporting whatever
			// collection `SoundEditor::getCurrentModelStack` lands on with no root UI open.
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

	it("binds MIDI/CC learn to exactly the param legacy would have learned", _ {
		Fixture& f = fixture();
		size_t learnable = 0;
		for (const Pair& p : tree().pairs) {
			auto* learner = dynamic_cast<::MenuItemWithCCLearning*>(p.item);
			if (learner == nullptr) {
				continue;
			}
			// `patch_cable_strength::PatchCableStrength` is a `MenuItemWithCCLearning` whose
			// learn target is a patch cable, not a param (`patch_cable_strength.cpp`'s
			// `getDestinationDescriptor`); the two nodes that stand in for one
			// (`vibrato`, `sidechain_volume`) are E2 placeholders with no binding at all, so they
			// are outside this comparison until E2 implements them.
			if (p.node->kind == menu::Kind::Custom) {
				continue;
			}
			learnable++;
			expect(menu::bindings::learn_target(*p.node, f.ctx) == learner->getLearningThing()).to_be_true();
		}
		// Every `ParamRef` leaf in the root tree plus the four transposes; a drop to 0 would make
		// the loop vacuous.
		expect(learnable > size_t(60)).to_be_true();
	});

	it("carries the legacy occupied-slot counts for the wide batch-2 items", _ {
		(void)fixture();
		// `MenuItem::getOccupiedSlots()` is protected, so the overridden counts are asserted
		// against the values their sources state: `mod_fx::Type` draws the whole four-slot page
		// (`mod_fx/type.h:60-63`), and both rhythm items take two
		// (`arpeggiator/rhythm.h:51-52`, `arpeggiator/midi_cv/rhythm.h:56-57`).
		expect(int(nodes::mod_fx_type.slots)).to_equal(4);
		expect(int(nodes::arp_rhythm.slots)).to_equal(2);
		expect(int(nodes::arp_rhythm_midi.slots)).to_equal(2);
	});
});

describe tables_2_values("scenes::menu batch-2 tables: values", $ {
	it("reads what the legacy item reads, for every node that holds a value", _ {
		Fixture& f = fixture();
		for (const Pair& p : tree().pairs) {
			const std::optional<int32_t> want = legacy_value(p.item);
			if (!want.has_value()) {
				continue; // a bespoke Kind::Custom item or a submenu -- nothing to read
			}
			if (p.node->kind == menu::Kind::Custom) {
				continue; // an E2 placeholder: the node deliberately carries no binding
			}
			expect(menu::bindings::read(*p.node, f.ctx) == *want).to_be_true();
		}
	});

	it("names every enum option exactly as the legacy Selection does", _ {
		Fixture& f = fixture();
		for (const Pair& p : tree().pairs) {
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
		for (const Pair& p : tree().pairs) {
			const bool want = p.item->isRelevant(f.ctx.mod, f.ctx.source_index);
			const bool got = p.node->relevant == nullptr || p.node->relevant(f.ctx);
			expect(got == want).to_be_true();
		}
	});

	it("has a registered name for every predicate the tables use", _ {
		for (const Pair& p : tree().pairs) {
			if (p.node->relevant == nullptr) {
				continue;
			}
			expect(menu::relevance::name_of(p.node->relevant) != std::string_view{"?"}).to_be_true();
		}
		const auto registry = menu::relevance::registry();
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

describe tables_2_validation("scenes::menu batch-2 tables: validation", $ {
	it("validates the sound root clean apart from the nine legacy name collisions", _ {
		Fixture& f = fixture();
		menu::set_shortcut_table_for_test({}, 0, 0);
		const MenuContext contexts[] = {f.ctx};
		const auto issues = menu::validate_tree(nodes::kSoundRootRoots, contexts, nodes::kRangeDependentNodes,
		                                        nodes::kHalfPrecisionNodes,
		                                        menu::ValidateOptions{.allow_placeholder_custom = true});
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
		// Nine sibling collisions, every one a legacy `FormattedTitle` -- a name legacy builds at
		// render time by substituting an index, which one `uint16_t Node::name` cannot express:
		//
		//   * the three pairs `tables_1_spec` blesses -- `sample0/1RecorderMenu`
		//     (`STRING_FOR_RECORD_TO_SAMPLE`, `osc/audio_recorder.h:35`), `source0/1VolumeMenu`
		//     (`STRING_FOR_OSC_LEVEL_MENU_TITLE`, `osc/source/volume.h:28`) and
		//     `modulator0/1Volume` (`STRING_FOR_FM_MOD_LEVEL_MENU_TITLE`, `modulator_level.h:27-28`);
		//   * six more that only appear once the root itself is declared: `env1Menu` .. `env4Menu`
		//     are four *siblings* of `soundEditorRootMenu`, all named
		//     `STRING_FOR_ENVELOPE_MENU_TITLE` (`envelope/envelope_menu.h:31`) -- four items
		//     colliding pairwise is six reports. Task 6 could not see them: it declared the
		//     envelopes as four separate roots, with no parent to be siblings under.
		//
		// Everything else legacy names twice under one parent is filtered by `validate.cpp`'s
		// `co_visible` -- the randomizer's eleven sound/MIDI pairs, the arpeggiator's rhythm,
		// sequence-length and note-mode twins, and `bendMenu` beside `drumBendRangeMenu` -- because
		// no context shows both. The rule still fires on a real collision, which is what these nine
		// are: E2's formatted-name field on `Node` is what removes them, and Task 12 records the
		// tolerance as a spec E §4.4 amendment.
		expect(duplicates).to_equal(size_t(9));
		expect(others).to_equal(size_t(0));
	});

	it("assigns an id to every node in the sound root tree", _ {
		Fixture& f = fixture();
		const MenuContext contexts[] = {f.ctx};
		(void)menu::validate_tree(nodes::kSoundRootRoots, contexts, {}, {},
		                          menu::ValidateOptions{.allow_placeholder_custom = true});
		for (const Pair& p : tree().pairs) {
			expect(menu::node_id(*p.node) != uint16_t(0)).to_be_true();
		}
	});
});

describe tables_2_pixels("scenes::menu batch-2 tables: pixels", $ {
	it("renders the sound-editor root list", _ {
		Fixture& f = fixture();
		deluge::notify::dirty().drain();
		Rig r;
		r.open(nodes::sound_root, f.ctx);
		r.shell.tick(0);
		expect_golden(self, "../ui/menu/menu_sound_root_48.pbm", r.fake.pbm(0));
	});

	it("renders the sound-editor root list on a 64-row panel", _ {
		Fixture& f = fixture();
		deluge::notify::dirty().drain();
		Rig r{board_64(), profile_64()};
		r.open(nodes::sound_root, f.ctx);
		r.shell.tick(0);
		expect_golden(self, "../ui/menu/menu_sound_root_64.pbm", r.fake.pbm(0));
	});
});
// clang-format on

/// Four `describe` groups in one TU, so `DELUGE_SPEC` (one spec per entry point) is spelled out
/// here with `add_specs` instead.
int tables_2_spec(int argc, char** const argv) {
	return CppSpec::parse(argc, argv)
	               .add_specs(tables_2_shape, tables_2_values, tables_2_validation, tables_2_pixels)
	               .exec()
	               .is_success()
	           ? EXIT_SUCCESS
	           : EXIT_FAILURE;
}
