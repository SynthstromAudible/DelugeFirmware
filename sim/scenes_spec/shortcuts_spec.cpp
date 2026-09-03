/// @file
/// @brief The seven shortcut tables, cross-checked cell by cell against the legacy arrays they were
///        transcribed from.
///
/// Transcription is where errors hide, so nothing here trusts the port's own table: every `(x, y)`
/// of every legacy array is compared with the cell `shortcut_layer` reports, **and** every non-null
/// port cell is compared back the other way, so neither a dropped cell nor an invented one can
/// pass. On top of that:
///
///   * the layers' shape and bounds (`shortcut_node` outside 16x8 is null);
///   * which roots share a table, and which two have a distinct second layer;
///   * the name-editor swap the two drum roots carry (`drumNameEditMenu`);
///   * `shortcut_node`'s second-layer fallback (legacy replaces a cell only where the second layer
///     has one);
///   * `shortcut_pos` round-trips every cell of every layer;
///   * `validate_tree` over `nodes::kAllRoots` with the shortcut sweep **on** -- the
///     `ShortcutToNothing` rule's real run, which is what says every cell points at a node some
///     root actually reaches;
///   * a `Shell` built with `scenes::deluge_profile()` still runs with no `BoardProfile::shortcuts`
///     field (the removal's regression net).
///
/// @note The SPEC may include `gui/menu_item/` and `gui/ui/menus.h`; the library may not
///       (`scripts/check_layer_rules.py`'s `scenes-includes-outside`). Same rule the `tables_*`
///       specs run under.

#include "scenes/menu/context.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/menu/node.h"
#include "scenes/menu/node_id.h"
#include "scenes/menu/nodes/nodes.h"
#include "scenes/menu/shortcuts.h"
#include "scenes/menu/validate.h"
#include "scenes/profile.h"
#include "scenes/registry.h"

#include "shell/profiles/deluge.h"
#include "shell/shell.h"

#include "gui/l10n/l10n.h"
#include "gui/l10n/strings.h"
#include "gui/menu_item/submenu.h"
#include "gui/ui/menus.h"
#include "gui/ui/sound_editor.h"

#include "model/clip/instrument_clip.h"
#include "model/song/song.h"
#include "processing/sound/sound_instrument.h"

#include "support/song_fixture.h"

#include "spec/shell/support/fake_surfaces.h"

#include "cppspec.hpp"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <map>
#include <set>
#include <span>
#include <string>
#include <vector>

#include "spec/support/spec_main.h"

using namespace deluge::scenes;
using deluge::scenes::menu::MenuContext;
using deluge::scenes::menu::Node;
using deluge::scenes::menu::ShortcutPos;

// `modSourceShortcuts` is a file-scope global in `sound_editor.cpp:65` and named in no header;
// `LegacyVersion1` below has to restore the two cells the V1 arm writes.
extern "C++" PatchSource modSourceShortcuts[2][8];

