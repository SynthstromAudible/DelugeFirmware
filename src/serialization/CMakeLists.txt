# deluge_serialization: the format-agnostic document tree (Value) and the XML and JSON parsers that build it.
# Standard library only (plus foundation/panic.h); scripts/check_library_boundary.py enforces that with no allowlist.
add_library(deluge_serialization STATIC parse_xml.cpp parse_json.cpp)
# Headers are included as "serialization/..."; the include root is src/ (this directory's parent).
target_include_directories(deluge_serialization PUBLIC ${CMAKE_CURRENT_LIST_DIR}/..)
target_compile_features(deluge_serialization PUBLIC cxx_std_23)
