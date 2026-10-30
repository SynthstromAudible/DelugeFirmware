// ThreadSanitizer stress harness for the containers the audio render walks: the real deluge::OrderedPosVector (the
// type behind a clip's note rows, a row's notes, an automation node list and an output's clip instances), the real
// RenderingDrums (a kit's drums rendering now, over stub drums), and a ClipArray-shaped vector of pointers (a song's
// session clips).
//
// Models the real split: an [audio] walker walks both containers inside an AudioExcludedSection, as the host render
// block now runs (libdeluge/system.h); a [task] editor inserts and erases, reallocating often. The critical-section
// primitives are stubbed with host semantics (one recursive global lock; deluge_in_audio_context() false), the same as
// the Rust BSP's host build, and the firmware's allocator with malloc/free.
//
// The OrderedPosVector edits call the product mutators unwrapped: their own AudioExcludedSection is what excludes
// the walker, so removing the section from a mutator (a neutering) turns this red. The pointer vector stands in for
// ClipArray's storage and is wrapped here; with STRESS_EDIT_UNGUARDED defined its edits run outside any section, the
// red case for the mechanism itself.
//
// Built and run by run.sh beside the other stress binaries.
#include "deluge/util/container/ordered_pos_vector.h"
#include "libdeluge/system.h"
#include "model/instrument/rendering_drums.h"
#include "processing/sound/sound_drum.h" // the stub in stubs/: a link and a magic
#include "util/structural_edit_probe.h"

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

extern "C" void ENTER_CRITICAL_SECTION() noexcept {
	g_critical_section.lock();
}
extern "C" void EXIT_CRITICAL_SECTION() noexcept {
	g_critical_section.unlock();
}
extern "C" bool deluge_in_audio_context() noexcept {
	return false;
}
// The firmware's panic, reached by RenderingDrums' misuse checks: a stress run that hits one fails loudly.
extern "C" void freezeWithError(char const* errmsg) {
	std::fprintf(stderr, "freezeWithError(%s)\n", errmsg);
	std::abort();
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
RenderingDrums g_drums;
SoundDrum g_drumPool[16];
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
			// The product mutator sections itself; the magic is written into a slot the walker could see only
			// mid-insert.
			(void)g_nodes.insertSorted(round * 31 + i * 7);
			edit([&] { g_clips.insert(g_clips.begin() + (g_clips.size() / 2), &g_dummy); });
		}
		while (g_nodes.size() > 8) {
			g_nodes.erase(g_nodes.begin());
		}
		while (g_clips.size() > 8) {
			edit([&] { g_clips.erase(g_clips.begin() + (g_clips.size() / 2)); });
		}
		if (round % 64 == 0) {
			g_nodes.clear();
		}
		// The product RenderingDrums sections its own adds and removes: a drum starting and stopping, in any order.
		// Adds and removes from this tier race the render's own (below): whether a drum is in is decided inside the
		// section, so either may find it already in or already out.
		for (int i = 0; i < 16; ++i) {
			g_drums.add(&g_drumPool[(round * 7 + i) % 16]);
		}
		for (int i = 0; i < 16; i += 2) {
			g_drums.remove(&g_drumPool[(round * 5 + i) % 16]);
		}
	}
	g_done.store(true, std::memory_order_release);
}

void walker() {
	uint64_t walks = 0;
	while (!g_done.load(std::memory_order_acquire)) {
		AudioExcludedSection render_block;
		for (const Node& node : g_nodes) {
			if (node.magic != kMagic) {
				std::fprintf(stderr, "walk saw a node mid-edit\n");
				std::abort();
			}
		}
		g_drums.walk([](SoundDrum* drum) {
			if (drum->magic != SoundDrum::kMagic) {
				std::fprintf(stderr, "walk saw a drum mid-splice\n");
				std::abort();
			}
		});
		// The render adds and removes drums too (a drum's voices starting and ending as it renders).
		g_drums.remove(&g_drumPool[walks % 16]);
		g_drums.add(&g_drumPool[(walks * 3) % 16]);
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
