#pragma once

/// @brief The menu-tree validator (spec E §4.4: "host test, every build").
///
/// Every rule spec E §4.4 lists is one `Issue::Kind` below, plus the two flag audits ruling 4 of
/// this task's brief adds -- a `Node` flag that a table forgot to set is invisible at runtime
/// (the item just quietly behaves like an ordinary one), so the expected sets are written down
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
		///        (`CustomItem::is_placeholder()`). Suppressed by
		///        `ValidateOptions::allow_placeholder_custom`, which is how an E1 spec says out loud
		///        that E2 still owes those implementations.
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
	/// @brief `Binding::updates_reverb_params` on a node that also has a `set` hook -- a combination
	///        that silently does nothing.
	///
	/// `bindings::write` prefers the hook and returns before the flag is ever read
	/// (`bindings.cpp`'s `write`, the `bind.set != nullptr` arm), so a table declaring both would
	/// look like it raised `AudioEngine::mustUpdateReverbParamsBeforeNextRender` and would not.
	/// The two legacy `UpdatingReverbParams` items (`sidechain::Shape`, `sidechain::GlobalVolume`)
	/// are both pure `ParamRef` nodes, so the combination is a table bug rather than a shape this
	/// tree needs -- and the right answer for a hook that *does* need the flag is to set the engine
	/// flag inside the hook, as the sidechain attack/release/sync hooks do. Unlike the two audits
	/// above this rule needs no expected set: it is a property of one node.
	ReverbUpdateWithHook,
	/// @brief Exactly one of `Node::select_chain` and `Node::chain_targets` is set.
	///
	/// A hook with no targets is a page the tree cannot number, validate or dump -- the chain would
	/// navigate to a node with id 0, which `MenuScene::on_enter` can never resolve again. Targets
	/// with no hook is the mirror: the pages are numbered and dumped but nothing can ever open them,
	/// so they read as reachable when they are not. Like `ReverbUpdateWithHook` this needs no
	/// expected set -- it is a property of one node.
	ChainTargetsMismatch,
};

/// @brief The cap on the `Kind::Custom` escape hatch, counted in **nodes that point at a custom
///        item**.
///
/// **Deviation from spec E §4** ("at most 10 across the whole tree"), fix round 2, I4. The rule
/// used to count *distinct `CustomItem` instances*, on the reasoning that two oscillator
/// file-selector nodes sharing one `FileSelector` implementation are one bespoke item -- which is
/// how spec E §4's own list of seven counts them. That spelling was inert: E1 ships the *shape* of
/// its bespoke items and points all 59 of them at the one shared `nodes::kPending` placeholder
/// (`nodes/sound_osc.h`), so the distinct-instance count of the whole shipped tree is **1** and
/// `TooManyCustom` could not fire however many custom nodes a table added. It would have started
/// counting for real only once E2 replaced the placeholders one by one -- i.e. the rule was
/// switched off for exactly the phase it was meant to police.
///
/// Counting nodes makes it live today, at the cost of no longer being spec E's number: the shipped
/// tree has **52** `Kind::Custom` nodes reachable from `nodes::kAllRoots` -- 64 through E2 task 9,
/// less the nine E2 task 11 turned into ordinary `Kind::Enum` items (the six pad colours, the play
/// cursor, the dev-sysex setting and the shortcuts version) and the three E2 task 11b turned into
/// `Kind::Range` (`default_tempo`, `default_swing_amount`, `default_key`) -- behind 42 distinct
/// items, of which 41 are real and one is still `nodes::kPending` (see `kPendingCustomNodes`).
/// So the cap is pinned just above that count rather than at 10: it is a ratchet against the escape
/// hatch growing unnoticed, not a claim that ten was ever achievable.
/// `sim/scenes_spec/shortcuts_spec.cpp` asserts the exact 52 separately, so a table that adds a custom node has to say
/// so in two places. Recorded in `docs/superpowers/specs/2026-08-26-ui-scenes-menus-design.md`'s amendments and in
/// `src/scenes/README.md`.
///
/// **E2 task 12 ratcheted it from 56 to 53.** The cap was sized against E2 task 9's tree and then
/// three item tasks took nodes back out of the escape hatch without it following them down; a cap
/// four above the count is four bespoke items an inattentive table could add for free.
inline constexpr size_t kMaxCustomNodes = 53;

/// @brief The number of `Kind::Custom` nodes the shipped tree actually has (`kAllRoots`).
///
/// Named here so `kMaxCustomNodes`'s headroom is visible rather than a magic gap, and asserted
/// against the live tree by `sim/scenes_spec/shortcuts_spec.cpp`.
inline constexpr size_t kShippedCustomNodes = 52;

/// @brief How many of `kShippedCustomNodes` still point at `nodes::kPending`.
///
/// **Eleven, and they are one family.** E2 finished every bespoke item it owned; what is left is the
/// file/sample group, which belongs to a later plan because all of it needs the card:
///
///   * `file0_selector` / `file1_selector` -- `FILE_BROWSER` (`nodes/sound_osc.h`, the `SOURCE_NODES`
///     macro), legacy's `sample_browser` / `file_selector`;
///   * `sample0_start` / `sample1_start` and `sample0_end` / `sample1_end` -- `START_POINT` /
///     `END_POINT` (same macro), legacy's `sample/start.h` and `sample/end.h`;
///   * `sample0_recorder` / `sample1_recorder` -- `RECORD_TO_SAMPLE` (`nodes/sound_osc.h`), legacy's
///     `osc/audio_recorder.h`;
///   * `midi_device_linked` -- `MIDI_DEVICE_DEFINITION_LINKED` (`nodes/roots.h`);
///   * `specific_output_selector` -- the audio clip's `TRACK` picker (`nodes/roots.h`);
///   * `audio_clip_waveform` -- the audio clip's `WAVEFORM` view (`nodes/roots.h`).
///
/// `sim/scenes_spec/shortcuts_spec.cpp` asserts the exact **set** by node identity, not just this
/// count, so the later plan that closes one of them out has to say which -- and adding a twelfth
/// placeholder to a table fails there rather than hiding inside a number that happened not to move.
inline constexpr size_t kPendingCustomNodes = 11;

/// @brief Which of the strict rules the caller is deliberately standing down.
///
/// Default-constructed is strict, so a caller that says nothing gets every rule. Only E1's own
/// specs opt out, and only from the one rule that is *about* E1 being unfinished.
struct ValidateOptions {
	/// @brief Accept `Kind::Custom` nodes pointing at `CustomItem::is_placeholder()` stand-ins.
	///
	/// E1 declared the tree's shape, including the bespoke items whose implementations belonged to
	/// E2 (the file selector, the audio recorder, the sample loop points, the name editors, the
	/// patch-cable pages, the multi-range picker). Those nodes carry the shared placeholder rather
	/// than a null pointer -- `NullCustom` stays a live rule, and a null `custom` is still an
	/// outright bug -- but the tree is not finished, and by default `validate_tree` says so.
	///
	/// **E2 built all but the file/sample family**, which needs the card and belongs to a later
	/// plan; `kPendingCustomNodes` above names the eleven nodes that remain and
	/// `sim/scenes_spec/shortcuts_spec.cpp` pins them by identity. So this flag outlives E2 -- the
	/// plan that closes those eleven out is the one that deletes it and the rule it suppresses.
	bool allow_placeholder_custom = false;

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

/// @brief Check @p roots against every spec E §4.4 rule.
///
/// Calls `assign_node_ids(roots)` first (ruling Q7), so ids exist before anything downstream needs
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
