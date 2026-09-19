/// libdeluge/storage_progress.h — has any storage worker been given the CPU?
///
/// One counter shared by all four storage workers (write, bytes, directory,
/// prefix). It answers a question no per-channel signal can: "has anything but
/// me run?". A caller that has asked the channel many times while this never
/// changed is not waiting on a slow card — it is starving the driver by never
/// yielding.
///
/// This is the one shape an elapsed-time bound cannot catch: a tight spinner
/// freezes the executor, so virtual time never advances and the deadline never
/// arrives. On hardware the clock keeps running and the time bound fires; under
/// the host harness the caller would hang instead.
#ifndef LIBDELUGE_STORAGE_PROGRESS_H
#define LIBDELUGE_STORAGE_PROGRESS_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/// Monotonic count of storage-worker state-machine advances. WRAPS: compare
/// successive readings for INEQUALITY, never ordering. [task] [isr]
uint32_t deluge_storage_driver_progress(void);

#ifdef __cplusplus
}
#endif

#endif // LIBDELUGE_STORAGE_PROGRESS_H
