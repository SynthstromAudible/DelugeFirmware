/// @file
/// @brief The last two bespoke menu items (`ui/scenes/menu/nodes/output_items.h`): which node carries
///        which item, what each one does when the menu focuses it -- and, at the end of it, that the
///        tree has **no `Kind::Custom` placeholders left at all**.
///
/// Three groups, three different kinds of claim:
///
///   * **The mapping, by identity.** Both nodes carry the item their legacy class became, with the
///     `BrowserId` that class opened; no other node in `nodes::kAllRoots` carries either item; and
///     a deliberately crossed copy of the observed table is asserted *not* to match, so the
///     comparison cannot be passing on an all-null read. A behaviour check alone passes with the
///     wrong item on the node, which is why this is an identity table too.
///   * **The behaviour, against main's answers on the same fixture.** The reference rows
///     (`tests/golden/ui/menu_oracle/output_items.tsv` and its panels) hold what main's
///     `midi::device_definition::Linked` and `audio_clip::SpecificSourceOutputSelector` answer: the
///     `readCurrentValue` of the link, and the selector's landings, walks, relevance and panels. The
///     port is driven through a freshly built `MenuContext` on the same fixture and compared with them.
///   * **The milestone.** `validate_tree(kAllRoots, ...)` with strict `ValidateOptions` reports
///     **zero** `PlaceholderCustom`, `kPendingCustomNodes` is 0, and nothing reachable from the
///     roots answers `is_placeholder()`. `ValidateOptions` has no `allow_placeholder_custom` flag, so there is no flag
///     that could hide a regression here.
///

#include "fakes.h"
#include "gui/ui/menu_view_of.h"
#include "ui/scenes/bind/ports/write_sink.h"
#include "ui/scenes/browser/browser_scene.h"
#include "ui/scenes/l10n_bridge.h"
#include "ui/scenes/menu/bindings.h"
#include "ui/scenes/menu/context.h"
#include "ui/scenes/menu/menu_scene.h"
#include "ui/scenes/menu/node.h"
#include "ui/scenes/menu/node_id.h"
#include "ui/scenes/menu/nodes/nodes.h"
#include "ui/scenes/menu/nodes/output_items.h"
#include "ui/scenes/menu/nodes/sound_osc.h" // kPending -- the mechanism that must outlive its users
#include "ui/scenes/menu/relevance.h"
#include "ui/scenes/menu/validate.h"
#include "ui/scenes/profile.h"
#include "ui/scenes/registry.h"

#include "base/notify/notice_queue.h"
#include "ui/shell/profiles/deluge.h"
#include "ui/shell/shell.h"

#include "gui/l10n/l10n.h"
#include "hid/display/display.h"
#include "model/instrument/midi_instrument.h"
#include "model/song/song.h"
#include "processing/audio_output.h"
#include "ui/gfx/oled/icons.h" // the two checkbox icons `row_icon` chooses between

#include "support/oracle_table.h"
#include "support/output_items_fixtures.h"
#include "support/popup_capture.h"
#include "support/song_fixture.h"

// Before <cppspec.hpp>: it #defines `it`, which collides with locals named `it` in the project
// headers this drags in (`audio_engine.h` -> `util/container/ordered_pos_vector.h`).
#include "support/card_access.h"

#include "support/mocks/fake_surfaces.h"
#include "unit/ui/gfx/support/golden.h"
#include "unit/ui/gfx/support/pbm.h"

#include "cppspec.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "support/menu_context.h"
#include "support/spec_main.h"

using namespace deluge::ui::scenes;
using namespace deluge::ui::scenes::spec::outputitems;
using deluge::ui::scenes::menu::MenuContext;
using deluge::ui::scenes::menu::Node;
using deluge::ui::shell::BoardProfile;
using deluge::ui::shell::SceneId;
using deluge::ui::shell::SceneKind;
using deluge::ui::shell::Shell;
using deluge::ui::shell::ShellConfig;

namespace {

namespace nodes = deluge::ui::scenes::menu::nodes;

/// Board encoder index (`kDelugeEncoders`, `ui/shell/profiles/deluge.cpp`): 3 = Select.
constexpr uint8_t kSelectEncoder = 3;
/// Matrix coordinates of the select-encoder button (`kMatrix`, `ui/shell/profiles/deluge.cpp`).
constexpr uint8_t kSelectEncButtonX = 4, kSelectEncButtonY = 3;
constexpr uint8_t kBackX = 7, kBackY = 1;

void select_language() {
	deluge::l10n::chosenLanguage = &deluge::l10n::built_in::english;
	deluge::base::notify::Notice discard;
	while (deluge::base::notify::notices().pop(discard)) {}
}

/// @brief The card these examples list: `tests/fixtures/scenes_spec/output_items`, installed for this file before boot
///        (`tests/integration/checks/scenes_spec_cards.txt`). Constructing one checks it is the installed card.
struct ScratchCard {
	std::filesystem::path dir = std::filesystem::path(DELUGE_REPO_ROOT) / "tests/fixtures/scenes_spec/output_items";
	spec::SdRoot root{dir.string()};
};

[[noreturn]] void spec_bail(const char* what) {
	std::fprintf(stderr, "output_items_spec: %s\n", what);
	std::abort();
}

/// @brief Refuse to start, or to finish, with the browser singleton still holding a listing.
///
/// **Assert-then-clear, not defensive-clear** scenes are process
/// singletons, so an example that leaked a listing would make the *next* one's refusal
/// indistinguishable from a wiring bug. Each rig's destructor tears its own stack down; this guard
/// says so out loud if one did not. `before_each`/`after_each` get no `self`, so `expect` is
/// unavailable there and a named reason on stderr plus `std::abort()` is what "loudly" means.
struct CleanScenes {
	CleanScenes() { check("a scene singleton was still busy when an example started"); }
	~CleanScenes() { check("an example finished with a scene singleton still busy"); }
	CleanScenes(const CleanScenes&) = delete;
	CleanScenes& operator=(const CleanScenes&) = delete;

private:
	static void check(const char* what) {
		if (browser::browser_scene().listing_in_flight()) {
			spec_bail(what);
		}
	}
};

/// @brief A `Shell` over `FakeSurfaceSet` with the application's own scene registry installed.
struct Rig {
	deluge::ui::shell::SceneRegistry reg = scene_registry();
	uitest::FakeSurfaceSet fake;
	Shell shell;
	uint32_t clock = 0;

