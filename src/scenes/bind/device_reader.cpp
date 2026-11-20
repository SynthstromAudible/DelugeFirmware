#include "scenes/bind/device_reader.h"

#include "deluge-project/device_settings.h"

#include <cassert>

namespace deluge::scenes::bind {

std::optional<int32_t> DeviceReader::read(std::string_view path) const {
	return device_->read(path);
}

plugin::SettingReader DeviceReader::reader() const {
	return device_->reader();
}

int32_t read_device_value(const DeviceSettings* port, std::string_view path) {
	const plugin::SettingInfo* row = plugin::find_setting(project::device_table(), path);
	assert(row != nullptr && "a DEVICE row's path");
	if (row == nullptr) {
		return 0;
	}
	if (port == nullptr) {
		return row->default_value;
	}
	return port->read(path).value_or(row->default_value);
}

bool read_device_flag(const DeviceSettings* port, std::string_view path) {
	return read_device_value(port, path) != 0;
}

} // namespace deluge::scenes::bind
