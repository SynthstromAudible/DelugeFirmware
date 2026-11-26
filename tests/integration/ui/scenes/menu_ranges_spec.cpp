/// @file
/// @brief The three `Kind::Range` items -- `default_tempo`, `default_swing_amount`, `default_key` -- against the
///        reference rows `tests/golden/ui/menu_oracle/menu_ranges.tsv` holds for main's `IntegerRange` and `KeyRange`.
///
/// Every value example drives the **port** through a real `Shell` and the real input path from a **sentinel** starting
/// state in `FlashStorage::defaultRanges`, then compares what it left with the frozen answer main gave for the same
/// start, edge and turn. `RangeStorageGuard` restores whatever the process had before, so no example leaks into the
/// next.

#include "gui/ui/menu_view_of.h"
#include "gui/ui/shell_host/host_ports.h"
#include "ui/scenes/l10n_bridge.h"
#include "ui/scenes/menu/bindings.h"
#include "ui/scenes/menu/context.h"
#include "ui/scenes/menu/menu_scene.h"
#include "ui/scenes/menu/node.h"
#include "ui/scenes/menu/node_id.h"
#include "ui/scenes/menu/nodes/nodes.h"
#include "ui/scenes/menu/nodes/settings.h"
#include "ui/scenes/profile.h"
#include "ui/scenes/registry.h"

#include "base/notify/notice_queue.h"
#include "ui/shell/profiles/deluge.h"
#include "ui/shell/shell.h"

#include "gui/l10n/l10n.h"
#include "gui/l10n/strings.h"
#include "hid/display/oled.h"
#include "model/song/song.h"
#include "storage/flash_storage.h"

#include "support/oracle_table.h"
#include "support/song_fixture.h"

#include "support/mocks/fake_surfaces.h"
#include "unit/ui/gfx/support/golden.h"
#include "unit/ui/gfx/support/pbm.h"

#include "cppspec.hpp"

#include <array>
#include <cstdint>
#include <fstream>
#include <set>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "support/menu_context.h"
#include "support/spec_main.h"

using namespace deluge::ui::scenes;
using deluge::l10n::String;
using deluge::ui::scenes::menu::MenuContext;
using deluge::ui::scenes::menu::Node;
using deluge::ui::shell::BoardProfile;
using deluge::ui::shell::SceneId;
using deluge::ui::shell::SceneKind;
using deluge::ui::shell::Shell;
using deluge::ui::shell::ShellConfig;
using deluge::ui::widgets::oled::RangeEdge;

namespace {

namespace nodes = deluge::ui::scenes::menu::nodes;

void select_language() {
	deluge::l10n::chosenLanguage = &deluge::l10n::built_in::english;
	deluge::base::notify::Notice discard;
	while (deluge::base::notify::notices().pop(discard)) {}
}

/// Board encoder indices (`kDelugeEncoders`).
constexpr uint8_t kHorizontalEncoder = 1;
constexpr uint8_t kSelectEncoder = 3;
constexpr uint8_t kGoldUpper = 4;
constexpr uint8_t kGoldLower = 5;

constexpr const Node* kRoots[] = {&nodes::root_settings};

/// @brief A context over the host's ports alone: what the range nodes read their DEVICE rows through.
MenuContext host_context() {
	MenuContext ctx{};
	ctx.ports = deluge::gui::shell_host::host_ports();
	return ctx;
}

/// @brief Saves and restores every `FlashStorage::defaultRanges` slot, plus the global the comparisons steer.
///
/// `defaultUseSharps` decides which note table `format_range_key` reads. An example that moved either and did not put
/// it back would steer the next one.
struct RangeStorageGuard {
	std::array<FlashStorage::DefaultRange, FlashStorage::NUM_DEFAULT_RANGE_SLOTS> saved{};
	bool saved_sharps = FlashStorage::defaultUseSharps;

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
			shell.stack().pop(SceneKind::Null);
		}
		menu_scene().reset_process_state(); // each example starts with the rows a boot has
	}

	explicit Rig(const DelugeBoard& board = deluge::ui::shell::kDelugeBoard, const BoardProfile& p = deluge_profile())
	    : profile(p), fake(board, p) {}

	MenuContext open(const Node& root) {
		const MenuContext ctx = spec::ctx_of(*song.song, deluge::gui::menuViewOf(song.clip), 0, false, false, false,
		                                     /*kit_global_fx=*/false);
		(void)menu::assign_node_ids(kRoots);
		menu_scene().open(root, ctx);
		shell.init(SceneId{SceneKind::FirstApp, menu::node_id(root)});
		return ctx;
	}

	void edges(uint8_t index, int16_t count, uint32_t at_ms) {
		shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_ENCODER, index, 0, count}, at_ms);
	}
	/// One detent of a *detented* encoder is two A-pin edges (`LoweringConfig::edges_per_detent`).
	void detent(uint8_t index, int32_t detents, uint32_t at_ms) {
		edges(index, static_cast<int16_t>(detents * 2), at_ms);
	}
	/// A gold knob is `EncoderKind::Accelerating`, so the raw edge count IS the magnitude.
	void gold(uint8_t index, int16_t delta, uint32_t at_ms) { edges(index, delta, at_ms); }
};

