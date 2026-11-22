#include "cppspec.hpp"
#include "support/spec_main.h"
#include "ui/widgets/core/led_frame.h"

using namespace deluge::ui;

// clang-format off
describe led_frame("ui::LedFrame", $ {
	it("records entries in call order and lets a later set win", _ {
		widgets::core::LedFrame f;
		// set()/indicator() are [[nodiscard]] bool -- false means the declaration was dropped. All four
		// of these fit, so all four report true; the full-frame case is covered below.
		expect(f.set(widgets::core::LedId{3}, widgets::core::LedState{widgets::core::LedState::Kind::On, 0, 0})).to_be_true();
		expect(f.set(widgets::core::LedId{5}, widgets::core::LedState{widgets::core::LedState::Kind::On, 0, 0})).to_be_true();      // second, distinct id
		expect(f.set(widgets::core::LedId{3}, widgets::core::LedState{widgets::core::LedState::Kind::Blink, 200, 0})).to_be_true(); // overwrites id 3 in place
		expect(f.indicator(widgets::core::IndicatorId::Lower, widgets::core::KnobIndicator{{1, 2, 3, 0}})).to_be_true();

		// Two entries, not three: the repeat set() overwrote rather than appended. Order is
		// call order (3 then 5), not id order or insertion-reversed -- an implementation that
		// inserted at the front, or sorted by id, would fail these two index checks.
		expect(f.entries().size()).to_equal(size_t(2));
		expect(f.entries()[0].first).to_equal(widgets::core::LedId{3});
		expect(f.entries()[0].second.kind).to_equal(widgets::core::LedState::Kind::Blink);
		expect(f.entries()[1].first).to_equal(widgets::core::LedId{5});
		expect(f.entries()[1].second.kind).to_equal(widgets::core::LedState::Kind::On);
		expect(f.indicator_or_null(widgets::core::IndicatorId::Lower)->levels[2]).to_equal(uint8_t(3));
		expect(f.indicator_or_null(widgets::core::IndicatorId::Upper)).to_be_null();

		f.clear();
		expect(f.entries().empty()).to_be_true();
		expect(f.indicator_or_null(widgets::core::IndicatorId::Lower)).to_be_null(); // indicator reset too, not just entries
		expect(f.indicator_or_null(widgets::core::IndicatorId::Upper)).to_be_null();
	});

	it("carries the tick's clock and the restarts its declarers asked for, which clear() forgets", _ {
		using widgets::core::BlinkRate;
		widgets::core::LedFrame f;
		f.begin(1234);
		expect(f.now_ms()).to_equal(uint32_t(1234));
		expect(f.restart_requested(BlinkRate::Normal).has_value()).to_be_false();
		f.restart(BlinkRate::Normal, false);
		f.restart(BlinkRate::Fast);
		expect(*f.restart_requested(BlinkRate::Normal)).to_be_false(); // initially dark
		expect(*f.restart_requested(BlinkRate::Fast)).to_be_true();
		f.begin(2000);
		expect(f.restart_requested(BlinkRate::Normal).has_value()).to_be_false();
	});

	it("reports the dropped declaration when the frame is full", _ {
		widgets::core::LedFrame f;
		for (size_t i = 0; i < widgets::core::LedFrame::kMaxEntries; i++) {
			expect(f.set(widgets::core::LedId(i), widgets::core::LedState{widgets::core::LedState::Kind::On, 0, 0})).to_be_true();
		}
		// kMaxEntries distinct ids fit; the next distinct id does not, and says so rather than
		// vanishing. An id already in the frame still succeeds -- it overwrites, it does not append.
		expect(f.set(widgets::core::LedId(widgets::core::LedFrame::kMaxEntries), widgets::core::LedState{widgets::core::LedState::Kind::On, 0, 0})).to_be_false();
		expect(f.set(widgets::core::LedId(0), widgets::core::LedState{widgets::core::LedState::Kind::Blink, 100, 0})).to_be_true();
		expect(f.entries().size()).to_equal(widgets::core::LedFrame::kMaxEntries);

		// An out-of-range indicator is refused, not stored.
		expect(f.indicator(widgets::core::IndicatorId{2}, widgets::core::KnobIndicator{{9, 9, 9, 9}})).to_be_false();
	});
});
DELUGE_SPEC(led_frame_spec, led_frame)
