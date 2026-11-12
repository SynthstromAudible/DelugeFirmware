/// @file
/// @brief Records what the legacy menu items answer, as the reference tables `tests/golden/ui/menu_oracle/` holds.
///
/// Not a spec of anything: the scenes-spec gate skips `freeze/`, and every example fails unless `MENU_ORACLE_OUT`
/// names the directory to write into. One run writes the one family `MENU_ORACLE_FAMILIES` names, so each family
/// starts in a fresh process, as the spec that reads it does:
///
///     GOLDEN_SCENARIO=scenes_spec SCENES_SPEC=freeze/menu_oracles_spec SCENES_SPEC_CARD=<card> \
///       MENU_ORACLE_FAMILIES=<family> MENU_ORACLE_OUT=<repo>/tests/golden/ui/menu_oracle golden_vt_render
///
/// Each family asks the legacy items the questions its spec asks, in the spec's order and on the spec's own fixtures
/// (the shared `support/*_fixtures.h`), because a write leaves the model where it wrote.
///
/// @note This file may include `gui/menu_item/`, `gui/ui/menus.h` and `gui/ui/sound_editor.h`; the specs that read
///       the tables do not.

#include "support/oracle_table.h"
#include "support/oracle_tree.h"
#include "support/tables_2_fixtures.h"
#include "support/tables_3_fixtures.h"

#include "gui/ui/menu_view_of.h"
#include "scenes/menu/bindings.h"
#include "scenes/menu/context.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/menu/node.h"
#include "scenes/menu/nodes/dynamic.h"
#include "scenes/menu/nodes/nodes.h"
#include "scenes/menu/relevance.h"

// The legacy headers below render through an unqualified `OLED::main`, which resolves inside `gui/ui/menus.cpp` only
// because that file has a file-scope `using namespace deluge::hid::display`.
namespace deluge::gui::menu_item {
using deluge::hid::display::OLED;
} // namespace deluge::gui::menu_item
#include "gui/menu_item/arpeggiator/note_mode.h"
#include "gui/menu_item/arpeggiator/note_mode_for_drums.h"
#include "gui/menu_item/arpeggiator/octave_mode.h"
#include "gui/menu_item/arpeggiator/preset_mode.h"
#include "gui/menu_item/automation/automation.h"
#include "gui/menu_item/colour.h"
#include "gui/menu_item/formatted_title.h"
#include "gui/menu_item/menu_item_with_cc_learning.h"
#include "gui/menu_item/midi/command.h"
#include "gui/menu_item/midi/devices.h"
#include "gui/menu_item/midi/follow/follow_channel.h"
#include "gui/menu_item/midi/follow/follow_kit_root_note.h"
#include "gui/menu_item/selection.h"
#include "gui/menu_item/submenu.h"
#include "gui/menu_item/unison/count.h"
#include "gui/menu_item/unison/stereoSpread.h"
#include "gui/menu_item/voice/polyphony.h"
#include "gui/ui/menus.h"
#include "gui/ui/sound_editor.h"
#include "gui/ui/ui.h"
#include "gui/views/arranger_view.h"
#include "gui/views/automation_view.h"
#include "hid/buttons.h"
#include "hid/display/oled.h"

// `eq_menu.h` names `UnpatchedParam` without including it.
#include "gui/menu_item/unpatched_param.h"

#include "gui/menu_item/cv/selection.h"
#include "gui/menu_item/cv/submenu.h"
#include "gui/menu_item/cv/transpose.h"
#include "gui/menu_item/cv/volts.h"
#include "gui/menu_item/eq/eq_menu.h"
#include "gui/menu_item/gate/mode.h"
#include "gui/menu_item/gate/off_time.h"
#include "gui/menu_item/gate/selection.h"
#include "gui/menu_item/patch_cable_strength/fixed.h"
#include "gui/menu_item/patch_cable_strength/range.h"
#include "gui/menu_item/patch_cable_strength/regular.h"
#include "gui/menu_item/patch_cables.h"
#include "gui/menu_item/sidechain/volume.h"
#include "gui/menu_item/source_selection/range.h"
#include "gui/menu_item/source_selection/regular.h"

#include "effects/reverb.h"
#include "io/midi/cable_types/usb_hosted.h"
#include "io/midi/midi_device_manager.h"
#include "io/midi/midi_engine.h"
#include "model/clip/instrument_clip.h"
#include "model/instrument/kit.h"
#include "model/note/note_row.h"
#include "model/output.h"
#include "model/song/song.h"
#include "modulation/arpeggiator.h"
#include "modulation/params/param.h"
#include "modulation/patch/patch_cable_set.h"
#include "processing/engines/audio_engine.h"
#include "processing/engines/cv_engine.h"
#include "storage/flash_storage.h"

#include "cppspec.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "spec/support/spec_main.h"

/// @name Legacy globals no header declares for this file.
/// @{
extern deluge::gui::menu_item::arpeggiator::PresetMode arpPresetModeMenu;
extern deluge::gui::menu_item::voice::PolyphonyType polyphonyMenu;
extern deluge::gui::menu_item::unison::CountToStereoSpread numUnisonMenu;
extern deluge::gui::menu_item::PatchCables patchCablesMenu;
extern deluge::gui::menu_item::patch_cable_strength::Fixed vibratoMenu;
extern deluge::gui::menu_item::sidechain::VolumeShortcut sidechainVolumeShortcutMenu;
extern deluge::gui::menu_item::cv::Selection cvSelectionMenu;
extern deluge::gui::menu_item::gate::Selection gateSelectionMenu;
extern deluge::gui::menu_item::cv::Volts cvVoltsMenu;
extern deluge::gui::menu_item::cv::Transpose cvTransposeMenu;
/// @}

namespace {

using deluge::scenes::spec::oracle_escape;

/// @brief The rows one family writes, in the order it asks them.
class OracleWriter {
public:
	explicit OracleWriter(std::string name) : name_(std::move(name)) {}

	void row(std::string_view path, std::string_view column, std::vector<std::string> values = {}) {
		std::string key{path};
		key += '\t';
		key += column;
		if (!keys_.insert(key).second) {
			duplicates_ += std::string{path} + " " + std::string{column} + "\n";
			return;
		}
		std::string line = oracle_escape(path) + "\t" + oracle_escape(column);
		for (const std::string& v : values) {
			line += "\t" + oracle_escape(v);
		}
		lines_.push_back(std::move(line));
	}
	void row(std::string_view path, std::string_view column, std::string value) {
		row(path, column, std::vector<std::string>{std::move(value)});
	}
	void row(std::string_view path, std::string_view column, int64_t value) {
		row(path, column, std::to_string(value));
	}

	/// @brief The keys written twice, one per line; empty when every key was written once.
	[[nodiscard]] const std::string& duplicates() const { return duplicates_; }
	[[nodiscard]] size_t size() const { return lines_.size(); }

	/// @brief Write `<MENU_ORACLE_OUT>/<name>.tsv`; false when the directory is unset or the file cannot be written.
	bool flush() const {
		const char* dir = std::getenv("MENU_ORACLE_OUT");
		if (dir == nullptr || dir[0] == '\0') {
			return false;
		}
		std::ofstream out(std::string{dir} + "/" + name_ + ".tsv");
		out << "# Reference answers of upstream main's menu, one row per answer: path, column, values "
		       "(tab-separated).\n"
		       "# A row changes only to follow main's source (git show origin/main:<path>); tests/golden/README.md "
		       "says how.\n";
		for (const std::string& line : lines_) {
			out << line << "\n";
		}
		return out.good();
	}

private:
	std::string name_;
	std::set<std::string> keys_;
	std::vector<std::string> lines_;
	std::string duplicates_;
};

/// @brief Whether this run writes @p family: `MENU_ORACLE_FAMILIES` names exactly one.
bool writes(std::string_view family) {
	const char* named = std::getenv("MENU_ORACLE_FAMILIES");
	return named != nullptr && family == named;
}

/// @brief A panel as the tables hold it: `pixels/<name>.pbm`.
bool write_pixels(std::string_view name, const std::string& pbm) {
	const char* dir = std::getenv("MENU_ORACLE_OUT");
	if (dir == nullptr) {
		return false;
	}
	std::filesystem::create_directories(std::string{dir} + "/pixels");
	std::ofstream(std::string{dir} + "/pixels/" + std::string{name} + ".pbm") << pbm;
	return true;
}

} // namespace

