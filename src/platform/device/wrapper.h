/* bindgen input: the whole libdeluge C ABI. Parsed as C (no __cplusplus), so every declaration and POD type is
 * read and the C++-only helpers are skipped. Include dirs: <repo>/include, then each crate include dir that
 * build/bindgen.rs lists; each crate header is included here, after the umbrella. */
#include <libdeluge/libdeluge.h>

#include "deluge_base_alloc.h"
#include "deluge_storage_resource.h"
#include "deluge_storage_sample_fill.h"
#include "deluge_storage_sample_peaks.h"
#include "deluge_storage_sample_reader.h"
#include "deluge_storage_sample_source.h"
#include "deluge_storage_stream.h"
