#pragma once

#include "storage/storage_manager.h"
#include <charconv>
#include <cstring>

// Fixed and sparse parameter tables share the same XML/JSON object protocol.
// The callback validates the ID against its collection before storing a value.
template <typename Store>
void readParameterObject(Deserializer& reader, const char* tag, Store store) {
	reader.match('{');
	const char* name = nullptr;
	while (*(name = reader.readNextTagOrAttributeName()) != 0) {
		uint32_t id = 0;
		if (name[0] == 'p' && name[1] != 0) {
			const char* end = name + std::strlen(name);
			auto result = std::from_chars(name + 1, end, id);
			if (result.ec == std::errc{} && result.ptr == end) {
				store(id, reader);
			}
		}
		reader.exitTag(name);
	}
	reader.exitTag(tag, true);
}

template <typename Store>
void readParameterValueObject(Deserializer& reader, const char* tag, Store store) {
	readParameterObject(reader, tag,
	                    [&](uint32_t id, Deserializer& value) { store(id, value.readTagOrAttributeValueInt()); });
}
