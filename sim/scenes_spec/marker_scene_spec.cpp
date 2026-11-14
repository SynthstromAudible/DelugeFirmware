/// @file
/// @brief `MarkerScene` -- the port of `SampleMarkerEditor` -- driven through a real `Shell` over
///        `uitest::FakeSurfaceSet`, against a real `Sample`.
///
/// @par The sample fixture, and why it never touches the card
///
/// `deluge::sample::find_peaks_per_col` reads clusters, and a spec process has no mounted card with
/// the file this `Sample` claims to be. It does **not** have to: the scan's whole-cluster arm
/// (`waveform_scan.cpp`, `investigatingAWholeCluster && sampleCluster->investigated`) reuses the
/// sample's own **overview cache** and never opens a reader at all. So the fixture builds a sample
/// long enough that every display column spans two or more clusters -- `kSampleLength` is exactly
/// `kDisplayWidth` clusters' worth of frames -- and pre-marks every overview entry investigated
/// with a chosen min/max. Every column is then a cache hit, the waveform is exactly the shape the
/// fixture asked for, and no I/O happens.
///
/// That is not a workaround for the harness: it is the same path a zoomed-out render takes on the
/// device once the background pre-scan (issue #4460) has run, which is the common case.
///
/// @par The reference
///
/// The "against main's editor" group, and the select-encoder examples, compare the port against what upstream
/// `main`'s `SampleMarkerEditor` answers for the same gesture on the same fixture, as
/// `tests/golden/ui/menu_oracle/marker_scene.tsv` and its `pads/marker_*` and `pixels/marker_*` files hold it: the
/// markers after each turn or tap, the selected marker, the loop lock, the view after a zoom or a scroll, the main
/// pads (`renderMainPads`, which draws `renderColumn` and `renderMarkerInCol` over the waveform) and the 128x48 OLED
/// page (`renderOLED`).
///
/// The pad overlay in particular is compared against the real `renderColumn`/`renderMarkerInCol`,
/// reached through the public `renderMainPads` because both are private
/// (`sample_marker_editor.h:90-94`). A transcription of those two bodies, as the host target
/// `tests/unit/ui/markers_grid_spec.cpp` would have to keep (it cannot link `deluge_app`), would
/// drift from them.
///
/// One divergence is **measured** here rather than only written down: `renderMainPads`'
/// phase-2 suppression, which the scene deliberately does not follow (see `marker_scene.h`). It is
/// asserted in both directions at its example.
///
/// @note The spec may include `gui/`; the library may not. Same rule `browser_scene_spec.cpp` runs under.

#include "gui/l10n/strings.h"
#include "gui/ui/menu_view_of.h"
#include "scenes/bind/write_sink.h"
#include "scenes/marker/marker_scene.h"
#include "scenes/menu/bind_host.h"
#include "scenes/menu/context.h"
#include "scenes/menu/menu_core.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/menu/node_id.h"
#include "scenes/menu/nodes/nodes.h"
#include "scenes/profile.h"
#include "scenes/registry.h"
#include "scenes/services/view_state.h"

#include "shell/profiles/deluge.h"
#include "shell/shell.h"
#include "ui/core/metrics.h"
#include "ui/grid/columns.h"

#include "gui/ui/shell_guest_services.h"
#include "gui/ui/shell_host_ui.h"
#include "gui/ui/ui.h"
#include "gui/views/instrument_clip_view.h"
#include "hid/button.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/led/pad_leds.h"
#include "model/clip/audio_clip.h"
#include "model/edits/setting_edit.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/sample/sample.h"
#include "model/sample/sample_holder_for_voice.h"
#include "model/song/song.h"
#include "model/voice/voice_sample.h"
#include "playback/playback_handler.h"
#include "processing/sound/sound.h"
#include "processing/source.h"
#include "storage/cluster/cluster.h"
#include "storage/multi_range/multisample_range.h"

#include "integration/scenarios/slot_coherence.h"
#include "model/project_bridge/slot_bridge.h"
#include "model/project_bridge/source_bridge.h"
#include "support/global_guest_menu.h"
#include "support/marker_scene_fixtures.h"
#include "support/menu_over_root.h"
#include "support/oracle_files.h"
#include "support/oracle_table.h"
#include "support/song_fixture.h"

// Before <cppspec.hpp>: it #defines `it`, which collides with every local named `it` in the project
// headers this drags in.
#include "support/card_access.h"

#include "support/mocks/fake_surfaces.h"
#include "unit/gfx/support/golden.h"

#include "cppspec.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "support/spec_main.h"

/// `Buttons::isButtonPressed` reads `buttonStates` (`hid/buttons.cpp`), which the header does not export.
namespace Buttons {
extern bool buttonStates[NUM_BUTTON_COLS + 1][NUM_BUTTON_ROWS];
} // namespace Buttons

using namespace deluge::scenes;
using deluge::scenes::marker::kMarkerSceneKind;
using deluge::scenes::marker::marker_scene;
using deluge::scenes::marker::MarkerScene;
using deluge::scenes::menu::MenuContext;
using deluge::ui::shell::BoardProfile;
using deluge::ui::shell::SceneId;
using deluge::ui::shell::SceneKind;
using deluge::ui::shell::Shell;
using deluge::ui::shell::ShellConfig;

namespace mf = deluge::scenes::spec::marker_fx;
using mf::AudioRig;
using mf::kBytesPerFrame;
using mf::kSampleLength;
using mf::kZoomMax;
using mf::make_sample;
using mf::MarkerDisplay;
using mf::Rig;

namespace {

// --- the fixture ------------------------------------------------------------------------------

/// @brief A 128x64 single-panel board, so the same scene can be recorded at the other shipped
///        panel geometry.
constexpr DelugeDisplayDesc kDisplays64[] = {{128, 64, 0}};

DelugeBoard board_64() {
	DelugeBoard b = deluge::ui::shell::kDelugeBoard;
	b.display_height = 64;
	b.display_count = 1;
	b.displays = kDisplays64;
	return b;
}

const BoardProfile& profile_64() {
	static const BoardProfile p = [] {
		static constexpr const deluge::ui::Style* kStyles[] = {&deluge::ui::kDelugeStyle64};
		BoardProfile q = deluge_profile();
		q.style_by_index = kStyles;
		return q;
	}();
	return p;
}

/// @brief Events handed to the marker scene itself, so the actions it files come back for the example to read.
struct DirectEvents {
	deluge::ui::shell::ButtonSet held{};
	deluge::ui::PadMask pads{};

	/// @brief One main-pad event.
	/// @param shift Is shift held?
	deluge::ui::shell::Result pad(int32_t x, int32_t y, bool on, bool shift = false) {
		const deluge::ui::shell::UiContext ctx{held, pads, shift, false, 0, deluge_profile()};
		return marker_scene().handle(deluge::ui::shell::UiEvent{.kind = deluge::ui::shell::UiEvent::Kind::Pad,
		                                                        .x = static_cast<uint8_t>(x),
		                                                        .y = static_cast<uint8_t>(y),
		                                                        .velocity = static_cast<uint8_t>(on ? 127 : 0),
		                                                        .on = on},
		                             ctx);
	}

	/// @brief One turn of the horizontal encoder.
	deluge::ui::shell::Result horizontal(int32_t delta, bool shift) {
		const deluge::ui::shell::UiContext ctx{held, pads, shift, false, 0, deluge_profile()};
		return marker_scene().handle(deluge::ui::shell::UiEvent{.kind = deluge::ui::shell::UiEvent::Kind::Encoder,
		                                                        .encoder = deluge::ui::EncoderRole::Horizontal,
		                                                        .delta = static_cast<int8_t>(delta)},
		                             ctx);
	}
};

/// @brief The string ids of the popups @p r files, in order.
std::vector<uint16_t> popups_of(const deluge::ui::shell::Result& r) {
	std::vector<uint16_t> out;
	for (const auto& a : r.actions) {
		if (a.kind == deluge::ui::shell::Action::Kind::Popup) {
			out.push_back(a.popup_string);
		}
	}
	return out;
}

constexpr uint16_t popup_id(deluge::l10n::String s) {
	return static_cast<uint16_t>(s);
}

/// @brief `marker_scene.tsv`: where `main`'s editor leaves the markers after each gesture below.
spec::OracleTable& table() {
	return spec::OracleTable::named("marker_scene");
}

/// @brief The values of row (`marker/<name>`, @p column), or `<no row>` alone when there is none.
std::vector<std::string> frozen(std::string_view name, std::string_view column) {
	const std::vector<std::string>* v = table().find("marker/" + std::string{name}, column);
	return v != nullptr ? *v : std::vector<std::string>{"<no row>"};
}

/// @brief The one value of row (`marker/<name>`, @p column), as a number; -1 when there is no such row.
int64_t frozen_number(std::string_view name, std::string_view column) {
	const std::vector<std::string> v = frozen(name, column);
	return v.size() == 1 && v[0] != "<no row>" ? std::stoll(v[0]) : -1;
}

/// @brief Where the port's main pads first differ from a reference pad image, and how many differ.
///
/// Main columns only: the sidebar is `renderSidebar`'s, which this scene does not port (see the class docstring), so
/// main's two sidebar columns have no counterpart to compare. A missing image differs everywhere.
struct PadDiff {
	int32_t count = 0;
	int32_t first_x = -1;
	int32_t first_y = -1;
};

PadDiff pad_diff(Rig& r, const std::optional<spec::OraclePads>& frozen_pads) {
	const deluge::gfx::grid::PadGrid& g = r.fake.grid().grid();
	PadDiff d;
	for (int32_t x = 0; x < kDisplayWidth; x++) {
		for (int32_t y = 0; y < kDisplayHeight; y++) {
			if (!frozen_pads || !(g.at(static_cast<uint8_t>(x), static_cast<uint8_t>(y)) == (*frozen_pads)[y][x])) {
				if (d.count == 0) {
					d.first_x = x;
					d.first_y = y;
				}
				d.count++;
			}
		}
	}
	return d;
}

/// @brief Assert the port's main pads are `pads/<name>.txt`, pad for pad.
/// @param self The enclosing `it(...)` block's lambda parameter -- `expect` expands to `self.expect`.
template <typename Self>
void expect_pads_match(Self& self, Rig& r, std::string_view name) {
	const PadDiff d = pad_diff(r, spec::OracleFiles::pads(name));
	// The coordinates first, so a failure names a pad rather than only a count.
	expect(d.first_x).to_equal(-1);
	expect(d.first_y).to_equal(-1);
	expect(d.count).to_equal(0);
}

} // namespace

