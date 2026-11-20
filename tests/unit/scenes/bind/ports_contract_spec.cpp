#include "fakes.h"
#include "port_contracts.h"

#include "support/printers/bind_addresses_printer.h"
#include "support/printers/controls_printer.h"
#include "support/printers/learn_input_printer.h"
#include "support/printers/mapping_printer.h"

#include "deluge-project/device_settings.h"
#include "deluge-project/features.h"
#include "scenes/bind/device_reader.h"

#include <cppspec.hpp>

#include <string_view>
#include <type_traits>

using namespace deluge::scenes::bind;
using device_path = deluge::project::device_path;

namespace {

/// @brief A broken device port: it reads every row as its default and keeps no write, so the write cases must fail
///        on it.
class ForgetfulDeviceSettings final : public DeviceSettings {
	[[nodiscard]] deluge::serialization::Value load(const deluge::plugin::SettingInfo& row) const override {
		return deluge::serialization::Value::makeNumber(row.default_value);
	}
	[[nodiscard]] DeviceWrite store(const deluge::plugin::SettingInfo&, const deluge::serialization::Value&) override {
		return DeviceWrite::STORED;
	}
};

} // namespace

// clang-format off
describe ports_contract("the scenes' ports, as their fakes answer them", ${
	it("answers what the spec set, and the contract holds", _ {
		test::FakeHost host;
		host.audio.inputs = true;
		host.audio.voices = 3;
		host.audio.line_in = true;
		host.audio.threshold = 2;
		host.audio.sidechain_type = deluge::plugin::SyncType::DOTTED;
		host.card.sample_loading = true;
		host.ui.performance = PerformanceEditing::Param;
		host.ui.battery_mv = 3712;
		host.ui.firmware = "1.2.3-spec";
		const Ports p = host.ports();
		expect(p.audio->audio_inputs_present()).to_be_true();
		expect(p.audio->live_voice_count()).to_equal(3);
		expect(p.audio->line_in_plugged()).to_be_true();
		expect(p.audio->song_threshold_recording()).to_equal(2);
		expect(static_cast<int>(p.audio->reverb_sidechain_sync_type()))
			.to_equal(static_cast<int>(deluge::plugin::SyncType::DOTTED));
		expect(contract::audio_runtime_facts_are_choices(*p.audio)).to_be_true();
		expect(p.card->sample_load_in_flight()).to_be_true();
		expect(p.card->output_may_be_replaced()).to_be_false();
		expect(static_cast<int>(p.ui->performance_editing_mode())).to_equal(static_cast<int>(PerformanceEditing::Param));
		expect(contract::ui_facts_are_stable(*p.ui)).to_be_true();
		expect(p.ui->battery_millivolts()).to_equal(3712);
		expect(std::string{p.ui->firmware_version()}).to_equal(std::string{"1.2.3-spec"});
		expect(contract::firmware_version_is_a_stable_view(*p.ui)).to_be_true();
		host.ui.firmware = "";
		expect(contract::firmware_version_is_a_stable_view(*p.ui)).to_be_false();
		host.ui.firmware = "1.2.3-spec";
		expect(contract::voice_count_is_a_count(*p.audio)).to_be_true();
		expect(contract::card_facts_are_stable(*p.card)).to_be_true();
		host.card.replacing = true;
		expect(p.card->output_may_be_replaced()).to_be_true();
		expect(contract::card_facts_are_stable(*p.card)).to_be_true();
	});
	it("answers a sound's facts as the spec set them, and an unknown slot as empty", _ {
		test::FakeHost host;
		host.sound.loaded[7] = 0b10;
		host.sound.identities[{7, 0, 2}] = 99;
		const Ports p = host.ports();
		expect(p.sound->loaded_sources(deluge::project::SlotAddress{.id = 7})).to_equal(uint8_t{0b10});
		expect(p.sound->range_identity(deluge::project::SlotAddress{.id = 7}, 0, 2).value_or(0)).to_equal(uint64_t{99});
		expect(contract::unknown_slot_is_empty(*p.sound, deluge::project::SlotAddress{.id = 12345})).to_be_true();
	});
	it("answers a clip's play state as the spec set it, and an unknown clip as silent", _ {
		test::FakeHost host;
		host.clips.playing_at[5] = 96;
		const Ports p = host.ports();
		expect(p.clips->clip_playing(deluge::project::ClipAddress{.clip_id = 5})).to_be_true();
		expect(p.clips->clip_live_pos(deluge::project::ClipAddress{.clip_id = 5})).to_equal(96);
		expect(contract::unknown_clip_is_silent(*p.clips, deluge::project::ClipAddress{.clip_id = 54321})).to_be_true();
	});
	it("answers the tracks as the spec set them, and an unknown chain as having no effects", _ {
		test::FakeHost host;
		host.tracks.track_list = {{.output_id = 3, .kind = deluge::project::TrackKind::SYNTH}};
		const Ports p = host.ports();
		expect(p.tracks->tracks().size()).to_equal(size_t{1});
		expect(contract::unknown_chain_has_no_effects(
		           *p.tracks, deluge::project::ChainAddress{deluge::project::ChainAddress::Kind::OUTPUT, 999}))
		    .to_be_true();
	});
	it("records each clip edit in order with its arguments, and answers as the spec set", _ {
		test::FakeHost host;
		const Ports p = host.ports();
		const deluge::project::ClipAddress clip{.clip_id = 4};
		expect(p.clip_writes->resize_audio_clip(clip, ResizeKind::STRETCH, 192, 96)).to_be_true();
		expect(host.clip_writes.calls.size()).to_equal(size_t{1});
		if (host.clip_writes.calls.size() == 1) {
			const auto& call = host.clip_writes.calls[0];
			expect(call.kind).to_equal(test::RecordingClipWrites::Call::Kind::RESIZE);
			expect(call.clip).to_equal(clip);
			expect(call.resize).to_equal(ResizeKind::STRETCH);
			expect(call.value).to_equal(int64_t{192});
			expect(call.old_length).to_equal(96);
		}
		host.clip_writes.accept = false;
		expect(contract::unknown_clip_is_refused(*p.clip_writes, deluge::project::ClipAddress{.clip_id = 54321})).to_be_true();
	});
	it("records each sound edit in order with its arguments, and answers as the spec set", _ {
		test::FakeHost host;
		const Ports p = host.ports();
		const deluge::project::SlotAddress slot{deluge::project::SlotAddress::Kind::OUTPUT, 7};
		expect(p.sound_writes->insert_range(slot, 1, 2, 60, int16_t{48})).to_be_true();
		expect(host.sound_writes.calls.size()).to_equal(size_t{1});
		if (host.sound_writes.calls.size() == 1) {
			const auto& call = host.sound_writes.calls[0];
			expect(call.kind).to_equal(test::RecordingSoundWrites::Call::Kind::INSERT_RANGE);
			expect(call.slot).to_equal(slot);
			expect(call.source).to_equal(uint8_t{1});
			expect(call.range).to_equal(std::optional<uint16_t>{2});
			expect(call.value).to_equal(int64_t{60});
			expect(call.neighbour_top).to_equal(std::optional<int16_t>{48});
		}
		expect(p.sound_writes->load_dx_program(slot, 1, 5)).to_be_true();
		expect(host.sound_writes.calls.size()).to_equal(size_t{2});
		if (host.sound_writes.calls.size() == 2) {
			expect(host.sound_writes.calls[0].kind).to_equal(test::RecordingSoundWrites::Call::Kind::INSERT_RANGE);
			expect(host.sound_writes.calls[1].kind).to_equal(test::RecordingSoundWrites::Call::Kind::DX_PROGRAM);
			expect(host.sound_writes.calls[1].value).to_equal(int64_t{5});
		}
		expect(p.sound_writes->velocity_to_level_everywhere(deluge::midi::kUpstreamUsb2)).to_be_true();
		expect(host.sound_writes.calls.size()).to_equal(size_t{3});
		if (host.sound_writes.calls.size() == 3) {
			expect(host.sound_writes.calls[2].kind).to_equal(test::RecordingSoundWrites::Call::Kind::VELOCITY_EVERYWHERE);
			expect(host.sound_writes.calls[2].device.value).to_equal(deluge::midi::kUpstreamUsb2.value);
		}
		host.sound_writes.accept = false;
		expect(contract::velocity_of_no_device_is_refused(*p.sound_writes)).to_be_true();
		expect(contract::unknown_sound_is_refused(*p.sound_writes, deluge::project::SlotAddress{deluge::project::SlotAddress::Kind::OUTPUT, 54321}))
		    .to_be_true();
	});
	it("records each learn in order with its arguments, and answers as the spec set", _ {
		test::FakeHost host;
		const Ports p = host.ports();
		const deluge::project::OwnerParamTarget knob{
		    {deluge::project::ControlsAddress::Kind::OUTPUT, 7}, deluge::project::engine_target(3)};
		expect(p.learn->learn_knob(knob, deluge::midi::kDin, 74, 3)).to_be_true();
		expect(host.learn.calls.size()).to_equal(size_t{1});
		if (host.learn.calls.size() == 1) {
			const auto& call = host.learn.calls[0];
			expect(call.kind).to_equal(test::RecordingLearnSink::Call::Kind::LEARN_KNOB);
			expect(call.target.has_value()).to_be_true();
			if (call.target) {
				expect(*call.target).to_equal(deluge::project::MappingTarget{knob});
			}
			expect(call.device.value).to_equal(deluge::midi::kDin.value);
			expect(call.which_knob).to_equal(74);
			expect(call.midi_channel).to_equal(3);
		}
		const LearnInput input{.device = deluge::midi::kUpstreamUsb1, .channel_or_zone = 4};
		const deluge::project::MappingTarget track{deluge::project::TrackInputTarget{9}};
		expect(p.learn->learn(track, deluge::project::MidiCommand{.channel_or_zone = 4, .note_or_cc = 60}, input))
		    .to_be_true();
		expect(host.learn.calls.size()).to_equal(size_t{2});
		if (host.learn.calls.size() == 2) {
			const auto& call = host.learn.calls[1];
			expect(call.kind).to_equal(test::RecordingLearnSink::Call::Kind::LEARN);
			expect(call.target.has_value()).to_be_true();
			if (call.target) {
				expect(*call.target).to_equal(track);
			}
			expect(call.command.has_value()).to_be_true();
			if (call.command) {
				expect(*call.command).to_equal(deluge::project::MidiCommand{.channel_or_zone = 4, .note_or_cc = 60});
			}
			expect(call.input.has_value()).to_be_true();
			if (call.input) {
				expect(*call.input).to_equal(input);
			}
		}
		host.learn.accept = false;
		expect(contract::unknown_owner_knob_is_refused(*p.learn, knob)).to_be_true();
	});
	it("records each menu write in order with its arguments", _ {
		test::FakeHost host;
		const Ports p = host.ports();
		const Target name{.kind = Target::Kind::MODEL, .path = "name"};
		const Addresses where{.clip = deluge::project::ClipAddress{5}};
		expect(p.writes->write_text(name, where, "Lead")).to_be_true();
		p.writes->set_default_audio_input_channel(3);
		p.writes->set_song_threshold_recording(2);
		p.writes->set_reverb_sidechain_sync_type(deluge::plugin::SyncType::TRIPLET);
		expect(host.writes.gestures.size()).to_equal(size_t{4});
		if (host.writes.gestures.size() == 4) {
			expect(host.writes.gestures[2].kind).to_equal(test::RecordingWrites::Gesture::Kind::THRESHOLD_RECORDING);
			expect(host.writes.gestures[2].value).to_equal(int64_t{2});
			expect(host.writes.gestures[3].kind).to_equal(test::RecordingWrites::Gesture::Kind::SIDECHAIN_SYNC_TYPE);
			expect(host.writes.gestures[3].value).to_equal(
			    int64_t{std::to_underlying(deluge::plugin::SyncType::TRIPLET)});
			const auto& text = host.writes.gestures[0];
			expect(text.kind).to_equal(test::RecordingWrites::Gesture::Kind::TEXT);
			expect(text.target).to_equal(name);
			expect(text.where).to_equal(where);
			expect(text.text).to_equal(std::string{"Lead"});
			expect(host.writes.gestures[1].kind).to_equal(test::RecordingWrites::Gesture::Kind::DEFAULT_INPUT_CHANNEL);
			expect(host.writes.gestures[1].value).to_equal(int64_t{3});
		}
		host.writes.accept = false;
		expect(contract::unknown_slot_write_is_refused(
		           *p.writes, deluge::project::SlotAddress{deluge::project::SlotAddress::Kind::OUTPUT, 54321},
		           deluge::project::ClipAddress{.clip_id = 54321}))
		    .to_be_true();
	});
	it("answers a param's value as the spec set it, and a param no song holds as having none", _ {
		test::FakeHost host;
		const deluge::project::ParamAddress lpf = deluge::project::ParamAddress::of(
		    deluge::project::OwnerId::instrument(7), deluge::project::engine_target(3));
		host.controls.param_values.emplace_back(lpf, 42);
		const Ports p = host.ports();
		expect(p.controls->param_value(lpf).value_or(0)).to_equal(42);
		expect(contract::unknown_param_has_no_value(
		           *p.controls, deluge::project::ParamAddress::of(deluge::project::OwnerId::instrument(54321),
		                                                         deluge::project::engine_target(3))))
		    .to_be_true();
	});
	it("records each knob write in order with its arguments, and answers take_for as the spec set", _ {
		test::FakeHost host;
		const Ports p = host.ports();
		const deluge::project::ParamAddress lpf =
		    deluge::project::ParamAddress::of(deluge::project::OwnerId::clip(4), deluge::project::engine_target(3));
		expect(p.controls_sink->write_param(lpf, 7, 1, 24, deluge::controls::Resolution::LEARNED)).to_be_true();
		expect(host.controls_sink.param_writes.size()).to_equal(size_t{1});
		if (host.controls_sink.param_writes.size() == 1) {
			const auto& write = host.controls_sink.param_writes[0];
			expect(write.address).to_equal(lpf);
			expect(write.value).to_equal(7);
			expect(write.resolution).to_equal(deluge::controls::Resolution::LEARNED);
		}
		expect(p.controls_sink->delete_knob_automation(lpf)).to_be_true();
		expect(host.controls_sink.gestures.size()).to_equal(size_t{1});
		if (host.controls_sink.gestures.size() == 1) {
			const auto& gesture = host.controls_sink.gestures[0];
			expect(gesture.kind).to_equal(test::RecordingControlsSink::Gesture::Kind::DELETE_KNOB_AUTOMATION);
			expect(gesture.address.has_value()).to_be_true();
			if (gesture.address) {
				expect(*gesture.address).to_equal(lpf);
			}
		}
		host.controls_sink.take_answer = 12;
		expect(p.controls_sink->take_for(9).value_or(0)).to_equal(uint32_t{12});
		expect(host.controls_sink.take_requests.size()).to_equal(size_t{1});
		host.controls_sink.accept = false;
		expect(contract::unknown_param_write_is_refused(
		           *p.controls_sink, deluge::project::ParamAddress::of(deluge::project::OwnerId::clip(54321),
		                                                              deluge::project::engine_target(3))))
		    .to_be_true();
	});
	it("answers the MIDI devices as the spec set them, and keeps a write to a known device", _ {
		test::FakeHost host;
		const Ports p = host.ports();
		expect(contract::connected_devices_are_known(*p.midi)).to_be_true();
		expect(p.midi->connected().size()).to_equal(size_t{1});
		expect(std::string{p.midi->name(deluge::midi::kDin)}).to_equal(std::string{"DIN"});

		std::optional<MidiDeviceSettings> din = p.midi->settings(deluge::midi::kDin);
		expect(din.has_value()).to_be_true();
		if (din) {
			expect(din->send_clock).to_be_false();
			din->send_clock = true;
			expect(p.midi->write(deluge::midi::kDin, *din)).to_be_true();
		}
		const std::optional<MidiDeviceSettings> back = p.midi->settings(deluge::midi::kDin);
		expect(back.has_value()).to_be_true();
		if (back) {
			expect(back->send_clock).to_be_true();
		}
		expect(host.midi.writes.size()).to_equal(size_t{1});

		expect(contract::write_to_no_device_is_refused(*p.midi)).to_be_true();
		expect(host.midi.writes.size()).to_equal(size_t{2});
		expect(p.midi->name(deluge::midi::kUpstreamUsb1).empty()).to_be_true();
	});
	it("records each global and follow learn, reads it back through the MIDI devices, and keeps none from an unknown device", _ {
		test::FakeHost host;
		const Ports p = host.ports();
		expect(p.midi->global_command(deluge::project::GlobalCommand::PLAY).has_value()).to_be_false();
		expect(contract::learns_read_back(*p.learn, *p.midi, deluge::midi::kDin)).to_be_true();
		expect(contract::unknown_device_learn_is_refused(*p.learn, *p.midi)).to_be_true();
		// Two learns, two unlearns and two restores, then the two refused learns, each recorded.
		expect(host.learn.calls.size()).to_equal(size_t{8});
		if (host.learn.calls.size() == 8) {
			expect(host.learn.calls[0].kind).to_equal(test::RecordingLearnSink::Call::Kind::LEARN_GLOBAL);
			expect(host.learn.calls[0].global).to_equal(deluge::project::GlobalCommand::TAP);
			expect(host.learn.calls[1].kind).to_equal(test::RecordingLearnSink::Call::Kind::LEARN_FOLLOW);
			expect(host.learn.calls[1].follow).to_equal(deluge::project::FollowInput::TRACK_2);
			expect(host.learn.calls[2].binding.has_value()).to_be_false();
		}
		// A sink that keeps nothing fails the read-back case.
		host.learn.devices = nullptr;
		expect(contract::learns_read_back(host.learn, host.midi, deluge::midi::kDin)).to_be_false();
	});
	it("keeps the device's settings by DEVICE row, checks every write, and holds the device contract", _ {
		test::FakeHost host;
		const Ports p = host.ports();
		expect(p.device).not_().to_be_null();
		expect(contract::device_unknown_path_reads_nothing(*p.device)).to_be_true();
		expect(contract::device_unknown_path_write_is_refused(*p.device)).to_be_true();
		expect(contract::device_out_of_range_write_is_refused(*p.device)).to_be_true();
		expect(contract::device_write_reads_back(*p.device)).to_be_true();
		expect(contract::device_reader_reads_the_rows(*p.device)).to_be_true();
		expect(contract::device_typed_reads_match(*p.device)).to_be_true();
		// Each write in range reached the fake's store (and was put back after).
		expect(host.device.stores.empty()).to_be_false();

		// Each row reads its default; a write stores its file form, in the row's stored units.
		expect(p.device->read(device_path::kPlayCursor).value_or(-1))
			.to_equal(static_cast<int32_t>(deluge::project::FlashCursor::SLOW));
		host.device.stores.clear();
		expect(static_cast<int>(p.device->write(device_path::kPlayCursor, 0))).to_equal(static_cast<int>(DeviceWrite::STORED));
		expect(host.device.stores.size()).to_equal(size_t{1});
		expect(host.device.stores.front().first).to_equal(std::string{device_path::kPlayCursor});
		expect(host.device.stores.front().second.str()).to_equal(std::string{"fast"});
		expect(p.device->read(device_path::kPlayCursor).value_or(-1)).to_equal(0);
		host.device.accept_restart.insert(std::string{device_path::kUseSharps});
		expect(static_cast<int>(p.device->write(device_path::kUseSharps, 0)))
			.to_equal(static_cast<int>(DeviceWrite::STORED_FOR_RESTART));

		// A value the row cannot name reads as the row's default, and is still a value the port holds.
		host.device.values.insert_or_assign(std::string{device_path::kPatchCablePolarity},
		                                    deluge::serialization::Value::makeScalar("tripolar"));
		expect(p.device->read(device_path::kPatchCablePolarity).value_or(-1)).to_equal(1);
		// An index past the row's choices is clamped into them, as a choice item's widget shows it.
		host.device.values.insert_or_assign(std::string{device_path::kPlayCursor},
		                                    deluge::serialization::Value::makeNumber(5));
		expect(p.device->read(device_path::kPlayCursor).value_or(-1)).to_equal(2);
		host.device.values.insert_or_assign(std::string{device_path::kPlayCursor},
		                                    deluge::serialization::Value::makeNumber(-3));
		expect(p.device->read(device_path::kPlayCursor).value_or(-1)).to_equal(0);
		expect(contract::device_reader_reads_the_rows(*p.device)).to_be_true();
	});
	it("fails the device write cases on a port that keeps no write", _ {
		ForgetfulDeviceSettings forgetful;
		expect(contract::device_write_reads_back(forgetful)).to_be_false();
		expect(contract::device_row_reads_back(forgetful, device_path::kPlayCursor)).to_be_false();
		// Its refusals are the front's, which no port can skip.
		expect(contract::device_unknown_path_write_is_refused(forgetful)).to_be_true();
		expect(contract::device_out_of_range_write_is_refused(forgetful)).to_be_true();
	});
	it("reads a device row typed: what the port holds, and with no port the row's default", _ {
		using deluge::project::Feature;
		using deluge::project::FlashCursor;
		using deluge::project::ShortcutsVersion;
		test::FakeHost host;
		const Ports p = host.ports();
		const deluge::project::DevicePath battery = device_path::feature(Feature::SHOW_BATTERY_LEVEL);
		expect(read_device_flag(p.device, battery)).to_be_true();
		expect(static_cast<int>(p.device->write(battery, 0))).to_equal(static_cast<int>(DeviceWrite::STORED));
		expect(read_device_flag(p.device, battery)).to_be_false();
		expect(static_cast<int>(p.device->write(device_path::kPlayCursor, 1))).to_equal(static_cast<int>(DeviceWrite::STORED));
		expect(static_cast<int>(read_device<FlashCursor>(p.device, device_path::kPlayCursor)))
			.to_equal(static_cast<int>(FlashCursor::OFF));
		expect(static_cast<int>(p.device->write(device_path::kShortcutsVersion, 0)))
			.to_equal(static_cast<int>(DeviceWrite::STORED));
		expect(static_cast<int>(read_device<ShortcutsVersion>(p.device, device_path::kShortcutsVersion)))
			.to_equal(static_cast<int>(ShortcutsVersion::V1));
		expect(contract::device_typed_reads_match(*p.device)).to_be_true();

		// Only a DevicePath reads: a path written as text does not compile.
		static_assert(!std::is_invocable_v<decltype(&read_device_flag), const DeviceSettings*, std::string_view>);
		static_assert(!std::is_invocable_v<decltype(&read_device_value), const DeviceSettings*, const char*>);

		// No port: every row reads its default.
		expect(static_cast<int>(read_device<FlashCursor>(nullptr, device_path::kPlayCursor)))
			.to_equal(static_cast<int>(FlashCursor::SLOW));
		expect(static_cast<int>(read_device<ShortcutsVersion>(nullptr, device_path::kShortcutsVersion)))
			.to_equal(static_cast<int>(ShortcutsVersion::V3));
		expect(read_device_flag(nullptr, battery)).to_be_true();
		expect(read_device_flag(nullptr, device_path::feature(Feature::ENABLE_DX7_ENGINE))).to_be_false();
	});
});

CPPSPEC_SPEC(ports_contract)
