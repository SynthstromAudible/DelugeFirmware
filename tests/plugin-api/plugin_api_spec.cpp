#include "plugin-api/plugin.h"

#include <cppspec.hpp>

namespace {
struct TestState {
	int32_t value{0};
};
struct TestEngine {
	using State = TestState;
	static constexpr deluge::plugin::PluginDescriptor descriptor() {
		return {.id = deluge::plugin::PluginId{"test.plugin"},
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
} // namespace

// clang-format off
describe plugin_api("a plugin's state shape", ${
	it("is satisfied by a plugin with a descriptor and a state codec", _ {
		expect(deluge::plugin::InstrumentPlugin<TestEngine>).to_be_true();
	});

	it("round-trips its state through a Value", _ {
		TestState in{.value = 7};
		TestState out{};
		auto v = TestEngine::encode(in, {});
		expect(TestEngine::decode(v, out, {})).to_have_value();
		expect(out.value).to_equal(7);
	});
});

CPPSPEC_SPEC(plugin_api)
