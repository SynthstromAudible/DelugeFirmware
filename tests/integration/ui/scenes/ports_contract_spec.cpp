#include "port_contracts.h"
#include "support/peak_seed.h"
#include "ui/scenes/menu/bind_host.h"
#include "ui/scenes/menu/bindings.h"
#include "ui/scenes/menu/context.h"
#include "ui/scenes/menu/nodes/settings.h"
#include "ui/scenes/services/waveform_paint.h"

#include "ui/views/waveform/view.h"

#include "gui/colour/pad_colour.h"
#include "gui/ui/shell_host/shell_host.h"
#include "gui/ui/shell_host_ui.h"
#include "gui/ui/shell_slice_ops.h"
#include "gui/waveform/waveform_renderer.h"
#include "hid/display/screensaver.h"
#include "hid/led/pad_leds.h"
#include "instruments/classic/cables.h"
#include "io/midi/midi_device_manager.h"
#include "io/midi/midi_engine.h"
#include "io/midi/midi_follow.h"
#include "io/midi/midi_transpose.h"
#include "io/usb/usb_audio.h"
#include "model/action/action_logger.h"
#include "model/settings/runtime_feature_settings.h"
#include "project/device_settings.h"
#include "storage/flash_storage.h"

#include "dsp/core/stereo_sample.h"
#include "libdeluge/cv_gate.h"
#include "model/clip/audio_clip.h"
#include "model/project_bridge/slot_bridge.h"
#include "model/sample/sample.h"
#include "model/sample/sample_recorder.h"
#include "model/sample/waveform_scan.h"
#include "model/song/song.h"
#include "music/preset_scales.h"
#include "playback/mode/arrangement.h"
#include "playback/mode/session.h"
#include "playback/playback_handler.h"
#include "plugin/types/display_mapping.h"
#include "processing/engines/audio_engine.h"
#include "processing/engines/cv_engine.h"
#include "processing/metronome/metronome.h"
#include "processing/recording/foreground_recording.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/source_resources.h"
#include "processing/stem_export/stem_export.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/cluster/cluster.h"
#include "storage/multi_range/multisample_range.h"

#include <version.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "support/record_state.h"
#include "support/song_fixture.h"
#include "support/torn_down_take.h"
#include "unit/ui/widgets/support/frames.h"

#include "cppspec.hpp"

#include "support/spec_main.h"

using namespace deluge::ui::scenes;

// The main loop's battery voltage (src/deluge/deluge.cpp), declared at global scope as the host does.
extern uint16_t batteryMV;

using bind::WaveformAddress;

