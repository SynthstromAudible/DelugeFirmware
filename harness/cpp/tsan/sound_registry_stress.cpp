// ThreadSanitizer stress harness for deluge::util::AudioOwnedList — the list AudioEngine::sounds is built on.
//
// Models the real split: a [task] producer adds entries (past several capacity doublings, as a kit song's load
// does), removes older ones, then POISONS and frees each removed entry; an [audio] consumer walks the list in a
// tight loop, as the render does, and checks every entry's magic. The critical-section primitives are stubbed with
// host semantics (one recursive global lock; deluge_in_audio_context() false), the same as the Rust BSP's host
// build. A walk that sees a removed entry reads poisoned or freed memory: TSan reports it, and the magic check
// aborts independently of TSan.
//
// Built and run by run.sh beside snapshot_primitive_stress.cpp.
#include "deluge/util/audio_owned_list.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>

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

namespace {

constexpr uint64_t kMagic = 0x5EED'50D0'CAFE'F00DULL;

struct Entry {
	uint64_t magic = kMagic;
	uint64_t check = ~kMagic;
};

deluge::util::AudioOwnedList<Entry> g_list;
std::atomic<bool> g_done{false};

void producer() {
	std::deque<Entry*> live;
	for (int round = 0; round < 2000; ++round) {
		for (int i = 0; i < 64; ++i) {
			auto* e = new Entry;
			g_list.add(e);
			live.push_back(e);
		}
		while (live.size() > 16) {
			Entry* old = live.front();
			live.pop_front();
			g_list.remove(old);
			std::memset(old, 0xDD, sizeof *old); // poison before freeing
			delete old;
		}
	}
	for (Entry* e : live) {
		g_list.remove(e);
		delete e;
	}
	g_done.store(true, std::memory_order_release);
}

void consumer() {
	uint64_t walks = 0;
	while (!g_done.load(std::memory_order_acquire)) {
		g_list.walk([](const auto& entries) {
			for (const Entry* e : entries) {
				if (e->magic != kMagic || e->check != ~kMagic) {
					std::fprintf(stderr, "walk saw a removed entry\n");
					std::abort();
				}
			}
		});
		++walks;
	}
	std::printf("consumer: %llu walks\n", static_cast<unsigned long long>(walks));
}

} // namespace

int main() {
	std::thread audio(consumer);
	std::thread task(producer);
	task.join();
	audio.join();
	std::printf("sound_registry_stress: clean\n");
	return 0;
}
