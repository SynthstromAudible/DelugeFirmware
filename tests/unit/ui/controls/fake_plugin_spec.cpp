// clang-format off
#include "board_controls.hpp"
#include "ui/controls/board.h"
#include "ui/controls/pages.h"
#include "ui/controls/resolve.h"
#include "ui/controls/section.h"
#include "project/song.h"
#include "host/chains/pages.h"
#include "instruments/classic/pages.h"
#include "plugin/plugin.h"
#include "plugin/remote_controls_publisher.h"
#include "plugin/state_handle.h"
#include "support/printers/controls_printer.h"
#include "support/printers/owner_id_printer.h"
#include "support/printers/remote_controls_printer.h"
#include <algorithm>
#include <array>
#include <cppspec.hpp>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <variant>

using namespace deluge;
using ui::controls::Board;
using plugin::PressAction;
using plugin::RemotePage;
using plugin::RemoteSlot;

namespace {

/// A second instrument plugin, which the controls library never names: its state is one number.
struct FakeState {
	int32_t gain{0};
	bool operator==(const FakeState&) const = default;
};
struct Fake {
	using State = FakeState;
	static constexpr plugin::PluginDescriptor descriptor() {
		return {.id = plugin::PluginId{"test.fake"}, .version = 1, .kind = plugin::PluginKind::INSTRUMENT};
	}
	static serialization::Value encode(const State& /*state*/, const plugin::CodecContext& /*context*/) {
		return serialization::Value::makeMap();
	}
	static std::expected<void, plugin::StateError> decode(const serialization::Value& /*value*/, State& /*out*/,
	                                                      const plugin::CodecContext& /*context*/) {
		return {};
	}
};

/// The fake's params, in its own space.
constexpr plugin::ParamId kGain{10};
constexpr plugin::ParamId kTone{11};
constexpr plugin::ParamId kDrive{12};
constexpr plugin::ParamId kColour{13};
constexpr plugin::ParamId kShape{20};
constexpr plugin::ParamId kShapeKind{21};
constexpr plugin::ParamId kWidth{22};
constexpr plugin::ParamId kRepeat{30};

constexpr RemoteSlot knob(plugin::ParamId id, PressAction press = PressAction::NONE) {
	return {.param = id, .press = press, .press_param = std::nullopt};
}
constexpr RemotePage fake_page(uint32_t section, uint32_t page_id, RemoteSlot lower, RemoteSlot upper,
                                bool keeps = false) {
	RemotePage out{.section = section, .page_id = page_id, .name = {}};
	out.slots[0] = lower;
	out.slots[1] = upper;
	out.keeps_stutter = keeps;
	return out;
}

/// The fake's 3 sections: two pages on NEXT_PAGE, a STEP_SETTING slot, and a stutter its section keeps running.
constexpr std::array<RemotePage, 4> kFakePages{
    fake_page(0, 0, knob(kGain, PressAction::NEXT_PAGE), knob(kTone)),
    fake_page(0, 1, knob(kDrive, PressAction::NEXT_PAGE), knob(kColour)),
    fake_page(1, 0, RemoteSlot{.param = kShape,
                               .press = PressAction::STEP_SETTING, .press_param = kShapeKind},
              knob(kWidth)),
    fake_page(2, 0, knob(kRepeat, PressAction::HOLD_STUTTER), RemoteSlot{}, true),
};

/// The fake serves its pages only to its own state: a record reached with another plugin's state serves nothing.
bool own_state(const plugin::PageContext& ctx) {
	return ctx.state != nullptr && ctx.state->as<Fake>() != nullptr;
}
uint32_t fake_count(const plugin::PageContext& ctx) {
	return own_state(ctx) ? static_cast<uint32_t>(kFakePages.size()) : 0;
}
bool fake_get(const plugin::PageContext& ctx, uint32_t index, RemotePage& out) {
	if (!own_state(ctx) || index >= kFakePages.size()) {
		return false;
	}
	out = kFakePages[index];
	return true;
}
bool fake_find(const plugin::PageContext& ctx, uint32_t section, uint32_t page_id, RemotePage& out) {
	for (const RemotePage& p : kFakePages) {
		if (own_state(ctx) && p.section == section && p.page_id == page_id) {
			out = p;
			return true;
		}
	}
	return false;
}
RemoteSlot fake_decorate(const plugin::PageContext& /*ctx*/, plugin::ParamId param) {
	for (const RemotePage& p : kFakePages) {
		for (const RemoteSlot& s : p.slots) {
			if (s.param == param) {
				return s;
			}
		}
	}
	return knob(param);
}
constexpr plugin::RemoteControls kFakeControls{.plugin = Fake::descriptor().id,
                                           .count = &fake_count,
                                           .get = &fake_get,
                                           .find = &fake_find,
                                           .decorate = &fake_decorate};

/// The app's publishers and the fake's.
std::span<const plugin::RemoteControls> publishers() {
	static const std::array<plugin::RemoteControls, 3> kRecords{instruments::classic::kRemoteControls,
	                                                            host::chains::kRemoteControls, kFakeControls};
	return kRecords;
}

/// A board of 3 section buttons and one knob, slot 0: every action the fake publishes is on slot 0.
constexpr Board kSmallBoard{.sections = 3, .slots = 1};

ui::controls::PageSources sources(const Board& board) {
	return {.publishers = publishers(), .board = board};
}

/// A song whose output 3 plays the fake in mode @p mode, its knobs mapped to the fake's sections' params.
project::Song fake_song(uint8_t mode) {
	project::Song song{};
	project::DocOutput& output = song.output_structure.getOrCreate(3);
	output.mod_knob_mode = mode;
	output.controls = {{.control = {.button = 0, .knob = 0}, .target = kGain},
	                   {.control = {.button = 0, .knob = 1}, .target = kTone},
	                   {.control = {.button = 1, .knob = 0}, .target = kShape},
	                   {.control = {.button = 1, .knob = 1}, .target = kWidth},
	                   {.control = {.button = 2, .knob = 0}, .target = kRepeat}};
	output.instrument = project::InstrumentSlot{.plugin = std::string(Fake::descriptor().id.name),
	                                            .state = plugin::StateHandle::of<Fake>({.gain = 7})};
	return song;
}
const ui::controls::KnobFocus kFake{.owner = ui::controls::SoundFocus{.slot = {project::SlotAddress::Kind::OUTPUT, 3}}};

/// The ENGINE-space param @p page's slot @p slot turns, or none.
std::optional<uint32_t> turned(const ui::controls::Page& page, size_t slot) {
	return page.slots[slot].target ? std::optional{page.slots[slot].target->id.value} : std::nullopt;
}
} // namespace

