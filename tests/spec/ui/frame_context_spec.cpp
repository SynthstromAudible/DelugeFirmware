#include "support/frames.h"
#include "ui/core/frame_context.h"
#include "ui/oled/list.h"

#include "../support/spec_main.h"
#include "cppspec.hpp"

using namespace deluge::ui;
using deluge::ui::grid::GridFrame;
using deluge::ui::oled::list;
using deluge::ui::oled::ListRow;
using deluge::ui::oled::menu_rect;
using deluge::ui::oled::OledFrame;
using Rect = deluge::gfx::oled::Rect;
using GridRect = deluge::gfx::grid::Rect;
namespace uitest = deluge::ui::test;

// The input-consumption contract of ui/core/frame_context.h, from the outside:
//
//   * a frame's accessors are the input, and they pass it through unchanged while focused;
//   * `region(rect, false)` presents zero input to its child, so a widget drawn there paints but
//     cannot move -- and focus only narrows, so the child cannot take it back;
//   * a widget that consumes an input records a claim on the tick's context, which is how the
//     shell learns what is left over for the scene;
//   * pad input is positional and is *not* masked: an unfocused grid region still reports its
//     presses, because a press names its target by where it landed;
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
		// The root frame is the live one, and nothing has been consumed yet.
		expect(r.frame.focused).to_be_true();
		expect(r.ctx.claims.any()).to_be_false();
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
			expect(f.focused).to_be_false();
			expect(int(f.turn())).to_equal(0);
			expect(int(f.edit(0))).to_equal(0);
			expect(int(f.horizontal())).to_equal(0);
			expect(int(f.vertical())).to_equal(0);
			expect(f.select_pressed()).to_be_false();
			expect(f.back_pressed()).to_be_false();
			expect(f.shift()).to_be_false();
			expect(f.now_ms()).to_equal(uint32_t(0));
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

	it("lets a region narrow focus but never widen it", _ {
		uitest::Rig48 r;
		r.input.turn = 1;
		r.frame.region(Rect{0, 0, 64, 20}, false, [&](OledFrame& outer) {
			// `true` here means "inherit", and the parent has nothing to hand down.
			outer.region(Rect{0, 0, 32, 10}, true, [&](OledFrame& inner) {
				expect(inner.focused).to_be_false();
				expect(int(inner.turn())).to_equal(0);
			});
			// The default overload is the same "inherit".
			outer.region(Rect{0, 0, 32, 10}, [&](OledFrame& inner) { expect(inner.focused).to_be_false(); });
		});
		r.frame.region(Rect{0, 0, 64, 20}, [&](OledFrame& f) {
			expect(f.focused).to_be_true();   // a focused parent hands focus down by default
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
			expect(f.focused).to_be_false();
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
});
DELUGE_SPEC(ui_frame_context_spec, frame_context)
