#pragma once

/// @file
/// @brief Where a node's own answer differs from what upstream `main`'s menu item answers, pinned by identity and by
///        count so a difference that is fixed, or that grows, shows.
///
/// The reference rows say what `main` answers; these are the questions the port answers differently today. A spec that
/// compares an answer to its row excuses exactly the nodes these predicates name, requires the excused difference to be
/// there (the port's answer is the one stated here, the row is main's), and requires the number of excused nodes to be
/// the count below. Fixing the port, or breaking another node the same way, turns the example red.

#include "modulation/params/param.h"
#include "scenes/menu/node.h"
#include "scenes/menu/nodes/nodes.h"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace deluge::scenes::spec {

/// @brief `main`'s `UnpatchedParam::usesAffectEntire()` is true for every unpatched param, and so is the audio clip's
///        interpolation item's; `node_uses_affect_entire` answers false for the global params and the interpolation
///        node carries no marking. Excused only on the roots whose menu has no AFFECT ENTIRE hold of its own (the
///        song's, the performance view's, the audio clip's and the kit's global effects), where the answer cannot
///        change what a hold writes.
inline bool affect_entire_differs_from_main(std::string_view root, const menu::Node& node) {
	const bool root_has_no_hold =
	    root == "audio clip" || root == "performance" || root == "song" || root == "kit global fx";
	return root_has_no_hold
	       && (node.bind.param.kind == deluge::modulation::params::Kind::UNPATCHED_GLOBAL
	           || &node == &deluge::scenes::menu::nodes::audio_clip_interpolation);
}

/// @brief How many leaves of the ten roots that is: the `ae` rows main has at 1 and the port at 0.
inline constexpr size_t kAffectEntireDifferences = 55;

/// @brief `main` flags no audio recorder and no file selector range dependent (`MenuItem::isRangeDependent` is
///        overridden by the loop points, the transpose and the range picker alone); the port flags them.
inline bool range_dependence_differs_from_main(const menu::Node& node) {
	namespace nodes = deluge::scenes::menu::nodes;
	return &node == &nodes::sample0_recorder || &node == &nodes::sample1_recorder || &node == &nodes::file0_selector
	       || &node == &nodes::file1_selector;
}

/// @brief How many leaves that is: across the ten roots, in the sound root, and in the seventeen batch-1 roots.
inline constexpr size_t kRangeDependenceDifferencesTen = 13;
inline constexpr size_t kRangeDependenceDifferencesSound = 4;
inline constexpr size_t kRangeDependenceDifferencesBatch1 = 4;

/// @brief `reverb::Width::getTitle()` returns its name ("Width"); the port's node carries its own title id ("Reverb
///        width"). The room size title matches.
inline bool title_differs_from_main(const menu::Node& node) {
	return &node == &deluge::scenes::menu::nodes::reverb_width;
}

/// @brief The root fixtures reaching the width node.
inline constexpr size_t kReverbWidthTitleDifferences = 6;

/// @name The shortcut layouts' excusals
/// Where a shortcut cell's port node cannot be compared with main's reference row by plain equality, pinned by identity
/// and by count so a fix, or a new excused cell, turns the example red.
/// @{

/// @brief Nodes whose port name is a placeholder the menu substitutes an index into at render time (`*` stands for the
///        digit: the two oscillator levels, the two sample recorders and the two FM modulator levels), where main's
///        reference row holds the resolved name. The cell is compared by presence of both names.
inline bool shortcut_name_is_placeholder(const menu::Node& node) {
	namespace nodes = deluge::scenes::menu::nodes;
	return &node == &nodes::source0_volume || &node == &nodes::source1_volume || &node == &nodes::sample0_recorder
	       || &node == &nodes::sample1_recorder || &node == &nodes::modulator0_volume
	       || &node == &nodes::modulator1_volume;
}

/// @brief How many shortcut cells hold one of those nodes, over the seven reference tables (the song table once).
inline constexpr size_t kShortcutPlaceholderNameCells = 6;

/// @brief How many shortcut cells hold a node `tables3::has_dynamic_name` names (the filter morph items), whose name
///        main resolves from the live filter mode.
inline constexpr size_t kShortcutDynamicNameCells = 28;

/// @brief Main's V1 audio-clip layout puts the marker editor's start item, a nameless item no root reaches, on
///        `(0..1, 7)`; the port has one marker node, so those two cells are compared by hand.
inline bool shortcut_marker_collapsed(std::string_view table, int32_t x, int32_t y) {
	return table == "audio_clips" && y == 7 && x < 2;
}
inline constexpr size_t kShortcutCollapsedMarkerCells = 2;

/// @brief Landing cells whose page the port shares with another root's tree (main has one page per root family), so the
///        landing rows name the page by its paths in the landing root's own tree.
inline constexpr size_t kLandingSharedPageCells = 158;

/// @brief Landing cells whose page is in no path of the landing root's own tree, named `outside:<paths>` in the rows:
///        the song and performance roots' pad (7, 5).
inline constexpr size_t kLandingOutsideRootCells = 2;

/// @brief Landing cells whose page is in no tree, so the rows name it: the sample recorder page.
inline constexpr size_t kLandingUnlistedPageCells = 4;

/// @}

/// @name The clip view's shortcut overview
/// @{

/// @brief The arp octaves pad on a kit's own table while the kit-global latch is clear (the clip view's overview right
///        after a menu close): main's `arpeggiator::Octaves::isRelevant` asks only `editingGateDrumRow()` and
///        `editingKitAffectEntire()`, the latch, and lights it; the port's `arp_octaves` is a setting target, which a
///        kit's own chain does not hold, and leaves it black. Pressing it latches, and main then refuses it too.
inline bool clip_overview_cell_differs_from_main(std::string_view case_name, int32_t x, int32_t y) {
	return (case_name == "kit_affect_entire_unlatched" || case_name == "kit_affect_entire_no_row_unlatched") && x == 11
	       && y == 3;
}

/// @brief How many cells that is, over every case.
inline constexpr size_t kClipOverviewCellDifferences = 2;

/// @}

} // namespace deluge::scenes::spec
