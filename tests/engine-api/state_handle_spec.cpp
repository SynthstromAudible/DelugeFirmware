#include "engine-api/state_handle.h"

#include <cppspec.hpp>

namespace {
struct CounterState {
	int32_t value{0};
	bool operator==(const CounterState&) const = default;
};
/// An engine whose state is one number.
struct CounterEngine {
	using State = CounterState;
	static constexpr deluge::engine::EngineDescriptor descriptor() {
		return {.id = deluge::engine::EngineId{"test.counter"},
		        .version = 1,
		        .kind = deluge::engine::EngineKind::INSTRUMENT};
	}
	static deluge::serialization::Value encode(const State& s, const deluge::engine::CodecContext&) {
		auto map = deluge::serialization::Value::makeMap();
		map.set("value", deluge::serialization::Value::makeNumber(s.value));
		return map;
	}
	static std::expected<void, deluge::engine::StateError> decode(const deluge::serialization::Value& v, State& s,
	                                                              const deluge::engine::CodecContext&) {
		if (!v.isMap()) {
			return std::unexpected(deluge::engine::StateError::NOT_A_MAP);
		}
		s.value = deluge::serialization::int_of(v.find("value")).value_or(s.value);
		return {};
	}
};
/// Another engine, so a handle can be asked for the wrong one.
struct OtherEngine : CounterEngine {
	static constexpr deluge::engine::EngineDescriptor descriptor() {
		return {
		    .id = deluge::engine::EngineId{"test.other"}, .version = 1, .kind = deluge::engine::EngineKind::INSTRUMENT};
	}
};
using deluge::engine::StateHandle;
} // namespace

// clang-format off
describe state_handle("an engine's state, held without knowing its type", ${
	it("is empty until made of a state", _ {
		const StateHandle empty;
		expect(empty.has_value()).to_be_false();
		const StateHandle held = StateHandle::of<CounterEngine>({.value = 3});
		expect(held.has_value()).to_be_true();
		expect(held.engine() == CounterEngine::descriptor().id).to_be_true();
	});

	it("copies deeply", _ {
		StateHandle a = StateHandle::of<CounterEngine>({.value = 3});
		StateHandle b = a;
		b.as<CounterEngine>()->value = 9;
		expect(a.as<CounterEngine>()->value).to_equal(3);
		expect(b.as<CounterEngine>()->value).to_equal(9);
	});

	it("keeps its state where it was when it moves", _ {
		StateHandle a = StateHandle::of<CounterEngine>({.value = 4});
		CounterState* at = a.as<CounterEngine>();
		StateHandle b = std::move(a);
		expect(b.as<CounterEngine>() == at).to_be_true();
		StateHandle c;
		c = std::move(b);
		expect(c.as<CounterEngine>() == at).to_be_true();
	});

	it("compares its states", _ {
		expect(StateHandle::of<CounterEngine>({.value = 1}) == StateHandle::of<CounterEngine>({.value = 1})).to_be_true();
		expect(StateHandle::of<CounterEngine>({.value = 1}) == StateHandle::of<CounterEngine>({.value = 2})).to_be_false();
		expect(StateHandle::of<CounterEngine>({.value = 1}) == StateHandle::of<OtherEngine>({.value = 1})).to_be_false();
		expect(StateHandle{} == StateHandle{}).to_be_true();
	});

	it("gives its state only to its own engine", _ {
		StateHandle a = StateHandle::of<CounterEngine>({.value = 1});
		expect(a.as<OtherEngine>() == nullptr).to_be_true();
		const StateHandle& c = a;
		expect(c.as<CounterEngine>() != nullptr).to_be_true();
	});

	it("encodes and decodes its state through its engine", _ {
		StateHandle a = StateHandle::of<CounterEngine>({.value = 6});
		const deluge::serialization::Value v = a.encode({});
		StateHandle b = StateHandle::of<CounterEngine>({});
		expect(b.decode(v, {}).has_value()).to_be_true();
		expect(b == a).to_be_true();
	});
});
// clang-format on

CPPSPEC_SPEC(state_handle)
