/// @file
/// @brief `dump_menu_tree` -- walks every root of the declarative menu tree (`nodes::kAllRoots`,
///        `ui/scenes/menu/nodes/nodes.h`) and prints it as JSON, in the schema the Python `DocEmitter` of main's menu
///        generator emits.
///
/// ## The schema
///
/// `origin/main:src/deluge/gui/menu_item/generate/dmui/visitor/doc_emitter.py` emits, per menu
/// (`origin/main:src/deluge/gui/menu_item/generate/main.py`'s `top_level_menus`):
///
///   visit_menu (a leaf):         {"type": "basic", "title", "name", "description", ["available_when"]}
///   visit_submenu:                {"type": "submenu", "title", "name", "description", "children",
///                                  ["available_when"]}
///   visit_multimode:               {"type": "multimode", "modes": [...]}
///   visit_multimode_mode:          {"type": "multimode-mode", "title", "name", "available_when",
///                                  "description"}
///   visit_multicontext_instance:  {"type": "multicontext", "title", "name", "description"}
///
/// (`visit_multicontext` itself raises -- only a `.with_context()` instance is ever visited.)
/// `origin/main:src/deluge/gui/menu_item/generate/main.py` writes
/// `json.dumps([menu.visit(emitter) for menu in top_level_menus])`: a plain JSON array, no `sort_keys`, no indent.
/// `title`/`name` are the menu's l10n *key* string (e.g. `"STRING_FOR_LPF_FREQUENCY"`,
/// `origin/main:src/deluge/gui/menu_item/generate/dmui/dsl.py`'s `Menu.__init__` docstring), and `description` is read
/// verbatim from a `docs/menus/*.md` file the menu names (`read_description`).
///
/// ## How this dump differs
///
/// `Kind` (`node.h`) has no `MultiMode`/`MultiModeMode` counterpart: the enumerators with no users are absent
/// (`node.h`'s own note), and `MultiModeMenu` is a DSL concept that plain `Node` data
/// has no equivalent of. So `"multimode"`/`"multimode-mode"` never appear in this dump's actual output -- they are
/// listed above only so the schema this file approximates is documented in full. What this dump actually maps:
///
///   * `Kind::Submenu` -> `"submenu"`; every other `Kind` (a leaf, including `Kind::Custom`) ->
///     `"basic"`.
///   * a node with `Node::relevant` set -> `"available_when"` = `relevance::name_of(fn)` (the relevance registry) --
///     the *name* of the guarding predicate, not its result in any one context. A node with no `relevant` gets no
///     `"available_when"` key at all (`relevance::name_of(nullptr)` would answer `"always"`, but the mapping is keyed
///     on "a node with `relevant`", so a null one is left out rather than spelled out).
///   * a node with `Node::dynamic_children` set -> `"multicontext"` -- but this dump does not attempt to expand it.
///     The `"multicontext"` entry of `origin/main:src/deluge/gui/menu_item/generate/dmui/visitor/doc_emitter.py`
///     (`visit_multicontext_instance`, above) has **no `"children"` key at all** -- the Python DSL pre-resolves every
///     `.with_context()` instance to one context *before* the visitor runs, so the visitor itself never has more than
///     one context's worth of data to print. This dump has no equivalent pre-resolution step (there is no single "the"
///     context a `Node` tree is dumped under -- that is the whole reason `dynamic_children` takes a `MenuContext`
///     argument), so there is no verified shape to fall back to either. No node in the tree sets `dynamic_children`
///     (a tree-wide grep for `dynamic_submenu(` confirms it), so rather than keep a dormant, never-exercised guess at
///     a JSON shape, `emit_node` fails loudly instead: it prints the offending node's resolved name and `node_id` to
///     stderr and exits non-zero. Whoever adds the first real `dynamic_children` node has to stop there and design
///     this dump's shape against an actual case, rather than silently inheriting whatever this file guessed in the
///     abstract. The dump builds no `MenuContext` to expand against, and the walk never reads one: the only
///     `MenuContext`s it makes are the ones `option_names` asks the names hooks in (`g_names_contexts`): empty ones
///     with a source index, and the booted song's first instrument clip as its row editor sees it, for the values an
///     owner adds. They feed only `"options"`, never the walk. The one line that is load-bearing is
///     `l10n::chosenLanguage = &l10n::built_in::english;` in `dump_menu_tree`: `text_of()` segfaults without it.
///   * a node that offers values -> `"options"`, its value names in list order (`option_names`):
///     a target row's name table (with the names it takes with no audio input plugged in merged in), static
///     `bind.names`, a `bind.dynamic_names` hook asked in each of `g_names_contexts` and merged (`merge_names`), or a
///     toggle's two words, as its own page lists them. A custom page or a number spelled as words names its values
///     through the same two fields. A node with none gets no key.
///   * a page that draws its own list -> `"rows"`, each `{"name", "options"?, "rows"?}` (`drawn_rows`): "DX7
///     parameters" lists its rows and the params each reaches, as main's website reference does. No other node has
///     the key.
///   * a node with `Node::chain_targets` set -> a `"select_chain"` array of nested entries, in the
///     same shape as `"children"`: the pages a select press on that node opens instead of going up
///     a level (main's `MenuItem::selectButtonPress`). Seven nodes have one, and seven pages
///     appear in this dump **only** under a `"select_chain"` -- `arpOctaveModeToNoteModeMenu` and
///     its three relatives (`origin/main:src/deluge/gui/ui/menus.cpp`) and `cvSubmenu` / `gateModeMenu` /
///     `gateOffTimeMenu` (also `origin/main:src/deluge/gui/ui/menus.cpp`) are named by no submenu literal in main or
///     here. A node with no chain gets no key at all, as with `available_when`. A chain that revisits a node on its
///     own path is written as a `"ref"` stub (`emit_chain_ref`) rather than nested without end.
///   * `"id"` = `node_id(n)` on every entry -- the lookup key a future `.state` golden
///     (`app16/<id>`) resolves against. A node reached only through `dynamic_children` never gets
///     an id (`node_id.h`'s own contract: `children` and `chain_targets` are walked, a dynamic
///     hook's produce is not), so such an entry prints `"id": 0` -- 0 is reserved for "no node"
///     everywhere else in this registry too.
///   * `"name"`/`"title"` are resolved display text (`deluge_profile().text_or_digits`), not the
///     l10n symbolic key the Python schema carries: `Node::name`/`Node::title` are plain
///     `uint16_t` ids with no reverse mapping back to a `STRING_FOR_...` spelling, and resolved
///     text is what a JSON consumer -- or a human spot-checking this dump against
///     `website/src/content/docs/reference/menu_hierarchies.mdx` -- actually wants.
///   * `"description"` is dropped entirely -- structurally absent from every emitted entry, not present-but-empty. The
///     Python schema's `description` is a whole markdown file's contents, read from `docs/menus/`; `Node` (`node.h`)
///     carries nothing like it, so there is nothing to put there. Anything written against
///     `origin/main:src/deluge/gui/menu_item/generate/dmui/visitor/doc_emitter.py`'s shape (expecting
///     `entry["description"]` to exist, even as `""`) will `KeyError`/throw on this dump's output instead. That warning
///     is repeated where a consumer is more likely to actually read it: `website/src/data/generated/README.md`, next to
///     the JSON itself.
///
/// ## The walk
///
/// One JSON array, one entry per `nodes::kAllRoots` root that is not a menu group (`Node::is_group`: the chains'
/// paging composites, whose items the other roots already list), each walked over its own static `Node::children`
/// **and** `Node::chain_targets` -- the same two edge kinds `node_id.h`'s `assign_node_ids` walks over the same root
/// list, so every `"id"` printed here is one that call actually assigned. Sharing is preserved, not deduplicated: a
/// node reachable from more than one parent (e.g. `song_master` under both the song and audio-clip roots, or
/// `randomizer` under several) appears once per parent, each occurrence carrying the same `"id"`.
///
/// ## Determinism
///
/// Root order is `kAllRoots`'s own compile-time order; child order is `Node::children`'s own
/// declaration order (a `std::span` over a `constexpr` array, never reordered). JSON object keys
/// are written in a fixed alphabetical order (`available_when`, `children`, `id`, `name`,
/// `options`, `rows`, `select_chain`, `title`, `type`) baked into `emit_node`'s own field-by-field writes rather than
/// sorted at runtime --
/// there is no hash-map/set iteration anywhere on the emission path. `text_of` resolves through
/// `l10n::chosenLanguage`, pinned at the start of `dump_menu_tree` to the built-in English table before the walk
/// starts, so every run resolves the same language regardless of process environment. Two-space
/// indentation, one trailing newline at end of file. golden_vt_render's `menu_dump` scenario writes it, and
/// the `ui-menu-dump` gate compares the result byte for byte with the committed
/// `website/src/data/generated/menu-tree.json`.
#include "scenarios/menu_dump.h"

