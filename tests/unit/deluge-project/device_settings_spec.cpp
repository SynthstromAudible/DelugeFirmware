#include "deluge-project/device_settings.h"
#include "deluge-project/setting_group.h"
#include "deluge-project/song.h"
#include "music-theory/preset_scales.h"

#include "cppspec.hpp"
#include "support/matchers/value_matchers.h"

#include <set>
#include <string>
#include <string_view>

using namespace deluge::project;
using deluge::plugin::SettingInfo;
using deluge::plugin::SettingKind;

// clang-format off
describe device_settings("the DEVICE setting group", ${
	it("is the table table_of gives DEVICE, and has rows", _ {
		const auto table = table_of(SettingGroupId::DEVICE);
		expect(table.empty()).to_be_false();
		expect(table.data()).to_equal(device_table().data());
		expect(table.size()).to_equal(device_table().size());
	});

	it("names each path once", _ {
		std::set<std::string> seen;
		for (const SettingInfo& row : device_table()) {
			expect(seen.insert(std::string{row.path}).second).to_be_true(std::string{row.path} + " is listed twice");
		}
	});

	it("gives every CHOICE row choices, and every row a default it can store", _ {
		for (const SettingInfo& row : device_table()) {
			const std::string at{row.path};
			if (row.kind == SettingKind::CHOICE) {
				expect(row.choices.empty()).to_be_false(at + " has no choices");
			}
			if (row.kind == SettingKind::INTEGER) {
				to_be_at_least(expect(row.default_value), row.min);
				to_be_at_least(expect(row.max), row.default_value);
			}
			expect(deluge::plugin::stores_value(row, row.default_value)).to_be_true(at + "'s default is not storable");
		}
	});

	it("is counted by the group count", _ {
		expect(is_counted_by_group_count(SettingGroupId::DEVICE)).to_be_true();
		expect(static_cast<int>(SettingGroupId::DEVICE)).to_equal(kSettingGroupCount - 1);
	});

	it("holds a disabled-by-default row per preset scale, and the rows the scenes name", _ {
		for (int32_t scale = 0; scale < deluge::music::NUM_PRESET_SCALES; ++scale) {
			const std::string path = std::string{device_path::kDisabledScalePrefix} + std::to_string(scale);
			const SettingInfo* row = deluge::plugin::find_setting(device_table(), path);
			expect(row).not_().to_be_null(path);
			if (row != nullptr) {
				expect(row->kind).to_equal(SettingKind::BOOL);
				expect(row->default_value).to_equal(0);
			}
		}
		// Each named row's kind and the default the device's settings reset to.
		struct Named {
			std::string_view path;
			SettingKind kind;
			int32_t default_value;
		};
		const Named named[] = {
		    {device_path::kUseSharps, SettingKind::BOOL, 1},
		    {device_path::kPatchCablePolarity, SettingKind::CHOICE, 1},
		    {device_path::kShortcutsVersion, SettingKind::CHOICE, static_cast<int32_t>(ShortcutsVersion::V3)},
		    {device_path::kPlayCursor, SettingKind::CHOICE, static_cast<int32_t>(FlashCursor::SLOW)},
		};
		for (const Named& want : named) {
			const SettingInfo* row = deluge::plugin::find_setting(device_table(), want.path);
			expect(row).not_().to_be_null(std::string{want.path});
			if (row != nullptr) {
				expect(row->kind).to_equal(want.kind);
				expect(row->default_value).to_equal(want.default_value);
			}
		}
	});

	it("is held by no song: a read finds no owner and a write is refused", _ {
		Song song;
		const SettingAddress device{SettingGroupId::DEVICE, DeviceAddress{}};
		expect(read_setting(song, device, device_path::kUseSharps).has_value()).to_be_false();
		expect(write_setting(song, device, device_path::kUseSharps, deluge::serialization::Value::makeNumber(0)))
		    .to_be_false();
		expect(holds_setting(song, device, device_path::kUseSharps)).to_be_false();
	});
});
// clang-format on

CPPSPEC_SPEC(device_settings)
