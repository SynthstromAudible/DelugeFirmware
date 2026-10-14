#include "support/frames.h"
#include "ui/core/frame_context.h"
#include "ui/grid/effects.h"
#include "ui/oled/columns.h"
#include "ui/oled/list.h"
#include "ui/oled/popup.h"
#include "ui/oled/scroll_text.h"
#include "ui/oled/values.h"

#include "../support/spec_main.h"
#include "cppspec.hpp"

#include <span>
#include <string_view>
#include <vector>

using namespace deluge::ui;
using deluge::gfx::oled::FontId;
using deluge::ui::grid::gesture;
using deluge::ui::grid::GestureEvent;
using deluge::ui::grid::GridFrame;
using deluge::ui::grid::HoldPolicy;
using deluge::ui::grid::root_grid_frame;
using deluge::ui::oled::Column;
using deluge::ui::oled::columns;
using deluge::ui::oled::enum_value;
using deluge::ui::oled::list;
using deluge::ui::oled::ListRow;
using deluge::ui::oled::menu_rect;
using deluge::ui::oled::OledFrame;
using deluge::ui::oled::root_frame;
using deluge::ui::oled::scroll_text;
using deluge::ui::oled::working;
using Rect = deluge::gfx::oled::Rect;
using GridRect = deluge::gfx::grid::Rect;
namespace uitest = deluge::ui::test;

namespace {
/// One column of a horizontal menu whose value is an `enum_value`: a caller-supplied slot
/// renderer built out of a widget that has no `focused` parameter of its own, so an unfocused
/// frame is the only thing that can keep it inert.
struct EnumSlot {
	std::span<const std::string_view> names;
	int32_t index = 0;
	ListState state{};
	ScrollText scroll{};

	static void render(OledFrame& slot, bool /*focused*/, void* ctx) {
		auto& self = *static_cast<EnumSlot*>(ctx);
		enum_value(slot, slot.bounds(), self.index, self.names, self.state, self.scroll);
	}
};
} // namespace

// The input-consumption contract of ui/core/frame_context.h, from the outside:
//
//   * a frame's accessors are the input, and they pass it through unchanged while focused;
//   * focus is an `InputMask`: a region sees exactly the inputs its parent granted it, so
//     `region(rect, false)` draws without consuming, `region(rect, InputMask{.turn = false})`
//     hands on everything but the turn, and narrowing is the only direction available;
//   * a widget that consumes an input records a claim on the tick's context, which is how the
//     shell learns what is left over for the scene;
//   * pad input is positional and is *not* masked: an unfocused grid region still reports its
//     presses, because a press names its target by where it landed -- and neither is the clock,
//     so an unfocused region animates, times its gestures and arms the gate as it always did;
//   * `bounds()` is what a region asked for and `visible()` is what its parent granted.
//
// Everything here goes through the rigs in support/frames.h, which build one `FrameContext` per
// rig -- exactly what the shell will build per tick.

