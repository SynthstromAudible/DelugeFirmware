#pragma once

// Helpers for the model's codec specs: every decoder must read an XML-parsed and a JSON-parsed tree alike, so a spec
// states each case in both encodings and runs it on both.

#include "serialization/parse_json.h"
#include "serialization/parse_xml.h"
#include "serialization/value.h"

#include <string_view>
#include <utility>

namespace deluge::project::test {

/// The `song` element of an XML document.
inline serialization::Value xml_song(std::string_view text) {
	auto root = serialization::parseXml(text);
	return *root->find("song");
}

/// The `song` object of a JSON document.
inline serialization::Value json_song(std::string_view text) {
	auto root = serialization::parseJson(text);
	return *root->find("song");
}

/// Runs @p check on the song of @p xmlText and of @p jsonText; both must hold the same content.
template <class F>
void both_shapes(std::string_view xmlText, std::string_view jsonText, F&& check) {
	check(xml_song(xmlText));
	check(json_song(jsonText));
}

} // namespace deluge::project::test
