#pragma once
#include "cppspec.hpp"
#include "support/golden_update.h"
#include <fstream>
#include <sstream>
#include <string>

#ifndef GOLDEN_ROOT
#error "GOLDEN_ROOT must be defined by CMake (deluge_golden_root, tests/support/cmake/golden.cmake)"
#endif

/// @brief Compare @p actual against the recorded golden file `tests/golden/<name>`.
///
/// @param self   The enclosing `it(...)` block's own lambda parameter, taken as an explicit,
///               templated first parameter and forwarded from each call site: CppSpec's
///               `expect(...)` macro expands to `self.expect(...)` (see the `_` macro in
///               cppspec.hpp), and there is no ambient `self` available to a plain free function.
/// @param name   Path under GOLDEN_ROOT, starting with its consumer's directory, e.g. "ui/gfx/oled/primitives.pbm".
/// @param actual The value to compare against the golden (or, when re-recording, to write to it).
///
/// @note Under `./dbt test --update` (deluge::test::golden::updating()) the golden is rewritten from @p actual, when it
///       differs, before the comparison.
template <typename Self>
inline void expect_golden(Self& self, const std::string& name, const std::string& actual) {
	const std::string path = std::string(GOLDEN_ROOT) + "/" + name;
	if (deluge::test::golden::updating()) {
		deluge::test::golden::write_if_changed(path, actual);
	}
	std::ifstream in(path);
	std::stringstream want;
	want << in.rdbuf();
	expect(in.good()).to_be_true();      // golden exists
	expect(actual).to_equal(want.str()); // byte-identical
}
