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
#include "gui/menu_item/multi_range.h" // E2 task 9: the range picker's legacy counterpart
#include "gui/menu_item/range.h"
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
///     (`envelope/envelope_menu.h:31`, `osc/source/volume.h:27`, `modulator_level.h:27-28`,
///     `osc/audio_recorder.h:28`).
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

/// @brief Call a legacy item's own `writeCurrentValue()`.
///
/// `Value<T>::writeCurrentValue` is `protected` (`gui/menu_item/value.h:52`) and the only public
/// route to it is `selectEncoderAction`, which also calls `renderUIsForOled()` -- a UI path this
/// spec has no business entering. Naming the member through a derived class is the
/// [class.protected]-blessed route: the pointer-to-member is formed inside a class derived from
/// `Value<T>`, and its type is still `void (Value<T>::*)()`.
template <typename T>
struct ValuePeek : legacy::Value<T> {
	static void write(legacy::Value<T>& value) { (value.*(&ValuePeek::writeCurrentValue))(); }
};

/// @brief Drive @p item's legacy write path with @p v.
/// @return False for an item that holds no value (a submenu, or a bespoke `Kind::Custom` one).
bool legacy_write(::MenuItem* item, int32_t v) {
	if (auto* n = dynamic_cast<legacy::Value<int32_t>*>(item); n != nullptr) {
		n->setValue(v);
		ValuePeek<int32_t>::write(*n);
		return true;
	}
	if (auto* b = dynamic_cast<legacy::Value<bool>*>(item); b != nullptr) {
		b->setValue(v != 0);
		ValuePeek<bool>::write(*b);
		return true;
	}
	return false;
}

/// @brief Three values spanning @p r: the two endpoints and the middle.
/// @brief A sentinel value inside @p r that is not @p v, or nothing when the range is a point.
///
/// Fix round 1, I2: the write comparison needs a starting state the value under test is *not*
/// already at, or a port hook that stores nothing looks identical to one that stores correctly.
std::optional<int32_t> sentinel_for(const menu::IntRange& r, int32_t v) {
	if (r.min == r.max) {
		return std::nullopt;
	}
	return v == r.min ? r.max : r.min;
}

/// @brief What the two write paths leave behind, each driven from the same starting state.
struct WriteProbe {
	bool ran = false;
	int32_t got_port = 0;
	int32_t want_port = 0;
	std::optional<int32_t> got_legacy{};
	std::optional<int32_t> want_legacy{};
};

/// @brief Drive @p node's `set` hook and @p item's `writeCurrentValue()` **independently**, each
///        from @p sentinel, and report what each left behind.
///
/// Fix round 1, I2. The original shape ran `legacy_write(v)` first and `bindings::write(v)` second,
/// then asserted the second changed nothing -- which a port hook that stores *nothing at all* also
/// satisfies, because legacy had already moved the model to `v`. Here each path starts from a state
/// that is not `v`, so a no-op port hook leaves `got_*` at the sentinel while `want_*` is at `v`.
///
/// @return `ran == false` for an item that holds no value at all (a submenu, or a bespoke one).
WriteProbe probe_write(const Node& node, ::MenuItem* item, const MenuContext& ctx, int32_t v, int32_t sentinel) {
	WriteProbe out;
	if (!legacy_write(item, sentinel)) {
		return out;
	}
	menu::bindings::write(node, ctx, v);
	out.got_port = menu::bindings::read(node, ctx);
	out.got_legacy = legacy_value(item);

	if (!legacy_write(item, sentinel) || !legacy_write(item, v)) {
		return out;
	}
	out.want_port = menu::bindings::read(node, ctx);
	out.want_legacy = legacy_value(item);
	out.ran = true;
	return out;
}

