/// @file
/// @brief The nine menu items that open a card-backed scene (`scenes/menu/nodes/sample.h`): which
///        node carries which item, and what each one actually opens when the menu focuses it.
///
/// Two groups, and they are deliberately different kinds of claim:
///
///   * **The mapping**, asserted by *identity* -- every one of the nine nodes carries the item its
///     legacy class became, with the source index and marker that class was constructed with, and
///     no other node in `nodes::kAllRoots` carries one of these items. Nine near-identical items
///     across two macro-generated oscillator pages is exactly the shape where a positional check
///     looks strong and pins nothing, so the observed table is compared whole and a deliberately
///     swapped copy is asserted **not** to match.
///   * **The opening**, driven end-to-end through a real `Shell`: the menu is opened on the node,
///     ticked, and the scene that lands on the stack is asked what it opened on. Each of the three
///     scenes is discriminated per source by a fact that differs between the two oscillators -- a
///     different folder, a different sample length, a range that exists on one source and not the
///     other -- so an item wired to the wrong source fails rather than passing on symmetry.
///
/// Plus the relevance half of the task: `osc<i>_file_selector` and `osc<i>_is_sample` are compared
/// against the legacy `isRelevant()` overrides they port over **eight** oscillator configurations
/// rather than the single fixture `tables_1_spec.cpp` uses, with each side driven from its own
/// sentinel (`SoundEditor`'s globals are written immediately before legacy is asked, and the port is
/// asked through a `MenuContext` built from the same model state).
///
/// @note The spec may include `gui/` (for the legacy menu items); the library may not. Same rule
///       `tables_1_spec.cpp`, `browser_scene_spec.cpp` and `marker_scene_spec.cpp` run under.

#include "scenes/browser/browser_scene.h"
#include "scenes/marker/marker_scene.h"
#include "scenes/menu/context.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/menu/node.h"
#include "scenes/menu/node_id.h"
#include "scenes/menu/nodes/nodes.h"
#include "scenes/menu/nodes/sample.h"
#include "scenes/menu/relevance.h"
#include "scenes/menu/validate.h"
#include "scenes/profile.h"
#include "scenes/recorder/recorder_scene.h"
#include "scenes/registry.h"

#include "shell/profiles/deluge.h"
#include "shell/shell.h"

#include "gui/l10n/l10n.h"
#include "gui/menu_item/file_selector.h"
#include "gui/menu_item/sample/end.h"
#include "gui/menu_item/sample/start.h"
#include "gui/ui/menus.h"
#include "gui/ui/sound_editor.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "model/clip/audio_clip.h"
#include "model/sample/sample.h"
#include "model/song/song.h"
#include "processing/sound/sound.h"
#include "processing/source.h"
#include "storage/audio/audio_file_holder.h"
#include "storage/cluster/cluster.h"
#include "storage/multi_range/multisample_range.h"

#include "support/song_fixture.h"

// Before <cppspec.hpp>: it #defines `it`, which collides with every local named `it` in the project
// headers this drags in (`audio_engine.h` -> `util/container/ordered_pos_vector.h`).
#include "support/card_access.h"

#include "spec/shell/support/fake_surfaces.h"

#include "cppspec.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include "spec/support/spec_main.h"

using namespace deluge::scenes;
using deluge::scenes::menu::MenuContext;
using deluge::scenes::menu::Node;
using deluge::ui::shell::BoardProfile;
using deluge::ui::shell::SceneId;
using deluge::ui::shell::SceneKind;
using deluge::ui::shell::Shell;
using deluge::ui::shell::ShellConfig;
namespace nodes = deluge::scenes::menu::nodes;

namespace {

// ---------------------------------------------------------------------------------------------
// The card the browser examples list

/// @brief A scratch card with one folder per oscillator, so "which source did the browser open on"
///        is a different directory rather than the same one twice.
///
/// Built under the host temp directory and removed again, for `browser_scene_spec.cpp`'s reason:
/// the committed `tests/fixtures/card` is flat and nearly empty, and nothing here may write to it.
struct ScratchCard {
	std::filesystem::path dir;
	spec::SdRoot root;

	static std::filesystem::path make() {
		std::filesystem::path d = std::filesystem::temp_directory_path() / "deluge_sample_items_spec";
		std::filesystem::remove_all(d);
		std::filesystem::create_directories(d / "SAMPLES" / "OSC_A");
		std::filesystem::create_directories(d / "SAMPLES" / "OSC_B");
		std::ofstream(d / "SAMPLES" / "OSC_A" / "A1.WAV") << "RIFF";
		std::ofstream(d / "SAMPLES" / "OSC_B" / "B1.WAV") << "RIFF";
		return d;
	}

