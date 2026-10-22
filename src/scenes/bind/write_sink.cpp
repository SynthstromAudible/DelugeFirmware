#include "scenes/bind/write_sink.h"

namespace deluge::scenes::bind {

bool RecordingSink::write(const WriteRequest& request) {
	requests.push_back(request);
	return true;
}

} // namespace deluge::scenes::bind
