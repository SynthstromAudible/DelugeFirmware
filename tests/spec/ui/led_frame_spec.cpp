#include "../support/spec_main.h"
#include "cppspec.hpp"
#include "ui/core/led_frame.h"

using namespace deluge::ui;

// clang-format off
describe led_frame("ui::LedFrame", $ {
	it("records entries in call order and lets a later set win", _ {
		LedFrame f;
		// set()/indicator() are [[nodiscard]] bool -- false means the declaration was dropped. All four
		// of these fit, so all four report true; the full-frame case is covered below.
		expect(f.set(LedId{3}, LedState{LedState::Kind::On, 0, 0})).to_be_true();
		expect(f.set(LedId{5}, LedState{LedState::Kind::On, 0, 0})).to_be_true();      // second, distinct id
		expect(f.set(LedId{3}, LedState{LedState::Kind::Blink, 200, 0})).to_be_true(); // overwrites id 3 in place
		expect(f.indicator(IndicatorId::Lower, KnobIndicator{{1, 2, 3, 0}})).to_be_true();

		// Two entries, not three: the repeat set() overwrote rather than appended. Order is
		// call order (3 then 5), not id order or insertion-reversed -- an implementation that
		// inserted at the front, or sorted by id, would fail these two index checks.
		expect(f.entries().size()).to_equal(size_t(2));
		expect(f.entries()[0].first == LedId{3}).to_be_true();
		expect(f.entries()[0].second.kind == LedState::Kind::Blink).to_be_true();
		expect(f.entries()[1].first == LedId{5}).to_be_true();
		expect(f.entries()[1].second.kind == LedState::Kind::On).to_be_true();
		expect(f.indicator_or_null(IndicatorId::Lower)->levels[2]).to_equal(uint8_t(3));
		expect(f.indicator_or_null(IndicatorId::Upper) == nullptr).to_be_true();

		f.clear();
		expect(f.entries().empty()).to_be_true();
		expect(f.indicator_or_null(IndicatorId::Lower) == nullptr).to_be_true(); // indicator reset too, not just entries
		expect(f.indicator_or_null(IndicatorId::Upper) == nullptr).to_be_true();
	});

	it("reports the dropped declaration when the frame is full", _ {
		LedFrame f;
		for (size_t i = 0; i < LedFrame::kMaxEntries; i++) {
			expect(f.set(LedId(i), LedState{LedState::Kind::On, 0, 0})).to_be_true();
		}
		// kMaxEntries distinct ids fit; the next distinct id does not, and says so rather than
		// vanishing. An id already in the frame still succeeds -- it overwrites, it does not append.
		expect(f.set(LedId(LedFrame::kMaxEntries), LedState{LedState::Kind::On, 0, 0})).to_be_false();
		expect(f.set(LedId(0), LedState{LedState::Kind::Blink, 100, 0})).to_be_true();
		expect(f.entries().size()).to_equal(LedFrame::kMaxEntries);

		// An out-of-range indicator is refused, not stored.
		expect(f.indicator(IndicatorId{2}, KnobIndicator{{9, 9, 9, 9}})).to_be_false();
	});
});
DELUGE_SPEC(ui_led_frame_spec, led_frame)