describe fake_plugin("a second plugin's controls, on a second board", ${
	for (const Board board : {kSmallBoard, kDelugeControls}) {
		context(board.sections == 3 ? "on a 3-section board" : "on the Deluge", _{
			it("reaches the fake's own record through its slot's plugin", _{
				const ui::controls::Page page = ui::controls::current_page(fake_song(0), kFake, sources(board));
				expect(turned(page, 0)).to_equal(std::optional{kGain.value});
				expect(turned(page, 1)).to_equal(std::optional{kTone.value});
				expect(page.slots[0].press).to_equal(PressAction::NEXT_PAGE);
			});
			it("turns the fake's param on the fake's owner", _{
				const project::Song song = fake_song(0);
				const ui::controls::Target t =
				    ui::controls::resolve_turn(kFake, ui::controls::section_pages(song, kFake, sources(board)), 0);
				const auto* write = std::get_if<ui::controls::ParamWrite>(&t);
				expect(write).not_().to_be_null();
				if (write != nullptr) {
					expect(write->address.id.value).to_equal(kGain.value);
					expect(write->address.owner).to_equal(project::OwnerId::instrument(3));
				}
			});
			it("walks the fake's two pages on NEXT_PAGE", _{
				const project::Song song = fake_song(0);
				const ui::controls::SectionPages pages = ui::controls::section_pages(song, kFake, sources(board));
				expect(pages.cycle.size()).to_equal(size_t{2});
				const auto sel = std::get<ui::controls::SelectPage>(ui::controls::resolve_press(kFake, pages, 0, true));
				expect(sel.page_id).to_equal(1u);
				expect(turned(ui::controls::Page{.slots = sel.slots}, 1)).to_equal(std::optional{kColour.value});
				expect(turned(ui::controls::Page{.slots = sel.slots}, 0)).to_equal(std::optional{kDrive.value});
			});
			it("steps the fake's setting", _{
				const project::Song song = fake_song(1);
				const ui::controls::Target t =
				    ui::controls::resolve_press(kFake, ui::controls::section_pages(song, kFake, sources(board)), 0, true);
				expect(std::get<ui::controls::StepSetting>(t).address.id.value).to_equal(kShapeKind.value);
			});
			it("holds the fake's stutter, which its section keeps running", _{
				const project::Song song = fake_song(2);
				const ui::controls::Target t =
				    ui::controls::resolve_press(kFake, ui::controls::section_pages(song, kFake, sources(board)), 0, true);
				expect(std::get<ui::controls::HostCommand>(t)).to_equal(ui::controls::HostCommand::STUTTER_BEGIN);
				const bool keeps = ui::controls::section_keeps_stutter(song, kFake, 2, sources(board));
				expect(keeps).to_be_true();
				expect(ui::controls::resolve_section(kFake, 2, 2, true, {.keeps_stutter = keeps}).end_stutter).to_be_false();
				const bool other = ui::controls::section_keeps_stutter(song, kFake, 0, sources(board));
				expect(other).to_be_false();
				expect(ui::controls::resolve_section(kFake, 2, 0, true, {.keeps_stutter = other}).end_stutter).to_be_true();
			});
		});
	}
	it("refuses a section past the 3-section board's last", _{
		expect(ui::controls::map_input(kSmallBoard, {ui::controls::Physical::Kind::SECTION_BUTTON, 3}).has_value()).to_be_false();
		expect(ui::controls::map_input(kSmallBoard, {ui::controls::Physical::Kind::SECTION_BUTTON, 2}).has_value()).to_be_true();
		expect(ui::controls::map_input(kSmallBoard, {ui::controls::Physical::Kind::SLOT_ENCODER, 1}).has_value()).to_be_false();
		// A mode saved past the board's last section is its last section.
		const project::Song song = fake_song(5);
		expect(int{ui::controls::knob_mode(song, kFake, kSmallBoard)}).to_equal(2);
		expect(int(ui::controls::current_page(song, kFake, sources(kSmallBoard)).section)).to_equal(2);
	});
	it("has no page past the fake's last section, on a board with more", _{
		const project::Song song = fake_song(5);
		expect(int{ui::controls::knob_mode(song, kFake, kDelugeControls)}).to_equal(5);
		const ui::controls::SectionPages pages = ui::controls::section_pages(song, kFake, sources(kDelugeControls));
		expect(std::ranges::none_of(pages.current.slots, [](const ui::controls::Slot& s) { return s.target.has_value(); }))
		    .to_be_true();
		expect(pages.cycle.empty()).to_be_true();
		expect(ui::controls::section_keeps_stutter(song, kFake, 5, sources(kDelugeControls))).to_be_false();
	});
	it("gives a sound whose slot names no published plugin no page", _{
		project::Song song = fake_song(0);
		song.output_structure.getOrCreate(3).instrument->plugin = "test.unpublished";
		const ui::controls::SectionPages pages = ui::controls::section_pages(song, kFake, sources(kSmallBoard));
		expect(std::ranges::none_of(pages.current.slots, [](const ui::controls::Slot& s) { return s.target.has_value(); }))
		    .to_be_true();
		expect(pages.cycle.empty()).to_be_true();
	});
});
// clang-format on

CPPSPEC_SPEC(fake_plugin)
