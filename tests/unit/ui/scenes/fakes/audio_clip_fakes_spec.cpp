/// @file
/// @brief The audio clip scene (`audio_clip/audio_clip_scene.cpp`) over the ports' fakes and a song's record: the
///        tempo grab, the clear, the trim arm, the scroll lock, the bars-and-beats popup, the navigation it opens on,
///        what it does with no ports installed, and the waveform it reads and paints.

#include "ui/scenes/audio_clip/audio_clip_scene.h"

#include "base/notify/notice_queue.h"
#include "gui/l10n/strings.h"
#include "project/device_settings.h"
#include "project/features.h"
#include "project/song.h"
#include "ui/scenes/services/peak_retry.h"
#include "ui/shell/profiles/deluge.h"
#include "ui/shell/scene.h"
#include "ui/views/waveform/peaks.h"
#include "ui/widgets/grid/waveform.h"

#include "fakes.h"
#include "support/printers/transport_action_printer.h"
#include "unit/ui/widgets/support/frames.h"

#include "cppspec.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "support/spec_main.h"

using deluge::project::ClipAddress;
using deluge::project::device_path;
using deluge::project::Feature;
using deluge::ui::scenes::audio_clip::AudioClipScene;
using deluge::ui::scenes::bind::AudioClipFact;
using deluge::ui::scenes::bind::ClipNavigation;
using deluge::ui::scenes::bind::Ports;
using deluge::ui::scenes::bind::WaveformAddress;
using deluge::ui::scenes::bind::test::FakeHost;
using deluge::ui::scenes::bind::test::FakeWaveforms;
using deluge::ui::scenes::bind::test::RecordingClipWrites;
using deluge::ui::scenes::bind::test::RecordingTransportActions;
using deluge::ui::shell::Action;
using deluge::ui::shell::ButtonId;
using deluge::ui::shell::ButtonSet;
using deluge::ui::shell::Result;
using deluge::ui::shell::SceneId;
using deluge::ui::shell::UiContext;
using deluge::ui::shell::UiEvent;
using deluge::ui::test::GridRig;
using deluge::ui::widgets::core::EncoderRole;