namespace {

namespace nodes = deluge::scenes::menu::nodes;

using Root = MenuContext::Root;

/// @brief `menus.cpp:2171`'s `comingSoonMenu` sentinel. It is a `#define` local to that translation
///        unit, so the spec spells the same value: a cell holding it is "not implemented", which
///        legacy answers with a popup and no navigation (`sound_editor.cpp:1330-1333`) and the port
///        transcribes as `nullptr`.
::MenuItem* coming_soon() {
	return reinterpret_cast<::MenuItem*>(0xFFFFFFFFu);
}

bool is_empty_cell(const ::MenuItem* item) {
	return item == nullptr || item == coming_soon();
}

void select_language() {
	deluge::l10n::chosenLanguage = &deluge::l10n::built_in::english;
}

/// @brief Point the `soundEditor` globals at a real synth clip, and pick a language.
///
/// Several legacy items build `getName()` out of live state rather than returning the l10n id the
/// constructor was given -- `filter::FilterParam::getName` reads
/// `soundEditor.currentModControllable->lpfMode` (`gui/menu_item/filter/info.h:44`), which is a null
/// dereference with nothing installed. `tables_2_spec` / `tables_3_spec` install the same globals
/// for the same reason; the fixture is built once and never torn down (`song_fixture.h`'s ownership
/// note).
void install_sound_context() {
	select_language();
	static spec::SongFixture fixture = spec::make_synth_song();
	static const MenuContext ctx =
	    menu::context_for(*fixture.song, fixture.clip, 0, false, false, false, /*kit_global_fx=*/false);
	currentSong = fixture.song;
	fixture.song->setCurrentClip(ctx.clip);
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
	soundEditor.setupKitGlobalFXMenu = false;
	soundEditor.navigationDepth = 0;
}

std::string text_of(uint16_t id) {
	deluge::ui::shell::TextScratch scratch;
	return std::string{deluge_profile().text_or_digits(id, scratch)};
}

/// @brief Read a `Submenu`'s protected child list -- `tables_2_spec`'s `SubmenuPeek`, verbatim and
///        for the same [class.protected] reason.
struct SubmenuPeek : deluge::gui::menu_item::Submenu {
	static const deluge::vector<::MenuItem*>& items_of(deluge::gui::menu_item::Submenu& menu) {
		return menu.*(&SubmenuPeek::items);
	}
};

/// @brief Walk the legacy sound root and the port's in lockstep by child index, recording which
///        `Node` each legacy `MenuItem*` is.
///
/// The anchor the name comparison cannot provide (fix round 1, I2). Legacy gives numbered siblings
/// *identical* names, identical titles and, for several of them, the same constructor `params::`
/// id with only an index argument to tell them apart -- `env1AttackMenu` and `env2AttackMenu` are
/// both `{STRING_FOR_ATTACK, STRING_FOR_ENV_ATTACK_MENU_TITLE, LOCAL_ENV_0_ATTACK, <i>}`
/// (`menus.cpp:415,424`), and `source0VolumeMenu` / `source1VolumeMenu` are both
/// `{STRING_FOR_OSC_LEVEL_MENU_TITLE, LOCAL_OSC_A_VOLUME, <i>}` (`:401,402`). Nothing about the
/// items themselves distinguishes them; only their **position in the tree** does. So this walks
/// both trees together and keys on the legacy pointer.
///
/// Deliberately conservative: it descends only where both sides are submenus with the same static
/// child count, and stops (rather than failing) anywhere they diverge. The lockstep property itself
/// is `tables_2_spec`'s subject, not this file's -- here the map only has to be *right* about what
/// it does contain, and the example asserts it covered enough cells to be meaningful.
void map_tree(::MenuItem& item, const Node& node, std::map<const ::MenuItem*, const Node*>& out, int depth = 0) {
	if (depth > 16 || !out.emplace(&item, &node).second) {
		return;
	}
	auto* sub = dynamic_cast<deluge::gui::menu_item::Submenu*>(&item);
	if (sub == nullptr) {
		return;
	}
	const deluge::vector<::MenuItem*>& kids = SubmenuPeek::items_of(*sub);
	if (node.children.size() != kids.size()) {
		return;
	}
	for (size_t i = 0; i < kids.size(); i++) {
		if (kids[i] != nullptr && node.children[i] != nullptr) {
			map_tree(*kids[i], *node.children[i], out, depth + 1);
		}
	}
}

const std::map<const ::MenuItem*, const Node*>& sound_tree_identity() {
	static const std::map<const ::MenuItem*, const Node*> map = [] {
		std::map<const ::MenuItem*, const Node*> m;
		map_tree(soundEditorRootMenu, nodes::sound_root, m);
		return m;
	}();
	return map;
}

/// @brief Nodes whose legacy item builds `getName()` out of live state rather than returning the
///        l10n id its constructor was given, so one static `Node::name` cannot equal it.
///
/// The four filter morph items only: `filter::FilterParam::getName` asks
/// `FilterInfo::getMorphNameOr` (`gui/menu_item/filter/info.h:81`), which answers "Drive" or "FM"
/// depending on the *current* filter mode. `tables_3_spec`'s `has_dynamic_name` holds the same two
/// sound-root entries for the same reason; these tables add the song/global-FX twins. Their cells
/// are still checked for presence, and their identity is covered where the trees are walked in
/// lockstep (`tables_2_spec`, `tables_3_spec`).
bool has_dynamic_name(const Node& node) {
	return &node == &nodes::lpf_morph || &node == &nodes::hpf_morph || &node == &nodes::global_lpf_morph
	       || &node == &nodes::global_hpf_morph;
}

/// @brief Describe a mismatch between @p node and the legacy item @p item, or an empty string when
///        they agree.
///
/// A free function rather than an inline `expect`: CppSpec's `expect` is only available inside an
/// example's own lambda, so the sweep collects descriptions and the example asserts on them.
///
/// The comparison is the legacy item's own `getName()`, which is what `tables_2_spec` /
/// `tables_3_spec` compare a node's name against everywhere else. A resolved name containing `*` is
/// a `FormattedTitle` placeholder legacy substitutes an index into at render time
/// (`gui/menu_item/formatted_title.h`) -- one static `uint16_t Node::name` cannot express it, which
/// is batch 1's recorded gap -- so those cells assert only that both sides are present.
std::string diff_cell(const Node* node, const ::MenuItem* item, const char* table, uint8_t x, uint8_t y) {
	const std::string where = std::string{table} + " (" + std::to_string(x) + ", " + std::to_string(y) + "): ";
	if (is_empty_cell(item)) {
		return node == nullptr ? std::string{} : where + "port has " + text_of(node->name) + ", legacy has nothing";
	}
	const std::string legacy_name{item->getName()};
	if (node == nullptr) {
		return where + "port has nothing, legacy has " + legacy_name;
	}
	const std::string name = text_of(node->name);
	if (has_dynamic_name(*node) || name.find('*') != std::string::npos) {
		return (name.empty() || legacy_name.empty()) ? where + "empty formatted name" : std::string{};
	}
	return name == legacy_name ? std::string{} : where + "port has " + name + ", legacy has " + legacy_name;
}

/// @brief One legacy 16x8 array against the port layer it was transcribed into, both ways.
/// @return Every disagreement, one per line; empty when the transcription is exact.
std::string diff_table(const char* table, ::MenuItem* legacy[kDisplayWidth][kDisplayHeight],
                       std::span<const Node* const> layer) {
	std::string out;
	if (layer.size() != size_t(menu::kShortcutCols) * menu::kShortcutRows) {
		return std::string{table} + ": layer is the wrong size\n";
	}
	for (uint8_t x = 0; x < menu::kShortcutCols; x++) {
		for (uint8_t y = 0; y < menu::kShortcutRows; y++) {
			const std::string d =
			    diff_cell(layer[static_cast<size_t>(y) * menu::kShortcutCols + x], legacy[x][y], table, x, y);
			if (!d.empty()) {
				out += d + "\n";
			}
		}
	}
	return out;
}

/// @brief One legacy one-dimensional array against the port layer, which holds it in column 11.
std::string diff_column(const char* table, ::MenuItem* legacy[kDisplayHeight], std::span<const Node* const> layer) {
	std::string out;
	if (layer.size() != size_t(menu::kShortcutCols) * menu::kShortcutRows) {
		return std::string{table} + ": layer is the wrong size\n";
	}
	for (uint8_t x = 0; x < menu::kShortcutCols; x++) {
		for (uint8_t y = 0; y < menu::kShortcutRows; y++) {
			const ::MenuItem* item = x == 11 ? legacy[y] : nullptr;
			const std::string d = diff_cell(layer[static_cast<size_t>(y) * menu::kShortcutCols + x], item, table, x, y);
			if (!d.empty()) {
				out += d + "\n";
			}
		}
	}
	return out;
}

/// @brief Select `SHORTCUTS_VERSION_1` on both sides for the scope, and put everything back.
///
/// `SoundEditor::setShortcutsVersion` (`gui/ui/sound_editor.cpp:111-143`) *writes into* the legacy
/// arrays -- and its `SHORTCUTS_VERSION_3` arm is a comment, not an undo (`:139-141`), so nothing but
/// this guard restores them. Twelve cells are snapshotted: the six of `paramShortcutsForSounds` and
/// four of `paramShortcutsForAudioClips` the V1 arm assigns, plus the two `modSourceShortcuts` ones
/// (`:134-135`), which is patching-source data the port does not carry but other examples in this
/// process would still see corrupted.
struct LegacyVersion1 {
	::MenuItem* sounds[6];
	::MenuItem* clips[4];
	PatchSource sources[2];
	uint8_t legacy_version;
	menu::ShortcutsVersion port_version;

