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

#include "harness/path_operations_scenario.h"

#include "gui/ui/browser/browser.h"
#include "gui/ui/save/save_song_ui.h"
#include "io/file.hpp"
#include "io/midi/midi_device.h"
#include "storage/byte_channel.h"
#include "storage/owner.h"
#include "storage/smsysex.h"
#include "util/string.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

// The headers above reach `src/lib/printf.h`, which soft-aliases `printf` to the embedded `printf_`.
// That implementation emits through `putchar_`, which only the C host BSP defines -- this harness
// links the Rust BSP and has no provider, so the alias turns every line this file writes into an
// undefined symbol at link time. Every one of those lines IS the gate's output.
#ifdef printf
#undef printf
#endif

/// Reaches `Browser`'s protected folder read. Declared a friend by `Browser`.
class PathOperationsBrowserProbe {
public:
	/// Read `missing`, falling back to `fallback`, exactly as a listing entered on a missing folder does.
	///
	/// `saveSongUI` only because the method needs a `Browser` and this one already exists; the read is the same
	/// whichever browser makes it.
	static Error readWithFallback(const char* missing, const char* fallback) {
		Browser::currentDir = missing;
		Browser::allowedFileExtensions = allowedFileExtensionsXML;
		return saveSongUI.readFileItemsFromFolderAndMemory(nullptr, OutputType::NONE, "SONG", nullptr, fallback, true);
	}
	static std::string currentDir() { return Browser::currentDir; }
};

namespace {

/// A `MIDICable` that keeps what was sent to it instead of putting it on a wire.
///
/// `sendSysex` is pure virtual, which is what makes this possible at all. The other five pure
/// virtuals get minimal bodies -- except `sendBufferSpace`, whose return value matters: a zero would
/// have the SysEx path decline to send and the gate would read "no reply" for a reply that was never
/// attempted.
class LoopbackCable final : public MIDICable {
public:
	void writeToFlash(uint8_t* /*memory*/) override {}
	char const* getDisplayName() override { return "PO-LOOPBACK"; }
	void sendMessage(MIDIMessage /*message*/) override {}

	void sendSysex(const uint8_t* data, int32_t len) override {
		lastReply.assign(reinterpret_cast<const char*>(data), static_cast<size_t>(len));
		replies++;
	}

	/// Nonzero deliberately -- see the class doc.
	[[nodiscard]] size_t sendBufferSpace() const override { return 1024; }
	void writeReferenceAttributesToFile(Serializer& /*writer*/) override {}

