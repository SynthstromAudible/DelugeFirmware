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

/// @file
/// @brief The staging-release gate: releasing a staged document while the channel is still reading into it.
///
/// The byte channel's whole-file read writes into the caller's buffer from the storage tier and cannot be cancelled,
/// so a consumer that abandons a staged read -- a preset load superseded by a newer one, a song load given up -- must
/// not have that buffer freed under the read. The scenario holds a staged read in flight, releases its staging as an
/// abandoning consumer does, allocates the same size again and fills it with a sentinel, and lets the read finish. The
/// sentinel must survive: memory handed to someone else may not be written by a read its previous owner gave up on.
/// The released buffer must then still be freed once the read has finished, or the release has only traded the
/// corruption for a leak.
///
/// Gate staging-release:
///     a staged document read (the byte channel's read-into) that its consumer abandons while the byte channel holds
///     its buffer must not write into that memory once it has been freed and handed out again: the scenario releases
///     the staging mid-read, reallocates the same size filled with a sentinel, lets the read finish, and requires the
///     sentinel intact (SR-INTACT). SR-ISSUED requires the read to have been held when the staging was released, and
///     SR-REAPED requires the released buffer to be freed once the read has finished, so the release trades the
///     corruption for nothing, not for a leak. SR-STATUS requires a caller-buffer operation whose open fails to report
///     the open's own status: NOT_FOUND for a read of a missing document, and not a bare IO error for a write to a name
///     the card refuses.

#include "scenarios/support/scenario_declare.h"
#include "scenarios/support/scenario_report.h"

#include "io/file.hpp"
#include "libdeluge/file_bytes.h"
#include "memory/heaps.h"
#include "storage/storage_manager.h"

#include <cstdio>
#include <cstring>
#include <span>

// The headers above reach `third_party/printf/printf.h`, which soft-aliases `printf` to the embedded `printf_`. That
// implementation emits through `putchar_`, which the Rust BSP builds do not define -- this harness links the Rust BSP
// and has no provider, so the alias would turn every line this file writes into an undefined symbol at link time.
#ifdef printf
#undef printf
#endif

DELUGE_SCENARIO_DECLARE(staging_release);