std::array<int32_t, 3> probe_values(const menu::IntRange& r) {
	return {r.min, static_cast<int32_t>((static_cast<int64_t>(r.min) + r.max) / 2), r.max};
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

/// @brief Reach `Range::getText`, which is `protected`.
///
/// The same `[class.protected]` move `tables_2_spec`'s `SubmenuPeek` makes, one class further up:
/// `MultiRange` is `final` so nothing can derive from it, but `getText` is declared on `Range`
/// (`gui/menu_item/range.h:38-39`) and `MultiRange` merely overrides it. A pointer-to-member formed
/// inside a class derived from `Range` therefore has type `void (Range::*)(...)`, applies to the
/// `multiRangeMenu` global, and dispatches virtually to `MultiRange::getText`.
///
/// The alternative -- writing the expected note-range strings out in this file -- would make the
/// spec a second copy of the thing under test rather than a check of it.
struct RangePeek : legacy::Range {
	static std::string text_of_range(legacy::Range& r, int32_t value) {
		static_cast<legacy::MultiRange&>(r).setValue(value);
		char buf[32] = {};
		(r.*(&RangePeek::getText))(buf, nullptr, nullptr, /*mayShowJustOne=*/false); // `multi_range.cpp:401`
		return std::string{buf};
	}
};

/// @brief A synth fixture whose source 0 carries @p n `MultiRange`s, with the `soundEditor` globals
///        pointed at it.
///
/// Its own fixture rather than the shared one: inserting ranges changes what every other example
/// reads off source 0 (`transpose_range`, `sample_start`, ...), and the shared `Fixture` is a
/// function-local static every group in this file uses.
struct RangeFixture {
	spec::SongFixture song = spec::make_synth_song();
	MenuContext ctx{};

	explicit RangeFixture(int32_t n) {
		select_language();
		auto* sound = static_cast<SoundInstrument*>(song.instrument);
		for (int32_t i = 0; i < n; i++) {
			(void)sound->sources[0].ranges.insertMultiRange(i);
		}
		ctx = menu::context_for(*song.song, song.clip, 0, false, false, /*kit_global_fx=*/false);
		currentSong = song.song;
		song.song->setCurrentClip(song.clip);
		soundEditor.currentSound = ctx.sound;
		soundEditor.currentModControllable = ctx.mod;
		soundEditor.currentParamManager = ctx.params;
		soundEditor.currentSourceIndex = ctx.source_index;
		soundEditor.currentSource = ctx.source;
		soundEditor.currentMultiRange = ctx.range;
		soundEditor.currentMultiRangeIndex = ctx.range_index;
	}
	~RangeFixture() {
		// Put the shared fixture's pointers back: `fixture()` is a static and every other group in
		// this file reads through the same `soundEditor` globals.
		(void)fixture();
	}
	RangeFixture(const RangeFixture&) = delete;
	RangeFixture& operator=(const RangeFixture&) = delete;
};

} // namespace

