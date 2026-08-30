#include "shell/router.h"
#include "support/fake_surfaces.h"

#include "cppspec.hpp"

#include <array>
#include <string>
#include <vector>

#include "../support/spec_main.h"
using namespace deluge::ui::shell;
using deluge::ui::EncoderRole;
using deluge::ui::InputClaims;

namespace {

/// The two application kinds the test scenes register under -- mirrors scene_stack_spec.cpp's
/// choice of `FirstApp`/`FirstApp + 1` so a stray shell kind never collides with them.
constexpr SceneKind kA = SceneKind::FirstApp;
constexpr SceneKind kB = static_cast<SceneKind>(static_cast<uint8_t>(SceneKind::FirstApp) + 1);

/// @brief A scene that records its own name into a shared call-order log, remembers the last
///        `UiContext::shift` it was handed, and returns a configurable `Result`.
class RecordingScene final : public Scene {
public:
	RecordingScene(SceneKind k, std::vector<std::string>* log, std::string name)
	    : kind_(k), log_(log), name_(std::move(name)) {}

	SceneKind kind() const override { return kind_; }
	Result handle(const UiEvent&, const UiContext& ctx) override {
		calls++;
		last_shift = ctx.shift;
		log_->push_back(name_);
		return next_result;
	}

	Result next_result = Result::unhandled();
	int calls = 0;
	bool last_shift = false;

private:
	SceneKind kind_;
	std::vector<std::string>* log_;
	std::string name_;
};

/// @brief A `Service` on the same terms as `RecordingScene`: records its name, returns a
///        configurable `Result`.
class RecordingService final : public Service {
public:
	RecordingService(std::vector<std::string>* log, std::string name) : log_(log), name_(std::move(name)) {}
	Result handle(const UiEvent&, const UiContext&) override {
		calls++;
		log_->push_back(name_);
		return next_result;
	}

	Result next_result = Result::unhandled();
	int calls = 0;

private:
	std::vector<std::string>* log_;
	std::string name_;
};

/// A plain `Button` event (not one of the widget-owned shapes).
UiEvent button_event(ButtonId b, bool on) {
	UiEvent e{};
	e.kind = UiEvent::Kind::Button;
	e.button = b;
	e.on = on;
	return e;
}

/// A `DELUGE_EVENT_BUTTON` at matrix (x, y), for feeding `InputLowering::on_board_event` -- the
/// same shorthand `input_lowering_spec.cpp` uses.
DelugeInputEvent board_button(uint8_t x, uint8_t y, bool on) {
	return DelugeInputEvent{DELUGE_EVENT_BUTTON, x, y, static_cast<int16_t>(on ? 255 : 0)};
}

/// A `Result` that reports `handled` and carries no actions.
Result handled_no_actions() {
	Result r;
	r.handled = true;
	return r;
}

/// @brief A `UiContext` built from an `InputLowering`'s live state, as a shell (rather than a
///        hand-built spec context) would build one.
UiContext make_ctx(const InputLowering& lowering) {
	return UiContext{lowering.held(),   lowering.held_pads(),       lowering.shift(),
	                 lowering.sticky(), lowering.snapshot().now_ms, lowering.profile(),
	                 &lowering};
}

/// @brief Two registered scenes (`kA` on top by default), the mod-knob and global-controls
///        services, a real `InputLowering`, and the `Router` under test wired over all of them.
struct Rig {
	std::vector<std::string> log;
	RecordingScene scene_a{kA, &log, "scene"};
	RecordingScene scene_b{kB, &log, "scene_b"};
	std::array<Scene*, 18> table{};
	SceneRegistry reg{};
	SceneStack stack{reg};
	RecordingService mod_knobs{&log, "mod"};
	RecordingService global_controls{&log, "global"};
	InputLowering lowering{uitest::test_profile_input(), {}};
	Router router{stack, Services{&mod_knobs, &global_controls}, lowering};

	Rig() {
		table[16] = &scene_a;
		table[17] = &scene_b;
		reg.by_kind = std::span<Scene* const>{table};
		stack.push({kA});
	}

