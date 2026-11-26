#pragma once

/// @file
/// @brief The slicer's fixtures, which `slicer_scene_spec` and the menu oracle freezer share: a sample whose every
///        display column is an answered from its peak index, a `Shell` over the scene opened through the app's slicer
///        host, a `SliceOps` that records rather than acts (`fake_slice_ops.h`), and the port's own front door for a
///        seeded slice table.
///
/// The sample never touches the card: its peak index is seeded (`peak_seed.h`), so every column on block boundaries is
/// answered from the index and the waveform the fixture asked for is drawn with no reader opened.

#include "peak_seed.h"
#include "ui/scenes/menu/bind_host.h"
#include "ui/scenes/profile.h"
#include "ui/scenes/registry.h"
#include "ui/scenes/slicing/slicer_scene.h"

#include "ui/shell/profiles/deluge.h"
#include "ui/shell/shell.h"

#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/matrix/matrix_driver.h"
#include "memory/heaps.h"
#include "model/sample/sample.h"
#include "storage/cluster/cluster.h"

#include "support/slicer_host_open.h"
#include "support/song_fixture.h"

#include "fake_slice_ops.h"

#include "support/mocks/fake_surfaces.h"

#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace deluge::ui::scenes::spec::slicer_fx {

using deluge::ui::scenes::slicing::kSlicerSceneKind;
using deluge::ui::scenes::slicing::slicer_scene;
using deluge::ui::shell::BoardProfile;
using deluge::ui::shell::SceneId;
using deluge::ui::shell::SceneKind;
using deluge::ui::shell::Shell;
using deluge::ui::shell::ShellConfig;

// --- the sample -------------------------------------------------------------------------------

inline constexpr size_t kClusterSize = 32768;
inline constexpr int32_t kBytesPerFrame = 2; // 16-bit mono
/// Thirty-two clusters' worth of frames -- two clusters per display column at max zoom, which is
/// the number that makes the scan take its cached whole-cluster arm. See the file docstring.
inline constexpr uint64_t kSampleLength = static_cast<uint64_t>(kClusterSize) * 32 / kBytesPerFrame;
inline constexpr int32_t kNumClusters = 32;

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

	peak_seed::seed_peaks(*s, [&](int32_t i) {
		return std::pair<int8_t, int8_t>{static_cast<int8_t>(static_cast<int8_t>(-cluster_peak(i))),
		                                 static_cast<int8_t>(cluster_peak(i))};
	});
	return s;
}

/// @brief The minimum `Display` a slicer run needs: `display` is null in a spec process, and the encoder arms pop up.
struct SlicerDisplay final : deluge::hid::Display {
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

// --- the port's rig ---------------------------------------------------------------------------

/// The path the preview drum holds the fixture's sample under.
inline constexpr std::string_view kSlicePath = "SAMPLES/SLICE.WAV";

struct Rig {
	spec::SongFixture song = spec::make_synth_song();
	Sample* sample = make_sample();
	/// The sample in the preview drum, which the slicer's host opens on; `open` puts it there.
	std::optional<spec::SliceHeld> held;

	const BoardProfile& profile;
	deluge::ui::shell::SceneRegistry reg = scene_registry();
	uitest::FakeSurfaceSet fake;
	Shell shell;

	Rig(const Rig&) = delete;
	Rig& operator=(const Rig&) = delete;

	/// @brief Tear the stack down, so `SlicerScene` -- a process singleton -- never carries state
	///        into the next example. `recorder_scene_spec.cpp`'s `~Rig`, for its reason.
	~Rig() {
		while (shell.stack().depth() > 0) {
			shell.stack().pop(SceneKind::Null);
		}
		Buttons::clearShiftSticky();
		// The scene is a process singleton and `FakeOps` is an example-scoped local, so leaving it
		// wired would hand the next example a dangling boundary. Unwired is also the state
		// `SliceOps`'s docstring calls the honest default.
		slicer_scene().set_ops(nullptr);
		slicer_scene().set_ports(nullptr);
		held.reset();
		std::memset(matrixDriver.padStates, 0, sizeof(matrixDriver.padStates));
	}

	explicit Rig(const DelugeBoard& board = deluge::ui::shell::kDelugeBoard, const BoardProfile& p = deluge_profile())
	    : profile(p), fake(board, p), shell(fake, p, reg, deluge::ui::shell::Services{}, ShellConfig{}) {
		// The app's boot wires the host's ops; an example starts unwired and installs its own.
		slicer_scene().set_ops(nullptr);
		// The app's ports, as its boot installs them; an example that needs a fake copies them and replaces one.
		slicer_scene().set_ports(&deluge::ui::scenes::menu::host_ports());
	}

	/// @brief Put the fixture's sample in the preview drum and open the slicer's host on it, as the browser's SLICE
	///        door does, without pushing the scene.
	void open_host() {
		if (!held) {
			held.emplace(sample, kSlicePath);
		}
		held->open();
	}

	/// @brief Open the slicer on the fixture's sample, the way `SampleBrowser` does: the host first, then the push.
	void open() {
		open_host();
		shell.init(SceneId{kSlicerSceneKind, 0});
	}

	void tick(uint32_t at_ms) { shell.tick(at_ms); }

