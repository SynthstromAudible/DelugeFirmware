#include "fakes.h"
#include "port_contracts.h"

#include "support/printers/bind_addresses_printer.h"
#include "support/printers/controls_printer.h"
#include "support/printers/learn_input_printer.h"
#include "support/printers/mapping_printer.h"

#include <cppspec.hpp>

using namespace deluge::scenes::bind;

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
		host.ui.cursor = FlashCursor::Off;
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
		expect(static_cast<int>(p.ui->flash_cursor())).to_equal(static_cast<int>(FlashCursor::Off));
		expect(static_cast<int>(p.ui->performance_editing_mode())).to_equal(static_cast<int>(PerformanceEditing::Param));
		expect(contract::ui_facts_are_stable(*p.ui)).to_be_true();
		expect(p.ui->battery_millivolts()).to_equal(3712);
		expect(std::string{p.ui->firmware_version()}).to_equal(std::string{"1.2.3-spec"});
		expect(contract::battery_and_firmware_are_readings(*p.ui)).to_be_true();
		host.ui.firmware = "";
		expect(contract::battery_and_firmware_are_readings(*p.ui)).to_be_false();
		host.ui.firmware = "1.2.3-spec";
		expect(contract::voice_count_is_a_count(*p.audio)).to_be_true();
		expect(contract::card_facts_are_stable(*p.card)).to_be_true();
		host.card.replacing = true;
		expect(p.card->output_may_be_replaced()).to_be_true();
		expect(contract::card_facts_are_stable(*p.card)).to_be_true();
		expect(contract::flash_cursor_is_named(*p.ui)).to_be_true();
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
});

CPPSPEC_SPEC(ports_contract)
