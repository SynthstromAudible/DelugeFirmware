/*
 * Copyright © 2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 * Licensed under the GNU General Public License v3 or later; see LICENSE.
 */

// Stub main for synth_link_test (see tests/dsp/CMakeLists.txt). The test links the synth core (envelope/lfo/
// patcher) + ONLY its gui-free dependency TUs and NOTHING from the app — so a successful link proves the synth
// core is a self-contained, app-layer-free unit (the link-time companion to scripts/check_synth_boundary.py's
// transitive include check). It is a build/link assertion; running it is a no-op.
int main() {
	return 0;
}
