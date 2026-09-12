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

#include "harness/favourites_channel_scenario.h"

#include "io/file.hpp"
#include "libdeluge/file_bytes.h"
#include "model/favourite/favourite_manager.h"
#include "storage/byte_channel.h"
#include "storage/owner.h"

#include <cstdio>
#include <cstring>
#include <span>
#include <string>
#include <vector>

// The headers above reach `src/lib/printf.h`, which soft-aliases `printf` to an implementation this harness
// has no provider for. Every line this file writes IS the gate's output.
#ifdef printf
#undef printf
#endif

namespace {

using deluge::favourites::BankState;

/// A category no real browser uses, so the gate owns every file it touches on the shared card image.
constexpr const char* kCategory = "FVGATE";
constexpr const char* kPathA = "SAMPLES/FVGATE/A.WAV";
constexpr const char* kPathB = "SAMPLES/FVGATE/B.WAV";
constexpr const char* kSeedName = "SAMPLES/FVGATE/SEEDED.WAV";
constexpr uint8_t kSeedSlot = 5;
constexpr uint8_t kSlotA = 3;
constexpr uint8_t kSlotB = 4;
constexpr uint8_t kRefusedSlot = 7;
constexpr uint8_t kBanks = 4;
constexpr int32_t kMaxWaitTicks = 200000;
/// How long every byte-channel operation waits during the order stage. Long enough that the save is still in flight
/// when the bank is selected away and back; without it the save lands within the same tick and the stage cannot see
/// whether anything waited for it.
constexpr uint32_t kOrderLatencyMs = 50;

enum class Stage : uint8_t {
	Prepare,
	SeedOpen,
	SeedSelect,
	SeedSet,
	SeedCheck,
	AbsentOpen,
	AbsentCheck,
	RefuseEdit,
	RefuseCheck,
	RoundTripSet,
	RoundTripAway,
	RoundTripBack,
	RoundTripCheck,
	OrderEdit,
	OrderCheck,
	StaleFirst,
	StaleSecond,
	StaleWait,
	StaleCheck,
	Cleanup,
	CleanupFiles,
	Report,
	Summary,
	Done,
	WaitIdle,
};

Stage g_stage = Stage::Prepare;
Stage g_afterIdle = Stage::Prepare;
bool g_stepDone = false;
bool g_summaryPrinted = false;
int32_t g_failures = 0;
int32_t g_waitTicks = 0;
BankState g_stateAtEdit = BankState::Loaded;
bool g_seedSeen = false;
int32_t g_issuedBeforeStale = 0;

const char* stateName(BankState state) {
	switch (state) {
	case BankState::Loading:
		return "loading";
	case BankState::Loaded:
		return "loaded";
	case BankState::Unavailable:
		return "unavailable";
	}
	return "?";
}

std::string bankFile(uint8_t bank) {
	return FavouritesManager::bankPath(kCategory, bank);
}

/// 1 present, 0 genuinely absent, -1 could not tell. Checks require an exact value, so a card that
/// cannot answer fails them rather than reading as "absent".
int32_t presence(const std::string& path) {
	auto opened = deluge::io::File::open(path, DELUGE_FILE_READ);
	if (opened.has_value()) {
		(void)opened->close();
		return 1;
	}
	return opened.error() == deluge::io::Status::NOT_FOUND ? 0 : -1;
}

bool readWholeFile(const std::string& path, std::vector<uint8_t>& out) {
	out.clear();
	auto opened = deluge::io::File::open(path, DELUGE_FILE_READ);
	if (!opened.has_value()) {
		return false;
	}
	auto size = opened->size();
	if (!size.has_value()) {
		(void)opened->close();
		return false;
	}
	out.resize(*size);
	if (!out.empty()) {
		auto got = opened->read(std::span<std::byte>(reinterpret_cast<std::byte*>(out.data()), out.size()));
		if (!got.has_value() || got->size() != out.size()) {
			(void)opened->close();
			return false;
		}
	}
	(void)opened->close();
	return true;
}

uint32_t crc32(const void* data, size_t len) {
	const auto* p = static_cast<const uint8_t*>(data);
	uint32_t crc = 0xFFFFFFFFu;
	for (size_t i = 0; i < len; i++) {
		crc ^= p[i];
		for (int32_t k = 0; k < 8; k++) {
			crc = (crc >> 1) ^ (0xEDB88320u & (~(crc & 1u) + 1u));
		}
	}
	return ~crc;
}

void waitIdleThen(Stage next) {
	g_afterIdle = next;
	g_waitTicks = 0;
	g_stage = Stage::WaitIdle;
}

void runTick() {
	switch (g_stage) {
	case Stage::Prepare:
		// The card image is shared with other gates and runs, so start from a known state.
		for (uint8_t bank = 0; bank < kBanks; bank++) {
			(void)deluge::io::unlink(bankFile(bank));
		}
		g_stage = Stage::SeedOpen;
		break;

	case Stage::SeedOpen:
		favouritesManager.setCategory(kCategory);
		waitIdleThen(Stage::SeedSelect);
		break;

	case Stage::SeedSelect:
		favouritesManager.selectFavouritesBank(2);
		waitIdleThen(Stage::SeedSet);
		break;

	case Stage::SeedSet:
		// Seeded through the real entry point rather than a hand-written file: a hand-written document that
		// the parser reads differently would fail the stale check for a reason that is not the defect.
		favouritesManager.setFavorite(kSeedSlot, 9, kSeedName);
		waitIdleThen(Stage::SeedCheck);
		break;

	case Stage::SeedCheck: {
		const int32_t there = presence(bankFile(2));
		printf("FV-SEED file=%d\n", there);
		if (there != 1) {
			printf("FV-SEED FAIL the seed favourite never reached bank 2's file; every later check would be "
			       "vacuous\n");
			g_failures++;
			g_stage = Stage::Report;
			break;
		}
		g_stage = Stage::AbsentOpen;
		break;
	}

	case Stage::AbsentOpen:
		favouritesManager.selectFavouritesBank(1);
		waitIdleThen(Stage::AbsentCheck);
		break;

	case Stage::AbsentCheck: {
		const int32_t there = presence(bankFile(1));
		const BankState state = favouritesManager.bankState();
		printf("FV-ABSENT file=%d state=%s\n", there, stateName(state));
		if (there != 0 || state != BankState::Loaded) {
			printf("FV-ABSENT FAIL opening a bank with no file must leave no file and a usable bank\n");
			g_failures++;
		}
		g_stage = Stage::RefuseEdit;
		break;
	}

	case Stage::RefuseEdit:
		// The edit is made in the SAME tick as the bank change, so the bank has not answered yet.
		favouritesManager.selectFavouritesBank(0);
		g_stateAtEdit = favouritesManager.bankState();
		favouritesManager.setFavorite(kRefusedSlot, 3, kPathA);
		waitIdleThen(Stage::RefuseCheck);
		break;

	case Stage::RefuseCheck: {
		const int32_t there = presence(bankFile(0));
		const bool slotEmpty = favouritesManager.isEmpty(kRefusedSlot);
		printf("FV-REFUSE stateAtEdit=%s file=%d slotEmpty=%d\n", stateName(g_stateAtEdit), there, (int)slotEmpty);
		if (g_stateAtEdit != BankState::Loading) {
			printf("FV-REFUSE FAIL the bank was not loading when the edit was made, so nothing was refused\n");
			g_failures++;
		}
		else if (there != 0 || !slotEmpty) {
			printf("FV-REFUSE FAIL an edit made while the bank was loading changed it or wrote it\n");
			g_failures++;
		}
		g_stage = Stage::RoundTripSet;
		break;
	}

	case Stage::RoundTripSet:
		favouritesManager.setFavorite(kSlotA, 6, kPathA);
		waitIdleThen(Stage::RoundTripAway);
		break;

	case Stage::RoundTripAway:
		favouritesManager.selectFavouritesBank(1);
		waitIdleThen(Stage::RoundTripBack);
		break;

	case Stage::RoundTripBack:
		favouritesManager.selectFavouritesBank(0);
		waitIdleThen(Stage::RoundTripCheck);
		break;

	case Stage::RoundTripCheck: {
		const bool survived = favouritesManager.getFavoriteFilename(kSlotA) == kPathA;
		std::vector<uint8_t> bytes;
		const bool read = readWholeFile(bankFile(0), bytes);
		const bool prolog = read && bytes.size() >= 5 && memcmp(bytes.data(), "<?xml", 5) == 0;
		printf("FV-ROUNDTRIP survived=%d bytes=%zu crc=%08X\n", (int)survived, read ? bytes.size() : size_t{0},
		       read ? crc32(bytes.data(), bytes.size()) : 0u);
		if (!survived || !prolog) {
			printf("FV-ROUNDTRIP FAIL a favourite did not survive leaving its bank and coming back, or its file is "
			       "not an XML document\n");
			g_failures++;
		}
		g_stage = Stage::OrderEdit;
		break;
	}

	case Stage::OrderEdit:
		// All three in one tick: the save must land before the bank is read back, whichever arm carries it.
#ifdef DELUGE_HOST
		// This scenario is compiled into every app build, but the knob exists only on the host.
		deluge_file_bytes_set_latency_ms(kOrderLatencyMs);
#endif
		favouritesManager.setFavorite(kSlotB, 2, kPathB);
		favouritesManager.selectFavouritesBank(1);
		favouritesManager.selectFavouritesBank(0);
		waitIdleThen(Stage::OrderCheck);
		break;

	case Stage::OrderCheck: {
		const bool newKept = favouritesManager.getFavoriteFilename(kSlotB) == kPathB;
		const bool oldKept = favouritesManager.getFavoriteFilename(kSlotA) == kPathA;
		const BankState state = favouritesManager.bankState();
#ifdef DELUGE_HOST
		deluge_file_bytes_set_latency_ms(0);
#endif
		printf("FV-ORDER new=%d old=%d state=%s latencyMs=%u\n", (int)newKept, (int)oldKept, stateName(state),
		       kOrderLatencyMs);
		if (!newKept || !oldKept || state != BankState::Loaded) {
			printf("FV-ORDER FAIL a favourite set just before switching away and back was lost: the bank was read "
			       "before its save landed\n");
			g_failures++;
		}
		g_stage = Stage::StaleFirst;
		break;
	}

	case Stage::StaleFirst:
		g_issuedBeforeStale = favouritesManager.intentsIssued();
		g_seedSeen = false;
		favouritesManager.selectFavouritesBank(2);
		g_stage = Stage::StaleSecond;
		break;

	case Stage::StaleSecond:
		// One tick later: this tick's service has issued bank 2's load, so bank 3 is selected while it is IN
		// FLIGHT. In the same tick the two loads would simply collapse, and nothing could go stale.
		favouritesManager.selectFavouritesBank(3);
		g_waitTicks = 0;
		g_stage = Stage::StaleWait;
		break;

	case Stage::StaleWait:
		if (!favouritesManager.isEmpty(kSeedSlot)) {
			g_seedSeen = true;
		}
		if (favouritesManager.idle()) {
			g_stage = Stage::StaleCheck;
			break;
		}
		if (++g_waitTicks > kMaxWaitTicks) {
			printf("FV-STALE FAIL the bank never went idle (%d ticks)\n", g_waitTicks);
			g_failures++;
			g_stage = Stage::Report;
		}
		break;

	case Stage::StaleCheck: {
		const int32_t loads = favouritesManager.intentsIssued() - g_issuedBeforeStale;
		const bool seedNow = !favouritesManager.isEmpty(kSeedSlot);
		const BankState state = favouritesManager.bankState();
		printf("FV-STALE loads=%d seedSeen=%d seedNow=%d state=%s\n", loads, (int)g_seedSeen, (int)seedNow,
		       stateName(state));
		if (loads < 2) {
			printf("FV-STALE FAIL bank 2's load was never issued, so no answer could go stale and this proves "
			       "nothing\n");
			g_failures++;
		}
		else if (g_seedSeen || seedNow || state != BankState::Loaded) {
			printf("FV-STALE FAIL bank 2's answer was applied after bank 3 had been selected\n");
			g_failures++;
		}
		g_stage = Stage::Cleanup;
		break;
	}

	case Stage::Cleanup:
		favouritesManager.close();
		waitIdleThen(Stage::CleanupFiles);
		break;

	case Stage::CleanupFiles:
		// Leave the card as the run found it.
		for (uint8_t bank = 0; bank < kBanks; bank++) {
			(void)deluge::io::unlink(bankFile(bank));
		}
		g_stage = Stage::Report;
		break;

	case Stage::Report: {
		const int32_t violations =
		    deluge::storage::bytes::interactionTierIo(deluge::storage::bytes::IoTier::Favourites);
		printf("FV-TIER violations=%d\n", violations);
		if (violations != 0) {
			printf("FV-TIER FAIL favourites card I/O ran on the interaction tier\n");
			g_failures++;
		}
		// Reported here, required by the script: served must be non-zero on the channel leg only.
		printf("FV-CHANNEL served=%d favourites=%d\n", deluge::storage::bytes::channelServedOps(),
		       favouritesManager.channelCompletions());
		const int32_t issued = favouritesManager.intentsIssued();
		const int32_t channel = favouritesManager.channelCompletions();
		const int32_t owner = favouritesManager.ownerCompletions();
		printf("FV-ACCOUNTED issued=%d channel=%d owner=%d\n", issued, channel, owner);
		if (issued == 0 || channel + owner != issued) {
			printf("FV-ACCOUNTED FAIL every issued card operation must end on exactly one arm\n");
			g_failures++;
		}
		g_stage = Stage::Summary;
		break;
	}

	case Stage::Summary:
		printf("FV-RESULT failures=%d\n", g_failures);
		// Piped into a script stdout is block-buffered and the harness exits without flushing C stdio.
		fflush(stdout);
		g_summaryPrinted = true;
		g_stage = Stage::Done;
		break;

	case Stage::Done:
		break;

	case Stage::WaitIdle:
		if (favouritesManager.idle()) {
			g_stage = g_afterIdle;
			break;
		}
		if (++g_waitTicks > kMaxWaitTicks) {
			printf("FV-WAIT FAIL the favourites bank never went idle (%d ticks)\n", g_waitTicks);
			g_failures++;
			g_stage = Stage::Report;
		}
		break;
	}
}

} // namespace

