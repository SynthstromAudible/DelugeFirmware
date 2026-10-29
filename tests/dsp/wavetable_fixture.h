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

#include "util/fixedpoint.h"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <span>
#include <vector>

/// @file
/// @brief A test-local WAV decoder producing the samples the wavetable loader forms, for specs that need real cycles.
namespace wavetable_fixture {

/// @brief A mono WAV as the wavetable loader sees it.
struct Fixture {
	int32_t cycle_size;           ///< The `clm ` tag's cycle size, else 2048; not clamped to the length.
	std::vector<int32_t> samples; ///< Each sample left-justified to Q31, as the loader forms its 32-bit value.
};

namespace detail {

[[nodiscard]] inline uint16_t u16(std::span<const uint8_t> b, std::size_t at) {
	return static_cast<uint16_t>(b[at] | (b[at + 1] << 8));
}

[[nodiscard]] inline uint32_t u32(std::span<const uint8_t> b, std::size_t at) {
	return static_cast<uint32_t>(b[at]) | (static_cast<uint32_t>(b[at + 1]) << 8)
	       | (static_cast<uint32_t>(b[at + 2]) << 16) | (static_cast<uint32_t>(b[at + 3]) << 24);
}

/// @brief The `clm ` payload's cycle size: `<!>` then exactly four ASCII digits, at least 1; else nothing.
[[nodiscard]] inline std::optional<int32_t> clm_cycle_size(std::span<const uint8_t> payload) {
	if (payload.size() < 7 || payload[0] != '<' || payload[1] != '!' || payload[2] != '>') {
		return std::nullopt;
	}
	int32_t number = 0;
	for (std::size_t i = 3; i < 7; ++i) {
		if (payload[i] < '0' || payload[i] > '9') {
			return std::nullopt;
		}
		number = (number * 10) + (payload[i] - '0');
	}
	return number >= 1 ? std::optional<int32_t>{number} : std::nullopt;
}

/// @brief One sample of @p bits (PCM) or IEEE float 32 at @p p, as the loader forms its 32-bit value: a PCM sample's
/// bytes in the top of the word (8-bit unsigned with its top bit flipped), a float through q31_from_float.
[[nodiscard]] inline int32_t sample_value(std::span<const uint8_t> p, int bits, bool is_float) {
	if (is_float) {
		return q31_from_float(std::bit_cast<float>(u32(p, 0)));
	}
	switch (bits) {
	case 8:
		return static_cast<int32_t>((static_cast<uint32_t>(p[0]) << 24) + 0x80000000U);
	case 16:
		return static_cast<int32_t>(static_cast<uint32_t>(u16(p, 0)) << 16);
	case 24:
		return static_cast<int32_t>(
		    (static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16))
		    << 8);
	default:
		return static_cast<int32_t>(u32(p, 0));
	}
}

} // namespace detail

/// @brief Reads @p path: PCM 8 (unsigned), 16, 24 and 32 bits and IEEE float 32, plain or WAVE_FORMAT_EXTENSIBLE.
/// @return The fixture, or nothing for a file that is not a mono WAV of one of those formats.
[[nodiscard]] inline std::optional<Fixture> load(const std::filesystem::path& path) {
	std::ifstream in{path, std::ios::binary};
	const std::vector<uint8_t> file{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
	const std::span<const uint8_t> b{file};
	if (b.size() < 12 || detail::u32(b, 0) != 0x46464952U || detail::u32(b, 8) != 0x45564157U) { // "RIFF", "WAVE"
		return std::nullopt;
	}
	int bits = 0;
	int channels = 0;
	bool is_float = false;
	int32_t cycle_size = 2048;
	std::span<const uint8_t> data;
	for (std::size_t at = 12; at + 8 <= b.size();) {
		const uint32_t id = detail::u32(b, at);
		const uint32_t size = detail::u32(b, at + 4);
		const std::span<const uint8_t> payload = b.subspan(at + 8, std::min<std::size_t>(size, b.size() - at - 8));
		if (id == 0x20746d66U && payload.size() >= 16) { // "fmt "
			uint16_t format = detail::u16(payload, 0);
			if (format == 0xFFFE && payload.size() >= 26) {
				format = detail::u16(payload, 24); // the subformat GUID's first two bytes
			}
			channels = detail::u16(payload, 2);
			bits = detail::u16(payload, 14);
			if (format != 1 && format != 3) {
				return std::nullopt;
			}
			is_float = format == 3;
		}
		else if (id == 0x206d6c63U) { // "clm "
			if (const auto size_from_tag = detail::clm_cycle_size(payload)) {
				cycle_size = *size_from_tag;
			}
		}
		else if (id == 0x61746164U) { // "data"
			data = payload;
		}
		at += 8 + size + (size & 1);
	}
	if (channels != 1 || data.empty() || (is_float && bits != 32) || (!is_float && bits % 8 != 0) || bits > 32) {
		return std::nullopt;
	}
	const auto bytes = static_cast<std::size_t>(bits / 8);
	Fixture f{.cycle_size = cycle_size, .samples = {}};
	f.samples.reserve(data.size() / bytes);
	for (std::size_t at = 0; at + bytes <= data.size(); at += bytes) {
		f.samples.push_back(detail::sample_value(data.subspan(at, bytes), bits, is_float));
	}
	return f;
}

/// @brief The repository's root, from the DELUGE_REPO_ROOT definition the spec targets get.
[[nodiscard]] inline std::filesystem::path repo_root() {
	return std::filesystem::path{DELUGE_REPO_ROOT};
}

} // namespace wavetable_fixture
