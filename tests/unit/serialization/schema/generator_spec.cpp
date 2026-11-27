#include "support/schema/generator.h"
#include "support/schema/round_trip.h"
#include "test_schemas.h"

#include <cppspec.hpp>

#include <cstdlib>

#include <cstdint>
#include <string>

using deluge::serialization::Value;
using namespace deluge::serialization::schema;
using namespace deluge::serialization::schema::test;

// clang-format off
describe generated_round_trip("generated documents", ${
	it("decode, encode and decode again to the same record, with nothing noted", _ {
		for (uint32_t seed = 1; seed <= 300; ++seed) {
			const auto checked = round_trip(kPanelSchema, Generator{seed}.record(kPanelDesc));
			expect(checked.error_or(std::string{})).to_equal(std::string{});
		}
	});

	it("keep unknown keys through the round trip", _ {
		for (uint32_t seed = 1; seed <= 100; ++seed) {
			const Value document = Generator{seed, {.unknown_keys = true}}.record(kPanelDesc);
			const auto checked = round_trip(kPanelSchema, document, nullptr, RoundTrip::UNKNOWN_KEYS);
			expect(checked.error_or(std::string{})).to_equal(std::string{});
		}
	});
});
// clang-format on

int generator_spec(int argc, char** const argv) {
	return CppSpec::parse(argc, argv).add_specs(generated_round_trip).exec().is_success() ? EXIT_SUCCESS : EXIT_FAILURE;
}
