//! The render block's ports (`libdeluge/audio_ports.h`): which ports a session has, and the block the drive hands the
//! app. Pure, so the host tests cover it.

use crate::sys::{
    DelugeAudioBlock, DelugeAudioPort, DelugeAudioPortFlags_DELUGE_PORT_CODEC_INPUT,
    DelugeAudioPortFlags_DELUGE_PORT_MAIN_MIX_OUT, DelugeAudioPortFlags_DELUGE_PORT_MONITORABLE,
    DelugeStereoSample,
};

/// Whether a port's far end is there: it takes our output, and it sends us input.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) struct Far {
    pub receiving: bool,
    pub sending: bool,
}

/// The pair blocks of a block, one each way per port, as the arrays its ports point into: they must outlive the block.
pub(crate) struct Pairs {
    pub codec_in: [*const DelugeStereoSample; 1],
    pub codec_out: [*mut DelugeStereoSample; 1],
    pub usb_in: [*const DelugeStereoSample; 1],
    pub usb_out: [*mut DelugeStereoSample; 1],
}

/// A session's ports: fixed until power-off, but for USB gaining its pairs once, when the platform starts it (before a
/// host can be there).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) struct PortTable {
    /// USB's pairs each way: 0 with USB audio off, 1 in STEREO.
    usb_pairs: u8,
}

impl PortTable {
    pub(crate) const fn new(usb_audio: bool) -> Self {
        Self {
            usb_pairs: usb_audio as u8,
        }
    }

    /// One render block of `frames` frames over `pairs`, USB's far end as `usb`, and the platform's work on the last
    /// block. Every output starts silent.
    pub(crate) fn block(
        &self,
        frames: u32,
        pairs: &Pairs,
        usb: Far,
        platform_cycles: u32,
    ) -> DelugeAudioBlock {
        let all = |n: u8| if n == 0 { 0 } else { (1u32 << n) - 1 };
        let codec = DelugeAudioPort {
            in_: pairs.codec_in.as_ptr(),
            out: pairs.codec_out.as_ptr(),
            in_pairs: 1,
            out_pairs: 1,
            flags: DelugeAudioPortFlags_DELUGE_PORT_MAIN_MIX_OUT
                | DelugeAudioPortFlags_DELUGE_PORT_MONITORABLE
                | DelugeAudioPortFlags_DELUGE_PORT_CODEC_INPUT,
            far_receiving: true,
            far_sending: true,
            out_silent: all(1),
        };
        let n = self.usb_pairs;
        let usb = DelugeAudioPort {
            in_: if n == 0 {
                core::ptr::null()
            } else {
                pairs.usb_in.as_ptr()
            },
            out: if n == 0 {
                core::ptr::null()
            } else {
                pairs.usb_out.as_ptr()
            },
            in_pairs: n,
            out_pairs: n,
            flags: DelugeAudioPortFlags_DELUGE_PORT_MAIN_MIX_OUT
                | DelugeAudioPortFlags_DELUGE_PORT_MONITORABLE,
            far_receiving: usb.receiving,
            far_sending: usb.sending,
            out_silent: all(n),
        };
        DelugeAudioBlock {
            frames,
            ports: [codec, usb],
            platform_cycles,
        }
    }
}

#[cfg(test)]
mod tests {
    extern crate std;
    use super::*;
    use crate::sys::{
        DelugeAudioPortFlags_DELUGE_PORT_CODEC_INPUT as CODEC_INPUT,
        DelugeAudioPortFlags_DELUGE_PORT_MAIN_MIX_OUT as MAIN_MIX_OUT,
        DelugeAudioPortFlags_DELUGE_PORT_MONITORABLE as MONITORABLE,
        DelugeAudioPortIndex_DELUGE_PORT_CODEC as CODEC,
        DelugeAudioPortIndex_DELUGE_PORT_USB as USB, DelugeStereoSample,
    };

    /// One pair block each way for each port, and the pointer arrays the block points into.
    struct Blocks {
        codec_in: [DelugeStereoSample; 4],
        codec_out: [DelugeStereoSample; 4],
        usb_in: [DelugeStereoSample; 4],
        usb_out: [DelugeStereoSample; 4],
    }

