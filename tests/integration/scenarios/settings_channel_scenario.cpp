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

#include "scenarios/settings_channel_scenario.h"

#include "gui/views/performance_view.h"
#include "io/file.hpp"
#include "io/midi/cable_types/usb_hosted.h"
#include "io/midi/midi_device_manager.h"
#include "io/midi/midi_follow.h"
#include "io/stall_bound.h"
#include "libdeluge/file_bytes.h"
#include "model/settings/runtime_feature_settings.h"
#include "storage/byte_channel.h"

#include <cstdio>
#include <cstring>
#include <span>
#include <string>
#include <vector>

// The GUI headers above reach `src/lib/printf.h`, which soft-aliases `printf` to the embedded
// `printf_`. That implementation emits through `putchar_`, which the Rust BSP builds do not define -- this
// harness links the Rust BSP and has no provider, so the alias turns every line this file writes
// into an undefined symbol at link time. Every one of those lines IS the gate's output.
#ifdef printf
#undef printf
#endif

/// The gate's view into state the settings consumers keep private, befriended by each consumer it reaches. Every
/// check still drives the consumer's real entry point; the probe only arranges and observes.
struct SettingsChannelProbe {
	static void addUnknownSetting(std::string name, uint32_t value) {
		runtimeFeatureSettings.unknownSettings.push_back(UnknownSetting{.name = std::move(name), .value = value});
	}
	static size_t unknownSettingCount() { return runtimeFeatureSettings.unknownSettings.size(); }
	static void truncateUnknownSettings(size_t count) { runtimeFeatureSettings.unknownSettings.resize(count); }
	static void forgetPerformanceDefaults() { performanceView.successfullyReadDefaultsFromFile = false; }
	static void readPerformanceDefaults() { performanceView.readDefaultsFromFile(); }
	static bool performanceDefaultsLoaded() { return performanceView.successfullyReadDefaultsFromFile; }
};