	LegacyVersion1() {
		const std::pair<uint8_t, uint8_t> sound_cells[6] = {{0, 6}, {1, 6}, {2, 6}, {3, 6}, {2, 7}, {3, 7}};
		for (size_t i = 0; i < 6; i++) {
			sounds[i] = paramShortcutsForSounds[sound_cells[i].first][sound_cells[i].second];
		}
		const std::pair<uint8_t, uint8_t> clip_cells[4] = {{0, 7}, {1, 7}, {0, 6}, {1, 6}};
		for (size_t i = 0; i < 4; i++) {
			clips[i] = paramShortcutsForAudioClips[clip_cells[i].first][clip_cells[i].second];
		}
		sources[0] = modSourceShortcuts[0][7];
		sources[1] = modSourceShortcuts[1][7];
		legacy_version = soundEditor.shortcutsVersion;
		port_version = menu::shortcuts_version();

		soundEditor.setShortcutsVersion(SHORTCUTS_VERSION_1);
		menu::set_shortcuts_version(menu::ShortcutsVersion::V1);
	}

	~LegacyVersion1() {
		const std::pair<uint8_t, uint8_t> sound_cells[6] = {{0, 6}, {1, 6}, {2, 6}, {3, 6}, {2, 7}, {3, 7}};
		for (size_t i = 0; i < 6; i++) {
			paramShortcutsForSounds[sound_cells[i].first][sound_cells[i].second] = sounds[i];
		}
		const std::pair<uint8_t, uint8_t> clip_cells[4] = {{0, 7}, {1, 7}, {0, 6}, {1, 6}};
		for (size_t i = 0; i < 4; i++) {
			paramShortcutsForAudioClips[clip_cells[i].first][clip_cells[i].second] = clips[i];
		}
		modSourceShortcuts[0][7] = sources[0];
		modSourceShortcuts[1][7] = sources[1];
		soundEditor.shortcutsVersion = legacy_version;
		menu::set_shortcuts_version(port_version);
	}
};

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
	it("is transcribed from the default SHORTCUTS_VERSION_3 layout", _ {
		// `SoundEditor::setShortcutsVersion` (`sound_editor.cpp:111-143`) *mutates* the legacy arrays
		// in place for the pre-3.0 layout; the port transcribes them as written. If anything in this
		// process ever selects version 1, every cross-check below would be comparing against a
		// different table -- so say so here, once, rather than failing ten cells at a time.
		// `SHORTCUTS_VERSION_1` moves this cell to `audioClipSampleMarkerEditorMenuStart`
		// (`sound_editor.cpp:119`), so its still holding the end marker is the version's fingerprint.
		expect(paramShortcutsForAudioClips[0][7] == &audioClipSampleMarkerEditorMenuEnd).to_be_true();
	});

	it("matches paramShortcutsForSounds cell for cell", _ {
		install_sound_context();
		expect(diff_table("paramShortcutsForSounds", paramShortcutsForSounds,
		                  menu::shortcut_layer(Root::Sound, /*second_layer=*/false)))
		    .to_equal(std::string{});
	});

	it("matches paramShortcutsForSoundsSecondLayer cell for cell", _ {
		install_sound_context();
		expect(diff_table("paramShortcutsForSoundsSecondLayer", paramShortcutsForSoundsSecondLayer,
		                  menu::shortcut_layer(Root::Sound, /*second_layer=*/true)))
		    .to_equal(std::string{});
	});

	it("matches paramShortcutsForAudioClips cell for cell", _ {
		install_sound_context();
		expect(diff_table("paramShortcutsForAudioClips", paramShortcutsForAudioClips,
		                  menu::shortcut_layer(Root::AudioClip, /*second_layer=*/false)))
		    .to_equal(std::string{});
	});

	it("matches paramShortcutsForSongView cell for cell, for both session roots", _ {
		install_sound_context();
		expect(diff_table("paramShortcutsForSongView", paramShortcutsForSongView,
		                  menu::shortcut_layer(Root::Song, /*second_layer=*/false)))
		    .to_equal(std::string{});
		expect(diff_table("paramShortcutsForSongView", paramShortcutsForSongView,
		                  menu::shortcut_layer(Root::Performance, /*second_layer=*/false)))
		    .to_equal(std::string{});
	});

	it("matches paramShortcutsForKitGlobalFX cell for cell", _ {
		install_sound_context();
		expect(diff_table("paramShortcutsForKitGlobalFX", paramShortcutsForKitGlobalFX,
		                  menu::shortcut_layer(Root::KitGlobalFx, /*second_layer=*/false)))
		    .to_equal(std::string{});
	});

	it("matches midiOrCVParamShortcuts, in column 11", _ {
		select_language();
		// Column 11 is where `potentialShortcutPadAction` (`sound_editor.cpp:1277`) and
		// `updatePadLightsFor` (`:848`) index this table -- the task brief said column 0.
		expect(diff_column("midiOrCVParamShortcuts", midiOrCVParamShortcuts,
		                   menu::shortcut_layer(Root::MidiOrCv, /*second_layer=*/false)))
		    .to_equal(std::string{});
	});