// clang-format off
describe marker_scene_spec_body("scenes/marker MarkerScene", $ {

	before_each([] {
		static MarkerDisplay d;
		if (display == nullptr) {
			display = &d;
		}
	});

	// --- composition --------------------------------------------------------------------------

	it("draws the main pads only, leaving the sidebar to the view beneath", _ {
		// `SampleMarkerEditor::renderMainPads` fills the main pads and `renderSidebar` defers to the
		// root (`SampleMarkerEditor::renderSidebar`); `getGreyoutColsAndRows` is `0b10`.
		const deluge::ui::shell::Scene& s = marker_scene();
		expect(s.pad_regions() == deluge::ui::shell::PadRegions::Main).to_be_true();
		expect(static_cast<uint64_t>(s.greyout_mask().cols)).to_equal(uint64_t{0b10});
		expect(static_cast<uint64_t>(s.greyout_mask().rows)).to_equal(uint64_t{0});
	});

	it("claims every pad but the audition column, which the host hands to the clip view", _ {
		// `SampleMarkerEditor::padAction`'s audition arm (`gui/ui/sample_marker_editor.cpp`).
		expect(marker_scene().grid_pad_claim() == deluge::ui::shell::GridPadClaim::AllButClipAudition).to_be_true();
	});

	// --- opening ------------------------------------------------------------------------------

	it("opens zoomed all the way out, on the marker SceneId::arg names", _ {
		Rig r;
		r.open(MarkerType::END);
		r.tick(0);
		expect(static_cast<int32_t>(marker_scene().marker_type())).to_equal(static_cast<int32_t>(MarkerType::END));
		// `WaveformNav::opened` with no remembered view: scroll 0, zoom = max_zoom().
		expect(static_cast<int32_t>(marker_scene().x_scroll())).to_equal(0);
		expect(static_cast<int32_t>(marker_scene().x_zoom())).to_equal(kZoomMax);
	});

	it("adopts the holder's remembered scroll and zoom when it has one", _ {
		Rig r;
		r.range->sampleHolder.waveformViewZoom = kZoomMax / 2;
		r.range->sampleHolder.waveformViewScroll = kZoomMax * 2;
		r.open(MarkerType::START);
		r.tick(0);
		expect(static_cast<int32_t>(marker_scene().x_zoom())).to_equal(kZoomMax / 2);
		expect(static_cast<int32_t>(marker_scene().x_scroll())).to_equal(kZoomMax * 2);
	});

	it("refuses to open with no sample on the holder", _ {
		Rig r;
		r.range->sampleHolder.audioFile = nullptr;
		expect(MarkerScene::can(r.ctx())).to_be_false();
		r.open(MarkerType::START);
		r.tick(0);
		bool on_stack = false;
		for (const deluge::ui::shell::Scene* s : r.shell.stack().scenes()) {
			on_stack = on_stack || s->kind() == kMarkerSceneKind;
		}
		expect(on_stack).to_be_false();
		r.range->sampleHolder.audioFile = r.sample;
	});

	// --- the select encoder, against main ----------------------------------------------------

	it("moves the START marker one column per detent and writes it through", _ {
		Rig r;
		r.open(MarkerType::START);
		r.tick(0);
		expect(static_cast<int32_t>(r.range->sampleHolder.startPos)).to_equal(0);

		r.turn_select(1, 10);
		r.tick(10);
		// One column right at max zoom is one column's worth of samples.
		expect(static_cast<int32_t>(r.range->sampleHolder.startPos)).to_equal(kZoomMax);

		r.turn_select(2, 20);
		r.tick(20);
		expect(static_cast<int32_t>(r.range->sampleHolder.startPos)).to_equal(3 * kZoomMax);
	});

	it("writes the same position main's selectEncoderAction writes, for the same turns", _ {
		Rig r;
		r.open(MarkerType::START);
		r.tick(0);
		r.turn_select(1, 10);
		r.tick(10);
		r.turn_select(1, 20);
		r.tick(20);
		expect(mf::markers_of(r.range->sampleHolder)).to_equal(frozen("select_two_turns", "markers"));
		expect(static_cast<int32_t>(r.range->sampleHolder.startPos)).to_equal(2 * kZoomMax);
	});

	it("keeps the sound's record current: a moved marker is in the record a save writes, as main's Edit puts it", _ {
		Rig r;
		expect(deluge::harness::soundIncoherence(*r.sound, *r.song.song)).to_equal(std::string{});
		const uint64_t start_before = r.range->sampleHolder.startPos;

		r.open(MarkerType::START);
		r.tick(0);
		r.turn_select(1, 10);
		r.tick(10);
		expect(r.range->sampleHolder.startPos > start_before).to_be_true();
		expect(deluge::harness::soundIncoherence(*r.sound, *r.song.song)).to_equal(std::string{});
		expect(mf::markers_of(r.range->sampleHolder)).to_equal(frozen("record_one_turn", "markers"));
	});

	it("keeps an audio clip's moved marker in its descriptor record, which a save writes, where main's moves it", _ {
		AudioRig r;
		r.open(MarkerType::START);
		r.tick(0);
		r.turn_select(1, 10);
		r.tick(10);
		expect(r.played().first > 0U).to_be_true();
		expect(r.recorded() == r.played()).to_be_true();
		expect(mf::clip_markers_of(r.played())).to_equal(frozen("audio_clip_turn", "clip_markers"));
	});

	it("keeps a playing audio clip playing from the moved marker's record, turn after turn, as main's does", _ {
		AudioRig r;
		const uint8_t saved_playback_state = playbackHandler.playbackState;
		playbackHandler.playbackState = PLAYBACK_SWITCHED_ON | PLAYBACK_CLOCK_INTERNAL_ACTIVE;
		r.f.clip->activeIfNoSolo = true;
		alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
		r.f.clip->resumePlayback(setupModelStackWithTimelineCounter(memory, r.f.song, r.f.clip), true);
		expect(r.f.clip->voiceSample != nullptr).to_be_true(); // playing before the turn

		r.open(MarkerType::START);
		r.tick(0);
		r.turn_select(1, 10);
		r.tick(10);
		expect(r.played().first > 0U).to_be_true();
		expect(r.recorded() == r.played()).to_be_true();
		expect(r.f.clip->voiceSample != nullptr).to_be_true();

		// Opened again and turned again, where main's editor, opened over the playing clip, takes it.
		while (r.shell.stack().depth() > 0) {
			r.shell.stack().pop(SceneKind::Null);
		}
		const uint64_t before = r.played().first;
		r.open(MarkerType::START);
		r.tick(20);
		r.turn_select(1, 30);
		r.tick(30);
		expect(r.played().first > before).to_be_true();
		expect(r.recorded() == r.played()).to_be_true();
		expect(r.f.clip->voiceSample != nullptr).to_be_true();
		expect(mf::clip_markers_of(r.played())).to_equal(frozen("audio_playing_turn", "clip_markers"));

		r.f.clip->unassignVoiceSample(false);
		r.f.clip->activeIfNoSolo = false;
		playbackHandler.playbackState = saved_playback_state;
	});

	it("hands a moved marker to the menu's sink as its source's ranges, naming the marker", _ {
		Rig r;
		bind::RecordingSink recording;
		r.open(MarkerType::START, &recording);
		r.tick(0);
		r.turn_select(1, 10);
		r.tick(10);
		expect(recording.gestures.size()).to_equal(size_t{1});
		if (recording.gestures.empty()) {
			return;
		}
		const bind::GestureRequest& g = recording.gestures[0];
		expect(g.kind == bind::GestureRequest::Kind::RANGES).to_be_true();
		expect(g.slot == *r.ctx().addresses.slot && g.source == 0).to_be_true();
		expect(g.ranges.size()).to_equal(size_t{1});
		expect(static_cast<int32_t>(g.ranges[0].start_pos)).to_equal(kZoomMax);
		expect(g.cause == bind::RangesCause{.kind = bind::RangesCause::Kind::MARKER,
		                                    .marker = static_cast<int8_t>(MarkerType::START)})
		    .to_be_true();
		expect(static_cast<int32_t>(r.range->sampleHolder.startPos)).to_equal(0); // nothing but the sink wrote
	});

	it("hands an audio clip's moved marker to the menu's sink by the clip's address", _ {
		AudioRig r;
		bind::RecordingSink recording;
		r.open(MarkerType::END, &recording);
		r.tick(0);
		r.turn_select(-1, 10);
		r.tick(10);
		expect(recording.gestures.size()).to_equal(size_t{1});
		if (recording.gestures.empty()) {
			return;
		}
		const bind::GestureRequest& g = recording.gestures[0];
		expect(g.kind == bind::GestureRequest::Kind::MARKER).to_be_true();
		expect(g.clip == deluge::project::ClipAddress{r.f.clip->id()}).to_be_true();
		expect(g.text).to_equal(std::string{"endSamplePos"});
		expect(static_cast<int64_t>(g.value)).to_equal(static_cast<int64_t>(kSampleLength - kZoomMax));
		expect(r.played().second).to_equal(kSampleLength);
	});

	it("counts a moved marker as editing the instrument", _ {
		Rig r;
		r.song.instrument->editedByUser = false;
		r.open(MarkerType::START);
		r.tick(0);
		r.turn_select(1, 10);
		r.tick(10);
		expect(r.song.instrument->editedByUser).to_be_true();
	});

	it("remembers its view in the view-state store, not on the holder", _ {
		Rig r;
		r.open(MarkerType::START);
		r.tick(0);
		r.x_enc_button(true, 10);
		r.tick(10);
		r.turn_horizontal(1, 20);
		r.tick(20);
		expect(r.view().has_value()).to_be_true();
		expect(static_cast<int32_t>(r.view()->zoom)).to_equal(kZoomMax / 2);
		expect(static_cast<int32_t>(r.range->sampleHolder.waveformViewZoom)).to_equal(0);
	});

	it("writes a source's first range into the record when the editor is reached on a source with none", _ {
		spec::SongFixture f = spec::make_synth_song();
		Song* const saved = currentSong;
		currentSong = f.song;
		auto* sound = static_cast<Sound*>(static_cast<ModControllableAudio*>(f.instrument->toModControllable()));
		deluge::edits::settingFor(*sound, "osc1/type", deluge::serialization::Value::makeScalar("sample"));
		const deluge::project::SlotAddress slot{deluge::project::SlotAddress::Kind::OUTPUT, *f.instrument->id()};
		const bool had_none = menu::host_runtime_facts().sound_state(slot)->sources[0].ranges.empty();

		MenuContext recorded = menu::context_for(*f.song, deluge::gui::menuViewOf(f.clip), 0, false, false, false, false);
		bind::RecordingSink recording;
		recorded.sink = &recording;
		if (had_none) {
			expect(menu::ensure_first_range(recorded)).to_be_true();
			expect(recording.gestures.size()).to_equal(size_t{1});
			if (recording.gestures.empty()) {
				currentSong = saved;
				return;
			}
			expect(recording.gestures[0].kind == bind::GestureRequest::Kind::RANGES).to_be_true();
			expect(recording.gestures[0].ranges.size()).to_equal(size_t{1});
			expect(recording.gestures[0].ranges[0].top_note).to_equal(deluge::instrument::kOpenTopNote);
		}

		MenuContext ctx = menu::context_for(*f.song, deluge::gui::menuViewOf(f.clip), 0, false, false, false, false);
		expect(menu::ensure_first_range(ctx)).to_be_true();
		expect(ctx.range_index).to_equal(static_cast<int16_t>(0));
		expect(menu::host_runtime_facts().sound_state(slot)->sources[0].ranges.size()).to_equal(size_t{1});
		expect(deluge::harness::soundIncoherence(*sound, *f.song)).to_equal(std::string{});
		// The range is the one a source makes for itself, as legacy's getOrCreateFirstRange leaves it.
		deluge::edits::settingFor(*sound, "osc2/type", deluge::serialization::Value::makeScalar("sample"));
		(void)sound->sources[1].getOrCreateFirstRange();
		const std::vector<deluge::instrument::RangeState> legacy = deluge::bridge::rangesOf(sound->sources[1]);
		const std::vector<deluge::instrument::RangeState>& first =
		    menu::host_runtime_facts().sound_state(slot)->sources[0].ranges;
		expect(!legacy.empty() && !first.empty() && first[0] == legacy[0]).to_be_true();
		expect(had_none).to_be_true();
		currentSong = saved;
	});

	it("keeps the range's loop lock when another file is loaded into the range, as main's holder keeps it", _ {
		Rig r;
		r.sample->filePath = "SAMPLES/A.WAV";
		r.range->sampleHolder.filePath = "SAMPLES/A.WAV";
		r.range->sampleHolder.startPos = 2 * kZoomMax;
		r.range->sampleHolder.endPos = 12 * kZoomMax;
		r.range->sampleHolder.loopStartPos = 4 * kZoomMax;
		r.range->sampleHolder.loopEndPos = 8 * kZoomMax;
		r.open(MarkerType::LOOP_START);
		r.tick(0);
		r.pad(4, 3, true, 10); // hold LOOP_START
		r.tick(10);
		r.pad(7, 3, true, 20); // tap LOOP_END: locked
		r.tick(20);

		(void)r.load_into(*r.range, "SAMPLES/B.WAV");
		r.reopen(MarkerType::LOOP_START, 0);
		r.tick(30);
		expect(menu::host_runtime_facts().sound_state(r.slot())->sources[0].ranges[0].file_path)
		    .to_equal(std::string{"SAMPLES/B.WAV"});
		r.turn_select(1, 40);
		r.tick(40);
		// Still locked: the loop end keeps its length from the start.
		expect(static_cast<int32_t>(r.range->sampleHolder.loopStartPos)).to_equal(5 * kZoomMax);
		expect(static_cast<int32_t>(r.range->sampleHolder.loopEndPos)).to_equal(9 * kZoomMax);
	});

	it("keeps the range's scroll and zoom when another file is loaded into the range", _ {
		Rig r;
		r.sample->filePath = "SAMPLES/A.WAV";
		r.range->sampleHolder.filePath = "SAMPLES/A.WAV";
		r.open(MarkerType::START);
		r.tick(0);
		r.x_enc_button(true, 10);
		r.tick(10);
		r.turn_horizontal(1, 20);
		r.tick(200); // past the zoom's slide
		const int64_t scroll = marker_scene().x_scroll();
		expect(static_cast<int32_t>(marker_scene().x_zoom())).to_equal(kZoomMax / 2);

		(void)r.load_into(*r.range, "SAMPLES/B.WAV");
		r.reopen(MarkerType::START, 0);
		r.tick(300);
		expect(static_cast<int32_t>(marker_scene().x_zoom())).to_equal(kZoomMax / 2);
		expect(marker_scene().x_scroll()).to_equal(scroll);
	});

	it("keeps the loop lock with its range as a range is inserted before it, and gives the new range none", _ {
		Rig r;
		r.sample->filePath = "SAMPLES/A.WAV";
		r.range->sampleHolder.filePath = "SAMPLES/A.WAV";
		r.range->sampleHolder.startPos = 2 * kZoomMax;
		r.range->sampleHolder.endPos = 12 * kZoomMax;
		r.range->sampleHolder.loopStartPos = 4 * kZoomMax;
		r.range->sampleHolder.loopEndPos = 8 * kZoomMax;
		r.open(MarkerType::LOOP_START);
		r.tick(0);
		r.pad(4, 3, true, 10); // hold LOOP_START
		r.tick(10);
		r.pad(7, 3, true, 20); // tap LOOP_END: locked
		r.tick(20);
		expect(r.view().has_value() && r.view()->loop_locked).to_be_true();

		// A range below it: the locked range is now the source's second, and the new first holds the same file.
		expect(menu::host_write_sink().insert_range(r.slot(), 0, 0, 40, std::nullopt)).to_be_true();
		auto* inserted = static_cast<MultisampleRange*>(r.source->ranges.getElement(0));
		auto* moved = static_cast<MultisampleRange*>(r.source->ranges.getElement(1));
		r.range = moved; // the source's vector moved it
		inserted->sampleHolder.filePath = "SAMPLES/A.WAV";
		inserted->sampleHolder.audioFile = r.sample;
		inserted->sampleHolder.startPos = 2 * kZoomMax;
		inserted->sampleHolder.endPos = 12 * kZoomMax;
		inserted->sampleHolder.loopStartPos = 4 * kZoomMax;
		inserted->sampleHolder.loopEndPos = 8 * kZoomMax;

		r.reopen(MarkerType::LOOP_START, 1);
		r.tick(30);
		r.turn_select(1, 40);
		r.tick(40);
		expect(static_cast<int32_t>(moved->sampleHolder.loopStartPos)).to_equal(5 * kZoomMax);
		expect(static_cast<int32_t>(moved->sampleHolder.loopEndPos)).to_equal(9 * kZoomMax);

		r.reopen(MarkerType::LOOP_START, 0);
		r.tick(50);
		r.turn_select(1, 60);
		r.tick(60);
		expect(static_cast<int32_t>(inserted->sampleHolder.loopStartPos)).to_equal(5 * kZoomMax);
		expect(static_cast<int32_t>(inserted->sampleHolder.loopEndPos)).to_equal(8 * kZoomMax);
	});

	it("keeps the lock and the view with their range as a range before it is deleted, which moves it down", _ {
		Rig r;
		r.sample->filePath = "SAMPLES/A.WAV";
		r.range->sampleHolder.filePath = "SAMPLES/A.WAV";
		r.range->sampleHolder.startPos = 2 * kZoomMax;
		r.range->sampleHolder.endPos = 12 * kZoomMax;
		r.range->sampleHolder.loopStartPos = 4 * kZoomMax;
		r.range->sampleHolder.loopEndPos = 8 * kZoomMax;
		// A range below the edited one, so the edited one is the source's second.
		expect(menu::host_write_sink().insert_range(r.slot(), 0, 0, 40, std::nullopt)).to_be_true();
		auto* below = static_cast<MultisampleRange*>(r.source->ranges.getElement(0));
		below->sampleHolder.filePath = "SAMPLES/A.WAV";
		below->sampleHolder.audioFile = r.sample;
		r.range = static_cast<MultisampleRange*>(r.source->ranges.getElement(1));
		// The edited range's remembered view and lock, in the store under its identity.
		deluge::scenes::services::view_state().forget_unless(r.song.song->project.identity());
		deluge::scenes::services::view_state().set_sample_view(
		    r.key(), {.scroll = 0, .zoom = kZoomMax / 2, .loop_locked = true});
		const uint64_t identity = r.range->identity();

		// Deleting the range below moves the edited one down into its slot, by move-assignment.
		expect(menu::host_write_sink().delete_range(r.slot(), 0, 0, std::nullopt)).to_be_true();
		r.range = static_cast<MultisampleRange*>(r.source->ranges.getElement(0));
		expect(r.range->identity() == identity).to_be_true();

		r.reopen(MarkerType::LOOP_START, 0);
		r.tick(30);
		expect(static_cast<int32_t>(marker_scene().x_zoom())).to_equal(kZoomMax / 2);
		r.turn_select(1, 40);
		r.tick(40);
		// Still locked, at the remembered zoom: one detent is one half-width column, and the loop end keeps its length
		// from the start.
		expect(static_cast<int32_t>(r.range->sampleHolder.loopStartPos)).to_equal(4 * kZoomMax + kZoomMax / 2);
		expect(static_cast<int32_t>(r.range->sampleHolder.loopEndPos)).to_equal(8 * kZoomMax + kZoomMax / 2);
	});

	it("gives a cloned audio clip the original's scroll and zoom, as beenClonedFrom gives the clone's holder", _ {
		using deluge::scenes::services::ViewKey;
		AudioRig r;
		deluge::gui::ui::wireShellClipCloneViews(); // what boot installs
		deluge::scenes::services::view_state().forget_unless(r.f.song->project.identity());
		deluge::scenes::services::view_state().set_sample_view(
		    ViewKey{.kind = ViewKey::Kind::AUDIO_CLIP, .id = r.f.clip->id()}, {.scroll = 3 * kZoomMax, .zoom = kZoomMax / 4});
		r.f.clip->sampleHolder.audioFile = nullptr; // the clone takes no sample: this example is about the view

		alignas(ModelStack) char memory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* stack = setupModelStackWithTimelineCounter(memory, r.f.song, r.f.clip);
		expect(r.f.clip->clone(stack) == Error::NONE).to_be_true();
		const auto* clone = static_cast<const AudioClip*>(stack->getTimelineCounter());
		expect(clone != r.f.clip).to_be_true();
		const std::optional<deluge::scenes::services::SampleView> view =
		    deluge::scenes::services::view_state().sample_view(ViewKey{.kind = ViewKey::Kind::AUDIO_CLIP, .id = clone->id()});
		expect(view.has_value() && view->scroll == 3 * kZoomMax && view->zoom == kZoomMax / 4).to_be_true();
		r.f.clip->sampleHolder.audioFile = r.sample;
	});

	it("reads the waveform's length again when the range's sample changes under it", _ {
		Rig r;
		r.open(MarkerType::END);
		r.tick(0);
		Sample* half = make_sample();
		half->lengthInSamples = kSampleLength / 2;
		half->audioDataLengthBytes = kSampleLength / 2 * kBytesPerFrame;
		r.range->sampleHolder.audioFile = half;
		r.tick(10);
		r.turn_select(1, 20);
		r.tick(20);
		// The END marker clamps at the sample now on the range, not the one the editor opened on.
		expect(static_cast<int64_t>(r.range->sampleHolder.endPos)).to_equal(static_cast<int64_t>(kSampleLength / 2));
		r.range->sampleHolder.audioFile = r.sample;
	});

	it("moves a playing clip's marker through SetSetting alone without taking its voice off the sample", _ {
		// A clip-length drag moves the marker this way and then SetClipLength resumes the clip itself.
		AudioRig r;
		const uint8_t saved_playback_state = playbackHandler.playbackState;
		playbackHandler.playbackState = PLAYBACK_SWITCHED_ON | PLAYBACK_CLOCK_INTERNAL_ACTIVE;
		r.f.clip->activeIfNoSolo = true;
		alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
		r.f.clip->resumePlayback(setupModelStackWithTimelineCounter(memory, r.f.song, r.f.clip), true);
		VoiceSample* const voice = r.f.clip->voiceSample;
		expect(voice != nullptr).to_be_true();
		voice->oscPos = 0x5EED; // a re-seat takes the voice off the sample, which loses this

		deluge::edits::audioClipSettingFor(*r.f.clip, "endSamplePos",
		                                   deluge::serialization::Value::makeNumber(kSampleLength - 100));
		expect(r.recorded() == r.played()).to_be_true();
		expect(r.f.clip->voiceSample == voice && voice->oscPos == 0x5EEDU).to_be_true();

		r.f.clip->unassignVoiceSample(false);
		r.f.clip->activeIfNoSolo = false;
		playbackHandler.playbackState = saved_playback_state;
	});

	it("refuses a turn that would drive one marker into another, exactly as main does", _ {
		Rig r;
		// START at column 4, END at column 5 (`endPos - 1` floors there): one detent right would
		// land START on END's column, and main's "don't drive one marker into the other" loop
		// (`SampleMarkerEditor::selectEncoderAction`) refuses it outright.
		r.range->sampleHolder.startPos = 4 * kZoomMax;
		r.range->sampleHolder.endPos = 6 * kZoomMax;
		r.open(MarkerType::START);
		r.tick(0);
		r.turn_select(1, 10);
		r.tick(10);
		expect(mf::markers_of(r.range->sampleHolder)).to_equal(frozen("refuse_into_end", "markers"));
		expect(static_cast<int32_t>(r.range->sampleHolder.startPos)).to_equal(4 * kZoomMax);
	});

	it("allows the same turn once the other marker is a column further away", _ {
		// The other half of the refusal above: without this, an implementation that simply never
		// moved START would pass the previous example.
		Rig r;
		r.range->sampleHolder.startPos = 4 * kZoomMax;
		r.range->sampleHolder.endPos = 8 * kZoomMax; // END now at column 7
		r.open(MarkerType::START);
		r.tick(0);
		r.turn_select(1, 10);
		r.tick(10);
		expect(mf::markers_of(r.range->sampleHolder)).to_equal(frozen("allow_column_away", "markers"));
		expect(static_cast<int32_t>(r.range->sampleHolder.startPos)).to_equal(5 * kZoomMax);
	});

	it("clamps the END marker at the sample's length", _ {
		Rig r;
		r.range->sampleHolder.endPos = kSampleLength;
		r.open(MarkerType::END);
		r.tick(0);
		r.turn_select(1, 10);
		r.tick(10);
		// `repeatMode` is ONCE and the sample is not reversed, so `shouldAllowExtraScrollRight` is
		// false and the clamp applies.
		expect(static_cast<int32_t>(r.range->sampleHolder.endPos)).to_equal(static_cast<int32_t>(kSampleLength));
	});

	// --- pads ---------------------------------------------------------------------------------

	it("moves the selected marker to a tapped empty column", _ {
		Rig r;
		r.open(MarkerType::START);
		r.tick(0);
		r.pad(6, 3, true, 10);
		r.tick(10);
		expect(static_cast<int32_t>(r.range->sampleHolder.startPos)).to_equal(6 * kZoomMax);
	});

	it("selects a marker rather than moving one when its own column is tapped, and holds it", _ {
		Rig r;
		r.range->sampleHolder.startPos = 2 * kZoomMax;
		r.range->sampleHolder.endPos = 10 * kZoomMax;
		r.open(MarkerType::START);
		r.tick(0);
		// Column 9 is END's (endPos - 1 floors there).
		r.pad(9, 3, true, 10);
		r.tick(10);
		expect(static_cast<int32_t>(marker_scene().marker_type())).to_equal(static_cast<int32_t>(MarkerType::END));
		expect(marker_scene().holding_marker()).to_be_true();
		// Nothing moved.
		expect(static_cast<int32_t>(r.range->sampleHolder.startPos)).to_equal(2 * kZoomMax);
		expect(static_cast<int32_t>(r.range->sampleHolder.endPos)).to_equal(10 * kZoomMax);
		r.pad(9, 3, false, 20);
		r.tick(20);
		expect(marker_scene().holding_marker()).to_be_false();
	});

	it("refuses BACK and the mute column while a marker is held or a note auditions, and exits on them otherwise", _ {
		// `buttonAction`'s BACK arm and `padAction`'s mute arm exit only with no UI mode
		// (`sample_marker_editor.cpp:661-668`, `:366-370`); a held marker and auditioning are UI modes.
		const auto on_stack = [](Rig& r) {
			for (const deluge::ui::shell::Scene* sc : r.shell.stack().scenes()) {
				if (sc->kind() == kMarkerSceneKind) {
					return true;
				}
			}
			return false;
		};
		{
			Rig r;
			r.range->sampleHolder.startPos = 2 * kZoomMax;
			r.open(MarkerType::START);
			r.tick(0);
			r.pad(2, 3, true, 10); // hold START
			r.tick(10);
			r.shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_BUTTON, 7, 1, 255}, 20);
			r.tick(20);
			r.pad(kDisplayWidth, 0, true, 30);
			r.tick(30);
			expect(on_stack(r)).to_be_true();
			r.pad(2, 3, false, 40);
			r.tick(40);
			r.pad(kDisplayWidth, 0, false, 45);
			r.shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_BUTTON, 7, 1, 0}, 45);
			currentUIMode = UI_MODE_AUDITIONING;
			r.shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_BUTTON, 7, 1, 255}, 50);
			r.tick(50);
			expect(on_stack(r)).to_be_true();
			currentUIMode = UI_MODE_NONE;
			r.shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_BUTTON, 7, 1, 0}, 55);
			r.shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_BUTTON, 7, 1, 255}, 60);
			r.tick(60);
			expect(on_stack(r)).to_be_false();
		}
		{
			Rig r;
			r.open(MarkerType::START);
			r.tick(0);
			r.pad(kDisplayWidth, 0, true, 10);
			r.tick(10);
			expect(on_stack(r)).to_be_false();
		}
	});

	it("neither zooms nor scrolls while a marker is held, and holds X_ENC only with no other mode", _ {
		// `horizontalEncoderAction`'s `zoomUIModes` (`:697`, `:752-770`) and `buttonAction`'s X_ENC arm (`:672-681`).
		Rig r;
		r.range->sampleHolder.startPos = 2 * kZoomMax;
		r.open(MarkerType::START);
		r.tick(0);
		r.pad(2, 3, true, 10); // hold START
		r.tick(10);
		r.turn_select(1, 15); // `selectEncoderAction`'s guard (`:269`): refused with a marker held
		r.tick(15);
		expect(static_cast<int32_t>(r.range->sampleHolder.startPos)).to_equal(2 * kZoomMax);
		r.x_enc_button(true, 20);
		r.tick(20);
		expect(marker_scene().holding_xenc()).to_be_false();
		r.turn_horizontal(1, 30);
		r.tick(30);
		expect(static_cast<int32_t>(marker_scene().x_zoom())).to_equal(kZoomMax);
		r.x_enc_button(false, 40);
		r.pad(2, 3, false, 40);
		r.tick(40);
		r.x_enc_button(true, 50);
		r.tick(50);
		expect(marker_scene().holding_xenc()).to_be_true();
		r.turn_horizontal(1, 60); // zoom in
		r.tick(60);
		expect(static_cast<int32_t>(marker_scene().x_zoom()) < kZoomMax).to_be_true();
		const int32_t zoomed = static_cast<int32_t>(marker_scene().x_zoom());
		for (uint32_t t = 70; t <= 70 + 150; t += 50) { // the slide runs out (110 ms, drawn every 50 ms)
			r.tick(t);
		}
		expect(marker_scene().sliding()).to_be_false();
		r.pad(9, 3, true, 230); // refused with X_ENC held: nothing selected or moved
		r.tick(230);
		expect(marker_scene().holding_marker()).to_be_false();
		expect(static_cast<int32_t>(r.range->sampleHolder.startPos)).to_equal(2 * kZoomMax);
		r.pad(9, 3, false, 235);
		r.x_enc_button(false, 240);
		r.tick(240);
		currentUIMode = UI_MODE_AUDITIONING; // a turn scrolls while auditioning
		const int64_t scroll = r.view().has_value() ? r.view()->scroll : -1;
		r.turn_horizontal(1, 250);
		r.tick(250);
		currentUIMode = UI_MODE_NONE;
		expect(static_cast<int32_t>(marker_scene().x_zoom())).to_equal(zoomed);
		expect(r.view().has_value() && r.view()->scroll != scroll).to_be_true();
		// Zoomed in, with START held in view, a turn does not scroll.
		const int64_t s = r.view().has_value() ? r.view()->scroll : 0;
		const int32_t col = static_cast<int32_t>((2 * kZoomMax - s) / zoomed);
		expect(col >= 0 && col < kDisplayWidth).to_be_true();
		r.pad(col, 3, true, 260);
		r.tick(260);
		expect(marker_scene().holding_marker()).to_be_true();
		r.turn_horizontal(1, 270);
		r.tick(270);
		expect(r.view().has_value() && r.view()->scroll == s).to_be_true();
		r.pad(col, 3, false, 280);
	});

	it("slides a zoom over 110 ms from the old view to the new one, refusing every input while it slides", _ {
		// `WaveformBasicNavigator::zoom` (`:30-69`) and `PadLEDs::renderZoom`, drawn every 50 ms; under
		// `UI_MODE_HORIZONTAL_ZOOM` the select encoder (`:269`), the horizontal encoder (`zoomUIModes`), BACK and the
		// mute column (`!currentUIMode`), a pad press (`:368`) and X_ENC (`:656`) are all refused.
		Rig r;
		r.range->sampleHolder.startPos = 4 * kZoomMax;
		r.range->sampleHolder.endPos = 12 * kZoomMax;
		r.open(MarkerType::START);
		r.tick(0);
		const deluge::gfx::grid::PadGrid old = r.fake.grid().grid();
		r.x_enc_button(true, 10);
		r.tick(10);
		r.turn_horizontal(1, 20);
		r.tick(20);
		expect(marker_scene().sliding()).to_be_true();
		r.x_enc_button(false, 25);
		r.tick(25);
		r.turn_select(1, 30);
		r.turn_horizontal(1, 35);
		r.x_enc_button(true, 40); // held past the slide, and still never the editor's
		r.pad(9, 3, true, 45);
		r.pad(9, 3, false, 47);
		r.shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_BUTTON, 7, 1, 255}, 50); // BACK
		r.shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_BUTTON, 7, 1, 0}, 52);
		r.pad(kDisplayWidth, 3, true, 55);
		r.pad(kDisplayWidth, 3, false, 57);
		r.tick(60);
		expect(static_cast<int32_t>(r.range->sampleHolder.startPos)).to_equal(4 * kZoomMax);
		expect(static_cast<int32_t>(marker_scene().x_zoom())).to_equal(kZoomMax / 2);
		expect(marker_scene().holding_xenc()).to_be_false();
		expect(marker_scene().holding_marker()).to_be_false();
		expect(&r.shell.stack().input_top() == &marker_scene()).to_be_true();

		// Halfway (the step at 70 ms), neither view.
		r.tick(70);
		const deluge::gfx::grid::PadGrid mid = r.fake.grid().grid();
		r.tick(120);
		expect(marker_scene().sliding()).to_be_true();
		// The first step at or past 110 ms ends it: the settled frame, with its marker.
		r.tick(170);
		expect(marker_scene().sliding()).to_be_false();
		// The settled frame re-arms the blink's whole period, which the slide held still.
		expect(marker_scene().next_timer() == std::optional<uint32_t>{200}).to_be_true();
		expect(marker_scene().holding_xenc()).to_be_false();
		r.x_enc_button(false, 175);
		const deluge::gfx::grid::PadGrid settled = r.fake.grid().grid();
		bool from_is_old = marker_scene().zoom_slide().from.has_value();
		for (uint8_t y = 0; from_is_old && y < kDisplayHeight; y++) {
			for (uint8_t x = 0; x < kDisplayWidth; x++) {
				from_is_old = from_is_old && marker_scene().zoom_slide().from->at(x, y) == old.at(x, y);
			}
		}
		expect(from_is_old).to_be_true();
		bool mid_is_old = true;
		bool mid_is_settled = true;
		for (uint8_t y = 0; y < kDisplayHeight; y++) {
			for (uint8_t x = 0; x < kDisplayWidth; x++) {
				mid_is_old = mid_is_old && mid.at(x, y) == old.at(x, y);
				mid_is_settled = mid_is_settled && mid.at(x, y) == settled.at(x, y);
			}
		}
		expect(mid_is_old).to_be_false();
		expect(mid_is_settled).to_be_false();
		// START, at 4 * kZoomMax, is column 8 at half the zoom, and is drawn again.
		const int32_t col = static_cast<int32_t>((4 * kZoomMax - static_cast<int32_t>(marker_scene().x_scroll()))
		                                         / static_cast<int32_t>(marker_scene().x_zoom()));
		expect(settled.at(static_cast<uint8_t>(col), 0).g > settled.at(static_cast<uint8_t>(col), 0).r).to_be_true();
		r.turn_select(1, 180);
		r.tick(180);
		expect(static_cast<int32_t>(r.range->sampleHolder.startPos) != 4 * kZoomMax).to_be_true();
	});

	it("leaves no slide behind when it closes mid-slide, or when its waveform goes away", _ {
		// Legacy's `currentUIMode` is dropped on the way out of every UI; a slide left running would refuse every
		// input of the next visit, or of this one with nothing left to draw.
		{
		Rig r;
		r.open(MarkerType::START);
		r.tick(0);
		r.x_enc_button(true, 10);
		r.turn_horizontal(1, 20);
		r.tick(20);
		expect(marker_scene().sliding()).to_be_true();
		marker_scene().on_exit();
		expect(marker_scene().sliding()).to_be_false();
		expect(marker_scene().holding_xenc()).to_be_false();
		expect(marker_scene().zoom_slide().active).to_be_false();
		r.x_enc_button(false, 30);
		}

		struct Gone final : deluge::scenes::services::Waveforms {
			[[nodiscard]] std::optional<deluge::scenes::services::WaveformInfo>
			info(const deluge::scenes::services::WaveformAddress&) const override {
				return std::nullopt;
			}
			bool peaks(const deluge::scenes::services::WaveformAddress&, int64_t, uint64_t, WaveformRenderData&, int32_t,
			           int32_t) const override {
				return false;
			}
		};
		Rig q;
		q.open(MarkerType::START);
		q.tick(0);
		q.x_enc_button(true, 10);
		q.turn_horizontal(1, 20);
		q.tick(20);
		expect(marker_scene().sliding()).to_be_true();
		Gone gone;
		marker_scene().set_waveforms(&gone);
		q.tick(80);
		expect(marker_scene().sliding()).to_be_false();
		marker_scene().set_waveforms(nullptr);
		q.x_enc_button(false, 90);
	});

	it("jumps with SHIFT to a shortcut's item, and stays on an empty cell, with a marker held, or auditioning", _ {
		// `SampleMarkerEditor::padAction`'s `soundEditor.potentialShortcutPadAction` (`:350-356`).
		Rig r;
		r.range->sampleHolder.startPos = 2 * kZoomMax;
		r.open_through_menu();
		for (uint32_t t = 0; t <= 40; t += 10) {
			r.tick(t);
		}
		expect(static_cast<int>(r.top())).to_equal(static_cast<int>(kMarkerSceneKind));
		r.pad(2, 3, true, 50); // hold START
		r.tick(50);
		r.shift(true, 60);
		r.pad(8, 7, true, 70); // with START held: swallowed, as `potentialShortcutPadAction`'s mode gate does
		r.tick(70);
		r.pad(8, 7, false, 75);
		r.pad(2, 3, false, 75);
		r.tick(80);
		expect(static_cast<int>(r.top())).to_equal(static_cast<int>(kMarkerSceneKind));
		expect(static_cast<int32_t>(r.range->sampleHolder.loopStartPos)).to_equal(0);
		const SampleHolderForVoice& h = r.range->sampleHolder;
		const std::array<uint64_t, 4> before{h.startPos, h.endPos, h.loopStartPos, h.loopEndPos};
		r.pad(7, 6, true, 90); // an empty cell: the editor stays, and no marker moves
		r.tick(90);
		r.pad(7, 6, false, 95);
		r.tick(100);
		expect(static_cast<int>(r.top())).to_equal(static_cast<int>(kMarkerSceneKind));
		const std::array<uint64_t, 4> after{h.startPos, h.endPos, h.loopStartPos, h.loopEndPos};
		expect(after == before).to_be_true();
		currentUIMode = UI_MODE_AUDITIONING;
		r.pad(8, 7, true, 110); // LPF frequency while auditioning: the marker's own press
		r.tick(110);
		r.pad(8, 7, false, 115);
		currentUIMode = UI_MODE_NONE;
		r.tick(120);
		expect(static_cast<int>(r.top())).to_equal(static_cast<int>(kMarkerSceneKind));
		r.pad(8, 7, true, 130); // LPF frequency: the menu, on its page, focused on it
		for (uint32_t t = 130; t <= 170; t += 10) {
			r.tick(t);
		}
		r.pad(8, 7, false, 175);
		r.shift(false, 175);
		r.tick(180);
		expect(static_cast<int>(r.top())).to_equal(static_cast<int>(SceneKind::FirstApp));
		expect(menu::focused_value(menu_scene().menu_context(), menu_scene().state()) == &menu::nodes::lpf_freq)
		    .to_be_true();
	});

	it("takes SHIFT + (0,7) and (0,6) over the automation view as its own presses, a marker held or not", _ {
		// `potentialShortcutPadAction`'s `ignoreAction`: with SHIFT and the automation view as the root, its
		// interpolation and pad-selection pads are no shortcut, so `padAction` goes on to the marker's own arms.
		Rig r;
		r.range->sampleHolder.startPos = 2 * kZoomMax;
		r.range->sampleHolder.endPos = 12 * kZoomMax;
		r.range->sampleHolder.loopStartPos = 5 * kZoomMax;
		r.open_through_menu(/*root_is_automation=*/true);
		for (uint32_t t = 0; t <= 40; t += 10) {
			r.tick(t);
		}
		expect(static_cast<int>(r.top())).to_equal(static_cast<int>(kMarkerSceneKind));
		r.shift(true, 50);
		r.pad(0, 7, true, 60); // START is selected and column 0 is empty: START moves there
		r.tick(60);
		r.pad(0, 7, false, 65);
		r.shift(false, 70);
		r.tick(70);
		expect(static_cast<int>(r.top())).to_equal(static_cast<int>(kMarkerSceneKind));
		expect(static_cast<int32_t>(r.range->sampleHolder.startPos)).to_equal(0);
		r.pad(5, 3, true, 80); // hold LOOP_START
		r.tick(80);
		expect(marker_scene().holding_marker()).to_be_true();
		r.shift(true, 90);
		r.pad(0, 6, true, 100); // START's column under the LOOP_START hold: the loop start goes
		r.tick(100);
		r.pad(0, 6, false, 105);
		r.pad(5, 3, false, 105);
		r.shift(false, 110);
		r.tick(110);
		expect(static_cast<int>(r.top())).to_equal(static_cast<int>(kMarkerSceneKind));
		expect(static_cast<int32_t>(r.range->sampleHolder.loopStartPos)).to_equal(0);
		expect(static_cast<int32_t>(marker_scene().marker_type())).to_equal(static_cast<int32_t>(MarkerType::START));
	});

	it("jumps from SHIFT + (0,6) to its item when the root is not the automation view", _ {
		Rig r;
		r.open_through_menu();
		for (uint32_t t = 0; t <= 40; t += 10) {
			r.tick(t);
		}
		r.shift(true, 50);
		r.pad(0, 6, true, 60);
		for (uint32_t t = 60; t <= 100; t += 10) {
			r.tick(t);
		}
		r.pad(0, 6, false, 105);
		r.shift(false, 105);
		r.tick(110);
		expect(static_cast<int>(r.top())).to_equal(static_cast<int>(SceneKind::FirstApp));
		expect(menu::focused_value(menu_scene().menu_context(), menu_scene().state())
		       == &menu::nodes::sample0_interpolation)
		    .to_be_true();
	});

	it("jumps over the automation view to an item that is no param, as main's shortcut from outside the editor", _ {
		// `potentialShortcutPadAction` refuses a non-param item over the automation view only while the editor itself
		// is the current UI; from the marker editor it is not.
		expect(menu::is_automation_item(menu::nodes::polyphony)).to_be_false();
		Rig r;
		r.open_through_menu(/*root_is_automation=*/true);
		for (uint32_t t = 0; t <= 40; t += 10) {
			r.tick(t);
		}
		r.shift(true, 50);
		r.pad(7, 1, true, 60); // POLYPHONY
		for (uint32_t t = 60; t <= 100; t += 10) {
			r.tick(t);
		}
		r.pad(7, 1, false, 105);
		r.shift(false, 105);
		r.tick(110);
		expect(static_cast<int>(r.top())).to_equal(static_cast<int>(SceneKind::FirstApp));
		expect(menu::focused_value(menu_scene().menu_context(), menu_scene().state()) == &menu::nodes::polyphony)
		    .to_be_true();
	});

	it("hands a vertical turn to the instrument clip view under the editor, and keeps it with SHIFT or X_ENC held", _ {
		// `SampleMarkerEditor::verticalEncoderAction` (`:883-901`).
		Rig r;
		spec::UiStackGuard ui_guard;
		spec::GlobalGuestGuard guest_guard;
		currentUIMode = UI_MODE_NONE;
		expect(spec::open_shortcut_over(&instrumentClipView, r.song.clip, 0, 7)).to_be_true();
		for (int32_t i = 0; i < 4; i++) {
			(void)shellHostUI.timerCallback();
		}
		expect(&shellHostUI.shell().stack().input_top() == &marker_scene()).to_be_true();
		const int32_t before = r.song.clip->yScroll;
		(void)shellHostUI.verticalEncoderAction(1);
		expect(r.song.clip->yScroll != before).to_be_true();
		const int32_t moved = r.song.clip->yScroll;
		// With SHIFT or X_ENC held the clip view would move or transpose the notes on screen; the editor keeps the
		// turn.
		NoteRow* row = r.song.clip->createNewNoteRowForYVisual(moved + 2, r.song.song);
		expect(row != nullptr).to_be_true();
		if (row == nullptr) {
			return;
		}
		{
			alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
			ModelStackWithTimelineCounter* stack = r.song.song->setupModelStackWithCurrentClip(memory);
			ModelStackWithNoteRow* with_row = r.song.clip->getNoteRowForYNote(row->y(), stack);
			(void)row->attemptNoteAdd(0, 24, 64, kNumProbabilityValues, row->getDefaultIterance(), 0, with_row, nullptr);
		}
		r.song.song->persistentView.xScroll[NAVIGATION_CLIP] = 0;
		r.song.song->persistentView.xZoom[NAVIGATION_CLIP] = 48;
		const int32_t note_y = row->y();
		const int32_t colour = r.song.clip->colourOffset();
		{
			spec::ShiftHeld shift;
			(void)shellHostUI.verticalEncoderAction(1);
		}
		expect(r.song.clip->yScroll).to_equal(moved);
		expect(r.song.clip->colourOffset()).to_equal(colour); // the clip view would shift its colours
		expect(r.song.clip->getNoteRowForYNote(note_y) != nullptr
		       && !r.song.clip->getNoteRowForYNote(note_y)->hasNoNotes())
		    .to_be_true();
		const auto xy = deluge::hid::button::toXY(deluge::hid::button::X_ENC);
		Buttons::buttonStates[xy.x][xy.y] = true;
		(void)shellHostUI.verticalEncoderAction(1);
		Buttons::buttonStates[xy.x][xy.y] = false;
		NoteRow* still = r.song.clip->getNoteRowForYNote(note_y);
		expect(still != nullptr && !still->hasNoNotes()).to_be_true();
	});

	it("draws the playhead on every row with the flash cursor Slow, flashes it Fast, and shows nothing Off", _ {
		// `graphicsRoutine` (`:977-1027`): `(pos - xScroll) / xZoom`, all eight rows, through `PadLEDs::setTickSquares`.
		struct Playing final : deluge::scenes::services::Waveforms {
			std::optional<int64_t> pos;
			[[nodiscard]] std::optional<deluge::scenes::services::WaveformInfo>
			info(const deluge::scenes::services::WaveformAddress& a) const override {
				return menu::host_waveforms().info(a);
			}
			bool peaks(const deluge::scenes::services::WaveformAddress& a, int64_t scroll, uint64_t zoom,
			           WaveformRenderData& cache, int32_t x_start, int32_t x_end) const override {
				return menu::host_waveforms().peaks(a, scroll, zoom, cache, x_start, x_end);
			}
			[[nodiscard]] std::optional<int64_t>
			play_position(const deluge::scenes::services::WaveformAddress&) const override {
				return pos;
			}
		};
		Playing playing;
		marker_scene().set_waveforms(&playing);
		const uint8_t saved = PadLEDs::flashCursor;
		PadLEDs::flashCursor = FLASH_CURSOR_SLOW;
		Rig r;
		r.open(MarkerType::END);
		r.tick(0);
		expect(marker_scene().next_timer().has_value() && *marker_scene().next_timer() <= 15).to_be_false();
		// A voice that starts after a render is found by the timer, with no other event (`graphicsRoutine` runs every
		// pass).
		playing.pos = 7 * kZoomMax;
		expect(marker_scene().next_timer() == std::optional<uint32_t>{15}).to_be_true();
		r.tick(1);
		r.tick(20);
		const RGB tick = deluge::ui::grid::tick_colour(deluge::ui::grid::TickColour::Normal, RGB{});
		for (uint8_t y = 0; y < 8; y++) {
			expect(r.fake.grid().grid().at(7, y) == tick).to_be_true();
		}
		// Fast: nothing painted; the PIC flashes each row's square when it moves (`setTickSquares`' fast arm), and
		// only then -- the square it is on was already handed over under Slow.
		PadLEDs::flashCursor = FLASH_CURSOR_FAST;
		size_t from = r.fake.flashes_sent.size();
		r.tick(40);
		expect(r.fake.grid().grid().at(7, 0) == tick).to_be_false();
		expect(r.fake.flashes_sent.size()).to_equal(from);
		playing.pos = 9 * kZoomMax;
		r.tick(60);
		size_t at_nine = 0;
		for (size_t i = from; i < r.fake.flashes_sent.size(); i++) {
			at_nine += r.fake.flashes_sent[i].x == 9 ? 1 : 0;
		}
		expect(at_nine).to_equal(size_t{8});
		expect(r.fake.grid().grid().at(9, 0) == tick).to_be_false();
		// Off: nothing, and nothing asked for.
		PadLEDs::flashCursor = FLASH_CURSOR_OFF;
		from = r.fake.flashes_sent.size();
		playing.pos = 11 * kZoomMax;
		r.tick(80);
		expect(r.fake.grid().grid().at(11, 0) == tick).to_be_false();
		expect(r.fake.flashes_sent.size()).to_equal(from);
		expect(marker_scene().next_timer() == std::optional<uint32_t>{15}).to_be_false();
		// The voice ends under Slow: one more frame, which clears the playhead, and then no more.
		PadLEDs::flashCursor = FLASH_CURSOR_SLOW;
		playing.pos = 7 * kZoomMax;
		r.tick(100);
		r.tick(120);
		expect(r.fake.grid().grid().at(7, 0) == tick).to_be_true();
		playing.pos.reset();
		expect(marker_scene().next_timer() == std::optional<uint32_t>{15}).to_be_true();
		r.tick(140);
		r.tick(160);
		expect(r.fake.grid().grid().at(7, 0) == tick).to_be_false();
		expect(marker_scene().next_timer() == std::optional<uint32_t>{15}).to_be_false();
		PadLEDs::flashCursor = saved;
		marker_scene().set_waveforms(nullptr);
	});

	it("adds a loop start by holding START and tapping to its right", _ {
		Rig r;
		r.range->sampleHolder.startPos = 2 * kZoomMax;
		r.range->sampleHolder.endPos = 12 * kZoomMax;
		r.open(MarkerType::START);
		r.tick(0);
		r.pad(2, 3, true, 10); // hold START (its column)
		r.tick(10);
		expect(marker_scene().holding_marker()).to_be_true();
		r.pad(6, 3, true, 20); // tap column 6
		r.tick(20);
		expect(static_cast<int32_t>(r.range->sampleHolder.loopStartPos)).to_equal(6 * kZoomMax);
		expect(static_cast<int32_t>(marker_scene().marker_type()))
		    .to_equal(static_cast<int32_t>(MarkerType::LOOP_START));
	});

	it("refuses a tap and the held pad's release once a note auditions under the hold", _ {
		// `padAction` refuses a press unless `currentUIMode` is exactly AUDITIONING or HOLDING_SAMPLE_MARKER, and ends
		// the hold only under HOLDING_SAMPLE_MARKER exactly (`:368-370`, `:632`). The hold assigns the mode (`:534`), so
		// an audition already sounding when the marker went down no longer counts.
		Rig r;
		r.range->sampleHolder.startPos = 2 * kZoomMax;
		r.range->sampleHolder.endPos = 12 * kZoomMax;
		r.open(MarkerType::START);
		r.tick(0);
		r.pad(2, 3, true, 10); // hold START
		r.tick(10);
		currentUIMode = UI_MODE_AUDITIONING; // a note auditioned under the hold
		r.pad(6, 3, true, 20);
		r.pad(6, 3, false, 25);
		r.pad(2, 3, false, 30); // the held pad's release, refused too
		r.tick(30);
		expect(static_cast<int32_t>(r.range->sampleHolder.loopStartPos)).to_equal(0);
		expect(marker_scene().holding_marker()).to_be_true();
		currentUIMode = UI_MODE_NONE;
	});

	it("takes a tap and the held pad's release under an audition that was already sounding when the hold began", _ {
		Rig q;
		q.range->sampleHolder.startPos = 2 * kZoomMax;
		q.range->sampleHolder.endPos = 12 * kZoomMax;
		q.open(MarkerType::START);
		q.tick(0);
		currentUIMode = UI_MODE_AUDITIONING; // already sounding when START goes down
		q.pad(2, 3, true, 10);
		q.tick(10);
		q.pad(6, 3, true, 20);
		q.tick(20);
		expect(static_cast<int32_t>(q.range->sampleHolder.loopStartPos)).to_equal(6 * kZoomMax);
		q.pad(6, 3, false, 25);
		q.pad(2, 3, false, 30);
		q.tick(30);
		expect(marker_scene().holding_marker()).to_be_false();
		currentUIMode = UI_MODE_NONE;
	});

	it("pops LOCK and then FREE as the loop lock toggles, and FREE as a loop marker is removed", _ {
		// `loopLock` / `loopUnlock` (`displayPopup("LOCK")` / `("FREE")`) on the lock toggle and on every removal.
		Rig r;
		r.range->sampleHolder.startPos = 2 * kZoomMax;
		r.range->sampleHolder.endPos = 12 * kZoomMax;
		r.range->sampleHolder.loopStartPos = 4 * kZoomMax;
		r.range->sampleHolder.loopEndPos = 8 * kZoomMax;
		r.open(MarkerType::LOOP_START);
		r.tick(0);
		DirectEvents d;
		(void)d.pad(4, 3, true); // hold LOOP_START
		const std::vector<uint16_t> lock = popups_of(d.pad(7, 3, true)); // tap LOOP_END
		expect(lock == std::vector<uint16_t>{popup_id(deluge::l10n::String::STRING_FOR_LOOP_LOCKED)}).to_be_true();
		(void)d.pad(7, 3, false);
		const std::vector<uint16_t> free = popups_of(d.pad(7, 3, true));
		expect(free == std::vector<uint16_t>{popup_id(deluge::l10n::String::STRING_FOR_LOOP_FREE)}).to_be_true();
		(void)d.pad(7, 3, false);
		(void)d.pad(4, 3, false);
	});

	it("pops FREE as held START removes the loop start", _ {
		Rig r;
		r.range->sampleHolder.startPos = 2 * kZoomMax;
		r.range->sampleHolder.endPos = 12 * kZoomMax;
		r.range->sampleHolder.loopStartPos = 4 * kZoomMax;
		r.open(MarkerType::START);
		r.tick(0);
		DirectEvents d;
		(void)d.pad(2, 3, true); // hold START
		const std::vector<uint16_t> p = popups_of(d.pad(4, 3, true)); // tap LOOP_START
		expect(p == std::vector<uint16_t>{popup_id(deluge::l10n::String::STRING_FOR_LOOP_FREE)}).to_be_true();
		expect(static_cast<int32_t>(r.range->sampleHolder.loopStartPos)).to_equal(0);
	});

	it("pops FREE as held END removes the loop end", _ {
		Rig r;
		r.range->sampleHolder.startPos = 2 * kZoomMax;
		r.range->sampleHolder.endPos = 12 * kZoomMax;
		r.range->sampleHolder.loopStartPos = 4 * kZoomMax;
		r.range->sampleHolder.loopEndPos = 8 * kZoomMax;
		r.open(MarkerType::END);
		r.tick(0);
		DirectEvents d;
		(void)d.pad(11, 3, true); // hold END
		const std::vector<uint16_t> p = popups_of(d.pad(7, 3, true)); // tap LOOP_END
		expect(p == std::vector<uint16_t>{popup_id(deluge::l10n::String::STRING_FOR_LOOP_FREE)}).to_be_true();
		expect(static_cast<int32_t>(r.range->sampleHolder.loopEndPos)).to_equal(0);
	});

	it("pops FREE as held LOOP_START removes itself by tapping START", _ {
		Rig r;
		r.range->sampleHolder.startPos = 2 * kZoomMax;
		r.range->sampleHolder.endPos = 12 * kZoomMax;
		r.range->sampleHolder.loopStartPos = 4 * kZoomMax;
		r.open(MarkerType::LOOP_START);
		r.tick(0);
		DirectEvents d;
		(void)d.pad(4, 3, true); // hold LOOP_START
		const std::vector<uint16_t> p = popups_of(d.pad(2, 3, true)); // tap START
		expect(p == std::vector<uint16_t>{popup_id(deluge::l10n::String::STRING_FOR_LOOP_FREE)}).to_be_true();
		expect(static_cast<int32_t>(r.range->sampleHolder.loopStartPos)).to_equal(0);
	});

	it("pops FREE as held LOOP_END removes itself by tapping END", _ {
		Rig r;
		r.range->sampleHolder.startPos = 2 * kZoomMax;
		r.range->sampleHolder.endPos = 12 * kZoomMax;
		r.range->sampleHolder.loopStartPos = 4 * kZoomMax;
		r.range->sampleHolder.loopEndPos = 8 * kZoomMax;
		r.open(MarkerType::LOOP_END);
		r.tick(0);
		DirectEvents d;
		(void)d.pad(7, 3, true); // hold LOOP_END
		const std::vector<uint16_t> p = popups_of(d.pad(11, 3, true)); // tap END
		expect(p == std::vector<uint16_t>{popup_id(deluge::l10n::String::STRING_FOR_LOOP_FREE)}).to_be_true();
		expect(static_cast<int32_t>(r.range->sampleHolder.loopEndPos)).to_equal(0);
	});

	it("halves and doubles a locked loop with SHIFT and the horizontal encoder without a LOCK or FREE popup", _ {
		// The shift arm writes `loopLocked` itself (`horizontalEncoderAction`), never through `loopLock`/`loopUnlock`.
		Rig r;
		r.range->sampleHolder.startPos = 2 * kZoomMax;
		r.range->sampleHolder.endPos = 12 * kZoomMax;
		r.range->sampleHolder.loopStartPos = 4 * kZoomMax;
		r.range->sampleHolder.loopEndPos = 8 * kZoomMax;
		r.range->sampleHolder.loopLocked = true;
		r.open(MarkerType::LOOP_START);
		r.tick(0);
		DirectEvents d;
		const std::vector<uint16_t> halved = popups_of(d.horizontal(-1, /*shift=*/true));
		expect(halved == std::vector<uint16_t>{popup_id(deluge::l10n::String::STRING_FOR_LOOP_HALVED)}).to_be_true();
		const std::vector<uint16_t> doubled = popups_of(d.horizontal(1, /*shift=*/true));
		expect(doubled == std::vector<uint16_t>{popup_id(deluge::l10n::String::STRING_FOR_LOOP_DOUBLED)}).to_be_true();
		expect(r.view().has_value() && r.view()->loop_locked).to_be_true();
		// ...and none is left queued for the next pad press to file.
		expect(popups_of(d.pad(13, 3, true)).empty()).to_be_true();
		(void)d.pad(13, 3, false);
	});

	it("swallows SHIFT + a main pad while a marker is held, as potentialShortcutPadAction does", _ {
		Rig r;
		r.range->sampleHolder.startPos = 2 * kZoomMax;
		r.range->sampleHolder.endPos = 12 * kZoomMax;
		r.open(MarkerType::START);
		r.tick(0);
		DirectEvents d;
		(void)d.pad(2, 3, true); // hold START
		const deluge::ui::shell::Result r1 = d.pad(8, 7, true, /*shift=*/true);
		expect(r1.handled).to_be_true();
		expect(static_cast<int32_t>(r.range->sampleHolder.loopStartPos)).to_equal(0);
		expect(marker_scene().holding_marker()).to_be_true();
		const deluge::ui::shell::Result r2 = d.pad(6, 3, true, /*shift=*/true);
		expect(r2.handled).to_be_true();
		expect(static_cast<int32_t>(r.range->sampleHolder.loopStartPos)).to_equal(0);
	});

	it("greys at once over the keyboard screen and fades in elsewhere", _ {
		// `SampleMarkerEditor::opened`: `if (getRootUI() == &keyboardScreen) PadLEDs::skipGreyoutFade()`.
		Rig r;
		r.open(MarkerType::START);
		expect(marker_scene().greyout_changes_instantly()).to_be_false();
		MenuContext c = r.ctx();
		c.root_is_keyboard = true;
		marker_scene().open(c);
		expect(marker_scene().greyout_changes_instantly()).to_be_true();
	});

	it("removes the loop start by holding it and tapping START", _ {
		Rig r;
		r.range->sampleHolder.startPos = 2 * kZoomMax;
		r.range->sampleHolder.endPos = 12 * kZoomMax;
		r.range->sampleHolder.loopStartPos = 6 * kZoomMax;
		r.open(MarkerType::LOOP_START);
		r.tick(0);
		r.pad(6, 3, true, 10); // hold LOOP_START
		r.tick(10);
		r.pad(2, 3, true, 20); // tap START
		r.tick(20);
		expect(static_cast<int32_t>(r.range->sampleHolder.loopStartPos)).to_equal(0);
		expect(static_cast<int32_t>(marker_scene().marker_type())).to_equal(static_cast<int32_t>(MarkerType::START));
	});

	// --- the blink ----------------------------------------------------------------------------

	it("asks for a tick every blink period while the selected marker is on screen", _ {
		Rig r;
		r.open(MarkerType::START);
		r.tick(0);
		expect(marker_scene().next_timer().has_value()).to_be_true();
		expect(static_cast<int32_t>(marker_scene().next_timer().value_or(0)))
		    .to_equal(static_cast<int32_t>(marker::kBlinkPhaseMs));
		expect(static_cast<int32_t>(marker_scene().blink_phase())).to_equal(0);

		r.tick(marker::kBlinkPhaseMs);
		expect(static_cast<int32_t>(marker_scene().blink_phase())).to_equal(1);
		r.tick(2 * marker::kBlinkPhaseMs);
		expect(static_cast<int32_t>(marker_scene().blink_phase())).to_equal(2);
		r.tick(4 * marker::kBlinkPhaseMs);
		expect(static_cast<int32_t>(marker_scene().blink_phase())).to_equal(0);
	});

	it("asks for no tick at all when the selected marker is scrolled off screen", _ {
		// `timerCallback`'s early return (`sample_marker_editor.cpp:797-800`). The witness is the
		// TIMER, not the phase: a scene that kept asking would keep the whole shell awake for a
		// blink nobody can see.
		Rig r;
		r.range->sampleHolder.startPos = 0;
		r.range->sampleHolder.endPos = kSampleLength;
		// A remembered view zoomed right in, a long way to the right of the START marker: at zoom
		// 1024 and scroll 100000, START (sample 0) is at column -98.
		r.range->sampleHolder.waveformViewZoom = 1024;
		r.range->sampleHolder.waveformViewScroll = 100000;
		r.open(MarkerType::START);
		r.tick(0);
		expect(static_cast<int32_t>(marker_scene().x_scroll())).to_equal(100000);
		expect(marker_scene().next_timer().has_value()).to_be_false();

		// ...and the phase stays put across a tick that would otherwise have advanced it.
		r.tick(marker::kBlinkPhaseMs * 3);
		expect(static_cast<int32_t>(marker_scene().blink_phase())).to_equal(0);
	});

	it("paints the blink: phases 0, 1 and 2 alike, the selected marker gone on phase 3", _ {
		// The visible half of the blink, and the half `SampleMarkerEditor::timerCallback`
		// disagrees with `renderMainPads` about -- see `marker_scene.h`. Phases 0 and 2 must look the
		// same as each other; 1 and 3 must each look different from them and from one another.
		Rig r;
		r.range->sampleHolder.startPos = 4 * kZoomMax;
		r.range->sampleHolder.endPos = 12 * kZoomMax;
		r.open(MarkerType::START);
		r.tick(0);
		const std::string phase0 = r.fake.state();

		r.tick(marker::kBlinkPhaseMs);
		expect(static_cast<int32_t>(marker_scene().blink_phase())).to_equal(1);
		const std::string phase1 = r.fake.state();

		r.tick(2 * marker::kBlinkPhaseMs);
		expect(static_cast<int32_t>(marker_scene().blink_phase())).to_equal(2);
		const std::string phase2 = r.fake.state();

		r.tick(3 * marker::kBlinkPhaseMs);
		expect(static_cast<int32_t>(marker_scene().blink_phase())).to_equal(3);
		const std::string phase3 = r.fake.state();

		expect(phase0).to_equal(phase2); // phases 0 and 2 both draw normally
		// **Phase 1 is indistinguishable from phase 0 here, and that is legacy's behaviour, not a
		// missing arm.** Phase 1 floods the selected column with the marker colour
		// (`renderMarkerInCol(x, image, markerType, 0, kDisplayHeight, false)`, `:815`); a column
		// with exactly one marker on it is *already* a full-height undimmed bar, because
		// `renderColumn`'s one-marker case takes `yStart = kDisplayHeight / 2` and mirrors it and
		// the selected marker is never dimmed. The flood only shows when the selected marker is
		// SHARING its column, which no legal marker layout on this fixture does.
		expect(phase0).to_equal(phase1);
		expect(phase0 == phase3).to_be_false(); // phase 3 removes the selected marker
		expect(phase1 == phase3).to_be_false();

		// Phase 3 leaves the END marker (which is not the selected one) exactly where phase 0 had
		// it: only the SELECTED marker is suppressed.
		expect(phase3.find("pad 11 0 ") != std::string::npos)
		    .to_equal(phase0.find("pad 11 0 ") != std::string::npos);
	});

	// --- the OLED ---------------------------------------------------------------------------

	it("draws the marker's own title, and the loop-lock badge only when the loop is locked", _ {
		Rig r;
		r.range->sampleHolder.startPos = 3 * kZoomMax;
		r.open(MarkerType::START);
		r.tick(0);
		const std::string unlocked = r.oled();

		r.range->sampleHolder.loopStartPos = 5 * kZoomMax;
		r.range->sampleHolder.loopEndPos = 9 * kZoomMax;
		r.range->sampleHolder.loopLocked = true;
		r.open(MarkerType::LOOP_START);
		r.tick(10);
		const std::string locked = r.oled();
		expect(unlocked == locked).to_be_false();
	});

	it("labels a reversed sample's START marker 'End point', as reverseRemap does", _ {
		Rig r;
		r.range->sampleHolder.startPos = 3 * kZoomMax;
		r.open(MarkerType::START);
		r.tick(0);
		const std::string forwards = r.oled();

		r.source->sampleControls.reversed = true;
		r.open(MarkerType::START);
		r.tick(10);
		const std::string reversed = r.oled();
		expect(forwards == reversed).to_be_false();

		// ...and it is the same picture the END marker draws forwards, which is the actual claim.
		r.source->sampleControls.reversed = false;
		r.range->sampleHolder.endPos = 3 * kZoomMax;
		r.open(MarkerType::END);
		r.tick(20);
		expect(reversed).to_equal(r.oled());
	});

	// --- against main's editor ---------------------------------------------------------------
	//
	// Every example below runs a gesture through the shell into `MarkerScene` and compares what it
	// leaves with what main's `SampleMarkerEditor` left after the same gesture on the same fixture.
	//
	// **Uncovered, and deliberately so.** Main's `SampleMarkerEditor::renderSidebar` has no
	// counterpart -- the scene does not port the sidebar (see the class docstring) -- and neither does
	// `verticalEncoderAction`, which falls through to the UI beneath. The audition-column and
	// mute-column arms of `padAction` reach `instrumentClipView` and `exitUI()`, neither of which this
	// layer performs.

	it("draws the same pads as SampleMarkerEditor::renderMainPads, four markers apart", _ {
		Rig r;
		r.range->sampleHolder.startPos = 2 * kZoomMax;
		r.range->sampleHolder.loopStartPos = 6 * kZoomMax;
		r.range->sampleHolder.loopEndPos = 10 * kZoomMax;
		r.range->sampleHolder.endPos = 14 * kZoomMax;
		r.open(MarkerType::LOOP_START);
		r.tick(0);
		// Main's `renderMainPads` draws the waveform underneath its `renderColumn` and `renderMarkerInCol`, so this
		// compares the blend rather than the overlay on black.
		expect_pads_match(self, r, "marker_four_apart_0");
	});

	it("stacks a shared column the same way SampleMarkerEditor::renderColumn does", _ {
		// All four markers land on column 4: START floors `pos / zoom`, the two end-ish markers floor
		// `(pos - 1) / zoom`, so 4z, 4z+1, 4z+2 and 4z+3 are one column between them. This is the
		// stacking arithmetic -- `numMarkers`, `baseIncrement`/`selectedIncrement`, and the mirror
		// about the middle row -- which a column with one marker on it cannot exercise at all.
		Rig r;
		r.range->sampleHolder.startPos = 4 * kZoomMax;
		r.range->sampleHolder.loopStartPos = 4 * kZoomMax + 1;
		r.range->sampleHolder.loopEndPos = 4 * kZoomMax + 2;
		r.range->sampleHolder.endPos = 4 * kZoomMax + 3;
		r.open(MarkerType::LOOP_END);
		r.tick(0);
		expect_pads_match(self, r, "marker_shared_column_0");
	});

	it("gives the selected marker the wider slice when three share a column, as main does", _ {
		// `numMarkers == 3` is the ONE case where `baseIncrement` (1) and `selectedIncrement` (2)
		// differ (`SampleMarkerEditor::renderColumn`); with two or four sharing they are equal and
		// swapping them changes nothing. Without this example a port that swapped the two passes.
		Rig r;
		r.range->sampleHolder.startPos = 4 * kZoomMax;
		r.range->sampleHolder.loopStartPos = 4 * kZoomMax + 1;
		r.range->sampleHolder.loopEndPos = 4 * kZoomMax + 2;
		r.range->sampleHolder.endPos = 12 * kZoomMax;
		r.open(MarkerType::LOOP_START);
		r.tick(0);
		expect_pads_match(self, r, "marker_three_share_0");
	});

	it("suppresses the same markers as renderMainPads does at every blink phase", _ {
		// The suppression mask is `blinkPhase >= 1` and the phase-1 flood is `blinkPhase == 1`
		// (`SampleMarkerEditor::renderMainPads`). With the selected marker SHARING a column the two
		// are distinguishable, which is exactly what the port-only blink example above says a
		// one-marker column cannot show.
		Rig r;
		r.range->sampleHolder.startPos = 4 * kZoomMax;
		r.range->sampleHolder.loopStartPos = 4 * kZoomMax + 1;
		r.range->sampleHolder.loopEndPos = 9 * kZoomMax;
		r.range->sampleHolder.endPos = 13 * kZoomMax;
		r.open(MarkerType::LOOP_START);
		for (int32_t phase = 0; phase < 4; phase++) {
			r.tick(static_cast<uint32_t>(phase) * marker::kBlinkPhaseMs);
			expect(static_cast<int32_t>(marker_scene().blink_phase())).to_equal(phase);
			// **Phase 2 is the documented divergence, and this is where it is measured rather than
			// merely asserted.** Main renders the blink two ways: `timerCallback` draws phases 0 and 2
			// normally, while `renderMainPads` suppresses whenever `blinkPhase >= 1`. The scene follows
			// `timerCallback` on purpose (`marker_scene.h`), so at phase 2 it must match `renderMainPads`'
			// PHASE-0 image and must not match its phase-2 image. Both halves are asserted, so a port that
			// quietly started following `renderMainPads` would fail here.
			expect_pads_match(self, r, "marker_suppress_" + std::to_string(phase == 2 ? 0 : phase));
			if (phase == 2) {
				const std::optional<spec::OraclePads> suppressed = spec::OracleFiles::pads("marker_suppress_2");
				expect(suppressed.has_value()).to_be_true();
				expect(pad_diff(r, suppressed).count > 0).to_be_true();
			}
		}
	});

	it("moves the marker to the column main's padAction moves it to", _ {
		Rig r;
		r.open(MarkerType::START);
		r.tick(0);
		r.pad(6, 3, true, 10);
		r.tick(10);
		expect(mf::markers_of(r.range->sampleHolder)).to_equal(frozen("pad_move", "markers"));
		expect(static_cast<int32_t>(r.range->sampleHolder.startPos)).to_equal(6 * kZoomMax);
	});

	it("selects the marker main's padAction selects, and holds it the same way", _ {
		Rig r;
		r.range->sampleHolder.startPos = 2 * kZoomMax;
		r.range->sampleHolder.endPos = 10 * kZoomMax;
		r.open(MarkerType::START);
		r.tick(0);
		r.pad(9, 3, true, 10); // column 9 is END's
		r.tick(10);
		const MarkerType ported = marker_scene().marker_type();
		const bool ported_holding = marker_scene().holding_marker();
		expect(static_cast<int64_t>(ported)).to_equal(frozen_number("pad_select", "marker"));
		expect(static_cast<int32_t>(ported)).to_equal(static_cast<int32_t>(MarkerType::END));
		// Main's `UI_MODE_HOLDING_SAMPLE_MARKER` is what the scene's `holding_marker()` replaces.
		expect(int64_t{ported_holding ? 1 : 0}).to_equal(frozen_number("pad_select", "holding"));
		expect(ported_holding).to_be_true();
	});

	it("adds the loop start where main's hold-and-tap adds it", _ {
		Rig r;
		r.range->sampleHolder.startPos = 2 * kZoomMax;
		r.range->sampleHolder.endPos = 12 * kZoomMax;
		r.open(MarkerType::START);
		r.tick(0);
		r.pad(2, 3, true, 10); // hold START's own column
		r.tick(10);
		r.pad(6, 3, true, 20); // tap column 6
		r.tick(20);
		const MarkerType ported_marker = marker_scene().marker_type();
		expect(mf::markers_of(r.range->sampleHolder)).to_equal(frozen("add_loop_start", "markers"));
		expect(static_cast<int64_t>(ported_marker)).to_equal(frozen_number("add_loop_start", "marker"));
		expect(static_cast<int32_t>(ported_marker)).to_equal(static_cast<int32_t>(MarkerType::LOOP_START));
	});

	it("removes the loop start where main's hold-and-tap removes it", _ {
		Rig r;
		r.range->sampleHolder.startPos = 2 * kZoomMax;
		r.range->sampleHolder.endPos = 12 * kZoomMax;
		r.range->sampleHolder.loopStartPos = 6 * kZoomMax;
		r.open(MarkerType::LOOP_START);
		r.tick(0);
		r.pad(6, 3, true, 10); // hold LOOP_START
		r.tick(10);
		r.pad(2, 3, true, 20); // tap START
		r.tick(20);
		const MarkerType ported_marker = marker_scene().marker_type();
		expect(mf::markers_of(r.range->sampleHolder)).to_equal(frozen("remove_loop_start", "markers"));
		expect(static_cast<int32_t>(r.range->sampleHolder.loopStartPos)).to_equal(0);
		expect(static_cast<int64_t>(ported_marker)).to_equal(frozen_number("remove_loop_start", "marker"));
		expect(static_cast<int32_t>(ported_marker)).to_equal(static_cast<int32_t>(MarkerType::START));
	});

	it("removes the loop end where main's hold-and-tap removes it", _ {
		// The LOOP_END arm of `SampleMarkerEditor::padAction`, which the LOOP_START one above does
		// not reach: a different marker held, a different column tapped, and a different write.
		Rig r;
		r.range->sampleHolder.startPos = 2 * kZoomMax;
		r.range->sampleHolder.endPos = 12 * kZoomMax;
		r.range->sampleHolder.loopStartPos = 4 * kZoomMax;
		r.range->sampleHolder.loopEndPos = 8 * kZoomMax;
		r.open(MarkerType::LOOP_END);
		r.tick(0);
		r.pad(7, 3, true, 10); // hold LOOP_END (`(pos - 1) / zoom` floors to 7)
		r.tick(10);
		r.pad(11, 3, true, 20); // tap END's own column
		r.tick(20);
		const MarkerType ported_marker = marker_scene().marker_type();
		expect(mf::markers_of(r.range->sampleHolder)).to_equal(frozen("remove_loop_end", "markers"));
		expect(static_cast<int32_t>(r.range->sampleHolder.loopEndPos)).to_equal(0);
		expect(static_cast<int64_t>(ported_marker)).to_equal(frozen_number("remove_loop_end", "marker"));
		expect(static_cast<int32_t>(ported_marker)).to_equal(static_cast<int32_t>(MarkerType::END));
	});

	it("toggles the loop lock where main's hold-and-tap toggles it", _ {
		// `markerHeld == LOOP_START && markerPressed == LOOP_END` (`SampleMarkerEditor::padAction`).
		Rig r;
		r.range->sampleHolder.startPos = 2 * kZoomMax;
		r.range->sampleHolder.endPos = 12 * kZoomMax;
		r.range->sampleHolder.loopStartPos = 4 * kZoomMax;
		r.range->sampleHolder.loopEndPos = 8 * kZoomMax;
		r.open(MarkerType::LOOP_START);
		r.tick(0);
		r.pad(4, 3, true, 10); // hold LOOP_START
		r.tick(10);
		r.pad(7, 3, true, 20); // tap LOOP_END
		r.tick(20);
		const bool ported_locked = r.view().has_value() && r.view()->loop_locked;
		expect(int64_t{ported_locked ? 1 : 0}).to_equal(frozen_number("lock", "locked"));
		expect(ported_locked).to_be_true();
	});

	it("zooms to the same scroll and zoom main's horizontalEncoderAction reaches", _ {
		// Main zooms only while the horizontal encoder's BUTTON is held
		// (`UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON`, `SampleMarkerEditor::horizontalEncoderAction`), and
		// the port is driven through that gate rather than around it. `QuietAudio` (`support/card_access.h`) keeps
		// the audio routine from running inside the example.
		spec::QuietAudio quiet;
		Rig r;
		r.range->sampleHolder.startPos = 4 * kZoomMax;
		r.range->sampleHolder.endPos = 12 * kZoomMax;
		r.open(MarkerType::START);
		r.tick(0);
		r.x_enc_button(true, 10);
		r.tick(10);
		r.turn_horizontal(1, 20);
		r.tick(20);
		const int32_t ported_zoom = static_cast<int32_t>(marker_scene().x_zoom());
		const int32_t ported_scroll = static_cast<int32_t>(marker_scene().x_scroll());
		const deluge::ui::grid::ZoomTransition slide = marker_scene().zoom_slide();
		// A second detent while the first slides.
		r.turn_horizontal(1, 30);
		r.tick(30);
		const int32_t ported_second = static_cast<int32_t>(marker_scene().x_zoom());
		const bool ported_second_refused_by_slide = marker_scene().sliding();

		expect(std::vector<std::string>{std::to_string(ported_scroll), std::to_string(ported_zoom)})
		    .to_equal(frozen("zoom", "view"));
		// ...and it really did zoom, so a port that refused the turn could not pass this.
		expect(ported_zoom).to_equal(kZoomMax / 2);
		// The slide main hands `PadLEDs::renderZoom`: its pin square, step and direction, and the new view's bare
		// waveform (`renderFullScreen` into `imageStore[0]`, the old image going to the other half).
		const std::vector<std::string> main_slide = frozen("zoom", "slide");
		expect(main_slide.size()).to_equal(size_t{3});
		if (main_slide.size() == 3) {
			expect(std::to_string(slide.params.pin_square_from)).to_equal(main_slide[0]);
			expect(std::to_string(slide.params.pin_square_to)).to_equal(main_slide[0]);
			expect(std::to_string(slide.params.magnitude)).to_equal(main_slide[1]);
			expect(std::string{slide.params.new_zoom < slide.params.old_zoom ? "1" : "0"}).to_equal(main_slide[2]);
		}
		expect(slide.params.pin_square_from != 0).to_be_true(); // a pin of 0 would match a port that forgot it
		const std::optional<spec::OraclePads> main_to = spec::OracleFiles::pads("marker_zoom_to");
		bool to_matches = slide.to.has_value() && main_to.has_value();
		for (uint8_t y = 0; to_matches && y < kDisplayHeight; y++) {
			for (uint8_t x = 0; x < kDisplayWidth; x++) {
				to_matches = to_matches && slide.to->at(x, y) == (*main_to)[y][x];
			}
		}
		expect(to_matches).to_be_true();

		// A successful zoom leaves main's `currentUIMode == UI_MODE_HORIZONTAL_ZOOM` until the slide finishes, and
		// `horizontalEncoderAction`'s `isUIModeWithinRange(zoomUIModes)` gate refuses the next detent; the port's
		// slide refuses it the same way: the row's second value is that mode as main's second detent found it, and the
		// port's is its slide still running. Nothing ran main's slide to completion, so a detent after it is the port's
		// alone ("slides a zoom ...").
		expect(std::vector<std::string>{std::to_string(ported_second), ported_second_refused_by_slide ? "1" : "0"})
		    .to_equal(frozen("zoom", "second"));
	});

	it("scrolls to the same position main's horizontalEncoderAction scrolls to", _ {
		// The same entry point's OTHER arm: no button held, so `waveformBasicNavigator.scroll`.
		Rig r;
		r.range->sampleHolder.startPos = 0;
		r.range->sampleHolder.endPos = kSampleLength;
		r.range->sampleHolder.waveformViewZoom = kZoomMax / 4;
		r.range->sampleHolder.waveformViewScroll = 8 * (kZoomMax / 4);
		r.open(MarkerType::START);
		r.tick(0);
		r.turn_horizontal(1, 10);
		r.tick(10);
		const int32_t ported_scroll = static_cast<int32_t>(marker_scene().x_scroll());
		expect(int64_t{ported_scroll}).to_equal(frozen_number("scroll", "scroll"));
		expect(ported_scroll).to_equal(9 * (kZoomMax / 4));
	});

	it("draws the same OLED page as SampleMarkerEditor::renderOLED", _ {
		Rig r;
		r.range->sampleHolder.startPos = 3 * kZoomMax;
		r.range->sampleHolder.endPos = 12 * kZoomMax;
		r.open(MarkerType::START);
		r.tick(0);
		// The full 128x48 panel, title rows included: `OLED_MAIN_TOPMOST_PIXEL` is 5 and main offsets its own drawing
		// by it, so the faceplate rows are blank on both sides and there is nothing to normalise away.
		expect(r.oled()).to_equal(spec::OracleFiles::pixels("marker_start"));
	});

	it("draws the same OLED page for a locked loop, badge and all", _ {
		// The loop-lock badge and the position readout's other units -- `renderOLED`'s arms the
		// START page never reaches.
		Rig r;
		r.range->sampleHolder.startPos = 0;
		r.range->sampleHolder.endPos = 14 * kZoomMax;
		r.range->sampleHolder.loopStartPos = 5 * kZoomMax;
		r.range->sampleHolder.loopEndPos = 9 * kZoomMax;
		r.range->sampleHolder.loopLocked = true;
		r.open(MarkerType::LOOP_END);
		r.tick(0);
		expect(r.oled()).to_equal(spec::OracleFiles::pixels("marker_loop_locked"));
	});

	it("labels a reversed sample's marker the same way reverseRemap does", _ {
		// `reverseRemap` (`sample_marker_editor.cpp`) is read by BOTH renderers; with the
		// sample reversed, START must draw main's "End point" page.
		Rig r;
		r.source->sampleControls.reversed = true;
		r.range->sampleHolder.startPos = 3 * kZoomMax;
		r.range->sampleHolder.endPos = 12 * kZoomMax;
		r.open(MarkerType::START);
		r.tick(0);
		expect(r.oled()).to_equal(spec::OracleFiles::pixels("marker_reversed"));
		r.source->sampleControls.reversed = false;
	});

	// --- goldens ------------------------------------------------------------------------------

	it("draws the start point on a 48-row panel", _ {
			Rig r;
			r.range->sampleHolder.startPos = 3 * kZoomMax;
			r.open(MarkerType::START);
			r.tick(0);
			expect_golden(self, "ui/scenes/marker_start_48.pbm", r.fake.pbm(0));
		});
	it("draws the start point on a 64-row panel", _ {
			Rig r64{board_64(), profile_64()};
			r64.range->sampleHolder.startPos = 3 * kZoomMax;
			r64.open(MarkerType::START);
			r64.tick(0);
			expect_golden(self, "ui/scenes/marker_start_64.pbm", r64.fake.pbm(0));
		});
		it("draws a locked loop end, badge and all, on a 48-row panel", _ {
			Rig r;
			r.range->sampleHolder.startPos = 0;
			r.range->sampleHolder.loopStartPos = 5 * kZoomMax;
			r.range->sampleHolder.loopEndPos = 9 * kZoomMax;
			r.range->sampleHolder.loopLocked = true;
			r.open(MarkerType::LOOP_END);
			r.tick(0);
			expect_golden(self, "ui/scenes/marker_loop_locked_48.pbm", r.fake.pbm(0));
		});
		it("draws a locked loop end, badge and all, on a 64-row panel", _ {
			Rig r64{board_64(), profile_64()};
			r64.range->sampleHolder.startPos = 0;
			r64.range->sampleHolder.loopStartPos = 5 * kZoomMax;
			r64.range->sampleHolder.loopEndPos = 9 * kZoomMax;
			r64.range->sampleHolder.loopLocked = true;
			r64.open(MarkerType::LOOP_END);
			r64.tick(0);
			expect_golden(self, "ui/scenes/marker_loop_locked_64.pbm", r64.fake.pbm(0));
		});

	it("draws the waveform and every marker on the pads", _ {
		Rig r;
		r.range->sampleHolder.startPos = 2 * kZoomMax;
		r.range->sampleHolder.loopStartPos = 6 * kZoomMax;
		r.range->sampleHolder.loopEndPos = 10 * kZoomMax;
		r.range->sampleHolder.endPos = 14 * kZoomMax;
		r.open(MarkerType::LOOP_START);
		r.tick(0);
		expect_golden(self, "ui/scenes/marker_pads.state", r.fake.state());
	});
});

describe marker_scene_reference_rows("marker_scene: the reference rows", $ {
	it("parses marker_scene.tsv clean and asks every row of it", _ {
		spec::OracleTable& t = spec::OracleTable::named("marker_scene");
		expect(t.problems().size()).to_equal(size_t{0});
		expect(t.size()).to_equal(size_t{20});
		expect(t.unasked()).to_equal(std::string{});
	});

	it("reads every pad image and panel frozen beside it", _ {
		expect(spec::OracleFiles::unread("marker_")).to_equal(std::string{});
	});
});
// clang-format on
DELUGE_SPEC(marker_scene_spec, marker_scene_spec_body, marker_scene_reference_rows)
