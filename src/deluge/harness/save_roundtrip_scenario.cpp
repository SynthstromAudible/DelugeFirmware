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

#include "harness/save_roundtrip_scenario.h"

#ifdef DELUGE_HOST

#include "gui/context_menu/overwrite_file.h"
#include "gui/ui/browser/browser.h"
#include "gui/ui/load/load_song_ui.h"
#include "gui/ui/save/save_song_ui.h"
#include "gui/ui/save/save_ui.h"
#include "gui/ui/ui.h"
#include "gui/views/arranger_view.h"
#include "io/file.hpp"
#include "model/song/song.h"
#include "storage/owner.h"
#include "storage/storage_manager.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

// The GUI headers above reach `src/lib/printf.h`, which soft-aliases `printf` to the embedded
// `printf_`. That implementation emits through `putchar_`, which only the C host BSP defines --
// this harness links the Rust BSP and has no provider, so the alias turns every line this file
// writes into an undefined symbol at link time. Every one of those lines IS the gate's output.
#ifdef printf
#undef printf
#endif

/// Reaches `SaveUI::enterKeyPress()`, which is protected: the gate must drive the REAL gesture, not
/// the save machinery underneath it, or it would keep testing the unconverted path forever. Mirrors
/// `BrowserListingAbProbe`'s friendship in `browser.h`.
class SaveRoundtripProbe {
public:
	/// Put the save browser on a folder, exactly as reaching the save screen does.
	///
	/// The gesture below reads getCurrentFileItem(), so the listing is not optional -- setting
	/// currentDir alone leaves fileItems empty and the gesture runs off the end.
	static bool openOn(const char* dir) {
		Browser::currentDir = dir;
		Browser::enteredText.clear();
		// openUI() rather than beginListing() alone: a finished save ENDS in close(), which pops the UI
		// stack, so the save UI has to actually be on it or the success path underflows. That is why
		// Fixture sets a root UI first.
		if (!openUI(&saveSongUI)) {
			return false;
		}
		for (int32_t t = 0; Browser::isListingInProgress() && t < 200; t++) {
			saveSongUI.graphicsRoutine();
		}
		return !Browser::isListingInProgress();
	}
	/// Tick the browser just opened while its listing runs, and say whether it still is. Nothing else in the
	/// headless harness ticks a browser, and the listing runs on the browser's tick surface.
	static bool listingInProgress() {
		if (Browser::isListingInProgress()) {
			getCurrentUI()->graphicsRoutine();
		}
		return Browser::isListingInProgress();
	}
	/// The name the user has typed, which is what the save is aimed at.
	static void typeName(const char* name) { Browser::enteredText = name; }
	/// The shipped entry an ENTER press reaches.
	static void pressEnter() { saveSongUI.enterKeyPress(); }
	static bool overwritePromptOpen() { return getCurrentUI() == &deluge::gui::context_menu::overwriteFile; }
};

