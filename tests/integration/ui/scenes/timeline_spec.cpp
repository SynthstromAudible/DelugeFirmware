/// @file
/// @brief `timeline::TimelineState` -- the port of legacy's `TimelineView` ->
///        `ClipNavigationTimelineView` -> `ClipView` scroll/zoom arithmetic -- driven against
///        **those classes themselves**, through the one concrete subclass it targets
///        (`audioClipView`) and, for the triplet arms it disables, `instrumentClipView`.
///
/// @par Each side from its own sentinel
///
/// Every example declares a `Sentinel` -- scroll, zoom, triplets, clip length -- and seeds it into
/// the **`Song`** twice: once before the port runs (which reaches it through `TimelineState::adopt`)
/// and once again before legacy runs (which reads `currentSong->persistentView.xScroll[NAVIGATION_CLIP]` itself).
/// Neither side is ever handed the other's numbers. The limits legacy asks its own virtuals for
/// (`AudioClipView::getMaxZoom`/`getMaxLength`) reach the port as
/// `Clip::getMaxZoom()`/`Clip::getMaxLength()` (`model/clip/clip.cpp`) --
/// the same model question, asked of the model rather than of legacy's answer.
///
/// @par The one-detent ceiling, measured
///
/// Legacy's scroll and zoom each end in a `PadLEDs` transition the port does not carry
/// (`src/ui/scenes/timeline/timeline.h`). `TimelineView::initiateXScroll` sets `UI_MODE_HORIZONTAL_SCROLL`
/// and `TimelineView::initiateXZoom` sets `UI_MODE_HORIZONTAL_ZOOM`, and
/// nothing in a spec process runs either animation to the frame that clears them -- so the *next*
/// detent hits `TimelineView::horizontalEncoderAction`'s own `isNoUIModeActive()` or
/// `isUIModeActiveExclusively(UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON)` gate (`gui/views/timeline_view.cpp`) and does
/// nothing. The marker editor has the same ceiling
/// (`tests/integration/ui/scenes/marker_scene_spec.cpp`'s zoom example), but clearing the mode by hand did
/// **not** buy a second detent there.
///
/// **Here clearing it does buy the next detent, and that is measured, not argued.** The evidence is "scrolls a second
/// page when the transition mode is cleared": it asserts `UI_MODE_HORIZONTAL_SCROLL` set after the first detent, clears
/// it, takes a second, and lands on **page two** -- which is impossible if the second turn did nothing. The zoom
/// examples then use the same move to reach legacy's max-zoom *delay* (`TimelineView::horizontalEncoderAction`'s
/// `delayHorizontalZoomUntil` arm), which is otherwise unobservable because the mode gate would refuse first.
///
/// No claim is made here about **why** the marker editor shows the opposite. `WaveformBasicNavigator::zoom`'s tail
/// (`origin/main:src/deluge/gui/waveform/waveform_basic_navigator.cpp`) is the same three statements
/// `TimelineView::initiateXZoom` ends in and contains no wait of its own; the only structural
/// difference is the `waveformRenderer.renderFullScreen` call, which `initiateXZoom` has no
/// analogue for. A blocking transition is not the cause -- the conclusion stands on the scroll
/// example alone.
///
/// @par What this spec does not cover
///
///   * **`TimelineState::tick_square`'s auto-scroll arm.** `ClipView::getTickSquare` is protected
///     (`gui/views/clip_view.h`) and its follow arm is gated on `getCurrentlyRecordingLinearly()` and
///     `getCurrentUI() == this` -- a live recorder and a real root UI, neither of which this
///     fixture has. The pointer-to-member handle below reaches the *body*; the arms it can reach
///     from a non-recording clip are compared, the recording ones are named uncovered here.
///
/// @note The spec may include `gui/` and `hid/` (it drives the legacy views); the library may not.
///       Same rule `marker_scene_spec.cpp` runs under.

#include "gui/ui/shell_host/host_ports.h"
#include "ui/scenes/timeline/timeline.h"

#include "gui/ui/ui.h"
#include "gui/views/audio_clip_view.h"
#include "gui/views/instrument_clip_view.h"
#include "hid/button.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "model/clip/audio_clip.h"
#include "model/song/song.h"

#include "support/song_fixture.h"
#include "support/timeline_publish.h"

// Before <cppspec.hpp>: it #defines `it`, which collides with locals named `it` in the project
// headers this drags in. See the warning at the top of support/card_access.h.
#include "support/audio_clip_answers.h"
#include "support/card_access.h"

#include "unit/ui/gfx/support/pbm.h"

#include "cppspec.hpp"
#include "support/audio_clip_freezer.h"

#include <charconv>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <format>
#include <string>
#include <string_view>

#include "support/spec_main.h"

using deluge::ui::scenes::timeline::BarsBeats;
using deluge::ui::scenes::timeline::TimelineState;
using deluge::ui::scenes::timeline::ZoomOutcome;
namespace spec_fixture = deluge::ui::scenes::spec;

/// @brief `PlaybackHandler`'s own "the swung-tick answer is zero" latch
///        (`currentlyActioningSwungTickOrResettingPlayPos` in `playback/playback_handler.cpp`, read at
///        `PlaybackHandler::getNumSwungTicksInSinceLastActionedSwungTick`).
///
/// Declared here rather than included because it is a file-scope global with no header declaration -- the same shape
/// `menu/nodes/settings.cpp` uses for `jcong`. **Not a convenience.** `Clip::getLivePos()` (`model/clip/clip.cpp`) adds
/// `getNumSwungTicksInSinceLastActionedSwungTick()` to `lastProcessedPos`; on a song no transport ever started that
/// reaches `getActualSwungTickCount`'s external-clock swing branch and divides by
/// `(uint32_t)(50 + currentSong->project.groove.swing_amount)`, which is a **SIGFPE, not a failure** -- found by
/// running the tick-square examples without this. Holding the latch takes
/// `getNumSwungTicksInSinceLastActionedSwungTick`'s own early return, so `getLivePos()` is exactly `lastProcessedPos`,
/// which is what a stopped transport means.
extern bool currentlyActioningSwungTickOrResettingPlayPos;

namespace {

/// @brief Hold that latch for a scope.
struct StoppedTransport {
	bool saved = currentlyActioningSwungTickOrResettingPlayPos;
	StoppedTransport() { currentlyActioningSwungTickOrResettingPlayPos = true; }
	~StoppedTransport() { currentlyActioningSwungTickOrResettingPlayPos = saved; }
	StoppedTransport(const StoppedTransport&) = delete;
	StoppedTransport& operator=(const StoppedTransport&) = delete;
};

/// @brief The starting state both sides are seeded from, and neither derives from the other.
struct Sentinel {
	int32_t x_scroll = 0;
	uint32_t x_zoom = 6;
	bool triplets_on = false;
	uint32_t triplets_level = 0;
	/// `Clip::getMaxLength()` is `loopLength` (`model/clip/clip.cpp`), and
	/// `Clip::getMaxZoom()` is derived from it: 768 ticks gives a max zoom of 48, so
	/// the reachable zoom ladder is 3, 6, 12, 24, 48, 96, 192, 384, 768.
	int32_t loop_length = 768;
};

/// @brief `ClipView::getTickSquare` is protected (`gui/views/clip_view.h`).
///
/// A class derived from `ClipView` may name it, and `&TickProbe::getTickSquare` then has type
/// `int32_t (ClipView::*)()` -- a legal handle to legacy's own body, with no cast anywhere and no
/// second instance of anything. `TickProbe` is never constructed (it is abstract).
struct TickProbe : ClipView {
	static constexpr int32_t (ClipView::*kGetTickSquare)() = &TickProbe::getTickSquare;

