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

// reason_check.h — reference-graph sanity for the Cluster "reasons" system.
//
// Stealable audio Clusters are reference-counted by numReasonsToBeLoaded: while it is > 0 the Cluster is pinned
// (mayBeStolen() returns false). Two failure modes are invisible to the heap sanitizers because the memory itself stays
// valid:
//   * a Cluster is stolen while it still has a reason - the reason-holder's pointer now aliases someone else's data
//     (the use-after-steal that poisoning can't catch once the memory is reused), and
//   * a reason is added and never removed - the Cluster is pinned forever, i.e. a leak (the symptom that started this).
//
// This module keeps a side table of cluster-address -> {outstanding reasons, the call-site that took the first
// reason}. It asserts at steal() time that the count is zero (attributing the offending reason if not), and offers a
// snapshot/diff that reports Clusters still holding reasons across a round trip - the same workflow as the MEM_GUARD /
// heap-poison tooling, but for references rather than raw allocations. Unlike MEM_GUARD it stays on under a sanitizer
// (it never touches freed memory), so it complements the host-sim ASan build.
#ifndef DELUGE_MEMORY_REASON_CHECK_H
#define DELUGE_MEMORY_REASON_CHECK_H

#include "definitions.h"
#include <cstdint>

#if REASON_CHECK

namespace reason_check {

using Snapshot = uint32_t;

// A reason was just added to `cluster`; `callsite` is the caller of Cluster::addReason (resolve with addr2line).
void onAdd(void* cluster, void* callsite);

// A reason was just removed from `cluster`. Drops the table entry when the count returns to zero.
void onRemove(void* cluster);

// `cluster` is about to be stolen with `numReasons` outstanding. Non-zero means a still-referenced Cluster is being
// repurposed - report the call-site that took a reason and freeze. Always forgets the cluster afterwards.
void onSteal(void* cluster, int32_t numReasons);

// Forget a cluster entirely (it is being destroyed / its address recycled). Prevents a stale entry from corrupting
// tracking when a new Cluster lands at the same address.
void forget(void* cluster);

// Capture the current reason epoch; reasons taken after this compare strictly greater (for reportOutstanding).
Snapshot snapshot();

// Clusters currently holding at least one reason.
uint32_t liveClusters();

// Log every cluster still holding a reason whose first reason was taken after `since` (0 = all), attributed to the
// call-site that took it. Safe point only; allocates nothing.
void reportOutstanding(Snapshot since = 0);

} // namespace reason_check

#endif // REASON_CHECK

#endif // DELUGE_MEMORY_REASON_CHECK_H