	it("matches gateDrumParamShortcuts, in column 11", _ {
		install_sound_context();
		expect(diff_column("gateDrumParamShortcuts", gateDrumParamShortcuts,
		                   menu::shortcut_layer(Root::GateDrum, /*second_layer=*/false)))
		    .to_equal(std::string{});
	});

	it("resolves the four inline MIDI/CV cells that are in no legacy table", _ {
		// `potentialShortcutPadAction`'s own `else if` chain (`sound_editor.cpp:1282-1302`), in the
		// arm `editingCVOrMIDIClip() || editingNonAudioDrumRow()` takes -- so all three of the roots
		// that arm covers, and none of the others.
		for (Root root : {Root::MidiOrCv, Root::MidiDrum, Root::GateDrum}) {
			expect(menu::shortcut_node(root, false, 4, 7) == &nodes::sequence_direction).to_be_true();
			expect(menu::shortcut_node(root, false, 15, 1) == &nodes::spread_velocity_midi).to_be_true();
			expect(menu::shortcut_node(root, false, 15, 2) == &nodes::randomizer_lock).to_be_true();
			expect(menu::shortcut_node(root, false, 15, 3) == &nodes::note_probability_midi).to_be_true();
			// The second layer is the same layer here, so the toggle must not lose them.
			expect(menu::shortcut_node(root, true, 4, 7) == &nodes::sequence_direction).to_be_true();
			// `:1294-1295`'s `default:` -- the randomizer column is y = 1..3 and nothing else.
			expect(menu::shortcut_node(root, false, 15, 0) == nullptr).to_be_true();
			expect(menu::shortcut_node(root, false, 15, 4) == nullptr).to_be_true();
		}
		// Not the sound roots: (4, 7) there is `paramShortcutsForSounds`' own `sequenceDirectionMenu`
		// cell, and (15, 1..3) the *non*-MIDI randomizer trio -- both from the table, both blinkable.
		expect(menu::shortcut_node(Root::Sound, false, 15, 1) == &nodes::spread_velocity).to_be_true();
		expect(menu::shortcut_node(Root::AudioClip, false, 15, 1) == nullptr).to_be_true();
	});

	it("keeps those four cells out of the layers, so the blink never reports them", _ {
		// The asymmetry is the point: `updatePadLightsFor`'s MIDI/CV and gate-drum arms
		// (`sound_editor.cpp:833-853`) scan column 11 and nothing else, so legacy navigates from
		// these pads and never lights them. Removing the resolve-only split would fail here.
		for (Root root : {Root::MidiOrCv, Root::MidiDrum, Root::GateDrum}) {
			const auto layer = menu::shortcut_layer(root, false);
			expect(layer[size_t(7) * menu::kShortcutCols + 4] == nullptr).to_be_true();
			for (uint8_t y = 1; y <= 3; y++) {
				expect(layer[size_t(y) * menu::kShortcutCols + 15] == nullptr).to_be_true();
			}
			expect(menu::shortcut_pos(root, nodes::sequence_direction).valid).to_be_false();
			expect(menu::shortcut_pos(root, nodes::spread_velocity_midi).valid).to_be_false();
			expect(menu::shortcut_pos(root, nodes::randomizer_lock).valid).to_be_false();
			expect(menu::shortcut_pos(root, nodes::note_probability_midi).valid).to_be_false();
		}
	});

	it("pins every sound cell by legacy MenuItem identity, not by name", _ {
		install_sound_context();
		// The name comparison above is blind to a swap of two numbered sibling columns: legacy gives
		// `env1ReleaseMenu`/`env2ReleaseMenu` (x = 8 / 9), `lfo1RateMenu`/`lfo2RateMenu` (12 / 13),
		// `osc0TypeMenu`/`osc1TypeMenu` (2 / 3) and `sample0RepeatMenu`/`sample1RepeatMenu` (0 / 1)
		// the same name, and `source0VolumeMenu`/`source1VolumeMenu` the same *formatted* name, which
		// the `*` tolerance downgrades to a presence check. This keys on the legacy pointer instead,
		// through the position each item holds in the legacy tree -- so swapping any two columns
		// fails here even though every name still matches.
		const auto& identity = sound_tree_identity();
		const auto layer = menu::shortcut_layer(Root::Sound, false);
		const auto second = menu::shortcut_layer(Root::Sound, true);
		std::string bad;
		size_t pinned = 0;
		size_t unreachable = 0;
		for (uint8_t x = 0; x < menu::kShortcutCols; x++) {
			for (uint8_t y = 0; y < menu::kShortcutRows; y++) {
				const size_t i = size_t(y) * menu::kShortcutCols + x;
				for (const auto& [legacy_cell, port_cell] :
				     {std::pair{paramShortcutsForSounds[x][y], layer[i]},
				      std::pair{paramShortcutsForSoundsSecondLayer[x][y], second[i]}}) {
					if (is_empty_cell(legacy_cell)) {
						continue;
					}
					const auto found = identity.find(legacy_cell);
					if (found == identity.end()) {
						// Not reachable from `soundEditorRootMenu` by static children alone (a
						// dynamic submenu's child, say). Counted, not asserted -- the name
						// comparison already covers it.
						unreachable++;
						continue;
					}
					pinned++;
					if (found->second != port_cell) {
						bad += "(" + std::to_string(x) + ", " + std::to_string(y) + "): "
						       + std::string{legacy_cell->getName()} + " is a different node in the tree\n";
					}
				}
			}
		}
		expect(bad).to_equal(std::string{});
		// Not vacuous, and it reaches the columns that matter.
		expect(pinned > size_t(80)).to_be_true();
		for (uint8_t x : {uint8_t{0}, uint8_t{1}, uint8_t{2}, uint8_t{3}, uint8_t{8}, uint8_t{9}, uint8_t{12},
		                  uint8_t{13}}) {
			size_t in_column = 0;
			for (uint8_t y = 0; y < menu::kShortcutRows; y++) {
				::MenuItem* legacy_cell = paramShortcutsForSounds[x][y];
				in_column += size_t(!is_empty_cell(legacy_cell) && identity.count(legacy_cell) != 0);
			}
			expect(in_column >= size_t(4)).to_be_true();
		}
		(void)unreachable;
	});

