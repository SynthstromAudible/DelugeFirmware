// tests/unit/runtime/app_loop_spec.cpp
#include "runtime/app_loop.h"

#include "cppspec.hpp"

#include <vector>

using deluge::runtime::JobId;
using deluge::runtime::kNever;
using deluge::runtime::kNoJob;
using deluge::runtime::Loop;
using deluge::runtime::Micros;

extern "C" double getSystemTime() noexcept {
	return 0.0;
}
extern "C" void deluge_app_wake(uint32_t) noexcept {
}

namespace {
Micros g_now = 0;
int g_wakes = 0;
Micros clock() {
	return g_now;
}
void waker() {
	++g_wakes;
}

std::vector<char> g_log;
Loop* g_loop = nullptr;
JobId g_self = kNoJob;
Micros g_body_time = 0;

void bodyA() {
	g_log.push_back('a');
	g_now += g_body_time;
}
void bodyB() {
	g_log.push_back('b');
	g_now += g_body_time;
}
void bodyC() {
	g_log.push_back('c');
	g_now += g_body_time;
}
void bodyThrows() {
	g_log.push_back('t');
	throw 1;
}
void selfRequesting() {
	g_log.push_back('s');
	g_loop->request(g_self);
}
void selfCancelling() {
	g_log.push_back('c');
	g_loop->cancel(g_self);
}

void reset() {
	g_now = 1000;
	g_wakes = 0;
	g_log.clear();
	g_body_time = 0;
	g_self = kNoJob;
}

/// Runs service() until the pass it starts has ended; returns the last call's answer. The reasons go
/// to the first call only, as a driver hands them over once.
Micros pass(Loop& loop, Micros now, deluge::runtime::WakeMask reasons) {
	Micros r = loop.service(now, reasons);
	while (loop.inPass()) {
		r = loop.service(now, 0);
	}
	return r;
}
} // namespace

