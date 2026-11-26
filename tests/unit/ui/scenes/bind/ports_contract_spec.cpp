#include "fakes.h"
#include "port_contracts.h"

#include "support/printers/bind_addresses_printer.h"
#include "support/printers/controls_printer.h"
#include "support/printers/learn_input_printer.h"
#include "support/printers/mapping_printer.h"
#include "support/printers/transport_action_printer.h"

#include "project/device_settings.h"
#include "project/features.h"
#include "project/transport.h"
#include "ui/scenes/bind/device_reader.h"

#include <cppspec.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

using namespace deluge::ui::scenes::bind;
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

/// @brief The info the failing waveforms fakes answer at every address: a finished mono file of 1000 frames.
constexpr WaveformInfo kThousandFrames{.length_samples = 1000, .sample_rate = 44100, .channels = 1, .identity = 1};

/// @brief A broken waveforms port: it answers a column past the end PENDING, so a caller would wait for a column that
///        never comes.
class EagerWaveforms final : public Waveforms {
	[[nodiscard]] std::optional<WaveformInfo> info(const WaveformAddress&) const override { return kThousandFrames; }
	[[nodiscard]] deluge::ui::views::waveform::PeakRead peaks(const WaveformAddress&,
	                                                          deluge::ui::views::waveform::Window at) const override {
		deluge::ui::views::waveform::PeakRead read;
		for (int32_t c = 0; c < deluge::ui::views::waveform::kColumns; c++) {
			if (at.scroll + int64_t{c} * at.zoom < static_cast<int64_t>(kThousandFrames.length_samples)) {
				read.set(c, deluge::ui::views::waveform::ColumnRead::of({.min = -1, .max = 1}));
			}
			else {
				read.set(c, deluge::ui::views::waveform::ColumnRead::pending());
			}
		}
		return read;
	}
	[[nodiscard]] std::optional<int64_t> play_position(const WaveformAddress&) const override { return std::nullopt; }
};

/// @brief A broken waveforms port: each read answers every column another peak.
class FlakyWaveforms final : public Waveforms {
	[[nodiscard]] std::optional<WaveformInfo> info(const WaveformAddress&) const override { return kThousandFrames; }
	[[nodiscard]] deluge::ui::views::waveform::PeakRead peaks(const WaveformAddress&,
	                                                          deluge::ui::views::waveform::Window) const override {
		deluge::ui::views::waveform::PeakRead read;
		reads_++;
		for (int32_t c = 0; c < deluge::ui::views::waveform::kColumns; c++) {
			read.set(c, deluge::ui::views::waveform::ColumnRead::of({.min = -reads_, .max = reads_}));
		}
		return read;
	}
	[[nodiscard]] std::optional<int64_t> play_position(const WaveformAddress&) const override { return std::nullopt; }
	mutable int32_t reads_{0};
};

/// @brief A broken waveforms port: its info names no waveform (identity 0), so two files could not be told apart.
class UnnamedWaveforms final : public Waveforms {
	[[nodiscard]] std::optional<WaveformInfo> info(const WaveformAddress&) const override {
		WaveformInfo unnamed = kThousandFrames;
		unnamed.identity = 0;
		return unnamed;
	}
	[[nodiscard]] deluge::ui::views::waveform::PeakRead peaks(const WaveformAddress&,
	                                                          deluge::ui::views::waveform::Window) const override {
		return deluge::ui::views::waveform::PeakRead::all_past_end();
	}
	[[nodiscard]] std::optional<int64_t> play_position(const WaveformAddress&) const override { return std::nullopt; }
};

/// @brief A broken waveforms port: it answers every column past the end over a loaded file.
class BlankWaveforms final : public Waveforms {
	[[nodiscard]] std::optional<WaveformInfo> info(const WaveformAddress&) const override { return kThousandFrames; }
	[[nodiscard]] deluge::ui::views::waveform::PeakRead peaks(const WaveformAddress&,
	                                                          deluge::ui::views::waveform::Window) const override {
		return deluge::ui::views::waveform::PeakRead::all_past_end();
	}
	[[nodiscard]] std::optional<int64_t> play_position(const WaveformAddress&) const override { return std::nullopt; }
};