namespace {

constexpr size_t kClusterSize = 32768;
constexpr int32_t kNumClusters = 32;
constexpr uint64_t kSampleLength = static_cast<uint64_t>(kClusterSize) * kNumClusters / 2; // 16-bit mono

/// A sample whose overview cache already holds every cluster's peaks, so a scan reads no card.
/// @param clusters   How many clusters the sample holds.
/// @param memory     Where to construct it; fresh memory when null.
/// @param first_peak Cluster 0's overview peak, in Q8.24 pad units' top byte; cluster i's is `first_peak + 3 * i`.
Sample* make_sample(int32_t clusters = kNumClusters, void* memory = nullptr, int32_t first_peak = 16) {
	Cluster::set_size(kClusterSize);
	if (memory == nullptr) {
		memory = deluge::memory::alloc_fast(sizeof(Sample));
	}
	auto* s = new (memory) Sample();
	s->byteDepth = 2;
	s->numChannels = 1;
	s->sampleRate = 44100;
	s->audioDataStartPosBytes = 0;
	const uint64_t length = static_cast<uint64_t>(kClusterSize) * clusters / 2;
	s->audioDataLengthBytes = length * 2;
	s->lengthInSamples = length;
	peak_seed::seed_peaks(*s, [&](int32_t i) {
		return std::pair<int8_t, int8_t>{static_cast<int8_t>(static_cast<int8_t>(-(first_peak + 3 * i))),
		                                 static_cast<int8_t>(static_cast<int8_t>(first_peak + 3 * i))};
	});
	return s;
}

/// The fixture's whole-sample zoom, `ceil(length / 16)`: two clusters a column.
constexpr int64_t kWholeZoom = static_cast<int64_t>(((kSampleLength - 1) >> 4) + 1);

using deluge::ui::views::waveform::ColumnRead;
using deluge::ui::views::waveform::ColumnState;
using deluge::ui::views::waveform::Peak;
using deluge::ui::views::waveform::PeakRead;
using deluge::ui::views::waveform::Window;

/// @brief A synth song whose source 0, range 0 holds a sample, made current for one example; the song before it is
///        put back after, and the range emptied.
struct RangeHeld {
	spec::SongFixture synth = spec::make_synth_song();
	Song* const saved = currentSong;
	MultisampleRange* multisample = nullptr;
	WaveformAddress address{};
	explicit RangeHeld(Sample* sample) {
		currentSong = synth.song;
		auto* sound = synth.instrument->toModControllable()->effectsOwner()->sound;
		deluge::test::set(sound->record(), "osc1/type", "sample");
		sound->deriveConfig();
		multisample = static_cast<MultisampleRange*>(sound->getOrCreateFirstRange(0));
		multisample->sampleHolder.audioFile = sample;
		address = {.kind = WaveformAddress::Kind::RANGE,
		           .slot = {deluge::project::SlotAddress::Kind::OUTPUT, *synth.instrument->id()},
		           .source = 0,
		           .range = 0};
	}
	~RangeHeld() {
		multisample->sampleHolder.audioFile = nullptr;
		currentSong = saved;
	}
	RangeHeld(const RangeHeld&) = delete;
	RangeHeld& operator=(const RangeHeld&) = delete;
};

/// @brief How many columns of @p read are in @p state.
int32_t count_of(const PeakRead& read, ColumnState state) {
	int32_t n = 0;
	for (int32_t c = 0; c < deluge::ui::views::waveform::kColumns; c++) {
		n += read.column(c).state() == state ? 1 : 0;
	}
	return n;
}

/// The browser's preview sound, as `AudioEngine::init` makes it (which nothing here runs), for one example; the
/// previous one is put back after.
struct PreviewSound {
	alignas(SoundDrum) char memory[sizeof(SoundDrum)]{};
	SoundDrum* const saved = AudioEngine::sampleForPreview;
	SoundDrum* drum = new (memory) SoundDrum();
	PreviewSound() {
		deluge::test::set(drum->record(), "osc1/type", "sample");
		drum->deriveConfig();
		AudioEngine::sampleForPreview = drum;
	}
	~PreviewSound() {
		AudioEngine::sampleForPreview = saved;
		drum->~SoundDrum();
	}
	PreviewSound(const PreviewSound&) = delete;
	PreviewSound& operator=(const PreviewSound&) = delete;
	/// Put @p sample in the preview's range, as the load of main's `SampleBrowser::previewIfPossible`
	/// (`origin/main:src/deluge/gui/ui/browser/sample_browser.cpp`) does.
	void hold(Sample* sample) {
		static_cast<MultisampleRange*>(drum->getOrCreateFirstRange(0))->sampleHolder.audioFile = sample;
	}
};

/// @brief The app's waveforms port, as the app's ports hold it.
const bind::Waveforms& app_waveforms() {
	return *menu::host_ports().waveforms;
}
} // namespace

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
		expect(p.device_actions).not_().to_be_null();
		expect(p.midi_feedback).not_().to_be_null();
		if (p.midi_feedback != nullptr) {
			expect(bind::contract::unfollowable_param_has_no_cc(*p.midi_feedback)).to_be_true();
			// The legacy sources themselves.
			expect(p.midi_feedback->enabled()).to_equal(midiFollow.isFeedbackEnabled());
			expect(p.midi_feedback->global_effectable_context()).to_equal(midiFollow.isGlobalEffectableContext());
		}
		expect(p.transport).not_().to_be_null();
		expect(p.transport_actions).not_().to_be_null();
		expect(p.waveforms).not_().to_be_null();
		expect(p.take_actions).not_().to_be_null();
		expect(p.indicators).not_().to_be_null();
		expect(p.view_actions).not_().to_be_null();
		if (p.audio != nullptr && p.take_actions != nullptr) {
			// No take is held between cases.
			expect(bind::contract::no_take_reads_nothing(*p.audio)).to_be_true();
			expect(bind::contract::audio_clock_is_monotonic(*p.audio)).to_be_true();
			expect(p.audio->audio_sample_timer()).to_equal(AudioEngine::audioSampleTimer);
			expect(bind::contract::take_calls_without_a_take_do_nothing(*p.take_actions, *p.audio)).to_be_true();
		}
		if (p.indicators != nullptr && p.transport != nullptr) {
			expect(bind::contract::clearing_ticks_keeps_the_transport(*p.indicators, *p.transport)).to_be_true();
		}
		if (p.view_actions != nullptr && p.ui != nullptr) {
			// Nothing auditions between cases.
			expect(p.ui->auditioning()).to_be_false();
			expect(bind::contract::ending_no_audition_changes_nothing(*p.view_actions, *p.ui)).to_be_true();
		}
		if (p.transport != nullptr) {
			expect(bind::contract::transport_recording_is_a_mode(*p.transport)).to_be_true();
			expect(bind::contract::count_in_is_a_count(*p.transport)).to_be_true();
			expect(p.transport->playing()).to_equal(playbackHandler.playbackState != 0);
			expect(p.transport->clocks_active()).to_equal(playbackHandler.isEitherClockActive());
			expect(static_cast<int>(std::to_underlying(p.transport->recording())))
			    .to_equal(static_cast<int>(playbackHandler.recording));
			expect(p.transport->undo_allowed()).to_equal(actionLogger.allowedToDoReversion());
			expect(p.transport->overdubs_pending())
			    .to_equal(currentSong != nullptr && currentSong->hasAnyPendingNextOverdubs());
			expect(p.transport->metronome_on()).to_equal(playbackHandler.metronomeOn);
			expect(p.transport->count_in_visual()).to_equal(playbackHandler.count_in_visual());
			expect(p.transport->arrangement_mode()).to_equal(currentPlaybackMode == &arrangement);
			// The spec process records nothing to the arrangement, so the case's second clause holds trivially here.
			expect(bind::contract::arrangement_mode_is_stable(*p.transport)).to_be_true();
			if (p.transport_actions != nullptr) {
				// Two toggles, each with its popup, leave the metronome as the other specs found it.
				expect(bind::contract::metronome_toggles_read_back(*p.transport_actions, *p.transport)).to_be_true();
			}
			if (p.device_actions != nullptr) {
				expect(bind::contract::forgetting_undo_keeps_the_transport(*p.device_actions, *p.transport)).to_be_true();
			}
		}
		if (p.audio != nullptr) {
			expect(bind::contract::foreground_recording_is_a_source(*p.audio)).to_be_true();
			expect(bind::contract::voice_count_is_a_count(*p.audio)).to_be_true();
			// The spec process holds the audio routine still, so the engine reads busy here; the port answers as it does.
			expect(p.audio->audio_busy()).to_equal(AudioEngine::audioBusy());
			expect(p.audio->resampling()).to_equal(deluge::recording::isCurrentlyResampling());
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
			expect(bind::contract::clip_navigation_is_stable(*p.clips)).to_be_true();
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
			expect(bind::contract::connected_devices_have_references(*p.midi)).to_be_true();
			expect(bind::contract::write_to_no_device_is_refused(*p.midi)).to_be_true();
			if (p.learn != nullptr && !p.midi->connected().empty()) {
				expect(bind::contract::learns_read_back(*p.learn, *p.midi, p.midi->connected().front())).to_be_true();
				expect(bind::contract::unknown_device_learn_is_refused(*p.learn, *p.midi)).to_be_true();
				expect(bind::contract::follow_device_without_channel_reads_back(*p.learn, *p.midi,
				                                                                p.midi->connected().front()))
				    .to_be_true();
			}
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
	it("answers the song's clip navigation, the arrangement's mode, and a tempo grab off an audio clip as nothing", _ {
		const bind::Ports& p = menu::host_ports();
		Song* const saved_song = currentSong;
		PlaybackMode* const saved_mode = currentPlaybackMode;
		spec::SongFixture synth = spec::make_synth_song(); // made current
		synth.song->persistentView.xScroll[NAVIGATION_CLIP] = 96;
		synth.song->persistentView.xZoom[NAVIGATION_CLIP] = 6;
		synth.song->persistentView.xScroll[NAVIGATION_ARRANGEMENT] = 4242;
		synth.song->persistentView.xZoom[NAVIGATION_ARRANGEMENT] = 99;
		synth.song->persistentView.tripletsOn = true;
		synth.song->persistentView.tripletsLevel = 8;
		expect(p.clips->clip_navigation())
		    .to_equal(std::optional{bind::ClipNavigation{.x_scroll = 96, .x_zoom = 6, .triplets_on = true, .triplets_level = 8}});
		expect(bind::contract::clip_navigation_is_stable(*p.clips)).to_be_true();

		currentPlaybackMode = &arrangement;
		expect(p.transport->arrangement_mode()).to_be_true();
		expect(bind::contract::arrangement_mode_is_stable(*p.transport)).to_be_true();
		currentPlaybackMode = &session;
		expect(p.transport->arrangement_mode()).to_be_false();
		expect(bind::contract::arrangement_mode_is_stable(*p.transport)).to_be_true();

		// The fixture's clip is a synth clip: the grab finds no audio clip, and the tempo stays.
		expect(bind::contract::grab_tempo_off_an_audio_clip_does_nothing(
		           *p.transport_actions, *p.transport, synth.song->project.transport,
		           deluge::project::ClipAddress{.clip_id = synth.clip->id()}))
		    .to_be_true();

		spec::make_current(nullptr);
		expect(p.clips->clip_navigation().has_value()).to_be_false();
		expect(bind::contract::clip_navigation_is_stable(*p.clips)).to_be_true();
		currentPlaybackMode = saved_mode;
		spec::make_current(saved_song);
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
		const deluge::instruments::classic::Polarity polarity = FlashStorage::defaultPatchCablePolarity;
		expect(static_cast<int>(p.device->write(device_path::kPatchCablePolarity, 0)))
		    .to_equal(static_cast<int>(bind::DeviceWrite::STORED));
		expect(static_cast<int>(FlashStorage::defaultPatchCablePolarity))
		    .to_equal(static_cast<int>(deluge::instruments::classic::Polarity::UNIPOLAR));
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
			    std::string{deluge::instruments::classic::to_string(deluge::instruments::classic::Polarity::UNIPOLAR)},
			    std::string{deluge::instruments::classic::to_string(deluge::instruments::classic::Polarity::BIPOLAR)}});
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

	it("does what each CV and gate write implies, as the CV and gate pages' own items do", _ {
		const bind::Ports& p = menu::host_ports();
		expect(p.device).not_().to_be_null();
		if (p.device == nullptr) {
			return;
		}
		using device_path = deluge::project::device_path;
		const CVChannel cv_channel = cvEngine.cvChannels[1];
		const uint8_t off_time = cvEngine.minGateOffTime;
		const GateChannel first_gate = cvEngine.gateChannels[0];
		const GateChannel run_gate = cvEngine.gateChannels[WHICH_GATE_OUTPUT_IS_RUN];
		const GateChannel clock_gate = cvEngine.gateChannels[WHICH_GATE_OUTPUT_IS_CLOCK];
		const bool clock_scheduled = playbackHandler.triggerClockOutTickScheduled;
		// Nothing is waiting to go out of a gate in a spec; take anything that is, so the case starts clean.
		(void)cvEngine.takePendingGateOutputs(true);

		// A CV output's volts and transpose go out at once: each write sends the output's voltage again, which the
		// host's DAC counts as sent at once.
		const deluge::project::DevicePath volts = device_path::cv_volts_per_octave(1).value();
		const deluge::project::DevicePath transpose = device_path::cv_transpose(1).value();
		uint32_t sent = deluge_cv_sent_count();
		expect(static_cast<int>(p.device->write(volts, 150))).to_equal(static_cast<int>(bind::DeviceWrite::STORED));
		expect(static_cast<int>(cvEngine.cvChannels[1].voltsPerOctave)).to_equal(150);
		expect(deluge_cv_sent_count()).to_equal(sent + 1);
		// The transpose is one number over the semitones and the cents, split as main's item splits it.
		sent = deluge_cv_sent_count();
		(void)p.device->write(transpose, -250);
		expect(static_cast<int>(cvEngine.cvChannels[1].transpose)).to_equal(-2);
		expect(static_cast<int>(cvEngine.cvChannels[1].cents)).to_equal(-50);
		expect(p.device->read(transpose).value_or(0)).to_equal(-250);
		expect(deluge_cv_sent_count()).to_equal(sent + 1);
		(void)p.device->write(transpose, 1234);
		expect(static_cast<int>(cvEngine.cvChannels[1].transpose)).to_equal(12);
		expect(static_cast<int>(cvEngine.cvChannels[1].cents)).to_equal(34);
		expect(p.device->read(transpose).value_or(0)).to_equal(1234);

		// A gate's mode switches it at once. The clock's gate set to carry the clock takes the clock's state to send;
		// set back to a trigger, it switches now, and no clock tick waits to go out of it.
		const deluge::project::DevicePath clock = device_path::gate_mode(WHICH_GATE_OUTPUT_IS_CLOCK).value();
		const deluge::project::DevicePath run = device_path::gate_mode(WHICH_GATE_OUTPUT_IS_RUN).value();
		(void)p.device->write(clock, std::to_underlying(GateType::SPECIAL));
		expect(static_cast<int>(cvEngine.gateChannels[WHICH_GATE_OUTPUT_IS_CLOCK].mode))
		    .to_equal(static_cast<int>(GateType::SPECIAL));
		expect(cvEngine.isAnythingPending()).to_be_true();
		playbackHandler.triggerClockOutTickScheduled = true;
		(void)p.device->write(clock, std::to_underlying(GateType::V_TRIG));
		expect(cvEngine.isAnythingPending()).to_be_false();
		expect(playbackHandler.triggerClockOutTickScheduled).to_be_false();
		// The run's gate set to carry the run signal follows playback at once: stopped, it goes off.
		cvEngine.gateChannels[WHICH_GATE_OUTPUT_IS_RUN].on = true;
		(void)p.device->write(run, std::to_underlying(GateType::SPECIAL));
		expect(cvEngine.gateChannels[WHICH_GATE_OUTPUT_IS_RUN].on).to_be_false();
		expect(p.device->read(run).value_or(-1)).to_equal(int32_t{std::to_underlying(GateType::SPECIAL)});

		// A gate with no third choice holding the special mode (a flash byte the menu cannot write) reads clamped to its
		// last choice, and its page shows S-Trig, as main's list clamps it; reading heals nothing.
		cvEngine.gateChannels[0].mode = GateType::SPECIAL;
		const deluge::project::DevicePath first = device_path::gate_mode(0).value();
		expect(p.device->read(first).value_or(-1)).to_equal(int32_t{std::to_underlying(GateType::S_TRIG)});
		{
			menu::MenuContext on_device{};
			on_device.ports = menu::host_ports();
			on_device.source_index = 0;
			const int32_t shown = menu::bindings::read(menu::nodes::gate_mode, on_device);
			std::array<std::string_view, menu::kMaxDynamicNames> names{};
			std::array<deluge::ui::shell::TextScratch, menu::kMaxDynamicNames> scratch{};
			const size_t count = menu::bindings::names(menu::nodes::gate_mode, on_device, names, scratch);
			expect(count).to_equal(size_t{2});
			expect(shown).to_equal(1);
			if (shown >= 0 && static_cast<size_t>(shown) < count) {
				expect(std::string{names[static_cast<size_t>(shown)]}).to_equal(std::string{"S-Trig"});
			}
		}
		expect(static_cast<int>(cvEngine.gateChannels[0].mode)).to_equal(static_cast<int>(GateType::SPECIAL));

		// The minimum off-time is one plain store, which the gate tick reads.
		(void)p.device->write(device_path::kGateMinOffTime, 42);
		expect(static_cast<int>(cvEngine.minGateOffTime)).to_equal(42);

		cvEngine.cvChannels[1] = cv_channel;
		cvEngine.minGateOffTime = off_time;
		cvEngine.gateChannels[0] = first_gate;
		cvEngine.gateChannels[WHICH_GATE_OUTPUT_IS_RUN] = run_gate;
		cvEngine.gateChannels[WHICH_GATE_OUTPUT_IS_CLOCK] = clock_gate;
		playbackHandler.triggerClockOutTickScheduled = clock_scheduled;
		(void)cvEngine.takePendingGateOutputs(true);
	});

	it("keeps the MIDI settings where the MIDI engine keeps them, each write one plain store", _ {
		const bind::Ports& p = menu::host_ports();
		expect(p.device).not_().to_be_null();
		if (p.device == nullptr) {
			return;
		}
		using device_path = deluge::project::device_path;
		const uint8_t root = midiEngine.midiFollowKitRootNote;
		const MIDIFollowFeedbackChannelType feedback = midiEngine.midiFollowFeedbackChannelType;
		const MIDIFollowFeedbackAutomationMode automation = midiEngine.midiFollowFeedbackAutomation;
		const bool filter = midiEngine.midiFollowFeedbackFilter;
		const bool display = midiEngine.midiFollowDisplayParam;
		const bool select_row = midiEngine.midiSelectKitRow;
		const bool thru = midiEngine.midiThru;
		const MIDITakeoverMode takeover = midiEngine.midiTakeover;
		const MIDITransposeControlMethod method = MIDITranspose::controlMethod;
		const bool differentiating = MIDIDeviceManager::differentiatingInputsByDevice;

		(void)p.device->write(device_path::kFollowKitRootNote, 61);
		(void)p.device->write(device_path::kFollowFeedbackChannel, 5);
		(void)p.device->write(device_path::kFollowFeedbackAutomation, 3);
		(void)p.device->write(device_path::kMidiTakeover, 2);
		(void)p.device->write(device_path::kMidiTranspose, 1);
		expect(static_cast<int>(midiEngine.midiFollowKitRootNote)).to_equal(61);
		expect(static_cast<int>(midiEngine.midiFollowFeedbackChannelType))
		    .to_equal(static_cast<int>(MIDIFollowFeedbackChannelType::TrackAndA));
		expect(static_cast<int>(midiEngine.midiFollowFeedbackAutomation))
		    .to_equal(static_cast<int>(MIDIFollowFeedbackAutomationMode::HIGH));
		expect(static_cast<int>(midiEngine.midiTakeover)).to_equal(static_cast<int>(MIDITakeoverMode::SCALE));
		expect(static_cast<int>(MIDITranspose::controlMethod))
		    .to_equal(static_cast<int>(MIDITransposeControlMethod::CHROMATIC));
		for (const bool on : {true, false}) {
			(void)p.device->write(device_path::kFollowFeedbackFilter, on ? 1 : 0);
			(void)p.device->write(device_path::kFollowDisplayParam, on ? 1 : 0);
			(void)p.device->write(device_path::kMidiSelectKitRow, on ? 1 : 0);
			(void)p.device->write(device_path::kMidiThru, on ? 1 : 0);
			(void)p.device->write(device_path::kDifferentiateInputs, on ? 1 : 0);
			expect(midiEngine.midiFollowFeedbackFilter).to_equal(on);
			expect(midiEngine.midiFollowDisplayParam).to_equal(on);
			expect(midiEngine.midiSelectKitRow).to_equal(on);
			expect(midiEngine.midiThru).to_equal(on);
			expect(MIDIDeviceManager::differentiatingInputsByDevice).to_equal(on);
		}

		// The chord method, which no menu offers, reads clamped to chromatic, and its page shows Chromatic, as main's
		// list clamps it; reading heals nothing.
		MIDITranspose::controlMethod = MIDITransposeControlMethod::CHORD;
		expect(p.device->read(device_path::kMidiTranspose).value_or(-1)).to_equal(1);
		{
			menu::MenuContext on_device{};
			on_device.ports = menu::host_ports();
			const int32_t shown = menu::bindings::read(menu::nodes::midi_transpose_method, on_device);
			std::array<std::string_view, menu::kMaxDynamicNames> names{};
			std::array<deluge::ui::shell::TextScratch, menu::kMaxDynamicNames> scratch{};
			const size_t count = menu::bindings::names(menu::nodes::midi_transpose_method, on_device, names, scratch);
			expect(count).to_equal(size_t{2});
			expect(shown).to_equal(1);
			if (shown >= 0 && static_cast<size_t>(shown) < count) {
				expect(std::string{names[static_cast<size_t>(shown)]}).to_equal(std::string{"Chromatic"});
			}
		}
		expect(static_cast<int>(MIDITranspose::controlMethod))
		    .to_equal(static_cast<int>(MIDITransposeControlMethod::CHORD));

		midiEngine.midiFollowKitRootNote = root;
		midiEngine.midiFollowFeedbackChannelType = feedback;
		midiEngine.midiFollowFeedbackAutomation = automation;
		midiEngine.midiFollowFeedbackFilter = filter;
		midiEngine.midiFollowDisplayParam = display;
		midiEngine.midiSelectKitRow = select_row;
		midiEngine.midiThru = thru;
		midiEngine.midiTakeover = takeover;
		MIDITranspose::controlMethod = method;
		MIDIDeviceManager::differentiatingInputsByDevice = differentiating;
	});
});

