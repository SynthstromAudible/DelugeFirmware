// dx7::Graph against the reference's cores with the modern no-input and bus-input ops rendered as the device renders
// them (VectorCore): every workload's every block rendered by both side by side from the same operator inputs (the
// samples, the six ops' states and the feedback history must match), then every workload's whole voice through the
// graph, which also equals the recorded reference wherever the voice stays under MkI. On ARM the graph is built as the
// firmware builds it, its modern ops through the device's NEON kernel (the test-local copy of the asm), and is held to
// the same references and to the reference core taking that kernel, block by block and voice by voice.

#include "dsp-next/fixed/dx7/graph.h"
#include "dsp-next/fixed/dx7/tables.h"
#include "dx7_asm_pin_workloads.h"
#include "dx7_workload_records.h"
#include "reference_dx7.h"
#include <algorithm>
#include <array>
#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>

namespace dx7 = deluge::dsp::fixed::dx7;
namespace dw = dx7_workloads;

namespace {

/// @brief The app's tables, generated once.
const dx7::Tables& tables() {
	static const dx7::Tables t = [] {
		dx7::Tables g{};
		dx7::generate(g);
		return g;
	}();
	return t;
}

/// @brief The spec's one set of buses, lent to every render as DxEngine lends its one.
dx7::Buses& buses() {
	static dx7::Buses b;
	return b;
}

using G = dx7::Graph;

/// @brief The graph's settings for a reference core's call.
G::Settings settings_of(dx7::Engine engine, int algorithm, int32_t feedback_shift,
                        const dx7_reference::FmOpParams* params) {
	G::Settings s{.engine = engine,
	              .algorithm = static_cast<uint8_t>(algorithm),
	              .feedback = static_cast<uint8_t>(feedback_shift < 16 ? 8 - feedback_shift : 0),
	              .tables = &tables()};
	for (std::size_t op = 0; op < 6; ++op) {
		s.level_in[op] = params[op].level_in;
		s.freq[op] = params[op].freq;
	}
	return s;
}

/// @brief The reference core's six ops and history as the graph's view.
G::View view_of(const dx7_reference::FmOpParams* params, const int32_t* fb_buf) {
	G::View v{.feedback = {.y0 = fb_buf[0], .y1 = fb_buf[1]}};
	for (std::size_t op = 0; op < 6; ++op) {
		v.ops[op] = {.phase = params[op].phase, .gain_out = params[op].gain_out};
	}
	return v;
}

/// @brief A core that renders one block through the graph alone, the reference core's state carried in and out.
template <dx7::Engine E>
struct GraphCore {
	void render(const dx7_reference::Tables& /*t*/, int32_t* output, int n, dx7_reference::FmOpParams* params,
	            int algorithm, int32_t* fb_buf, int32_t feedback_shift) {
		G graph{view_of(params, fb_buf)};
		graph.renderBlock(settings_of(E, algorithm, feedback_shift, params), {output, static_cast<std::size_t>(n)},
		                  buses());
		const G::View v = graph.view();
		for (std::size_t op = 0; op < 6; ++op) {
			params[op].phase = v.ops[op].phase;
			params[op].gain_out = v.ops[op].gain_out;
		}
		fb_buf[0] = v.feedback.y0;
		fb_buf[1] = v.feedback.y1;
	}
};

struct GraphCores {
	GraphCore<dx7::Engine::MODERN> modern;
	GraphCore<dx7::Engine::MKI> mki;
};

/// @brief Blocks where the graph and the reference core disagreed, over every ComparingCore.
std::size_t& mismatches() {
	static std::size_t m = 0;
	return m;
}

/// @brief A core that renders each block through the reference core @p Reference and, from the same inputs, through
/// the graph, counting the blocks where they disagree; the voice carries on with the reference's.
template <class Reference, dx7::Engine E>
struct ComparingCore {
	Reference reference{};

	void render(const dx7_reference::Tables& t, int32_t* output, int n, dx7_reference::FmOpParams* params,
	            int algorithm, int32_t* fb_buf, int32_t feedback_shift) {
		alignas(16) std::array<int32_t, dx7::kMaxBlock> graph_out{};
		std::copy_n(output, dx7::kMaxBlock, graph_out.begin());
		G graph{view_of(params, fb_buf)};
		graph.renderBlock(settings_of(E, algorithm, feedback_shift, params),
		                  {graph_out.data(), static_cast<std::size_t>(n)}, buses());

		reference.render(t, output, n, params, algorithm, fb_buf, feedback_shift);

		const G::View v = graph.view();
		bool same = std::equal(output, output + n, graph_out.begin());
		for (std::size_t op = 0; op < 6; ++op) {
			same = same && v.ops[op].phase == params[op].phase && v.ops[op].gain_out == params[op].gain_out;
		}
		same = same && v.feedback.y0 == fb_buf[0] && v.feedback.y1 == fb_buf[1];
		mismatches() += same ? 0 : 1;
	}
};

struct ComparingCores {
	ComparingCore<dw::VectorCore, dx7::Engine::MODERN> modern;
	ComparingCore<dx7_reference::CoreMkI, dx7::Engine::MKI> mki;
};

#if DELUGE_DX7_ASM_PIN

/// @brief Silence, for the asm's no-input modulation and its set-output mix.
alignas(16) constexpr std::array<int32_t, dx7::kMaxBlock> kZeros{};

/// @brief The reference's modern core with fm_core.cpp's neon branch: NONE and BUS inputs through the asm (the
/// test-local copy), the feedback op through the scalar kernel.
struct AsmCore {
	dx7_reference::AlignedBuf<int32_t, dx7_reference::DX_MAX_N> buf_[2]{}; // NOLINT(*-avoid-c-arrays): as Core

