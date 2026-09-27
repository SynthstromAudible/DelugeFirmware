// ThreadSanitizer stress harness for the containers the audio render walks: the real deluge::OrderedPosVector (the
// type behind a clip's note rows, a row's notes, an automation node list and an output's clip instances) and a
// ClipArray-shaped vector of pointers (a song's session clips).
//
// Models the real split: an [audio] walker walks both containers inside an AudioExcludedSection, as the host render
// block now runs (libdeluge/system.h); a [task] editor inserts and erases, reallocating often. The critical-section
// primitives are stubbed with host semantics (one recursive global lock; deluge_in_audio_context() false), the same as
// the Rust BSP's host build, and the firmware's allocator with malloc/free.
//
// With STRESS_EDIT_UNGUARDED defined the editor's edits run outside any section: the red case, which TSan must
// report. Without it the editor wraps each edit in an AudioExcludedSection, as the product containers now do.
//
// Built and run by run.sh beside the other stress binaries.
#include "deluge/util/container/ordered_pos_vector.h"
#include "libdeluge/system.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <vector>

namespace {
std::recursive_mutex g_critical_section;
}

extern "C" void ENTER_CRITICAL_SECTION() {
	g_critical_section.lock();
}
extern "C" void EXIT_CRITICAL_SECTION() {
	g_critical_section.unlock();
}
extern "C" bool deluge_in_audio_context() {
	return false;
}

namespace deluge::memory {
void* alloc_fast(std::size_t size, std::size_t) {
	return std::malloc(size);
}
void* alloc_sdram(std::size_t size, std::size_t) {
	return std::malloc(size);
}
void* alloc_external(std::size_t size, std::size_t) {
	return std::malloc(size);
}
void* alloc_stealable(std::size_t size, std::size_t) {
	return std::malloc(size);
}
void dealloc(void* p) {
	std::free(p);
}
} // namespace deluge::memory

namespace deluge {
void noteStructuralEdit(StructuralContainer) {
}
} // namespace deluge

namespace {

constexpr uint32_t kMagic = 0x5EED'F00Du;

struct Node {
	int32_t pos = 0;
	uint32_t magic = kMagic;
};

deluge::OrderedPosVector<Node> g_nodes;
std::vector<void*> g_clips; // ClipArray's shape: a vector of pointers, erased and inserted in the middle
std::atomic<bool> g_done{false};
int g_dummy = 0;

template <class F>
void edit(F&& f) {
#ifdef STRESS_EDIT_UNGUARDED
	f();
#else
	AudioExcludedSection section;
	f();
#endif
}

void editor() {
	for (int round = 0; round < 3000; ++round) {
		for (int i = 0; i < 24; ++i) {
			edit([&] {
				auto at = g_nodes.insertSorted(round * 31 + i * 7);
				if (at) {
					g_nodes[*at].magic = kMagic;
				}
			});
			edit([&] { g_clips.insert(g_clips.begin() + (g_clips.size() / 2), &g_dummy); });
		}
		while (g_nodes.size() > 8) {
			edit([&] { g_nodes.erase(g_nodes.begin()); });
		}
		while (g_clips.size() > 8) {
			edit([&] { g_clips.erase(g_clips.begin() + (g_clips.size() / 2)); });
		}
		if (round % 64 == 0) {
			edit([&] { g_nodes.clear(); });
		}
	}
	g_done.store(true, std::memory_order_release);
}

void walker() {
	uint64_t walks = 0;
	while (!g_done.load(std::memory_order_acquire)) {
		AudioExcludedSection renderBlock;
		for (const Node& node : g_nodes) {
			if (node.magic != kMagic) {
				std::fprintf(stderr, "walk saw a node mid-edit\n");
				std::abort();
			}
		}
		for (void* clip : g_clips) {
			if (clip != &g_dummy) {
				std::fprintf(stderr, "walk saw a clip slot mid-edit\n");
				std::abort();
			}
		}
		++walks;
	}
	std::printf("container_exclusion_stress: %llu walks\n", static_cast<unsigned long long>(walks));
}

} // namespace

int main() {
	std::thread render(walker);
	std::thread task(editor);
	task.join();
	render.join();
	return 0;
}
