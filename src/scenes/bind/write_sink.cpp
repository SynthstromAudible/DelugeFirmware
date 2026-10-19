#include "scenes/bind/write_sink.h"

namespace deluge::scenes::bind {

void RecordingSink::write(const WriteRequest& request) {
	requests.push_back(request);
}

} // namespace deluge::scenes::bind
