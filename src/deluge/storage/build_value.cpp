#include "storage/build_value.h"

#include <string>

namespace deluge::storage {

Value buildValue(Deserializer& reader) {
	Value result = Value::makeMap();

	for (;;) {
		char const* name = reader.readNextTagOrAttributeName();
		if (name[0] == '\0') {
			break; // End-of-object signal: this level is done.
		}
		std::string key(name);

		if (reader.match('{')) {
			// Object: recurse into the frame match('{') just pushed, then pop it.
			Value child = buildValue(reader);
			reader.exitTag(nullptr, true);
			result.set(std::move(key), std::move(child));
		}
		else if (reader.match('[')) {
			// Array: consume element boxes one at a time until the array is exhausted.
			Value child = Value::makeArray();
			while (reader.match('{')) {
				Value element = buildValue(reader);
				reader.exitTag(nullptr, true); // pop the element's frame
				child.append(std::move(element));
			}
			reader.match(']'); // pop the array's own frame
			result.set(std::move(key), std::move(child));
		}
		else {
			// Scalar.
			Value child = Value::makeScalar(reader.readTagOrAttributeValue());
			reader.exitTag();
			result.set(std::move(key), std::move(child));
		}
	}

	return result;
}

} // namespace deluge::storage
