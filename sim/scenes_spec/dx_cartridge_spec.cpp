// The DX7 cartridge both menus read: the `.syx` read through the file prefix channel, the bank's names as the ported
// cartridge page reads them, and a program loaded through the app's sink as legacy's cartridge page loads it.
//
// The spec may include `gui/`; the library may not (`scripts/check_layer_rules.py`).

#include "gui/ui/menu_view_of.h"
#include "scenes/bind/runtime_facts.h"
#include "scenes/bind/write_sink.h"
#include "scenes/menu/bind_host.h"
#include "scenes/menu/context.h"

#include "gui/l10n/l10n.h"
#include "gui/l10n/strings.h"
#include "gui/menu_item/dx/cartridge.h"
#include "gui/ui/browser/dx_cartridge_read.h"
#include "hid/display/display.h"
#include "model/project_bridge/source_bridge.h"
#include "model/song/song.h"
#include "processing/sound/sound.h"
#include "processing/source.h"
#include "storage/DX7Cartridge.h"

#include "support/song_fixture.h"

#include "cppspec.hpp"

#include "spec/support/spec_main.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <deque>
#include <optional>
#include <string>
#include <vector>

using namespace deluge::scenes;
using deluge::gui::ui::DxCartridgeRead;
namespace legacy = deluge::gui::menu_item;

namespace {

/// @brief A 32-voice bulk dump whose program @c i is named "PROG <i>" and whose voice bytes differ per program, its
/// checksum right.
std::array<std::byte, kSysexSize> make_bank() {
	std::array<std::byte, kSysexSize> bank{};
	const uint8_t header[] = {0xF0, 0x43, 0x00, 0x09, 0x20, 0x00};
	for (size_t i = 0; i < sizeof header; i++) {
		bank[i] = std::byte{header[i]};
	}
	for (int32_t voice = 0; voice < 32; voice++) {
		std::byte* v = &bank[6 + voice * 128];
		for (int32_t i = 0; i < 118; i++) {
			v[i] = std::byte{static_cast<uint8_t>((voice * 7 + i) % 32)};
		}
		std::string name = "PROG " + std::to_string(voice);
		name.resize(10, ' ');
		for (int32_t i = 0; i < 10; i++) {
			v[118 + i] = std::byte{static_cast<uint8_t>(name[i])};
		}
	}
	bank[kSysexSize - 2] = sysexChecksum({&bank[6], 4096});
	bank[kSysexSize - 1] = std::byte{0xF7};
	return bank;
}

/// @brief Parse a fresh bank into legacy's cartridge item, as a finished read does.
void load_bank() {
	std::array<std::byte, kSysexSize> bank = make_bank();
	(void)legacy::dxCartridge.tryLoadFromBuffer(bank);
}

/// @brief A `Display` that keeps the popups legacy shows: `display` is null in a spec process.
struct PopupDisplay final : deluge::hid::Display {
	std::vector<std::string> popups;
	constexpr size_t getNumBrowserAndMenuLines() override { return 3; }
	void displayPopup(char const* text, int8_t, bool, uint8_t, int32_t, PopupType) override {
		popups.emplace_back(text);
	}
	void popupText(char const*, PopupType) override {}
	void popupTextTemporary(char const*, PopupType) override {}
	void cancelPopup() override {}
	void freezeWithError(char const*) override {}
	void displayError(Error) override {}
	void removeWorkingAnimation() override {}
	void displayLoadingAnimationText(char const*, bool, bool) override {}
	void removeLoadingAnimation() override {}
	bool hasPopup() override { return false; }
	bool hasPopupOfType(PopupType) override { return false; }
	void consoleText(char const*) override {}
	void timerRoutine() override {}
};

/// @brief The popups shown while it lives, in English.
struct Popups {
	PopupDisplay mine;
	deluge::hid::Display* saved = display;
	Popups() {
		display = &mine;
		deluge::l10n::chosenLanguage = &deluge::l10n::built_in::english;
	}
	~Popups() { display = saved; }
	Popups(const Popups&) = delete;
	Popups& operator=(const Popups&) = delete;
	static std::string read_error() { return deluge::l10n::get(deluge::l10n::String::STRING_FOR_DX_ERROR_READ_ERROR); }
};

/// @brief The scripted channel: each collect answers the next status (the last one repeating), READY with the bank.
struct FakeChannel {
	static inline std::deque<DelugeChannelStatus> answers;
	static inline int32_t requests = 0;
	static inline std::string last_request;
	static inline uint64_t now_us = 0;

