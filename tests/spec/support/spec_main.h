#pragma once
#include "cppspec.hpp"
#include <cstdlib>
/// Every spec TU exports one C-linkage-free entry point the driver calls (tests/spec/CMakeLists.txt's
/// create_specs_driver derives the name from the path: `<dir>_<stem>`); CppSpec's own CPPSPEC_SPEC macro
/// cannot spell that name for nested directories, so this macro does.
#define DELUGE_SPEC(fn, spec)                                                                                          \
	int fn(int argc, char** const argv) {                                                                              \
		return CppSpec::parse(argc, argv).add_spec(spec).exec().is_success() ? EXIT_SUCCESS : EXIT_FAILURE;            \
	}
