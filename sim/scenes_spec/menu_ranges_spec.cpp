/// @file
/// @brief The three `Kind::Range` items -- `default_tempo`, `default_swing_amount`, `default_key` --
///        against the real legacy `IntegerRange`/`KeyRange` objects they port.
///
/// Both sides are reachable and both sides are **driven** in every comparison below. That is not a
/// formality: this plan has shipped three comparisons that read stale state and would have passed
/// with the port wrong, and a range item is exactly the shape that invites it -- legacy's
/// `IntegerRange::lower`/`upper` and the port's hooks now name the *same* storage
/// (`FlashStorage::defaultRanges`, `storage/flash_storage.h`), so a comparison that forgets to reset
/// between the two halves is comparing a value with itself. Every value example here therefore:
///
///   1. writes a **sentinel** starting state,
///   2. drives *legacy* (`selectEncoderAction` / `horizontalEncoderAction` / `beginSession`) and
///      records what it produced,
///   3. writes the **same sentinel** again, and
///   4. drives the *port* through a real `Shell` and the real input path,
///
/// then compares. `RangeStorageGuard` restores whatever the process had before, so no example leaks
/// into the next one.

#include "scenes/l10n_bridge.h"
#include "scenes/menu/context.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/menu/node.h"
#include "scenes/menu/node_id.h"
#include "scenes/menu/nodes/nodes.h"
#include "scenes/menu/nodes/settings.h"
#include "scenes/profile.h"
#include "scenes/registry.h"

#include "notify/notice_queue.h"
#include "shell/profiles/deluge.h"
#include "shell/shell.h"

#include "gui/l10n/l10n.h"
#include "gui/l10n/strings.h"
#include "gui/menu_item/integer_range.h"
#include "gui/menu_item/key_range.h"
#include "gui/menu_item/range.h"
#include "gui/ui/sound_editor.h"
#include "hid/display/oled.h"
#include "model/song/song.h"
#include "storage/flash_storage.h"

#include "support/song_fixture.h"

#include "spec/gfx/support/golden.h"
#include "spec/gfx/support/pbm.h"
#include "spec/shell/support/fake_surfaces.h"

#include "cppspec.hpp"

#include <array>
#include <cstdint>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "spec/support/spec_main.h"

/// The three real legacy items. Defined at global scope in `gui/ui/menus.cpp:1645-1650`; only
/// `startStemExportMenu`'s neighbours make it into `gui/ui/menus.h`, so these are declared here --
/// the situation `tables_3_spec.cpp:589` already records for the other file-scope menu globals.
extern deluge::gui::menu_item::IntegerRange defaultTempoMenu;
extern deluge::gui::menu_item::IntegerRange defaultSwingAmountMenu;
extern deluge::gui::menu_item::KeyRange defaultKeyMenu;

using namespace deluge::scenes;
using deluge::gui::menu_item::RangeEdit;
using deluge::l10n::String;
using deluge::scenes::menu::MenuContext;
using deluge::scenes::menu::Node;
using deluge::ui::oled::RangeEdge;
using deluge::ui::shell::BoardProfile;
using deluge::ui::shell::SceneId;
using deluge::ui::shell::SceneKind;
using deluge::ui::shell::Shell;
using deluge::ui::shell::ShellConfig;

namespace {

namespace nodes = deluge::scenes::menu::nodes;

void select_language() {
	deluge::l10n::chosenLanguage = &deluge::l10n::built_in::english;
	deluge::notify::Notice discard;
	while (deluge::notify::notices().pop(discard)) {}
}

/// Board encoder indices (`kDelugeEncoders`, `shell/profiles/deluge.cpp:69-76`).
constexpr uint8_t kHorizontalEncoder = 1;
constexpr uint8_t kSelectEncoder = 3;
constexpr uint8_t kGoldUpper = 4;
constexpr uint8_t kGoldLower = 5;

constexpr const Node* kRoots[] = {&nodes::root_settings};

/// @brief Saves and restores every `FlashStorage::defaultRanges` slot, plus the two globals the
///        comparisons steer.
///
/// `defaultUseSharps` decides which note table `KeyRange::getText` and `format_range_key` read;
/// `soundEditor.editingRangeEdge` is legacy's single global edge. An example that moved either and
/// did not put it back would steer the next one.
struct RangeStorageGuard {
	std::array<FlashStorage::DefaultRange, FlashStorage::NUM_DEFAULT_RANGE_SLOTS> saved{};
	bool saved_sharps = FlashStorage::defaultUseSharps;
	RangeEdit saved_edge = soundEditor.editingRangeEdge;