	Rig(const Rig&) = delete;
	Rig& operator=(const Rig&) = delete;

	Rig()
	    : fake(deluge::ui::shell::kDelugeBoard, deluge_profile()),
	      shell(fake, deluge_profile(), reg, deluge::ui::shell::Services{}, ShellConfig{}) {}

	/// @brief Tear the stack down, so every scene's `on_exit` always runs.
	~Rig() {
		while (shell.stack().depth() > 0) {
			shell.stack().pop(SceneKind::Null);
		}
		menu_scene().reset_process_state(); // each example starts with the rows a boot has
	}

	void open(const Node& root, const MenuContext& ctx) {
		select_language();
		(void)menu::assign_node_ids(nodes::kAllRoots);
		menu_scene().open(root, ctx);
		shell.init(SceneId{SceneKind::FirstApp, menu::node_id(root)});
	}

	void tick(uint32_t at) {
		clock = at;
		shell.tick(at);
	}
	void step() { tick(clock + 10); }

	void edges(uint8_t index, int16_t count) {
		shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_ENCODER, index, 0, count}, clock + 5);
	}
	/// One detent of the (detented, 2 edges per detent) select encoder.
	void turn(int32_t detents) { edges(kSelectEncoder, static_cast<int16_t>(detents * 2)); }
	void button(uint8_t x, uint8_t y, bool on) {
		shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_BUTTON, x, y, static_cast<int16_t>(on ? 255 : 0)},
		                     clock + 5);
	}
	void enter() {
		button(kSelectEncButtonX, kSelectEncButtonY, true);
		button(kSelectEncButtonX, kSelectEncButtonY, false);
	}

	bool on_stack(SceneKind kind) {
		for (const deluge::ui::shell::Scene* s : shell.stack().scenes()) {
			if (s->kind() == kind) {
				return true;
			}
		}
		return false;
	}
};

/// @brief @p parent's visible children in the order the scene lists them
///        (`menu_actions_spec.cpp`'s helper, same two-line rule as `menu::visible_children`).
int32_t visible_index_of(const Node& parent, const Node& child, const MenuContext& ctx) {
	if (parent.dynamic_children != nullptr) {
		return -1; // neither parent here has one; a table edit that adds one must be seen.
	}
	int32_t index = 0;
	for (const Node* kid : parent.children) {
		if (!menu::bindings::relevant(*kid, ctx)) {
			continue;
		}
		if (kid == &child) {
			return index;
		}
		index++;
	}
	return -1;
}

/// @brief Open @p parent, walk the cursor onto @p child and press select -- through the real
///        lowering, the real `list` widget and the real `enter_child`.
void press_child(Rig& r, const Node& parent, const Node& child, const MenuContext& ctx) {
	const int32_t index = visible_index_of(parent, child, ctx);
	if (index < 0) {
		spec_bail("the child under test is not visible in its parent");
	}
	r.open(parent, ctx);
	r.tick(5);
	for (int32_t i = 0; i < index; i++) {
		r.turn(1);
		r.step();
	}
	r.enter();
	r.step();
}

/// @brief Whether @p f declares LOAD on the shared normal blink, as `blinkLed(IndicatorLED::LOAD)` runs it.
bool load_blinks(const deluge::ui::widgets::core::LedFrame& f) {
	for (const auto& [id, state] : f.entries()) {
		if (id == static_cast<deluge::ui::widgets::core::LedId>(deluge::ui::shell::ButtonId::Load)) {
			return state.kind == deluge::ui::widgets::core::LedState::Kind::Shared
			       && state.rate == deluge::ui::widgets::core::BlinkRate::Normal;
		}
	}
	return false;
}

// ---------------------------------------------------------------------------------------------
// The mapping, as a value that can be compared whole

/// @brief Which item class a node carries, plus what it was constructed with.
struct Wiring {
	std::string node;
	int kind = -1;    ///< 0 = MidiDeviceLinkedItem, 1 = SpecificOutputItem, -1 = neither
	int browser = -1; ///< `MidiDeviceLinkedItem::browser_arg()`, or -1 for an item with none

	bool operator==(const Wiring&) const = default;
};

Wiring wiring_of(const char* name, const Node& node) {
	Wiring w;
	w.node = name;
	if (const auto* linked = dynamic_cast<const nodes::MidiDeviceLinkedItem*>(node.custom); linked != nullptr) {
		w.kind = 0;
		w.browser = static_cast<int>(linked->browser_arg());
	}
	else if (dynamic_cast<const nodes::SpecificOutputItem*>(node.custom) != nullptr) {
		w.kind = 1;
	}
	return w;
}

std::string format_wiring(const std::vector<Wiring>& ws) {
	std::string out;
	for (const Wiring& w : ws) {
		out += w.node + "={kind=" + std::to_string(w.kind) + ",browser=" + std::to_string(w.browser) + "} ";
	}
	return out;
}

