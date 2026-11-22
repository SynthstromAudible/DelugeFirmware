//! The host-link build support: the link of the host-built C++ app, shared by the BSP's and the
//! harnesses' build scripts (link arguments do not reach dependents, so each binary's own build
//! script emits them), and the link helpers the device link shares.

pub mod host_app;
pub mod link;
