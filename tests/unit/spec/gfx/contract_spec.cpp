// NDEBUG on, deliberately and before every include: the whole point of DELUGE_GFX_CONTRACT is
// that it is NOT assert(), i.e. that it survives the define every shipped build sets. If someone
// re-spells the macro in terms of assert(), the trap example below stops trapping and this spec
// fails -- which is exactly the regression worth catching.
#ifndef NDEBUG
#define NDEBUG 1
#endif

#include "../support/spec_main.h"
#include "cppspec.hpp"
#include "gfx/contract.h"
#include "gfx/grid/pad_grid.h"
#include "gfx/grid/tween.h"
#include "gfx/oled/canvas.h"
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#define DELUGE_SPEC_CAN_FORK 1
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {

/// Runs \p body in a forked child and reports whether the child died on a signal (SIGILL / SIGTRAP
/// from __builtin_trap, depending on the architecture) rather than returning.
///
/// A child process, not a signal handler with siglongjmp: __builtin_trap is not required to be
/// resumable, the faulting instruction may be re-executed on return from the handler, and the
/// optimiser is entitled to treat everything after a trap as unreachable. Letting a throwaway
/// process die is the only way to observe the trap without the observation changing it.
#ifdef DELUGE_SPEC_CAN_FORK
template <typename F>
bool dies_on_signal(F&& body) {
	const pid_t pid = fork();
	if (pid == 0) {
		body();
		_exit(0); // reached only if the contract did NOT trap
	}
	int status = 0;
	waitpid(pid, &status, 0);
	return WIFSIGNALED(status);
}
#endif

/// A `volatile` sink so the compiler cannot delete the constructions below as dead stores.
volatile uint8_t g_sink = 0;

} // namespace

// clang-format off
describe contract("gfx::DELUGE_GFX_CONTRACT", $ {
	it("passes through when the condition holds", _ {
		// The satisfied case must be transparent, and must evaluate its argument exactly once --
		// a macro that evaluated `cond` twice would make `counter++ < 1` mean something different
		// inside a contract than outside one.
		int evaluations = 0;
		DELUGE_GFX_CONTRACT((++evaluations, true));
		expect(evaluations).to_equal(1);

		// NDEBUG is on in this TU (see the top of the file), so a plain assert is a no-op here.
		// That is the baseline the trap examples below are measured against.
		assert(false && "assert must be compiled out in this TU");
	});

#ifdef DELUGE_SPEC_CAN_FORK
	it("traps on a Canvas built over a too-short span, even under NDEBUG", _ {
		expect(dies_on_signal([] {
			// kDelugeOled needs 128 * 48 / 8 = 768 bytes; 16 is nowhere near enough, and the
			// canvas would write off the end of this buffer on the first draw call.
			std::vector<uint8_t> tiny(16);
			deluge::gfx::oled::Canvas c{std::span<uint8_t>{tiny}, deluge::gfx::oled::kDelugeOled};
			g_sink = c.bytes()[0];
		})).to_be_true();
	});

	it("does not trap on a Canvas built over exactly enough storage", _ {
		expect(dies_on_signal([] {
			std::vector<uint8_t> storage(deluge::gfx::oled::kDelugeOled.bytes());
			deluge::gfx::oled::Canvas c{std::span<uint8_t>{storage}, deluge::gfx::oled::kDelugeOled};
			g_sink = c.bytes()[0];
		})).to_be_false();
	});

	it("traps on an out-of-range PadGrid::at, even under NDEBUG", _ {
		expect(dies_on_signal([] {
			deluge::gfx::grid::PadGrid g{deluge::gfx::grid::kDelugeGrid}; // 18 cols x 8 rows
			g_sink = g.at(18, 0).r;                                      // one past the last column
		})).to_be_true();
	});

	it("traps when a tween's out grid has different dims", _ {
		expect(dies_on_signal([] {
			using namespace deluge::gfx::grid;
			PadGrid from{kDelugeGrid};
			PadGrid to{kDelugeGrid};
			PadGrid out{GridDimensions{8, 2, 4}}; // tween.h's documented precondition, violated
			fade(from, to, 32768, out);
			g_sink = out.at(0, 0).r;
		})).to_be_true();
	});

	// A zoom magnitude of 6 makes renderZoom's `increaseMagnitude(x, coarse - 14)` shift right by
	// 2^5 + 14 == 46, past the operand width -- UB, and a divide by zero on the next line if it
	// folds to 0. Legacy only survives it on ARM's shift-masking and non-trapping udiv; this port
	// refuses instead. `bit_width(3) == 2`, so 96 -> magnitude 5 (legal) and 192 -> 6 (not).
	it("traps on a zoom magnitude past what renderZoom's shift arithmetic can hold", _ {
		expect(dies_on_signal([] {
			using namespace deluge::gfx::grid;
			PadGrid from{kDelugeGrid}, to{kDelugeGrid}, out{kDelugeGrid};
			from.set(0, 0, RGB{200, 40, 40});
			zoom(from, to, ZoomParams{192, 3, 9 * 65536, 9 * 65536, 0}, out);
			g_sink = out.at(0, 0).r;
		})).to_be_true();
	});

	it("does not trap at the largest legal zoom magnitude", _ {
		expect(dies_on_signal([] {
			using namespace deluge::gfx::grid;
			static_assert(kMaxZoomMagnitude == 5);
			PadGrid from{kDelugeGrid}, to{kDelugeGrid}, out{kDelugeGrid};
			from.set(0, 0, RGB{200, 40, 40});
			zoom(from, to, ZoomParams{96, 3, 9 * 65536, 9 * 65536, 0}, out);
			g_sink = out.at(0, 0).r;
		})).to_be_false();
	});
#endif
});
DELUGE_SPEC(gfx_contract_spec, contract)
