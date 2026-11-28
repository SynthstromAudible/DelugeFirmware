#pragma once

/// @file
/// @brief The marker editor's fixtures, which `marker_scene_spec` and the menu oracle freezer share: a sample whose
///        every display column is answered from its peak index, a synth whose source 0 holds it in one range, an audio
///        clip that holds it, and the holder's markers as a value to put back.
///
/// The sample never touches the card: its peak index is seeded (`peak_seed.h`), so every column on block boundaries is
/// answered from the index and the waveform the fixture asked for is drawn with no reader opened.

#include "gui/ui/menu_view_of.h"
#include "peak_seed.h"
#include "project/sample_marker.h"
#include "ui/scenes/marker/marker_scene.h"
#include "ui/scenes/menu/context.h"
#include "ui/scenes/menu/menu_scene.h"
#include "ui/scenes/menu/node_id.h"
#include "ui/scenes/menu/nodes/nodes.h"
#include "ui/scenes/profile.h"
#include "ui/scenes/registry.h"
#include "ui/scenes/services/view_state.h"

#include "ui/shell/profiles/deluge.h"
#include "ui/shell/shell.h"

#include "hid/display/display.h"
#include "memory/heaps.h"
#include "model/clip/audio_clip.h"
#include "model/project_bridge/source_bridge.h"
#include "model/sample/sample.h"
#include "model/sample/sample_holder_for_voice.h"
#include "model/song/song.h"
#include "processing/sound/sound.h"
#include "processing/sound/source_resources.h"
#include "storage/cluster/cluster.h"
#include "storage/multi_range/multisample_range.h"

#include "support/popup_capture.h"
#include "support/song_fixture.h"

#include "support/menu_context.h"
#include "support/mocks/fake_surfaces.h"
#include "support/record_state.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace deluge::ui::scenes::spec::marker_fx {

using deluge::ui::scenes::marker::kMarkerSceneKind;
using deluge::ui::scenes::marker::marker_scene;
using deluge::ui::scenes::menu::MenuContext;
using deluge::ui::shell::BoardProfile;
using deluge::ui::shell::SceneId;
using deluge::ui::shell::SceneKind;
using deluge::ui::shell::Shell;
using deluge::ui::shell::ShellConfig;

// --- geometry ---------------------------------------------------------------------------------

inline constexpr size_t kClusterSize = 32768;
inline constexpr int32_t kBytesPerFrame = 2; // 16-bit mono, so one frame is two bytes
/// Thirty-two clusters' worth of frames, which is **two clusters per display column at max zoom**
/// -- and two is the number that matters. The scan only takes its cache arm when a column spans
/// two or more clusters (`waveform_scan.cpp`'s `numClustersSpan >= 2`); at one cluster per column
/// it takes the two-cluster arm with `investigatingAWholeCluster` left false and opens a reader,
/// which in a spec process fails on every column. Halving this length is enough to turn the whole
/// waveform black -- found by doing exactly that.
inline constexpr uint64_t kSampleLength = static_cast<uint64_t>(kClusterSize) * 32 / kBytesPerFrame;
inline constexpr int32_t kNumClusters = 32;
/// `WaveformView::max_zoom()` for `kSampleLength`: `ceil(len / 16)`.
inline constexpr int32_t kZoomMax = static_cast<int32_t>(((kSampleLength - 1) >> 4) + 1);

/// The per-cluster peak the fixture seeds the sample's peak index with: a widening shape, so a golden
/// shows something recognisable rather than a flat band.
inline int8_t cluster_peak(int32_t i) {
	return static_cast<int8_t>(16 + 3 * i);
}

inline Sample* make_sample() {
	Cluster::set_size(kClusterSize);

	// Placed in the deluge heap and never freed -- `song_fixture.h`'s own rule, and for its reason.
	void* memory = deluge::memory::alloc_fast(sizeof(Sample));
	auto* s = new (memory) Sample();
	s->byteDepth = 2;
	s->numChannels = 1;
	s->sampleRate = 44100;
	s->audioDataStartPosBytes = 0;
	s->audioDataLengthBytes = kSampleLength * kBytesPerFrame;
	s->lengthInSamples = kSampleLength;

	// The normalisation the bar heights are computed against
	// (`Sample::getFoundValueCentrePoint`/`getValueSpan`).
	peak_seed::seed_peaks(*s, [&](int32_t i) {
		return std::pair<int8_t, int8_t>{static_cast<int8_t>(-cluster_peak(i)), static_cast<int8_t>(cluster_peak(i))};
	});
	return s;
}

