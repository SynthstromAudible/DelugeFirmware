#![no_main]

//! Fuzz target: assert deluge_audio_format's central promise — no panic on any
//! input. A parse that succeeds must also survive the wavetable-acceptance
//! policy pass at both `insist` settings.

use libfuzzer_sys::fuzz_target;

fuzz_target!(|data: &[u8]| {
    if let Ok(header) = deluge_audio_format::parse(data) {
        let _ = deluge_audio_format::wavetable::accept_as_wavetable(&header, false);
        let _ = deluge_audio_format::wavetable::accept_as_wavetable(&header, true);
    }
});
