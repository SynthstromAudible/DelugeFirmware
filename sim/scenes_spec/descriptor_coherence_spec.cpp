/// @file
/// @brief The scenes' hand-written menu tables against what the engine descriptors and the host name table give.
///
/// Each setting row a menu shows has one legacy item (`support/descriptor_cases.h`), and the scenes node that replaces
/// that item is the one `tables_3_spec.cpp`'s lockstep walk pairs with it. This spec walks the same pairing and, for
/// each node, compares what the node shows with what its row gives:
///   * its range (`bindings::range`) is `engine::menu_range`, or for a CHOICE row the count of applicable choices;
///   * its names (`bindings::names`) are the host name table's choices, in menu order, and its name and title are the
///     table's;
///   * its reads (`bindings::read`) at the row's stored probes are `engine::to_menu`, and its writes
///     (`bindings::write`) at the menu probes store what `engine::from_menu` says.
///
/// A node that disagrees on one of those is one of the named exceptions below, keyed by its row's family and path and
/// each with its reason; the spec fails when an exception no longer disagrees, so a fixed node leaves the list. A node
/// with a read or a write hook that no row reaches is named with its reason: a device setting in `kDeviceSettings`, by
/// its path from the settings root; a note editor, a stem export option or a skipped row's node in `subtrees` or
/// `skipped_nodes`.
///
/// @note The SPEC may include `gui/menu_item/`; the library may not (`scripts/check_layer_rules.py`).

#include "support/descriptor_cases.h"

#include "engines/deluge/params.h"
#include "engines/global_effectable/params.h"
#include "gui/ui/menus.h"
#include "model/project_bridge/param_ids.h"
#include "scenes/menu/bindings.h"
#include "scenes/menu/node.h"
#include "scenes/menu/nodes/nodes.h"
#include "scenes/profile.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <map>
#include <set>
#include <tuple>

