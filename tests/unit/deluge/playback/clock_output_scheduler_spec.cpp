#include "playback/clock_output_scheduler.h"

#include <utility>

#include "cppspec.hpp"
#include "support/spec_main.h"

// Default test ratios matching common Deluge configuration:
// Analog: internalTicksPer=24, outTicksPer=24 (1:1 at magnitude 1)
// Input:  inputTicksPer=3, internalTicksPerInput=6 (1:2 at magnitude 1)
namespace {
constexpr uint32_t kDefaultInternalTicksPer = 24;
constexpr uint32_t kDefaultOutTicksPer = 24;
constexpr uint32_t kDefaultInputTicksPer = 3;
constexpr uint32_t kDefaultInternalTicksPerInput = 6;
constexpr uint32_t kDefaultTempo = 20000; // ~20ms per input tick ~ 125 BPM
constexpr uint32_t kDefaultInputTickTime = 100000;
} // namespace

// --- Group 1: Not behind -- just schedule ---

// clang-format off
describe clock_schedule_not_behind_group("clock schedule not behind", ${
	it("schedules next tick", _ {
		// currentInternalTick = 0 * 6 / 3 = 0
		// nextOutTick = 1, internalTickForNextOut = 1
		// 1 > 0, not behind -> schedule
		auto result =
		    computeExternalClockSchedule(0, 0, kDefaultInternalTicksPer, kDefaultOutTicksPer, kDefaultInputTicksPer,
		                                 kDefaultInternalTicksPerInput, kDefaultTempo, kDefaultInputTickTime);

		expect(std::to_underlying(result.action)).to_equal(std::to_underlying(ClockScheduleAction::Schedule));
		expect(result.scheduledTime).to_be_greater_than(kDefaultInputTickTime);
	});

	it("computes correct time", _ {
		// currentInternalTick = 2 * 6 / 3 = 4
		// nextOutTick = 5, internalTickForNextOut = 5
		// 5 > 4, not behind. internalTicksUntilNext = 1
		// timeUntilNext = 1 * 20000 * 3 / 6 = 10000
		// scheduledTime = 100000 + 10000 = 110000
		auto result =
		    computeExternalClockSchedule(4, 2, kDefaultInternalTicksPer, kDefaultOutTicksPer, kDefaultInputTicksPer,
		                                 kDefaultInternalTicksPerInput, kDefaultTempo, kDefaultInputTickTime);

		expect(std::to_underlying(result.action)).to_equal(std::to_underlying(ClockScheduleAction::Schedule));
		expect(result.scheduledTime).to_equal(110000u);
	});

	it("none when zero tempo", _ {
		auto result = computeExternalClockSchedule(0, 0, kDefaultInternalTicksPer, kDefaultOutTicksPer,
		                                           kDefaultInputTicksPer, kDefaultInternalTicksPerInput,
		                                           0, // timePerInputTickMovingAverage = 0
		                                           kDefaultInputTickTime);

		expect(std::to_underlying(result.action)).to_equal(std::to_underlying(ClockScheduleAction::None));
	});
});

// --- Group 2: Behind by one -- emit + schedule ---

describe clock_schedule_behind_by_one_group("clock schedule behind by one", ${
	it("emits one tick", _ {
		// currentInternalTick = 2 * 6 / 3 = 4
		// nextOutTick = 4, internalTickForNextOut = 4
		// 4 <= 4, behind (boundary) -> emit
		// tickAfterEmit = 5, internalTickForTickAfterEmit = 5
		// 5 > 4 -> schedule (not resync)
		auto result =
		    computeExternalClockSchedule(3, 2, kDefaultInternalTicksPer, kDefaultOutTicksPer, kDefaultInputTicksPer,
		                                 kDefaultInternalTicksPerInput, kDefaultTempo, kDefaultInputTickTime);

		expect(std::to_underlying(result.action)).to_equal(std::to_underlying(ClockScheduleAction::EmitAndSchedule));
	});

	it("schedules based on post emit state", _ {
		// After emit: nextOutTick=5, internalTickForNextOut=5
		// currentInternalTick=4, internalTicksUntilNext = 1
		// timeUntilNext = 1 * 20000 * 3 / 6 = 10000
		// scheduledTime = 100000 + 10000 = 110000
		auto result =
		    computeExternalClockSchedule(3, 2, kDefaultInternalTicksPer, kDefaultOutTicksPer, kDefaultInputTicksPer,
		                                 kDefaultInternalTicksPerInput, kDefaultTempo, kDefaultInputTickTime);

		expect(result.scheduledTime).to_equal(110000u);
	});
});

// --- Group 3: Behind by many -- emit + resync ---

