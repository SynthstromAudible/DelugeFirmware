#pragma once
#include "util/semver.h"

struct FirmwareVersion {
	enum class Type : uint8_t {
		OFFICIAL,
		COMMUNITY = 254,
		UNKNOWN = 255,
	};

	FirmwareVersion() = delete;
	constexpr FirmwareVersion(Type type, SemVer version) : type_(type), version_(version) {};

	constexpr static FirmwareVersion current() {
		return FirmwareVersion(Type::COMMUNITY, {
		                                            // clang-format off
			0,
			0,
			0,
			//clang-format on
		});
	};


	constexpr static FirmwareVersion official(SemVer version) { return FirmwareVersion{Type::OFFICIAL, version}; }
	constexpr static FirmwareVersion community(SemVer version) { return FirmwareVersion{Type::COMMUNITY, version}; }

  auto operator<=>(const FirmwareVersion&) const = default;

	static FirmwareVersion parse(std::string_view string);
	[[nodiscard]] constexpr Type type() const { return type_; }
	[[nodiscard]] constexpr SemVer version() const { return version_; }

private:
	Type type_ = Type::COMMUNITY;
	SemVer version_;
};