	/// @brief `ClipNavigationTimelineView::xScrollBeforeFollowingAutoExtendingLinearRecording`
	///        (`clip_navigation_timeline_view.h`), which is protected and static.
	///
	/// Setting it to -1 is `ClipNavigationTimelineView::focusRegained`'s entire body
	/// (`clip_navigation_timeline_view.cpp`); the real `focusRegained` is not called because
	/// `AudioClipView`'s (`audio_clip_view.cpp`) also drags in `view.focusRegained()` and
	/// `setActiveModControllableTimelineCounter`, neither of which this comparison is about.
	/// @{
	static void set_remembered_scroll(int32_t value) { xScrollBeforeFollowingAutoExtendingLinearRecording = value; }
	static int32_t remembered_scroll() { return xScrollBeforeFollowingAutoExtendingLinearRecording; }
	/// @}
};

/// @brief `deluge_boot`'s own `display = new OLED` (`src/deluge/deluge.cpp`).
///
/// A spec process never calls `deluge_boot()` (see `support/song_fixture.h` for why), so the
/// `display` global is null (`hid/display/display.cpp`). Legacy's scroll arm ends in
/// `displayScrollPos()` -> `display->popupTextTemporary` **unconditionally**
/// (`TimelineView::displayScrollPos`, `TimelineView::initiateXScroll`), and its zoom arm in `displayZoomLevel()` ->
/// `displayPopup`
///. Without this the gesture examples below do not fail, they segfault -- found by
/// running them without it.
void ensure_display() {
	if (display == nullptr) {
		display = new deluge::hid::display::OLED;
	}
}

struct Rig {
	spec_fixture::AudioSongFixture f = spec_fixture::make_audio_clip_song();

	Rig() {
		ensure_display();
		currentUIMode = UI_MODE_NONE;
	}

	/// @brief Put @p s into the `Song`, which is what BOTH sides read from.
	///
	/// It also expires any max-zoom delay a *previous* example armed. That latch
	/// (`TimelineView::delayHorizontalZoomUntil`, `timeline_view.h`) is a member of the `audioClipView` **singleton**,
	/// and it is compared against `AudioEngine::audioSampleTimer` (`TimelineView::horizontalEncoderAction`), which a
	/// spec process never advances -- so once armed it stays armed for the rest of the process and silently refuses
	/// every later zoom in that direction. Not hypothetical: without this line the three "forgets the restore point"
	/// examples below leave legacy at zoom 12 while the port zooms to 6. Advancing the timer past `kShortPressTime` is
	/// **legacy's own expiry** (`TimelineView::horizontalEncoderAction` clears the latch), not a reach into private
	/// state, and it is safe here because every `PadLEDs::recordTransitionBegin` in this file happens *after* the last
	/// `seed()` of its example, so no transition is ever handed a jumped clock.
	void seed(const Sentinel& s) const {
		AudioEngine::audioSampleTimer += 2 * kShortPressTime;
		f.song->persistentView.xScroll[NAVIGATION_CLIP] = s.x_scroll;
		f.song->persistentView.xZoom[NAVIGATION_CLIP] = s.x_zoom;
		f.song->persistentView.tripletsOn = s.triplets_on;
		f.song->persistentView.tripletsLevel = s.triplets_level;
		f.clip->setLoopLength(s.loop_length);
		currentUIMode = UI_MODE_NONE;
	}

	/// @brief A port state adopted from the song, with `AudioClipView`'s own
	///        `supportsTriplets() == false` (`audio_clip_view.h`).
	[[nodiscard]] TimelineState port() const {
		TimelineState s;
		s.adopt(*deluge::gui::shell_host::host_ports().clips->clip_navigation()); // the fixture's song is current
		s.supports_triplets = false;
		return s;
	}

	/// @brief The same, but with triplets supported -- what `instrumentClipView` reports
	///        (`RootUI::supportsTriplets`, `gui/ui/root_ui.h`).
	[[nodiscard]] TimelineState port_with_triplets() const {
		TimelineState s;
		s.adopt(*deluge::gui::shell_host::host_ports().clips->clip_navigation()); // the fixture's song is current
		s.supports_triplets = true;
		return s;
	}

	[[nodiscard]] uint32_t max_zoom() const { return static_cast<uint32_t>(f.clip->getMaxZoom()); }
	[[nodiscard]] uint32_t max_length() const { return static_cast<uint32_t>(f.clip->getMaxLength()); }

	[[nodiscard]] int32_t song_scroll() const { return f.song->persistentView.xScroll[NAVIGATION_CLIP]; }
	[[nodiscard]] uint32_t song_zoom() const { return f.song->persistentView.xZoom[NAVIGATION_CLIP]; }

	/// @brief `TimelineView::buttonAction`'s X_ENC arm (`timeline_view.cpp`), which is how
	///        legacy is told the horizontal encoder's button is held.
	static void hold_x_enc(bool on) { audioClipView.buttonAction(deluge::hid::button::X_ENC, on); }

	/// @brief Clear the transition mode `initiateXZoom` left behind,
	///        keeping the held-button mode, so legacy will consider a second detent.
	static void clear_zoom_transition() { currentUIMode &= ~UI_MODE_HORIZONTAL_ZOOM; }
	/// @brief The same for `initiateXScroll`.
	static void clear_scroll_transition() { currentUIMode &= ~UI_MODE_HORIZONTAL_SCROLL; }
};

/// @brief `timeline_spec`'s table of legacy's answers (`support/audio_clip_answers.h`).
deluge::ui::scenes::spec::OracleTable& answers() {
	return deluge::ui::scenes::spec::audio_clip_answers("timeline_spec");
}

/// @brief The number the row (@p path, @p column) holds; `INT64_MIN` when it holds none.
int64_t tnum(std::string_view path, std::string_view column) {
	const std::string v = answers().value(path, column);
	int64_t n = 0;
	const auto [end, ec] = std::from_chars(v.data(), v.data() + v.size(), n);
	return ec == std::errc{} && end == v.data() + v.size() ? n : INT64_MIN;
}

/// @brief Freeze, or check, legacy's number @p v as (@p path, @p column).
template <typename Self>
void tkeep(Self& self, std::string_view path, std::string_view column, int64_t v) {
	deluge::ui::scenes::spec::keep(self, "timeline_spec", path, column, std::to_string(v));
}

/// @brief The 128x48 popup canvas, as a PBM string -- the surface `display->popupTextTemporary`
///        draws into (`OLED::popupText`, `hid/display/oled.cpp`).
std::string popup_pbm() {
	return page_major_to_pbm(deluge::hid::display::OLED::popup.bytes().data(), 128, 48);
}

} // namespace