// ---------------------------------------------------------------------------------------------------------------
// Helpers every family shares: the legacy-side readers and writers, and the lockstep walk.

namespace {

namespace legacy = deluge::gui::menu_item;
namespace spec = deluge::scenes::spec;
namespace menu = deluge::scenes::menu;
namespace nodes = deluge::scenes::menu::nodes;
namespace bindings = deluge::scenes::menu::bindings;
using deluge::scenes::menu::MenuContext;
using deluge::scenes::menu::Node;
using spec::TreeEntry;

/// @brief What went wrong in the freeze itself (a state the reference could not be taken in), one per line.
std::string& freeze_problems() {
	static std::string problems;
	return problems;
}

void check(bool ok, const std::string& what) {
	if (!ok) {
		freeze_problems() += what + "\n";
	}
}

/// @brief @p items, the children of @p menu, in the order `menu` shows them.
///
/// `eq::EqMenu::ensureCorrectItemsOrderAndGetValues` reorders its four items on its first render, to bass, bass
/// frequency, treble frequency, treble -- the graph's handles left to right -- and the port's `nodes::eq` lists them in
/// that order. Every other menu's order is its constructor's.
deluge::vector<::MenuItem*> in_menu_order(::MenuItem& menu, const deluge::vector<::MenuItem*>& items) {
	deluge::vector<::MenuItem*> out = items;
	if (dynamic_cast<deluge::gui::menu_item::eq::EqMenu*>(&menu) == nullptr) {
		return out;
	}
	namespace params = deluge::modulation::params;
	constexpr std::array kOrder{params::UNPATCHED_BASS, params::UNPATCHED_BASS_FREQ, params::UNPATCHED_TREBLE_FREQ,
	                            params::UNPATCHED_TREBLE};
	const auto rank = [&](::MenuItem* item) {
		const auto p = static_cast<deluge::gui::menu_item::UnpatchedParam*>(item)->getP();
		return std::distance(kOrder.begin(), std::ranges::find(kOrder, p));
	};
	std::ranges::stable_sort(out, {}, rank);
	return out;
}

/// @brief Read a `Submenu`'s protected child list: the pointer-to-member is formed inside a derived class, which
///        [class.protected] permits.
struct SubmenuPeek : legacy::Submenu {
	static deluge::vector<::MenuItem*> items_of(legacy::Submenu& menu) {
		return in_menu_order(menu, menu.*(&SubmenuPeek::items));
	}
};

/// @brief Call a legacy item's protected `writeCurrentValue()`.
template <typename T>
struct ValuePeek : legacy::Value<T> {
	static void write(legacy::Value<T>& value) { (value.*(&ValuePeek::writeCurrentValue))(); }
};

/// @brief The legacy item's current value, read the way legacy reads it, or nothing for an item that holds none.
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

/// @brief Drive @p item's legacy write path with @p v. False for an item that holds no value.
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

/// @brief Reach `Output::activeClip`, which is `protected`; never instantiated.
struct OutputPeek : ::Output {
	static Clip*& active_clip_of(::Output& out) { return out.*(&OutputPeek::activeClip); }
};

using Pair = std::pair<TreeEntry, ::MenuItem*>;

/// @brief Walk the port's tree and the legacy tree together, pairing them level by level. False, with @p where, as soon
///        as they disagree on a shape.
bool walk_pairs(const Node& node, ::MenuItem& item, const std::string& path, const Node* parent, int32_t depth,
                std::vector<Pair>& out, std::string& where) {
	out.emplace_back(TreeEntry{path, &node, parent, depth}, &item);
	if (node.kind != menu::Kind::Submenu) {
		// A `Kind::Custom` placeholder standing in for a legacy `Submenu` would drop that whole legacy subtree.
		if (dynamic_cast<legacy::Submenu*>(&item) != nullptr) {
			where = path + ": non-submenu node stands in for legacy submenu " + std::string{item.getName()};
			return false;
		}
		return true;
	}
	auto* submenu = dynamic_cast<legacy::Submenu*>(&item);
	if (submenu == nullptr) {
		where = path + ": node is a submenu but legacy item is not: " + std::string{item.getName()};
		return false;
	}
	const auto kids = SubmenuPeek::items_of(*submenu);
	if (kids.size() != node.children.size()) {
		where = path + ": child count differs under " + std::string{item.getName()} + " (" + std::to_string(kids.size())
		        + " legacy vs " + std::to_string(node.children.size()) + " port)";
		return false;
	}
	for (size_t i = 0; i < kids.size(); i++) {
		if (node.children[i] == nullptr || kids[i] == nullptr) {
			where = path + ": null child under " + std::string{item.getName()};
			return false;
		}
		if (!walk_pairs(*node.children[i], *kids[i], path + "/" + std::to_string(i), &node, depth + 1, out, where)) {
			return false;
		}
	}
	return true;
}

bool walk(const Node& root, ::MenuItem& legacy_root, std::string_view label, std::vector<Pair>& out,
          std::string& where) {
	return walk_pairs(root, legacy_root, std::string{label}, nullptr, 0, out, where);
}

/// @brief The legacy object a root's label stands for.
::MenuItem* legacy_root_for(std::string_view label) {
	if (label == "sound") {
		return &soundEditorRootMenu;
	}
	if (label == "drum") {
		return &soundEditorRootMenuDrum;
	}
	if (label == "midi/cv") {
		return &soundEditorRootMenuMIDIOrCV;
	}
	if (label == "midi drum") {
		return &soundEditorRootMenuMidiDrum;
	}
	if (label == "gate drum") {
		return &soundEditorRootMenuGateDrum;
	}
	if (label == "audio clip") {
		return &soundEditorRootMenuAudioClip;
	}
	if (label == "performance") {
		return &soundEditorRootMenuPerformanceView;
	}
	if (label == "song") {
		return &soundEditorRootMenuSongView;
	}
	if (label == "kit global fx") {
		return &soundEditorRootMenuKitGlobalFX;
	}
	if (label == "settings") {
		return &settingsRootMenu;
	}
	return nullptr;
}

/// @brief The node the legacy item @p item was ported to, for the menus a chain in these trees can name.
///
/// Null in, null out -- legacy's `return nullptr` is "go up". An unmapped answer is null too, so callers name the
/// legacy answer through `chain_label`, which tells the two apart.
const Node* chained_node_for(::MenuItem* item) {
	if (item == nullptr) {
		return nullptr;
	}
	if (item == &legacy::arpeggiator::arpOctaveModeToNoteModeMenu) {
		return &nodes::arp_octave_mode_to_note_mode;
	}
	if (item == &legacy::arpeggiator::arpOctaveModeToNoteModeMenuForDrums) {
		return &nodes::arp_octave_mode_to_note_mode_drums;
	}
	if (item == &legacy::arpeggiator::arpNoteModeFromOctaveModeMenu) {
		return &nodes::arp_note_mode_from_octave_mode;
	}
	if (item == &legacy::arpeggiator::arpNoteModeFromOctaveModeMenuForDrums) {
		return &nodes::arp_note_mode_from_octave_mode_drums;
	}
	if (item == &legacy::unison::stereoSpreadMenu) {
		return &nodes::unison_spread;
	}
	if (item == &legacy::voice::polyphonicVoiceCountMenu) {
		return &nodes::voice_count;
	}
	if (item == &legacy::source_selection::regularMenu) {
		return &nodes::source_selection_regular;
	}
	if (item == &legacy::source_selection::rangeMenu) {
		return &nodes::source_selection_range;
	}
	if (item == &legacy::patch_cable_strength::regularMenu) {
		return &nodes::patch_cable_strength_regular;
	}
	if (item == &legacy::patch_cable_strength::rangeMenu) {
		return &nodes::patch_cable_strength_range;
	}
	if (item == &cvSubmenu) {
		return &nodes::cv_submenu;
	}
	if (item == &gateModeMenu) {
		return &nodes::gate_mode;
	}
	if (item == &gateOffTimeMenu) {
		return &nodes::gate_off_time;
	}
	return nullptr;
}

// ---------------------------------------------------------------------------------------------------------------
// The tree family: the answers every node of a root gives, shared by `tables_2` and `tables_3`.

/// @brief What a family decides about its fixtures, since the two specs install and exclude differently.
struct TreeKit {
	void (*install)(const MenuContext&, Song*);
	bool (*dynamic_name)(const Node&);
	bool (*write_needs_ui)(const Node&);
};

/// @brief One fixture a root's answers are recorded on: its name in the column (empty: the root's own), its context and
///        song, and the root UI legacy's two session-only items read.
struct TreeFixture {
	std::string_view name;
	MenuContext ctx;
	Song* song;
	UI* root_ui = nullptr;
};

/// @brief A root walked against its legacy tree.
struct TreeWalk {
	std::string label;
	const Node* root;
	std::vector<Pair> pairs;
};

std::string at(std::string_view column, std::string_view fixture) {
	return fixture.empty() ? std::string{column} : std::string{column} + "@" + std::string{fixture};
}

/// @brief The family's install, then the `navigationDepth` legacy alone reads.
void freezer_install(const TreeKit& kit, const MenuContext& ctx, Song* song) {
	kit.install(ctx, song);
	soundEditor.navigationDepth = 0;
}

/// @brief The symbolic name of a chained page, whichever family names it.
std::string chain_label(const Node* node) {
	std::string name = spec::tables2::chain_name(node);
	return name.starts_with("UNMAPPED") ? spec::tables3::chain_name(node) : name;
}

/// @brief The name of what a legacy `selectButtonPress()` answered: the page it chains to, `<ascend>` for none, or the
///        legacy item's own name when the port has no node for it.
std::string legacy_chain_label(::MenuItem* answer) {
	if (answer == nullptr) {
		return "<ascend>";
	}
	const Node* node = chained_node_for(answer);
	return node == nullptr ? "UNMAPPED:" + std::string{answer->getName()} : chain_label(node);
}

/// @brief What an item answers whatever the fixture: name, title, child count, param, affect-entire, edit marking,
///        slots, range dependence. Written for every pair `walk` made, in walk order.
void emit_static(OracleWriter& out, const TreeEntry& e, ::MenuItem& item, bool dynamic_name) {
	if (!dynamic_name) {
		out.row(e.path, "name", std::string{item.getName()});
		out.row(e.path, "title", std::string{item.getTitle()});
	}
	if (e.node->kind == menu::Kind::Submenu) {
		out.row(e.path, "kids", static_cast<int64_t>(e.node->children.size()));
		return;
	}
	out.row(e.path, "ae", item.usesAffectEntire() ? 1 : 0);
	out.row(e.path, "edits", item.selectEncoderActionEditsInstrument() ? 1 : 0);
	out.row(e.path, "slots", item.getOccupiedSlots());
	out.row(e.path, "range_dep", item.isRangeDependent() ? 1 : 0);
	if (e.node->kind != menu::Kind::Custom) {
		out.row(e.path, "param_index", static_cast<int64_t>(item.getParamIndex()));
	}
}

/// @brief What an item answers on one fixture, under exactly the guards the spec applies: shown or not, its value,
///        its options, its learn target, and its param kind.
void emit_fixture(OracleWriter& out, const TreeKit& kit, const TreeEntry& e, ::MenuItem& item, const TreeFixture& f) {
	freezer_install(kit, f.ctx, f.song);
	if (e.parent != nullptr && !menu::bindings::relevant(*e.parent, f.ctx)) {
		return; // a page this fixture never opens
	}
	soundEditor.navigationDepth = static_cast<uint8_t>(e.navigation_depth());
	if (f.root_ui != nullptr) {
		setRootUILowLevel(f.root_ui);
	}
	if (!spec::tables3::recorder_on_soundless_row(e, f.ctx)) {
		out.row(e.path, at("rel", f.name), item.isRelevant(spec::menu_engine(f.ctx).mod, f.ctx.source_index) ? 1 : 0);
	}
	if (f.root_ui != nullptr) {
		nullifyUIs();
	}
	soundEditor.navigationDepth = 0;
	if (e.node->kind == menu::Kind::Custom || !menu::bindings::relevant(*e.node, f.ctx)) {
		return;
	}
	if (!spec::tables3::value_is_port_owned(*e.node) && !spec::tables3::value_is_two_ended(*e.node)) {
		if (const std::optional<int32_t> v = legacy_value(&item)) {
			out.row(e.path, at("read", f.name), *v);
		}
	}
	if (auto* selection = dynamic_cast<legacy::Selection*>(&item);
	    selection != nullptr && e.node->kind == menu::Kind::Enum) {
		std::vector<std::string> names;
		for (const std::string_view o : selection->getOptions(legacy::Selection::OptType::FULL)) {
			names.emplace_back(o);
		}
		out.row(e.path, at("options", f.name), std::move(names));
	}
	if (auto* learner = dynamic_cast<::MenuItemWithCCLearning*>(&item); learner != nullptr) {
		const deluge::modulation::KnobTarget thing = learner->getLearningThing();
		const auto unpatched = f.ctx.addresses.slot ? deluge::modulation::params::Kind::UNPATCHED_SOUND
		                                            : deluge::modulation::params::Kind::UNPATCHED_GLOBAL;
		out.row(e.path, at("learn", f.name),
		        thing ? std::to_string(deluge::bridge::controlTargetOf(thing, unpatched).value) : std::string{"none"});
	}
	if (spec::menu_engine(f.ctx).clip != nullptr && e.node->kind != menu::Kind::Submenu) {
		out.row(e.path, at("param_kind", f.name), static_cast<int64_t>(item.getParamKind()));
	}
}

/// @brief The write probes on one fixture: for each of the node's three probe values, written by the item from a
///        sentinel, what the port reads and what the item reads back.
void emit_writes(OracleWriter& out, const TreeKit& kit, const TreeEntry& e, ::MenuItem& item, const TreeFixture& f) {
	if (e.node->bind.set == nullptr || kit.write_needs_ui(*e.node) || spec::tables3::value_is_port_owned(*e.node)
	    || spec::tables3::value_is_two_ended(*e.node)) {
		return;
	}
	freezer_install(kit, f.ctx, f.song);
	if (!menu::bindings::relevant(*e.node, f.ctx)) {
		return;
	}
	const menu::IntRange r = menu::bindings::range(*e.node, f.ctx);
	for (const int32_t v : spec::distinct_probe_values(r)) {
		const std::optional<int32_t> sentinel = spec::sentinel_for(r, v);
		if (!sentinel || !legacy_write(&item, *sentinel)) {
			continue;
		}
		if (!legacy_write(&item, v)) {
			continue;
		}
		out.row(
		    e.path, at("write", f.name) + "/" + std::to_string(v),
		    {std::to_string(menu::bindings::read(*e.node, f.ctx)), std::to_string(legacy_value(&item).value_or(0))});
	}
}

/// @brief Walk @p root against its legacy tree, and write the static answers.
bool begin_tree(OracleWriter& out, const TreeKit& kit, std::string_view label, const Node& root, TreeWalk& walked) {
	walked.label = std::string{label};
	walked.root = &root;
	std::string problem;
	if (!walk(root, *legacy_root_for(label), label, walked.pairs, problem)) {
		check(false, "walk: " + problem);
		return false;
	}
	for (const auto& [e, item] : walked.pairs) {
		emit_static(out, e, *item, kit.dynamic_name(*e.node));
	}
	return true;
}

/// @brief What every fixture answers, for the pairs of one walked root.
void emit_reads(OracleWriter& out, const TreeKit& kit, const TreeWalk& walked, std::span<const TreeFixture> fixtures) {
	for (const TreeFixture& f : fixtures) {
		for (const auto& [e, item] : walked.pairs) {
			emit_fixture(out, kit, e, *item, f);
		}
	}
}

/// @brief The write probes for the pairs of one walked root.
void emit_tree_writes(OracleWriter& out, const TreeKit& kit, const TreeWalk& walked,
                      std::span<const TreeFixture> fixtures) {
	for (const TreeFixture& f : fixtures) {
		for (const auto& [e, item] : walked.pairs) {
			emit_writes(out, kit, e, *item, f);
		}
	}
}

/// @brief The first path under @p walked whose node is @p node, or empty.
std::string path_of(const TreeWalk& walked, const Node& node) {
	for (const auto& [e, item] : walked.pairs) {
		if (e.node == &node) {
			return e.path;
		}
	}
	return {};
}

/// @brief The legacy item the walk paired @p node with, as @p T, or null.
template <typename T = ::MenuItem>
T* item_of(const TreeWalk& walked, const Node& node) {
	for (const auto& [e, item] : walked.pairs) {
		if (e.node == &node) {
			return dynamic_cast<T*>(item);
		}
	}
	return nullptr;
}

/// @brief A legacy `getOptions(FULL)` as a row's values.
std::vector<std::string> options_of(legacy::Selection& selection) {
	std::vector<std::string> names;
	for (const std::string_view o : selection.getOptions(legacy::Selection::OptType::FULL)) {
		names.emplace_back(o);
	}
	return names;
}

/// @brief A cppspec-free stand-in for `std::string_view` joined with commas, for failure messages.
std::string joined(const std::vector<std::string>& v) {
	std::string out;
	for (const std::string& s : v) {
		out += s + ",";
	}
	return out;
}

// ---------------------------------------------------------------------------------------------------------------
// Family `tables_3`: the settings root and the nine others.

namespace tables_3 {

namespace t3 = spec::tables3;

/// @brief Records `display->displayPopup(...)`, so a legacy item can be driven with no display booted: two of
///        `midi::Command`'s arms call straight through the global.
struct SpyDisplay final : deluge::hid::Display {
	std::vector<std::string> popups;

