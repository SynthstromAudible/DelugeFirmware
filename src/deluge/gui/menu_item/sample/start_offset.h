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
#include "gui/menu_item/sample/utils.h"
#include "gui/menu_item/unpatched_param.h"
#include "gui/menu_item/velocity_encoder.h"
#include "gui/ui/sound_editor.h"
#include "model/voice/voice.h"
#include "modulation/params/param.h"
#include "modulation/params/param_set.h"
#include "processing/sound/sound.h"

namespace deluge::gui::menu_item::sample {

class StartOffset final : public UnpatchedParam {
public:
	static constexpr int32_t kResolution = 1024;
	static constexpr int32_t kShift = 21; // 31 - log2(1024) = 21

	StartOffset(l10n::String name, uint8_t source_id)
	    : UnpatchedParam(name, params::UNPATCHED_SAMPLE_START_OFFSET_A + source_id), source_id_{source_id} {}

	bool isRelevant(ModControllableAudio* modControllable, int32_t) override {
		return isSampleModeSample(modControllable, source_id_);
	}

	[[nodiscard]] int32_t getMaxValue() const override { return kResolution; }
	[[nodiscard]] int32_t getMinValue() const override { return -kResolution; }

	void readCurrentValue() override {
		int32_t q31 = soundEditor.currentParamManager->getUnpatchedParamSet()->getValue(getP());
		this->setValue(q31 >> kShift);
	}

	int32_t getFinalValue() override {
		int32_t v = this->getValue();
		if (v >= kResolution) {
			return 2147483647;
		}
		if (v <= -kResolution) {
			return -2147483648;
		}
		return v << kShift;
	}

	void writeCurrentValue() override {
		// Capture old param value before writing
		int32_t oldQ31 = soundEditor.currentParamManager->getUnpatchedParamSet()->getValue(getP());

		// Write the new value through the param system
		UnpatchedParam::writeCurrentValue();

		// For STRETCH mode voices, update the guide's tick shift immediately
		// so the time stretcher crossfades to the new position
		Sound* sound = soundEditor.currentSound;
		if (!sound || sound->sources[source_id_].repeatMode != SampleRepeatMode::STRETCH) {
			return;
		}

		int32_t newQ31 = soundEditor.currentParamManager->getUnpatchedParamSet()->getValue(getP());
		if (newQ31 == oldQ31) {
			return;
		}

		for (const auto& voicePtr : sound->voices()) {
			Voice& voice = *voicePtr;
			auto& guide = voice.guides[source_id_];
			if (guide.sequenceSyncLengthTicks > 0) {
				int32_t syncLen = static_cast<int32_t>(guide.sequenceSyncLengthTicks);
				int64_t oldTickShift = ((int64_t)oldQ31 * (int64_t)syncLen) >> 31;
				int64_t newTickShift = ((int64_t)newQ31 * (int64_t)syncLen) >> 31;
				// Normalize negative shifts to equivalent positive position
				if (oldTickShift < 0) {
					oldTickShift += syncLen;
				}
				if (newTickShift < 0) {
					newTickShift += syncLen;
				}
				int32_t delta = static_cast<int32_t>(newTickShift - oldTickShift);
				guide.sequenceSyncStartedAtTick -= delta;
				guide.wrapSyncPosition = (newQ31 != 0);
			}
		}
	}

	void selectEncoderAction(int32_t offset) override {
		UnpatchedParam::selectEncoderAction(velocity_.getScaledOffset(offset));
	}

	[[nodiscard]] RenderingStyle getRenderingStyle() const override { return KNOB; }

private:
	uint8_t source_id_;
	mutable VelocityEncoder velocity_;
};
} // namespace deluge::gui::menu_item::sample
