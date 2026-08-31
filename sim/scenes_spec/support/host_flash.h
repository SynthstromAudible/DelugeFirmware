#pragma once

/// @brief `ErasedFlash` -- an RAII guard that leaves the host's modelled settings sector erased,
///        on both sides of an example that writes it.
///
/// **Why this exists.** The host BSP backs `deluge_flash_read`/`_erase`/`_program` with one
/// process-global 256-byte buffer (`src/bsp/host/host_bsp.c`, the `flash.h` stub section). The NOR
/// semantics it models are right -- erased reads as `0xFF`, programming can only clear bits -- but
/// the *lifetime* is the whole process, and a spec binary runs many examples in one process. So
/// any example that reaches `FlashStorage::writeSettings()` leaves 256 bytes of settings sitting
/// there for every example that runs after it, in whatever order CppSpec happens to run them.
///
/// Today that is benign only by accident of ordering: the two examples that write settings both
/// restore the C++ globals they perturbed, and no later example calls `FlashStorage::readSettings`
/// expecting the erased-flash arm. Neither of those is a property anyone is holding on to. An
/// example added later that asserts "a fresh boot finds no saved settings" would pass or fail
/// depending on which file ctest happened to link first -- the worst shape of test failure, since
/// it is not reproducible from the example itself.
///
/// **The contract.** Construct one for the scope of any example that writes settings. It erases on
/// the way in, so the example starts from the state a fresh process starts from
/// (`readSettings`' "no settings were previously saved" arm), and erases again on the way out, so
/// no later example inherits its bytes. Both ends matter: erasing only on exit still lets an
/// *earlier* writer bleed into this example.
///
/// Modelled on `NoSdRoot` (`sim/scenes_spec/context_menu_scene_spec.cpp`), which does the same job
/// for the SD passthrough root: save/neutralise/restore around a driven action that touches
/// process-global state the harness shares.
///
/// @note One sector, and it starts at 0 -- `deluge_flash_erase`'s `offset` is ignored by the host
///       stub, and `FlashStorage::writeSettings` only ever erases and programs at 0.
///
/// @warning This erases the *flash image*, not `FlashStorage`'s C++ globals. An example that
///          perturbs `FlashStorage::padColours` (or any other in-RAM setting) still has to
///          save and restore that itself; this guard only stops the bytes leaking through the
///          persistence layer.

#include "libdeluge/flash.h"

namespace deluge::scenes::spec {

struct ErasedFlash {
	ErasedFlash() { deluge_flash_erase(0); }
	~ErasedFlash() { deluge_flash_erase(0); }

	ErasedFlash(const ErasedFlash&) = delete;
	ErasedFlash& operator=(const ErasedFlash&) = delete;
};

} // namespace deluge::scenes::spec
