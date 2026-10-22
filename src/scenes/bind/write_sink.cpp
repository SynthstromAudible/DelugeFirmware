#include "scenes/bind/write_sink.h"

namespace deluge::scenes::bind {

bool RecordingSink::write(const WriteRequest& request) {
	requests.push_back(request);
	return true;
}

bool RecordingSink::write_param(const project::ParamAddress& address, int32_t value, int32_t pos, int32_t length) {
	param_writes.push_back({address, value, pos, length});
	return true;
}

bool RecordingSink::delete_automation(const project::ParamAddress& address) {
	deletes.push_back(address);
	return true;
}

} // namespace deluge::scenes::bind