std::vector<Wiring> observed_wiring() {
	return {
	    wiring_of("midi_device_linked", nodes::midi_device_linked),
	    wiring_of("specific_output_selector", nodes::specific_output_selector),
	};
}

/// @brief What each node must carry, derived from the legacy class each item ports.
std::vector<Wiring> expected_wiring() {
	return {
	    // `midi::device_definition::Linked::writeCurrentValue`'s ON arm is `openUI(&loadMidiDeviceDefinitionUI)`
	    // (`origin/main:src/deluge/gui/menu_item/midi/device_definition/linked.h`), which is
	    // `browser::BrowserId::MidiDeviceDefinition` == 1 (`browser::BrowserId`).
	    {"midi_device_linked", 0, 1},
	    // `audio_clip::SpecificSourceOutputSelector`
	    // (`origin/main:src/deluge/gui/menu_item/audio_clip/specific_output_source_selector.h`) opens nothing at all --
	    // it is a page, so it carries no browser id.
	    {"specific_output_selector", 1, -1},
	};
}

// ---------------------------------------------------------------------------------------------
// The reference rows

spec::OracleTable& table() {
	return spec::OracleTable::named("output_items");
}

/// @brief The panel `pixels/<name>.pbm` holds.
std::string frozen_panel(std::string_view name) {
	std::ifstream in(spec::oracle_dir() + "/pixels/" + std::string{name} + ".pbm");
	std::stringstream text;
	text << in.rdbuf();
	return text.str();
}

/// @brief The output index the row (@p path, "at") holds, or -2 where there is no such row.
int32_t frozen_index(std::string_view path, std::string_view column) {
	const std::vector<std::string>* v = table().find(path, column);
	return v == nullptr || v->empty() ? -2 : std::stoi(v->front());
}

/// @brief The rows the port's root frame can actually ink on the 48-row panel.
///
/// The Deluge panel has a five-row bezel (`kDelugeBoard`'s `DelugeDisplayDesc{128, 48, 5}`) and the
/// port's root frame has its origin at `visible_top`, so anything a legacy routine inks above row 5
/// is clipped away here. Every comparison drops those five rows from *both* sides, exactly as
/// `menu_ranges_spec.cpp` and `context_menu_scene_spec.cpp` do.
std::string below_bezel(const std::string& pbm) {
	size_t pos = 0;
	int newlines = 0;
	while (pos < pbm.size() && newlines < 2) {
		if (pbm[pos] == '\n') {
			newlines++;
		}
		pos++;
	}
	std::string out = pbm.substr(0, pos);
	for (int i = 0; i < 5 && pos < pbm.size(); i++) {
		const size_t next = pbm.find('\n', pos);
		pos = next == std::string::npos ? pbm.size() : next + 1;
	}
	out += pbm.substr(pos);
	return out;
}

/// @brief Drive the port over @p walk's fixture one detent at a time, and say where the track records from after each.
std::string port_walk(Rig& r, ClipRig& rig, const Walk& walk) {
	store_source(rig, walk.start);
	press_child(r, nodes::root_audio_clip, nodes::specific_output_selector, rig.ctx());
	std::string seen;
	for (int32_t i = 1; i <= (walk.detents >= 0 ? walk.detents : -walk.detents); i++) {
		r.turn(walk.detents >= 0 ? 1 : -1);
		r.step();
		seen +=
		    std::string{walk.name} + "/" + std::to_string(i) + " " + std::to_string(rig.recording_from_index()) + "\n";
	}
	return seen;
}

/// @brief The same lines from the reference rows.
std::string frozen_walk(const Walk& walk) {
	std::string want;
	for (int32_t i = 1; i <= (walk.detents >= 0 ? walk.detents : -walk.detents); i++) {
		const std::string path = std::string{"selector/"} + walk.name + "/" + std::to_string(i);
		want += std::string{walk.name} + "/" + std::to_string(i) + " " + table().value(path, "at") + "\n";
	}
	return want;
}

const Walk& walk_named(std::string_view name) {
	return *std::ranges::find(kWalks, name, &Walk::name);
}

} // namespace

// clang-format off
describe output_items_mapping("scenes::menu the two output items: the mapping", $ {

	it("carries the item its legacy class became, on both nodes", _ {
		expect(format_wiring(observed_wiring())).to_equal(format_wiring(expected_wiring()));

		// ... and the comparison is not vacuous. Two items is a small table, but it is exactly the
		// size at which a positional check looks strong and pins nothing, so a deliberately crossed
		// copy is asserted NOT to match. The crossing swaps the *payload* and leaves each row's node
		// name where it is, because that is the shape a real miswiring has: `midi_device_linked`
		// still called that, carrying the output picker.
		std::vector<Wiring> crossed = observed_wiring();
		std::swap(crossed[0].kind, crossed[1].kind);
		expect(format_wiring(crossed)).not_().to_equal(format_wiring(expected_wiring()));

		// And the browser id is a real discriminator, not a constant: pointing the link arm at the
		// *sample* browser (`BrowserId::Sample` == 0) must fail the table too.
		std::vector<Wiring> wrong_browser = observed_wiring();
		wrong_browser[0].browser = 0;
		expect(format_wiring(wrong_browser)).not_().to_equal(format_wiring(expected_wiring()));
	});

	it("puts these items on those two nodes and on nothing else in the tree", _ {
		// By identity over the whole reachable tree, not by a count: an item accidentally attached to
		// a third node somewhere else would leave every assertion above passing.
		select_language();
		const uint16_t assigned = menu::assign_node_ids(nodes::kAllRoots);

		const Node* const kExpected[] = {&nodes::midi_device_linked, &nodes::specific_output_selector};
		std::set<const Node*> want{std::begin(kExpected), std::end(kExpected)};

		std::set<const Node*> found;
		for (uint16_t id = 1; id <= assigned; id++) {
			const Node* n = menu::node_by_id(id);
			if (n == nullptr || n->kind != menu::Kind::Custom) {
				continue;
			}
			if (dynamic_cast<const nodes::MidiDeviceLinkedItem*>(n->custom) != nullptr
			    || dynamic_cast<const nodes::SpecificOutputItem*>(n->custom) != nullptr) {
				found.insert(n);
			}
		}
		const auto ids = [](const std::set<const Node*>& in) {
			std::vector<std::string> parts;
			for (const Node* n : in) {
				parts.push_back(std::to_string(menu::node_id(*n)) + "=" + std::to_string(n->name));
			}
			std::sort(parts.begin(), parts.end());
			std::string out;
			for (const std::string& p : parts) {
				out += p + " ";
			}
			return out;
		};
		expect(ids(found)).to_equal(ids(want));
	});
});