describe waveforms_host("the app's waveforms port, by address", ${
	it("answers a synth's range and an audio clip, and nothing where no file is loaded", _ {
		spec::SongFixture synth = spec::make_synth_song();
		Song* const saved = currentSong;
		currentSong = synth.song;
		const deluge::project::SlotAddress slot{deluge::project::SlotAddress::Kind::OUTPUT, *synth.instrument->id()};
		const WaveformAddress range{.kind = WaveformAddress::Kind::RANGE, .slot = slot, .source = 0, .range = 0};
		const bind::Waveforms& waveforms = app_waveforms();
		expect(waveforms.info(range).has_value()).to_be_false();

		auto* sound = synth.instrument->toModControllable()->effectsOwner()->sound;
		SourceResources& source = sound->sourceResources[0];
		deluge::test::set(sound->record(), "osc1/type", "sample");
		sound->deriveConfig();
		auto* multisample = static_cast<MultisampleRange*>(sound->getOrCreateFirstRange(0));
		Sample* sample = make_sample();
		multisample->sampleHolder.audioFile = sample;
		const auto info = waveforms.info(range);
		expect(info.has_value()).to_be_true();
		if (info.has_value()) {
			expect(info->length_samples).to_equal(kSampleLength);
			expect(info->sample_rate).to_equal(44100u);
			expect(info->channels).to_equal(1);
			expect(info->centre).to_equal(sample->getFoundValueCentrePoint());
			expect(info->span).to_equal(sample->getValueSpan());
			expect(info->recording).to_be_false();
		}

		spec::AudioSongFixture audio = spec::make_audio_clip_song();
		currentSong = audio.song;
		const WaveformAddress clip{.kind = WaveformAddress::Kind::AUDIO_CLIP,
		                           .clip = deluge::project::ClipAddress{audio.clip->id()}};
		expect(waveforms.info(clip).has_value()).to_be_false();
		audio.clip->sampleHolder.audioFile = make_sample();
		expect(waveforms.info(clip).has_value()).to_be_true();
		if (waveforms.info(clip).has_value()) {
			expect(waveforms.info(clip)->length_samples).to_equal(kSampleLength);
		}
		audio.clip->sampleHolder.audioFile = nullptr;
		currentSong = saved;
	});

	it("fills the peaks the scan fills for the sample the address names", _ {
		spec::SongFixture synth = spec::make_synth_song();
		Song* const saved = currentSong;
		currentSong = synth.song;
		auto* sound = synth.instrument->toModControllable()->effectsOwner()->sound;
		SourceResources& source = sound->sourceResources[0];
		deluge::test::set(sound->record(), "osc1/type", "sample");
		sound->deriveConfig();
		auto* multisample = static_cast<MultisampleRange*>(sound->getOrCreateFirstRange(0));
		Sample* sample = make_sample();
		multisample->sampleHolder.audioFile = sample;
		const WaveformAddress range{.kind = WaveformAddress::Kind::RANGE,
		                            .slot = {deluge::project::SlotAddress::Kind::OUTPUT, *synth.instrument->id()}};
		const int64_t zoom = ((kSampleLength - 1) >> 4) + 1;
		const deluge::ui::views::waveform::PeakRead through = app_waveforms().peaks(range, {.scroll = 0, .zoom = zoom});
		const deluge::ui::views::waveform::PeakRead direct =
		    deluge::sample::waveform_columns(*sample, std::nullopt, {.scroll = 0, .zoom = zoom});
		expect(through.column(0).state()).to_equal(deluge::ui::views::waveform::ColumnState::PEAK);
		expect(through).to_equal(direct);
		multisample->sampleHolder.audioFile = nullptr;
		currentSong = saved;
	});

	it("Q32: answers the range the sample's index holds, whatever the reads through the host port read", _ {
		Sample* sample = make_sample();
		RangeHeld held(sample);
		const Sample::FoundRange before = sample->foundRange();
		(void)app_waveforms().peaks(held.address, {.scroll = 0, .zoom = kWholeZoom});
		(void)app_waveforms().peaks(held.address, {.scroll = 3 * kClusterSize, .zoom = kClusterSize});
		expect(sample->foundRange().max).to_equal(before.max);
		expect(sample->foundRange().min).to_equal(before.min);
		// The fixture's range is the 8-bit scale it widened its index to, at the drawing's 32-bit scale.
		expect(before.max).to_equal(127 << 24);
		expect(before.min).to_equal(-128 << 24);
	});

	it("answers every column of a window over the whole sample PEAK, or PAST_END from the length on", _ {
		RangeHeld held(make_sample());
		const PeakRead whole = app_waveforms().peaks(held.address, {.scroll = 0, .zoom = kWholeZoom});
		expect(count_of(whole, ColumnState::PEAK)).to_equal(16);
		const PeakRead wider = app_waveforms().peaks(held.address, {.scroll = 0, .zoom = 2 * kWholeZoom});
		for (int32_t c = 0; c < 8; c++) {
			expect(wider.column(c).state()).to_equal(ColumnState::PEAK);
		}
		for (int32_t c = 8; c < 16; c++) {
			expect(wider.column(c).state()).to_equal(ColumnState::PAST_END);
		}
		expect(wider.pending()).to_be_false();
	});

	it("Q3: answers a window whose zoom is below 1 every column PAST_END through the host", _ {
		RangeHeld held(make_sample());
		expect(app_waveforms().peaks(held.address, {.scroll = 5, .zoom = 0})).to_equal(PeakRead::all_past_end());
	});

	it("Q22: reads a column straddling the start from frame 0, and one wholly before it as PAST_END", _ {
		// Q22. At two clusters a column, the straddling column 0 covers frames -256 to 32511: from frame 0, all of
		// cluster 0 and most of cluster 1, so its peak is the fold of both (Q30). The scroll keeps its end on a block
		// boundary: a column ending inside a block reads frames there, which the fixture's sample has not.
		RangeHeld held(make_sample());
		const bind::Waveforms& waveforms = app_waveforms();
		const PeakRead straddling = waveforms.peaks(held.address, {.scroll = -256, .zoom = kWholeZoom});
		expect(straddling.column(0)).to_equal(ColumnRead::of({.min = -19 << 24, .max = 19 << 24}));
		const PeakRead before = waveforms.peaks(held.address, {.scroll = -16, .zoom = 8});
		expect(before.column(0).state()).to_equal(ColumnState::PAST_END);
		expect(before.column(1).state()).to_equal(ColumnState::PAST_END);
		expect(waveforms.peaks(held.address, {.scroll = -16 * kWholeZoom, .zoom = kWholeZoom}))
		    .to_equal(PeakRead::all_past_end());
	});

	it("answers a take's captured columns PEAK and its edge column and those after it PAST_END", _ {
		spec::AudioSongFixture audio = spec::make_audio_clip_song();
		Song* const saved = currentSong;
		currentSong = audio.song;
		Sample* sample = make_sample();
		auto* recorder = new SampleRecorder(); // left to the heap: its destructor would free the take
		recorder->sample = sample;
		recorder->numSamplesCaptured = static_cast<uint32_t>(5 * kWholeZoom + kWholeZoom / 2); // into column 5
		audio.clip->recorder = recorder;
		const WaveformAddress clip{.kind = WaveformAddress::Kind::AUDIO_CLIP,
		                           .clip = deluge::project::ClipAddress{audio.clip->id()}};
		const bind::Waveforms& waveforms = app_waveforms();
		const std::optional<bind::WaveformInfo> info = waveforms.info(clip);
		expect(info.has_value()).to_be_true();
		if (info.has_value()) {
			expect(info->recording).to_be_true();
		}
		const PeakRead read = waveforms.peaks(clip, {.scroll = 0, .zoom = kWholeZoom});
		for (int32_t c = 0; c < 5; c++) {
			expect(read.column(c).state()).to_equal(ColumnState::PEAK);
		}
		for (int32_t c = 5; c < 16; c++) {
			expect(read.column(c).state()).to_equal(ColumnState::PAST_END);
		}
		expect(read.pending()).to_be_false();
		audio.clip->recorder = nullptr;
		currentSong = saved;
	});

	// A sample freed and another constructed in its memory: each sample owns its own peak index, so the new one is read
	// from its own rather than shown the freed one's.
	it("reads every column of a file constructed in a freed file's memory, not the freed file's", _ {
		void* const memory = deluge::memory::alloc_fast(sizeof(Sample));
		const Window whole{.scroll = 0, .zoom = kWholeZoom};
		Sample* const first = make_sample(kNumClusters, memory);
		RangeHeld held(first);
		const PeakRead before = app_waveforms().peaks(held.address, whole);
		held.multisample->sampleHolder.audioFile = nullptr;
		first->~Sample();
		Sample* const second = make_sample(kNumClusters, memory, 20); // the same length, other peaks
		expect(static_cast<void*>(second)).to_equal(memory);
		held.multisample->sampleHolder.audioFile = second;
		const PeakRead after = app_waveforms().peaks(held.address, whole);
		const PeakRead own = deluge::sample::waveform_columns(*second, std::nullopt, whole);
		expect(count_of(own, ColumnState::PEAK)).to_equal(16);
		expect(after).to_equal(own);
		expect(after).not_().to_equal(before);
	});

	it("reads every captured column of a take constructed in a freed take's memory, not the freed take's", _ {
		spec::AudioSongFixture audio = spec::make_audio_clip_song();
		Song* const saved = currentSong;
		currentSong = audio.song;
		const WaveformAddress clip{.kind = WaveformAddress::Kind::AUDIO_CLIP,
		                           .clip = deluge::project::ClipAddress{audio.clip->id()}};
		const Window whole{.scroll = 0, .zoom = kWholeZoom};
		const auto captured = static_cast<uint32_t>(5 * kWholeZoom + kWholeZoom / 2); // into column 5
		void* const memory = deluge::memory::alloc_fast(sizeof(Sample));
		auto* recorder = new SampleRecorder(); // left to the heap: its destructor would free the take
		recorder->numSamplesCaptured = captured;
		audio.clip->recorder = recorder;
		Sample* const first = make_sample(kNumClusters, memory);
		recorder->sample = first;
		const PeakRead before = app_waveforms().peaks(clip, whole);
		first->~Sample();
		Sample* const second = make_sample(kNumClusters, memory, 20); // a take aborted and recorded again
		expect(static_cast<void*>(second)).to_equal(memory);
		recorder->sample = second;
		const PeakRead after = app_waveforms().peaks(clip, whole);
		const PeakRead own = deluge::sample::waveform_columns(*second, std::optional<uint64_t>{captured}, whole);
		expect(count_of(own, ColumnState::PEAK)).to_equal(5);
		expect(after).to_equal(own);
		expect(after).not_().to_equal(before);
		audio.clip->recorder = nullptr;
		currentSong = saved;
	});

	it("answers every column PAST_END at an address that resolves to nothing", _ {
		const bind::Waveforms& waveforms = app_waveforms();
		PreviewSound sound;
		const WaveformAddress preview{.kind = WaveformAddress::Kind::PREVIEW};
		expect(waveforms.peaks(preview, {.scroll = 0, .zoom = kWholeZoom})).to_equal(PeakRead::all_past_end());
		RangeHeld held(nullptr);
		expect(waveforms.peaks(held.address, {.scroll = 0, .zoom = kWholeZoom})).to_equal(PeakRead::all_past_end());
	});

	it("answers the preview sample under PREVIEW, and nothing with no preview loaded", _ {
		const WaveformAddress preview{.kind = WaveformAddress::Kind::PREVIEW};
		const bind::Waveforms& waveforms = app_waveforms();
		PreviewSound sound;
		expect(waveforms.info(preview).has_value()).to_be_false();
		Sample* sample = make_sample();
		sound.hold(sample);
		const auto info = waveforms.info(preview);
		expect(info.has_value()).to_be_true();
		if (info.has_value()) {
			expect(info->length_samples).to_equal(kSampleLength);
			expect(info->centre).to_equal(sample->getFoundValueCentrePoint());
			expect(info->span).to_equal(sample->getValueSpan());
			expect(info->recording).to_be_false();
		}
		sound.hold(nullptr);
		expect(waveforms.info(preview).has_value()).to_be_false();
	});

	it("gives two previewed files two identities", _ {
		const WaveformAddress preview{.kind = WaveformAddress::Kind::PREVIEW};
		const bind::Waveforms& waveforms = app_waveforms();
		PreviewSound sound;
		sound.hold(make_sample());
		const std::optional<bind::WaveformInfo> first = waveforms.info(preview);
		sound.hold(make_sample());
		const std::optional<bind::WaveformInfo> second = waveforms.info(preview);
		expect(first.has_value()).to_be_true();
		expect(second.has_value()).to_be_true();
		if (first.has_value() && second.has_value()) {
			expect(first->identity).not_().to_equal(second->identity);
		}
		sound.hold(nullptr);
	});

	it("paints the preview's main columns as legacy's full-screen renderer does", _ {
		const WaveformAddress preview{.kind = WaveformAddress::Kind::PREVIEW};
		const bind::Waveforms& waveforms = app_waveforms();
		PreviewSound sound;
		Sample* sample = make_sample();
		sound.hold(sample);
		const std::optional<bind::WaveformInfo> found = waveforms.info(preview);
		expect(found.has_value()).to_be_true();
		const bind::WaveformInfo info = found.value_or(bind::WaveformInfo{});
		const deluge::ui::views::waveform::WaveformView view = deluge::ui::views::waveform::WaveformView::open(info.length_samples, std::nullopt);

		deluge::ui::test::GridRig rig;
		rig.begin_tick(0);
		const std::optional<services::WaveformFrame> wf = services::read_waveform(waveforms, preview, view.window());
		expect(wf.has_value()).to_be_true();
		if (wf) {
			services::paint_columns(rig.frame, wf->read, wf->centre, wf->span);
		}

		RGB legacy[kDisplayHeight][kDisplayWidth + kSideBarWidth]{};
		(void)waveformRenderer.renderFullScreen(sample, static_cast<uint64_t>(view.scroll()),
		                                        static_cast<uint64_t>(view.zoom()), legacy);
		int32_t differ = 0;
		int32_t lit = 0;
		for (int32_t x = 0; x < kDisplayWidth; x++) {
			for (int32_t y = 0; y < kDisplayHeight; y++) {
				const RGB ours = rig.grid.at(static_cast<uint8_t>(x), static_cast<uint8_t>(y));
				differ += ours == legacy[y][x] ? 0 : 1;
				lit += legacy[y][x] == RGB{} ? 0 : 1;
			}
		}
		expect(lit).to_be_greater_than(0);
		expect(differ).to_equal(0);
		sound.hold(nullptr);
	});

	it("answers SLICE from the sample the slicer's host holds once the preview stops, and nothing once it is released", _ {
		// Entering MANUAL stops the preview (`AudioEngine::stopAnyPreviewing`), which unbinds the preview's holder:
		// PREVIEW then names nothing, while the slicer keeps drawing its file, from the same sample's peaks.
		const bind::Waveforms& waveforms = app_waveforms();
		PreviewSound sound;
		// Resident and leased, as a loaded sample is, so the stop's unbind may drop the holder's lease.
		Sample* const sample = make_sample(kNumClusters, deluge::memory::alloc_external(sizeof(Sample), 16));
		audioFileManager.adoptAudioFileObject(sample);
		sample->addReason(); // the preview holder's
		sample->addReason(); // the song's, which keeps the file loaded after the stop
		auto* range = static_cast<MultisampleRange*>(sound.drum->getOrCreateFirstRange(0));
		range->sampleHolder.filePath = "SAMPLES/SLICE.WAV";
		sound.hold(sample);
		deluge::gui::ui::openShellSlice("SAMPLES/SLICE.WAV");
		const WaveformAddress preview{.kind = WaveformAddress::Kind::PREVIEW};
		const WaveformAddress slice{.kind = WaveformAddress::Kind::SLICE};
		const Window window{.scroll = 0, .zoom = kWholeZoom};
		const PeakRead read = waveforms.peaks(preview, window);
		expect(count_of(read, ColumnState::PEAK)).to_equal(16);
		AudioEngine::stopAnyPreviewing();
		expect(waveforms.info(preview).has_value()).to_be_false();
		const std::optional<bind::WaveformInfo> info = waveforms.info(slice);
		expect(info.has_value()).to_be_true();
		if (info) {
			expect(info->identity).to_equal(deluge::gui::shell_host::waveform_identity(*sample));
		}
		expect(waveforms.peaks(slice, window)).to_equal(read);
		deluge::gui::ui::releaseShellSlicerBeforeClose();
		expect(waveforms.info(slice).has_value()).to_be_false();
		range->sampleHolder.filePath.clear();
	});

	it("holds the waveforms contract on the app's port: a loaded range, and a range at a slot no sound holds", _ {
		const bind::Ports& p = menu::host_ports();
		expect(p.waveforms).not_().to_be_null();
		if (p.waveforms == nullptr) {
			return;
		}
		RangeHeld held(make_sample());
		WaveformAddress nowhere = held.address;
		nowhere.slot.id = *held.synth.instrument->id() + 1000; // no output has this id
		expect(bind::contract::waveforms_unknown_address_reads_nothing(*p.waveforms, nowhere)).to_be_true();
		expect(bind::contract::waveforms_empty_window_reads_past_end(*p.waveforms, held.address)).to_be_true();
		expect(bind::contract::waveforms_columns_follow_the_length(*p.waveforms, held.address)).to_be_true();
		expect(bind::contract::waveforms_peaks_are_stable(*p.waveforms, held.address)).to_be_true();
		expect(bind::contract::waveforms_info_is_consistent(*p.waveforms, held.address)).to_be_true();
	});

	it("resolves a range, an audio clip and the preview to the sample their holder holds", _ {
		const bind::Waveforms& waveforms = app_waveforms();
		Sample* const in_range = make_sample();
		RangeHeld held(in_range);
		const std::optional<bind::WaveformInfo> range = waveforms.info(held.address);
		expect(range.has_value()).to_be_true();
		if (range.has_value()) {
			expect(range->identity).to_equal(deluge::gui::shell_host::waveform_identity(*in_range));
		}

		spec::AudioSongFixture audio = spec::make_audio_clip_song();
		currentSong = audio.song;
		Sample* const in_clip = make_sample();
		audio.clip->sampleHolder.audioFile = in_clip;
		const std::optional<bind::WaveformInfo> clip = waveforms.info(
		    {.kind = WaveformAddress::Kind::AUDIO_CLIP, .clip = deluge::project::ClipAddress{audio.clip->id()}});
		expect(clip.has_value()).to_be_true();
		if (clip.has_value()) {
			expect(clip->identity).to_equal(deluge::gui::shell_host::waveform_identity(*in_clip));
			expect(clip->recording).to_be_false();
		}
		audio.clip->sampleHolder.audioFile = nullptr;
		currentSong = held.synth.song;

		PreviewSound sound;
		Sample* const in_preview = make_sample();
		sound.hold(in_preview);
		const std::optional<bind::WaveformInfo> preview = waveforms.info({.kind = WaveformAddress::Kind::PREVIEW});
		expect(preview.has_value()).to_be_true();
		if (preview.has_value()) {
			expect(preview->identity).to_equal(deluge::gui::shell_host::waveform_identity(*in_preview));
		}
		sound.hold(nullptr);
	});

	it("answers a linear recording's take, from its recorder, as recording", _ {
		spec::AudioSongFixture audio = spec::make_audio_clip_song();
		Song* const saved = currentSong;
		currentSong = audio.song;
		audio.clip->sampleHolder.audioFile = make_sample(); // the clip's file, which the take stands in for
		Sample* const take = make_sample();
		auto* recorder = new SampleRecorder(); // left to the heap: its destructor would free the take
		recorder->sample = take;
		recorder->numSamplesCaptured = static_cast<uint32_t>(kWholeZoom);
		audio.clip->recorder = recorder;
		const std::optional<bind::WaveformInfo> info = app_waveforms().info(
		    {.kind = WaveformAddress::Kind::AUDIO_CLIP, .clip = deluge::project::ClipAddress{audio.clip->id()}});
		expect(info.has_value()).to_be_true();
		if (info.has_value()) {
			expect(info->recording).to_be_true();
			expect(info->identity).to_equal(deluge::gui::shell_host::waveform_identity(*take));
		}
		audio.clip->recorder = nullptr;
		audio.clip->sampleHolder.audioFile = nullptr;
		currentSong = saved;
	});
});