/// @brief A broken audio port: its clock steps back on every read.
class BackwardsClock final : public AudioFacts {
	[[nodiscard]] bool audio_inputs_present() const override { return false; }
	[[nodiscard]] int32_t live_voice_count() const override { return 0; }
	[[nodiscard]] bool line_in_plugged() const override { return false; }
	[[nodiscard]] int32_t song_threshold_recording() const override { return 0; }
	[[nodiscard]] deluge::plugin::SyncType reverb_sidechain_sync_type() const override {
		return deluge::plugin::SyncType::EVEN;
	}
	[[nodiscard]] bool audio_busy() const override { return false; }
	[[nodiscard]] ForegroundRecording foreground_recording() const override { return ForegroundRecording::NONE; }
	[[nodiscard]] bool resampling() const override { return false; }
	[[nodiscard]] std::optional<InputTake> input_take() const override { return std::nullopt; }
	[[nodiscard]] std::optional<std::string> input_take_path() const override { return std::nullopt; }
	[[nodiscard]] uint32_t audio_sample_timer() const override { return now_--; }
	mutable uint32_t now_{1000};
};

/// @brief A broken take port: its finish leaves a take held on the audio fake, as though it had begun one.
class HeldTakeActions final : public TakeActions {
public:
	explicit HeldTakeActions(test::FakeAudioFacts& audio) : audio_{audio} {}
	[[nodiscard]] bool begin(TakeChannels) override { return false; }
	void end_soon() override {}
	[[nodiscard]] InputLevels take_levels() override { return {}; }
	[[nodiscard]] bool take_clipped() override { return false; }
	void finish() override { audio_.take = InputTake{.channels = 1}; }
	void orphan() override {}

private:
	test::FakeAudioFacts& audio_;
};

/// @brief A broken take port: it answers a clip, or a level, with no take held.
class ChattyTakeActions final : public TakeActions {
public:
	ChattyTakeActions(bool clipped, InputLevels levels) : clipped_{clipped}, levels_{levels} {}
	[[nodiscard]] bool begin(TakeChannels) override { return false; }
	void end_soon() override {}
	[[nodiscard]] InputLevels take_levels() override { return levels_; }
	[[nodiscard]] bool take_clipped() override { return clipped_; }
	void finish() override {}
	void orphan() override {}

private:
	bool clipped_;
	InputLevels levels_;
};

/// @brief A broken indicators port: clearing the tick squares starts the transport fake playing.
class PlayingIndicators final : public Indicators {
public:
	explicit PlayingIndicators(test::FakeTransport& transport) : transport_{transport} {}
	void clear_tick_squares() override { transport_.is_playing = true; }

private:
	test::FakeTransport& transport_;
};

/// @brief A broken view port: ending an audition puts a popup up on the UI fake.
class PopupViewActions final : public ViewActions {
public:
	explicit PopupViewActions(test::FakeUiFacts& ui) : ui_{ui} {}
	void end_audition(AuditionEnd) override { ui_.popup = true; }

private:
	test::FakeUiFacts& ui_;
};

/// @brief A broken transport-actions port: a tempo grab starts the transport fake playing, whatever clip it names.
class PlayingTempoGrab final : public TransportActions {
public:
	explicit PlayingTempoGrab(test::FakeTransport& transport) : transport_{transport} {}
	void play() override {}
	void record() override {}
	void tap_tempo(bool) override {}
	void toggle_metronome() override {}
	void clear_tempo_automation() override {}
	void show_swing_interval() override {}
	void show_tempo() override {}
	void undo(bool) override {}
	void begin_output_recording() override {}
	void end_recording_soon() override {}
	void stop_output_recording_at_loop_end() override {}
	void fill(bool) override {}
	void grab_tempo(deluge::project::ClipAddress) override { transport_.is_playing = true; }

private:
	test::FakeTransport& transport_;
};