	std::string lastReply;
	int32_t replies = 0;
};

LoopbackCable g_cable;

/// The four verbs this gate drives, with the request each sends and the reply tag it must get back.
///
/// Real requests through `smSysex::sysexReceived`, not calls to the handlers underneath: the subject
/// is whether the PROTOCOL still answers, and a gate that reached past the dispatcher would keep
/// testing something else.
struct VerbCase {
	const char* name;
	const char* json;
	const char* replyTag;
	/// Whether the reply's `err` must be zero. **This is what makes the reply's STATUS testable**, not
	/// merely its arrival: a handler that replied before collecting would report success it has not
	/// obtained, which only a case expected to FAIL can distinguish.
	bool expectOk;
};

constexpr VerbCase kVerbs[] = {
    {"delete", R"({"delete":{"path":"PO_TEST/gone.txt"}})", "^delete", true},
    {"mkdir", R"({"mkdir":{"path":"PO_TEST/MADE","date":0,"time":0}})", "^mkdir", true},
    {"rename", R"({"rename":{"from":"PO_TEST/a.txt","to":"PO_TEST/b.txt"}})", "^rename", true},
    {"utime", R"({"utime":{"path":"PO_TEST/MADE","date":0,"time":0}})", "^utime", false},
    // Deliberately doomed: the path is not there. Its reply must carry a NON-zero err, which a reply
    // sent before its operation completed cannot do -- that one reports the success it assumed.
    {"delete-missing", R"({"delete":{"path":"PO_TEST/never-existed.txt"}})", "^delete", false},
};
constexpr int32_t kVerbCount = static_cast<int32_t>(sizeof(kVerbs) / sizeof(kVerbs[0]));

/// How many ticks one verb gets to produce its reply before the gate calls it wedged.
///
/// Comfortably above the handler's own worst case once it is resumable and its channel arm has a
/// stall bound. A cap below that would report a legitimate slow fallback as a wedge.
constexpr int32_t kMaxVerbTicks = 5000;

/// The nested path the mkdir_all stage creates. None of its three segments exists at the start, which
/// is what makes it a test of the WALK rather than of one mkdir.
constexpr const char* kNestedPath = "PO_TEST/A/B";

/// How long the nested creation gets to land before the gate calls it missing. Above the seam's own
/// stall bound, so a fallback that does eventually happen is not reported as a failure to happen.
constexpr int32_t kMaxNestedTicks = 900;

/// The on-owner missing-folder fallback: a read on the storage owner entered on a folder that is not there, whose
/// default folder is not there either and so has to be created. Every shipped listing with a default folder runs off
/// the owner, so nothing else reaches this arm -- and with the channel switched on it once skipped the creation,
/// because the synchronous open it makes ignores the channel's create flag.
constexpr const char* kOwnerMissingDir = "PO_TEST/NOSUCH";
constexpr const char* kOwnerDefaultDir = "PO_TEST/OWNDEF";

enum class Stage : uint8_t {
	Fixture,
	FixtureCheck,
	Verb,
	VerbCheck,
	Nested,
	NestedCheck,
	OwnerFallback,
	OwnerFallbackCheck,
	Report,
	Summary,
	Done,
};

Stage g_stage = Stage::Fixture;
bool g_stepDone = false;
bool g_summaryPrinted = false;
int32_t g_failures = 0;
int32_t g_verbIndex = 0;
int32_t g_verbTicks = 0;
int32_t g_repliesBefore = 0;
int32_t g_nestedTicks = 0;
int32_t g_nestedCreated = -1;
int32_t g_nestedIdempotent = -1;
int32_t g_ownerFallbackTicks = 0;
bool g_ownerFallbackRan = false;
bool g_ownerFallbackOnOwner = false;
Error g_ownerFallbackError = Error::NONE;
std::string g_ownerFallbackDir;

/// Pull the `err` attribute out of a captured reply, or -1 if it has none.
///
/// The reply is JSON with an `"err":N` member. Scanned rather than parsed because the scenario needs
/// one number out of it and a deserializer here would be more machinery than the check is worth.
int32_t replyErr(const std::string& reply) {
	const size_t at = reply.find("\"err\"");
	if (at == std::string::npos) {
		return -1;
	}
	const size_t colon = reply.find(':', at);
	if (colon == std::string::npos) {
		return -1;
	}
	return static_cast<int32_t>(strtol(reply.c_str() + colon + 1, nullptr, 10));
}

/// Whether `path` names a directory that is there.
bool directoryExists(const char* path) {
	auto dir = deluge::io::Directory::open(path);
	return dir.has_value();
}

/// Feed one verb's request in as a real SysEx message.
///
/// `data[1]` is the message sequence number the reply is addressed with, and the JSON starts at
/// `data[2]` -- see `smSysex::sysexReceived` and `processFrontSysEx`.
void sendVerb(const VerbCase& verb) {
	uint8_t buffer[512];
	const size_t jsonLen = strlen(verb.json);
	buffer[0] = 0;
	buffer[1] = static_cast<uint8_t>(1 + g_verbIndex); // a distinct sequence number per verb
	memcpy(buffer + 2, verb.json, jsonLen);
	smSysex::sysexReceived(g_cable, buffer, static_cast<int32_t>(jsonLen + 2));
}

void runTick() {
	switch (g_stage) {
	case Stage::Fixture: {
		// The delete and rename verbs need something to act on, and the mkdir_all stage needs its
		// nested path absent. Built with the synchronous API deliberately: this is the gate's own
		// setup, not the code under test.
		(void)deluge::io::mkdir("PO_TEST");
		auto seed = deluge::io::File::open("PO_TEST/gone.txt", DELUGE_FILE_WRITE_CREATE);
		if (seed.has_value()) {
			(void)seed->close();
		}
		auto seed2 = deluge::io::File::open("PO_TEST/a.txt", DELUGE_FILE_WRITE_CREATE);
		if (seed2.has_value()) {
			(void)seed2->close();
		}
		(void)deluge::io::unlink("PO_TEST/b.txt");
		(void)deluge::io::unlink(kNestedPath);
		(void)deluge::io::unlink("PO_TEST/A");
		(void)deluge::io::unlink(kOwnerDefaultDir);
		g_stage = Stage::FixtureCheck;
		break;
	}

	case Stage::FixtureCheck:
		g_stage = Stage::Verb;
		break;

	case Stage::Verb:
		g_repliesBefore = g_cable.replies;
		g_verbTicks = 0;
		sendVerb(kVerbs[g_verbIndex]);
		g_stage = Stage::VerbCheck;
		break;

	case Stage::VerbCheck: {
		// The handler becomes resumable, so one tick is not enough: wait for the reply, bounded so a
		// wedged handler fails the gate rather than hanging it.
		if (g_cable.replies == g_repliesBefore && ++g_verbTicks < kMaxVerbTicks) {
			break;
		}
		const VerbCase& verb = kVerbs[g_verbIndex];
		const bool replied = g_cable.replies > g_repliesBefore;
		const bool tagged = replied && g_cable.lastReply.find(verb.replyTag) != std::string::npos;
		const int32_t err = replied ? replyErr(g_cable.lastReply) : -1;
		const bool okAsExpected = verb.expectOk ? (err == 0) : (err > 0);
		printf("PO-SYSEX verb=%s replied=%d tagged=%d err=%d\n", verb.name, replied ? 1 : 0, tagged ? 1 : 0, err);
		if (!replied) {
			printf("PO-SYSEX FAIL %s produced no reply within %d ticks\n", verb.name, kMaxVerbTicks);
			g_failures++;
		}
		else if (!tagged) {
			printf("PO-SYSEX FAIL %s replied without its %s tag\n", verb.name, verb.replyTag);
			g_failures++;
		}
		else if (!okAsExpected) {
			// The status the reply CARRIES, not merely that one arrived. A handler that replied before
			// its operation finished reports a status it never obtained, and this is what sees it.
			printf("PO-SYSEX FAIL %s err=%d, expected %s\n", verb.name, err, verb.expectOk ? "zero" : "non-zero");
			g_failures++;
		}
		g_verbIndex++;
		g_stage = (g_verbIndex < kVerbCount) ? Stage::Verb : Stage::Nested;
		break;
	}

	case Stage::Nested:
		// Through the REAL entry point. Its three segments are all absent, so this exercises the walk.
		// `saveSongUI` is used only because the method needs a `Browser` instance and this one already
		// exists -- the folder creation is the same code whichever browser calls it.
		(void)saveSongUI.createFoldersRecursiveIfNotExists(kNestedPath);
		g_stage = Stage::NestedCheck;
		break;

	case Stage::NestedCheck:
		// Wait for it, bounded. The creation is deferred on both arms, and on a channel that accepts
		// and never answers the seam falls back to the owner only after its stall bound -- so checking
		// one tick later would report "not created" for a folder that does arrive. Letting it land
		// makes a dead channel fail on PO-CHANNEL alone, which is the precise signal, rather than on
		// this stage as a side effect of timing.
		if (!directoryExists(kNestedPath) && ++g_nestedTicks < kMaxNestedTicks) {
			break;
		}
		g_nestedCreated = directoryExists(kNestedPath) ? 1 : 0;
		// Called a second time on a path that now exists: "make sure this is there" must be success,
		// not EXISTS. Every caller of this relies on that, since the folders usually already exist.
		(void)saveSongUI.createFoldersRecursiveIfNotExists(kNestedPath);
		g_nestedIdempotent = directoryExists(kNestedPath) ? 1 : 0;
		g_stage = Stage::OwnerFallback;
		break;

	case Stage::OwnerFallback:
		if (directoryExists(kOwnerDefaultDir)) {
			printf("PO-OWNER-FALLBACK FAIL fixture: %s still exists, so nothing would have to be created\n",
			       kOwnerDefaultDir);
			g_failures++;
			g_stage = Stage::Report;
			break;
		}
		g_ownerFallbackRan = false;
		g_ownerFallbackTicks = 0;
		if (!deluge::storage::Owner::run(
		        [](void*) {
			        g_ownerFallbackOnOwner = deluge::storage::Owner::on_owner();
			        g_ownerFallbackError =
			            PathOperationsBrowserProbe::readWithFallback(kOwnerMissingDir, kOwnerDefaultDir);
			        g_ownerFallbackDir = PathOperationsBrowserProbe::currentDir();
			        g_ownerFallbackRan = true;
		        },
		        nullptr)) {
			printf("PO-OWNER-FALLBACK FAIL the owner queue refused the read\n");
			g_failures++;
			g_stage = Stage::Report;
			break;
		}
		g_stage = Stage::OwnerFallbackCheck;
		break;

	case Stage::OwnerFallbackCheck: {
		// The marker op behind the dispatched read makes one tick enough; bounded anyway, like every wait here.
		if (!g_ownerFallbackRan && ++g_ownerFallbackTicks < kMaxNestedTicks) {
			break;
		}
		const bool created = directoryExists(kOwnerDefaultDir);
		printf("PO-OWNER-FALLBACK ran=%d onOwner=%d err=%d dir=%s created=%d\n", g_ownerFallbackRan ? 1 : 0,
		       g_ownerFallbackOnOwner ? 1 : 0, static_cast<int>(g_ownerFallbackError), g_ownerFallbackDir.c_str(),
		       created ? 1 : 0);
		if (!g_ownerFallbackRan || !g_ownerFallbackOnOwner) {
			printf("PO-OWNER-FALLBACK FAIL the read did not run on the storage owner\n");
			g_failures++;
		}
		else if (g_ownerFallbackError != Error::NONE || !created
		         || !deluge::string::caselessEquals(g_ownerFallbackDir, kOwnerDefaultDir)) {
			printf("PO-OWNER-FALLBACK FAIL a read on the owner, entered on a missing folder, did not create and list "
			       "its default folder\n");
			g_failures++;
		}
		g_stage = Stage::Report;
		break;
	}

	case Stage::Report: {
		printf("PO-MKDIRALL created=%d idempotent=%d\n", g_nestedCreated, g_nestedIdempotent);
		if (g_nestedCreated != 1) {
			printf("PO-MKDIRALL FAIL the nested path was not created\n");
			g_failures++;
		}
		if (g_nestedIdempotent != 1) {
			printf("PO-MKDIRALL FAIL a second creation of an existing path did not succeed\n");
			g_failures++;
		}

		// Reported, never asserted here. This gate runs in both configurations, and with the channel
		// compiled out zero is the honest answer; failing on it would make the default build fail for
		// doing exactly what it is supposed to do. The channel-on leg of `path_operations.sh` is what
		// requires this to be non-zero.
		printf("PO-CHANNEL served=%d\n", deluge::storage::bytes::channelServedOps());

		const int32_t violations = deluge::storage::bytes::interactionTierIo(deluge::storage::bytes::IoTier::Browser);
		printf("PO-TIER violations=%d\n", violations);
		if (violations != 0) {
			// Before the conversion this is the DEFECT the rung exists to remove, and the gate is
			// expected to fail on it. After the conversion a non-zero count is a regression.
			printf("PO-TIER FAIL browser folder creation ran on the interaction tier\n");
			g_failures++;
		}

		const int32_t illegal = deluge::storage::bytes::illegalInline();
		printf("PO-ILLEGAL illegal=%d\n", illegal);
		if (illegal != 0) {
			printf("PO-ILLEGAL FAIL a folder creation was refused for want of a legal place to run\n");
			g_failures++;
		}
		g_stage = Stage::Summary;
		break;
	}

	case Stage::Summary:
		printf("PO-RESULT failures=%d\n", g_failures);
		// Piped into a script stdout is block-buffered and the harness exits without flushing C
		// stdio, so without this the entire gate output vanishes while the run reports success.
		fflush(stdout);
		g_summaryPrinted = true;
		g_stage = Stage::Done;
		break;

	case Stage::Done:
		break;
	}
}

} // namespace