/// @brief A 128x64 single-panel board, so the same scene is recorded at the other shipped panel
///        geometry too -- `menu_scene_spec.cpp`'s `board_64` helpers, and the same rule: the 48/64
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
		static constexpr const deluge::ui::widgets::core::Style* kStyles[] = {
		    &deluge::ui::widgets::core::kDelugeStyle64};
		BoardProfile q = deluge_profile();
		q.style_by_index = kStyles;
		return q;
	}();
	return p;
}

menu::MenuScene& scene() {
	return menu_scene();
}

/// @brief Which `FlashStorage::defaultRanges` slot a node's lower end reads, and which its upper end reads, each read
///        through the node's own rows so this cannot disagree with the table.
std::pair<int, int> slots_of(const Node& node) {
	// Probe: park a unique value in each slot's fields, ask the node, and see which one it answered with. Beats a
	// hand-written node -> slot table, which is the thing under test.
	RangeStorageGuard guard;
	const MenuContext ctx = host_context();
	for (uint8_t i = 0; i < FlashStorage::NUM_DEFAULT_RANGE_SLOTS; i++) {
		FlashStorage::defaultRanges[i] = {.lower = 1000 + i, .upper = 2000 + i};
	}
	return {menu::bindings::read(node, ctx) - 1000, menu::bindings::read_upper(node, ctx) - 2000};
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

/// @brief One driven comparison of an edit: a start, an edge and some detents, against the frozen answer.
struct EditCase {
	const char* what;
	int32_t start_lower;
	int32_t start_upper;
	RangeEdge edge;
	int32_t detents; // one select detent == `Range::step`, which is 1 for all three items
};

spec::OracleTable& table() {
	return spec::OracleTable::named("menu_ranges");
}

std::string_view edge_name(RangeEdge e) {
	return e == RangeEdge::Left ? "left" : e == RangeEdge::Right ? "right" : "off";
}

/// @brief The panel `pixels/<name>.pbm` holds.
std::string frozen_panel(std::string_view name) {
	std::ifstream in(spec::oracle_dir() + "/pixels/" + std::string{name} + ".pbm");
	std::stringstream text;
	text << in.rdbuf();
	return text.str();
}

/// @brief Steer the open range's edge to @p want with the horizontal encoder, as a user does; @p at advances.
void steer_edge(Rig& r, RangeEdge want, uint32_t& at) {
	for (int i = 0; i < 4 && scene().state().edit_edge != want; i++) {
		r.detent(kHorizontalEncoder, want == RangeEdge::Right ? 1 : -1, at);
		r.shell.tick(at + 5);
		at += 10;
	}
}

/// @brief Open @p node on the range @p lower..@p upper in @p slot, steer to @p edge, turn the select encoder
///        @p detents, and say where the two ends went.
std::string port_edit(const Node& node, FlashStorage::DefaultRangeSlot slot, int32_t lower, int32_t upper,
                      RangeEdge edge, int32_t detents) {
	FlashStorage::defaultRanges[slot] = {.lower = lower, .upper = upper};
	Rig r;
	(void)r.open(node);
	r.shell.tick(5);
	uint32_t at = 10;
	steer_edge(r, edge, at);
	if (scene().state().edit_edge != edge) {
		return "edge not reached";
	}
	r.detent(kSelectEncoder, detents, at);
	r.shell.tick(at + 5);
	return std::to_string(FlashStorage::defaultRanges[slot].lower) + " "
	       + std::to_string(FlashStorage::defaultRanges[slot].upper);
}

/// @brief A two-value row as "a b", or "<path> <no row>" so a miss prints where it was looked for.
std::string frozen_pair(const std::string& path, std::string_view column) {
	const std::vector<std::string>* v = table().find(path, column);
	return v == nullptr || v->size() != 2 ? path + " <no row>" : (*v)[0] + " " + (*v)[1];
}

std::string frozen_edit(std::string_view item, int32_t lower, int32_t upper, RangeEdge edge, int32_t detents) {
	return frozen_pair(std::string{item} + "/" + std::to_string(lower) + "-" + std::to_string(upper) + "/"
	                       + std::string{edge_name(edge)} + "/" + std::to_string(detents),
	                   "edit");
}

} // namespace