	it("refuses the two kit global-FX cells legacy's own guard never let it press", _ {
		// The mirror of the four inline cells above. `paramShortcutsForKitGlobalFX` holds
		// `randomizerLockMenu` at (15, 2) and `randomizerNoteProbabilityMenu` at (15, 3), but the arm
		// is gated `(x <= kDisplayWidth - 2) || (x == 15 && y == 1)` (`sound_editor.cpp:1153-1157`),
		// so legacy can only ever press column 15 at y == 1.
		expect(menu::shortcut_node(Root::KitGlobalFx, false, 15, 1) == &nodes::spread_velocity).to_be_true();
		expect(menu::shortcut_node(Root::KitGlobalFx, false, 15, 2) == nullptr).to_be_true();
		expect(menu::shortcut_node(Root::KitGlobalFx, false, 15, 3) == nullptr).to_be_true();
		expect(menu::shortcut_node(Root::KitGlobalFx, true, 15, 2) == nullptr).to_be_true();
		// The guard admits everything up to column 14, so nothing else is lost.
		expect(menu::shortcut_node(Root::KitGlobalFx, false, 14, 0) == &nodes::global_delay_rate).to_be_true();

		// But the blink side has no such guard (`setupShortcutsBlinkFromTable`, `:825`), so the two
		// cells are still in the layer and still report a pad. Removing the press gate, or applying
		// it to the layer as well, fails one half of this.
		const auto layer = menu::shortcut_layer(Root::KitGlobalFx, false);
		expect(layer[size_t(2) * menu::kShortcutCols + 15] == &nodes::randomizer_lock).to_be_true();
		expect(layer[size_t(3) * menu::kShortcutCols + 15] == &nodes::note_probability).to_be_true();
		const ShortcutPos lock = menu::shortcut_pos(Root::KitGlobalFx, nodes::randomizer_lock);
		expect(lock.valid).to_be_true();
		expect(int32_t{lock.x}).to_equal(int32_t{15});
		expect(int32_t{lock.y}).to_equal(int32_t{2});

		// And no other root grows a gate: the sound table's own column 15 stays pressable.
		expect(menu::shortcut_node(Root::Sound, false, 15, 2) == &nodes::randomizer_lock).to_be_true();
	});

