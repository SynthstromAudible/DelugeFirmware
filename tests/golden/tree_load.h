#pragma once

#include "storage/deserializer.h" // Deserializer, Error

#include <cstring>

// Position an in-memory ValueTreeDeserializer (record-first deserializer, step 2b) at the given root
// element's content, mirroring XMLDeserializer::openXMLFile(nullptr, firstTag): read top-level tags,
// consuming firmware-version tags along the way, until `firstTag` is reached -- leaving the reader
// positioned inside that element exactly as the streaming reader does. Returns Error::NONE on success
// or Error::FILE_CORRUPTED if the element is never found. Shared by the load-via-tree golden harnesses.
inline Error openTreeFile(Deserializer& reader, char const* firstTagName) {
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
