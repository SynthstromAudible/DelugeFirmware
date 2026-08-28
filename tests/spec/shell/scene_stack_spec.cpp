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

/// @brief The two scenes, an 18-entry registry table over them, and a stack bound to it.
///
/// 18 entries is `FirstApp + 2`: every shell kind is left unregistered (null), `kA` sits at 16 and
/// `kB` at 17, which is also what makes `kUnregistered` (40) an index past the end of the span.
struct Rig {
	SceneA a;
	SceneB b;
	std::array<Scene*, 18> table{};
	SceneRegistry reg{};
	SceneStack st{reg};
	int seq = 0;

	Rig() {
		table[16] = &a;
		table[17] = &b;
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
		Rig r;
		expect(r.st.push({kA})).to_be_true();
		expect(r.st.push({kB})).to_be_true();
		expect(r.st.push({kA, 2})).to_be_true();
		expect(r.st.depth()).to_equal(size_t(3));
		r.st.pop(kB);
		expect(r.st.depth()).to_equal(size_t(1));
		expect(r.st.top().kind() == kA).to_be_true();
		expect(r.b.exits).to_equal(1);
		expect(r.a.exits).to_equal(1);                               // the kA above kB, not the root
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