	it("swaps the name editor for the drum one on the two drum roots", _ {
		install_sound_context();
		// `potentialShortcutPadAction` (`sound_editor.cpp:1279-1281`, `:1306-1308`) rewrites
		// `editNameMenu` to `drumNameEditMenu` for a kit output or a non-audio drum row; the port
		// bakes that into the drum roots' own layers.
		const auto at = [](Root root, uint8_t x, uint8_t y) { return menu::shortcut_node(root, false, x, y); };
		expect(at(Root::Sound, 11, 5) == &nodes::edit_name).to_be_true();
		expect(at(Root::Drum, 11, 5) == &nodes::drum_name_edit).to_be_true();
		expect(at(Root::MidiOrCv, 11, 5) == &nodes::edit_name).to_be_true();
		expect(at(Root::MidiDrum, 11, 5) == &nodes::drum_name_edit).to_be_true();
		expect(at(Root::GateDrum, 11, 5) == &nodes::drum_name_edit).to_be_true();

		// ... and nothing else about those layers differs.
		const auto sound = menu::shortcut_layer(Root::Sound, false);
		const auto drum = menu::shortcut_layer(Root::Drum, false);
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
				expect(menu::shortcut_layer(root, second).size())
				    .to_equal(size_t(menu::kShortcutCols) * menu::kShortcutRows);
			}
		}
	});

	it("resolves nothing outside the 16x8 grid", _ {
		for (Root root : all_roots()) {
			expect(menu::shortcut_node(root, false, menu::kShortcutCols, 0) == nullptr).to_be_true();
			expect(menu::shortcut_node(root, false, 0, menu::kShortcutRows) == nullptr).to_be_true();
			expect(menu::shortcut_node(root, true, 255, 255) == nullptr).to_be_true();
		}
	});

	it("gives only the two sound roots a distinct second layer", _ {
		for (Root root : all_roots()) {
			const bool distinct = menu::shortcut_layer(root, true).data() != menu::shortcut_layer(root, false).data();
			expect(distinct).to_equal(root == Root::Sound || root == Root::Drum);
		}
	});

	it("shares one table between the two session roots, and leaves settings empty", _ {
		expect(menu::shortcut_layer(Root::Song, false).data() == menu::shortcut_layer(Root::Performance, false).data())
		    .to_be_true();
		for (const Node* cell : menu::shortcut_layer(Root::Settings, false)) {
			expect(cell == nullptr).to_be_true();
		}
		for (const Node* cell : menu::shortcut_layer(Root::Settings, true)) {
			expect(cell == nullptr).to_be_true();
		}
	});

	it("falls back to the first layer where the second has no cell", _ {
		// Legacy's own rule (`sound_editor.cpp:1320-1323`): the second layer *replaces* a cell only
		// where it has one, so a second press of a pad without a second-layer item re-opens the
		// first-layer one rather than nothing.
		expect(menu::shortcut_node(Root::Sound, true, 8, 0) == &nodes::env3_release).to_be_true();
		expect(menu::shortcut_node(Root::Sound, false, 8, 0) == &nodes::env1_release).to_be_true();
		expect(menu::shortcut_node(Root::Sound, true, 8, 7) == &nodes::lpf_freq).to_be_true();
		expect(menu::shortcut_node(Root::Sound, false, 8, 7) == &nodes::lpf_freq).to_be_true();
	});

	it("round-trips every cell through shortcut_pos", _ {
		for (Root root : all_roots()) {
			// The second layer is searched first, so a node that is only in the first layer must
			// still be found there, and one in both reports its second-layer pad.
			for (bool second : {true, false}) {
				const auto layer = menu::shortcut_layer(root, second);
				for (uint8_t y = 0; y < menu::kShortcutRows; y++) {
					for (uint8_t x = 0; x < menu::kShortcutCols; x++) {
						const Node* cell = layer[static_cast<size_t>(y) * menu::kShortcutCols + x];
						if (cell == nullptr) {
							continue;
						}
						const ShortcutPos pos = menu::shortcut_pos(root, *cell);
						expect(pos.valid).to_be_true();
						// ... and a press finds it again, EXCEPT where the arm's own guard excluded
						// the pad from the press path in the first place: legacy's kit global-FX arm
						// admits column 15 only at y == 1 (`sound_editor.cpp:1153-1157`), while the
						// blink side scans the whole table. Spelled out here rather than skipped, so
						// this fails if the gate's shape changes.
						const bool pressable = root != Root::KitGlobalFx || pos.x <= 14 || pos.y == 1;
						expect(menu::shortcut_node(root, second, pos.x, pos.y) == (pressable ? cell : nullptr))
						    .to_be_true();
					}
				}
			}
		}
	});

	it("reports no pad for a node with no shortcut anywhere", _ {
		// `root_settings` is a root; nothing in any table points at it.
		for (Root root : all_roots()) {
			expect(menu::shortcut_pos(root, nodes::root_settings).valid).to_be_false();
		}
	});

	it("has no patching-source pads yet", _ {
		// E1: `sourceShortcutBlinkFrequencies` comes from the live patch matrix, not a table, and
		// every patch-cable item is still a placeholder -- see `shortcuts.h`.
		expect(menu::source_shortcut_pads(Root::Sound, nodes::lpf_freq).empty()).to_be_true();
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
		// Fix round 2, I4. `validate_tree`'s `TooManyCustom` rule used to count distinct
		// `CustomItem` instances; every one of E1's bespoke nodes points at the single shared
		// `nodes::kPending`, so that count was 1 for the whole tree and the rule could never fire.
		// It now counts nodes -- and this pins the real number, which `validate.h`'s
		// `kMaxCustomNodes` is sized just above and `src/scenes/README.md` quotes.
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
		expect(custom_nodes <= menu::kMaxCustomNodes).to_be_true();
		// Sixteen of the fifty-two still share the one placeholder. The real, distinct items are
		// `battery_level` and `firmware_version` (E2 task 2 fix round 1), E2 task 5's three
		// `EditNameItem`s -- one per `rename::TargetId`, since the target is the item's only state
		// (`edit_name.h`) -- E2 task 7's five `ResetItem`s, one per `context_menu::DescriptorId`
		// (`settings.h`), and E2 task 8's twelve `MidiCommandItem`s, one per learnable
		// `GlobalMIDICommand` (`midi_command.h`). So twenty-three instances: `nodes::kPending`,
		// `BatteryLevelItem`, `FirmwareVersionItem`, `kEditClipName`, `kEditOutputName`,
		// `kEditDrumName`, `kResetFlashItem`, `kResetCommunityFeaturesItem`, `kResetMidiFollowItem`,
		// `kResetMidiDevicesItem`, `kResetAllItem`, and `kMidiCommandPlayItem` ..
		// `kMidiCommandShiftItem`. E2 task 9 adds eight more: `kPatchCablesItem`, the two
		// `SourceSelectionItem`s (regular/range), the two navigated `PatchCableStrengthItem`s plus the
		// two `Fixed` ones (`kVibratoItem`, `kSidechainVolumeItem`), and `kMultiRangeItem` -- so
		// thirty-one. E2 task 10 adds three more: `kCvSelectionItem`, `kGateSelectionItem` and
		// `kMidiDevicesItem` (`nodes/dynamic.h`), the last three settings items that were bespoke
		// `MenuItem` subclasses in legacy -- so thirty-four. E2 task 11 adds **none**: the nine
		// placeholders it retires (the six pad colours, the play cursor, the dev-sysex setting and
		// the shortcuts version) all become ordinary `Kind::Enum` nodes, so the node count falls by
		// nine while the distinct-instance count is unchanged -- `kPending` still has other users.
		// E2 task 11b adds five: the four `ActionItem`s (`kStartStemExportItem`, `kSetClipLengthItem`,
		// `kSongMidiLearnItem`, `kConfigureSongMacrosItem` -- four instances rather than one, for the
		// same reason the twelve MIDI-command items are twelve: the action is the item's only state)
		// and `kPerformEditorItem` (`nodes/actions.h`) -- so **thirty-nine**. Those five move no node
		// count: all were already `Kind::Custom` nodes pointing at `kPending`, and `kPending` still
		// has other users. It also retires the two `ActiveScaleMenu` placeholders into two
		// `ActiveScalesItem`s -- one per `ScaleSet`, because the bitset an item edits is its only
		// state (`nodes/scales.h`) -- so **forty-one**. And it takes three nodes *out* of the escape
		// hatch: `default_tempo`, `default_swing_amount` and `default_key` become `Kind::Range`
		// (`node.h`), so the node count falls 55 -> **52** while the instance count rises. Finally it
		// retires `audio_source_selector` into a sixth `ContextMenuItem` -- the class the five
		// `ResetItem`s became when a second kind of context-menu opener appeared
		// (`nodes/context_menu_items.h`) -- so **forty-two**.
		//
		// E3 task 6 adds **nine**: two `FileBrowserItem`s, five `MarkerItem`s and two
		// `AudioRecorderItem`s (`nodes/sample.h`) -- so **fifty-one**. Nine instances rather than
		// three, for the reason the twelve MIDI-command items are twelve: `on_focus` is handed a
		// `MenuContext` and a `MenuState`, never its own `Node`, so an item shared across the two
		// oscillators could not tell which source it was being focused as. They move no node count
		// either: all nine were already `Kind::Custom` nodes pointing at `kPending`.
		//
		// E3 task 7 adds the last two -- `kMidiDeviceLinked` and `kSpecificOutputSelector`
		// (`nodes/output_items.h`) -- and **`nodes::kPending` leaves the set with them**, because
		// nothing points at it any more. So the count goes 51 -> **52**, not 53, and it is now
		// exactly `kShippedCustomNodes`: every custom node has an item of its own, and no two nodes
		// share one. `output_items_spec.cpp` asserts the placeholder-free claim directly.
		//
		// Twelve instances rather than one parameterised by the node, because the command a node
		// binds has to be a property of the item: `CustomItem::learn_*` is handed `MenuState`, not
		// its own `Node`, so an item that shared one instance across the twelve could not tell which
		// of the twelve slots it was being asked to learn onto.
		//
		// E3 task 8 adds **three**, and they are the first additions that are also *new nodes*
		// rather than retired placeholders: `kMpeDirectionItem`, `kMpeZoneItem` and
		// `kMpeZoneChannelsItem` (`nodes/midi_device.h`), the three pages of the MPE arm under the
		// per-device MIDI page `midi_devices` now chains to. All three are bespoke because their
		// value is `MenuState` navigation state (`mpe_direction`/`mpe_zone`) that a
		// `Binding::get`/`set` -- handed only a `const MenuContext&` -- cannot see, and the
		// direction selector additionally computes its title from that value. The other four
		// children of that page are ordinary data nodes. So the count goes 52 -> **55**.
		expect(distinct_items).to_equal(size_t(55));
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
		// E2 task 12 made this a SET rather than a count, because a count is a net with a hole in
		// it: an item task that finished one placeholder and a table that added another somewhere
		// else would cancel out, and the number that says "the tree is not done" would not move.
		//
		// **E3 tasks 6 and 7 emptied the set.** Task 6 took nine rows out (the file/sample family:
		// `nodes/sample.h`) and task 7 the last two -- `midi_device_linked` and
		// `specific_output_selector`, now `nodes::kMidiDeviceLinked` and
		// `nodes::kSpecificOutputSelector` (`nodes/output_items.h`). The example is kept, empty,
		// rather than deleted: this is the assertion a table that adds a *new* placeholder fails,
		// and it is the only one that names which node arrived.
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

		// And the rule itself agrees with the walk. There is no flag to put down any more: the
		// option that used to suppress `PlaceholderCustom` was deleted with the last placeholder
		// (`validate.h`), so a default-constructed `ValidateOptions` IS the strict one. (The
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
		// Fix round 2, M1. `assign_node_ids` had no non-spec caller, so `MenuScene::save_state`,
		// `restore_state` and the shortcut-pad jump each silently no-opped in an application that
		// had built its registry and nothing else. `scenes::assigned_node_ids()` is now called from
		// both `menu_scene()` and `scene_registry()`.
		//
		// Written so it does not depend on which spec ran before it: the registry's own count is
		// compared against a fresh numbering of the very set it numbers, and the roots are then
		// checked against *that*.
		const uint16_t at_registry = deluge::scenes::assigned_node_ids();
		expect(at_registry > uint16_t(0)).to_be_true();
		expect(menu::assign_node_ids(menu::MenuScene::roots())).to_equal(at_registry);
		for (const Node* root : menu::MenuScene::roots()) {
			expect(root != nullptr).to_be_true();
			if (root != nullptr) {
				expect(menu::node_id(*root) != uint16_t(0)).to_be_true();
			}
		}
	});

	it("gives every shortcut target a node id, so the pad jump can name it", _ {
		(void)menu::assign_node_ids(nodes::kAllRoots);
		for (Root root : all_roots()) {
			for (bool second : {false, true}) {
				for (const Node* cell : menu::shortcut_layer(root, second)) {
					if (cell != nullptr) {
						expect(menu::node_id(*cell) != uint16_t(0)).to_be_true();
					}
				}
			}
		}
	});
});

