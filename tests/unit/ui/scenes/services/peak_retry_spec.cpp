// A waveform view whose read left columns pending draws again every poll while columns keep being answered or their
// index keeps growing, stops after the give-up time with neither, and starts again when a read answers one more or
// reads another window. A view with no give-up asks again for as long as a column is pending.
#include "ui/scenes/services/peak_retry.h"

#include "cppspec.hpp"

#include "support/spec_main.h"

#include <cstdint>

using namespace deluge::ui::scenes::services;
using deluge::ui::views::waveform::ColumnRead;
using deluge::ui::views::waveform::PeakRead;
using deluge::ui::views::waveform::Window;

namespace {

/// The window every frame reads unless a case moves the view.
constexpr Window kAt{.scroll = 0, .zoom = 100};
/// Another window: the view scrolled.
constexpr Window kScrolled{.scroll = 1600, .zoom = 100};

/// @brief A read with the first @p answered columns answered and the rest pending.
PeakRead answering(int32_t answered) {
	PeakRead read;
	for (int32_t c = 0; c < answered; c++) {
		read.set(c, ColumnRead::of({.min = -c, .max = c}));
	}
	return read;
}

/// @brief A read with no column answered whose pending columns' index holds @p known frames.
PeakRead filling(uint64_t known) {
	PeakRead read;
	read.add_progress(known);
	return read;
}

} // namespace

