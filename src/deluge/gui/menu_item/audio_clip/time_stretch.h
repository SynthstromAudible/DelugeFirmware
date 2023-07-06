/*
 * Copyright (c) 2014-2023 Synthstrom Audible Limited
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
class TimeStretch final : public Selection {
public:
	using Selection::Selection;

	void readCurrentValue() {
		soundEditor.currentValue = ((AudioClip*)currentSong->currentClip)->sampleControls.timeStretchEnabled;
	}
	void writeCurrentValue() {
		AudioClip* clip = (AudioClip*)currentSong->currentClip;
		bool active = (playbackHandler.isEitherClockActive() && currentSong->isClipActive(clip) && clip->voiceSample);

		clip->unassignVoiceSample();

		clip->sampleControls.timeStretchEnabled = soundEditor.currentValue;

		if (clip->sampleHolder.audioFile) {

			clip->sampleHolder.claimClusterReasons(clip->sampleControls.timeStretchEnabled);

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