describe clock_schedule_behind_by_many_group("clock schedule behind by many", ${
	it("resyncs when far behind", _ {
		// currentInternalTick = 10 * 6 / 3 = 20
		// nextOutTick=1, internalTickForNextOut=1
		// 1 <= 20, behind -> emit
		// tickAfterEmit=2, internalTickForTickAfterEmit=2
		// 2 <= 20, still behind -> resync
		auto result =
		    computeExternalClockSchedule(0, 10, kDefaultInternalTicksPer, kDefaultOutTicksPer, kDefaultInputTicksPer,
		                                 kDefaultInternalTicksPerInput, kDefaultTempo, kDefaultInputTickTime);

		expect(std::to_underlying(result.action)).to_equal(std::to_underlying(ClockScheduleAction::EmitAndResync));
	});
});

// --- Group 4: Edge cases ---

describe clock_schedule_edge_cases_group("clock schedule edge cases", ${
	it("exactly on boundary", _ {
		// currentInternalTick = 4, nextOutTick=4, internalTickForNextOut=4
		// 4 <= 4 -> behind (boundary), should emit
		auto result =
		    computeExternalClockSchedule(3, 2, kDefaultInternalTicksPer, kDefaultOutTicksPer, kDefaultInputTicksPer,
		                                 kDefaultInternalTicksPerInput, kDefaultTempo, kDefaultInputTickTime);

		expect(std::to_underlying(result.action)).to_equal(std::to_underlying(ClockScheduleAction::EmitAndSchedule));
	});

	it("zero input ticks", _ {
		auto result =
		    computeExternalClockSchedule(0, 0, kDefaultInternalTicksPer, kDefaultOutTicksPer, kDefaultInputTicksPer,
		                                 kDefaultInternalTicksPerInput, kDefaultTempo, kDefaultInputTickTime);

		// currentInternalTick=0, nextOutTick=1, internalTickForNextOut=1
		// 1 > 0, not behind -> just schedule
		expect(std::to_underlying(result.action)).to_equal(std::to_underlying(ClockScheduleAction::Schedule));
	});

	it("negative last input tick", _ {
		// lastInputTickReceived=-1 is a real initialization value
		auto result =
		    computeExternalClockSchedule(-1, -1, kDefaultInternalTicksPer, kDefaultOutTicksPer, kDefaultInputTicksPer,
		                                 kDefaultInternalTicksPerInput, kDefaultTempo, kDefaultInputTickTime);

		// currentInternalTick = -1 * 6 / 3 = -2
		// nextOutTick = 0, internalTickForNextOut = 0
		// 0 > -2, not behind -> schedule
		expect(std::to_underlying(result.action)).to_equal(std::to_underlying(ClockScheduleAction::Schedule));
	});

	it("negative last tick done", _ {
		// lastTickDone=-1 (initial state) with a normal input tick
		auto result =
		    computeExternalClockSchedule(-1, 1, kDefaultInternalTicksPer, kDefaultOutTicksPer, kDefaultInputTicksPer,
		                                 kDefaultInternalTicksPerInput, kDefaultTempo, kDefaultInputTickTime);

		// currentInternalTick = 1 * 6 / 3 = 2
		// nextOutTick = 0, internalTickForNextOut = 0
		// 0 <= 2, behind -> emit
		// tickAfterEmit = 1, internalTickForTickAfterEmit = 1
		// 1 <= 2 -> still behind -> resync
		expect(std::to_underlying(result.action)).to_equal(std::to_underlying(ClockScheduleAction::EmitAndResync));
	});

	it("large tick counts", _ {
		// Millions of ticks -- verify no overflow
		auto result = computeExternalClockSchedule(1000000, 500001, kDefaultInternalTicksPer, kDefaultOutTicksPer,
		                                           kDefaultInputTicksPer, kDefaultInternalTicksPerInput, kDefaultTempo,
		                                           kDefaultInputTickTime);

		// currentInternalTick = 500001 * 6 / 3 = 1000002
		// nextOutTick = 1000001, internalTickForNextOut = 1000001
		// 1000001 <= 1000002, behind -> emit
		// tickAfterEmit = 1000002, internalTickForTickAfterEmit = 1000002
		// 1000002 < 1000002? NO (boundary) -> schedule at time=now for ISR
		expect(std::to_underlying(result.action)).to_equal(std::to_underlying(ClockScheduleAction::EmitAndSchedule));
		expect(result.scheduledTime).to_equal(kDefaultInputTickTime);
	});

	it("one to one ratio", _ {
		auto result = computeExternalClockSchedule(5, 3, 1, 1, // 1:1 out-to-internal ratio
		                                           1, 2,       // 1:2 input-to-internal ratio
		                                           kDefaultTempo, kDefaultInputTickTime);

		// currentInternalTick = 3 * 2 / 1 = 6
		// nextOutTick = 6, internalTickForNextOut = 6
		// 6 <= 6, behind -> emit
		// tickAfterEmit = 7, internalTickForTickAfterEmit = 7
		// 7 > 6 -> schedule
		expect(std::to_underlying(result.action)).to_equal(std::to_underlying(ClockScheduleAction::EmitAndSchedule));
	});

	it("high multiplier ratio", _ {
		auto result = computeExternalClockSchedule(0, 2, 1, 4, // 1:4 ratio
		                                           3, 6, kDefaultTempo, kDefaultInputTickTime);

		// currentInternalTick = 2 * 6 / 3 = 4
		// nextOutTick=1, internalTickForNextOut = 1 * 1 / 4 = 0 (integer division)
		// 0 <= 4, behind -> emit
		// tickAfterEmit=2, internalTickForTickAfterEmit = 2 * 1 / 4 = 0
		// 0 <= 4, still behind -> resync
		expect(std::to_underlying(result.action)).to_equal(std::to_underlying(ClockScheduleAction::EmitAndResync));
	});
});

