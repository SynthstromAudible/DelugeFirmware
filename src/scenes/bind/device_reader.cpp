#include "scenes/bind/device_reader.h"

#include "deluge-project/device_settings.h"

namespace deluge::scenes::bind {

std::optional<int32_t> DeviceReader::read(std::string_view path) const {
	return device_->read(path);
}

plugin::SettingReader DeviceReader::reader() const {
	return device_->reader();
}

std::optional<int32_t> read_device_value(const DeviceSettings* port, std::string_view path) {
	if (port != nullptr) {
		return port->read(path);
	}
	const plugin::SettingInfo* row = plugin::find_setting(project::device_table(), path);
	return row != nullptr ? std::optional<int32_t>{row->default_value} : std::nullopt;
}

bool read_device_flag(const DeviceSettings* port, std::string_view path) {
	return port != nullptr && port->read(path).value_or(0) != 0;
}

} // namespace deluge::scenes::bind
