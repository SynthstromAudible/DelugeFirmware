/*
 * Copyright © 2019-2023 Synthstrom Audible Limited
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

#ifndef R_USB_MIDI_CONFIG_H
#define R_USB_MIDI_CONFIG_H

// Let's just keep these two pipes as not the same as the send-pipe for USB MIDI hosting (PIPE1)
#define USB_CFG_PMIDI_BULK_IN  (USB_PIPE2)
#define USB_CFG_PMIDI_BULK_OUT (USB_PIPE3)

#endif /* R_USB_MIDI_CONFIG_H */
