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

#include "gui/ui/sound_target.h"

#include "gui/ui/ui.h"
#include "instruments/classic/words.h"
#include "model/clip/audio_clip.h"
#include "model/clip/instrument_clip.h"
#include "model/edits/edit.h"
#include "model/edits/param_address_for.h"
#include "model/edits/set_param_value.h"
#include "model/edits/setting_edit.h"
#include "model/instrument/instrument.h"
#include "model/instrument/kit.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/song/song.h"
#include "modulation/automation/auto_param.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_set.h"
#include "processing/sound/sound.h"
#include "processing/sound/source_resources.h"
#include "storage/multi_range/multi_range.h"

#include <iterator>
#include <optional>
#include <string>

namespace params = deluge::modulation::params;

namespace deluge::gui {

void retargetRangeAfterRetype(SoundTarget& target, RangesRetype retype) {
	if (!retype.reallocated) {
		return;
	}
	if (retype.cleared) {
		target.rangeIndex = 0;
	}
	if (target.rangeIndex >= 0 && target.rangeIndex < std::ssize(target.source->ranges)) {
		target.range = target.source->ranges.getElement(target.rangeIndex);
	}
}

void setOscTypeFor(SoundTarget& target, deluge::synth::OscType type) {
	const RangesRetypeWatch watch{*target.source};
	deluge::edits::settingFor(
	    *target.sound, deluge::edits::oscSettingPath(target.sourceIndex, "type"),
	    deluge::serialization::Value::makeScalar(std::string{deluge::instruments::classic::to_string(type)}));
	retargetRangeAfterRetype(target, watch.result());
}

ModelStackWithThreeMainThings* modelStackFor(const SoundTarget& target, void* memory) {
	InstrumentClip* clip = getCurrentInstrumentClip();
	Instrument* instrument = getCurrentInstrument();

	if (!rootUIIsClipMinderScreen()) {
		return currentSong->setupModelStackWithSongAsTimelineCounter(memory);
	}
	if (instrument->type == OutputType::KIT && clip->affectEntire) {
		ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(memory);
		return modelStack->addOtherTwoThingsButNoNoteRow(target.mod, target.params);
	}
	NoteRow* noteRow = nullptr;
	int32_t noteRowIndex;
	if (instrument->type == OutputType::KIT) {
		Drum* selectedDrum = static_cast<Kit*>(instrument)->selectedDrum;
		if (selectedDrum) {
			noteRow = clip->getNoteRowForDrum(selectedDrum, &noteRowIndex);
		}
	}
	return setupModelStackWithThreeMainThingsIncludingNoteRow(memory, currentSong, getCurrentClip(), noteRowIndex,
	                                                          noteRow, target.mod, target.params);
}

AudioFileHolder* audioFileHolderFor(const SoundTarget& target) {
	if (getCurrentClip()->type == ClipType::AUDIO) {
		return &getCurrentAudioClip()->sampleHolder;
	}
	return target.range->getAudioFileHolder();
}

void cutSound(const SoundTarget& target) {
	if (getCurrentClip()->type == ClipType::AUDIO) {
		getCurrentAudioClip()->unassignVoiceSample(false);
	}
	else {
		target.sound->killAllVoices();
	}
}

void resetSilentOscVolume(const SoundTarget& target) {
	alignas(ModelStackWithAutoParam) char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithThreeMainThings* modelStack = modelStackFor(target, modelStackMemory);
	ParamCollectionSummary* summary = modelStack->paramManager->getPatchedParamSetSummary();
	PatchedParamSet* paramSet = (PatchedParamSet*)summary->paramCollection;
	int32_t paramId = params::LOCAL_OSC_A_VOLUME + target.sourceIndex;
	ModelStackWithAutoParam* modelStackWithParam =
	    modelStack->addParam(paramSet, summary, paramId, &paramSet->params[paramId]);

	// Only the current ParamManager's: another one holding the same sound keeps its own level.
	if (!modelStackWithParam->autoParam->containsSomething(*modelStackWithParam->doc(), -2147483648)) {
		if (const std::optional<deluge::project::ParamAddress> address =
		        deluge::edits::addressFor(*modelStackWithParam)) {
			deluge::edits::Context ctx{*currentSong, currentSong->project};
			deluge::edits::SetParamValue edit{*address,
			                                  2147483647,
			                                  0,
			                                  0,
			                                  deluge::edits::SetParamValue::Resolve::EXACT,
			                                  true,
			                                  deluge::edits::SetParamValue::Write::SETUP};
			deluge::edits::apply(ctx, edit);
		}
	}
}

} // namespace deluge::gui
