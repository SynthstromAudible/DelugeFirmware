#pragma once

#include "storage/deserializer.h" // Deserializer, Error
#include "storage/document_format.h"
#include "storage/parse_json.h"
#include "storage/parse_xml.h"
#include "storage/value_tree_deserializer.h"

#include <cstring>
#include <optional>
#include <string_view>

/// @brief Parses a document's @p bytes as @p format to the Value tree the drivers load from.
inline std::optional<deluge::storage::Value> parseDocument(std::string_view bytes,
                                                           deluge::storage::DocumentFormat format) {
	return format == deluge::storage::DocumentFormat::JSON ? deluge::storage::parseJson(bytes)
	                                                       : deluge::storage::parseXml(bytes);
}

/// @brief The tree reader's mode for a document of @p format.
constexpr deluge::storage::SourceFormat sourceFormatOf(deluge::storage::DocumentFormat format) {
	return format == deluge::storage::DocumentFormat::JSON ? deluge::storage::SourceFormat::Json
	                                                       : deluge::storage::SourceFormat::Xml;
}

// Position an in-memory ValueTreeDeserializer at the given root element's content, mirroring
// XMLDeserializer::openXMLFile(firstTag): read top-level tags, consuming firmware-version tags along the way, until
// `firstTag` is reached -- leaving the reader positioned inside that element exactly as the streaming reader does. A
// JSON document's root object is entered first. Returns Error::NONE on success or Error::FILE_CORRUPTED if the element
// is never found. Shared by the load-via-tree golden harnesses.
inline Error openTreeFile(Deserializer& reader, char const* firstTagName,
                          deluge::storage::DocumentFormat format = deluge::storage::DocumentFormat::XML) {
	if (format == deluge::storage::DocumentFormat::JSON) {
		reader.match('{');
	}
	char const* tagName;
	while (*(tagName = reader.readNextTagOrAttributeName())) {
		if (!strcmp(tagName, firstTagName)) {
			return Error::NONE;
		}
		reader.tryReadingFirmwareTagFromFile(tagName, false);
		reader.exitTag(tagName);
	}
	return Error::FILE_CORRUPTED;
}