	RangeStorageGuard() {
		for (size_t i = 0; i < saved.size(); i++) {
			saved[i] = FlashStorage::defaultRanges[i];
		}
	}
	RangeStorageGuard(const RangeStorageGuard&) = delete;
	RangeStorageGuard& operator=(const RangeStorageGuard&) = delete;
	~RangeStorageGuard() {
		for (size_t i = 0; i < saved.size(); i++) {
			FlashStorage::defaultRanges[i] = saved[i];
		}
		FlashStorage::defaultUseSharps = saved_sharps;
		soundEditor.editingRangeEdge = saved_edge;
	}
};

struct Rig {
	spec::SongFixture song = spec::make_synth_song();
	const BoardProfile& profile;
	deluge::ui::shell::SceneRegistry reg = scene_registry();
	uitest::FakeSurfaceSet fake;
	Shell shell{fake, profile, reg, deluge::ui::shell::Services{}, ShellConfig{}};

	Rig(const Rig&) = delete;
	Rig& operator=(const Rig&) = delete;

	/// @brief Tear the stack down, so every scene's `on_exit` always runs (E3 task 10).
	///
	/// `MenuScene` is a process singleton (`shell/scene.h`'s `SceneState` cache): an example that
	/// left it -- or anything pushed above it -- on the stack would make the NEXT example's
	/// `on_enter` see stale state instead of a fresh one, the cross-contamination
	/// `recorder_scene_spec.cpp`'s `~Rig` was written to stop (a leaked recorder there made the next
	/// `on_enter` refuse outright and killed 13 of 16 examples on one mutant's first run). Popping
	/// unconditionally means every example starts from the same place whatever the one before it did.
	~Rig() {
		while (shell.stack().depth() > 0) {
			shell.stack().pop(SceneKind::Null);
		}
	}

	explicit Rig(const DelugeBoard& board = deluge::ui::shell::kDelugeBoard, const BoardProfile& p = deluge_profile())
	    : profile(p), fake(board, p) {}

	MenuContext open(const Node& root) {
		const MenuContext ctx =
		    menu::context_for(*song.song, song.clip, 0, false, false, false, /*kit_global_fx=*/false);
		(void)menu::assign_node_ids(kRoots);
		menu_scene().open(root, ctx);
		shell.init(SceneId{SceneKind::FirstApp, menu::node_id(root)});
		return ctx;
	}

	void edges(uint8_t index, int16_t count, uint32_t at_ms) {
		shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_ENCODER, index, 0, count}, at_ms);
	}
	/// One detent of a *detented* encoder is two A-pin edges (`shell/input_lowering.h:28`).
	void detent(uint8_t index, int32_t detents, uint32_t at_ms) {
		edges(index, static_cast<int16_t>(detents * 2), at_ms);
	}
	/// A gold knob is `EncoderKind::Accelerating`, so the raw edge count IS the magnitude. Keep the
	/// clock under 500 ms or `InputLowering::action_gold_turn` swallows the first turn as a wiggle.
	void gold(uint8_t index, int16_t delta, uint32_t at_ms) { edges(index, delta, at_ms); }
};

/// @brief A 128x64 single-panel board, so the same scene is recorded at the other shipped panel
///        geometry too -- `menu_scene_spec.cpp:293-314`'s helpers, and the same ruling: the 48/64
///        split comes from `BoardProfile::style_by_index`, not from a switch inside the scene.
constexpr DelugeDisplayDesc kDisplays64[] = {{128, 64, 0}};

DelugeBoard board_64() {
	DelugeBoard b = deluge::ui::shell::kDelugeBoard;
	b.display_height = 64;
	b.display_count = 1;
	b.displays = kDisplays64;
	return b;
}

const BoardProfile& profile_64() {
	static const BoardProfile p = [] {
		static constexpr const deluge::ui::Style* kStyles[] = {&deluge::ui::kDelugeStyle64};
		BoardProfile q = deluge_profile();
		q.style_by_index = kStyles;
		return q;
	}();
	return p;
}

menu::MenuScene& scene() {
	return menu_scene();
}

