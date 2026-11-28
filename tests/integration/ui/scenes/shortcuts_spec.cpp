/// @file
/// @brief The seven shortcut tables, the blink plans, the SHIFT overlay colours and the pages the pads land on, checked
///        against the reference rows of upstream `main`'s menu (`tests/golden/ui/menu_oracle/shortcuts.tsv`).
///
/// Transcription is where errors hide, so nothing here trusts the port's own table: every cell of every layer is
/// compared with the row that names the item `main` holds there, by the tree paths the item sits at (so a swap of two
/// numbered sibling columns is not hidden by their identical names) and by name, **and** every row is asked, so
/// neither a dropped cell nor an invented one can pass. On top of that:
///
///   * the layers' shape and bounds (`shortcut_node` outside 16x8 is null);
///   * which roots share a table, and which two have a distinct second layer;
///   * the name-editor swap the two drum roots carry;
///   * `shortcut_node`'s second-layer fallback (the second layer replaces a cell only where it has one);
///   * `shortcut_pos` round-trips every cell of every layer;
///   * `validate_tree` over `nodes::kAllRoots` with the shortcut sweep **on** -- the `ShortcutToNothing` rule's real
///     run, which is what says every cell points at a node some root actually reaches;
///   * a `Shell` built with `ui::scenes::deluge_profile()` still runs with no `BoardProfile::shortcuts` field;
///   * the blink plans, the overlay colours and the landing pages, against rows.
///
/// A cell's node identity is its tree path: the key the reference tables give every node under the ten roots
/// (`tables3::roots()`), so a row names an item the way `tables_3.tsv` does.
///
/// The last group asks that every row of the table was asked (`OracleTable::unasked`).

#include "gui/ui/menu_view_of.h"
#include "processing/engines/audio_engine.h"
#include "ui/scenes/menu/context.h"
#include "ui/scenes/menu/menu_scene.h"
#include "ui/scenes/menu/node.h"
#include "ui/scenes/menu/node_id.h"
#include "ui/scenes/menu/nodes/nodes.h"
#include "ui/scenes/menu/shortcut_blink.h"
#include "ui/scenes/menu/shortcut_overlay.h"
#include "ui/scenes/menu/shortcuts.h"
#include "ui/scenes/menu/validate.h"
#include "ui/scenes/profile.h"
#include "ui/scenes/registry.h"

#include "ui/shell/profiles/deluge.h"
#include "ui/shell/shell.h"

#include "gui/views/instrument_clip_view.h"
#include "modulation/patch/patch_cable_set.h"

#include "model/clip/instrument_clip.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h"
#include "processing/sound/sound_instrument.h"

#include "support/global_guest_menu.h"
#include "support/known_differences.h"
#include "support/menu_engine.h"
#include "support/oracle_table.h"
#include "support/oracle_tree.h"
#include "support/shortcut_layout.h"
#include "support/shortcuts_fixtures.h"
#include "support/song_fixture.h"
#include "support/tables_3_fixtures.h"

#include "support/mocks/fake_surfaces.h"
#include "support/printers/pad_flash_printer.h"
#include "support/printers/shortcut_blink_printer.h"
#include "unit/ui/widgets/support/frames.h"

#include "cppspec.hpp"
#include "support/matchers/value_matchers.h"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <map>
#include <set>
#include <span>
#include <string>
#include <vector>

#include "support/spec_main.h"

using namespace deluge::ui::scenes;
using deluge::project::ShortcutsVersion;
using deluge::ui::scenes::menu::MenuContext;
using deluge::ui::scenes::menu::MenuState;
using deluge::ui::scenes::menu::Node;
using deluge::ui::scenes::menu::ShortcutPos;
using deluge::ui::scenes::spec::UiStackGuard;
using deluge::ui::scenes::spec::shortcuts::FreeverbModel;
using deluge::ui::scenes::spec::shortcuts::install_sound_context;
using deluge::ui::scenes::spec::shortcuts::seed_blink_cables;
using deluge::ui::scenes::spec::shortcuts::select_language;
using deluge::ui::scenes::spec::shortcuts::sound_fixture;
using deluge::ui::scenes::spec::shortcuts::text_of;

namespace {

namespace nodes = deluge::ui::scenes::menu::nodes;

using Root = MenuContext::Root;

spec::OracleTable& table() {
	return spec::OracleTable::named("shortcuts");
}

/// @brief Every tree path of every root at which @p node sits, joined by spaces, as the cell rows name an item.
///        `-` for no node, and `name:<its name>` for a node no root reaches. With @p root, only the paths in that
///        root's own tree where the node sits in it.
std::string node_paths(const Node* node, std::string_view root = {}) {
	static const std::map<const Node*, std::vector<std::string>> paths = [] {
		std::map<const Node*, std::vector<std::string>> out;
		for (const auto& root : deluge::ui::scenes::spec::tables3::roots()) {
			for (const auto& e : deluge::ui::scenes::spec::tree_entries(root.name, *root.node)) {
				out[e.node].push_back(e.path);
			}
		}
		return out;
	}();
	if (node == nullptr) {
		return "-";
	}
	const auto found = paths.find(node);
	if (found == paths.end()) {
		return "name:" + text_of(node->name);
	}
	return root.empty() ? deluge::ui::scenes::spec::shortcuts::join_paths(found->second)
	                    : deluge::ui::scenes::spec::shortcuts::paths_in_root(found->second, root);
}

/// @brief The key of a cell's rows: `cell/<table>/<version>/<x>,<y>`.
std::string cell_key(std::string_view table_name, std::string_view version, uint8_t x, uint8_t y) {
	return "cell/" + std::string{table_name} + "/" + std::string{version} + "/" + std::to_string(x) + ","
	       + std::to_string(y);
}

/// @brief Describe a mismatch between the port's @p node at one cell and the rows of that cell, or an empty string when
///        they agree.
///
/// A free function rather than an inline `expect`: CppSpec's `expect` is only available inside an example's own lambda,
/// so the sweep collects descriptions and the example asserts on them.
///
/// The item is compared by tree path, and by name where the cell holds one. A node whose name is built at render time
/// (`tables3::has_dynamic_name`) or is a placeholder the menu substitutes an index into (`known_differences.h`) cannot
/// equal a static `uint16_t Node::name`, so those cells assert only that both sides are present; the example
/// "excuses exactly the pinned names" pins which cells and how many. V1 cells compare the item only: their names are
/// the V3 rows' and `tables_3.tsv`'s.
std::string diff_cell(const Node* node, std::string_view table_name, std::string_view version, uint8_t x, uint8_t y) {
	spec::OracleTable& t = table();
	const std::string key = cell_key(table_name, version, x, y);
	const std::string where = std::string{table_name} + " " + std::string{version} + " (" + std::to_string(x) + ", "
	                          + std::to_string(y) + "): ";
	const std::string want = t.value(key, "item");
	const std::string got = node_paths(node);
	if (want != got) {
		return where + "port has " + got + ", main has " + want + "\n";
	}
	if (node == nullptr || version != "v3") {
		return {};
	}
	const std::vector<std::string>* name_row = t.find(key, "name");
	if (name_row == nullptr || name_row->empty()) {
		return where + "no name row\n";
	}
	const std::string name = text_of(node->name);
	if (spec::tables3::has_dynamic_name(*node) || spec::shortcut_name_is_placeholder(*node)) {
		return (name.empty() || name_row->front().empty()) ? where + "empty formatted name\n" : std::string{};
	}
	return name == name_row->front() ? std::string{}
	                                 : where + "port has " + name + ", main has " + name_row->front() + "\n";
}

/// @brief One reference table's 16x8 cells against the port layer, both ways: every row is asked of a cell, and every
///        cell of the layer answers a row.
/// @return Every disagreement, one per line; empty when the transcription is exact.
std::string diff_table(std::string_view table_name, std::string_view version, std::span<const Node* const> layer) {
	std::string out;
	if (layer.size() != size_t(menu::kShortcutCols) * menu::kShortcutRows) {
		return std::string{table_name} + ": layer is the wrong size\n";
	}
	for (uint8_t x = 0; x < menu::kShortcutCols; x++) {
		for (uint8_t y = 0; y < menu::kShortcutRows; y++) {
			out += diff_cell(layer[static_cast<size_t>(y) * menu::kShortcutCols + x], table_name, version, x, y);
		}
	}
	return out;
}

/// @brief A one-dimensional reference table against the port layer, which holds it in column 11: every other column
///        is empty.
std::string diff_column(std::string_view table_name, std::span<const Node* const> layer) {
	std::string out;
	if (layer.size() != size_t(menu::kShortcutCols) * menu::kShortcutRows) {
		return std::string{table_name} + ": layer is the wrong size\n";
	}
	for (uint8_t x = 0; x < menu::kShortcutCols; x++) {
		for (uint8_t y = 0; y < menu::kShortcutRows; y++) {
			const Node* cell = layer[static_cast<size_t>(y) * menu::kShortcutCols + x];
			if (x == 11) {
				out += diff_cell(cell, table_name, "v3", x, y);
			}
			else if (cell != nullptr) {
				out += std::string{table_name} + " (" + std::to_string(x) + ", " + std::to_string(y)
				       + "): port has a cell outside column 11\n";
			}
		}
	}
	return out;
}

/// @brief A root and the label its landing rows carry.
struct LandingRoot {
	Root root;
	const char* label;
	const char* pages_in; ///< the root whose tree the chain's pages are named in
};

/// @brief Every root with shortcut cells.
constexpr LandingRoot kLandingRoots[] = {
    {Root::Sound, "sound", "sound"},
    {Root::Drum, "drum", "drum"},
    {Root::MidiDrum, "midi drum", "midi drum"},
    {Root::GateDrum, "gate drum", "gate drum"},
    {Root::KitGlobalFx, "kit global fx", "kit global fx"},
    {Root::Song, "song", "song"},
    {Root::Performance, "performance", "song"},
    {Root::AudioClip, "audio clip", "audio clip"},
    {Root::MidiOrCv, "midi/cv", "midi/cv"},
};

/// @brief The reference table a root's layer was transcribed from, or null for a root with no shortcuts.
const char* table_of(Root root) {
	switch (root) {
	case Root::Sound:
	case Root::Drum:
		return "sounds";
	case Root::AudioClip:
		return "audio_clips";
	case Root::Song:
	case Root::Performance:
		return "song_view";
	case Root::KitGlobalFx:
		return "kit_global_fx";
	case Root::MidiOrCv:
	case Root::MidiDrum:
		return "midi_cv";
	case Root::GateDrum:
		return "gate_drum";
	default:
		return nullptr;
	}
}

/// @brief How the landing rows name @p page: its tree paths in the landing root's own tree, `-` for none, and
///        `group:<node>` for one of the menu groups the chains name, which no tree lists and whose own name is "None".
std::string page_label(const Node* page, std::string_view root) {
	const std::pair<const Node*, const char*> groups[] = {
	    {&nodes::arp_group, "group:arp_group"},
	    {&nodes::arp_group_kit, "group:arp_group_kit"},
	    {&nodes::arp_group_midi, "group:arp_group_midi"},
	    {&nodes::voice_group, "group:voice_group"},
	    {&nodes::env_group, "group:env_group"},
	    {&nodes::lfo_group, "group:lfo_group"},
	    {&nodes::reverb_group, "group:reverb_group"},
	    {&nodes::filters_group, "group:filters_group"},
	    {&nodes::global_filters_group, "group:global_filters_group"},
	    {&nodes::global_reverb_group, "group:global_reverb_group"},
	    {&nodes::source_group, "group:source_group"},
	};
	for (const auto& [group, label] : groups) {
		if (page == group) {
			return label;
		}
	}
	return node_paths(page, root);
}

/// @brief Every root, so a shape rule can be stated once over all of them.
const std::vector<Root>& all_roots() {
	static const std::vector<Root> list = [] {
		std::vector<Root> v;
		for (uint8_t i = 0; i < static_cast<uint8_t>(Root::Count); i++) {
			v.push_back(static_cast<Root>(i));
		}
		return v;
	}();
	return list;
}

} // namespace

