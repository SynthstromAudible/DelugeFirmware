/// @file
/// @brief `timeline::TimelineState` -- the port of legacy's `TimelineView` ->
///        `ClipNavigationTimelineView` -> `ClipView` scroll/zoom arithmetic -- driven against
///        **those classes themselves**, through the one concrete subclass this plan's next tasks
///        target (`audioClipView`) and, for the triplet arms it disables, `instrumentClipView`.
///
/// @par Each side from its own sentinel
///
/// Every example declares a `Sentinel` -- scroll, zoom, triplets, clip length -- and seeds it into
/// the **`Song`** twice: once before the port runs (which reaches it through `TimelineState::adopt`)
/// and once again before legacy runs (which reads `currentSong->xScroll[NAVIGATION_CLIP]` itself).
/// Neither side is ever handed the other's numbers. The limits legacy asks its own virtuals for
/// (`AudioClipView::getMaxZoom`/`getMaxLength`, `audio_clip_view.cpp:857-862`/`:864-870`) reach the port as
/// `Clip::getMaxZoom()`/`Clip::getMaxLength()` (`model/clip/clip.cpp:142-149`, `:1066-1068`) --
/// the same model question, asked of the model rather than of legacy's answer.
///
/// @par The one-detent ceiling, measured
///
/// Legacy's scroll and zoom each end in a `PadLEDs` transition the port does not carry
/// (`src/scenes/timeline/timeline.h`). `initiateXScroll` sets `UI_MODE_HORIZONTAL_SCROLL`
/// (`timeline_view.cpp:279`) and `initiateXZoom` sets `UI_MODE_HORIZONTAL_ZOOM` (`:325`), and
/// nothing in a spec process runs either animation to the frame that clears them -- so the *next*
/// detent hits `horizontalEncoderAction`'s own `isNoUIModeActive()` (`:203`) or
/// `isUIModeActiveExclusively(UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON)` (`:164`) gate and does
/// nothing. E3 recorded the same ceiling for the marker editor
/// (`sim/scenes_spec/marker_scene_spec.cpp`'s zoom example) and found that clearing the mode by
/// hand did **not** buy a second detent there.
///
/// **Here clearing it does buy the next detent, and that is measured, not argued.** The evidence is
/// "scrolls a second page when the transition mode is cleared": it asserts
/// `UI_MODE_HORIZONTAL_SCROLL` set after the first detent, clears it, takes a second, and lands on
/// **page two** -- which is impossible if the second turn did nothing. The zoom examples then use
/// the same move to reach legacy's max-zoom *delay* (`:171-186`), which is otherwise unobservable
/// because the mode gate would refuse first.
///
/// No claim is made here about **why** E3 saw the opposite. `WaveformBasicNavigator::zoom`'s tail
/// (`gui/waveform/waveform_basic_navigator.cpp:64-69`) is the same three statements `initiateXZoom`
/// ends in (`timeline_view.cpp:325-327`) and contains no wait of its own; the only structural
/// difference is the `waveformRenderer.renderFullScreen` at `:59`, which `initiateXZoom` has no
/// analogue for. Task 8's first round asserted a blocking transition as the cause and that was
/// wrong -- the conclusion stood on the scroll example all along.
///
/// @par What this spec does not cover
///
///   * **`TimelineState::tick_square`'s auto-scroll arm.** `ClipView::getTickSquare` is protected
///     (`clip_view.h:38`) and its follow arm is gated on `getCurrentlyRecordingLinearly()` and
///     `getCurrentUI() == this` -- a live recorder and a real root UI, neither of which this
///     fixture has. The pointer-to-member handle below reaches the *body*; the arms it can reach
///     from a non-recording clip are compared, the recording ones are named uncovered here.
///
/// @note The spec may include `gui/` and `hid/` (it drives the legacy views); the library may not.
///       Same rule `marker_scene_spec.cpp` runs under.

#include "scenes/timeline/timeline.h"

#include "gui/ui/ui.h"
#include "gui/views/audio_clip_view.h"
#include "gui/views/instrument_clip_view.h"
#include "hid/button.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "model/clip/audio_clip.h"
#include "model/song/song.h"

#include "support/song_fixture.h"

// Before <cppspec.hpp>: it #defines `it`, which collides with locals named `it` in the project
// headers this drags in. See the warning at the top of support/card_access.h.
#include "support/card_access.h"

#include "spec/gfx/support/pbm.h"

#include "cppspec.hpp"

#include <cstdint>
#include <cstdio>
#include <string>

#include "spec/support/spec_main.h"

