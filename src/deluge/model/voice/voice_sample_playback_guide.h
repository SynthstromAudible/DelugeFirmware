/*
 * Copyright © 2017-2023 Synthstrom Audible Limited
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

#include "definitions_cxx.hpp"
#include "model/sample/sample_playback_guide.h"

class Source;
class Sample;
class Voice;
class SampleCache;

class VoiceSamplePlaybackGuide final : public SamplePlaybackGuide {
public:
	VoiceSamplePlaybackGuide();
	void setupPlaybackBounds(bool reversed) override;
	bool shouldObeyLoopEndPointNow();
	int32_t getBytePosToStartPlayback(bool justLooped) override;
	int32_t getBytePosToEndOrLoopPlayback() override;
	LoopType getLoopingType(const Source& source) const;

	[[nodiscard]] uint32_t getLoopStartPlaybackAtByte() const override { return loopStartPlaybackAtByte; }
	[[nodiscard]] uint32_t getLoopEndPlaybackAtByte() const override {
		return loopEndPlaybackAtByte ? loopEndPlaybackAtByte : endPlaybackAtByte;
	}

	uint32_t loopStartPlaybackAtByte; // If no loop-start point defined, this will be the same as startPlaybackAtByte,
	                                  // so it can just be referred to when looping happens
	uint32_t loopEndPlaybackAtByte;   // 0 means disabled

	int32_t preRollSamples{0}; // Output samples of silence before playback starts (negative start offset)

	bool noteOffReceived;
};