	constexpr size_t getNumBrowserAndMenuLines() override { return 3; }
	void displayPopup(char const* text, int8_t, bool, uint8_t, int32_t, PopupType) override {
		popups.emplace_back(text != nullptr ? text : "");
	}
	void popupText(char const* text, PopupType) override { popups.emplace_back(text != nullptr ? text : ""); }
	void popupTextTemporary(char const* text, PopupType) override { popups.emplace_back(text != nullptr ? text : ""); }
	void cancelPopup() override {}
	void freezeWithError(char const*) override {}
	void displayError(Error) override {}
	void removeWorkingAnimation() override {}
	void displayLoadingAnimationText(char const*, bool, bool) override {}
	void removeLoadingAnimation() override {}
	bool hasPopup() override { return !popups.empty(); }
	bool hasPopupOfType(PopupType) override { return !popups.empty(); }
	void consoleText(char const*) override {}
	void timerRoutine() override {}
};

/// @brief Installs a `SpyDisplay` and points `soundEditor`'s navigation record at @p item, so the legacy
///        `midi::Command` can be called without dereferencing a null `display`; restores both on the way out.
struct LegacyLearnScope {
	SpyDisplay spy;
	deluge::hid::Display* saved_display;
	::MenuItem* saved_item;
	uint8_t saved_depth;