namespace {

/// The clip's id, in the record and on the fakes.
constexpr uint32_t kClipId = 5;
/// The audio track it plays on.
constexpr uint32_t kTrackId = 3;
/// The clip's loop length, in ticks: sixteen columns at a zoom of 24.
constexpr int32_t kLoopLength = 384;
/// The fake file's length in frames: sixteen columns of 1000.
constexpr uint64_t kLength = 16000;

/// @brief The clip's address.
constexpr ClipAddress clip() {
	return ClipAddress{.clip_id = kClipId};
}

/// @brief The clip's waveform address.
constexpr WaveformAddress at_clip() {
	return WaveformAddress{.kind = WaveformAddress::Kind::AUDIO_CLIP, .clip = clip()};
}

/// @brief A song's record holding the clip (its loop length 384) and its audio track, and a fake host whose clip facts
///        answer the clip on that track, whose navigation shows the whole loop, and whose waveforms port holds the
///        clip's file, every column a different peak.
struct AudioClipHost {
	deluge::project::Song model;
	FakeHost host;
	Ports ports = host.ports();
	AudioClipHost() {
		model.clip_structure.getOrCreate(kClipId).loop_length = kLoopLength;
		(void)model.output_structure.getOrCreate(kTrackId);
		model.transport.inside_world_tick_magnitude = 2;
		model.transport.inside_world_tick_magnitude_offset_from_bpm = -1; // one bar is 96 << 1 = 192 ticks
		host.clips.audio_clips[kClipId] = AudioClipFact{.track_id = kTrackId, .max_zoom = 24};
		host.clips.samples = {0, 1000};
		host.clips.navigation = ClipNavigation{.x_scroll = 0, .x_zoom = 24};
		FakeWaveforms::Entry entry{
		    .at = at_clip(),
		    .info = {.length_samples = kLength,
		             .sample_rate = 44100,
		             .channels = 1,
		             .max_peak_from_zero = 1 << 28,
		             .centre = 1 << 20,
		             .span = 1 << 26,
		             .identity = 9},
		};
		for (int32_t c = 0; c < deluge::ui::views::waveform::kColumns; c++) {
			entry.peaks[static_cast<size_t>(c)] = {.min = -(c + 1) << 22, .max = (c + 1) << 23};
		}
		host.waveforms.waveforms.push_back(entry);
	}
	AudioClipHost(const AudioClipHost&) = delete;
	AudioClipHost& operator=(const AudioClipHost&) = delete;
	/// @return The clip's waveform entry.
	FakeWaveforms::Entry& entry() { return host.waveforms.waveforms.front(); }
	/// @brief Store @p on in the device's boolean row at @p path.
	void set_flag(deluge::project::DevicePath path, bool on) { (void)host.device.write(path, on ? 1 : 0); }
};

/// @brief A scene with @p h's ports installed, opened on the clip in @p h's record and entered.
AudioClipScene& opened(AudioClipScene& scene, AudioClipHost& h) {
	scene.set_ports(&h.ports);
	scene.open(clip(), &h.model);
	(void)scene.on_enter(SceneId{deluge::ui::scenes::audio_clip::kAudioClipSceneKind, 0});
	return scene;
}

/// @brief The context an event runs in: the buttons in @p held down, no shift.
struct Context {
	ButtonSet held{};
	deluge::ui::widgets::core::PadMask pads{};
	/// @return The context.
	[[nodiscard]] UiContext get() const {
		return UiContext{held, pads, false, false, 0, deluge::ui::shell::deluge_profile()};
	}
};

/// @brief Press, then release, the pad at (@p x, @p y) with @p ctx's buttons down.
Result tap(AudioClipScene& scene, uint8_t x, uint8_t y, const Context& ctx = {}) {
	const Result down =
	    scene.handle(UiEvent{.kind = UiEvent::Kind::Pad, .x = x, .y = y, .velocity = 64, .on = true}, ctx.get());
	(void)scene.handle(UiEvent{.kind = UiEvent::Kind::Pad, .x = x, .y = y, .velocity = 0, .on = false}, ctx.get());
	return down;
}

/// @brief Press (@p on) or release the button @p b with @p ctx's buttons down.
Result button(AudioClipScene& scene, ButtonId b, bool on, const Context& ctx = {}) {
	return scene.handle(UiEvent{.kind = UiEvent::Kind::Button, .on = on, .button = b}, ctx.get());
}

/// @brief Turn the horizontal encoder by @p delta detents.
Result turn_horizontal(AudioClipScene& scene, int32_t delta) {
	return scene.handle(UiEvent{.kind = UiEvent::Kind::Encoder,
	                            .encoder = EncoderRole::Horizontal,
	                            .delta = static_cast<int8_t>(delta)},
	                    Context{}.get());
}

/// @brief Turn the select encoder by @p delta detents, shift up.
Result turn_select(AudioClipScene& scene, int32_t delta) {
	return scene.handle(
	    UiEvent{.kind = UiEvent::Kind::Encoder, .encoder = EncoderRole::Select, .delta = static_cast<int8_t>(delta)},
	    Context{}.get());
}

/// @brief The popup string @p r raises, or 0.
uint16_t popup_of(const Result& r) {
	const auto* p = std::ranges::find_if(r.actions, [](const Action& a) { return a.kind == Action::Kind::Popup; });
	return p != r.actions.end() ? p->popup_string : 0;
}

/// @brief The text notices raised since the last call, oldest first.
std::vector<std::string> text_notices() {
	std::vector<std::string> out;
	deluge::base::notify::Notice n;
	while (deluge::base::notify::notices().pop(n)) {
		if (n.kind == deluge::base::notify::Notice::Kind::Text) {
			out.emplace_back(n.text);
		}
	}
	return out;
}

/// @brief How many main pads @p rig shows lit.
int32_t lit_main_pads(GridRig& rig) {
	int32_t lit = 0;
	for (uint8_t x = 0; x < 16; x++) {
		for (uint8_t y = 0; y < 8; y++) {
			lit += rig.grid.at(x, y) == RGB{} ? 0 : 1;
		}
	}
	return lit;
}

/// @brief Render one grid frame of @p scene at @p now_ms into @p rig.
GridRig& render(GridRig& rig, AudioClipScene& scene, uint32_t now_ms) {
	rig.begin_tick(now_ms);
	scene.render_grid(rig.frame);
	return rig;
}

/// @brief The string id @p s, as an action carries it.
constexpr uint16_t sid(deluge::l10n::String s) {
	return static_cast<uint16_t>(s);
}

} // namespace

