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

#include "libdeluge/system.h"
#include "model/clip/audio_clip.h"
#include "model/clip/instrument_clip.h"
#include "model/edits/change_effect.h"
#include "model/instrument/kit.h"
#include "model/mod_controllable/delay_arena_app.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/project_bridge/controls_bridge.h"
#include "model/project_bridge/param_backup.h"
#include "model/song/clip_iterators.h"
#include "model/song/song.h"
#include "modulation/automation/auto_param.h"
#include "modulation/params/param.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_set.h"
#include "modulation/patch/patch_cable_set.h"
#include "playback/playback_handler.h"
#include "processing/audio_output.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"
#include "util/functions.h"
#include "util/param_functions.h"
#include "util/structural_edit_probe.h"

#include <algorithm>
#include <limits>
#include <span>
#include <string>
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
	// A backup's owner by its address, or (not yet adopted) itself.
	const auto engineOf = [&](deluge::project::OwnerId owner) -> ModControllableAudio* {
		if (owner.kind == deluge::project::OwnerId::Kind::DRUM) {
			Drum* drum = deluge::bridge::drumWithId(song, owner.id);
			return drum != nullptr && drum->type == DrumType::SOUND ? static_cast<SoundDrum*>(drum) : nullptr;
		}
		Output* output = song.outputForId(owner.id);
		return output != nullptr ? static_cast<ModControllableAudio*>(output->toModControllable()) : nullptr;
	};
	for (auto& [key, paramManager] : song.live.param_backup.map) {
		if (ModControllableAudio* owner = engineOf(key.owner)) {
			visit(*owner, paramManager);
		}
	}
	for (deluge::bridge::ParamBackup::Pending& entry : song.live.param_backup.pending) {
		if (Output* output = entry.owner.output()) {
			visit(*static_cast<ModControllableAudio*>(output->toModControllable()), entry.params);
		}
		else if (Drum* drum = entry.owner.drum(); drum->type == DrumType::SOUND) {
			visit(*static_cast<SoundDrum*>(drum), entry.params);
		}
	}
}

namespace {

/// @brief Gives @p owner its type, when its file saved none, from whether any of its param managers could sound.
void resolve(Song& song, ModControllableAudio& owner, bool couldSound) {
	if (!owner.delayTypePending) {
		return;
	}
	owner.delayTypePending = false;
	const deluge::effects::DelayType type = owner.effects().delay.type;
	deluge::effects::DelayType resolved = type;
	if (!couldSound) {
		resolved = deluge::effects::DelayType::OFF;
	}
	else if (type == deluge::effects::DelayType::OFF) {
		resolved = deluge::effects::DelayType::DIGITAL;
	}
	if (resolved != type) {
		// Through the owner's slot or chain where it has one, so the model's record holds the type too.
		deluge::serialization::Value value =
		    deluge::serialization::Value::makeScalar(std::string(deluge::effects::to_string(resolved)));
		// In the song being settled, which a load has not made current yet: each owner plays its record already.
		deluge::edits::changeEffect(song, owner, "delay/type", std::move(value));
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
		resolve(song, *owner, could);
	}
}

} // namespace

void onDelayParamModified(const ModelStackWithAutoParam& modelStack, int32_t oldValue, bool automatedBefore) {
	const bool rate =
	    (modelStack.kind == params::Kind::PATCHED && modelStack.paramId == params::GLOBAL_DELAY_RATE)
	    || (modelStack.kind == params::Kind::UNPATCHED_GLOBAL && modelStack.paramId == params::UNPATCHED_DELAY_RATE);
	if (rate && modelStack.modControllable != nullptr && currentSong != nullptr) {
		auto& owner = *static_cast<ModControllableAudio*>(modelStack.modControllable);
		if (owner.effects().delay.type != deluge::effects::DelayType::OFF) {
			refreshDelayReserve(*currentSong, owner);
		}
		return;
	}
	bool reaches = false;
	switch (modelStack.kind) {
	case params::Kind::PATCHED:
		reaches = modelStack.paramId == params::GLOBAL_DELAY_FEEDBACK;
		break;
	case params::Kind::UNPATCHED_GLOBAL:
		reaches = modelStack.paramId == params::UNPATCHED_DELAY_AMOUNT;
		break;
	case params::Kind::PATCH_CABLE: {
		ParamDescriptor destination;
		PatchSource source{};
		PatchCableSet::dissectParamId(modelStack.paramId, &destination, &source);
		reaches = destination.getJustTheParam() == params::GLOBAL_DELAY_FEEDBACK;
		break;
	}
	default:
		break;
	}
	if (!reaches || modelStack.modControllable == nullptr || modelStack.paramManager == nullptr) {
		return;
	}
	auto& owner = *static_cast<ModControllableAudio*>(modelStack.modControllable);
	if (owner.effects().delay.type != deluge::effects::DelayType::OFF) {
		return;
	}
	ParamManager& paramManager = *modelStack.paramManager;
	const bool sound = owner.asSound() != nullptr;
	if (!delayCouldSound(paramManager, sound)
	    || delayCouldSound(paramManager, sound,
	                       {.param = modelStack.autoParam, .value = oldValue, .automated = automatedBefore})) {
		return;
	}
	// The Edit a press of the feedback knob makes: the press cycle's step from OFF. It is applied here, inside the
	// change that lifted the feedback (itself an Edit when it is the knob's), as an undo step of its own; it reads and
	// writes only the owner's delay type, so the outer Edit, still applying, is not disturbed by it.
	deluge::edits::changeEffect(owner, "delay/type",
	                            deluge::serialization::Value::makeScalar(std::string(deluge::effects::to_string(
	                                deluge::effects::next_delay_type(deluge::effects::DelayType::OFF)))));
}