	ScratchCard() : dir(make()), root(dir.string()) {}
	~ScratchCard() { std::filesystem::remove_all(dir); }
	ScratchCard(const ScratchCard&) = delete;
	ScratchCard& operator=(const ScratchCard&) = delete;
};

// ---------------------------------------------------------------------------------------------
// The samples the marker examples open on

constexpr size_t kClusterSize = 32768;
constexpr int32_t kBytesPerFrame = 2;

/// @brief A `Sample` with a fully investigated overview cache, so nothing reads the card.
///
/// `marker_scene_spec.cpp`'s fixture, parameterised by cluster count: **two clusters per display
/// column is a cliff**, not a preference (`waveform_scan.cpp`'s `numClustersSpan >= 2`), so the two
/// samples here are 32 and 64 clusters long -- both over the line, and different enough that
/// `WaveformNav::max_zoom()` tells them apart.
Sample* make_sample(int32_t num_clusters) {
	Cluster::set_size(kClusterSize);
	const uint64_t length = static_cast<uint64_t>(kClusterSize) * num_clusters / kBytesPerFrame;

	// Placed in the deluge heap and never freed -- `song_fixture.h`'s own rule, and for its reason.
	void* memory = deluge::memory::alloc_fast(sizeof(Sample));
	auto* s = new (memory) Sample();
	s->byteDepth = 2;
	s->numChannels = 1;
	s->sampleRate = 44100;
	s->audioDataStartPosBytes = 0;
	s->audioDataLengthBytes = length * kBytesPerFrame;
	s->lengthInSamples = length;
	s->maxValueFound = 127 << 24;
	s->minValueFound = -128 << 24;
	s->resizeOverviewCache(num_clusters);
	for (int32_t i = 0; i < num_clusters; i++) {
		OverviewCacheEntry& e = s->overviewCacheEntry(static_cast<uint32_t>(i));
		e.min = static_cast<int8_t>(-(16 + i));
		e.max = static_cast<int8_t>(16 + i);
		e.investigated = true;
	}
	return s;
}

/// `WaveformNav::max_zoom()` for a sample of @p num_clusters clusters.
int64_t zoom_max(int32_t num_clusters) {
	const uint64_t length = static_cast<uint64_t>(kClusterSize) * num_clusters / kBytesPerFrame;
	return static_cast<int64_t>(((length - 1) >> 4) + 1);
}

/// @brief The minimum `Display` the legacy items and the marker scene need. `display` is null in a
///        spec process (`hid/display/display.cpp:4`).
struct QuietDisplay final : deluge::hid::Display {
	constexpr size_t getNumBrowserAndMenuLines() override { return 3; }
	void displayPopup(char const*, int8_t, bool, uint8_t, int32_t, PopupType) override {}
	void popupText(char const*, PopupType) override {}
	void popupTextTemporary(char const*, PopupType) override {}
	void cancelPopup() override {}
	void freezeWithError(char const*) override {}
	void displayError(Error) override {}
	void removeWorkingAnimation() override {}
	void displayLoadingAnimationText(char const*, bool, bool) override {}
	void removeLoadingAnimation() override {}
	bool hasPopup() override { return false; }
	bool hasPopupOfType(PopupType) override { return false; }
	void consoleText(char const*) override {}
	void timerRoutine() override {}
};

void select_language() {
	deluge::l10n::chosenLanguage = &deluge::l10n::built_in::english;
}

bool scene_on_stack(Shell& shell, SceneKind kind) {
	for (const deluge::ui::shell::Scene* s : shell.stack().scenes()) {
		if (s->kind() == kind) {
			return true;
		}
	}
	return false;
}

/// @brief Focus @p node in the menu and let the shell apply whatever the item asked for.
///
/// Four ticks, not one. `MenuScene::after_render` files the `Push` at the *end* of the tick that
/// rendered the item, so the tick that opens a scene is never the tick that draws it; and
/// `BrowserScene` collects its listing in its own `after_render`, a tick later again (the seam is
/// asynchronous by construction even though `storage::Owner::run` is inline on the host --
/// `listing.h`'s honesty note). Ticking past both is what lets one helper serve all three scenes.
void focus_in(Shell& shell, const MenuContext& ctx, const Node& node) {
	select_language();
	(void)menu::assign_node_ids(nodes::kAllRoots);
	menu_scene().open(node, ctx);
	shell.init(SceneId{SceneKind::FirstApp, menu::node_id(node)});
	for (uint32_t at = 0; at <= 30; at += 10) {
		shell.tick(at);
	}
}

[[noreturn]] void spec_bail(const char* what) {
	std::fprintf(stderr, "sample_items_spec: %s\n", what);
	std::abort();
}

/// @brief Refuse to start, or to finish, with either scene singleton still holding something.
///
/// **Assert-then-clear, not defensive-clear.** Scenes are process singletons, so an example that
/// leaked a listing or an armed recorder would make the *next* example's refusal indistinguishable
/// from a wiring bug -- and a `before_each` that quietly tidied up is exactly what hid a real defect
/// through task 5's first fix pass. Each rig's destructor tears its own stack down; this guard's job
/// is to say so out loud if one did not. `before_each`/`after_each` take a `std::function<void()>`
/// with no `self` (`cppspec/description.hpp`), so `expect` is unavailable there and a named reason
/// on stderr plus `std::abort()` is what "loudly" means.
struct CleanScenes {
	CleanScenes() { check("a scene singleton was still busy when an example started"); }
	~CleanScenes() { check("an example finished with a scene singleton still busy"); }
	CleanScenes(const CleanScenes&) = delete;
	CleanScenes& operator=(const CleanScenes&) = delete;

private:
	static void check(const char* what) {
		// One per scene, and all three, because all three are singletons this file opens. The
		// marker editor has no allocation to leak, but it does have per-visit gesture state that
		// `on_exit` clears (`MarkerScene::on_exit`: legacy's `UI_MODE_HOLDING_SAMPLE_MARKER`), and a
		// leaked hold would make the next example's pad input behave differently for no visible
		// reason -- the same class of cross-contamination, one layer smaller.
		if (browser::browser_scene().listing_in_flight() || recorder::recorder_scene().recording()
		    || marker::marker_scene().holding_marker()) {
			spec_bail(what);
		}
	}
};

// ---------------------------------------------------------------------------------------------
// The rig: a synth song whose two oscillators are both sample sources, each with its own sample

struct SoundRig {
	spec::SongFixture song = spec::make_synth_song();
	Sound* sound = nullptr;
	Source* source[2] = {nullptr, nullptr};
	MultisampleRange* range[2] = {nullptr, nullptr};
	Sample* sample[2] = {nullptr, nullptr};