// clang-format off
describe scenes_peak_retry("a view's redraws of a pending waveform", ${
	it("does nothing for a read with nothing pending", _ {
		PeakRetry retry;
		expect(retry.after(answering(16), kAt, 0)).to_equal(PeakRetry::Next::DONE);
		expect(retry.retrying()).to_be_false();
		expect(retry.after(PeakRead::all_past_end(), kAt, 5)).to_equal(PeakRetry::Next::DONE);
	});
	it("asks again for a pending read, and starts retrying", _ {
		PeakRetry retry;
		expect(retry.after(answering(3), kAt, 100)).to_equal(PeakRetry::Next::ASK_AGAIN);
		expect(retry.retrying()).to_be_true();
		expect(retry.gave_up()).to_be_false();
	});
	it("keeps asking within the give-up time of the last progress, even with no more columns", _ {
		PeakRetry retry;
		(void)retry.after(answering(3), kAt, 100);
		expect(retry.after(answering(3), kAt, 100 + kPeakRetryGiveUpMs - 1)).to_equal(PeakRetry::Next::ASK_AGAIN);
		expect(retry.after(answering(5), kAt, 100 + kPeakRetryGiveUpMs + 50)).to_equal(PeakRetry::Next::ASK_AGAIN);
		expect(retry.after(answering(5), kAt, 100 + kPeakRetryGiveUpMs + 50 + kPeakRetryGiveUpMs - 1))
		    .to_equal(PeakRetry::Next::ASK_AGAIN);
	});
	it("gives up once at the give-up time with no progress, then waits", _ {
		PeakRetry retry;
		(void)retry.after(answering(3), kAt, 100);
		expect(retry.after(answering(3), kAt, 100 + kPeakRetryGiveUpMs)).to_equal(PeakRetry::Next::GIVE_UP);
		expect(retry.gave_up()).to_be_true();
		expect(retry.after(answering(3), kAt, 100 + kPeakRetryGiveUpMs + 10)).to_equal(PeakRetry::Next::WAIT);
		expect(retry.after(answering(2), kAt, 100 + kPeakRetryGiveUpMs + 20)).to_equal(PeakRetry::Next::WAIT);
	});
	it("asks again once a read answers one more column after waiting, from a fresh clock, and gives up once a stall", _ {
		PeakRetry retry;
		(void)retry.after(answering(3), kAt, 100);
		(void)retry.after(answering(3), kAt, 100 + kPeakRetryGiveUpMs);
		(void)retry.after(answering(3), kAt, 100 + kPeakRetryGiveUpMs + 10);
		const uint32_t later = 100 + kPeakRetryGiveUpMs + 500;
		expect(retry.after(answering(4), kAt, later)).to_equal(PeakRetry::Next::ASK_AGAIN);
		expect(retry.after(answering(4), kAt, later + kPeakRetryGiveUpMs - 1)).to_equal(PeakRetry::Next::ASK_AGAIN);
		// A second stall past the new progress stops the redraws without a second give-up.
		expect(retry.after(answering(4), kAt, later + kPeakRetryGiveUpMs)).to_equal(PeakRetry::Next::WAIT);
		expect(retry.gave_up()).to_be_true();
	});
	it("forgets everything on a restart", _ {
		PeakRetry retry;
		(void)retry.after(answering(3), kAt, 100);
		(void)retry.after(answering(3), kAt, 100 + kPeakRetryGiveUpMs);
		retry.restart();
		expect(retry.retrying()).to_be_false();
		expect(retry.gave_up()).to_be_false();
		expect(retry.after(answering(1), kAt, 10'000)).to_equal(PeakRetry::Next::ASK_AGAIN);
	});
	it("starts over on a read of another window: a given-up view asks again once its window moves", _ {
		PeakRetry retry;
		(void)retry.after(answering(3), kAt, 100);
		(void)retry.after(answering(3), kAt, 100 + kPeakRetryGiveUpMs);
		expect(retry.gave_up()).to_be_true();
		expect(retry.after(answering(3), kScrolled, 100 + kPeakRetryGiveUpMs + 10)).to_equal(PeakRetry::Next::ASK_AGAIN);
		expect(retry.gave_up()).to_be_false();
	});
	it("starts over on a read of another window: the new view's clock starts at its first frame", _ {
		PeakRetry retry;
		(void)retry.after(answering(8), kAt, 100);
		// Mid-retry at the old window, the new view answers fewer columns; it has its own give-up time.
		const uint32_t moved = 100 + kPeakRetryGiveUpMs - 1;
		expect(retry.after(answering(2), kScrolled, moved)).to_equal(PeakRetry::Next::ASK_AGAIN);
		expect(retry.after(answering(2), kScrolled, moved + kPeakRetryGiveUpMs - 1)).to_equal(PeakRetry::Next::ASK_AGAIN);
		expect(retry.after(answering(2), kScrolled, moved + kPeakRetryGiveUpMs)).to_equal(PeakRetry::Next::GIVE_UP);
	});
	it("is retrying only the window its last frame read", _ {
		PeakRetry retry;
		(void)retry.after(answering(3), kAt, 100);
		expect(retry.retrying(kAt)).to_be_true();
		expect(retry.retrying(kScrolled)).to_be_false();
	});
	it("with no give-up, asks again for as long as a column is pending", _ {
		PeakRetry retry{PeakRetry::Patience::UNTIL_ANSWERED};
		expect(retry.after(answering(3), kAt, 100)).to_equal(PeakRetry::Next::ASK_AGAIN);
		expect(retry.after(answering(3), kAt, 100 + kPeakRetryGiveUpMs)).to_equal(PeakRetry::Next::ASK_AGAIN);
		expect(retry.after(answering(3), kAt, 100 + 100 * kPeakRetryGiveUpMs)).to_equal(PeakRetry::Next::ASK_AGAIN);
		expect(retry.gave_up()).to_be_false();
		expect(retry.retrying()).to_be_true();
		expect(retry.after(answering(16), kAt, 100 + 100 * kPeakRetryGiveUpMs + 10)).to_equal(PeakRetry::Next::DONE);
		expect(retry.retrying()).to_be_false();
	});
	it("keeps asking while the pending columns' index grows, with no column newly answered", _ {
		PeakRetry retry;
		(void)retry.after(filling(0), kAt, 100);
		for (uint32_t step = 1; step <= 10; step++) {
			expect(retry.after(filling(step * 22'000), kAt, 100 + step * (kPeakRetryGiveUpMs - 1)))
			    .to_equal(PeakRetry::Next::ASK_AGAIN);
		}
		expect(retry.gave_up()).to_be_false();
	});
	it("gives up when the pending columns' index stops growing", _ {
		PeakRetry retry;
		(void)retry.after(filling(0), kAt, 100);
		expect(retry.after(filling(22'000), kAt, 200)).to_equal(PeakRetry::Next::ASK_AGAIN);
		expect(retry.after(filling(22'000), kAt, 200 + kPeakRetryGiveUpMs - 1)).to_equal(PeakRetry::Next::ASK_AGAIN);
		expect(retry.after(filling(22'000), kAt, 200 + kPeakRetryGiveUpMs)).to_equal(PeakRetry::Next::GIVE_UP);
	});
	it("counts a column newly answered as progress even when the pending columns then hold fewer folded frames", _ {
		PeakRetry retry;
		PeakRead before = filling(50'000);
		(void)retry.after(before, kAt, 100);
		PeakRead after = answering(1);
		after.add_progress(10'000); // the answered column's 50,000 frames left the sum
		expect(retry.after(after, kAt, 100 + kPeakRetryGiveUpMs - 1)).to_equal(PeakRetry::Next::ASK_AGAIN);
		after.add_progress(1'000);
		expect(retry.after(after, kAt, 100 + 2 * kPeakRetryGiveUpMs - 2)).to_equal(PeakRetry::Next::ASK_AGAIN);
		expect(retry.after(after, kAt, 100 + 3 * kPeakRetryGiveUpMs - 2)).to_equal(PeakRetry::Next::GIVE_UP);
	});
	it("measures across the millisecond clock's wrap", _ {
		PeakRetry retry;
		(void)retry.after(answering(3), kAt, 0xFFFF'FFF0);
		expect(retry.after(answering(3), kAt, 0x10)).to_equal(PeakRetry::Next::ASK_AGAIN);
		expect(retry.after(answering(3), kAt, 0xFFFF'FFF0 + kPeakRetryGiveUpMs)).to_equal(PeakRetry::Next::GIVE_UP);
	});
});
// clang-format on

DELUGE_SPEC(peak_retry_spec, scenes_peak_retry)