namespace {

/// The setting the round trip carries. `RuntimeFeatureSettingType` values are plain integers in the
/// file, so any of them proves the path; this one is chosen because it is a simple toggle with no
/// side effects on anything else in the scenario.
constexpr RuntimeFeatureSettingType kProbeSetting = RuntimeFeatureSettingType::AlternativePlaybackStartBehaviour;

/// A value the file cannot already hold by accident.
///
/// Nonzero on purpose: 0 is the default for every toggle, so a round trip that asserted 0 would pass
/// against a file that was never written at all.
constexpr uint32_t kProbeValue = 1;

/// The burst's three values. The first must differ from the third: a seam that let the in-flight write be
/// replaced would lose the second and third and leave the first on the card, and only differing values show it.
constexpr uint32_t kBurstFirst = 2;
constexpr uint32_t kBurstSecond = 4;
constexpr uint32_t kBurstThird = 3;
/// What the order stage writes.
constexpr uint32_t kOrderValue = 5;
/// What the burst's read-back scrubs the in-memory value to first, so a read that did nothing cannot pass.
constexpr uint32_t kScrubValue = 99;
/// A bound on every idle wait, so a channel that never settles ends the run with a report.
constexpr int32_t kMaxWaitTicks = 20000;
/// Unknown settings the oversize stage adds. Each serialises to roughly 55 bytes, so 600 is far past the channel's
/// 8 KiB bound; real settings files measure 66 to 1,957 bytes.
constexpr int32_t kOversizeEntries = 600;

/// The busy stage's hold: a copy of this many chunks, each waiting this long, keeps the channel busy with work that
/// progresses for kBusyChunks * kBusyChunkLatencyMs virtual milliseconds (5 s). At one scenario step per two virtual
/// milliseconds, and the app's own "settings io" task beside it, that is thousands of refused ticks for a write
/// queued behind it -- far past `deluge::io::kChannelStallTicks`, which the check requires rather than assumes.
constexpr uint32_t kBusyChunks = 100;
constexpr uint32_t kBusyChunkLatencyMs = 50;
/// The value the write held behind the copy carries. Differs from every value the earlier stages leave.
constexpr uint32_t kBusyValue = 6;
constexpr const char* kBusySource = "SCBUSY.BIN";
constexpr const char* kBusyCopy = "SCBUSYCP.BIN";

/// What every settings document must begin with. Spelled out here rather than taken from the code under test, so
/// a change that altered the constant and the check together could not pass.
constexpr const char* kXmlHead = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";

/// Every settings file the channel writes, one per consumer.
constexpr const char* kSettingsFiles[] = {
    "SETTINGS/CommunityFeatures.XML",
    "SETTINGS/MIDIFollow.XML",
    "SETTINGS/PerformanceView.XML",
    "SETTINGS/MIDIDevices.XML",
};

/// Every stage has a `*Check` successor that runs one tick later.
///
/// Not decoration. A settings operation is only ENQUEUED when the consumer's entry point runs -- it
/// executes on a later worker poll -- so reading the result in the same tick reads the state from
/// before the operation and reports success for an operation that has not happened. The same shape
/// the save round-trip and the browser gates use, and for the same reason.
enum class Stage : uint8_t {
	Write,
	WriteCheck,
	Read,
	ReadCheck,
	BurstFirst,
	BurstWait,
	BurstReadWait,
	BurstCheck,
	OrderSubmit,
	OrderWait,
	OrderCheck,
	HeadWrite,
	HeadWait,
	HeadCheck,
	UnlinkWrite,
	UnlinkWait,
	UnlinkCheck,
	PerfRead,
	PerfWait,
	PerfCheck,
	OversizeWrite,
	OversizeWait,
	OversizeCheck,
	BusyStart,
	BusyWait,
	BusyReadWait,
	BusyCheck,
	Fallback,
	Restore,
	RestoreWait,
	Tier,
	Summary,
	Done,
};

Stage g_stage = Stage::Write;
bool g_stepDone = false;
bool g_summaryPrinted = false;
int32_t g_failures = 0;
uint32_t g_wrote = 0;
uint32_t g_read = 0;
uint32_t g_original = 0;
int32_t g_waitTicks = 0;
int32_t g_servedBefore = 0;
int32_t g_illegalBefore = 0;
int32_t g_burstServed = 0;
int32_t g_burstIllegal = 0;
int32_t g_orderIllegal = 0;
int32_t g_orderServed = 0;
/// What the oversize stage found before it wrote: whether the community-features file was there, and its bytes.
bool g_oversizeHadFile = false;
std::vector<uint8_t> g_oversizeBefore;
size_t g_unknownBefore = 0;
int32_t g_tierBefore = 0;
/// The seam's refused-tick count when the busy stage submitted its write.
int32_t g_refusedBefore = 0;
/// How many ticks the busy stage's write was refused, taken once the seam went idle.
int32_t g_busyRefused = 0;
/// The DIN ports' clock setting as the scenario found it. The head stage changes it to make the MIDI device table
/// worth writing; the restore puts it back.
bool g_originalSendClock = true;
/// Upstream USB cable 2's MPE zones as the scenario found them, input then output port. That cable is constructed with
/// MPE on, which alone makes the device table worth writing; the unlink stage turns the zones off and the restore puts
/// them back.
uint8_t g_originalCable2Lower[2] = {0, 0};
uint8_t g_originalCable2Upper[2] = {15, 15};
/// The rest of upstream USB cable 2's worth-writing state as the scenario found it. It is constructed not receiving
/// clock, which also makes it worth writing.
bool g_originalCable2SendClock = true;
bool g_originalCable2ReceiveClock = true;
bool g_originalCable2Relative = false;
bool g_originalCable2Thru = false;
int32_t g_originalCable2Velocity = 0;

/// 1 present, 0 genuinely absent, -1 could not tell.
int32_t presence(const char* path) {
	auto opened = deluge::io::File::open(path, DELUGE_FILE_READ);
	if (opened.has_value()) {
		(void)opened->close();
		return 1;
	}
	return opened.error() == deluge::io::Status::NOT_FOUND ? 0 : -1;
}

/// Read `path` whole, synchronously. The scenario is the observer, not the subject, so it may block.
bool readWholeFile(const char* path, std::vector<uint8_t>& out) {
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

/// Advance to `next` once the seam is idle; report and move to the restore if it never is.
void waitIdleThen(const char* marker, Stage next, Stage onTimeout) {
	if (deluge::storage::bytes::idle()) {
		g_waitTicks = 0;
		g_stage = next;
		return;
	}
	if (++g_waitTicks > kMaxWaitTicks) {
		printf("%s FAIL the seam never went idle (%d ticks)\n", marker, g_waitTicks);
		g_failures++;
		g_waitTicks = 0;
		g_stage = onTimeout;
	}
}

void runTick() {
	switch (g_stage) {
	case Stage::Write:
		// Through the REAL consumer entry point, never the layer underneath it: this gate's whole
		// subject is which tier that entry point does its I/O on, so a gate that reached past it
		// would test a path the product never takes.
		g_wrote = kProbeValue;
		runtimeFeatureSettings.set(kProbeSetting, g_wrote);
		runtimeFeatureSettings.writeSettingsToFile();
		g_stage = Stage::WriteCheck;
		break;

	case Stage::WriteCheck:
		// Nothing to assert here yet -- the write's own outcome is what the read proves. This stage waits for
		// the write to COMPLETE, not for a tick to pass: the entry point only queues it, and how many ticks it
		// takes depends on everything else the harness runs. Spending exactly one tick passed only while
		// something else happened to slow the step down, and read the scrub below once nothing did.
		waitIdleThen("SC-ROUNDTRIP", Stage::Read, Stage::Restore);
		break;

	case Stage::Read:
		// Scrub the in-memory value first. Without this the read could do nothing at all and the
		// comparison below would still pass -- a round trip that compares a file against itself
		// passes vacuously.
		runtimeFeatureSettings.set(kProbeSetting, kProbeValue + 1);
		runtimeFeatureSettings.readSettingsFromFile();
		g_stage = Stage::ReadCheck;
		break;

	case Stage::ReadCheck:
		// Likewise the read: a value taken before its parse lands is the scrub above, not the file.
		if (!deluge::storage::bytes::idle()) {
			waitIdleThen("SC-ROUNDTRIP", Stage::ReadCheck, Stage::Restore);
			break;
		}
		g_read = runtimeFeatureSettings.get(kProbeSetting);
		printf("SC-ROUNDTRIP subsystem=runtime_features wrote=%u read=%u\n", g_wrote, g_read);
		if (g_read != g_wrote) {
			printf("SC-ROUNDTRIP FAIL the value did not survive the write/read pair\n");
			g_failures++;
		}
		g_stage = Stage::BurstFirst;
		break;

	case Stage::BurstFirst:
		// Start from an empty seam, so the burst's served count is the burst's alone.
		if (!deluge::storage::bytes::idle()) {
			waitIdleThen("SC-BURST", Stage::BurstFirst, Stage::Restore);
			break;
		}
		g_servedBefore = deluge::storage::bytes::channelServedOps();
		g_illegalBefore = deluge::storage::bytes::illegalInline();
		runtimeFeatureSettings.set(kProbeSetting, kBurstFirst);
		runtimeFeatureSettings.writeSettingsToFile();
		// Drive the seam once, HERE, so the first write is in flight before the other two are submitted. Not a
		// tick later: this scenario's tick is not the seam's only pump -- the app's own "settings io" task calls
		// serviceRoutine() too -- so by the next tick the first write may already have finished. One call starts a
		// request and returns without collecting it, and nothing else can run inside this stage.
		deluge::storage::bytes::serviceRoutine();
		// The second waits and the third replaces it: two card writes, not three -- and not one, which is what
		// replacing the in-flight write would give.
		runtimeFeatureSettings.set(kProbeSetting, kBurstSecond);
		runtimeFeatureSettings.writeSettingsToFile();
		runtimeFeatureSettings.set(kProbeSetting, kBurstThird);
		runtimeFeatureSettings.writeSettingsToFile();
		g_waitTicks = 0;
		g_stage = Stage::BurstWait;
		break;

	case Stage::BurstWait:
		if (!deluge::storage::bytes::idle()) {
			waitIdleThen("SC-BURST", Stage::BurstWait, Stage::Restore);
			break;
		}
		g_burstServed = deluge::storage::bytes::channelServedOps() - g_servedBefore;
		g_burstIllegal = deluge::storage::bytes::illegalInline() - g_illegalBefore;
		runtimeFeatureSettings.set(kProbeSetting, kScrubValue);
		runtimeFeatureSettings.readSettingsFromFile();
		g_stage = Stage::BurstReadWait;
		break;

	case Stage::BurstReadWait:
		waitIdleThen("SC-BURST", Stage::BurstCheck, Stage::Restore);
		break;

	case Stage::BurstCheck: {
		const uint32_t value = runtimeFeatureSettings.get(kProbeSetting);
		printf("SC-BURST value=%u served=%d illegal=%d\n", value, g_burstServed, g_burstIllegal);
		if (value != kBurstThird) {
			printf("SC-BURST FAIL the last write of the burst did not land (read %u, wanted %u)\n", value, kBurstThird);
			g_failures++;
		}
		// Exactly two card writes, all through the channel.
		if (g_burstServed != 2) {
			printf("SC-BURST FAIL the channel carried %d writes for a burst that should collapse to 2\n",
			       g_burstServed);
			g_failures++;
		}
		g_stage = Stage::OrderSubmit;
		break;
	}

	case Stage::OrderSubmit:
		// Write then read in ONE tick, so the read is submitted while the write is still waiting. No scrub of
		// the in-memory value in between.
		// A read that started ahead of the write would load the file's previous value over this one, which
		// is what the check below catches; the served count is what shows the read ran at all.
		g_illegalBefore = deluge::storage::bytes::illegalInline();
		g_servedBefore = deluge::storage::bytes::channelServedOps();
		runtimeFeatureSettings.set(kProbeSetting, kOrderValue);
		runtimeFeatureSettings.writeSettingsToFile();
		runtimeFeatureSettings.readSettingsFromFile();
		g_waitTicks = 0;
		g_stage = Stage::OrderWait;
		break;

	case Stage::OrderWait:
		waitIdleThen("SC-ORDER", Stage::OrderCheck, Stage::Restore);
		break;

	case Stage::OrderCheck: {
		const uint32_t value = runtimeFeatureSettings.get(kProbeSetting);
		g_orderIllegal = deluge::storage::bytes::illegalInline() - g_illegalBefore;
		g_orderServed = deluge::storage::bytes::channelServedOps() - g_servedBefore;
		printf("SC-ORDER value=%u served=%d\n", value, g_orderServed);
		if (value != kOrderValue) {
			printf("SC-ORDER FAIL a read queued behind a write did not see it (read %u, wanted %u)\n", value,
			       kOrderValue);
			g_failures++;
		}
		if (g_orderServed != 2) {
			printf("SC-ORDER FAIL the channel carried %d operations for one write and one read\n", g_orderServed);
			g_failures++;
		}
		g_stage = Stage::HeadWrite;
		break;
	}

	case Stage::HeadWrite:
		// Every settings file the channel carries, each through its consumer's real entry point. The MIDI device
		// table writes only when something in it is worth keeping, so give it one thing: DIN ports that do not
		// send clock.
		runtimeFeatureSettings.writeSettingsToFile();
		midiFollow.writeDefaultsToFile();
		performanceView.savePerformanceViewLayout();
		MIDIDeviceManager::dinMIDIPorts.sendClock = false;
		MIDIDeviceManager::anyChangesToSave = true;
		MIDIDeviceManager::writeDevicesToFile();
		g_waitTicks = 0;
		g_stage = Stage::HeadWait;
		break;

	case Stage::HeadWait:
		waitIdleThen("SC-HEAD", Stage::HeadCheck, Stage::Restore);
		break;

	case Stage::HeadCheck: {
		const size_t headLen = std::strlen(kXmlHead);
		int32_t missing = 0;
		for (const char* path : kSettingsFiles) {
			std::vector<uint8_t> bytes;
			const bool read = readWholeFile(path, bytes);
			const bool head = read && bytes.size() >= headLen && std::memcmp(bytes.data(), kXmlHead, headLen) == 0;
			printf("SC-HEAD file=%s read=%d head=%d\n", path, (int)read, (int)head);
			if (!head) {
				missing++;
			}
			// Every settings document must fit the channel: after boot, one that does not has no way onto the card.
			const size_t bound = DELUGE_FILE_BYTES_MAX_BYTES;
			printf("SC-FITS file=%s bytes=%zu bound=%zu\n", path, bytes.size(), bound);
			if (!read || bytes.size() > bound) {
				printf("SC-FITS FAIL %s does not fit the channel\n", path);
				g_failures++;
			}
		}
		if (missing != 0) {
			printf("SC-HEAD FAIL %d settings file(s) do not begin with the XML declaration\n", missing);
			g_failures++;
		}
		g_stage = Stage::UnlinkWrite;
		break;
	}

	case Stage::UnlinkWrite:
		// Back to nothing worth keeping, and a save: the table must remove its file -- through the channel, not
		// inline. `sendClock = true` is the default, and the head stage left nothing else worth writing.
		MIDIDeviceManager::dinMIDIPorts.sendClock = true;
		// Upstream USB cable 2 is constructed with MPE zones on, and a port with zones is worth writing -- so on a
		// default device the table is never empty. Turn them off, as a user clearing them would.
		for (auto& port : MIDIDeviceManager::upstreamUSBMIDICable2.ports) {
			port.mpeLowerZoneLastMemberChannel = 0;
			port.mpeUpperZoneLastMemberChannel = 15;
		}
		// It is also constructed not receiving clock. Everything else a cable can hold is cleared too, so the
		// precondition does not depend on what an earlier run left in the reused card image's settings.
		MIDIDeviceManager::upstreamUSBMIDICable2.sendClock = true;
		MIDIDeviceManager::upstreamUSBMIDICable2.receiveClock = true;
		MIDIDeviceManager::upstreamUSBMIDICable2.is_relative = false;
		MIDIDeviceManager::upstreamUSBMIDICable2.midi_thru = false;
		MIDIDeviceManager::upstreamUSBMIDICable2.defaultVelocityToLevel = 0;
		MIDIDeviceManager::anyChangesToSave = true;
		{
			// The stage's precondition, printed and required: a table that still holds something worth keeping is
			// WRITTEN, not removed, and the check below would then be testing the wrong branch.
			const bool din = MIDIDeviceManager::dinMIDIPorts.worthWritingToFile();
			const bool up1 = MIDIDeviceManager::upstreamUSBMIDICable1.worthWritingToFile();
			const bool up2 = MIDIDeviceManager::upstreamUSBMIDICable2.worthWritingToFile();
			size_t hostedWorth = 0;
			for (auto* device : MIDIDeviceManager::hostedMIDIDevices) {
				hostedWorth += device->worthWritingToFile() ? 1 : 0;
			}
			printf(
			    "SC-UNLINK-PRE din=%d up1=%d up2=%d hosted=%zu/%zu up2SendClock=%d up2ReceiveClock=%d up2Relative=%d "
			    "up2Thru=%d up2Velocity=%d\n",
			    (int)din, (int)up1, (int)up2, hostedWorth, MIDIDeviceManager::hostedMIDIDevices.size(),
			    (int)MIDIDeviceManager::upstreamUSBMIDICable2.sendClock,
			    (int)MIDIDeviceManager::upstreamUSBMIDICable2.receiveClock,
			    (int)MIDIDeviceManager::upstreamUSBMIDICable2.is_relative,
			    (int)MIDIDeviceManager::upstreamUSBMIDICable2.midi_thru,
			    (int)MIDIDeviceManager::upstreamUSBMIDICable2.defaultVelocityToLevel);
			if (din || up1 || up2 || hostedWorth != 0) {
				printf("SC-UNLINK FAIL precondition: the MIDI device table still holds something worth writing\n");
				g_failures++;
			}
		}
		g_servedBefore = deluge::storage::bytes::channelServedOps();
		g_illegalBefore = deluge::storage::bytes::illegalInline();
		MIDIDeviceManager::writeDevicesToFile();
		g_waitTicks = 0;
		g_stage = Stage::UnlinkWait;
		break;

	case Stage::UnlinkWait:
		waitIdleThen("SC-UNLINK", Stage::UnlinkCheck, Stage::Restore);
		break;

	case Stage::UnlinkCheck: {
		const int32_t there = presence("SETTINGS/MIDIDevices.XML");
		const int32_t served = deluge::storage::bytes::channelServedOps() - g_servedBefore;
		const int32_t illegal = deluge::storage::bytes::illegalInline() - g_illegalBefore;
		printf("SC-UNLINK file=%d served=%d illegal=%d\n", there, served, illegal);
		if (there != 0) {
			printf(
			    "SC-UNLINK FAIL the MIDI device file is still there (presence %d) after a save with nothing to keep\n",
			    there);
			g_failures++;
		}
		if (served < 1 || illegal != 0) {
			printf("SC-UNLINK FAIL the removal did not go through the channel (served %d, refused inline %d)\n", served,
			       illegal);
			g_failures++;
		}
		g_stage = Stage::PerfRead;
		break;
	}

	case Stage::PerfRead:
		// The view reads its defaults when it is opened: after boot. Forget the read that already
		// happened and ask again through the entry point `focusRegained()` uses.
		SettingsChannelProbe::forgetPerformanceDefaults();
		g_servedBefore = deluge::storage::bytes::channelServedOps();
		g_illegalBefore = deluge::storage::bytes::illegalInline();
		SettingsChannelProbe::readPerformanceDefaults();
		g_waitTicks = 0;
		g_stage = Stage::PerfWait;
		break;

	case Stage::PerfWait:
		waitIdleThen("SC-PERF", Stage::PerfCheck, Stage::Restore);
		break;

	case Stage::PerfCheck: {
		const bool loaded = SettingsChannelProbe::performanceDefaultsLoaded();
		const int32_t served = deluge::storage::bytes::channelServedOps() - g_servedBefore;
		const int32_t illegal = deluge::storage::bytes::illegalInline() - g_illegalBefore;
		printf("SC-PERF loaded=%d served=%d illegal=%d\n", (int)loaded, served, illegal);
		if (!loaded) {
			printf("SC-PERF FAIL the performance defaults were not loaded\n");
			g_failures++;
		}
		if (served < 1 || illegal != 0) {
			printf("SC-PERF FAIL the read did not go through the channel (served %d, refused inline %d)\n", served,
			       illegal);
			g_failures++;
		}
		g_stage = Stage::OversizeWrite;
		break;
	}

	case Stage::OversizeWrite:
		// A settings document over the channel's bound, through the real entry point. After boot it has no legal
		// place to be written, so it must be refused: counted, not written, not dispatched.
		g_oversizeHadFile = readWholeFile("SETTINGS/CommunityFeatures.XML", g_oversizeBefore);
		g_unknownBefore = SettingsChannelProbe::unknownSettingCount();
		for (int32_t i = 0; i < kOversizeEntries; i++) {
			SettingsChannelProbe::addUnknownSetting("scOversizeProbe" + std::to_string(i), static_cast<uint32_t>(i));
		}
		g_illegalBefore = deluge::storage::bytes::illegalInline();
		g_servedBefore = deluge::storage::bytes::channelServedOps();
		g_tierBefore = deluge::storage::bytes::interactionTierIo(deluge::storage::bytes::IoTier::Settings);
		runtimeFeatureSettings.writeSettingsToFile();
		g_waitTicks = 0;
		g_stage = Stage::OversizeWait;
		break;

	case Stage::OversizeWait:
		waitIdleThen("SC-OVERSIZE", Stage::OversizeCheck, Stage::OversizeCheck);
		break;

	case Stage::OversizeCheck: {
		std::vector<uint8_t> after;
		const bool hasFile = readWholeFile("SETTINGS/CommunityFeatures.XML", after);
		// The entries come out only now. An inline arm serialises when it RUNS, so taking them out in the submitting
		// tick would let a dispatched write put an ordinary document on the card and pass this check.
		SettingsChannelProbe::truncateUnknownSettings(g_unknownBefore);
		const bool unchanged = hasFile == g_oversizeHadFile && after == g_oversizeBefore;
		const int32_t illegal = deluge::storage::bytes::illegalInline() - g_illegalBefore;
		const int32_t served = deluge::storage::bytes::channelServedOps() - g_servedBefore;
		const int32_t tier =
		    deluge::storage::bytes::interactionTierIo(deluge::storage::bytes::IoTier::Settings) - g_tierBefore;
		printf("SC-OVERSIZE illegal=%d served=%d tier=%d unchanged=%d\n", illegal, served, tier, (int)unchanged);
		if (illegal != 1 || served != 0 || tier != 0 || !unchanged) {
			printf("SC-OVERSIZE FAIL an oversize settings write was not refused cleanly\n");
			g_failures++;
		}
		g_stage = Stage::BusyStart;
		break;
	}

	case Stage::BusyStart: {
		// A settings write queued behind a long operation that keeps making progress -- a copy of a large file, as a
		// SysEx copy or a song save's sample collection runs -- must wait for it and land, not give up on a channel
		// that is merely busy.
		if (!deluge::storage::bytes::idle()) {
			waitIdleThen("SC-BUSY", Stage::BusyStart, Stage::Fallback);
			break;
		}
		std::vector<std::byte> chunk(DELUGE_FILE_BYTES_BLOCK, std::byte{0x5A});
		auto source = deluge::io::File::open(kBusySource, DELUGE_FILE_WRITE_CREATE);
		bool staged = source.has_value();
		for (uint32_t i = 0; staged && i < kBusyChunks; i++) {
			auto put = source->write(chunk);
			staged = put.has_value() && *put == chunk.size();
		}
		if (source.has_value()) {
			(void)source->close();
		}
		deluge_file_bytes_set_chunk_latency_ms(kBusyChunkLatencyMs);
		if (!staged || !deluge_file_bytes_request_copy(kBusySource, kBusyCopy)) {
			printf("SC-BUSY FAIL could not stage the copy that holds the channel (source %d)\n", (int)staged);
			g_failures++;
			deluge_file_bytes_set_chunk_latency_ms(0);
			g_stage = Stage::Fallback;
			break;
		}
		g_refusedBefore = deluge::storage::bytes::refusedTicks();
		runtimeFeatureSettings.set(kProbeSetting, kBusyValue);
		runtimeFeatureSettings.writeSettingsToFile();
		g_waitTicks = 0;
		g_stage = Stage::BusyWait;
		break;
	}

	case Stage::BusyWait:
		if (!deluge::storage::bytes::idle()) {
			waitIdleThen("SC-BUSY", Stage::BusyWait, Stage::BusyCheck);
			break;
		}
		deluge_file_bytes_set_chunk_latency_ms(0);
		g_busyRefused = deluge::storage::bytes::refusedTicks() - g_refusedBefore;
		runtimeFeatureSettings.set(kProbeSetting, kScrubValue);
		runtimeFeatureSettings.readSettingsFromFile();
		g_stage = Stage::BusyReadWait;
		break;

	case Stage::BusyReadWait:
		waitIdleThen("SC-BUSY", Stage::BusyCheck, Stage::BusyCheck);
		break;

	case Stage::BusyCheck: {
		deluge_file_bytes_set_chunk_latency_ms(0);
		const uint32_t value = runtimeFeatureSettings.get(kProbeSetting);
		std::vector<uint8_t> copied;
		const bool copyThere = readWholeFile(kBusyCopy, copied);
		printf("SC-BUSY value=%u refused=%d bound=%d copyBytes=%zu\n", value, g_busyRefused,
		       deluge::io::kChannelStallTicks, copyThere ? copied.size() : size_t{0});
		// The value first: a write that gave up was refused on at most the stall bound's ticks, so the precondition
		// below cannot tell a failed write from a hold too short to prove anything.
		if (value != kBusyValue) {
			printf("SC-BUSY FAIL a settings write held behind a long copy did not land (read %u, wanted %u)\n", value,
			       kBusyValue);
			g_failures++;
		}
		else if (g_busyRefused <= deluge::io::kChannelStallTicks) {
			printf("SC-BUSY FAIL the write was refused on only %d ticks, within the stall bound; nothing was held past "
			       "it, so this proves nothing\n",
			       g_busyRefused);
			g_failures++;
		}
		if (!copyThere || copied.size() != size_t{kBusyChunks} * DELUGE_FILE_BYTES_BLOCK) {
			printf("SC-BUSY FAIL the copy that held the channel did not complete\n");
			g_failures++;
		}
		(void)deluge::io::unlink(kBusySource);
		(void)deluge::io::unlink(kBusyCopy);
		g_stage = Stage::Fallback;
		break;
	}

	case Stage::Fallback: {
		printf("SC-FALLBACK burst=%d order=%d\n", g_burstIllegal, g_orderIllegal);
		// A busy channel queues: nothing the burst or the ordered write-then-read submitted was refused.
		if (g_burstIllegal != 0 || g_orderIllegal != 0) {
			printf("SC-FALLBACK FAIL a settings operation was refused instead of queued behind a busy channel\n");
			g_failures++;
		}
		g_stage = Stage::Restore;
		break;
	}

	case Stage::Restore:
		// The card image is shared with other gates that read this file at boot: leave the setting as it was.
		runtimeFeatureSettings.set(kProbeSetting, g_original);
		runtimeFeatureSettings.writeSettingsToFile();
		MIDIDeviceManager::dinMIDIPorts.sendClock = g_originalSendClock;
		for (size_t i = 0; i < 2; i++) {
			MIDIDeviceManager::upstreamUSBMIDICable2.ports[i].mpeLowerZoneLastMemberChannel = g_originalCable2Lower[i];
			MIDIDeviceManager::upstreamUSBMIDICable2.ports[i].mpeUpperZoneLastMemberChannel = g_originalCable2Upper[i];
		}
		MIDIDeviceManager::upstreamUSBMIDICable2.sendClock = g_originalCable2SendClock;
		MIDIDeviceManager::upstreamUSBMIDICable2.receiveClock = g_originalCable2ReceiveClock;
		MIDIDeviceManager::upstreamUSBMIDICable2.is_relative = g_originalCable2Relative;
		MIDIDeviceManager::upstreamUSBMIDICable2.midi_thru = g_originalCable2Thru;
		MIDIDeviceManager::upstreamUSBMIDICable2.defaultVelocityToLevel = g_originalCable2Velocity;
		g_waitTicks = 0;
		g_stage = Stage::RestoreWait;
		break;

	case Stage::RestoreWait:
		waitIdleThen("SC-RESTORE", Stage::Tier, Stage::Tier);
		break;

	case Stage::Tier: {
		// Reported, never asserted here: `tests/integration/checks/settings_channel.py` is what requires this to be
		// non-zero -- see there.
		printf("SC-CHANNEL served=%d\n", deluge::storage::bytes::channelServedOps());
		const int32_t violations = deluge::storage::bytes::interactionTierIo(deluge::storage::bytes::IoTier::Settings);
		printf("SC-TIER violations=%d\n", violations);
		if (violations != 0) {
			// Settings I/O on the interaction tier is the defect this gate exists to catch.
			printf("SC-TIER FAIL settings I/O ran on the interaction tier\n");
			g_failures++;
		}
		g_stage = Stage::Summary;
		break;
	}

	case Stage::Summary:
		printf("SC-RESULT failures=%d\n", g_failures);
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

void deluge_scenario_settings_channel_begin(void) {
	// Line-buffer stdout: piped into a script it is block-buffered by default. See the flush above.
	setvbuf(stdout, nullptr, _IOLBF, 0);
	g_stage = Stage::Write;
	g_stepDone = false;
	g_summaryPrinted = false;
	g_failures = 0;
	g_wrote = 0;
	g_read = 0;
	g_original = runtimeFeatureSettings.get(kProbeSetting);
	g_originalSendClock = MIDIDeviceManager::dinMIDIPorts.sendClock;
	for (size_t i = 0; i < 2; i++) {
		g_originalCable2Lower[i] = MIDIDeviceManager::upstreamUSBMIDICable2.ports[i].mpeLowerZoneLastMemberChannel;
		g_originalCable2Upper[i] = MIDIDeviceManager::upstreamUSBMIDICable2.ports[i].mpeUpperZoneLastMemberChannel;
	}
	g_originalCable2SendClock = MIDIDeviceManager::upstreamUSBMIDICable2.sendClock;
	g_originalCable2ReceiveClock = MIDIDeviceManager::upstreamUSBMIDICable2.receiveClock;
	g_originalCable2Relative = MIDIDeviceManager::upstreamUSBMIDICable2.is_relative;
	g_originalCable2Thru = MIDIDeviceManager::upstreamUSBMIDICable2.midi_thru;
	g_originalCable2Velocity = MIDIDeviceManager::upstreamUSBMIDICable2.defaultVelocityToLevel;
	g_waitTicks = 0;
	g_burstServed = 0;
	g_burstIllegal = 0;
	g_orderIllegal = 0;
	g_refusedBefore = 0;
	g_busyRefused = 0;
}

bool deluge_scenario_settings_channel_complete(void) {
	return g_summaryPrinted;
}

void deluge_scenario_settings_channel_dispatch_step(void) {
	if (g_summaryPrinted) {
		return;
	}
	g_stepDone = false;
	// The channel's pump. In the firmware this is a repeating task registered in `deluge.cpp`; here
	// the scenario owns the tick, so without this call the channel arm would be requested and never
	// collected -- and a dead channel would pass.
	deluge::storage::bytes::serviceRoutine();
	runTick();
	// Nothing this tick queued runs anywhere else: the step is done when the tick returns.
	g_stepDone = true;
}

bool deluge_scenario_settings_channel_step_done(void) {
	return g_stepDone;
}

int32_t deluge_scenario_settings_channel_failures(void) {
	return g_failures;
}

} // extern "C"
