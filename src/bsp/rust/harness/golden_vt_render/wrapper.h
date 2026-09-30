/* bindgen input: the whole libdeluge C ABI. Parsed as C (no __cplusplus), so every declaration and POD type is
 * read and the C++-only helpers are skipped. Include dirs: <repo>/include, and <repo>/src/OSLikeStuff for the
 * scheduler API that scheduler.h re-exports. */
#include <libdeluge/libdeluge.h>