/// @brief A synth song whose source 0 is a sample source with one multisample range.
struct Rig {
	spec::SongFixture song = spec::make_synth_song();
	Sample* sample = make_sample();
	Sound* sound = nullptr;
	SourceResources* source = nullptr;
	MultisampleRange* range = nullptr;
	/// The view the editor remembers for the range as it opens: none (zoom 0) opens zoomed out.
	deluge::ui::scenes::services::SampleView remembered{};
	/// The waveforms the editor reads; the app's when null.
	const bind::Waveforms* waveforms = nullptr;

	const BoardProfile& profile;
	deluge::ui::shell::SceneRegistry reg = scene_registry();
	uitest::FakeSurfaceSet fake;
	Shell shell;

	Rig(const Rig&) = delete;
	Rig& operator=(const Rig&) = delete;

	/// @brief Tear the stack down, so every scene's `on_exit` always runs.
	///
	/// `MarkerScene` is a process singleton (`ui/shell/scene.h`'s `SceneState` cache): an example that
	/// left it -- or anything pushed above it -- on the stack would make the NEXT example's
	/// `on_enter` see stale state instead of a fresh one, the cross-contamination
	/// `support/recorder_scene_fixtures.h`'s `~Rig` was written to stop (a leaked recorder there makes the next
	/// `on_enter` refuse outright and fails every later example). Popping
	/// unconditionally means every example starts from the same place whatever the one before it did.
	~Rig() {
		while (shell.stack().depth() > 0) {
			shell.stack().pop(SceneKind::Null);
		}
		deluge::ui::scenes::spec::make_current(saved_song);
	}

	explicit Rig(const DelugeBoard& board = deluge::ui::shell::kDelugeBoard, const BoardProfile& p = deluge_profile())
	    : profile(p), fake(board, p), shell(fake, p, reg, deluge::ui::shell::Services{}, ShellConfig{}) {
		deluge::ui::scenes::spec::make_current(
		    song.song); // the app's ports and waveforms resolve addresses in the current song
		sound = deluge::model::effects_owner_of(deluge::model::owner_of(*song.instrument))->sound;
		source = &sound->sourceResources[0];
		deluge::test::set(*sound->state_, "osc1/type", "sample");
		deluge::test::set(*sound->state_, "osc1/loopMode", "1"); // ONCE
		deluge::test::set(*sound->state_, "osc1/reversed", "0");
		sound->deriveConfig();
		range = static_cast<MultisampleRange*>(sound->getOrCreateFirstRange(0));
		range->sampleHolder.audioFile = sample;
		range->sampleHolder.startPos = 0;
		range->sampleHolder.endPos = kSampleLength;
		range->sampleHolder.loopStartPos = 0;
		range->sampleHolder.loopEndPos = 0;
		range->sampleHolder.loopLocked = false;
		// The sound was set up field by field; record it as a load does, so its record is its truth.
		deluge::bridge::recordLoadedSound(*sound, *song.song);
	}

	MenuContext ctx() {
		// `context_for` resolves the range only when the source has exactly one, as it has here.
		MenuContext c = spec::ctx_of(*song.song, deluge::gui::menuViewOf(song.clip), 0, false, false, false,
		                             /*kit_global_fx=*/false);
		if (waveforms != nullptr) {
			c.ports.waveforms = waveforms;
		}
		return c;
	}

	/// @brief The waveform the editor opens on: the range.
	[[nodiscard]] bind::WaveformAddress address() {
		return {.kind = bind::WaveformAddress::Kind::RANGE, .slot = slot(), .source = 0, .range = 0};
	}

	/// @brief The key the scene remembers the range's view under: the range's own identity.
	deluge::ui::scenes::services::ViewKey key() {
		return {.kind = deluge::ui::scenes::services::ViewKey::Kind::RANGE, .id = range->identity()};
	}

	/// @brief The view the scene remembers for the range, if any.
	std::optional<deluge::ui::scenes::services::SampleView> view() {
		return deluge::ui::scenes::services::view_state().sample_view(key());
	}

	/// @brief Open the editor on @p marker, the way `LoopPoint::beginSession` does.
	/// @param sound_writes Where the scene's sound edits go; the app's when null.
	void open(deluge::project::SampleMarker marker, bind::SoundWrites* sound_writes = nullptr) {
		// Examples set the holder by hand before opening; record it as a load would, since an Edit starts from the
		// record.
		deluge::bridge::recordLoadedSound(*sound, *song.song);
		// The scene remembers the view and the loop lock in the view-state store: the view is the example's, the loop
		// lock the holder's, as main keeps it.
		const SampleHolderForVoice& h = range->sampleHolder;
		deluge::ui::scenes::services::view_state().forget_unless(song.song->project.identity());
		deluge::ui::scenes::services::view_state().set_sample_view(
		    key(), {.scroll = remembered.scroll, .zoom = remembered.zoom, .loop_locked = h.loopLocked});
		MenuContext c = ctx();
		if (sound_writes != nullptr) {
			c.ports.sound_writes = sound_writes;
		}
		marker_scene().open(c);
		shell.init(SceneId{kMarkerSceneKind, static_cast<uint16_t>(static_cast<int16_t>(marker))});
	}