	deluge::ui::shell::SceneRegistry reg = scene_registry();
	uitest::FakeSurfaceSet fake;
	Shell shell;

	SoundRig(const SoundRig&) = delete;
	SoundRig& operator=(const SoundRig&) = delete;

	/// @brief Tear the stack down, so every scene's `on_exit` always runs.
	///
	/// **Scenes are process singletons** (task 5's finding): an example that left the recorder
	/// armed would make the next example's `on_enter` refuse and cascade into failures that say
	/// nothing about the mutation that caused them. Popping here is the assert-then-clear discipline
	/// as an RAII guard rather than as a `before_each` reset -- the state is destroyed on the way
	/// out of the example that made it, not tidied up on the way into the next one.
	~SoundRig() {
		while (shell.stack().depth() > 0) {
			shell.stack().pop(SceneKind::Null);
		}
	}

	/// @param second_source True to give oscillator 1 a sample source of its own; false to leave it
	///                      a square wave with no `MultiRange` at all, which is the shape that makes
	///                      "the item selected source 1" observable as a refusal.
	explicit SoundRig(bool second_source)
	    : fake(deluge::ui::shell::kDelugeBoard, deluge_profile()),
	      shell(fake, deluge_profile(), reg, deluge::ui::shell::Services{}, ShellConfig{}) {
		sound = static_cast<Sound*>(static_cast<ModControllableAudio*>(song.instrument->toModControllable()));
		setup_source(0, 32);
		if (second_source) {
			setup_source(1, 64);
		}
	}

	void setup_source(int32_t i, int32_t num_clusters) {
		source[i] = &sound->sources[i];
		source[i]->oscType = OscType::SAMPLE;
		source[i]->repeatMode = SampleRepeatMode::ONCE;
		source[i]->sampleControls.reversed = false;
		sample[i] = make_sample(num_clusters);
		range[i] = static_cast<MultisampleRange*>(source[i]->getOrCreateFirstRange());
		range[i]->sampleHolder.audioFile = sample[i];
		range[i]->sampleHolder.filePath = i == 0 ? "SAMPLES/OSC_A/A1.WAV" : "SAMPLES/OSC_B/B1.WAV";
		range[i]->sampleHolder.startPos = 0;
		range[i]->sampleHolder.endPos = sample[i]->lengthInSamples;
		range[i]->sampleHolder.loopStartPos = 0;
		range[i]->sampleHolder.loopEndPos = 0;
		range[i]->sampleHolder.loopLocked = false;
		range[i]->sampleHolder.waveformViewZoom = 0;
		range[i]->sampleHolder.waveformViewScroll = 0;
	}

