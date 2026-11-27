// The DX7 cartridge the menu reads: the `.syx` read through the file prefix channel, the bank's names as the ported
// cartridge page reads them, and a program loaded through the app's sound port as main's cartridge page loads it. What
// main's page answered is held in `tests/golden/ui/menu_oracle/dx_cartridge.tsv` and its panels.
//
// The spec may include `gui/`; the library may not (`scripts/check_layer_rules.py`).

#include "gui/ui/menu_view_of.h"
#include "support/sound_fixtures.h"
#include "ui/scenes/bind/ports/sound_facts.h"
#include "ui/scenes/browser/browser_scene.h"
#include "ui/scenes/context_menu/context_menu_scene.h"
#include "ui/scenes/menu/bind_host.h"
#include "ui/scenes/menu/context.h"

#include "ui/shell/notices.h"

#include "gui/l10n/l10n.h"
#include "gui/l10n/strings.h"
#include "gui/ui/browser/dx_cartridge_bank.h"
#include "gui/ui/browser/dx_cartridge_read.h"
#include "gui/ui/browser_host.h"
#include "hid/display/display.h"
#include "model/song/song.h"
#include "processing/sound/sound.h"
#include "processing/sound/source_resources.h"
#include "storage/DX7Cartridge.h"

#include "fakes.h"
#include "support/browser_host_fake.h"
#include "support/card_access.h"
#include "support/dx_cartridge_fixtures.h"
#include "support/dx_rig.h"
#include "support/oracle_table.h"
#include "support/printers/sound_record_printer.h"
#include "support/song_fixture.h"

#include "cppspec.hpp"

#include "support/menu_context.h"
#include "support/spec_main.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using namespace deluge::ui::scenes;
using namespace deluge::ui::scenes::spec::dx_rig;
using deluge::gui::ui::DxCartridgeRead;
using deluge::ui::scenes::browser::browser_scene;
using deluge::ui::scenes::browser::BrowserId;
namespace nodes = deluge::ui::scenes::menu::nodes;

