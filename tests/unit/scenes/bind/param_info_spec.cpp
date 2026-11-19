#include "deluge-project/param_address.h"
#include "effects/host_chain/params.h"
#include "instruments/deluge/params.h"
#include "plugin-api/types/plugin_id.h"
#include "scenes/bind/ports/param_info.h"

#include <cppspec.hpp>

using namespace deluge;

// clang-format off
describe param_info("the descriptor rows a Deluge host's owners have", ${
	it("finds a sound's param in the instrument's rows and a kit's in the chain's", _ {
		const project::ParamTarget volume{.space = project::ParamSpace::ENGINE, .id = instrument::param::kVolume};
		expect(scenes::bind::deluge_param_info(plugin::kDelugeInstrument, volume)).not_().to_be_null();
		const project::ParamTarget chain_first{.space = project::ParamSpace::ENGINE, .id = host_chain::kParams[0].id};
		expect(scenes::bind::deluge_param_info(plugin::kGlobalEffectable, chain_first)).not_().to_be_null();
	});
	it("has no row for a non-engine param or another plugin", _ {
		const project::ParamTarget expression{.space = project::ParamSpace::EXPRESSION, .id = instrument::param::kVolume};
		expect(scenes::bind::deluge_param_info(plugin::kDelugeInstrument, expression)).to_be_null();
		const project::ParamTarget volume{.space = project::ParamSpace::ENGINE, .id = instrument::param::kVolume};
		expect(scenes::bind::deluge_param_info(plugin::kDelugeEffects, volume)).to_be_null();
	});
});

CPPSPEC_SPEC(param_info)
