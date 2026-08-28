#pragma once
#include "cppspec.hpp"
#include <cstdlib>
/// @brief Define this spec TU's entry point, named `<dir>_<stem>` as tests/spec/CMakeLists.txt's
///        create_specs_driver expects, and have it run @p spec.
///
/// CppSpec's own CPPSPEC_SPEC macro cannot spell that name for nested directories, so this macro
/// does.
/// @param fn   The entry-point function name to define (`<dir>_<stem>`).
/// @param spec The `describe(...)` spec object to run.
#define DELUGE_SPEC(fn, spec)                                                                                          \
	int fn(int argc, char** const argv) {                                                                              \
		return CppSpec::parse(argc, argv).add_spec(spec).exec().is_success() ? EXIT_SUCCESS : EXIT_FAILURE;            \
	}