/// @brief Which `FlashStorage::defaultRanges` slot a node addresses. Read through the node's own
///        hooks so this cannot disagree with the table.
FlashStorage::DefaultRangeSlot slot_of(const Node& node) {
	// Probe: park a unique value in each slot's lower field, ask the node, and see which one it
	// answered with. Beats a hand-written node -> slot table, which is the thing under test.
	RangeStorageGuard guard;
	const MenuContext ctx{};
	for (uint8_t i = 0; i < FlashStorage::NUM_DEFAULT_RANGE_SLOTS; i++) {
		FlashStorage::defaultRanges[i].lower = 1000 + i;
	}
	const int32_t answer = node.bind.get(ctx);
	return static_cast<FlashStorage::DefaultRangeSlot>(answer - 1000);
}

/// @brief `OLED::main`, into which every legacy `renderOLED()` draws, as a PBM.
std::string legacy_panel_pbm() {
	const auto& canvas = deluge::hid::display::OLED::main;
	return page_major_to_pbm(canvas.bytes().data(), OLED_MAIN_WIDTH_PIXELS, OLED_MAIN_HEIGHT_PIXELS);
}

/// @brief The rows the port's root frame can actually ink on the 48-row panel.
///
/// The Deluge panel has a five-row bezel (`kDelugeBoard`'s `DelugeDisplayDesc{128, 48, 5}`), and the
/// port's root frame has its origin at `visible_top`, so anything a legacy routine inks above row 5
/// is clipped away here. Every comparison below drops those five rows from *both* sides, exactly as
/// `context_menu_scene_spec.cpp` does and for the same reason.
std::string below_bezel(const std::string& pbm) {
	// A PBM from `page_major_to_pbm` is "P1\n<w> <h>\n" then one line per row.
	size_t pos = 0;
	int newlines = 0;
	while (pos < pbm.size() && newlines < 2) {
		if (pbm[pos] == '\n') {
			newlines++;
		}
		pos++;
	}
	std::string out = pbm.substr(0, pos);
	// Skip five row lines.
	for (int i = 0; i < 5 && pos < pbm.size(); i++) {
		const size_t next = pbm.find('\n', pos);
		pos = next == std::string::npos ? pbm.size() : next + 1;
	}
	out += pbm.substr(pos);
	return out;
}

/// @brief What the port blitted to the Main surface, as a PBM.
std::string port_panel_pbm(Rig& r) {
	return r.fake.pbm(0);
}

/// @brief One driven comparison of an edit: the same start, the same detents, both sides.
struct EditCase {
	const char* what;
	int32_t start_lower;
	int32_t start_upper;
	RangeEdit edge;
	int32_t detents; // one select detent == `Range::step`, which is 1 for all three items
};

} // namespace

