/// @file
/// @brief `deluge_menu_dump` -- walks every root of the declarative menu tree (`nodes::kAllRoots`,
///        `scenes/menu/nodes/nodes.h`) and prints it as JSON, in the schema the retired Python
///        DocEmitter used to emit (task-10-brief.md).
///
/// ## The recovered schema
///
/// `src/deluge/gui/menu_item/generate/dmui/visitor/doc_emitter.py` was deleted at `e23db5f87`
/// ("chore(menus): retire the Python menu generator") when Task 6 replaced the six subtrees it
/// generated with `constexpr Node` tables. The brief cites it for this dump's output shape but
/// cannot read a deleted file, so it was recovered from git history
/// (`git log --oneline --diff-filter=D -- '*doc_emitter.py'` -> `e23db5f87`;
/// `git show e23db5f87^:src/deluge/gui/menu_item/generate/dmui/visitor/doc_emitter.py`). Its
/// `DocEmitter` visitor produced, per menu (`main.py`'s `top_level_menus`):
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
/// `main.py` wrote `json.dumps([menu.visit(emitter) for menu in top_level_menus])`: a plain JSON
/// array, no `sort_keys`, no indent. `title`/`name` were the menu's l10n *key* string (e.g.
/// `"STRING_FOR_LPF_FREQUENCY"`, `dsl.py`'s `Menu.__init__` docstring), and `description` was read
/// verbatim from a `docs/menus/*.md` file the menu named (`read_description`).
///
/// ## Why this dump differs
///
/// `Kind` (`node.h`) has no `MultiMode`/`MultiModeMode` counterpart: five `Kind` enumerators were
/// dropped for having zero users once all eleven roots were declared (`node.h`'s own note), and
/// `MultiModeMenu` was one of the DSL concepts that did not survive the port to plain `Node` data
/// at all. So `"multimode"`/`"multimode-mode"` never appear in this dump's actual output -- they
/// are listed above only so the schema this file approximates is documented in full, as the brief
/// asks. What this dump actually maps, per the brief:
///
///   * `Kind::Submenu` -> `"submenu"`; every other `Kind` (a leaf, including `Kind::Custom`) ->
///     `"basic"`.
///   * a node with `Node::relevant` set -> `"available_when"` = `relevance::name_of(fn)` (Task 6's
///     registry) -- the *name* of the guarding predicate, not its result in any one context. A
///     node with no `relevant` gets no `"available_when"` key at all (`relevance::name_of(nullptr)`
///     would answer `"always"`, but the brief's mapping is keyed on "a node with `relevant`", so a
///     null one is left out rather than spelled out).
///   * a node with `Node::dynamic_children` set -> `"multicontext"` -- but this dump does not
///     attempt to expand it. **Fix round 1** caught the first cut of this file inventing a
///     `"children"` shape (an array of `{"context", "nodes"}` objects) for that case, and that
///     invention was wrong on its own terms: the recovered `doc_emitter.py`'s `"multicontext"`
///     entry (`visit_multicontext_instance`, above) has **no `"children"` key at all** -- the
///     Python DSL pre-resolved every `.with_context()` instance to one context *before* the
///     visitor ever ran, so the visitor itself never had more than one context's worth of data to
///     print. This dump has no equivalent pre-resolution step (there is no single "the" context a
///     `Node` tree is dumped under -- that is the whole reason `dynamic_children` takes a
///     `MenuContext` argument), so there is no verified shape to fall back to either. No node in
///     the tree sets `dynamic_children` today (a tree-wide grep for `dynamic_submenu(` confirms
///     it), so rather than keep a dormant, never-exercised guess at a JSON shape, `emit_node`
///     fails loudly instead: it prints the offending node's resolved name and `node_id` to stderr
///     and exits non-zero. That failure is the deliberate design point where E2 -- whoever adds
///     the first real `dynamic_children` node -- has to stop and design this dump's shape against
///     an actual case, rather than silently inheriting whatever this file guessed in the
///     abstract. **Fix round 2** removed the last trace of the abandoned attempt: this file used
///     to build ten `MenuContext`s (Task 8's fixtures, via a real `Song`/`Clip`/`Instrument`) so
///     the (now-deleted) expansion code would have something to call `dynamic_children(ctx, ...)`
///     with. Nothing else here ever read a `MenuContext` -- confirmed by testing, not just
///     reasoning: dropping the whole fixture-building call and keeping only the one line it
///     happened to also do (`l10n::chosenLanguage = &l10n::built_in::english;`, load-bearing for
///     `text_of()` below -- the tool segfaults without it) reproduced this dump's output
///     byte-for-byte. So the fixture builder (`sim/scenes_spec/menu_dump_contexts.{h,cpp}`, and
///     the `scenes-includes-outside` layer-rule workaround that existed only to let it reach
///     `sim/scenes_spec/support/song_fixture.h` from outside `src/scenes/`) is gone; this dump
///     never touches a `MenuContext` at all now, only `Node`'s own static fields.
///   * a node with `Node::chain_targets` set -> a `"select_chain"` array of nested entries, in the
///     same shape as `"children"`: the pages a select press on that node opens instead of going up
///     a level (legacy's `MenuItem::selectButtonPress`). Seven nodes have one, and seven pages
///     appear in this dump **only** under a `"select_chain"` -- `arpOctaveModeToNoteModeMenu` and
///     its three relatives (`menus.cpp:511-521`) and `cvSubmenu` / `gateModeMenu` /
///     `gateOffTimeMenu` (`:1223-1232`) are named by no submenu literal in legacy or in the port.
///     A node with no chain gets no key at all, as with `available_when`. A chain that looped
///     would have no finite nested spelling, so `emit_chain_targets` fails loudly on one rather
///     than guessing a shape (`fail_on_chain_cycle`; nothing in the tree loops today).
///   * `"id"` = `node_id(n)` on every entry (ruling Q7) -- the lookup key a future `.state` golden
///     (`app16/<id>`) resolves against. A node reached only through `dynamic_children` never gets
///     an id (`node_id.h`'s own contract: `children` and `chain_targets` are walked, a dynamic
///     hook's produce is not), so such an entry prints `"id": 0` -- 0 is reserved for "no node"
///     everywhere else in this registry too.
///   * `"name"`/`"title"` are resolved display text (`deluge_profile().text_or_digits`), not the
///     l10n symbolic key the retired schema carried: `Node::name`/`Node::title` are plain
///     `uint16_t` ids with no reverse mapping back to a `STRING_FOR_...` spelling, and resolved
///     text is what a JSON consumer -- or a human spot-checking this dump against
///     `website/src/content/docs/reference/menu_hierarchies.mdx` -- actually wants.
///   * `"description"` is dropped entirely -- structurally absent from every emitted entry, not
///     present-but-empty. The retired schema's `description` was a whole markdown file's
///     contents, read from `docs/menus/`; `Node` (`node.h`) carries nothing like it, so there is
///     nothing to put there. (E2's own per-item docs, if they ever exist, would be the thing to
///     wire up here.) **Fix round 1:** this matters to more than this comment -- anything written
///     against `doc_emitter.py`'s shape (expecting `entry["description"]` to exist, even as `""`)
///     will `KeyError`/throw on this dump's output instead. That warning is repeated where a
///     consumer is more likely to actually read it:
///     `website/src/data/generated/README.md` (next to the JSON itself) and
///     `scripts/menu_dump_check.sh`'s own header comment.
///
/// ## The walk
///
/// One JSON array, one entry per `nodes::kAllRoots` root (11: the ten `MenuContext::Root`
/// enumerators' own roots, in enumerator order, plus `root_cv` -- the MIDI/CV root's CV-titled
/// twin, `nodes/roots.h`'s note), each walked over its own static `Node::children` **and**
/// `Node::chain_targets` -- the same two edge kinds `node_id.h`'s `assign_node_ids` walks over the
/// same root list, so every `"id"` printed here is one that call actually assigned. Sharing is
/// preserved, not deduplicated: a node reachable from more than one parent (e.g. `song_master`
/// under both the song and audio-clip roots, or `randomizer` under several) appears once per
/// parent, each occurrence carrying the same `"id"`.
///
/// ## Determinism
///
/// Root order is `kAllRoots`'s own compile-time order; child order is `Node::children`'s own
/// declaration order (a `std::span` over a `constexpr` array, never reordered). JSON object keys
/// are written in a fixed alphabetical order (`available_when`, `children`, `id`, `name`,
/// `select_chain`, `title`, `type`) baked into `emit_node`'s own field-by-field writes rather than
/// sorted at runtime --
/// there is no hash-map/set iteration anywhere on the emission path. `text_of` resolves through
/// `l10n::chosenLanguage`, pinned once in `main` to the built-in English table before the walk
/// starts, so every run resolves the same language regardless of process environment. Two-space
/// indentation, one trailing newline at end of file. `scripts/menu_dump_check.sh` rebuilds this
/// tool, runs it, and diffs the result byte-for-byte against the committed
/// `website/src/data/generated/menu-hierarchies.json`.
#include "gui/l10n/l10n.h"
#include "scenes/menu/node.h"
#include "scenes/menu/node_id.h"
#include "scenes/menu/nodes/nodes.h"
#include "scenes/menu/relevance.h"
#include "scenes/profile.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