// clang-format off
describe tables_1_range_picker("scenes::menu batch-1 tables: the multi-range picker", $ {
	it("labels every row exactly as MultiRange::getText labels it, on the same fixture", _ {
		// Driven from an independent sentinel on each side: the port reads the ranges out of its own
		// `MenuContext` while legacy reads them out of `soundEditor.currentSource` and its own
		// `Value<int32_t>` cursor, which this sets explicitly per row. A port label built from
		// nothing (an empty string, say) fails against a legacy label that is not empty.
		RangeFixture f{3};
		// Give the three ranges distinct, realistic top notes so the labels are not all the same
		// string -- an all-"top" list would pass a comparison that only ever looked at one arm.
		f.ctx.source->ranges.getElement(0)->topNote = 47;  // B2
		f.ctx.source->ranges.getElement(1)->topNote = 71;  // B4
		f.ctx.source->ranges.getElement(2)->topNote = 32767;

		expect(menu::range_count(f.ctx)).to_equal(int32_t(3));
		size_t compared = 0;
		for (int32_t i = 0; i < 3; i++) {
			char buf[32];
			deluge::ui::shell::TextScratch scratch;
			const std::string got{nodes::range_row_label(f.ctx, i, buf, sizeof(buf), scratch)};
			const std::string want = RangePeek::text_of_range(legacy::multiRangeMenu, i);
			expect(got).to_equal(want);
			compared++;
		}
		expect(compared).to_equal(size_t(3));
		// And they really are three different strings -- so the comparison above is about the labels
		// rather than about one string repeated.
		char b0[32], b1[32], b2[32];
		deluge::ui::shell::TextScratch s0, s1, s2;
		const std::string l0{nodes::range_row_label(f.ctx, 0, b0, sizeof(b0), s0)};
		const std::string l1{nodes::range_row_label(f.ctx, 1, b1, sizeof(b1), s1)};
		const std::string l2{nodes::range_row_label(f.ctx, 2, b2, sizeof(b2), s2)};
		expect(l0 != l1 && l1 != l2 && l0 != l2).to_be_true();
	});

	it("chains to the node it stood in for, and its chain_targets are the range-dependent set", _ {
		// `MultiRange::selectButtonPress` is `return menuItemHeadingTo` (`multi_range.cpp:360-362`),
		// and `menuItemHeadingTo` is what `SoundEditor::setup` wrote there (`:1804`). The port's
		// equivalent pair is `MenuState::pending_after_range` and this hook.
		RangeFixture f{2};
		menu::MenuState st{};
		expect(nodes::multi_range_chain(f.ctx, st) == nullptr).to_be_true(); // nothing pending -> ascend
		st.pending_after_range = &nodes::source0_transpose;
		expect(nodes::multi_range_chain(f.ctx, st) == &nodes::source0_transpose).to_be_true();

		// The span has to be the whole `range_dependent` set, because any one of them can be what
		// `permission_for` refused. Asserted against `nodes::kRangeDependentNodes` itself so the two
		// lists cannot drift (`multi_range.h` says why it is spelled twice).
		expect(nodes::kMultiRangeChain.size()).to_equal(nodes::kRangeDependentNodes.size());
		for (const Node* n : nodes::kRangeDependentNodes) {
			expect(std::find(nodes::kMultiRangeChain.begin(), nodes::kMultiRangeChain.end(), n)
			       != nodes::kMultiRangeChain.end())
			    .to_be_true();
		}
		// And the picker itself is NOT range-dependent -- see `multi_range.h`: it would recurse.
		expect(nodes::multi_range.range_dependent).to_be_false();
	});

	it("getOrCreateFirstRange is run for a zero-range source, which is E1's carried debt", _ {
		// `checkPermissionToBeginSessionForRangeSpecificParam:1938` creates the source's first range
		// before answering anything; `permission_for` is a pure predicate and does not
		// (`context.cpp`'s "Owed to E2"). `menu::ensure_first_range` is where that landed, and
		// `MenuScene::enter_child` is what calls it.
		RangeFixture f{0};
		expect(menu::range_count(f.ctx)).to_equal(int32_t(0));
		expect(f.ctx.range == nullptr).to_be_true();

		expect(menu::ensure_first_range(f.ctx)).to_be_true();
		// One range now exists, and -- the second half of `:1952-1954`'s `*previouslySelectedRange =
		// firstRange` -- the context points at it, so a range-dependent item opens rather than
		// bouncing to a one-row picker.
		expect(menu::range_count(f.ctx)).to_equal(int32_t(1));
		expect(f.ctx.range != nullptr).to_be_true();
		expect(static_cast<int>(f.ctx.range_index)).to_equal(0);
		expect(menu::permission_for(nodes::source0_transpose, f.ctx) == menu::Permission::Yes).to_be_true();
		// `Source::getOrCreateFirstRange` gives the new range the top of the keyboard (`source.cpp:165`).
		expect(menu::range_top_note(f.ctx, 0)).to_equal(int32_t(32767));

		// Idempotent: a second call must not add a second range.
		expect(menu::ensure_first_range(f.ctx)).to_be_true();
		expect(menu::range_count(f.ctx)).to_equal(int32_t(1));
	});

	it("selects a range the way setCurrentMultiRange does, and remembers it on the Source", _ {
		RangeFixture f{3};
		menu::select_range(f.ctx, 2);
		expect(static_cast<int>(f.ctx.range_index)).to_equal(2);
		expect(f.ctx.range == f.ctx.source->ranges.getElement(2)).to_be_true();
		// `multi_range.cpp:239` -- the choice sticks on the source, which is what `on_focus` reads
		// back on the next visit.
		expect(static_cast<int>(f.ctx.source->defaultRangeI)).to_equal(2);
		// Out of range is ignored rather than clamped -- every caller has clamped already.
		menu::select_range(f.ctx, 7);
		expect(static_cast<int>(f.ctx.range_index)).to_equal(2);
	});
});

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
		// Batch 1's own 27, plus the 30 Task 7 added for the rest of the sound-editor root, plus the
		// four Task 8 added for the settings root and the nine others (`relevance.cpp`'s
		// `static_assert` is the authority; this asserts the count is deliberate).
		expect(registry.size()).to_equal(size_t(61));
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
		const MenuContext contexts[] = {f.ctx};
		// `allow_placeholder_custom`: the four bespoke oscillator items point at `kPending` until E2
		// lands the real implementations, and `validate_tree` is strict about that by default
		// (task-7 ruling 3). Standing the rule down here rather than everywhere is the point --
		// the next example asserts strict mode still reports exactly those four.
		const auto issues =
		    menu::validate_tree(nodes::kBatch1Roots, contexts, nodes::kRangeDependentNodes,
		                        nodes::kHalfPrecisionNodes,
		                        menu::ValidateOptions{.allow_placeholder_custom = true,
		                                              .skip_shortcut_sweep = true});
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

	it("reports no placeholder at all any more, even when strict", _ {
		Fixture& f = fixture();
		const MenuContext contexts[] = {f.ctx};
		// This example used to expect **eight** -- one `PlaceholderCustom` per batch-1 node pointing
		// at `kPending`: both file selectors, the four sample loop points and both audio recorders.
		// E3 task 6 gave all eight a real item (`nodes/sample.h`), so the batch-1 tree is now clean
		// under the default (strict) options and the number is zero rather than smaller.
		// `sim/scenes_spec/sample_items_spec.cpp` pins which item each of those nodes got;
		// `shortcuts_spec.cpp` pins the two placeholders left anywhere in the whole tree, neither of
		// which is reachable from a batch-1 root.
		const auto issues =
		    menu::validate_tree(nodes::kBatch1Roots, contexts, nodes::kRangeDependentNodes,
		                        nodes::kHalfPrecisionNodes, menu::ValidateOptions{.skip_shortcut_sweep = true});
		size_t placeholders = 0;
		for (const menu::Issue& i : issues) {
			if (i.kind == menu::Issue::Kind::PlaceholderCustom) {
				placeholders++;
			}
			expect(i.kind != menu::Issue::Kind::NullCustom).to_be_true();
		}
		expect(placeholders).to_equal(size_t(0));
	});

	it("assigns an id to every node in the batch-1 tree", _ {
		Fixture& f = fixture();
		const MenuContext contexts[] = {f.ctx};
		(void)menu::validate_tree(nodes::kBatch1Roots, contexts, {}, {},
		                          menu::ValidateOptions{.skip_shortcut_sweep = true});
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
describe tables_1_writes("scenes::menu batch-1 tables: writes", $ {
	it("leaves the model where the legacy item leaves it, for every set hook", _ {
		// Task 6 had the same gap task 7's review found in batch 2 (review round 1, I5): the spec
		// compared reads, names, params, enums, learn targets and relevance, but never drove a
		// single `set` hook. Every batch-1 hook is driven here, the same way `tables_2_spec` and
		// `tables_3_spec` drive batches 2 and 3: for three values spanning the node's range, each
		// write path is run **from its own copy of a common starting state** (`probe_write`) and the
		// two results are compared through both readers. Fix round 1, I2: the original shape ran
		// legacy first and the port second and asserted the port changed nothing, which a port hook
		// that stores nothing also satisfies.
		//
		// `ParamRef` nodes are not here, for the reason `tables_2_spec` spells out: with no root UI
		// open, legacy's `UnpatchedParam::writeCurrentValue` resolves the *song's* param collection
		// (`sound_editor.cpp:1986`) where the port resolves the clip's. `bindings_spec` covers that
		// path directly.
		Fixture& f = fixture();
		size_t exercised = 0;
		// Fix round 2, I2: a probe count with slack ("> 45") cannot tell a node that left the table
		// from one that never had a hook. What is asserted is the **set of distinct nodes driven**,
		// against a set derived from the table itself -- every node in the walk carrying a `set`
		// hook -- so dropping a node, or a relevance predicate going false in the fixture, fails
		// here rather than being absorbed by the slack. Only `tables_3_spec` pinned a node set
		// before; batches 1 and 2 now do the same.
		std::set<const Node*> covered;
		std::set<const Node*> with_hook;
		for (const Pair& p : all_pairs()) {
			if (p.node->bind.set == nullptr) {
				continue;
			}
			with_hook.insert(p.node);
			const menu::IntRange r = menu::bindings::range(*p.node, f.ctx);
			for (int32_t v : probe_values(r)) {
				const std::optional<int32_t> sentinel = sentinel_for(r, v);
				if (!sentinel.has_value()) {
					continue; // a one-value range has no state to start from that is not `v`
				}
				const WriteProbe probe = probe_write(*p.node, p.item, f.ctx, v, *sentinel);
				if (!probe.ran) {
					break; // holds no value at all
				}
				expect(probe.got_port == probe.want_port).to_be_true();
				expect(probe.got_legacy == probe.want_legacy).to_be_true();
				exercised++;
				covered.insert(p.node);
			}
		}
		// Every batch-1 node with a `set` hook is driven -- no exceptions in this batch.
		expect(covered.size()).to_equal(with_hook.size());
		for (const Node* n : with_hook) {
			expect(covered.contains(n)).to_be_true();
		}
		// ... and each was driven for all three values its range spans.
		expect(exercised).to_equal(size_t(3) * with_hook.size());
	});
});
// clang-format on

/// Six `describe` groups in one TU, so `DELUGE_SPEC` (one spec per entry point) is spelled out
/// here with `add_specs` instead.
int tables_1_spec(int argc, char** const argv) {
	return CppSpec::parse(argc, argv)
	               .add_specs(tables_1_shape, tables_1_values, tables_1_validation, tables_1_pixels, tables_1_writes,
	                          tables_1_range_picker)
	               .exec()
	               .is_success()
	           ? EXIT_SUCCESS
	           : EXIT_FAILURE;
}