	static DelugeChannelStatus collect(const char*, uint8_t* out, uint32_t out_cap, uint32_t* out_len) {
		const DelugeChannelStatus s = answers.front();
		if (answers.size() > 1) {
			answers.pop_front();
		}
		if (s == DELUGE_CHANNEL_READY) {
			const std::array<std::byte, kSysexSize> bank = make_bank();
			std::memcpy(out, bank.data(), std::min<size_t>(out_cap, bank.size()));
			*out_len = static_cast<uint32_t>(bank.size());
		}
		return s;
	}
	static void request(const char* path, uint32_t) {
		requests++;
		last_request = path;
	}
	static bool booted() { return true; }
	static bool not_booted() { return false; }
	static uint64_t clock() { return now_us; }

	static void script(std::initializer_list<DelugeChannelStatus> s) {
		answers = s;
		requests = 0;
		last_request.clear();
		now_us = 0;
	}
	static DxCartridgeRead::Channel channel() { return {.collect = &collect, .request = &request, .booted = &booted}; }
};

/// @brief A synth whose first oscillator is DX7, its slot as the menu names it.
struct DxSong {
	spec::SongFixture song = spec::make_synth_song();
	Song* saved = currentSong;
	Sound* sound = nullptr;

	DxSong() {
		currentSong = song.song;
		sound = static_cast<Sound*>(static_cast<ModControllableAudio*>(song.instrument->toModControllable()));
		sound->sources[0].setOscType(OscType::DX7);
	}
	~DxSong() { currentSong = saved; }
	DxSong(const DxSong&) = delete;
	DxSong& operator=(const DxSong&) = delete;

	deluge::project::SlotAddress slot() {
		return *menu::context_for(*song.song, deluge::gui::menuViewOf(song.clip), 0, false, false, false, false)
		            .addresses.slot;
	}
	deluge::instrument::DxPatchState patch() { return deluge::bridge::dxPatchOf(sound->sources[0]).value(); }
};

} // namespace