// clang-format off
describe menu_range_tables("scenes::menu Kind::Range -- the table", $ {
	it("has exactly three consumers in the whole tree, by identity", _ {
		// The rule is "a kind exists only if at least 3 items use it". Three is what justifies
		// `Kind::Range`, so three is what the tree must hold -- asserted as a *set of nodes*, not a count with slack: a fourth range item added
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
		expect(found).to_equal(expected);
		expect(static_cast<int>(found.size())).to_equal(3);
	});

	it("gives each node a spec, and points both ends of each at its own storage slot", _ {
		// The lookalike-mapping trap: three nodes, three near-identical specs. Probed, not asserted
		// from a hand-written table -- and then the probe result is checked against a single
		// transposition of itself, so an all-zero answer cannot pass.
		const Node* order[] = {&nodes::default_tempo, &nodes::default_swing_amount, &nodes::default_key};
		std::vector<int> observed;
		for (const Node* n : order) {
			expect(n->bind.range_spec).not_().to_be_null();
			expect(n->bind.range_spec->format).not_().to_be_null();
			expect(n->bind.range_spec->upper_path.text().empty()).to_be_false();
			const auto [lower, upper] = slots_of(*n);
			expect(upper).to_equal(lower);
			observed.push_back(lower);
		}
		const std::vector<int> expected{FlashStorage::DEFAULT_RANGE_TEMPO, FlashStorage::DEFAULT_RANGE_SWING_AMOUNT,
		                                FlashStorage::DEFAULT_RANGE_KEY};
		expect(observed).to_equal(expected);
		std::vector<int> swapped = observed;
		std::swap(swapped[0], swapped[2]);
		expect(swapped).not_().to_equal(expected);
	});

	it("takes main's bounds", _ {
		const MenuContext ctx = host_context();
		const auto bounds = [&](const Node& node) {
			const menu::IntRange range = menu::bindings::range(node, ctx);
			return std::to_string(range.min) + " " + std::to_string(range.max);
		};
		expect(bounds(nodes::default_tempo)).to_equal(frozen_pair("default_tempo", "bounds"));
		expect(bounds(nodes::default_swing_amount)).to_equal(frozen_pair("default_swing_amount", "bounds"));
		expect(bounds(nodes::default_key)).to_equal(std::string{"0 11"}); // KEY_MIN, KEY_MAX
	});

	it("reads and writes FlashStorage::defaultRanges through its DEVICE rows, both ends", _ {
		RangeStorageGuard guard;
		const MenuContext ctx = host_context();
		FlashStorage::defaultRanges[FlashStorage::DEFAULT_RANGE_TEMPO] = {.lower = 77, .upper = 188};
		expect(menu::bindings::read(nodes::default_tempo, ctx)).to_equal(77);
		expect(menu::bindings::read_upper(nodes::default_tempo, ctx)).to_equal(188);
		menu::bindings::write(nodes::default_tempo, ctx, 100);
		menu::bindings::write_upper(nodes::default_tempo, ctx, 200);
		expect(FlashStorage::defaultRanges[FlashStorage::DEFAULT_RANGE_TEMPO].lower).to_equal(100);
		expect(FlashStorage::defaultRanges[FlashStorage::DEFAULT_RANGE_TEMPO].upper).to_equal(200);
	});

	it("shows an end the flash holds past the range as it is, as main's widget does", _ {
		// Main's `IntegerRange` and `KeyRange` show `lower`/`upper` as they hold them; only a turn clamps. A flash byte
		// can hold anything.
		RangeStorageGuard guard;
		const MenuContext ctx = host_context();
		FlashStorage::defaultRanges[FlashStorage::DEFAULT_RANGE_TEMPO] = {.lower = 250, .upper = 30};
		expect(menu::bindings::read(nodes::default_tempo, ctx)).to_equal(250);
		expect(menu::bindings::read_upper(nodes::default_tempo, ctx)).to_equal(30);
	});
});

