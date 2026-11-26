//! A peak: the lowest and highest sample value over some frames, at 16-bit scale.

/// The lowest and highest 16-bit sample value over a span of frames, every channel folded together.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Peak {
    /// The lowest value.
    pub min: i16,
    /// The highest value.
    pub max: i16,
}

impl Peak {
    /// The fold's identity: no frames seen. It encodes as 0, so zeroed memory holds empty peaks.
    pub const EMPTY: Peak = Peak {
        min: i16::MAX,
        max: i16::MIN,
    };

    /// Whether no frame has been folded in.
    #[must_use]
    pub const fn is_empty(self) -> bool {
        self.min > self.max
    }

    /// The peak over both spans.
    #[must_use]
    pub fn fold(self, other: Peak) -> Peak {
        Peak {
            min: self.min.min(other.min),
            max: self.max.max(other.max),
        }
    }

    /// The stored form: the high half is `max + 32768`, the low half `32767 - min`, so a fold is the per-half
    /// unsigned maximum and 0 is [`Peak::EMPTY`].
    #[must_use]
    pub const fn encode(self) -> u32 {
        let hi = (self.max as i32 + 32768) as u32;
        let lo = (32767 - self.min as i32) as u32;
        (hi << 16) | lo
    }

    /// The inverse of [`Peak::encode`].
    #[must_use]
    pub const fn decode(e: u32) -> Peak {
        let hi = (e >> 16) as i32;
        let lo = (e & 0xffff) as i32;
        Peak {
            min: (32767 - lo) as i16,
            max: (hi - 32768) as i16,
        }
    }

    /// The fold of two stored values, without decoding.
    #[must_use]
    pub const fn encode_fold(a: u32, b: u32) -> u32 {
        let hi = if (a >> 16) > (b >> 16) {
            a >> 16
        } else {
            b >> 16
        };
        let lo = if (a & 0xffff) > (b & 0xffff) {
            a & 0xffff
        } else {
            b & 0xffff
        };
        (hi << 16) | lo
    }
}

/// The peak of native-format samples: each sample's top 16 bits, over every channel.
///
/// `bytes` holds whole samples of `byte_depth` bytes (1 to 4), little-endian; a trailing partial sample is ignored,
/// and any other depth reads as [`Peak::EMPTY`]. 8-bit samples are signed here: the fill converts unsigned files in
/// place.
#[must_use]
pub fn peak_of_frames(bytes: &[u8], byte_depth: u8) -> Peak {
    let mut min = i16::MAX;
    let mut max = i16::MIN;
    let mut fold = |v: i16| {
        min = min.min(v);
        max = max.max(v);
    };
    match byte_depth {
        1 => bytes.iter().for_each(|&b| fold(i16::from(b as i8) << 8)),
        2 => bytes
            .as_chunks::<2>()
            .0
            .iter()
            .for_each(|s| fold(i16::from_le_bytes(*s))),
        3 => bytes
            .as_chunks::<3>()
            .0
            .iter()
            .for_each(|s| fold(i16::from_le_bytes([s[1], s[2]]))),
        4 => bytes
            .as_chunks::<4>()
            .0
            .iter()
            .for_each(|s| fold(i16::from_le_bytes([s[2], s[3]]))),
        _ => {}
    }
    Peak { min, max }
}
