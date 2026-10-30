// The DX7 reference (reference_dx7.h) over every workload (dx7_workloads.h): its tables and records, that the
// workloads reach every operator path, and whether a voice ends after its release.
#include "dx7_workload_records.h"
#include "reference_dx7.h"
#include <cppspec.hpp>
#include <cstdio>
#include <cstdlib>

namespace dw = dx7_workloads;

// clang-format off
describe dx7_reference_spec("the DX7 reference", ${
	it("builds the tables as recorded", _ {
		const dw::TableRecord record = dw::table_record(dw::reference_tables());
		if (std::getenv("DELUGE_DX7_RECORD") != nullptr) {
			std::printf("    TABLES {0x%llxULL, 0x%llxULL, 0x%llxULL, 0x%llxULL, 0x%llxULL}\n",
			            (unsigned long long)record.exp2, (unsigned long long)record.sin,
			            (unsigned long long)record.freq, (unsigned long long)record.sin_log,
			            (unsigned long long)record.sin_exp);
		}
		expect(record).to_equal(dw::kRecordedTables);
	});
	for (const dw::Workload& workload : dw::all_workloads()) {
		it(dw::name(workload.id), _ {
			const dw::WorkloadRecord record = dw::run_reference(workload);
			if (std::getenv("DELUGE_DX7_RECORD") != nullptr) {
				std::printf("    RECORD // %s\n    RECORD {.outputs = 0x%llxULL, .state = 0x%llxULL, .alive = 0x%llxULL},\n",
				            dw::name(workload.id), (unsigned long long)record.outputs,
				            (unsigned long long)record.state, (unsigned long long)record.alive);
			}
			expect(record).to_equal(dw::kRecordedReference.at(static_cast<std::size_t>(workload.id)));
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
		const dw::Ending modern = dw::ending(dw::WorkloadId::RELEASE_TAIL_MODERN);
		const dw::Ending mki = dw::ending(dw::WorkloadId::RELEASE_TAIL_MKI);
		if (std::getenv("DELUGE_DX7_RECORD") != nullptr) {
			std::printf("    ENDING modern=%d@%zu mki=%d@%zu\n", modern.ended, modern.block, mki.ended, mki.block);
		}
		expect(modern.ended).to_be_true();
		expect(mki.ended).to_equal(dw::kMkIVoiceEnds); // the observed behaviour, pinned
	});
});
// clang-format on

CPPSPEC_MAIN(dx7_reference_spec);
