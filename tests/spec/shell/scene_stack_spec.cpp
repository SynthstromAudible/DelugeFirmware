#include "../ui/support/frames.h"
#include "shell/scene_stack.h"

#include "cppspec.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>

#include "../support/spec_main.h"
using namespace deluge::ui::shell;

namespace {

/// The two application kinds the test scenes register under. `FirstApp` is where the scenes plan
/// starts numbering, so a spec that borrowed a shell kind would collide with a real scene later.
constexpr SceneKind kA = SceneKind::FirstApp;
constexpr SceneKind kB = static_cast<SceneKind>(static_cast<uint8_t>(SceneKind::FirstApp) + 1);
/// A third opaque kind, so a spec that wants three distinct layers does not have to stack one kind
/// twice. The registry holds one `Scene` per kind, so `[kA, kB, kA]` is the *same object* at two
/// indices -- a configuration nothing in the shell builds and none of the lifecycle hooks is
/// coherent under (it takes `on_enter` twice with no `on_exit` between, and both indices share one
/// state slot).
constexpr SceneKind kC = static_cast<SceneKind>(static_cast<uint8_t>(SceneKind::FirstApp) + 2);
/// The passive layer: a stand-in for the three shipped `input_transparent()` overlays (popup,
/// console line, working animation), which is what makes a spec able to state the difference
/// between the raw top and the input top.
constexpr SceneKind kT = static_cast<SceneKind>(static_cast<uint8_t>(SceneKind::FirstApp) + 3);
/// A kind inside `kMaxSceneKinds` but past the end of the rig's registry span.
constexpr SceneKind kUnregistered = static_cast<SceneKind>(40);

/// @brief A scene that refuses `on_enter` for `arg == 99` and round-trips its `arg` through
///        `SceneState`, so a spec can watch both the rollback path and the state slots.
class SceneA final : public Scene {
public:
	SceneKind kind() const override { return kA; }
	Result handle(const UiEvent&, const UiContext&) override { return Result::unhandled(); }

	bool on_enter(const SceneId& id) override {
		if (id.arg == 99) {
			return false;
		}
		arg = id.arg;
		enters++;
		return true;
	}
	void on_exit() override {
		exits++;
		if (seq != nullptr) {
			exit_at = (*seq)++;
		}
	}
	void on_resume() override { resumes++; }
	void save_state(SceneState& s) const override {
		std::memcpy(s.bytes, &arg, sizeof(arg));
		saves++;
	}
	void restore_state(const SceneState& s) override {
		std::memcpy(&restored, s.bytes, sizeof(restored));
		restores++;
	}

	int* seq = nullptr; ///< Shared exit-order clock, or null to not record order.
	int exit_at = -1;   ///< When this scene last exited, from `seq`.
	uint16_t arg = 0;
	uint16_t restored = 0;
	int enters = 0, exits = 0, resumes = 0, restores = 0;
	mutable int saves = 0;
};

/// @brief A scene that only counts, for the lifecycle callbacks a spec asserts on from the other
///        side of a stack operation.
class SceneB final : public Scene {
public:
	SceneKind kind() const override { return kB; }
	Result handle(const UiEvent&, const UiContext&) override { return Result::unhandled(); }

	void on_exit() override {
		exits++;
		if (seq != nullptr) {
			exit_at = (*seq)++;
		}
	}
	void on_resume() override { resumes++; }
	void save_state(SceneState&) const override { saves++; }
	void restore_state(const SceneState&) override { restores++; }

	int* seq = nullptr;
	int exit_at = -1;
	int exits = 0, resumes = 0, restores = 0;
	mutable int saves = 0;
};

/// @brief A third counting scene, for a spec that needs three distinct opaque layers.
class SceneC final : public Scene {
public:
	SceneKind kind() const override { return kC; }
	Result handle(const UiEvent&, const UiContext&) override { return Result::unhandled(); }

	void on_exit() override { exits++; }
	void on_resume() override { resumes++; }

	int exits = 0, resumes = 0;
};

/// @brief An `input_transparent()` scene: drawn over whatever is beneath and never taking its
///        input, exactly as `PopupScene`, `ConsoleScene` and `WorkingScene` are.
///
/// It counts `on_resume` for the same reason the other two do -- a spec that asserts "nobody was
/// notified" has to be able to see this one not being notified either.
class SceneT final : public Scene {
public:
	SceneKind kind() const override { return kT; }
	Result handle(const UiEvent&, const UiContext&) override { return Result::unhandled(); }
	bool input_transparent() const override { return true; }

