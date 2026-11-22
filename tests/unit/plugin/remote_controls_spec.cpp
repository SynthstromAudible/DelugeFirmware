// clang-format off
#include "plugin/remote_controls_publisher.h"
#include "plugin/types/remote_controls.h"
#include "support/printers/remote_controls_printer.h"
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
		expect(slot.press).to_equal(PressAction::NONE);
	});
	it("compares slots and pages field by field", _{
		const RemoteSlot step{.param = ParamId{5}, .press = PressAction::STEP_SETTING, .press_param = ParamId{1024}};
		RemoteSlot other = step;
		expect(step).to_equal(other);
		other.press_param = ParamId{1025};
		expect(step).not_().to_equal(other, "a slot with another press param");
		RemotePage a{.section = 1, .page_id = 2, .name = "lpf"};
		a.slots[0] = step;
		RemotePage b = a;
		expect(a).to_equal(b);
		b.group = 1;
		expect(a).not_().to_equal(b, "a page in another group");
		b = a;
		b.slots[0] = RemoteSlot{};
		expect(a).not_().to_equal(b, "a page with another first slot");
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
		expect(a).not_().to_equal(b, "a page that keeps the stutter");
	});
	it("finds a record by plugin id, and none for an unknown id", _{
		const std::array<RemoteControls, 2> records{RemoteControls{.plugin = kDelugeInstrument},
		                                            RemoteControls{.plugin = kGlobalEffectable}};
		expect(remote_controls_for(records, kGlobalEffectable)).to_equal(&records[1]);
		expect(remote_controls_for(records, kDelugeInstrument)).to_equal(&records[0]);
		expect(remote_controls_for(records, PluginId{"nobody"})).to_be_null();
	});
});
// clang-format on

CPPSPEC_SPEC(remote_controls)
