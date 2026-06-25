/*
 * Copyright © 2024 Synthstrom Audible Limited
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

#include "deluge-project/param_backup.h"
#include "util/exceptions.h"
#include <utility>

namespace deluge::project {

ParamManager* ParamBackup::getForExactClip(ModControllableAudio* modControllable, Clip* clip, ParamManager* stealInto) {
	auto itCorrectClip = map.find({modControllable, clip});
	if (itCorrectClip == map.end()) {
		return nullptr;
	}

	if (stealInto) {
		stealInto->stealParamCollectionsFrom(
		    &itCorrectClip->second,
		    true); // Steal expression params too - if they're here (slightly rare case).
		map.erase(itCorrectClip);
		return stealInto;
	}
	return &itCorrectClip->second;
}

ParamManager* ParamBackup::getPreferablyWithClip(ModControllableAudio* modControllable, Clip* clip,
                                                 ParamManager* stealInto) {
	// The entry with a null Clip (if any) sorts first for each ModControllable, so this finds it preferentially.
	auto itAnyClip = map.lower_bound({modControllable, static_cast<Clip*>(nullptr)});
	if (itAnyClip == map.end() || itAnyClip->first.first != modControllable) {
		return nullptr; // If nothing with even the correct modControllable at all, get out
	}

	auto itCorrectClip = itAnyClip;
	if (clip && itAnyClip->first.second != clip) {
		auto itExact = map.find({modControllable, clip});
		if (itExact != map.end()) {
			itCorrectClip = itExact;
		}
	}

	if (stealInto) {
		stealInto->stealParamCollectionsFrom(
		    &itCorrectClip->second,
		    true); // Steal expression params too - if they're here (slightly rare case).
		map.erase(itCorrectClip);
		return stealInto;
	}
	return &itCorrectClip->second;
}

void ParamBackup::backUp(ModControllableAudio* modControllable, Clip* clip, ParamManagerForTimeline* paramManager,
                         bool shouldStealExpressionParamsToo) {
	if (!paramManager->containsAnyMainParamCollections()) {
		return;
	}

	auto it = map.find({modControllable, clip});

	// If one already existed...
	if (it != map.end()) {
		// Let's destroy its contents and replace them
		it->second.destructAndForgetParamCollections();
		it->second.stealParamCollectionsFrom(paramManager, shouldStealExpressionParamsToo);
	}

	// Otherwise, insert one
	else {
		try {
			auto [itNew, inserted] = map.try_emplace({modControllable, clip});
			itNew->second.stealParamCollectionsFrom(paramManager, shouldStealExpressionParamsToo);
		} catch (deluge::exception&) {
			// If RAM error, destroy paramManager
			paramManager->destructAndForgetParamCollections();
		}
	}
}

void ParamBackup::deleteForModControllable(ModControllableAudio* modControllable) {
	auto it = map.lower_bound({modControllable, static_cast<Clip*>(nullptr)});
	while (it != map.end() && it->first.first == modControllable) {
		it = map.erase(it);
	}
}

} // namespace deluge::project
