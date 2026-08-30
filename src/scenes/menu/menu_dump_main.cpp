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
///   * a node with `Node::dynamic_children` set -> `"multicontext"`, expanded under each of the ten
///     `MenuContext`s `build_contexts()` builds (Task 8's fixtures). The brief does not specify the
///     wire shape for this expansion and no node in the tree sets `dynamic_children` today (a
///     tree-wide grep confirms it), so this is this file's own extension, exercised by nothing:
///     `"children"` becomes an array of `{"context": <Root enumerator name>, "nodes": [...]}`, one
///     entry per context, each `"nodes"` the (possibly ctx-dependent) result of calling
///     `dynamic_children(ctx, out)` for that context. If a future table ever sets this hook, this
///     is the shape its dump entries will take; revisit this note and the CI-committed JSON
///     together when that happens.
///   * `"id"` = `node_id(n)` on every entry (ruling Q7) -- the lookup key a future `.state` golden
///     (`app16/<id>`) resolves against. A node reached only through `dynamic_children` never gets
///     an id (`node_id.h`'s own contract: only *static* children are walked), so such an entry
///     prints `"id": 0` -- 0 is reserved for "no node" everywhere else in this registry too.
///   * `"name"`/`"title"` are resolved display text (`deluge_profile().text_or_digits`), not the
///     l10n symbolic key the retired schema carried: `Node::name`/`Node::title` are plain
///     `uint16_t` ids with no reverse mapping back to a `STRING_FOR_...` spelling, and resolved
///     text is what a JSON consumer -- or a human spot-checking this dump against
///     `website/src/content/docs/reference/menu_hierarchies.mdx` -- actually wants.
///   * `"description"` is dropped entirely. The retired schema's `description` was a whole
///     markdown file's contents, read from `docs/menus/`; `Node` (`node.h`) carries nothing like
///     it, so there is nothing to put there. (E2's own per-item docs, if they ever exist, would be
///     the thing to wire up here.)
///
/// ## The walk
///
/// One JSON array, one entry per `nodes::kAllRoots` root (11: the ten `MenuContext::Root`
/// enumerators' own roots, in enumerator order, plus `root_cv` -- the MIDI/CV root's CV-titled
/// twin, `nodes/roots.h`'s note), each walked over its own *static* `Node::children` -- the same
/// walk `node_id.h`'s `assign_node_ids` does over the same root list, so every `"id"` printed here
/// is one that call actually assigned. Sharing is preserved, not deduplicated: a node reachable
/// from more than one parent (e.g. `song_master` under both the song and audio-clip roots, or
/// `randomizer` under several) appears once per parent, each occurrence carrying the same `"id"`.
///
/// ## Determinism
///
/// Root order is `kAllRoots`'s own compile-time order; child order is `Node::children`'s own
/// declaration order (a `std::span` over a `constexpr` array, never reordered); the ten contexts
/// are visited in `MenuContext::Root` enumerator order. JSON object keys are written in a fixed
/// alphabetical order (`available_when`, `children`, `id`, `name`, `title`, `type`) baked into
/// `emit_node`'s own field-by-field writes rather than sorted at runtime -- there is no
/// hash-map/set iteration anywhere on the emission path, static or dynamic. Two-space indentation,
/// one trailing newline at end of file. `scripts/menu_dump_check.sh` rebuilds this tool, runs it,
/// and diffs the result byte-for-byte against the committed
/// `website/src/data/generated/menu-hierarchies.json`.
#include "scenes/menu/context.h"
#include "scenes/menu/menu_dump_contexts.h" // NamedContext, build_contexts() -- see that header's note
#include "scenes/menu/node.h"
#include "scenes/menu/node_id.h"
#include "scenes/menu/nodes/nodes.h"
#include "scenes/menu/relevance.h"
#include "scenes/profile.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace menu = deluge::scenes::menu;
namespace nodes = deluge::scenes::menu::nodes;
namespace relevance = deluge::scenes::menu::relevance;
using deluge::scenes::menu_dump::NamedContext;

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

void emit_node(std::string& out, const menu::Node& node, int brace_level, const std::vector<NamedContext>& contexts);

