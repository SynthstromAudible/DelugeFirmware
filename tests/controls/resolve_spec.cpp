// clang-format off
#include "controls/resolve.h"
#include <cppspec.hpp>

using namespace deluge;
using namespace deluge::controls;

namespace {
Page page(uint32_t section, uint32_t id, std::optional<uint32_t> lower, std::optional<uint32_t> upper,
          plugin::PressAction upper_press = plugin::PressAction::NONE) {
	Page p{.section = section, .page_id = id};
	if (lower) { p.slots[0].target = project::ParamTarget{.space = project::ParamSpace::ENGINE, .id = {*lower}}; }
	if (upper) { p.slots[1].target = project::ParamTarget{.space = project::ParamSpace::ENGINE, .id = {*upper}}; }
	p.slots[1].press = upper_press;
	return p;
}

/// The song compressor's section: the reverb page (group 0, id 0), then a compressor page per setting (group 1).
SectionPages comp_section(uint32_t current_id, uint32_t comp_entry) {
	SectionPages out{.group_entry = {0, comp_entry}};
	for (uint32_t id = 0; id < 4; id++) {
		Page p = page(4, id, 100 + id, 1032, plugin::PressAction::TOGGLE_GROUP);
		p.group = id == 0 ? 0 : 1;
		p.slots[0].press = id == 0 ? plugin::PressAction::REVERB_PRESET : plugin::PressAction::NEXT_PAGE;
		out.cycle.push_back(p);
	}
	out.current = out.cycle[current_id];
	return out;
}

/// A mod FX section with the views @p ids, the lower slot moving each view's param and stepping to the next.
SectionPages mod_fx_section(std::initializer_list<uint32_t> ids, Page current) {
	SectionPages out{.current = current};
	for (const uint32_t id : ids) {
		Page p = page(5, id, 60 + id, 70);
		p.slots[0].press = plugin::PressAction::NEXT_PAGE;
		out.cycle.push_back(p);
	}
	return out;
}

Page mod_fx_view(uint32_t id) {
	Page p = page(5, id, 60 + id, 70);
	p.slots[0].press = plugin::PressAction::NEXT_PAGE;
	return p;
}
} // namespace

