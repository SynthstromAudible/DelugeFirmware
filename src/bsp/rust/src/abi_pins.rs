//! Pins for what this crate's modules do not define themselves, checked against the headers through `sys`: the
//! in-tree crates' exports, the crates' own declarations of header functions, the crate-defined header types'
//! layouts, and every header value a module copies by hand because it also compiles without `sys`. See
//! `abi_pin_macros.rs`. `harness/golden_vt_render` compiles this file too.

// --- The in-tree crates' exports ---

// Only the device build depends on `deluge_alloc` directly (as `fs_alloc`).
#[cfg(target_os = "none")]
abi_pin!(in fs_alloc::abi: deluge_heap_create, deluge_alloc, deluge_heap_register_reclaim, deluge_free,
    deluge_realloc, deluge_usable_size, deluge_slab_acquire, deluge_slab_pin, deluge_slab_unpin, deluge_slab_touch,
    deluge_slab_release);
#[cfg(target_os = "none")]
abi_pin!(in fs_alloc: deluge_heap_destroy, deluge_heap_check);
#[cfg(target_os = "none")]
abi_pin!(in fs_alloc::section: deluge_heap_set_section_hooks);
#[cfg(target_os = "none")]
abi_pin!(in fs_alloc::slab: deluge_slab_create, deluge_slab_create_unmanaged, deluge_slab_set_zero_on_acquire);

abi_pin!(in deluge_audio_format::abi: deluge_audio_header_parser_init, deluge_audio_header_parser_pending,
    deluge_audio_header_parser_feed, deluge_audio_header_parser_located_audio_data,
    deluge_audio_header_parser_container, deluge_audio_header_parser_finish_here,
    deluge_audio_header_accept_as_wavetable);

abi_pin!(in deluge_sample_fill: deluge_streaming_set_fill_context);
abi_pin!(in deluge_sample_fill::chunk: deluge_streamed_chunk_payload_offset);

abi_pin!(in deluge_sample_reader::abi: deluge_sample_reader_open, deluge_sample_reader_seek,
    deluge_sample_reader_close, deluge_sample_reader_window, deluge_sample_reader_advance, deluge_sample_reader_ok,
    deluge_sample_read, deluge_sample_peek, deluge_sample_invalidate);
abi_pin!(in deluge_sample_reader::reservation: deluge_sample_reserve_open, deluge_sample_reserve_move,
    deluge_sample_reserve_close);

abi_pin!(in deluge_sample_source::abi: deluge_sample_source_open, deluge_sample_region_acquire_ex,
    deluge_sample_region_acquire, deluge_sample_region_state, deluge_sample_region_retain,
    deluge_sample_region_release, deluge_sample_source_close);

abi_pin!(in deluge_sample_stream::abi: deluge_sample_stream_open, deluge_sample_stream_close,
    deluge_sample_stream_set_geometry, deluge_sample_stream_get_asset_id, deluge_sample_stream_set_asset_id,
    deluge_sample_stream_read_at);

// --- The crates' own declarations of header functions (they cannot see `sys`) ---

abi_pin!(in deluge_resource::manager: deluge_debug_now_frames);
abi_pin!(in deluge_resource::sync: ENTER_CRITICAL_SECTION, EXIT_CRITICAL_SECTION, deluge_in_audio_context);
abi_pin!(in deluge_sample_fill::native: deluge_streaming_resource_manager);
abi_pin!(in deluge_sample_reader::reader: deluge_streaming_resource_manager, deluge_streaming_request_chunk);
abi_pin!(in deluge_sample_reader::reservation: deluge_streaming_resource_manager, deluge_efatfs_read_at,
    deluge_streaming_signal_fill);
abi_pin!(in deluge_sample_source::abi: deluge_streaming_resource_manager, deluge_sample_stream_asset_id);
abi_pin!(in deluge_sample_source::manager_residency: deluge_streaming_signal_fill);
abi_pin!(in deluge_sample_stream: deluge_efatfs_open, deluge_efatfs_close, deluge_efatfs_read_at,
    deluge_streaming_resource_manager, deluge_streaming_set_fill_context);

// --- The crate-defined header types (see `build_crate_types.rs`): every field, every variant ---

