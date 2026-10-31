#include "util/structural_edit_probe.h"

#include "definitions.h"
#include "libdeluge/system.h"
#include "memory/heaps.h"

#include <array>
#include <atomic>
#include <cstddef>

namespace deluge {
namespace {
std::array<std::atomic<uint32_t>, static_cast<size_t>(StructuralContainer::COUNT_)> unguarded_{};
std::array<std::atomic<uint32_t>, static_cast<size_t>(StructuralContainer::COUNT_)> noted_{};
std::array<std::atomic<uint32_t>, static_cast<size_t>(StructuralContainer::COUNT_)> render_{};
} // namespace

void noteStructuralEdit([[maybe_unused]] StructuralContainer kind) {
#if ALPHA_OR_BETA_VERSION
	noted_[static_cast<size_t>(kind)].fetch_add(1, std::memory_order_relaxed);
	if (deluge::memory::onRenderTier()) {
		render_[static_cast<size_t>(kind)].fetch_add(1, std::memory_order_relaxed);
	}
	if (!deluge_in_audio_context() && deluge_audio_excluded_depth() == 0) {
		unguarded_[static_cast<size_t>(kind)].fetch_add(1, std::memory_order_relaxed);
	}
#endif
}

uint32_t unguardedStructuralEdits(StructuralContainer kind) {
	return unguarded_[static_cast<size_t>(kind)].load(std::memory_order_relaxed);
}

uint32_t notedStructuralEdits(StructuralContainer kind) {
	return noted_[static_cast<size_t>(kind)].load(std::memory_order_relaxed);
}

uint32_t renderStructuralEdits(StructuralContainer kind) {
	return render_[static_cast<size_t>(kind)].load(std::memory_order_relaxed);
}

uint32_t unguardedStructuralEditsTotal() {
	uint32_t total = 0;
	for (const auto& count : unguarded_) {
		total += count.load(std::memory_order_relaxed);
	}
	return total;
}

} // namespace deluge

/// [task] C ABI for the host harnesses' exit report: deluge::unguardedStructuralEditsTotal().
extern "C" uint32_t deluge_unguarded_structural_edits_total(void) {
	return deluge::unguardedStructuralEditsTotal();
}
