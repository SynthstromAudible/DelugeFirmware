#include "deluge-project/device_settings.h"
#include "deluge-project/setting_group.h"
#include "deluge-project/song.h"
#include "music-theory/preset_scales.h"

#include "cppspec.hpp"
#include "support/matchers/value_matchers.h"

#include <cstddef>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

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
			const std::optional<DevicePath> named = device_path::disabled_scale(static_cast<size_t>(scale));
			expect(named.has_value()).to_be_true(path);
			if (named.has_value()) {
				expect(std::string{named->text()}).to_equal(path);
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

	it("offers the screensaver's modes in ScreensaverMode's order, and defaults to the Deluge's", _ {
		using Mode = deluge::project::ScreensaverMode;
		const SettingInfo* row = deluge::plugin::find_setting(device_table(), device_path::kScreensaverMode);
		expect(row).not_().to_be_null();
		if (row == nullptr) {
			return;
		}
		expect(row->kind).to_equal(SettingKind::CHOICE);
		expect(row->default_value).to_equal(static_cast<int32_t>(std::to_underlying(Mode::DELUGE)));
		struct Choice {
			Mode mode;
			std::string_view word;
		};
		const Choice choices[] = {
		    {Mode::OFF, "off"}, {Mode::BLANK, "blank"}, {Mode::STARSCAPE, "starscape"}, {Mode::DELUGE, "deluge"}};
		expect(row->choices.size()).to_equal(std::size(choices));
		for (const Choice& want : choices) {
			const size_t index = std::to_underlying(want.mode);
			if (index < row->choices.size()) {
				expect(std::string{row->choices[index]}).to_equal(std::string{want.word});
			}
		}
	});

	it("holds each CV output's and each gate output's rows, the clock's and the run's gates with a third choice", _ {
		for (size_t output = 0; output < kCvOutputCount; ++output) {
			const std::string n = std::to_string(output);
			const SettingInfo* volts = deluge::plugin::find_setting(device_table(), "cv/" + n + "/voltsPerOctave");
			const SettingInfo* transpose = deluge::plugin::find_setting(device_table(), "cv/" + n + "/transpose");
			expect(volts).not_().to_be_null(n);
			expect(transpose).not_().to_be_null(n);
			if (volts != nullptr && transpose != nullptr) {
				expect(std::vector{volts->min, volts->max, volts->default_value}).to_equal(std::vector{0, 200, 100});
				expect(std::vector{transpose->min, transpose->max, transpose->default_value})
				    .to_equal(std::vector{-9600, 9600, 0});
			}
			expect(std::string{device_path::cv_volts_per_octave(output).value_or(device_path::kUseSharps).text()})
			    .to_equal("cv/" + n + "/voltsPerOctave");
			expect(std::string{device_path::cv_transpose(output).value_or(device_path::kUseSharps).text()})
			    .to_equal("cv/" + n + "/transpose");
		}
		expect(device_path::cv_transpose(kCvOutputCount).has_value()).to_be_false();
		for (size_t output = 0; output < kGateOutputCount; ++output) {
			const std::string path = "gate/" + std::to_string(output) + "/mode";
			const SettingInfo* mode = deluge::plugin::find_setting(device_table(), path);
			expect(mode).not_().to_be_null(path);
			if (mode == nullptr) {
				continue;
			}
			std::vector<std::string> words;
			for (const std::string_view word : mode->choices) {
				words.emplace_back(word);
			}
			std::vector<std::string> want{"vTrig", "sTrig"};
			if (output == kClockGateOutput) {
				want.emplace_back("clock");
			}
			else if (output == kRunGateOutput) {
				want.emplace_back("run");
			}
			expect(words).to_equal(want, path);
			expect(mode->default_value).to_equal(0);
			expect(std::string{device_path::gate_mode(output).value_or(device_path::kUseSharps).text()}).to_equal(path);
		}
		expect(device_path::gate_mode(kGateOutputCount).has_value()).to_be_false();
		const SettingInfo* off = deluge::plugin::find_setting(device_table(), device_path::kGateMinOffTime);
		expect(off).not_().to_be_null();
		if (off != nullptr) {
			expect(std::vector{off->min, off->max, off->default_value}).to_equal(std::vector{1, 100, 10});
		}
	});

	it("makes a row's path only from its own constants: no text converts to one", _ {
		static_assert(!std::is_constructible_v<DevicePath, std::string_view>);
		static_assert(!std::is_constructible_v<DevicePath, const char*>);
		static_assert(!std::is_convertible_v<std::string_view, DevicePath>);
		expect(std::string{device_path::kPlayCursor.text()}).to_equal(std::string{"pads/playCursor"});
		expect(std::string{device_path::feature(Feature::QUANTIZE).text()}).to_equal(std::string{"features/quantize"});
	});

	it("names no disabled-by-default row past the preset scales", _ {
		expect(device_path::disabled_scale(static_cast<size_t>(deluge::music::NUM_PRESET_SCALES)).has_value())
			.to_be_false();
	});

	it("holds a row per community feature, of its kind, its words and its default", _ {
		for (const FeatureInfo& info : kFeatures) {
			const std::string_view path = device_path::feature(info.id);
			expect(std::string{path}).to_equal(std::string{device_path::kFeaturePrefix} + std::string{info.path});
			expect(feature_at(path)).to_equal(&info);
			const SettingInfo* row = deluge::plugin::find_setting(device_table(), path);
			expect(row).not_().to_be_null(std::string{path});
			if (row == nullptr) {
				continue;
			}
			expect(row->kind).to_equal(is_toggle(info) ? SettingKind::BOOL : SettingKind::CHOICE);
			expect(row->default_value).to_equal(static_cast<int32_t>(info.default_value));
			if (row->kind == SettingKind::CHOICE) {
				expect(row->choices.data()).to_equal(info.choices.data());
				expect(row->choices.size()).to_equal(info.choices.size());
			}
		}
		expect(std::string{device_path::feature(Feature::HORIZONTAL_MENUS)})
		    .to_equal(std::string{"features/enableHorizontalMenus"});
		expect(feature_at("features/noSuchFeature")).to_be_null();
		expect(feature_at("enableHorizontalMenus")).to_be_null();
		expect(feature_at(device_path::kPlayCursor)).to_be_null();
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
