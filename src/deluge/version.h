#pragma once
#include <compare>
#include <cstdint>

struct SemVer {
	uint8_t major;
	uint8_t minor;
	uint8_t patch;

	auto operator<=>(const SemVer&) const = default;
};

// clang-format off
constexpr SemVer kCommunityFirmwareVersion {
	1, 
  0, 
  0,
};
//clang-format on

constexpr char const* kFirmwareVersionString = "1.0.0-dev-909ba39c-dirty";
