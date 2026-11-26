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

/// @file
/// @brief A sample's detected pitches, kept so a detection with the same search answers without reading the card.

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace deluge::sample {

/// @brief The search a pitch detection ran: what a remembered result answers.
struct PitchLimits {
	float minFreqHz; ///< lowest fundamental searched, Hz
	float maxFreqHz; ///< highest fundamental searched, Hz
	bool primeTest;  ///< whether the prime test ran
	bool operator==(const PitchLimits&) const = default;
};

/// @brief A sample's detected pitches, one per search, as its sidecar's PTCH section stores them: a count (u32,
///        at most kMax), then per result its lowest and highest fundamental searched and its result (f32 Hz each;
///        0 is a detection that found none) and flags (u32, bit 0: the prime test ran), little-endian.
class PitchResults {
public:
	static constexpr size_t kMax = 4;
	static constexpr uint32_t kTag = 'P' | ('T' << 8) | ('C' << 16) | (uint32_t{'H'} << 24);
	static constexpr size_t kEntryBytes = 16;
	static constexpr size_t kMaxBytes = 4 + kMax * kEntryBytes;

	/// @brief The remembered result for @p limits, in Hz (0: the detection found no fundamental).
	[[nodiscard]] constexpr std::optional<float> find(const PitchLimits& limits) const {
		for (const std::optional<Result>& r : results_) {
			if (r && r->limits == limits) {
				return r->hz;
			}
		}
		return std::nullopt;
	}
	/// @brief Remember @p hz for @p limits, replacing a result for the same limits, else the oldest.
	constexpr void remember(const PitchLimits& limits, float hz) {
		unwritten_ = true;
		for (std::optional<Result>& r : results_) {
			if (r && r->limits == limits) {
				r->hz = hz;
				return;
			}
		}
		results_[next_] = Result{.limits = limits, .hz = hz};
		next_ = (next_ + 1) & (kMax - 1);
	}
	/// @brief The PTCH section's bytes, valid until the next encode().
	[[nodiscard]] constexpr std::span<const uint8_t> encode() {
		size_t count = 0;
		size_t at = 4;
		for (const std::optional<Result>& r : results_) {
			if (!r) {
				continue;
			}
			put(at, std::bit_cast<uint32_t>(r->limits.minFreqHz));
			put(at + 4, std::bit_cast<uint32_t>(r->limits.maxFreqHz));
			put(at + 8, std::bit_cast<uint32_t>(r->hz));
			put(at + 12, r->limits.primeTest ? 1u : 0u);
			at += kEntryBytes;
			count++;
		}
		put(0, static_cast<uint32_t>(count));
		return std::span<const uint8_t>{encoded_}.first(at);
	}
	/// @brief Read a PTCH section; nothing if it is malformed (a count over kMax, or a length that disagrees).
	[[nodiscard]] static constexpr std::optional<PitchResults> decode(std::span<const uint8_t> bytes) {
		if (bytes.size() < 4) {
			return std::nullopt;
		}
		const uint32_t count = get(bytes, 0);
		if (count > kMax || bytes.size() != 4 + count * kEntryBytes) {
			return std::nullopt;
		}
		PitchResults out;
		for (size_t i = 0; i < count; i++) {
			const size_t at = 4 + i * kEntryBytes;
			out.results_[i] = Result{.limits = {.minFreqHz = std::bit_cast<float>(get(bytes, at)),
			                                    .maxFreqHz = std::bit_cast<float>(get(bytes, at + 4)),
			                                    .primeTest = (get(bytes, at + 12) & 1) != 0},
			                         .hz = std::bit_cast<float>(get(bytes, at + 8))};
		}
		out.next_ = static_cast<uint8_t>(count & (kMax - 1));
		return out;
	}
	[[nodiscard]] constexpr bool empty() const {
		for (const std::optional<Result>& r : results_) {
			if (r) {
				return false;
			}
		}
		return true;
	}
	/// @brief Whether a result was remembered since the last markWritten().
	[[nodiscard]] constexpr bool unwritten() const { return unwritten_; }
	constexpr void markWritten() { unwritten_ = false; }
	/// @brief Mark them unwritten again: a write that took them failed.
	constexpr void markUnwritten() { unwritten_ = !empty(); }

private:
	struct Result {
		PitchLimits limits;
		float hz;
	};
	constexpr void put(size_t at, uint32_t v) {
		for (size_t i = 0; i < 4; i++) {
			encoded_[at + i] = static_cast<uint8_t>(v >> (8 * i));
		}
	}
	static constexpr uint32_t get(std::span<const uint8_t> bytes, size_t at) {
		uint32_t v = 0;
		for (size_t i = 0; i < 4; i++) {
			v |= uint32_t{bytes[at + i]} << (8 * i);
		}
		return v;
	}

	std::array<std::optional<Result>, kMax> results_{};
	std::array<uint8_t, kMaxBytes> encoded_{};
	uint8_t next_{0};
	bool unwritten_{false};
};

} // namespace deluge::sample
