/*
 * Copyright © 2015-2026 Synthstrom Audible Limited
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

#include "dsp-next/fixed/delay/delay_processor.h"

namespace deluge::dsp::fixed {

namespace {

/// @brief The block's result, read from the kernel's State and cleared there for the next block.
DelayBlockResult takeResult(Delay::State& s) {
	const DelayBlockResult result{.wrapped = s.wrapped, .secondary_written = s.secondary_written};
	s.wrapped = false;
	s.secondary_written = 0;
	return result;
}

} // namespace

DelayBlockResult DelayProcessor::process(std::span<StereoSample> block, DelayTape* primary, DelayTape* secondary,
                                         const DelayBlock& settings) {
	delay_.configure({.rate = static_cast<uint32_t>(settings.rate),
	                  .feedback = settings.feedback,
	                  .analog = settings.analog,
	                  .analog_saturation = settings.analog_saturation,
	                  .ping_pong = settings.ping_pong,
	                  .render_in_stereo = settings.render_in_stereo,
	                  .primary = primary,
	                  .secondary = secondary});
	delay_.renderBlock(block, block);
	return delay_.on<&takeResult>();
}

void DelayProcessor::resetHighPass() {
	delay_.on<&Delay::reset_high_pass>();
}

} // namespace deluge::dsp::fixed
