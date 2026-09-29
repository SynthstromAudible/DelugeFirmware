/*
 * Copyright © 2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#include "scenarios/staging_release_scenario.h"

#include "io/file.hpp"
#include "libdeluge/file_write.h"
#include "memory/heaps.h"
#include "storage/storage_manager.h"

#include <cstdio>
#include <cstring>
#include <span>

// The headers above reach `src/lib/printf.h`, which soft-aliases `printf` to the embedded `printf_`. That
// implementation emits through `putchar_`, which the Rust BSP builds do not define -- this harness links the Rust BSP
// and has no provider, so the alias would turn every line this file writes into an undefined symbol at link time.
#ifdef printf
#undef printf
#endif

namespace {

constexpr const char* kFolder = "SR_TEST";
constexpr const char* kPath = "SR_TEST/DOC.XML";
/// Big enough that the read is plainly more than a header, small enough to stage in one buffer.
constexpr uint32_t kDocBytes = 32 * 1024;
/// How long the write channel holds each operation before it touches the card. Long enough that the release and the
/// reallocation below both happen while the read is still waiting to run.
constexpr uint32_t kLatencyMs = 50;
constexpr uint8_t kSentinel = 0xA5;
/// Ticks each stage may take. Each tick is a virtual millisecond, so this is far past kLatencyMs.
constexpr int32_t kStageTicks = 5000;

enum class Stage : uint8_t {
	Fixture,
	Issue,
	Release,
	AwaitRead,
	Reap,
	Report,
	Done,
};

Stage g_stage = Stage::Fixture;
bool g_stepDone = false;
bool g_summaryPrinted = false;
int32_t g_failures = 0;
int32_t g_ticks = 0;
StorageManager::StagingState g_staging{};
/// The buffer allocated after the release, filled with kSentinel.
uint8_t* g_reused = nullptr;
uint32_t g_reusedSize = 0;

void fail(const char* what) {
	printf("SR-FAIL %s\n", what);
	g_failures++;
}

/// A document of kDocBytes: a root tag, then filler. It is never parsed -- the read is abandoned before it lands.
bool writeFixture() {
	(void)deluge::io::mkdir(kFolder);
	auto file = deluge::io::File::open(kPath, DELUGE_FILE_WRITE_CREATE);
	if (!file.has_value()) {
		return false;
	}
	static uint8_t block[1024];
	std::memset(block, 'x', sizeof(block));
	std::memcpy(block, "<doc>", 5);
	for (uint32_t written = 0; written < kDocBytes; written += sizeof(block)) {
		if (!file->write(std::span{reinterpret_cast<const std::byte*>(block), sizeof(block)})) {
			return false;
		}
		std::memset(block, 'x', 5);
	}
	return file->close().has_value();
}

void runTick() {
	switch (g_stage) {
	case Stage::Fixture:
		if (!writeFixture()) {
			fail("the fixture document could not be written");
			g_stage = Stage::Report;
			break;
		}
		deluge_file_write_set_latency_ms(kLatencyMs);
		g_ticks = 0;
		g_stage = Stage::Issue;
		break;

	case Stage::Issue: {
		// A consumer's staged open, ticked until the channel holds its buffer: the state a superseded preset load is
		// in when the newer request cancels it.
		const StorageManager::StagedOpen opened = StorageManager::openDocumentStaged(kPath, "doc", "", &g_staging);
		if (opened != StorageManager::StagedOpen::Deferred) {
			fail("the staged open finished before its read could be held");
			g_stage = Stage::Report;
			break;
		}
		if (g_staging.issued) {
			printf("SR-ISSUED ticks=%d size=%u\n", g_ticks, g_staging.size);
			g_stage = Stage::Release;
		}
		else if (++g_ticks >= kStageTicks) {
			fail("the staged open never issued its read");
			g_stage = Stage::Report;
		}
		break;
	}

	case Stage::Release: {
		const uint8_t* const held = g_staging.buffer;
		g_reusedSize = g_staging.size;
		// What an abandoning consumer does: InstrumentLoad::abandon(), LoadSongUI::endLoad().
		StorageManager::releaseStaging(&g_staging);
		g_reused = static_cast<uint8_t*>(deluge::memory::alloc_sdram(g_reusedSize));
		if (g_reused == nullptr) {
			fail("no memory for the reallocation");
			g_stage = Stage::Report;
			break;
		}
		std::memset(g_reused, kSentinel, g_reusedSize);
		printf("SR-RELEASE size=%u sameAddress=%d\n", g_reusedSize, (g_reused == held) ? 1 : 0);
		g_ticks = 0;
		g_stage = Stage::AwaitRead;
		break;
	}

	case Stage::AwaitRead: {
		// Let the held read run to its answer. Nobody else collects it: the consumer that issued it has gone.
		uint32_t got = 0;
		uint32_t needed = 0;
		const uint8_t collected = deluge_file_write_collect_read_file(kPath, &got, &needed);
		if (collected == DELUGE_FILE_WRITE_PENDING && ++g_ticks < kStageTicks) {
			break;
		}
		uint32_t overwritten = 0;
		for (uint32_t i = 0; i < g_reusedSize; i++) {
			overwritten += (g_reused[i] != kSentinel) ? 1 : 0;
		}
		printf("SR-INTACT overwritten=%u of=%u answer=%u ticks=%d\n", overwritten, g_reusedSize,
		       static_cast<unsigned>(collected), g_ticks);
		if (overwritten != 0) {
			fail("a read abandoned by its consumer wrote into memory that had been freed and handed out again");
		}
		deluge::memory::dealloc(g_reused);
		g_reused = nullptr;
		g_ticks = 0;
		g_stage = Stage::Reap;
		break;
	}

	case Stage::Reap:
		// The channels' service task, which frees a parked buffer once the channel has let go of it.
		StorageManager::reapOrphanedStaging();
		if (StorageManager::orphanedStagingCount() == 0 || ++g_ticks >= kStageTicks) {
			printf("SR-REAPED orphans=%d ticks=%d\n", static_cast<int>(StorageManager::orphanedStagingCount()),
			       g_ticks);
			if (StorageManager::orphanedStagingCount() != 0) {
				fail("the released buffer was never freed after its read finished");
			}
			g_stage = Stage::Report;
		}
		break;

	case Stage::Report:
		deluge_file_write_set_latency_ms(0);
		printf("SR-RESULT failures=%d\n", static_cast<int>(g_failures));
		g_summaryPrinted = true;
		g_stage = Stage::Done;
		break;

	case Stage::Done:
		break;
	}
}

} // namespace

extern "C" {

void deluge_scenario_staging_release_begin(void) {
	setvbuf(stdout, nullptr, _IOLBF, 0);
	g_stage = Stage::Fixture;
	g_stepDone = false;
	g_summaryPrinted = false;
	g_failures = 0;
	g_ticks = 0;
	g_staging = {};
	g_reused = nullptr;
	g_reusedSize = 0;
}

bool deluge_scenario_staging_release_complete(void) {
	return g_summaryPrinted;
}

void deluge_scenario_staging_release_dispatch_step(void) {
	if (g_summaryPrinted) {
		return;
	}
	g_stepDone = false;
	runTick();
	g_stepDone = true;
}

bool deluge_scenario_staging_release_step_done(void) {
	return g_stepDone;
}

int32_t deluge_scenario_staging_release_failures(void) {
	return g_failures;
}

} // extern "C"
