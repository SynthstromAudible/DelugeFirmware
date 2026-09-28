//! Recursive read/enumerate walker: walks a whole tree through an `ops::FsOps`
//! backend and compares it against another `FsOps` -- either a second live
//! mount, or a hardcoded [`Captured`] tree literal built from known-good
//! expected bytes -- asserting agreement byte-for-byte on file contents and
//! entry-for-entry on directory listings.
use crate::ops::{Entry, FsOps};

/// Walk the whole tree rooted at `/` through both `a` and `b`, comparing
/// directory listings and file contents at every level. Returns `Err` with a
/// precise description of the FIRST divergence found (depth-first, in
/// sorted-entry order), or `Ok(())` if the two stacks agree everywhere.
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

/// A backend-agnostic snapshot of a filesystem tree, either captured live
/// (via `FsOps`) or built directly as a hardcoded known-good literal (see
/// [`Captured::literal`]) so `compare_read` can check a live mount against a
/// fixed expectation without needing a second live backend to diff against.
pub enum Node {
    File(Vec<u8>),
    Dir(Vec<(Entry, Node)>),
}

pub struct Captured(Node);

impl Captured {
    /// Wrap a hand-built [`Node`] tree (known-good expected bytes/listing) as
    /// an `FsOps` `compare_read` can diff a live mount against.
    pub fn literal(node: Node) -> Self {
        Captured(node)
    }

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
