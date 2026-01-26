/*
 * Copyright © 2024-2025 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 */
#include "storage/field_serialization.h"
#include "storage/storage_manager.h"

namespace deluge::storage {

void writeAttributeInt(Serializer& writer, const char* tag, int32_t value) {
	writer.writeAttribute(tag, value);
}

void writeAttributeHex(Serializer& writer, const char* tag, int32_t value) {
	writer.writeAttributeHex(tag, value, 8);
}

int32_t readAndExitTag(Deserializer& reader, const char* tag) {
	int32_t value = reader.readTagOrAttributeValueInt();
	reader.exitTag(tag);
	return value;
}

int32_t readHexAndExitTag(Deserializer& reader, const char* tag) {
	int32_t value = reader.readTagOrAttributeValueHex(0);
	reader.exitTag(tag);
	return value;
}

} // namespace deluge::storage