describe menu_range_text("scenes::menu Kind::Range -- the spellings", $ {
	it("spells an integer end as main does, over the whole range", _ {
		char scratch[deluge::ui::widgets::oled::kRangeTextMax];
		const menu::IntRange tempo = menu::bindings::range(nodes::default_tempo, host_context());
		for (int32_t v = tempo.min; v <= tempo.max; v++) {
			const std::string at = "tempo " + std::to_string(v) + " ";
			expect(at + std::string(nodes::format_range_integer(v, nullptr, std::span{scratch})))
			    .to_equal(at + table().value("tempo/" + std::to_string(v), "text"));
		}
	});

	it("spells a note end as main does, in both sharps and flats", _ {
		RangeStorageGuard guard;
		char scratch[deluge::ui::widgets::oled::kRangeTextMax];
		const MenuContext ctx = host_context();
		for (const bool sharps : {true, false}) {
			FlashStorage::defaultUseSharps = sharps;
			for (int32_t v = 0; v <= 11; v++) {
				const std::string path = std::string{"key/"} + (sharps ? "sharps/" : "flats/") + std::to_string(v);
				expect(path + " " + std::string(nodes::format_range_key(v, &ctx, std::span{scratch})))
				    .to_equal(path + " " + table().value(path, "text"));
			}
		}
		expect(table().value("key/sharps/3", "text")).not_().to_equal(table().value("key/flats/3", "text"));
	});
});