#include "gui/l10n/l10n.h"
#include "gui/l10n/setting_names.h"
#include "gui/l10n/strings.h"
#include "gui/ui/menu_view_of.h"
#include "gui/ui/shell_host/host_ports.h"
#include "model/clip/clip.h"
#include "model/song/song.h"
#include "ui/scenes/bind/dispatch.h"
#include "ui/scenes/bind/target.h"
#include "ui/scenes/menu/context.h"
#include "ui/scenes/menu/node.h"
#include "ui/scenes/menu/node_id.h"
#include "ui/scenes/menu/nodes/dx.h"
#include "ui/scenes/menu/nodes/dx_tables.h"
#include "ui/scenes/menu/nodes/nodes.h"
#include "ui/scenes/menu/relevance.h"
#include "ui/scenes/profile.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>

#include "instruments/classic/settings.h"
#include "plugin/types/plugin_id.h"
#include "plugin/types/setting.h"
#include <string_view>
#include <vector>

namespace menu = deluge::ui::scenes::menu;
namespace nodes = deluge::ui::scenes::menu::nodes;
namespace relevance = deluge::ui::scenes::menu::relevance;

namespace {

/// Whether @p target's row says when it applies (its descriptor carries conditions); a row with none is always shown.
bool target_is_conditional(const deluge::ui::scenes::bind::Target& target) {
	if (!target.valid()) {
		return false;
	}
	// A sound's routing rows are its group's; every other setting's the dump names by the instrument's table
	const bool routing = target.kind == deluge::ui::scenes::bind::Target::Kind::MODEL
	                     && target.group == deluge::project::SettingGroupId::SOUND_ROUTING;
	const std::span<const deluge::plugin::SettingInfo> table =
	    routing ? deluge::project::table_of(target.group) : deluge::instruments::classic::kSettings;
	const deluge::plugin::SettingInfo* row = deluge::plugin::find_setting(table, target.path);
	return row != nullptr && row->extras != nullptr
	       && (!row->extras->applies_when.empty() || !row->extras->applies_any.empty());
}

/// @brief `n` two-space-wide indents, as a fresh string -- the only indentation primitive the
///        emitter uses, so every call site's nesting level is visible at the call site itself.
std::string spaces(int level) {
	return std::string(static_cast<size_t>(level) * 2, ' ');
}

/// @brief Append `s` to `out` as a JSON string literal, with the handful of escapes JSON requires.
///        Anything else (including non-ASCII UTF-8, which every l10n string may contain) passes
///        through unescaped -- valid UTF-8 is valid inside a JSON string as-is.
void write_json_string(std::string& out, std::string_view s) {
	out += '"';
	for (unsigned char c : s) {
		switch (c) {
		case '"':
			out += "\\\"";
			break;
		case '\\':
			out += "\\\\";
			break;
		case '\n':
			out += "\\n";
			break;
		case '\r':
			out += "\\r";
			break;
		case '\t':
			out += "\\t";
			break;
		default:
			if (c < 0x20) {
				// Not `std::snprintf`: the firmware's own `third_party/printf/printf.h` `#define`s
				// `snprintf` to its custom `snprintf_` everywhere it is visible (which
				// `support/song_fixture.h`'s model includes drag in transitively), so a plain
				// hex formatter avoids the collision rather than fighting it.
				static constexpr char kHexDigits[] = "0123456789abcdef";
				out += "\\u00";
				out += kHexDigits[(c >> 4) & 0xf];
				out += kHexDigits[c & 0xf];
			}
			else {
				out += static_cast<char>(c);
			}
		}
	}
	out += '"';
}

/// @brief `id`, resolved through `deluge_profile()`'s English text bridge -- the OLED spelling
///        `website/.../menu_hierarchies.mdx` lists first for each entry.
std::string text_of(uint16_t id) {
	deluge::ui::shell::TextScratch scratch;
	return std::string{deluge::ui::scenes::deluge_profile().text_or_digits(id, scratch)};
}

/// @brief Add @p more to @p into, in list order: the values @p more lists before any @p into already has go in front
///        (a choice only an owner adds, ahead of the rest), the rest it lacks at the end (one only an owner or a
///        missing input adds, after them), each value once.
void merge_names(std::vector<std::string>& into, const std::vector<std::string>& more) {
	std::vector<std::string> front;
	bool seen_known = into.empty(); // a first list is taken as it stands
	for (const std::string& name : more) {
		if (std::ranges::find(into, name) != into.end()) {
			seen_known = true;
			continue;
		}
		if (std::ranges::find(front, name) != front.end()) {
			continue;
		}
		if (seen_known) {
			into.push_back(name);
		}
		else {
			front.push_back(name);
		}
	}
	into.insert(into.begin(), front.begin(), front.end());
}

/// @brief The names the row of the concrete target @p target gives its values: every choice it names, in the row's
///        order.
///
/// A sound's settings and effects are named as the Deluge instrument names them, a host chain's effects as the global
/// effectable names them where the instrument has no entry, and a model row as its setting group names it. A param or
/// a cable has no values to name.
std::vector<std::string> row_option_names(const deluge::ui::scenes::bind::Target& target) {
	using Kind = deluge::ui::scenes::bind::Target::Kind;
	const deluge::l10n::SettingNames* names = nullptr;
	switch (target.kind) {
	case Kind::SOUND:
		names = deluge::l10n::setting_names(deluge::plugin::kDelugeInstrument, target.path);
		break;
	case Kind::EFFECT:
		names = deluge::l10n::setting_names(deluge::plugin::kDelugeInstrument, target.path);
		if (names == nullptr) {
			names = deluge::l10n::setting_names(deluge::plugin::kGlobalEffectable, target.path);
		}
		break;
	case Kind::MODEL:
		names = deluge::l10n::setting_names(target.group, target.path);
		break;
	default:
		break;
	}
	const auto spelled = [](const deluge::l10n::SettingNames* table) {
		std::vector<std::string> out;
		if (table != nullptr) {
			for (const deluge::l10n::String id : table->choices) {
				out.push_back(text_of(static_cast<uint16_t>(id)));
			}
		}
		return out;
	};
	std::vector<std::string> out = spelled(names);
	if (names != nullptr && target.kind == Kind::SOUND) {
		// With no audio input plugged in, a row may name its values otherwise (an oscillator's one "Input").
		const deluge::plugin::SettingContext unplugged{.audio_inputs_present = false};
		merge_names(out,
		            spelled(deluge::l10n::setting_names(deluge::plugin::kDelugeInstrument, target.path, unplugged)));
	}
	return out;
}

/// @brief The names @p target's rows give their values. A source-indexed target names one row per output, and an
///        output may offer values the others do not (gate output 3's CLOCK), so it gives each output's, each value
///        once, in list order (`merge_names`).
std::vector<std::string> target_option_names(const deluge::ui::scenes::bind::Target& target) {
	if (!target.source_indexed) {
		return row_option_names(target);
	}
	std::vector<std::string> out;
	deluge::ui::scenes::bind::Addresses where{};
	for (uint8_t index = 0;; ++index) {
		where.source_index = index;
		const std::optional<deluge::ui::scenes::bind::Target> named = deluge::ui::scenes::bind::concrete(target, where);
		if (!named) {
			return out;
		}
		merge_names(out, row_option_names(*named));
	}
}

/// The source indices a names hook is asked under: every output the CV and gate selectors pick (`NUM_GATE_CHANNELS`
/// gate outputs, which covers the two CV outputs and the two oscillators).
constexpr int8_t kDumpedSourceIndices = NUM_GATE_CHANNELS;

/// @brief The contexts a names hook is asked in: an otherwise empty one per source index, which gives the values every
///        owner is offered and those one output adds (a gate output's CLOCK), then the booted song's first instrument
///        clip as its row editor sees it, a sound and a note row, which gives those an owner adds (a sound's USE SONG
///        SETTINGS, a note row's NONE). The booted song is the app's default one, so the dump stays the same from run
///        to run.
std::vector<menu::MenuContext> g_names_contexts;

void build_names_contexts() {
	g_names_contexts.clear();
	for (int8_t source = 0; source < kDumpedSourceIndices; ++source) {
		menu::MenuContext ctx{};
		ctx.source_index = source;
		g_names_contexts.push_back(ctx);
	}
	if (currentSong == nullptr) {
		return;
	}
	for (Clip* clip : currentSong->live.clips.session_clips) {
		if (clip == nullptr || clip->type != ClipType::INSTRUMENT) {
			continue;
		}
		// The song the app plays, which the app's ports act on.
		menu::MenuContext ctx = menu::context_for(currentSong->project, &deluge::gui::shell_host::host_ports(),
		                                          deluge::gui::menuViewOf(clip), 0, false, false, false,
		                                          /*kit_global_fx=*/false);
		ctx.root = menu::MenuContext::Root::NoteRowEditor;
		g_names_contexts.push_back(ctx);
		return;
	}
}

/// @brief The values @p node offers, as its list spells them; empty for a node that offers none.
///
/// A target's come from its row's name table, all of them (in a context the menu may leave some out); static `names`
/// are the list itself; a `dynamic_names` hook is asked in each of `g_names_contexts`, so it gives the values it offers
/// every owner, those one output adds (the clock output's CLOCK mode) and those the booted song's clip adds (a note
/// row's NONE direction), each once, in list order. A toggle's two values are Disabled and Enabled, the words its own
/// page lists (`MenuView::render_leaf_toggle`). A custom page or a number spelled as words names
/// its values the same two ways (`with_value_names`); one that names none offers nothing here.
std::vector<std::string> option_names(const menu::Node& node) {
	switch (node.kind) {
	case menu::Kind::Toggle:
		return {text_of(nodes::kToggleWords[0]), text_of(nodes::kToggleWords[1])};
	case menu::Kind::Submenu:
		return {};
	default:
		break;
	}
	if (node.bind.target.valid()) {
		return target_option_names(node.bind.target);
	}
	std::vector<std::string> out;
	if (node.bind.dynamic_names != nullptr) {
		// Asked in every context the dump has (`g_names_contexts`), so a page that adds a value for one output or one
		// owner lists it; each value once, in list order (`merge_names`).
		for (const menu::MenuContext& ctx : g_names_contexts) {
			std::array<std::string_view, menu::kMaxDynamicNames> names{};
			const size_t count = std::min(node.bind.dynamic_names(ctx, names), names.size());
			merge_names(out, std::vector<std::string>(names.begin(), names.begin() + static_cast<ptrdiff_t>(count)));
		}
		return out;
	}
	for (const uint16_t id : node.bind.names) {
		out.push_back(text_of(id));
	}
	return out;
}

/// @brief A row a page draws in its own list, which no `Node` stands for: its label, the values it offers and the rows
///        under it.
struct DrawnRow {
	std::string name;
	std::vector<std::string> options;
	std::vector<DrawnRow> rows;
};

namespace dx = deluge::ui::scenes::menu::dx;

/// @brief The params a DX7 row lists, by the param its select press opens, as main's website reference lists them
///        (`extractDxParamDescriptorContext` and `selectDescriptorIndicesForItem`,
///        `origin/main:website/scripts/generate-menu-hierarchies.mjs`): the descriptor group the param is in, narrowed
///        to the one param the row's label names (level, velocity sens, rate scale, ampmod), or for the row that names
///        none of its group, the params the other rows do not name (pitch: mode, coarse, fine, detune).
/// @param index The param's index in its operator, or past the operators for a global row.
/// @param global Whether it is a global row's.
std::vector<int16_t> dx_row_params(int16_t index, bool global) {
	const auto span = [](int16_t first, int16_t last) {
		std::vector<int16_t> out;
		for (int16_t i = first; i <= last; ++i) {
			out.push_back(i);
		}
		return out;
	};
	if (global) {
		return index < 8 ? span(0, 7) : index < 11 ? span(index, index) : span(11, 17);
	}
	if (index < 8) {
		return span(0, 7); // envelope
	}
	if (index < 13) {
		return span(8, 12); // note scaling
	}
	if (index == 18) {
		return span(17, 20); // pitch
	}
	return span(index, index); // level, velocity sense, rate scaling, ampmod
}

/// @brief A DX7 param as a row: its long name, and the values it offers by name (the curves, ratio or fixed, the LFO
///        waveforms; `DxParam::renderOLED`'s words).
DrawnRow dx_param_row(int16_t index, bool global) {
	DrawnRow row{.name = std::string{global ? dx::global_param_name(index) : dx::operator_param_name(index)}};
	if (!global && (index == 11 || index == 12)) {
		for (uint8_t curve = 0; curve < 4; ++curve) {
			row.options.emplace_back(dx::curve_name(curve));
		}
	}
	else if (!global && index == 17) {
		row.options = {"ratio", "fixed"};
	}
	else if (global && index == 16) {
		for (uint8_t shape = 0; shape < 6; ++shape) {
			row.options.emplace_back(dx::lfo_shape(shape));
		}
	}
	return row;
}

/// @brief The rows "DX7 parameters" draws (`dx::global_row`), each with the params it reaches: an operator's own rows
///        (`dx::operator_row`) for "operator N", the params for the rest.
std::vector<DrawnRow> dx_global_rows() {
	std::vector<DrawnRow> out;
	for (int32_t r = 0; r < dx::kGlobalRows; ++r) {
		DrawnRow row{.name = std::string{dx::global_row(r)}};
		const int16_t param = dx::global_row_param(r);
		if (param < 0) {
			for (int32_t o = 0; o < dx::kOperatorRows; ++o) {
				DrawnRow op_row{.name = std::string{dx::operator_row(o)}};
				const auto index = static_cast<int16_t>(dx::operator_row_param(0, o));
				for (const int16_t p : dx_row_params(index, false)) {
					op_row.rows.push_back(dx_param_row(p, false));
				}
				row.rows.push_back(std::move(op_row));
			}
		}
		else {
			for (const int16_t p : dx_row_params(static_cast<int16_t>(param - dx::kFirstGlobal), true)) {
				row.rows.push_back(dx_param_row(p, true));
			}
		}
		out.push_back(std::move(row));
	}
	return out;
}

/// @brief The rows @p node draws in its own list, which the dump lists under `"rows"`: only "DX7 parameters" has
///        any, which `DxGlobalParamsItem` draws from the DX7 tables.
std::vector<DrawnRow> drawn_rows(const menu::Node& node) {
	if (&node == &nodes::dx_global_params) {
		return dx_global_rows();
	}
	return {};
}

void emit_names(std::string& out, const std::vector<std::string>& names, int field_level) {
	out += "[\n";
	for (size_t i = 0; i < names.size(); ++i) {
		out += spaces(field_level + 1);
		write_json_string(out, names[i]);
		out += i + 1 < names.size() ? ",\n" : "\n";
	}
	out += spaces(field_level) + "]";
}

/// @brief @p rows as a `"rows"` array at @p field_level: each row's `name`, then its `options` and `rows` where it has
///        any, keys in the file's alphabetical order.
void emit_drawn_rows(std::string& out, const std::vector<DrawnRow>& rows, int field_level) {
	out += "[\n";
	for (size_t i = 0; i < rows.size(); ++i) {
		const DrawnRow& row = rows[i];
		const int row_level = field_level + 1;
		out += spaces(row_level) + "{\n";
		out += spaces(row_level + 1) + "\"name\": ";
		write_json_string(out, row.name);
		if (!row.options.empty()) {
			out += ",\n" + spaces(row_level + 1) + "\"options\": ";
			emit_names(out, row.options, row_level + 1);
		}
		if (!row.rows.empty()) {
			out += ",\n" + spaces(row_level + 1) + "\"rows\": ";
			emit_drawn_rows(out, row.rows, row_level + 1);
		}
		out += "\n" + spaces(row_level) + "}";
		out += i + 1 < rows.size() ? ",\n" : "\n";
	}
	out += spaces(field_level) + "]";
}

/// @brief @p node's `"options"` key, one name per line, or nothing for a node that offers no values.
void emit_options(std::string& out, const menu::Node& node, int field_level) {
	const std::vector<std::string> options = option_names(node);
	if (options.empty()) {
		return;
	}
	out += spaces(field_level) + "\"options\": [\n";
	for (size_t i = 0; i < options.size(); ++i) {
		out += spaces(field_level + 1);
		write_json_string(out, options[i]);
		out += i + 1 < options.size() ? ",\n" : "\n";
	}
	out += spaces(field_level) + "],\n";
}

/// @brief The nodes whose `emit_node` call is still open, innermost last.
///
/// Only `emit_chain_targets` consults it -- a `children` span cannot loop (a `Node`'s children are
/// fixed at its own definition, so a cycle would need a node to contain its own address) but a
/// `chain_targets` span can, since two leaves may name each other. See `fail_on_chain_cycle`.
std::vector<const menu::Node*> g_path;

void emit_node(std::string& out, const menu::Node& node, int brace_level);

/// @brief `node`'s static `children` span, as a JSON array at `elem_level` (each element's own
///        indent; the closing `]` sits one level up, aligned with the `"children"` key). Skips a
///        null child pointer (defensive -- `node_id.cpp`'s own walk does the same).
void emit_static_children(std::string& out, const menu::Node& node, int elem_level) {
	bool any = false;
	for (const menu::Node* c : node.children) {
		if (c != nullptr) {
			any = true;
			break;
		}
	}
	if (!any) {
		out += "[]";
		return;
	}
	out += "[\n";
	bool first = true;
	for (const menu::Node* c : node.children) {
		if (c == nullptr) {
			continue;
		}
		if (!first) {
			out += ",\n";
		}
		first = false;
		out += spaces(elem_level);
		emit_node(out, *c, elem_level);
	}
	out += "\n" + spaces(elem_level - 1) + "]";
}

/// @brief A chain target that is already on the walk's own path, as a `"ref"` stub.
///
/// A cycle is fine as *data* but has no finite nested-JSON spelling, so the walk stops at a node already on its own
/// path and writes a stub instead. The case is main's: `patch_cable_strength::rangeMenu`'s select press opens
/// `source_selection::rangeMenu` (`PatchCableStrength::selectButtonPress` in
/// `origin/main:src/deluge/gui/menu_item/patch_cable_strength.cpp`) and that page's select press opens the strength
/// page straight back (`Range::selectButtonPress` in
/// `origin/main:src/deluge/gui/menu_item/source_selection/range.cpp`). A two-hop loop, on purpose -- it is how the user
/// toggles between "which source modulates this cable's depth" and "by how much".
///
/// The stub carries the same `"id"` the full entry does, so a consumer resolves the loop by looking
/// that id up in an entry it has already seen on this path; `"ref": true` is the only key that
/// distinguishes it, and its absence everywhere else means an existing consumer sees a normal (if
/// childless) entry rather than a schema it cannot parse. Keys stay in the file's fixed
/// alphabetical order: `id`, `name`, `ref`, `title`, `type`.
///
/// `assign_node_ids` and `validate.cpp`'s `collect` need no equivalent -- each already stops at a
/// node it has seen.
void emit_chain_ref(std::string& out, const menu::Node& node, int brace_level) {
	const int field_level = brace_level + 1;
	out += "{\n";
	out += spaces(field_level) + "\"id\": " + std::to_string(menu::node_id(node)) + ",\n";
	out += spaces(field_level) + "\"name\": ";
	write_json_string(out, text_of(node.name));
	out += ",\n";
	out += spaces(field_level) + "\"ref\": true,\n";
	out += spaces(field_level) + "\"title\": ";
	write_json_string(out, text_of(node.title));
	out += ",\n";
	out += spaces(field_level) + "\"type\": ";
	write_json_string(out, node.kind == menu::Kind::Submenu ? "submenu" : "basic");
	out += "\n";
	out += spaces(brace_level) + "}";
}

/// @brief `node`'s `chain_targets` span, in the same array shape `emit_static_children` writes.
///
/// These are the pages a select press on `node` opens -- main's `MenuItem::selectButtonPress` chaining.
/// Seven nodes in the tree have them, and for four of those (`arpOctaveModeToNoteModeMenu` and
/// relatives) plus three under settings (`cvSubmenu`, `gateModeMenu`, `gateOffTimeMenu`) this is
/// the *only* place they appear in the dump: no submenu literal names them, in main or here
/// (`node.h`'s `chain_targets` note).
void emit_chain_targets(std::string& out, const menu::Node& node, int elem_level) {
	out += "[\n";
	bool first = true;
	for (const menu::Node* t : node.chain_targets) {
		if (t == nullptr) {
			continue;
		}
		if (!first) {
			out += ",\n";
		}
		first = false;
		out += spaces(elem_level);
		if (std::find(g_path.begin(), g_path.end(), t) != g_path.end()) {
			emit_chain_ref(out, *t, elem_level); // a cycle -- see `emit_chain_ref`
		}
		else {
			emit_node(out, *t, elem_level);
		}
	}
	out += "\n" + spaces(elem_level - 1) + "]";
}

/// @brief Fail closed on a `Node::dynamic_children` node. There is no verified JSON shape for this dump to fall back
///        to (`origin/main:src/deluge/gui/menu_item/generate/dmui/visitor/doc_emitter.py`'s own `"multicontext"` entry
///        has no `"children"` key at all -- its Python DSL pre-resolved the context upstream, which this dump cannot
///        do), so rather than guess one, this prints the offending node and stops the whole dump.
///
/// @note No node sets `dynamic_children`, and none can. The candidates are the MIDI device list and the CV and gate
///       output selectors (`origin/main:src/deluge/gui/menu_item/midi/devices.cpp`,
///       `origin/main:src/deluge/gui/menu_item/cv/selection.h`,
///       `origin/main:src/deluge/gui/menu_item/gate/selection.h`). A submenu's rows are labelled by `display_text`
///       (`MenuView::render_vertical`; `src/ui/scenes/menu/present.cpp`) -- a `Binding::name_text` hook with no row
///       index, or the `Node::name` l10n id -- and `Devices` labels every row with `MIDICable::getDisplayName()`
///       (`Devices::drawPixelsForOled` in `origin/main:src/deluge/gui/menu_item/midi/devices.cpp`), a runtime string
///       with no id at all -- while the two selectors' option lists are *static* in main (`getOptions` in
///       `origin/main:src/deluge/gui/menu_item/cv/selection.h` and in
///       `origin/main:src/deluge/gui/menu_item/gate/selection.h`) over the compile-time `NUM_PHYSICAL_CV_CHANNELS` /
///       `NUM_GATE_CHANNELS`. All three are `CustomItem`s that draw their own list (`ui/scenes/menu/nodes/dynamic.h`),
///       as main's do, so this arm has nothing to be designed against.
///       `tests/integration/ui/scenes/tables_3_spec.cpp`'s "is no placeholder" example pins that zero, so the day a
///       node does set the hook, that example fails and points here.
///
///       A `"type": "submenu"` entry with `"dynamic": true` and the children the hook yields under a fixture context
///       is not viable: the dump builds no `MenuContext` to expand against, and building one would make the committed
///       JSON a function of whatever `MIDIDeviceManager`'s globals happen to hold in the dump process -- exactly the
///       determinism the `ui-menu-dump` gate compares for. A static "every node this hook may yield" span on `Node`,
///       in the shape `chain_targets` has for `select_chain` and for the same three reasons (`assign_node_ids`
///       numbers it, `validate_tree` checks it, this dump prints it), is the design that would survive -- but it is a
///       `node.h` change with no user, so it stays a note rather than dormant code.
///
/// Never returns. Nothing has been written to stdout yet regardless of how much of `out` was
/// built so far (`main` only writes it out once, at the very end), so this cannot leave a
/// truncated or malformed JSON document on stdout -- it leaves nothing at all.
[[noreturn]] void fail_on_dynamic_children(const menu::Node& node) {
	std::fprintf(stderr,
	             "menu dump: node \"%s\" (id %u) sets dynamic_children -- this dump has no\n"
	             "menu dump: verified JSON shape for it (see menu_dump.cpp's file header).\n"
	             "menu dump: Design this dump's multicontext handling against\n"
	             "menu dump: this node's real use case before re-adding it; do not resurrect the\n"
	             "menu dump: removed guess.\n",
	             text_of(node.name).c_str(), menu::node_id(node));
	std::exit(1);
}

/// @brief One node, as the schema the file header documents -- object keys in fixed alphabetical
///        order (`available_when`, `children`, `id`, `name`, `options`, `select_chain`, `title`, `type`),
///        regardless of which optional fields this particular node has.
void emit_node(std::string& out, const menu::Node& node, int brace_level) {
	if (node.dynamic_children != nullptr) {
		fail_on_dynamic_children(node); // noreturn
	}

	const int field_level = brace_level + 1;
	const bool is_submenu_type = node.kind == menu::Kind::Submenu;
	const char* type_str = is_submenu_type ? "submenu" : "basic";

	bool any_chain = false;
	for (const menu::Node* t : node.chain_targets) {
		if (t != nullptr) {
			any_chain = true;
			break;
		}
	}

	g_path.push_back(&node);

	out += "{\n";

	if (node.relevant != nullptr) {
		out += spaces(field_level) + "\"available_when\": ";
		write_json_string(out, relevance::name_of(node.relevant));
		out += ",\n";
	}
	// A target node is shown when its setting's conditions hold: the dump names the setting whose descriptor decides.
	if (target_is_conditional(node.bind.target)) {
		out += spaces(field_level) + "\"available_when\": ";
		write_json_string(out, std::string("setting:") + std::string(node.bind.target.path));
		out += ",\n";
	}
	if (is_submenu_type) {
		out += spaces(field_level) + "\"children\": ";
		emit_static_children(out, node, field_level + 1);
		out += ",\n";
	}
	out += spaces(field_level) + "\"id\": " + std::to_string(menu::node_id(node)) + ",\n";
	out += spaces(field_level) + "\"name\": ";
	write_json_string(out, text_of(node.name));
	out += ",\n";
	emit_options(out, node, field_level);
	if (const std::vector<DrawnRow> rows = drawn_rows(node); !rows.empty()) {
		out += spaces(field_level) + "\"rows\": ";
		emit_drawn_rows(out, rows, field_level);
		out += ",\n";
	}
	// A node with no chain gets no `"select_chain"` key at all, the same way a node with no
	// `relevant` gets no `"available_when"`: absent means "a select press here goes up a level",
	// which is every node but seven.
	if (any_chain) {
		out += spaces(field_level) + "\"select_chain\": ";
		emit_chain_targets(out, node, field_level + 1);
		out += ",\n";
	}
	out += spaces(field_level) + "\"title\": ";
	write_json_string(out, text_of(node.title));
	out += ",\n";
	out += spaces(field_level) + "\"type\": ";
	write_json_string(out, type_str);
	out += "\n";

	out += spaces(brace_level) + "}";

	g_path.pop_back();
}

} // namespace

