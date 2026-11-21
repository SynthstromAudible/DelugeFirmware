#include "scenes/bind/ports/device_settings.h"

#include "deluge-project/device_settings.h"
#include "plugin-api/types/display_mapping.h"
#include "serialization/tree.h"

#include <algorithm>
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
	return read(*row);
}

int32_t DeviceSettings::read(const plugin::SettingInfo& row) const {
	const serialization::Value value = load(row);
	const std::string* text = serialization::text_of(&value);
	if (text == nullptr) {
		return row.default_value;
	}
	if (const std::optional<int32_t> stored = plugin::stored_value(row, *text)) {
		return *stored;
	}
	// A CHOICE index past the row's choices (a byte the flash held that no menu offers) is clamped into them, as a
	// choice item's widget shows one (`enum_value` in `ui/oled/values.cpp`).
	if (const std::optional<int64_t> index = plugin::whole_number(*text);
	    index && row.kind == plugin::SettingKind::CHOICE && !row.choices.empty()) {
		return static_cast<int32_t>(std::clamp<int64_t>(*index, 0, static_cast<int64_t>(row.choices.size()) - 1));
	}
	// Anything else the row cannot name reads as its default, so a row the device holds never reads as nothing.
	return row.default_value;
}

plugin::SettingReader DeviceSettings::reader() const {
	return {.read = [](const void* self,
	                   std::string_view path) { return static_cast<const DeviceSettings*>(self)->read(path); },
	        .self = this};
}

DeviceWrite DeviceSettings::write(std::string_view path, int32_t menu_value) {
	const plugin::SettingInfo* row = plugin::find_setting(project::device_table(), path);
	if (row == nullptr) {
		return DeviceWrite::REFUSED;
	}
	// A DEVICE row stores one value: a mapping that writes a partner row too has no DEVICE row to write.
	const auto stored = plugin::from_menu(*row, menu_value);
	if (!stored || !plugin::mapping_of(*row).partner_path().empty()) {
		return DeviceWrite::REFUSED;
	}
	const std::optional<serialization::Value> value = file_value(*row, stored->first);
	const std::string* text = value ? serialization::text_of(&*value) : nullptr;
	if (text == nullptr || !plugin::check_setting_text(project::device_table(), path, *text)) {
		return DeviceWrite::REFUSED;
	}
	return store(*row, *value);
}

} // namespace deluge::scenes::bind