	bool on_enter(const SceneId& id) override {
		if (id.arg == 99) {
			return false;
		}
		enters++;
		return true;
	}
	void on_exit() override { exits++; }
	void on_resume() override { resumes++; }

	int enters = 0, exits = 0, resumes = 0;
};

/// @brief The four scenes, a 20-entry registry table over them, and a stack bound to it.
///
/// 20 entries is `FirstApp + 4`: every shell kind is left unregistered (null), `kA` sits at 16,
/// `kB` at 17, `kC` at 18 and `kT` at 19, which still makes `kUnregistered` (40) an index past the
/// end of the span.
struct Rig {
	SceneA a;
	SceneB b;
	SceneC c;
	SceneT t;
	std::array<Scene*, 20> table{};
	SceneRegistry reg{};
	SceneStack st{reg};
	int seq = 0;

	Rig() {
		table[16] = &a;
		table[17] = &b;
		table[18] = &c;
		table[19] = &t;
		reg.by_kind = std::span<Scene* const>{table};
		a.seq = &seq;
		b.seq = &seq;
	}

	Rig(const Rig&) = delete;
	Rig& operator=(const Rig&) = delete;
};

} // namespace

// clang-format off
describe shell_scene_stack("shell::SceneStack", $ {
	it("push, pop, on_resume, refusal rollback", _ {
		Rig r;
		expect(r.st.depth()).to_equal(size_t(0));
		expect(r.st.top().kind() == SceneKind::Null).to_be_true();   // NullScene swallows input
		expect(r.st.root() == nullptr).to_be_true();
		expect(r.st.push({kA})).to_be_true();
		expect(r.st.push({kB})).to_be_true();
		expect(r.st.depth()).to_equal(size_t(2));
		expect(r.st.top().kind() == kB).to_be_true();
		expect(r.st.push({kA, 99})).to_be_false();
		expect(r.st.depth()).to_equal(size_t(2));
		expect(r.b.resumes).to_equal(1);                             // rolled back, previous top resumed
		expect(r.st.top().kind() == kB).to_be_true();
		r.st.pop();
		expect(r.st.depth()).to_equal(size_t(1));
		expect(r.b.exits).to_equal(1);
		expect(r.a.resumes).to_equal(1);
		expect(r.st.top().kind() == kA).to_be_true();
	});

	it("pop(kind) pops down to and including that kind", _ {
		// Three *distinct* kinds. This used to stack `kA` twice for want of a third scene, which
		// with one `Scene` per kind put the same object at indices 0 and 2 -- so "the root resumed"
		// and "the popped copy was told it left" were the same counter, and neither claim below was
		// actually being made.
		Rig r;
		expect(r.st.push({kA})).to_be_true();
		expect(r.st.push({kB})).to_be_true();
		expect(r.st.push({kC})).to_be_true();
		expect(r.st.depth()).to_equal(size_t(3));
		r.st.pop(kB);
		expect(r.st.depth()).to_equal(size_t(1));
		expect(r.st.top().kind() == kA).to_be_true();
		expect(r.b.exits).to_equal(1);
		expect(r.c.exits).to_equal(1);                               // the kC above kB, and it alone
		expect(r.a.exits).to_equal(0);                               // never the root
		expect(r.a.resumes).to_equal(1);                             // the root resumed
	});

	it("remove(kind) excises the target wherever it sits, leaving the rest in order", _ {
		Rig r;
		expect(r.st.push({kA, 1})).to_be_true();
		expect(r.st.push({kB})).to_be_true();
		expect(r.st.push({kA, 2})).to_be_true();
		expect(r.st.depth()).to_equal(size_t(3));
		expect(r.st.changed_since_last_query()).to_be_true(); // drain the three pushes' flag

		expect(r.st.remove(kB)).to_be_true();
		expect(r.st.depth()).to_equal(size_t(2));
		expect(r.b.exits).to_equal(1);
		expect(r.b.saves).to_equal(1);
		expect(r.a.exits).to_equal(0);                               // neither A was touched
		expect(r.a.saves).to_equal(0);
		expect(r.a.resumes).to_equal(0);                             // kB was not the top: no on_resume
		expect(r.st.id_at(0) == SceneId{kA, 1}).to_be_true();        // the root, untouched
		expect(r.st.id_at(1) == SceneId{kA, 2}).to_be_true();        // the former top, shifted down intact
		expect(r.st.top().kind() == kA).to_be_true();
		expect(r.st.changed_since_last_query()).to_be_true();

		// Absent: no scene of that kind is stacked any more.
		expect(r.st.remove(kB)).to_be_false();
		expect(r.st.depth()).to_equal(size_t(2));
		expect(r.st.id_at(0) == SceneId{kA, 1}).to_be_true();
		expect(r.st.id_at(1) == SceneId{kA, 2}).to_be_true();
		expect(r.st.changed_since_last_query()).to_be_false();
	});

	it("remove(kind) on the actual top resumes the scene now exposed", _ {
		Rig r;
		expect(r.st.push({kA})).to_be_true();
		expect(r.st.push({kB})).to_be_true();
		expect(r.st.remove(kB)).to_be_true();
		expect(r.st.depth()).to_equal(size_t(1));
		expect(r.b.exits).to_equal(1);
		expect(r.a.resumes).to_equal(1);                             // the newly-exposed top is resumed
		expect(r.st.top().kind() == kA).to_be_true();
	});

	// --- the input top is what `on_resume` is about ------------------------------------------
	//
	// The four rows below are the shell defect E4 task 10 measured: `SceneKind::Popup` is a scene
	// here where legacy's popup is a display overlay, so `remove`ing one used to fire `on_resume`
	// on the scene beneath -- and `SlicerScene::on_resume` is `Slicer::focusRegained`
	// (`gui/ui/slicer.cpp:59-72`), which wipes the slice table. See §15.7 of the shell design spec.

	it("removing an input-transparent scene notifies nobody", _ {
		Rig r;
		expect(r.st.push({kA})).to_be_true();
		expect(r.st.push({kT})).to_be_true();
		expect(&r.st.input_top() == &r.a).to_be_true();              // it never took the input top
		expect(r.st.remove(kT)).to_be_true();
		expect(r.t.exits).to_equal(1);
		expect(r.st.depth()).to_equal(size_t(1));
		// The scene beneath never lost the input top, so it never regained it. `input_transparent()`
		// is the shell's own statement that no transition happened.
		expect(r.a.resumes).to_equal(0);

		// Same answer with something real between the two: `kB` is the input top throughout.
		expect(r.st.push({kB})).to_be_true();
		expect(r.st.push({kT})).to_be_true();
		expect(r.st.remove(kT)).to_be_true();
		expect(r.b.resumes).to_equal(0);
		expect(r.a.resumes).to_equal(0);
	});

	it("removing a scene from above a transparent one resumes the input top, not the raw top", _ {
		Rig r;
		expect(r.st.push({kA})).to_be_true();
		expect(r.st.push({kT})).to_be_true();
		expect(r.st.push({kB})).to_be_true();                        // [kA, kT, kB]
		expect(r.st.remove(kB)).to_be_true();
		expect(r.st.top().kind() == kT).to_be_true();                // the raw top is the overlay
		expect(&r.st.input_top() == &r.a).to_be_true();              // the input top is kA
		expect(r.a.resumes).to_equal(1);                             // and kA is what gets told
		expect(r.t.resumes).to_equal(0);                             // never the passive layer

		// `pop()` is the same question asked the other way round, and gets the same answer.
		Rig p;
		expect(p.st.push({kA})).to_be_true();
		expect(p.st.push({kT})).to_be_true();
		expect(p.st.push({kB})).to_be_true();
		p.st.pop();
		expect(p.a.resumes).to_equal(1);
		expect(p.t.resumes).to_equal(0);
	});

	it("removing the real top from under a transparent scene notifies nobody", _ {
		// A stack of nothing but input-transparent scenes has no input top (`input_top`'s own
		// note), so there is nothing to resume -- and the overlay left standing must not be told it
		// is now taking input, because it is not.
		Rig r;
		expect(r.st.push({kT})).to_be_true();
		expect(r.st.push({kA})).to_be_true();
		expect(r.st.remove(kA)).to_be_true();
		expect(r.st.top().kind() == kT).to_be_true();
		expect(r.st.input_top().kind() == SceneKind::Null).to_be_true();
		expect(r.t.resumes).to_equal(0);
	});

	it("a refused push resumes the input top only when the refused scene held it", _ {
		// The refused scene is installed before `on_enter` runs (so it can see itself as the top),
		// so what it held while it was there is what the rollback has to hand back.
		Rig r;
		expect(r.st.push({kA})).to_be_true();
		expect(r.st.push({kT, 99})).to_be_false();                   // transparent, and it refused
		expect(r.st.depth()).to_equal(size_t(1));
		expect(r.a.resumes).to_equal(0);                             // kA never lost the input top

		// An opaque refusal over a transparent layer resumes the scene two down, not the layer.
		Rig o;
		expect(o.st.push({kB})).to_be_true();
		expect(o.st.push({kT})).to_be_true();                        // [kB, kT]
		expect(o.st.push({kA, 99})).to_be_false();
		expect(o.st.depth()).to_equal(size_t(2));
		expect(o.b.resumes).to_equal(1);                             // the input top comes back
		expect(o.t.resumes).to_equal(0);
	});

	it("a refused replace_top resumes the scene it exited, whatever the input top is", _ {
		// `replace_top`'s rollback is the one arm that is *not* an input-top question.
		// `changeUIAtLevel`'s failure path calls `oldUI->focusRegained()` unconditionally
		// (`gui/ui/ui.cpp:96-101`), and it is right to: the previous scene really was exited, so
		// `on_resume` is the paired notification for an `on_exit` that ran, not a claim about the
		// top. Here the restored scene is the passive layer, and it is still the one told.
		Rig r;
		expect(r.st.push({kB})).to_be_true();
		expect(r.st.push({kT})).to_be_true();
		expect(r.st.replace_top({kA, 99})).to_be_false();
		expect(r.st.depth()).to_equal(size_t(2));
		expect(r.st.top().kind() == kT).to_be_true();
		expect(r.t.exits).to_equal(1);
		expect(r.t.resumes).to_equal(1);                             // the exited scene is told
		expect(r.b.resumes).to_equal(0);                             // and nothing else is
	});

	it("replace_root saves and restores POD state by SceneId", _ {
		Rig r;
		expect(r.st.push({kA, 5})).to_be_true();
		expect(r.a.arg).to_equal(uint16_t(5));
		expect(r.st.replace_root({kB})).to_be_true();
		expect(r.a.saves).to_equal(1);
		expect(r.a.exits).to_equal(1);
		expect(r.st.depth()).to_equal(size_t(1));
		expect(r.st.replace_root({kA, 5})).to_be_true();
		expect(r.a.restored).to_equal(uint16_t(5));
		expect(r.a.restores).to_equal(1);
		expect(r.st.depth()).to_equal(size_t(1));
		expect(r.st.root() == &r.a).to_be_true();
	});

	it("reset empties the stack and top() is the NullScene", _ {
		Rig r;
		expect(r.st.push({kA})).to_be_true();
		r.st.reset();
		expect(r.st.depth()).to_equal(size_t(0));
		expect(r.st.top().kind() == SceneKind::Null).to_be_true();
		expect(r.a.exits).to_equal(1);
		expect(r.a.saves).to_equal(0);                               // reset is not a save point
	});

	it("depth is capped at 8", _ {
		Rig r;
		for (uint16_t i = 0; i < kStackDepth; i++) {
			expect(r.st.push({kA, i})).to_be_true();
		}
		expect(r.st.depth()).to_equal(size_t(kStackDepth));
		expect(r.st.push({kA, 8})).to_be_false();
		expect(r.st.depth()).to_equal(size_t(kStackDepth));
		expect(r.a.enters).to_equal(int(kStackDepth));               // the refused push never entered
	});

	it("replace_top swaps the top without touching a state slot", _ {
		Rig r;
		expect(r.st.push({kA, 5})).to_be_true();
		expect(r.st.replace_top({kB})).to_be_true();
		expect(r.st.depth()).to_equal(size_t(1));
		expect(r.st.top().kind() == kB).to_be_true();
		expect(r.a.exits).to_equal(1);
		expect(r.a.saves).to_equal(0);
		// kA's slot was never written, so coming back to it does not restore.
		expect(r.st.replace_root({kA, 7})).to_be_true();
		expect(r.a.restores).to_equal(0);
		expect(r.a.arg).to_equal(uint16_t(7));
	});

	it("replace_top on an empty stack is a plain push", _ {
		Rig r;
		expect(r.st.depth()).to_equal(size_t(0));
		expect(r.st.replace_top({kA, 3})).to_be_true();
		expect(r.st.depth()).to_equal(size_t(1));
		expect(r.st.top().kind() == kA).to_be_true();
		expect(r.a.enters).to_equal(1);
		expect(r.a.arg).to_equal(uint16_t(3));
		expect(r.a.exits).to_equal(0);                               // there was nothing to exit
		expect(r.a.resumes).to_equal(0);
	});

	it("a refused replace_top restores and resumes the previous top", _ {
		Rig r;
		expect(r.st.push({kA})).to_be_true();
		expect(r.st.push({kB})).to_be_true();
		expect(r.st.replace_top({kA, 99})).to_be_false();
		expect(r.st.depth()).to_equal(size_t(2));
		expect(r.st.top().kind() == kB).to_be_true();                // kB is back on top
		expect(r.st.id_at(1) == SceneId{kB}).to_be_true();           // and so is its id
		expect(r.b.exits).to_equal(1);                               // it was exited before the swap
		expect(r.b.resumes).to_equal(1);                             // and resumed by the rollback
		expect(r.a.enters).to_equal(1);                              // only the original push entered
		expect(r.a.resumes).to_equal(0);                             // the scene beneath was not disturbed
	});

	it("replace_root to the same kind with a different arg restores the kind's slot", _ {
		Rig r;
		expect(r.st.push({kA, 5})).to_be_true();
		expect(r.st.replace_root({kB})).to_be_true();                // saves arg 5 into kA's slot
		expect(r.st.replace_root({kA, 7})).to_be_true();
		// The slot is keyed by SceneKind, not by SceneId, so the saved state comes back whatever
		// the new arg says -- and on_enter, which ran first, is what re-targeted the scene.
		expect(r.a.restores).to_equal(1);
		expect(r.a.restored).to_equal(uint16_t(5));
		expect(r.a.arg).to_equal(uint16_t(7));
	});

	it("replace_root leaves a scene's state alone when its slot is invalid", _ {
		Rig r;
		expect(r.st.push({kA, 5})).to_be_true();
		expect(r.st.replace_root({kB})).to_be_true();
		expect(r.b.restores).to_equal(0);                            // kB's slot has never been saved
		expect(r.st.depth()).to_equal(size_t(1));
		expect(r.st.top().kind() == kB).to_be_true();
	});

	it("an unregistered kind cannot be pushed", _ {
		Rig r;
		expect(r.reg.find(kUnregistered) == nullptr).to_be_true();
		expect(r.reg.find(SceneKind::Popup) == nullptr).to_be_true();
		expect(r.st.push({kUnregistered})).to_be_false();
		expect(r.st.depth()).to_equal(size_t(0));
		expect(r.st.replace_root({kUnregistered})).to_be_false();
		expect(r.st.replace_top({kUnregistered})).to_be_false();
	});

	it("reset exits the stack top first", _ {
		Rig r;
		expect(r.st.push({kA})).to_be_true();
		expect(r.st.push({kB})).to_be_true();
		r.st.reset();
		expect(r.b.exit_at).to_equal(0);
		expect(r.a.exit_at).to_equal(1);
	});

	it("every operation marks the stack changed, once", _ {
		Rig r;
		expect(r.st.changed_since_last_query()).to_be_false();
		r.st.push({kA});
		expect(r.st.changed_since_last_query()).to_be_true();
		expect(r.st.changed_since_last_query()).to_be_false();
		r.st.pop();
		expect(r.st.changed_since_last_query()).to_be_true();
		r.st.reset();
		expect(r.st.changed_since_last_query()).to_be_true();
	});

	it("scenes() and id_at() report the stack root first", _ {
		Rig r;
		expect(r.st.push({kA, 1})).to_be_true();
		expect(r.st.push({kB, 2})).to_be_true();
		expect(r.st.scenes().size()).to_equal(size_t(2));
		expect(r.st.scenes()[0] == &r.a).to_be_true();
		expect(r.st.scenes()[1] == &r.b).to_be_true();
		expect(r.st.id_at(0) == SceneId{kA, 1}).to_be_true();
		expect(r.st.id_at(1) == SceneId{kB, 2}).to_be_true();
		expect(r.st.id_at(2) == SceneId{}).to_be_true();             // out of range
	});

	it("NullScene swallows input and draws nothing", _ {
		NullScene null;
		expect(null.kind() == SceneKind::Null).to_be_true();

		ButtonSet held;
		deluge::ui::PadMask held_pads{};
		BoardProfile profile{};
		UiContext ctx{held, held_pads, false, false, 0, profile};
		UiEvent ev{};
		ev.kind = UiEvent::Kind::Button;
		ev.button = ButtonId::Back;
		ev.on = true;
		expect(null.handle(ev, ctx).handled).to_be_false();
		expect(null.handle(ev, ctx).actions.empty()).to_be_true();
		expect(null.after_render().handled).to_be_false();

		deluge::ui::test::GridRig grid;
		const std::string before_pads = grid.state();
		null.render_grid(grid.frame);
		expect(grid.state() == before_pads).to_be_true();

		deluge::ui::test::Rig48 oled;
		const std::string before_pixels = oled.pbm();
		null.render_oled(SurfaceRole::Main, oled.frame);
		expect(oled.pbm() == before_pixels).to_be_true();
	});
});
// clang-format on

DELUGE_SPEC(shell_scene_stack_spec, shell_scene_stack)
