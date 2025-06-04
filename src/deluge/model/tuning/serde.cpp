#include "model/tuning/tuning.h"
#include "storage/storage_manager.h"

void Tuning::writeToFile(Serializer& writer) {
	writer.writeOpeningTagBeginning("octaveTuning", true);
	writer.writeAttribute("name", name);
	writer.writeAttribute("divisions", divisions);
	writer.writeOpeningTagEnd();

	writer.writeArrayStart("offsets");
	for (int i = 0; i < divisions; i++) {
		writer.writeOpeningTagBeginning("offset");
		writer.writeAttribute("cents", offsets[i]);
		writer.closeTag(true);
	}
	writer.writeArrayEnding("offsets");

	writer.writeClosingTag("octaveTuning", true, true);
}

void Tuning::readTagFromFile(Deserializer& reader, char const* tagName) {

	if (!strcmp(tagName, "octaveTuning")) {
		while (*(tagName = reader.readNextTagOrAttributeName())) {
			if (!strcmp(tagName, "name")) {
				String newName;
				reader.readTagOrAttributeValueString(&newName);
				strncpy(name, newName.get(), sizeof(name));
			}
			else if (!strcmp(tagName, "divisions")) {
				setDivisions(reader.readTagOrAttributeValueInt());
			}
			else if (!strcmp(tagName, "offsets")) {
				int current_offset = 0;
				while (*(tagName = reader.readNextTagOrAttributeName())) {
					if (!strcmp(tagName, "offset")) {
						while (*(tagName = reader.readNextTagOrAttributeName())) {
							if (!strcmp(tagName, "cents")) {
								offsets[current_offset] = reader.readTagOrAttributeValueInt();
							}
							reader.exitTag();
						}
					}
					reader.exitTag();
				}
			}
			reader.exitTag();
		}
	}
}
