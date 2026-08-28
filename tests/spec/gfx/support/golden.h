#pragma once
#include "cppspec.hpp"
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#ifndef GFX_GOLDEN_DIR
#error "GFX_GOLDEN_DIR must be defined by CMake"
#endif

/// @brief Compare @p actual against the recorded golden file `tests/golden/gfx/<name>`.
///
/// @param self   The enclosing `it(...)` block's own lambda parameter, taken as an explicit,
///               templated first parameter and forwarded from each call site: CppSpec's
///               `expect(...)` macro expands to `self.expect(...)` (see the `_` macro in
///               cppspec.hpp), and there is no ambient `self` available to a plain free function.
/// @param name   Path relative to GFX_GOLDEN_DIR, e.g. "oled/primitives.pbm".
/// @param actual The value to compare against the golden (or, when re-recording, to write to it).
///
/// @note Setting `GFX_GOLDEN_UPDATE=1` rewrites the golden file instead of comparing against it.
///       The check is for exactly "1", not merely "set": `GFX_GOLDEN_UPDATE=0` -- the natural way
///       to say "no" -- must not trigger it, nor must an empty or stray value inherited from the
///       shell. Re-recording destroys the evidence that the port is behaviour-preserving (the
///       rewritten file then matches whatever was just produced, so the comparison would pass
///       vacuously), so make sure a re-record is deliberate, not accidental.
template <typename Self>
inline void expect_golden(Self& self, const std::string& name, const std::string& actual) {
	const std::string path = std::string(GFX_GOLDEN_DIR) + "/" + name;
	const char* update = std::getenv("GFX_GOLDEN_UPDATE");
	if (update != nullptr && std::string(update) == "1") {
		std::ofstream(path) << actual;
	}
	std::ifstream in(path);
	std::stringstream want;
	want << in.rdbuf();
	expect(in.good()).to_be_true();      // golden exists
	expect(actual).to_equal(want.str()); // byte-identical
}
