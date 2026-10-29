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

#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>

/// @brief Owns one band's 16-bit samples: a block of external SDRAM, freed on destruction, shrunk only in place.
class BandSamples {
public:
	BandSamples() = default;
	/// @brief Room for @p num_samples samples, or an empty owner when the allocation fails.
	[[nodiscard]] static BandSamples allocate(std::size_t num_samples);
	BandSamples(BandSamples&& other) noexcept : samples_{std::exchange(other.samples_, nullptr)} {}
	BandSamples& operator=(BandSamples&& other) noexcept {
		std::swap(samples_, other.samples_);
		return *this;
	}
	BandSamples(const BandSamples&) = delete;
	BandSamples& operator=(const BandSamples&) = delete;
	~BandSamples();
	/// @brief The first sample: cycle 0's, the band's dataAccessAddress.
	[[nodiscard]] int16_t* data() const { return samples_; }
	[[nodiscard]] explicit operator bool() const { return samples_ != nullptr; }
	/// @brief Keeps the first @p num_samples and gives the rest back to the heap; the block does not move, so
	///        dataAccessAddress stays valid.
	void shrink_to(std::size_t num_samples);

private:
	explicit BandSamples(int16_t* samples) : samples_{samples} {}

	int16_t* samples_ = nullptr;
};
static_assert(sizeof(BandSamples) == sizeof(void*), "a band's header lives in SRAM: keep it one pointer");