extern "C" {

void deluge_scenario_path_operations_begin(void) {
	// Line-buffer stdout: piped into a script it is block-buffered by default. See the flush above.
	setvbuf(stdout, nullptr, _IOLBF, 0);
	g_stage = Stage::Fixture;
	g_stepDone = false;
	g_summaryPrinted = false;
	g_failures = 0;
	g_verbIndex = 0;
	g_verbTicks = 0;
	g_repliesBefore = 0;
	g_nestedTicks = 0;
	g_nestedCreated = -1;
	g_nestedIdempotent = -1;
	g_cable.replies = 0;
	g_cable.lastReply.clear();
}

bool deluge_scenario_path_operations_complete(void) {
	return g_summaryPrinted;
}

void deluge_scenario_path_operations_dispatch_step(void) {
	if (g_summaryPrinted) {
		return;
	}
	g_stepDone = false;
	// The SysEx pump. In the firmware this is a repeating task registered in `deluge.cpp`; here the
	// scenario owns the tick, so without this call a queued request would never be processed.
	smSysex::handleNextSysEx();
	runTick();
	// A marker op behind whatever the tick queued: the owner queue is FIFO, so this runs after the
	// dispatched operation (and immediately if nothing was dispatched). That is what makes each
	// *Check stage read a result the operation has actually produced.
	if (!deluge::storage::Owner::run([](void*) { g_stepDone = true; }, nullptr)) {
		g_stepDone = true;
	}
}

bool deluge_scenario_path_operations_step_done(void) {
	return g_stepDone;
}

int32_t deluge_scenario_path_operations_failures(void) {
	return g_failures;
}

} // extern "C"
