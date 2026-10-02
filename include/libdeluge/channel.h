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

/// libdeluge/channel.h — the request/collect channels' shared vocabulary.
///
/// A channel takes a request, returns at once, and is polled for the answer. Every poll answers one of these.
#ifndef LIBDELUGE_CHANNEL_H
#define LIBDELUGE_CHANNEL_H

#include "abi.h"
#include "types.h"

/// What a poll found: one of the `DELUGE_CHANNEL_*` values. A byte, and the values are macros rather than enum
/// constants, so that bindgen evaluates them.
typedef uint8_t DelugeChannelStatus;
/// The request is queued or running: poll again.
#define DELUGE_CHANNEL_PENDING 0u
/// The answer is here, and this poll took it.
#define DELUGE_CHANNEL_READY 1u
/// No request for this key and card is queued, running or answered: ask again. An answer from before a card
/// ejection is never collected; its poll answers this.
#define DELUGE_CHANNEL_UNKNOWN 2u
/// The request ran and failed, and this poll took the failure.
#define DELUGE_CHANNEL_FAILED 3u

/// The longest path any channel takes, INCLUDING the NUL. A longer path is refused, never truncated: a truncated
/// path names a different file, which may exist.
#define DELUGE_PATH_MAX 256u

#endif // LIBDELUGE_CHANNEL_H