// clang-format off
describe shortcuts_transcription("scenes::menu shortcut tables: transcription", $ {
	it("matches paramShortcutsForSounds cell for cell", _ {
		const FreeverbModel freeverb;
		install_sound_context();
		expect(diff_table("sounds", "v3", menu::shortcut_layer(Root::Sound, ShortcutsVersion::V3, /*second_layer=*/false)))
		    .to_equal(std::string{});
	});

	it("matches paramShortcutsForSoundsSecondLayer cell for cell", _ {
		const FreeverbModel freeverb;
		install_sound_context();
		expect(diff_table("sounds_second", "v3", menu::shortcut_layer(Root::Sound, ShortcutsVersion::V3, /*second_layer=*/true)))
		    .to_equal(std::string{});
	});

	it("matches paramShortcutsForAudioClips cell for cell", _ {
		const FreeverbModel freeverb;
		install_sound_context();
		expect(diff_table("audio_clips", "v3", menu::shortcut_layer(Root::AudioClip, ShortcutsVersion::V3, /*second_layer=*/false)))
		    .to_equal(std::string{});
	});

	it("matches paramShortcutsForSongView cell for cell, for both session roots", _ {
		const FreeverbModel freeverb;
		install_sound_context();
		expect(diff_table("song_view", "v3", menu::shortcut_layer(Root::Song, ShortcutsVersion::V3, /*second_layer=*/false)))
		    .to_equal(std::string{});
		expect(diff_table("song_view", "v3", menu::shortcut_layer(Root::Performance, ShortcutsVersion::V3, /*second_layer=*/false)))
		    .to_equal(std::string{});
	});

	it("matches paramShortcutsForKitGlobalFX cell for cell", _ {
		const FreeverbModel freeverb;
		install_sound_context();
		expect(diff_table("kit_global_fx", "v3", menu::shortcut_layer(Root::KitGlobalFx, ShortcutsVersion::V3, /*second_layer=*/false)))
		    .to_equal(std::string{});
	});

	it("matches midiOrCVParamShortcuts, in column 11", _ {
		select_language();
		// Column 11 is where a pad press and the blink index this table.
		expect(diff_column("midi_cv", menu::shortcut_layer(Root::MidiOrCv, ShortcutsVersion::V3, /*second_layer=*/false)))
		    .to_equal(std::string{});
	});

	it("matches gateDrumParamShortcuts, in column 11", _ {
		install_sound_context();
		expect(diff_column("gate_drum", menu::shortcut_layer(Root::GateDrum, ShortcutsVersion::V3, /*second_layer=*/false)))
		    .to_equal(std::string{});
	});

	it("names legacy's coming-soon cells, which shortcut_node leaves empty, and no others", _ {
		// A coming-soon cell answers with a popup and no navigation; the port transcribes it as an empty cell that
		// `shortcut_coming_soon` names. Roots with no table (settings, the note editors) have none.
		spec::OracleTable& t = table();
		std::string diffs;
		size_t soon = 0;
		for (const Root root : all_roots()) {
			const char* table_name = table_of(root);
			for (uint8_t x = 0; x < menu::kShortcutCols; x++) {
				for (uint8_t y = 0; y < menu::kShortcutRows; y++) {
					const bool want = table_name != nullptr && t.find(cell_key(table_name, "v3", x, y), "soon") != nullptr;
					soon += want ? 1 : 0;
					if (menu::shortcut_coming_soon(root, x, y) != want
					    || (want && menu::shortcut_node(root, ShortcutsVersion::V3, false, x, y) != nullptr)) {
						diffs += "root(" + std::to_string(static_cast<int>(root)) + ") (" + std::to_string(x) + ","
						         + std::to_string(y) + ") ";
					}
				}
			}
		}
		expect(diffs).to_equal(std::string{});
		expect(soon).to_be_greater_than(size_t(0)); // the rows named cells at all
		expect(menu::shortcut_coming_soon(Root::Sound, 4, 2)).to_be_true();
	});

	it("excuses exactly the pinned names", _ {
		// The cells compared by presence of both names rather than by equality: the nodes whose name is built at render
		// time, and the placeholder-named ones. Identity and count are pinned in `known_differences.h`; a node whose port
		// name carries a `*` and is in neither set would fail its cell, so this holds the sets to the layers.
		const FreeverbModel freeverb;
		install_sound_context();
		const std::span<const Node* const> layers[] = {
		    menu::shortcut_layer(Root::Sound, ShortcutsVersion::V3, false),         menu::shortcut_layer(Root::Sound, ShortcutsVersion::V3, true),
		    menu::shortcut_layer(Root::AudioClip, ShortcutsVersion::V3, false),     menu::shortcut_layer(Root::Song, ShortcutsVersion::V3, false),
		    menu::shortcut_layer(Root::KitGlobalFx, ShortcutsVersion::V3, false),   menu::shortcut_layer(Root::MidiOrCv, ShortcutsVersion::V3, false),
		    menu::shortcut_layer(Root::GateDrum, ShortcutsVersion::V3, false),
		};
		size_t dynamic = 0;
		size_t placeholder = 0;
		std::string stray;
		for (const auto& layer : layers) {
			for (const Node* node : layer) {
				if (node == nullptr) {
					continue;
				}
				dynamic += spec::tables3::has_dynamic_name(*node) ? 1 : 0;
				placeholder += spec::shortcut_name_is_placeholder(*node) ? 1 : 0;
				const bool starred = text_of(node->name).find('*') != std::string::npos;
				if (starred != spec::shortcut_name_is_placeholder(*node) && !spec::tables3::has_dynamic_name(*node)) {
					stray += node_paths(node) + " ";
				}
			}
		}
		expect(stray).to_equal(std::string{});
		expect(dynamic).to_equal(spec::kShortcutDynamicNameCells);
		expect(placeholder).to_equal(spec::kShortcutPlaceholderNameCells);
	});

	it("resolves the four inline MIDI/CV cells that are in no legacy table", _ {
		// `potentialShortcutPadAction`'s own `else if` chain, in the
		// arm `editingCVOrMIDIClip() || editingNonAudioDrumRow()` takes -- so all three of the roots
		// that arm covers, and none of the others.
		for (Root root : {Root::MidiOrCv, Root::MidiDrum, Root::GateDrum}) {
			expect(menu::shortcut_node(root, ShortcutsVersion::V3, false, 4, 7)).to_equal(&nodes::sequence_direction);
			expect(menu::shortcut_node(root, ShortcutsVersion::V3, false, 15, 1)).to_equal(&nodes::spread_velocity_midi);
			expect(menu::shortcut_node(root, ShortcutsVersion::V3, false, 15, 2)).to_equal(&nodes::randomizer_lock);
			expect(menu::shortcut_node(root, ShortcutsVersion::V3, false, 15, 3)).to_equal(&nodes::note_probability_midi);
			// The second layer is the same layer here, so the toggle must not lose them.
			expect(menu::shortcut_node(root, ShortcutsVersion::V3, true, 4, 7)).to_equal(&nodes::sequence_direction);
			// The `default:` of the randomizer switch in main's `SoundEditor::get_basic_shortcut_action`
			// (`origin/main:src/deluge/gui/ui/sound_editor.cpp`) -- the randomizer column is y = 1..3 and nothing else.
			expect(menu::shortcut_node(root, ShortcutsVersion::V3, false, 15, 0)).to_be_null();
			expect(menu::shortcut_node(root, ShortcutsVersion::V3, false, 15, 4)).to_be_null();
		}
		// Not the sound roots: (4, 7) there is `paramShortcutsForSounds`' own `sequenceDirectionMenu`
		// cell, and (15, 1..3) the *non*-MIDI randomizer trio -- both from the table, both blinkable.
		expect(menu::shortcut_node(Root::Sound, ShortcutsVersion::V3, false, 15, 1)).to_equal(&nodes::spread_velocity);
		expect(menu::shortcut_node(Root::AudioClip, ShortcutsVersion::V3, false, 15, 1)).to_be_null();
	});

	it("keeps those four cells out of the layers, so the blink never reports them", _ {
		// The asymmetry is the point: `updatePadLightsFor`'s MIDI/CV and gate-drum arms
		// (`SoundEditor::updatePadLightsFor`) scan column 11 and nothing else, so legacy navigates from
		// these pads and never lights them. Removing the resolve-only split would fail here.
		for (Root root : {Root::MidiOrCv, Root::MidiDrum, Root::GateDrum}) {
			const auto layer = menu::shortcut_layer(root, ShortcutsVersion::V3, false);
			expect(layer[size_t(7) * menu::kShortcutCols + 4]).to_be_null();
			for (uint8_t y = 1; y <= 3; y++) {
				expect(layer[size_t(y) * menu::kShortcutCols + 15]).to_be_null();
			}
			expect(menu::shortcut_pos(root, ShortcutsVersion::V3, nodes::sequence_direction).valid).to_be_false();
			expect(menu::shortcut_pos(root, ShortcutsVersion::V3, nodes::spread_velocity_midi).valid).to_be_false();
			expect(menu::shortcut_pos(root, ShortcutsVersion::V3, nodes::randomizer_lock).valid).to_be_false();
			expect(menu::shortcut_pos(root, ShortcutsVersion::V3, nodes::note_probability_midi).valid).to_be_false();
		}
	});


	it("pins every sound cell by the tree path of its item, not by name", _ {
		install_sound_context();
		// The name comparison is blind to a swap of two numbered sibling columns: `env1Release`/`env2Release`
		// (x = 8 / 9), `lfo1Rate`/`lfo2Rate` (12 / 13), `osc0Type`/`osc1Type` (2 / 3) and
		// `sample0Repeat`/`sample1Repeat` (0 / 1) have the same name, and `source0Volume`/`source1Volume` the same
		// *formatted* name, which the `*` tolerance downgrades to a presence check. The rows name the item by the
		// tree paths it sits at, so swapping any two columns fails here even though every name still matches.
		spec::OracleTable& t = table();
		const auto layer = menu::shortcut_layer(Root::Sound, ShortcutsVersion::V3, false);
		const auto second = menu::shortcut_layer(Root::Sound, ShortcutsVersion::V3, true);
		std::string bad;
		size_t pinned = 0;
		size_t pinned_in_column[menu::kShortcutCols]{};
		for (uint8_t x = 0; x < menu::kShortcutCols; x++) {
			for (uint8_t y = 0; y < menu::kShortcutRows; y++) {
				const size_t i = size_t(y) * menu::kShortcutCols + x;
				for (const auto& [name, port_cell] : {std::pair<const char*, const Node*>{"sounds", layer[i]},
				                                      std::pair<const char*, const Node*>{"sounds_second", second[i]}}) {
					const std::string item = t.value(cell_key(name, "v3", x, y), "item");
					if (item == "-") {
						continue;
					}
					// An item no root reaches by static children (`name:...`) is covered by the name column alone.
					if (item.starts_with("name:")) {
						continue;
					}
					pinned++;
					pinned_in_column[x]++;
					if (item != node_paths(port_cell)) {
						bad += std::string{name} + " (" + std::to_string(x) + ", " + std::to_string(y) + "): port has "
						       + node_paths(port_cell) + ", main has " + item + "\n";
					}
				}
			}
		}
		expect(bad).to_equal(std::string{});
		// Not vacuous, and it reaches the columns that matter.
		expect(pinned).to_be_greater_than(size_t(80));
		for (const uint8_t x : {uint8_t{0}, uint8_t{1}, uint8_t{2}, uint8_t{3}, uint8_t{8}, uint8_t{9}, uint8_t{12},
		                        uint8_t{13}}) {
			to_be_at_least(expect(pinned_in_column[x]), size_t(4));
		}
	});

	it("refuses the two kit global-FX cells legacy's own guard never let it press", _ {
		// The mirror of the four inline cells above. `paramShortcutsForKitGlobalFX` holds
		// `randomizerLockMenu` at (15, 2) and `randomizerNoteProbabilityMenu` at (15, 3), but the arm
		// is gated `(x <= kDisplayWidth - 2) || (x == 15 && y == 1)` (`SoundEditor::potentialShortcutPadAction`),
		// so legacy can only ever press column 15 at y == 1.
		expect(menu::shortcut_node(Root::KitGlobalFx, ShortcutsVersion::V3, false, 15, 1)).to_equal(&nodes::spread_velocity);
		expect(menu::shortcut_node(Root::KitGlobalFx, ShortcutsVersion::V3, false, 15, 2)).to_be_null();
		expect(menu::shortcut_node(Root::KitGlobalFx, ShortcutsVersion::V3, false, 15, 3)).to_be_null();
		expect(menu::shortcut_node(Root::KitGlobalFx, ShortcutsVersion::V3, true, 15, 2)).to_be_null();
		// The guard admits everything up to column 14, so nothing else is lost.
		expect(menu::shortcut_node(Root::KitGlobalFx, ShortcutsVersion::V3, false, 14, 0)).to_equal(&nodes::global_delay_rate);

		// But the blink side has no such guard (main's `SoundEditor::setupShortcutsBlinkFromTable`,
		// `origin/main:src/deluge/gui/ui/sound_editor.cpp`), so the two cells are still in the layer and still report a
		// pad. Removing the press gate, or applying it to the layer as well, fails one half of this.
		const auto layer = menu::shortcut_layer(Root::KitGlobalFx, ShortcutsVersion::V3, false);
		expect(layer[size_t(2) * menu::kShortcutCols + 15]).to_equal(&nodes::randomizer_lock);
		expect(layer[size_t(3) * menu::kShortcutCols + 15]).to_equal(&nodes::note_probability);
		const ShortcutPos lock = menu::shortcut_pos(Root::KitGlobalFx, ShortcutsVersion::V3, nodes::randomizer_lock);
		expect(lock.valid).to_be_true();
		expect(int32_t{lock.x}).to_equal(int32_t{15});
		expect(int32_t{lock.y}).to_equal(int32_t{2});

		// And no other root grows a gate: the sound table's own column 15 stays pressable.
		expect(menu::shortcut_node(Root::Sound, ShortcutsVersion::V3, false, 15, 2)).to_equal(&nodes::randomizer_lock);
	});

	it("swaps the name editor for the drum one on the two drum roots", _ {
		install_sound_context();
		// `SoundEditor::potentialShortcutPadAction` rewrites
		// `editNameMenu` to `drumNameEditMenu` for a kit output or a non-audio drum row; the port
		// bakes that into the drum roots' own layers.
		const auto at = [](Root root, uint8_t x, uint8_t y) { return menu::shortcut_node(root, ShortcutsVersion::V3, false, x, y); };
		expect(at(Root::Sound, 11, 5)).to_equal(&nodes::edit_name);
		expect(at(Root::Drum, 11, 5)).to_equal(&nodes::drum_name_edit);
		expect(at(Root::MidiOrCv, 11, 5)).to_equal(&nodes::edit_name);
		expect(at(Root::MidiDrum, 11, 5)).to_equal(&nodes::drum_name_edit);
		expect(at(Root::GateDrum, 11, 5)).to_equal(&nodes::drum_name_edit);

		// ... and nothing else about those layers differs.
		const auto sound = menu::shortcut_layer(Root::Sound, ShortcutsVersion::V3, false);
		const auto drum = menu::shortcut_layer(Root::Drum, ShortcutsVersion::V3, false);
		size_t differing = 0;
		for (size_t i = 0; i < sound.size(); i++) {
			differing += size_t(sound[i] != drum[i]);
		}
		expect(differing).to_equal(size_t(1));
	});
});

