// Every word through every mapping kind's word→position functions, against today's arithmetic
// (param_mapping_reference.h). Too slow for the unit tier; the param-mapping-sweep registry entry runs it nightly.
#include "param_mapping_reference.h"
#include "plugin-api/types/param_mapping.h"

#include <cstdint>
#include <cstdio>

using namespace deluge::plugin;
namespace ref = deluge::plugin::test::reference;

namespace {
struct Kind {
	const char* name;
	MappingKind kind;
	int32_t (*knob)(int32_t);
	int32_t (*menu)(int32_t);
};
constexpr Kind kKinds[] = {
    {"STANDARD", MappingKind::STANDARD, ref::standard_to_knob, ref::standard_to_menu},
    {"UNIPOLAR", MappingKind::UNIPOLAR, ref::unipolar_to_knob, ref::half_to_menu},
    {"PAN", MappingKind::PAN, ref::standard_to_knob, ref::pan_to_menu},
    {"CABLE_DEPTH", MappingKind::CABLE_DEPTH, ref::cable_to_knob, ref::cable_to_menu},
};
} // namespace

int main() {
	int failed = 0;
	for (const Kind& k : kKinds) {
		uint64_t bad = 0;
		for (int64_t v = INT32_MIN; v <= INT32_MAX; ++v) {
			auto word = static_cast<int32_t>(v);
			bad += to_knob(k.kind, word) != k.knob(word);
			bad += to_menu(k.kind, word) != k.menu(word);
		}
		std::printf("PM-SWEEP kind=%s bad=%llu\n", k.name, static_cast<unsigned long long>(bad));
		failed |= bad != 0;
	}
	return failed;
}
