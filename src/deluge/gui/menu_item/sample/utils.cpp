#include "gui/menu_item/sample/utils.h"

#include "gui/menu_item/setting_edit.h"
#include "processing/sound/sound.h"
#include "processing/source.h"

#include <model/clip/audio_clip.h>
#include <model/song/song.h>

bool isSampleModeSample(ModControllableAudio* modControllable, int32_t whichThing) {
	const auto sound = static_cast<Sound*>(modControllable);
	Source& source = sound->sources[whichThing];

	return sound->getSynthMode() == ::SynthMode::SUBTRACTIVE && source.oscType == OscType::SAMPLE
	       && source.hasAtLeastOneAudioFileLoaded();
}

SampleControls& getCurrentSampleControls(int32_t whichThing) {
	if (AudioClip* audioClip = getCurrentAudioClip(); audioClip != nullptr) {
		return audioClip->sampleControls;
	}

	Source& source = soundEditor.currentSound->sources[whichThing];
	return source.sampleControls;
}

void setCurrentInterpolationMode(int32_t whichThing, InterpolationMode mode) {
	if (AudioClip* audioClip = getCurrentAudioClip(); audioClip != nullptr) {
		deluge::edits::audioClipSettingFor(
		    *audioClip, "linearInterpolation",
		    deluge::serialization::Value::makeNumber(mode == InterpolationMode::LINEAR ? 1 : 0));
		return;
	}
	deluge::gui::menu_item::settingFor(*soundEditor.currentSound,
	                                   deluge::gui::menu_item::oscSettingPath(whichThing, "linearInterpolation"),
	                                   deluge::serialization::Value::makeNumber(mode == InterpolationMode::LINEAR));
}

void setCurrentPitchAndSpeedIndependent(int32_t whichThing, bool independent) {
	if (AudioClip* audioClip = getCurrentAudioClip(); audioClip != nullptr) {
		deluge::edits::audioClipSettingFor(*audioClip, "pitchSpeedIndependent",
		                                   deluge::serialization::Value::makeNumber(independent ? 1 : 0));
		return;
	}
	deluge::gui::menu_item::settingFor(*soundEditor.currentSound,
	                                   deluge::gui::menu_item::oscSettingPath(whichThing, "timeStretchEnable"),
	                                   deluge::serialization::Value::makeNumber(independent));
}