// clang-format off
describe app_loop("deluge::runtime::Loop", $ {
	before_each([] { reset(); });

	it("runs a timed job once its gap has passed since it was added", _ {
		Loop loop{clock, waker};
		loop.add("a", bodyA, 100, 0);
		expect(pass(loop, 1050, 0)).to_equal(Micros{1100});
		expect(g_log.size()).to_equal(size_t{0});
		g_now = 1100;
		pass(loop, 1100, 0);
		expect(g_log.size()).to_equal(size_t{1});
	});

	it("measures the gap from the end of a run", _ {
		Loop loop{clock, waker};
		loop.add("a", bodyA, 100, 0);
		g_now = 1100;
		g_body_time = 30;
		// The run ends at 1130, so the next is due at 1230.
		expect(pass(loop, 1100, 0)).to_equal(Micros{1230});
	});

	it("runs eligible jobs once each, in registration order", _ {
		Loop loop{clock, waker};
		loop.add("a", bodyA, 0, 0);
		loop.add("b", bodyB, 0, 0);
		pass(loop, 1000, 0);
		expect(g_log).to_equal(std::vector<char>{'a', 'b'});
	});

	it("runs a wake-only job only when a reason it listens for arrives", _ {
		Loop loop{clock, waker};
		loop.add("a", bodyA, kNever, 0b10);
		expect(pass(loop, 5000, 0b01)).to_equal(kNever);
		expect(g_log.size()).to_equal(size_t{0});
		pass(loop, 5000, 0b10);
		expect(g_log.size()).to_equal(size_t{1});
	});

	it("runs a requested job on the next pass and wakes the loop", _ {
		Loop loop{clock, waker};
		JobId a = loop.add("a", bodyA, kNever, 0);
		loop.request(a);
		expect(g_wakes).to_equal(1);
		pass(loop, 1000, 0);
		expect(g_log.size()).to_equal(size_t{1});
		// Consumed: the next pass does not run it again.
		pass(loop, 1000, 0);
		expect(g_log.size()).to_equal(size_t{1});
	});

	it("runs a job that requests itself again on the next pass, not the same one", _ {
		Loop loop{clock, waker};
		g_loop = &loop;
		g_self = loop.add("s", selfRequesting, kNever, 0);
		loop.request(g_self);
		expect(pass(loop, 1000, 0)).to_equal(Micros{1000});
		expect(g_log.size()).to_equal(size_t{1});
		pass(loop, 1000, 0);
		expect(g_log.size()).to_equal(size_t{2});
	});

	it("never runs a job again once it cancels itself", _ {
		Loop loop{clock, waker};
		g_loop = &loop;
		g_self = loop.add("c", selfCancelling, 0, 0);
		pass(loop, 1000, 0);
		loop.request(g_self);
		expect(pass(loop, 2000, 0)).to_equal(kNever);
		expect(g_log).to_equal(std::vector<char>{'c'});
	});

	it("records each job's last and worst run, and its run count", _ {
		Loop loop{clock, waker};
		JobId a = loop.add("a", bodyA, 0, 0);
		g_body_time = 30;
		pass(loop, 1000, 0);
		g_body_time = 10;
		pass(loop, g_now, 0);
		auto s = loop.stats(a);
		expect(s.runs).to_equal(uint32_t{2});
		expect(s.lastRun).to_equal(Micros{10});
		expect(s.maxRun).to_equal(Micros{30});
	});

	it("zeroes worst runs and counts on resetStats, and keeps the last run", _ {
		Loop loop{clock, waker};
		JobId a = loop.add("a", bodyA, 0, 0);
		g_body_time = 30;
		pass(loop, 1000, 0);
		loop.resetStats();
		auto s = loop.stats(a);
		expect(s.runs).to_equal(uint32_t{0});
		expect(s.maxRun).to_equal(Micros{0});
		expect(s.lastRun).to_equal(Micros{30});
	});

	it("reports an empty JobStats for an id never added", _ {
		Loop loop{clock, waker};
		expect(loop.stats(3).runs).to_equal(uint32_t{0});
		expect(loop.stats(3).name == nullptr).to_be_truthy();
	});

	it("runs one job per call and resumes the pass where it left off", _ {
		Loop loop{clock, waker};
		loop.add("a", bodyA, 0, 0);
		loop.add("b", bodyB, 0, 0);
		g_body_time = 5;
		// A job ran: the answer is the clock after it, to be called again after a yield.
		expect(loop.service(1000, 0)).to_equal(Micros{1005});
		expect(g_log).to_equal(std::vector<char>{'a'});
		expect(loop.inPass()).to_be_truthy();
		loop.service(g_now, 0);
		expect(g_log).to_equal(std::vector<char>{'a', 'b'});
		expect(loop.inPass()).to_be_truthy();
		// Nothing left in the pass: it ends, and the answer is when the next one is due (a's, which
		// ended at 1005 with no gap).
		expect(loop.service(g_now, 0)).to_equal(Micros{1005});
		expect(loop.inPass()).to_be_falsy();
		expect(g_log).to_equal(std::vector<char>{'a', 'b'});
	});

	it("a reason arriving mid-pass reaches jobs earlier in the order on the next pass", _ {
		Loop loop{clock, waker};
		loop.add("a", bodyA, kNever, 0b10);
		loop.add("b", bodyB, 0, 0);
		loop.add("c", bodyC, kNever, 0b10);
		loop.service(1000, 0);
		expect(g_log).to_equal(std::vector<char>{'b'});
		// Mid-pass: c, still ahead, runs in this pass; a, already behind, does not.
		loop.service(1000, 0b10);
		expect(g_log).to_equal(std::vector<char>{'b', 'c'});
		// The pass ends and asks for another at once, for a.
		expect(loop.service(1000, 0)).to_equal(Micros{1000});
		expect(loop.inPass()).to_be_falsy();
		// The next pass carries the reason to a.
		loop.service(1000, 0);
		expect(g_log).to_equal(std::vector<char>{'b', 'c', 'a'});
	});

	it("a pass ends without asking for another when only reasons no job listens to arrived mid-pass", _ {
		Loop loop{clock, waker};
		loop.add("a", bodyA, 100, 0);
		loop.add("b", bodyB, 100, 0b10);
		g_now = 1100;
		loop.service(1100, 0);
		// Mid-pass: a reason nobody listens for (a driver's timer reason, say).
		loop.service(1100, 0b01);
		expect(g_log).to_equal(std::vector<char>{'a', 'b'});
		// The pass ends and asks for the next due time, not for another pass at once.
		expect(loop.service(1100, 0)).to_equal(Micros{1200});
		expect(loop.inPass()).to_be_falsy();
	});

	it("a job that throws runs again only after its gap, and the jobs after it still run in the same pass", _ {
		Loop loop{clock, waker};
		loop.add("t", bodyThrows, 100, 0);
		loop.add("b", bodyB, 0, 0);
		pass(loop, 1000, 0);
		expect(g_log).to_equal(std::vector<char>{'b'});
		g_now = 1100;
		pass(loop, 1100, 0);
		// t throws (caught), and b, after it in order, still runs in the same pass.
		expect(g_log).to_equal(std::vector<char>{'b', 't', 'b'});
		// t's gap restarts from this run: not due again at the same time.
		pass(loop, 1100, 0);
		expect(g_log).to_equal(std::vector<char>{'b', 't', 'b', 'b'});
		// Due again once the gap has passed.
		g_now = 1200;
		pass(loop, 1200, 0);
		expect(g_log).to_equal(std::vector<char>{'b', 't', 'b', 'b', 't', 'b'});
	});

	it("a throw is counted in the job's stats", _ {
		Loop loop{clock, waker};
		JobId t = loop.add("t", bodyThrows, 0, 0);
		pass(loop, 1000, 0);
		auto s = loop.stats(t);
		expect(s.runs).to_equal(uint32_t{1});
		expect(s.failures).to_equal(uint32_t{1});
		loop.resetStats();
		expect(loop.stats(t).failures).to_equal(uint32_t{0});
	});

	it("refuses a job past its capacity", _ {
		Loop loop{clock, waker};
		for (size_t i = 0; i < Loop::kMaxJobs; ++i) {
			expect(loop.add("x", bodyA, kNever, 0)).not_().to_equal(kNoJob);
		}
		expect(loop.add("over", bodyA, kNever, 0)).to_equal(kNoJob);
	});
});

CPPSPEC_SPEC(app_loop)
