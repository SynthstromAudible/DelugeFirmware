// Reverb model implementation
//
// Written by Jezar at Dreampoint, June 2000
// http://www.dreampoint.co.uk
// This code is public domain

/*
 * Copyright © 2015-2023 Synthstrom Audible Limited
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

#include "dsp/reverb/freeverb/freeverb.hpp"
#include <limits>

namespace deluge::dsp::reverb {

Freeverb::Freeverb() {
	// Tie the components to their buffers
	combL[0].setBuffer(bufcombL1);
	combR[0].setBuffer(bufcombR1);
	combL[1].setBuffer(bufcombL2);
	combR[1].setBuffer(bufcombR2);
	combL[2].setBuffer(bufcombL3);
	combR[2].setBuffer(bufcombR3);
	combL[3].setBuffer(bufcombL4);
	combR[3].setBuffer(bufcombR4);
	combL[4].setBuffer(bufcombL5);
	combR[4].setBuffer(bufcombR5);
	combL[5].setBuffer(bufcombL6);
	combR[5].setBuffer(bufcombR6);
	combL[6].setBuffer(bufcombL7);
	combR[6].setBuffer(bufcombR7);
	combL[7].setBuffer(bufcombL8);
	combR[7].setBuffer(bufcombR8);
	allpassL[0].setBuffer(bufallpassL1);
	allpassR[0].setBuffer(bufallpassR1);
	allpassL[1].setBuffer(bufallpassL2);
	allpassR[1].setBuffer(bufallpassR2);
	allpassL[2].setBuffer(bufallpassL3);
	allpassR[2].setBuffer(bufallpassR3);
	allpassL[3].setBuffer(bufallpassL4);
	allpassR[3].setBuffer(bufallpassR4);

	// Set default values
	allpassL[0].setFeedback(0.5f);
	allpassR[0].setFeedback(0.5f);
	allpassL[1].setFeedback(0.5f);
	allpassR[1].setFeedback(0.5f);
	allpassL[2].setFeedback(0.5f);
	allpassR[2].setFeedback(0.5f);
	allpassL[3].setFeedback(0.5f);
	allpassR[3].setFeedback(0.5f);
	setWet(initialwet);
	setRoomSize(initialroom);
	setDry(initialdry);
	setDamping(initialdamp);
	setWidth(initialwidth);

	// Buffer will be full of rubbish - so we MUST mute them
	mute();
}

void Freeverb::mute() {
	for (int32_t i = 0; i < numcombs; i++) {
		combL[i].mute();
		combR[i].mute();
	}
	for (int32_t i = 0; i < numallpasses; i++) {
		allpassL[i].mute();
		allpassR[i].mute();
	}
}

void Freeverb::update() {
	// Recalculate internal values after parameter change

	wet1 = wet * (width / 2 + 0.5f);
	wet2 = (((float)1 - width) / 2) / (width / 2 + 0.5f) * std::numeric_limits<int32_t>::max();

	gain = fixedgain * std::numeric_limits<int32_t>::max();

	for (size_t i = 0; i < numcombs; i++) {
		combL[i].setFeedback(roomsize * std::numeric_limits<int32_t>::max());
		combR[i].setFeedback(roomsize * std::numeric_limits<int32_t>::max());
	}

	for (size_t i = 0; i < numcombs; i++) {
		combL[i].setDamp(damp);
		combR[i].setDamp(damp);
	}
}

} // namespace deluge::dsp::reverb
