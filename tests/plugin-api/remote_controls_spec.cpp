// clang-format off
#include "plugin-api/remote_controls_publisher.h"
#include "plugin-api/types/remote_controls.h"
#include <cppspec.hpp>

using namespace deluge::plugin;

describe remote_controls("remote controls pages", ${
	it("holds eight slots per page, as CLAP's remote controls do", _{
		expect(kRemoteControlsCount).to_equal(std::size_t{8});
		expect(RemotePage{}.slots.size()).to_equal(std::size_t{8});
	});
	it("starts a slot with no param and no press action", _{
		const RemoteSlot slot{};
		expect(slot.param.has_value()).to_be_false();
		expect(slot.press == PressAction::NONE).to_be_true();
	});
	it("compares slots and pages field by field", _{
		const RemoteSlot step{.param = ParamId{5}, .press = PressAction::STEP_SETTING, .press_param = ParamId{1024}};
		RemoteSlot other = step;
		expect(step == other).to_be_true();
		other.press_param = ParamId{1025};
		expect(step == other).to_be_false();
		RemotePage a{.section = 1, .page_id = 2, .name = "lpf"};
		a.slots[0] = step;
		RemotePage b = a;
		expect(a == b).to_be_true();
		b.group = 1;
		expect(a == b).to_be_false();
		b = a;
		b.slots[0] = RemoteSlot{};
		expect(a == b).to_be_false();
	});
	it("tells a setting from an automatable param by its flags", _{
		const ParamFlags flags = ParamFlag::STEPPED | ParamFlag::ENUM | ParamFlag::SETTING;
		expect(flags.has(ParamFlag::SETTING)).to_be_true();
		expect(flags.has(ParamFlag::CENTRED)).to_be_false();
	});
	it("starts a page leaving no stutter running", _{
		expect(RemotePage{}.keeps_stutter).to_be_false();
		RemotePage a;
		RemotePage b;
		b.keeps_stutter = true;
		expect(a == b).to_be_false();
	});
	it("finds a record by plugin id, and none for an unknown id", _{
		const std::array<RemoteControls, 2> records{RemoteControls{.plugin = kDelugeInstrument},
		                                            RemoteControls{.plugin = kGlobalEffectable}};
		expect(remote_controls_for(records, kGlobalEffectable) == &records[1]).to_be_true();
		expect(remote_controls_for(records, kDelugeInstrument) == &records[0]).to_be_true();
		expect(remote_controls_for(records, PluginId{"nobody"}) == nullptr).to_be_true();
	});
});
// clang-format on

CPPSPEC_SPEC(remote_controls)
