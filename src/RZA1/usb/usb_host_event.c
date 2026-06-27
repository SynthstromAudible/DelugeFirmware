/*
 * Copyright © 2014-2025 Synthstrom Audible Limited
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

#include "RZA1/usb/usb_host_event.h"
#include "libdeluge/system.h" // ENTER/EXIT_CRITICAL_SECTION

#define USB_HOST_EVENT_RING_SIZE 8

static volatile int ring[USB_HOST_EVENT_RING_SIZE];
static volatile unsigned head = 0; // next slot to write
static volatile unsigned tail = 0; // next slot to read

void usbHostEventRecord(int event)
{
    ENTER_CRITICAL_SECTION();
    unsigned next = (head + 1u) % USB_HOST_EVENT_RING_SIZE;
    if (next != tail)
    { // drop if full rather than overwrite unread events
        ring[head] = event;
        head       = next;
    }
    EXIT_CRITICAL_SECTION();
}

int usbHostEventTake(void)
{
    int event = USB_HOST_EVENT_NONE;
    ENTER_CRITICAL_SECTION();
    if (tail != head)
    {
        event = ring[tail];
        tail  = (tail + 1u) % USB_HOST_EVENT_RING_SIZE;
    }
    EXIT_CRITICAL_SECTION();
    return event;
}