	/// @brief The context `SoundEditor::setup` would have resolved for **source 0** -- which is the
	///        point: nothing re-resolves it on the way into source 1's page, so an item that opens a
	///        scene over source 1 has to say so itself.
	MenuContext ctx() {
		MenuContext c = menu::context_for(*song.song, song.clip, 0, false, false, /*kit_global_fx=*/false);
		c.source = source[0];
		c.range = range[0];
		c.sample_controls = &source[0]->sampleControls;
		c.range_index = 0;
		return c;
	}

	/// @brief Focus @p node in the menu and let the shell apply whatever it asked for
	///        (`focus_in`, which says how many ticks and why).
	void focus(const Node& node) { focus_in(shell, ctx(), node); }

	bool on_stack(SceneKind kind) { return scene_on_stack(shell, kind); }
};

/// @brief The audio-clip counterpart, for `audio_clip_waveform`.
struct ClipRig {
	spec::AudioSongFixture song = spec::make_audio_clip_song();
	Sample* sample = make_sample(48);

	deluge::ui::shell::SceneRegistry reg = scene_registry();
	uitest::FakeSurfaceSet fake;
	Shell shell;

	ClipRig(const ClipRig&) = delete;
	ClipRig& operator=(const ClipRig&) = delete;
	~ClipRig() {
		while (shell.stack().depth() > 0) {
			shell.stack().pop(SceneKind::Null);
		}
	}

	ClipRig()
	    : fake(deluge::ui::shell::kDelugeBoard, deluge_profile()),
	      shell(fake, deluge_profile(), reg, deluge::ui::shell::Services{}, ShellConfig{}) {
		song.clip->sampleHolder.audioFile = sample;
		song.clip->sampleHolder.filePath = "SAMPLES/OSC_A/A1.WAV";
		song.clip->sampleHolder.startPos = 0;
		song.clip->sampleHolder.endPos = sample->lengthInSamples;
		song.clip->sampleHolder.waveformViewZoom = 0;
		song.clip->sampleHolder.waveformViewScroll = 0;
	}

	MenuContext ctx() { return menu::context_for(*song.song, song.clip, 0, false, false, /*kit_global_fx=*/false); }

	void focus(const Node& node) { focus_in(shell, ctx(), node); }

	bool on_stack(SceneKind kind) { return scene_on_stack(shell, kind); }
};

// ---------------------------------------------------------------------------------------------
// The mapping, as a value that can be compared whole

/// @brief Which of the three item classes a node carries, plus what it was constructed with.
struct Wiring {
	std::string node; ///< the node's own name, so a failure names it
	int kind = -1;    ///< 0 = FileBrowserItem, 1 = MarkerItem, 2 = AudioRecorderItem, -1 = none
	int source = -99; ///< `SampleSceneItem::source_index()`
	int marker = -1;  ///< `MarkerItem::marker()`, or -1 for the two that have none

