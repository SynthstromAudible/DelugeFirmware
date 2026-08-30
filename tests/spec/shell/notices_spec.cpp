#include "../gfx/support/golden.h"
#include "../ui/support/frames.h"
#include "shell/notices.h"
#include "shell/scene_stack.h"
#include "ui/grid/frame.h"

#include "cppspec.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>

#include "../support/spec_main.h"
using namespace deluge::ui::shell;
using deluge::notify::Notice;
using deluge::notify::NoticeQueue;

namespace {

/// @brief A `notify::Notice` with \p text and \p popup_type copied into its own 48-byte buffer,
///        of kind \p kind.
Notice make_notice(Notice::Kind kind, std::string_view text = {}, uint8_t popup_type = 0, int16_t error_code = 0,
                   Notice::Severity severity = Notice::Severity::Info) {
	Notice n{};
	n.kind = kind;
	n.popup_type = popup_type;
	n.error_code = error_code;
	n.severity = severity;
	const size_t len = std::min(text.size(), sizeof(n.text) - 1);
	std::memcpy(n.text, text.data(), len);
	n.text[len] = '\0';
	return n;
}

/// Legacy's `Error::NONE`/`Error::ABORTED_BY_USER`, mirrored numerically on the same terms as
/// `notices.cpp`'s own `kErrorNone`/`kErrorAbortedByUser` (see that file's citation of
/// `definitions_cxx.hpp:455,474`) -- kept as a separate copy here rather than reused from
/// `notices.cpp`'s anonymous namespace, so the spec pins the *values* `Notices::drain` is
/// contractually bound to suppress, not merely whatever constant the implementation happens to
/// define.
constexpr int16_t kErrorNone = 0;
constexpr int16_t kErrorAbortedByUser = 19;

/// @brief A board profile with fixed `text`/`error_text` lookups, so `Notices::drain` has
///        something to resolve string/error ids through without reaching into l10n or `Error`.
BoardProfile fixed_lookup_profile() {
	static constexpr auto text_fn = +[](uint16_t) -> std::string_view { return "STRING"; };
	static constexpr auto error_fn = +[](int16_t) -> std::string_view { return "ERROR TEXT"; };
	BoardProfile p{};
	p.text = text_fn;
	p.error_text = error_fn;
	return p;
}

/// @brief A trivial application-shaped scene, standing in for "some scene the application
///        legitimately pushed" wherever a test needs one above a live notice -- registered at
///        `SceneKind::Boot`, a kind this Rig otherwise never uses.
///
/// Review round 1 (task 11), I4: those tests used to reuse `SceneKind::Console` for this, which
/// stopped being neutral the moment `Console` gained its own expiry-driven `remove()` (this file's
/// "glyphs are not clipped" test's sibling fix) -- a `tick()` call now excises a stand-in `Console`
/// that was never `append()`-ed to (no active lines) exactly as eagerly as a real one that expired,
/// which is correct for a real console strip and wrong for a scene standing in for something else
/// entirely.
class OtherScene final : public Scene {
public:
	SceneKind kind() const override { return SceneKind::Boot; }
	Result handle(const UiEvent&, const UiContext&) override { return Result::unhandled(); }
};

/// @brief A registry, stack, the three overlay scenes, a neutral stand-in scene and a `Notices`
///        wired over them -- the `Shell`-less assembly the brief asks for. A real
///        `notify::NoticeQueue`, constructed directly rather than via the process-global
///        `notices()`, is a separate member each spec pushes into and hands to `drain()`.
struct Rig {
	BoardProfile profile;
	PopupScene popup;
	ConsoleScene console;
	WorkingScene working;
	OtherScene other;
	std::array<Scene*, 5> table{}; // indices 1=Popup, 2=Console, 3=Working, 4=Boot (Null is 0, unused)
	SceneRegistry reg{};
	SceneStack stack{reg};
	Notices notices{stack, profile, popup, console, working};
	NoticeQueue queue;

	/// @param p The board profile `Notices` resolves ids through; the default has both lookups
	///          wired, and `BoardProfile{}` is the null-lookup board every shipping profile is
	///          today.
	explicit Rig(BoardProfile p = fixed_lookup_profile()) : profile(p) {
		table[static_cast<size_t>(SceneKind::Popup)] = &popup;
		table[static_cast<size_t>(SceneKind::Console)] = &console;
		table[static_cast<size_t>(SceneKind::Working)] = &working;
		table[static_cast<size_t>(SceneKind::Boot)] = &other;
		reg.by_kind = std::span<Scene* const>{table};
	}

