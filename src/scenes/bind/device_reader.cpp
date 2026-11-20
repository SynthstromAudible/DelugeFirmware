#include "scenes/bind/device_reader.h"

namespace deluge::scenes::bind {

std::optional<int32_t> DeviceReader::read(std::string_view path) const {
	return device_->read(path);
}

plugin::SettingReader DeviceReader::reader() const {
	return device_->reader();
}

} // namespace deluge::scenes::bind