describe shortcuts_profile("shell::BoardProfile without the shortcuts field", $ {
	it("still builds and runs a Shell over deluge_profile()", _ {
		// The removal's regression net (brief step 1): `BoardProfile::shortcuts` and
		// `ShortcutTable` are gone, and the profile the application uses is unaffected.
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
	it("matches paramShortcutsForSounds cell for cell once legacy has been switched to it", _ {
		install_sound_context();
		const LegacyVersion1 guard;
		// The same cell-for-cell sweep the V3 group runs, against the array `setShortcutsVersion`
		// has just rewritten. This is what makes `nodes::shortcuts_version` a setting that selects
		// something: E1 recorded it as one that "selects nothing", because only the V3 layer existed.
		expect(diff_table("paramShortcutsForSounds (v1)", paramShortcutsForSounds,
		                  menu::shortcut_layer(Root::Sound, /*second_layer=*/false)))
		    .to_equal(std::string{});
	});

	it("moves exactly the two sound cells that carry a different item under V1", _ {
		install_sound_context();
		// Legacy's V1 arm assigns six cells of this table (`sound_editor.cpp:125-132`) but four of
		// them write the value it already held -- so naming the two that move, and asserting the
		// other four did not, is the difference between transcribing the *arm* and transcribing its
		// *effect*. Interpolation gives way to the sample end point; nothing else changes.
		const Node* before_0_6 = menu::shortcut_node(Root::Sound, false, 0, 6);
		const Node* before_1_6 = menu::shortcut_node(Root::Sound, false, 1, 6);
		expect(before_0_6 == &nodes::sample0_interpolation).to_be_true();
		expect(before_1_6 == &nodes::sample1_interpolation).to_be_true();

		const LegacyVersion1 guard;
		expect(menu::shortcut_node(Root::Sound, false, 0, 6) == &nodes::sample0_end).to_be_true();
		expect(menu::shortcut_node(Root::Sound, false, 1, 6) == &nodes::sample1_end).to_be_true();
		expect(paramShortcutsForSounds[0][6] == &sample0EndMenu).to_be_true();
		expect(paramShortcutsForSounds[1][6] == &sample1EndMenu).to_be_true();

		// The four no-op assignments, on both sides.
		expect(menu::shortcut_node(Root::Sound, false, 2, 6) == &nodes::source0_wave_index).to_be_true();
		expect(menu::shortcut_node(Root::Sound, false, 3, 6) == &nodes::source1_wave_index).to_be_true();
		expect(menu::shortcut_node(Root::Sound, false, 2, 7) == &nodes::noise).to_be_true();
		expect(menu::shortcut_node(Root::Sound, false, 3, 7) == &nodes::osc_sync).to_be_true();

		// And the drum root, which reads the same table through its own name-editor swap.
		expect(menu::shortcut_node(Root::Drum, false, 0, 6) == &nodes::sample0_end).to_be_true();
		expect(menu::shortcut_node(Root::Drum, false, 11, 5) == &nodes::drum_name_edit).to_be_true();
	});

	it("matches paramShortcutsForAudioClips apart from the marker cell the port collapses", _ {
		install_sound_context();
		const LegacyVersion1 guard;
		// V1 puts the marker editor's *start* item at `(0..1, 7)` and its *end* item at `(0..1, 6)`
		// (`sound_editor.cpp:119-124`). The port has ONE node for the marker editor,
		// `audio_clip_waveform`, so both pairs resolve to it -- which is right for the end pair and
		// as close as this table can get for the start pair. `diff_cell` compares by name and
		// `audioClipSampleMarkerEditorMenuStart` is constructed with `EMPTY_STRING`
		// (`gui/ui/menus.cpp:1182`), so those two cells are excluded by hand and asserted directly.
		std::span<const Node* const> layer = menu::shortcut_layer(Root::AudioClip, /*second_layer=*/false);
		std::string diffs;
		for (uint8_t x = 0; x < menu::kShortcutCols; x++) {
			for (uint8_t y = 0; y < menu::kShortcutRows; y++) {
				if (y == 7 && x < 2) {
					continue; // the start-marker pair, asserted below
				}
				const std::string d = diff_cell(layer[static_cast<size_t>(y) * menu::kShortcutCols + x],
				                                paramShortcutsForAudioClips[x][y], "paramShortcutsForAudioClips (v1)",
				                                x, y);
				if (!d.empty()) {
					diffs += d + "\n";
				}
			}
		}
		expect(diffs).to_equal(std::string{});

		for (uint8_t x = 0; x < 2; x++) {
			expect(paramShortcutsForAudioClips[x][7] == &audioClipSampleMarkerEditorMenuStart).to_be_true();
			expect(paramShortcutsForAudioClips[x][6] == &audioClipSampleMarkerEditorMenuEnd).to_be_true();
			expect(menu::shortcut_node(Root::AudioClip, false, x, 7) == &nodes::audio_clip_waveform).to_be_true();
			expect(menu::shortcut_node(Root::AudioClip, false, x, 6) == &nodes::audio_clip_waveform).to_be_true();
		}
	});

	it("leaves every other root's layer alone, and restores V3 when the version goes back", _ {
		install_sound_context();
		// The version selects a layer here rather than editing one, so the round trip is total --
		// which is the one place the port deliberately does NOT reproduce legacy, whose V3 arm is a
		// no-op comment. Recorded on `menu::set_shortcuts_version`.
		const Node* song_before = menu::shortcut_node(Root::Song, false, 0, 7);
		const Node* second_before = menu::shortcut_node(Root::Sound, true, 8, 0);
		{
			const LegacyVersion1 guard;
			expect(menu::shortcut_node(Root::Song, false, 0, 7) == song_before).to_be_true();
			expect(menu::shortcut_node(Root::Sound, true, 8, 0) == second_before).to_be_true();
			expect(menu::shortcut_node(Root::MidiOrCv, false, 11, 0)
			       == menu::shortcut_node(Root::MidiOrCv, false, 11, 0))
			    .to_be_true();
		}
		expect(menu::shortcut_node(Root::Sound, false, 0, 6) == &nodes::sample0_interpolation).to_be_true();
		expect(menu::shortcut_node(Root::AudioClip, false, 0, 6) == &nodes::audio_clip_interpolation).to_be_true();
		expect(menu::shortcuts_version() == menu::ShortcutsVersion::V3).to_be_true();
	});

	it("finds the V1 pad through shortcut_pos too, so the blink follows the version", _ {
		install_sound_context();
		const LegacyVersion1 guard;
		// The reverse lookup reads the same layers, so it must move with them -- otherwise the pad a
		// press opens and the pad that blinks for the item it opened would disagree.
		const ShortcutPos pos = menu::shortcut_pos(Root::Sound, nodes::sample0_end);
		expect(pos.valid).to_be_true();
		expect(pos.x).to_equal(uint8_t(0));
		expect(pos.y).to_equal(uint8_t(6));
	});
});
// clang-format on

/// Five `describe` groups in one TU, so `DELUGE_SPEC` is spelled out here with `add_specs`.
int shortcuts_spec(int argc, char** const argv) {
	return CppSpec::parse(argc, argv)
	               .add_specs(shortcuts_transcription, shortcuts_shape, shortcuts_validation, shortcuts_profile,
	                          shortcuts_version_1)
	               .exec()
	               .is_success()
	           ? EXIT_SUCCESS
	           : EXIT_FAILURE;
}
