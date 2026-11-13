// clang-format off
#include "instruments/deluge/pages.h"
#include "effects/setting_params.h"
#include <cppspec.hpp>

using namespace deluge;
namespace ip = deluge::instrument::param;

describe instrument_pages("the Deluge instrument's pages", ${
	it("puts volume over pan on the first mod button", _{
		const auto& page = instrument::kDevicePages[0];
		expect(*page.slots[1].param == ip::kVolumePostFX).to_be_true();
		expect(*page.slots[0].param == ip::kPan).to_be_true();
	});
	it("steps the delay's ping-pong from the delay rate's press, or its sync type with the alternative delay knobs", _{
		expect(*instrument::slot_for(ip::kDelayRate, {}).press_param == effects::setting::kDelayPingPong).to_be_true();
		expect(*instrument::slot_for(ip::kDelayRate, {.alt_delay = true}).press_param
		       == effects::setting::kDelaySyncType).to_be_true();
	});
	it("cycles the filter page from the frequency knob, but not in FM", _{
		expect(instrument::slot_for(ip::kLpfFrequency, {}).press == plugin::PressAction::NEXT_PAGE).to_be_true();
		expect(instrument::slot_for(ip::kLpfFrequency, {.fm = true}).press == plugin::PressAction::NONE).to_be_true();
	});
	it("holds the stutter from the stutter rate", _{
		expect(instrument::slot_for(ip::kStutterRate, {}).press == plugin::PressAction::HOLD_STUTTER).to_be_true();
	});
	it("kinds a cable's depth, a patched param and an unpatched one apart", _{
		const auto depth = instrument::cable_depth_id(instrument::PatchSource::SIDECHAIN, ip::kVolumePostReverbSend);
		expect(instrument::slot_for(*depth, {}).kind == plugin::ParamKind::PATCH_CABLE).to_be_true();
		expect(instrument::slot_for(*depth, {}).press == plugin::PressAction::STEP_SETTING).to_be_true();
		expect(instrument::slot_for(ip::kPan, {}).kind == plugin::ParamKind::PATCHED).to_be_true();
		expect(instrument::slot_for(ip::kBitcrushAmount, {}).kind == plugin::ParamKind::UNPATCHED_SOUND).to_be_true();
	});
	it("cycles low-pass, high-pass, EQ on the first mod button", _{
		expect(*instrument::kFilterCycle[1].slots[1].param == ip::kHpfFrequency).to_be_true();
		expect(*instrument::kFilterCycle[2].slots[0].param == ip::kBass).to_be_true();
		expect(instrument::kFilterCycle[2].page_id).to_equal(2u);
	});
});
// clang-format on

CPPSPEC_SPEC(instrument_pages)