using deluge::scenes::timeline::BarsBeats;
using deluge::scenes::timeline::TimelineState;
using deluge::scenes::timeline::ZoomOutcome;
namespace spec_fixture = deluge::scenes::spec;

/// @brief `PlaybackHandler`'s own "the swung-tick answer is zero" latch
///        (`playback/playback_handler.cpp:100`, read at
///        `PlaybackHandler::getNumSwungTicksInSinceLastActionedSwungTick`, `:1208-1217`).
///
/// Declared here rather than included because it is a file-scope global with no header declaration
/// -- the same shape `menu/nodes/settings.cpp` uses for `jcong`. **Not a convenience.**
/// `Clip::getLivePos()` (`model/clip/clip.cpp:151-166`) adds
/// `getNumSwungTicksInSinceLastActionedSwungTick()` to `lastProcessedPos`; on a song no transport
/// ever started that reaches `getActualSwungTickCount`'s external-clock swing branch and divides by
/// `(uint32_t)(50 + currentSong->swingAmount)` (`:1263`), which is a **SIGFPE, not a failure** --
/// found by running the tick-square examples without this. Holding the latch takes legacy's own
/// early return (`:1209-1214`), so `getLivePos()` is exactly `lastProcessedPos`, which is what a
/// stopped transport means.
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
	/// `Clip::getMaxLength()` is `loopLength` (`model/clip/clip.cpp:1066-1068`), and
	/// `Clip::getMaxZoom()` (`:142-149`) is derived from it: 768 ticks gives a max zoom of 48, so
	/// the reachable zoom ladder is 3, 6, 12, 24, 48, 96, 192, 384, 768.
	int32_t loop_length = 768;
};

/// @brief `ClipView::getTickSquare` is protected (`clip_view.h:38`).
///
/// A class derived from `ClipView` may name it, and `&TickProbe::getTickSquare` then has type
/// `int32_t (ClipView::*)()` -- a legal handle to legacy's own body, with no cast anywhere and no
/// second instance of anything. `TickProbe` is never constructed (it is abstract).
struct TickProbe : ClipView {
	static constexpr int32_t (ClipView::*kGetTickSquare)() = &TickProbe::getTickSquare;

	/// @brief `ClipNavigationTimelineView::xScrollBeforeFollowingAutoExtendingLinearRecording`
	///        (`clip_navigation_timeline_view.h:32`), which is protected and static.
	///
	/// Setting it to -1 is `ClipNavigationTimelineView::focusRegained`'s entire body
	/// (`clip_navigation_timeline_view.cpp:26-28`); the real `focusRegained` is not called because
	/// `AudioClipView`'s (`audio_clip_view.cpp:78-89`) also drags in `view.focusRegained()` and
	/// `setActiveModControllableTimelineCounter`, neither of which this comparison is about.
	/// @{
	static void set_remembered_scroll(int32_t value) { xScrollBeforeFollowingAutoExtendingLinearRecording = value; }
	static int32_t remembered_scroll() { return xScrollBeforeFollowingAutoExtendingLinearRecording; }
	/// @}
};

