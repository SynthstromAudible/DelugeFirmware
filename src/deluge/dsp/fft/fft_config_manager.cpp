/*
 * Copyright © 2021-2023 Synthstrom Audible Limited
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

#include "dsp/fft/fft_config_manager.h"
#include "memory/general_memory_allocator.h"
#include "processing/engines/audio_engine.h"

#include <array>
#include <cstddef>
#include <new>
#include <span>

namespace FFTConfigManager {

namespace {

namespace fft = deluge::dsp::fixed::fft;

/// Each size's transform, null until first used. The transform object lives at the front of its own block, before the
/// tables and scratch it lends them, so the cache itself is one pointer per size.
std::array<const fft::RealFft*, fft::kMaxMagnitude + 1> transforms{};

/// Where a block's tables start: past the transform object, at the tables' alignment.
constexpr std::size_t kTablesOffset = (sizeof(fft::RealFft) + alignof(fft::Complex) - 1) & ~(alignof(fft::Complex) - 1);

} // namespace

std::size_t blockBytes(int32_t magnitude) {
	return kTablesOffset + fft::storage_bytes(magnitude);
}

const fft::RealFft* getConfig(int32_t magnitude) {
	if (magnitude < fft::kMinMagnitude || magnitude > fft::kMaxMagnitude) {
		return nullptr;
	}
	if (transforms[magnitude] == nullptr) {
		// Allocated once and never freed: every later transform of this size reuses it.
		const std::size_t table_bytes = fft::storage_bytes(magnitude);
		auto* block = static_cast<std::byte*>(delugeAlloc(blockBytes(magnitude), true));
		if (block == nullptr) {
			return nullptr;
		}
		const fft::Layout layout = fft::lay_out({block + kTablesOffset, table_bytes}, magnitude);
		fft::Twiddles twiddles = layout.twiddles;
		fft::factor(twiddles);
		fft::generate_stage_twiddles(twiddles);
		AudioEngine::routineWithClusterLoading();
		fft::generate_super_twiddles(twiddles);
		transforms[magnitude] = new (block) fft::RealFft{twiddles, layout.scratch};
	}
	return transforms[magnitude];
}

} // namespace FFTConfigManager
