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
#include "gui/menu_item/toggle.h"
#include "gui/ui/sound_editor.h"
#include "gui/views/audio_clip_view.h"
#include "model/clip/audio_clip.h"
#include "model/model_stack.h"
#include "model/sample/sample.h"
#include "model/song/song.h"
#include "playback/playback_handler.h"

namespace deluge::gui::menu_item::audio_clip {
class Reverse final : public Toggle {
public:
	using Toggle::Toggle;

	void readCurrentValue() override { this->setValue(getCurrentAudioClip()->sampleControls.reversed); }
	void writeCurrentValue() override {
		auto* clip = getCurrentAudioClip();
		bool active = (playbackHandler.isEitherClockActive() && currentSong->isClipActive(clip) && clip->voiceSample);

		clip->unassignVoiceSample();

		clip->sampleControls.reversed = this->getValue();

		if (clip->sampleHolder.audioFile != nullptr) {
			if (clip->sampleControls.reversed) {
				uint64_t lengthInSamples = (static_cast<Sample*>(clip->sampleHolder.audioFile))->lengthInSamples;
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
} // namespace deluge::gui::menu_item::audio_clip
