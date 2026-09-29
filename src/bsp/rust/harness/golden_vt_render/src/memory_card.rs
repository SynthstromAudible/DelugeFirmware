//! The harness's SD card: a sparse in-memory disk, formatted FAT32 and filled with a fixture tree in-process.
//!
//! The gates exercise the real storage stack -- `embedded-fatfs` over a block device that mirrors the device's -- so
//! the card stays a FAT volume with a real card's geometry: 32 KB clusters, and at least 2.5 GB (FAT32 needs 65,525
//! clusters at that size). It lives in memory (`deluge_bsp::sd::use_memory_disk`), keeping only the sectors written,
//! so nothing is left in the temp dir and no run sees what an earlier run wrote. `DELUGE_SD_IMAGE`, when set, still
//! names a file to use instead, and `GOLDEN_SD_DUMP=<path>` writes the memory disk out when the run exits.
//!
//! Everything here runs before boot, from `main`, before any executor exists: the block device below reads and
//! writes the disk directly, without the `SD_BUS` lock or the latency model the booted stack goes through.

use std::path::Path;

use aligned::{A4, Aligned};
use block_device_adapters::BufStream;
use block_device_driver::BlockDevice;
use embedded_fatfs::{FatType, FileSystem, FormatVolumeOptions, FsOptions, format_volume};
use embedded_io_async::Write as _;

const SECTOR_BYTES: u64 = 512;
const CLUSTER_BYTES: u32 = 32 * 1024;
/// Comfortably over FAT32's 65,525 clusters at 32 KB each.
const MIN_BYTES: u64 = 2560 << 20;

/// The disk as a block device, before boot.
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
        // SAFETY: `Aligned<A4, [u8; 512]>` is `#[repr(C)]` over `[u8; 512]` plus a zero-sized marker, so the slice is
        // a contiguous run of 512-byte blocks; the flat view does not outlive `data`. The same reinterpretation as
        // `efatfs_host_shim::HostSdBlockDevice`.
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

/// The volume's size for `tree`: twice what the tree holds plus 64 MB, and never under [`MIN_BYTES`].
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

/// Stand the card in with a memory disk, format it FAT32, and copy `tree` (a card's contents: `SONGS/`, `SAMPLES/`,
/// ...) onto it. `None` leaves the card formatted and empty.
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
            copy_dir(&fs.root_dir(), tree).await;
            fs.unmount()
                .await
                .expect("memory card: unmounting after the copy");
        }
    });
    log::info!(
        "golden_vt_render: memory card, {sectors} sectors, filled from {}",
        tree.map_or("nothing".into(), |t| t.display().to_string())
    );
}

/// Copy `from`'s entries into `into`, in name order so the card's directory order is the same on every run.
async fn copy_dir<IO, TP, OCC>(into: &embedded_fatfs::Dir<'_, IO, TP, OCC>, from: &Path)
where
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
            Box::pin(copy_dir(&dir, &entry.path())).await;
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
        }
    }
}

/// Write the memory disk to `GOLDEN_SD_DUMP` when that names a path. Called on exit.
pub fn dump_if_asked() {
    let Some(path) = std::env::var_os("GOLDEN_SD_DUMP") else {
        return;
    };
    match deluge_bsp::sd::dump_memory_disk(Path::new(&path)) {
        Ok(()) => log::info!(
            "golden_vt_render: memory card written to {}",
            Path::new(&path).display()
        ),
        Err(e) => log::error!("golden_vt_render: GOLDEN_SD_DUMP: {e}"),
    }
}