// clang-format off
describe menu_range_tables("scenes::menu Kind::Range -- the table", $ {
	it("has exactly three consumers in the whole tree, by identity", _ {
		// Spec E §4's threshold is "a kind exists only if at least 3 items use it", and this kind was
		// deleted once for having none. Three is what brought it back, so three is what the tree must
		// hold -- asserted as a *set of nodes*, not a count with slack: a fourth range item added
		// without revisiting the rule fails here, and so does one of these three quietly reverting to
		// `Kind::Custom`.
		select_language();
		const uint16_t assigned = menu::assign_node_ids(nodes::kAllRoots);
		std::set<const Node*> found;
		for (uint16_t id = 1; id <= assigned; id++) {
			const Node* n = menu::node_by_id(id);
			if (n != nullptr && n->kind == menu::Kind::Range) {
				found.insert(n);
			}
		}
		const std::set<const Node*> expected{&nodes::default_tempo, &nodes::default_swing_amount,
		                                     &nodes::default_key};
		expect(found == expected).to_be_true();
		expect(static_cast<int>(found.size())).to_equal(3);
	});

	it("gives each node a spec, and points each at its own storage slot", _ {
		// The lookalike-mapping trap: three nodes, three near-identical specs. Probed, not asserted
		// from a hand-written table -- and then the probe result is checked against a single
		// transposition of itself, so an all-zero answer cannot pass.
		const Node* order[] = {&nodes::default_tempo, &nodes::default_swing_amount, &nodes::default_key};
		std::vector<int> observed;
		for (const Node* n : order) {
			expect(n->bind.range_spec != nullptr).to_be_true();
			expect(n->bind.range_spec->format != nullptr).to_be_true();
			expect(n->bind.range_spec->get_upper != nullptr).to_be_true();
			expect(n->bind.range_spec->set_upper != nullptr).to_be_true();
			observed.push_back(static_cast<int>(slot_of(*n)));
		}
		const std::vector<int> expected{FlashStorage::DEFAULT_RANGE_TEMPO, FlashStorage::DEFAULT_RANGE_SWING_AMOUNT,
		                                FlashStorage::DEFAULT_RANGE_KEY};
		expect(observed).to_equal(expected);
		std::vector<int> swapped = observed;
		std::swap(swapped[0], swapped[2]);
		expect(swapped == expected).to_be_false();
	});

	it("takes its bounds from the real legacy objects", _ {
		// `IntegerRange`'s bounds are constructor arguments and are readable; `KeyRange` has none at
		// all -- `selectEncoderAction` uses two locals (`key_range.cpp:29-30`) -- so that one is
		// transcribed and said to be.
		expect(nodes::kDefaultTempoRange.min).to_equal(defaultTempoMenu.minValue);
		expect(nodes::kDefaultTempoRange.max).to_equal(defaultTempoMenu.maxValue);
		expect(nodes::kDefaultSwingAmountRange.min).to_equal(defaultSwingAmountMenu.minValue);
		expect(nodes::kDefaultSwingAmountRange.max).to_equal(defaultSwingAmountMenu.maxValue);
		expect(nodes::kDefaultKeyRange.min).to_equal(0);   // KEY_MIN, `key_range.cpp:29`
		expect(nodes::kDefaultKeyRange.max).to_equal(11);  // KEY_MAX, `key_range.cpp:30`
	});

	it("shares its storage with the legacy items, in both directions", _ {
		// The premise every comparison below rests on. Checked *both ways* so a one-way alias (a copy
		// made at construction, say) cannot pass.
		RangeStorageGuard guard;
		const MenuContext ctx{};
		FlashStorage::defaultRanges[FlashStorage::DEFAULT_RANGE_TEMPO] = {.lower = 77, .upper = 188};
		expect(defaultTempoMenu.lower).to_equal(77);
		expect(defaultTempoMenu.upper).to_equal(188);
		expect(nodes::default_tempo.bind.get(ctx)).to_equal(77);
		expect(nodes::default_tempo.bind.range_spec->get_upper(ctx)).to_equal(188);

		defaultTempoMenu.lower = 61;
		defaultTempoMenu.upper = 239;
		expect(nodes::default_tempo.bind.get(ctx)).to_equal(61);
		expect(nodes::default_tempo.bind.range_spec->get_upper(ctx)).to_equal(239);

		nodes::default_tempo.bind.set(ctx, 100);
		nodes::default_tempo.bind.range_spec->set_upper(ctx, 200);
		expect(defaultTempoMenu.lower).to_equal(100);
		expect(defaultTempoMenu.upper).to_equal(200);
	});
});

describe menu_range_text("scenes::menu Kind::Range -- the spellings", $ {
	it("spells an integer end exactly as IntegerRange::getText does, over the whole range", _ {
		RangeStorageGuard guard;
		char scratch[deluge::ui::oled::kRangeTextMax];
		for (int32_t v = defaultTempoMenu.minValue; v <= defaultTempoMenu.maxValue; v++) {
			// Legacy is *driven*: `getText` reads `lower`, so the value has to be installed first.
			defaultTempoMenu.lower = v;
			defaultTempoMenu.upper = v;
			char legacy_buf[64] = {};
			int32_t left_len = 0, right_len = 0;
			defaultTempoMenu.getText(legacy_buf, &left_len, &right_len, /*mayShowJustOne=*/true);
			expect(right_len).to_equal(0);

			const std::string_view port = nodes::format_range_integer(v, std::span{scratch});
			expect(std::string(port)).to_equal(std::string(legacy_buf, static_cast<size_t>(left_len)));
		}
	});

	it("spells a note end exactly as KeyRange::getText does, in both sharps and flats", _ {
		RangeStorageGuard guard;
		char scratch[deluge::ui::oled::kRangeTextMax];
		for (const bool sharps : {true, false}) {
			FlashStorage::defaultUseSharps = sharps;
			for (int32_t v = 0; v <= 11; v++) {
				defaultKeyMenu.lower = v;
				defaultKeyMenu.upper = v;
				char legacy_buf[64] = {};
				int32_t left_len = 0, right_len = 0;
				defaultKeyMenu.getText(legacy_buf, &left_len, &right_len, /*mayShowJustOne=*/true);

				const std::string_view port = nodes::format_range_key(v, std::span{scratch});
				expect(std::string(port)).to_equal(std::string(legacy_buf, static_cast<size_t>(left_len)));
			}
		}
		// And the two tables really do differ, so the loop above is not comparing one table with
		// itself: D# and Eb are the same note code and different spellings.
		FlashStorage::defaultUseSharps = true;
		const std::string sharp(nodes::format_range_key(3, std::span{scratch}));
		FlashStorage::defaultUseSharps = false;
		const std::string flat(nodes::format_range_key(3, std::span{scratch}));
		expect(sharp == flat).to_be_false();
	});
});