describe output_items_milestone("scenes::menu the tree has no placeholders left", $ {

	it("reports zero PlaceholderCustom with nothing standing the rule down", _ {
		// **No placeholder is left in the tree.** `ValidateOptions` is default-constructed, and it has no flag that
		// could be hiding this. The shortcut sweep stays on: this is `kAllRoots`, the one caller for which it means
		// anything.
		select_language();
		const auto issues = menu::validate_tree(nodes::kAllRoots, {}, {}, {}, menu::ValidateOptions{});
		size_t placeholders = 0;
		std::string names;
		for (const menu::Issue& i : issues) {
			if (i.kind == menu::Issue::Kind::PlaceholderCustom) {
				placeholders++;
				names += std::to_string(i.detail) + " ";
			}
		}
		expect(names).to_equal(std::string{}); // names the offender rather than only moving a count
		expect(placeholders).to_equal(size_t(0));
		expect(menu::kPendingCustomNodes).to_equal(size_t(0));
	});

	it("walks the tree and finds no node pointing at a stand-in", _ {
		// The same claim from the other end, and not through `validate_tree` at all: every
		// `Kind::Custom` node reachable from the roots has a non-null `custom` that is not a
		// placeholder. Two assertions, not one -- `custom != nullptr && is_placeholder()` is also
		// false for a null pointer, which is the worse bug (`validate.h`'s `NullCustom`).
		select_language();
		const uint16_t assigned = menu::assign_node_ids(nodes::kAllRoots);
		size_t custom_nodes = 0;
		for (uint16_t id = 1; id <= assigned; id++) {
			const Node* n = menu::node_by_id(id);
			if (n == nullptr || n->kind != menu::Kind::Custom) {
				continue;
			}
			custom_nodes++;
			expect(n->custom).not_().to_be_null();
			if (n->custom != nullptr) {
				expect(n->custom->is_placeholder()).to_be_false();
			}
		}
		expect(custom_nodes).to_equal(menu::kShippedCustomNodes);
	});

	it("keeps the placeholder mechanism itself, which no node uses", _ {
		// `nodes::kPending` stays, and still answers `is_placeholder()`. That is the point of
		// removing the *option* rather than the *rule*: the next table that needs a stand-in gets
		// one, and `validate_tree` reports it with nothing left to suppress it.
		expect(nodes::kPending.is_placeholder()).to_be_true();
	});
});

