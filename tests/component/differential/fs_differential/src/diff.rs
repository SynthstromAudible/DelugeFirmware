//! Tree comparison between two [`FsOps`] backends: a live mount against another mount or a
//! [`Captured`] known-good tree, compared entry for entry and byte for byte.
use crate::ops::{Entry, FsOps};

/// Compares the whole tree under `/` in `a` and `b`: directory listings and file contents.
///
/// # Errors
///
/// Returns a description of the first divergence, depth-first in sorted-entry order.
pub fn compare_read(a: &dyn FsOps, b: &dyn FsOps) -> Result<(), String> {
    walk(a, b, "/")
}

fn walk(a: &dyn FsOps, b: &dyn FsOps, dir: &str) -> Result<(), String> {
    let (ea, eb) = (a.read_dir(dir), b.read_dir(dir));
    if ea != eb {
        return Err(format!("dir {dir}: got={ea:?} want={eb:?}"));
    }
    for e in &ea {
        let child = child_path(dir, e);
        if e.is_dir {
            walk(a, b, &child)?;
        } else {
            let (fa, fb) = (a.read_file(&child), b.read_file(&child));
            if fa != fb {
                return Err(format!(
                    "file {child}: {} vs {} bytes / content differs",
                    fa.len(),
                    fb.len()
                ));
            }
        }
    }
    Ok(())
}

fn child_path(dir: &str, e: &Entry) -> String {
    if dir == "/" {
        format!("/{}", e.name)
    } else {
        format!("{dir}/{}", e.name)
    }
}

/// A node of a filesystem tree snapshot held by [`Captured`].
pub enum Node {
    File(Vec<u8>),
    Dir(Vec<(Entry, Node)>),
}

/// A filesystem tree snapshot, captured from a live mount or written as a known-good literal, that
/// [`compare_read`] can check a mount against.
pub struct Captured(Node);

impl Captured {
    /// Wraps a hand-built known-good tree.
    pub fn literal(node: Node) -> Self {
        Captured(node)
    }

    /// Snapshots the whole tree of `fs`.
    pub fn capture(fs: &dyn FsOps) -> Self {
        Captured(Self::capture_dir(fs, "/"))
    }

    fn capture_dir(fs: &dyn FsOps, path: &str) -> Node {
        let entries = fs.read_dir(path);
        let mut kids = Vec::with_capacity(entries.len());
        for e in entries {
            let child = child_path(path, &e);
            let node = if e.is_dir {
                Self::capture_dir(fs, &child)
            } else {
                Node::File(fs.read_file(&child))
            };
            kids.push((e, node));
        }
        Node::Dir(kids)
    }

    fn lookup(&self, path: &str) -> &Node {
        let mut cur = &self.0;
        for part in path.split('/').filter(|s| !s.is_empty()) {
            match cur {
                Node::Dir(kids) => {
                    cur = &kids
                        .iter()
                        .find(|(e, _)| e.name == part)
                        .unwrap_or_else(|| {
                            panic!("captured tree: no entry {part:?} on path {path}")
                        })
                        .1;
                }
                Node::File(_) => panic!("captured tree: {path} descends through a file"),
            }
        }
        cur
    }
}

impl FsOps for Captured {
    fn read_file(&self, path: &str) -> Vec<u8> {
        match self.lookup(path) {
            Node::File(b) => b.clone(),
            Node::Dir(_) => panic!("read_file({path}) on a captured directory"),
        }
    }
    fn read_dir(&self, path: &str) -> Vec<Entry> {
        match self.lookup(path) {
            Node::Dir(kids) => kids.iter().map(|(e, _)| e.clone()).collect(),
            Node::File(_) => panic!("read_dir({path}) on a captured file"),
        }
    }
}
