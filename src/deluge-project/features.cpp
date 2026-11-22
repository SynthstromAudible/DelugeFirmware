/*
 * Copyright © 2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#include "deluge-project/features.h"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace deluge::project {
namespace {

/// Whether every entry sits at its id's index, names a file name no other entry has, and has a default it can hold;
/// and whether each name's text ends in a NUL, as a string literal's does, so a writer may take it as a C string.
constexpr bool features_are_well_formed() {
	for (size_t at = 0; at < kFeatures.size(); ++at) {
		const FeatureInfo& info = kFeatures[at];
		if (std::to_underlying(info.id) != at || info.path.empty() || info.default_value >= info.choices.size()
		    || info.path.data()[info.path.size()] != '\0') {
			return false;
		}
		const auto same_path = [&](const FeatureInfo& other) { return other.path == info.path; };
		if (std::ranges::count_if(kFeatures, same_path) != 1) {
			return false;
		}
	}
	return true;
}
static_assert(features_are_well_formed(),
              "a feature's entry is out of place, named twice, has no default, or is not a string literal");

static_assert(std::to_underlying(ShortcutOverlay::MENU_AND_CLIP) + 1 == kShortcutOverlayWords.size()
                  && std::to_underlying(SyncScalingAction::FILL) + 1 == kSyncScalingActionWords.size(),
              "an enumerator's value is its word's index");

} // namespace
} // namespace deluge::project