	Rig(const Rig&) = delete;
	Rig& operator=(const Rig&) = delete;
};

} // namespace

// clang-format off
describe shell_router("shell::Router", $ {
	it("routes in the legacy order and stops at the first handler", _ {
		Rig r;
		r.scene_a.next_result = handled_no_actions();
		// Mod0 sits at matrix (1, 0) in test_profile_input's table, but a bare UiEvent is enough --
		// the router only inspects its kind/button/encoder fields.
		UiEvent mod_event = button_event(ButtonId::Mod0, true);

		r.router.route(mod_event, make_ctx(r.lowering));

		expect(r.log.size()).to_equal(size_t(2));
		expect(r.log[0] == "mod").to_be_true();
		expect(r.log[1] == "scene").to_be_true();
		expect(r.mod_knobs.calls).to_equal(1);
		expect(r.scene_a.calls).to_equal(1);
		expect(r.global_controls.calls).to_equal(0);   // never reached: the scene already handled it
	});

	it("skips mod_knobs entirely for a plain button, and applies actions from unhandled steps too", _ {
		Rig r;
		Result scene_result;
		scene_result.handled = false;
		Action push;
		push.kind = Action::Kind::Push;
		push.id = SceneId{kB};
		scene_result.actions.push_back(push);
		r.scene_a.next_result = scene_result;
		r.global_controls.next_result = handled_no_actions();

		expect(r.stack.depth()).to_equal(size_t(1));
		r.router.route(button_event(ButtonId::Back, true), make_ctx(r.lowering));

		expect(r.mod_knobs.calls).to_equal(0);          // Back is not a mod/gold-knob event
		expect(r.log.size()).to_equal(size_t(2));
		expect(r.log[0] == "scene").to_be_true();
		expect(r.log[1] == "global").to_be_true();
		expect(r.stack.depth()).to_equal(size_t(2));    // Push applied even though the scene said unhandled
		expect(r.stack.top().kind() == kB).to_be_true();
	});

	it("Pop, ReplaceRoot, LockInput, and Repaint actions all take effect", _ {
		Rig r;
		expect(r.stack.push({kB})).to_be_true();
		expect(r.stack.depth()).to_equal(size_t(2));

		Action pop;
		pop.kind = Action::Kind::Pop; // default pop_to (Null) means "just the top" -- a blind pop of
		// whatever is CURRENTLY topmost. Fine for a scene that really does expect to be the top; a
		// scene that wants to close ITSELF specifically (an overlay may legitimately have been
		// pushed above it since it rendered) wants `Action::Kind::Remove` instead, which excises a
		// named kind wherever it sits and leaves everything else alone -- see `Action`'s own doc
		// comment and `MenuScene::handle`'s root-`Back` case (task 11 review round 1, I3).
		Action lock;
		lock.kind = Action::Kind::LockInput;
		lock.lock = true;
		Action repaint;
		repaint.kind = Action::Kind::Repaint;
		repaint.surface = SurfaceRole::Main;
		Result res;
		res.handled = true;
		res.actions.push_back(pop);
		res.actions.push_back(lock);
		res.actions.push_back(repaint);
		r.scene_b.next_result = res;

		r.router.route(button_event(ButtonId::Back, true), make_ctx(r.lowering));

		expect(r.stack.depth()).to_equal(size_t(1));
		expect(r.stack.top().kind() == kA).to_be_true();       // kB popped off
		expect(r.lowering.input_locked()).to_be_true();
		expect(r.router.repaint_requested(SurfaceRole::Main)).to_be_true();
		expect(r.router.repaint_requested(SurfaceRole::Aux)).to_be_false();
		r.router.clear_repaint_requests();
		expect(r.router.repaint_requested(SurfaceRole::Main)).to_be_false();

		// ReplaceRoot, exercised separately now that kA (depth 1) is back on top.
		Action replace_root;
		replace_root.kind = Action::Kind::ReplaceRoot;
		replace_root.id = SceneId{kB};
		Result res2;
		res2.handled = true;
		res2.actions.push_back(replace_root);
		r.scene_a.next_result = res2;

		r.router.route(button_event(ButtonId::Back, true), make_ctx(r.lowering));
		expect(r.stack.depth()).to_equal(size_t(1));
		expect(r.stack.top().kind() == kB).to_be_true();
	});

	it("a Remove action excises only the named kind, leaving an overlay above it in place", _ {
		// The reason `Remove` exists at all (task 11 review round 1, I3): unlike `Pop(kind)`, which
		// also takes everything ABOVE `kind` down with it, `Remove(kind)` must leave `kB` -- pushed
		// above `kA` here to stand in for a `Notices` overlay legitimately shown over an app scene --
		// exactly where it was.
		Rig r;
		expect(r.stack.push({kB})).to_be_true();
		expect(r.stack.depth()).to_equal(size_t(2));

		Action remove;
		remove.kind = Action::Kind::Remove;
		remove.pop_to = kA;
		Result res;
		res.handled = true;
		res.actions.push_back(remove);
		r.scene_b.next_result = res;

		r.router.route(button_event(ButtonId::Back, true), make_ctx(r.lowering));

		expect(r.stack.depth()).to_equal(size_t(1)); // only kA is gone
		expect(r.stack.top().kind() == kB).to_be_true();
	});

	it("a RepaintAll action marks every surface", _ {
		Rig r;
		Action repaint_all;
		repaint_all.kind = Action::Kind::RepaintAll;
		Result res;
		res.handled = true;
		res.actions.push_back(repaint_all);
		r.scene_a.next_result = res;

		r.router.route(button_event(ButtonId::Back, true), make_ctx(r.lowering));

		expect(r.router.repaint_requested(SurfaceRole::Main)).to_be_true();
		expect(r.router.repaint_requested(SurfaceRole::Aux)).to_be_true();
		expect(r.router.repaint_requested(SurfaceRole::Param0)).to_be_true();
	});

	it("a Repaint action with surface None is a no-op, not an all-surfaces sentinel", _ {
		Rig r;
		Action repaint_none;
		repaint_none.kind = Action::Kind::Repaint;
		repaint_none.surface = SurfaceRole::None;
		Result res;
		res.handled = true;
		res.actions.push_back(repaint_none);
		r.scene_a.next_result = res;

		r.router.route(button_event(ButtonId::Back, true), make_ctx(r.lowering));

		expect(r.router.repaint_requested(SurfaceRole::Main)).to_be_false();
		expect(r.router.repaint_requested(SurfaceRole::Aux)).to_be_false();
		expect(r.router.repaint_requested(SurfaceRole::Param0)).to_be_false();
		expect(r.router.repaint_requested(SurfaceRole::None)).to_be_false();
	});

	it("ButtonId::ModEnc0/ModEnc1 presses are not mod_knobs events -- they reach the scene "
	   "first, exactly like any other button", _ {
		Rig r;
		UiEvent mod_enc_event{};
		mod_enc_event.kind = UiEvent::Kind::EncoderButton;
		mod_enc_event.button = ButtonId::ModEnc0;
		mod_enc_event.on = true;

		r.router.route(mod_enc_event, make_ctx(r.lowering));

		expect(r.mod_knobs.calls).to_equal(0);          // never reached: not a mod_knobs event
		expect(r.log.size()).to_equal(size_t(2));
		expect(r.log[0] == "scene").to_be_true();       // the scene sees it first
		expect(r.log[1] == "global").to_be_true();      // and, unhandled, it falls to global_controls

		// ModEnc1 (the other gold knob's push) follows the same rule.
		r.log.clear();
		UiEvent mod_enc1_event{};
		mod_enc1_event.kind = UiEvent::Kind::EncoderButton;
		mod_enc1_event.button = ButtonId::ModEnc1;
		mod_enc1_event.on = true;
		r.router.route(mod_enc1_event, make_ctx(r.lowering));
		expect(r.mod_knobs.calls).to_equal(0);
		expect(r.log[0] == "scene").to_be_true();
	});

	it("Popup actions store both popup_string and popup_type, cleared together", _ {
		Rig r;
		Action popup;
		popup.kind = Action::Kind::Popup;
		popup.popup_string = 42;
		popup.popup_type = 7;
		Result res;
		res.handled = true;
		res.actions.push_back(popup);
		r.scene_a.next_result = res;

		r.router.route(button_event(ButtonId::Back, true), make_ctx(r.lowering));

		expect(r.router.pending_popup_string()).to_equal(uint16_t(42));
		expect(r.router.pending_popup_type()).to_equal(uint8_t(7));
		r.router.clear_pending_popup();
		expect(r.router.pending_popup_string()).to_equal(uint16_t(0));
		expect(r.router.pending_popup_type()).to_equal(uint8_t(0));
	});

	it("Shift and mod buttons are modifiers: consumed by bookkeeping, scenes still see them", _ {
		Rig r;

		r.lowering.begin_tick(0);
		r.lowering.on_board_event(board_button(8, 0, true), 0); // Shift, test_profile_input's matrix
		r.lowering.end_tick(0);
		UiEvent shift_event;
		expect(r.lowering.next(shift_event)).to_be_true();
		expect(shift_event.kind == UiEvent::Kind::Button).to_be_true();
		expect(shift_event.button == ButtonId::Shift).to_be_true();

		UiContext ctx = make_ctx(r.lowering);
		expect(ctx.shift).to_be_true();  // the lowering already latched the physical hold
		r.router.route(shift_event, ctx);
		expect(r.scene_a.calls).to_equal(1);   // the scene saw the Shift button event itself
		expect(r.scene_a.last_shift).to_be_true();

		// A later, unrelated event: the lowering still reports shift held, and a ctx built from it
		// for *this* event reflects that -- bookkeeping (the latch) ran on the Shift press, but the
		// scene sees every event's own ctx.shift, this one included.
		r.lowering.begin_tick(10);
		r.lowering.on_board_event(board_button(5, 0, true), 10); // Synth, a plain button
		r.lowering.end_tick(10);
		UiEvent synth_event;
		expect(r.lowering.next(synth_event)).to_be_true();
		expect(synth_event.button == ButtonId::Synth).to_be_true();

		UiContext ctx2 = make_ctx(r.lowering);
		expect(ctx2.shift).to_be_true();
		r.router.route(synth_event, ctx2);
		expect(r.scene_a.calls).to_equal(2);
		expect(r.scene_a.last_shift).to_be_true();
	});

	it("route_post_render skips an event a widget already claimed, and routes what nothing claimed", _ {
		Rig r;
		UiEvent turn_event{};
		turn_event.kind = UiEvent::Kind::Encoder;
		turn_event.encoder = EncoderRole::Select;
		turn_event.delta = 1;
		UiContext ctx = make_ctx(r.lowering);

		InputClaims claimed{};
		claimed.turn = true;
		r.router.route_post_render(turn_event, ctx, claimed);
		expect(r.scene_a.calls).to_equal(0);
		expect(r.router.dropped_events()).to_equal(uint32_t(0));   // claimed, not dropped

		InputClaims unclaimed{};
		r.router.route_post_render(turn_event, ctx, unclaimed);
		expect(r.scene_a.calls).to_equal(1);
		expect(r.router.dropped_events()).to_equal(uint32_t(1));   // ran the chain; nothing handled it
	});

	it("dropped_events counts an event nothing along the chain handled", _ {
		Rig r;
		expect(r.router.dropped_events()).to_equal(uint32_t(0));
		r.router.route(button_event(ButtonId::Back, true), make_ctx(r.lowering));
		expect(r.router.dropped_events()).to_equal(uint32_t(1));
		expect(r.log.size()).to_equal(size_t(2));   // scene, then global -- both default-unhandled
	});
});
// clang-format on

DELUGE_SPEC(shell_router_spec, shell_router)