namespace {

/// @return The tick inverse @p song's synced delays play at: the external clock's when it drives playback, otherwise
/// @p song's own tempo (a song being loaded is not yet the current one).
uint32_t tickInverseFor(Song& song) {
	return playbackHandler.isExternalClockActive() ? playbackHandler.getTimePerInternalTickInverse(true)
	                                               : song.project.transport.inverse_samples_per_timer_tick;
}

/// @return The tape, in frames, a delay whose rate param lives in @p paramManager needs with @p settings.
std::size_t reserveIn(ParamManager& paramManager, bool sound, const deluge::effects::DelayState& settings,
                      uint32_t timePerInternalTickInverse) {
	if (!paramManager.containsAnyMainParamCollections()) {
		return 0;
	}
	// The render's rate with no cable or modulation on it.
	const int32_t value = sound ? paramManager.getPatchedParamSet()->getValue(params::GLOBAL_DELAY_RATE)
	                            : paramManager.getUnpatchedParamSet()->getValue(params::UNPATCHED_DELAY_RATE);
	const int32_t userRate =
	    getFinalParameterValueExp(paramNeutralValues[params::GLOBAL_DELAY_RATE], cableToExpParamShortcut(value));
	const int32_t rate = deluge::effects::synced_delay_rate(userRate, settings, timePerInternalTickInverse);
	return deluge::effects::tape_shape(static_cast<uint32_t>(rate), true).frames;
}

/// @brief Sets each of @p reserves' owners its reserve inside one section, then maintains the arena.
void applyReserves(std::span<const std::pair<ModControllableAudio*, std::size_t>> reserves) {
	{
		AudioExcludedSection section; // the render reads the arena's slots
		for (const auto& [owner, frames] : reserves) {
			owner->core().delay().setReserve(frames);
		}
		deluge::noteStructuralEdit(deluge::StructuralContainer::DELAY_TAPE);
	}
	maintainDelayArena();
}

} // namespace

std::size_t delayReserveFrames(Song& song, ModControllableAudio& owner) {
	const deluge::effects::DelayState& settings = owner.effects().delay;
	if (settings.type == deluge::effects::DelayType::OFF) {
		return 0;
	}
	const uint32_t inverse = tickInverseFor(song);
	const bool sound = owner.asSound() != nullptr;
	std::size_t frames = 0;
	forEachDelayParamManager(song, [&](ModControllableAudio& candidate, ParamManager& paramManager) {
		if (&candidate == &owner) {
			frames = std::max(frames, reserveIn(paramManager, sound, settings, inverse));
		}
	});
	return frames;
}

void refreshDelayReserve(Song& song, ModControllableAudio& owner) {
	const std::pair<ModControllableAudio*, std::size_t> reserve{&owner, delayReserveFrames(song, owner)};
	applyReserves({&reserve, 1});
}

void refreshDelayReserves(Song& song) {
	const uint32_t inverse = tickInverseFor(song);
	std::vector<std::pair<ModControllableAudio*, std::size_t>> reserves;
	forEachDelayParamManager(song, [&](ModControllableAudio& owner, ParamManager& paramManager) {
		const deluge::effects::DelayState& settings = owner.effects().delay;
		if (settings.type == deluge::effects::DelayType::OFF) {
			return;
		}
		const std::size_t frames = reserveIn(paramManager, owner.asSound() != nullptr, settings, inverse);
		auto found = std::ranges::find_if(reserves, [&](const auto& entry) { return entry.first == &owner; });
		if (found == reserves.end()) {
			reserves.emplace_back(&owner, frames);
		}
		else {
			found->second = std::max(found->second, frames);
		}
	});
	if (!reserves.empty()) {
		applyReserves(reserves);
	}
}

void settleLoadedDelays(Song& song) {
	resolvePendingDelayTypes(song);
	refreshDelayReserves(song);
}

void resolvePendingDelayTypes(Song& song) {
	resolveWhere(song, [](const ModControllableAudio&) { return true; });
}

} // namespace deluge::model
