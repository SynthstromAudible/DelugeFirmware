#include "plugin-api/state_handle.h"

#include <cppspec.hpp>

namespace {
struct CounterState {
	int32_t value{0};
	bool operator==(const CounterState&) const = default;
};
/// A plugin whose state is one number.
struct CounterPlugin {
	using State = CounterState;
	static constexpr deluge::plugin::PluginDescriptor descriptor() {
		return {.id = deluge::plugin::PluginId{"test.counter"},
		        .version = 1,
		        .kind = deluge::plugin::PluginKind::INSTRUMENT};
	}
	static deluge::serialization::Value encode(const State& s, const deluge::plugin::CodecContext&) {
		auto map = deluge::serialization::Value::makeMap();
		map.set("value", deluge::serialization::Value::makeNumber(s.value));
		return map;
	}
	static std::expected<void, deluge::plugin::StateError> decode(const deluge::serialization::Value& v, State& s,
	                                                              const deluge::plugin::CodecContext&) {
		if (!v.isMap()) {
			return std::unexpected(deluge::plugin::StateError::NOT_A_MAP);
		}
		s.value = deluge::serialization::int_of(v.find("value")).value_or(s.value);
		return {};
	}
};
/// Another plugin, so a handle can be asked for the wrong one.
struct OtherPlugin : CounterPlugin {
	static constexpr deluge::plugin::PluginDescriptor descriptor() {
		return {
		    .id = deluge::plugin::PluginId{"test.other"}, .version = 1, .kind = deluge::plugin::PluginKind::INSTRUMENT};
	}
};
using deluge::plugin::StateHandle;
} // namespace

// clang-format off
describe state_handle("a plugin's state, held without knowing its type", ${
	it("is empty until made of a state", _ {
		const StateHandle empty;
		expect(empty.has_value()).to_be_false();
		const StateHandle held = StateHandle::of<CounterPlugin>({.value = 3});
		expect(held.has_value()).to_be_true();
		expect(held.plugin() == CounterPlugin::descriptor().id).to_be_true();
	});

	it("copies deeply", _ {
		StateHandle a = StateHandle::of<CounterPlugin>({.value = 3});
		StateHandle b = a;
		b.as<CounterPlugin>()->value = 9;
		expect(a.as<CounterPlugin>()->value).to_equal(3);
		expect(b.as<CounterPlugin>()->value).to_equal(9);
	});

	it("keeps its state where it was when it moves", _ {
		StateHandle a = StateHandle::of<CounterPlugin>({.value = 4});
		CounterState* at = a.as<CounterPlugin>();
		StateHandle b = std::move(a);
		expect(b.as<CounterPlugin>() == at).to_be_true();
		StateHandle c;
		c = std::move(b);
		expect(c.as<CounterPlugin>() == at).to_be_true();
	});

	it("compares its states", _ {
		expect(StateHandle::of<CounterPlugin>({.value = 1}) == StateHandle::of<CounterPlugin>({.value = 1})).to_be_true();
		expect(StateHandle::of<CounterPlugin>({.value = 1}) == StateHandle::of<CounterPlugin>({.value = 2})).to_be_false();
		expect(StateHandle::of<CounterPlugin>({.value = 1}) == StateHandle::of<OtherPlugin>({.value = 1})).to_be_false();
		expect(StateHandle{} == StateHandle{}).to_be_true();
	});

	it("gives its state only to its own plugin", _ {
		StateHandle a = StateHandle::of<CounterPlugin>({.value = 1});
		expect(a.as<OtherPlugin>() == nullptr).to_be_true();
		const StateHandle& c = a;
		expect(c.as<CounterPlugin>() != nullptr).to_be_true();
	});

	it("encodes and decodes its state through its plugin", _ {
		StateHandle a = StateHandle::of<CounterPlugin>({.value = 6});
		const deluge::serialization::Value v = a.encode({});
		StateHandle b = StateHandle::of<CounterPlugin>({});
		expect(b.decode(v, {}).has_value()).to_be_true();
		expect(b == a).to_be_true();
	});
});
// clang-format on

CPPSPEC_SPEC(state_handle)