bool deluge::ui::scenes::menu::dump_menu_tree(std::string& out) {
	// Load-bearing: `text_of` below
	// resolves every "name"/"title" through `deluge_profile().text_or_digits`, which reads
	// `l10n::chosenLanguage` -- normally set once at boot by the real app (`OLED()`'s
	// constructor). Nothing else does that here, so without this line `chosenLanguage` is null
	// and the tool segfaults on the first node. Pinned to English rather than read from the
	// environment so a run's language can never silently vary.
	deluge::l10n::chosenLanguage = &deluge::l10n::built_in::english;
	build_names_contexts();

	uint16_t assigned = menu::assign_node_ids(nodes::kAllRoots);
	if (assigned == 0) {
		std::fprintf(stderr, "menu dump: assign_node_ids assigned zero nodes over kAllRoots\n");
		return false;
	}
	if (assigned >= menu::kMaxNodes) {
		std::fprintf(stderr, "menu dump: node id registry likely overflowed (%u >= kMaxNodes)\n", assigned);
		return false;
	}

	out.clear();
	out += "[\n";
	bool first = true;
	for (const menu::Node* root : nodes::kAllRoots) {
		// A group pages its member pages as one; no menu lists it, and its items are already under the roots above.
		if (root == nullptr || root->is_group) {
			continue;
		}
		if (!first) {
			out += ",\n";
		}
		first = false;
		out += spaces(1);
		emit_node(out, *root, 1);
	}
	out += "\n]\n";
	return true;
}
