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

/// The scenes_spec runner's own check: a file whose examples pass is reported as passing, and it runs before boot
/// installs a root UI.
#include "gui/ui/ui.h"
#include "spec/support/spec_main.h"

describe selftest_pass(
    "scenes_spec runner self-test: pass", $ {
	    it("passes", _ { expect(1).to_equal(1); });
	    it("runs before boot installs a root UI", _ { expect(getRootUI() == nullptr).to_be_true(); });
    });

DELUGE_SPEC(selftest_pass_spec, selftest_pass)