describe menu_range_edit("scenes::menu Kind::Range -- editing, against legacy", $ {
	it("moves both ends exactly as IntegerRange::selectEncoderAction does", _ {
		select_language();
		const EditCase cases[] = {
		    {"coincident, no edge: moves as one", 120, 120, RangeEdit::OFF, 5},
		    {"split, no edge: refuses to move", 90, 150, RangeEdit::OFF, 5},
		    {"left edge up drags the upper", 120, 120, RangeEdit::LEFT, 7},
		    {"left edge down leaves the upper", 120, 150, RangeEdit::LEFT, -7},
		    {"right edge down drags the lower", 120, 120, RangeEdit::RIGHT, -7},
		    {"right edge clamps at maxValue", 200, 238, RangeEdit::RIGHT, 9},
		    {"left edge clamps at minValue", 62, 200, RangeEdit::LEFT, -9},
		};
		for (const EditCase& c : cases) {
			RangeStorageGuard guard;

			// --- legacy, driven from the sentinel
			FlashStorage::defaultRanges[FlashStorage::DEFAULT_RANGE_TEMPO] = {.lower = c.start_lower,
			                                                                 .upper = c.start_upper};
			soundEditor.editingRangeEdge = c.edge;
			defaultTempoMenu.selectEncoderAction(c.detents);
			const int32_t legacy_lower = defaultTempoMenu.lower;
			const int32_t legacy_upper = defaultTempoMenu.upper;

			// --- port, driven from the SAME sentinel, through the real input path
			FlashStorage::defaultRanges[FlashStorage::DEFAULT_RANGE_TEMPO] = {.lower = c.start_lower,
			                                                                 .upper = c.start_upper};
			Rig r;
			(void)r.open(nodes::default_tempo);
			r.shell.tick(5);
			// The port's edge is `MenuState::edit_edge`; steer it with the horizontal encoder, which
			// is what a user has. `enter_leaf` seeds LEFT on a split pair, so ask for OFF explicitly
			// where the case wants it.
			uint32_t at = 10;
			for (int guard_count = 0; guard_count < 4 && scene().state().edit_edge != static_cast<RangeEdge>(c.edge);
			     guard_count++) {
				r.detent(kHorizontalEncoder, c.edge == RangeEdit::RIGHT ? 1 : -1, at);
				r.shell.tick(at + 5);
				at += 10;
			}
			expect(static_cast<int>(scene().state().edit_edge)).to_equal(static_cast<int>(c.edge));

			// `range_value` edits the selected edge on the select encoder now, as legacy's
			// `IntegerRange::selectEncoderAction` does (`ui/oled/values.cpp`'s `select_delta`) -- so
			// this is driven on `kSelectEncoder`, not `kGoldUpper`. `detent()` already multiplies by
			// `edges_per_detent` (2), so `c.detents` select detents is exactly the offset
			// `defaultTempoMenu.selectEncoderAction(c.detents)` was called with above.
			r.detent(kSelectEncoder, c.detents, at);
			r.shell.tick(at + 5);

			expect(FlashStorage::defaultRanges[FlashStorage::DEFAULT_RANGE_TEMPO].lower).to_equal(legacy_lower);
			expect(FlashStorage::defaultRanges[FlashStorage::DEFAULT_RANGE_TEMPO].upper).to_equal(legacy_upper);
			// ... and the case actually moved something on at least one side, or it proves nothing.
			const bool moved = legacy_lower != c.start_lower || legacy_upper != c.start_upper;
			expect(moved).to_equal(c.detents != 0 && !(c.edge == RangeEdit::OFF && c.start_lower != c.start_upper));
		}
	});

	it("moves both ends exactly as KeyRange::selectEncoderAction does", _ {
		select_language();
		const EditCase cases[] = {
		    {"coincident, no edge", 5, 5, RangeEdit::OFF, 3},
		    {"split, no edge: refuses", 2, 9, RangeEdit::OFF, 3},
		    {"left edge stops at the upper", 5, 5, RangeEdit::LEFT, 4},
		    {"left edge walks up to the upper", 2, 9, RangeEdit::LEFT, 4},
		    {"right edge stops at the lower", 2, 9, RangeEdit::RIGHT, -10},
		    {"right edge clamps at KEY_MAX", 0, 10, RangeEdit::RIGHT, 5},
		};
		for (const EditCase& c : cases) {
			RangeStorageGuard guard;

			FlashStorage::defaultRanges[FlashStorage::DEFAULT_RANGE_KEY] = {.lower = c.start_lower,
			                                                               .upper = c.start_upper};
			soundEditor.editingRangeEdge = c.edge;
			defaultKeyMenu.selectEncoderAction(c.detents);
			const int32_t legacy_lower = defaultKeyMenu.lower;
			const int32_t legacy_upper = defaultKeyMenu.upper;

			FlashStorage::defaultRanges[FlashStorage::DEFAULT_RANGE_KEY] = {.lower = c.start_lower,
			                                                               .upper = c.start_upper};
			Rig r;
			(void)r.open(nodes::default_key);
			r.shell.tick(5);
			uint32_t at = 10;
			for (int guard_count = 0; guard_count < 4 && scene().state().edit_edge != static_cast<RangeEdge>(c.edge);
			     guard_count++) {
				r.detent(kHorizontalEncoder, c.edge == RangeEdit::RIGHT ? 1 : -1, at);
				r.shell.tick(at + 5);
				at += 10;
			}
			expect(static_cast<int>(scene().state().edit_edge)).to_equal(static_cast<int>(c.edge));

			// As the `IntegerRange` block above: driven on `kSelectEncoder`, matching legacy's
			// `KeyRange::selectEncoderAction` now that `range_value` reads the select encoder.
			r.detent(kSelectEncoder, c.detents, at);
			r.shell.tick(at + 5);

			expect(FlashStorage::defaultRanges[FlashStorage::DEFAULT_RANGE_KEY].lower).to_equal(legacy_lower);
			expect(FlashStorage::defaultRanges[FlashStorage::DEFAULT_RANGE_KEY].upper).to_equal(legacy_upper);
		}
	});

	it("the two clamp rules really do differ, so the parameter is doing work", _ {
		// The falsification: run the *same* start and the *same* detent through both legacy items and
		// require the answers to disagree. If they agreed, `RangeClamp` would be untested by
		// construction and either value would pass both loops above.
		RangeStorageGuard guard;
		FlashStorage::defaultRanges[FlashStorage::DEFAULT_RANGE_TEMPO] = {.lower = 5, .upper = 5};
		FlashStorage::defaultRanges[FlashStorage::DEFAULT_RANGE_KEY] = {.lower = 5, .upper = 5};
		soundEditor.editingRangeEdge = RangeEdit::LEFT;
		defaultTempoMenu.selectEncoderAction(1);   // Push: 5-5 -> 6-6 ... but 5 is below minValue 60,
		defaultKeyMenu.selectEncoderAction(1);     // Bound: 5-5 -> 5-5
		// `IntegerRange` clamps into [60, 240] first, so the left end lands on 60 and drags the upper.
		expect(defaultTempoMenu.lower).to_equal(60);
		expect(defaultTempoMenu.upper).to_equal(60);
		expect(defaultKeyMenu.lower).to_equal(5);
		expect(defaultKeyMenu.upper).to_equal(5);
		expect(defaultTempoMenu.lower == defaultKeyMenu.lower).to_be_false();
	});

	it("selects an edge exactly as Range::horizontalEncoderAction does", _ {
		select_language();
		struct Step {
			RangeEdit from;
			int32_t horizontal;
			RangeEdit to;
		};
		const Step steps[] = {
		    {RangeEdit::OFF, -1, RangeEdit::LEFT},   {RangeEdit::OFF, 1, RangeEdit::RIGHT},
		    {RangeEdit::LEFT, -1, RangeEdit::OFF},   {RangeEdit::LEFT, 1, RangeEdit::RIGHT},
		    {RangeEdit::RIGHT, 1, RangeEdit::OFF},   {RangeEdit::RIGHT, -1, RangeEdit::LEFT},
		};
		for (const Step& step : steps) {
			RangeStorageGuard guard;

			// legacy
			soundEditor.editingRangeEdge = step.from;
			defaultTempoMenu.horizontalEncoderAction(step.horizontal);
			const RangeEdit legacy_edge = soundEditor.editingRangeEdge;
			expect(static_cast<int>(legacy_edge)).to_equal(static_cast<int>(step.to));

			// port, from the same starting edge, through the real horizontal encoder
			FlashStorage::defaultRanges[FlashStorage::DEFAULT_RANGE_TEMPO] = {.lower = 90, .upper = 150};
			Rig r;
			(void)r.open(nodes::default_tempo);
			r.shell.tick(5);
			uint32_t at = 10;
			for (int i = 0; i < 4 && scene().state().edit_edge != static_cast<RangeEdge>(step.from); i++) {
				r.detent(kHorizontalEncoder, step.from == RangeEdit::RIGHT ? 1 : -1, at);
				r.shell.tick(at + 5);
				at += 10;
			}
			expect(static_cast<int>(scene().state().edit_edge)).to_equal(static_cast<int>(step.from));
			r.detent(kHorizontalEncoder, step.horizontal, at);
			r.shell.tick(at + 5);
			expect(static_cast<int>(scene().state().edit_edge)).to_equal(static_cast<int>(legacy_edge));
		}
	});

	it("seeds the edge on entry exactly as each item's beginSession does", _ {
		select_language();
		// `IntegerRange::beginSession` takes LEFT when the ends differ (`integer_range.cpp:26-31`);
		// `KeyRange` has no override, so `Range::beginSession`'s bare OFF stands (`range.cpp:30-33`).
		// Both sides driven, per item, for both a split and a coincident pair.
		struct Seed {
			const Node* node;
			FlashStorage::DefaultRangeSlot slot;
			bool integer;
			int32_t lower;
			int32_t upper;
		};
		const Seed seeds[] = {
		    {&nodes::default_tempo, FlashStorage::DEFAULT_RANGE_TEMPO, true, 120, 120},
		    {&nodes::default_tempo, FlashStorage::DEFAULT_RANGE_TEMPO, true, 90, 150},
		    {&nodes::default_key, FlashStorage::DEFAULT_RANGE_KEY, false, 5, 5},
		    {&nodes::default_key, FlashStorage::DEFAULT_RANGE_KEY, false, 2, 9},
		};
		for (const Seed& seed : seeds) {
			RangeStorageGuard guard;

			FlashStorage::defaultRanges[seed.slot] = {.lower = seed.lower, .upper = seed.upper};
			soundEditor.editingRangeEdge = RangeEdit::RIGHT; // a value neither arm can produce
			if (seed.integer) {
				defaultTempoMenu.beginSession(nullptr);
			}
			else {
				defaultKeyMenu.beginSession(nullptr);
			}
			const RangeEdit legacy_edge = soundEditor.editingRangeEdge;

			FlashStorage::defaultRanges[seed.slot] = {.lower = seed.lower, .upper = seed.upper};
			Rig r;
			(void)r.open(*seed.node);
			r.shell.tick(5);
			expect(static_cast<int>(scene().state().edit_edge)).to_equal(static_cast<int>(legacy_edge));
		}
		// ... and the two seeds differ on the split pair, so the loop is not vacuous.
		expect(nodes::default_tempo.bind.range_spec->focus_left_when_split).to_be_true();
		expect(nodes::default_key.bind.range_spec->focus_left_when_split).to_be_false();
	});
});

