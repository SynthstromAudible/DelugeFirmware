//! A host SD card: a sparse in-memory disk, formatted FAT32 and filled from a directory.
//!
//! `golden_vt_render` fills it with each gate's fixture tree; the emulator (`sim`) fills it with
//! the user's card directory, which [`write_back`] updates when the emulator closes.
//!
//! The card has a real card's geometry — 32 KB clusters and at least 2.5 GB, since FAT32 needs
//! 65,525 clusters — so the real storage stack runs over it. Only written sectors are kept
//! (`deluge_bsp::sd::use_memory_disk`), and no run sees an earlier run's writes. `DELUGE_SD_IMAGE`,
//! when set, names a file to use instead; `GOLDEN_SD_DUMP=<path>` writes the disk out on exit.
//!
//! Everything here runs from `main` before any executor exists, so the block device below uses the
//! disk directly, without [`SD_BUS`](crate::storage::sd::SD_BUS) or the latency model.

use std::collections::HashMap;
use std::path::{Path, PathBuf};
use std::sync::Mutex;
use std::time::SystemTime;

use aligned::{A4, Aligned};
use block_device_adapters::BufStream;
use block_device_driver::BlockDevice;
use embedded_fatfs::{FatType, FileSystem, FormatVolumeOptions, FsOptions, format_volume};
use embedded_io_async::{Read as _, Write as _};

const SECTOR_BYTES: u64 = 512;
const CLUSTER_BYTES: u32 = 32 * 1024;
/// The minimum volume size, comfortably over FAT32's 65,525 clusters at 32 KB each.
const MIN_BYTES: u64 = 2560 << 20;

/// The memory disk as a block device, for use before boot.
struct Disk;

#[derive(Debug)]
struct DiskError;

impl BlockDevice<512> for Disk {
    type Align = A4;
    type Error = DiskError;

    async fn read(
        &mut self,
        block_address: u32,
        data: &mut [Aligned<A4, [u8; 512]>],
    ) -> Result<(), DiskError> {
        // SAFETY: `Aligned<A4, [u8; 512]>` is `#[repr(C)]` over `[u8; 512]` plus a zero-sized
        // marker, so the slice is a contiguous run of 512-byte blocks; the flat view does not
        // outlive `data`.
        let flat = unsafe {
            core::slice::from_raw_parts_mut(data.as_mut_ptr().cast::<u8>(), data.len() * 512)
        };
        deluge_bsp::sd::read_sectors(block_address, data.len() as u32, flat)
            .await
            .map_err(|_| DiskError)
    }

    async fn write(
        &mut self,
        block_address: u32,
        data: &[Aligned<A4, [u8; 512]>],
    ) -> Result<(), DiskError> {
        // SAFETY: as `read`, read-only.
        let flat =
            unsafe { core::slice::from_raw_parts(data.as_ptr().cast::<u8>(), data.len() * 512) };
        deluge_bsp::sd::write_sectors(block_address, data.len() as u32, flat)
            .await
            .map_err(|_| DiskError)
    }

    async fn size(&mut self) -> Result<u64, DiskError> {
        Ok(deluge_bsp::sd::total_sectors() as u64 * SECTOR_BYTES)
    }
}

/// Returns the volume size for `tree`: twice its contents plus 64 MB, at least [`MIN_BYTES`].
fn volume_bytes(tree: Option<&Path>) -> u64 {
    let held = tree.map_or(0, tree_bytes);
    let bytes = (held.saturating_mul(2) + (64 << 20)).max(MIN_BYTES);
    (bytes + SECTOR_BYTES - 1) & !(SECTOR_BYTES - 1)
}

fn tree_bytes(dir: &Path) -> u64 {
    std::fs::read_dir(dir)
        .unwrap_or_else(|e| panic!("memory card: reading {}: {e}", dir.display()))
        .map(|entry| {
            let entry = entry.expect("memory card: a directory entry");
            let kind = entry.file_type().expect("memory card: an entry's type");
            if kind.is_dir() {
                tree_bytes(&entry.path())
            } else {
                entry.metadata().map_or(0, |m| m.len())
            }
        })
        .sum()
}

