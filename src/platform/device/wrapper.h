/* bindgen input: the whole libdeluge C ABI. Parsed as C (no __cplusplus), so every declaration and POD type is
 * read and the C++-only helpers are skipped. Include dirs: <repo>/include, then each crate include dir that
 * build/bindgen.rs lists; each crate header is included here, after the umbrella. */
#include <libdeluge/libdeluge.h>
