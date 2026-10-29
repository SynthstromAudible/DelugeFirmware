#include "dsp-next/fixed/dx7/lookup.h"
#include "dsp-next/fixed/dx7/tables.h"
#include "dx7_workload_records.h"
#include "reference_dx7.h"
#include <algorithm>
#include <climits>
#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>

namespace dx7 = deluge::dsp::fixed::dx7;

// clang-format off
describe dx7_tables_spec("dx7::Tables", ${
	static dx7::Tables tables;
	static dx7_reference::Tables reference;
	dx7::generate(tables);
	dx7_reference::init_tables(reference);

	it("equals the reference's tables word for word", _ {
		expect(std::ranges::equal(tables.exp2, reference.exp2tab)).to_be_true();
		expect(std::ranges::equal(tables.sine, reference.sintab)).to_be_true();
		expect(std::ranges::equal(tables.frequency, reference.freq_lut)).to_be_true();
		expect(std::ranges::equal(tables.mki_sin_log, reference.sinLogTable)).to_be_true();
		expect(std::ranges::equal(tables.mki_sin_exp, reference.sinExpTable)).to_be_true();
	});

	it("matches the recorded table digests", _ {
		expect(dx7_workloads::table_record(tables)).to_equal(dx7_workloads::kRecordedTables);
	});

	it("looks up sine and the MkI sine as the reference over every 4093rd 32-bit input", _ {
		for (int64_t x = INT32_MIN; x <= INT32_MAX; x += 4093) {
			const auto v = static_cast<int32_t>(x);
			expect(dx7::sin_lookup(tables, v)).to_equal(dx7_reference::sin_lookup(reference, v));
			for (uint16_t env : {0, 1, 100, 8000, 16383, 16384, 65535}) {
				expect(dx7::mki_sin(tables, v, env)).to_equal(dx7_reference::mki_sin(reference, v, env));
			}
		}
	});

	it("looks up exp2 as the reference over [-14.0, 6.0] in Q24, the range the operators feed", _ {
		// level_in - (14 << 24) for level_in in [0, 20 << 24]; the final shift is 6 - (x >> 24), so 0..20
		for (int64_t x = int64_t{-14} << 24; x <= int64_t{6} << 24; x += 4093) {
			const auto v = static_cast<int32_t>(x);
			expect(dx7::exp2_lookup(tables, v)).to_equal(dx7_reference::exp2_lookup(reference, v));
		}
		expect(dx7::exp2_lookup(tables, 6 << 24)).to_equal(dx7_reference::exp2_lookup(reference, 6 << 24));
	});

	it("looks up frequency as the reference over [0, 20.0) octaves in Q24, the range the voice feeds", _ {
		// the final shift is 20 - (logfreq >> 24), so 1..20
		for (int64_t x = 0; x < int64_t{20} << 24; x += 4093) {
			const auto v = static_cast<int32_t>(x);
			expect(dx7::freq_lookup(tables, v)).to_equal(dx7_reference::freq_lookup(reference, v));
		}
		expect(dx7::freq_lookup(tables, (20 << 24) - 1)).to_equal(dx7_reference::freq_lookup(reference, (20 << 24) - 1));
	});

	it("is no larger than its arrays", _ {
		expect(sizeof(dx7::Tables)).to_equal(std::size_t{((2048 + 2048 + 1025) * 4) + (2048 * 2)});
	});
});
// clang-format on

CPPSPEC_MAIN(dx7_tables_spec);
