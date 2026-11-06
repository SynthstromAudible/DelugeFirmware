#pragma once

/// @brief The menu-tree validator (host test, every build).
///
/// Every rule is one `Issue::Kind` below, including the two flag audits -- a `Node` flag that a table forgot to set is
/// invisible at runtime (the item just quietly behaves like an ordinary one), so the expected sets are written down
/// here and a mismatch is an `Issue`, not a silent pass.
///
/// The validator is deliberately **model-free**: it walks `Node` data, calls `RelevantFn`s with
/// whatever `MenuContext`s the caller supplies, and consults `shortcuts.h`. That is what lets it
/// run in the host suite (`tests/spec/scenes/validate_spec.cpp`, over fake trees and fake
/// contexts) as well as against the real tables in the sim spec.

#include "scenes/menu/context.h"
#include "scenes/menu/node.h"
#include "shell/fixed_vector.h"

#include <cstdint>
#include <span>

namespace deluge::scenes::menu {

/// @brief One rule violation.
struct Issue {
	enum class Kind : uint8_t {
		/// @brief A node reachable from `roots` that `assign_node_ids` could not give an id --
		///        i.e. the tree overflowed `kMaxNodes` (`node_id.h`'s documented failure mode).
		///        `detail` is the true reachable count as far as this walk got.
		Unreachable,
		/// @brief A `Kind::Submenu` that has no visible child under **any** supplied context, and
		///        no `dynamic_children` hook that might produce one at render time.
		EmptySubmenu,
		/// @brief A `Binding::param` whose `kind`/`index` pair does not name a real parameter, or a
		///        value node with neither a param nor a `get` hook to read. `detail` is the index.
		BadParamRef,
		/// @brief A `Kind::Enum` with neither `names` nor `dynamic_names`.
		EnumWithoutNames,
		/// @brief A shortcut cell that points at a node no root reaches. `detail` packs the pad as
		///        `y * 256 + x`.
		ShortcutToNothing,
		/// @brief Two children of one parent share a `name` string id **and can be on screen
		///        together**. `detail` is that id.
		///
		/// Quantified rather than absolute, the same way `EmptySubmenu` is: legacy deliberately
		/// puts variant pairs side by side under one parent -- one item for a sound, one for a
		/// MIDI/CV clip, named identically because only one is ever visible. Two siblings collide
		/// when they share a `relevant` hook (including both having none) or when some supplied
		/// context admits both; see `validate.cpp`'s `co_visible` for the full reasoning. **With
		/// no contexts supplied only the static half applies**, which is the right answer for a
		/// structural-only check but means a caller passing `{}` will not catch a pair whose two
		/// different predicates happen always to agree.
		DuplicateName,
		/// @brief More than `kMaxCustomNodes` nodes pointing at a `CustomItem` in the whole tree.
		///        `detail` is the count found. Reported once, against the last custom node seen.
		TooManyCustom,
		/// @brief A node whose `title` is 0 (no string id at all -- the builders default `title` to
		///        `name`, so this means neither was given one).
		MissingTitle,
		/// @brief A `Kind::Range` with no `Binding::range_spec`, or one whose `format` is null:
		///        `render_leaf_range` would draw a title and nothing else.
		///
		/// The `Kind::Range` counterpart of `NullCustom`, and it needs to exist for the same reason:
		/// the pointer is the *only* thing that says how the upper edge is read, written and spelled
		/// (`node.h`'s `RangeSpec`), and a table that forgot it produces a blank editor rather than a
		/// crash. `range_item()` takes the spec as a required argument, so this fires only on a node
		/// assembled by hand.
		RangeWithoutSpec,
		/// @brief A `Kind::Custom` whose `custom` pointer is null: nothing would render.
		NullCustom,
		/// @brief A `Kind::Custom` pointing at a stand-in rather than the real item
		///        (`CustomItem::is_placeholder()`).
		///
		/// **Unconditional.** Every node in the tree has a real item (`kPendingCustomNodes` is 0), and
		/// a permanently-satisfiable opt-out is exactly the shape that later hides a regression, so
		/// `ValidateOptions` has none. The rule stays, and so does
		/// `nodes::kPending`: the next table that needs a stand-in still gets one, and this reports
		/// it with nothing left to turn it off.
		PlaceholderCustom,
		/// @brief The set of nodes carrying a flag does not equal the expected set this header
		///        declares. `detail` is `static_cast<uint16_t>(Flag)`; `node` is the offending node
		///        (unexpectedly set, or expected-but-missing -- the two are distinguished by
		///        whether `node` appears in the expected list).
		FlagMismatch,
	} kind{};
	const Node* node = nullptr;
	uint16_t detail = 0;
};

/// @brief Which flag a `FlagMismatch` is about (`Issue::detail`).
enum class Flag : uint16_t {
	/// @brief `Node::range_dependent` (`node.h`, legacy `MenuItem::isRangeDependent()`).
	RangeDependent,
	/// @brief `Binding::scaling == Scaling::HalfPrecision` (`bindings.h`'s two named exceptions).
	HalfPrecision,
	/// @brief Exactly one of `Node::select_chain` and `Node::chain_targets` is set.
	///
	/// A hook with no targets is a page the tree cannot number, validate or dump -- the chain would
	/// navigate to a node with id 0, which `MenuScene::on_enter` can never resolve again. Targets
	/// with no hook is the mirror: the pages are numbered and dumped but nothing can ever open them,
	/// so they read as reachable when they are not. This needs no expected set -- it is a property
	/// of one node.
	ChainTargetsMismatch,
	/// @brief `Binding::adjust` on a node with no `get` -- a combination that would read through
	///        `param` and write through the hook, silently disagreeing about where the value lives
	///        (`read` stays `get` even for an offset-writing node).
	///
	/// The value-kind rule just above (`is_value_kind(n.kind) && !n.bind.param.valid() && n.bind.get
	/// == nullptr`) already reports a value node with neither `get` nor a `param` at all; this is
	/// the remaining case -- `adjust` set on a node that still resolves its read through `param`
	/// rather than `get`. Every shipped offset-writing node (velocity, probability, iterance, fill)
	/// has both a `get` and an `adjust`, so this needs no expected set either -- it is a property
	/// of one node, like the two flags just above.
	AdjustWithoutGet,
	/// @brief A node with a `Binding::target` that also sets a hook the target replaces: `get`, `set`, `range`,
	///        `fixed_range`, `names`, `dynamic_names`, `relevant`, `adjust` or `range_spec`. A target node's value,
	///        range, choices and relevance come from its descriptor; a hook beside it would be a second, silently
	///        ignored, source. A `param` beside a target is learn-only (what `learn_target` binds), as beside a `get`;
	///        `value_text` and `permitted` stay the node's own (how the value reads, and when it may be edited).
	TargetWithHooks,
};

/// @brief The cap on the `Kind::Custom` escape hatch, counted in **nodes that point at a custom
///        item**.
///
/// Counting nodes rather than distinct `CustomItem` instances keeps the rule live: the shipped
/// tree's custom nodes each have their own item (no two share one), so the two counts are equal
/// today, but a distinct-instance count would stay at 1 if a table pointed many nodes at one shared
/// stand-in, and `TooManyCustom` could never fire however many custom nodes it added.
///
/// The shipped tree has **61** `Kind::Custom` nodes reachable from `nodes::kAllRoots`, behind **61
/// distinct items, every one of them real**. The cap is pinned exactly one above that count: it is
/// a ratchet against the escape hatch growing unnoticed, not a claim that any smaller number was
/// achievable. A cap sitting further above the count would be that many bespoke items an
/// inattentive table could add for free. `sim/scenes_spec/shortcuts_spec.cpp` asserts the exact 61
/// separately, so a table that adds a custom node has to say so in two places.
inline constexpr size_t kMaxCustomNodes = 62;

/// @brief The number of `Kind::Custom` nodes the shipped tree actually has (`kAllRoots`).
///
/// Named here so `kMaxCustomNodes`'s headroom is visible rather than a magic gap, and asserted
/// against the live tree by `sim/scenes_spec/shortcuts_spec.cpp`.
inline constexpr size_t kShippedCustomNodes = 61;

/// @brief How many of `kShippedCustomNodes` still point at `nodes::kPending`.
///
/// **Zero -- the menu tree has no placeholders left.** The file/sample family points at the scenes
/// that own those jobs (`nodes/sample.h`): the two oscillator file browsers at
/// `browser::BrowserScene`, the four loop points and the audio clip's waveform view at
/// `marker::MarkerScene`, and the two record-to-sample items at `recorder::RecorderScene`. Two
/// one-offs open neither a sample scene nor, in one case, anything at all
/// (`nodes/output_items.h`):
///
///   * `midi_device_linked` -- a `Toggle` whose ON arm opens the definition browser
///     (`browser::BrowserId::MidiDeviceDefinition`) and whose OFF arm clears the linked file name.
///   * `specific_output_selector` -- the audio clip's `TRACK` picker, a page over the song's output
///     list rather than a file browser.
///
/// **This must stay 0.** `sim/scenes_spec/output_items_spec.cpp` asserts it three ways -- this
/// constant, a strict `validate_tree` over `kAllRoots` reporting no `PlaceholderCustom`, and a walk
/// of every reachable `Kind::Custom` node -- and there is no `ValidateOptions` field that could
/// stand the rule down. A table that adds a placeholder fails there rather than hiding inside a
/// number that happened not to move.
inline constexpr size_t kPendingCustomNodes = 0;

/// @brief Which of the strict rules the caller is deliberately standing down.
///
/// Default-constructed is strict, so a caller that says nothing gets every rule. There is
/// deliberately no option to stand down `PlaceholderCustom`: with `kPendingCustomNodes` at 0 an
/// opt-out would be permanently satisfiable, which is the shape that hides the next regression.
/// `nodes::kPending` still exists, so a table that needs a stand-in can have one -- it just cannot
/// ask for silence.
struct ValidateOptions {
	/// @brief Skip the `ShortcutToNothing` sweep entirely.
	///
	/// That rule is quantified over *every* `MenuContext::Root`'s shortcut table, so it only means
	/// anything when @p roots is the whole tree: pointed at a subtree (batch 1's seventeen roots,
	/// the sound root on its own, the settings root minus one page) it reports every shortcut cell
	/// in every *other* root's table, which says nothing about the subtree under test. A caller
	/// validating part of the tree sets this; `sim/scenes_spec/shortcuts_spec.cpp` runs the sweep
	/// over `nodes::kAllRoots`, where it is a real check.
	bool skip_shortcut_sweep = false;
};

/// @brief Check @p roots against every rule.
///
/// Calls `assign_node_ids(roots)` first, so ids exist before anything downstream needs
/// them and so the `Unreachable` rule has something to test.
///
/// @param roots     The tree's root nodes, in declaration order.
/// @param contexts  One `MenuContext` per root the caller cares about -- the "under at least one
///                  context" quantifier of the `EmptySubmenu` rule. An empty span skips relevance
///                  filtering entirely (every declared child counts as visible), which is what a
///                  structural-only check wants.
/// @param expected_range_dependent Every node that must carry `Node::range_dependent`, and no
///                  others.
/// @param expected_half_precision  Every node whose binding must use `Scaling::HalfPrecision`, and
///                  no others.
/// @param options   Which strict rules to stand down. Default-constructed is strict.
/// @return The issues found, capped at 64 (a tree with more than 64 problems does not need an
///         exhaustive list to be rejected).
deluge::ui::shell::FixedVector<Issue, 64> validate_tree(std::span<const Node* const> roots,
                                                        std::span<const MenuContext> contexts,
                                                        std::span<const Node* const> expected_range_dependent = {},
                                                        std::span<const Node* const> expected_half_precision = {},
                                                        ValidateOptions options = {});

} // namespace deluge::scenes::menu