describe shortcuts_shape("scenes::menu shortcut tables: shape", $ {
	it("gives every root a full 16x8 layer, both layers", _ {
		for (Root root : all_roots()) {
			for (bool second : {false, true}) {
				expect(menu::shortcut_layer(root, ShortcutsVersion::V3, second).size())
				    .to_equal(size_t(menu::kShortcutCols) * menu::kShortcutRows);
			}
		}
	});

	it("resolves nothing outside the 16x8 grid", _ {
		for (Root root : all_roots()) {
			expect(menu::shortcut_node(root, ShortcutsVersion::V3, false, menu::kShortcutCols, 0)).to_be_null();
			expect(menu::shortcut_node(root, ShortcutsVersion::V3, false, 0, menu::kShortcutRows)).to_be_null();
			expect(menu::shortcut_node(root, ShortcutsVersion::V3, true, 255, 255)).to_be_null();
		}
	});

	it("gives only the two sound roots a distinct second layer", _ {
		for (Root root : all_roots()) {
			const bool distinct = menu::shortcut_layer(root, ShortcutsVersion::V3, true).data() != menu::shortcut_layer(root, ShortcutsVersion::V3, false).data();
			expect(distinct).to_equal(root == Root::Sound || root == Root::Drum);
		}
	});

	it("shares one table between the two session roots, and leaves settings empty", _ {
		expect(static_cast<const void*>(menu::shortcut_layer(Root::Song, ShortcutsVersion::V3, false).data()))
		    .to_equal(static_cast<const void*>(menu::shortcut_layer(Root::Performance, ShortcutsVersion::V3, false).data()));
		for (const Node* cell : menu::shortcut_layer(Root::Settings, ShortcutsVersion::V3, false)) {
			expect(cell).to_be_null();
		}
		for (const Node* cell : menu::shortcut_layer(Root::Settings, ShortcutsVersion::V3, true)) {
			expect(cell).to_be_null();
		}
	});

	it("falls back to the first layer where the second has no cell", _ {
		// Legacy's own rule (`SoundEditor::potentialShortcutPadAction`): the second layer *replaces* a cell only
		// where it has one, so a second press of a pad without a second-layer item re-opens the
		// first-layer one rather than nothing.
		expect(menu::shortcut_node(Root::Sound, ShortcutsVersion::V3, true, 8, 0)).to_equal(&nodes::env3_release);
		expect(menu::shortcut_node(Root::Sound, ShortcutsVersion::V3, false, 8, 0)).to_equal(&nodes::env1_release);
		expect(menu::shortcut_node(Root::Sound, ShortcutsVersion::V3, true, 8, 7)).to_equal(&nodes::lpf_freq);
		expect(menu::shortcut_node(Root::Sound, ShortcutsVersion::V3, false, 8, 7)).to_equal(&nodes::lpf_freq);
	});

	it("round-trips every cell through shortcut_pos", _ {
		for (Root root : all_roots()) {
			// The second layer is searched first, so a node that is only in the first layer must
			// still be found there, and one in both reports its second-layer pad.
			for (bool second : {true, false}) {
				const auto layer = menu::shortcut_layer(root, ShortcutsVersion::V3, second);
				for (uint8_t y = 0; y < menu::kShortcutRows; y++) {
					for (uint8_t x = 0; x < menu::kShortcutCols; x++) {
						const Node* cell = layer[static_cast<size_t>(y) * menu::kShortcutCols + x];
						if (cell == nullptr) {
							continue;
						}
						const ShortcutPos pos = menu::shortcut_pos(root, ShortcutsVersion::V3, *cell);
						expect(pos.valid).to_be_true();
						// ... and a press finds it again, EXCEPT where the arm's own guard excluded
						// the pad from the press path in the first place: legacy's kit global-FX arm
						// admits column 15 only at y == 1 (`SoundEditor::potentialShortcutPadAction`), while the
						// blink side scans the whole table. Spelled out here rather than skipped, so
						// this fails if the gate's shape changes.
						const bool pressable = root != Root::KitGlobalFx || pos.x <= 14 || pos.y == 1;
						expect(menu::shortcut_node(root, ShortcutsVersion::V3, second, pos.x, pos.y)).to_equal(pressable ? cell : nullptr);
					}
				}
			}
		}
	});

	it("reports no pad for a node with no shortcut anywhere", _ {
		// `root_settings` is a root; nothing in any table points at it.
		for (Root root : all_roots()) {
			expect(menu::shortcut_pos(root, ShortcutsVersion::V3, nodes::root_settings).valid).to_be_false();
		}
	});
});