namespace {

using deluge::scenes::menu::IntRange;
using deluge::scenes::menu::Kind;
using deluge::scenes::menu::Node;
namespace bindings = deluge::scenes::menu::bindings;
namespace nodes = deluge::scenes::menu::nodes;

// ---------------------------------------------------------------------------------------------
// The pairing of legacy items with nodes.

/// @brief `Submenu::items` is protected: name it through a derived class ([class.protected]), as `tables_3_spec.cpp`
/// does.
struct SubmenuPeek : legacy::Submenu {
	static const deluge::vector<::MenuItem*>& items_of(legacy::Submenu& menu) { return menu.*(&SubmenuPeek::items); }
};

/// @brief A legacy item the scenes have not ported, which the lockstep walk steps over (`tables_3_spec.cpp`'s list).
bool unported_legacy(const ::MenuItem* item) {
	return dynamic_cast<const legacy::cpu::VoiceUsage*>(item) != nullptr;
}

using Pairing = std::map<const ::MenuItem*, std::vector<const Node*>>;

void pair_walk(const Node& node, ::MenuItem& item, Pairing& out) {
	std::vector<const Node*>& nodes_of_item = out[&item];
	if (std::find(nodes_of_item.begin(), nodes_of_item.end(), &node) == nodes_of_item.end()) {
		nodes_of_item.push_back(&node);
	}
	auto* submenu = dynamic_cast<legacy::Submenu*>(&item);
	if (node.kind != Kind::Submenu || submenu == nullptr) {
		return;
	}
	std::vector<::MenuItem*> kids;
	for (::MenuItem* kid : SubmenuPeek::items_of(*submenu)) {
		if (!unported_legacy(kid)) {
			kids.push_back(kid);
		}
	}
	for (size_t i = 0; i < kids.size() && i < node.children.size(); ++i) {
		pair_walk(*node.children[i], *kids[i], out);
	}
}

/// @brief The nodes that show a legacy item's row on a page no submenu names (a page only a select press chains to),
/// so the lockstep walk cannot reach them.
///
/// The preset's two chained pages (`arpOctaveModeToNoteModeMenu` and its drum twin) are the legacy octave mode item
/// with a chain; the note mode pages they chain to are the legacy note mode items (`menus.cpp:511-523`).
const Pairing& chained_pages() {
	static const Pairing pages{
	    {&arpOctaveModeMenu, {&nodes::arp_octave_mode_to_note_mode, &nodes::arp_octave_mode_to_note_mode_drums}},
	    {&arpNoteModeMenu, {&nodes::arp_note_mode_from_octave_mode}},
	    {&arpNoteModeMenuForDrums, {&nodes::arp_note_mode_from_octave_mode_drums}},
	};
	return pages;
}

/// @brief Every legacy item with the nodes the ten roots pair it with, and the chained pages.
const Pairing& pairing() {
	static const Pairing pairs = [] {
		Pairing out;
		const std::pair<::MenuItem*, const Node*> roots[] = {
		    {&soundEditorRootMenu, &nodes::sound_root},
		    {&soundEditorRootMenuDrum, &nodes::root_drum},
		    {&soundEditorRootMenuMIDIOrCV, &nodes::root_midi_or_cv},
		    {&soundEditorRootMenuMidiDrum, &nodes::root_midi_drum},
		    {&soundEditorRootMenuGateDrum, &nodes::root_gate_drum},
		    {&soundEditorRootMenuAudioClip, &nodes::root_audio_clip},
		    {&soundEditorRootMenuPerformanceView, &nodes::root_performance},
		    {&soundEditorRootMenuSongView, &nodes::root_song},
		    {&soundEditorRootMenuKitGlobalFX, &nodes::root_kit_global_fx},
		    {&settingsRootMenu, &nodes::root_settings},
		};
		for (const auto& [legacy_root, node_root] : roots) {
			pair_walk(*node_root, *legacy_root, out);
		}
		for (const auto& [item, pages] : chained_pages()) {
			out[item].insert(out[item].end(), pages.begin(), pages.end());
		}
		return out;
	}();
	return pairs;
}

// ---------------------------------------------------------------------------------------------
// The named exceptions.

/// @brief What a node can disagree on.
enum Aspect : uint8_t {
	RANGE = 1,
	NAMES = 2,
	TITLE = 4,
	READ = 8,
	WRITE = 16,
};

/// @brief A node that disagrees with its row, which aspects, and why. The spec fails if it no longer disagrees.
struct NodeException {
	Family family; ///< the owner's family: the same path in another family's table is another row
	std::string_view path;
	uint8_t aspects;
	std::string_view reason;
	std::string_view only{}; ///< empty = every finding of those aspects; else only the findings containing this text
};

/// @brief A stored value the record cannot hold for a row, set on the engine where the node reads it.
struct UnsetProbe {
	std::string_view path;
	int32_t stored;
	void (*set)(const deluge::scenes::menu::MenuContext&);
};

void unset_note_for_drum(const deluge::scenes::menu::MenuContext& ctx) {
	ctx.sound->outputMidiNoteForDrum = MIDI_NOTE_NONE;
}

const UnsetProbe* unset_probe_of(std::string_view path) {
	static const UnsetProbe probes[] = {{"midiOutput/noteForDrum", 255, &unset_note_for_drum}};
	for (const UnsetProbe& probe : probes) {
		if (probe.path == path) {
			return &probe;
		}
	}
	return nullptr;
}

const std::vector<NodeException>& node_exceptions() {
	static const std::vector<NodeException> list{
	    {Family::SONG, "reverb/roomSize", NAMES | TITLE,
	     "legacy renames the item Time under the MUTABLE and DIGITAL reverb models and the table gives that one name; "
	     "the node always shows the Freeverb name, Room Size (`tables_3_spec.cpp` parks it)"},
	    {Family::SONG, "reverb/width", NAMES | TITLE,
	     "legacy renames the item Diffusion under the MUTABLE and DIGITAL reverb models and the table gives that one "
	     "name; the node always shows the Freeverb name, Width (`tables_3_spec.cpp` parks it)"},
	    {Family::SONG, "reverb/compressor/shape", WRITE,
	     "the node's write omits the top step's special case, as legacy's does (menu 50 stores 2147483602, not "
	     "2147483647)",
	     "write(50)"},
	};
	return list;
}

const NodeException* exception_of(Family family, std::string_view path) {
	for (const NodeException& e : node_exceptions()) {
		if (e.family == family && e.path == path) {
			return &e;
		}
	}
	return nullptr;
}

// ---------------------------------------------------------------------------------------------
// One node against one row.

std::string node_text(uint16_t id) {
	deluge::ui::shell::TextScratch scratch;
	return std::string{deluge::scenes::deluge_profile().text_or_digits(id, scratch)};
}

/// @brief A node's title with its `*` replaced by the owner's number, which the path's first digit gives.
std::string node_title(const Node& node, std::string_view path) {
	std::string title = node_text(node.title);
	const size_t star = title.find('*');
	const size_t digit = path.find_first_of("0123456789");
	if (star != std::string::npos && digit != std::string_view::npos) {
		title[star] = path[digit];
	}
	return title;
}

struct Findings {
	std::vector<std::string> range, names, title, read, write;
	bool any() const { return !(range.empty() && names.empty() && title.empty() && read.empty() && write.empty()); }
};

std::string menu_show(const Case& c, std::string_view what, int64_t at, int64_t node_value, int64_t row_value) {
	return std::string{c.path} + " " + std::string{what} + "(" + std::to_string(at) + ") node "
	       + std::to_string(node_value) + " row " + std::to_string(row_value);
}

/// @brief The node's names, as the menu lists them.
std::vector<std::string> node_names(const Node& node, const deluge::scenes::menu::MenuContext& ctx) {
	std::array<std::string_view, deluge::scenes::menu::kMaxDynamicNames> out{};
	std::array<deluge::ui::shell::TextScratch, deluge::scenes::menu::kMaxDynamicNames> scratch{};
	const size_t count = bindings::names(node, ctx, out, scratch);
	std::vector<std::string> list;
	for (size_t i = 0; i < count; ++i) {
		list.emplace_back(out[i]);
	}
	return list;
}

bool node_lists_names(const Node& node) {
	return node.bind.dynamic_names != nullptr || !node.bind.names.empty();
}

/// @brief A row whose menu is not one value of one row, so a check cannot set it and read the menu back.
struct Undriven {
	std::string_view path;
	uint8_t aspects; ///< the aspects not compared (RANGE, READ, WRITE)
	std::string_view reason;
};

const std::vector<Undriven>& undriven_rows() {
	static const std::vector<Undriven> list{
	    {"deviceDefinitionFileName", RANGE | READ | WRITE,
	     "the row is the definition file's name (text); the node is a checkbox of whether a file is linked, and "
	     "toggling it opens the file browser"},
	    {"mode", RANGE | READ | WRITE,
	     "one menu item lists the audio sources over two rows, the mode and the input channel, so no menu value is one "
	     "row's value"},
	    {"inputChannel", RANGE | READ | WRITE, "as mode: the item's list spans the mode row too"},
	    {"outputRecordingFrom", RANGE | READ | WRITE,
	     "the menu lists the song's other tracks by position, where the row holds a track's durable id"},
	};
	return list;
}

bool undriven(const Case& c, Aspect aspect) {
	for (const Undriven& u : undriven_rows()) {
		if (u.path == c.path) {
			return (u.aspects & aspect) != 0;
		}
	}
	return false;
}

Findings compare(const Case& c, const Node& node, const NodeException* exception) {
	Findings f;
	const deluge::scenes::menu::MenuContext& ctx = enter(c.where, c.source);
	const SettingInfo& row = row_of(c);
	const SettingContext shown = context_of(c.where, c.family);
	const bool listed_choice = row.kind == SettingKind::CHOICE
	                           && deluge::engine::mapping_of(row).kind == deluge::engine::DisplayMappingKind::LINEAR;
	const std::vector<int32_t> choices = listed_choice ? applicable(row, shown, ctx, c.where) : std::vector<int32_t>{};

	// The range. A named-only group's row is a plain field the menu lists or toggles by its own encoding, so its
	// range is not a menu range.
	const IntRange node_range = bindings::range(node, ctx);
	const Range want_range = listed_choice ? Range{0, static_cast<int32_t>(choices.size()) - 1} : menu_range(row);
	if (!undriven(c, RANGE) && (node_range.min != want_range.first || node_range.max != want_range.second)) {
		f.range.push_back(std::string{c.path} + " range " + std::to_string(node_range.min) + ".."
		                  + std::to_string(node_range.max) + " row " + std::to_string(want_range.first) + ".."
		                  + std::to_string(want_range.second));
	}

	// The names: the node's own, and for a listing node each option.
	for (const NamesOwner& owner : owners_of(c.family)) {
		const SettingNames* names = setting_names(owner, c.path, shown);
		if (names == nullptr) {
			continue;
		}
		const std::string have_name = node_text(node.name);
		const std::string want_name = text(names->name);
		if (have_name != want_name) {
			f.names.push_back(std::string{c.path} + " name node '" + have_name + "' row '" + want_name + "'");
		}
		const std::string have_title = node_title(node, c.path);
		const std::string want_title = title_of(*names, c.path);
		if (have_title != want_title) {
			f.title.push_back(std::string{c.path} + " title node '" + have_title + "' row '" + want_title + "'");
		}
		if (!node_lists_names(node)) {
			continue;
		}
		std::vector<std::string> have = node_names(node, ctx);
		std::vector<std::string> want;
		if (undriven(c, RANGE)) {
			// The menu's options by position, from the first: a list the context shortens (a clip's has no
			// "obey parent") is a prefix of the table's.
			for (size_t i = 0; i < have.size(); ++i) {
				want.push_back(choice_text(*names, i, false));
			}
			if (have.size() > names->choices.size()) {
				f.names.push_back(std::string{c.path} + " lists " + std::to_string(have.size()) + " row "
				                  + std::to_string(names->choices.size()));
			}
		}
		else if (listed_choice) {
			for (const int32_t choice : choices) {
				want.push_back(choice_text(*names, static_cast<size_t>(choice), false));
			}
		}
		else {
			for (int32_t i = want_range.first; i <= want_range.second && i >= 0; ++i) {
				want.push_back(choice_text(*names, static_cast<size_t>(i), false));
			}
		}
		if (have.size() != want.size()) {
			f.names.push_back(std::string{c.path} + " lists " + std::to_string(have.size()) + " row "
			                  + std::to_string(want.size()));
		}
		for (size_t i = 0; i < have.size() && i < want.size(); ++i) {
			if (have[i] != want[i]) {
				f.names.push_back(std::string{c.path} + " option " + std::to_string(i) + " node '" + have[i] + "' row '"
				                  + want[i] + "'");
			}
		}
	}

	// The reads.
	const SettingInfo* partner = partner_of(c, row);
	if (undriven(c, READ)) {
		// no read
	}
	else if (listed_choice) {
		for (size_t position = 0; position < choices.size(); ++position) {
			set_stored(c.family, ctx, c.where, row, choices[position]);
			const int32_t got = bindings::read(node, ctx);
			if (got != static_cast<int32_t>(position)) {
				f.read.push_back(menu_show(c, "read", choices[position], got, static_cast<int64_t>(position)));
			}
		}
	}
	else if (partner == nullptr) {
		const auto [least, greatest] = deluge::engine::stored_bounds(row);
		std::vector<int32_t> probes{least, greatest, row.default_value};
		probes.insert(probes.end(), c.stored.begin(), c.stored.end());
		for (const int32_t stored : probes) {
			set_stored(c.family, ctx, c.where, row, stored);
			const std::optional<int32_t> now = get_stored(c.family, ctx, c.where, row);
			const int32_t held = c.lossy ? now.value_or(-99999) : stored;
			const int32_t got = bindings::read(node, ctx);
			const int32_t want = to_menu(row, held);
			if (got != want) {
				f.read.push_back(menu_show(c, "read", held, got, want));
			}
		}
	}
	else {
		for (const auto& [stored, partner_stored] : c.pairs) {
			set_stored(c.family, ctx, c.where, row, stored);
			set_stored(c.family, ctx, c.where, *partner, partner_stored);
			const int64_t at = int64_t{stored} * 1000 + partner_stored;
			const int32_t got = bindings::read(node, ctx);
			const int32_t want = to_menu(row, stored, partner_stored);
			if (got != want) {
				f.read.push_back(menu_show(c, "read", at, got, want));
			}
		}
	}

	// Where the row has no value, what the node shows.
	if (const UnsetProbe* probe = unset_probe_of(c.path); probe != nullptr) {
		probe->set(ctx);
		const int32_t got = bindings::read(node, ctx);
		const int32_t want = to_menu(row, probe->stored);
		if (got != want) {
			f.read.push_back(menu_show(c, "read unset", probe->stored, got, want));
		}
	}

	// The writes: the node's own range bounds them, as the legacy item's did.
	if (undriven(c, WRITE)) {
		return f;
	}
	for (const int32_t menu : menu_probes({node_range.min, node_range.max})) {
		bindings::write(node, ctx, menu);
		const int64_t got = get_stored(c.family, ctx, c.where, row).value_or(-99999);
		int64_t want = -12345;
		int64_t got_partner = 0;
		int64_t want_partner = 0;
		if (listed_choice) {
			want =
			    menu >= 0 && static_cast<size_t>(menu) < choices.size() ? choices[static_cast<size_t>(menu)] : -12345;
		}
		else {
			const std::optional<std::pair<int32_t, int32_t>> stored = from_menu(row, menu);
			if (stored) {
				want = stored->first;
				want_partner = stored->second;
			}
			if (c.lossy && stored) {
				set_stored(c.family, ctx, c.where, row, stored->first);
				want = get_stored(c.family, ctx, c.where, row).value_or(-12345);
			}
			if (partner != nullptr) {
				got_partner = get_stored(c.family, ctx, c.where, *partner).value_or(-99999);
				if (!stored) {
					want_partner = -12345;
				}
			}
		}
		if (got != want || got_partner != want_partner) {
			f.write.push_back(menu_show(c, "write", menu, got, want)
			                  + (partner != nullptr ? " partner " + std::to_string(got_partner) + " row "
			                                              + std::to_string(want_partner)
			                                        : std::string{}));
		}
	}
	return f;
}

// ---------------------------------------------------------------------------------------------
// The run.

struct Tally {
	std::vector<std::string> unpaired;
	std::set<std::tuple<Family, std::string_view, uint8_t>> seen; ///< the exceptions' aspects that did differ
	std::set<const Node*> compared;                               ///< the distinct nodes a case was compared with
	std::set<const Node*> excepted;                               ///< those of them an exception covers
	bool ran = false;
};

Tally& tally() {
	static Tally t;
	return t;
}

/// @brief Whether @p finding is excused by @p exception for @p aspect; records that the exception still differs.
bool excused(const NodeException* exception, Aspect aspect, const std::string& finding) {
	if (exception == nullptr || (exception->aspects & aspect) == 0) {
		return false;
	}
	if (!exception->only.empty() && finding.find(exception->only) == std::string::npos) {
		return false;
	}
	tally().seen.emplace(exception->family, exception->path, aspect);
	std::fprintf(stdout, "descriptor_coherence_spec: excepted %s\n", finding.c_str());
	return true;
}

void judge(auto& self, const Case& c, const Node& node, const Findings& f, const NodeException* exception) {
	const struct {
		Aspect aspect;
		const std::vector<std::string>& found;
		const char* label;
	} aspects[] = {
	    {RANGE, f.range, "range"}, {NAMES, f.names, "names"}, {TITLE, f.title, "title"},
	    {READ, f.read, "read"},    {WRITE, f.write, "write"},
	};
	for (const auto& a : aspects) {
		std::string unexcused;
		for (const std::string& finding : a.found) {
			if (!excused(exception, a.aspect, finding) && unexcused.empty()) {
				unexcused = finding;
			}
		}
		const std::string at = std::string{c.path} + " node '" + node_text(node.name) + "' " + a.label;
		expect(at + (unexcused.empty() ? " agrees" : " differs: " + unexcused)).to_equal(at + " agrees");
	}
}

void run_cases(auto& self, const std::vector<const Case*>& cases) {
	for (const Case* c : cases) {
		const auto found = pairing().find(c->item);
		if (found == pairing().end()) {
			tally().unpaired.emplace_back(c->path);
			continue;
		}
		const NodeException* exception = exception_of(c->family, c->path);
		for (const Node* node : found->second) {
			tally().compared.insert(node);
			if (exception != nullptr) {
				tally().excepted.insert(node);
			}
			judge(self, *c, *node, compare(*c, *node, exception), exception);
		}
	}
}

void run_all(auto& self) {
	if (tally().ran) {
		return;
	}
	tally().ran = true;
	AudioEngine::micPluggedIn = false;
	AudioEngine::lineInPluggedIn = false;
	run_cases(self, all_name_cases());
	// With an input plugged in the oscillator types list the inputs.
	AudioEngine::micPluggedIn = true;
	std::vector<const Case*> oscillator_types;
	for (const Case* c : all_cases()) {
		if (c->path.starts_with("osc") && c->path.ends_with("/type")) {
			oscillator_types.push_back(c);
		}
	}
	run_cases(self, oscillator_types);
	AudioEngine::micPluggedIn = false;
}

// ---------------------------------------------------------------------------------------------
// The nodes no row reaches.

/// @brief The device settings: the value nodes under the settings root, which hold a device's state (FlashStorage, a
/// runtime feature, a MIDI device, a CV or gate output, a pad colour) rather than a song's or a sound's, so no engine
/// or model row holds them. Each is named by the names from the root to it, with how many nodes carry that path.
struct DeviceNode {
	std::string_view path;
	size_t count;
};

constexpr DeviceNode kDeviceSettings[] = {
    {"Settings > Community Features > Shell Menu", 1},
    {"Settings > Community Features > Shortcut Overlay", 1},
    {"Settings > Community Features > Show Battery Level", 1},
    {"Settings > Community Features > Trim From Start Of Audio Clips", 1},
    {"Settings > Community Features > Rounded Corners", 1},
    {"Settings > Community Features > Horizontal Menus", 1},
    {"Settings > Community Features > Alternative Tap Tempo Behaviour", 1},
    {"Settings > Community Features > Grid View Loop Layer Pads", 1},
    {"Settings > Community Features > Alternative Playback Start Behaviour", 1},
    {"Settings > Community Features > Chord Keyboards", 1},
    {"Settings > Community Features > Launch Event Playhead", 1},
    {"Settings > Community Features > KB View Sidebar Menu Exit", 1},
    {"Settings > Community Features > DX7 Engine", 1},
    {"Settings > Community Features > Light Shift", 1},
    {"Settings > Community Features > Sticky Shift", 1},
    {"Settings > Community Features > Display Norns Layout", 1},
    {"Settings > Community Features > Highlight Incoming Notes", 1},
    {"Settings > Community Features > Sync Scaling Action", 1},
    {"Settings > Community Features > Alternative Golden Knob Delay Params", 1},
    {"Settings > Community Features > Delete Unused Kit Rows", 1},
    {"Settings > Community Features > Catch Notes", 1},
    {"Settings > Community Features > Quantize", 1},
    {"Settings > Community Features > Fine Tempo Knob", 1},
    {"Settings > Community Features > Drum Randomizer", 1},
    {"Settings > Recording > Loop Command", 1},
    {"Settings > Recording > Threshold Recording > Mode", 1},
    {"Settings > Recording > Sampling monitoring", 1},
    {"Settings > Recording > Loop margins", 1},
    {"Settings > Recording > Quantization", 1},
    {"Settings > Recording > Count-in bars", 1},
    {"Settings > Play-Cursor", 1},
    {"Settings > Sample Preview", 1},
    {"Settings > Pads > Colours > Once", 1},
    {"Settings > Pads > Colours > Fill", 1},
    {"Settings > Pads > Colours > Soloed", 1},
    {"Settings > Pads > Colours > Muted", 1},
    {"Settings > Pads > Colours > Stopped", 1},
    {"Settings > Pads > Colours > Active", 1},
    {"Settings > Pads > Keyboard for Text", 1},
    {"Settings > Pads > Shortcut Version", 1},
    {"Settings > Defaults > Screensaver > Timeout", 1},
    {"Settings > Defaults > Screensaver > Mode", 1},
    {"Settings > Defaults > Hold Press Time", 1},
    {"Settings > Defaults > High CPU Indicator", 1},
    {"Settings > Defaults > Sample Slice Mode", 1},
    {"Settings > Defaults > Pad Brightness", 1},
    {"Settings > Defaults > Startup Song", 1},
    {"Settings > Defaults > Patch cable polarity", 1},
    {"Settings > Defaults > Metronome", 1},
    {"Settings > Defaults > Bend range", 1},
    {"Settings > Defaults > Resolution", 1},
    {"Settings > Defaults > Velocity", 1},
    {"Settings > Defaults > Scale > Init Scale", 1},
    {"Settings > Defaults > Key", 1},
    {"Settings > Defaults > Swing Interval", 1},
    {"Settings > Defaults > Swing Amount", 1},
    {"Settings > Defaults > Tempo", 1},
    {"Settings > Defaults > Automation > Disable Audition Pad Shortcuts", 1},
    {"Settings > Defaults > Automation > Nudge Note", 1},
    {"Settings > Defaults > Automation > Shift", 1},
    {"Settings > Defaults > Automation > Clear", 1},
    {"Settings > Defaults > Automation > Interpolation", 1},
    {"Settings > Defaults > UI > Use Sharps (#)", 1},
    {"Settings > Defaults > UI > Clip Type > Use Last Clip Type", 1},
    {"Settings > Defaults > UI > Clip Type > New Clip Type", 1},
    {"Settings > Defaults > UI > Keyboard > Favourites", 1},
    {"Settings > Defaults > UI > Keyboard > Sidebar Controls > Momentary Modwheel", 1},
    {"Settings > Defaults > UI > Keyboard > Sidebar Controls > Momentary Velocity", 1},
    {"Settings > Defaults > UI > Keyboard > Layout", 1},
    {"Settings > Defaults > UI > Song > Grid > Empty pads > Create + Record", 1},
    {"Settings > Defaults > UI > Song > Grid > Empty pads > Unarm", 1},
    {"Settings > Defaults > UI > Song > Grid > Select in green mode", 1},
    {"Settings > Defaults > UI > Song > Grid > Default active mode", 1},
    {"Settings > Defaults > UI > Song > Layout", 1},
    {"Settings > Defaults > UI > Accessibility > Menu Highlighting", 1},
    {"Settings > Defaults > UI > Accessibility > Shortcuts", 1},
    {"Settings > MIDI > Devices >> MIDI devices > MIDI-Thru", 1},
    {"Settings > MIDI > Devices >> MIDI devices > Use Relative CC", 1},
    {"Settings > MIDI > Devices >> MIDI devices > Clock In", 1},
    {"Settings > MIDI > Devices >> MIDI devices > Clock Out", 1},
    {"Settings > MIDI > Devices >> MIDI devices > Velocity", 1},
    {"Settings > MIDI > Differentiate inputs", 1},
    {"Settings > MIDI > Takeover", 1},
    {"Settings > MIDI > Transpose", 1},
    {"Settings > MIDI > MIDI-Thru", 1},
    {"Settings > MIDI > Select Kit Row", 1},
    {"Settings > MIDI > MIDI-Follow > Display Param", 1},
    {"Settings > MIDI > MIDI-Follow > Feedback > Filter Responses", 1},
    {"Settings > MIDI > MIDI-Follow > Feedback > Automation Feedback", 1},
    {"Settings > MIDI > MIDI-Follow > Feedback > Channel", 1},
    {"Settings > MIDI > MIDI-Follow > Kit Root Note", 1},
    {"Settings > MIDI > MIDI-Follow > Channel > Channel Track**", 16},
    {"Settings > MIDI > MIDI-Follow > Channel > Channel C", 1},
    {"Settings > MIDI > MIDI-Follow > Channel > Channel B", 1},
    {"Settings > MIDI > MIDI-Follow > Channel > Channel A", 1},
    {"Settings > MIDI > Clock > Tempo Magnitude Matching", 1},
    {"Settings > MIDI > Clock > Input", 1},
    {"Settings > MIDI > Clock > Output", 1},
    {"Settings > Trigger Clock > Output > PPQN", 1},
    {"Settings > Trigger Clock > Input > Auto-Start", 1},
    {"Settings > Trigger Clock > Input > PPQN", 1},
    {"Settings > Gate >> Minimum Off-Time", 1},
    {"Settings > Gate >> Gate out* mode", 1},
    {"Settings > CV >> CV output * > Transpose", 1},
    {"Settings > CV >> CV output * > Volts per Octave", 1},
};

constexpr std::string_view kDeviceReason =
    "a device setting (FlashStorage, a runtime feature, a MIDI device, a CV or gate output, a pad colour): the "
    "device's, not a song's or a sound's, so no engine or model row holds it";

/// @brief A subtree whose value nodes have no row, and why.
struct Subtree {
	const Node* root;
	std::string_view reason;
};

const std::vector<Subtree>& subtrees() {
	static const std::vector<Subtree> list{
	    {&nodes::note_editor_root,
	     "the selected note's property, edited as an offset over every note under a zoomed-out square: no row"},
	    {&nodes::note_row_editor_root, "the selected note row's property, edited as an offset over its notes: no row"},
	    {&nodes::configure_stem_export, "an option of the stem export, which the export reads: no row"},
	    {&nodes::kit_fx_configure_stem_export, "an option of the stem export, which the export reads: no row"},
	    {&nodes::song_threshold_recording, "the song's recording threshold mode: a runtime field seeded from "
	                                       "FlashStorage when the song is made, and not saved: no row"},
	};
	return list;
}

/// @brief A node whose path is a `kSkips` entry of `support/descriptor_cases.h`, whose reason it takes.
struct Skipped {
	const Node* node;
	std::string_view path;
};

const std::vector<Skipped>& skipped_nodes() {
	static const std::vector<Skipped> list{
	    {&nodes::reverb_sidechain_sync, "reverb/compressor/syncLevel"},
	    {&nodes::reverb_sidechain_volume, "reverb/compressor/volume"},
	    {&nodes::stutter_direction, "stutter/reverse"},
	};
	return list;
}

std::string_view skip_reason(std::string_view path) {
	for (const Skip& skip : kSkips) {
		if (skip.path == path) {
			return skip.reason;
		}
	}
	return {};
}

void collect(const Node& node, std::set<const Node*>& out) {
	if (!out.insert(&node).second) {
		return;
	}
	for (const Node* child : node.children) {
		collect(*child, out);
	}
	for (const Node* child : node.chain_targets) {
		collect(*child, out);
	}
}

/// @brief A node with the names from a root to it, the first way a walk reaches it.
struct Reached {
	const Node* node;
	std::string path;
};

/// @brief Every node of the tree that has a read or write hook.
std::vector<Reached> hooked_nodes() {
	std::set<const Node*> seen;
	std::vector<Reached> out;
	std::vector<Reached> stack;
	for (const Node* root : nodes::kAllRoots) {
		stack.push_back({root, node_text(root->name)});
	}
	while (!stack.empty()) {
		const Reached at = stack.back();
		stack.pop_back();
		if (!seen.insert(at.node).second) {
			continue;
		}
		for (const Node* child : at.node->children) {
			stack.push_back({child, at.path + " > " + node_text(child->name)});
		}
		for (const Node* child : at.node->chain_targets) {
			stack.push_back({child, at.path + " >> " + node_text(child->name)});
		}
		if (at.node->bind.get != nullptr || at.node->bind.set != nullptr) {
			out.push_back(at);
		}
	}
	return out;
}

/// @brief The reason @p reached has no row, or empty where nothing explains it.
std::string_view unpathed_reason(const Reached& reached) {
	for (const Skipped& skipped : skipped_nodes()) {
		if (skipped.node == reached.node) {
			return skip_reason(skipped.path);
		}
	}
	for (const Subtree& subtree : subtrees()) {
		std::set<const Node*> under;
		collect(*subtree.root, under);
		if (under.contains(reached.node)) {
			return subtree.reason;
		}
	}
	for (const DeviceNode& device : kDeviceSettings) {
		if (device.path == reached.path) {
			return kDeviceReason;
		}
	}
	return {};
}

/// @brief The nodes under the settings root that no case compared with a row, by path, and how many share each.
std::map<std::string, size_t> uncompared_device_nodes() {
	std::set<const Node*> under;
	collect(nodes::root_settings, under);
	std::map<std::string, size_t> out;
	for (const Reached& reached : hooked_nodes()) {
		if (under.contains(reached.node) && !tally().compared.contains(reached.node)) {
			++out[reached.path];
		}
	}
	return out;
}

// ---------------------------------------------------------------------------------------------
// The param nodes' names.

/// @brief Digits and case folded, so a title numbered for its owner compares with the table's.
std::string folded(std::string text) {
	for (char& ch : text) {
		ch = (ch >= '0' && ch <= '9') ? '*' : static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
	}
	return text;
}

} // namespace

