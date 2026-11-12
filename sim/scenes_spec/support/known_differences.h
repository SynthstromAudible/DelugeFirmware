#pragma once

/// @file
/// @brief Where a node's own flag differs from what upstream `main`'s menu item answers, pinned by identity so a
///        difference that is fixed (or grows) shows.
///
/// The reference rows say what `main` answers; these are the questions the port answers differently today. A spec
/// that compares a flag to its row excuses exactly these, and requires the excused difference to still be there.

#include "modulation/params/param.h"
#include "scenes/menu/node.h"
#include "scenes/menu/nodes/nodes.h"

namespace deluge::scenes::spec {

/// @brief `main`'s `UnpatchedParam::usesAffectEntire()` is true for every unpatched param, and so is the audio clip's
///        interpolation item's; `node_uses_affect_entire` answers false for the global params (a song's, an audio
///        clip's, a kit's global effects) and the interpolation node carries no marking.
inline bool affect_entire_differs_from_main(const menu::Node& node) {
	return node.bind.param.kind == deluge::modulation::params::Kind::UNPATCHED_GLOBAL
	       || &node == &deluge::scenes::menu::nodes::audio_clip_interpolation;
}

/// @brief `main` flags no audio recorder and no file selector range dependent (`MenuItem::isRangeDependent` is
///        overridden by the loop points, the transpose and the range picker alone); the port flags them.
inline bool range_dependence_differs_from_main(const menu::Node& node) {
	namespace nodes = deluge::scenes::menu::nodes;
	return &node == &nodes::sample0_recorder || &node == &nodes::sample1_recorder || &node == &nodes::file0_selector
	       || &node == &nodes::file1_selector;
}

} // namespace deluge::scenes::spec