describe menu_range_edit("scenes::menu Kind::Range -- editing", $ {
	it("moves both ends of an integer range as main's IntegerRange does", _ {
		select_language();
		const EditCase cases[] = {
		    {"coincident, no edge: moves as one", 120, 120, RangeEdge::Off, 5},
		    {"split, no edge: refuses to move", 90, 150, RangeEdge::Off, 5},
		    {"left edge up drags the upper", 120, 120, RangeEdge::Left, 7},
		    {"left edge down leaves the upper", 120, 150, RangeEdge::Left, -7},
		    {"right edge down drags the lower", 120, 120, RangeEdge::Right, -7},
		    {"right edge clamps at maxValue", 200, 238, RangeEdge::Right, 9},
		    {"left edge clamps at minValue", 62, 200, RangeEdge::Left, -9},
		};
		for (const EditCase& c : cases) {
			RangeStorageGuard guard;
			const std::string at = std::string{c.what} + ": ";
			expect(at + port_edit(nodes::default_tempo, FlashStorage::DEFAULT_RANGE_TEMPO, c.start_lower, c.start_upper,
			                      c.edge, c.detents))
			    .to_equal(at + frozen_edit("tempo", c.start_lower, c.start_upper, c.edge, c.detents));
		}
		// A case that moves nothing proves nothing: the frozen answers move where the case says they do.
		expect(frozen_edit("tempo", 120, 120, RangeEdge::Off, 5)).to_equal(std::string{"125 125"});
		expect(frozen_edit("tempo", 90, 150, RangeEdge::Off, 5)).to_equal(std::string{"90 150"});
	});

	it("moves both ends of a key range as main's KeyRange does", _ {
		select_language();
		const EditCase cases[] = {
		    {"coincident, no edge", 5, 5, RangeEdge::Off, 3},
		    {"split, no edge: refuses", 2, 9, RangeEdge::Off, 3},
		    {"left edge stops at the upper", 5, 5, RangeEdge::Left, 4},
		    {"left edge walks up to the upper", 2, 9, RangeEdge::Left, 4},
		    {"right edge stops at the lower", 2, 9, RangeEdge::Right, -10},
		    {"right edge clamps at KEY_MAX", 0, 10, RangeEdge::Right, 5},
		};
		for (const EditCase& c : cases) {
			RangeStorageGuard guard;
			const std::string at = std::string{c.what} + ": ";
			expect(at + port_edit(nodes::default_key, FlashStorage::DEFAULT_RANGE_KEY, c.start_lower, c.start_upper,
			                      c.edge, c.detents))
			    .to_equal(at + frozen_edit("key", c.start_lower, c.start_upper, c.edge, c.detents));
		}
	});

	it("clamps the two kinds differently from one start, so the clamp parameter does work", _ {
		RangeStorageGuard guard;
		select_language();
		expect(port_edit(nodes::default_tempo, FlashStorage::DEFAULT_RANGE_TEMPO, 5, 5, RangeEdge::Left, 1))
		    .to_equal(frozen_edit("tempo", 5, 5, RangeEdge::Left, 1));
		expect(port_edit(nodes::default_key, FlashStorage::DEFAULT_RANGE_KEY, 5, 5, RangeEdge::Left, 1))
		    .to_equal(frozen_edit("key", 5, 5, RangeEdge::Left, 1));
		expect(frozen_edit("tempo", 5, 5, RangeEdge::Left, 1))
		    .not_().to_equal(frozen_edit("key", 5, 5, RangeEdge::Left, 1));
	});

	it("selects an edge on a horizontal turn as main does", _ {
		select_language();
		for (const RangeEdge from : {RangeEdge::Off, RangeEdge::Left, RangeEdge::Right}) {
			for (const int32_t h : {-1, 1}) {
				RangeStorageGuard guard;
				FlashStorage::defaultRanges[FlashStorage::DEFAULT_RANGE_TEMPO] = {.lower = 90, .upper = 150};
				Rig r;
				(void)r.open(nodes::default_tempo);
				r.shell.tick(5);
				uint32_t at = 10;
				steer_edge(r, from, at);
				expect(static_cast<int>(scene().state().edit_edge)).to_equal(static_cast<int>(from));
				r.detent(kHorizontalEncoder, h, at);
				r.shell.tick(at + 5);
				const std::string path = "tempo/" + std::string{edge_name(from)} + "/" + std::to_string(h);
				expect(path + " " + std::string{edge_name(scene().state().edit_edge)})
				    .to_equal(path + " " + table().value(path, "edge"));
			}
		}
	});

	it("seeds the edge on entry as main does", _ {
		select_language();
		struct Seed {
			const Node* node;
			const char* item;
			FlashStorage::DefaultRangeSlot slot;
			int32_t lower;
			int32_t upper;
		};
		const Seed seeds[] = {
		    {&nodes::default_tempo, "tempo", FlashStorage::DEFAULT_RANGE_TEMPO, 120, 120},
		    {&nodes::default_tempo, "tempo", FlashStorage::DEFAULT_RANGE_TEMPO, 90, 150},
		    {&nodes::default_key, "key", FlashStorage::DEFAULT_RANGE_KEY, 5, 5},
		    {&nodes::default_key, "key", FlashStorage::DEFAULT_RANGE_KEY, 2, 9},
		};
		for (const Seed& s : seeds) {
			RangeStorageGuard guard;
			FlashStorage::defaultRanges[s.slot] = {.lower = s.lower, .upper = s.upper};
			Rig r;
			(void)r.open(*s.node);
			r.shell.tick(5);
			const std::string path = std::string{s.item} + "/" + std::to_string(s.lower) + "-" + std::to_string(s.upper);
			expect(path + " " + std::string{edge_name(scene().state().edit_edge)})
			    .to_equal(path + " " + table().value(path, "seed"));
		}
		expect(nodes::default_tempo.bind.range_spec->focus_left_when_split).to_be_true();
		expect(nodes::default_key.bind.range_spec->focus_left_when_split).to_be_false();
	});
});