/// @brief `deluge_boot`'s own `display = new OLED` (`src/deluge/deluge.cpp:665`).
///
/// A spec process never calls `deluge_boot()` (see `support/song_fixture.h` for why), so the
/// `display` global is null (`hid/display/display.cpp:4`). Legacy's scroll arm ends in
/// `displayScrollPos()` -> `display->popupTextTemporary` **unconditionally**
/// (`timeline_view.cpp:221`, `:264`), and its zoom arm in `displayZoomLevel()` -> `displayPopup`
/// (`:198`, `:130`). Without this the gesture examples below do not fail, they segfault -- found by
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
	/// (`TimelineView::delayHorizontalZoomUntil`, `timeline_view.h:78`) is a member of the
	/// `audioClipView` **singleton**, and it is compared against `AudioEngine::audioSampleTimer`
	/// (`timeline_view.cpp:172`), which a spec process never advances -- so once armed it stays
	/// armed for the rest of the process and silently refuses every later zoom in that direction.
	/// Not hypothetical: the three "forgets the restore point" examples below were written after
	/// the delay example and legacy sat at zoom 12 while the port zoomed to 6, until this line
	/// existed. Advancing the timer past `kShortPressTime` is **legacy's own expiry**
	/// (`:175-179` clears the latch), not a reach into private state, and it is safe here because
	/// every `PadLEDs::recordTransitionBegin` in this file happens *after* the last `seed()` of its
	/// example, so no transition is ever handed a jumped clock.
	void seed(const Sentinel& s) const {
		AudioEngine::audioSampleTimer += 2 * kShortPressTime;
		f.song->xScroll[NAVIGATION_CLIP] = s.x_scroll;
		f.song->xZoom[NAVIGATION_CLIP] = s.x_zoom;
		f.song->tripletsOn = s.triplets_on;
		f.song->tripletsLevel = s.triplets_level;
		f.clip->loopLength = s.loop_length;
		currentUIMode = UI_MODE_NONE;
	}

	/// @brief A port state adopted from the song, with `AudioClipView`'s own
	///        `supportsTriplets() == false` (`audio_clip_view.h:38`).
	[[nodiscard]] TimelineState port() const {
		TimelineState s;
		s.adopt(*f.song);
		s.supports_triplets = false;
		return s;
	}

	/// @brief The same, but with triplets supported -- what `instrumentClipView` reports
	///        (`RootUI::supportsTriplets`, `gui/ui/root_ui.h:32`).
	[[nodiscard]] TimelineState port_with_triplets() const {
		TimelineState s;
		s.adopt(*f.song);
		s.supports_triplets = true;
		return s;
	}

	[[nodiscard]] uint32_t max_zoom() const { return static_cast<uint32_t>(f.clip->getMaxZoom()); }
	[[nodiscard]] uint32_t max_length() const { return static_cast<uint32_t>(f.clip->getMaxLength()); }

	[[nodiscard]] int32_t song_scroll() const { return f.song->xScroll[NAVIGATION_CLIP]; }
	[[nodiscard]] uint32_t song_zoom() const { return f.song->xZoom[NAVIGATION_CLIP]; }

	/// @brief `TimelineView::buttonAction`'s X_ENC arm (`timeline_view.cpp:73-89`), which is how
	///        legacy is told the horizontal encoder's button is held.
	static void hold_x_enc(bool on) { audioClipView.buttonAction(deluge::hid::button::X_ENC, on, false); }

	/// @brief Clear the transition mode `initiateXZoom` left behind (`timeline_view.cpp:325`),
	///        keeping the held-button mode, so legacy will consider a second detent.
	static void clear_zoom_transition() { currentUIMode &= ~UI_MODE_HORIZONTAL_ZOOM; }
	/// @brief The same for `initiateXScroll` (`timeline_view.cpp:279`).
	static void clear_scroll_transition() { currentUIMode &= ~UI_MODE_HORIZONTAL_SCROLL; }
};

/// @brief The 128x48 popup canvas, as a PBM string -- the surface `display->popupTextTemporary`
///        draws into (`OLED::popupText`, `hid/display/oled.cpp:605-644`).
std::string popup_pbm() {
	return page_major_to_pbm(deluge::hid::display::OLED::popup.bytes().data(), 128, 48);
}

} // namespace