	explicit LegacyLearnScope(::MenuItem* item) : saved_display(display) {
		saved_depth = soundEditor.navigationDepth;
		saved_item = soundEditor.menuItemNavigationRecord[0];
		soundEditor.navigationDepth = 0;
		soundEditor.menuItemNavigationRecord[0] = item;
		display = &spy;
	}
	~LegacyLearnScope() {
		display = saved_display;
		soundEditor.menuItemNavigationRecord[0] = saved_item;
		soundEditor.navigationDepth = saved_depth;
	}
	LegacyLearnScope(const LegacyLearnScope&) = delete;
	LegacyLearnScope& operator=(const LegacyLearnScope&) = delete;
};

const TreeKit kKit{&t3::install, &t3::has_dynamic_name, &t3::write_needs_booted_ui};

const t3::TablesRoot& settings_root() {
	return t3::roots().back();
}

/// @brief Reverb width and room size are renamed by legacy under the MUTABLE model; the port's node names are fixed
/// ids.
void emit_reverb_titles(OracleWriter& out, const std::vector<TreeWalk>& walks) {
	const deluge::effects::ReverbModel saved = AudioEngine::reverb.model();
	for (const TreeWalk& walked : walks) {
		for (const auto& [e, item] : walked.pairs) {
			if (e.node != &nodes::reverb_width && e.node != &nodes::reverb_room_size) {
				continue;
			}
			AudioEngine::setReverbModel(deluge::effects::ReverbModel::FREEVERB);
			out.row(e.path, "name@freeverb", std::string{item->getName()});
			AudioEngine::setReverbModel(deluge::effects::ReverbModel::MUTABLE);
			out.row(e.path, "name@mutable", std::string{item->getName()});
		}
	}
	AudioEngine::setReverbModel(saved);
}

/// @brief The song root's two session-only items in the arranger and the automation view: one root, three root UIs.
void emit_session_only(OracleWriter& out, const TreeWalk& song_walk) {
	struct Case {
		const char* name;
		UI* root_ui;
		bool session_view_root;
		bool expected;
	};
	static spec::SongFixture song = spec::make_synth_song();
	const Case cases[] = {
	    {"arranger", &arrangerView, false, false},
	    {"automation", &automationView, false, false},
	};
	for (const Node* guarded : {&nodes::configure_song_macros, &nodes::song_midi_learn}) {
		::MenuItem* item = item_of(song_walk, *guarded);
		check(item != nullptr, "session-only item paired");
		if (item == nullptr) {
			continue;
		}
		for (const Case& c : cases) {
			const MenuContext ctx = menu::context_for(*song.song, deluge::gui::menuViewOf(nullptr), 0, false, true,
			                                          c.session_view_root, false);
			t3::install(ctx, song.song);
			setRootUILowLevel(c.root_ui);
			const bool want = item->isRelevant(spec::menu_engine(ctx).mod, ctx.source_index);
			nullifyUIs();
			check(want == c.expected, std::string{"session-only item in the "} + c.name);
			out.row(path_of(song_walk, *guarded), std::string{"rel@"} + c.name, want ? 1 : 0);
		}
	}
}

/// @brief cv::Selection and gate::Selection chain, output by output; the three chain-only pages' own shape and values.
void emit_chaining(OracleWriter& out, const TreeWalk& settings) {
	const t3::TablesRoot& root = settings_root();
	t3::install(root.ctx, root.song);
	const std::string cv_path = path_of(settings, nodes::cv_selection);
	const std::string gate_path = path_of(settings, nodes::gate_selection);
	check(!cv_path.empty() && !gate_path.empty(), "the two selectors are in the settings root");

	for (int32_t which = 0; which <= NUM_GATE_CHANNELS; which++) {
		soundEditor.currentSourceIndex = which;
		cvSelectionMenu.setValue(which);
		gateSelectionMenu.setValue(which);
		out.row(cv_path, "chain/" + std::to_string(which), legacy_chain_label(cvSelectionMenu.selectButtonPress()));
		out.row(gate_path, "chain/" + std::to_string(which), legacy_chain_label(gateSelectionMenu.selectButtonPress()));
	}

	// The three pages are children of nothing, so the lockstep walk never sees them. Their own names and titles are
	// built from runtime state (the output number), so only the shape and the values are recorded.
	const auto cv_kids = SubmenuPeek::items_of(cvSubmenu);
	out.row("chain/cv_submenu", "kids", static_cast<int64_t>(cv_kids.size()));
	for (size_t i = 0; i < cv_kids.size(); i++) {
		out.row("chain/cv_submenu/" + std::to_string(i), "name", std::string{cv_kids[i]->getName()});
	}

	for (int32_t which = 0; which < NUM_PHYSICAL_CV_CHANNELS; which++) {
		soundEditor.currentSourceIndex = which;
		cvVoltsMenu.readCurrentValue();
		out.row("chain/cv_volts", "read/" + std::to_string(which), cvVoltsMenu.getValue());
		cvTransposeMenu.readCurrentValue();
		out.row("chain/cv_transpose", "read/" + std::to_string(which), cvTransposeMenu.getValue());
		// A write goes to that output and no other.
		MenuContext ctx = root.ctx;
		ctx.source_index = static_cast<int8_t>(which);
		bindings::write(nodes::cv_volts, ctx, 100 + which);
	}
	for (int32_t which = 0; which < NUM_GATE_CHANNELS; which++) {
		soundEditor.currentSourceIndex = which;
		gateModeMenu.readCurrentValue();
		out.row("chain/gate_mode", "read/" + std::to_string(which), gateModeMenu.getValue());
		gateModeMenu.updateOptions(which);
		out.row("chain/gate_mode", "options/" + std::to_string(which), options_of(gateModeMenu));
	}
	soundEditor.currentSourceIndex = root.ctx.source_index;
	gateOffTimeMenu.readCurrentValue();
	out.row("chain/gate_off_time", "read", gateOffTimeMenu.getValue());
}

/// @brief The twelve MIDI-command learn items, against the legacy `midi::Command` in the same position.
void emit_midi_commands(OracleWriter& out, const TreeWalk& settings) {
	const t3::TablesRoot& root = settings_root();
	t3::install(root.ctx, root.song);
	const t3::CommandSlots saved = t3::CommandSlots::capture();
	const auto legacy_of = [&](size_t i) {
		return item_of<legacy::midi::Command>(settings, *nodes::kMidiCommandsChildren[i]);
	};
	check(std::size(nodes::kMidiCommandsChildren) == 12, "twelve MIDI commands");
	for (size_t i = 0; i < std::size(nodes::kMidiCommandsChildren); i++) {
		legacy::midi::Command* command = legacy_of(i);
		check(command != nullptr, "command " + std::to_string(i) + " paired");
		if (command == nullptr) {
			return;
		}
		out.row(path_of(settings, *nodes::kMidiCommandsChildren[i]), "command",
		        static_cast<int64_t>(command->commandNumber));
	}
	t3::FakeCable& cable = t3::fake_cable();
	for (size_t i = 0; i < std::size(nodes::kMidiCommandsChildren); i++) {
		legacy::midi::Command* command = legacy_of(i);
		const auto cc = static_cast<int32_t>(40 + i);
		t3::CommandSlots::clear_all();
		{
			LegacyLearnScope scope{command};
			command->learnCC(cable, 3, cc, 127);
			check(scope.spy.popups.empty(), "a CC learned on the current item pops nothing");
		}
		const std::string slot = t3::CommandSlots::capture().describe();
		check(slot != "(all clear)", "the CC learned");
		out.row("learn_cc/" + std::to_string(i), "slot", slot);
	}

	// From the list: the `Submenu` forwards a note-on and a program change to the row under its cursor.
	{
		auto* list = item_of<legacy::Submenu>(settings, nodes::midi_commands);
		check(list != nullptr, "the commands list is paired");
		const size_t play = 0;
		t3::CommandSlots::clear_all();
		{
			(void)list->focusChild(legacy_of(play));
			LegacyLearnScope scope{list};
			check(list->learnNoteOn(cable, 5, 61), "the list learns a note-on");
		}
		out.row("learn_list/0/note", "slot", t3::CommandSlots::capture().describe());
		t3::CommandSlots::clear_all();
		{
			(void)list->focusChild(legacy_of(play));
			LegacyLearnScope scope{list};
			list->learnProgramChange(cable, 2, 9);
		}
		out.row("learn_list/0/pgm", "slot", t3::CommandSlots::capture().describe());
		// The list has no `learnCC`; the row itself learns the CC (the port forwards it, an accepted difference).
		t3::CommandSlots::clear_all();
		{
			LegacyLearnScope scope{legacy_of(play)};
			legacy_of(play)->learnCC(cable, 3, 42, 127);
		}
		out.row("learn_list/0/cc", "slot", t3::CommandSlots::capture().describe());
	}

	// On its own screen: a note-on, and a program change (FILL refuses it).
	for (size_t i = 0; i < std::size(nodes::kMidiCommandsChildren); i++) {
		legacy::midi::Command* command = legacy_of(i);
		t3::CommandSlots::clear_all();
		{
			LegacyLearnScope scope{command};
			check(command->learnNoteOn(cable, 5, static_cast<int32_t>(60 + i)), "a note-on learns");
		}
		out.row("learn_own/" + std::to_string(i) + "/note", "slot", t3::CommandSlots::capture().describe());

		t3::CommandSlots::clear_all();
		const bool is_fill = command->commandNumber == GlobalMIDICommand::FILL;
		std::string pgm;
		{
			LegacyLearnScope scope{command};
			command->learnProgramChange(cable, 2, static_cast<int32_t>(7 + i));
			check(scope.spy.popups.size() == (is_fill ? 1u : 0u), "only FILL refuses a program change");
			if (is_fill && !scope.spy.popups.empty()) {
				check(scope.spy.popups[0] == deluge::l10n::get(deluge::l10n::String::STRING_FOR_CANT_LEARN_PC),
				      "FILL refuses with CANT_LEARN_PC");
			}
		}
		const std::string slot = t3::CommandSlots::capture().describe();
		check((slot == "(all clear)") == is_fill, "FILL alone binds nothing");
		out.row("learn_own/" + std::to_string(i) + "/pgm", "slot", is_fill ? std::string{"refused"} : slot);
	}

	// An MPE zone, which a note-on learn from an MPE-configured port really delivers.
	for (const int32_t zone :
	     {static_cast<int32_t>(MIDI_CHANNEL_MPE_LOWER_ZONE), static_cast<int32_t>(MIDI_CHANNEL_MPE_UPPER_ZONE)}) {
		t3::CommandSlots::clear_all();
		{
			LegacyLearnScope scope{legacy_of(0)};
			check(legacy_of(0)->learnNoteOn(cable, zone, 64), "a zone learns");
		}
		out.row("learn_mpe/" + std::to_string(zone), "slot", t3::CommandSlots::capture().describe());
	}
	saved.restore();
}

/// @brief The follow channels and the kit root note, against the legacy items.
void emit_follow(OracleWriter& out, const TreeWalk& settings) {
	const t3::TablesRoot& root = settings_root();
	t3::install(root.ctx, root.song);
	const t3::FollowSaved saved;
	t3::FakeCable& cable = t3::fake_cable();
	size_t which = 0;
	for (const Node* node : nodes::kMidiFollowChannelChildren) {
		auto* item = item_of<legacy::midi::FollowChannel>(settings, *node);
		check(item != nullptr, "follow channel " + std::to_string(which) + " paired");
		if (item != nullptr) {
			for (const bool note : {true, false}) {
				midiEngine.midiFollowChannelType[which].clear();
				{
					LegacyLearnScope scope{item};
					if (note) {
						(void)item->learnNoteOn(cable, 4, 60);
					}
					else {
						item->learnCC(cable, 4, 60, 100);
					}
				}
				out.row("follow/" + std::to_string(which), note ? "learned/note" : "learned/cc",
				        t3::follow_state(which));
			}
		}
		which++;
	}
	check(which == 19, "nineteen follow channels");

	auto* kit_root = item_of<legacy::midi::FollowKitRootNote>(settings, nodes::midi_follow_kit_root_note);
	check(kit_root != nullptr, "the kit root note is paired");
	if (kit_root != nullptr) {
		midiEngine.midiFollowKitRootNote = 36;
		{
			LegacyLearnScope scope{kit_root};
			(void)kit_root->learnNoteOn(cable, 4, 61);
		}
		out.row("follow/kit_root", "learned", static_cast<int64_t>(midiEngine.midiFollowKitRootNote));
	}
}

/// @brief The MIDI device list and the two output selectors.
void emit_dynamic(OracleWriter& out) {
	const t3::TablesRoot& root = settings_root();
	t3::install(root.ctx, root.song);

	// `Devices::drawPixelsForOled` walks `lowestDeviceNum` upward through `getCable` and keeps the connected ones.
	const auto visible = [] {
		std::vector<std::string> names;
		for (int32_t idx = -3; idx < static_cast<int32_t>(MIDIDeviceManager::hostedMIDIDevices.size()); idx++) {
			MIDICable* cable = deluge::gui::menu_item::midi::devicesMenu.getCable(idx);
			if (cable != nullptr && cable->connectionFlags != 0u) {
				names.emplace_back(cable->getDisplayName());
			}
		}
		return names;
	};
	{
		t3::CableFixture cables;
		cables.set(1, 1, 1, {"ALPHA", "BETA"});
		out.row("devices/all", "list", visible());
		cables.set(1, 0, 0, {});
		out.row("devices/din_only", "list", visible());
		auto ghost = std::make_unique<MIDICableUSBHosted>();
		ghost->name = "GHOST";
		ghost->connectionFlags = 0;
		MIDIDeviceManager::hostedMIDIDevices.push_back(ghost.get());
		out.row("devices/ghost", "list", visible());
		MIDIDeviceManager::hostedMIDIDevices.clear();
	}

	out.row("cv_selection", "options", options_of(cvSelectionMenu));
	for (int32_t which = 0; which < NUM_PHYSICAL_CV_CHANNELS; which++) {
		cvSelectionMenu.setValue(which);
		out.row("cv_selection", "chain/" + std::to_string(which),
		        legacy_chain_label(cvSelectionMenu.selectButtonPress()));
	}
	out.row("gate_selection", "options", options_of(gateSelectionMenu));
	for (int32_t which = 0; which <= NUM_GATE_CHANNELS; which++) {
		gateSelectionMenu.setValue(which);
		out.row("gate_selection", "chain/" + std::to_string(which),
		        legacy_chain_label(gateSelectionMenu.selectButtonPress()));
	}
	for (int32_t which = 0; which < NUM_GATE_CHANNELS; which++) {
		gateModeMenu.updateOptions(which);
		out.row("gate_mode/" + std::to_string(which), "options", options_of(gateModeMenu));
	}
	soundEditor.currentSourceIndex = root.ctx.source_index;
}

/// @brief The pad colours' storage bytes.
void emit_colours(OracleWriter& out, const TreeWalk& settings) {
	const Node* const colours[] = {&nodes::pad_colour_active, &nodes::pad_colour_stopped, &nodes::pad_colour_muted,
	                               &nodes::pad_colour_soloed, &nodes::pad_colour_fill,    &nodes::pad_colour_once};
	uint8_t saved[FlashStorage::NUM_PAD_COLOUR_SLOTS];
	std::copy(std::begin(FlashStorage::padColours), std::end(FlashStorage::padColours), saved);
	for (size_t k = 0; k < std::size(colours); k++) {
		auto* item = item_of<legacy::Colour>(settings, *colours[k]);
		check(item != nullptr, "pad colour " + std::to_string(k) + " paired");
		if (item == nullptr) {
			continue;
		}
		// The item writes one byte of `FlashStorage::padColours`: find which, from a base the write moves off.
		std::fill(std::begin(FlashStorage::padColours), std::end(FlashStorage::padColours),
		          static_cast<uint8_t>(legacy::Colour::RED));
		item->setValue(static_cast<int32_t>(legacy::Colour::PINK));
		item->writeCurrentValue();
		int32_t moved = -1;
		int32_t moved_count = 0;
		for (size_t i = 0; i < FlashStorage::NUM_PAD_COLOUR_SLOTS; i++) {
			if (FlashStorage::padColours[i] != legacy::Colour::RED) {
				moved = static_cast<int32_t>(i);
				moved_count++;
			}
		}
		check(moved_count == 1, "a pad colour item writes exactly one byte");
		out.row("colour/" + std::to_string(k), "storage", moved);
	}
	std::copy(std::begin(saved), std::end(saved), FlashStorage::padColours);
}

void freeze(OracleWriter& out) {
	const std::span<const t3::TablesRoot> roots = t3::roots();
	std::vector<TreeWalk> walks(roots.size());
	for (size_t i = 0; i < roots.size(); i++) {
		t3::install(roots[i].ctx, roots[i].song);
		if (!begin_tree(out, kKit, roots[i].name, *roots[i].node, walks[i])) {
			return;
		}
	}
	emit_reverb_titles(out, walks);
	for (size_t i = 0; i < roots.size(); i++) {
		const TreeFixture fixture{"", roots[i].ctx, roots[i].song, roots[i].root_ui};
		emit_reads(out, kKit, walks[i], std::span{&fixture, 1});
	}
	const auto walk_of = [&](std::string_view label) -> const TreeWalk& {
		for (const TreeWalk& w : walks) {
			if (w.label == label) {
				return w;
			}
		}
		std::abort();
	};
	emit_session_only(out, walk_of("song"));
	for (size_t i = 0; i < roots.size(); i++) {
		const TreeFixture fixture{"", roots[i].ctx, roots[i].song, roots[i].root_ui};
		emit_tree_writes(out, kKit, walks[i], std::span{&fixture, 1});
	}
	const TreeWalk& settings = walk_of("settings");
	emit_chaining(out, settings);
	emit_midi_commands(out, settings);
	emit_follow(out, settings);
	emit_dynamic(out);
	emit_colours(out, settings);
}

} // namespace tables_3

// ---------------------------------------------------------------------------------------------------------------
// Family `tables_2`: the sound root.

namespace tables_2 {

namespace t2 = spec::tables2;

const TreeKit kKit{&t2::install, &t2::has_dynamic_name, &t2::write_needs_booted_ui};

/// @brief The two fixtures the sound root's answers are recorded on, the kit row with its first drum selected.
std::vector<TreeFixture> fixtures() {
	t2::Rigged& synth = t2::fixture();
	t2::KitRig& kit = t2::kit_fixture();
	const MenuContext kit_ctx = t2::select_drum(kit, kit.drum_a);
	return {TreeFixture{"synth", synth.ctx, synth.song.song}, TreeFixture{"kit row", kit_ctx, kit.song.song}};
}

/// @brief The page a chain case is recorded under: its tree path, or `chain/<name>` for a chain-only page.
std::string chain_path(const TreeWalk& walked, const Node& node) {
	std::string path = path_of(walked, node);
	return path.empty() ? "chain/" + t2::chain_name(&node) : path;
}

/// @brief What each press of SELECT chains to, value by value, on a synth clip and on a kit row.
void emit_chaining(OracleWriter& out, const TreeWalk& walked) {
	t2::Rigged& f = t2::fixture();
	struct Case {
		const Node* node;
		::MenuItem* item;
		int32_t value;
	};
	const Case cases[] = {
	    {&nodes::arp_preset, &arpPresetModeMenu, int32_t(ArpPreset::CUSTOM)},
	    {&nodes::arp_preset, &arpPresetModeMenu, int32_t(ArpPreset::UP)},
	    {&nodes::arp_preset, &arpPresetModeMenu, int32_t(ArpPreset::OFF)},
	    {&nodes::polyphony, &polyphonyMenu, int32_t(PolyphonyMode::POLY)},
	    {&nodes::polyphony, &polyphonyMenu, int32_t(PolyphonyMode::MONO)},
	    {&nodes::polyphony, &polyphonyMenu, int32_t(PolyphonyMode::AUTO)},
	    {&nodes::unison_count, &numUnisonMenu, 1},
	    {&nodes::unison_count, &numUnisonMenu, 4},
	    {&nodes::arp_octave_mode_to_note_mode, &legacy::arpeggiator::arpOctaveModeToNoteModeMenu, 0},
	    {&nodes::arp_octave_mode_to_note_mode_drums, &legacy::arpeggiator::arpOctaveModeToNoteModeMenuForDrums, 0},
	};
	for (const Case& c : cases) {
		t2::install(f.ctx, f.song.song);
		menu::bindings::write(*c.node, f.ctx, c.value);
		// `Selection::selectButtonPress`'s fall-through arm asks whether the item is the open menu; in the port the
		// chain hook only ever runs on the focused leaf, so that is the state to put legacy in.
		soundEditor.navigationDepth = 0;
		soundEditor.menuItemNavigationRecord[0] = c.item;
		c.item->readCurrentValue();
		out.row(chain_path(walked, *c.node), "chain@synth/" + std::to_string(c.value),
		        legacy_chain_label(c.item->selectButtonPress()));
	}
	soundEditor.menuItemNavigationRecord[0] = nullptr;

	// The other half of `PresetMode::selectButtonPress`: a kit row picks the drum twin of the octave-mode page.
	t2::KitRig& k = t2::kit_fixture();
	const MenuContext ctx = t2::select_drum(k, k.drum_a);
	menu::bindings::write(nodes::arp_preset, ctx, int32_t(ArpPreset::CUSTOM));
	soundEditor.navigationDepth = 0;
	soundEditor.menuItemNavigationRecord[0] = &arpPresetModeMenu;
	arpPresetModeMenu.readCurrentValue();
	out.row(chain_path(walked, nodes::arp_preset), "chain@kit row/" + std::to_string(int32_t(ArpPreset::CUSTOM)),
	        legacy_chain_label(arpPresetModeMenu.selectButtonPress()));
	soundEditor.menuItemNavigationRecord[0] = nullptr;
	t2::install(f.ctx, f.song.song);
}

/// @brief The four chain-only arp pages answer AFFECT ENTIRE as their legacy items do; they are in no tree.
void emit_chain_only_ae(OracleWriter& out) {
	const std::pair<const Node*, ::MenuItem*> pages[] = {
	    {&nodes::arp_octave_mode_to_note_mode, &legacy::arpeggiator::arpOctaveModeToNoteModeMenu},
	    {&nodes::arp_octave_mode_to_note_mode_drums, &legacy::arpeggiator::arpOctaveModeToNoteModeMenuForDrums},
	    {&nodes::arp_note_mode_from_octave_mode, &legacy::arpeggiator::arpNoteModeFromOctaveModeMenu},
	    {&nodes::arp_note_mode_from_octave_mode_drums, &legacy::arpeggiator::arpNoteModeFromOctaveModeMenuForDrums},
	};
	for (const auto& [node, item] : pages) {
		out.row("chain/" + t2::chain_name(node), "ae", item->usesAffectEntire() ? 1 : 0);
	}
}

/// @brief A cable made the way the strength page makes one, committed through the port; the cable sets the later rows
///        index into are built in this order.
void commit_cable(const MenuContext& ctx, PatchSource source, int32_t param, int32_t value, Polarity polarity,
                  const Node* page) {
	menu::MenuState st{};
	st.depth = 0;
	st.path[0] = page;
	st.walk.patching_param = param;
	st.walk.patch_source = source;
	st.walk.patch_polarity = polarity;
	st.edit_int = value;
	nodes::kPatchCableStrengthRegularItem.commit(ctx, st);
}

/// @brief The patch-cable items: their names, where they chain, what the strength page reads and a write leaves, and
///        which strength page each stored cable opens.
void emit_patch_cables(OracleWriter& out, const TreeWalk& walked) {
	t2::Rigged& f = t2::fixture();
	namespace params = deluge::modulation::params;
	out.row("cables/patch_cables", "name", std::string{patchCablesMenu.getName()});
	out.row("cables/vibrato", "name", std::string{vibratoMenu.getName()});
	out.row("cables/sidechain_volume", "name", std::string{sidechainVolumeShortcutMenu.getName()});

	t2::install(f.ctx, f.song.song);
	soundEditor.patchingParamSelected = params::LOCAL_LPF_FREQ;
	legacy::source_selection::regularMenu.s = PatchSource::ENVELOPE_1;
	legacy::source_selection::rangeMenu.s = PatchSource::LFO_GLOBAL_1;
	check(!Buttons::isShiftButtonPressed(), "shift is not held");
	struct Case {
		const char* label;
		::MenuItem* item;
	};
	::MenuItem* lpf = item_of(walked, nodes::lpf_freq);
	check(lpf != nullptr, "lpf_freq is paired");
	const Case cases[] = {
	    {"source_selection_regular", &legacy::source_selection::regularMenu},
	    {"patch_cable_strength_regular", &legacy::patch_cable_strength::regularMenu},
	    {"source_selection_range", &legacy::source_selection::rangeMenu},
	    {"patch_cable_strength_range", &legacy::patch_cable_strength::rangeMenu},
	    {"vibrato", &vibratoMenu},
	    {"sidechain_volume", &sidechainVolumeShortcutMenu},
	    {"lpf_freq", lpf},
	};
	for (const Case& c : cases) {
		if (c.item != nullptr) {
			out.row(std::string{"cables/"} + c.label, "chain", legacy_chain_label(c.item->selectButtonPress()));
		}
	}

	// The strength page's reader, on a cable the port created and wrote.
	{
		const deluge::synth::CableTarget learn =
		    deluge::synth::CableTarget::depth_of(params::LOCAL_LPF_FREQ, PatchSource::ENVELOPE_1);
		check(t2::seed_cable(f.ctx, learn, 1234), "the cable is created");
		legacy::patch_cable_strength::regularMenu.readCurrentValue();
		out.row("strength/seed", "read", legacy::patch_cable_strength::regularMenu.getValue());
		check(t2::seed_cable(f.ctx, learn, -2500), "the cable is moved");
		legacy::patch_cable_strength::regularMenu.readCurrentValue();
		out.row("strength/sentinel", "read", legacy::patch_cable_strength::regularMenu.getValue());
		menu::MenuState st{};
		st.walk.patching_param = params::LOCAL_LPF_FREQ;
		st.walk.patch_source = PatchSource::ENVELOPE_1;
		nodes::kPatchCableStrengthRegularItem.on_focus(f.ctx, st);
		st.edit_int = 4321;
		nodes::kPatchCableStrengthRegularItem.commit(f.ctx, st);
		legacy::patch_cable_strength::regularMenu.readCurrentValue();
		out.row("strength/commit", "write/4321", legacy::patch_cable_strength::regularMenu.getValue());
	}

	// The cables the other strength examples create, in their order: the row indices below follow it.
	{
		const Polarity fresh = PatchCable::getDefaultPolarity(PatchSource::ENVELOPE_3);
		commit_cable(f.ctx, PatchSource::ENVELOPE_3, params::LOCAL_HPF_RESONANCE, 2000,
		             fresh == Polarity::BIPOLAR ? Polarity::UNIPOLAR : Polarity::BIPOLAR, nullptr);
		commit_cable(f.ctx, PatchSource::LFO_GLOBAL_1, params::LOCAL_HPF_MORPH, 1200,
		             PatchCable::getDefaultPolarity(PatchSource::LFO_GLOBAL_1), &nodes::sidechain);
		check(t2::seed_cable(
		          f.ctx,
		          deluge::synth::CableTarget::depth_of(params::LOCAL_OSC_A_PITCH_ADJUST, PatchSource::LFO_GLOBAL_1),
		          1500),
		      "the pitch cable is created");
	}

	// Which strength page the selected cable needs, and the sources it installs.
	PatchCableSet* set = spec::menu_engine(f.ctx).params->getPatchCableSetAllowJibberish();
	check(t2::seed_cable(f.ctx, deluge::synth::CableTarget::depth_of(params::LOCAL_LPF_FREQ, PatchSource::ENVELOPE_1),
	                     1000),
	      "the plain cable is created");
	check(t2::seed_cable(f.ctx,
	                     deluge::synth::CableTarget::depth_of_depth(params::LOCAL_LPF_FREQ, PatchSource::ENVELOPE_1,
	                                                                PatchSource::LFO_GLOBAL_1),
	                     900),
	      "the ranged cable is created");
	for (int32_t i = 0; i < set->numPatchCables; i++) {
		patchCablesMenu.currentValue = i;
		const std::string page = legacy_chain_label(patchCablesMenu.selectButtonPress());
		out.row("cables/row/" + std::to_string(i), "page", page);
		std::vector<std::string> sources{std::to_string(int(soundEditor.patchingParamSelected)),
		                                 std::to_string(int(legacy::source_selection::regularMenu.s))};
		if (!set->patchCables[i].destination.is_param()) {
			sources.push_back(std::to_string(int(legacy::source_selection::rangeMenu.s)));
		}
		out.row("cables/row/" + std::to_string(i), "sources", sources);
	}
	patchCablesMenu.currentValue = set->numPatchCables;
	out.row("cables/row/end", "page", legacy_chain_label(patchCablesMenu.selectButtonPress()));

	// Shift+SELECT deletes automation exactly on the legacy `Automation` items.
	for (const auto& [e, item] : walked.pairs) {
		if (dynamic_cast<legacy::Automation*>(item) != nullptr) {
			out.row(e.path, "automation", 1);
		}
	}
}

/// @brief With AFFECT ENTIRE held on a kit row, legacy's hand-written sweeps write every drum: what each drum reads
///        after the write, from a sentinel seeded on both through legacy's own single-target write.
void emit_sweeps(OracleWriter& out, const TreeWalk& walked) {
	t2::KitRig& k = t2::kit_fixture();
	const auto observe = [&](const Node& node) {
		std::array<int32_t, 2> read{};
		read[0] = menu::bindings::read(node, t2::select_drum(k, k.drum_a));
		read[1] = menu::bindings::read(node, t2::select_drum(k, k.drum_b));
		return read;
	};
	for (const auto& [e, item] : walked.pairs) {
		if (e.node->bind.set == nullptr || t2::write_needs_booted_ui(*e.node)) {
			continue;
		}
		const menu::IntRange r = menu::bindings::range(*e.node, t2::select_drum(k, k.drum_a));
		const int32_t v = r.max;
		const std::optional<int32_t> sentinel = spec::sentinel_for(r, v);
		if (!sentinel.has_value() || !legacy_value(item).has_value()) {
			continue;
		}
		(void)t2::select_drum(k, k.drum_a);
		if (!legacy_write(item, *sentinel)) {
			continue;
		}
		(void)t2::select_drum(k, k.drum_b);
		if (!legacy_write(item, *sentinel)) {
			continue;
		}
		(void)t2::select_drum(k, k.drum_a, /*affect_entire_held=*/true); // legacy reads the sweep off `currentUIMode`
		if (!legacy_write(item, v)) {
			continue;
		}
		const std::array<int32_t, 2> after = observe(*e.node);
		out.row("sweep/" + e.path, "drum_a", after[0]);
		out.row("sweep/" + e.path, "drum_b", after[1]);
	}
	t2::install(t2::fixture().ctx, t2::fixture().song.song);
}

void freeze(OracleWriter& out) {
	t2::Rigged& f = t2::fixture();
	t2::install(f.ctx, f.song.song);
	TreeWalk walked;
	if (!begin_tree(out, kKit, "sound", nodes::sound_root, walked)) {
		return;
	}
	std::vector<TreeFixture> fx = fixtures();
	emit_reads(out, kKit, walked, fx);
	emit_chain_only_ae(out);
	emit_chaining(out, walked);
	emit_patch_cables(out, walked);
	fx = fixtures();
	emit_tree_writes(out, kKit, walked, fx);
	emit_sweeps(out, walked);
}

} // namespace tables_2

} // namespace