describe shortcuts_validation("scenes::menu shortcut tables: validation", $ {
	it("points every cell at a node some root reaches", _ {
		select_language();
		// The `ShortcutToNothing` rule's real run: over `kAllRoots`, with the sweep left ON (every
		// other caller validates a *subset* of the tree and stands the sweep down, because a cell in
		// another root's table says nothing about the subtree under test).
		const auto issues = menu::validate_tree(nodes::kAllRoots, {}, {}, {},
		                                        menu::ValidateOptions{});
		size_t dangling = 0;
		for (const menu::Issue& i : issues) {
			if (i.kind == menu::Issue::Kind::ShortcutToNothing) {
				dangling++;
			}
		}
		expect(dangling).to_equal(size_t(0));
	});

	it("has exactly the custom-node count the cap is sized from, and does not trip the cap", _ {
		// `validate_tree`'s `TooManyCustom` rule counts custom *nodes*, not distinct `CustomItem`
		// instances (a shared item would collapse the count and the rule could never fire). This
		// pins the real number, which `validate.h`'s
		// `kMaxCustomNodes` is sized just above and `src/ui/scenes/README.md` quotes.
		//
		// The whole tree is enumerated through the id registry rather than re-walked here:
		// `assign_node_ids` numbers exactly the statically reachable set, 1..assigned.
		select_language();
		const uint16_t assigned = menu::assign_node_ids(nodes::kAllRoots);
		size_t custom_nodes = 0;
		size_t distinct_items = 0;
		std::set<const menu::CustomItem*> items;
		for (uint16_t id = 1; id <= assigned; id++) {
			const Node* n = menu::node_by_id(id);
			if (n != nullptr && n->kind == menu::Kind::Custom && n->custom != nullptr) {
				custom_nodes++;
				items.insert(n->custom);
			}
		}
		distinct_items = items.size();
		expect(custom_nodes).to_equal(menu::kShippedCustomNodes);
		to_be_at_most(expect(custom_nodes), menu::kMaxCustomNodes);
		// Every custom node has an item of its own and no two nodes share one, so there are as many
		// distinct instances as custom nodes (62) and `nodes::kPending` is not among them -- nothing
		// points at it. `output_items_spec.cpp` asserts the placeholder-free claim directly.
		//
		// Items are per-node rather than shared or parameterised by the node, because the state an
		// item acts on has to be a property of the item: `CustomItem::on_focus` and
		// `CustomItem::learn_*` are handed a `MenuContext` and a `MenuState`, never their own
		// `Node`, so an instance shared across the two oscillators (the `FileBrowserItem`s,
		// `MarkerItem`s and `AudioRecorderItem`s), across the three `EditNameItem` targets, the five
		// context-menu openers, the four `ActionItem`s, the two `ActiveScalesItem`s (one per
		// `ScaleSet`) or the twelve `MidiCommandItem`s (one per learnable `GlobalMIDICommand`) could
		// not tell which of its nodes it was being asked about. The families are
		// `BatteryLevelItem` and `FirmwareVersionItem`; `kEditClipName`, `kEditOutputName` and
		// `kEditDrumName` (`nodes/edit_name.h`); the `ContextMenuItem`s (`nodes/context_menu_items.h`);
		// `kMidiCommandPlayItem` .. `kMidiCommandShiftItem` (`nodes/midi_command.h`); `kPatchCablesItem`,
		// the two `SourceSelectionItem`s (regular/range), the two navigated `PatchCableStrengthItem`s
		// plus the two `Fixed` ones (`kVibratoItem`, `kSidechainVolumeItem`) and `kMultiRangeItem`;
		// `kCvSelectionItem`, `kGateSelectionItem` and `kMidiDevicesItem` (`nodes/dynamic.h`); the
		// four `ActionItem`s (`kStartStemExportItem`, `kSetClipLengthItem`, `kSongMidiLearnItem`,
		// `kConfigureSongMacrosItem`) and `kPerformEditorItem` (`nodes/actions.h`); the
		// `ActiveScalesItem`s (`nodes/scales.h`); the sample family (`nodes/sample.h`);
		// `kMidiDeviceLinked` and `kSpecificOutputSelector` (`nodes/output_items.h`); and
		// `kMpeDirectionItem`, `kMpeZoneItem` and `kMpeZoneChannelsItem` (`nodes/midi_device.h`), the
		// three pages of the MPE arm under the per-device MIDI page `midi_devices` chains to; and the five DX7 pages
		// (`nodes/dx.h`). Those
		// three are bespoke because their value is `MenuState` navigation state
		// (`mpe_direction`/`mpe_zone`) that a `Binding::get`/`set` -- handed only a
		// `const MenuContext&` -- cannot see, and the direction selector additionally computes its
		// title from that value.
		expect(distinct_items).to_equal(size_t(62));
		expect(distinct_items).to_equal(menu::kShippedCustomNodes); // one item per node, nothing shared

		const auto issues = menu::validate_tree(nodes::kAllRoots, {}, {}, {}, menu::ValidateOptions{});
		size_t too_many = 0;
		for (const menu::Issue& i : issues) {
			if (i.kind == menu::Issue::Kind::TooManyCustom) {
				too_many++;
			}
		}
		expect(too_many).to_equal(size_t(0));
	});

	it("owes no placeholder at all, and says so by identity rather than by a count", _ {
		// This is a SET rather than a count, because a count is a net with a hole in it: one
		// placeholder finished and another added somewhere else would cancel out, and the number that
		// says "the tree is not done" would not move.
		//
		// **The set is empty**: the file/sample family (`nodes/sample.h`) and
		// `nodes::kMidiDeviceLinked` / `nodes::kSpecificOutputSelector` (`nodes/output_items.h`) all
		// have real items. The example is kept, empty: it is the assertion a table that adds a *new*
		// placeholder fails, and it is the only one that names which node arrived.
		//
		// Named nodes, not string ids: two oscillators declare `FILE_BROWSER` under the same name
		// (`nodes/sound_osc.h`'s `SOURCE_NODES` macro), so a set keyed on the label could not tell
		// them apart. The comparison prints `id=name-string-id` pairs in node-id order, so a failure
		// says which node arrived rather than only that a count moved.
		select_language();
		const uint16_t assigned = menu::assign_node_ids(nodes::kAllRoots);

		// Sorted by node id, not by pointer: a `std::set<const Node*>` orders by address, which
		// varies with link order and would make the two sides of a failure hard to read against each
		// other.
		const auto describe = [](const std::set<const Node*>& nodes_in) {
			std::vector<std::string> parts;
			parts.reserve(nodes_in.size());
			for (const Node* n : nodes_in) {
				const uint16_t id = menu::node_id(*n);
				std::string entry = std::to_string(id);
				entry.insert(0, 4 - std::min<size_t>(4, entry.size()), '0'); // fixed width, so the sort is numeric
				parts.push_back(entry + "=" + std::to_string(n->name));
			}
			std::sort(parts.begin(), parts.end());
			std::string out;
			for (const std::string& part : parts) {
				out += part + " ";
			}
			return out.empty() ? std::string{"(none)"} : out;
		};
		const std::set<const Node*> want{}; // `menu::kPendingCustomNodes` is 0
		static_assert(menu::kPendingCustomNodes == 0);

		std::set<const Node*> found;
		for (uint16_t id = 1; id <= assigned; id++) {
			const Node* n = menu::node_by_id(id);
			if (n != nullptr && n->kind == menu::Kind::Custom && n->custom != nullptr && n->custom->is_placeholder()) {
				found.insert(n);
			}
		}
		expect(describe(found)).to_equal(describe(want));

		// And the rule itself agrees with the walk. There is no option that suppresses `PlaceholderCustom` (`validate.h`), so a
		// default-constructed `ValidateOptions` IS the strict one. (The
		// shortcut sweep stays on -- this is `kAllRoots`, the one caller for which it means
		// anything.)
		const auto strict = menu::validate_tree(nodes::kAllRoots, {}, {}, {}, menu::ValidateOptions{});
		std::set<const Node*> reported;
		for (const menu::Issue& i : strict) {
			if (i.kind == menu::Issue::Kind::PlaceholderCustom) {
				reported.insert(i.node);
			}
		}
		expect(describe(reported)).to_equal(describe(want));
	});

	it("numbers the tree when the application's registry is built, not only when a spec asks", _ {
		// `MenuScene::save_state`, `restore_state` and the shortcut-pad jump silently no-op unless
		// `assign_node_ids` has run, so `ui::scenes::assigned_node_ids()` is called from both
		// `menu_scene()` and `scene_registry()` and an application that built only its registry
		// still has numbered nodes.
		//
		// Written so it does not depend on which spec ran before it: the registry's own count is
		// compared against a fresh numbering of the very set it numbers, and the roots are then
		// checked against *that*.
		const uint16_t at_registry = deluge::ui::scenes::assigned_node_ids();
		expect(at_registry).to_be_greater_than(uint16_t(0));
		expect(menu::assign_node_ids(menu::nodes::kAllRoots)).to_equal(at_registry);
		for (const Node* root : menu::MenuScene::roots()) {
			expect(root).not_().to_be_null();
			if (root != nullptr) {
				expect(menu::node_id(*root)).not_().to_equal(uint16_t(0));
			}
		}
	});

	it("gives every shortcut target a node id, so the pad jump can name it", _ {
		(void)menu::assign_node_ids(nodes::kAllRoots);
		for (Root root : all_roots()) {
			for (bool second : {false, true}) {
				for (const Node* cell : menu::shortcut_layer(root, ShortcutsVersion::V3, second)) {
					if (cell != nullptr) {
						expect(menu::node_id(*cell)).not_().to_equal(uint16_t(0));
					}
				}
			}
		}
	});
});

