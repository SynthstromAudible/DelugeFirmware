// The DX7 cartridge both menus read: the `.syx` read through the file prefix channel, the bank's names as the ported
// cartridge page reads them, and a program loaded through the app's sink as legacy's cartridge page loads it.
//
// The spec may include `gui/`; the library may not (`scripts/check_layer_rules.py`).

#include "gui/ui/menu_view_of.h"
#include "scenes/bind/runtime_facts.h"
#include "scenes/bind/write_sink.h"
#include "scenes/browser/browser_scene.h"
#include "scenes/menu/bind_host.h"
#include "scenes/menu/context.h"

#include "gui/l10n/l10n.h"
#include "gui/l10n/strings.h"
#include "gui/menu_item/dx/cartridge.h"
#include "gui/ui/browser/dx_cartridge_read.h"
#include "gui/ui/shell_choice_dispatch.h"
#include "hid/display/display.h"
#include "model/project_bridge/source_bridge.h"
#include "model/song/song.h"
#include "processing/sound/sound.h"
#include "processing/source.h"
#include "storage/DX7Cartridge.h"

#include "support/card_access.h"
#include "support/dx_rig.h"
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
using namespace deluge::scenes::spec::dx_rig;
using deluge::gui::ui::DxCartridgeRead;
using deluge::scenes::browser::browser_scene;
using deluge::scenes::browser::BrowserId;
namespace legacy = deluge::gui::menu_item;
namespace nodes = deluge::scenes::menu::nodes;

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

/// @brief The card these examples list: `tests/fixtures/scenes_spec/dx_cartridge`, installed for this file before boot
///        (`tests/integration/checks/scenes_spec_cards.txt`).
///
/// ```
/// DX7/BANK1.SYX      a 32-program bank whose programs are "PROG 0" to "PROG 31", as make_bank() builds it
/// DX7/BROKEN.SYX     ten bytes, no bank
/// DX7/SUB/BANK2.SYX  the same voices named "SUB 0" to "SUB 31"
/// ```
struct DxCard {
	spec::SdRoot root{std::string(DELUGE_REPO_ROOT) + "/tests/fixtures/scenes_spec/dx_cartridge"};
};

/// @brief The shared read answered by the scripted channel, PENDING for good, while it lives.
struct PendingSharedRead {
	PendingSharedRead() {
		FakeChannel::script({DELUGE_CHANNEL_PENDING});
		deluge::gui::ui::dxCartridgeRead().set_channel(FakeChannel::channel());
	}
	~PendingSharedRead() {
		deluge::gui::ui::dxCartridgeRead().cancel();
		deluge::gui::ui::dxCartridgeRead().set_channel(DxCartridgeRead::libdelugeChannel());
	}
	PendingSharedRead(const PendingSharedRead&) = delete;
	PendingSharedRead& operator=(const PendingSharedRead&) = delete;
};

/// @brief Whether the browser is the scene taking input over @p r's menu.
bool browser_up(MenuRig& r) {
	return &r.shell.stack().input_top() == &browser_scene();
}

/// @brief Tick @p r until the browser's listing has landed and its result been collected.
void settle(MenuRig& r) {
	for (int32_t i = 0; i < 8 || (browser_scene().listing_in_flight() && i < 400); i++) {
		r.tick();
	}
}

/// @brief Run the host's choice dispatch on @p r's ticks until the `.syx` read the browser parked has ended.
void read_bank(MenuRig& r) {
	for (int32_t i = 0; i < 2000 && (browser_scene().awaiting_deferred_load() || i < 4); i++) {
		deluge::gui::ui::dispatchShellChoices();
		r.tick();
	}
	settle(r);
}

