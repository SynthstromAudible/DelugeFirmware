#include "port_contracts.h"
#include "scenes/menu/bind_host.h"
#include "scenes/menu/bindings.h"
#include "scenes/menu/context.h"
#include "scenes/menu/nodes/settings.h"

#include "deluge-project/device_settings.h"
#include "gui/colour/pad_colour.h"
#include "gui/ui/shell_host_ui.h"
#include "hid/display/screensaver.h"
#include "hid/led/pad_leds.h"
#include "instruments/deluge/cables.h"
#include "io/usb/usb_audio.h"
#include "model/settings/runtime_feature_settings.h"
#include "storage/flash_storage.h"

#include "dsp-next/core/stereo_sample.h"
#include "model/song/song.h"
#include "music-theory/preset_scales.h"
#include "playback/playback_handler.h"
#include "plugin-api/types/display_mapping.h"
#include "processing/engines/audio_engine.h"
#include "processing/engines/cv_engine.h"
#include "processing/metronome/metronome.h"
#include "processing/stem_export/stem_export.h"

#include <version.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "support/song_fixture.h"

#include "cppspec.hpp"

#include "support/spec_main.h"

using namespace deluge::scenes;

// The main loop's battery voltage (src/deluge/deluge.cpp), declared at global scope as the host does.
extern uint16_t batteryMV;

