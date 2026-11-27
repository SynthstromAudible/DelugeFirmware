#include "serialization/schema/walk_context.h"

namespace deluge::serialization::schema {

std::string WalkContext::path() const {
	std::string joined;
	for (const std::string& step : path_) {
		if (!joined.empty()) {
			joined += '/';
		}
		joined += step;
	}
	return joined;
}

} // namespace deluge::serialization::schema