// clang-format off
describe descriptor_coherence_body("The scenes' menu tables against the setting descriptors and the host names", $ {
	it("gives each node the range, names, title, reads and writes of its row", _ {
		run_all(self);
		std::fprintf(stdout, "descriptor_coherence_spec: %zu distinct nodes compared (%zu excepted), %zu cases with no node\n",
		             tally().compared.size(), tally().excepted.size(), tally().unpaired.size());
		for (const std::string& path : tally().unpaired) {
			std::fprintf(stdout, "descriptor_coherence_spec: no node for %s\n", path.c_str());
		}
		expect(tally().compared.size()).to_be_greater_than(size_t{0});
		expect(tally().unpaired.size()).to_equal(size_t(0));
	});
	it("names only rows a case shows, for each row it does not drive", _ {
		for (const Undriven& u : undriven_rows()) {
			bool shown = false;
			for (const Case* c : all_name_cases()) {
				shown = shown || c->path == u.path;
			}
			expect(std::string{u.path} + (shown ? " is a case" : " is no case")).to_equal(std::string{u.path} + " is a case");
		}
	});
	it("still differs wherever it names an exception", _ {
		run_all(self);
		for (const NodeException& e : node_exceptions()) {
			std::fprintf(stdout, "descriptor_coherence_spec: exception %.*s aspects %u: %.*s\n", int(e.path.size()),
			             e.path.data(), unsigned(e.aspects), int(e.reason.size()), e.reason.data());
			for (const Aspect aspect : {RANGE, NAMES, TITLE, READ, WRITE}) {
				if ((e.aspects & aspect) != 0) {
					const std::string at = std::string{e.path} + " aspect " + std::to_string(aspect);
					expect(at + (tally().seen.contains({e.family, e.path, aspect}) ? " differs" : " agrees"))
					    .to_equal(at + " differs");
				}
			}
		}
	});
	it("reads and writes the sidechain sync's triplet and dotted values as legacy does", _ {
		// A file's triplet or dotted sidechain sync shows as 10..27, past the menu's 0..9: `to_menu` shows it and
		// `from_menu` refuses it. The node's range is the row's, its read is `to_menu`, and its write of a value past
		// the range decodes the level and type as legacy's does.
		run_all(self);
		const SettingInfo& row = *deluge::engine::find_setting(deluge::effects::kEffectSettings, "sidechain/syncLevel");
		const SettingInfo& type = *deluge::engine::find_setting(deluge::effects::kEffectSettings, "sidechain/syncType");
		const Node& node = *pairing().at(&sidechainSyncMenu).front();
		const MenuContext& ctx = enter(Where::SYNTH);
		expect(bindings::range(node, ctx).max).to_equal(menu_range(row).second);
		for (const auto& [level, kind] : std::vector<Range>{{1, 1}, {9, 1}, {1, 2}, {9, 2}, {4, 1}}) {
			set_stored(Family::EFFECT, ctx, Where::SYNTH, row, level);
			set_stored(Family::EFFECT, ctx, Where::SYNTH, type, kind);
			const std::string at = "sidechain sync " + std::to_string(level) + "/" + std::to_string(kind);
			expect(at + " " + std::to_string(bindings::read(node, ctx)))
			    .to_equal(at + " " + std::to_string(to_menu(row, level, kind)));
			expect(to_menu(row, level, kind) > menu_range(row).second).to_be_true();
		}
		for (const int32_t menu : {10, 18, 19, 27}) {
			expect(from_menu(row, menu).has_value()).to_be_false();
			bindings::write(node, ctx, menu);
			const std::string at = "sidechain sync write " + std::to_string(menu);
			const int32_t level = (menu - 1) % 9 + 1;
			const int32_t kind = (menu - 1) / 9;
			expect(at + " " + std::to_string(get_stored(Family::EFFECT, ctx, Where::SYNTH, row).value_or(-1)) + "/"
			       + std::to_string(get_stored(Family::EFFECT, ctx, Where::SYNTH, type).value_or(-1)))
			    .to_equal(at + " " + std::to_string(level) + "/" + std::to_string(kind));
		}
	});
	it("titles a param node as the item, which legacy does not title as the param's table name", _ {
		// A param's table name is `getParamDisplayName`, the knob popup's; the menu item's own title is what a node
		// carries, and legacy's differs from the popup's name in the same places.
		run_all(self);
		size_t same = 0;
		size_t differ = 0;
		std::set<const Node*> all;
		for (const Node* root : nodes::kAllRoots) {
			collect(*root, all);
		}
		const MenuContext& ctx = enter(Where::SYNTH);
		(void)ctx;
		for (const Node* node : all) {
			const deluge::scenes::menu::ParamRef ref = node->bind.param;
			if (!ref.valid() || node->bind.get != nullptr || node->bind.set != nullptr) {
				continue;
			}
			const deluge::engine::ParamId id = deluge::bridge::paramIdOf(ref.kind, ref.index);
			const bool global = ref.kind == deluge::modulation::params::Kind::UNPATCHED_GLOBAL;
			const std::span<const deluge::engine::ParamInfo> table =
			    global ? std::span<const deluge::engine::ParamInfo>{deluge::global_effectable::kParams}
			           : std::span<const deluge::engine::ParamInfo>{deluge::instrument::kParams};
			const deluge::engine::ParamInfo* row = deluge::engine::find_param(table, id);
			expect(row != nullptr).to_be_true();
			if (row == nullptr) {
				continue;
			}
			const deluge::l10n::ParamNameContext state{.lpf_mode = deluge::effects::FilterMode::SVF_NOTCH,
			                                           .hpf_mode = deluge::effects::FilterMode::SVF_BAND};
			const std::string table_name =
			    deluge::l10n::get(deluge::l10n::param_name(global ? engine::kGlobalEffectable : engine::kDelugeInstrument,
			                                               id, state));
			if (folded(node_text(node->title)) == folded(table_name)) {
				++same;
				continue;
			}
			++differ;
			// Legacy's own item is titled otherwise too.
			bool paired = false;
			for (const auto& [item, nodes_of_item] : pairing()) {
				if (std::find(nodes_of_item.begin(), nodes_of_item.end(), node) != nodes_of_item.end()) {
					paired = true;
					const std::string at = "param " + table_name + " item " + node_text(node->title);
					expect(at + (folded(std::string{const_cast<::MenuItem*>(item)->getTitle()}) == folded(table_name)
					                 ? " titled as the table"
					                 : " titled otherwise"))
					    .to_equal(at + " titled otherwise");
				}
			}
			// A title that differs from the table's with no legacy item to compare would go unchecked.
			expect(paired).to_be_true("param " + table_name + " titled " + node_text(node->title) + " has no legacy item");
		}
		std::fprintf(stdout, "descriptor_coherence_spec: %zu param nodes titled as their table name, %zu titled as the item\n",
		             same, differ);
		expect(same > 0).to_be_true();
		expect(differ > 0).to_be_true();
	});
	it("names why each value node no row reaches has none", _ {
		run_all(self);
		size_t unreached = 0;
		for (const Reached& reached : hooked_nodes()) {
			if (tally().compared.contains(reached.node)) {
				continue;
			}
			++unreached;
			const std::string at = "node " + reached.path;
			expect(at + (unpathed_reason(reached).empty() ? " unexplained" : " explained"))
			    .to_equal(at + " explained");
		}
		std::fprintf(stdout, "descriptor_coherence_spec: %zu value nodes have no row\n", unreached);
	});
	it("lists exactly the device settings under the settings root", _ {
		run_all(self);
		std::map<std::string, size_t> found = uncompared_device_nodes();
		for (const DeviceNode& device : kDeviceSettings) {
			const std::string at = "device setting " + std::string{device.path};
			const auto at_path = found.find(std::string{device.path});
			expect(at_path != found.end() ? at_path->second : size_t{0}).to_equal(device.count, at);
			found.erase(std::string{device.path});
		}
		for (const auto& [path, count] : found) {
			expect(count).to_equal(size_t{0}, "unlisted device setting " + path);
		}
	});
});
// clang-format on

DELUGE_SPEC(descriptor_coherence_spec, descriptor_coherence_body)