describe shortcuts_profile("shell::BoardProfile without the shortcuts field", $ {
	it("still builds and runs a Shell over deluge_profile()", _ {
		// `BoardProfile` has no shortcuts field, and the profile the application uses builds and runs a `Shell`.
		const deluge::ui::shell::BoardProfile& p = deluge_profile();
		uitest::FakeSurfaceSet fake(deluge::ui::shell::kDelugeBoard, p);
		deluge::ui::shell::SceneRegistry reg = scene_registry();
		deluge::ui::shell::Shell shell{fake, p, reg, deluge::ui::shell::Services{}, deluge::ui::shell::ShellConfig{}};
		shell.init(deluge::ui::shell::SceneId{deluge::ui::shell::SceneKind::Null, 0});
		shell.tick(0);
		expect(p.sidebar_cols).to_equal(uint8_t(2));
	});
});
// clang-format on

// clang-format off
describe shortcuts_version_1("scenes::menu shortcut tables: the SHORTCUTS_VERSION_1 layer", $ {
	it("matches paramShortcutsForSounds cell for cell once switched to it", _ {
		const FreeverbModel freeverb;
		install_sound_context();
		// The same cell-for-cell sweep the V3 group runs, against the cells main's `setShortcutsVersion` rewrites. This
		// is what makes `nodes::shortcuts_version` a setting that selects something.
		expect(diff_table("sounds", "v1", menu::shortcut_layer(Root::Sound, ShortcutsVersion::V1, /*second_layer=*/false)))
		    .to_equal(std::string{});
	});

	it("moves exactly the two sound cells that carry a different item under V1", _ {
		install_sound_context();
		// Main's V1 arm assigns six cells of this table but four of them write the value they already held -- so naming
		// the two that move, and asserting the other four did not, is the difference between transcribing the *arm*
		// and transcribing its *effect*. Interpolation gives way to the sample end point; nothing else changes.
		spec::OracleTable& t = table();
		std::string moved;
		for (uint8_t x = 0; x < menu::kShortcutCols; x++) {
			for (uint8_t y = 0; y < menu::kShortcutRows; y++) {
				if (t.value(cell_key("sounds", "v1", x, y), "item") != t.value(cell_key("sounds", "v3", x, y), "item")) {
					moved += "(" + std::to_string(x) + "," + std::to_string(y) + ") ";
				}
			}
		}
		expect(moved).to_equal(std::string{"(0,6) (1,6) "});

		const Node* before_0_6 = menu::shortcut_node(Root::Sound, ShortcutsVersion::V3, false, 0, 6);
		const Node* before_1_6 = menu::shortcut_node(Root::Sound, ShortcutsVersion::V3, false, 1, 6);
		expect(before_0_6).to_equal(&nodes::sample0_interpolation);
		expect(before_1_6).to_equal(&nodes::sample1_interpolation);

		expect(menu::shortcut_node(Root::Sound, ShortcutsVersion::V1, false, 0, 6)).to_equal(&nodes::sample0_end);
		expect(menu::shortcut_node(Root::Sound, ShortcutsVersion::V1, false, 1, 6)).to_equal(&nodes::sample1_end);
		expect(t.value(cell_key("sounds", "v1", 0, 6), "item")).to_equal(node_paths(&nodes::sample0_end));
		expect(t.value(cell_key("sounds", "v1", 1, 6), "item")).to_equal(node_paths(&nodes::sample1_end));

		// The four no-op assignments.
		expect(menu::shortcut_node(Root::Sound, ShortcutsVersion::V1, false, 2, 6)).to_equal(&nodes::source0_wave_index);
		expect(menu::shortcut_node(Root::Sound, ShortcutsVersion::V1, false, 3, 6)).to_equal(&nodes::source1_wave_index);
		expect(menu::shortcut_node(Root::Sound, ShortcutsVersion::V1, false, 2, 7)).to_equal(&nodes::noise);
		expect(menu::shortcut_node(Root::Sound, ShortcutsVersion::V1, false, 3, 7)).to_equal(&nodes::osc_sync);

		// And the drum root, which reads the same table through its own name-editor swap.
		expect(menu::shortcut_node(Root::Drum, ShortcutsVersion::V1, false, 0, 6)).to_equal(&nodes::sample0_end);
		expect(menu::shortcut_node(Root::Drum, ShortcutsVersion::V1, false, 11, 5)).to_equal(&nodes::drum_name_edit);
	});

	it("matches paramShortcutsForAudioClips: the marker editor on START at (0..1, 7) and on END at (0..1, 6)", _ {
		const FreeverbModel freeverb;
		install_sound_context();
		// V1 puts the marker editor's *start* item at `(0..1, 7)` and its *end* item at `(0..1, 6)`. The start item has
		// no name and no root lists it, so its frozen rows read `name:`; the port's `audio_clip_waveform_start` is that
		// nameless unlisted node, and every cell compares against its frozen row.
		spec::OracleTable& t = table();
		std::span<const Node* const> layer = menu::shortcut_layer(Root::AudioClip, ShortcutsVersion::V1, /*second_layer=*/false);
		std::string diffs;
		for (uint8_t x = 0; x < menu::kShortcutCols; x++) {
			for (uint8_t y = 0; y < menu::kShortcutRows; y++) {
				diffs += diff_cell(layer[static_cast<size_t>(y) * menu::kShortcutCols + x], "audio_clips", "v1", x, y);
			}
		}
		expect(diffs).to_equal(std::string{});

		for (uint8_t x = 0; x < 2; x++) {
			expect(t.value(cell_key("audio_clips", "v1", x, 7), "item")).to_equal(std::string{"name:"});
			expect(t.value(cell_key("audio_clips", "v1", x, 6), "item")).to_equal(node_paths(&nodes::audio_clip_waveform));
			expect(menu::shortcut_node(Root::AudioClip, ShortcutsVersion::V1, false, x, 7)).to_equal(&nodes::audio_clip_waveform_start);
			expect(menu::shortcut_node(Root::AudioClip, ShortcutsVersion::V1, false, x, 6)).to_equal(&nodes::audio_clip_waveform);
		}
	});

	it("blinks no pad while the audio clip's marker editor is up on START or END, as updatePadLightsFor does", _ {
		MenuContext ctx = install_sound_context();
		ctx.root = Root::AudioClip;
		const spec::ShortcutLayout guard{ShortcutsVersion::V1};
		const MenuState st{};
		expect(menu::plan_blinks(ctx, st, nodes::audio_clip_waveform_start)).to_equal(menu::BlinkPlan{});
		expect(menu::plan_blinks(ctx, st, nodes::audio_clip_waveform)).to_equal(menu::BlinkPlan{});
		// An item with a pad of its own does blink there.
		expect(menu::plan_blinks(ctx, st, nodes::audio_clip_reverse).param.valid).to_be_true();
	});

	it("leaves every other root's layer alone, and the V3 layer as it was", _ {
		install_sound_context();
		// The version selects a layer here rather than editing one, so the V3 layer is untouched by V1.
		const Node* song_before = menu::shortcut_node(Root::Song, ShortcutsVersion::V3, false, 0, 7);
		const Node* second_before = menu::shortcut_node(Root::Sound, ShortcutsVersion::V3, true, 8, 0);
		const Node* midi_before = menu::shortcut_node(Root::MidiOrCv, ShortcutsVersion::V3, false, 11, 0);
		expect(menu::shortcut_node(Root::Song, ShortcutsVersion::V1, false, 0, 7)).to_equal(song_before);
		expect(menu::shortcut_node(Root::Sound, ShortcutsVersion::V1, true, 8, 0)).to_equal(second_before);
		expect(menu::shortcut_node(Root::MidiOrCv, ShortcutsVersion::V1, false, 11, 0)).to_equal(midi_before);
		expect(menu::shortcut_node(Root::Sound, ShortcutsVersion::V3, false, 0, 6)).to_equal(&nodes::sample0_interpolation);
		expect(menu::shortcut_node(Root::AudioClip, ShortcutsVersion::V3, false, 0, 6)).to_equal(&nodes::audio_clip_interpolation);
	});

	it("finds the V1 pad through shortcut_pos too, so the blink follows the version", _ {
		install_sound_context();
		// The reverse lookup reads the same layers, so it must move with them -- otherwise the pad a press opens and
		// the pad that blinks for the item it opened would disagree.
		const ShortcutPos pos = menu::shortcut_pos(Root::Sound, ShortcutsVersion::V1, nodes::sample0_end);
		expect(pos.valid).to_be_true();
		expect(pos.x).to_equal(uint8_t(0));
		expect(pos.y).to_equal(uint8_t(6));
	});
});

