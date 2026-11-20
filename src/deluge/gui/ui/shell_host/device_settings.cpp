#include "deluge-project/device_settings.h"
#include "gui/ui/shell_host/shell_host.h"
#include "hid/led/pad_leds.h"
#include "instruments/deluge/cables.h"
#include "io/debug/log.h"
#include "music-theory/preset_scales.h"
#include "plugin-api/types/setting.h"
#include "serialization/tree.h"
#include "storage/flash_storage.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace deluge::gui::shell_host {
namespace {

using plugin::SettingInfo;
using project::FlashCursor;
using project::ShortcutsVersion;
using scenes::bind::DeviceWrite;
using serialization::Value;
namespace device_path = project::device_path;

// A CHOICE row's index is the legacy value for these.
static_assert(std::to_underlying(FlashCursor::FAST) == FLASH_CURSOR_FAST);
static_assert(std::to_underlying(FlashCursor::OFF) == FLASH_CURSOR_OFF);
static_assert(std::to_underlying(FlashCursor::SLOW) == FLASH_CURSOR_SLOW);
static_assert(std::to_underlying(ShortcutsVersion::V1) == FlashStorage::kShortcutsVersion1);
static_assert(std::to_underlying(ShortcutsVersion::V3) == FlashStorage::kShortcutsVersion3);
static_assert(std::to_underlying(instrument::Polarity::UNIPOLAR) == 0
              && std::to_underlying(instrument::Polarity::BIPOLAR) == 1);

/// @brief A BOOL row's file form.
Value flag(bool on) {
	return Value::makeNumber(on ? 1 : 0);
}

/// @brief A CHOICE row's file form for choice @p index: its word, or, for an index the row has no choice at (a value
///        the device's flash held that no menu offers), the index itself as a number, which the port's read clamps.
Value choice(const SettingInfo& row, size_t index) {
	return index < row.choices.size() ? Value::makeScalar(std::string{row.choices[index]})
	                                  : Value::makeNumber(static_cast<int64_t>(index));
}

/// @brief The preset scale a `defaults/scales/disabled/<i>` path names, or nullopt.
std::optional<size_t> disabled_scale_of(std::string_view path) {
	if (!path.starts_with(device_path::kDisabledScalePrefix)) {
		return std::nullopt;
	}
	const std::optional<int64_t> index = plugin::whole_number(path.substr(device_path::kDisabledScalePrefix.size()));
	if (!index || *index < 0 || *index >= music::NUM_PRESET_SCALES) {
		return std::nullopt;
	}
	return static_cast<size_t>(*index);
}

/// The device's own settings where the device keeps them: the flash settings and the pad controller's play cursor. A
/// store writes the value as the menu's own item does, with that item's side effects; none saves (the settings menu
/// saves on its close).
class HostDeviceSettings final : public scenes::bind::DeviceSettings {
	[[nodiscard]] Value load(const SettingInfo& row) const override {
		const std::string_view path = row.path;
		if (path == device_path::kUseSharps) {
			return flag(FlashStorage::defaultUseSharps);
		}
		if (path == device_path::kPatchCablePolarity) {
			return choice(row, std::to_underlying(FlashStorage::defaultPatchCablePolarity));
		}
		if (path == device_path::kShortcutsVersion) {
			// Any byte but V1's selects the current layout, as the shortcut lookups read it.
			return choice(row, std::to_underlying(FlashStorage::shortcutsVersion == FlashStorage::kShortcutsVersion1
			                                          ? ShortcutsVersion::V1
			                                          : ShortcutsVersion::V3));
		}
		if (path == device_path::kPlayCursor) {
			return choice(row, PadLEDs::flashCursor);
		}
		if (const std::optional<size_t> scale = disabled_scale_of(path)) {
			return flag(FlashStorage::defaultDisabledPresetScales[*scale]);
		}
		D_PRINTLN("device setting %.*s has no home", static_cast<int>(path.size()), path.data());
		return Value::makeNumber(row.default_value);
	}

	[[nodiscard]] DeviceWrite store(const SettingInfo& row, const Value& value) override {
		const std::string_view path = row.path;
		// The front checked the value against the row, so it is a choice's word, a bool or an integer in range.
		const int32_t stored = plugin::stored_value(row, value.str()).value_or(row.default_value);
		if (path == device_path::kUseSharps) {
			FlashStorage::defaultUseSharps = stored != 0;
			return DeviceWrite::STORED;
		}
		if (path == device_path::kPatchCablePolarity) {
			FlashStorage::defaultPatchCablePolarity = static_cast<instrument::Polarity>(stored);
			return DeviceWrite::STORED;
		}
		if (path == device_path::kShortcutsVersion) {
			FlashStorage::shortcutsVersion = static_cast<uint8_t>(stored);
			return DeviceWrite::STORED;
		}
		if (path == device_path::kPlayCursor) {
			// Leaving the slow flash clears its tick squares, judged on the value being replaced while it is still the
			// current one (`flash::Status::writeCurrentValue` in
			// `origin/main:src/deluge/gui/menu_item/flash/status.h`).
			if (PadLEDs::flashCursor == FLASH_CURSOR_SLOW) {
				PadLEDs::clearTickSquares(true);
			}
			PadLEDs::flashCursor = static_cast<uint8_t>(stored);
			return DeviceWrite::STORED;
		}
		if (const std::optional<size_t> scale = disabled_scale_of(path)) {
			FlashStorage::defaultDisabledPresetScales[*scale] = stored != 0;
			return DeviceWrite::STORED;
		}
		// A row this host keeps nowhere: refused rather than dropped, so the write is seen not to have happened.
		D_PRINTLN("device setting %.*s has no home", static_cast<int>(path.size()), path.data());
		return DeviceWrite::REFUSED;
	}
};

} // namespace

scenes::bind::DeviceSettings& device_settings() {
	static HostDeviceSettings settings;
	return settings;
}

} // namespace deluge::gui::shell_host
