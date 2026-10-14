#include "shell/leds.h"

#include "cppspec.hpp"

#include <array>
#include <cstdint>
#include <span>

#include "../support/spec_main.h"
using namespace deluge::ui::shell;
using deluge::ui::LedFrame;
using deluge::ui::LedId;
using deluge::ui::LedState;

namespace {

/// `LedId` is an opaque enum the board profile gives meaning to, so a spec names its own two.
constexpr LedId kPlay = static_cast<LedId>(3);
constexpr LedId kSong = static_cast<LedId>(7);

constexpr LedState on() {
	return LedState{LedState::Kind::On, 0, 0};
}
constexpr LedState off() {
	return LedState{LedState::Kind::Off, 0, 0};
}
constexpr LedState blink(uint16_t period_ms, uint32_t until_ms = 0) {
	return LedState{LedState::Kind::Blink, period_ms, until_ms};
}

/// @brief The state \p frame ended up declaring for \p id, or `Off` if it declared none.
LedState declared(const LedFrame& frame, LedId id) {
	for (const auto& [entry_id, state] : frame.entries()) {
		if (entry_id == id) {
			return state;
		}
	}
	return off();
}

/// @brief A `Service` that declares one LED.
class DeclaringService final : public Service {
public:
	DeclaringService(LedId id, LedState state) : id_(id), state_(state) {}
	void leds(LedFrame& frame) override { (void)frame.set(id_, state_); }

private:
	LedId id_;
	LedState state_;
};

/// @brief A `Scene` that declares up to two LEDs.
class DeclaringScene final : public Scene {
public:
	DeclaringScene(LedId a, LedState sa) : a_(a), sa_(sa) {}
	DeclaringScene(LedId a, LedState sa, LedId b, LedState sb) : a_(a), sa_(sa), b_(b), sb_(sb), has_b_(true) {}

	SceneKind kind() const override { return SceneKind::FirstApp; }
	Result handle(const UiEvent&, const UiContext&) override { return Result::unhandled(); }
	void leds(LedFrame& frame) override {
		(void)frame.set(a_, sa_);
		if (has_b_) {
			(void)frame.set(b_, sb_);
		}
	}

private:
	LedId a_;
	LedState sa_;
	LedId b_{};
	LedState sb_{};
	bool has_b_ = false;
};

} // namespace

// clang-format off
describe shell_leds("shell::LedCompose", $ {
	it("services first, then stack bottom to top; top wins per LED", _ {
		DeclaringService global(kPlay, on());
		DeclaringScene base(kPlay, off(), kSong, on());
		DeclaringScene overlay(kSong, blink(200));
		Service* services[] = {&global};
		Scene* stack[] = {&base, &overlay};

		LedFrame out;
		LedCompose::compose(out, services, stack);

		// The service declared Play On and the base overrode it; the base declared Song On and the
		// overlay above it overrode that. Two ids declared, not four entries.
		expect(out.entries().size()).to_equal(size_t(2));
		expect(declared(out, kPlay).kind == LedState::Kind::Off).to_be_true();
		expect(declared(out, kSong).kind == LedState::Kind::Blink).to_be_true();
		expect(declared(out, kSong).period_ms).to_equal(uint16_t(200));
	});

	it("a scene supplies the LEDs the scene above it does not mention", _ {
		DeclaringScene base(kPlay, on(), kSong, on());
		DeclaringScene overlay(kSong, off());
		Scene* stack[] = {&base, &overlay};

		LedFrame out;
		LedCompose::compose(out, std::span<Service* const>{}, stack);
		expect(declared(out, kPlay).kind == LedState::Kind::On).to_be_true();
		expect(declared(out, kSong).kind == LedState::Kind::Off).to_be_true();
	});

	it("blink phase is shared and until_ms folds an alert back to On", _ {
		const LedState forever = blink(200);
		const LedState alert = blink(200, 350);

		// Same period, same phase, whatever tick each was first declared on.
		expect(led_is_on(forever, 0)).to_be_true();
		expect(led_is_on(alert, 0)).to_be_true();
		expect(led_is_on(forever, 200)).to_be_false();
		expect(led_is_on(alert, 200)).to_be_false();
		expect(led_is_on(forever, 400)).to_be_true();

		// Past its deadline the alert is simply on, while the endless blink keeps blinking.
		expect(led_is_on(forever, 600)).to_be_false();
		expect(led_is_on(alert, 600)).to_be_true();

		// A different period is a different phase.
		expect(led_is_on(blink(100), 100)).to_be_false();
		expect(led_is_on(blink(100), 200)).to_be_true();
		// Neither On nor Off has a phase at all.
		expect(led_is_on(on(), 200)).to_be_true();
		expect(led_is_on(off(), 0)).to_be_false();
	});

	it("led_next_change reports exactly when a blink flips, and nothing for a state that cannot", _ {
		// The wake source `Shell::compute_next_wake` grew in E4 task 4 fix round 1. Every value
		// here is the moment `led_is_on` above answers differently, asserted against `led_is_on`
		// itself rather than restated -- so the two cannot drift apart.
		const LedState forever = blink(200);
		expect(led_next_change(forever, 0).has_value()).to_be_true();
		expect(*led_next_change(forever, 0)).to_equal(uint32_t(200));
		expect(*led_next_change(forever, 1)).to_equal(uint32_t(199));
		expect(*led_next_change(forever, 199)).to_equal(uint32_t(1));
		// On a boundary the answer is a full period, not zero: the flip AT `now_ms` is the one the
		// caller has already rendered.
		expect(*led_next_change(forever, 200)).to_equal(uint32_t(200));
		expect(led_is_on(forever, 199) != led_is_on(forever, 199 + *led_next_change(forever, 199))).to_be_true();

		// A bounded blink wakes at whichever comes first, its boundary or its deadline...
		const LedState alert = blink(200, 350);
		expect(*led_next_change(alert, 300)).to_equal(uint32_t(50)); // deadline 350 beats boundary 400
		expect(*led_next_change(alert, 100)).to_equal(uint32_t(100)); // boundary 200 beats deadline 350
		// ...and stops asking once it has latched solid on.
		expect(led_next_change(alert, 350).has_value()).to_be_false();
		expect(led_next_change(alert, 1000).has_value()).to_be_false();
		expect(led_is_on(alert, 350)).to_be_true();

		// Nothing else ever changes on its own.
		expect(led_next_change(on(), 0).has_value()).to_be_false();
		expect(led_next_change(off(), 0).has_value()).to_be_false();
		// A zero-period blink reads lit for ever (`led_is_on`'s divide-by-zero guard), so it has no
		// flip to wake for either.
		expect(led_next_change(blink(0), 0).has_value()).to_be_false();
		expect(led_is_on(blink(0), 0)).to_be_true();
	});

	it("writes only changes to the sink", _ {
		LedSurface s;
		const size_t play = static_cast<size_t>(static_cast<uint8_t>(kPlay));

		// Nothing declared it: not the shell's LED to drive.
		expect(led_send_needed(s, play)).to_be_false();

		s.desired_declared[play] = true;
		s.desired_on[play] = true;
		expect(led_send_needed(s, play)).to_be_true(); // never sent

		s.sent_once[play] = true;
		s.last_on[play] = true;
		expect(led_send_needed(s, play)).to_be_false(); // unchanged since

		s.desired_on[play] = false;
		expect(led_send_needed(s, play)).to_be_true(); // changed

		expect(led_send_needed(s, LedFrame::kMaxEntries)).to_be_false(); // out of range
	});
});
// clang-format on

DELUGE_SPEC(shell_leds_spec, shell_leds)