describe midi_device_linked_item("scenes::menu midi_device_linked", $ {

	before_each([] {
		if (display == nullptr) {
			display = &deluge::ui::scenes::spec::quiet_display();
		}
	});

	it("agrees with Linked::readCurrentValue about whether a definition is linked", _ {
		// Three model states; the port is asked through a freshly built `MenuContext`.
		MidiRig rig;
		std::vector<std::string> frozen;
		std::vector<std::string> port;
		for (const LinkedCase& c : kLinkedCases) {
			const std::string path = std::string{"linked/"} + c.name;
			rig.instrument()->setDeviceDefinitionFileName(c.path);
			frozen.push_back(path + " " + table().value(path, "value"));
			port.push_back(path + " " + (nodes::MidiDeviceLinkedItem::linked(rig.ctx()) ? "1" : "0"));
		}
		expect(port).to_equal(frozen);
		// Not all one answer, so a constant-returning port could not pass.
		expect(table().value("linked/empty", "value")).to_equal(std::string{"0"});
		expect(table().value("linked/device1", "value")).to_equal(std::string{"1"});
	});

	it("draws the checkbox its parent's row wants, not the CustomItem default arrow", _ {
		// `Linked::renderSubmenuItemTypeForOled`
		// (`origin/main:src/deluge/gui/menu_item/midi/device_definition/linked.h`) is one of the three overrides of
		// legacy's base blit, so this row is a checkbox in both states -- never the submenu arrow
		// `CustomItem::row_icon` defaults to (`ui/scenes/menu/node.h`). Both icons are named, so a port that returned one
		// constant could not pass, and the arrow is named too, so a port that fell back to the default could not
		// either.
		MidiRig rig;

		rig.instrument()->deviceDefinitionFileName.clear();
		expect(nodes::kMidiDeviceLinked.row_icon(rig.ctx())).to_equal(&deluge::ui::gfx::oled::icons::uncheckedBox);

		rig.instrument()->setDeviceDefinitionFileName("MIDI_DEVICES/DEFINITION/DEVICE1.XML");
		expect(nodes::kMidiDeviceLinked.row_icon(rig.ctx())).to_equal(&deluge::ui::gfx::oled::icons::checkedBox);

		expect(nodes::kMidiDeviceLinked.row_icon(rig.ctx())).not_().to_equal(&deluge::ui::gfx::oled::icons::submenuArrow);
	});

	it("names its two states with the words a toggle's own page lists, its row being a checkbox", _ {
		// The page draws no words (its row is a checkbox, `row_icon` above), so the menu dump and the website's
		// reference spell it as every toggle: the table `MenuView::render_leaf_toggle` draws, Disabled then Enabled.
		expect(nodes::midi_device_linked.bind.names.data()).to_equal(std::data(nodes::kToggleWords));
		expect(nodes::midi_device_linked.bind.names.size()).to_equal(std::size(nodes::kToggleWords));
		expect(std::string{deluge::ui::scenes::text_lookup(nodes::midi_device_linked.bind.names[0])})
		    .to_equal(std::string{"Disabled"});
		expect(std::string{deluge::ui::scenes::text_lookup(nodes::midi_device_linked.bind.names[1])})
		    .to_equal(std::string{"Enabled"});
	});

	it("notes the linked file's name, without its folders, as Linked::renderSubmenuItemTypeForOled does", _ {
		// `strrchr(fullPath, '/') + 1`; a name with no folder is noted whole.
		MidiRig rig;
		std::array<char, 40> buf{};
		rig.instrument()->deviceDefinitionFileName.clear();
		expect(nodes::kMidiDeviceLinked.row_note(rig.ctx(), buf).empty()).to_be_true();
		rig.instrument()->setDeviceDefinitionFileName("MIDI_DEVICES/DEFINITION/BLOFELD.XML");
		expect(std::string{nodes::kMidiDeviceLinked.row_note(rig.ctx(), buf)}).to_equal(std::string{"BLOFELD.XML"});
		rig.instrument()->setDeviceDefinitionFileName("NOFOLDER.XML");
		expect(std::string{nodes::kMidiDeviceLinked.row_note(rig.ctx(), buf)}).to_equal(std::string{"NOFOLDER.XML"});
	});

	it("draws the DEVICE DEFINITION list with the linked file's name two rows under LINKED", _ {
		MidiRig rig;
		rig.instrument()->setDeviceDefinitionFileName("MIDI_DEVICES/DEFINITION/BLOFELD.XML");
		Rig r;
		r.open(nodes::midi_device_definition, rig.ctx());
		r.tick(5);
		expect_golden(self, "ui/menu/menu_midi_device_definition_linked_48.pbm", r.fake.pbm(0));
	});

	it("says nothing is linked when the output is not a MIDI instrument", _ {
		// Legacy's `readCurrentValue` casts `getCurrentOutput()` unguarded and is safe only because
		// `Linked::isRelevant` (`origin/main:src/deluge/gui/menu_item/midi/device_definition/linked.h`) refused first
		// -- which is `relevance::is_midi_out` on the node. The port answers false rather than reading through the
		// wrong type, and the predicate that keeps it off screen is checked here too.
		ClipRig rig;
		expect(nodes::MidiDeviceLinkedItem::linked(rig.ctx())).to_be_false();
		expect(menu::relevance::is_midi_out(rig.ctx())).to_be_false();
	});

	it("opens the MIDI device-definition browser when nothing is linked yet", _ {
		CleanScenes clean;
		spec::QuietAudio quiet;
		ScratchCard card;
		MidiRig rig;
		rig.instrument()->deviceDefinitionFileName.clear();

		Rig r;
		press_child(r, nodes::midi_device_definition, nodes::midi_device_linked, rig.ctx());
		// Two more ticks: `after_render` files the `Push` at the end of the tick that focused the
		// item, and `BrowserScene` collects its listing in its own `after_render` a tick later.
		r.step();
		r.step();

		expect(r.on_stack(browser::kBrowserSceneKind)).to_be_true();
		// **The wiring under test**: the second descriptor, not the sample one.
		expect(static_cast<int>(browser::browser_scene().browser_id()))
		    .to_equal(static_cast<int>(browser::BrowserId::MidiDeviceDefinition));
		expect(std::string{browser::browser_scene().current_dir()}).to_equal(std::string{"MIDI_DEVICES/DEFINITION"});
	});

	it("blinks LOAD while the definition browser is up, and the menu keeps it blinking once the browser closes", _ {
		// `LoadUI::focusRegained`'s `blinkLed(IndicatorLED::LOAD)` as the browser opens; the sound editor's
		// `setLedStates` never touches LOAD, so it blinks on under the menu until the menu closes.
		CleanScenes clean;
		spec::QuietAudio quiet;
		ScratchCard card;
		MidiRig rig;
		rig.instrument()->deviceDefinitionFileName.clear();

		Rig r;
		press_child(r, nodes::midi_device_definition, nodes::midi_device_linked, rig.ctx());
		r.step();
		r.step();
		expect(r.on_stack(browser::kBrowserSceneKind)).to_be_true();
		{
			deluge::ui::widgets::core::LedFrame f;
			browser::browser_scene().leds(f);
			expect(load_blinks(f)).to_be_true();
		}

		for (int32_t i = 0; i < 20; i++) {
			r.step(); // the listing lands; the browser refuses buttons until it has
		}
		r.button(kBackX, kBackY, true); // BACK in the default folder closes the browser
		r.button(kBackX, kBackY, false);
		r.step();
		r.step();
		expect(r.on_stack(browser::kBrowserSceneKind)).to_be_false();
		{
			deluge::ui::widgets::core::LedFrame f;
			menu_scene().leds(f);
			expect(load_blinks(f)).to_be_true();
		}
	});

	it("declares no LOAD blink from a menu that opened no definition browser", _ {
		// The unlink arm opens nothing, so nothing ever blinked LOAD.
		CleanScenes clean;
		MidiRig rig;
		rig.instrument()->setDeviceDefinitionFileName("MIDI_DEVICES/DEFINITION/DEVICE1.XML");
		Rig r;
		press_child(r, nodes::midi_device_definition, nodes::midi_device_linked, rig.ctx());
		r.step();
		deluge::ui::widgets::core::LedFrame f;
		menu_scene().leds(f);
		expect(load_blinks(f)).to_be_false();
	});

	it("does not toggle when the menu is restored onto it -- the browser's own load survives", _ {
		// **The defect this pins.** `SceneStack::replace_root` saves every scene, and this
		// menu is parked on the leaf while `BrowserScene` sits above it -- `MenuSavedState` does not
		// carry `go_up_on_resume`, so the restore lands back on the leaf. `restore_state` ->
		// `enter_leaf` -> `on_focus` then replayed the *press*, and on the linked arm cleared the
		// definition file the browser had just loaded, with no user action. Legacy cannot: its
		// `beginSession` does not toggle, and `Toggle::selectButtonPress` guards this exact case
		// (`origin/main:src/deluge/gui/menu_item/toggle.h`).
		CleanScenes clean;
		spec::QuietAudio quiet;
		ScratchCard card;
		MidiRig rig;
		rig.instrument()->deviceDefinitionFileName.clear();

		Rig r;
		press_child(r, nodes::midi_device_definition, nodes::midi_device_linked, rig.ctx());
		expect(menu_scene().focused()).to_equal(&nodes::midi_device_linked);

		// What `BrowserScene`'s own `accept` does when the user picks a file
		// (`midi_device_accept`, `browser/browser_scene.cpp`).
		const std::string loaded = "MIDI_DEVICES/DEFINITION/DEVICE1.XML";
		rig.instrument()->setDeviceDefinitionFileName(loaded);

		// The state `replace_root` would carry across, taken while the menu is parked on the item.
		deluge::ui::shell::SceneState saved{};
		menu_scene().save_state(saved);
		menu_scene().restore_state(saved);

		expect(menu_scene().focused()).to_equal(&nodes::midi_device_linked); // it did land here
		expect(std::string{rig.instrument()->deviceDefinitionFileName}).to_equal(loaded);
	});

	it("does not toggle when opened as a root through a SceneId, in either direction", _ {
		// The other entry point into `FocusCause::NoRowPress`: `retarget`, which is `open()`, a
		// `SceneId::arg` deep link and the shortcut-pad jump. Both arms are checked, because each
		// would do something different and both are wrong without a press: the linked arm would
		// clear the file, the unlinked arm would open a browser nobody asked for.
		CleanScenes clean;
		spec::QuietAudio quiet;
		ScratchCard card;
		MidiRig rig;
		const std::string loaded = "MIDI_DEVICES/DEFINITION/DEVICE1.XML";
		rig.instrument()->setDeviceDefinitionFileName(loaded);

		{
			Rig r;
			r.open(nodes::midi_device_linked, rig.ctx());
			r.tick(0);
			r.step();
			expect(std::string{rig.instrument()->deviceDefinitionFileName}).to_equal(loaded);
			expect(r.on_stack(browser::kBrowserSceneKind)).to_be_false();
		}
		{
			rig.instrument()->deviceDefinitionFileName.clear();
			Rig r;
			r.open(nodes::midi_device_linked, rig.ctx());
			r.tick(0);
			r.step();
			r.step();
			expect(r.on_stack(browser::kBrowserSceneKind)).to_be_false();
		}
	});

	it("clears the linked file and opens nothing when one is already linked", _ {
		CleanScenes clean;
		MidiRig rig;
		rig.instrument()->setDeviceDefinitionFileName("MIDI_DEVICES/DEFINITION/DEVICE1.XML");

		Rig r;
		press_child(r, nodes::midi_device_definition, nodes::midi_device_linked, rig.ctx());

		// `Linked::writeCurrentValue`'s OFF arm
		// (`origin/main:src/deluge/gui/menu_item/midi/device_definition/linked.h`).
		expect(rig.instrument()->deviceDefinitionFileName.empty()).to_be_true();
		// And no browser: this arm opens no UI at all.
		r.step();
		r.step();
		expect(r.on_stack(browser::kBrowserSceneKind)).to_be_false();
		// `Toggle::selectButtonPress` returns `NO_NAVIGATION` (`origin/main:src/deluge/gui/menu_item/toggle.h`) -- the
		// user is left on the parent list, which the port reaches by descending for one render and ascending out.
		expect(menu_scene().focused()).to_equal(&nodes::midi_device_definition);
		expect(static_cast<int>(menu_scene().state().depth)).to_equal(0);
	});
});

