#include "util/firmware_version.h"
#include "util/try.h"
#include <cctype>
#include <string_view>

FirmwareVersion FirmwareVersion::parse(std::string_view version_string) {
	Type type;

	if (version_string[0] == 'c') {
		type = Type::COMMUNITY;
		version_string = version_string.substr(1);
	}
	else {
		type = Type::OFFICIAL;
	}
	auto parse_result = SemVer::parse(version_string);
	if (!parse_result) {
		return {Type::UNKNOWN, {0, 0, 0}};
	}
	return FirmwareVersion{type, parse_result.value()};
}