extern "C" {

void deluge_scenario_favourites_channel_begin(void) {
	setvbuf(stdout, nullptr, _IOLBF, 0);
	g_stage = Stage::Prepare;
	g_afterIdle = Stage::Prepare;
	g_stepDone = false;
	g_summaryPrinted = false;
	g_failures = 0;
	g_waitTicks = 0;
	g_seedSeen = false;
	g_issuedBeforeStale = 0;
}

bool deluge_scenario_favourites_channel_complete(void) {
	return g_summaryPrinted;
}

void deluge_scenario_favourites_channel_dispatch_step(void) {
	if (g_summaryPrinted) {
		return;
	}
	g_stepDone = false;
	// The firmware drives both from the "settings io" task; here the scenario owns the tick. Channel first,
	// so an answer it delivers is applied by the bank in the same tick.
	deluge::storage::bytes::serviceRoutine();
	favouritesManager.serviceRoutine();
	runTick();
	// A marker behind whatever the tick queued: the owner queue is FIFO, so this runs after it.
	if (!deluge::storage::Owner::run([](void*) { g_stepDone = true; }, nullptr)) {
		g_stepDone = true;
	}
}

bool deluge_scenario_favourites_channel_step_done(void) {
	return g_stepDone;
}

int32_t deluge_scenario_favourites_channel_failures(void) {
	return g_failures;
}

} // extern "C"