describe specific_output_item("scenes::menu specific_output_selector", $ {

	before_each([] {
		if (display == nullptr) {
			display = &deluge::ui::scenes::spec::quiet_display();
		}
	});

	it("hands a turn to the menu's sink as a write of the track it records from", _ {
		ClipRig rig;
		rig.song.output->setOutputRecordingFrom(rig.synth_a);
		MenuContext ctx = rig.ctx();
		deluge::ui::scenes::bind::test::RecordingWrites recording;
		ctx.ports.writes = &recording;
		Rig r;
		press_child(r, nodes::root_audio_clip, nodes::specific_output_selector, ctx);
		r.turn(1);
		r.step();
		const auto wrote = std::ranges::find_if(recording.requests, [&](const deluge::ui::scenes::bind::WriteRequest& w) {
			return w.target.group == deluge::project::SettingGroupId::AUDIO_INPUT
			       && w.target.path == "outputRecordingFrom"
			       && w.menu_value == static_cast<int32_t>(*rig.synth_b->id());
		});
		expect(std::distance(recording.requests.begin(), wrote)).to_be_less_than(std::ssize(recording.requests));
	});

	it("lands where SpecificSourceOutputSelector::beginSession lands, from an invalid stored source", _ {
		ClipRig rig;
		// Nothing recorded from yet, which is `beginSession`'s repair arm.
		store_source(rig, Start::None);
		Rig r;
		press_child(r, nodes::root_audio_clip, nodes::specific_output_selector, rig.ctx());
		expect(rig.recording_from_index()).to_equal(frozen_index("selector/invalid_source", "lands"));
		// The repair really moved something: index 0 is the audio track itself, which it may not
		// record from, so the first valid target is the synth at index 1.
		expect(frozen_index("selector/invalid_source", "lands")).to_equal(rig.index_of(rig.synth_a));
	});

	it("keeps a stored source that is still valid, exactly as beginSession does", _ {
		ClipRig rig;
		store_source(rig, Start::SynthB);
		Rig r;
		press_child(r, nodes::root_audio_clip, nodes::specific_output_selector, rig.ctx());
		expect(rig.recording_from_index()).to_equal(frozen_index("selector/valid_source", "lands"));
		expect(frozen_index("selector/valid_source", "lands")).to_equal(rig.index_of(rig.synth_b));
	});

	it("skips the outputs it cannot record from, exactly as selectEncoderAction does", _ {
		// One detent from the synth at index 1 must land on the synth at index **3**, skipping the
		// MIDI instrument at index 2 -- the discriminator this fixture exists for.
		ClipRig rig;
		Rig r;
		const Walk& walk = walk_named("skip");
		expect(port_walk(r, rig, walk)).to_equal(frozen_walk(walk));
		expect(frozen_index("selector/skip/1", "at")).to_equal(rig.index_of(rig.synth_b));
		expect(frozen_index("selector/skip/1", "at")).not_().to_equal(rig.index_of(rig.midi));
	});

	it("steps five outputs a click while shift is held, skipping those it cannot record from", _ {
		// `getNextRecordableOutputIndex(outputIndex, offset)` takes the scaled offset: five steps from the synth at
		// index 1 run past the end of the list, where the walk stops on the last recordable output.
		ClipRig rig;
		(void)rig.add_synth("C");
		Instrument* const last = rig.add_synth("D");
		store_source(rig, Start::SynthA);
		Rig r;
		press_child(r, nodes::root_audio_clip, nodes::specific_output_selector, rig.ctx());
		r.button(8, 0, true); // SHIFT
		r.turn(1);
		r.step();
		r.button(8, 0, false);
		expect(rig.recording_from_index()).to_equal(frozen_index("selector/shift/click", "at"));
		// ... which is where five single detents end.
		expect(rig.recording_from_index()).to_equal(frozen_index("selector/shift/5", "at"));
		expect(frozen_index("selector/shift/5", "at")).to_equal(rig.index_of(last));
	});

	it("walks the shift fixture's five single detents as main does", _ {
		ClipRig rig;
		(void)rig.add_synth("C");
		(void)rig.add_synth("D");
		Rig r;
		const Walk& walk = walk_named("shift");
		expect(port_walk(r, rig, walk)).to_equal(frozen_walk(walk));
	});

	it("stops at the end of the list rather than wrapping", _ {
		ClipRig rig;
		Rig r;
		const Walk& walk = walk_named("end");
		expect(port_walk(r, rig, walk)).to_equal(frozen_walk(walk));
		expect(rig.recording_from_index()).to_equal(rig.index_of(rig.synth_b)); // the last recordable one, not past it
	});

	it("stops at the bottom of the list too, rather than falling off it", _ {
		// The mirror of the example above, and it is not symmetry padding: index 0 is the audio
		// track itself, which `canRecordFrom` refuses, so a walk that clamped to "the first index"
		// rather than "the last index it could stop on" would land on an output the track cannot
		// record from -- and `setOutputRecordingFrom` would then store null. Found by mutating the
		// end guard; the forward end could not see it, because index 3 happens to be valid.
		ClipRig rig;
		Rig r;
		const Walk& walk = walk_named("bottom");
		expect(port_walk(r, rig, walk)).to_equal(frozen_walk(walk));
		expect(rig.recording_from_index()).to_equal(rig.index_of(rig.synth_a)); // the first recordable one, not index 0
		expect(rig.song.output->getOutputRecordingFrom()).not_().to_be_null();
	});

	it("walks back down again, skipping the same output on the way", _ {
		ClipRig rig;
		Rig r;
		const Walk& walk = walk_named("back");
		expect(port_walk(r, rig, walk)).to_equal(frozen_walk(walk));
		expect(rig.recording_from_index()).to_equal(rig.index_of(rig.synth_a));
	});

	it("draws what SpecificSourceOutputSelector::drawPixelsForOled draws", _ {
		// The pixel oracle, and it is what pins the layout: main's rows are `OLED_MAIN_TOPMOST_PIXEL + 14` and `+ 28`
		// (`origin/main:src/deluge/gui/menu_item/audio_clip/specific_output_source_selector.h`), and the port's frame
		// origin already carries `OLED_MAIN_TOPMOST_PIXEL` (5, `src/board_config.h`), so a port drawing at
		// `ui::widgets::oled::output_name`'s rows (12 and 30) would fail here.
		ClipRig rig;
		rig.song.output->setOutputRecordingFrom(rig.synth_a);

		Rig r;
		press_child(r, nodes::root_audio_clip, nodes::specific_output_selector, rig.ctx());
		r.step(); // the frame after the descent, so the item is what the panel shows

		// The frozen panel is main's `renderOLED()`: `drawScreenTitle` + `drawPixelsForOled`, which is what the port's
		// `draw_title_for` plus its two `text` calls are.
		const std::string frozen = frozen_panel("output_selector");
		expect(frozen.empty()).to_be_false();
		expect(below_bezel(r.fake.pbm(0))).to_equal(below_bezel(frozen));
	});

	it("draws the same empty page when nothing can be recorded from", _ {
		// A song with only the audio track in it: `canRecordFrom` refuses `this`, so there is no valid target at all
		// and both sides take the "No track" arm of `SpecificSourceOutputSelector::drawPixelsForOled` -- a different
		// branch of the renderer from the example above.
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		f.output->setInputChannel(AudioInputChannel::SPECIFIC_OUTPUT);
		f.output->setOutputRecordingFrom(nullptr);
		const MenuContext ctx = spec::ctx_of(*f.song, deluge::gui::menuViewOf(f.clip), 0, false, false, false, /*kit_global_fx=*/false);

		Rig r;
		press_child(r, nodes::root_audio_clip, nodes::specific_output_selector, ctx);
		r.step();

		const std::string frozen = frozen_panel("output_selector_empty");
		expect(frozen.empty()).to_be_false();
		expect(below_bezel(r.fake.pbm(0))).to_equal(below_bezel(frozen));
	});

	it("agrees with SpecificSourceOutputSelector::isRelevant about when it is offered", _ {
		// `isRelevant` is `inputChannel == SPECIFIC_OUTPUT` on the current audio output; the node carries
		// `relevance::audio_source_is_specific`. Compared over the two answers, then on a non-audio output.
		ClipRig rig;
		std::vector<std::string> frozen;
		std::vector<std::string> port;
		for (const auto& [name, channel] : {std::pair{"specific", AudioInputChannel::SPECIFIC_OUTPUT},
		                                    std::pair{"left", AudioInputChannel::LEFT}}) {
			const std::string path = std::string{"selector/"} + name;
			rig.song.output->setInputChannel(channel);
			frozen.push_back(path + " " + table().value(path, "rel"));
			port.push_back(path + " " + (menu::relevance::audio_source_is_specific(rig.ctx()) ? "1" : "0"));
		}
		expect(port).to_equal(frozen);
		expect(table().value("selector/specific", "rel")).to_equal(std::string{"1"});
		expect(table().value("selector/left", "rel")).to_equal(std::string{"0"});

		// And on a MIDI clip -- where main's unguarded cast would be reading the wrong type -- the
		// port simply says no.
		MidiRig midi;
		expect(menu::relevance::audio_source_is_specific(midi.ctx())).to_be_false();
	});
});

