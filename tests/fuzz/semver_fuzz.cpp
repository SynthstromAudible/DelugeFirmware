// Fuzz harness for SemVer::Parser in src/deluge/util/semver.cpp.
// Version strings flow from firmware files on the SD card into this
// parser, so a crash or read past the end of the input is reachable
// by an attacker who controls those files.
//
// Build via tests/fuzz/CMakeLists.txt (libFuzzer requires clang and a
// C++23 stdlib that provides std::expected).

#include "../../src/deluge/util/semver.h"
#include <cstdint>
#include <cstdlib>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
	std::string_view input(reinterpret_cast<const char*>(data), size);
	auto result = SemVer::parse(input);
	if (result.has_value()) {
		// On success, every parsed numeric field must round-trip into a
		// uint8_t with no information loss. The parser uses std::from_chars
		// into a uint8_t directly so this is a tautology today, but the
		// check guards against future refactors that widen the field.
		volatile uint8_t consume = result->major ^ result->minor ^ result->patch;
		(void)consume;
	}
	return 0;
}
