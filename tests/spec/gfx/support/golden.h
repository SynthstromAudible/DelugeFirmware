#pragma once
#include "cppspec.hpp"
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#ifndef GFX_GOLDEN_DIR
#error "GFX_GOLDEN_DIR must be defined by CMake"
#endif

/// Compare `actual` with tests/golden/gfx/<name>. GFX_GOLDEN_UPDATE=1 rewrites the file.
///
/// Exactly "1", not merely "set": re-recording a golden destroys the evidence the extraction is
/// behaviour-preserving (the rewritten file then matches whatever was just produced, so the
/// comparison below passes vacuously). GFX_GOLDEN_UPDATE=0 -- the natural way to say "no" -- must
/// therefore not trigger it, and neither must an empty or stray value inherited from the shell.
///
/// NB: deviates from the brief's free-function signature. CppSpec's `expect(...)` macro expands
/// to `self.expect(...)`, where `self` is the `it(...)` block's own lambda parameter (see the `_`
/// macro in cppspec.hpp) -- there is no ambient `self` available to a plain free function. Taking
/// `self` as an explicit (templated) first parameter and forwarding it from each call site is the
/// minimal change that lets this compile against the real CppSpec API.
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