// clang-format off
describe freeze_frame("menu oracle freezer: its run", $ {
	it("has a directory to write into and exactly one family to write", _ {
		const char* dir = std::getenv("MENU_ORACLE_OUT");
		expect(dir != nullptr && dir[0] != '\0').to_be_true();
		const char* family = std::getenv("MENU_ORACLE_FAMILIES");
		expect(family != nullptr && std::string_view{family}.find(',') == std::string_view::npos).to_be_true();
	});
});
// clang-format on

// clang-format off
describe freeze_tables("menu oracle freezer: the sound root and the ten roots", $ {
	it("writes tables_3.tsv", _ {
		if (!writes("tables_3")) {
			return;
		}
		freeze_problems().clear();
		OracleWriter out{"tables_3"};
		tables_3::freeze(out);
		expect(freeze_problems()).to_equal(std::string{});
		expect(out.duplicates()).to_equal(std::string{});
		expect(out.size() > size_t{9000}).to_be_true();
		expect(out.flush()).to_be_true();
	});

	it("writes tables_2.tsv", _ {
		if (!writes("tables_2")) {
			return;
		}
		freeze_problems().clear();
		OracleWriter out{"tables_2"};
		tables_2::freeze(out);
		expect(freeze_problems()).to_equal(std::string{});
		expect(out.duplicates()).to_equal(std::string{});
		expect(out.size() > size_t{2500}).to_be_true();
		expect(out.flush()).to_be_true();
	});
});
// clang-format on

DELUGE_SPEC(freeze_menu_oracles_spec, freeze_frame,
            freeze_tables /*, one family per later task, appended in task order */)
