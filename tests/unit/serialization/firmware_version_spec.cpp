#include "serialization/firmware_version.h"

#include <cppspec.hpp>

// clang-format off
describe firmware_version("a document's firmware version", ${
	it("orders official releases by version, pre-releases first", _ {
		auto older = FirmwareVersion::parse("3.1.0-alpha");
		auto newer = FirmwareVersion::parse("3.1.0-alpha2");
		auto release = FirmwareVersion::parse("3.1.0");
		expect(older < newer).to_equal(true);
		expect(newer < release).to_equal(true);
	});

	it("counts every community version as newer than every official one", _ {
		expect(FirmwareVersion::parse("c1.0.0") > FirmwareVersion::parse("4.1.4")).to_equal(true);
	});

	it("reads an unparseable string as an unknown version", _ {
		expect(FirmwareVersion::parse("banana").type() == FirmwareVersion::Type::UNKNOWN).to_equal(true);
	});
});

CPPSPEC_SPEC(firmware_version)
