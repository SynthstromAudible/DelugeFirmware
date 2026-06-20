/* bindgen input: the full libdeluge C-ABI surface the Rust BSP implements.
 * Parsed as C (no __cplusplus), so the extern "C" blocks' declarations and POD
 * types are picked up while the C++-only helpers (e.g. CriticalSectionGuard) are
 * skipped. Include dir: <repo>/include. */
#include <libdeluge/libdeluge.h> /* umbrella: audio_io, block_device, board, clock,
                                    control_surface, cv_gate, display, memory,
                                    midi_io, system, types */
#include <libdeluge/app.h>
#include <libdeluge/encoder_io.h>
#include <libdeluge/flash.h>
#include <libdeluge/signals.h>
#include <libdeluge/storage_wait.h>
