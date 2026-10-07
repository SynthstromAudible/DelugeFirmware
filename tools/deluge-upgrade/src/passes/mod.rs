//! Every conversion, in the order the official firmware's load applied their effects.

use crate::tree::Node;
use crate::upgrade::Context;
use crate::version::FirmwareVersion;

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
];