// NOLINTBEGIN(readability-function-cognitive-complexity)
describe timeline_spec_body(
    "scenes/timeline TimelineState", $ {
	    // ---- the pure arithmetic, no triplets: `instrumentClipView` with triplets off, which is the same
	    // `TimelineView` arithmetic every clip view runs ------------------------------------------

	    it(
	        "agrees with TimelineView::getPosFromSquare across a page and beyond both edges", _ {
		        Rig r;
		        const Sentinel s{.x_scroll = 96, .x_zoom = 6};
		        r.seed(s);
		        const TimelineState port = r.port();

		        r.seed(s);
		        for (int32_t square = -4; square <= kDisplayWidth + 4; square++) {
			        expect(port.pos_from_square(square)).to_equal(instrumentClipView.getPosFromSquare(square));
		        }
		        // ...and it is really doing arithmetic, not returning the scroll.
		        expect(port.pos_from_square(1)).to_equal(102);
	        });

	    it(
	        "agrees with TimelineView::getSquareFromPos on both sides of the scroll, exactness included", _ {
		        // The rounding direction is the whole point: `getSquareFromPos` runs through
		        // `divide_round_negative` (`timeline_view.cpp`) precisely so a position LEFT of the
		        // scroll floors towards minus infinity instead of towards zero. Positions from three
		        // columns before the scroll to three past the right edge cover both branches.
		        Rig r;
		        const Sentinel s{.x_scroll = 96, .x_zoom = 6};
		        r.seed(s);
		        const TimelineState port = r.port();

		        r.seed(s);
		        for (int32_t pos = 96 - 3 * 6; pos <= 96 + (kDisplayWidth + 3) * 6; pos++) {
			        bool port_exact = false;
			        bool legacy_exact = false;
			        expect(port.square_from_pos(pos, &port_exact))
			            .to_equal(instrumentClipView.getSquareFromPos(pos, &legacy_exact));
			        expect(port_exact).to_equal(legacy_exact);
		        }
		        // The negative-side rounding, named: one tick left of the scroll is column -1, not 0.
		        expect(port.square_from_pos(95)).to_equal(-1);
		        expect(port.square_from_pos(96 - 6)).to_equal(-1);
		        expect(port.square_from_pos(96 - 7)).to_equal(-2);
	        });

	    it(
	        "agrees with TimelineView::getSquareEndFromPos", _ {
		        Rig r;
		        const Sentinel s{.x_scroll = 96, .x_zoom = 6};
		        r.seed(s);
		        const TimelineState port = r.port();

		        r.seed(s);
		        for (int32_t pos = 90; pos <= 96 + kDisplayWidth * 6; pos++) {
			        expect(port.square_end_from_pos(pos)).to_equal(instrumentClipView.getSquareEndFromPos(pos));
		        }
	        });

	    it(
	        "agrees with ClipView's chop and extend amounts", _ {
		        Rig r;
		        const Sentinel s{.x_scroll = 0, .x_zoom = 12};
		        r.seed(s);
		        const TimelineState port = r.port();

		        r.seed(s);
		        for (int32_t square = 0; square <= kDisplayWidth; square++) {
			        expect(port.length_chop_amount(square)).to_equal(instrumentClipView.getLengthChopAmount(square));
			        expect(port.length_extend_amount(square))
			            .to_equal(instrumentClipView.getLengthExtendAmount(square));
		        }
		        expect(port.length_chop_amount(4)).to_equal(12);
	        });

	    it(
	        "agrees with TimelineView::isSquareDefined with triplets off", _ {
		        Rig r;
		        const Sentinel s{.x_scroll = 0, .x_zoom = 6};
		        r.seed(s);
		        const TimelineState port = r.port();

		        r.seed(s);
		        for (int32_t square = 0; square <= kDisplayWidth; square++) {
			        expect(port.square_defined(square)).to_equal(instrumentClipView.isSquareDefined(square));
		        }
	        });

	    // ---- the pure arithmetic, triplets on (instrumentClipView) --------------------------------

	    it(
	        "agrees with TimelineView::getPosFromSquare in triplets view, both zoom branches", _ {
		        // `getPosFromSquare` has three arms (`timeline_view.cpp`): zoom below the triplets
		        // level, zoom below twice it, and everything else. `instrumentClipView` is used rather than
		        // `audioClipView` because `AudioClipView::supportsTriplets()` is false
		        // (`audio_clip_view.h`) and `inTripletsView()` would be false on every arm.
		        Rig r;
		        for (uint32_t zoom : {3U, 6U, 12U, 24U}) {
			        const Sentinel s{.x_scroll = 144, .x_zoom = zoom, .triplets_on = true, .triplets_level = 12};
			        r.seed(s);
			        const TimelineState port = r.port_with_triplets();

			        r.seed(s);
			        for (int32_t square = -2; square <= kDisplayWidth + 2; square++) {
				        expect(port.pos_from_square(square)).to_equal(instrumentClipView.getPosFromSquare(square));
			        }
		        }
	        });

	    it(
	        "agrees with TimelineView::getSquareFromPos in triplets view, exactness included", _ {
		        Rig r;
		        for (uint32_t zoom : {3U, 6U, 12U, 24U}) {
			        const Sentinel s{.x_scroll = 144, .x_zoom = zoom, .triplets_on = true, .triplets_level = 12};
			        r.seed(s);
			        const TimelineState port = r.port_with_triplets();

			        r.seed(s);
			        for (int32_t pos = 144 - 24; pos <= 144 + 24 * 8; pos++) {
				        bool port_exact = false;
				        bool legacy_exact = false;
				        expect(port.square_from_pos(pos, &port_exact))
				            .to_equal(instrumentClipView.getSquareFromPos(pos, &legacy_exact));
				        expect(port_exact).to_equal(legacy_exact);
			        }
		        }
	        });

	    it(
	        "agrees with TimelineView::isSquareDefined in triplets view, undefined columns and all", _ {
		        Rig r;
		        const Sentinel s{.x_scroll = 144, .x_zoom = 3, .triplets_on = true, .triplets_level = 12};
		        r.seed(s);
		        const TimelineState port = r.port_with_triplets();

		        r.seed(s);
		        int32_t undefined = 0;
		        for (int32_t square = 0; square <= kDisplayWidth; square++) {
			        const bool legacy = instrumentClipView.isSquareDefined(square);
			        expect(port.square_defined(square)).to_equal(legacy);
			        if (!legacy) {
				        undefined++;
			        }
		        }
		        // The arm exists for the columns that represent no time at all; if none of them showed up
		        // this example would be comparing "true" against "true" sixteen times.
		        expect(undefined).to_be_greater_than(0);
	        });

	    it(
	        "agrees with ClipView's chop and extend amounts in triplets view", _ {
		        Rig r;
		        for (uint32_t zoom : {3U, 6U, 12U, 24U}) {
			        const Sentinel s{.x_scroll = 144, .x_zoom = zoom, .triplets_on = true, .triplets_level = 12};
			        r.seed(s);
			        const TimelineState port = r.port_with_triplets();

			        r.seed(s);
			        for (int32_t square = 1; square <= kDisplayWidth; square++) {
				        expect(port.length_chop_amount(square))
				            .to_equal(instrumentClipView.getLengthChopAmount(square));
				        expect(port.length_extend_amount(square))
				            .to_equal(instrumentClipView.getLengthExtendAmount(square));
			        }
		        }
	        });

	    it(
	        "toggles triplets to the same level TimelineView::tripletsButtonPressed does", _ {
		        Rig r;
		        const Sentinel s{.x_scroll = 0, .x_zoom = 24};
		        r.seed(s);
		        TimelineState port = r.port_with_triplets();
		        port.toggle_triplets();

		        r.seed(s);
		        instrumentClipView.tripletsButtonPressed();
		        expect(port.triplets_on).to_equal(r.f.song->persistentView.tripletsOn);
		        expect(port.triplets_level).to_equal(r.f.song->persistentView.tripletsLevel);
		        expect(port.triplets_level).to_equal(32U);

		        port.toggle_triplets();
		        instrumentClipView.tripletsButtonPressed();
		        expect(port.triplets_on).to_equal(r.f.song->persistentView.tripletsOn);
		        expect(port.triplets_on).to_be_false();
		        // Legacy leaves the level behind when switching off (`TimelineView::tripletsButtonPressed` in
		        // `timeline_view.cpp`).
		        expect(port.triplets_level).to_equal(r.f.song->persistentView.tripletsLevel);
	        });

	    // ---- the scroll gesture --------------------------------------------------------------------

	    it(
	        "scrolls one page right exactly where ClipView::horizontalEncoderAction lands", _ {
		        spec_fixture::QuietAudio quiet;
		        Rig r;
		        const Sentinel s{.x_scroll = 0, .x_zoom = 6};
		        r.seed(s);
		        TimelineState port = r.port();
		        const bool port_moved = port.scroll(1, r.max_length());

		        r.seed(s);
		        LEGACY_ONLY(audioClipView.horizontalEncoderAction(1);
		                    tkeep(self, "scroll_page", "scroll", r.song_scroll()););
		        expect(int64_t{port.x_scroll}).to_equal(tnum("scroll_page", "scroll"));
		        expect(port_moved).to_be_true();
		        expect(port.x_scroll).to_equal(6 * kDisplayWidth);
		        currentUIMode = UI_MODE_NONE;
	        });

	    it(
	        "clamps a left scroll at zero exactly as the left guard does", _ {
		        // `TimelineView::horizontalEncoderAction` (`timeline_view.cpp`). From half a page in, one detent left
		        // overshoots zero.
		        spec_fixture::QuietAudio quiet;
		        Rig r;
		        const Sentinel s{.x_scroll = 48, .x_zoom = 6};
		        r.seed(s);
		        TimelineState port = r.port();
		        const bool port_moved = port.scroll(-1, r.max_length());

		        r.seed(s);
		        LEGACY_ONLY(audioClipView.horizontalEncoderAction(-1);
		                    tkeep(self, "scroll_clamp", "scroll", r.song_scroll()););
		        expect(int64_t{port.x_scroll}).to_equal(tnum("scroll_clamp", "scroll"));
		        expect(port_moved).to_be_true();
		        expect(port.x_scroll).to_equal(0);
		        currentUIMode = UI_MODE_NONE;
	        });

	    it(
	        "refuses a right scroll that would leave the clip, exactly as the right guard does", _ {
		        // `TimelineView::horizontalEncoderAction`: a rightward scroll is only taken when the new scroll is
		        // still inside `getMaxLength()`. At zoom 48 one page IS the 768-tick clip, so one detent right lands
		        // exactly on the length and must be refused.
		        spec_fixture::QuietAudio quiet;
		        Rig r;
		        const Sentinel s{.x_scroll = 0, .x_zoom = 48};
		        r.seed(s);
		        TimelineState port = r.port();
		        const bool port_moved = port.scroll(1, r.max_length());

		        r.seed(s);
		        LEGACY_ONLY(audioClipView.horizontalEncoderAction(1);
		                    tkeep(self, "scroll_refused", "scroll", r.song_scroll()););
		        expect(int64_t{port.x_scroll}).to_equal(tnum("scroll_refused", "scroll"));
		        expect(port_moved).to_be_false();
		        expect(port.x_scroll).to_equal(0);
		        currentUIMode = UI_MODE_NONE;
	        });

	    it(
	        "scrolls a second page when the transition mode is cleared, as legacy does", _ {
		        // The scroll half of the one-detent ceiling: `initiateXScroll` sets `UI_MODE_HORIZONTAL_SCROLL` and the
		        // next detent then fails `TimelineView::horizontalEncoderAction`'s `isNoUIModeActive()` test. Unlike
		        // the marker-editor zoom, clearing the mode really does buy the next detent here -- measured by this
		        // example, which would land on page one rather than page two if it did not.
		        spec_fixture::QuietAudio quiet;
		        Rig r;
		        const Sentinel s{.x_scroll = 0, .x_zoom = 6};
		        r.seed(s);
		        TimelineState port = r.port();
		        (void)port.scroll(1, r.max_length());
		        (void)port.scroll(1, r.max_length());

		        r.seed(s);
		        LEGACY_ONLY(
		            audioClipView.horizontalEncoderAction(1);
		            tkeep(self, "scroll_second_page", "scroll_mode", (currentUIMode & UI_MODE_HORIZONTAL_SCROLL) != 0);
		            Rig::clear_scroll_transition(); audioClipView.horizontalEncoderAction(1);
		            tkeep(self, "scroll_second_page", "scroll", r.song_scroll()););
		        expect(tnum("scroll_second_page", "scroll_mode")).to_equal(int64_t{1});
		        expect(int64_t{port.x_scroll}).to_equal(tnum("scroll_second_page", "scroll"));
		        expect(port.x_scroll).to_equal(2 * 6 * kDisplayWidth);
		        currentUIMode = UI_MODE_NONE;
	        });

	    // ---- the zoom gesture ----------------------------------------------------------------------

	    it(
	        "zooms in one detent to the same zoom and scroll legacy reaches", _ {
		        spec_fixture::QuietAudio quiet;
		        Rig r;
		        const Sentinel s{.x_scroll = 6 * kDisplayWidth, .x_zoom = 12};
		        r.seed(s);
		        TimelineState port = r.port();
		        const ZoomOutcome outcome = port.zoom(1, r.max_zoom(), 1000);

		        r.seed(s);
		        LEGACY_ONLY(Rig::hold_x_enc(true); audioClipView.horizontalEncoderAction(1);
		                    tkeep(self, "zoom_in", "scroll", r.song_scroll());
		                    tkeep(self, "zoom_in", "zoom", r.song_zoom()); Rig::hold_x_enc(false););
		        expect(int64_t{port.x_zoom}).to_equal(tnum("zoom_in", "zoom"));
		        expect(int64_t{port.x_scroll}).to_equal(tnum("zoom_in", "scroll"));
		        expect(outcome).to_equal(ZoomOutcome::Zoomed);
		        // ...and it really zoomed in, so a port that refused the turn could not pass this.
		        expect(static_cast<int32_t>(port.x_zoom)).to_equal(6);
		        currentUIMode = UI_MODE_NONE;
	        });

	    it(
	        "snaps the scroll to a whole page when it zooms out, exactly as legacy does", _ {
		        // `TimelineView::horizontalEncoderAction`: the new scroll is the old one floored to a multiple of
		        // `newZoom * kDisplayWidth`. From 192 at zoom 12, zooming OUT to 24 makes a page 384 ticks
		        // wide, so the scroll must snap back to 0 rather than stay at 192.
		        spec_fixture::QuietAudio quiet;
		        Rig r;
		        const Sentinel s{.x_scroll = 192, .x_zoom = 12};
		        r.seed(s);
		        TimelineState port = r.port();
		        const ZoomOutcome outcome = port.zoom(-1, r.max_zoom(), 1000);

		        r.seed(s);
		        LEGACY_ONLY(Rig::hold_x_enc(true); audioClipView.horizontalEncoderAction(-1);
		                    tkeep(self, "zoom_out_snap", "scroll", r.song_scroll());
		                    tkeep(self, "zoom_out_snap", "zoom", r.song_zoom()); Rig::hold_x_enc(false););
		        expect(int64_t{port.x_zoom}).to_equal(tnum("zoom_out_snap", "zoom"));
		        expect(int64_t{port.x_scroll}).to_equal(tnum("zoom_out_snap", "scroll"));
		        expect(outcome).to_equal(ZoomOutcome::Zoomed);
		        expect(static_cast<int32_t>(port.x_zoom)).to_equal(24);
		        expect(port.x_scroll).to_equal(0);
		        currentUIMode = UI_MODE_NONE;
	        });

	    it(
	        "refuses to zoom in past the floor, exactly as the zoom limit does", _ {
		        // `TimelineView::horizontalEncoderAction` (`timeline_view.cpp`):
		        // `zoomMagnitude == -1 && oldXZoom <= 3`. Three is the floor of `Clip::getMaxZoom()`'s ladder
		        // (`model/clip/clip.cpp`), so this is the real edge.
		        spec_fixture::QuietAudio quiet;
		        Rig r;
		        const Sentinel s{.x_scroll = 0, .x_zoom = 3};
		        r.seed(s);
		        TimelineState port = r.port();
		        const ZoomOutcome outcome = port.zoom(1, r.max_zoom(), 1000);

		        r.seed(s);
		        LEGACY_ONLY(Rig::hold_x_enc(true); audioClipView.horizontalEncoderAction(1);
		                    tkeep(self, "zoom_floor", "scroll", r.song_scroll());
		                    tkeep(self, "zoom_floor", "zoom", r.song_zoom()); Rig::hold_x_enc(false););
		        expect(int64_t{port.x_zoom}).to_equal(tnum("zoom_floor", "zoom"));
		        expect(int64_t{port.x_scroll}).to_equal(tnum("zoom_floor", "scroll"));
		        expect(outcome).to_equal(ZoomOutcome::Refused);
		        expect(static_cast<int32_t>(port.x_zoom)).to_equal(3);
		        currentUIMode = UI_MODE_NONE;
	        });

	    it(
	        "refuses to zoom out past the ceiling, exactly as the zoom limit does", _ {
		        // The other half of `TimelineView::horizontalEncoderAction`'s zoom limits:
		        // `oldXZoom >= min(maxZoom * 16, kMaxZoom)`. With a 768-tick clip the max zoom is 48, so the ceiling is
		        // 768 -- and starting there, zooming out must do nothing.
		        spec_fixture::QuietAudio quiet;
		        Rig r;
		        const Sentinel s{.x_scroll = 0, .x_zoom = 768};
		        r.seed(s);
		        TimelineState port = r.port();
		        const ZoomOutcome outcome = port.zoom(-1, r.max_zoom(), 1000);

		        r.seed(s);
		        LEGACY_ONLY(Rig::hold_x_enc(true); audioClipView.horizontalEncoderAction(-1);
		                    tkeep(self, "zoom_ceiling", "zoom", r.song_zoom()); Rig::hold_x_enc(false););
		        expect(int64_t{port.x_zoom}).to_equal(tnum("zoom_ceiling", "zoom"));
		        expect(outcome).to_equal(ZoomOutcome::Refused);
		        expect(static_cast<int32_t>(port.x_zoom)).to_equal(768);
		        currentUIMode = UI_MODE_NONE;
	        });

	    it(
	        "holds the second detent back when a zoom lands on the clip's own max zoom", _ {
		        // `TimelineView::horizontalEncoderAction` (`timeline_view.cpp`): landing exactly on `getMaxZoom()` arms
		        // a `kShortPressTime` delay in that direction, and the next detent the same way is dropped. Legacy
		        // measures it against `AudioEngine::audioSampleTimer`, which does not advance in a spec process, so its
		        // delay is always "still running"; the port is driven at the same `now_ms` for both detents so it is
		        // too.
		        spec_fixture::QuietAudio quiet;
		        Rig r;
		        const Sentinel s{.x_scroll = 0, .x_zoom = 96};
		        r.seed(s);
		        TimelineState port = r.port();
		        const ZoomOutcome first = port.zoom(1, r.max_zoom(), 1000);
		        const ZoomOutcome second = port.zoom(1, r.max_zoom(), 1000);

		        r.seed(s);
		        LEGACY_ONLY(
		            Rig::hold_x_enc(true); audioClipView.horizontalEncoderAction(1);
		            tkeep(self, "zoom_delay", "zoom_mode_first", (currentUIMode & UI_MODE_HORIZONTAL_ZOOM) != 0);
		            Rig::clear_zoom_transition(); audioClipView.horizontalEncoderAction(1);
		            tkeep(self, "zoom_delay", "zoom_mode_second", (currentUIMode & UI_MODE_HORIZONTAL_ZOOM) != 0);
		            tkeep(self, "zoom_delay", "zoom", r.song_zoom()); Rig::hold_x_enc(false););
		        // Legacy ends at 48 whether the second detent was dropped by the delay latch,
		        // by the `isUIModeActiveExclusively` gate, or because `initiateXZoom` never
		        // armed the mode at all -- so the example pins WHICH. The first detent really did
		        // reach `initiateXZoom`...
		        expect(tnum("zoom_delay", "zoom_mode_first")).to_equal(int64_t{1});
		        // ...and with that cleared, the mode gate passes, so the only thing left that
		        // can stop the second detent is the delay latch. Its early `return` comes
		        // before `initiateXZoom`, so the mode must still be clear afterwards.
		        expect(tnum("zoom_delay", "zoom_mode_second")).to_equal(int64_t{0});
		        expect(int64_t{port.x_zoom}).to_equal(tnum("zoom_delay", "zoom"));
		        expect(first).to_equal(ZoomOutcome::Zoomed);
		        expect(second).to_equal(ZoomOutcome::Delayed);
		        // The first detent landed on 48, the clip's own max zoom; the second was dropped.
		        expect(static_cast<int32_t>(port.x_zoom)).to_equal(48);
		        currentUIMode = UI_MODE_NONE;
	        });

	    it(
	        "lets the delayed detent through once the delay has expired", _ {
		        // The other side of the same arm: outside the window the latch is cleared and
		        // the zoom proceeds. Only the port can be driven here -- legacy's window is measured in
		        // audio samples off a timer a spec process never advances, so its latch never expires. That
		        // is named as uncovered rather than papered over.
		        Rig r;
		        const Sentinel s{.x_scroll = 0, .x_zoom = 96};
		        r.seed(s);
		        TimelineState port = r.port();
		        expect(port.zoom(1, r.max_zoom(), 1000)).to_equal(ZoomOutcome::Zoomed);
		        expect(port.zoom(1, r.max_zoom(), 1000)).to_equal(ZoomOutcome::Delayed);
		        expect(port.zoom(1, r.max_zoom(), 1000 + 500)).to_equal(ZoomOutcome::Zoomed);
		        expect(static_cast<int32_t>(port.x_zoom)).to_equal(24);
	        });

	    it(
	        "lets a detent the OTHER way through while the delay is running", _ {
		        // The delay latch only drops the detent whose magnitude matches the armed one.
		        Rig r;
		        const Sentinel s{.x_scroll = 0, .x_zoom = 96};
		        r.seed(s);
		        TimelineState port = r.port();
		        expect(port.zoom(1, r.max_zoom(), 1000)).to_equal(ZoomOutcome::Zoomed);
		        expect(port.zoom(-1, r.max_zoom(), 1000)).to_equal(ZoomOutcome::Zoomed);
		        expect(static_cast<int32_t>(port.x_zoom)).to_equal(96);
	        });

	    // ---- the linear-recording restore point every horizontal turn clears ----------------------

	    it(
	        "forgets the linear-recording restore point on a manual scroll, as legacy does", _ {
		        // `ClipNavigationTimelineView::horizontalEncoderAction` is two statements
		        // (`clip_navigation_timeline_view.cpp`): clear `xScrollBeforeFollowingAutoExtendingLinearRecording`,
		        // then delegate (`TimelineView::horizontalEncoderAction`). A port that skipped the clear would keep a
		        // stale restore point and yank the scroll back when the recording stopped -- `getTickSquare`'s restore
		        // arm (`clip_view.cpp`) reads exactly this field.
		        spec_fixture::QuietAudio quiet;
		        Rig r;
		        const Sentinel s{.x_scroll = 0, .x_zoom = 6};

		        r.seed(s);
		        TimelineState port = r.port();
		        port.scroll_before_following = 480;
		        (void)port.scroll(1, r.max_length());

		        r.seed(s);
		        LEGACY_ONLY(TickProbe::set_remembered_scroll(480); audioClipView.horizontalEncoderAction(1);
		                    tkeep(self, "follow_scroll", "scroll", r.song_scroll());
		                    tkeep(self, "follow_scroll", "follow", TickProbe::remembered_scroll()););
		        expect(int64_t{port.scroll_before_following}).to_equal(tnum("follow_scroll", "follow"));
		        expect(port.scroll_before_following).to_equal(-1);
		        // ...and the scroll really did happen, so this is not passing because nothing ran.
		        expect(int64_t{port.x_scroll}).to_equal(tnum("follow_scroll", "scroll"));
		        expect(port.x_scroll).to_equal(6 * kDisplayWidth);
		        currentUIMode = UI_MODE_NONE;
	        });

	    it(
	        "forgets it on a manual zoom too", _ {
		        spec_fixture::QuietAudio quiet;
		        Rig r;
		        const Sentinel s{.x_scroll = 0, .x_zoom = 12};

		        r.seed(s);
		        TimelineState port = r.port();
		        port.scroll_before_following = 480;
		        (void)port.zoom(1, r.max_zoom(), 1000);

		        r.seed(s);
		        LEGACY_ONLY(TickProbe::set_remembered_scroll(480); Rig::hold_x_enc(true);
		                    audioClipView.horizontalEncoderAction(1); tkeep(self, "follow_zoom", "zoom", r.song_zoom());
		                    tkeep(self, "follow_zoom", "follow", TickProbe::remembered_scroll());
		                    Rig::hold_x_enc(false););
		        expect(int64_t{port.scroll_before_following}).to_equal(tnum("follow_zoom", "follow"));
		        expect(port.scroll_before_following).to_equal(-1);
		        expect(int64_t{port.x_zoom}).to_equal(tnum("follow_zoom", "zoom"));
		        expect(static_cast<int32_t>(port.x_zoom)).to_equal(6);
		        currentUIMode = UI_MODE_NONE;
	        });

	    it(
	        "forgets it even when the zoom limit then refuses the turn", _ {
		        // The clear is legacy's FIRST statement, above every gate `TimelineView` applies
		        // (`TimelineView::horizontalEncoderAction`), so a refused turn still
		        // wipes the restore point. At zoom 3 the floor refuses, and both sides must still have
		        // forgotten.
		        spec_fixture::QuietAudio quiet;
		        Rig r;
		        const Sentinel s{.x_scroll = 0, .x_zoom = 3};

		        r.seed(s);
		        TimelineState port = r.port();
		        port.scroll_before_following = 480;
		        const ZoomOutcome outcome = port.zoom(1, r.max_zoom(), 1000);

		        r.seed(s);
		        LEGACY_ONLY(
		            TickProbe::set_remembered_scroll(480); Rig::hold_x_enc(true);
		            audioClipView.horizontalEncoderAction(1); tkeep(self, "follow_refused", "zoom", r.song_zoom());
		            tkeep(self, "follow_refused", "follow", TickProbe::remembered_scroll()); Rig::hold_x_enc(false););
		        expect(outcome).to_equal(ZoomOutcome::Refused);
		        expect(int64_t{port.x_zoom}).to_equal(tnum("follow_refused", "zoom"));
		        expect(static_cast<int32_t>(port.x_zoom)).to_equal(3);
		        expect(int64_t{port.scroll_before_following}).to_equal(tnum("follow_refused", "follow"));
		        expect(port.scroll_before_following).to_equal(-1);
		        currentUIMode = UI_MODE_NONE;
	        });

	    // ---- the scroll helpers the length edits use -----------------------------------------------

	    it(
	        "scrolls right to the end of a length exactly as scrollRightToEndOfLengthIfNecessary does", _ {
		        spec_fixture::QuietAudio quiet;
		        Rig r;
		        const Sentinel s{.x_scroll = 0, .x_zoom = 6};
		        r.seed(s);
		        TimelineState port = r.port();
		        const bool port_moved = port.scroll_right_to_end_of_length_if_necessary(s.loop_length);

		        r.seed(s);
		        LEGACY_ONLY(tkeep(self, "scroll_right_to_end", "moved",
		                          audioClipView.scrollRightToEndOfLengthIfNecessary(s.loop_length));
		                    tkeep(self, "scroll_right_to_end", "scroll", r.song_scroll()););
		        const bool legacy_moved = tnum("scroll_right_to_end", "moved") != 0;
		        expect(port_moved).to_equal(legacy_moved);
		        expect(int64_t{port.x_scroll}).to_equal(tnum("scroll_right_to_end", "scroll"));
		        expect(port_moved).to_be_true();
		        // 768 ticks at 6 per column is eight pages; the last one starts at 672.
		        expect(port.x_scroll).to_equal(672);
		        currentUIMode = UI_MODE_NONE;
	        });

	    it(
	        "does not scroll right when the end is already on screen", _ {
		        spec_fixture::QuietAudio quiet;
		        Rig r;
		        const Sentinel s{.x_scroll = 672, .x_zoom = 6};
		        r.seed(s);
		        TimelineState port = r.port();
		        const bool port_moved = port.scroll_right_to_end_of_length_if_necessary(s.loop_length);

		        r.seed(s);
		        LEGACY_ONLY(tkeep(self, "scroll_right_on_screen", "moved",
		                          audioClipView.scrollRightToEndOfLengthIfNecessary(s.loop_length));
		                    tkeep(self, "scroll_right_on_screen", "scroll", r.song_scroll()););
		        const bool legacy_moved = tnum("scroll_right_on_screen", "moved") != 0;
		        expect(port_moved).to_equal(legacy_moved);
		        expect(port_moved).to_be_false();
		        expect(int64_t{port.x_scroll}).to_equal(tnum("scroll_right_on_screen", "scroll"));
		        currentUIMode = UI_MODE_NONE;
	        });

	    it(
	        "scrolls left when the view sits entirely past the length, as scrollLeftIfTooFarRight does", _ {
		        spec_fixture::QuietAudio quiet;
		        Rig r;
		        const Sentinel s{.x_scroll = 672, .x_zoom = 6};
		        r.seed(s);
		        TimelineState port = r.port();
		        // A clip shortened to 600 leaves the page starting at 672 entirely past the end.
		        const bool port_moved = port.scroll_left_if_too_far_right(600);

		        r.seed(s);
		        LEGACY_ONLY(tkeep(self, "scroll_left", "moved", audioClipView.scrollLeftIfTooFarRight(600));
		                    tkeep(self, "scroll_left", "scroll", r.song_scroll()););
		        const bool legacy_moved = tnum("scroll_left", "moved") != 0;
		        expect(port_moved).to_equal(legacy_moved);
		        expect(port_moved).to_be_true();
		        expect(int64_t{port.x_scroll}).to_equal(tnum("scroll_left", "scroll"));
		        expect(port.x_scroll).to_equal(672 - 6 * kDisplayWidth);
		        currentUIMode = UI_MODE_NONE;
	        });

	    it(
	        "zooms to max exactly as TimelineView::zoomToMax does, in both modes", _ {
		        spec_fixture::QuietAudio quiet;
		        Rig r;
		        const Sentinel s{.x_scroll = 192, .x_zoom = 12};
		        r.seed(s);
		        TimelineState port = r.port();
		        const bool port_moved = port.zoom_to_max(r.max_zoom());

		        r.seed(s);
		        LEGACY_ONLY(tkeep(self, "zoom_to_max", "moved", audioClipView.zoomToMax());
		                    tkeep(self, "zoom_to_max", "zoom", r.song_zoom());
		                    tkeep(self, "zoom_to_max", "scroll", r.song_scroll()););
		        const bool legacy_moved = tnum("zoom_to_max", "moved") != 0;
		        expect(port_moved).to_equal(legacy_moved);
		        expect(int64_t{port.x_zoom}).to_equal(tnum("zoom_to_max", "zoom"));
		        expect(int64_t{port.x_scroll}).to_equal(tnum("zoom_to_max", "scroll"));
		        expect(static_cast<int32_t>(port.x_zoom)).to_equal(48);
		        expect(port.x_scroll).to_equal(0);
		        currentUIMode = UI_MODE_NONE;

		        // `inOnly` refuses to zoom OUT: from 12, a max zoom of 48 is further out, so nothing moves.
		        r.seed(s);
		        TimelineState port_in_only = r.port();
		        const bool port_in_only_moved = port_in_only.zoom_to_max(r.max_zoom(), true);

		        r.seed(s);
		        LEGACY_ONLY(tkeep(self, "zoom_to_max", "in_only_moved", audioClipView.zoomToMax(true));
		                    tkeep(self, "zoom_to_max", "in_only_zoom", r.song_zoom()););
		        const bool legacy_in_only_moved = tnum("zoom_to_max", "in_only_moved") != 0;
		        expect(port_in_only_moved).to_equal(legacy_in_only_moved);
		        expect(port_in_only_moved).to_be_false();
		        expect(int64_t{port_in_only.x_zoom}).to_equal(tnum("zoom_to_max", "in_only_zoom"));
		        currentUIMode = UI_MODE_NONE;
	        });

	    // ---- the bars/beats decomposition ----------------------------------------------------------

	    it(
	        "decomposes into the same bars, beats and sub-beats displayNumberOfBarsAndBeats shows", _ {
		        // Legacy's decomposition never leaves the function -- it is `sprintf`ed straight into
		        // `display->popupTextTemporary` (`TimelineView::displayNumberOfBarsAndBeats` in `timeline_view.cpp`).
		        // So both sides are compared through that same popup: legacy's own call, and the port's three numbers
		        // formatted the same way and pushed through the same `display`. Anything the port got wrong shows up as
		        // different pixels.
		        Rig r;
		        const Sentinel s;
		        r.seed(s);
		        const uint32_t one_bar = r.f.song->getBarLength();

		        for (uint32_t number : {0U, 1U, 96U, 97U, 191U, 384U, 767U}) {
			        const std::string page = std::format("timeline_bars_beats_{}", number);
			        LEGACY_ONLY(deluge::hid::display::OLED::popup.clear();
			                    audioClipView.displayNumberOfBarsAndBeats(number, s.x_zoom, true, "FAR");
			                    deluge::ui::scenes::spec::keep_page(self, page, popup_pbm()););
			        const std::string legacy = deluge::ui::scenes::spec::AudioClipPages::page(page);

			        const BarsBeats bb = TimelineState::bars_and_beats(number, one_bar, true);
			        char buffer[15];
			        snprintf(buffer, sizeof(buffer), "%d : %d : %d", static_cast<int>(bb.bar),
			                 static_cast<int>(bb.beat), static_cast<int>(bb.sub_beat));
			        deluge::hid::display::OLED::popup.clear();
			        display->popupTextTemporary(buffer);
			        expect(popup_pbm()).to_equal(legacy);
		        }

		        // The numbers themselves, so a spec that compared two blank canvases could not pass.
		        const BarsBeats first = TimelineState::bars_and_beats(0, one_bar, true);
		        expect(static_cast<int32_t>(first.bar)).to_equal(1);
		        expect(static_cast<int32_t>(first.beat)).to_equal(1);
		        expect(static_cast<int32_t>(first.sub_beat)).to_equal(1);
		        const BarsBeats zero_based = TimelineState::bars_and_beats(0, one_bar, false);
		        expect(static_cast<int32_t>(zero_based.bar)).to_equal(0);
	        });

	    // ---- the tick square -----------------------------------------------------------------------

	    it(
	        "puts the play position in the same square ClipView::getTickSquare does", _ {
		        // Driven through legacy's own protected body -- see `TickProbe`. The clip is not recording
		        // linearly, and `scroll_before_following` is `kNotFollowing` on both sides, so both take
		        // the else arm's no-op path and the comparison is of the square itself.
		        StoppedTransport stopped;
		        Rig r;
		        const Sentinel s{.x_scroll = 96, .x_zoom = 6};
		        r.seed(s);
		        r.f.clip->lastProcessedPos = 96 + 5 * 6;
		        TickProbe::set_remembered_scroll(-1);
		        TimelineState port = r.port();
		        const TimelineState::TickSquare ported =
		            port.tick_square(static_cast<int32_t>(r.f.clip->getLivePos()),
		                             TimelineState::TickSquareGates{.recording_linearly = false,
		                                                            .ui_idle = true,
		                                                            .clip_unarmed = true,
		                                                            .max_length = r.max_length()});

		        LEGACY_ONLY(r.seed(s); r.f.clip->lastProcessedPos = 96 + 5 * 6; TickProbe::set_remembered_scroll(-1);
		                    tkeep(self, "tick_square", "square", (audioClipView.*TickProbe::kGetTickSquare)());
		                    tkeep(self, "tick_square", "scroll", r.song_scroll()););
		        const int64_t legacy = tnum("tick_square", "square");
		        expect(int64_t{ported.square}).to_equal(legacy);
		        expect(ported.square).to_equal(5);
		        expect(ported.scrolled).to_be_false();
		        expect(int64_t{port.x_scroll}).to_equal(tnum("tick_square", "scroll"));
	        });

	    it(
	        "restores the remembered scroll when the follow stops, legacy's xZoom comparison and all", _ {
		        // `ClipView::getTickSquare` (`clip_view.cpp`), the arm that stops following. Its
		        // `if (newXScroll != currentSong->persistentView.xZoom[NAVIGATION_CLIP])` compares a scroll against the
		        // ZOOM -- see the note at the top of `timeline.cpp`. Both sides are given the same remembered scroll
		        // (6, which is deliberately NOT the zoom) and both must restore it.
		        StoppedTransport stopped;
		        spec_fixture::QuietAudio quiet;
		        Rig r;
		        const Sentinel s{.x_scroll = 480, .x_zoom = 12};

		        r.seed(s);
		        r.f.clip->lastProcessedPos = 0;
		        TimelineState port = r.port();
		        port.scroll_before_following = 6;
		        const TimelineState::TickSquare ported =
		            port.tick_square(static_cast<int32_t>(r.f.clip->getLivePos()),
		                             TimelineState::TickSquareGates{.recording_linearly = false,
		                                                            .ui_idle = true,
		                                                            .clip_unarmed = true,
		                                                            .max_length = r.max_length()});

		        LEGACY_ONLY(r.seed(s); r.f.clip->lastProcessedPos = 0; TickProbe::set_remembered_scroll(6);
		                    (void)(audioClipView.*TickProbe::kGetTickSquare)();
		                    tkeep(self, "follow_stops", "scroll", r.song_scroll());
		                    tkeep(self, "follow_stops", "follow", TickProbe::remembered_scroll()););
		        expect(ported.scrolled).to_be_true();
		        expect(int64_t{port.x_scroll}).to_equal(tnum("follow_stops", "scroll"));
		        expect(port.x_scroll).to_equal(6);
		        expect(int64_t{port.scroll_before_following}).to_equal(tnum("follow_stops", "follow"));
		        expect(port.scroll_before_following).to_equal(-1);
		        currentUIMode = UI_MODE_NONE;

		        // And with the remembered scroll EQUAL to the zoom, legacy's comparison suppresses the
		        // restore. That is the bug, driven on both sides so it cannot be silently "fixed" on one.
		        r.seed(s);
		        r.f.clip->lastProcessedPos = 0;
		        TimelineState suppressed = r.port();
		        suppressed.scroll_before_following = 12;
		        const TimelineState::TickSquare not_restored =
		            suppressed.tick_square(static_cast<int32_t>(r.f.clip->getLivePos()),
		                                   TimelineState::TickSquareGates{.recording_linearly = false,
		                                                                  .ui_idle = true,
		                                                                  .clip_unarmed = true,
		                                                                  .max_length = r.max_length()});

		        LEGACY_ONLY(r.seed(s); r.f.clip->lastProcessedPos = 0; TickProbe::set_remembered_scroll(12);
		                    (void)(audioClipView.*TickProbe::kGetTickSquare)();
		                    tkeep(self, "follow_stops_suppressed", "scroll", r.song_scroll());
		                    tkeep(self, "follow_stops_suppressed", "follow", TickProbe::remembered_scroll()););
		        expect(not_restored.scrolled).to_be_false();
		        expect(int64_t{suppressed.x_scroll}).to_equal(tnum("follow_stops_suppressed", "scroll"));
		        expect(suppressed.x_scroll).to_equal(480);
		        expect(int64_t{suppressed.scroll_before_following}).to_equal(tnum("follow_stops_suppressed", "follow"));
		        currentUIMode = UI_MODE_NONE;
	        });

	    // ---- the Song seam -------------------------------------------------------------------------

	    it(
	        "adopts and publishes the clip navigation slot, and only that slot", _ {
		        Rig r;
		        r.f.song->persistentView.xScroll[NAVIGATION_CLIP] = 96;
		        r.f.song->persistentView.xZoom[NAVIGATION_CLIP] = 6;
		        r.f.song->persistentView.xScroll[NAVIGATION_ARRANGEMENT] = 4242;
		        r.f.song->persistentView.xZoom[NAVIGATION_ARRANGEMENT] = 99;
		        r.f.song->persistentView.tripletsOn = true;
		        r.f.song->persistentView.tripletsLevel = 8;

		        TimelineState port;
		        port.adopt(*deluge::gui::shell_host::host_ports().clips->clip_navigation());
		        expect(port.x_scroll).to_equal(96);
		        expect(static_cast<int32_t>(port.x_zoom)).to_equal(6);
		        expect(port.triplets_on).to_be_true();
		        expect(static_cast<int32_t>(port.triplets_level)).to_equal(8);

		        port.x_scroll = 192;
		        port.x_zoom = 12;
		        spec_fixture::publish(port, *r.f.song);
		        expect(r.f.song->persistentView.xScroll[NAVIGATION_CLIP]).to_equal(192);
		        expect(static_cast<int32_t>(r.f.song->persistentView.xZoom[NAVIGATION_CLIP])).to_equal(12);
		        expect(r.f.song->persistentView.xScroll[NAVIGATION_ARRANGEMENT]).to_equal(4242);
		        expect(static_cast<int32_t>(r.f.song->persistentView.xZoom[NAVIGATION_ARRANGEMENT])).to_equal(99);
	        });

	    it(
	        "focus_regained forgets the remembered scroll, as ClipNavigationTimelineView does", _ {
		        TimelineState port;
		        port.scroll_before_following = 96;
		        port.focus_regained();
		        expect(port.scroll_before_following).to_equal(-1);
	        });

	    it(
	        "asked every frozen answer of legacy's, and read every frozen page", _ {
		        expect(answers().size()).to_be_greater_than(size_t{0});
		        expect(answers().unasked()).to_equal(std::string{});
		        expect(deluge::ui::scenes::spec::AudioClipPages::unread("timeline_")).to_equal(std::string{});
	        });
    });
// NOLINTEND(readability-function-cognitive-complexity)

DELUGE_SPEC(timeline_spec, timeline_spec_body)
