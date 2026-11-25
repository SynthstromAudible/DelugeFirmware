
/// @file
/// @brief The synth envelope's bench cases.

#include "bench/cases.h"
#include "dsp/core/fixed_point.h"
#include "memory/heaps.h"
#include "synth/modulation/envelope.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <new>

namespace deluge::bench {

namespace {

using dsp::Q;

constexpr uint32_t kRate = 64; ///< A stage of 131,072 samples: longer than a run.
constexpr Q<31> kSustain = dsp::q<31>(0.5);

constexpr std::size_t kMaxEnvelopes = 64;
using Envelopes = std::array<Envelope, kMaxEnvelopes>;

std::array<Q<31>, kMaxEnvelopes> levels; ///< Where a block leaves its levels.

/// Built once, in the app's fast heap, where voices live.
Envelopes& envelopes() {
	static Envelopes* made = new (memory::alloc_fast(sizeof(Envelopes), alignof(Envelopes))) Envelopes{};
	return *made;
}

[[gnu::noinline]] Q<31> render(Envelope& e, std::size_t n, uint32_t decay = kRate) {
	return e.render(n, kRate, decay, kSustain, kRate, synth::EnvelopeState{}, Q<31>{});
}

} // namespace

void EnvelopeCase::setup(std::size_t n) {
	for (std::size_t i = 0; i < count_; ++i) {
		Envelope& e = envelopes()[i];
		(void)e.note_on(kind_ != EnvelopeKind::ATTACK, kSustain);
		if (kind_ == EnvelopeKind::SUSTAIN) {
			(void)render(e, n, 1U << 23); // the decay ends in this block
		}
		else if (kind_ == EnvelopeKind::RELEASE) {
			(void)render(e, n);
			e.unconditional_release();
		}
	}
}

void EnvelopeCase::block(std::size_t n) {
	for (std::size_t i = 0; i < count_; ++i) {
		levels[i] = render(envelopes()[i], n);
	}
}

} // namespace deluge::bench
