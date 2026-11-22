//! The emulator's firmware version: the git state it was built from.
//!
//! The C++ tree the emulator links is the one `./dbt test` builds, which embeds a constant version
//! ending `-test` so git state never rebuilds it. The emulator's menu should name the commit, so
//! its startup replaces that suffix with the commit this crate's build script read.

/// The version `current` (the linked app's) becomes for commit `commit`: its `-test` suffix
/// replaced by `-<commit>`, then `-dirty` when the work tree had changes. `None` when `current`
/// does not end `-test`, i.e. the app already carries a git version.
#[cfg_attr(not(test), allow(dead_code))]
fn git_version(current: &str, commit: &str, dirty: bool) -> Option<String> {
    let base = current.strip_suffix("-test")?;
    let dirty = if dirty { "-dirty" } else { "" };
    Some(format!("{base}-{commit}{dirty}"))
}

#[cfg(feature = "sim")]
pub(crate) fn install() {
    use core::ffi::{CStr, c_char};
    use std::ffi::CString;

    unsafe extern "C" {
        static mut kFirmwareVersionString: *const c_char;
        static mut kCommitShort: *const c_char;
    }

    let commit = env!("DELUGE_SIM_GIT_COMMIT");
    let dirty = env!("DELUGE_SIM_GIT_DIRTY") == "1";
    // SAFETY: called on the main thread before the app starts, so nothing reads these pointers
    // concurrently; each points at a NUL-terminated string that lives for the whole process
    // (the app's own literal, or one leaked here).
    unsafe {
        let current = CStr::from_ptr(kFirmwareVersionString).to_string_lossy();
        if let Some(version) = git_version(&current, commit, dirty) {
            kFirmwareVersionString = CString::new(version).unwrap().into_raw();
            kCommitShort = CString::new(commit).unwrap().into_raw();
        }
    }
}

#[cfg(test)]
mod tests {
    use super::git_version;

    #[test]
    fn replaces_the_test_suffix_with_the_commit() {
        assert_eq!(
            git_version("1.3.0-alpha-test", "530db0219", false).as_deref(),
            Some("1.3.0-alpha-530db0219")
        );
    }

    #[test]
    fn marks_a_dirty_tree() {
        assert_eq!(
            git_version("1.3.0-test", "530db0219", true).as_deref(),
            Some("1.3.0-530db0219-dirty")
        );
    }

    #[test]
    fn leaves_a_git_version_alone() {
        assert_eq!(git_version("1.3.0-alpha-530db0219", "abc", false), None);
    }
}