describe take_host("the app's take port, over the sim's inputs", ${
	it("begins a take it publishes and reads back, and holds none once it has orphaned it", _ {
		spec::SongFixture synth = spec::make_synth_song();
		Song* const saved = currentSong;
		currentSong = synth.song;
		bind::TakeActions& takes = deluge::gui::shell_host::take_actions();
		const bind::AudioFacts& audio = deluge::gui::shell_host::audio_facts();
		const bool stereo = audio.audio_inputs_present();
		expect(takes.begin(stereo ? bind::TakeChannels::STEREO : bind::TakeChannels::MONO)).to_be_true();
		SampleRecorder* const held = deluge::gui::shell_host::input_take();
		expect(held).not_().to_be_null();
		expect(AudioEngine::foregroundRecorder()).to_equal(held);
		// Input monitoring follows the published channel.
		expect(AudioEngine::foregroundRecordingSource())
		    .to_equal(stereo ? AudioInputChannel::STEREO : AudioInputChannel::LEFT);
		// One take at a time: a second begin is refused and leaves the first held.
		expect(takes.begin(bind::TakeChannels::MONO)).to_be_false();
		expect(deluge::gui::shell_host::input_take()).to_equal(held);
		const std::optional<bind::InputTake> take = audio.input_take();
		expect(take.has_value()).to_be_true();
		if (take.has_value()) {
			expect(take->channels).to_equal(static_cast<uint8_t>(stereo ? 2 : 1));
			expect(take->capturing_data).to_be_true();
			expect(take->settled).to_be_false();
		}
		// The take's sample exists from the begin; its file, and so the path's text, comes with the card routine.
		expect(audio.input_take_path().has_value()).to_be_true();
		takes.orphan();
		expect(deluge::gui::shell_host::input_take()).to_be_null();
		expect(bind::contract::no_take_reads_nothing(audio)).to_be_true();
		// The orphaned take stays published until it settles; settle and free it, as the card routine would.
		spec::settle_torn_down_take();
		expect(AudioEngine::foregroundRecorder()).to_be_null();
		expect(AudioEngine::foregroundRecordingSource()).to_equal(AudioInputChannel::NONE);
		currentSong = saved;
	});
});

// clang-format on

DELUGE_SPEC(ports_contract_spec, ports_contract_host, waveforms_host, take_host)