namespace {

constexpr const char* kFolder = "SR_TEST";
constexpr const char* kPath = "SR_TEST/DOC.XML";
/// Big enough that the read is plainly more than a header, small enough to stage in one buffer.
constexpr uint32_t kDocBytes = 32 * 1024;
/// How long the byte channel holds each operation before it touches the card. Long enough that the release and the
/// reallocation below both happen while the read is still waiting to run.
constexpr uint32_t kLatencyMs = 50;
constexpr uint8_t kSentinel = 0xA5;
/// Ticks each stage's wait may take. Each tick is a virtual millisecond, so this is far past kLatencyMs.
constexpr int32_t kStageTicks = 5000;
/// The status legs' paths: a document that is not there, and a name the card refuses (`*` is not a FAT character).
constexpr const char* kAbsentPath = "SR_TEST/ABSENT.XML";
constexpr const char* kBadNamePath = "SR_TEST/BAD*NAME.XML";

enum class Stage : uint8_t {
	Fixture,
	Issue,
	Release,
	AwaitRead,
	Reap,
	StatusRead,
	StatusReadWait,
	StatusWrite,
	StatusWriteWait,
	Report,
	Done,
};

using deluge::scenarios::ScenarioReport;
using deluge::scenarios::Wait;
using deluge::scenarios::WaitState;

Stage g_stage = Stage::Fixture;
bool g_stepDone = false;
bool g_summaryPrinted = false;
ScenarioReport g_report{"SR"};
/// The wait of the stage in progress; each stage that waits starts its own.
Wait g_wait{g_report, "the staged open issuing its read", kStageTicks};
StorageManager::StagingState g_staging{};
/// The buffer allocated after the release, filled with kSentinel.
uint8_t* g_reused = nullptr;
uint32_t g_reusedSize = 0;
/// The status legs' caller buffers. Static: each must outlive its request until the collect is terminal.
uint8_t g_statusBuffer[64];
/// The status each status leg's collect reported.
DelugeStatus g_readStatus = DELUGE_OK;
DelugeStatus g_writeStatus = DELUGE_OK;

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
			g_report.fail("the fixture document could not be written");
			g_stage = Stage::Report;
			break;
		}
		deluge_file_bytes_set_latency_ms(kLatencyMs);
		g_wait = Wait{g_report, "the staged open issuing its read", kStageTicks};
		g_stage = Stage::Issue;
		break;

	case Stage::Issue: {
		// A consumer's staged open, ticked until the channel holds its buffer: the state a superseded preset load is
		// in when the newer request cancels it.
		const StorageManager::StagedOpen opened = StorageManager::openDocumentStaged(kPath, "doc", "", &g_staging);
		if (opened != StorageManager::StagedOpen::Deferred) {
			g_report.fail("the staged open finished before its read could be held");
			g_stage = Stage::Report;
			break;
		}
		switch (g_wait.poll(g_staging.issued)) {
		case WaitState::Pending:
			break;
		case WaitState::Met:
			printf("SR-ISSUED ticks=%d size=%u\n", g_wait.ticks(), g_staging.size);
			g_stage = Stage::Release;
			break;
		case WaitState::Expired:
			g_stage = Stage::Report;
			break;
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
			g_report.fail("no memory for the reallocation");
			g_stage = Stage::Report;
			break;
		}
		std::memset(g_reused, kSentinel, g_reusedSize);
		printf("SR-RELEASE size=%u sameAddress=%d\n", g_reusedSize, (g_reused == held) ? 1 : 0);
		g_wait = Wait{g_report, "the abandoned read's answer", kStageTicks};
		g_stage = Stage::AwaitRead;
		break;
	}

	case Stage::AwaitRead: {
		// Let the held read run to its answer. Nobody else collects it: the consumer that issued it has gone.
		uint32_t got = 0;
		const DelugeChannelStatus collected = deluge_file_bytes_collect_read_into(kPath, &got, nullptr, nullptr);
		const WaitState waited = g_wait.poll(collected != DELUGE_CHANNEL_PENDING);
		if (waited == WaitState::Pending) {
			break;
		}
		if (waited == WaitState::Met) {
			uint32_t overwritten = 0;
			for (uint32_t i = 0; i < g_reusedSize; i++) {
				overwritten += (g_reused[i] != kSentinel) ? 1 : 0;
			}
			printf("SR-INTACT overwritten=%u of=%u answer=%u ticks=%d\n", overwritten, g_reusedSize,
			       static_cast<unsigned>(collected), g_wait.ticks());
			if (overwritten != 0) {
				g_report.fail("a read abandoned by its consumer wrote into memory that had been freed and handed out "
				              "again");
			}
		}
		deluge::memory::dealloc(g_reused);
		g_reused = nullptr;
		g_wait = Wait{g_report, "the released buffer freed after its read finished", kStageTicks};
		g_stage = (waited == WaitState::Met) ? Stage::Reap : Stage::Report;
		break;
	}

	case Stage::Reap:
		// The channels' service task, which frees a parked buffer once the channel has let go of it.
		StorageManager::reapOrphanedStaging();
		if (g_wait.poll(StorageManager::orphanedStagingCount() == 0) == WaitState::Pending) {
			break;
		}
		// Printed met or not: the orphan count is this leg's evidence either way.
		printf("SR-REAPED orphans=%d ticks=%d\n", static_cast<int>(StorageManager::orphanedStagingCount()),
		       g_wait.ticks());
		g_wait = Wait{g_report, "the read of a missing document, accepted and answered", kStageTicks};
		g_stage = Stage::StatusRead;
		break;

	case Stage::StatusRead:
		// A caller-buffer operation whose open fails reports the open's own status: a missing document is NOT_FOUND,
		// not a card error.
		deluge_file_bytes_set_latency_ms(0);
		(void)deluge::io::unlink(kAbsentPath);
		if (deluge_file_bytes_request_read_into(kAbsentPath, g_statusBuffer, sizeof(g_statusBuffer))) {
			g_stage = Stage::StatusReadWait;
		}
		else if (g_wait.poll(false) == WaitState::Expired) {
			g_stage = Stage::Report;
		}
		break;

	case Stage::StatusReadWait: {
		DelugeStatus error = DELUGE_OK;
		const DelugeChannelStatus collected =
		    deluge_file_bytes_collect_read_into(kAbsentPath, nullptr, &error, nullptr);
		const WaitState waited =
		    g_wait.poll(collected != DELUGE_CHANNEL_PENDING && collected != DELUGE_CHANNEL_UNKNOWN);
		if (waited == WaitState::Pending) {
			// UNKNOWN: another caller's request displaced the answer before it was taken (a read's answer is not held
			// for its owner, because a read can be asked again). Ask again, as file_reader does.
			if (collected == DELUGE_CHANNEL_UNKNOWN) {
				g_stage = Stage::StatusRead;
			}
			break;
		}
		if (waited == WaitState::Expired) {
			g_stage = Stage::Report;
			break;
		}
		g_readStatus = (collected == DELUGE_CHANNEL_FAILED) ? error : DELUGE_OK;
		g_wait = Wait{g_report, "the write to a name the card refuses, accepted and answered", kStageTicks};
		g_stage = Stage::StatusWrite;
		break;
	}

	case Stage::StatusWrite:
		if (deluge_file_bytes_request_write_from(kBadNamePath, g_statusBuffer, sizeof(g_statusBuffer), true)) {
			g_stage = Stage::StatusWriteWait;
		}
		else if (g_wait.poll(false) == WaitState::Expired) {
			g_stage = Stage::Report;
		}
		break;

	case Stage::StatusWriteWait: {
		DelugeStatus error = DELUGE_OK;
		const DelugeChannelStatus collected = deluge_file_bytes_collect_write_from(kBadNamePath, nullptr, &error);
		const WaitState waited = g_wait.poll(collected != DELUGE_CHANNEL_PENDING);
		if (waited == WaitState::Pending) {
			break;
		}
		if (waited == WaitState::Met) {
			g_writeStatus = (collected == DELUGE_CHANNEL_FAILED) ? error : DELUGE_OK;
			printf("SR-STATUS read=%d write=%d\n", static_cast<int>(g_readStatus), static_cast<int>(g_writeStatus));
			if (g_readStatus != DELUGE_ERR_NOT_FOUND) {
				g_report.fail("a read of a missing document did not report NOT_FOUND");
			}
			if (g_writeStatus == DELUGE_OK || g_writeStatus == DELUGE_ERR_IO) {
				g_report.fail("a write to a name the card refuses did not report the open's own status");
			}
		}
		(void)deluge::io::unlink(kBadNamePath);
		g_stage = Stage::Report;
		break;
	}

	case Stage::Report:
		deluge_file_bytes_set_latency_ms(0);
		g_report.printResult();
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
	g_report.reset();
	g_wait = Wait{g_report, "the staged open issuing its read", kStageTicks};
	g_staging = {};
	g_reused = nullptr;
	g_reusedSize = 0;
	g_readStatus = DELUGE_OK;
	g_writeStatus = DELUGE_OK;
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
	return g_report.failures();
}

} // extern "C"
