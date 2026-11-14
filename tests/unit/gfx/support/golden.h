#pragma once
#include "cppspec.hpp"
#include "support/golden_update.h"
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
/// @note Under `./dbt test --update` (deluge::test::golden::updating()) the golden is rewritten from @p actual, when it
///       differs, before the comparison.
template <typename Self>
inline void expect_golden(Self& self, const std::string& name, const std::string& actual) {
	const std::string path = std::string(GFX_GOLDEN_DIR) + "/" + name;
	if (deluge::test::golden::updating()) {
		deluge::test::golden::write_if_changed(path, actual);
	}
	std::ifstream in(path);
	std::stringstream want;
	want << in.rdbuf();
	expect(in.good()).to_be_true();      // golden exists
	expect(actual).to_equal(want.str()); // byte-identical
}
