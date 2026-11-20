#include "scenes/bind/ports/device_settings.h"

#include "deluge-project/device_settings.h"
#include "plugin-api/types/display_mapping.h"
#include "serialization/tree.h"

#include <array>
#include <string>

namespace deluge::scenes::bind {

namespace {

/// @brief The file form of stored value @p stored of @p row: a CHOICE's word, else the number.
/// @return The value, or nullopt for a choice the row does not have, or a TEXT or STRUCTURED row.
std::optional<serialization::Value> file_value(const plugin::SettingInfo& row, int32_t stored) {
	switch (row.kind) {
	case plugin::SettingKind::CHOICE: {
		std::array<char, 1> unused{};
		const std::optional<std::string_view> word = plugin::stored_text(row, stored, unused);
		return word ? std::optional{serialization::Value::makeScalar(std::string{*word})} : std::nullopt;
	}
	case plugin::SettingKind::BOOL:
	case plugin::SettingKind::INTEGER:
		return serialization::Value::makeNumber(stored);
	case plugin::SettingKind::TEXT:
	case plugin::SettingKind::STRUCTURED:
		break;
	}
	return std::nullopt;
}

} // namespace

std::optional<int32_t> DeviceSettings::read(std::string_view path) const {
	const plugin::SettingInfo* row = plugin::find_setting(project::device_table(), path);
	if (row == nullptr) {
		return std::nullopt;
	}
	const serialization::Value value = load(*row);
	const std::string* text = serialization::text_of(&value);
	const std::optional<int32_t> stored = text != nullptr ? plugin::stored_value(*row, *text) : std::nullopt;
	// A value the row cannot name (a choice the device holds no word for) reads as the row's default, as a model row's
	// does, so the menu lists a value it can choose again, and a row the device holds never reads as nothing.
	return stored.value_or(row->default_value);
}

plugin::SettingReader DeviceSettings::reader() const {
	return {.read = [](const void* self,
	                   std::string_view path) { return static_cast<const DeviceSettings*>(self)->read(path); },
	        .self = this};
}

DeviceWrite DeviceSettings::write(std::string_view path, int32_t menu_value) {
	const plugin::SettingInfo* row = plugin::find_setting(project::device_table(), path);
	if (row == nullptr) {
		return DeviceWrite::Refused;
	}
	// A DEVICE row stores one value: a mapping that writes a partner row too has no DEVICE row to write.
	const auto stored = plugin::from_menu(*row, menu_value);
	if (!stored || !plugin::mapping_of(*row).partner_path().empty()) {
		return DeviceWrite::Refused;
	}
	const std::optional<serialization::Value> value = file_value(*row, stored->first);
	const std::string* text = value ? serialization::text_of(&*value) : nullptr;
	if (text == nullptr || !plugin::check_setting_text(project::device_table(), path, *text)) {
		return DeviceWrite::Refused;
	}
	return store(*row, *value);
}

} // namespace deluge::scenes::bind