describe resolve("resolving a control", ${
	it("turns the current page's slot on the focused owner", _{
		const KnobFocus focus{.owner = SongFocus{}};
		const SectionPages pages{.current = page(0, 0, 121, 129)};
		const Target target = resolve_turn(focus, pages, 1);
		const auto* write = std::get_if<ParamWrite>(&target);
		expect(write != nullptr).to_be_true();
		expect(write->address.id.value).to_equal(129u);
		expect(write->address.owner == project::OwnerId::song()).to_be_true();
	});
	it("resolves an empty slot to nothing", _{
		const SectionPages pages{.current = page(2, 0, std::nullopt, std::nullopt)};
		expect(std::holds_alternative<std::monostate>(resolve_turn(KnobFocus{.owner = SongFocus{}}, pages, 0))).to_be_true();
	});
	it("moves to the next page of the section, the pressed slot and a matching partner with it", _{
		const Page lpf = page(1, 0, 11, 24, plugin::PressAction::NEXT_PAGE);
		const Page hpf = page(1, 1, 12, 30, plugin::PressAction::NEXT_PAGE);
		const Page eq = page(1, 2, 91, 92, plugin::PressAction::NEXT_PAGE);
		const SectionPages pages{.current = lpf, .cycle = {lpf, hpf, eq}};
		const Target t = resolve_press(KnobFocus{.owner = SongFocus{}}, pages, 1, true);
		const SelectPage sel = std::get<SelectPage>(t);
		expect(sel.page_id).to_equal(1u);
		expect(sel.slots[1].target->id.value).to_equal(30u);
		expect(sel.slots[0].target->id.value).to_equal(12u);
	});
	it("leaves a partner the user mapped elsewhere where it is", _{
		Page custom = page(1, 0, 47, 24, plugin::PressAction::NEXT_PAGE);   // lower slot: reverb, not the resonance
		const Page lpf = page(1, 0, 11, 24, plugin::PressAction::NEXT_PAGE);
		const Page hpf = page(1, 1, 12, 30, plugin::PressAction::NEXT_PAGE);
		const SectionPages pages{.current = custom, .cycle = {lpf, hpf}};
		const SelectPage sel = std::get<SelectPage>(resolve_press(KnobFocus{.owner = SongFocus{}}, pages, 1, true));
		expect(sel.slots[0].target->id.value).to_equal(47u);
	});
	it("begins a stutter on a press and ends it on the release", _{
		Page p = page(6, 0, std::nullopt, 90);
		p.slots[1].press = plugin::PressAction::HOLD_STUTTER;
		const SectionPages pages{.current = p};
		expect(std::get<HostCommand>(resolve_press(KnobFocus{.owner = SongFocus{}}, pages, 1, true)) == HostCommand::STUTTER_BEGIN).to_be_true();
		expect(std::get<HostCommand>(resolve_press(KnobFocus{.owner = SongFocus{}}, pages, 1, false)) == HostCommand::STUTTER_END).to_be_true();
	});
	it("does nothing on a release except to end a hold or a CC select", _{
		const SectionPages pages{.current = page(1, 0, 11, 24, plugin::PressAction::NEXT_PAGE)};
		expect(std::holds_alternative<std::monostate>(resolve_press(KnobFocus{.owner = SongFocus{}}, pages, 1, false))).to_be_true();
	});
	it("enters the compressor group at its entry page, and toggles back to the reverb page", _{
		const KnobFocus song{.owner = SongFocus{}};
		const SelectPage in = std::get<SelectPage>(resolve_press(song, comp_section(0, 2), 1, true));
		expect(in.page_id).to_equal(2u);
		expect(in.slots[0].target->id.value).to_equal(102u);
		const SelectPage out = std::get<SelectPage>(resolve_press(song, comp_section(2, 2), 1, true));
		expect(out.page_id).to_equal(0u);
		expect(out.slots[0].target->id.value).to_equal(100u);
	});
	it("steps the slot's setting on the focused owner, in its clip", _{
		const KnobFocus focus{.owner = SoundFocus{.slot = {project::SlotAddress::Kind::OUTPUT, 7},
		                                          .clip = project::ClipAddress{12}}};
		Page p = page(3, 0, 20, 21);
		p.slots[0].press = plugin::PressAction::STEP_SETTING;
		p.slots[0].press_param = plugin::ParamId{1025};
		const StepSetting step = std::get<StepSetting>(resolve_press(focus, SectionPages{.current = p}, 0, true));
		expect(step.address.owner == project::OwnerId::instrument(7)).to_be_true();
		expect(step.address.space == project::ParamSpace::ENGINE).to_be_true();
		expect(step.address.id.value).to_equal(1025u);
		expect(step.address.clip_id == std::optional<uint32_t>{12}).to_be_true();
	});
	it("selects a MIDI knob's CC on a press, and ends the selection on the release", _{
		const KnobFocus focus{.owner = MidiFocus{.track = {5}, .clip = project::ClipAddress{9}}};
		Page p = page(0, 0, std::nullopt, 74);
		p.slots[1].press = plugin::PressAction::MIDI_CC_SELECT;
		const SectionPages pages{.current = p};
		expect(std::get<HostCommand>(resolve_press(focus, pages, 1, true)) == HostCommand::MIDI_CC_SELECT_ON).to_be_true();
		expect(std::get<HostCommand>(resolve_press(focus, pages, 1, false)) == HostCommand::MIDI_CC_SELECT_OFF).to_be_true();
	});
	it("moves to the next reverb preset", _{
		const KnobFocus song{.owner = SongFocus{}};
		expect(std::get<HostCommand>(resolve_press(song, comp_section(0, 1), 0, true)) == HostCommand::REVERB_PRESET).to_be_true();
	});
	it("walks a group's pages, skipping another group's", _{
		const KnobFocus song{.owner = SongFocus{}};
		SectionPages pages = comp_section(1, 1);
		const SelectPage next = std::get<SelectPage>(resolve_press(song, pages, 0, true));
		expect(next.page_id).to_equal(2u);
		expect(next.slots[0].target->id.value).to_equal(102u);
		// Group 0's page between group 1's first two: the press passes over it.
		pages.cycle[2].group = 0;
		const SelectPage skipped = std::get<SelectPage>(resolve_press(song, pages, 0, true));
		expect(skipped.page_id).to_equal(3u);
		expect(skipped.slots[0].target->id.value).to_equal(103u);
	});
	it("turns nothing for a slot out of range, or with no owner to write", _{
		const SectionPages pages{.current = page(0, 0, 121, 129)};
		expect(std::holds_alternative<std::monostate>(resolve_turn(KnobFocus{.owner = SongFocus{}}, pages, 8))).to_be_true();
		expect(std::holds_alternative<std::monostate>(resolve_turn(KnobFocus{.owner = SongFocus{}}, pages, -1))).to_be_true();
		expect(std::holds_alternative<std::monostate>(resolve_turn(KnobFocus{.owner = MidiFocus{.track = {5}}}, pages, 0))).to_be_true();
		expect(std::holds_alternative<std::monostate>(resolve_turn(KnobFocus{}, pages, 0))).to_be_true();
		const KnobFocus cv{.owner = NoControlsFocus{.kind = NoControlsFocus::Kind::CV_TRACK, .track = {5}, .clip_id = 3}};
		expect(std::holds_alternative<std::monostate>(resolve_turn(cv, pages, 0))).to_be_true();
		expect(std::holds_alternative<std::monostate>(resolve_press(cv, pages, 1, false))).to_be_true();
	});
	it("toggles to nothing where the other group has no entry page", _{
		const KnobFocus song{.owner = SongFocus{}};
		SectionPages pages = comp_section(0, 9);
		expect(std::holds_alternative<std::monostate>(resolve_press(song, pages, 1, true))).to_be_true();
	});
	it("wraps from a group's last page to its first, past another group's page", _{
		const KnobFocus song{.owner = SongFocus{}};
		const SelectPage next = std::get<SelectPage>(resolve_press(song, comp_section(3, 1), 0, true));
		expect(next.page_id).to_equal(1u);
		expect(next.slots[0].target->id.value).to_equal(101u);
	});
	it("moves from a page not in the cycle to the first one after it, by page id", _{
		const KnobFocus song{.owner = SongFocus{}};
		const SelectPage after = std::get<SelectPage>(resolve_press(song, mod_fx_section({1, 2}, mod_fx_view(0)), 0, true));
		expect(after.page_id).to_equal(1u);
		expect(after.slots[0].target->id.value).to_equal(61u);
		const SelectPage wrapped = std::get<SelectPage>(resolve_press(song, mod_fx_section({0, 2}, mod_fx_view(1)), 0, true));
		expect(wrapped.page_id).to_equal(2u);
		const SelectPage round = std::get<SelectPage>(resolve_press(song, mod_fx_section({0}, mod_fx_view(1)), 0, true));
		expect(round.page_id).to_equal(0u);
		expect(round.slots[0].target->id.value).to_equal(60u);
	});
});
// clang-format on

CPPSPEC_SPEC(resolve)