	DELUGE_REFERENCE_DX7_LEGACY_ARITHMETIC void render(const dx7_reference::Tables& t, int32_t* output, int n,
	                                                   dx7_reference::FmOpParams* params, int algorithm,
	                                                   int32_t* fb_buf, int32_t feedback_shift) {
		const auto& alg = dx7::kAlgorithms[algorithm];
		int nmod = 1 + (n + 11) % 12;
		int simd_n = nmod == 8 ? n + 4 : (n + 3) & ~3;
		const int inv_n = (1 << 30) / n;
		bool has_contents[3] = {true, false, false}; // NOLINT(*-avoid-c-arrays): as Core
		for (int op = 0; op < 6; op++) {
			int flags = alg[op];
			bool add = dx7::has(static_cast<uint8_t>(flags), dx7::Flag::OUT_BUS_ADD);
			dx7_reference::FmOpParams& param = params[op];
			int inbus = (flags >> 4) & 3;
			int outbus = flags & 3;
			int32_t* outptr = (outbus == 0) ? output : buf_[outbus - 1].get();
			int32_t gain1 = param.gain_out;
			int32_t gain2 = dx7_reference::exp2_lookup(t, param.level_in - (14 * (1 << 24)));
			param.gain_out = gain2;
			int32_t dgain = (int)((((int64_t)(gain2 - gain1 + (n >> 1))) * (int64_t)(inv_n)) >> 30);
			if (gain1 >= dx7::kGainLevelThresh || gain2 >= dx7::kGainLevelThresh) {
				if (!has_contents[outbus]) {
					add = false;
				}
				const int32_t* mix = add ? outptr : kZeros.data();
				if (inbus == 0 || !has_contents[inbus]) {
					if ((flags & 0xc0) == 0xc0 && feedback_shift < 16) {
						dx7_reference::compute_fb(t, outptr, n, param.phase, param.freq, gain1, gain2, dgain, fb_buf,
						                          feedback_shift, add);
					}
					else {
						reference_neon_fm_kernel(kZeros.data(), mix, outptr, simd_n, param.phase, param.freq, gain1,
						                         dgain);
					}
				}
				else {
					reference_neon_fm_kernel(buf_[inbus - 1].get(), mix, outptr, simd_n, param.phase, param.freq, gain1,
					                         dgain);
				}
				has_contents[outbus] = true;
			}
			else if (!add) {
				has_contents[outbus] = false;
			}
			param.phase += param.freq * n;
		}
	}
};

struct AsmComparingCores {
	ComparingCore<AsmCore, dx7::Engine::MODERN> modern;
	ComparingCore<dx7_reference::CoreMkI, dx7::Engine::MKI> mki;
};

struct AsmCores {
	AsmCore modern;
	dx7_reference::CoreMkI mki;
};

#endif

} // namespace

// clang-format off
describe dx7_graph_spec("dx7::Graph", ${
	it("renders every workload's every block as the reference core does from the same inputs", _ {
		mismatches() = 0;
		std::size_t blocks = 0;
		for (const dw::Workload& w : dw::all_workloads()) {
			std::ignore = dw::run<ComparingCores>(w);
			blocks += w.blocks.size();
		}
		expect(blocks).to_be_greater_than(std::size_t{0});
		expect(mismatches()).to_equal(std::size_t{0});
	});

	for (const dw::Workload& workload : dw::all_workloads()) {
		it(dw::name(workload.id), _ {
			const dw::WorkloadRecord record = dw::run<GraphCores>(workload).record;
			expect(record).to_equal(dw::run_reference_vector(workload));
			if (dw::renders_mki_throughout(workload)) {
				expect(record).to_equal(dw::kRecordedReference.at(static_cast<std::size_t>(workload.id)));
			}
		});
	}

	it("keeps every workload that stays under MkI as recorded, and moves every other", _ {
		std::size_t mki = 0;
		for (const dw::Workload& w : dw::all_workloads()) {
			const bool kept = dw::run_reference_vector(w) == dw::kRecordedReference.at(static_cast<std::size_t>(w.id));
			if (dw::renders_mki_throughout(w)) {
				++mki;
				expect(kept).to_be_true();
			}
		}
		expect(mki).to_be_greater_than(std::size_t{0});
	});

#if DELUGE_DX7_ASM_PIN
	it("renders every block as the reference core taking the device's NEON kernel", _ {
		mismatches() = 0;
		for (const dw::Workload& w : dw::all_workloads()) {
			std::ignore = dw::run<AsmComparingCores>(w);
		}
		expect(mismatches()).to_equal(std::size_t{0});
	});

	it("renders every voice as the reference voice taking the device's NEON kernel", _ {
		for (const dw::Workload& w : dw::all_workloads()) {
			expect(dw::run<GraphCores>(w).record).to_equal(dw::run<AsmCores>(w).record);
		}
	});
#endif
});
// clang-format on

CPPSPEC_MAIN(dx7_graph_spec);
