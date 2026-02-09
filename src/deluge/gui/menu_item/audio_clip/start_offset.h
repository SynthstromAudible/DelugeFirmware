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
#include "gui/menu_item/integer.h"
#include "gui/menu_item/velocity_encoder.h"
#include "model/clip/audio_clip.h"
#include "model/model_stack.h"
#include "model/song/song.h"
#include "playback/playback_handler.h"

namespace deluge::gui::menu_item::audio_clip {

class StartOffset final : public Integer {
public:
	static constexpr int32_t kResolution = 1024;
	static constexpr int32_t kShift = 21; // 31 - log2(1024) = 21

	using Integer::Integer;

	[[nodiscard]] int32_t getMaxValue() const override { return kResolution; }
	[[nodiscard]] int32_t getMinValue() const override { return -kResolution; }

	void readCurrentValue() override { this->setValue(getCurrentAudioClip()->startOffset >> kShift); }

	void writeCurrentValue() override {
		auto* clip = getCurrentAudioClip();
		int32_t v = this->getValue();
		if (v >= kResolution) {
			clip->startOffset = 2147483647;
		}
		else if (v <= -kResolution) {
			clip->startOffset = -2147483647 - 1;
		}
		else {
			clip->startOffset = v << kShift;
		}

		// If playing, resume so the tick shift takes effect immediately
		if (playbackHandler.isEitherClockActive() && currentSong->isClipActive(clip) && clip->voiceSample) {
			char modelStackMemory[MODEL_STACK_MAX_SIZE];
			ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
			clip->resumePlayback(modelStack, true);
		}
	}

	void selectEncoderAction(int32_t offset) override {
		Integer::selectEncoderAction(velocity_.getScaledOffset(offset));
	}

	[[nodiscard]] RenderingStyle getRenderingStyle() const override { return KNOB; }

private:
	mutable VelocityEncoder velocity_;
};

} // namespace deluge::gui::menu_item::audio_clip
