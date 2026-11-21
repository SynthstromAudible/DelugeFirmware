// A waveform view whose read left columns pending draws again every poll while columns keep being answered, stops
// after the give-up time with none newly answered, and starts again when a read answers one more.
#include "scenes/services/peak_retry.h"

#include "cppspec.hpp"

#include "support/spec_main.h"

#include <cstdint>

using namespace deluge::scenes::services;
using deluge::sample_view::ColumnRead;
using deluge::sample_view::PeakRead;

namespace {

/// @brief A read with the first @p answered columns answered and the rest pending.
PeakRead answering(int32_t answered) {
	PeakRead read;
	for (int32_t c = 0; c < answered; c++) {
		read.set(c, ColumnRead::of({.min = -c, .max = c}));
	}
	return read;
}

} // namespace

// clang-format off
describe scenes_peak_retry("a view's redraws of a pending waveform", ${
	it("does nothing for a read with nothing pending", _ {
		PeakRetry retry;
		expect(retry.after(answering(16), 0)).to_equal(PeakRetry::Next::DONE);
		expect(retry.retrying()).to_be_false();
		expect(retry.after(PeakRead::all_past_end(), 5)).to_equal(PeakRetry::Next::DONE);
	});
	it("asks again for a pending read, and starts retrying", _ {
		PeakRetry retry;
		expect(retry.after(answering(3), 100)).to_equal(PeakRetry::Next::ASK_AGAIN);
		expect(retry.retrying()).to_be_true();
		expect(retry.gave_up()).to_be_false();
	});
	it("keeps asking within the give-up time of the last progress, even with no more columns", _ {
		PeakRetry retry;
		(void)retry.after(answering(3), 100);
		expect(retry.after(answering(3), 100 + kPeakRetryGiveUpMs - 1)).to_equal(PeakRetry::Next::ASK_AGAIN);
		expect(retry.after(answering(5), 100 + kPeakRetryGiveUpMs + 50)).to_equal(PeakRetry::Next::ASK_AGAIN);
		expect(retry.after(answering(5), 100 + kPeakRetryGiveUpMs + 50 + kPeakRetryGiveUpMs - 1))
		    .to_equal(PeakRetry::Next::ASK_AGAIN);
	});
	it("gives up once at the give-up time with no progress, then waits", _ {
		PeakRetry retry;
		(void)retry.after(answering(3), 100);
		expect(retry.after(answering(3), 100 + kPeakRetryGiveUpMs)).to_equal(PeakRetry::Next::GIVE_UP);
		expect(retry.gave_up()).to_be_true();
		expect(retry.after(answering(3), 100 + kPeakRetryGiveUpMs + 10)).to_equal(PeakRetry::Next::WAIT);
		expect(retry.after(answering(2), 100 + kPeakRetryGiveUpMs + 20)).to_equal(PeakRetry::Next::WAIT);
	});
	it("asks again once a read answers one more column after waiting, from a fresh clock", _ {
		PeakRetry retry;
		(void)retry.after(answering(3), 100);
		(void)retry.after(answering(3), 100 + kPeakRetryGiveUpMs);
		(void)retry.after(answering(3), 100 + kPeakRetryGiveUpMs + 10);
		const uint32_t later = 100 + kPeakRetryGiveUpMs + 500;
		expect(retry.after(answering(4), later)).to_equal(PeakRetry::Next::ASK_AGAIN);
		expect(retry.after(answering(4), later + kPeakRetryGiveUpMs - 1)).to_equal(PeakRetry::Next::ASK_AGAIN);
	});
	it("forgets everything on a restart", _ {
		PeakRetry retry;
		(void)retry.after(answering(3), 100);
		(void)retry.after(answering(3), 100 + kPeakRetryGiveUpMs);
		retry.restart();
		expect(retry.retrying()).to_be_false();
		expect(retry.gave_up()).to_be_false();
		expect(retry.after(answering(1), 10'000)).to_equal(PeakRetry::Next::ASK_AGAIN);
	});
	it("measures across the millisecond clock's wrap", _ {
		PeakRetry retry;
		(void)retry.after(answering(3), 0xFFFF'FFF0);
		expect(retry.after(answering(3), 0x10)).to_equal(PeakRetry::Next::ASK_AGAIN);
		expect(retry.after(answering(3), 0xFFFF'FFF0 + kPeakRetryGiveUpMs)).to_equal(PeakRetry::Next::GIVE_UP);
	});
});
// clang-format on

DELUGE_SPEC(peak_retry_spec, scenes_peak_retry)