// clang-format off
describe dx_cartridge("the DX7 cartridge", ${
	it("is no bank before a read has loaded one", _ {
		expect(menu::host_runtime_facts().dx_cartridge().has_value()).to_be_false();
	});

	it("names the loaded bank's programs as legacy's cartridge page does", _ {
		load_bank();
		const std::optional<bind::DxCartridgeNames> names = menu::host_runtime_facts().dx_cartridge();
		expect(names.has_value()).to_be_true();
		expect(names->count).to_equal(legacy::dxCartridge.pd->numPatches());
		char want[32][11];
		legacy::dxCartridge.pd->getProgramNames(want);
		for (int32_t i = 0; i < names->count; i++) {
			expect(std::string{names->names[i].data()}).to_equal(std::string{want[i]});
		}
		expect(std::string{names->names[5].data()}).to_equal(std::string{"PROG 5"});
	});

	it("loads a program over the source's patch as legacy's page does, naming an unsaved synth after it", _ {
		DxSong s;
		load_bank();
		s.song.instrument->mightExistOnCard = false;
		deluge::instrument::DxPatchState want = s.patch();
		legacy::dxCartridge.pd->unpackProgram(want.params, 5);
		expect(menu::host_write_sink().load_dx_program(s.slot(), 0, 5)).to_be_true();
		expect(s.patch() == want).to_be_true();
		expect(s.song.instrument->name).to_equal(std::string{"PROG 5"});
	});

	it("leaves the name of a synth that might be on the card alone", _ {
		DxSong s;
		load_bank();
		s.song.instrument->mightExistOnCard = true;
		s.song.instrument->name = "KEEP";
		expect(menu::host_write_sink().load_dx_program(s.slot(), 0, 3)).to_be_true();
		expect(s.song.instrument->name).to_equal(std::string{"KEEP"});
	});

	it("refuses a program outside the bank, a source that is not DX7, a slot that does not resolve and no bank", _ {
		DxSong s;
		load_bank();
		const deluge::instrument::DxPatchState before = s.patch();
		expect(menu::host_write_sink().load_dx_program(s.slot(), 0, -1)).to_be_false();
		expect(menu::host_write_sink().load_dx_program(s.slot(), 0, 32)).to_be_false();
		expect(menu::host_write_sink().load_dx_program(s.slot(), 1, 0)).to_be_false();
		deluge::project::SlotAddress nowhere = s.slot();
		nowhere.id += 1000;
		expect(menu::host_write_sink().load_dx_program(nowhere, 0, 0)).to_be_false();
		DX7Cartridge* bank = legacy::dxCartridge.pd;
		legacy::dxCartridge.pd = nullptr;
		expect(menu::host_write_sink().load_dx_program(s.slot(), 0, 0)).to_be_false();
		legacy::dxCartridge.pd = bank;
		expect(s.patch() == before).to_be_true();
	});

	it("asks the channel for the file on the first collect and again on UNKNOWN, but not while it is PENDING", _ {
		DxCartridgeRead read{FakeChannel::channel(), &FakeChannel::clock};
		FakeChannel::script({DELUGE_CHANNEL_UNKNOWN, DELUGE_CHANNEL_PENDING, DELUGE_CHANNEL_UNKNOWN,
		                     DELUGE_CHANNEL_PENDING});
		expect(read.start("DX7/BANK.SYX") == DxCartridgeRead::Status::Pending).to_be_true();
		expect(FakeChannel::requests).to_equal(0);
		expect(read.poll() == DxCartridgeRead::Status::Pending).to_be_true();
		expect(FakeChannel::requests).to_equal(1);
		expect(FakeChannel::last_request).to_equal(std::string{"DX7/BANK.SYX"});
		expect(read.poll() == DxCartridgeRead::Status::Pending).to_be_true();
		expect(FakeChannel::requests).to_equal(1);
		expect(read.poll() == DxCartridgeRead::Status::Pending).to_be_true();
		expect(FakeChannel::requests).to_equal(2);
		expect(read.busy()).to_be_true();
	});

	it("parses the bytes when they arrive and ends Loaded, once", _ {
		legacy::dxCartridge.pd = nullptr;
		DxCartridgeRead read{FakeChannel::channel(), &FakeChannel::clock};
		FakeChannel::script({DELUGE_CHANNEL_PENDING, DELUGE_CHANNEL_READY});
		(void)read.start("DX7/BANK.SYX");
		expect(read.poll() == DxCartridgeRead::Status::Pending).to_be_true();
		expect(read.poll() == DxCartridgeRead::Status::Loaded).to_be_true();
		expect(read.busy()).to_be_false();
		expect(read.poll() == DxCartridgeRead::Status::Idle).to_be_true();
		expect(legacy::dxCartridge.pd != nullptr).to_be_true();
		char name[11];
		legacy::dxCartridge.pd->getProgramName(7, name);
		expect(std::string{name}).to_equal(std::string{"PROG 7"});
	});

	it("ends Failed with the read-error popup when the channel could not read the file", _ {
		DxCartridgeRead read{FakeChannel::channel(), &FakeChannel::clock};
		FakeChannel::script({DELUGE_CHANNEL_FAILED});
		Popups p;
		(void)read.start("DX7/BANK.SYX");
		expect(read.poll() == DxCartridgeRead::Status::Failed).to_be_true();
		expect(read.busy()).to_be_false();
		expect(p.mine.popups == std::vector<std::string>{Popups::read_error()}).to_be_true();
	});

	it("gives up with the read-error popup once the channel has left the read unanswered for the bound", _ {
		DxCartridgeRead read{FakeChannel::channel(), &FakeChannel::clock};
		FakeChannel::script({DELUGE_CHANNEL_PENDING});
		Popups p;
		(void)read.start("DX7/BANK.SYX");
		expect(read.poll() == DxCartridgeRead::Status::Pending).to_be_true();
		FakeChannel::now_us = DxCartridgeRead::kStallUs - 1;
		expect(read.poll() == DxCartridgeRead::Status::Pending).to_be_true();
		expect(p.mine.popups.empty()).to_be_true();
		FakeChannel::now_us = DxCartridgeRead::kStallUs;
		expect(read.poll() == DxCartridgeRead::Status::Failed).to_be_true();
		expect(read.give_ups()).to_equal(1);
		expect(read.busy()).to_be_false();
		expect(p.mine.popups == std::vector<std::string>{Popups::read_error()}).to_be_true();
	});

	it("starts nothing while a read is in flight or for an empty path, and forgets a cancelled read", _ {
		DxCartridgeRead read{FakeChannel::channel(), &FakeChannel::clock};
		FakeChannel::script({DELUGE_CHANNEL_PENDING});
		expect(read.start("") == DxCartridgeRead::Status::Idle).to_be_true();
		expect(read.start("DX7/A.SYX") == DxCartridgeRead::Status::Pending).to_be_true();
		expect(read.start("DX7/B.SYX") == DxCartridgeRead::Status::Idle).to_be_true();
		read.cancel();
		expect(read.busy()).to_be_false();
		expect(read.poll() == DxCartridgeRead::Status::Idle).to_be_true();
	});

	it("reads inline before boot finishes, when nothing answers the channel", _ {
		DxCartridgeRead read{{.collect = &FakeChannel::collect, .request = &FakeChannel::request,
		                      .booted = &FakeChannel::not_booted},
		                     &FakeChannel::clock};
		FakeChannel::script({DELUGE_CHANNEL_READY});
		Popups p;
		expect(read.start("DX7/NO_SUCH_BANK.SYX") == DxCartridgeRead::Status::Failed).to_be_true();
		expect(read.busy()).to_be_false();
		expect(FakeChannel::requests).to_equal(0);
	});
});
// clang-format on

CPPSPEC_SPEC(dx_cartridge)