/// Replaces the card with a memory disk, formats it FAT32 and copies `tree` onto it.
///
/// `tree` is a card's contents (`SONGS/`, `SAMPLES/`, …); `None` leaves the card empty.
///
/// # Panics
///
/// Panics if the disk was already opened, or on any I/O or filesystem error.
pub fn install(tree: Option<&Path>) {
    let sectors = u32::try_from(volume_bytes(tree) / SECTOR_BYTES).expect("memory card: over 2 TB");
    deluge_bsp::sd::use_memory_disk(sectors)
        .expect("memory card: the disk was opened before the harness chose it");
    embassy_futures::block_on(async {
        let mut raw = BufStream::<Disk, 512>::new(Disk);
        let options = FormatVolumeOptions::new()
            .fat_type(FatType::Fat32)
            .bytes_per_cluster(CLUSTER_BYTES)
            .total_sectors(sectors);
        format_volume(&mut raw, options)
            .await
            .expect("memory card: formatting FAT32");
        raw.flush().await.expect("memory card: flushing the format");

        if let Some(tree) = tree {
            let fs = FileSystem::new(BufStream::<Disk, 512>::new(Disk), FsOptions::new())
                .await
                .expect("memory card: mounting the fresh volume");
            let mut loaded = HashMap::new();
            copy_dir(&fs.root_dir(), tree, Path::new(""), &mut loaded).await;
            *LOADED.lock().unwrap() = Some(Loaded {
                tree: tree.to_path_buf(),
                files: loaded,
            });
            fs.unmount()
                .await
                .expect("memory card: unmounting after the copy");
        }
    });
    log::info!(
        "memory card: {sectors} sectors, filled from {}",
        tree.map_or("nothing".into(), |t| t.display().to_string())
    );
}

/// What [`install`] copied: the source directory, and each file's card path with its modification
/// time at the copy.
struct Loaded {
    tree: PathBuf,
    files: HashMap<PathBuf, Option<SystemTime>>,
}

static LOADED: Mutex<Option<Loaded>> = Mutex::new(None);

/// Copies `from`'s entries into `into` and records each file in `loaded` under its card path.
///
/// Copies in name order so the card's directory order is the same on every run.
async fn copy_dir<IO, TP, OCC>(
    into: &embedded_fatfs::Dir<'_, IO, TP, OCC>,
    from: &Path,
    at: &Path,
    loaded: &mut HashMap<PathBuf, Option<SystemTime>>,
) where
    IO: embedded_fatfs::ReadWriteSeek,
    TP: embedded_fatfs::TimeProvider,
    OCC: embedded_fatfs::OemCpConverter,
{
    let mut entries: Vec<_> = std::fs::read_dir(from)
        .unwrap_or_else(|e| panic!("memory card: reading {}: {e}", from.display()))
        .map(|entry| entry.expect("memory card: a directory entry"))
        .collect();
    entries.sort_by_key(|entry| entry.file_name());
    for entry in entries {
        let name = entry
            .file_name()
            .into_string()
            .expect("memory card: a fixture name that is not UTF-8");
        if entry
            .file_type()
            .expect("memory card: an entry's type")
            .is_dir()
        {
            let dir = into
                .create_dir(&name)
                .await
                .unwrap_or_else(|e| panic!("memory card: creating {name}: {e:?}"));
            Box::pin(copy_dir(&dir, &entry.path(), &at.join(&name), loaded)).await;
        } else {
            let bytes = std::fs::read(entry.path())
                .unwrap_or_else(|e| panic!("memory card: reading {name}: {e}"));
            let mut file = into
                .create_file(&name)
                .await
                .unwrap_or_else(|e| panic!("memory card: creating {name}: {e:?}"));
            file.truncate()
                .await
                .unwrap_or_else(|e| panic!("memory card: truncating {name}: {e:?}"));
            file.write_all(&bytes)
                .await
                .unwrap_or_else(|e| panic!("memory card: writing {name}: {e:?}"));
            file.flush()
                .await
                .unwrap_or_else(|e| panic!("memory card: flushing {name}: {e:?}"));
            file.close()
                .await
                .unwrap_or_else(|e| panic!("memory card: closing {name}: {e:?}"));
            loaded.insert(at.join(&name), modified(&entry.path()));
        }
    }
}