	/// @brief One tick to deliver the turn, one to draw its consequence.
	///
	/// `Shell::tick` delivers encoder events at **step 8**, after step 5/6 have already built this
	/// tick's frames (`Shell::tick`), and the `Repaint` the arm files is therefore for
	/// the tick after. So a pixel comparison taken one tick after a turn shows
	/// the value from *before* it -- found by writing exactly that and watching the panel read 17
	/// where main's read 20. Every gesture therefore settles before anything is read.
	void settle(uint32_t at_ms) {
		shell.tick(at_ms);
		shell.tick(at_ms + 1);
	}

	/// @brief One detent of the (detented, 2 edges per click) select encoder, index 3
	///        (`ui/shell/profiles/deluge.cpp`).
	///
	/// @warning **At most 63 detents per call.** `InputLowering::apply_encoder_edges` takes its
	///          edge count as an `int8_t` (`InputLowering::apply_encoder_edges`), so 64 detents is 128
	///          edges and wraps negative -- 120 detents arrived as -16 and turned a count of 136
	///          into 0, which is how this was found. Main's `Slicer` has no such limit
	///          (`selectEncoderAction(int8_t offset)` is handed whole detents), so every multi-turn
	///          gesture is split into <= 63-detent turns, and main's turns were split the same way.
	void turn_select(int32_t detents, uint32_t at_ms) {
		shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_ENCODER, 3, 0, static_cast<int16_t>(detents * 2)}, at_ms);
	}
	/// @brief One detent of the horizontal encoder, index 1.
	void turn_horizontal(int32_t detents, uint32_t at_ms) {
		shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_ENCODER, 1, 0, static_cast<int16_t>(detents * 2)}, at_ms);
	}
	/// @brief One detent of the vertical encoder, index 0.
	void turn_vertical(int32_t detents, uint32_t at_ms) {
		shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_ENCODER, 0, 0, static_cast<int16_t>(detents * 2)}, at_ms);
	}
	/// @brief Press or release SHIFT, at matrix (8, 0) (`ui/shell/profiles/deluge.cpp`).
	void shift(bool on, uint32_t at_ms) {
		shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_BUTTON, 8, 0, static_cast<int16_t>(on ? 255 : 0)}, at_ms);
	}

	/// @brief Press or release one button, by matrix coordinates (`ui/shell/profiles/deluge.cpp`).
	void button(uint8_t mx, uint8_t my, bool on, uint32_t at_ms) {
		shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_BUTTON, mx, my, static_cast<int16_t>(on ? 255 : 0)}, at_ms);
	}
	/// @name The five buttons `Slicer::buttonAction` has arms for
	/// @{
	void x_enc(bool on, uint32_t at_ms) { button(0, 1, on, at_ms); }
	void y_enc(bool on, uint32_t at_ms) { button(0, 0, on, at_ms); }
	void select_enc(bool on, uint32_t at_ms) { button(4, 3, on, at_ms); }
	void save(bool on, uint32_t at_ms) { button(6, 3, on, at_ms); }
	void back(bool on, uint32_t at_ms) { button(7, 1, on, at_ms); }
	/// @}

	/// @brief Press or release a pad, absolute coordinates; velocity 0 is a release.
	void pad(int32_t x, int32_t y, int32_t velocity, uint32_t at_ms) {
		shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_PAD, static_cast<uint8_t>(x), static_cast<uint8_t>(y),
		                                      static_cast<int16_t>(velocity)},
		                     at_ms);
	}

	/// @return Is `SlicerScene` still on the stack?
	bool on_stack() {
		for (const deluge::ui::shell::Scene* s : shell.stack().scenes()) {
			if (s->kind() == kSlicerSceneKind) {
				return true;
			}
		}
		return false;
	}

	/// @return The text the shell's popup service last showed.
	///
	/// @warning This is a *record*, not a level -- `Notices::last_popup_text()` is "the text last
	///          shown" and survives the popup's removal (`ui/shell/notices.h`). Ask
	///          `popup_showing()` whether one is on screen.
	std::string popup() { return shell.notices().last_popup_text(); }

	/// @return Is a popup scene on the stack right now? `Notices` pushes `SceneKind::Popup` for one
	///         and `SceneStack::remove`s it on cancel (`Notices::drain`'s `CancelPopup` case)
	///         or on timeout (`Notices::tick`).
	bool popup_showing() {
		for (const deluge::ui::shell::Scene* s : shell.stack().scenes()) {
			if (s->kind() == SceneKind::Popup) {
				return true;
			}
		}
		return false;
	}

	std::string oled() { return fake.pbm(0); }
	std::string grid() { return fake.state(); }
};

// --- the audio/model/card boundary, faked ------------------------------------------------------

using deluge::ui::scenes::slicing::test::FakeOps;

// --- the shared slice table ---------------------------------------------------------------------

/// @brief Seed the port's manual slice table through its own front door, its ported adder
///        (`SlicerScene::add_slice_at`), with the play positions handed in.
///
/// Main's adder is behind the voice walk no spec can drive, so main's table for the same taps is its
/// own public members assigned the sorted positions; `slicer_scene_spec`'s `expect_tables_match` makes
/// the seeding an assertion.
///
/// @param positions Start positions to place, in the order a finger would tap them -- deliberately
///                  NOT sorted, so the port's insertion sort has to do something.
inline void seed_port(const std::vector<int32_t>& positions) {
	for (const int32_t p : positions) {
		(void)slicer_scene().add_slice_at(p);
	}
}

} // namespace deluge::ui::scenes::spec::slicer_fx
