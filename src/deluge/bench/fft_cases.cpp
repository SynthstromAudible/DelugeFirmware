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

/// @file
/// @brief The FFT cases' buffers, setup and blocks.
/// @details Each block runs one transform of the case's size through the configuration the app uses.

#include "bench/cases.h"

#include "dsp-next/fixed/fft/real_fft.h"
#include "dsp-next/fixed/noise/noise.h"
#include "dsp/fft/fft_config_manager.h"
#include "memory/general_memory_allocator.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>

namespace deluge::bench {

namespace {

constexpr uint32_t kFftSeed = 0x5EED'0FF7;
constexpr int32_t kMaxMagnitude = 13;

/// @brief One size's transform and buffers, shared by its forward and inverse cases.
struct FftRig {
	const dsp::fixed::fft::RealFft* config = nullptr;
	std::span<int32_t> samples;               ///< 2^m samples: the forward's input, the inverse's output.
	std::span<dsp::fixed::fft::Complex> bins; ///< 2^(m-1)+1 bins: the forward's output, the inverse's input.
};

std::array<FftRig, kMaxMagnitude + 1> rigs{};

} // namespace

void FftCase::setup(std::size_t /*n*/) {
	const int32_t m = config_.magnitude;
	FftRig& rig = rigs[m];
	if (rig.config == nullptr) {
		rig.config = FFTConfigManager::getConfig(m);
		if (rig.config == nullptr) {
			std::printf("BENCH-ERROR fft_%s_%d: no transform\n", config_.inverse ? "inv" : "fwd", 1 << m);
			return;
		}
	}
	if (rig.samples.empty()) {
		const std::size_t num_samples = std::size_t{1} << m;
		const std::size_t num_bins = (num_samples / 2) + 1;
		rig.samples = {static_cast<int32_t*>(delugeAlloc(num_samples * sizeof(int32_t), false)), num_samples};
		rig.bins = {
		    static_cast<dsp::fixed::fft::Complex*>(delugeAlloc(num_bins * sizeof(dsp::fixed::fft::Complex), false)),
		    num_bins};
		dsp::fixed::SeededNoise noise{kFftSeed};
		if (config_.inverse) {
			for (std::size_t k = 0; k < num_bins; ++k) {
				rig.bins[k].r = noise.render() >> 8;
				rig.bins[k].i = noise.render() >> 8;
			}
		}
		else {
			for (std::size_t k = 0; k < num_samples; ++k) {
				rig.samples[k] = noise.render() >> 12;
			}
		}
	}
}

void FftCase::block(std::size_t /*n*/) {
	FftRig& rig = rigs[config_.magnitude];
	if (rig.config == nullptr) {
		return; // setup printed BENCH-ERROR
	}
	if (config_.inverse) {
		rig.config->inverse(rig.bins, rig.samples);
	}
	else {
		rig.config->forward(rig.samples, rig.bins);
	}
}

} // namespace deluge::bench