// NOLINTBEGIN(readability-function-cognitive-complexity)
describe timeline_spec_body(
    "scenes/timeline TimelineState", $ {
	    // ---- the pure arithmetic, no triplets (audioClipView) -------------------------------------

	    it(
	        "agrees with TimelineView::getPosFromSquare across a page and beyond both edges", _ {
		        Rig r;
		        const Sentinel s{.x_scroll = 96, .x_zoom = 6};
		        r.seed(s);
		        const TimelineState port = r.port();

		        r.seed(s);
		        for (int32_t square = -4; square <= kDisplayWidth + 4; square++) {
			        expect(port.pos_from_square(square)).to_equal(audioClipView.getPosFromSquare(square));
		        }
		        // ...and it is really doing arithmetic, not returning the scroll.
		        expect(port.pos_from_square(1)).to_equal(102);
	        });

	    it(
	        "agrees with TimelineView::getSquareFromPos on both sides of the scroll, exactness included", _ {
		        // The rounding direction is the whole point: `getSquareFromPos` runs through
		        // `divide_round_negative` (`timeline_view.cpp:459`) precisely so a position LEFT of the
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
			            .to_equal(audioClipView.getSquareFromPos(pos, &legacy_exact));
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
			        expect(port.square_end_from_pos(pos)).to_equal(audioClipView.getSquareEndFromPos(pos));
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
			        expect(port.length_chop_amount(square)).to_equal(audioClipView.getLengthChopAmount(square));
			        expect(port.length_extend_amount(square)).to_equal(audioClipView.getLengthExtendAmount(square));
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
			        expect(port.square_defined(square)).to_equal(audioClipView.isSquareDefined(square));
		        }
	        });

	    // ---- the pure arithmetic, triplets on (instrumentClipView) --------------------------------

	    it(
	        "agrees with TimelineView::getPosFromSquare in triplets view, both zoom branches", _ {
		        // `getPosFromSquare` has three arms (`timeline_view.cpp:371-395`): zoom below the triplets
		        // level, zoom below twice it, and everything else. `instrumentClipView` is used rather than
		        // `audioClipView` because `AudioClipView::supportsTriplets()` is false
		        // (`audio_clip_view.h:38`) and `inTripletsView()` would be false on every arm.
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
		        expect(undefined > 0).to_be_true();
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
		        expect(port.triplets_on).to_equal(r.f.song->tripletsOn);
		        expect(port.triplets_level).to_equal(r.f.song->tripletsLevel);
		        expect(port.triplets_level).to_equal(32U);

		        port.toggle_triplets();
		        instrumentClipView.tripletsButtonPressed();
		        expect(port.triplets_on).to_equal(r.f.song->tripletsOn);
		        expect(port.triplets_on).to_be_false();
		        // Legacy leaves the level behind when switching off (`timeline_view.cpp:357-361`).
		        expect(port.triplets_level).to_equal(r.f.song->tripletsLevel);
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
		        audioClipView.horizontalEncoderAction(1);
		        expect(port.x_scroll).to_equal(r.song_scroll());
		        expect(port_moved).to_be_true();
		        expect(port.x_scroll).to_equal(6 * kDisplayWidth);
		        currentUIMode = UI_MODE_NONE;
	        });

	    it(
	        "clamps a left scroll at zero exactly as the left guard does", _ {
		        // `timeline_view.cpp:211-213`. From half a page in, one detent left overshoots zero.
		        spec_fixture::QuietAudio quiet;
		        Rig r;
		        const Sentinel s{.x_scroll = 48, .x_zoom = 6};
		        r.seed(s);
		        TimelineState port = r.port();
		        const bool port_moved = port.scroll(-1, r.max_length());

		        r.seed(s);
		        audioClipView.horizontalEncoderAction(-1);
		        expect(port.x_scroll).to_equal(r.song_scroll());
		        expect(port_moved).to_be_true();
		        expect(port.x_scroll).to_equal(0);
		        currentUIMode = UI_MODE_NONE;
	        });

	    it(
	        "refuses a right scroll that would leave the clip, exactly as the right guard does", _ {
		        // `timeline_view.cpp:216`: a rightward scroll is only taken when the new scroll is still
		        // inside `getMaxLength()`. At zoom 48 one page IS the 768-tick clip, so one detent right
		        // lands exactly on the length and must be refused.
		        spec_fixture::QuietAudio quiet;
		        Rig r;
		        const Sentinel s{.x_scroll = 0, .x_zoom = 48};
		        r.seed(s);
		        TimelineState port = r.port();
		        const bool port_moved = port.scroll(1, r.max_length());

		        r.seed(s);
		        audioClipView.horizontalEncoderAction(1);
		        expect(port.x_scroll).to_equal(r.song_scroll());
		        expect(port_moved).to_be_false();
		        expect(port.x_scroll).to_equal(0);
		        currentUIMode = UI_MODE_NONE;
	        });

	    it(
	        "scrolls a second page when the transition mode is cleared, as legacy does", _ {
		        // The scroll half of the one-detent ceiling: `initiateXScroll` sets
		        // `UI_MODE_HORIZONTAL_SCROLL` (`timeline_view.cpp:279`) and the next detent then fails
		        // `isNoUIModeActive()` (`:203`). Unlike E3's marker-editor zoom, clearing the mode really
		        // does buy the next detent here -- measured by this example, which would land on page one
		        // rather than page two if it did not.
		        spec_fixture::QuietAudio quiet;
		        Rig r;
		        const Sentinel s{.x_scroll = 0, .x_zoom = 6};
		        r.seed(s);
		        TimelineState port = r.port();
		        (void)port.scroll(1, r.max_length());
		        (void)port.scroll(1, r.max_length());

		        r.seed(s);
		        audioClipView.horizontalEncoderAction(1);
		        expect((currentUIMode & UI_MODE_HORIZONTAL_SCROLL) != 0).to_be_true();
		        Rig::clear_scroll_transition();
		        audioClipView.horizontalEncoderAction(1);
		        expect(port.x_scroll).to_equal(r.song_scroll());
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
		        Rig::hold_x_enc(true);
		        audioClipView.horizontalEncoderAction(1);
		        expect(static_cast<int32_t>(port.x_zoom)).to_equal(static_cast<int32_t>(r.song_zoom()));
		        expect(port.x_scroll).to_equal(r.song_scroll());
		        expect(outcome == ZoomOutcome::Zoomed).to_be_true();
		        // ...and it really zoomed in, so a port that refused the turn could not pass this.
		        expect(static_cast<int32_t>(port.x_zoom)).to_equal(6);
		        Rig::hold_x_enc(false);
		        currentUIMode = UI_MODE_NONE;
	        });

	    it(
	        "snaps the scroll to a whole page when it zooms out, exactly as legacy does", _ {
		        // `timeline_view.cpp:195`: the new scroll is the old one floored to a multiple of
		        // `newZoom * kDisplayWidth`. From 192 at zoom 12, zooming OUT to 24 makes a page 384 ticks
		        // wide, so the scroll must snap back to 0 rather than stay at 192.
		        spec_fixture::QuietAudio quiet;
		        Rig r;
		        const Sentinel s{.x_scroll = 192, .x_zoom = 12};
		        r.seed(s);
		        TimelineState port = r.port();
		        const ZoomOutcome outcome = port.zoom(-1, r.max_zoom(), 1000);

		        r.seed(s);
		        Rig::hold_x_enc(true);
		        audioClipView.horizontalEncoderAction(-1);
		        expect(static_cast<int32_t>(port.x_zoom)).to_equal(static_cast<int32_t>(r.song_zoom()));
		        expect(port.x_scroll).to_equal(r.song_scroll());
		        expect(outcome == ZoomOutcome::Zoomed).to_be_true();
		        expect(static_cast<int32_t>(port.x_zoom)).to_equal(24);
		        expect(port.x_scroll).to_equal(0);
		        Rig::hold_x_enc(false);
		        currentUIMode = UI_MODE_NONE;
	        });

	    it(
	        "refuses to zoom in past the floor, exactly as the zoom limit does", _ {
		        // `timeline_view.cpp:189`: `zoomMagnitude == -1 && oldXZoom <= 3`. Three is the floor of
		        // `Clip::getMaxZoom()`'s ladder (`model/clip/clip.cpp:142-149`), so this is the real edge.
		        spec_fixture::QuietAudio quiet;
		        Rig r;
		        const Sentinel s{.x_scroll = 0, .x_zoom = 3};
		        r.seed(s);
		        TimelineState port = r.port();
		        const ZoomOutcome outcome = port.zoom(1, r.max_zoom(), 1000);

		        r.seed(s);
		        Rig::hold_x_enc(true);
		        audioClipView.horizontalEncoderAction(1);
		        expect(static_cast<int32_t>(port.x_zoom)).to_equal(static_cast<int32_t>(r.song_zoom()));
		        expect(port.x_scroll).to_equal(r.song_scroll());
		        expect(outcome == ZoomOutcome::Refused).to_be_true();
		        expect(static_cast<int32_t>(port.x_zoom)).to_equal(3);
		        Rig::hold_x_enc(false);
		        currentUIMode = UI_MODE_NONE;
	        });

	    it(
	        "refuses to zoom out past the ceiling, exactly as the zoom limit does", _ {
		        // The other half of `:189-191`: `oldXZoom >= min(maxZoom * 16, kMaxZoom)`. With a 768-tick
		        // clip the max zoom is 48, so the ceiling is 768 -- and starting there, zooming out must
		        // do nothing.
		        spec_fixture::QuietAudio quiet;
		        Rig r;
		        const Sentinel s{.x_scroll = 0, .x_zoom = 768};
		        r.seed(s);
		        TimelineState port = r.port();
		        const ZoomOutcome outcome = port.zoom(-1, r.max_zoom(), 1000);

		        r.seed(s);
		        Rig::hold_x_enc(true);
		        audioClipView.horizontalEncoderAction(-1);
		        expect(static_cast<int32_t>(port.x_zoom)).to_equal(static_cast<int32_t>(r.song_zoom()));
		        expect(outcome == ZoomOutcome::Refused).to_be_true();
		        expect(static_cast<int32_t>(port.x_zoom)).to_equal(768);
		        Rig::hold_x_enc(false);
		        currentUIMode = UI_MODE_NONE;
	        });

	    it(
	        "holds the second detent back when a zoom lands on the clip's own max zoom", _ {
		        // `timeline_view.cpp:171-186`: landing exactly on `getMaxZoom()` arms a `kShortPressTime`
		        // delay in that direction, and the next detent the same way is dropped. Legacy measures it
		        // against `AudioEngine::audioSampleTimer`, which does not advance in a spec process, so its
		        // delay is always "still running"; the port is driven at the same `now_ms` for both detents
		        // so it is too.
		        spec_fixture::QuietAudio quiet;
		        Rig r;
		        const Sentinel s{.x_scroll = 0, .x_zoom = 96};
		        r.seed(s);
		        TimelineState port = r.port();
		        const ZoomOutcome first = port.zoom(1, r.max_zoom(), 1000);
		        const ZoomOutcome second = port.zoom(1, r.max_zoom(), 1000);

		        r.seed(s);
		        Rig::hold_x_enc(true);
		        audioClipView.horizontalEncoderAction(1);
		        // Legacy ends at 48 whether the second detent was dropped by the delay latch (`:175`),
		        // by the `isUIModeActiveExclusively` gate (`:164`), or because `initiateXZoom` never
		        // armed the mode at all -- so the example pins WHICH. The first detent really did
		        // reach `initiateXZoom` (`:325`)...
		        expect((currentUIMode & UI_MODE_HORIZONTAL_ZOOM) != 0).to_be_true();
		        Rig::clear_zoom_transition();
		        // ...and with that cleared, the mode gate at `:164` passes, so the only thing left that
		        // can stop the second detent is the delay latch. Its `goto getOut` at `:176` returns
		        // before `initiateXZoom`, so the mode must still be clear afterwards.
		        audioClipView.horizontalEncoderAction(1);
		        expect((currentUIMode & UI_MODE_HORIZONTAL_ZOOM) != 0).to_be_false();
		        expect(static_cast<int32_t>(port.x_zoom)).to_equal(static_cast<int32_t>(r.song_zoom()));
		        expect(first == ZoomOutcome::Zoomed).to_be_true();
		        expect(second == ZoomOutcome::Delayed).to_be_true();
		        // The first detent landed on 48, the clip's own max zoom; the second was dropped.
		        expect(static_cast<int32_t>(port.x_zoom)).to_equal(48);
		        Rig::hold_x_enc(false);
		        currentUIMode = UI_MODE_NONE;
	        });

	    it(
	        "lets the delayed detent through once the delay has expired", _ {
		        // The other side of the same arm (`:175-179`): outside the window the latch is cleared and
		        // the zoom proceeds. Only the port can be driven here -- legacy's window is measured in
		        // audio samples off a timer a spec process never advances, so its latch never expires. That
		        // is named as uncovered rather than papered over.
		        Rig r;
		        const Sentinel s{.x_scroll = 0, .x_zoom = 96};
		        r.seed(s);
		        TimelineState port = r.port();
		        expect(port.zoom(1, r.max_zoom(), 1000) == ZoomOutcome::Zoomed).to_be_true();
		        expect(port.zoom(1, r.max_zoom(), 1000) == ZoomOutcome::Delayed).to_be_true();
		        expect(port.zoom(1, r.max_zoom(), 1000 + 500) == ZoomOutcome::Zoomed).to_be_true();
		        expect(static_cast<int32_t>(port.x_zoom)).to_equal(24);
	        });

	    it(
	        "lets a detent the OTHER way through while the delay is running", _ {
		        // `:175` only drops the detent whose magnitude matches the armed one.
		        Rig r;
		        const Sentinel s{.x_scroll = 0, .x_zoom = 96};
		        r.seed(s);
		        TimelineState port = r.port();
		        expect(port.zoom(1, r.max_zoom(), 1000) == ZoomOutcome::Zoomed).to_be_true();
		        expect(port.zoom(-1, r.max_zoom(), 1000) == ZoomOutcome::Zoomed).to_be_true();
		        expect(static_cast<int32_t>(port.x_zoom)).to_equal(96);
	        });

	    // ---- the linear-recording restore point every horizontal turn clears ----------------------

	    it(
	        "forgets the linear-recording restore point on a manual scroll, as legacy does", _ {
		        // `ClipNavigationTimelineView::horizontalEncoderAction` is two statements
		        // (`clip_navigation_timeline_view.cpp:30-34`): clear
		        // `xScrollBeforeFollowingAutoExtendingLinearRecording` (`:32`), then delegate (`:33`).
		        // A port that skipped the clear would keep a stale restore point and yank the scroll
		        // back when the recording stopped -- `getTickSquare`'s restore arm
		        // (`clip_view.cpp:376-386`) reads exactly this field.
		        spec_fixture::QuietAudio quiet;
		        Rig r;
		        const Sentinel s{.x_scroll = 0, .x_zoom = 6};

		        r.seed(s);
		        TimelineState port = r.port();
		        port.scroll_before_following = 480;
		        (void)port.scroll(1, r.max_length());

		        r.seed(s);
		        TickProbe::set_remembered_scroll(480);
		        audioClipView.horizontalEncoderAction(1);
		        expect(port.scroll_before_following).to_equal(TickProbe::remembered_scroll());
		        expect(port.scroll_before_following).to_equal(-1);
		        // ...and the scroll really did happen, so this is not passing because nothing ran.
		        expect(port.x_scroll).to_equal(r.song_scroll());
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
		        TickProbe::set_remembered_scroll(480);
		        Rig::hold_x_enc(true);
		        audioClipView.horizontalEncoderAction(1);
		        expect(port.scroll_before_following).to_equal(TickProbe::remembered_scroll());
		        expect(port.scroll_before_following).to_equal(-1);
		        expect(static_cast<int32_t>(port.x_zoom)).to_equal(static_cast<int32_t>(r.song_zoom()));
		        expect(static_cast<int32_t>(port.x_zoom)).to_equal(6);
		        Rig::hold_x_enc(false);
		        currentUIMode = UI_MODE_NONE;
	        });

	    it(
	        "forgets it even when the zoom limit then refuses the turn", _ {
		        // The clear is legacy's FIRST statement, above every gate `TimelineView` applies
		        // (`timeline_view.cpp:137-151`, `:161`, `:164`, `:189-192`), so a refused turn still
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
		        TickProbe::set_remembered_scroll(480);
		        Rig::hold_x_enc(true);
		        audioClipView.horizontalEncoderAction(1);
		        expect(outcome == ZoomOutcome::Refused).to_be_true();
		        expect(static_cast<int32_t>(port.x_zoom)).to_equal(static_cast<int32_t>(r.song_zoom()));
		        expect(static_cast<int32_t>(port.x_zoom)).to_equal(3);
		        expect(port.scroll_before_following).to_equal(TickProbe::remembered_scroll());
		        expect(port.scroll_before_following).to_equal(-1);
		        Rig::hold_x_enc(false);
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
		        const bool legacy_moved = audioClipView.scrollRightToEndOfLengthIfNecessary(s.loop_length);
		        expect(port_moved).to_equal(legacy_moved);
		        expect(port.x_scroll).to_equal(r.song_scroll());
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
		        const bool legacy_moved = audioClipView.scrollRightToEndOfLengthIfNecessary(s.loop_length);
		        expect(port_moved).to_equal(legacy_moved);
		        expect(port_moved).to_be_false();
		        expect(port.x_scroll).to_equal(r.song_scroll());
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
		        const bool legacy_moved = audioClipView.scrollLeftIfTooFarRight(600);
		        expect(port_moved).to_equal(legacy_moved);
		        expect(port_moved).to_be_true();
		        expect(port.x_scroll).to_equal(r.song_scroll());
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
		        const bool legacy_moved = audioClipView.zoomToMax();
		        expect(port_moved).to_equal(legacy_moved);
		        expect(static_cast<int32_t>(port.x_zoom)).to_equal(static_cast<int32_t>(r.song_zoom()));
		        expect(port.x_scroll).to_equal(r.song_scroll());
		        expect(static_cast<int32_t>(port.x_zoom)).to_equal(48);
		        expect(port.x_scroll).to_equal(0);
		        currentUIMode = UI_MODE_NONE;

		        // `inOnly` refuses to zoom OUT: from 12, a max zoom of 48 is further out, so nothing moves.
		        r.seed(s);
		        TimelineState port_in_only = r.port();
		        const bool port_in_only_moved = port_in_only.zoom_to_max(r.max_zoom(), true);

		        r.seed(s);
		        const bool legacy_in_only_moved = audioClipView.zoomToMax(true);
		        expect(port_in_only_moved).to_equal(legacy_in_only_moved);
		        expect(port_in_only_moved).to_be_false();
		        expect(static_cast<int32_t>(port_in_only.x_zoom)).to_equal(static_cast<int32_t>(r.song_zoom()));
		        currentUIMode = UI_MODE_NONE;
	        });

	    // ---- the bars/beats decomposition ----------------------------------------------------------

	    it(
	        "decomposes into the same bars, beats and sub-beats displayNumberOfBarsAndBeats shows", _ {
		        // Legacy's decomposition never leaves the function -- it is `sprintf`ed straight into
		        // `display->popupTextTemporary` (`timeline_view.cpp:262-264`). So both sides are compared
		        // through that same popup: legacy's own call, and the port's three numbers formatted the
		        // same way and pushed through the same `display`. Anything the port got wrong shows up as
		        // different pixels.
		        Rig r;
		        const Sentinel s;
		        r.seed(s);
		        const uint32_t one_bar = r.f.song->getBarLength();

		        for (uint32_t number : {0U, 1U, 96U, 97U, 191U, 384U, 767U}) {
			        deluge::hid::display::OLED::popup.clear();
			        audioClipView.displayNumberOfBarsAndBeats(number, s.x_zoom, true, "FAR");
			        const std::string legacy = popup_pbm();

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

		        r.seed(s);
		        r.f.clip->lastProcessedPos = 96 + 5 * 6;
		        TickProbe::set_remembered_scroll(-1);
		        const int32_t legacy = (audioClipView.*TickProbe::kGetTickSquare)();
		        expect(ported.square).to_equal(legacy);
		        expect(ported.square).to_equal(5);
		        expect(ported.scrolled).to_be_false();
		        expect(port.x_scroll).to_equal(r.song_scroll());
	        });

	    it(
	        "restores the remembered scroll when the follow stops, legacy's xZoom comparison and all", _ {
		        // `clip_view.cpp:376-386`, the arm that stops following. Its `if (newXScroll !=
		        // currentSong->xZoom[NAVIGATION_CLIP])` compares a scroll against the ZOOM -- see the note
		        // at the top of `timeline.cpp`. Both sides are given the same remembered scroll (6, which
		        // is deliberately NOT the zoom) and both must restore it.
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

		        r.seed(s);
		        r.f.clip->lastProcessedPos = 0;
		        TickProbe::set_remembered_scroll(6);
		        (void)(audioClipView.*TickProbe::kGetTickSquare)();
		        expect(ported.scrolled).to_be_true();
		        expect(port.x_scroll).to_equal(r.song_scroll());
		        expect(port.x_scroll).to_equal(6);
		        expect(port.scroll_before_following).to_equal(TickProbe::remembered_scroll());
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

		        r.seed(s);
		        r.f.clip->lastProcessedPos = 0;
		        TickProbe::set_remembered_scroll(12);
		        (void)(audioClipView.*TickProbe::kGetTickSquare)();
		        expect(not_restored.scrolled).to_be_false();
		        expect(suppressed.x_scroll).to_equal(r.song_scroll());
		        expect(suppressed.x_scroll).to_equal(480);
		        expect(suppressed.scroll_before_following).to_equal(TickProbe::remembered_scroll());
		        currentUIMode = UI_MODE_NONE;
	        });

	    // ---- the Song seam -------------------------------------------------------------------------

	    it(
	        "adopts and publishes the clip navigation slot, and only that slot", _ {
		        Rig r;
		        r.f.song->xScroll[NAVIGATION_CLIP] = 96;
		        r.f.song->xZoom[NAVIGATION_CLIP] = 6;
		        r.f.song->xScroll[NAVIGATION_ARRANGEMENT] = 4242;
		        r.f.song->xZoom[NAVIGATION_ARRANGEMENT] = 99;
		        r.f.song->tripletsOn = true;
		        r.f.song->tripletsLevel = 8;

		        TimelineState port;
		        port.adopt(*r.f.song);
		        expect(port.x_scroll).to_equal(96);
		        expect(static_cast<int32_t>(port.x_zoom)).to_equal(6);
		        expect(port.triplets_on).to_be_true();
		        expect(static_cast<int32_t>(port.triplets_level)).to_equal(8);

		        port.x_scroll = 192;
		        port.x_zoom = 12;
		        port.publish(*r.f.song);
		        expect(r.f.song->xScroll[NAVIGATION_CLIP]).to_equal(192);
		        expect(static_cast<int32_t>(r.f.song->xZoom[NAVIGATION_CLIP])).to_equal(12);
		        expect(r.f.song->xScroll[NAVIGATION_ARRANGEMENT]).to_equal(4242);
		        expect(static_cast<int32_t>(r.f.song->xZoom[NAVIGATION_ARRANGEMENT])).to_equal(99);
	        });

	    it(
	        "focus_regained forgets the remembered scroll, as ClipNavigationTimelineView does", _ {
		        TimelineState port;
		        port.scroll_before_following = 96;
		        port.focus_regained();
		        expect(port.scroll_before_following).to_equal(-1);
	        });
    });
// NOLINTEND(readability-function-cognitive-complexity)

DELUGE_SPEC(timeline_spec, timeline_spec_body)