	bool operator==(const Wiring&) const = default;
};

Wiring wiring_of(const char* name, const Node& node) {
	Wiring w;
	w.node = name;
	const auto* base = dynamic_cast<const nodes::SampleSceneItem*>(node.custom);
	if (base == nullptr) {
		return w;
	}
	w.source = base->source_index();
	if (dynamic_cast<const nodes::FileBrowserItem*>(base) != nullptr) {
		w.kind = 0;
	}
	else if (const auto* m = dynamic_cast<const nodes::MarkerItem*>(base); m != nullptr) {
		w.kind = 1;
		w.marker = static_cast<int>(m->marker());
	}
	else if (dynamic_cast<const nodes::AudioRecorderItem*>(base) != nullptr) {
		w.kind = 2;
	}
	return w;
}

std::string format_wiring(const std::vector<Wiring>& ws) {
	std::string out;
	for (const Wiring& w : ws) {
		out += w.node + "={kind=" + std::to_string(w.kind) + ",src=" + std::to_string(w.source)
		       + ",marker=" + std::to_string(w.marker) + "} ";
	}
	return out;
}

std::vector<Wiring> observed_wiring() {
	return {
	    wiring_of("file0_selector", nodes::file0_selector),
	    wiring_of("file1_selector", nodes::file1_selector),
	    wiring_of("sample0_start", nodes::sample0_start),
	    wiring_of("sample1_start", nodes::sample1_start),
	    wiring_of("sample0_end", nodes::sample0_end),
	    wiring_of("sample1_end", nodes::sample1_end),
	    wiring_of("audio_clip_waveform", nodes::audio_clip_waveform),
	    wiring_of("sample0_recorder", nodes::sample0_recorder),
	    wiring_of("sample1_recorder", nodes::sample1_recorder),
	};
}

/// @brief What each node must carry, derived from the legacy class each node's item ports.
///
/// `MarkerType::START` is 0 and `MarkerType::END` is 3 (`definitions_cxx.hpp:777-784`); the same
/// two numbers are `static_assert`ed against the enum in `nodes/sample.cpp`, so this table and the
/// items cannot drift apart silently.
std::vector<Wiring> expected_wiring() {
	return {
	    // `FileSelector` (`gui/menu_item/file_selector.cpp:38,44`): `setCurrentSource(sourceId_)`,
	    // then `openUI(&sampleBrowser)`.
	    {"file0_selector", 0, 0, -1},
	    {"file1_selector", 0, 1, -1},
	    // `sample::Start` (`gui/menu_item/sample/start.h:23`) sets `markerType = START`;
	    // `LoopPoint::beginSession` (`sample/loop_point.cpp:68-69`) sets the source first.
	    {"sample0_start", 1, 0, 0},
	    {"sample1_start", 1, 1, 0},
	    // `sample::End` (`gui/menu_item/sample/end.h:23`) sets `markerType = END`.
	    {"sample0_end", 1, 0, 3},
	    {"sample1_end", 1, 1, 3},
	    // `audioClipSampleMarkerEditorMenuEnd` (`gui/ui/menus.cpp:1183`) -- `MarkerType::END`, and
	    // **no** `setCurrentSource` (`audio_clip/sample_marker_editor.cpp:43-51`), which is the -1.
	    {"audio_clip_waveform", 1, -1, 3},
	    // `osc::AudioRecorder::beginSession` (`osc/audio_recorder.h:38,50`).
	    {"sample0_recorder", 2, 0, -1},
	    {"sample1_recorder", 2, 1, -1},
	};
}

// ---------------------------------------------------------------------------------------------
// The relevance comparison

/// @brief One oscillator configuration to compare the two relevance predicates under.
struct OscCase {
	const char* name;
	::SynthMode synth_mode;
	OscType osc_type;
	bool load_file;
};

constexpr OscCase kOscCases[] = {
    {"subtractive+sample+loaded", ::SynthMode::SUBTRACTIVE, OscType::SAMPLE, true},
    {"subtractive+sample+empty", ::SynthMode::SUBTRACTIVE, OscType::SAMPLE, false},
    {"subtractive+wavetable+loaded", ::SynthMode::SUBTRACTIVE, OscType::WAVETABLE, true},
    {"subtractive+square", ::SynthMode::SUBTRACTIVE, OscType::SQUARE, false},
    {"ringmod+sample+loaded", ::SynthMode::RINGMOD, OscType::SAMPLE, true},
    {"ringmod+wavetable+loaded", ::SynthMode::RINGMOD, OscType::WAVETABLE, true},
    {"fm+sample+loaded", ::SynthMode::FM, OscType::SAMPLE, true},
    {"fm+wavetable+loaded", ::SynthMode::FM, OscType::WAVETABLE, true},
};

} // namespace