/// @brief Put the browser's selection on @p name in the folder it lists.
void select_file(MenuRig& r, std::string_view name) {
	for (int32_t i = 0; i < 8 && std::string_view{browser_scene().entered_text()} != name; i++) {
		r.turn(1);
	}
}

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
	it("opens the .syx browser on DX7 from DX7 source's browse row, whose BACK returns to DX7 source", _ {
		DxCard card;
		select_language();
		MenuRig r;
		r.open(nodes::dx_menu);
		expect(static_cast<int32_t>(r.cursor())).to_equal(0);
		r.select();
		settle(r);
		expect(browser_up(r)).to_be_true();
		expect(static_cast<int32_t>(browser_scene().browser_id())).to_equal(static_cast<int32_t>(BrowserId::DxSyx));
		expect(std::string{browser_scene().current_dir()}).to_equal(std::string{"DX7"});
		expect(browser_scene().file_index()).to_equal(0);
		int32_t lit = 0; // `qwertyVisible = false`: no keys drawn on the main pads
		for (uint8_t x = 0; x < 16; x++) {
			for (uint8_t y = 0; y < 8; y++) {
				lit += r.fake.grid().grid().at(x, y) == RGB{} ? 0 : 1;
			}
		}
		expect(lit).to_equal(0);
		r.back();
		settle(r);
		expect(browser_up(r)).to_be_false();
		expect(r.focused() == &nodes::dx_menu).to_be_true();
	});

	it("a re-read of the cartridge page redraws and loads nothing: the program and the cursor stay", _ {
		// `DxCartridge::readValueAgain` only redraws; `beginSession` is what loads the first program.
		select_language();
		MenuRig r;
		r.song.instrument->mightExistOnCard = false;
		load_bank();
		r.open(nodes::dx_cartridge);
		r.turn(3);
		expect(static_cast<int32_t>(r.cursor())).to_equal(3);
		expect(r.song.instrument->name).to_equal(std::string{"PROG 3"});
		const deluge::instrument::DxPatchState before = deluge::bridge::dxPatchOf(r.sound->sources[0]).value();
		menu_scene().read_value_again();
		expect(static_cast<int32_t>(r.cursor())).to_equal(3);
		expect(r.song.instrument->name).to_equal(std::string{"PROG 3"});
		expect(deluge::bridge::dxPatchOf(r.sound->sources[0]).value() == before).to_be_true();
	});

	it("reads a bank and lands on the cartridge page under the browse row, whose BACK reopens the browser", _ {
		DxCard card;
		select_language();
		MenuRig r;
		r.song.instrument->mightExistOnCard = false;
		r.open(nodes::dx_menu);
		r.select();
		settle(r);
		select_file(r, "BANK1");
		r.select();
		expect(browser_up(r)).to_be_true(); // up until the read ends, as `DxSyxBrowser`'s is
		expect(browser_scene().awaiting_deferred_load()).to_be_true();
		read_bank(r);
		expect(browser_up(r)).to_be_false();
		expect(r.focused() == &nodes::dx_cartridge).to_be_true();
		expect(r.state().path[r.state().depth - 1] == &nodes::dx_browse).to_be_true();
		expect(r.song.instrument->name).to_equal(std::string{"PROG 0"}); // the first program, loaded on entry
		r.back();
		settle(r);
		expect(browser_up(r)).to_be_true();
		expect(r.focused() == &nodes::dx_browse).to_be_true();
		r.back();
		settle(r);
		expect(browser_up(r)).to_be_false();
		expect(r.focused() == &nodes::dx_menu).to_be_true();
	});

	it("leaves the browser up with the read error when the file is no bank", _ {
		DxCard card;
		select_language();
		Popups p;
		MenuRig r;
		r.open(nodes::dx_menu);
		r.select();
		settle(r);
		select_file(r, "BROKEN");
		r.select();
		read_bank(r);
		expect(browser_up(r)).to_be_true();
		expect(browser_scene().awaiting_deferred_load()).to_be_false();
		expect(p.mine.popups.empty()).to_be_false();
		r.back();
		settle(r);
		expect(r.focused() == &nodes::dx_menu).to_be_true();
	});

	it("forgets the read when BACK leaves the browser before it ends", _ {
		DxCard card;
		select_language();
		PendingSharedRead pending;
		MenuRig r;
		r.open(nodes::dx_menu);
		r.select();
		settle(r);
		select_file(r, "BANK1");
		r.select();
		deluge::gui::ui::dispatchShellChoices(); // the read starts
		expect(deluge::gui::ui::dxCartridgeRead().busy()).to_be_true();
		r.back();
		settle(r);
		expect(browser_up(r)).to_be_false();
		deluge::gui::ui::dispatchShellChoices(); // no browser waits for it: cancelled
		expect(deluge::gui::ui::dxCartridgeRead().busy()).to_be_false();
		expect(r.focused() == &nodes::dx_menu).to_be_true();
	});

	it("takes no second file while a read is out, as DxSyxBrowser's in-flight guard does", _ {
		DxCard card;
		select_language();
		MenuRig r;
		r.open(nodes::dx_menu);
		r.select();
		settle(r);
		select_file(r, "BANK1");
		r.select();
		select_file(r, "BROKEN");
		r.select();
		const std::optional<deluge::scenes::browser::BrowserChoice> choice = browser_scene().take_choice();
		expect(choice.has_value()).to_be_true();
		expect(choice->path).to_equal(std::string{"DX7/BANK1.SYX"});
		browser_scene().deferred_load_done(false);
		r.tick();
		r.back();
		settle(r);
	});

	it("leaves the browser up for another file when the read cannot start", _ {
		DxCard card;
		select_language();
		PendingSharedRead pending;
		MenuRig r;
		r.open(nodes::dx_menu);
		r.select();
		settle(r);
		DxCartridgeRead& read = deluge::gui::ui::dxCartridgeRead();
		(void)read.start("DX7/BROKEN.SYX"); // a read already out, as legacy's browser could leave one
		select_file(r, "BANK1");
		r.select();
		expect(read.start("DX7/BANK1.SYX") == DxCartridgeRead::Status::Idle).to_be_true();
		deluge::gui::ui::dispatchShellChoices(); // `start` answers Idle: the browser must not wait for it
		r.tick();
		expect(browser_up(r)).to_be_true();
		expect(browser_scene().awaiting_deferred_load()).to_be_false();
		read.cancel();
		r.back();
		settle(r);
	});

	it("lists the bank's programs as legacy's cartridge page draws them", _ {
		select_language();
		load_bank();
		MenuRig r;
		for (const int32_t cursor : {0, 1, 2, 5, 30, 31}) {
			legacy::dxCartridge.currentValue = cursor;
			legacy::dxCartridge.scrollPos = std::clamp(cursor - 1, 0, legacy::dxCartridge.pd->numPatches() - 3);
			deluge::hid::display::OLED::main.clear();
			legacy::dxCartridge.renderOLED();
			const std::string legacy_panel = below_bezel(legacy_panel_pbm());
			r.open(nodes::dx_cartridge);
			r.turn(cursor);
			r.tick();
			const std::string tag = "cursor " + std::to_string(cursor) + "\n";
			expect(tag + below_bezel(r.fake.pbm(0))).to_equal(tag + legacy_panel);
		}
	});

	it("loads the first program on entry, and a program per turn only where the cursor moved", _ {
		load_bank();
		MenuRig r;
		bind::RecordingSink sink;
		r.open(nodes::dx_cartridge, &sink);
		const auto programs = [&] {
			std::vector<int64_t> out;
			for (const bind::GestureRequest& g : sink.gestures) {
				if (g.kind == bind::GestureRequest::Kind::DX_PROGRAM) {
					out.push_back(g.value);
				}
			}
			return out;
		};
		expect(programs().empty()).to_be_false();
		expect(programs().back()).to_equal(int64_t{0});
		sink.gestures.clear();
		r.turn(-1);
		expect(programs().empty()).to_be_true();
		r.turn(3);
		r.turn(40);
		r.turn(1);
		expect(programs() == std::vector<int64_t>{3, 31}).to_be_true();
		expect(static_cast<int32_t>(r.cursor())).to_equal(31);
	});

	it("closes the whole menu on SELECT, as legacy's exitCompletely does, rather than going up a level", _ {
		DxCard card;
		select_language();
		MenuRig r;
		r.open(nodes::dx_menu);
		r.select();
		settle(r);
		select_file(r, "BANK1");
		r.select();
		read_bank(r);
		expect(r.focused() == &nodes::dx_cartridge).to_be_true();
		r.select();
		settle(r);
		expect(r.shell.stack().depth()).to_equal(size_t{0});
	});
});
// clang-format on

CPPSPEC_SPEC(dx_cartridge)
