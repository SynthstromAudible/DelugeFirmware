#include "engine-api/engine.h"

#include <cppspec.hpp>

namespace {
struct TestState {
	int32_t value{0};
};
struct TestEngine {
	using State = TestState;
	static constexpr deluge::engine::EngineDescriptor descriptor() {
		return {.id = deluge::engine::EngineId{"test.engine"},
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
} // namespace

// clang-format off
describe engine_api("an engine's state shape", ${
	it("is satisfied by an engine with a descriptor and a state codec", _ {
		expect(deluge::engine::InstrumentEngine<TestEngine>).to_equal(true);
	});

	it("round-trips its state through a Value", _ {
		TestState in{.value = 7};
		TestState out{};
		auto v = TestEngine::encode(in, {});
		expect(TestEngine::decode(v, out, {}).has_value()).to_equal(true);
		expect(out.value).to_equal(7);
	});
});

CPPSPEC_SPEC(engine_api)