// clang-format off
describe frame_context("ui::FrameContext", $ {
	it("passes every input through to the frame's accessors", _ {
		uitest::Rig48 r;
		r.input.turn = 1;
		r.input.edit[0] = -2;
		r.input.edit[1] = 3;
		r.input.horizontal = -1;
		r.input.vertical = 4;
		r.input.select_pressed = true;
		r.input.back_pressed = true;
		r.input.shift = true;
		r.input.now_ms = 1234;

		expect(int(r.frame.turn())).to_equal(1);
		expect(int(r.frame.edit(0))).to_equal(-2);
		expect(int(r.frame.edit(1))).to_equal(3);
		expect(int(r.frame.edit(2))).to_equal(0);      // no such encoder: 0, never out of bounds
		expect(int(r.frame.horizontal())).to_equal(-1);
		expect(int(r.frame.vertical())).to_equal(4);
		expect(r.frame.select_pressed()).to_be_true();
		expect(r.frame.back_pressed()).to_be_true();
		expect(r.frame.shift()).to_be_true();
		expect(r.frame.now_ms()).to_equal(uint32_t(1234));
		// The root frame holds every input. Reading is claiming (rule 2): every accessor read
		// above claimed its own input on this frame, except edit(2) -- there is no third encoder to
		// own, so it neither reads nor claims anything. now_ms()/shift() are never masked and never
		// claimed; they are not controls.
		expect(r.frame.focused()).to_be_true();
		expect(r.frame.mask == InputMask::all()).to_be_true();
		expect(r.ctx.claims.turn).to_be_true();
		expect(r.ctx.claims.edit0).to_be_true();
		expect(r.ctx.claims.edit1).to_be_true();
		expect(r.ctx.claims.horizontal).to_be_true();
		expect(r.ctx.claims.vertical).to_be_true();
		expect(r.ctx.claims.select).to_be_true();
		expect(r.ctx.claims.back).to_be_true();
		expect(r.ctx.claims.double_claim).to_be_false();
	});

	it("presents zero input to an unfocused region", _ {
		uitest::Rig48 r;
		r.input.turn = 1;
		r.input.edit[0] = 1;
		r.input.horizontal = 1;
		r.input.vertical = 1;
		r.input.select_pressed = true;
		r.input.back_pressed = true;
		r.input.shift = true;
		r.input.now_ms = 500;

		r.frame.region(Rect{0, 0, 64, 20}, false, [&](OledFrame& f) {
			expect(f.focused()).to_be_false();
			expect(int(f.turn())).to_equal(0);
			expect(int(f.edit(0))).to_equal(0);
			expect(int(f.horizontal())).to_equal(0);
			expect(int(f.vertical())).to_equal(0);
			expect(f.select_pressed()).to_be_false();
			expect(f.back_pressed()).to_be_false();
			// ... but not the clock or the modifier: `now_ms` is the tick's own time, not an
			// input a control owns, and `shift` modifies a gesture rather than being one. An
			// unfocused region still animates, still times its gestures and still arms the gate
			// with a real deadline (ui/core/frame_context.h rule 3).
			expect(f.shift()).to_be_true();
			expect(f.now_ms()).to_equal(uint32_t(500));
			// Claims are no-ops too: a widget that consumed nothing owns nothing.
			f.claim_turn();
			f.claim_select();
			f.claim_edit(0);
			f.claim_back();
		});
		expect(r.ctx.claims.any()).to_be_false();
		// The parent still sees everything: masking is per frame, not a mutation of the snapshot.
		expect(int(r.frame.turn())).to_equal(1);
	});

	it("lets a region narrow the input mask but never widen it", _ {
		uitest::Rig48 r;
		r.input.turn = 1;
		r.input.edit[0] = 1;
		r.input.select_pressed = true;
		r.frame.region(Rect{0, 0, 64, 20}, false, [&](OledFrame& outer) {
			// `true` here means "inherit", and the parent has nothing to hand down.
			outer.region(Rect{0, 0, 32, 10}, true, [&](OledFrame& inner) {
				expect(inner.focused()).to_be_false();
				expect(int(inner.turn())).to_equal(0);
			});
			// Nor can a child ask for one input back by name.
			outer.region(Rect{0, 0, 32, 10}, InputMask::all(), [&](OledFrame& inner) {
				expect(inner.mask == InputMask::none()).to_be_true();
				expect(int(inner.edit(0))).to_equal(0);
			});
			// The default overload is the same "inherit".
			outer.region(Rect{0, 0, 32, 10}, [&](OledFrame& inner) { expect(inner.focused()).to_be_false(); });
		});

		// A *partial* mask: everything but the turn, which is what `columns` grants its focused
		// column. Masking is per input, so the rest arrives untouched.
		r.frame.region(Rect{0, 0, 64, 20}, InputMask{.turn = false}, [&](OledFrame& f) {
			expect(f.focused()).to_be_false();          // not everything, so not "focused"
			expect(int(f.turn())).to_equal(0);
			expect(int(f.edit(0))).to_equal(1);
			expect(f.select_pressed()).to_be_true();
			f.claim_turn();                             // not granted: a no-op ...
			f.claim_edit(0);                            // ... this one lands
		});
		expect(r.ctx.claims.turn).to_be_false();
		expect(r.ctx.claims.edit0).to_be_true();

		r.frame.region(Rect{0, 0, 64, 20}, [&](OledFrame& f) {
			expect(f.focused()).to_be_true();   // a full mask is handed down by default
			expect(int(f.turn())).to_equal(1);
		});
	});

	it("records what a focused widget consumed, and nothing for an unfocused one", _ {
		const ListRow rows[] = {{"ONE"}, {"TWO"}, {"THREE"}};
		uitest::Rig48 r;
		ListState st;
		ScrollText sc;
		r.input.turn = 1;
		const Response moved = list(r.frame, menu_rect(r.frame), st, rows, sc);
		expect(moved.changed).to_be_true();
		expect(st.cursor).to_equal(int16_t(1));
		// `list` owns the select encoder and its press: both are claimed on a frame it rendered
		// focused, whether or not the detent moved anything.
		expect(r.ctx.claims.turn).to_be_true();
		expect(r.ctx.claims.select).to_be_true();
		expect(r.ctx.claims.any()).to_be_true();
		// ... and nothing it does not own.
		expect(r.ctx.claims.edit0).to_be_false();
		expect(r.ctx.claims.edit1).to_be_false();
		expect(r.ctx.claims.horizontal).to_be_false();
		expect(r.ctx.claims.back).to_be_false();

		// A list rendered unfocused claims nothing at all, on a context of its own.
		uitest::Rig48 idle;
		ListState idle_st;
		ScrollText idle_sc;
		idle.input.turn = 1;
		idle.frame.region(idle.frame.bounds(), false,
		                  [&](OledFrame& f) { list(f, menu_rect(f), idle_st, rows, idle_sc); });
		expect(idle.ctx.claims.any()).to_be_false();
	});

	it("does not move a list rendered in an unfocused region", _ {
		const ListRow rows[] = {{"ONE"}, {"TWO"}, {"THREE"}};
		uitest::Rig48 r;
		ListState st;
		ScrollText sc;
		r.input.turn = 2;
		r.input.select_pressed = true;
		Response resp{};
		r.frame.region(r.frame.bounds(), false, [&](OledFrame& f) {
			resp = list(f, menu_rect(f), st, rows, sc);
		});
		expect(st.cursor).to_equal(int16_t(0));   // the detents never reached it
		expect(resp.changed).to_be_false();
		expect(resp.entered).to_be_false();       // nor did the press
		// It still *drew*: the first row's highlight is on the canvas exactly as when focused.
		expect(r.lit(0, 20)).to_be_true();
	});

	it("moves only the focused one of two lists sharing a tick", _ {
		const ListRow rows[] = {{"ONE"}, {"TWO"}, {"THREE"}};
		uitest::Rig64 r;
		ListState left;
		ListState right;
		ScrollText left_sc;
		ScrollText right_sc;
		r.input.turn = 1;
		r.frame.region(Rect{0, 0, 64, 64}, true, [&](OledFrame& f) {
			list(f, Rect{0, 0, 64, 30}, left, rows, left_sc);
		});
		r.frame.region(Rect{64, 0, 64, 64}, false, [&](OledFrame& f) {
			list(f, Rect{0, 0, 64, 30}, right, rows, right_sc);
		});
		expect(left.cursor).to_equal(int16_t(1));
		expect(right.cursor).to_equal(int16_t(0));
		expect(r.ctx.claims.turn).to_be_true();   // claimed once, by the focused one
	});

	it("keeps pad input positional: an unfocused grid region still reports presses", _ {
		uitest::GridRig r;
		PadEvent ev[] = {{10, 2, 88, true}};
		r.input.pads = ev;
		r.input.held_pads.set(11, 2, true);
		r.input.turn = 1;
		r.frame.region(GridRect{8, 0, 8, 8}, false, [&](GridFrame& f) {
			expect(f.focused()).to_be_false();
			expect(int(f.turn())).to_equal(0);          // encoders are masked ...
			const Response hit = f.pad(2, 2, RGB{1, 2, 3});
			expect(hit.pressed).to_be_true();           // ... pads are not
			expect(int(hit.velocity)).to_equal(88);
			expect(f.pad(3, 2, RGB{4, 5, 6}).held).to_be_true();
			expect(f.local_events().size()).to_equal(size_t(1));
			expect(int(f.local_events()[0].x)).to_equal(2);
			expect(f.first_press_in(GridRect{0, 0, 8, 8}).has_value()).to_be_true();
			expect(f.held_pads().test(11, 2)).to_be_true();
			expect(f.peek(2, 2)).to_equal(RGB{1, 2, 3});
		});
		expect(r.grid.at(10, 2)).to_equal(RGB{1, 2, 3});   // and it painted, as always
	});

	it("reports the requested rect as bounds and the granted one as visible", _ {
		uitest::GridRig r;
		expect(r.frame.bounds()).to_equal(GridRect{0, 0, 18, 8});
		expect(r.frame.visible()).to_equal(GridRect{0, 0, 18, 8});   // nothing clips the root

		r.frame.region(GridRect{14, 4, 8, 8}, [&](GridFrame& f) {
			// Asked for 8x8 from (14,4); the 18x8 grid grants 4x4 of it.
			expect(f.bounds()).to_equal(GridRect{0, 0, 8, 8});
			expect(f.visible()).to_equal(GridRect{0, 0, 4, 4});
			// Local coordinates are measured from the *requested* corner either way, so a pad
			// inside the granted part lands where the component thinks it does ...
			expect(f.pad(3, 3, RGB{9, 9, 9}).pressed).to_be_false();
			// ... and one outside it is dropped rather than wrapped onto another pad.
			f.pad(5, 0, RGB{7, 7, 7});
		});
		expect(r.grid.at(17, 7)).to_equal(RGB{9, 9, 9});
		expect(r.grid.at(0, 4)).to_equal(RGB{0, 0, 0});

		// An OLED region overhanging its parent answers the same way.
		uitest::Rig48 o;
		o.frame.region(Rect{120, 0, 20, 20}, [&](OledFrame& f) {
			expect(f.bounds()).to_equal(Rect{0, 0, 20, 20});
			expect(f.visible()).to_equal(Rect{0, 0, 8, 20});
		});
	});

	it("keeps the clock running in an unfocused region: a label still scrolls", _ {
		// `scroll_text` reads `now_ms()`, and a masked clock would pin it
		// at 0 for ever, so a too-wide label in an unfocused pane would never ping-pong.
		const std::string_view label = "A VERY LONG LABEL THAT CANNOT FIT";
		const Rect box{0, 14, 40, 9};
		uitest::Rig48 r;
		ScrollText st;
		r.begin_tick(0);
		r.frame.region(r.frame.bounds(), false, [&](OledFrame& f) { scroll_text(f, box, label, FontId::Apple7, st); });
		expect(int(st.offset)).to_equal(0);          // the first tick only arms the pause
		r.begin_tick(kScrollTime);
		r.frame.region(r.frame.bounds(), false, [&](OledFrame& f) { scroll_text(f, box, label, FontId::Apple7, st); });
		expect(int(st.offset) > 0).to_be_true();     // ... and the next one steps
	});

	it("arms the gate from an unfocused region with a deadline in the future", _ {
		// A masked clock would compute `now = 0` and install an absolute deadline of a few
		// hundred ms -- permanently in the past on a real clock, so `needs_frame()` would fire on
		// every tick from then on, at full rate, for the whole surface.
		uitest::Rig48 r;
		Blink blink;
		r.begin_tick(100000);
		r.frame.region(r.frame.bounds(), false, [&](OledFrame& f) { working(f, f.bounds(), "Loading", blink); });
		expect(r.gate.next_deadline().has_value()).to_be_true();
		expect(*r.gate.next_deadline() > uint32_t(100000)).to_be_true();
	});

	it("reads pad gestures in an unfocused region exactly as in a focused one", _ {
		// Pad events are never masked (rule 4), so their *interpretation* must not be either: a
		// masked clock made every gesture a Tap, because press and release both read now == 0.
		const auto script = [](bool focused) {
			uitest::GridRig r;
			Hold hold;
			int32_t sx = 0;
			int32_t sy = 0;
			std::vector<GestureEvent> seen;
			const auto step = [&](uint32_t now, std::span<const PadEvent> pads) {
				r.begin_tick(now);
				r.input.pads = pads;
				GridFrame root = root_grid_frame(r.grid, r.ctx);
				root.region(GridRect{0, 0, 18, 8}, focused,
				            [&](GridFrame& f) { seen.push_back(gesture(f, hold, HoldPolicy{}, sx, sy)); });
			};
			const PadEvent press[] = {{5, 5, 100, true}};
			const PadEvent release[] = {{5, 5, 0, false}};
			step(1000, press);
			step(1400, {});      // still inside the 500ms window
			step(1700, {});      // past it: Hold, exactly once
			step(1800, release); // released after the window: Release, not Tap
			return seen;
		};
		const std::vector<GestureEvent> focused = script(true);
		const std::vector<GestureEvent> unfocused = script(false);
		expect(focused.size()).to_equal(size_t(4));
		expect(focused[2] == GestureEvent::Hold).to_be_true();      // not vacuous: the clock ran
		expect(focused[3] == GestureEvent::Release).to_be_true();
		expect(unfocused == focused).to_be_true();
	});

	it("clears claims at the tick boundary", _ {
		const ListRow rows[] = {{"ONE"}, {"TWO"}};
		uitest::Rig48 r;
		ListState st;
		ScrollText sc;
		r.begin_tick(0);
		list(r.frame, menu_rect(r.frame), st, rows, sc);
		expect(r.ctx.claims.turn).to_be_true();
		expect(r.ctx.tick).to_equal(uint32_t(1));

		// A context that outlives a tick must not carry its claims over: a shell that reused one
		// would withhold the encoder from the scene for ever after the first tick that claimed it.
		r.begin_tick(20);
		expect(r.ctx.claims.any()).to_be_false();
		expect(r.ctx.claims == InputClaims{}).to_be_true();
		expect(r.ctx.tick).to_equal(uint32_t(2));
		expect(r.ctx.tick_ms).to_equal(uint32_t(20));
	});

	it("flags two focused controls claiming one input as a double claim", _ {
		const ListRow rows[] = {{"ONE"}, {"TWO"}, {"THREE"}};
		uitest::Rig64 r;
		ListState a;
		ListState b;
		ScrollText a_sc;
		ScrollText b_sc;
		r.begin_tick(0);
		r.input.turn = 1;
		// A claim records ownership; it does not arbitrate. Two focused regions both see the
		// detent and both move -- keeping one focused region per input is the scene's job, and
		// `double_claim` is the diagnostic for getting it wrong.
		r.frame.region(Rect{0, 0, 64, 64}, [&](OledFrame& f) { list(f, Rect{0, 0, 64, 30}, a, rows, a_sc); });
		r.frame.region(Rect{64, 0, 64, 64}, [&](OledFrame& f) { list(f, Rect{0, 0, 64, 30}, b, rows, b_sc); });
		expect(a.cursor).to_equal(int16_t(1));
		expect(b.cursor).to_equal(int16_t(1));
		expect(r.ctx.claims.double_claim).to_be_true();

		// One focused, one not: the ordinary case, and the flag stays down.
		uitest::Rig64 ok;
		ListState only;
		ListState idle;
		ScrollText only_sc;
		ScrollText idle_sc;
		ok.begin_tick(0);
		ok.input.turn = 1;
		ok.frame.region(Rect{0, 0, 64, 64}, true, [&](OledFrame& f) { list(f, Rect{0, 0, 64, 30}, only, rows, only_sc); });
		ok.frame.region(Rect{64, 0, 64, 64}, false, [&](OledFrame& f) { list(f, Rect{0, 0, 64, 30}, idle, rows, idle_sc); });
		expect(ok.ctx.claims.double_claim).to_be_false();
		expect(ok.ctx.claims.turn).to_be_true();
	});

	it("no longer touches the turn at all, and still masks it out of the focused slot", _ {
		// Re-derived (E-note-family task 2): `columns` used to spend the select encoder walking
		// the column focus; it no longer moves focus on a turn at all, or claims the turn itself --
		// a scene reads and claims it *before* calling `columns`, to edit the focused column's
		// value (`MenuScene::render_horizontal`, the port of `HorizontalMenu::selectEncoderAction`,
		// `horizontal_menu.cpp:309-326`). The slot mask is still `none()` for every column but the
		// focused one, and still `{.turn = false}` for that one -- now so the scene's own edit,
		// already applied above `columns`, cannot also reach the slot's widget a second time
		// (`ui/oled/columns.cpp`'s note on `render_slot`) -- while the column's own value widget
		// still owns the gold knobs, the press and back.
		const std::string_view names[] = {"AAA", "BBB", "CCC"};
		uitest::Rig48 r;
		EnumSlot left{.names = names};
		EnumSlot right{.names = names};
		const Column cols[] = {
		    Column{.label = "L", .render = &EnumSlot::render, .ctx = &left},
		    Column{.label = "R", .render = &EnumSlot::render, .ctx = &right},
		};
		ColumnsState state;
		r.begin_tick(0);
		r.input.turn = 1;
		columns(r.frame, r.frame.bounds(), state, cols, "MENU");

		expect(int(state.focused)).to_equal(0);   // no walk: focus never moves on a turn
		expect(left.index).to_equal(0);           // the focused slot's own turn is still masked out
		expect(right.index).to_equal(0);          // ... and the unfocused one is masked out entirely
		expect(r.ctx.claims.turn).to_be_false();  // and `columns` claims nothing here -- the caller's job now
		expect(r.ctx.claims.double_claim).to_be_false();

		// The press does reach the focused column's widget: `columns` claims the select only for a
		// submenu column, and these are value columns, so `enum_value` claims it in the slot.
		r.begin_tick(10);
		r.input.turn = 0;
		r.input.select_pressed = true;
		columns(r.frame, r.frame.bounds(), state, cols, "MENU");
		expect(r.ctx.claims.select).to_be_true();
		expect(r.ctx.claims.double_claim).to_be_false();

		// A submenu column is the other half of that rule: there the press is `columns`'s own.
		uitest::Rig48 sub;
		EnumSlot only{.names = names};
		const Column submenu[] = {
		    Column{.label = "S", .is_submenu = true, .render = &EnumSlot::render, .ctx = &only},
		};
		ColumnsState sub_state;
		sub.begin_tick(0);
		sub.input.select_pressed = true;
		const Response entered = columns(sub.frame, sub.frame.bounds(), sub_state, submenu, "MENU");
		expect(entered.entered).to_be_true();
		expect(sub.ctx.claims.select).to_be_true();
		// The slot's `enum_value` claims it too -- a submenu column whose renderer also consumes
		// the press is exactly the shape `double_claim` is meant to surface.
		expect(sub.ctx.claims.double_claim).to_be_true();
	});

	it("makes a grid claim a no-op in an unfocused region, at any depth", _ {
		uitest::GridRig r;
		r.begin_tick(0);
		r.input.turn = 1;
		r.frame.region(GridRect{0, 0, 8, 8}, false, [&](GridFrame& outer) {
			// Focus narrows on the grid exactly as on the OLED, and a nested `true` cannot undo it.
			outer.region(GridRect{0, 0, 4, 4}, true, [&](GridFrame& inner) {
				expect(inner.focused()).to_be_false();
				expect(int(inner.turn())).to_equal(0);
				inner.claim_turn();
				inner.claim_select();
			});
			outer.claim_edit(0);
		});
		expect(r.ctx.claims.any()).to_be_false();
		expect(r.ctx.claims.double_claim).to_be_false();

		// ... and a focused grid region records them, so the no-op above is not vacuous.
		r.frame.region(GridRect{0, 0, 8, 8}, [&](GridFrame& f) { f.claim_turn(); });
		expect(r.ctx.claims.turn).to_be_true();
	});

	it("claims on read, idempotently per frame, and only trips double_claim between frames (I3)", _ {
		uitest::Rig64 r;
		r.input.turn = 1;

		// One frame reading the same input three times: the first read claims it, the other two
		// are no-ops on that same instance -- not a widget claiming twice by mistake.
		r.frame.region(Rect{0, 0, 64, 64}, [&](OledFrame& f) {
			expect(int(f.turn())).to_equal(1);
			expect(int(f.turn())).to_equal(1);
			expect(int(f.turn())).to_equal(1);
		});
		expect(r.ctx.claims.turn).to_be_true();
		expect(r.ctx.claims.double_claim).to_be_false();

		// A widget that reads an input without ever calling claim_turn() still shows up in
		// ctx.claims: before this rule only an explicit claim_*() call was
		// visible here, so a hand-written reader of f.turn() (or f.back_pressed(), etc.) looked
		// exactly like one that had claimed nothing at all.
		uitest::Rig64 r2;
		r2.input.turn = 1;
		r2.frame.region(Rect{0, 0, 64, 64}, [&](OledFrame& f) { (void)f.turn(); });
		expect(r2.ctx.claims.turn).to_be_true();

		// Two different frames, each granted the turn, each only *reading* it (neither calls
		// claim_turn()): double_claim still fires. It is a property of two frames, never of one
		// frame's repeated reads.
		uitest::Rig64 r3;
		r3.input.turn = 1;
		r3.frame.region(Rect{0, 0, 64, 64}, [&](OledFrame& f) { (void)f.turn(); });
		r3.frame.region(Rect{64, 0, 64, 64}, [&](OledFrame& f) { (void)f.turn(); });
		expect(r3.ctx.claims.double_claim).to_be_true();
	});

	it("root_frame/root_grid_frame take the shell's InputMask (I2): none() leaves a list inert but "
	   "pads still report", _ {
		const ListRow rows[] = {{"ONE"}, {"TWO"}, {"THREE"}};
		uitest::Rig48 r;
		r.input.turn = 1;
		r.input.select_pressed = true;
		ListState st;
		ScrollText sc;
		// What the shell grants a scene sitting under an overlay: the root itself, not a region
		// below it, is built with none(). Everything over it draws exactly as it would focused,
		// but reads 0/false and claims nothing -- the same shape as an unfocused region(), one
		// level higher, where there is no parent frame to call region() on.
		OledFrame inert = root_frame(r.canvas, r.style, r.ctx, InputMask::none());
		const Response resp = list(inert, menu_rect(inert), st, rows, sc);
		expect(st.cursor).to_equal(int16_t(0));    // the turn never reached it
		expect(resp.changed).to_be_false();
		expect(resp.entered).to_be_false();        // nor did the press
		expect(r.ctx.claims.any()).to_be_false();
		expect(r.lit(0, 20)).to_be_true();         // it still drew the first row's highlight

		// The grid side: pads are never masked (rule 4), so a root built with none() reports every
		// press exactly as one built with all() would, even though the same root's encoders read 0.
		uitest::GridRig g;
		PadEvent ev[] = {{2, 2, 100, true}};
		g.input.pads = ev;
		g.input.turn = 1;
		GridFrame none_root = root_grid_frame(g.grid, g.ctx, InputMask::none());
		expect(int(none_root.turn())).to_equal(0);
		const Response hit = none_root.pad(2, 2, RGB{9, 9, 9});
		expect(hit.pressed).to_be_true();
		expect(g.grid.at(2, 2)).to_equal(RGB{9, 9, 9});
		expect(g.ctx.claims.any()).to_be_false();
	});
});
DELUGE_SPEC(ui_frame_context_spec, frame_context)