describe shortcut_blink_cadence("scenes::menu shortcut blink: legacy's counter", $ {
	using deluge::ui::widgets::grid::FlashColour;
	using deluge::ui::widgets::grid::PadFlash;

	// `SoundEditor::blinkShortcut` (`origin/main:src/deluge/gui/ui/sound_editor.cpp`): the counter's even steps are the
	// 20 ms source phase, the odd steps the 180 ms param phase; `counter >> 1` is the cycle the
	// frequency masks are tested against.

	/// Every flash `fire_blink_step` makes for @p plan at each of @p steps, tagged with its step.
	const auto fire = [](const menu::BlinkPlan& plan, uint32_t first, uint32_t last) {
		std::vector<std::pair<uint32_t, PadFlash>> out;
		for (uint32_t step = first; step <= last; step++) {
			deluge::ui::test::GridRig rig;
			menu::fire_blink_step(rig.frame, plan, step);
			for (const PadFlash& f : rig.flashes.flashes()) {
				out.emplace_back(step, f);
			}
		}
		return out;
	};

	it("steps legacy's counter: 20 ms source phase, 180 ms param phase", _ {
		const uint32_t anchor = 1000;
		const std::pair<uint32_t, uint32_t> steps[] = {{0, 0}, {19, 0}, {20, 1}, {199, 1}, {200, 2}, {219, 2}, {220, 3}};
		for (const auto& [offset, step] : steps) {
			expect(menu::blink_step(anchor, anchor + offset)).to_equal(step);
		}
		expect(menu::ms_to_next_step(anchor, anchor)).to_equal(uint32_t{20});
		expect(menu::ms_to_next_step(anchor, anchor + 20)).to_equal(uint32_t{180});
		expect(menu::ms_to_next_step(anchor, anchor + 190)).to_equal(uint32_t{10});
		expect(uint32_t{menu::kBlinkCycleMs}).to_equal(uint32_t{200});
	});

	it("flashes a frequency-3 param pad on every fourth cycle only", _ {
		menu::BlinkPlan plan;
		plan.param = menu::ParamBlink{.x = 6, .y = 0, .valid = true, .freq_mask = 3};
		const auto got = fire(plan, 1, 17);
		expect(got.size()).to_equal(size_t(3));
		const uint32_t want_steps[] = {1, 9, 17};
		for (size_t i = 0; i < got.size() && i < 3; i++) {
			expect(got[i].first).to_equal(want_steps[i]);
			expect(got[i].second).to_equal(PadFlash{6, 0, FlashColour::Default});
		}
	});

	it("flashes a frequency-0 param pad on every cycle, a frequency-1 one on every other", _ {
		menu::BlinkPlan every;
		every.param = menu::ParamBlink{.x = 8, .y = 7, .valid = true, .freq_mask = 0};
		const auto a = fire(every, 0, 7);
		expect(a.size()).to_equal(size_t(4)); // steps 1, 3, 5, 7
		for (size_t i = 0; i < a.size(); i++) {
			expect(a[i].first).to_equal(uint32_t(2 * i + 1));
		}

		menu::BlinkPlan other;
		other.param = menu::ParamBlink{.x = 8, .y = 7, .valid = true, .freq_mask = 1};
		const auto b = fire(other, 0, 7);
		expect(b.size()).to_equal(size_t(2)); // steps 1, 5: cycles 0 and 2
		const uint32_t want_steps[] = {1, 5};
		for (size_t i = 0; i < b.size() && i < 2; i++) {
			expect(b[i].first).to_equal(want_steps[i]);
		}
	});

	it("flashes sources on the even steps at pad 14 + sx, and alternates to yellow on odd cycles when asked", _ {
		menu::BlinkPlan plan;
		plan.sources[1][1] = menu::SourceBlink{.freq_mask = 0, .colour = FlashColour::Default, .alternates = true};
		const auto got = fire(plan, 0, 5);
		expect(got.size()).to_equal(size_t(3)); // steps 0, 2, 4; nothing on the odd steps
		const std::pair<uint32_t, PadFlash> want[] = {{0, {15, 1, FlashColour::Default}},
		                                              {2, {15, 1, FlashColour::Yellow}},
		                                              {4, {15, 1, FlashColour::Default}}};
		for (size_t i = 0; i < got.size() && i < 3; i++) {
			expect(got[i].first).to_equal(want[i].first);
			expect(got[i].second).to_equal(want[i].second);
		}

		menu::BlinkPlan slow;
		slow.sources[0][4] = menu::SourceBlink{.freq_mask = 3, .colour = FlashColour::Palette6, .alternates = true};
		const auto s = fire(slow, 0, 17);
		expect(s.size()).to_equal(size_t(3)); // cycles 0, 4, 8
		const std::pair<uint32_t, PadFlash> want_slow[] = {{0, {14, 4, FlashColour::Palette6}},
		                                                   {8, {14, 4, FlashColour::Yellow}},
		                                                   {16, {14, 4, FlashColour::Palette6}}};
		for (size_t i = 0; i < s.size() && i < 3; i++) {
			expect(s[i].first).to_equal(want_slow[i].first);
			expect(s[i].second).to_equal(want_slow[i].second);
		}
	});
});
// clang-format on

