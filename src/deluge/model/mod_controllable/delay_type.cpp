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

#include "model/mod_controllable/delay_type.h"

#include "model/clip/audio_clip.h"
#include "model/clip/instrument_clip.h"
#include "model/instrument/kit.h"
#include "model/note/note_row.h"
#include "model/song/clip_iterators.h"
#include "model/song/song.h"
#include "modulation/automation/auto_param.h"
#include "modulation/params/param.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_set.h"
#include "modulation/patch/patch_cable_set.h"
#include "processing/audio_output.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"

#include <algorithm>
#include <limits>
#include <utility>
#include <vector>

namespace params = deluge::modulation::params;

namespace deluge::model {
namespace {

/// The value a feedback knob at its minimum holds.
constexpr int32_t kFeedbackMinimum = std::numeric_limits<int32_t>::min();

/// @return Whether param @p id of @p set is above its minimum or automated, reading @p before in its place when it
/// is the changed one.
bool paramLifted(ParamSet& set, int32_t id, const ParamBefore& before) {
	AutoParam& param = set.params[id];
	if (&param == before.param) {
		return before.value != kFeedbackMinimum || before.automated;
	}
	if (param.getCurrentValue() != kFeedbackMinimum) {
		return true;
	}
	const deluge::project::DocAutoParam* doc = set.docForParam(id);
	return doc != nullptr && param.isAutomated(*doc);
}

/// @return Whether a patch cable of @p cables reaches the delay's feedback, directly or through another cable's depth;
/// the changed cable named by @p before counts only if its amount was not 0.
bool cableReachesFeedback(PatchCableSet& cables, const ParamBefore& before) {
	for (int32_t c = 0; c < cables.numPatchCables; c++) {
		const PatchCable& cable = cables.patchCables[c];
		if (cable.destinationParamDescriptor.getJustTheParam() != params::GLOBAL_DELAY_FEEDBACK) {
			continue;
		}
		if (&cable.param == before.param && before.value == 0) {
			continue;
		}
		return true;
	}
	return false;
}

} // namespace

bool delayCouldSound(ParamManager& paramManager, bool sound, const ParamBefore& before) {
	if (!paramManager.containsAnyMainParamCollections()) {
		return false;
	}
	if (!sound) {
		return paramLifted(*paramManager.getUnpatchedParamSet(), params::UNPATCHED_DELAY_AMOUNT, before);
	}
	return paramLifted(*paramManager.getPatchedParamSet(), params::GLOBAL_DELAY_FEEDBACK, before)
	       || cableReachesFeedback(*paramManager.getPatchCableSet(), before);
}

void forEachDelayParamManager(Song& song, const std::function<void(ModControllableAudio&, ParamManager&)>& visit) {
	for (InstrumentClip* clip : InstrumentClips::everywhere(&song)) {
		Output* output = clip->output;
		if (output == nullptr) {
			continue;
		}
		if (output->type == OutputType::SYNTH) {
			visit(*static_cast<SoundInstrument*>(output), clip->paramManager);
		}
		else if (output->type == OutputType::KIT) {
			visit(*static_cast<Kit*>(output), clip->paramManager);
			for (int32_t i = 0; i < std::ssize(clip->noteRows); i++) {
				NoteRow& row = clip->noteRows[i];
				if (row.drum != nullptr && row.drum->type == DrumType::SOUND) {
					visit(*static_cast<SoundDrum*>(row.drum), row.paramManager);
				}
			}
		}
	}
	for (AudioClip* clip : AudioClips::everywhere(&song)) {
		if (clip->output != nullptr) {
			visit(*static_cast<AudioOutput*>(clip->output), clip->paramManager);
		}
	}
	visit(song.live.global_effectable, song.paramManager);
	for (auto& [key, paramManager] : song.live.param_backup.map) {
		visit(*key.first, paramManager);
	}
}

namespace {

/// @brief Gives @p owner its type, when its file saved none, from whether any of its param managers could sound.
void resolve(ModControllableAudio& owner, bool couldSound) {
	if (!owner.delayTypePending) {
		return;
	}
	owner.delayTypePending = false;
	const deluge::effects::DelayType type = owner.effects().delay.type;
	if (!couldSound) {
		if (type != deluge::effects::DelayType::OFF) {
			owner.setDelayType(deluge::effects::DelayType::OFF);
		}
	}
	else if (type == deluge::effects::DelayType::OFF) {
		owner.setDelayType(deluge::effects::DelayType::DIGITAL);
	}
}

/// @brief Resolves each pending owner of @p song that @p wanted accepts, from all of its param managers.
void resolveWhere(Song& song, const std::function<bool(const ModControllableAudio&)>& wanted) {
	std::vector<std::pair<ModControllableAudio*, bool>> owners;
	forEachDelayParamManager(song, [&](ModControllableAudio& owner, ParamManager& paramManager) {
		if (!owner.delayTypePending || !wanted(owner)) {
			return;
		}
		const bool could = delayCouldSound(paramManager, owner.asSound() != nullptr);
		auto found = std::ranges::find_if(owners, [&](const auto& entry) { return entry.first == &owner; });
		if (found == owners.end()) {
			owners.emplace_back(&owner, could);
		}
		else {
			found->second = found->second || could;
		}
	});
	for (const auto& [owner, could] : owners) {
		resolve(*owner, could);
	}
}

} // namespace

void resolvePendingDelayTypes(Song& song) {
	resolveWhere(song, [](const ModControllableAudio&) { return true; });
}

void resolvePendingDelayType(Song& song, ModControllableAudio& owner) {
	if (!owner.delayTypePending) {
		return;
	}
	resolveWhere(song, [&](const ModControllableAudio& candidate) { return &candidate == &owner; });
}

} // namespace deluge::model