/// @brief `node`'s static `children` span, as a JSON array at `elem_level` (each element's own
///        indent; the closing `]` sits one level up, aligned with the `"children"` key). Skips a
///        null child pointer (defensive -- `node_id.cpp`'s own walk does the same).
void emit_static_children(std::string& out, const menu::Node& node, int elem_level,
                          const std::vector<NamedContext>& contexts) {
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
		emit_node(out, *c, elem_level, contexts);
	}
	out += "\n" + spaces(elem_level - 1) + "]";
}

/// @brief `node.dynamic_children`, expanded under every entry of `contexts` -- this file's own
///        extension for the `"multicontext"` case (see the file header's note; nothing in the
///        tree exercises this today). One `{"context", "nodes"}` object per context, at
///        `elem_level`; each context's `"nodes"` is that context's own
///        `dynamic_children(ctx, out)` result, in the order the hook itself returned it.
void emit_multicontext_children(std::string& out, const menu::Node& node, int elem_level,
                                const std::vector<NamedContext>& contexts) {
	if (contexts.empty()) {
		out += "[]";
		return;
	}
	out += "[\n";
	bool first = true;
	for (const NamedContext& nc : contexts) {
		if (!first) {
			out += ",\n";
		}
		first = false;
		out += spaces(elem_level) + "{\n";
		int inner = elem_level + 1;
		out += spaces(inner) + "\"context\": ";
		write_json_string(out, nc.name);
		out += ",\n";
		out += spaces(inner) + "\"nodes\": ";

		std::array<const menu::Node*, menu::kMaxDynamicChildren> buf{};
		size_t n = node.dynamic_children(nc.ctx, buf);
		if (n == 0) {
			out += "[]";
		}
		else {
			out += "[\n";
			for (size_t i = 0; i < n; i++) {
				if (i > 0) {
					out += ",\n";
				}
				out += spaces(inner + 1);
				if (buf[i] != nullptr) {
					emit_node(out, *buf[i], inner + 1, contexts);
				}
				else {
					out += "null";
				}
			}
			out += "\n" + spaces(inner) + "]";
		}
		out += "\n" + spaces(elem_level) + "}";
	}
	out += "\n" + spaces(elem_level - 1) + "]";
}

/// @brief One node, as the schema the file header documents -- object keys in fixed alphabetical
///        order (`available_when`, `children`, `id`, `name`, `title`, `type`), regardless of which
///        optional fields this particular node has.
void emit_node(std::string& out, const menu::Node& node, int brace_level, const std::vector<NamedContext>& contexts) {
	const int field_level = brace_level + 1;
	const bool is_submenu_type = node.kind == menu::Kind::Submenu;
	const bool is_multicontext = node.dynamic_children != nullptr;
	const char* type_str = is_multicontext ? "multicontext" : (is_submenu_type ? "submenu" : "basic");

	out += "{\n";

	if (node.relevant != nullptr) {
		out += spaces(field_level) + "\"available_when\": ";
		write_json_string(out, relevance::name_of(node.relevant));
		out += ",\n";
	}
	if (is_submenu_type) {
		out += spaces(field_level) + "\"children\": ";
		if (is_multicontext) {
			emit_multicontext_children(out, node, field_level + 1, contexts);
		}
		else {
			emit_static_children(out, node, field_level + 1, contexts);
		}
		out += ",\n";
	}
	out += spaces(field_level) + "\"id\": " + std::to_string(menu::node_id(node)) + ",\n";
	out += spaces(field_level) + "\"name\": ";
	write_json_string(out, text_of(node.name));
	out += ",\n";
	out += spaces(field_level) + "\"title\": ";
	write_json_string(out, text_of(node.title));
	out += ",\n";
	out += spaces(field_level) + "\"type\": ";
	write_json_string(out, type_str);
	out += "\n";

	out += spaces(brace_level) + "}";
}

} // namespace

int main() {
	std::vector<NamedContext> contexts = deluge::scenes::menu_dump::build_contexts();

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
		emit_node(out, *root, 1, contexts);
	}
	out += "\n]\n";

	size_t written = std::fwrite(out.data(), 1, out.size(), stdout);
	if (written != out.size()) {
		std::fprintf(stderr, "deluge_menu_dump: short write to stdout (%zu of %zu bytes)\n", written, out.size());
		return 1;
	}
	return 0;
}