namespace {

std::string colour_name(deluge::ui::widgets::grid::FlashColour c) {
	return deluge::ui::scenes::spec::shortcuts::colour_name(static_cast<uint8_t>(c));
}

/// @brief The port's blink plan, spelled as the rows spell main's.
std::string port_plan(const menu::BlinkPlan& plan) {
	std::string out;
	if (plan.param.valid) {
		out += "param " + std::to_string(plan.param.x) + "," + std::to_string(plan.param.y) + " f"
		       + std::to_string(plan.param.freq_mask) + " " + colour_name(plan.param.colour) + ";";
		for (int32_t sx = 0; sx < 2; sx++) {
			for (int32_t y = 0; y < kDisplayHeight; y++) {
				const menu::SourceBlink& s = plan.sources[sx][y];
				if (s.freq_mask != menu::kNoBlink) {
					out += " src " + std::to_string(sx) + "," + std::to_string(y) + " f" + std::to_string(s.freq_mask)
					       + " " + colour_name(s.colour) + (s.alternates ? " alt" : "");
				}
			}
		}
	}
	return out;
}

} // namespace

// clang-format off
describe shortcut_blink_plan("scenes::menu shortcut blink plan against main's shortcut blink plans", $ {
	it("plans the blinks main's updatePadLightsFor plans, for every patched and patch-cable item", _ {
		const MenuContext& ctx = install_sound_context();
		UiStackGuard ui;
		setRootUILowLevel(&instrumentClipView);
		expect(seed_blink_cables(ctx)).to_be_true();
		namespace p = deluge::modulation::params;
		spec::OracleTable& t = table();
		std::string bad;
		size_t compared = 0;
		size_t with_sources = 0;
		const auto check = [&](const std::string& where, const Node& node, const MenuState& st) {
			const std::vector<std::string>* row = t.find("blink/" + where, "plan");
			const std::string got = port_plan(menu::plan_blinks(ctx, st, node));
			compared++;
			if (row == nullptr || row->empty()) {
				bad += where + "\n  no row\n";
				return;
			}
			const std::string& want = row->front();
			with_sources += want.find(" src ") != std::string::npos ? 1 : 0;
			if (want != got) {
				bad += where + "\n  main: " + want + "\n  port: " + got + "\n";
			}
		};

		// Every leaf of the sound root.
		for (const auto& e : spec::tree_entries("sound", nodes::sound_root)) {
			// The patch cables page plans from its own timer, compared below.
			if (e.node->kind == menu::Kind::Submenu || e.node == &nodes::patch_cables) {
				continue;
			}
			MenuState st{};
			st.path[0] = e.node;
			check("leaf " + e.path, *e.node, st);
		}

		// The source pickers and strength pages, over params on both layers and the volume triple.
		const uint8_t kParams[] = {p::LOCAL_LPF_FREQ, p::GLOBAL_VOLUME_POST_FX, p::LOCAL_VOLUME,
		                           p::GLOBAL_VOLUME_POST_REVERB_SEND, p::LOCAL_ENV_2_ATTACK, p::LOCAL_PITCH_ADJUST};
		const PatchSource kSources[] = {PatchSource::ENVELOPE_1, PatchSource::ENVELOPE_3, PatchSource::LFO_GLOBAL_1,
		                                PatchSource::AFTERTOUCH, PatchSource::RANDOM};
		for (const uint8_t param : kParams) {
			// Main's rows name the param by its number there, which is its id
			const uint32_t id = deluge::instruments::classic::flat_param_id(param).value;
			MenuState st{};
			st.walk.patching_param = param;
			st.path[0] = &nodes::source_selection_regular;
			check("regular source list p" + std::to_string(id), nodes::source_selection_regular, st);
			for (const PatchSource source : kSources) {
				st.walk.patch_source = source;
				const std::string tag = " p" + std::to_string(id) + " s" + std::to_string(static_cast<int>(source));
				st.path[0] = &nodes::patch_cable_strength_regular;
				check("regular strength" + tag, nodes::patch_cable_strength_regular, st);
				st.path[0] = &nodes::source_selection_range;
				check("range source list" + tag, nodes::source_selection_range, st);
				for (const PatchSource range : {PatchSource::LFO_LOCAL_1, PatchSource::VELOCITY}) {
					st.walk.patch_range_source = range;
					st.path[0] = &nodes::patch_cable_strength_range;
					check("range strength" + tag + " r" + std::to_string(static_cast<int>(range)),
					      nodes::patch_cable_strength_range, st);
				}
			}
		}

		// The patch cables page, on every cable: it plans from its own throttled timer.
		PatchCableSet* set = spec::menu_engine(ctx, *sound_fixture().song).params->getPatchCableSetAllowJibberish();
		for (int32_t i = 0; i < set->numPatchCables; i++) {
			MenuState st{};
			st.path[0] = &nodes::patch_cables;
			st.list[0].cursor = static_cast<int16_t>(i);
			check("patch cables row " + std::to_string(i), nodes::patch_cables, st);
		}
		expect(bad).to_equal(std::string{});
		expect(compared).to_be_greater_than(size_t(100));
		expect(with_sources).to_be_greater_than(size_t(10));
	});

	it("plans no source blink on a sound-root entry that has no param pad", _ {
		// `plan_blinks` plans sources only behind a param pad. No sound-root entry reaches that guard with a source to
		// blink (a padless source list finds no patched source), so what this holds is the consequence: the entries
		// without a pad, counted so the example is not vacuous, plan no source.
		const MenuContext& ctx = install_sound_context();
		expect(seed_blink_cables(ctx)).to_be_true();
		size_t padless = 0;
		size_t with_pad = 0;
		for (const auto& e : spec::tree_entries("sound", nodes::sound_root)) {
			MenuState st{};
			st.path[0] = e.node;
			const menu::BlinkPlan plan = menu::plan_blinks(ctx, st, *e.node);
			if (!plan.param.valid) {
				padless++;
				expect(plan.any_source()).to_be_false();
			} else {
				with_pad++;
			}
		}
		expect(padless).to_equal(spec::kPadlessShortcutNodes);
		expect(with_pad).to_be_greater_than(size_t(100));
	});

	it("folds the volume triple to POST_FX's pad on a strength page", _ {
		namespace p = deluge::modulation::params;
		const MenuContext& ctx = install_sound_context();
		MenuState post_fx{};
		post_fx.walk.patching_param = p::GLOBAL_VOLUME_POST_FX;
		post_fx.walk.patch_source = PatchSource::AFTERTOUCH;
		const menu::BlinkPlan want = menu::plan_blinks(ctx, post_fx, nodes::patch_cable_strength_regular);
		expect(want.param.valid).to_be_true();
		for (const uint8_t param : {uint8_t{p::LOCAL_VOLUME}, uint8_t{p::GLOBAL_VOLUME_POST_REVERB_SEND}}) {
			MenuState st = post_fx;
			st.walk.patching_param = param;
			const menu::BlinkPlan got = menu::plan_blinks(ctx, st, nodes::patch_cable_strength_regular);
			expect(got.param).to_equal(want.param);
		}
	});
});