abi_layout!(deluge_audio_format::abi::DelugeOptionalF32 => DelugeOptionalF32 { has, value });
// `midi_note`'s field type is itself a crate-defined header type (`DelugeOptionalF32`, pinned above), not a
// primitive, so the exhaustive closure needs the `as` escape to transmute through its own layout pin rather than
// compare it directly.
abi_layout!(deluge_audio_format::abi::DelugeAudioHeader => DelugeAudioHeader { container, num_channels, byte_depth,
    raw_data_format, sample_rate, audio_data_start_pos_bytes, audio_data_length_bytes, file_loop_start_samples,
    file_loop_end_samples, midi_note as DelugeOptionalF32, wavetable_cycle_size,
    wavetable_explicitly_specifies_self });
abi_layout!(deluge_audio_format::abi::DelugeAudioWindow => DelugeAudioWindow { start, len });
// Opaque: the header declares this only as sized/aligned storage for the parser to walk through, with no fields of
// its own to check, so size and alignment are the whole contract.
abi_layout!(deluge_audio_format::abi::DelugeAudioHeaderParser => DelugeAudioHeaderParser opaque);

abi_layout!(deluge_sample_fill::FillContext => DelugeStreamingFillContext { efatfs_handle, audio_data_start_pos_bytes,
    audio_data_length_bytes, first_cluster_index_with_no_audio_data, cluster_size, cluster_size_magnitude,
    raw_data_format, byte_depth, num_channels });
abi_layout!(deluge_sample_fill::StreamingFillDescriptor => StreamingFillDescriptor { dest, num_sectors, ok, handle,
    byte_offset });

abi_layout!(deluge_sample_reader::abi::DelugeFrameWindow => DelugeFrameWindow { frames, frame_count });
abi_layout!(enum deluge_sample_reader::reader::ReadHint => DelugeReadHint {
    Cached = DELUGE_READ_CACHED, Scan = DELUGE_READ_SCAN });
abi_layout!(enum deluge_sample_reader::reservation::LoadMode => DelugeLoadMode {
    Enqueue = DELUGE_LOAD_ENQUEUE, Now = DELUGE_LOAD_NOW, NowOrEnqueue = DELUGE_LOAD_NOW_OR_ENQUEUE });
// The reader's own copy, beside the source's below.
abi_layout!(deluge_sample_reader::reader::Geometry => DelugeSampleGeometry { audio_data_start_bytes,
    audio_data_length_bytes, cluster_size_bytes, byte_depth, num_channels, raw_data_format });

abi_layout!(deluge_sample_source::geometry::Geometry => DelugeSampleGeometry { audio_data_start_bytes,
    audio_data_length_bytes, cluster_size_bytes, byte_depth, num_channels, raw_data_format });
abi_layout!(enum deluge_sample_source::abi::DelugeRegionState => DelugeRegionState {
    Ready = DELUGE_REGION_READY, Loading = DELUGE_REGION_LOADING, Unavailable = DELUGE_REGION_UNAVAILABLE });
abi_layout!(deluge_sample_source::abi::DelugeSampleRegion => DelugeSampleRegion { payload_base, region_index,
    resident_bytes, lease });

abi_layout!(deluge_sample_stream::DelugeSampleStreamGeometry => DelugeSampleStreamGeometry {
    audio_data_start_pos_bytes, audio_data_length_bytes, first_cluster_index_with_no_audio_data, cluster_size,
    cluster_size_magnitude, raw_data_format, byte_depth, num_channels });

// --- Header values copied by hand ---

