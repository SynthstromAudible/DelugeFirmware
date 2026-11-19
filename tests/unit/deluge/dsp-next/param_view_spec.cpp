#include "synth/voice/param_view.h"
#include "util/misc.h"

#include <cppspec.hpp>

#include "support/spec_main.h"

using deluge::synth::param_view;
using deluge::synth::PatchSource;

namespace {

/// A sound's params with one cable, from LFO 1 to param 3, and the portamento knob at 7.
struct FakeView {
	[[nodiscard]] int32_t unpatched(int32_t param) const { return param == 5 ? 7 : 0; }
	[[nodiscard]] deluge::dsp::Q<31> knob(int32_t param) const {
		return deluge::dsp::Q<31>::from_raw(unpatched(param));
	}
	[[nodiscard]] bool modulated(int32_t param) const { return param == 3; }
	[[nodiscard]] bool wired(PatchSource source) const { return source == PatchSource::LFO_GLOBAL_1; }
	[[nodiscard]] uint32_t sources_wired(Globality scope) const {
		return scope == GLOBALITY_GLOBAL ? 1u << util::to_underlying(PatchSource::LFO_GLOBAL_1) : 0u;
	}
	[[nodiscard]] bool can_sound(int32_t param) const { return param == 3; }
	[[nodiscard]] std::array<uint8_t, 2> bend_ranges() const { return {2, 48}; }
};

/// A view that cannot say what is wired.
struct NoWiring {
	[[nodiscard]] int32_t unpatched(int32_t) const { return 0; }
	[[nodiscard]] deluge::dsp::Q<31> knob(int32_t) const { return {}; }
	[[nodiscard]] bool modulated(int32_t) const { return false; }
	[[nodiscard]] bool can_sound(int32_t) const { return false; }
	[[nodiscard]] std::array<uint8_t, 2> bend_ranges() const { return {}; }
};

static_assert(param_view<FakeView>);
static_assert(!param_view<NoWiring>);

/// What a render reads to decide whether a param needs work this block.
template <param_view V>
bool needs_work(const V& view, int32_t param) {
	return view.modulated(param) || view.can_sound(param);
}

} // namespace

// clang-format off
describe param_view_reads("the reads a sound's render makes of its params", ${
	it("reads through any view that answers them", _ {
		const FakeView view;
		expect(needs_work(view, 3)).to_be_true();
		expect(needs_work(view, 4)).to_be_false();
		expect(view.unpatched(5)).to_equal(7);
		expect(view.wired(PatchSource::LFO_GLOBAL_1)).to_be_true();
	});
});
// clang-format on

DELUGE_SPEC(param_view_spec, param_view_reads)
