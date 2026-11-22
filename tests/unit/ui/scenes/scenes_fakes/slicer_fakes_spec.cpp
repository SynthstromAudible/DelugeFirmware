/// @file
/// @brief The slicer (`slicing/slicer_scene.cpp`) over the ports' fakes: what it reads with no ports installed, the
///        undo wipe on entry, the play cursor, the redraws of a read with pending columns, and when a reopen keeps
///        its view.

#include "ui/scenes/slicing/slicer_scene.h"

#include "project/device_settings.h"
#include "ui/scenes/browser/browser_kind.h"
#include "ui/scenes/services/peak_retry.h"
#include "ui/shell/profiles/deluge.h"
#include "ui/shell/scene.h"
#include "ui/views/waveform/view.h"

#include "fake_slice_ops.h"
#include "fakes.h"
#include "unit/ui/widgets/support/frames.h"

#include "cppspec.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "support/spec_main.h"

using deluge::project::device_path;
using deluge::project::FlashCursor;
using deluge::ui::scenes::bind::Ports;
using deluge::ui::scenes::bind::WaveformAddress;
using deluge::ui::scenes::bind::test::FakeHost;
using deluge::ui::scenes::bind::test::FakeWaveforms;
using deluge::ui::scenes::bind::test::RecordingDeviceActions;
using deluge::ui::scenes::slicing::SliceFile;
using deluge::ui::scenes::slicing::SlicerScene;
using deluge::ui::scenes::slicing::test::FakeOps;
using deluge::ui::shell::Action;
using deluge::ui::shell::Result;
using deluge::ui::shell::SceneId;
using deluge::ui::shell::SceneKind;
using deluge::ui::shell::UiContext;
using deluge::ui::shell::UiEvent;
using deluge::ui::test::GridRig;