namespace {

spec::OracleTable& table() {
	return spec::OracleTable::named("dx_cartridge");
}

/// @brief The bank the cartridge page holds: the one place this spec reads or replaces it.
DX7Cartridge*& bank() {
	return deluge::gui::dxCartridgeBank();
}

/// @brief Parse a fresh bank into the bank, as a finished read does.
void load_bank() {
	std::array<std::byte, kSysexSize> bytes = spec::make_bank();
	(void)deluge::gui::loadDxCartridgeBytes(bytes);
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
			const std::array<std::byte, kSysexSize> bytes = spec::make_bank();
			std::memcpy(out, bytes.data(), std::min<size_t>(out_cap, bytes.size()));
			*out_len = static_cast<uint32_t>(bytes.size());
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

/// @brief A synth in DX7 mode, its DX7 holding a voice, its slot as the menu names it.
struct DxSong {
	spec::SongFixture song = spec::make_synth_song();
	Song* saved = currentSong;
	Sound* sound = nullptr;

	DxSong() {
		deluge::ui::scenes::spec::make_current(song.song);
		sound = deluge::model::effects_owner_of(deluge::model::owner_of(*song.instrument))->sound;
		deluge::test::setSynthMode(*sound, deluge::instruments::classic::SynthMode::DX7);
		(void)sound->dx7Patch.ensure();
	}
	~DxSong() { deluge::ui::scenes::spec::make_current(saved); }
	DxSong(const DxSong&) = delete;
	DxSong& operator=(const DxSong&) = delete;

	deluge::project::SlotAddress slot() {
		return *spec::ctx_of(*song.song, deluge::gui::menuViewOf(song.clip), 0, false, false, false, false)
		            .addresses.slot;
	}
	deluge::instruments::classic::DxPatchState patch() { return sound->dx7Patch.state().value(); }
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

/// @brief Run the browser host's poll on @p r's ticks until the `.syx` read the browser asked for has ended.
void read_bank(MenuRig& r) {
	for (int32_t i = 0; i < 2000 && (browser_scene().awaiting_deferred_load() || i < 4); i++) {
		deluge::gui::ui::pollShellBrowserHost();
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

/// @brief `DX7/TRASH.SYX`, ten bytes like `BROKEN.SYX`, on the card while it lives: a file an example may delete
///        without costing the later ones the fixture card.
struct TrashFile {
	static constexpr const char* kPath = "DX7/TRASH.SYX";
	TrashFile() {
		auto file = deluge::io::File::open(kPath, DELUGE_FILE_WRITE_CREATE);
		if (file.has_value()) {
			const std::array<std::byte, 10> bytes{};
			(void)file->write(bytes);
			(void)file->close();
		}
	}
	~TrashFile() { (void)deluge::io::unlink(kPath); }
	TrashFile(const TrashFile&) = delete;
	TrashFile& operator=(const TrashFile&) = delete;
};

/// @brief Whether the card holds @p path.
bool card_has(const char* path) {
	return deluge::io::File::open(path, DELUGE_FILE_READ).has_value();
}

/// @brief The scene of kind @p kind on @p r's stack, or null.
deluge::ui::shell::Scene* on_stack(MenuRig& r, deluge::ui::shell::SceneKind kind) {
	for (deluge::ui::shell::Scene* scene : r.shell.stack().scenes()) {
		if (scene->kind() == kind) {
			return scene;
		}
	}
	return nullptr;
}

/// @brief The popup's text, or empty with none showing.
std::string popup_text(MenuRig& r) {
	const auto* popup =
	    static_cast<const deluge::ui::shell::PopupScene*>(on_stack(r, deluge::ui::shell::SceneKind::Popup));
	return popup != nullptr ? std::string{popup->text()} : std::string{};
}

/// @brief Whether the delete-file menu is on the stack.
bool delete_menu_up(MenuRig& r) {
	const auto* menu = static_cast<const deluge::ui::scenes::context_menu::ContextMenuScene*>(
	    on_stack(r, deluge::ui::scenes::context_menu::kContextMenuSceneKind));
	return menu != nullptr && menu->descriptor_id() == deluge::ui::scenes::context_menu::DescriptorId::DeleteFile;
}

/// @brief Shift held, SAVE pressed and released, Shift let go.
void shift_save(MenuRig& r) {
	r.button(kShiftX, kShiftY, true);
	r.tick();
	r.button(6, 3, true);
	r.tick();
	r.button(6, 3, false);
	r.tick();
	r.button(kShiftX, kShiftY, false);
	r.tick();
}

} // namespace

// clang-format off
describe dx_cartridge("the DX7 cartridge", ${
	it("is no bank before a read has loaded one", _ {
		expect(menu::host_ports().sound->dx_cartridge().has_value()).to_be_false();
	});

	it("names the loaded bank's programs as main's cartridge page does", _ {
		load_bank();
		const std::optional<bind::DxCartridgeNames> names = menu::host_ports().sound->dx_cartridge();
		expect(names.has_value()).to_be_true();
		const std::vector<std::string>* want = table().find("bank", "names");
		expect(want).not_().to_be_null();
		expect(names->count).to_equal(static_cast<int32_t>(want != nullptr ? want->size() : 0));
		for (int32_t i = 0; want != nullptr && i < names->count && i < static_cast<int32_t>(want->size()); i++) {
			expect(std::string{names->names[i].data()}).to_equal((*want)[static_cast<size_t>(i)]);
		}
		expect(std::string{names->names[5].data()}).to_equal(std::string{"PROG 5"});
	});

	it("loads a program over the source's patch as main's page does, naming an unsaved synth after it", _ {
		DxSong s;
		load_bank();
		s.song.instrument->mightExistOnCard = false;
		deluge::instruments::classic::DxPatchState want = s.patch();
		const std::vector<std::string>* params = table().find("bank/5", "params");
		expect(params).not_().to_be_null();
		if (params != nullptr) {
			expect(params->size()).to_equal(want.params.size());
		}
		for (size_t i = 0; params != nullptr && i < params->size() && i < want.params.size(); i++) {
			want.params[i] = static_cast<uint8_t>(std::stoi((*params)[i]));
		}
		expect(menu::host_ports().sound_writes->load_dx_program(s.slot(), 5)).to_be_true();
		expect(s.patch()).to_equal(want);
		expect(s.song.instrument->name).to_equal(std::string{"PROG 5"});
	});

	it("leaves the name of a synth that might be on the card alone", _ {
		DxSong s;
		load_bank();
		s.song.instrument->mightExistOnCard = true;
		s.song.instrument->name = "KEEP";
		expect(menu::host_ports().sound_writes->load_dx_program(s.slot(), 3)).to_be_true();
		expect(s.song.instrument->name).to_equal(std::string{"KEEP"});
	});

	it("refuses a program outside the bank, a sound in another mode, a slot that does not resolve and no bank", _ {
		DxSong s;
		load_bank();
		const deluge::instruments::classic::DxPatchState before = s.patch();
		expect(menu::host_ports().sound_writes->load_dx_program(s.slot(), -1)).to_be_false();
		expect(menu::host_ports().sound_writes->load_dx_program(s.slot(), 32)).to_be_false();
		deluge::project::SlotAddress nowhere = s.slot();
		nowhere.id += 1000;
		expect(menu::host_ports().sound_writes->load_dx_program(nowhere, 0)).to_be_false();
		DX7Cartridge* const held = bank();
		bank() = nullptr;
		expect(menu::host_ports().sound_writes->load_dx_program(s.slot(), 0)).to_be_false();
		bank() = held;
		expect(s.patch()).to_equal(before);
		deluge::test::setSynthMode(*s.sound, deluge::instruments::classic::SynthMode::SUBTRACTIVE);
		expect(menu::host_ports().sound_writes->load_dx_program(s.slot(), 0)).to_be_false();
	});

	it("asks the channel for the file on the first collect and again on UNKNOWN, but not while it is PENDING", _ {
		DxCartridgeRead read{FakeChannel::channel(), &FakeChannel::clock};
		FakeChannel::script({DELUGE_CHANNEL_UNKNOWN, DELUGE_CHANNEL_PENDING, DELUGE_CHANNEL_UNKNOWN,
		                     DELUGE_CHANNEL_PENDING});
		expect(read.start("DX7/BANK.SYX")).to_equal(DxCartridgeRead::Status::Pending);
		expect(FakeChannel::requests).to_equal(0);
		expect(read.poll()).to_equal(DxCartridgeRead::Status::Pending);
		expect(FakeChannel::requests).to_equal(1);
		expect(FakeChannel::last_request).to_equal(std::string{"DX7/BANK.SYX"});
		expect(read.poll()).to_equal(DxCartridgeRead::Status::Pending);
		expect(FakeChannel::requests).to_equal(1);
		expect(read.poll()).to_equal(DxCartridgeRead::Status::Pending);
		expect(FakeChannel::requests).to_equal(2);
		expect(read.busy()).to_be_true();
	});

	it("parses the bytes when they arrive and ends Loaded, once", _ {
		bank() = nullptr;
		DxCartridgeRead read{FakeChannel::channel(), &FakeChannel::clock};
		FakeChannel::script({DELUGE_CHANNEL_PENDING, DELUGE_CHANNEL_READY});
		(void)read.start("DX7/BANK.SYX");
		expect(read.poll()).to_equal(DxCartridgeRead::Status::Pending);
		expect(read.poll()).to_equal(DxCartridgeRead::Status::Loaded);
		expect(read.busy()).to_be_false();
		expect(read.poll()).to_equal(DxCartridgeRead::Status::Idle);
		expect(bank()).not_().to_be_null();
		char name[11];
		bank()->getProgramName(7, name);
		expect(std::string{name}).to_equal(std::string{"PROG 7"});
	});

	it("ends Failed with the read-error popup when the channel could not read the file", _ {
		DxCartridgeRead read{FakeChannel::channel(), &FakeChannel::clock};
		FakeChannel::script({DELUGE_CHANNEL_FAILED});
		Popups p;
		(void)read.start("DX7/BANK.SYX");
		expect(read.poll()).to_equal(DxCartridgeRead::Status::Failed);
		expect(read.busy()).to_be_false();
		expect(p.mine.popups).to_equal(std::vector<std::string>{Popups::read_error()});
	});

	it("refuses a bank whose sysex ID is not Yamaha's, with main's unknown-ID popup", _ {
		DX7Cartridge* const saved = bank();
		bank() = nullptr;
		Popups p;
		std::array<std::byte, kSysexSize> bytes = spec::make_bank();
		bytes[1] = std::byte{0x44}; // the manufacturer byte: Yamaha's is 0x43
		expect(deluge::gui::loadDxCartridgeBytes(bytes)).to_be_false();
		expect(p.mine.popups)
		    .to_equal(std::vector<std::string>{deluge::l10n::get(deluge::l10n::String::STRING_FOR_DX_ERROR_SYSEX_ID)});
		delete bank();
		bank() = saved;
	});

	it("gives up with the read-error popup once the channel has left the read unanswered for the bound", _ {
		DxCartridgeRead read{FakeChannel::channel(), &FakeChannel::clock};
		FakeChannel::script({DELUGE_CHANNEL_PENDING});
		Popups p;
		(void)read.start("DX7/BANK.SYX");
		expect(read.poll()).to_equal(DxCartridgeRead::Status::Pending);
		FakeChannel::now_us = DxCartridgeRead::kStallUs - 1;
		expect(read.poll()).to_equal(DxCartridgeRead::Status::Pending);
		expect(p.mine.popups.empty()).to_be_true();
		FakeChannel::now_us = DxCartridgeRead::kStallUs;
		expect(read.poll()).to_equal(DxCartridgeRead::Status::Failed);
		expect(read.give_ups()).to_equal(1);
		expect(read.busy()).to_be_false();
		expect(p.mine.popups).to_equal(std::vector<std::string>{Popups::read_error()});
	});

	it("starts nothing while a read is in flight or for an empty path, and forgets a cancelled read", _ {
		DxCartridgeRead read{FakeChannel::channel(), &FakeChannel::clock};
		FakeChannel::script({DELUGE_CHANNEL_PENDING});
		expect(read.start("")).to_equal(DxCartridgeRead::Status::Idle);
		expect(read.start("DX7/A.SYX")).to_equal(DxCartridgeRead::Status::Pending);
		expect(read.start("DX7/B.SYX")).to_equal(DxCartridgeRead::Status::Idle);
		read.cancel();
		expect(read.busy()).to_be_false();
		expect(read.poll()).to_equal(DxCartridgeRead::Status::Idle);
	});

	it("reads inline before boot finishes, when nothing answers the channel", _ {
		DxCartridgeRead read{{.collect = &FakeChannel::collect, .request = &FakeChannel::request,
		                      .booted = &FakeChannel::not_booted},
		                     &FakeChannel::clock};
		FakeChannel::script({DELUGE_CHANNEL_READY});
		Popups p;
		expect(read.start("DX7/NO_SUCH_BANK.SYX")).to_equal(DxCartridgeRead::Status::Failed);
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
		expect(r.focused()).to_equal(&nodes::dx_menu);
	});

	it("offers to delete a .syx file on Shift+SAVE, and refuses a folder with main's popup", _ {
		// `DxSyxBrowser` does not override `Browser::buttonAction`, whose Shift+SAVE this is. The file is a copy of
		// BROKEN.SYX so the fixture card stays whole for the examples after it.
		DxCard card;
		TrashFile trash;
		select_language();
		MenuRig r;
		r.open(nodes::dx_menu);
		r.select();
		settle(r);
		select_file(r, "TRASH");
		expect(std::string{browser_scene().entered_text()}).to_equal(std::string{"TRASH"});
		expect(card_has(TrashFile::kPath)).to_be_true();
		const int32_t trashed = browser_scene().file_index();
		shift_save(r);
		expect(delete_menu_up(r)).to_be_true();
		r.select(); // OK: the host deletes the file; an answer the request produced waits for the poll
		for (int32_t i = 0; i < 400 && (card_has(TrashFile::kPath) || deluge::gui::ui::shellBrowserAnswersHeld());
		     i++) {
			deluge::gui::ui::pollShellBrowserHost();
			r.tick();
		}
		settle(r);
		expect(card_has(TrashFile::kPath)).to_be_false();
		// `Browser::currentFileDeleted`: TRASH is the last entry, so the index steps back and the name is cleared.
		expect(browser_scene().file_index()).to_equal(trashed - 1);
		expect(std::string{browser_scene().entered_text()}).to_equal(std::string{});
		expect(browser_up(r)).to_be_true();
		expect(deluge::gui::ui::dxCartridgeRead().busy()).to_be_false();
		expect(browser_scene().awaiting_deferred_load()).to_be_false();
		for (int32_t i = 0; i < 8 && !browser_scene().current_is_folder(); i++) {
			r.turn(1);
		}
		expect(browser_scene().current_is_folder()).to_be_true();
		shift_save(r);
		expect(popup_text(r)).to_equal(
		    std::string{deluge::l10n::get(deluge::l10n::String::STRING_FOR_FOLDERS_CANNOT_BE_DELETED_ON_THE_DELUGE)});
		expect(delete_menu_up(r)).to_be_false();
		r.back();
		settle(r);
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
		const deluge::instruments::classic::DxPatchState before = r.sound->dx7Patch.state().value();
		menu_scene().read_value_again();
		expect(static_cast<int32_t>(r.cursor())).to_equal(3);
		expect(r.song.instrument->name).to_equal(std::string{"PROG 3"});
		expect(r.sound->dx7Patch.state().value()).to_equal(before);
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
		expect(r.focused()).to_equal(&nodes::dx_cartridge);
		expect(r.state().path[r.state().depth - 1]).to_equal(&nodes::dx_browse);
		expect(r.song.instrument->name).to_equal(std::string{"PROG 0"}); // the first program, loaded on entry
		r.back();
		settle(r);
		expect(browser_up(r)).to_be_true();
		expect(r.focused()).to_equal(&nodes::dx_browse);
		r.back();
		settle(r);
		expect(browser_up(r)).to_be_false();
		expect(r.focused()).to_equal(&nodes::dx_menu);
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
		expect(r.focused()).to_equal(&nodes::dx_menu);
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
		r.select(); // the read starts with the press
		expect(deluge::gui::ui::dxCartridgeRead().busy()).to_be_true();
		r.back();
		settle(r);
		expect(browser_up(r)).to_be_false();
		deluge::gui::ui::pollShellBrowserHost(); // no browser waits for it: cancelled
		expect(deluge::gui::ui::dxCartridgeRead().busy()).to_be_false();
		expect(r.focused()).to_equal(&nodes::dx_menu);
	});

	it("takes no second file while a read is out, as DxSyxBrowser's in-flight guard does", _ {
		DxCard card;
		select_language();
		MenuRig r;
		const deluge::ui::scenes::spec::UseBrowserHost use{deluge::ui::scenes::spec::fake_browser_host()};
		deluge::ui::scenes::spec::fake_browser_host().clear();
		r.open(nodes::dx_menu);
		r.select();
		settle(r);
		select_file(r, "BANK1");
		r.select();
		select_file(r, "BROKEN");
		r.select();
		const std::vector<deluge::ui::scenes::browser::BrowserChoice>& asked =
		    deluge::ui::scenes::spec::fake_browser_host().performed();
		expect(asked.size()).to_equal(size_t(1));
		expect(asked.empty() ? std::string{} : asked[0].path).to_equal(std::string{"DX7/BANK1.SYX"});
		browser_scene().deferred_load_done(false);
		r.tick();
		r.back();
		settle(r);
	});

	it("leaves the browser up for another file when the read cannot start, answering after the tick that asked", _ {
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
		expect(read.start("DX7/BANK1.SYX")).to_equal(DxCartridgeRead::Status::Idle);
		// The host's `start` answered Idle inside the press. That answer is held, so the browser still waits until the
		// guest's poll delivers it: answered inside the press, it would re-enter the browser from its own `handle`.
		expect(browser_scene().awaiting_deferred_load()).to_be_true();
		expect(deluge::gui::ui::shellBrowserAnswersHeld()).to_be_true();
		deluge::gui::ui::pollShellBrowserHost();
		expect(deluge::gui::ui::shellBrowserAnswersHeld()).to_be_false();
		r.tick();
		expect(browser_up(r)).to_be_true();
		expect(browser_scene().awaiting_deferred_load()).to_be_false();
		read.cancel();
		r.back();
		settle(r);
	});

	it("delivers a held answer before it collects the read, so the read no browser waits for is cancelled", _ {
		// `pollShellBrowserHost` delivers first: the held Idle clears the browser's wait, and the same poll's
		// `pollDxCartridgeRead` then cancels the read already out, as main's browser drops a read it no longer waits for.
		DxCard card;
		select_language();
		PendingSharedRead pending;
		MenuRig r;
		r.open(nodes::dx_menu);
		r.select();
		settle(r);
		DxCartridgeRead& read = deluge::gui::ui::dxCartridgeRead();
		(void)read.start("DX7/BROKEN.SYX"); // a read already out
		select_file(r, "BANK1");
		r.select(); // answered Idle inside the press, and held
		expect(read.busy()).to_be_true();
		deluge::gui::ui::pollShellBrowserHost();
		expect(read.busy()).to_be_false();
		r.back();
		settle(r);
	});

	it("lists the bank's programs as main's cartridge page draws them", _ {
		select_language();
		load_bank();
		MenuRig r;
		for (const int32_t cursor : {0, 1, 2, 5, 30, 31}) {
			const std::string frozen = frozen_panel("dx_cartridge_" + std::to_string(cursor));
			expect(frozen.empty()).to_be_false();
			r.open(nodes::dx_cartridge);
			r.turn(cursor);
			r.tick();
			const std::string tag = "cursor " + std::to_string(cursor) + "\n";
			expect(tag + below_bezel(r.fake.pbm(0))).to_equal(tag + below_bezel(frozen));
		}
	});

	it("loads the first program on entry, and a program per turn only where the cursor moved", _ {
		load_bank();
		MenuRig r;
		bind::test::RecordingSoundWrites writes;
		r.open(nodes::dx_cartridge, &writes);
		const auto programs = [&] {
			std::vector<int64_t> out;
			for (const bind::test::RecordingSoundWrites::Call& g : writes.calls) {
				if (g.kind == bind::test::RecordingSoundWrites::Call::Kind::DX_PROGRAM) {
					out.push_back(g.value);
				}
			}
			return out;
		};
		expect(programs().empty()).to_be_false();
		expect(programs().back()).to_equal(int64_t{0});
		writes.calls.clear();
		r.turn(-1);
		expect(programs().empty()).to_be_true();
		r.turn(3);
		r.turn(40);
		r.turn(1);
		expect(programs()).to_equal(std::vector<int64_t>{3, 31});
		expect(static_cast<int32_t>(r.cursor())).to_equal(31);
	});

	it("turns the cartridge's programs five a click under Shift, clamping to the bank", _ {
		// `DxCartridge::selectEncoderAction` clamps `currentValue + offset`, on the scaled offset.
		load_bank();
		MenuRig r;
		bind::test::RecordingSoundWrites writes;
		r.open(nodes::dx_cartridge, &writes);
		r.button(kShiftX, kShiftY, true);
		r.turn(1);
		expect(static_cast<int32_t>(r.cursor())).to_equal(5);
		expect(writes.calls.back().value).to_equal(int64_t{5});
		r.turn(10);
		expect(static_cast<int32_t>(r.cursor())).to_equal(31);
		r.button(kShiftX, kShiftY, false);
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
		expect(r.focused()).to_equal(&nodes::dx_cartridge);
		r.select();
		settle(r);
		expect(r.shell.stack().depth()).to_equal(size_t{0});
	});
});

describe dx_cartridge_reference_rows("the DX7 cartridge: the reference rows", ${
	it("parses dx_cartridge.tsv clean and asks every row of it", _ {
		spec::OracleTable& t = table();
		expect(t.problems().size()).to_equal(size_t{0});
		expect(t.size()).to_be_greater_than(size_t{1});
		expect(t.unasked()).to_equal(std::string{});
	});
});
// clang-format on

DELUGE_SPEC(dx_cartridge_spec, dx_cartridge, dx_cartridge_reference_rows)