// --- Group 5: Ratio math and overflow ---

describe clock_schedule_ratio_math_group("clock schedule ratio math", ${
	it("default analog ratio", _ {
		auto result = computeExternalClockSchedule(9, 5, 24, 24, 3, 6, kDefaultTempo, kDefaultInputTickTime);

		// currentInternalTick = 10, nextOutTick=10, internalTickForNextOut=10
		// 10 <= 10, behind -> emit
		// tickAfterEmit=11, internalTickForTickAfterEmit=11
		// 11 > 10 -> schedule
		expect(std::to_underlying(result.action)).to_equal(std::to_underlying(ClockScheduleAction::EmitAndSchedule));
	});

	it("default MIDI ratio", _ {
		auto result = computeExternalClockSchedule(4, 5, 2, 1, 3, 6, kDefaultTempo, kDefaultInputTickTime);

		// currentInternalTick = 10, nextOutTick=5, internalTickForNextOut=10
		// 10 <= 10, behind -> emit
		// tickAfterEmit=6, internalTickForTickAfterEmit=12
		// 12 > 10 -> schedule
		expect(std::to_underlying(result.action)).to_equal(std::to_underlying(ClockScheduleAction::EmitAndSchedule));
	});

	it("non power of two ratio", _ {
		auto result = computeExternalClockSchedule(2, 3, 3, 2, 3, 6, kDefaultTempo, kDefaultInputTickTime);

		// currentInternalTick = 6
		// nextOutTick=3, internalTickForNextOut = 9/2 = 4 (truncated)
		// 4 <= 6, behind -> emit
		// tickAfterEmit=4, internalTickForTickAfterEmit = 12/2 = 6
		// 6 < 6? NO (boundary) -> schedule at time=now for ISR
		expect(std::to_underlying(result.action)).to_equal(std::to_underlying(ClockScheduleAction::EmitAndSchedule));
		expect(result.scheduledTime).to_equal(kDefaultInputTickTime);
	});

	it("time computation accuracy", _ {
		auto result = computeExternalClockSchedule(9, 5, 24, 24, 3, 6, 20000, 100000);

		// After emit: nextOutTick=11, internalTickForNextOut=11
		// internalTicksUntilNext = 11 - 10 = 1
		// timeUntilNext = 1 * 20000 * 3 / 6 = 10000
		// scheduledTime = 100000 + 10000 = 110000
		expect(std::to_underlying(result.action)).to_equal(std::to_underlying(ClockScheduleAction::EmitAndSchedule));
		expect(result.scheduledTime).to_equal(110000u);
	});

	it("high magnitude no overflow", _ {
		// internalTicksPer = 24 << 4 = 384, internalTicksPerInput = 6 << 4 = 96
		auto result = computeExternalClockSchedule(100, 50, 384, 24, 3, 96, 20000, 100000);

		// currentInternalTick = 50 * 96 / 3 = 1600
		// nextOutTick=101, internalTickForNextOut = 101 * 384 / 24 = 1616
		// 1616 > 1600, not behind -> schedule
		// internalTicksUntilNext = 16
		// timeUntilNext = 16 * 20000 * 3 / 96 = 10000
		// scheduledTime = 100000 + 10000 = 110000
		expect(std::to_underlying(result.action)).to_equal(std::to_underlying(ClockScheduleAction::Schedule));
		expect(result.scheduledTime).to_equal(110000u);
	});
});

DELUGE_SPEC(clock_output_scheduler_spec, clock_schedule_not_behind_group, clock_schedule_behind_by_one_group, clock_schedule_behind_by_many_group, clock_schedule_edge_cases_group, clock_schedule_ratio_math_group)
