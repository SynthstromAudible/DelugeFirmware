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

#include "model/project_bridge/param_backup.h"

#include "model/clip/clip.h"
#include "model/drum/drum.h"
#include "model/instrument/kit.h"
#include "model/output.h"
#include "model/song/song.h"
#include "processing/audio_output.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"
#include "util/exceptions.h"

#include <algorithm>
#include <utility>

namespace deluge::bridge {

namespace {

std::optional<uint32_t> clipIdOf(const ::Clip* clip) {
	return clip != nullptr ? std::optional{clip->id()} : std::nullopt;
}

/// @brief Move @p from's collections into @p stealInto, or hand @p from back.
ParamManager* takeOrGive(ParamManager& from, ParamManager* stealInto) {
	if (stealInto == nullptr) {
		return &from;
	}
	stealInto->stealParamCollectionsFrom(&from, true); // expression params too, if they're here (slightly rare)
	return stealInto;
}

} // namespace

std::optional<project::OwnerId> BackupOwner::adopted() const {
	if (const auto* output = std::get_if<::Output*>(&owner_)) {
		return (*output)->docOutput_ != nullptr && (*output)->id()
		           ? std::optional{project::OwnerId::instrument(*(*output)->id())}
		           : std::nullopt;
	}
	const ::Drum* drum = std::get<::Drum*>(owner_);
	return drum->docDrum_ != nullptr && drum->id() ? std::optional{project::OwnerId::drum(*drum->id())} : std::nullopt;
}

std::optional<project::OwnerId> BackupOwner::any_id() const {
	if (const auto* output = std::get_if<::Output*>(&owner_)) {
		return (*output)->id() ? std::optional{project::OwnerId::instrument(*(*output)->id())} : std::nullopt;
	}
	const ::Drum* drum = std::get<::Drum*>(owner_);
	return drum->id() ? std::optional{project::OwnerId::drum(*drum->id())} : std::nullopt;
}

::Output* BackupOwner::output() const {
	const auto* output = std::get_if<::Output*>(&owner_);
	return output != nullptr ? *output : nullptr;
}

::Drum* BackupOwner::drum() const {
	const auto* drum = std::get_if<::Drum*>(&owner_);
	return drum != nullptr ? *drum : nullptr;
}

BackupOwner backupOwnerOf(::Sound& sound) {
	if (sound.isDrum()) {
		return BackupOwner{static_cast<::Drum&>(static_cast<::SoundDrum&>(sound))};
	}
	return BackupOwner{static_cast<::Output&>(static_cast<::SoundInstrument&>(sound))};
}

std::optional<BackupOwner> backupOwnerOf(::ModControllableAudio& owner, ::Song& song) {
	const auto mine = [&](::ModControllableAudio* candidate) { return candidate == &owner; };
	for (::Output* list :
	     {song.live.media.first_output, static_cast<::Output*>(song.live.media.first_hibernating_instrument)}) {
		for (::Output* output = list; output != nullptr; output = output->next) {
			switch (output->type) {
			case OutputType::SYNTH:
				if (mine(static_cast<::SoundInstrument*>(output))) {
					return BackupOwner{output};
				}
				break;
			case OutputType::AUDIO:
				if (mine(static_cast<::AudioOutput*>(output))) {
					return BackupOwner{output};
				}
				break;
			case OutputType::KIT: {
				auto* kit = static_cast<::Kit*>(output);
				if (mine(kit)) {
					return BackupOwner{output};
				}
				for (::Drum* drum = kit->firstDrum; drum != nullptr; drum = drum->next) {
					if (drum->type == DrumType::SOUND && mine(static_cast<::SoundDrum*>(drum))) {
						return BackupOwner{drum};
					}
				}
				break;
			}
			default:
				break;
			}
		}
	}
	return std::nullopt;
}

ParamManager* ParamBackup::getForExactClip(BackupOwner owner, const ::Clip* clip, ParamManager* stealInto) {
	const std::optional<uint32_t> clipId = clipIdOf(clip);
	if (!owner.adopted()) {
		auto it = std::ranges::find_if(
		    pending, [&](const Pending& entry) { return entry.owner.is(owner) && entry.clip_id == clipId; });
		if (it != pending.end()) {
			ParamManager* result = takeOrGive(it->params, stealInto);
			if (stealInto != nullptr) {
				pending.erase(it);
			}
			return result;
		}
	}
	const std::optional<project::OwnerId> id = owner.adopted() ? owner.adopted() : owner.any_id();
	if (!id) {
		return nullptr;
	}
	auto it = map.find({*id, clipId});
	if (it == map.end()) {
		return nullptr;
	}
	ParamManager* result = takeOrGive(it->second, stealInto);
	if (stealInto != nullptr) {
		map.erase(it);
	}
	return result;
}

ParamManager* ParamBackup::getPreferablyWithClip(BackupOwner owner, const ::Clip* clip, ParamManager* stealInto) {
	const std::optional<uint32_t> clipId = clipIdOf(clip);
	if (!owner.adopted()) {
		// The no-clip entry first, as the map orders it, unless there is one for exactly the clip.
		auto chosen = pending.end();
		for (auto it = pending.begin(); it != pending.end(); ++it) {
			if (!it->owner.is(owner)) {
				continue;
			}
			if (clipId && it->clip_id == clipId) {
				chosen = it;
				break;
			}
			if (chosen == pending.end() || (!it->clip_id && chosen->clip_id)) {
				chosen = it;
			}
		}
		if (chosen != pending.end()) {
			ParamManager* result = takeOrGive(chosen->params, stealInto);
			if (stealInto != nullptr) {
				pending.erase(chosen);
			}
			return result;
		}
	}
	const std::optional<project::OwnerId> id = owner.adopted() ? owner.adopted() : owner.any_id();
	if (!id) {
		return nullptr;
	}
	// The entry with no clip (if any) sorts first for each owner, so this finds it preferentially.
	auto itAnyClip = map.lower_bound({*id, std::nullopt});
	if (itAnyClip == map.end() || itAnyClip->first.owner != *id) {
		return nullptr;
	}
	auto itCorrectClip = itAnyClip;
	if (clipId && itAnyClip->first.clip_id != clipId) {
		auto itExact = map.find({*id, clipId});
		if (itExact != map.end()) {
			itCorrectClip = itExact;
		}
	}
	ParamManager* result = takeOrGive(itCorrectClip->second, stealInto);
	if (stealInto != nullptr) {
		map.erase(itCorrectClip);
	}
	return result;
}

void ParamBackup::backUp(BackupOwner owner, const ::Clip* clip, ParamManagerForTimeline* paramManager,
                         bool shouldStealExpressionParamsToo) {
	if (!paramManager->containsAnyMainParamCollections()) {
		return;
	}
	const std::optional<uint32_t> clipId = clipIdOf(clip);
	try {
		ParamManager* slot = nullptr;
		if (const std::optional<project::OwnerId> id = owner.adopted()) {
			slot = &map[project::BackedParamKey{*id, clipId}];
		}
		else {
			auto it = std::ranges::find_if(
			    pending, [&](const Pending& entry) { return entry.owner.is(owner) && entry.clip_id == clipId; });
			slot = it != pending.end() ? &it->params : &pending.emplace_back(Pending{owner, clipId, {}}).params;
		}
		// One that already existed has its contents destroyed and replaced.
		slot->destructAndForgetParamCollections();
		slot->stealParamCollectionsFrom(paramManager, shouldStealExpressionParamsToo);
	} catch (deluge::exception&) {
		// If RAM error, destroy paramManager
		paramManager->destructAndForgetParamCollections();
	}
}

void ParamBackup::deleteFor(BackupOwner owner) {
	if (const std::optional<project::OwnerId> id = owner.any_id()) {
		auto it = map.lower_bound({*id, std::nullopt});
		while (it != map.end() && it->first.owner == *id) {
			it = map.erase(it);
		}
	}
	std::erase_if(pending, [&](const Pending& entry) { return entry.owner.is(owner); });
}

void ParamBackup::adopt(BackupOwner owner) {
	const std::optional<project::OwnerId> id = owner.adopted();
	if (!id) {
		return;
	}
	for (auto it = pending.begin(); it != pending.end();) {
		if (!it->owner.is(owner)) {
			++it;
			continue;
		}
		ParamManager& slot = map[project::BackedParamKey{*id, it->clip_id}];
		slot.destructAndForgetParamCollections();
		slot.stealParamCollectionsFrom(&it->params, true);
		it = pending.erase(it);
	}
}

} // namespace deluge::bridge
