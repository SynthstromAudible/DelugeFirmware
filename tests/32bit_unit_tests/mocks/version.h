// mock up the version.h that would be made by cmake in the main build

#pragma once

#ifdef __cplusplus
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
	0,
	0,
	0,
};
//clang-format on

constexpr char const* kFirmwareVersionString = "TESTS";

#else

#define FIRMWARE_VERSION_MAJOR 0
#define FIRMWARE_VERSION_MINOR 0
#define FIRMWARE_VERSION_PATCH 0

#define FIRMWARE_VERSION_STRING "TESTS"
#define FIRMWARE_COMMIT_SHORT "0000"

#endif
