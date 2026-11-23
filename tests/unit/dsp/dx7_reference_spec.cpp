// The DX7 reference (reference_dx7.h) over every workload (dx7_workloads.h): its tables and records, that the
// workloads reach every operator path, and whether a voice ends after its release.
// `./dbt test --update unit -- -R dx7_reference_spec` re-records the records (dx7_workload_records.h); the components'
// specs confirm them on the next plain run.
#include "dx7_workload_records.h"
#include "reference_dx7.h"
#include "support/golden_update.h"
#include <cppspec.hpp>
#include <cstdio>
#include <string>
#include <string_view>

#include "support/spec_main.h"

namespace dw = dx7_workloads;
namespace golden = deluge::test::golden;

constexpr std::string_view kHeader = "tests/unit/dsp/dx7_workload_records.h";

// The records are the host's: the ARM tree runs this spec as a check of them, so there it always compares.
#ifdef __arm__
constexpr bool kRecordsHere = false;
#else
constexpr bool kRecordsHere = true;
#endif

// clang-format off
describe dx7_reference_spec_body("the DX7 reference", ${
	it("builds the tables as recorded", _ {
		const bool updating = kRecordsHere && golden::updating();
		const dw::TableRecord record = dw::table_record(dw::reference_tables());
		if (updating) {
			// Two lines, the second aligned under the first's brace, to keep the header within 120 columns.
			char line[160];
			std::snprintf(line, sizeof line, "inline constexpr TableRecord kRecordedTables{0x%llxULL, 0x%llxULL, 0x%llxULL,",
			              (unsigned long long)record.exp2, (unsigned long long)record.sin,
			              (unsigned long long)record.freq);
			golden::record(kHeader, "kRecordedTables", line);
			std::snprintf(line, sizeof line, "                                             0x%llxULL, 0x%llxULL};",
			              (unsigned long long)record.sin_log, (unsigned long long)record.sin_exp);
			golden::record(kHeader, "kRecordedTables", line);
		}
		if (!updating) {
			expect(record).to_equal(dw::kRecordedTables);
		}
	});
	for (const dw::Workload& workload : dw::all_workloads()) {
		it(dw::name(workload.id), _ {
			const bool updating = kRecordsHere && golden::updating();
			const dw::WorkloadRecord record = dw::run_reference(workload);
			if (updating) {
				char line[160];
				std::snprintf(line, sizeof line, "    {.outputs = 0x%llxULL, .state = 0x%llxULL, .alive = 0x%llxULL},",
				              (unsigned long long)record.outputs, (unsigned long long)record.state,
				              (unsigned long long)record.alive);
				golden::record(kHeader, "kRecordedReference", std::string{"    // "} + dw::name(workload.id));
				golden::record(kHeader, "kRecordedReference", line);
			}
			if (!updating) {
				expect(record).to_equal(dw::kRecordedReference.at(static_cast<std::size_t>(workload.id)));
			}
		});
	}
	it("reaches every algorithm's every op path", _ {
		dx7_reference::Counters::reset();
		for (const dw::Workload& w : dw::all_workloads()) {
			(void)dw::run_reference(w);
		}
		const auto& c = dx7_reference::Counters::get();
		// one expectation per path, so a failure names the path no workload reaches
		expect(c.modern_pure).to_be_greater_than(std::size_t{0});
		expect(c.modern_bus).to_be_greater_than(std::size_t{0});
		expect(c.modern_fb).to_be_greater_than(std::size_t{0});
		expect(c.mki_pure).to_be_greater_than(std::size_t{0});
		expect(c.mki_bus).to_be_greater_than(std::size_t{0});
		expect(c.mki_fb).to_be_greater_than(std::size_t{0});
		expect(c.mki_fb2).to_be_greater_than(std::size_t{0});
		expect(c.mki_fb3).to_be_greater_than(std::size_t{0});
		expect(c.mki_alg32_fb).to_be_greater_than(std::size_t{0});
		expect(c.gated).to_be_greater_than(std::size_t{0});
	});
	it("ends a modern voice after its release and records whether an MkI voice ends", _ {
		const bool updating = kRecordsHere && golden::updating();
		const dw::Ending modern = dw::ending(dw::WorkloadId::RELEASE_TAIL_MODERN);
		const dw::Ending mki = dw::ending(dw::WorkloadId::RELEASE_TAIL_MKI);
		if (updating) {
			golden::record(kHeader, "kMkIVoiceEnds",
			               mki.ended ? "inline constexpr bool kMkIVoiceEnds = true;"
			                         : "inline constexpr bool kMkIVoiceEnds = false;");
		}
		expect(modern.ended).to_be_true();
		if (!updating) {
			expect(mki.ended).to_equal(dw::kMkIVoiceEnds); // the observed behaviour, pinned
		}
	});
});
// clang-format on

DELUGE_SPEC(dx7_reference_spec, dx7_reference_spec_body)
