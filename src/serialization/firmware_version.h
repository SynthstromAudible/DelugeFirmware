#pragma once
#include "serialization/semver.h"

#include <compare>
#include <cstdint>
#include <string_view>

/// @brief A Deluge firmware version, as a document records the firmware that wrote it.
struct FirmwareVersion {
	/// @brief The type of firmware (e.g. Official vs Community)
	enum class Type : uint8_t {
		OFFICIAL,
		COMMUNITY = 254,
		UNKNOWN = 255,
	};

	FirmwareVersion() = delete;
	constexpr FirmwareVersion(Type type, SemVer version) : type_(type), version_(version) {};

	/**
	 * @brief Construct a FirmwareVersion object that represents a version from the official firmware
	 * @param version The version to hold
	 */
	constexpr static FirmwareVersion official(SemVer version) { return FirmwareVersion{Type::OFFICIAL, version}; }

	/**
	 * @brief Construct a FirmwareVersion object that represents a version from the community firmware
	 * @param version The version to hold
	 */
	constexpr static FirmwareVersion community(SemVer version) { return FirmwareVersion{Type::COMMUNITY, version}; }

	/**
	 * @brief Compare two FirmwareVersions.
	 * @details Since the operates on field order, the @ref Type is compared first, so that
	 *          all Community versions are considered newer than Official versions.
	 */
	auto operator<=>(const FirmwareVersion&) const = default;

	/**
	 * @brief Parse a Deluge firmware version string such as "4.1.4-beta" or "c1.1.0-alpha"
	 * @note This uses the first character to determine the @ref Type ('c' means @ref Type::COMMUNITY)
	 * @return FirmwareVersion The parsed firmware version
	 */
	static FirmwareVersion parse(std::string_view string);

	[[nodiscard]] constexpr Type type() const { return type_; }
	[[nodiscard]] constexpr SemVer version() const { return version_; }

private:
	Type type_ = Type::COMMUNITY;
	SemVer version_;
};