abi_const!(
    deluge_audio_format::abi::DELUGE_AUDIO_PARSE_NEED => DelugeAudioParseStatus_DELUGE_AUDIO_PARSE_NEED,
    deluge_audio_format::abi::DELUGE_AUDIO_PARSE_DONE => DelugeAudioParseStatus_DELUGE_AUDIO_PARSE_DONE,
    deluge_audio_format::abi::DELUGE_AUDIO_PARSE_NOT_AUDIO => DelugeAudioParseStatus_DELUGE_AUDIO_PARSE_NOT_AUDIO,
    deluge_audio_format::abi::DELUGE_AUDIO_PARSE_MALFORMED => DelugeAudioParseStatus_DELUGE_AUDIO_PARSE_MALFORMED,
    deluge_audio_format::abi::DELUGE_AUDIO_PARSE_UNSUPPORTED => DelugeAudioParseStatus_DELUGE_AUDIO_PARSE_UNSUPPORTED,
    deluge_audio_format::abi::DELUGE_AUDIO_PARSE_TRUNCATED => DelugeAudioParseStatus_DELUGE_AUDIO_PARSE_TRUNCATED,
    deluge_audio_format::abi::DELUGE_AUDIO_PARSE_BAD_CALL => DelugeAudioParseStatus_DELUGE_AUDIO_PARSE_BAD_CALL,
    deluge_audio_format::abi::DELUGE_AUDIO_WALK_WHOLE_HEADER => DelugeAudioWalkGoal_DELUGE_AUDIO_WALK_WHOLE_HEADER,
    deluge_audio_format::abi::DELUGE_AUDIO_WALK_AUDIO_ONLY => DelugeAudioWalkGoal_DELUGE_AUDIO_WALK_AUDIO_ONLY,
    deluge_audio_format::abi::DELUGE_WAVETABLE_ACCEPTED => DelugeWavetableAccept_DELUGE_WAVETABLE_ACCEPTED,
    deluge_audio_format::abi::DELUGE_WAVETABLE_REJECT_STEREO => DelugeWavetableAccept_DELUGE_WAVETABLE_REJECT_STEREO,
    deluge_audio_format::abi::DELUGE_WAVETABLE_REJECT_UNKNOWN_BIT_DEPTH
        => DelugeWavetableAccept_DELUGE_WAVETABLE_REJECT_UNKNOWN_BIT_DEPTH,
    deluge_audio_format::abi::DELUGE_WAVETABLE_REJECT_NOT_A_WAVETABLE
        => DelugeWavetableAccept_DELUGE_WAVETABLE_REJECT_NOT_A_WAVETABLE,
    deluge_audio_format::abi::DELUGE_WAVETABLE_REJECT_AIFF => DelugeWavetableAccept_DELUGE_WAVETABLE_REJECT_AIFF,
    deluge_audio_format::abi::DELUGE_WAVETABLE_BAD_CALL => DelugeWavetableAccept_DELUGE_WAVETABLE_BAD_CALL,
    deluge_audio_format::abi::DELUGE_AUDIO_MAX_WINDOW_BYTES => DELUGE_AUDIO_MAX_WINDOW_BYTES,
    deluge_audio_format::abi::DELUGE_AUDIO_HEADER_PARSER_WORDS => DELUGE_AUDIO_HEADER_PARSER_WORDS,
);

abi_const!(
    crate::audio_loader::DELUGE_MAX_PATH => DELUGE_MAX_PATH,
    crate::audio_loader::MAX_PATH_CHARS + 1 => DELUGE_PATH_MAX,
    crate::audio_loader::DELUGE_AUDIO_LOAD_CANDIDATES_TRUNCATED => DELUGE_AUDIO_LOAD_CANDIDATES_TRUNCATED,
    crate::audio_loader::DELUGE_AUDIO_LOAD_PENDING => DELUGE_CHANNEL_PENDING,
    crate::audio_loader::DELUGE_AUDIO_LOAD_READY => DELUGE_CHANNEL_READY,
    crate::audio_loader::DELUGE_AUDIO_LOAD_UNKNOWN => DELUGE_CHANNEL_UNKNOWN,
);

// `DelugeTimestamp` (types.h) is not one of the crate-defined header types in `build_crate_types.rs`, so there is no
// `sys_layout` entry for it: `file_bytes`'s copy is checked against `sys`'s own binding instead.
abi_layout!(crate::file_bytes::FileTimestamp => sys DelugeTimestamp { year, month, day, hour, minute, second });