	void tick(uint32_t at_ms) { shell.tick(at_ms); }

	/// @brief The sound's slot.
	deluge::project::SlotAddress slot() { return *ctx().addresses.slot; }

	/// @brief Close the editor and open it again on @p marker at range @p range_index, seeding nothing: it opens with
	///        what the view-state store kept.
	void reopen(deluge::project::SampleMarker marker, int16_t range_index) {
		while (shell.stack().depth() > 0) {
			shell.stack().pop(SceneKind::Null);
		}
		deluge::bridge::recordLoadedSound(*sound, *song.song);
		MenuContext c = ctx();
		c.range_index = range_index;
		marker_scene().open(c);
		shell.init(SceneId{kMarkerSceneKind, static_cast<uint16_t>(static_cast<int16_t>(marker))});
	}

	/// @brief Load the file at @p path into @p into, as a sample load does: the same range, another sample.
	Sample* load_into(MultisampleRange& into, const char* path) {
		Sample* loaded = make_sample();
		loaded->filePath = path; // the record names the range's file by its sample's path
		into.sampleHolder.filePath = path;
		into.sampleHolder.audioFile = loaded;
		return loaded;
	}

	/// @brief Open the menu at the sample start item, whose entry opens the editor over it: a shortcut's real door.
	/// @param root_is_automation Whether the root under the menu is the automation view, as the host's opener says.
	void open_through_menu(bool root_is_automation = false) {
		deluge::bridge::recordLoadedSound(*sound, *song.song);
		(void)menu::assign_node_ids(menu::nodes::kAllRoots);
		MenuContext c = ctx();
		c.root_is_automation = root_is_automation;
		menu_scene().open(menu::nodes::sample0_start, c);
		shell.init(SceneId{SceneKind::FirstApp, menu::node_id(menu::nodes::sample0_start)});
	}
	/// @brief The scene on top of the stack.
	SceneKind top() { return shell.stack().input_top().kind(); }
	void shift(bool on, uint32_t at_ms) {
		shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_BUTTON, 8, 0, static_cast<int16_t>(on ? 255 : 0)}, at_ms);
	}

	/// @brief One detent of the (detented, 2 edges per click) select encoder, index 3
	///        (`ui/shell/profiles/deluge.cpp`).
	void turn_select(int32_t detents, uint32_t at_ms) {
		shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_ENCODER, 3, 0, static_cast<int16_t>(detents * 2)}, at_ms);
	}
	void pad(int32_t x, int32_t y, bool on, uint32_t at_ms) {
		shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_PAD, static_cast<uint8_t>(x), static_cast<uint8_t>(y),
		                                      static_cast<int16_t>(on ? 255 : 0)},
		                     at_ms);
	}
	/// @brief Press or release the horizontal encoder's own button, at matrix (0, 1)
	///        (`ui/shell/profiles/deluge.cpp`). Legacy's `UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON`.
	void x_enc_button(bool on, uint32_t at_ms) {
		shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_BUTTON, 0, 1, static_cast<int16_t>(on ? 255 : 0)}, at_ms);
	}
	/// @brief One detent of the (detented) horizontal encoder, index 1 (`ui/shell/profiles/deluge.cpp`).
	void turn_horizontal(int32_t detents, uint32_t at_ms) {
		shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_ENCODER, 1, 0, static_cast<int16_t>(detents * 2)}, at_ms);
	}

	std::string oled() { return fake.pbm(0); }
	std::string grid() { return fake.state(); }

	Song* const saved_song = currentSong;
};

/// @brief Everything about the holder an editor run can move, so a second run can start from exactly the model the
///        first started from rather than from the model it left behind.
struct HolderState {
	uint64_t start{};
	uint64_t end{};
	uint32_t loop_start{};
	uint32_t loop_end{};
	bool locked{};
	deluge::ui::scenes::services::SampleView view{};
};

inline HolderState holder_snapshot(Rig& r) {
	const SampleHolderForVoice& h = r.range->sampleHolder;
	return HolderState{h.startPos, h.endPos, h.loopStartPos, h.loopEndPos, h.loopLocked, r.remembered};
}