// clang-format off
describe sample_items_mapping("scenes::menu the sample items: the mapping", $ {

	it("carries the item its legacy class became, on every one of the nine nodes", _ {
		// Compared as one whole table rather than node by node, so a failure prints which row moved.
		expect(format_wiring(observed_wiring())).to_equal(format_wiring(expected_wiring()));

		// ... and the comparison is not vacuous. Nine near-identical items over two macro-generated
		// oscillator pages is precisely the shape where a positional check passes with two of them
		// crossed over, so a deliberately crossed copy is asserted NOT to match. (The real proof is
		// the source mutation recorded in the task report; this is the guard that keeps the
		// assertion itself honest if someone later relaxes it.)
		//
		// The crossing swaps the *payload* fields only and leaves each row's node name where it is,
		// because that is the shape a real miswiring has: `sample0_start` still called
		// `sample0_start`, carrying source 1's item. Swapping whole rows -- name included -- would
		// only have shown that the table notices a reordering, which nothing can cause.
		std::vector<Wiring> crossed_sources = observed_wiring();
		std::swap(crossed_sources[2].source, crossed_sources[3].source); // sample0_start <-> sample1_start
		expect(format_wiring(crossed_sources) == format_wiring(expected_wiring())).to_be_false();
		std::vector<Wiring> crossed_markers = observed_wiring();
		std::swap(crossed_markers[2].marker, crossed_markers[4].marker); // sample0_start <-> sample0_end
		expect(format_wiring(crossed_markers) == format_wiring(expected_wiring())).to_be_false();
	});

	it("puts these items on those nine nodes and on nothing else in the tree", _ {
		// By identity over the whole reachable tree, not by a count: an item accidentally attached to
		// a tenth node somewhere else would leave every assertion above passing.
		select_language();
		const uint16_t assigned = menu::assign_node_ids(nodes::kAllRoots);

		const Node* const kExpected[] = {
		    &nodes::file0_selector,   &nodes::file1_selector,   &nodes::sample0_start,
		    &nodes::sample1_start,    &nodes::sample0_end,      &nodes::sample1_end,
		    &nodes::audio_clip_waveform, &nodes::sample0_recorder, &nodes::sample1_recorder,
		};
		std::set<const Node*> want{std::begin(kExpected), std::end(kExpected)};

		std::set<const Node*> found;
		for (uint16_t id = 1; id <= assigned; id++) {
			const Node* n = menu::node_by_id(id);
			if (n != nullptr && n->kind == menu::Kind::Custom
			    && dynamic_cast<const nodes::SampleSceneItem*>(n->custom) != nullptr) {
				found.insert(n);
			}
		}
		const auto ids = [](const std::set<const Node*>& in) {
			std::vector<std::string> parts;
			for (const Node* n : in) {
				parts.push_back(std::to_string(menu::node_id(*n)) + "=" + std::to_string(n->name));
			}
			std::sort(parts.begin(), parts.end());
			std::string out;
			for (const std::string& p : parts) {
				out += p + " ";
			}
			return out;
		};
		expect(ids(found)).to_equal(ids(want));
	});

	it("retires nine placeholders and leaves exactly two", _ {
		// The count `validate.h` carries, checked from the other end: none of the nine is a
		// placeholder any more, and the two that are named there still are.
		for (const Node* n : {&nodes::file0_selector, &nodes::file1_selector, &nodes::sample0_start,
		                      &nodes::sample1_start, &nodes::sample0_end, &nodes::sample1_end,
		                      &nodes::audio_clip_waveform, &nodes::sample0_recorder, &nodes::sample1_recorder}) {
			// Two assertions, not one: `custom != nullptr && is_placeholder()` is also false for a
			// null `custom`, which is a worse bug than a placeholder (`validate.h`'s `NullCustom`).
			expect(n->custom != nullptr).to_be_true();
			expect(n->custom != nullptr && n->custom->is_placeholder()).to_be_false();
		}
		expect(nodes::midi_device_linked.custom->is_placeholder()).to_be_true();
		expect(nodes::specific_output_selector.custom->is_placeholder()).to_be_true();
		expect(menu::kPendingCustomNodes).to_equal(size_t(2));
	});
});

