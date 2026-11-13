// clang-format off
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
	it("tells a setting from an automatable param by its flags", _{
		const ParamFlags flags = ParamFlag::STEPPED | ParamFlag::ENUM | ParamFlag::SETTING;
		expect(flags.has(ParamFlag::SETTING)).to_be_true();
		expect(flags.has(ParamFlag::CENTRED)).to_be_false();
	});
});
// clang-format on

CPPSPEC_SPEC(remote_controls)