inline void restore(Rig& r, const HolderState& s) {
	SampleHolderForVoice& h = r.range->sampleHolder;
	h.startPos = s.start;
	h.endPos = s.end;
	h.loopStartPos = s.loop_start;
	h.loopEndPos = s.loop_end;
	h.loopLocked = s.locked;
	r.remembered = s.view;
	// Set by hand, so recorded as a load would: the record is what an Edit starts from.
	deluge::bridge::recordLoadedSound(*r.sound, *r.song.song);
}

/// @brief An audio clip holding the sample, its markers set through the clip's own setters, with a `Shell` for the
///        scene. An audio clip's markers live in its descriptor record (`AudioClip::docAudioClip_`), which a save
///        writes.
struct AudioRig {
	spec::AudioSongFixture f = spec::make_audio_clip_song();
	Sample* sample = make_sample();
	const BoardProfile& profile;
	deluge::ui::shell::SceneRegistry reg = scene_registry();
	uitest::FakeSurfaceSet fake;
	Shell shell;
	/// The waveforms the editor reads; the app's when null.
	const bind::Waveforms* waveforms = nullptr;

	AudioRig(const AudioRig&) = delete;
	AudioRig& operator=(const AudioRig&) = delete;

	explicit AudioRig(const DelugeBoard& board = deluge::ui::shell::kDelugeBoard,
	                  const BoardProfile& p = deluge_profile())
	    : profile(p), fake(board, p), shell(fake, p, reg, deluge::ui::shell::Services{}, ShellConfig{}) {
		deluge::ui::scenes::spec::make_current(
		    f.song); // the app's ports and waveforms resolve addresses in the current song
		f.clip->sampleHolder.audioFile = sample;
		f.clip->sampleHolder.neutralPhaseIncrement = kMaxSampleValue;
		f.clip->setSampleStartPos(0);
		f.clip->setSampleEndPos(kSampleLength);
		f.clip->sampleControls.reversed = false;
	}
	~AudioRig() {
		while (shell.stack().depth() > 0) {
			shell.stack().pop(SceneKind::Null);
		}
		deluge::ui::scenes::spec::make_current(saved_song);
	}

	/// @brief Open the scene on @p marker over the audio clip.
	/// @param clip_writes Where the scene's clip edits go; the app's when null.
	void open(deluge::project::SampleMarker marker, bind::ClipWrites* clip_writes = nullptr) {
		MenuContext c = spec::ctx_of(*f.song, deluge::gui::menuViewOf(f.clip), 0, false, false, false,
		                             /*kit_global_fx=*/false);
		if (clip_writes != nullptr) {
			c.ports.clip_writes = clip_writes;
		}
		if (waveforms != nullptr) {
			c.ports.waveforms = waveforms;
		}
		marker_scene().open(c);
		shell.init(SceneId{kMarkerSceneKind, static_cast<uint16_t>(static_cast<int16_t>(marker))});
	}
	void tick(uint32_t at_ms) { shell.tick(at_ms); }
	void turn_select(int32_t detents, uint32_t at_ms) {
		shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_ENCODER, 3, 0, static_cast<int16_t>(detents * 2)}, at_ms);
	}

	/// @brief The waveform the editor opens on: the clip's.
	[[nodiscard]] bind::WaveformAddress address() const {
		return {.kind = bind::WaveformAddress::Kind::AUDIO_CLIP, .clip = deluge::project::ClipAddress{f.clip->id()}};
	}

	/// @brief The descriptor record's markers, as a save writes them.
	[[nodiscard]] std::pair<uint64_t, uint64_t> recorded() const {
		return {f.clip->docAudioClip_->start_pos, f.clip->docAudioClip_->end_pos};
	}
	/// @brief The holder's markers, as playback reads them.
	[[nodiscard]] std::pair<uint64_t, uint64_t> played() const {
		return {f.clip->sampleHolder.startPos, f.clip->sampleHolder.endPos};
	}

	Song* const saved_song = currentSong;
};

/// @brief A range's four markers as a reference row holds them: start, loop start, loop end, end.
inline std::vector<std::string> markers_of(const SampleHolderForVoice& h) {
	return {std::to_string(h.startPos), std::to_string(h.loopStartPos), std::to_string(h.loopEndPos),
	        std::to_string(h.endPos)};
}

/// @brief An audio clip's two markers as a reference row holds them: start, end.
inline std::vector<std::string> clip_markers_of(std::pair<uint64_t, uint64_t> markers) {
	return {std::to_string(markers.first), std::to_string(markers.second)};
}

} // namespace deluge::ui::scenes::spec::marker_fx