describe menu_range_pixels("scenes::menu Kind::Range -- pixels, against legacy", $ {
	it("draws exactly what the real legacy Range::renderOLED draws, in every edge state", _ {
		select_language();
		struct Shot {
			const char* name;
			int32_t lower;
			int32_t upper;
			RangeEdit edge;
		};
		const Shot shots[] = {
		    {"point", 120, 120, RangeEdit::OFF},
		    {"split", 90, 150, RangeEdit::OFF},
		    {"left", 90, 150, RangeEdit::LEFT},
		    {"right", 90, 150, RangeEdit::RIGHT},
		};
		for (const Shot& shot : shots) {
			RangeStorageGuard guard;

			// --- legacy, into its own canvas
			FlashStorage::defaultRanges[FlashStorage::DEFAULT_RANGE_TEMPO] = {.lower = shot.lower,
			                                                                 .upper = shot.upper};
			soundEditor.editingRangeEdge = shot.edge;
			deluge::hid::display::OLED::main.clear();
			defaultTempoMenu.renderOLED();
			const std::string legacy = below_bezel(legacy_panel_pbm());

			// --- port, same state, through the real Shell
			FlashStorage::defaultRanges[FlashStorage::DEFAULT_RANGE_TEMPO] = {.lower = shot.lower,
			                                                                 .upper = shot.upper};
			Rig r;
			(void)r.open(nodes::default_tempo);
			r.shell.tick(5);
			uint32_t at = 10;
			for (int i = 0; i < 4 && scene().state().edit_edge != static_cast<RangeEdge>(shot.edge); i++) {
				r.detent(kHorizontalEncoder, shot.edge == RangeEdit::RIGHT ? 1 : -1, at);
				r.shell.tick(at + 5);
				at += 10;
			}
			r.shell.tick(at + 10);
			const std::string port = below_bezel(port_panel_pbm(r));

			expect(port).to_equal(legacy);
		}
	});

	it("records the goldens at both panel geometries", _ {
		select_language();
		{
			RangeStorageGuard guard;
			FlashStorage::defaultRanges[FlashStorage::DEFAULT_RANGE_TEMPO] = {.lower = 90, .upper = 150};
			Rig r;
			(void)r.open(nodes::default_tempo);
			r.shell.tick(5);   // enter_leaf seeds LEFT on a split pair
			r.shell.tick(15);
			expect_golden(self, "../ui/menu/menu_default_tempo_48.pbm", r.fake.pbm(0));

			Rig r64{board_64(), profile_64()};
			FlashStorage::defaultRanges[FlashStorage::DEFAULT_RANGE_TEMPO] = {.lower = 90, .upper = 150};
			(void)r64.open(nodes::default_tempo);
			r64.shell.tick(5);
			r64.shell.tick(15);
			expect_golden(self, "../ui/menu/menu_default_tempo_64.pbm", r64.fake.pbm(0));
		}
		{
			RangeStorageGuard guard;
			FlashStorage::defaultRanges[FlashStorage::DEFAULT_RANGE_KEY] = {.lower = 1, .upper = 4};
			FlashStorage::defaultUseSharps = true;
			Rig k;
			(void)k.open(nodes::default_key);
			k.shell.tick(5);
			k.shell.tick(15);
			expect_golden(self, "../ui/menu/menu_default_key_48.pbm", k.fake.pbm(0));

			Rig k64{board_64(), profile_64()};
			FlashStorage::defaultRanges[FlashStorage::DEFAULT_RANGE_KEY] = {.lower = 1, .upper = 4};
			(void)k64.open(nodes::default_key);
			k64.shell.tick(5);
			k64.shell.tick(15);
			expect_golden(self, "../ui/menu/menu_default_key_64.pbm", k64.fake.pbm(0));
		}
	});

	it("is byte-identical after twenty quiet ticks, and moves no value", _ {
		select_language();
		RangeStorageGuard guard;
		FlashStorage::defaultRanges[FlashStorage::DEFAULT_RANGE_TEMPO] = {.lower = 90, .upper = 150};
		Rig r;
		(void)r.open(nodes::default_tempo);
		r.shell.tick(5);
		const std::string first = port_panel_pbm(r);
		for (uint32_t t = 1; t <= 20; t++) {
			r.shell.tick(5 + t * 15);
			expect(port_panel_pbm(r)).to_equal(first);
		}
		expect(FlashStorage::defaultRanges[FlashStorage::DEFAULT_RANGE_TEMPO].lower).to_equal(90);
		expect(FlashStorage::defaultRanges[FlashStorage::DEFAULT_RANGE_TEMPO].upper).to_equal(150);
		expect(scene().next_timer().has_value()).to_be_false();
	});
});
// clang-format on

/// Four `describe` groups in one TU, so `DELUGE_SPEC` is spelled out here with `add_specs`.
int menu_ranges_spec(int argc, char** const argv) {
	return CppSpec::parse(argc, argv)
	               .add_specs(menu_range_tables, menu_range_text, menu_range_edit, menu_range_pixels)
	               .exec()
	               .is_success()
	           ? EXIT_SUCCESS
	           : EXIT_FAILURE;
}
