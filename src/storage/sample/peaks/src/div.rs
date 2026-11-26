//! Exact division without an integer divide: the A9 has none, so a runtime divisor goes through the FPU.

/// `n / d` rounded down, for `d > 0`. Exact for `n < 2^53`; saturates to `u64::MAX` above it and for `d == 0` (no
/// caller reaches either: frame offsets are below 2^44).
#[must_use]
pub fn floor_div_u64(n: u64, d: u64) -> u64 {
    if d == 0 || n >= 1 << 53 {
        return u64::MAX;
    }
    // Below 2^32 both conversions are single VFP instructions; above it they are library calls.
    if let (Ok(n32), Ok(d32)) = (u32::try_from(n), u32::try_from(d)) {
        return u64::from(floor_div_u32(n32, d32));
    }
    let mut q = (n as f64 / d as f64) as u64;
    // One correction step: below 2^53 the double quotient is within one of the true one.
    if q.saturating_mul(d) > n {
        q -= 1;
    } else if (q + 1).saturating_mul(d) <= n {
        q += 1;
    }
    q
}

/// `n / d` rounded down, for `d > 0`: [`floor_div_u64`]'s 32-bit path, every conversion a VFP instruction.
fn floor_div_u32(n: u32, d: u32) -> u32 {
    let mut q = (f64::from(n) / f64::from(d)) as u32;
    // A double holds every u32 exactly, and the quotient is within one of the true one.
    if u64::from(q) * u64::from(d) > u64::from(n) {
        q -= 1;
    } else if (u64::from(q) + 1) * u64::from(d) <= u64::from(n) {
        q += 1;
    }
    q
}

/// `n / d` rounded up, for `d > 0`, with [`floor_div_u64`]'s range.
#[must_use]
pub fn ceil_div_u64(n: u64, d: u64) -> u64 {
    let q = floor_div_u64(n, d);
    if q != u64::MAX && q * d < n { q + 1 } else { q }
}

/// How many whole frames of `stride` bytes fit in `bytes`. A stride is 1, 2, 3, 4, 6 or 8 (one or two channels of one
/// to four bytes); each arm divides by a constant, which the compiler turns into a multiply. Any other stride holds
/// no frames.
#[must_use]
pub fn frames_in_bytes(bytes: u32, stride: u8) -> u32 {
    match stride {
        1 => bytes,
        2 => bytes >> 1,
        3 => bytes / 3,
        4 => bytes >> 2,
        6 => bytes / 6,
        8 => bytes >> 3,
        _ => 0,
    }
}
