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

} // namespace deluge::scenes::spec
