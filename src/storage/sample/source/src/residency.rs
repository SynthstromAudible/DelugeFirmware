//! The seam between the cursor's residency policy ([`crate::cursor`]) and how a chunk becomes
//! resident.
//!
//! [`crate::manager_residency::ManagerResidency`] is the production provider. The cursor is
//! generic over [`Residency`], so the acquire hot path has no vtable.

/// The result of asking a provider to make a region resident, mirroring `DelugeRegionState`.
///
/// `Ready` and `Loading` carry a pin the cursor takes ownership of; `Unavailable` pins nothing.
pub enum Get<P> {
    Ready(P),
    Loading(P),
    Unavailable,
}

/// A pin on one region's residency; dropping it releases the pin.
///
/// The cursor keeps lease balance by ownership: every slot transition drops the outgoing pin.
pub trait RegionPin {
    /// The cluster index this pin tracks; residency identity is by index.
    fn index(&self) -> u32;
    /// Whether the region is ready now. A live query: a pin taken as `Loading` may since have
    /// landed.
    fn is_ready(&self) -> bool;
    /// The pinned payload base. Meaningful only while [`Self::is_ready`].
    fn payload(&self) -> *const u8;
    /// The opaque, generation-checked token for independent pins on this region (the C ABI's
    /// `retain`/`release`), or `0` if none can be minted.
    fn token(&self) -> u64;
}

/// A provider that makes regions resident and hands out pins on them.
pub trait Residency {
    type Pin: RegionPin;
    /// Pins region `index`, scheduling its load at loader `priority` if it is not resident.
    fn acquire(&self, index: u32, priority: u32) -> Get<Self::Pin>;
    /// The number of regions; indices at or past it are `Unavailable`.
    fn num_clusters(&self) -> u32;
    /// Takes an independent pin by token. A no-op on `0` or a stale token.
    fn retain_token(&self, token: u64);
    /// Drops an independent pin taken by [`Self::retain_token`].
    fn release_token(&self, token: u64);
}
