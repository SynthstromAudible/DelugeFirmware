//! Every conversion, in the order the official firmware's load applied their effects.

use crate::tree::Node;
use crate::upgrade::Context;
use crate::version::FirmwareVersion;

pub mod cables;
pub mod params;
pub mod samples;
pub mod sound;

/// One conversion: it rewrites a document in place.
pub struct Pass {
    /// Its name, as the report and the tests use it.
    pub name: &'static str,
    /// It applies to a file whose stamp is older than this; with none, to every file.
    pub before: Option<FirmwareVersion>,
    /// The conversion.
    pub run: fn(&mut Node, &mut Context),
}

/// Every conversion the upgrader makes, in order.
pub static ALL: &[Pass] = &[
    // Read by every firmware that read them: no version gates them.
    Pass {
        name: "old sound forms",
        before: None,
        run: sound::old_sound_forms,
    },
    Pass {
        name: "range cables and knobs",
        before: None,
        run: cables::range_forms,
    },
    // Milliseconds become positions before 2.1 and are dropped after: every file is looked at.
    Pass {
        name: "millisecond zones",
        before: None,
        run: samples::millisecond_zones,
    },
    // Version-gated, in the order a load applied them: a sound's resonance compensation came before its default
    // expression cables (one of which goes to the filter whose cables the compensation checks).
    Pass {
        name: "sound resonance",
        before: Some(FirmwareVersion::official(1, 2, 0)),
        run: params::sound_resonance,
    },
    Pass {
        name: "kit resonance",
        before: Some(FirmwareVersion::official(1, 2, 0)),
        run: params::kit_resonance,
    },
    Pass {
        name: "default expression cables",
        before: Some(FirmwareVersion::official_tagged(4, 0, 0, "beta")),
        run: cables::default_expression_cables,
    },
    Pass {
        name: "linear interpolation",
        before: Some(FirmwareVersion::official_tagged(2, 1, 0, "beta")),
        run: samples::linear_interpolation,
    },
    Pass {
        name: "arpeggiator rate",
        before: Some(FirmwareVersion::official_tagged(1, 5, 0, "pretest")),
        run: params::arpeggiator_rate,
    },
    Pass {
        name: "swing interval",
        before: Some(FirmwareVersion::official_tagged(3, 1, 0, "alpha2")),
        run: params::swing_interval,
    },
];
