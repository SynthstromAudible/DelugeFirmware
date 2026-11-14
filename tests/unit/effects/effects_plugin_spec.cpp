#include "effect_test_states.h"
#include "effects/plugin.h"
#include "plugin-api/state_handle.h"

#include <cppspec.hpp>
#include <ostream>

using deluge::effects::DelugeEffects;
using deluge::plugin::StateHandle;

using namespace deluge::effects::testing;

namespace {
/// Another engine over the same state, so a handle can be asked for the wrong one.
struct OtherEffects : DelugeEffects {
	static constexpr deluge::plugin::PluginDescriptor descriptor() {
		return {.id = deluge::plugin::PluginId{"test.other-effects"},
		        .version = 1,
		        .kind = deluge::plugin::PluginKind::EFFECT};
	}
};
} // namespace

// clang-format off
describe effects_plugin("a host chain's effects held through a state handle", ${
	it("copies deeply", _ {
		const StateHandle original = StateHandle::of<DelugeEffects>(busy());
		StateHandle copy = original;
		copy.as<DelugeEffects>()->filter.lpf_mode = deluge::effects::FilterMode::SVF_BAND;
		expect(*original.as<DelugeEffects>()).to_equal(busy());
		expect(copy == original).to_be_false();
	});

	it("keeps the state where it was when the handle moves", _ {
		StateHandle handle = StateHandle::of<DelugeEffects>(busy());
		const deluge::effects::EffectStates* before = handle.as<DelugeEffects>();
		const StateHandle moved = std::move(handle);
		expect(moved.as<DelugeEffects>() == before).to_be_true();
	});

	it("names the effects engine, and answers no other", _ {
		const StateHandle handle = StateHandle::of<DelugeEffects>(busy());
		expect(handle.plugin() == deluge::plugin::kDelugeEffects).to_be_true();
		expect(handle.as<OtherEffects>() == nullptr).to_be_true();
	});

	it("encodes what it decodes", _ {
		const StateHandle handle = StateHandle::of<DelugeEffects>(busy());
		const deluge::serialization::Value encoded = handle.encode(kAtOne);
		StateHandle read = StateHandle::of<DelugeEffects>({});
		expect(read.decode(encoded, kAtOne).has_value()).to_be_true();
		expect(read.encode(kAtOne) == encoded).to_be_true();
	});
});

CPPSPEC_SPEC(effects_plugin)