namespace {

/// The fake's file: 16000 frames, so the zoomed-out view is 1000 frames a column.
constexpr uint64_t kLength = 16000;
/// The fake file's identity.
constexpr uint64_t kIdentity = 7;
/// The mute column, the first sidebar column.
constexpr uint8_t kMuteColumn = 16;

/// @brief The file the cases open on.
SliceFile a_wav() {
	return SliceFile{.identity = kIdentity, .path = "A.WAV"};
}

/// @brief A fake host whose waveforms port holds the file at SLICE, every column a peak.
struct SlicerHost {
	FakeHost host;
	Ports ports = host.ports();
	SlicerHost() {
		host.waveforms.waveforms.push_back(FakeWaveforms::Entry{
		    .at = {.kind = WaveformAddress::Kind::SLICE},
		    .info = {.length_samples = kLength, .sample_rate = 44100, .channels = 1, .identity = kIdentity},
		});
	}
	SlicerHost(const SlicerHost&) = delete;
	SlicerHost& operator=(const SlicerHost&) = delete;
	/// @return The SLICE entry.
	FakeWaveforms::Entry& entry() { return host.waveforms.waveforms.front(); }
	/// @brief Store @p cursor in the device's play cursor row.
	void set_cursor(FlashCursor cursor) {
		(void)host.device.write(device_path::kPlayCursor, std::to_underlying(cursor));
	}
};

/// @brief Press the pad at (@p x, @p y) on @p scene, as the shell hands it a pad.
Result press(SlicerScene& scene, uint8_t x, uint8_t y) {
	const deluge::ui::shell::ButtonSet held{};
	const deluge::ui::widgets::core::PadMask pads{};
	const UiContext ctx{held, pads, false, false, 0, deluge::ui::shell::deluge_profile()};
	return scene.handle(UiEvent{.kind = UiEvent::Kind::Pad, .x = x, .y = y, .velocity = 64, .on = true}, ctx);
}

/// @brief Press the button @p b on @p scene, as the shell hands it a press.
Result press_button(SlicerScene& scene, deluge::ui::shell::ButtonId b) {
	const deluge::ui::shell::ButtonSet held{};
	const deluge::ui::widgets::core::PadMask pads{};
	const UiContext ctx{held, pads, false, false, 0, deluge::ui::shell::deluge_profile()};
	return scene.handle(UiEvent{.kind = UiEvent::Kind::Button, .on = true, .button = b}, ctx);
}

/// @brief Whether @p r removes the scene kind @p kind.
bool removes(const Result& r, SceneKind kind) {
	return std::ranges::any_of(r.actions,
	                           [&](const Action& a) { return a.kind == Action::Kind::Remove && a.pop_to == kind; });
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

/// @brief Render one grid frame of @p scene at @p now_ms into a fresh rig.
/// @return The rig, holding the frame's pads, flashes and repaint deadline.
GridRig& render(GridRig& rig, SlicerScene& scene, uint32_t now_ms) {
	rig.begin_tick(now_ms);
	scene.render_grid(rig.frame);
	return rig;
}

} // namespace

// clang-format off
describe slicer_fakes("slicing::SlicerScene over the ports' fakes", ${
	it("exits on a mute column press with no ports installed, and stays while an audition is held", _ {
		SlicerScene scene;
		scene.open(a_wav());
		const Result bare = press(scene, kMuteColumn, 3);
		expect(removes(bare, deluge::ui::scenes::slicing::kSlicerSceneKind)).to_be_true();
		expect(removes(bare, deluge::ui::scenes::browser::kBrowserSceneKind)).to_be_true();

		SlicerHost h;
		h.host.ui.auditioning_now = true;
		scene.set_ports(&h.ports);
		const Result held = press(scene, kMuteColumn, 3);
		expect(held.handled).to_be_true();
		expect(held.actions.empty()).to_be_true();
	});

	it("wipes the undo log as it is entered", _ {
		SlicerHost h;
		SlicerScene scene;
		scene.set_ports(&h.ports);
		scene.open(a_wav());
		expect(scene.on_enter(SceneId{deluge::ui::scenes::slicing::kSlicerSceneKind, 0})).to_be_true();
		expect(h.host.device_actions.calls)
		    .to_equal(std::vector<RecordingDeviceActions::Kind>{RecordingDeviceActions::Kind::DELETE_ALL_LOGS});
	});

	it("draws no playhead with the play cursor OFF, and one where the ops report a position SLOW and FAST", _ {
		SlicerHost h;
		SlicerScene scene;
		FakeOps ops;
		scene.set_ports(&h.ports);
		scene.set_ops(&ops);
		scene.open(a_wav());
		// Column 5 of the zoomed-out view, 1000 frames a column, on rows 4 to 7.
		GridRig none;
		h.set_cursor(FlashCursor::OFF);
		ops.next_play_position.reset();
		const RGB unplayed = render(none, scene, 0).grid.at(5, 7);

		GridRig off;
		ops.next_play_position = 5500;
		render(off, scene, 10);
		expect(off.grid.at(5, 7)).to_equal(unplayed);
		expect(int32_t{off.flashes.count}).to_equal(0);

		GridRig slow;
		h.set_cursor(FlashCursor::SLOW);
		render(slow, scene, 20);
		expect(slow.grid.at(5, 7)).not_().to_equal(unplayed);
		expect(int32_t{slow.flashes.count}).to_equal(0);

		// FAST flashes the column where the row's square moved to; no pixel of the image changes.
		GridRig fast;
		h.set_cursor(FlashCursor::FAST);
		ops.next_play_position = 6500;
		render(fast, scene, 30);
		expect(fast.grid.at(6, 7)).to_equal(none.grid.at(6, 7));
		expect(int32_t{fast.flashes.count}).to_equal(4);
		expect(int32_t{fast.flashes.items[0].x}).to_equal(6);
	});

	it("Q28: paints no main pad while a column is pending and asks again, then paints and asks no more", _ {
		SlicerHost h;
		h.entry().pending.set(5);
		h.entry().pending_reads = 1;
		SlicerScene scene;
		scene.set_ports(&h.ports);
		scene.open(a_wav());

		GridRig first;
		render(first, scene, 0);
		expect(lit_main_pads(first)).to_equal(0);
		expect(first.gate.next_deadline()).to_equal(std::optional<uint32_t>{deluge::ui::scenes::services::kPeakRetryPollMs});

		GridRig second;
		render(second, scene, deluge::ui::scenes::services::kPeakRetryPollMs);
		expect(lit_main_pads(second)).to_be_greater_than(0);
		expect(second.gate.next_deadline().has_value()).to_be_false();
	});

	it("asks again while columns stay pending, until 300 ms pass with none newly answered, then stops", _ {
		SlicerHost h;
		h.entry().pending.set(5);
		h.entry().pending_reads = 1000;
		SlicerScene scene;
		scene.set_ports(&h.ports);
		scene.open(a_wav());
		for (uint32_t t = 0; t < deluge::ui::scenes::services::kPeakRetryGiveUpMs; t += deluge::ui::scenes::services::kPeakRetryPollMs) {
			GridRig rig;
			render(rig, scene, t);
			expect(rig.gate.next_deadline().has_value()).to_be_true();
		}
		GridRig given_up;
		render(given_up, scene, deluge::ui::scenes::services::kPeakRetryGiveUpMs);
		expect(given_up.gate.next_deadline().has_value()).to_be_false();
		GridRig waiting;
		render(waiting, scene, deluge::ui::scenes::services::kPeakRetryGiveUpMs + deluge::ui::scenes::services::kPeakRetryPollMs);
		expect(waiting.gate.next_deadline().has_value()).to_be_false();
	});

	it("keeps its view when reopened on the same identity and path, and opens afresh on another path", _ {
		SlicerHost h;
		SlicerScene scene;
		scene.set_ports(&h.ports);
		const deluge::ui::views::waveform::WaveformView zoomed =
		    deluge::ui::views::waveform::WaveformView::open(kLength, deluge::ui::views::waveform::Window{.scroll = 2000, .zoom = 100});
		scene.open(a_wav(), deluge::ui::views::waveform::ShownWaveform{.identity = kIdentity, .view = zoomed});
		expect(scene.x_zoom()).to_equal(int64_t{100});
		scene.open(a_wav());
		expect(scene.x_zoom()).to_equal(int64_t{100});
		expect(scene.x_scroll()).to_equal(int64_t{2000});
		scene.open(SliceFile{.identity = kIdentity, .path = "B.WAV"});
		expect(scene.x_zoom()).to_equal(int64_t{1000});
		expect(scene.x_scroll()).to_equal(int64_t{0});
	});

	it("opens on a shown view of the same file at that view", _ {
		SlicerHost h;
		SlicerScene scene;
		scene.set_ports(&h.ports);
		scene.open(a_wav());
		expect(scene.x_zoom()).to_equal(int64_t{1000});
		const deluge::ui::views::waveform::WaveformView shown =
		    deluge::ui::views::waveform::WaveformView::open(kLength, deluge::ui::views::waveform::Window{.scroll = 3000, .zoom = 200});
		scene.open(a_wav(), deluge::ui::views::waveform::ShownWaveform{.identity = kIdentity, .view = shown});
		expect(scene.x_zoom()).to_equal(int64_t{200});
		expect(scene.x_scroll()).to_equal(int64_t{3000});
	});

	it("reads nothing auditioned, draws no waveform, wipes nothing and plays SLOW with no ports installed", _ {
		SlicerScene scene;
		FakeOps ops;
		scene.set_ops(&ops);
		scene.open(a_wav());
		expect(scene.on_enter(SceneId{deluge::ui::scenes::slicing::kSlicerSceneKind, 0})).to_be_true();
		GridRig bare;
		render(bare, scene, 0);
		expect(lit_main_pads(bare)).to_equal(0);
		// With no waveform the view is one frame a column: the playhead's column is its position.
		ops.next_play_position = 5;
		GridRig played;
		render(played, scene, 10);
		expect(played.grid.at(5, 7)).not_().to_equal(RGB{});
		expect(int32_t{played.flashes.count}).to_equal(0);
	});

	it("tells the host it closed on BACK, on the mute column's exit and on a landed slice, and keeps its file", _ {
		SlicerScene scene;
		FakeOps ops;
		scene.set_ops(&ops);
		scene.open(a_wav());
		const Result back = press_button(scene, deluge::ui::shell::ButtonId::Back);
		expect(removes(back, deluge::ui::scenes::slicing::kSlicerSceneKind)).to_be_true();
		expect(ops.closed_calls).to_equal(1);
		expect(scene.has_sample()).to_be_true();

		const Result exit = press(scene, kMuteColumn, 3);
		expect(removes(exit, deluge::ui::scenes::browser::kBrowserSceneKind)).to_be_true();
		expect(ops.closed_calls).to_equal(2);
		expect(scene.has_sample()).to_be_true();

		(void)press_button(scene, deluge::ui::shell::ButtonId::SelectEnc);
		expect(scene.committing()).to_be_true();
		expect(ops.closed_calls).to_equal(2);
		scene.commit_done(true);
		const Result landed = scene.after_render();
		expect(removes(landed, deluge::ui::scenes::slicing::kSlicerSceneKind)).to_be_true();
		expect(ops.closed_calls).to_equal(3);
		expect(scene.has_sample()).to_be_true();
	});

	it("asks again for columns an earlier showing gave up on, once reopened on the same file", _ {
		SlicerHost h;
		h.entry().pending.set(5);
		h.entry().pending_reads = 1000;
		SlicerScene scene;
		scene.set_ports(&h.ports);
		scene.open(a_wav());
		for (uint32_t t = 0; t <= deluge::ui::scenes::services::kPeakRetryGiveUpMs; t += deluge::ui::scenes::services::kPeakRetryPollMs) {
			GridRig rig;
			render(rig, scene, t);
		}
		GridRig waiting;
		render(waiting, scene, 400);
		expect(waiting.gate.next_deadline().has_value()).to_be_false();
		scene.open(a_wav());
		GridRig reopened;
		render(reopened, scene, 410);
		expect(reopened.gate.next_deadline()).to_equal(std::optional<uint32_t>{410 + deluge::ui::scenes::services::kPeakRetryPollMs});
	});
});

DELUGE_SPEC(slicer_fakes_spec, slicer_fakes)