namespace {

constexpr const char* kSaveDir = "SONGS";
constexpr const char* kSaveName = "SVTEST01";
constexpr const char* kSavePath = "SONGS/SVTEST01.XML";

enum class Stage : uint8_t {
	Fixture,
	FixtureWait,
	SaveFirst,
	SaveFirstWait,
	SaveFirstCheck,
	LoadBack,
	LoadBackListing,
	LoadBackWait,
	Reopen,
	SaveSecond,
	SaveSecondWait,
	SaveSecondCheck,
	Confirm,
	ConfirmWait,
	ConfirmCheck,
	Report,
	Done,
};

Stage g_stage = Stage::Fixture;
bool g_stepDone = false;
bool g_summaryPrinted = false;
int32_t g_failures = 0;
uint32_t g_crcFirst = 0;
uint32_t g_crcSecond = 0;
size_t g_bytesFirst = 0;
std::vector<uint8_t> g_first;
int32_t g_saveWaitTicks = 0;

/// How long one save is given to finish before the run calls it wedged. A bound, not a timeout: an
/// unbounded wait presents as the harness hanging rather than as a failure anyone can read.
constexpr int32_t kMaxSaveWaitTicks = 200000;

/// Whether the save machinery has gone quiet.
///
/// A song save has not finished when `enterKeyPress()` returns. It serialises, writes and finishes
/// across ticks, and collects samples on a second machine beside that -- so reading the file back on
/// the very next tick asks the card a question the save has not answered yet. This scenario used to
/// do exactly that and passed anyway, because at the time the save still happened to complete inside
/// the keypress. It stopped being true the moment the collection stopped being kicked off inline,
/// and the gate reported "the save wrote no file" for a save that was merely still running.
bool saveMachineryIdle() {
	// Three machines now, not two: the sample collection, the staged document write the song save
	// runs on a machine of its own, and the staged-save machine the base class runs. Miss any one of
	// them and this reads the card while the save is still going, which is how this check failed the
	// last two times the save was split further.
	return !saveSongUI.collectInFlight() && !saveSongUI.docInFlight() && !SaveUI::saveInFlight();
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

/// Read a whole file. Returns false if it could not be opened -- which for this gate means the save
/// did not happen, and is the failure the whole scenario exists to surface.
bool readWholeFile(const char* path, std::vector<uint8_t>& out) {
	auto opened = deluge::io::File::open(path, DELUGE_FILE_READ);
	if (!opened.has_value()) {
		return false;
	}
	out.clear();
	uint8_t buf[512];
	while (true) {
		// read() returns the SUBSPAN actually filled, not a count: a short span is the end of file.
		auto read = opened->read(std::as_writable_bytes(std::span{buf, sizeof(buf)}));
		if (!read.has_value() || read->empty()) {
			break;
		}
		out.insert(out.end(), buf, buf + read->size());
	}
	(void)opened->close();
	return true;
}

/// The song file embeds a hex dump of `PadLEDs::imageStore` as its `preview` attribute -- 144 pads x
/// 3 colours, the artwork the browser draws (song.cpp writes it right after `previewNumPads`). That
/// is live DISPLAY state, so it legitimately differs between two saves separated by a load, and a
/// byte-identity check over the whole file is asserting something the format does not promise.
///
/// So the round trip compares everything EXCEPT that one attribute's value, and asserts separately
/// that the attribute is still present and the right length -- skipping a region without checking it
/// is still there would let a save that dropped the artwork entirely pass.
constexpr const char* kPreviewTag = "preview";

/// @return the [start,end) byte range of the preview attribute's VALUE, or {0,0} if absent.
std::pair<size_t, size_t> previewValueRange(const std::vector<uint8_t>& bytes) {
	const std::string hay(reinterpret_cast<const char*>(bytes.data()), bytes.size());
	// `previewNumPads` is written FIRST and contains "preview" as a prefix, so search past it --
	// matching it instead finds a 3-character value ("144") and silently compares the wrong field.
	size_t tag = hay.find(kPreviewTag);
	while (tag != std::string::npos) {
		const size_t after = tag + std::char_traits<char>::length(kPreviewTag);
		const char next = after < hay.size() ? hay[after] : '\0';
		if (!((next >= 'A' && next <= 'Z') || (next >= 'a' && next <= 'z'))) {
			break; // a real `preview` tag, not `previewNumPads`
		}
		tag = hay.find(kPreviewTag, after);
	}
	if (tag == std::string::npos) {
		return {0, 0};
	}
	const size_t open = hay.find('"', tag);
	if (open == std::string::npos) {
		return {0, 0};
	}
	const size_t close = hay.find('"', open + 1);
	if (close == std::string::npos) {
		return {0, 0};
	}
	return {open + 1, close};
}

/// Pull one integer attribute's value out of a saved song, for reporting what drifted.
int32_t attrValue(const std::vector<uint8_t>& bytes, const char* name) {
	const std::string hay(reinterpret_cast<const char*>(bytes.data()), bytes.size());
	const size_t at = hay.find(name);
	if (at == std::string::npos) {
		return -9999;
	}
	const size_t open = hay.find('"', at);
	if (open == std::string::npos) {
		return -9999;
	}
	return std::atoi(hay.c_str() + open + 1);
}

/// Compare two saves ignoring only the preview artwork. Reports what it found either way.
bool sameIgnoringPreview(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
	const auto ra = previewValueRange(a);
	const auto rb = previewValueRange(b);
	if (ra.second == 0 || rb.second == 0) {
		printf("SR-ROUNDTRIP FAIL a save wrote no preview attribute at all\n");
		return false;
	}
	const size_t lena = ra.second - ra.first;
	const size_t lenb = rb.second - rb.first;
	// 144 pads * 3 colours * 2 hex chars. Pinned so a save that truncated the artwork still fails.
	if (lena != 864 || lenb != 864) {
		printf("SR-ROUNDTRIP FAIL preview length first=%zu second=%zu, wanted 864\n", lena, lenb);
		return false;
	}
	if (a.size() != b.size()) {
		return false;
	}
	for (size_t i = 0; i < a.size(); i++) {
		if (i >= ra.first && i < ra.second) {
			continue; // the artwork, which is display state
		}
		if (a[i] != b[i]) {
			printf("SR-ROUNDTRIP FAIL first difference outside the preview at offset %zu\n", i);
			char pre[65] = {0};
			char fa[49] = {0};
			char fb[49] = {0};
			const size_t back = i > 64 ? 64 : i;
			for (size_t k = 0; k < back; k++) {
				const uint8_t c = a[i - back + k];
				pre[k] = (c >= 32 && c < 127) ? (char)c : '.';
			}
			for (size_t k = 0; k < 48 && i + k < a.size(); k++) {
				fa[k] = (a[i + k] >= 32 && a[i + k] < 127) ? (char)a[i + k] : '.';
				fb[k] = (b[i + k] >= 32 && b[i + k] < 127) ? (char)b[i + k] : '.';
			}
			printf("SR-MAGNITUDE first=%d second=%d (song.cpp writes the SUM of\n"
			       "  insideWorldTickMagnitude + insideWorldTickMagnitudeOffsetFromBPM, but the load\n"
			       "  assigns it to insideWorldTickMagnitude ALONE and recomputes the offset)\n",
			       attrValue(a, "inputTickMagnitude"), attrValue(b, "inputTickMagnitude"));
			printf("SR-FIELD context=[%s]\n", pre);
			printf("SR-FIELD first =[%s]\n", fa);
			printf("SR-FIELD second=[%s]\n", fb);
			return false;
		}
	}
	return true;
}

void runTick() {
	switch (g_stage) {
	case Stage::Fixture: {
		// Start from a known-absent target so the first save is genuinely the "free name" case.
		(void)deluge::io::mkdir(kSaveDir);
		(void)deluge::io::unlink(kSavePath);
		// the save pushes the overwrite context menu with openUI(), which needs a root UI
		// beneath it -- this harness boots without one (see openOn()'s note).
		changeRootUI(&arrangerView);
		(void)SaveRoundtripProbe::openOn(kSaveDir);
		g_stage = Stage::FixtureWait;
		break;
	}
	case Stage::FixtureWait: {
		if (!SaveRoundtripProbe::listingInProgress()) {
			g_stage = Stage::SaveFirst;
		}
		break;
	}
	case Stage::SaveFirst: {
		SaveRoundtripProbe::typeName(kSaveName);
		SaveRoundtripProbe::pressEnter();
		g_saveWaitTicks = 0;
		g_stage = Stage::SaveFirstWait;
		break;
	}
	case Stage::SaveFirstWait: {
		if (saveMachineryIdle()) {
			g_stage = Stage::SaveFirstCheck;
			break;
		}
		if (++g_saveWaitTicks > kMaxSaveWaitTicks) {
			printf("SR-SEAM FAIL free save never finished (%d ticks)\n", g_saveWaitTicks);
			g_failures++;
			g_stage = Stage::SaveFirstCheck;
		}
		break;
	}
	case Stage::SaveFirstCheck: {
		// A free name must NOT prompt.
		if (SaveRoundtripProbe::overwritePromptOpen()) {
			printf("SR-SEAM FAIL free name opened the overwrite prompt\n");
			g_failures++;
		}
		std::vector<uint8_t> bytes;
		if (!readWholeFile(kSavePath, bytes)) {
			printf("SR-SEAM FAIL free prompted=0 created=0 -- the save wrote no file at %s\n", kSavePath);
			g_failures++;
		}
		else {
			g_crcFirst = crc32(bytes.data(), bytes.size());
			g_bytesFirst = bytes.size();
			g_first = bytes;
			printf("SR-SEAM free prompted=0 created=1 bytes=%zu\n", g_bytesFirst);
		}
		g_stage = Stage::LoadBack;
		break;
	}
	case Stage::LoadBack: {
		currentSong->setSongFullPath(kSavePath);
		(void)openUI(&loadSongUI);
		g_saveWaitTicks = 0;
		g_stage = Stage::LoadBackListing;
		break;
	}
	case Stage::LoadBackListing: {
		// The browser's own listing lands first, as a LOAD press waits for it. Loading over a listing still
		// suspended would leave its machine active, and the Reopen stage's save browser would resume it as its
		// own.
		if (Browser::isListingInProgress()) {
			loadSongUI.graphicsRoutine();
			if (++g_saveWaitTicks > kMaxSaveWaitTicks) {
				printf("SR-LOAD FAIL the load browser's listing never finished (%d ticks)\n", g_saveWaitTicks);
				g_failures++;
				g_stage = Stage::Report;
			}
			break;
		}
		loadSongUI.beginLoad();
		g_stage = Stage::LoadBackWait;
		break;
	}
	case Stage::LoadBackWait: {
		if (!loadSongUI.isLoadingSong()) {
			g_stage = Stage::Reopen;
		}
		break;
	}
	case Stage::Reopen: {
		// Re-open the save browser: a successful save ENDS in close(), which pops it off the UI
		// stack, so a second save has to reach the save screen again exactly as the user would.
		// Driving the popped browser instead makes close() search a stack it is no longer on and
		// index off the end of it.
		(void)SaveRoundtripProbe::openOn(kSaveDir);
		// Typing happens on the NEXT tick, not this one. The listing this open starts is async, and
		// when it settles it picks a brand-new unused name -- which silently overwrote the typed
		// name here and aimed the second save at a free slot instead of the existing file.
		g_stage = Stage::SaveSecond;
		break;
	}
	case Stage::SaveSecond: {
		// The SAME name, deliberately: a finished song save writes currentSong->name = enteredText, so saving
		// under a different name would differ for a reason that is not a defect. Overwriting keeps
		// every name-derived field identical, and makes this the occupied-prompt case too.
		SaveRoundtripProbe::typeName(kSaveName);
		SaveRoundtripProbe::pressEnter();
		g_saveWaitTicks = 0;
		g_stage = Stage::SaveSecondWait;
		break;
	}
	case Stage::SaveSecondWait: {
		// The existence answer lands on a LATER tick than the key press. This check used to read the
		// prompt straight away, which held only while the check ran synchronously through the owner
		// queue that every harness step drains.
		if (saveMachineryIdle()) {
			g_stage = Stage::SaveSecondCheck;
			break;
		}
		if (++g_saveWaitTicks > kMaxSaveWaitTicks) {
			printf("SR-SEAM FAIL the occupied save never answered (%d ticks)\n", g_saveWaitTicks);
			g_failures++;
			g_stage = Stage::Report;
		}
		break;
	}
	case Stage::SaveSecondCheck: {
		if (!SaveRoundtripProbe::overwritePromptOpen()) {
			std::vector<uint8_t> probe;
			printf("SR-DIAG dir=%s entered=%s targetExists=%d\n", Browser::currentDir.c_str(),
			       Browser::enteredText.c_str(), readWholeFile(kSavePath, probe) ? 1 : 0);
			printf("SR-SEAM FAIL occupied prompted=0 -- an existing file did not prompt\n");
			g_failures++;
			g_stage = Stage::Report; // nothing to confirm; skip the confirmation case
			break;
		}
		printf("SR-SEAM occupied prompted=1\n");
		g_stage = Stage::Confirm;
		break;
	}
	case Stage::Confirm: {
		(void)deluge::gui::context_menu::overwriteFile.acceptCurrentOption();
		g_saveWaitTicks = 0;
		g_stage = Stage::ConfirmWait;
		break;
	}
	case Stage::ConfirmWait: {
		// Confirming the prompt STARTS the overwrite; it does not perform it.
		if (saveMachineryIdle()) {
			g_stage = Stage::ConfirmCheck;
			break;
		}
		if (++g_saveWaitTicks > kMaxSaveWaitTicks) {
			printf("SR-ROUNDTRIP FAIL the confirmed overwrite never finished (%d ticks)\n", g_saveWaitTicks);
			g_failures++;
			g_stage = Stage::ConfirmCheck;
		}
		break;
	}
	case Stage::ConfirmCheck: {
		std::vector<uint8_t> bytes;
		if (!readWholeFile(kSavePath, bytes)) {
			printf("SR-ROUNDTRIP FAIL the confirmed overwrite left no file at %s\n", kSavePath);
			g_failures++;
		}
		else {
			g_crcSecond = crc32(bytes.data(), bytes.size());
			printf("SR-ROUNDTRIP first=%08X second=%08X bytes=%zu\n", g_crcFirst, g_crcSecond, bytes.size());
			if (!sameIgnoringPreview(g_first, bytes)) {
				printf("SR-ROUNDTRIP FAIL first=%08X second=%08X -- the round trip changed the song\n", g_crcFirst,
				       g_crcSecond);
				// Report WHERE they diverge: a field that legitimately varies looks very different
				// from a truncation, and the byte counts here are equal.
				size_t n = g_first.size() < bytes.size() ? g_first.size() : bytes.size();
				for (size_t d = 0; d < n; d++) {
					if (g_first[d] != bytes[d]) {
						char a[41] = {0};
						char b[41] = {0};
						for (size_t k = 0; k < 40 && d + k < n; k++) {
							a[k] = (g_first[d + k] >= 32 && g_first[d + k] < 127) ? (char)g_first[d + k] : '.';
							b[k] = (bytes[d + k] >= 32 && bytes[d + k] < 127) ? (char)bytes[d + k] : '.';
						}
						char pre[81] = {0};
						size_t back = d > 80 ? 80 : d;
						for (size_t k = 0; k < back; k++) {
							uint8_t c = g_first[d - back + k];
							pre[k] = (c >= 32 && c < 127) ? (char)c : '.';
						}
						printf("SR-DIFF context=[%s]\n", pre);
						printf("SR-DIFF at=%zu first=[%s]\n", d, a);
						printf("SR-DIFF at=%zu second=[%s]\n", d, b);
						break;
					}
				}
				g_failures++;
			}
		}
		if (SaveRoundtripProbe::overwritePromptOpen()) {
			printf("SR-SEAM FAIL confirmed closed=0 -- the overwrite prompt is still up\n");
			g_failures++;
		}
		else {
			printf("SR-SEAM confirmed changed=1 closed=1\n");
		}
		g_stage = Stage::Report;
		break;
	}
	case Stage::Report: {
		g_stage = Stage::Done;
		printf("SR-RESULT failures=%d\n", g_failures);
		fflush(stdout);
		g_summaryPrinted = true;
		break;
	}
	case Stage::Done:
		break;
	}
}

} // namespace

extern "C" {

void deluge_scenario_save_roundtrip_begin(void) {
	// Line-buffer stdout: piped into a script it is block-buffered by default, and the harness exits
	// without flushing C stdio, so every SR- line -- which IS this gate's output -- would be lost
	// while the run still reported success. Same reason audio_load_ab_scenario.cpp does it.
	setvbuf(stdout, nullptr, _IOLBF, 0);
	g_stage = Stage::Fixture;
	g_stepDone = false;
	g_summaryPrinted = false;
	g_failures = 0;
	g_crcFirst = 0;
	g_crcSecond = 0;
	g_bytesFirst = 0;
}

bool deluge_scenario_save_roundtrip_complete(void) {
	return g_summaryPrinted;
}

void deluge_scenario_save_roundtrip_dispatch_step(void) {
	if (g_summaryPrinted) {
		return;
	}
	g_stepDone = false;
	runTick();
	// A marker op behind whatever the tick queued: the owner queue is FIFO, so this runs after the
	// dispatched save (and immediately if nothing was dispatched). That is what makes each *Check
	// stage read a result the op has actually produced.
	if (!deluge::storage::Owner::run([](void*) { g_stepDone = true; }, nullptr)) {
		g_stepDone = true;
	}
}

bool deluge_scenario_save_roundtrip_step_done(void) {
	return g_stepDone;
}

int32_t deluge_scenario_save_roundtrip_failures(void) {
	return g_failures;
}

} // extern "C"

#endif // DELUGE_HOST