describe sample_items_opening("scenes::menu the sample items: what each one opens", $ {

	before_each([] {
		static QuietDisplay d;
		if (display == nullptr) {
			display = &d;
		}
	});

	it("file0_selector opens the sample browser in source 0's folder", _ {
		spec::QuietAudio quiet;
		CleanScenes clean;
		ScratchCard card;
		SoundRig r{/*second_source=*/true};
		r.focus(nodes::file0_selector);

		expect(r.on_stack(browser::kBrowserSceneKind)).to_be_true();
		expect(static_cast<int>(browser::browser_scene().browser_id()))
		    .to_equal(static_cast<int>(browser::BrowserId::Sample));
		// `sample_start_at` (`browser_scene.cpp`) split source 0's holder path, which is only source
		// 0's because `select_source` left the context where it was.
		expect(std::string(browser::browser_scene().current_dir())).to_equal(std::string("SAMPLES/OSC_A"));
	});

	it("file1_selector opens it in source 1's folder instead", _ {
		// The per-source discriminator for this scene: the two oscillators' holders name different
		// directories, so an item wired to the wrong source lands in the wrong one. Wiring both
		// nodes to `kFile0Selector` passes the previous example and fails this one.
		spec::QuietAudio quiet;
		CleanScenes clean;
		ScratchCard card;
		SoundRig r{/*second_source=*/true};
		r.focus(nodes::file1_selector);

		expect(r.on_stack(browser::kBrowserSceneKind)).to_be_true();
		expect(std::string(browser::browser_scene().current_dir())).to_equal(std::string("SAMPLES/OSC_B"));
	});

	it("sample0_start and sample0_end open the marker editor on their own marker", _ {
		spec::QuietAudio quiet;
		CleanScenes clean;
		{
			SoundRig r{true};
			r.focus(nodes::sample0_start);
			expect(r.on_stack(marker::kMarkerSceneKind)).to_be_true();
			expect(static_cast<int>(marker::marker_scene().marker_type()))
			    .to_equal(static_cast<int>(MarkerType::START));
		}
		{
			SoundRig r{true};
			r.focus(nodes::sample0_end);
			expect(r.on_stack(marker::kMarkerSceneKind)).to_be_true();
			expect(static_cast<int>(marker::marker_scene().marker_type())).to_equal(static_cast<int>(MarkerType::END));
		}
	});

	it("sample1_start opens it on source 1's sample, not source 0's", _ {
		// The per-source discriminator for this scene. The two samples are 32 and 64 clusters long,
		// so `WaveformNav::opened` -- which zooms all the way out on a holder with no remembered view
		// -- lands on a different `xZoom` for each. A `sample1_start` wired to source 0 opens the
		// editor, shows a waveform, and reports 32 clusters' zoom.
		spec::QuietAudio quiet;
		CleanScenes clean;
		{
			SoundRig r{true};
			r.focus(nodes::sample0_start);
			expect(static_cast<int64_t>(marker::marker_scene().x_zoom())).to_equal(zoom_max(32));
		}
		{
			SoundRig r{true};
			r.focus(nodes::sample1_start);
			expect(r.on_stack(marker::kMarkerSceneKind)).to_be_true();
			expect(static_cast<int64_t>(marker::marker_scene().x_zoom())).to_equal(zoom_max(64));
		}
	});

	it("audio_clip_waveform opens the marker editor on the clip's own holder, on END", _ {
		// `audioClipSampleMarkerEditorMenuEnd` (`gui/ui/menus.cpp:1183`, `:1193`) is the END-marker
		// spelling, and the audio-clip opener selects no source at all -- the context has no `Sound`
		// for `select_source` to act on, which is why the item carries -1.
		spec::QuietAudio quiet;
		CleanScenes clean;
		ClipRig r;
		r.focus(nodes::audio_clip_waveform);

		expect(r.on_stack(marker::kMarkerSceneKind)).to_be_true();
		expect(static_cast<int>(marker::marker_scene().marker_type())).to_equal(static_cast<int>(MarkerType::END));
		expect(static_cast<int64_t>(marker::marker_scene().x_zoom())).to_equal(zoom_max(48));
	});

	it("refuses to open the marker editor when the clip has no sample, as legacy refuses", _ {
		// `audio_clip::SampleMarkerEditor::checkPermissionToBeginSession`
		// (`audio_clip/sample_marker_editor.cpp:35-38`) as `MarkerScene::can`'s audio-clip arm: the
		// node has no relevance predicate (legacy overrides none either), so the refusal is the
		// scene's and it has to actually happen.
		spec::QuietAudio quiet;
		CleanScenes clean;
		ClipRig r;
		r.song.clip->sampleHolder.audioFile = nullptr;
		r.focus(nodes::audio_clip_waveform);
		expect(r.on_stack(marker::kMarkerSceneKind)).to_be_false();
	});

	it("sample0_recorder opens the audio recorder", _ {
		spec::QuietAudio quiet;
		CleanScenes clean;
		ScratchCard card; // so a future change that DID write a file lands here, not in the fixture
		SoundRig r{true};
		r.focus(nodes::sample0_recorder);

		expect(r.on_stack(recorder::kRecorderSceneKind)).to_be_true();
		expect(recorder::recorder_scene().recording()).to_be_true();
	});

	it("sample1_recorder opens it over source 1, which is what a missing range proves", _ {
		// The per-source discriminator for this scene. Oscillator 1 is left a square wave with no
		// `MultiRange` at all, so `RecorderScene::can`'s `ctx.range == nullptr` refusal fires -- but
		// only if the item re-pointed the context at source 1. Wired to source 0 it would find source
		// 0's range and open, which is what makes this a test of `select_source` and not of `can`.
		spec::QuietAudio quiet;
		CleanScenes clean;
		ScratchCard card;
		SoundRig r{/*second_source=*/false};
		expect(r.source[1] == nullptr).to_be_true();

		r.focus(nodes::sample1_recorder);
		expect(r.on_stack(recorder::kRecorderSceneKind)).to_be_false();

		// And the same rig opens on source 0, so the refusal above is about the source rather than
		// about the rig being unable to record at all.
		SoundRig r2{/*second_source=*/false};
		r2.focus(nodes::sample0_recorder);
		expect(r2.on_stack(recorder::kRecorderSceneKind)).to_be_true();
	});

	it("does not leave source 0's range behind when it selects source 1", _ {
		// `menu::select_source` folds in `SoundEditor::setup`'s `:1786-1788`, which nulls a
		// remembered range whenever the source index moves. Asserted directly, because the two
		// examples above can only see it through a scene: without it, a context resolved for source
		// 0 and re-pointed at source 1 would still name source 0's `MultiRange`, and every scene here
		// dereferences `ctx.range`.
		spec::QuietAudio quiet;
		CleanScenes clean;
		SoundRig r{/*second_source=*/true};
		MenuContext c = r.ctx();
		expect(c.range == r.range[0]).to_be_true();

		menu::select_source(c, 1);
		expect(static_cast<int>(c.source_index)).to_equal(1);
		expect(c.source == r.source[1]).to_be_true();
		expect(c.range == r.range[1]).to_be_true();
		expect(c.sample_controls == &r.source[1]->sampleControls).to_be_true();

		// Re-selecting the source already named keeps the selected range -- `setCurrentSource`'s own
		// `currentMultiRange == nullptr` guard (`sound_editor.cpp:209`), which is what stops a
		// range the picker chose from being thrown away by the item that opens on it.
		menu::select_source(c, 1);
		expect(c.range == r.range[1]).to_be_true();

		// A context with no `Sound` is left exactly as it is -- the audio-clip case.
		MenuContext none{};
		menu::select_source(none, 1);
		expect(none.source == nullptr).to_be_true();
		expect(static_cast<int>(none.source_index)).to_equal(0);
	});
});