	Rig(const Rig&) = delete;
	Rig& operator=(const Rig&) = delete;
};

} // namespace

// clang-format off
describe shell_notices("shell::Notices", $ {
	it("a Text notice pushes the popup scene with the text and a General timeout", _ {
		Rig r;
		(void)r.queue.push(make_notice(Notice::Kind::Text, "SAVED"));

		r.notices.drain(r.queue, 0);

		expect(r.stack.top().kind() == SceneKind::Popup).to_be_true();
		expect(std::string(r.notices.last_popup_text())).to_equal(std::string("SAVED"));

		// Not yet expired one ms before the deadline.
		r.notices.tick(r.notices.timeout_for(1) - 1, 0);
		expect(r.stack.top().kind() == SceneKind::Popup).to_be_true();

		r.notices.tick(r.notices.timeout_for(1) + 1, 0);
		expect(r.stack.top().kind() == SceneKind::Popup).to_be_false();
	});

	it("a second notice replaces the popup's text without pushing twice", _ {
		Rig r;
		(void)r.queue.push(make_notice(Notice::Kind::Text, "FIRST"));
		r.notices.drain(r.queue, 0);
		expect(r.stack.depth()).to_equal(size_t(1));

		(void)r.queue.push(make_notice(Notice::Kind::Text, "SECOND"));
		r.notices.drain(r.queue, 10);

		expect(r.stack.depth()).to_equal(size_t(1)); // still just the one popup, not pushed twice
		expect(r.stack.top().kind() == SceneKind::Popup).to_be_true();
		expect(std::string(r.notices.last_popup_text())).to_equal(std::string("SECOND"));
	});

	it("Error uses error_text and popup_type 0 maps to General", _ {
		Rig r;
		(void)r.queue.push(make_notice(Notice::Kind::Error, {}, /*popup_type=*/0, /*error_code=*/7));

		r.notices.drain(r.queue, 0);

		expect(r.stack.top().kind() == SceneKind::Popup).to_be_true();
		expect(std::string(r.notices.last_popup_text())).to_equal(std::string("ERROR TEXT"));

		// Popup_type 0 mapped to General (Framed): rendering shows a border, unlike Notification.
		deluge::ui::test::Rig48 general;
		r.popup.render_oled(SurfaceRole::Main, general.frame);
		deluge::ui::test::Rig48 notification;
		r.popup.show("ERROR TEXT", /*popup_type=*/10, 0, r.notices.timeout_for(10));
		r.popup.render_oled(SurfaceRole::Main, notification.frame);
		expect(general.pbm() == notification.pbm()).to_be_false();
	});

	it("Error suppresses NONE and ABORTED_BY_USER, exactly as OLED::displayError does", _ {
		// oled.cpp:1173-1180: `switch (error) { case Error::NONE: case Error::ABORTED_BY_USER:
		// return; ... }` -- displayPopup is never reached for either code.
		Rig r;
		(void)r.queue.push(make_notice(Notice::Kind::Error, {}, /*popup_type=*/0, kErrorNone));
		r.notices.drain(r.queue, 0);
		expect(r.stack.depth()).to_equal(size_t(0));

		(void)r.queue.push(make_notice(Notice::Kind::Error, {}, /*popup_type=*/0, kErrorAbortedByUser));
		r.notices.drain(r.queue, 0);
		expect(r.stack.depth()).to_equal(size_t(0));

		// A code that is neither still shows, through the same profile.error_text lookup.
		(void)r.queue.push(make_notice(Notice::Kind::Error, {}, /*popup_type=*/0, /*error_code=*/7));
		r.notices.drain(r.queue, 0);
		expect(r.stack.top().kind() == SceneKind::Popup).to_be_true();
		expect(std::string(r.notices.last_popup_text())).to_equal(std::string("ERROR TEXT"));
	});

	it("a Warn text notice is persistent; a later notice replaces it and re-arms the timeout", _ {
		// notify_drain.cpp:137 (`flashes = (severity == Warn) ? 0 : 3`) feeds
		// OLED::displayPopup's `persistent = !numFlashes` (oled.h:156), so a Warn text notice
		// never sets TimerName::DISPLAY (oled.cpp:642) and so never auto-expires.
		Rig r;
		(void)r.queue.push(make_notice(Notice::Kind::Text, "WARNING", 0, 0, Notice::Severity::Warn));
		r.notices.drain(r.queue, 0);
		expect(r.stack.top().kind() == SceneKind::Popup).to_be_true();
		expect(std::string(r.notices.last_popup_text())).to_equal(std::string("WARNING"));

		// Stays up across a tick far past the ordinary (non-persistent) timeout.
		r.notices.tick(r.notices.timeout_for(1) * 100, 0);
		expect(r.stack.top().kind() == SceneKind::Popup).to_be_true();
		expect(std::string(r.notices.last_popup_text())).to_equal(std::string("WARNING"));

		// A later, non-persistent notice replaces it and re-arms the ordinary timeout -- no
		// special "cancel persistence" step, exactly as legacy's OLED::popupText unconditionally
		// re-decides persistence on every call (oled.cpp:636-643).
		(void)r.queue.push(make_notice(Notice::Kind::Text, "SAVED"));
		r.notices.drain(r.queue, r.notices.timeout_for(1) * 100);
		expect(r.stack.depth()).to_equal(size_t(1)); // still just the one popup
		expect(std::string(r.notices.last_popup_text())).to_equal(std::string("SAVED"));

		const uint32_t now = r.notices.timeout_for(1) * 100;
		r.notices.tick(now + r.notices.timeout_for(1) + 1, 0);
		expect(r.stack.top().kind() == SceneKind::Popup).to_be_false(); // expires now that it isn't Warn
	});

	it("CancelPopup ends a still-persistent (Warn) popup too", _ {
		Rig r;
		(void)r.queue.push(make_notice(Notice::Kind::Text, "WARNING", 0, 0, Notice::Severity::Warn));
		r.notices.drain(r.queue, 0);
		r.notices.tick(1'000'000, 0); // would never expire on its own
		expect(r.stack.top().kind() == SceneKind::Popup).to_be_true();

		(void)r.queue.push(make_notice(Notice::Kind::CancelPopup));
		r.notices.drain(r.queue, 1'000'000);
		expect(r.stack.top().kind() == SceneKind::Popup).to_be_false();
	});

	it("a null text/error lookup shows the id as digits, not an empty box", _ {
		// `deluge_profile()` -- the real board -- leaves both hooks null until spec E's l10n table
		// and spec F's wiring exist, so this is the shipping path, not a corner. An empty string
		// here would draw a correctly framed, correctly sized, completely blank popup: an SD write
		// failure would be unreportable.
		Rig r{BoardProfile{}};
		Notice text = make_notice(Notice::Kind::Text);
		text.string_id = 42;
		(void)r.queue.push(text);
		r.notices.drain(r.queue, 0);
		expect(r.stack.top().kind() == SceneKind::Popup).to_be_true();
		expect(std::string(r.notices.last_popup_text())).to_equal(std::string("42"));

		(void)r.queue.push(make_notice(Notice::Kind::Error, {}, /*popup_type=*/0, /*error_code=*/7));
		r.notices.drain(r.queue, 0);
		expect(std::string(r.notices.last_popup_text())).to_equal(std::string("7"));

		// A negative error code keeps its sign rather than wrapping through the unsigned path.
		(void)r.queue.push(make_notice(Notice::Kind::Error, {}, /*popup_type=*/0, /*error_code=*/-273));
		r.notices.drain(r.queue, 0);
		expect(std::string(r.notices.last_popup_text())).to_equal(std::string("-273"));
	});

	it("the popup and console lifetimes survive the uint32 clock wrap", _ {
		// 256 ms before the millisecond clock wraps: every deadline armed here lands on the far
		// side of it, and an absolute `now_ms >= deadline` compare reads those wrapped (tiny)
		// deadlines as long past -- the popup flashes and vanishes on the next tick, and a console
		// line is skipped on its very first render.
		constexpr uint32_t kNearWrap = 0xFFFFFF00u;
		Rig r;
		(void)r.queue.push(make_notice(Notice::Kind::Text, "SAVED"));
		r.notices.drain(r.queue, kNearWrap);
		expect(r.stack.top().kind() == SceneKind::Popup).to_be_true();

		// Ten milliseconds later -- across the wrap, well inside the 800 ms lifetime.
		r.notices.tick(static_cast<uint32_t>(kNearWrap + 10), 0);
		expect(r.stack.top().kind() == SceneKind::Popup).to_be_true();
		// One millisecond short of the deadline, still up.
		r.notices.tick(static_cast<uint32_t>(kNearWrap + r.notices.timeout_for(1) - 1), 0);
		expect(r.stack.top().kind() == SceneKind::Popup).to_be_true();
		// And it does still expire once the wrapped deadline is genuinely past.
		r.notices.tick(static_cast<uint32_t>(kNearWrap + r.notices.timeout_for(1) + 1), 0);
		expect(r.stack.top().kind() == SceneKind::Popup).to_be_false();

		// The console line appended just before the wrap draws on the tick after it.
		(void)r.queue.push(make_notice(Notice::Kind::Console, "HELLO"));
		r.notices.drain(r.queue, kNearWrap);
		deluge::ui::test::Rig48 across_the_wrap;
		across_the_wrap.begin_tick(static_cast<uint32_t>(kNearWrap + 10));
		r.console.render_oled(SurfaceRole::Main, across_the_wrap.frame);
		expect(across_the_wrap.pbm() != deluge::ui::test::Rig48{}.pbm()).to_be_true();
	});

	it("an expired popup is removed in place, not everything above it", _ {
		// A popup is `input_transparent()`, so an application `Push` legitimately lands above a live
		// one -- that scene must survive the popup's own expiry, and whatever was below the popup
		// (its own base) must survive untouched too.
		Rig r;
		(void)r.queue.push(make_notice(Notice::Kind::Working, "Loading"));
		r.notices.drain(r.queue, 0);
		expect(r.stack.depth()).to_equal(size_t(1));
		expect(r.stack.top().kind() == SceneKind::Working).to_be_true();

		(void)r.queue.push(make_notice(Notice::Kind::Text, "SAVED"));
		r.notices.drain(r.queue, 0);
		expect(r.stack.depth()).to_equal(size_t(2));
		expect(r.stack.top().kind() == SceneKind::Popup).to_be_true();

		// An application scene, pushed above the live popup -- legitimate, since the popup never took
		// the input this Push responded to. `OtherScene`/`Boot`, not `Console`: `Console` now has its
		// own expiry-driven removal (I4), so a `Console` pushed here with no active lines would be
		// removed by the very `tick()` below this test means to check ONLY pops the expired popup.
		expect(r.stack.push(SceneId{SceneKind::Boot})).to_be_true();
		expect(r.stack.depth()).to_equal(size_t(3));

		r.notices.tick(r.notices.timeout_for(1) + 1, 0);
		expect(r.stack.depth()).to_equal(size_t(2));                          // only the popup is gone
		expect(r.stack.top().kind() == SceneKind::Boot).to_be_true();         // the app scene is still on top
		expect(r.stack.id_at(0) == SceneId{SceneKind::Working}).to_be_true(); // and the base survived
	});

	it("a second Text notice while a scene sits above the live popup updates it in place", _ {
		Rig r;
		(void)r.queue.push(make_notice(Notice::Kind::Text, "FIRST"));
		r.notices.drain(r.queue, 0);
		expect(r.stack.depth()).to_equal(size_t(1));

		// An application scene pushed above the live popup. `OtherScene`/`Boot`, not `Console` --
		// same reason as the test just above.
		expect(r.stack.push(SceneId{SceneKind::Boot})).to_be_true();
		expect(r.stack.depth()).to_equal(size_t(2));
		expect(r.stack.top().kind() == SceneKind::Boot).to_be_true();

		// `show_popup_with_timeout` must find the popup by scanning the whole stack, not just `top()`
		// (which now reads the stand-in scene) -- otherwise this pushes a second `SceneKind::Popup`
		// entry pointing at the same `PopupScene` instance.
		(void)r.queue.push(make_notice(Notice::Kind::Text, "SECOND"));
		r.notices.drain(r.queue, 10);

		expect(r.stack.depth()).to_equal(size_t(2));                  // unchanged: no duplicate push
		expect(r.stack.top().kind() == SceneKind::Boot).to_be_true(); // the scene above is undisturbed
		expect(std::string(r.notices.last_popup_text())).to_equal(std::string("SECOND")); // updated in place
	});

	it("every notice scene is a passive grid overlay that never takes input", _ {
		Rig r;
		// The shell picks the grid base as the topmost scene that is *not* a grid overlay and
		// clears the grid before drawing it, so a notice that claimed to be the base -- having no
		// `render_grid` of its own -- would leave the pads black. Legacy's popups, console lines
		// and working animation are OLED-only (`OLED::sendMainImage`, `oled.cpp:364-376`): the
		// pads keep showing the view underneath.
		Scene* const all[] = {&r.popup, &r.console, &r.working};
		for (Scene* scene : all) {
			expect(scene->is_grid_overlay()).to_be_true();
			expect(scene->occludes_grid()).to_be_false();
			expect(scene->input_transparent()).to_be_true();
			// Composites nothing, so dims nothing: legacy popups have no grid effect at all.
			expect(scene->dims_grid()).to_be_false();
			// None of them paints a pad: `render_grid` is the base `Scene`'s do-nothing default.
			deluge::gfx::grid::PadGrid grid{{16, 2, 8}};
			grid.set(3, 3, RGB{9, 9, 9});
			deluge::ui::InputSnapshot snap{};
			deluge::ui::RepaintGate gate;
			deluge::ui::FrameContext ctx{snap, gate};
			deluge::ui::grid::GridFrame frame = deluge::ui::grid::root_grid_frame(grid, ctx);
			scene->render_grid(frame);
			expect(grid.at(3, 3) == RGB{9, 9, 9}).to_be_true();
		}
	});

	it("Console appends and never occludes; Working arms and ClearWorking pops it", _ {
		Rig r;
		expect(r.console.oled_transparent(SurfaceRole::Main)).to_be_true();

		deluge::ui::test::Rig48 before;
		r.console.render_oled(SurfaceRole::Main, before.frame);
		(void)r.queue.push(make_notice(Notice::Kind::Console, "HELLO"));
		r.notices.drain(r.queue, 0);
		deluge::ui::test::Rig48 after;
		r.console.render_oled(SurfaceRole::Main, after.frame);
		expect(before.pbm() == after.pbm()).to_be_false(); // the appended line actually drew something

		(void)r.queue.push(make_notice(Notice::Kind::Working, "Loading"));
		r.notices.drain(r.queue, 0);
		expect(r.stack.top().kind() == SceneKind::Working).to_be_true();
		expect(r.working.armed()).to_be_true();

		(void)r.queue.push(make_notice(Notice::Kind::ClearWorking));
		r.notices.drain(r.queue, 0);
		expect(r.working.armed()).to_be_false();
		expect(r.stack.top().kind() == SceneKind::Working).to_be_true(); // drain() clears, doesn't pop

		r.notices.tick(0, 0);
		expect(r.stack.top().kind() == SceneKind::Working).to_be_false(); // tick() is what pops it
	});

	it("a console line's glyphs are not clipped -- E and F must not render identically", _ {
		// Round-1 review finding: `ConsoleScene::render_oled` drew each line into a `kLineHeight`-tall
		// region shorter than `FontId::Apple7`'s real height (`text_height_for`, 9 -- `ui/core/metrics.h`'s
		// own note that this face draws one row lower than its 7-row sibling), which clipped every
		// glyph's bottom row. `E` and `F` differ ONLY in that bottom bar, so they are the cheapest
		// possible shape probe: a set-pixel COUNT assertion (the pre-existing "actually drew
		// something" check above) cannot tell a clipped `E` from a clipped `F` -- both lose the same
		// row and come out identical -- but a full-frame comparison can, and must.
		ConsoleScene e;
		e.append("E", 0);
		deluge::ui::test::Rig48 e_frame;
		e.render_oled(SurfaceRole::Main, e_frame.frame);

		ConsoleScene f;
		f.append("F", 0);
		deluge::ui::test::Rig48 f_frame;
		f.render_oled(SurfaceRole::Main, f_frame.frame);

		expect(e_frame.pbm() != f_frame.pbm()).to_be_true();

		// And directly: the glyph's bottom row (the last row of the panel, where `render_oled`
		// anchors the newest/only line) must carry ink for a character whose bottom row is part of
		// its shape -- `E`'s closing bar -- not just SOME row above it.
		const int32_t bottom_row = e_frame.frame.bounds().h - 1;
		bool bottom_row_lit = false;
		for (int32_t x = 0; x < e_frame.frame.bounds().w; x++) {
			bottom_row_lit = bottom_row_lit || e_frame.lit(x, bottom_row);
		}
		expect(bottom_row_lit).to_be_true();
	});

	it("CancelPopup pops only a popup", _ {
		Rig r;
		// Nothing on the stack yet: CancelPopup is a no-op.
		(void)r.queue.push(make_notice(Notice::Kind::CancelPopup));
		r.notices.drain(r.queue, 0);
		expect(r.stack.depth()).to_equal(size_t(0));

		// Working on top, not a popup: still a no-op.
		(void)r.queue.push(make_notice(Notice::Kind::Working, "Loading"));
		r.notices.drain(r.queue, 0);
		expect(r.stack.depth()).to_equal(size_t(1));
		(void)r.queue.push(make_notice(Notice::Kind::CancelPopup));
		r.notices.drain(r.queue, 0);
		expect(r.stack.depth()).to_equal(size_t(1));
		expect(r.stack.top().kind() == SceneKind::Working).to_be_true();

		// Now push a popup on top of it and cancel that.
		(void)r.queue.push(make_notice(Notice::Kind::Text, "SAVED"));
		r.notices.drain(r.queue, 0);
		expect(r.stack.depth()).to_equal(size_t(2));
		expect(r.stack.top().kind() == SceneKind::Popup).to_be_true();
		(void)r.queue.push(make_notice(Notice::Kind::CancelPopup));
		r.notices.drain(r.queue, 0);
		expect(r.stack.depth()).to_equal(size_t(1));
		expect(r.stack.top().kind() == SceneKind::Working).to_be_true();
	});

	it("renders the popup goldens at both geometries", _ {
		using deluge::gfx::oled::Size;

		// "SAVED" is five monospaced Apple7 characters: 30px wide, one 9px line -- the same
		// content size tests/spec/ui/popup_spec.cpp pins its own border assertions against.
		const Size content{30, 9};

		deluge::ui::test::Rig48 saved48;
		PopupScene popup48;
		popup48.show("SAVED", /*popup_type=*/1, 0, 800);
		popup48.render_oled(SurfaceRole::Main, saved48.frame);

		// Real content, asserted before trusting the golden: the Framed border sits exactly
		// where deluge::ui::oled::popup_rect_for computes it (mirrors popup_spec.cpp's own technique),
		// and the canvas is not blank -- so a blind re-record over a broken render (wrong style,
		// missing text, an empty canvas) cannot pass silently.
		const auto box48 = deluge::ui::oled::popup_rect_for(saved48.frame, content);
		const int32_t top48 = saved48.frame.origin.y;
		expect(saved48.lit(box48.x, top48 + box48.y + 1)).to_be_true();         // left edge
		expect(saved48.lit(box48.x + box48.w, top48 + box48.y + 1)).to_be_true(); // right edge
		expect(saved48.lit(box48.x + 1, top48 + box48.y)).to_be_true();         // top edge
		expect(saved48.lit(box48.x + 1, top48 + box48.y + box48.h)).to_be_true(); // bottom edge
		expect(saved48.lit(box48.x, top48 + box48.y)).to_be_false();            // corner (SMALL rounding)
		expect(saved48.pbm() != deluge::ui::test::Rig48{}.pbm()).to_be_true();  // not blank
		expect_golden(self, "../shell/popup_saved_48.pbm", saved48.pbm());

		deluge::ui::test::Rig64 saved64;
		PopupScene popup64;
		popup64.show("SAVED", /*popup_type=*/1, 0, 800);
		popup64.render_oled(SurfaceRole::Main, saved64.frame);

		const auto box64 = deluge::ui::oled::popup_rect_for(saved64.frame, content);
		const int32_t top64 = saved64.frame.origin.y;
		expect(saved64.lit(box64.x, top64 + box64.y + 1)).to_be_true();
		expect(saved64.lit(box64.x + box64.w, top64 + box64.y + 1)).to_be_true();
		expect(saved64.lit(box64.x + 1, top64 + box64.y)).to_be_true();
		expect(saved64.lit(box64.x + 1, top64 + box64.y + box64.h)).to_be_true();
		expect(saved64.lit(box64.x, top64 + box64.y)).to_be_false();
		expect(saved64.pbm() != deluge::ui::test::Rig64{}.pbm()).to_be_true();
		expect_golden(self, "../shell/popup_saved_64.pbm", saved64.pbm());

		// role != Main is a no-op.
		deluge::ui::test::Rig48 untouched;
		popup48.render_oled(SurfaceRole::Aux, untouched.frame);
		expect(untouched.pbm()).to_equal(deluge::ui::test::Rig48{}.pbm());
	});
});
// clang-format on

DELUGE_SPEC(shell_notices_spec, shell_notices)
