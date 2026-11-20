#include "port_contracts.h"
#include "scenes/menu/bind_host.h"

#include "deluge-project/device_settings.h"
#include "hid/led/pad_leds.h"
#include "instruments/deluge/cables.h"
#include "storage/flash_storage.h"

#include "model/song/song.h"
#include "processing/engines/audio_engine.h"

#include <version.h>

#include <string>
#include <string_view>
#include <vector>

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
			expect(bind::contract::flash_cursor_is_named(*p.ui)).to_be_true();
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
		namespace device_path = deluge::project::device_path;
		expect(bind::contract::device_unknown_path_reads_nothing(*p.device)).to_be_true();
		expect(bind::contract::device_unknown_path_write_is_refused(*p.device)).to_be_true();
		expect(bind::contract::device_out_of_range_write_is_refused(*p.device)).to_be_true();
		expect(bind::contract::device_write_reads_back(*p.device)).to_be_true();
		expect(bind::contract::device_reader_reads_the_rows(*p.device)).to_be_true();
		// Every row the host keeps reads back what it stored, each put back as it was.
		for (const deluge::plugin::SettingInfo& row : deluge::project::device_table()) {
			expect(bind::contract::device_row_reads_back(*p.device, row.path)).to_be_true(std::string{row.path});
		}

		// The legacy sources themselves, each put back.
		const uint8_t cursor = PadLEDs::flashCursor;
		PadLEDs::flashCursor = FLASH_CURSOR_FAST;
		expect(p.device->read(device_path::kPlayCursor).value_or(-1)).to_equal(0);
		PadLEDs::flashCursor = cursor;
		const deluge::instrument::Polarity polarity = FlashStorage::defaultPatchCablePolarity;
		expect(static_cast<int>(p.device->write(device_path::kPatchCablePolarity, 0)))
		    .to_equal(static_cast<int>(bind::DeviceWrite::Stored));
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
		    .to_equal(static_cast<int>(bind::DeviceWrite::Stored));
		expect(static_cast<bool>(FlashStorage::defaultDisabledPresetScales[3])).to_equal(!disabled);
		FlashStorage::defaultDisabledPresetScales[3] = disabled;
	});
});

// clang-format on

DELUGE_SPEC(ports_contract_spec, ports_contract_host)