describe sample_items_relevance("scenes::menu the sample items: relevance against legacy", $ {

	it("agrees with FileSelector::isRelevant and isSampleModeSample in every oscillator mode", _ {
		// `tables_1_spec.cpp` already compares every batch-1 node's predicate against its legacy
		// item, but on ONE fixture sound -- which for these two predicates exercises one of the eight
		// configurations below. Each side is driven from its own sentinel: the model is written, then
		// legacy is asked through `soundEditor`'s globals, then the model is written again and the
		// port is asked through a freshly built `MenuContext`.
		select_language();
		spec::SongFixture song = spec::make_synth_song();
		auto* sound = static_cast<Sound*>(static_cast<ModControllableAudio*>(song.instrument->toModControllable()));
		Sample* sample = make_sample(32);

		const auto seed = [&](const OscCase& c, int32_t which) {
			sound->synthMode = c.synth_mode;
			for (int32_t i = 0; i < 2; i++) {
				Source& s = sound->sources[i];
				s.oscType = i == which ? c.osc_type : OscType::SQUARE;
				auto* range = static_cast<MultisampleRange*>(s.getOrCreateFirstRange());
				range->sampleHolder.audioFile = (i == which && c.load_file) ? sample : nullptr;
			}
		};

		std::vector<std::string> observed;
		std::vector<std::string> want;
		for (const OscCase& c : kOscCases) {
			for (int32_t which = 0; which < 2; which++) {
				// --- legacy, from its own sentinel
				seed(c, which);
				soundEditor.currentSound = sound;
				soundEditor.currentSourceIndex = static_cast<int8_t>(which);
				soundEditor.currentSource = &sound->sources[which];
				const bool legacy_file = (which == 0 ? deluge::gui::menu_item::file0SelectorMenu
				                                     : deluge::gui::menu_item::file1SelectorMenu)
				                             .isRelevant(sound, which);
				const bool legacy_loop = (which == 0 ? sample0StartMenu : sample1StartMenu).isRelevant(sound, which);
				const bool legacy_end = (which == 0 ? sample0EndMenu : sample1EndMenu).isRelevant(sound, which);

				// --- port, same sentinel written again, through its own context
				seed(c, which);
				const MenuContext ctx =
				    menu::context_for(*song.song, song.clip, static_cast<int8_t>(which), false, false, false);
				const bool port_file = (which == 0 ? menu::relevance::osc0_file_selector
				                                   : menu::relevance::osc1_file_selector)(ctx);
				const bool port_sample =
				    (which == 0 ? menu::relevance::osc0_is_sample : menu::relevance::osc1_is_sample)(ctx);

				const std::string tag = std::string{c.name} + "/osc" + std::to_string(which) + ":";
				want.push_back(tag + (legacy_file ? "F1" : "F0") + (legacy_loop ? "S1" : "S0")
				               + (legacy_end ? "E1" : "E0"));
				observed.push_back(tag + (port_file ? "F1" : "F0") + (port_sample ? "S1" : "S0")
				                   + (port_sample ? "E1" : "E0"));
			}
		}
		expect(observed).to_equal(want);

		// Not vacuous: the sixteen rows are not all the same answer. (If they were, a predicate that
		// returned a constant would pass the comparison above.)
		std::set<std::string> answers;
		for (const std::string& row : want) {
			answers.insert(row.substr(row.find(':') + 1));
		}
		expect(answers.size() > size_t(1)).to_be_true();
	});
});
// clang-format on

/// Three `describe` groups in one TU, so `DELUGE_SPEC` is spelled out with `add_specs`
/// (`audio_source_spec.cpp`'s shape).
int sample_items_spec(int argc, char** const argv) {
	return CppSpec::parse(argc, argv)
	               .add_specs(sample_items_mapping, sample_items_opening, sample_items_relevance)
	               .exec()
	               .is_success()
	           ? EXIT_SUCCESS
	           : EXIT_FAILURE;
}