    fn pairs(b: &mut Blocks) -> Pairs {
        Pairs {
            codec_in: [b.codec_in.as_ptr()],
            codec_out: [b.codec_out.as_mut_ptr()],
            usb_in: [b.usb_in.as_ptr()],
            usb_out: [b.usb_out.as_mut_ptr()],
        }
    }

    fn blocks() -> Blocks {
        Blocks {
            codec_in: Default::default(),
            codec_out: Default::default(),
            usb_in: Default::default(),
            usb_out: Default::default(),
        }
    }

    const NONE: Far = Far {
        receiving: false,
        sending: false,
    };

    #[test]
    fn usb_off_has_no_pairs() {
        let mut b = blocks();
        let p = pairs(&mut b);
        let block = PortTable::new(false).block(4, &p, NONE, 0);
        let usb = block.ports[USB as usize];
        assert_eq!((usb.in_pairs, usb.out_pairs), (0, 0));
        assert!(usb.in_.is_null() && usb.out.is_null());
    }

    #[test]
    fn stereo_has_one_pair_each_way() {
        let mut b = blocks();
        let p = pairs(&mut b);
        let block = PortTable::new(true).block(4, &p, NONE, 0);
        assert_eq!(block.frames, 4);
        let usb = block.ports[USB as usize];
        assert_eq!((usb.in_pairs, usb.out_pairs), (1, 1));
        // SAFETY: the arrays `p` holds, for as long as it lives.
        unsafe {
            assert_eq!(*usb.in_, b.usb_in.as_ptr());
            assert_eq!(*usb.out, b.usb_out.as_mut_ptr());
        }
        let codec = block.ports[CODEC as usize];
        assert_eq!((codec.in_pairs, codec.out_pairs), (1, 1));
        unsafe { assert_eq!(*codec.in_, b.codec_in.as_ptr()) };
    }

    #[test]
    fn the_codec_port_has_its_rules() {
        let mut b = blocks();
        let p = pairs(&mut b);
        let block = PortTable::new(true).block(4, &p, NONE, 0);
        assert_eq!(
            block.ports[CODEC as usize].flags,
            MAIN_MIX_OUT | MONITORABLE | CODEC_INPUT
        );
    }

    /// USB pair 1/2 carries the main mix (or line in), and a host's input may be monitored; the codec's input rules
    /// are not its.
    #[test]
    fn the_usb_port_is_monitorable() {
        let mut b = blocks();
        let p = pairs(&mut b);
        let block = PortTable::new(true).block(4, &p, NONE, 0);
        assert_eq!(block.ports[USB as usize].flags, MAIN_MIX_OUT | MONITORABLE);
    }

    #[test]
    fn far_flags_are_per_direction() {
        let mut b = blocks();
        let p = pairs(&mut b);
        let t = PortTable::new(true);
        let usb = t
            .block(
                4,
                &p,
                Far {
                    receiving: true,
                    sending: false,
                },
                0,
            )
            .ports[USB as usize];
        assert!(usb.far_receiving && !usb.far_sending);
        let usb = t
            .block(
                4,
                &p,
                Far {
                    receiving: false,
                    sending: true,
                },
                0,
            )
            .ports[USB as usize];
        assert!(!usb.far_receiving && usb.far_sending);
        // The codec's far ends are always there.
        let codec = t.block(4, &p, NONE, 0).ports[CODEC as usize];
        assert!(codec.far_receiving && codec.far_sending);
    }

    /// Every output starts the block silent: the app clears the bits of the pairs it writes.
    #[test]
    fn every_output_starts_silent() {
        let mut b = blocks();
        let p = pairs(&mut b);
        let block = PortTable::new(true).block(4, &p, NONE, 0);
        assert_eq!(block.ports[USB as usize].out_silent, 1);
        assert_eq!(block.ports[CODEC as usize].out_silent, 1);
    }

    #[test]
    fn platform_cycles_are_passed_on() {
        let mut b = blocks();
        let p = pairs(&mut b);
        assert_eq!(
            PortTable::new(true)
                .block(4, &p, NONE, 1234)
                .platform_cycles,
            1234
        );
    }
}