// clang-format off
describe audio_clip_fakes("audio_clip::AudioClipScene over the ports' fakes", ${
	it("grabs the tempo from the clip on a main pad with TEMPO_ENC held", _ {
		AudioClipHost h;
		AudioClipScene scene;
		opened(scene, h);
		Context ctx;
		ctx.held.set(ButtonId::TempoEnc, true);
		const Result r = tap(scene, 4, 2, ctx);
		expect(r.handled).to_be_true();
		using Call = RecordingTransportActions::Call;
		expect(h.host.transport_actions.calls)
		    .to_equal(std::vector<Call>{{.kind = Call::Kind::GRAB_TEMPO, .clip = clip()}});
		// No marker gesture ran alongside it.
		expect(scene.end_marker_visible()).to_be_false();
	});

	it("clears the clip on BACK over a held X_ENC, keeping its automation and saying SAMPLE CLEARED with the "
	   "automation-clear row on, and clearing it and saying CLIP CLEARED with it off", _ {
		for (const bool clear_row : {true, false}) {
			AudioClipHost h;
			h.set_flag(device_path::kAutomationClear, clear_row);
			AudioClipScene scene;
			opened(scene, h);
			(void)button(scene, ButtonId::XEnc, true);
			expect(scene.ui_mode()).to_equal(deluge::ui::scenes::audio_clip::kUiModeHoldingHorizontalEncoder);
			const Result r = button(scene, ButtonId::Back, true);
			expect(h.host.clip_writes.calls.size()).to_equal(size_t{1});
			if (!h.host.clip_writes.calls.empty()) {
				expect(h.host.clip_writes.calls.front())
				    .to_equal(RecordingClipWrites::Call{.kind = RecordingClipWrites::Call::Kind::CLEAR,
				                                        .clip = clip(),
				                                        .clear_automation = !clear_row,
				                                        .clear_sequence_and_mpe = true});
			}
			expect(popup_of(r)).to_equal(clear_row ? sid(deluge::l10n::String::STRING_FOR_SAMPLE_CLEARED)
			                                       : sid(deluge::l10n::String::STRING_FOR_CLIP_CLEARED));
		}
	});

	it("raises the start marker on the start column only with the trim feature row on", _ {
		for (const bool trim : {true, false}) {
			AudioClipHost h;
			h.set_flag(device_path::feature(Feature::TRIM_FROM_START_OF_AUDIO_CLIP), trim);
			AudioClipScene scene;
			opened(scene, h);
			(void)tap(scene, 0, 3);
			expect(scene.start_marker_visible()).to_equal(trim);
			expect(scene.end_marker_visible()).to_be_false();
		}
	});

	it("locks scrolling and zooming while recording linearly with no clock, or with the arrangement's mode", _ {
		AudioClipHost h;
		AudioClipScene scene;
		opened(scene, h);
		expect(scene.scrollable_and_zoomable()).to_be_true();
		h.host.clips.recording_linearly[kClipId] = true;
		h.host.transport.clocks = false;
		expect(scene.scrollable_and_zoomable()).to_be_false();
		h.host.transport.clocks = true;
		h.host.transport.arrangement = true;
		expect(scene.scrollable_and_zoomable()).to_be_false();
		h.host.transport.arrangement = false;
		expect(scene.scrollable_and_zoomable()).to_be_true();
		h.host.clips.recording_linearly[kClipId] = false;
		h.host.transport.clocks = false;
		h.host.transport.arrangement = true;
		expect(scene.scrollable_and_zoomable()).to_be_true();
	});

	it("draws the playhead on the last column while recording linearly with no clock, or to the arrangement, and on "
	   "the timeline's square otherwise", _ {
		AudioClipHost h;
		h.host.clips.playing_at[kClipId] = 48; // the timeline's square 2, at 24 ticks a column
		h.host.clips.recording_linearly[kClipId] = true;
		AudioClipScene scene;
		opened(scene, h);
		const auto tick_column = [&] {
			GridRig rig;
			render(rig, scene, 0);
			return scene.last_tick_square();
		};
		h.host.transport.clocks = false;
		h.host.transport.arrangement = false;
		expect(tick_column()).to_equal(15);
		h.host.transport.clocks = true;
		h.host.transport.arrangement = true;
		expect(tick_column()).to_equal(15);
		h.host.transport.arrangement = false;
		expect(tick_column()).to_equal(2);
		// The arrangement's mode alone, with no take recording, keeps the timeline's square.
		h.host.clips.recording_linearly[kClipId] = false;
		h.host.transport.arrangement = true;
		expect(tick_column()).to_equal(2);
	});

	it("shows the scroll position in bars and beats of the bar the model's transport magnitudes give", _ {
		AudioClipHost h;
		AudioClipScene scene;
		opened(scene, h);
		h.model.clip_structure.getOrCreate(kClipId).loop_length = 4 * kLoopLength; // room to scroll a page right
		(void)text_notices();
		// A page left of the start moves nothing, and shows the start: bar 1, beat 1, sub-beat 1.
		(void)turn_horizontal(scene, -1);
		// A page right is 16 columns of 24 ticks: 384 ticks, two bars of 192.
		(void)turn_horizontal(scene, 1);
		expect(scene.timeline().x_scroll).to_equal(384);
		expect(text_notices()).to_equal(std::vector<std::string>{"1 : 1 : 1", "3 : 1 : 1"});
	});

	it("keeps its scroll and zoom as the song's clip navigation, writing only what moved", _ {
		AudioClipHost h;
		AudioClipScene scene;
		opened(scene, h);
		h.model.clip_structure.getOrCreate(kClipId).loop_length = 4 * kLoopLength; // room to scroll a page right
		// A page left of the start moves nothing, so nothing is written.
		(void)turn_horizontal(scene, -1);
		expect(h.host.clip_writes.calls.size()).to_equal(size_t{0});
		// A page right moves the view 384 ticks, at the zoom it opened on.
		(void)turn_horizontal(scene, 1);
		expect(h.host.clip_writes.calls.size()).to_equal(size_t{1});
		if (!h.host.clip_writes.calls.empty()) {
			const RecordingClipWrites::Call& call = h.host.clip_writes.calls.front();
			expect(static_cast<int>(call.kind)).to_equal(static_cast<int>(RecordingClipWrites::Call::Kind::NAVIGATION));
			expect(call.value).to_equal(int64_t{384});
			expect(call.x_zoom).to_equal(uint32_t{24});
		}
	});

	it("opens on the host's clip navigation, and keeps its view where the host gives none", _ {
		AudioClipHost h;
		h.host.clips.navigation = ClipNavigation{.x_scroll = 96, .x_zoom = 6, .triplets_on = true, .triplets_level = 8};
		AudioClipScene scene;
		opened(scene, h);
		expect(scene.timeline().x_scroll).to_equal(96);
		expect(scene.timeline().x_zoom).to_equal(uint32_t{6});
		expect(scene.timeline().triplets_on).to_be_true();
		expect(scene.timeline().triplets_level).to_equal(uint32_t{8});
		// The view supports no triplets, whatever the song's navigation says.
		expect(scene.timeline().in_triplets_view()).to_be_false();

		h.host.clips.navigation.reset();
		scene.open(clip(), &h.model);
		expect(scene.timeline().x_scroll).to_equal(96);
		expect(scene.timeline().x_zoom).to_equal(uint32_t{6});
	});

	it("with no ports installed, reads not live, draws no waveform and writes nothing", _ {
		AudioClipHost h;
		h.host.clips.playing_at[kClipId] = 0;
		AudioClipScene scene;
		scene.set_ports(nullptr);
		scene.open(clip(), &h.model);
		(void)scene.on_enter(SceneId{deluge::ui::scenes::audio_clip::kAudioClipSceneKind, 0});
		expect(scene.frame_driven()).to_equal(false);
		Context tempo;
		tempo.held.set(ButtonId::TempoEnc, true);
		(void)tap(scene, 4, 2, tempo);
		(void)button(scene, ButtonId::XEnc, true);
		(void)button(scene, ButtonId::Back, true);
		(void)button(scene, ButtonId::XEnc, false);
		(void)tap(scene, 15, 2); // the end column: the end marker goes up
		(void)tap(scene, 5, 2);  // a length edit
		(void)tap(scene, 5, 2);  // the edge column: the end marker goes down
		const Result turned = turn_select(scene, 1); // the track's mode
		expect(std::ranges::any_of(turned.actions, [](const Action& a) { return a.kind == Action::Kind::RepaintAll; }))
		    .to_be_false();
		GridRig rig;
		render(rig, scene, 0);
		expect(lit_main_pads(rig)).to_equal(0);
		expect(rig.gate.next_deadline().has_value()).to_be_false();
		expect(h.host.transport_actions.calls.empty()).to_be_true();
		expect(h.host.clip_writes.calls.empty()).to_be_true();
		expect(h.host.waveforms.reads.empty()).to_be_true();
		expect(h.host.writes.requests.empty()).to_be_true();
		// And with them installed the same clip plays live, and the same turn writes the track's mode.
		scene.set_ports(&h.ports);
		expect(scene.frame_driven()).to_equal(true);
		(void)turn_select(scene, 1);
		expect(h.host.writes.requests.size()).to_equal(size_t{1});
	});

	it("reads the waveform at its AUDIO_CLIP address over the clip's window in samples, and paints it with the "
	   "entry's centre and span in the clip's colour", _ {
		AudioClipHost h;
		AudioClipScene scene;
		opened(scene, h);
		GridRig rig;
		render(rig, scene, 0);
		expect(h.host.waveforms.reads.size()).to_equal(size_t{1});
		if (!h.host.waveforms.reads.empty()) {
			expect(h.host.waveforms.reads.front())
			    .to_equal(FakeWaveforms::Read{.at = at_clip(), .in = {.scroll = 0, .zoom = 1000}});
		}

		// The same bars, composed through the toolkit from the entry's own read, centre and span.
		const deluge::ui::views::waveform::PeakRead read = h.host.waveforms.peaks(at_clip(), {.scroll = 0, .zoom = 1000});
		const FakeWaveforms::Entry& e = h.entry();
		std::array<deluge::ui::widgets::grid::PeakCol, 16> peaks{};
		for (int32_t x = 0; x < 16; x++) {
			if (const std::optional<deluge::ui::views::waveform::Bar> bar = read.bar(x, e.info.centre, e.info.span)) {
				peaks[static_cast<size_t>(x)] = {.min24 = bar->bottom, .max24 = bar->top, .valid = true};
			}
		}
		const int16_t colour_offset = h.model.clip_structure.getOrCreate(kClipId).colour_offset;
		GridRig expected;
		expected.begin_tick(0);
		expected.frame.region(deluge::ui::gfx::grid::Rect{0, 0, 16, expected.frame.bounds().h}, [&](deluge::ui::widgets::grid::GridFrame& main) {
			deluge::ui::widgets::grid::waveform_full(main, peaks, deluge::ui::widgets::grid::kWaveformBarBrightnessColoured,
			                                RGB::fromHuePastel(colour_offset * -8 / 3));
		});
		expect(lit_main_pads(rig)).to_be_greater_than(0);
		expect(rig.state()).to_equal(expected.state());

		// Another span scales the bars otherwise.
		h.entry().info.span = 1 << 24;
		GridRig rescaled;
		render(rescaled, scene, 0);
		expect(rescaled.state()).not_().to_equal(rig.state());
	});

	it("Q28: draws no waveform column while a column is pending and asks again, then draws them and asks no more", _ {
		AudioClipHost h;
		h.entry().pending.set(5);
		h.entry().pending_reads = 1;
		AudioClipScene scene;
		opened(scene, h);

		GridRig first;
		render(first, scene, 0);
		expect(lit_main_pads(first)).to_equal(0);
		expect(first.gate.next_deadline()).to_equal(std::optional<uint32_t>{deluge::ui::scenes::services::kPeakRetryPollMs});

		GridRig second;
		render(second, scene, deluge::ui::scenes::services::kPeakRetryPollMs);
		expect(lit_main_pads(second)).to_be_greater_than(0);
		expect(second.gate.next_deadline().has_value()).to_be_false();
	});

	it("Q28: keeps asking while a column stays pending past the give-up time, as main's renderMainPads does", _ {
		// `AudioClipView::renderMainPads` (`origin/main:src/deluge/gui/views/audio_clip_view.cpp`) asks to render
		// again after every scan that could not load its clusters, with no bound.
		using deluge::ui::scenes::services::kPeakRetryGiveUpMs;
		using deluge::ui::scenes::services::kPeakRetryPollMs;
		AudioClipHost h;
		h.entry().pending.set(5);
		h.entry().pending_reads = 1000;
		AudioClipScene scene;
		opened(scene, h);
		for (const uint32_t at : {uint32_t{0}, kPeakRetryGiveUpMs, kPeakRetryGiveUpMs + kPeakRetryPollMs,
		                          10 * kPeakRetryGiveUpMs}) {
			GridRig rig;
			render(rig, scene, at);
			expect(lit_main_pads(rig)).to_equal(0);
			expect(rig.gate.next_deadline()).to_equal(std::optional<uint32_t>{at + kPeakRetryPollMs});
		}
	});

	it("Q28: asks again for a new window whose column is pending after a stall at the old one", _ {
		using deluge::ui::scenes::services::kPeakRetryGiveUpMs;
		using deluge::ui::scenes::services::kPeakRetryPollMs;
		AudioClipHost h;
		h.entry().pending.set(5);
		h.entry().pending_reads = 1000;
		AudioClipScene scene;
		opened(scene, h);
		GridRig first;
		render(first, scene, 0);
		GridRig stalled;
		render(stalled, scene, 2 * kPeakRetryGiveUpMs);
		h.host.clips.samples = {1000, 1000}; // scrolled one column
		const uint32_t moved = 2 * kPeakRetryGiveUpMs + kPeakRetryPollMs;
		GridRig scrolled;
		render(scrolled, scene, moved);
		expect(h.host.waveforms.reads.back().in.scroll).to_equal(int64_t{1000});
		expect(lit_main_pads(scrolled)).to_equal(0);
		expect(scrolled.gate.next_deadline()).to_equal(std::optional<uint32_t>{moved + kPeakRetryPollMs});
	});
});
// clang-format on

DELUGE_SPEC(audio_clip_fakes_spec, audio_clip_fakes)
