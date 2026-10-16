/*
 * Copyright © 2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

/// The card a scenes spec file runs on is chosen before boot (SCENES_SPEC_CARD); SdRoot and the card checks under it
/// must say whether the installed card is the one an example expects, rather than let it list another silently.

#include "support/card_access.h" // before <cppspec.hpp> -- see its @warning

#include "spec/support/spec_main.h"

#include <string>

using deluge::scenes::spec::no_sd_root_matches;
using deluge::scenes::spec::sd_root_matches;

describe selftest_card_access(
    "scenes_spec card checks", $ {
	    it(
	        "accepts the card the scenario installed",
	        _ { expect(sd_root_matches(std::string(DELUGE_REPO_ROOT) + "/tests/fixtures/card")).to_be_true(); });
	    it(
	        "refuses a card the scenario did not install", _ {
		        expect(sd_root_matches("/nonexistent-card")).to_be_false();
		        expect(sd_root_matches(std::string(DELUGE_REPO_ROOT) + "/tests/fixtures/card-shell-menu"))
		            .to_be_false();
	        });
	    it("does not take the installed card for no card", _ { expect(no_sd_root_matches()).to_be_false(); });
    });

DELUGE_SPEC(selftest_card_access_spec, selftest_card_access)
