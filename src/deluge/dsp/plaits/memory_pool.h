#pragma once
#include <array>
#include <cstddef>

namespace deluge::dsp {
// Stores only raw blocks: callers construct/destroy the DSP object on every use.
// Bounded retention avoids repeated allocator work for short notes.
template <size_t Capacity>
class PlaitsMemoryPool {
public:
	template <typename Allocate>
	void* acquire(Allocate allocate) {
		return count_ ? blocks_[--count_] : allocate();
	}
	template <typename Free>
	void release(void* block, bool deletingSong, Free free) {
		if (deletingSong) {
			while (count_)
				free(blocks_[--count_]);
		}
		if (!block)
			return;
		if (!deletingSong && count_ < Capacity)
			blocks_[count_++] = block;
		else
			free(block);
	}

private:
	std::array<void*, Capacity> blocks_{};
	size_t count_ = 0;
};
} // namespace deluge::dsp
