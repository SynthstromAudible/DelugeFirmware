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

#include "harness/document_save_channel_scenario.h"

#ifdef DELUGE_HOST

#include "gui/context_menu/overwrite_file.h"
#include "gui/ui/browser/browser.h"
#include "gui/ui/save/save_instrument_preset_ui.h"
#include "gui/ui/save/save_ui.h"
#include "gui/ui/ui.h"
#include "gui/views/arranger_view.h"
#include "io/file.hpp"
#include "processing/engines/audio_engine.h"
#include "storage/owner.h"
#include "storage/storage_manager.h"
#include <cstdio>
#include <string>
#include <vector>

// The GUI headers above reach `src/lib/printf.h`, which soft-aliases `printf` to the embedded
// `printf_`. That implementation emits through `putchar_`, which only the C host BSP defines -- this
// harness links the Rust BSP and has no provider, so the alias turns every line this file writes into
// an undefined symbol at link time. Every one of those lines IS the gate's output.
#ifdef printf
#undef printf
#endif

/// Reaches `SaveUI::enterKeyPress()`, which is protected: the gate drives the REAL gesture rather
/// than performSave() underneath it. Mirrors SaveRoundtripProbe and SongLoadChannelProbe.
class DocumentSaveChannelProbe {
public:
	static bool openOn() {
		Browser::enteredText.clear();
		if (!openUI(&saveInstrumentPresetUI)) {
			return false;
		}
		for (int32_t t = 0; Browser::isListingInProgress() && t < 200; t++) {
			saveInstrumentPresetUI.graphicsRoutine();
		}
		return !Browser::isListingInProgress();
	}
	static bool listingInProgress() { return Browser::isListingInProgress(); }
	static void typeName(const char* name) { Browser::enteredText = name; }
	static void pressEnter() { saveInstrumentPresetUI.enterKeyPress(); }
	static std::string targetPath() { return Browser::currentDir + "/" + Browser::enteredText + ".XML"; }
	static bool overwritePromptOpen() { return getCurrentUI() == &deluge::gui::context_menu::overwriteFile; }
};

