# deluge_serialization: the format-agnostic document tree (Value), the XML and JSON parsers that build it, and the
# firmware version a document records.
# Standard library only (plus foundation/panic.h); scripts/check_library_boundary.py enforces that with no allowlist.
add_library(deluge_serialization STATIC parse_xml.cpp parse_json.cpp semver.cpp firmware_version.cpp write_json.cpp)
# Parsing and writing documents is load/save work, off the audio path.
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/optimise_for_size.cmake)
deluge_optimise_target_for_size(deluge_serialization)
# Headers are included as "serialization/..."; the include root is src/ (this directory's parent).
target_include_directories(deluge_serialization PUBLIC ${CMAKE_CURRENT_LIST_DIR}/..)
target_compile_features(deluge_serialization PUBLIC cxx_std_23)
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/library_warnings.cmake)
deluge_library_warnings(deluge_serialization)
