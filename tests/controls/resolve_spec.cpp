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
		const auto& sel = std::get<SelectPage>(t);
		expect(sel.page_id).to_equal(1u);
		expect(sel.slots[1].target->id.value).to_equal(30u);
		expect(sel.slots[0].target->id.value).to_equal(12u);
	});
	it("leaves a partner the user mapped elsewhere where it is", _{
		Page custom = page(1, 0, 47, 24, plugin::PressAction::NEXT_PAGE);   // lower slot: reverb, not the resonance
		const Page lpf = page(1, 0, 11, 24, plugin::PressAction::NEXT_PAGE);
		const Page hpf = page(1, 1, 12, 30, plugin::PressAction::NEXT_PAGE);
		const SectionPages pages{.current = custom, .cycle = {lpf, hpf}};
		const auto& sel = std::get<SelectPage>(resolve_press(KnobFocus{.owner = SongFocus{}}, pages, 1, true));
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
});
// clang-format on

CPPSPEC_SPEC(resolve)