namespace menu = deluge::scenes::menu;
namespace nodes = deluge::scenes::menu::nodes;
namespace relevance = deluge::scenes::menu::relevance;

namespace {

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
				// Not `std::snprintf`: the firmware's own `src/lib/printf.h` `#define`s
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
	return std::string{deluge::scenes::deluge_profile().text_or_digits(id, scratch)};
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
/// **The real case this dump was told to wait for.** Its predecessor here was `fail_on_chain_cycle`,
/// which printed the node and exited: "a cycle is fine as *data* but has no finite nested-JSON
/// spelling, and inventing one (a `$ref`-style stub, say) without a real case to design against is
/// the mistake fix round 1 caught for `dynamic_children`". E2 task 9 supplies the case, and it is
/// legacy's, not the port's: `patch_cable_strength::rangeMenu`'s select press opens
/// `source_selection::rangeMenu` (`patch_cable_strength.cpp:302-306`) and that page's select press
/// opens the strength page straight back (`source_selection/range.cpp:30-32`). A two-hop loop, on
/// purpose -- it is how the user toggles between "which source modulates this cable's depth" and
/// "by how much".
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
/// These are the pages a select press on `node` opens -- legacy's `selectButtonPress()` chaining.
/// Seven nodes in the tree have them, and for four of those (`arpOctaveModeToNoteModeMenu` and
/// relatives) plus three under settings (`cvSubmenu`, `gateModeMenu`, `gateOffTimeMenu`) this is
/// the *only* place they appear in the dump: no submenu literal names them, in legacy or in the
/// port (`node.h`'s `chain_targets` note).
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

/// @brief Fail closed on a `Node::dynamic_children` node -- see the file header's "Fix round 1"
///        note. There is no verified JSON shape for this dump to fall back to (the recovered
///        `doc_emitter.py`'s own `"multicontext"` entry has no `"children"` key at all -- its
///        Python DSL pre-resolved the context upstream, which this dump cannot do), so rather
///        than guess one, this prints the offending node and stops the whole dump.
///
/// Never returns. Nothing has been written to stdout yet regardless of how much of `out` was
/// built so far (`main` only writes it out once, at the very end), so this cannot leave a
/// truncated or malformed JSON document on stdout -- it leaves nothing at all.
[[noreturn]] void fail_on_dynamic_children(const menu::Node& node) {
	std::fprintf(stderr,
	             "deluge_menu_dump: node \"%s\" (id %u) sets dynamic_children -- this dump has no\n"
	             "deluge_menu_dump: verified JSON shape for it (see menu_dump_main.cpp's file header,\n"
	             "deluge_menu_dump: \"Fix round 1\"). Design this dump's multicontext handling against\n"
	             "deluge_menu_dump: this node's real use case before re-adding it; do not resurrect the\n"
	             "deluge_menu_dump: removed guess.\n",
	             text_of(node.name).c_str(), menu::node_id(node));
	std::exit(1);
}

/// @brief One node, as the schema the file header documents -- object keys in fixed alphabetical
///        order (`available_when`, `children`, `id`, `name`, `select_chain`, `title`, `type`),
///        regardless of which optional fields this particular node has.
void emit_node(std::string& out, const menu::Node& node, int brace_level) {
	if (node.dynamic_children != nullptr) {
		fail_on_dynamic_children(node); // noreturn -- see the file header's "Fix round 1" note
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
	if (is_submenu_type) {
		out += spaces(field_level) + "\"children\": ";
		emit_static_children(out, node, field_level + 1);
		out += ",\n";
	}
	out += spaces(field_level) + "\"id\": " + std::to_string(menu::node_id(node)) + ",\n";
	out += spaces(field_level) + "\"name\": ";
	write_json_string(out, text_of(node.name));
	out += ",\n";
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

int main() {
	// Load-bearing (fix round 2 tested this, not just reasoned about it): `text_of` below
	// resolves every "name"/"title" through `deluge_profile().text_or_digits`, which reads
	// `l10n::chosenLanguage` -- normally set once at boot by the real app (`OLED()`'s
	// constructor). Nothing else does that here, so without this line `chosenLanguage` is null
	// and the tool segfaults on the first node. Pinned to English rather than read from the
	// environment so a run's language can never silently vary.
	deluge::l10n::chosenLanguage = &deluge::l10n::built_in::english;

	uint16_t assigned = menu::assign_node_ids(nodes::kAllRoots);
	if (assigned == 0) {
		std::fprintf(stderr, "deluge_menu_dump: assign_node_ids assigned zero nodes over kAllRoots\n");
		return 1;
	}
	if (assigned >= menu::kMaxNodes) {
		std::fprintf(stderr, "deluge_menu_dump: node id registry likely overflowed (%u >= kMaxNodes)\n", assigned);
		return 1;
	}

	std::string out;
	out += "[\n";
	bool first = true;
	for (const menu::Node* root : nodes::kAllRoots) {
		if (root == nullptr) {
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

	size_t written = std::fwrite(out.data(), 1, out.size(), stdout);
	if (written != out.size()) {
		std::fprintf(stderr, "deluge_menu_dump: short write to stdout (%zu of %zu bytes)\n", written, out.size());
		return 1;
	}
	return 0;
}