describe shortcut_overlay_colours("scenes::menu SHIFT overlay colours against main's shortcut colours", $ {
	it("paints main's shortcut colours cell by cell for the sound root", _ {
		const MenuContext& ctx = install_sound_context();
		UiStackGuard ui;
		setRootUILowLevel(&instrumentClipView); // the sound tables are a clip view's
		spec::OracleTable& t = table();
		deluge::ui::test::GridRig rig;
		menu::paint_shortcut_colours(rig.frame, ctx);
		std::string bad;
		size_t lit = 0;
		for (int32_t x = 0; x < kDisplayWidth; x++) {
			for (int32_t y = 0; y < kDisplayHeight; y++) {
				const std::string key = "overlay/sound/" + std::to_string(x) + "," + std::to_string(y);
				const std::vector<std::string>* row = t.find(key, "rgb");
				const RGB got = rig.frame.peek(x, y);
				const std::string ours = std::to_string(got.r) + "," + std::to_string(got.g) + "," + std::to_string(got.b);
				if (row == nullptr || row->size() != 3) {
					bad += "(" + std::to_string(x) + "," + std::to_string(y) + ") no row\n";
					continue;
				}
				const std::string main_rgb = (*row)[0] + "," + (*row)[1] + "," + (*row)[2];
				lit += main_rgb == "0,0,0" ? 0 : 1;
				if (main_rgb != ours) {
					bad += "(" + std::to_string(x) + "," + std::to_string(y) + ") main " + main_rgb + " ours " + ours + "\n";
				}
			}
		}
		expect(bad).to_equal(std::string{});
		expect(lit).to_be_greater_than(size_t(60));
	});
});

describe shortcut_landing("scenes::menu shortcut landing against main's chain search", $ {
	it("lands every shortcut cell of every root on the page legacy's chain search finds", _ {
		const bool was_on = runtimeFeatureSettings.isOn(RuntimeFeatureSettingType::HorizontalMenus);
		runtimeFeatureSettings.set(RuntimeFeatureSettingType::HorizontalMenus, RuntimeFeatureStateToggle::On);
		spec::OracleTable& t = table();
		std::string bad;
		size_t compared = 0;
		size_t on_a_page = 0;
		size_t shared = 0;
		size_t outside = 0;
		size_t by_name = 0;
		for (const LandingRoot& lr : kLandingRoots) {
			for (const bool second : {false, true}) {
				if (second && menu::shortcut_layer(lr.root, ShortcutsVersion::V3, true).data() == menu::shortcut_layer(lr.root, ShortcutsVersion::V3, false).data()) {
					continue; // the root has no second layer of its own
				}
				for (uint8_t x = 0; x < menu::kShortcutCols; x++) {
					for (uint8_t y = 0; y < menu::kShortcutRows; y++) {
						const Node* node =
						    menu::shortcut_layer(lr.root, ShortcutsVersion::V3, second)[static_cast<size_t>(y) * menu::kShortcutCols + x];
						if (node == nullptr) {
							continue;
						}
						const std::string key = "land/" + std::string{lr.label} + (second ? "/2/" : "/")
						                        + std::to_string(x) + "," + std::to_string(y);
						const std::string where = key + " " + text_of(node->name) + ": ";
						const std::vector<std::string>* row = t.find(key, "page");
						if (row == nullptr || row->empty()) {
							bad += where + "no row\n";
							continue;
						}
						compared++;
						const std::string got = page_label(menu::shortcut_landing_page(lr.root, *node), lr.pages_in);
						on_a_page += row->front() == "-" ? 0 : 1;
						const Node* port_page = menu::shortcut_landing_page(lr.root, *node);
						outside += row->front().starts_with("outside:") ? 1 : 0;
						by_name += row->front().starts_with("name:") ? 1 : 0;
						shared += port_page != nullptr && !row->front().starts_with("outside:")
						                  && !row->front().starts_with("name:") && !row->front().starts_with("group:")
						                  && row->front() != node_paths(port_page)
						              ? 1
						              : 0;
						if (row->front() != got) {
							bad += where + "main lands on " + row->front() + ", the port on " + got + "\n";
						}
					}
				}
			}
		}
		runtimeFeatureSettings.set(RuntimeFeatureSettingType::HorizontalMenus,
		                           was_on ? RuntimeFeatureStateToggle::On : RuntimeFeatureStateToggle::Off);
		expect(bad).to_equal(std::string{});
		expect(compared).to_be_greater_than(size_t(150));
		expect(on_a_page).to_be_greater_than(size_t(80));
		// The pinned departures from a plain path comparison. A page the port shares across roots is named by where it
		// sits in the landing root's own tree; the song and performance pad (7, 5) opens a page that is in no path of
		// their trees. The four sample-recorder pads (`sound` and `drum`, columns 0 and 1, row 4) open a page the trees
		// do not list, which the rows pin by its name.
		expect(shared).to_equal(spec::kLandingSharedPageCells);
		expect(outside).to_equal(spec::kLandingOutsideRootCells);
		expect(by_name).to_equal(spec::kLandingUnlistedPageCells);
	});
});

describe shortcuts_reference_rows("shortcuts: the reference rows", $ {
	it("parses shortcuts.tsv clean and asks every row of it", _ {
		spec::OracleTable& t = spec::OracleTable::named("shortcuts");
		expect(t.problems().size()).to_equal(size_t{0});
		expect(t.size()).to_be_greater_than(size_t{2000});
		expect(t.unasked()).to_equal(std::string{});
	});
});
// clang-format on

DELUGE_SPEC(shortcuts_spec, shortcuts_transcription, shortcuts_shape, shortcuts_validation, shortcuts_profile,
            shortcuts_version_1, shortcut_blink_cadence, shortcut_blink_plan, shortcut_overlay_colours,
            shortcut_landing, shortcuts_reference_rows)
