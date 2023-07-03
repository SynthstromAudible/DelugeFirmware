#pragma once
#include "model/clip/audio_clip.h"
#include "gui/views/audio_clip_view.h"
#include "model/model_stack.h"
#include "model/sample/sample.h"
#include "gui/menu_item/selection.h"
#include "playback/playback_handler.h"
#include "model/song/song.h"
#include "gui/ui/sound_editor.h"

namespace menu_item::audio_clip {
class Reverse final : public Selection {
public:
	using Selection::Selection;

	void readCurrentValue() {
		soundEditor.currentValue = ((AudioClip*)currentSong->currentClip)->sampleControls.reversed;
	}
	void writeCurrentValue() {
		AudioClip* clip = (AudioClip*)currentSong->currentClip;
		bool active = (playbackHandler.isEitherClockActive() && currentSong->isClipActive(clip) && clip->voiceSample);

		clip->unassignVoiceSample();

		clip->sampleControls.reversed = soundEditor.currentValue;

		if (clip->sampleHolder.audioFile) {
			if (clip->sampleControls.reversed) {
				uint64_t lengthInSamples = ((Sample*)clip->sampleHolder.audioFile)->lengthInSamples;
				if (clip->sampleHolder.endPos > lengthInSamples) {
					clip->sampleHolder.endPos = lengthInSamples;
				}
			}

			clip->sampleHolder.claimClusterReasons(clip->sampleControls.reversed);

			if (active) {
				char modelStackMemory[MODEL_STACK_MAX_SIZE];
				ModelStackWithTimelineCounter* modelStack =
				    currentSong->setupModelStackWithCurrentClip(modelStackMemory);

				clip->resumePlayback(modelStack, true);
			}

			uiNeedsRendering(&audioClipView, 0xFFFFFFFF, 0);
		}
	}
};
} // namespace menu_item::audio_clip