namespace {

constexpr const char* kSaveName = "DSTEST01";

enum class Stage : uint8_t {
	Fixture,
	FixtureWait,
	Save,
	SaveWait,
	Verify,
	Occupied,
	OccupiedWait,
	OccupiedCheck,
	Report,
	Done,
};

Stage g_stage = Stage::Fixture;
bool g_stepDone = false;
bool g_summaryPrinted = false;
int32_t g_failures = 0;
int32_t g_waitTicks = 0;
uint32_t g_crc = 0;
size_t g_bytes = 0;
std::string g_path;

/// A save spans ticks by construction; this bound exists so a save that never lands ends the run with
/// a REPORT rather than a silent timeout.
constexpr int32_t kMaxWaitTicks = 200000;

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

bool readWholeFile(const char* path, std::vector<uint8_t>& out) {
	auto opened = deluge::io::File::open(path, DELUGE_FILE_READ);
	if (!opened.has_value()) {
		return false;
	}
	auto size = opened->size();
	if (!size || *size == 0) {
		return false;
	}
	out.assign(*size, 0);
	auto read = opened->read(std::span<std::byte>(reinterpret_cast<std::byte*>(out.data()), *size));
	(void)opened->close();
	return read.has_value() && read->size() == *size;
}

void runTick() {
	switch (g_stage) {
	case Stage::Fixture: {
		// A root UI beneath, because the overwrite prompt this gate provokes is pushed with openUI().
		changeRootUI(&arrangerView);
		(void)DocumentSaveChannelProbe::openOn();
		g_stage = Stage::FixtureWait;
		break;
	}
	case Stage::FixtureWait: {
		if (!DocumentSaveChannelProbe::listingInProgress()) {
			g_stage = Stage::Save;
		}
		break;
	}
	case Stage::Save: {
		DocumentSaveChannelProbe::typeName(kSaveName);
		g_path = DocumentSaveChannelProbe::targetPath();
		// Start from a known-absent target so this is genuinely the "free name" case.
		(void)deluge::io::unlink(g_path.c_str());
		DocumentSaveChannelProbe::pressEnter();
		g_stage = Stage::SaveWait;
		break;
	}
	case Stage::SaveWait: {
		// saveInFlight(), NOT the UI being open: a staged save parks between ticks, and the save
		// screen stays up either way.
		if (!SaveUI::saveInFlight()) {
			g_stage = Stage::Verify;
			break;
		}
		if (++g_waitTicks > kMaxWaitTicks) {
			printf("DS-SAVED FAIL the save never finished (%d ticks)\n", g_waitTicks);
			g_failures++;
			g_stage = Stage::Report;
		}
		break;
	}
	case Stage::Verify: {
		std::vector<uint8_t> bytes;
		if (!readWholeFile(g_path.c_str(), bytes)) {
			printf("DS-SAVED FAIL nothing was written to %s\n", g_path.c_str());
			g_failures++;
			g_stage = Stage::Report;
			break;
		}
		g_bytes = bytes.size();
		g_crc = crc32(bytes.data(), bytes.size());
		// Shallow on purpose: the cross-leg CRC comparison in the script is what carries the weight.
		// These two catch the shapes that would make that comparison vacuous -- a document with no
		// prolog (which is what a staged save that forgot it produces) or one too short to be real.
		if (bytes.size() < 100 || memcmp(bytes.data(), "<?xml", 5) != 0) {
			printf("DS-SAVED FAIL the written document is short or has no prolog (%zu bytes)\n", bytes.size());
			g_failures++;
		}
		printf("DS-SAVED path=%s bytes=%zu crc=%08X\n", g_path.c_str(), g_bytes, g_crc);
		g_stage = Stage::Occupied;
		break;
	}
	case Stage::Occupied: {
		// The SAME name again. The user did not ask to replace it, so this must PROMPT and must not
		// write -- silently replacing somebody's preset is the failure this stage exists to catch.
		DocumentSaveChannelProbe::typeName(kSaveName);
		DocumentSaveChannelProbe::pressEnter();
		g_waitTicks = 0;
		g_stage = Stage::OccupiedWait;
		break;
	}
	case Stage::OccupiedWait: {
		if (!SaveUI::saveInFlight() || ++g_waitTicks > kMaxWaitTicks) {
			g_stage = Stage::OccupiedCheck;
		}
		break;
	}
	case Stage::OccupiedCheck: {
		const bool prompted = DocumentSaveChannelProbe::overwritePromptOpen();
		std::vector<uint8_t> bytes;
		const bool stillThere = readWholeFile(g_path.c_str(), bytes);
		const uint32_t crcNow = stillThere ? crc32(bytes.data(), bytes.size()) : 0;
		printf("DS-OCCUPIED prompted=%d unchanged=%d\n", (int)prompted, (int)(stillThere && crcNow == g_crc));
		if (!prompted) {
			printf("DS-OCCUPIED FAIL a taken name did not prompt\n");
			g_failures++;
		}
		if (!stillThere || crcNow != g_crc) {
			printf("DS-OCCUPIED FAIL the existing document was modified without the user agreeing\n");
			g_failures++;
		}
		g_stage = Stage::Report;
		break;
	}
	case Stage::Report: {
		printf("DS-SUMMARY served=%d offOwnerTicks=%d\n", (int)SaveUI::writeChannelServedOps(),
		       (int)SaveUI::offOwnerSaveTicks());
		printf("DS-RESULT failures=%d\n", g_failures);
		// Leave the card as we found it: these images are fingerprint-keyed and SHARED between gates,
		// and a file left behind here changes what another gate's browser listing sees.
		if (!g_path.empty()) {
			(void)deluge::io::unlink(g_path.c_str());
		}
		fflush(stdout);
		g_summaryPrinted = true;
		g_stage = Stage::Done;
		break;
	}
	case Stage::Done:
		break;
	}
}

} // namespace

extern "C" {

void deluge_scenario_document_save_channel_begin(void) {
	// Line-buffer stdout: piped into a script it is block-buffered by default, and the harness exits
	// without flushing C stdio, so every DS- line -- which IS this gate's output -- would be lost
	// exactly when the gate fails.
	setvbuf(stdout, nullptr, _IOLBF, 0);
	g_stage = Stage::Fixture;
	g_stepDone = false;
	g_summaryPrinted = false;
	g_failures = 0;
	g_waitTicks = 0;
	g_crc = 0;
	g_bytes = 0;
	g_path.clear();
}

bool deluge_scenario_document_save_channel_complete(void) {
	return g_summaryPrinted;
}

void deluge_scenario_document_save_channel_dispatch_step(void) {
	if (g_summaryPrinted) {
		return;
	}
	g_stepDone = false;
	runTick();
	// A marker op behind whatever the tick queued: the owner queue is FIFO, so this runs after any
	// work the tick dispatched. Setting g_stepDone directly races the owner, and the scenario sails
	// past work it only just queued.
	if (!deluge::storage::Owner::run([](void*) { g_stepDone = true; }, nullptr)) {
		g_stepDone = true;
	}
}

bool deluge_scenario_document_save_channel_step_done(void) {
	return g_stepDone;
}

int32_t deluge_scenario_document_save_channel_failures(void) {
	return g_failures;
}

} // extern "C"

#endif // DELUGE_HOST