describe midi_instrument_records("scenes::menu MIDI instrument writes reach the record a save writes", $ {
	// A MIDI instrument saves these fields from its output record (`DocOutput`), so a write that reaches only the
	// engine's copy is lost on save.
	it("writes the Y-axis, aftertouch and MPE toggles into the instrument's record", _ {
		MidiRig rig;
		const MenuContext ctx = rig.ctx();
		menu::bindings::write(nodes::mpe_y_to_mod_wheel, ctx, 1);
		menu::bindings::write(nodes::midi_aftertouch_collapse, ctx, 1);
		menu::bindings::write(nodes::midi_mpe_collapse, ctx, 0);
		const deluge::project::DocOutput& record = *rig.instrument()->docOutput_;
		expect(static_cast<int32_t>(record.output_mpe_y)).to_equal(static_cast<int32_t>(CC_EXTERNAL_MOD_WHEEL));
		expect(record.collapse_aftertouch).to_be_true();
		expect(record.collapse_mpe).to_be_false();
	});

	it("hands an unlink to the menu's sink as a text request", _ {
		MidiRig rig;
		rig.instrument()->setDeviceDefinitionFileName("MIDI_DEVICES/DEFINITION/DEVICE1.XML");
		MenuContext ctx = rig.ctx();
		deluge::ui::scenes::bind::test::RecordingWrites recording;
		ctx.ports.writes = &recording;
		menu::MenuState state{};
		nodes::kMidiDeviceLinked.on_focus(ctx, state);
		const bool unlinked = recording.gestures.size() == 1
		                      && recording.gestures[0].kind == deluge::ui::scenes::bind::test::RecordingWrites::Gesture::Kind::TEXT
		                      && recording.gestures[0].target.path == "deviceDefinitionFileName"
		                      && recording.gestures[0].text.empty();
		expect(unlinked).to_be_true();
	});

	it("clears a linked definition from the instrument's record when it unlinks", _ {
		MidiRig rig;
		rig.instrument()->setDeviceDefinitionFileName("MIDI_DEVICES/DEFINITION/DEVICE1.XML");
		menu::MenuState state{};
		nodes::kMidiDeviceLinked.on_focus(rig.ctx(), state);
		expect(rig.instrument()->docOutput_->device_definition_file_name).to_equal(std::string{});
	});
});

describe output_items_reference_rows("scenes::menu the two output items: the reference rows", $ {
	it("parses output_items.tsv clean and asks every row of it", _ {
		spec::OracleTable& t = table();
		expect(t.problems().size()).to_equal(size_t{0});
		expect(t.size()).to_be_greater_than(size_t{20});
		expect(t.unasked()).to_equal(std::string{});
	});
});
// clang-format on

DELUGE_SPEC(output_items_spec, output_items_mapping, output_items_milestone, midi_device_linked_item,
            specific_output_item, midi_instrument_records, output_items_reference_rows)