/// @brief A broken transport-actions port: a tempo grab writes the song's tempo, whatever clip it names.
class TempoWritingGrab final : public TransportActions {
public:
	explicit TempoWritingGrab(deluge::project::Transport& record) : record_{record} {}
	void play() override {}
	void record() override {}
	void tap_tempo(bool) override {}
	void toggle_metronome() override {}
	void clear_tempo_automation() override {}
	void show_swing_interval() override {}
	void show_tempo() override {}
	void undo(bool) override {}
	void begin_output_recording() override {}
	void end_recording_soon() override {}
	void stop_output_recording_at_loop_end() override {}
	void fill(bool) override {}
	void grab_tempo(deluge::project::ClipAddress) override { record_.samples_per_timer_tick_big += uint64_t{1} << 32; }

private:
	deluge::project::Transport& record_;
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
	it("answers the clip navigation as the spec set it, none by default, and holds its contract", _ {
		test::FakeHost host;
		const Ports p = host.ports();
		expect(p.clips->clip_navigation().has_value()).to_be_false();
		expect(contract::clip_navigation_is_stable(*p.clips)).to_be_true();
		const ClipNavigation set{.x_scroll = 96, .x_zoom = 6, .triplets_on = true, .triplets_level = 8};
		host.clips.navigation = set;
		expect(p.clips->clip_navigation()).to_equal(std::optional{set});
		expect(contract::clip_navigation_is_stable(*p.clips)).to_be_true();
		// A zoom of no ticks breaks it.
		host.clips.navigation->x_zoom = 0;
		expect(contract::clip_navigation_is_stable(*p.clips)).to_be_false();
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
		expect(p.controls_sink->write_param(lpf, 7, 1, 24, deluge::ui::controls::Resolution::LEARNED)).to_be_true();
		expect(host.controls_sink.param_writes.size()).to_equal(size_t{1});
		if (host.controls_sink.param_writes.size() == 1) {
			const auto& write = host.controls_sink.param_writes[0];
			expect(write.address).to_equal(lpf);
			expect(write.value).to_equal(7);
			expect(write.resolution).to_equal(deluge::ui::controls::Resolution::LEARNED);
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
		expect(contract::follow_device_without_channel_reads_back(*p.learn, *p.midi, deluge::midi::kDin)).to_be_true();
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
	it("reads one port through an installed set or none, and holds the device actions', feedback's and references' contract", _ {
		test::FakeHost host;
		const Ports p = host.ports();
		expect(port_of(nullptr, &Ports::audio)).to_be_null();
		expect(port_of(&p, &Ports::audio)).to_equal(p.audio);
		expect(port_of(&p, &Ports::device_actions)).to_equal(p.device_actions);

		expect(p.device_actions).not_().to_be_null();
		p.device_actions->save_settings();
		expect(host.device_actions.calls.size()).to_equal(size_t{1});
		if (host.device_actions.calls.size() == 1) {
			expect(host.device_actions.calls[0]).to_equal(test::RecordingDeviceActions::Kind::SAVE_SETTINGS);
		}

		expect(p.audio->audio_busy()).to_be_false();
		host.audio.busy = true;
		expect(p.audio->audio_busy()).to_be_true();

		expect(p.midi_feedback).not_().to_be_null();
		expect(contract::unfollowable_param_has_no_cc(*p.midi_feedback)).to_be_true();
		const deluge::project::ParamTarget followed = deluge::project::engine_target(7);
		host.midi_feedback.ccs.emplace_back(deluge::project::OwnerPlugin::INSTRUMENT, followed, uint8_t{74});
		expect(p.midi_feedback->cc_of(deluge::project::OwnerPlugin::INSTRUMENT, followed)).to_equal(std::optional<uint8_t>{74});
		expect(p.midi_feedback->cc_of(deluge::project::OwnerPlugin::GLOBAL_EFFECTABLE, followed).has_value()).to_be_false();
		expect(p.midi_feedback->cc_of(deluge::project::OwnerPlugin::INSTRUMENT, deluge::project::engine_target(8)).has_value())
		    .to_be_false();
		expect(contract::unfollowable_param_has_no_cc(*p.midi_feedback)).to_be_true();
		host.midi_feedback.on = true;
		host.midi_feedback.global_context = true;
		expect(p.midi_feedback->enabled()).to_be_true();
		expect(p.midi_feedback->global_effectable_context()).to_be_true();
		p.midi_feedback->send(74, -64);
		p.midi_feedback->send_all(true);
		using FeedbackCall = test::RecordingMidiFeedback::Call;
		expect(host.midi_feedback.calls.size()).to_equal(size_t{2});
		if (host.midi_feedback.calls.size() == 2) {
			expect(host.midi_feedback.calls[0].kind).to_equal(FeedbackCall::Kind::SEND);
			expect(host.midi_feedback.calls[0].cc).to_equal(uint8_t{74});
			expect(host.midi_feedback.calls[0].knob_pos).to_equal(-64);
			expect(host.midi_feedback.calls[1].kind).to_equal(FeedbackCall::Kind::SEND_ALL);
			expect(host.midi_feedback.calls[1].automation).to_be_true();
		}

		expect(contract::connected_devices_have_references(*p.midi)).to_be_true();
		host.midi.connect(deluge::midi::DeviceId{40}, "Hosted");
		const std::optional<deluge::project::MidiDeviceRef> hosted = p.midi->reference(deluge::midi::DeviceId{40});
		expect(hosted.has_value()).to_be_true();
		if (hosted) {
			expect(hosted->port).to_equal(deluge::project::MidiDeviceRef::Port::HOSTED);
			expect(hosted->name).to_equal(std::string{"Hosted"});
		}
		expect(contract::connected_devices_have_references(*p.midi)).to_be_true();
		// A listed device with no reference fails the case.
		host.midi.refs.erase(40);
		expect(contract::connected_devices_have_references(*p.midi)).to_be_false();
	});
	it("answers the transport as the spec set it, flips it on PLAY and the metronome toggle, and records every action", _ {
		test::FakeHost host;
		const Ports p = host.ports();
		expect(p.transport).not_().to_be_null();
		expect(p.transport_actions).not_().to_be_null();
		expect(contract::transport_recording_is_a_mode(*p.transport)).to_be_true();
		expect(contract::count_in_is_a_count(*p.transport)).to_be_true();
		host.transport.count_in = 3;
		expect(p.transport->count_in_visual()).to_equal(3);
		expect(contract::count_in_is_a_count(*p.transport)).to_be_true();
		host.transport.clocks = true;
		host.transport.mode = TransportRecording::ARRANGEMENT;
		host.transport.undo_ok = true;
		host.transport.overdubs = true;
		expect(p.transport->clocks_active()).to_be_true();
		expect(p.transport->recording()).to_equal(TransportRecording::ARRANGEMENT);
		expect(p.transport->undo_allowed()).to_be_true();
		expect(p.transport->overdubs_pending()).to_be_true();
		expect(contract::transport_recording_is_a_mode(*p.transport)).to_be_true();
		// Recording to the arrangement with the arrangement's mode off breaks the arrangement case.
		expect(p.transport->arrangement_mode()).to_be_false();
		expect(contract::arrangement_mode_is_stable(*p.transport)).to_be_false();
		host.transport.arrangement = true;
		expect(p.transport->arrangement_mode()).to_be_true();
		expect(contract::arrangement_mode_is_stable(*p.transport)).to_be_true();
		host.transport.mode = TransportRecording::NORMAL;
		host.transport.arrangement = false;
		expect(contract::arrangement_mode_is_stable(*p.transport)).to_be_true();
		host.transport.mode = TransportRecording::ARRANGEMENT;
		host.transport.arrangement = true;

		expect(contract::metronome_toggles_read_back(*p.transport_actions, *p.transport)).to_be_true();
		expect(p.transport->playing()).to_be_false();
		p.transport_actions->play();
		expect(host.transport.is_playing).to_be_true();
		p.transport_actions->record();
		p.transport_actions->tap_tempo(true);
		p.transport_actions->clear_tempo_automation();
		p.transport_actions->show_swing_interval();
		p.transport_actions->show_tempo();
		p.transport_actions->undo(true);
		p.transport_actions->begin_output_recording();
		p.transport_actions->end_recording_soon();
		p.transport_actions->stop_output_recording_at_loop_end();
		p.transport_actions->fill(true);
		deluge::project::Transport record{.samples_per_timer_tick_big = uint64_t{20} << 32};
		expect(contract::grab_tempo_off_an_audio_clip_does_nothing(*p.transport_actions, *p.transport, record,
		                                                           deluge::project::ClipAddress{.clip_id = 9}))
		    .to_be_true();
		using Call = test::RecordingTransportActions::Call;
		using K = Call::Kind;
		const std::vector<Call> expected{
		    {.kind = K::TOGGLE_METRONOME},
		    {.kind = K::TOGGLE_METRONOME},
		    {.kind = K::PLAY},
		    {.kind = K::RECORD},
		    {.kind = K::TAP_TEMPO, .flag = true},
		    {.kind = K::CLEAR_TEMPO_AUTOMATION},
		    {.kind = K::SHOW_SWING_INTERVAL},
		    {.kind = K::SHOW_TEMPO},
		    {.kind = K::UNDO, .flag = true},
		    {.kind = K::BEGIN_OUTPUT_RECORDING},
		    {.kind = K::END_RECORDING_SOON},
		    {.kind = K::STOP_OUTPUT_RECORDING_AT_LOOP_END},
		    {.kind = K::FILL, .flag = true},
		    {.kind = K::GRAB_TEMPO, .clip = deluge::project::ClipAddress{.clip_id = 9}},
		};
		expect(host.transport_actions.calls).to_equal(expected);

		// A recorder with no transport flips nothing, so the read-back case fails on it.
		test::FakeTransport still;
		test::RecordingTransportActions unwired;
		expect(contract::metronome_toggles_read_back(unwired, still)).to_be_false();
		unwired.play();
		expect(still.is_playing).to_be_false();

		// A tempo grab that starts the transport, or writes the tempo, breaks the tempo-grab case; the recorder keeps it.
		PlayingTempoGrab grabbing{still};
		expect(contract::grab_tempo_off_an_audio_clip_does_nothing(grabbing, still, record,
		                                                           deluge::project::ClipAddress{.clip_id = 9}))
		    .to_be_false();
		still.is_playing = false;
		TempoWritingGrab writing{record};
		expect(contract::grab_tempo_off_an_audio_clip_does_nothing(writing, still, record,
		                                                           deluge::project::ClipAddress{.clip_id = 9}))
		    .to_be_false();
		expect(still.is_playing).to_be_false();
		expect(contract::grab_tempo_off_an_audio_clip_does_nothing(unwired, still, record,
		                                                           deluge::project::ClipAddress{.clip_id = 9}))
		    .to_be_true();
	});
	it("answers the foreground recorder's source and resampling as the spec set them, and records forgetting undo", _ {
		test::FakeHost host;
		const Ports p = host.ports();
		expect(p.audio->foreground_recording()).to_equal(ForegroundRecording::NONE);
		expect(p.audio->resampling()).to_be_false();
		for (const ForegroundRecording f : {ForegroundRecording::UNSET, ForegroundRecording::NONE,
		                                    ForegroundRecording::EXTERNAL, ForegroundRecording::INTERNAL}) {
			host.audio.foreground = f;
			expect(p.audio->foreground_recording()).to_equal(f);
			expect(contract::foreground_recording_is_a_source(*p.audio)).to_be_true();
		}
		host.audio.foreground = static_cast<ForegroundRecording>(4);
		expect(contract::foreground_recording_is_a_source(*p.audio)).to_be_false();
		host.audio.resample = true;
		expect(p.audio->resampling()).to_be_true();

		host.transport.is_playing = true;
		expect(contract::forgetting_undo_keeps_the_transport(*p.device_actions, *p.transport)).to_be_true();
		expect(host.device_actions.calls).to_equal(
		    std::vector<test::RecordingDeviceActions::Kind>{test::RecordingDeviceActions::Kind::DELETE_ALL_LOGS});
	});
	it("holds a take from begin to finish or orphan, reads it back on the audio fake, and holds the take contract", _ {
		test::FakeHost host;
		const Ports p = host.ports();
		expect(p.take_actions).to_equal(static_cast<TakeActions*>(&host.take_actions));
		expect(p.indicators).to_equal(static_cast<Indicators*>(&host.indicators));
		expect(p.view_actions).to_equal(static_cast<ViewActions*>(&host.view_actions));
		expect(contract::no_take_reads_nothing(*p.audio)).to_be_true();
		expect(contract::audio_clock_is_monotonic(*p.audio)).to_be_true();
		expect(contract::take_calls_without_a_take_do_nothing(*p.take_actions, *p.audio)).to_be_true();

		host.take_actions.calls.clear();
		expect(p.take_actions->begin(TakeChannels::STEREO)).to_be_true();
		expect(p.audio->input_take()).to_equal(std::optional{InputTake{.channels = 2}});
		expect(contract::no_take_reads_nothing(*p.audio)).to_be_false();
		host.take_actions.clipped = true;
		expect(p.take_actions->take_clipped()).to_be_true();
		expect(p.take_actions->take_clipped()).to_be_false();
		p.take_actions->finish();
		expect(p.audio->input_take().has_value()).to_be_false();
		expect(p.take_actions->begin(TakeChannels::MONO)).to_be_true();
		expect(host.take_actions.channels).to_equal(TakeChannels::MONO);
		p.take_actions->orphan();
		expect(p.audio->input_take().has_value()).to_be_false();
		using Kind = test::RecordingTakeActions::Kind;
		expect(host.take_actions.calls)
		    .to_equal(std::vector<Kind>{Kind::BEGIN, Kind::TAKE_CLIPPED, Kind::TAKE_CLIPPED, Kind::FINISH,
		                                Kind::BEGIN, Kind::ORPHAN});
		host.take_actions.begins = false;
		expect(p.take_actions->begin(TakeChannels::MONO)).to_be_false();
		expect(p.audio->input_take().has_value()).to_be_false();

		host.take_actions.clipped = true;
		host.take_actions.levels = {.left = 0.5F, .right = 0.25F};
		expect(p.take_actions->take_clipped()).to_be_false(); // no take held
		expect(p.take_actions->take_levels()).to_equal(InputLevels{});
		expect(contract::take_calls_without_a_take_do_nothing(*p.take_actions, *p.audio)).to_be_true();
		host.take_actions.calls.clear();

		p.indicators->clear_tick_squares();
		p.view_actions->end_audition(AuditionEnd::KeyboardWhenAuditioning);
		expect(host.indicators.tick_clears).to_equal(1);
		expect(host.view_actions.ends).to_equal(std::vector<AuditionEnd>{AuditionEnd::KeyboardWhenAuditioning});
		expect(host.order).to_equal(std::vector<std::string_view>{"begin", "begin", "begin", "ticks", "audition"});

		host.transport.is_playing = true;
		expect(contract::clearing_ticks_keeps_the_transport(*p.indicators, *p.transport)).to_be_true();
		expect(contract::ending_no_audition_changes_nothing(*p.view_actions, *p.ui)).to_be_true();
		expect(host.view_actions.ends.size()).to_equal(size_t{4});
	});
	it("fails each take case on a port that breaks it", _ {
		test::FakeHost host;
		host.audio.take_path = "SAMPLES/RECORD/REC00001.WAV";
		expect(contract::no_take_reads_nothing(host.audio)).to_be_false();
		host.audio.take_path.reset();
		host.audio.take = InputTake{.channels = 1};
		expect(contract::no_take_reads_nothing(host.audio)).to_be_false();
		host.audio.take.reset();
		expect(contract::audio_clock_is_monotonic(BackwardsClock{})).to_be_false();
		HeldTakeActions held{host.audio};
		expect(contract::take_calls_without_a_take_do_nothing(held, host.audio)).to_be_false();
		host.audio.take.reset();
		ChattyTakeActions clipping{true, {}};
		expect(contract::take_calls_without_a_take_do_nothing(clipping, host.audio)).to_be_false();
		ChattyTakeActions loud{false, {.left = 0.5F, .right = 0.F}};
		expect(contract::take_calls_without_a_take_do_nothing(loud, host.audio)).to_be_false();
		// Each broken take port keeps the audio port's own case, and a quiet one passes.
		expect(contract::no_take_reads_nothing(host.audio)).to_be_true();
		ChattyTakeActions quiet{false, {}};
		expect(contract::take_calls_without_a_take_do_nothing(quiet, host.audio)).to_be_true();
	});
	it("fails the indicators' and the root view's cases on a port that breaks each", _ {
		test::FakeHost host;
		PlayingIndicators playing{host.transport};
		expect(contract::clearing_ticks_keeps_the_transport(playing, host.transport)).to_be_false();
		PopupViewActions popup{host.ui};
		expect(contract::ending_no_audition_changes_nothing(popup, host.ui)).to_be_false();
		// With a note auditioning the case's precondition fails, whatever the port does.
		host.ui.popup = false;
		host.ui.auditioning_now = true;
		expect(contract::ending_no_audition_changes_nothing(host.view_actions, host.ui)).to_be_false();
		host.ui.auditioning_now = false;
		// Each broken port keeps the case of the other port, and the recorders pass both.
		host.transport.is_playing = false;
		expect(contract::clearing_ticks_keeps_the_transport(host.indicators, host.transport)).to_be_true();
		expect(contract::ending_no_audition_changes_nothing(host.view_actions, host.ui)).to_be_true();
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
	it("records each sample release, refuses the files in use, and answers the last sample path as the spec set", _ {
		test::FakeHost host;
		const Ports ports = host.ports();
		host.card_actions.in_use.insert("SAMPLES/USED.WAV");
		expect(ports.card_actions->release_sample_file("SAMPLES/FREE.WAV")).to_be_true();
		expect(ports.card_actions->release_sample_file("SAMPLES/USED.WAV")).to_be_false();
		expect(host.card_actions.released.size()).to_equal(std::size_t{2});
		expect(host.card_actions.released.front()).to_equal(std::string{"SAMPLES/FREE.WAV"});
		host.card.last_path = "SAMPLES/LAST.WAV";
		expect(ports.card->last_sample_path()).to_equal(std::string{"SAMPLES/LAST.WAV"});
		expect(contract::card_facts_are_stable(*ports.card)).to_be_true();
		ports.favourites->set(3, "SAMPLES/FAV.WAV");
		expect(ports.favourites->is_empty(3)).to_be_false();
		expect(ports.favourites->recall(3)).to_equal(std::string{"SAMPLES/FAV.WAV"});
	});

	it("lists through the catalogue fake, records each listing, and holds the catalogue contract", _ {
		test::FakeCatalogue catalogue;
		catalogue.entries = {{"a.wav", false}, {"B", true}, {"c.wav", false}};
		catalogue.folder = "SAMPLES";
		catalogue.next_outcome.error = 4;
		const ListingOutcome out = catalogue.list(ListingSpec{.step = ListingStep::UP_LEVEL, .kinds = FileKinds::AUDIO});
		expect(out.error.value_or(0)).to_equal(4);
		expect(catalogue.listed.size()).to_equal(1U);
		expect(static_cast<int>(catalogue.listed[0].kinds)).to_equal(static_cast<int>(FileKinds::AUDIO));
		expect(catalogue.dir()).to_equal(std::string_view{"SAMPLES"});
		expect(catalogue.entry(1)->folder).to_be_true();
		expect(catalogue.search("b").index).to_equal(1U);
		expect(catalogue.search("b").exact).to_be_true();
		expect(contract::catalogue_ends_at_its_count(catalogue)).to_be_true();
		expect(contract::catalogue_search_past_the_end(catalogue)).to_be_true();
		expect(contract::catalogue_claim_is_single(catalogue)).to_be_true();
		expect(contract::catalogue_erase_closes_the_gap(catalogue)).to_be_true();
		expect(catalogue.entry(0)->name).to_equal(std::string_view{"B"});
		expect(contract::catalogue_clear_empties(catalogue)).to_be_true();
		test::FakeHost host;
		expect(host.ports().catalogue).to_equal(static_cast<Catalogue*>(&host.catalogue));
	});

	it("answers the waveforms as the spec set them, records every read, and holds the waveforms contract", _ {
		using deluge::ui::views::waveform::ColumnRead;
		using deluge::ui::views::waveform::PeakRead;
		test::FakeHost host;
		const Ports p = host.ports();
		expect(p.waveforms).to_equal(static_cast<const Waveforms*>(&host.waveforms));
		const WaveformAddress range{.kind = WaveformAddress::Kind::RANGE,
		                            .slot = {deluge::project::SlotAddress::Kind::OUTPUT, 4}};
		const WaveformAddress nowhere{.kind = WaveformAddress::Kind::PREVIEW};
		test::FakeWaveforms::Entry entry{.at = range, .info = kThousandFrames, .position = 250};
		for (int32_t c = 0; c < deluge::ui::views::waveform::kColumns; c++) {
			entry.peaks[static_cast<size_t>(c)] = {.min = -c, .max = c};
		}
		entry.pending.set(3);
		entry.pending_reads = 1;
		host.waveforms.waveforms.push_back(entry);

		expect(host.waveforms.info(range)).to_equal(std::optional{kThousandFrames});
		expect(host.waveforms.play_position(range)).to_equal(std::optional<int64_t>{250});
		// Column 3 is pending on the first read alone; at 100 frames a column, columns 10 on lie past the end.
		const PeakRead first = host.waveforms.peaks(range, {.scroll = 0, .zoom = 100});
		expect(first.column(3)).to_equal(ColumnRead::pending());
		expect(first.column(2)).to_equal(ColumnRead::of({.min = -2, .max = 2}));
		expect(first.column(10)).to_equal(ColumnRead::past_end());
		const PeakRead second = host.waveforms.peaks(range, {.scroll = 0, .zoom = 100});
		expect(second.column(3)).to_equal(ColumnRead::of({.min = -3, .max = 3}));
		// A column wholly before the start is past the end; one straddling it is not.
		const PeakRead before = host.waveforms.peaks(range, {.scroll = -150, .zoom = 100});
		expect(before.column(0)).to_equal(ColumnRead::past_end());
		expect(before.column(1)).to_equal(ColumnRead::of({.min = -1, .max = 1}));
		expect(host.waveforms.reads.size()).to_equal(size_t{3});
		if (host.waveforms.reads.size() == 3) {
			expect(host.waveforms.reads[2])
			    .to_equal(test::FakeWaveforms::Read{.at = range, .in = {.scroll = -150, .zoom = 100}});
		}

		expect(contract::waveforms_unknown_address_reads_nothing(host.waveforms, nowhere)).to_be_true();
		expect(contract::waveforms_empty_window_reads_past_end(host.waveforms, range)).to_be_true();
		expect(contract::waveforms_columns_follow_the_length(host.waveforms, range)).to_be_true();
		expect(contract::waveforms_peaks_are_stable(host.waveforms, range)).to_be_true();
		expect(contract::waveforms_info_is_consistent(host.waveforms, range)).to_be_true();

		// A take: columns that do not end by the captured frames are past the end, the straddling one too.
		const WaveformAddress take{.kind = WaveformAddress::Kind::AUDIO_CLIP, .clip = {35}};
		WaveformInfo recording = kThousandFrames;
		recording.recording = true;
		host.waveforms.waveforms.push_back({.at = take, .info = recording, .captured = 550});
		const PeakRead taken = host.waveforms.peaks(take, {.scroll = 0, .zoom = 100});
		expect(taken.column(4).state()).to_equal(deluge::ui::views::waveform::ColumnState::PEAK);
		expect(taken.column(5)).to_equal(ColumnRead::past_end());
		expect(taken.column(9)).to_equal(ColumnRead::past_end());
	});
	it("fails each waveforms case on a port that breaks it", _ {
		const WaveformAddress range{.kind = WaveformAddress::Kind::RANGE};
		// Eager answers info everywhere, a peak at zoom 0, and PENDING past the end.
		expect(contract::waveforms_unknown_address_reads_nothing(EagerWaveforms{}, range)).to_be_false();
		expect(contract::waveforms_empty_window_reads_past_end(EagerWaveforms{}, range)).to_be_false();
		expect(contract::waveforms_columns_follow_the_length(EagerWaveforms{}, range)).to_be_false();
		expect(contract::waveforms_peaks_are_stable(FlakyWaveforms{}, range)).to_be_false();
		expect(contract::waveforms_info_is_consistent(UnnamedWaveforms{}, range)).to_be_false();
		// Nothing drawn inside a loaded file.
		expect(contract::waveforms_columns_follow_the_length(BlankWaveforms{}, range)).to_be_false();
		expect(contract::waveforms_peaks_are_stable(BlankWaveforms{}, range)).to_be_false();
		// Each broken fake keeps the cases it does not break.
		expect(contract::waveforms_info_is_consistent(EagerWaveforms{}, range)).to_be_true();
		expect(contract::waveforms_peaks_are_stable(EagerWaveforms{}, range)).to_be_true();
		expect(contract::waveforms_empty_window_reads_past_end(BlankWaveforms{}, range)).to_be_true();
		expect(contract::waveforms_info_is_consistent(BlankWaveforms{}, range)).to_be_true();
	});
});

CPPSPEC_SPEC(ports_contract)