// clang-format off
describe ports_contract_host("the app's ports keep the contract their fakes keep", ${
	it("holds every contract case on the app's ports", _ {
		const bind::Ports& p = menu::host_ports();
		expect(p.audio).not_().to_be_null();
		expect(p.card).not_().to_be_null();
		expect(p.ui).not_().to_be_null();
		expect(p.sound).not_().to_be_null();
		expect(p.clips).not_().to_be_null();
		expect(p.tracks).not_().to_be_null();
		expect(p.controls).not_().to_be_null();
		expect(p.clip_writes).not_().to_be_null();
		expect(p.sound_writes).not_().to_be_null();
		expect(p.learn).not_().to_be_null();
		expect(p.midi).not_().to_be_null();
		expect(p.writes).not_().to_be_null();
		expect(p.controls_sink).not_().to_be_null();
		expect(p.device).not_().to_be_null();
		if (p.audio != nullptr) {
			expect(bind::contract::voice_count_is_a_count(*p.audio)).to_be_true();
			expect(bind::contract::audio_runtime_facts_are_choices(*p.audio)).to_be_true();
			expect(p.audio->line_in_plugged()).to_equal(AudioEngine::lineInPluggedIn);
			if (p.writes != nullptr) {
				// The threshold lives on the song: a missing one is a fixture problem, not a broken port.
				expect(currentSong).not_().to_be_null();
				// Writes the engine's and song's run-time state; the case puts both back.
				expect(bind::contract::runtime_writes_are_read_back(*p.writes, *p.audio)).to_be_true();
			}
		}
		if (p.card != nullptr) {
			expect(bind::contract::card_facts_are_stable(*p.card)).to_be_true();
		}
		if (p.ui != nullptr) {
			expect(bind::contract::ui_facts_are_stable(*p.ui)).to_be_true();
			expect(bind::contract::firmware_version_is_a_stable_view(*p.ui)).to_be_true();
			// The legacy sources themselves; the voltage is set to a value the ADC could report, then put back.
			expect(std::string{p.ui->firmware_version()}).to_equal(std::string{kFirmwareVersionString});
			const uint16_t saved_mv = batteryMV;
			batteryMV = 3712;
			expect(p.ui->battery_millivolts()).to_equal(3712);
			batteryMV = saved_mv;
			expect(p.ui->battery_millivolts()).to_equal(static_cast<int32_t>(saved_mv));
		}
		if (p.sound != nullptr) {
			// A slot id no song holds.
			expect(bind::contract::unknown_slot_is_empty(*p.sound, deluge::project::SlotAddress{.id = 0xFFFFFF}))
			    .to_be_true();
		}
		if (p.clips != nullptr) {
			// A clip id the default card's song does not hold.
			expect(bind::contract::unknown_clip_is_silent(*p.clips, deluge::project::ClipAddress{.clip_id = 0xFFFF'FFF0}))
			    .to_be_true();
		}
		if (p.clip_writes != nullptr) {
			// A clip id the default card's song does not hold.
			expect(bind::contract::unknown_clip_is_refused(*p.clip_writes, deluge::project::ClipAddress{.clip_id = 0xFFFF'FFF0}))
			    .to_be_true();
		}
		if (p.sound_writes != nullptr) {
			// An output id no song holds.
			expect(bind::contract::unknown_sound_is_refused(
			           *p.sound_writes,
			           deluge::project::SlotAddress{deluge::project::SlotAddress::Kind::OUTPUT, 0xFFFF'FFF0}))
			    .to_be_true();
			expect(bind::contract::velocity_of_no_device_is_refused(*p.sound_writes)).to_be_true();
		}
		if (p.learn != nullptr) {
			// An output id no song holds.
			expect(bind::contract::unknown_owner_knob_is_refused(
			           *p.learn, deluge::project::OwnerParamTarget{
			                         {deluge::project::ControlsAddress::Kind::OUTPUT, 0xFFFF'FFF0},
			                         deluge::project::engine_target(3)}))
			    .to_be_true();
		}
		if (p.midi != nullptr) {
			// DIN is always listed on the default card's rig.
			expect(p.midi->connected().empty()).to_be_false();
			expect(bind::contract::connected_devices_are_known(*p.midi)).to_be_true();
			expect(bind::contract::write_to_no_device_is_refused(*p.midi)).to_be_true();
		}
		if (p.writes != nullptr) {
			// A sound and a clip no song holds.
			expect(bind::contract::unknown_slot_write_is_refused(
			           *p.writes, deluge::project::SlotAddress{deluge::project::SlotAddress::Kind::OUTPUT, 0xFFFF'FFF0},
			           deluge::project::ClipAddress{.clip_id = 0xFFFF'FFF0}))
			    .to_be_true();
		}
		// A param of an output no song holds.
		const deluge::project::ParamAddress nowhere = deluge::project::ParamAddress::of(
		    deluge::project::OwnerId::instrument(0xFFFF'FFF0), deluge::project::engine_target(3));
		if (p.controls != nullptr) {
			expect(bind::contract::unknown_param_has_no_value(*p.controls, nowhere)).to_be_true();
		}
		if (p.controls_sink != nullptr) {
			expect(bind::contract::unknown_param_write_is_refused(*p.controls_sink, nowhere)).to_be_true();
		}
		if (p.tracks != nullptr) {
			// An output id no song holds.
			expect(bind::contract::unknown_chain_has_no_effects(
			           *p.tracks, deluge::project::ChainAddress{deluge::project::ChainAddress::Kind::OUTPUT, 0xFFFF'FFF0}))
			    .to_be_true();
		}
	});
	it("keeps the device's settings where the device keeps them, and puts back what it wrote", _ {
		const bind::Ports& p = menu::host_ports();
		expect(p.device).not_().to_be_null();
		if (p.device == nullptr) {
			return;
		}
		using device_path = deluge::project::device_path;
		expect(bind::contract::device_unknown_path_reads_nothing(*p.device)).to_be_true();
		expect(bind::contract::device_unknown_path_write_is_refused(*p.device)).to_be_true();
		expect(bind::contract::device_out_of_range_write_is_refused(*p.device)).to_be_true();
		expect(bind::contract::device_write_reads_back(*p.device)).to_be_true();
		expect(bind::contract::device_reader_reads_the_rows(*p.device)).to_be_true();
		expect(bind::contract::device_typed_reads_match(*p.device)).to_be_true();
		// Every row the host keeps reads back what it stored. A write that sets a second row (sticky shift lights the
		// shift LED) is put back by writing every row again in the table's order, which ends as it began.
		const auto before = bind::contract::device_values(*p.device);
		for (const deluge::plugin::SettingInfo& row : deluge::project::device_table()) {
			expect(bind::contract::device_row_reads_back(*p.device, row.path)).to_be_true(std::string{row.path});
		}
		size_t at = 0;
		for (const deluge::plugin::SettingInfo& row : deluge::project::device_table()) {
			if (const std::optional<int32_t> was = before[at++]) {
				(void)p.device->write(row.path, deluge::plugin::to_menu(row, *was));
			}
		}
		const auto after = bind::contract::device_values(*p.device);
		for (size_t row = 0; row < before.size(); ++row) {
			expect(after[row].value_or(-1)).to_equal(before[row].value_or(-1), std::string{deluge::project::device_table()[row].path});
		}

		// The community features are the legacy settings' values; a sticky-shift write lights the shift LED too.
		{
			using deluge::project::Feature;
			const std::string_view sticky = device_path::feature(Feature::SHIFT_IS_STICKY);
			const std::string_view light = device_path::feature(Feature::LIGHT_SHIFT_LED);
			const std::string_view overlay = device_path::feature(Feature::SHORTCUT_OVERLAY);
			const uint32_t saved_sticky = runtimeFeatureSettings.get(RuntimeFeatureSettingType::ShiftIsSticky);
			const uint32_t saved_light = runtimeFeatureSettings.get(RuntimeFeatureSettingType::LightShiftLed);
			const uint32_t saved_overlay = runtimeFeatureSettings.get(RuntimeFeatureSettingType::ShortcutOverlay);
			runtimeFeatureSettings.set(RuntimeFeatureSettingType::LightShiftLed, RuntimeFeatureStateToggle::Off);
			expect(p.device->read(light).value_or(-1)).to_equal(0);
			expect(static_cast<int>(p.device->write(sticky, 1))).to_equal(static_cast<int>(bind::DeviceWrite::STORED));
			expect(runtimeFeatureSettings.get(RuntimeFeatureSettingType::ShiftIsSticky)).to_equal(uint32_t{1});
			expect(p.device->read(light).value_or(-1)).to_equal(1);
			// A number the file held that no value names: an on/off feature is on only at 1, as every reader of it
			// tests (`RuntimeFeatureSettings::isOn`), and a feature with choices reads clamped into them.
			runtimeFeatureSettings.set(RuntimeFeatureSettingType::LightShiftLed, 2);
			expect(p.device->read(light).value_or(-1)).to_equal(0);
			expect(runtimeFeatureSettings.isOn(RuntimeFeatureSettingType::LightShiftLed)).to_be_false();
			runtimeFeatureSettings.set(RuntimeFeatureSettingType::ShortcutOverlay, 7);
			expect(p.device->read(overlay).value_or(-1))
			    .to_equal(static_cast<int32_t>(deluge::project::ShortcutOverlay::MENU_AND_CLIP));
			expect(runtimeFeatureSettings.get(RuntimeFeatureSettingType::ShortcutOverlay)).to_equal(uint32_t{7});
			(void)p.device->write(sticky, static_cast<int32_t>(saved_sticky != 0));
			runtimeFeatureSettings.set(RuntimeFeatureSettingType::LightShiftLed, saved_light);
			runtimeFeatureSettings.set(RuntimeFeatureSettingType::ShortcutOverlay, saved_overlay);
		}

		// The legacy sources themselves, each put back.
		const uint8_t cursor = PadLEDs::flashCursor;
		PadLEDs::flashCursor = FLASH_CURSOR_FAST;
		expect(p.device->read(device_path::kPlayCursor).value_or(-1)).to_equal(0);
		// A flash byte past the choices reads clamped to the last, and the page shows it as Slow, as a choice item's
		// widget clamps it.
		PadLEDs::flashCursor = 5;
		expect(p.device->read(device_path::kPlayCursor).value_or(-1)).to_equal(2);
		menu::MenuContext on_device{};
		on_device.ports = menu::host_ports();
		const int32_t shown = menu::bindings::read(menu::nodes::flash_status, on_device);
		expect(shown).to_equal(2);
		std::array<std::string_view, menu::kMaxDynamicNames> names{};
		std::array<deluge::ui::shell::TextScratch, menu::kMaxDynamicNames> scratch{};
		const size_t count = menu::bindings::names(menu::nodes::flash_status, on_device, names, scratch);
		expect(count).to_equal(size_t{3});
		if (shown >= 0 && static_cast<size_t>(shown) < count) {
			expect(std::string{names[static_cast<size_t>(shown)]}).to_equal(std::string{"Slow"});
		}
		expect(static_cast<int>(PadLEDs::flashCursor)).to_equal(5); // reading heals nothing
		PadLEDs::flashCursor = cursor;
		const deluge::instrument::Polarity polarity = FlashStorage::defaultPatchCablePolarity;
		expect(static_cast<int>(p.device->write(device_path::kPatchCablePolarity, 0)))
		    .to_equal(static_cast<int>(bind::DeviceWrite::STORED));
		expect(static_cast<int>(FlashStorage::defaultPatchCablePolarity))
		    .to_equal(static_cast<int>(deluge::instrument::Polarity::UNIPOLAR));
		FlashStorage::defaultPatchCablePolarity = polarity;
		// The row's words are the cable's own.
		const deluge::plugin::SettingInfo* polarity_row =
		    deluge::plugin::find_setting(deluge::project::device_table(), device_path::kPatchCablePolarity);
		expect(polarity_row).not_().to_be_null();
		if (polarity_row != nullptr) {
			std::vector<std::string> words;
			for (const std::string_view word : polarity_row->choices) {
				words.emplace_back(word);
			}
			expect(words).to_equal(std::vector<std::string>{
			    std::string{deluge::instrument::to_string(deluge::instrument::Polarity::UNIPOLAR)},
			    std::string{deluge::instrument::to_string(deluge::instrument::Polarity::BIPOLAR)}});
		}
		// A shortcut byte other than V1's reads as the current layout, as the shortcut lookups read it.
		const uint8_t version = FlashStorage::shortcutsVersion;
		FlashStorage::shortcutsVersion = 7;
		expect(p.device->read(device_path::kShortcutsVersion).value_or(-1))
		    .to_equal(static_cast<int32_t>(deluge::project::ShortcutsVersion::V3));
		FlashStorage::shortcutsVersion = version;
		const std::string scale_path = std::string{device_path::kDisabledScalePrefix} + "3";
		const bool disabled = FlashStorage::defaultDisabledPresetScales[3];
		expect(static_cast<int>(p.device->write(scale_path, disabled ? 0 : 1)))
		    .to_equal(static_cast<int>(bind::DeviceWrite::STORED));
		expect(static_cast<bool>(FlashStorage::defaultDisabledPresetScales[3])).to_equal(!disabled);
		FlashStorage::defaultDisabledPresetScales[3] = disabled;
	});

	it("does what each defaults write implies, as the defaults page's own items do", _ {
		const bind::Ports& p = menu::host_ports();
		expect(p.device).not_().to_be_null();
		if (p.device == nullptr) {
			return;
		}
		using device_path = deluge::project::device_path;
		spec::SongFixture f = spec::make_synth_song();
		const uint8_t velocity = FlashStorage::defaultVelocity;
		const int8_t magnitude = FlashStorage::defaultMagnitude;
		const uint8_t interval = FlashStorage::defaultSwingInterval;
		const ThresholdRecordingMode threshold = FlashStorage::defaultThresholdRecordingMode;
		const uint8_t hold = FlashStorage::defaultHoldTime;
		const int32_t hold_samples = FlashStorage::holdTime;
		const uint8_t scale = FlashStorage::defaultScale;
		const uint8_t metronome = FlashStorage::defaultMetronomeVolume;

		// The menu's bounds are main's.
		const auto bounds = [](deluge::project::DevicePath path) {
			const deluge::plugin::SettingInfo* row = deluge::plugin::find_setting(deluge::project::device_table(), path);
			return row != nullptr ? std::to_string(row->min) + " " + std::to_string(row->max) : std::string{"no row"};
		};
		expect(bounds(device_path::kMetronomeVolume))
		    .to_equal(std::to_string(kMinMenuMetronomeVolumeValue) + " " + std::to_string(kMaxMenuMetronomeVolumeValue));
		expect(bounds(device_path::kSwingInterval)).to_equal("0 " + std::to_string(deluge::plugin::kNumSwingIntervals - 1));

		// A default velocity reaches the open song's instruments; with no song, the flash setting alone.
		f.instrument->setDefaultVelocity(64);
		expect(static_cast<int>(p.device->write(device_path::kVelocity, 100))).to_equal(static_cast<int>(bind::DeviceWrite::STORED));
		expect(static_cast<int>(FlashStorage::defaultVelocity)).to_equal(100);
		expect(static_cast<int>(f.instrument->defaultVelocity)).to_equal(100);
		currentSong = nullptr;
		expect(static_cast<int>(p.device->write(device_path::kVelocity, 90))).to_equal(static_cast<int>(bind::DeviceWrite::STORED));
		expect(static_cast<int>(FlashStorage::defaultVelocity)).to_equal(90);
		expect(static_cast<int>(f.instrument->defaultVelocity)).to_equal(100);

		// A default threshold mode is the open song's mode too; with no song, the flash setting alone.
		expect(static_cast<int>(p.device->write(device_path::kThresholdRecording, 2))).to_equal(static_cast<int>(bind::DeviceWrite::STORED));
		expect(static_cast<int>(FlashStorage::defaultThresholdRecordingMode)).to_equal(static_cast<int>(ThresholdRecordingMode::MEDIUM));
		spec::make_current(f.song);
		f.song->thresholdRecordingMode = ThresholdRecordingMode::OFF;
		expect(static_cast<int>(p.device->write(device_path::kThresholdRecording, 3))).to_equal(static_cast<int>(bind::DeviceWrite::STORED));
		expect(static_cast<int>(FlashStorage::defaultThresholdRecordingMode)).to_equal(static_cast<int>(ThresholdRecordingMode::HIGH));
		expect(static_cast<int>(f.song->thresholdRecordingMode)).to_equal(static_cast<int>(ThresholdRecordingMode::HIGH));

		// The resolution moves the swing interval by the same delta, clamped.
		FlashStorage::defaultMagnitude = 2;
		FlashStorage::defaultSwingInterval = 6;
		(void)p.device->write(device_path::kMagnitude, 4);
		expect(static_cast<int>(FlashStorage::defaultMagnitude)).to_equal(4);
		expect(p.device->read(device_path::kSwingInterval).value_or(-1)).to_equal(4);
		(void)p.device->write(device_path::kMagnitude, 0);
		expect(p.device->read(device_path::kSwingInterval).value_or(-1)).to_equal(deluge::plugin::clamp_swing_interval(8));

		// The hold time is the press timer's hold in samples too.
		(void)p.device->write(device_path::kHoldTime, 10);
		expect(static_cast<int>(FlashStorage::defaultHoldTime)).to_equal(10);
		expect(FlashStorage::holdTime).to_equal(10 * kSampleRate / 20);

		// The metronome's volume is stored as the menu shows it, and the metronome clicks at it at once: a click rendered
		// after the write is as loud as one at that volume, which two volumes show is not every click's level.
		const auto click_peak = [] {
			AudioEngine::metronome.trigger(1U << 26);
			std::array<deluge::dsp::StereoSample, 128> block{};
			AudioEngine::metronome.render(block, deluge::dsp::q<27>(1.0));
			int64_t peak = 0;
			for (const deluge::dsp::StereoSample& frame : block) {
				peak = std::max(peak, std::abs(int64_t{frame.l.raw()}));
			}
			return peak;
		};
		AudioEngine::metronome.setVolume(7);
		const int64_t quiet = click_peak();
		AudioEngine::metronome.setVolume(40);
		const int64_t loud = click_peak();
		expect(quiet).not_().to_equal(loud);
		expect(quiet).not_().to_equal(int64_t{0});
		(void)p.device->write(device_path::kMetronomeVolume, 7);
		expect(static_cast<int>(FlashStorage::defaultMetronomeVolume)).to_equal(7);
		expect(click_peak()).to_equal(quiet);

		// The scale is the flash setting's code: the menu's choices skip the user scale.
		(void)p.device->write(device_path::kScale, 16);
		expect(static_cast<int>(FlashStorage::defaultScale))
		    .to_equal(static_cast<int>(deluge::music::scaleToFlashStorageCode(deluge::music::RANDOM_SCALE)));
		(void)p.device->write(device_path::kScale, 15);
		expect(static_cast<int>(FlashStorage::defaultScale))
		    .to_equal(static_cast<int>(deluge::music::scaleToFlashStorageCode(deluge::music::HIRAJOSHI_SCALE)));
		// A user scale's code reads as the slot before it, as main's item shows it.
		FlashStorage::defaultScale = deluge::music::scaleToFlashStorageCode(deluge::music::USER_SCALE);
		expect(p.device->read(device_path::kScale).value_or(-1)).to_equal(15);

		FlashStorage::defaultVelocity = velocity;
		FlashStorage::defaultMagnitude = magnitude;
		FlashStorage::defaultSwingInterval = interval;
		FlashStorage::defaultThresholdRecordingMode = threshold;
		FlashStorage::defaultHoldTime = hold;
		FlashStorage::holdTime = hold_samples;
		FlashStorage::defaultScale = scale;
		FlashStorage::defaultMetronomeVolume = metronome;
		AudioEngine::metronome.setVolume(metronome);
		// The last click still sounds: render it out, so no later spec hears it.
		for (int block = 0; AudioEngine::metronome.sounding() && block < 1024; ++block) {
			std::array<deluge::dsp::StereoSample, 128> scratch{};
			AudioEngine::metronome.render(scratch, deluge::dsp::q<27>(1.0));
		}
		expect(AudioEngine::metronome.sounding()).to_be_false();
	});

	it("does what each pads-and-screen write implies, as the settings pages' own items do", _ {
		const bind::Ports& p = menu::host_ports();
		expect(p.device).not_().to_be_null();
		if (p.device == nullptr) {
			return;
		}
		using device_path = deluge::project::device_path;
		const KeyboardLayoutType layout = FlashStorage::defaultKeyboardLayout;
		const bool velocity_glide = FlashStorage::keyboardFunctionsVelocityGlide;
		const bool modwheel_glide = FlashStorage::keyboardFunctionsModwheelGlide;
		const uint8_t brightness = FlashStorage::defaultPadBrightness;
		const ScreensaverMode screensaver = FlashStorage::screensaverMode;
		const uint8_t timeout = FlashStorage::screensaverTimeoutMinutes;
		const KeyboardLayout text_keyboard = FlashStorage::keyboardLayout;
		const uint8_t stopped = FlashStorage::padColours[FlashStorage::PAD_COLOUR_STOPPED];
		const uint8_t preview = FlashStorage::sampleBrowserPreviewMode;
		const FavouritesDefaultLayout favourites = FlashStorage::defaultFavouritesLayout;

		// A pad-grid layout the menu does not list reads as the last it lists, and reading it heals nothing.
		FlashStorage::defaultKeyboardLayout = KeyboardLayoutType::KeyboardLayoutTypeChordLibrary;
		expect(p.device->read(device_path::kKeyboardLayout).value_or(-1)).to_equal(2);
		expect(static_cast<int>(FlashStorage::defaultKeyboardLayout))
		    .to_equal(static_cast<int>(KeyboardLayoutType::KeyboardLayoutTypeChordLibrary));
		(void)p.device->write(device_path::kKeyboardLayout, 1);
		expect(static_cast<int>(FlashStorage::defaultKeyboardLayout))
		    .to_equal(static_cast<int>(KeyboardLayoutType::KeyboardLayoutTypeInKey));

		// The sidebars' rows hold "momentary", the flash settings the glide: each is the other's opposite.
		(void)p.device->write(device_path::kVelocityMomentary, 1);
		(void)p.device->write(device_path::kModwheelMomentary, 0);
		expect(FlashStorage::keyboardFunctionsVelocityGlide).to_be_false();
		expect(FlashStorage::keyboardFunctionsModwheelGlide).to_be_true();
		expect(p.device->read(device_path::kModwheelMomentary).value_or(-1)).to_equal(0);

		// The brightness shows four times its level; a device with no saved settings holds level 0, shown as 0. The
		// pads' dimmer taking a write is `menu_actions_spec`'s.
		const deluge::plugin::SettingInfo* brightness_row =
		    deluge::plugin::find_setting(deluge::project::device_table(), device_path::kPadBrightness);
		expect(brightness_row).not_().to_be_null();
		if (brightness_row != nullptr) {
			FlashStorage::defaultPadBrightness = 0;
			expect(deluge::plugin::to_menu(*brightness_row, p.device->read(device_path::kPadBrightness).value_or(-1)))
			    .to_equal(0);
			FlashStorage::defaultPadBrightness = 30; // past the brightest, shown as it is
			expect(deluge::plugin::to_menu(*brightness_row, p.device->read(device_path::kPadBrightness).value_or(-1)))
			    .to_equal(120);
		}
		(void)p.device->write(device_path::kPadBrightness, 44);
		expect(static_cast<int>(FlashStorage::defaultPadBrightness)).to_equal(11);

		// The screensaver's two rows re-arm the guest's screensaver at the write.
		(void)p.device->write(device_path::kScreensaverMode, 3);
		(void)p.device->write(device_path::kScreensaverTimeout, 7);
		expect(FlashStorage::screensaverMode).to_equal(ScreensaverMode::DELUGE);
		expect(static_cast<int>(FlashStorage::screensaverTimeoutMinutes)).to_equal(7);
		expect(shellHostUI.shell().screensaver_idle_ms()).to_equal(uint32_t{7 * 60000});
		(void)p.device->write(device_path::kScreensaverMode, 0);
		expect(shellHostUI.shell().screensaver_idle_ms()).to_equal(uint32_t{0});

		// The plain rows land where the device keeps them.
		(void)p.device->write(device_path::kTextKeyboardLayout, 2);
		expect(static_cast<int>(FlashStorage::keyboardLayout)).to_equal(static_cast<int>(KeyboardLayout::QWERTZ));
		(void)p.device->write(device_path::kPadColourStopped, 8);
		expect(static_cast<int>(FlashStorage::padColours[FlashStorage::PAD_COLOUR_STOPPED]))
		    .to_equal(static_cast<int>(deluge::gui::PINK));
		(void)p.device->write(device_path::kSamplePreview, 2);
		expect(static_cast<int>(FlashStorage::sampleBrowserPreviewMode)).to_equal(PREVIEW_ON);
		(void)p.device->write(device_path::kFavouritesLayout, 1);
		expect(static_cast<int>(FlashStorage::defaultFavouritesLayout))
		    .to_equal(static_cast<int>(FavouritesDefaultLayout::FavouritesDefaultLayoutFavoritesAndBanks));

		FlashStorage::defaultKeyboardLayout = layout;
		FlashStorage::keyboardFunctionsVelocityGlide = velocity_glide;
		FlashStorage::keyboardFunctionsModwheelGlide = modwheel_glide;
		FlashStorage::defaultPadBrightness = brightness;
		PadLEDs::setBrightnessLevel(brightness);
		FlashStorage::screensaverMode = screensaver;
		FlashStorage::screensaverTimeoutMinutes = timeout;
		deluge::hid::display::Screensaver::settingsChanged();
		FlashStorage::keyboardLayout = text_keyboard;
		FlashStorage::padColours[FlashStorage::PAD_COLOUR_STOPPED] = stopped;
		FlashStorage::sampleBrowserPreviewMode = preview;
		FlashStorage::defaultFavouritesLayout = favourites;
	});

	it("does what each audio, clock and stem-export write implies, as the settings pages' own items do", _ {
		const bind::Ports& p = menu::host_ports();
		expect(p.device).not_().to_be_null();
		if (p.device == nullptr) {
			return;
		}
		using device_path = deluge::project::device_path;
		const bool usb_enabled = FlashStorage::usbAudioEnabled;
		const bool direct_monitoring = deluge::usb_audio::directMonitoring;
		const deluge::usb_audio::OutputSource output_source = deluge::usb_audio::outputSource;
		const InputMonitoringMode monitoring = AudioEngine::inputMonitoringMode;
		const uint32_t in_ppqn = playbackHandler.analogInTicksPPQN;
		const uint32_t out_ppqn = playbackHandler.analogOutTicksPPQN;
		const bool clock_out = playbackHandler.midiOutClockEnabled;
		const bool clock_in = playbackHandler.midiInClockEnabled;
		const uint8_t count_in = playbackHandler.countInBars;
		const bool mixdown = stemExport.exportMixdown;
		const bool kit_fx = stemExport.includeKitFX;

		// USB audio's enable takes effect at the next start: a value that no longer matches what this session
		// presents is stored for a restart, and one that matches it is in effect now.
		const bool presented = deluge::usb_audio::present();
		expect(static_cast<int>(p.device->write(device_path::kUsbAudioEnabled, presented ? 0 : 1)))
		    .to_equal(static_cast<int>(bind::DeviceWrite::STORED_FOR_RESTART));
		expect(FlashStorage::usbAudioEnabled).to_equal(!presented);
		expect(static_cast<int>(p.device->write(device_path::kUsbAudioEnabled, presented ? 1 : 0)))
		    .to_equal(static_cast<int>(bind::DeviceWrite::STORED));
		expect(FlashStorage::usbAudioEnabled).to_equal(presented);

		// The audio tier's settings, each one plain store.
		(void)p.device->write(device_path::kUsbDirectMonitoring, 0);
		expect(deluge::usb_audio::directMonitoring).to_be_false();
		(void)p.device->write(device_path::kUsbOutputSource, 1);
		expect(static_cast<int>(deluge::usb_audio::outputSource))
		    .to_equal(static_cast<int>(deluge::usb_audio::OutputSource::LINE_IN));
		(void)p.device->write(device_path::kMonitorMode, 2);
		expect(static_cast<int>(AudioEngine::inputMonitoringMode)).to_equal(static_cast<int>(InputMonitoringMode::OFF));

		// The clocks. With no external clock playing, the trigger input's resync waits: the last swung tick stays.
		expect(currentSong).not_().to_be_null();
		const uint8_t playback_state = playbackHandler.playbackState;
		const bool analog_input = playbackHandler.usingAnalogClockInput;
		const int64_t input_tick = playbackHandler.lastInputTickReceived;
		const int64_t swung_tick = playbackHandler.lastSwungTickActioned;
		const int64_t out_tick = playbackHandler.lastTriggerClockOutTickDone;
		const GateType clock_gate = cvEngine.gateChannels[WHICH_GATE_OUTPUT_IS_CLOCK].mode;
		const bool clock_state = cvEngine.clockState;
		// No input tick received: following the external clock, the song is at internal tick 0.
		playbackHandler.lastInputTickReceived = 0;
		constexpr int64_t kStale = 777;
		playbackHandler.lastSwungTickActioned = kStale;
		(void)p.device->write(device_path::kTriggerInPpqn, 48);
		expect(playbackHandler.analogInTicksPPQN).to_equal(uint32_t{48});
		expect(playbackHandler.lastSwungTickActioned).to_equal(kStale);
		// Following the trigger clock, the song's ticks resync to the clock's at the write.
		playbackHandler.playbackState = PLAYBACK_CLOCK_EXTERNAL_ACTIVE;
		playbackHandler.usingAnalogClockInput = true;
		(void)p.device->write(device_path::kTriggerInPpqn, 24);
		expect(playbackHandler.analogInTicksPPQN).to_equal(uint32_t{24});
		expect(playbackHandler.lastSwungTickActioned).to_equal(int64_t{0});
		playbackHandler.playbackState = playback_state;
		playbackHandler.usingAnalogClockInput = analog_input;
		// With the clock output on, the output's tick count resyncs to the song's tick, 0, at the write.
		cvEngine.gateChannels[WHICH_GATE_OUTPUT_IS_CLOCK].mode = GateType::SPECIAL;
		playbackHandler.lastTriggerClockOutTickDone = kStale;
		(void)p.device->write(device_path::kTriggerOutPpqn, 96);
		expect(playbackHandler.analogOutTicksPPQN).to_equal(uint32_t{96});
		expect(playbackHandler.lastTriggerClockOutTickDone).to_equal(int64_t{0});
		playbackHandler.lastInputTickReceived = input_tick;
		playbackHandler.lastSwungTickActioned = swung_tick;
		playbackHandler.lastTriggerClockOutTickDone = out_tick;
		cvEngine.gateChannels[WHICH_GATE_OUTPUT_IS_CLOCK].mode = clock_gate;
		cvEngine.clockState = clock_state;
		(void)p.device->write(device_path::kMidiClockOut, clock_out ? 0 : 1);
		expect(playbackHandler.midiOutClockEnabled).to_equal(!clock_out);
		(void)p.device->write(device_path::kMidiClockIn, clock_in ? 0 : 1);
		expect(playbackHandler.midiInClockEnabled).to_equal(!clock_in);
		(void)p.device->write(device_path::kCountIn, 3);
		expect(static_cast<int>(playbackHandler.countInBars)).to_equal(3);

		// The stem export's options are the export's own.
		(void)p.device->write(device_path::kStemMixdown, mixdown ? 0 : 1);
		expect(stemExport.exportMixdown).to_equal(!mixdown);
		(void)p.device->write(device_path::kStemKitFx, kit_fx ? 0 : 1);
		expect(stemExport.includeKitFX).to_equal(!kit_fx);
		expect(p.device->read(device_path::kStemKitFx).value_or(-1)).to_equal(kit_fx ? 0 : 1);

		FlashStorage::usbAudioEnabled = usb_enabled;
		deluge::usb_audio::directMonitoring = direct_monitoring;
		deluge::usb_audio::outputSource = output_source;
		AudioEngine::inputMonitoringMode = monitoring;
		playbackHandler.analogInTicksPPQN = in_ppqn;
		playbackHandler.analogOutTicksPPQN = out_ppqn;
		playbackHandler.setMidiOutClockMode(clock_out);
		playbackHandler.setMidiInClockEnabled(clock_in);
		playbackHandler.countInBars = count_in;
		stemExport.exportMixdown = mixdown;
		stemExport.includeKitFX = kit_fx;
	});
});

// clang-format on

DELUGE_SPEC(ports_contract_spec, ports_contract_host)