abi_const!(
    crate::file_bytes::MAX_PATH_CHARS + 1 => DELUGE_FILE_BYTES_MAX_PATH,
    crate::file_bytes::MAX_BYTES => DELUGE_FILE_BYTES_MAX_BYTES,
    crate::file_bytes::BLOCK_BYTES => DELUGE_FILE_BYTES_BLOCK,
    crate::file_bytes::NO_FILESYSTEM => DelugeStatus_DELUGE_ERR_NO_FILESYSTEM,
    crate::file_bytes::NO_MEMORY => DelugeStatus_DELUGE_ERR_NO_MEMORY,
    crate::file_bytes::ERR_IO => DelugeStatus_DELUGE_ERR_IO,
    crate::file_bytes::NOT_FOUND => DelugeStatus_DELUGE_ERR_NOT_FOUND,
    crate::file_bytes::EXISTS => DelugeStatus_DELUGE_ERR_EXISTS,
    crate::file_bytes::OPEN_READ => DelugeFileOpenMode_DELUGE_FILE_READ,
    crate::file_bytes::OPEN_WRITE_CREATE => DelugeFileOpenMode_DELUGE_FILE_WRITE_CREATE,
    crate::file_bytes::DELUGE_FILE_BYTES_PENDING => DELUGE_CHANNEL_PENDING,
    crate::file_bytes::DELUGE_FILE_BYTES_READY => DELUGE_CHANNEL_READY,
    crate::file_bytes::DELUGE_FILE_BYTES_UNKNOWN => DELUGE_CHANNEL_UNKNOWN,
    crate::file_bytes::DELUGE_FILE_BYTES_FAILED => DELUGE_CHANNEL_FAILED,
);

abi_const!(
    crate::file_directory::MAX_PATH_CHARS + 1 => DELUGE_FILE_DIRECTORY_MAX_PATH,
    crate::file_directory::NO_FILESYSTEM => DelugeStatus_DELUGE_ERR_NO_FILESYSTEM,
    crate::file_directory::DELUGE_FILE_DIRECTORY_PENDING => DELUGE_CHANNEL_PENDING,
    crate::file_directory::DELUGE_FILE_DIRECTORY_READY => DELUGE_CHANNEL_READY,
    crate::file_directory::DELUGE_FILE_DIRECTORY_UNKNOWN => DELUGE_CHANNEL_UNKNOWN,
    crate::file_directory::DELUGE_FILE_DIRECTORY_FAILED => DELUGE_CHANNEL_FAILED,
);

abi_const!(
    crate::file_prefix::PREFIX_CAPACITY => DELUGE_FILE_PREFIX_CAPACITY,
    crate::file_prefix::MAX_PATH_CHARS + 1 => DELUGE_PATH_MAX,
    crate::file_prefix::DELUGE_FILE_PREFIX_PENDING => DELUGE_CHANNEL_PENDING,
    crate::file_prefix::DELUGE_FILE_PREFIX_READY => DELUGE_CHANNEL_READY,
    crate::file_prefix::DELUGE_FILE_PREFIX_UNKNOWN => DELUGE_CHANNEL_UNKNOWN,
    crate::file_prefix::DELUGE_FILE_PREFIX_FAILED => DELUGE_CHANNEL_FAILED,
);

abi_const!(
    crate::file_write::DELUGE_FILE_WRITE_PENDING => DELUGE_CHANNEL_PENDING,
    crate::file_write::DELUGE_FILE_WRITE_READY => DELUGE_CHANNEL_READY,
    crate::file_write::DELUGE_FILE_WRITE_UNKNOWN => DELUGE_CHANNEL_UNKNOWN,
    crate::file_write::DELUGE_FILE_WRITE_FAILED => DELUGE_CHANNEL_FAILED,
    crate::file_write_slot::MAX_PATH_CHARS + 1 => DELUGE_PATH_MAX,
);

// The host `sd.rs` spells what the device build takes from `deluge-sdk`.
#[cfg(not(target_os = "none"))]
abi_const!(
    crate::sd::DELUGE_OK => DelugeStatus_DELUGE_OK,
    crate::sd::DELUGE_ERR_IO => DelugeStatus_DELUGE_ERR_IO,
    crate::sd::DELUGE_ERR_NODEV => DelugeStatus_DELUGE_ERR_NODEV,
    crate::sd::DELUGE_ERR_WRITE_PROTECTED => DelugeStatus_DELUGE_ERR_WRITE_PROTECTED,
    crate::sd::CARD_NONE => DelugeCardEvent_DELUGE_CARD_EVENT_NONE,
    crate::sd::CARD_INSERTED => DelugeCardEvent_DELUGE_CARD_EVENT_INSERTED,
    crate::sd::CARD_EJECTED => DelugeCardEvent_DELUGE_CARD_EVENT_EJECTED,
);