describe menu_range_pixels("scenes::menu Kind::Range -- pixels", $ {
	it("draws exactly what main's range page draws, in every edge state", _ {
		select_language();
		struct Shot {
			const char* name;
			int32_t lower;
			int32_t upper;
			RangeEdge edge;
		};
		const Shot shots[] = {
		    {"point", 120, 120, RangeEdge::Off},
		    {"split", 90, 150, RangeEdge::Off},
		    {"left", 90, 150, RangeEdge::Left},
		    {"right", 90, 150, RangeEdge::Right},
		};
		for (const Shot& shot : shots) {
			RangeStorageGuard guard;
			FlashStorage::defaultRanges[FlashStorage::DEFAULT_RANGE_TEMPO] = {.lower = shot.lower, .upper = shot.upper};
			Rig r;
			(void)r.open(nodes::default_tempo);
			r.shell.tick(5);
			uint32_t at = 10;
			steer_edge(r, shot.edge, at);
			r.shell.tick(at + 10);
			const std::string frozen = frozen_panel(std::string{"menu_range_"} + shot.name);
			expect(frozen.empty()).to_be_false();
			expect(below_bezel(port_panel_pbm(r))).to_equal(below_bezel(frozen));
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
			expect_golden(self, "ui/menu/menu_default_tempo_48.pbm", r.fake.pbm(0));

			Rig r64{board_64(), profile_64()};
			FlashStorage::defaultRanges[FlashStorage::DEFAULT_RANGE_TEMPO] = {.lower = 90, .upper = 150};
			(void)r64.open(nodes::default_tempo);
			r64.shell.tick(5);
			r64.shell.tick(15);
			expect_golden(self, "ui/menu/menu_default_tempo_64.pbm", r64.fake.pbm(0));
		}
		{
			RangeStorageGuard guard;
			FlashStorage::defaultRanges[FlashStorage::DEFAULT_RANGE_KEY] = {.lower = 1, .upper = 4};
			FlashStorage::defaultUseSharps = true;
			Rig k;
			(void)k.open(nodes::default_key);
			k.shell.tick(5);
			k.shell.tick(15);
			expect_golden(self, "ui/menu/menu_default_key_48.pbm", k.fake.pbm(0));

			Rig k64{board_64(), profile_64()};
			FlashStorage::defaultRanges[FlashStorage::DEFAULT_RANGE_KEY] = {.lower = 1, .upper = 4};
			(void)k64.open(nodes::default_key);
			k64.shell.tick(5);
			k64.shell.tick(15);
			expect_golden(self, "ui/menu/menu_default_key_64.pbm", k64.fake.pbm(0));
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

describe menu_range_reference_rows("scenes::menu Kind::Range: the reference rows", $ {
	it("parses menu_ranges.tsv clean and asks every row of it", _ {
		spec::OracleTable& t = table();
		expect(t.problems().size()).to_equal(size_t{0});
		expect(t.size()).to_be_greater_than(size_t{200});
		expect(t.unasked()).to_equal(std::string{});
	});
});
// clang-format on

DELUGE_SPEC(menu_ranges_spec, menu_range_tables, menu_range_text, menu_range_edit, menu_range_pixels,
            menu_range_reference_rows)