fn modified(path: &Path) -> Option<SystemTime> {
    std::fs::metadata(path).and_then(|m| m.modified()).ok()
}

#[cfg_attr(not(feature = "sim"), allow(dead_code))]
/// Updates the directory [`install`] filled the card from with the card's current contents, and
/// reports what changed.
///
/// - A file that is new on the card, or differs from the directory's copy, is written out.
/// - A loaded file gone from the card is removed from the directory, unless the directory's copy
///   changed meanwhile, in which case it is kept and reported.
///
/// Call only once nothing is writing the card any more.
pub fn write_back() {
    let Some(loaded) = LOADED.lock().unwrap().take() else {
        return;
    };
    let on_card = embassy_futures::block_on(async {
        let fs = FileSystem::new(BufStream::<Disk, 512>::new(Disk), FsOptions::new())
            .await
            .expect("memory card: mounting the card to write it back");
        let mut on_card = Vec::new();
        read_dir(&fs.root_dir(), Path::new(""), &mut on_card).await;
        on_card
    });

    let mut changed = 0;
    for (path, bytes) in &on_card {
        let host = loaded.tree.join(path);
        if std::fs::read(&host).is_ok_and(|held| held == *bytes) {
            continue;
        }
        if let Some(parent) = host.parent() {
            std::fs::create_dir_all(parent)
                .unwrap_or_else(|e| panic!("memory card: creating {}: {e}", parent.display()));
        }
        std::fs::write(&host, bytes)
            .unwrap_or_else(|e| panic!("memory card: writing {}: {e}", host.display()));
        eprintln!("card: wrote {}", host.display());
        changed += 1;
    }
    let kept: std::collections::HashSet<&PathBuf> = on_card.iter().map(|(path, _)| path).collect();
    for (path, when) in &loaded.files {
        if kept.contains(path) {
            continue;
        }
        let host = loaded.tree.join(path);
        if !host.exists() {
            continue;
        }
        if modified(&host) != *when {
            eprintln!(
                "card: kept {}: deleted on the card, but changed in the directory meanwhile",
                host.display()
            );
            continue;
        }
        std::fs::remove_file(&host)
            .unwrap_or_else(|e| panic!("memory card: removing {}: {e}", host.display()));
        eprintln!("card: removed {}", host.display());
        changed += 1;
    }
    eprintln!(
        "card: {changed} change(s) written back to {}",
        loaded.tree.display()
    );
}

#[cfg_attr(not(feature = "sim"), allow(dead_code))]
/// Appends every file under `dir` to `files`, as its card path and its bytes.
async fn read_dir<IO, TP, OCC>(
    dir: &embedded_fatfs::Dir<'_, IO, TP, OCC>,
    at: &Path,
    files: &mut Vec<(PathBuf, Vec<u8>)>,
) where
    IO: embedded_fatfs::ReadWriteSeek,
    TP: embedded_fatfs::TimeProvider,
    OCC: embedded_fatfs::OemCpConverter,
{
    let mut entries = dir.iter();
    while let Some(entry) = entries.next().await {
        let entry =
            entry.unwrap_or_else(|e| panic!("memory card: listing {}: {e:?}", at.display()));
        let name = entry.file_name();
        if name == "." || name == ".." {
            continue;
        }
        let path = at.join(&name);
        if entry.is_dir() {
            Box::pin(read_dir(&entry.to_dir(), &path, files)).await;
        } else {
            let mut file = entry.to_file();
            let mut bytes = vec![0; entry.len() as usize];
            file.read_exact(&mut bytes)
                .await
                .unwrap_or_else(|e| panic!("memory card: reading {}: {e:?}", path.display()));
            files.push((path, bytes));
        }
    }
}

#[cfg_attr(feature = "sim", allow(dead_code))]
/// Writes the memory disk to the path in `GOLDEN_SD_DUMP`, if set; called on exit.
pub fn dump_if_asked() {
    let Some(path) = std::env::var_os("GOLDEN_SD_DUMP") else {
        return;
    };
    match deluge_bsp::sd::dump_memory_disk(Path::new(&path)) {
        Ok(()) => log::info!("memory card: written to {}", Path::new(&path).display()),
        Err(e) => log::error!("memory card: GOLDEN_SD_DUMP: {e}"),
    }
}
