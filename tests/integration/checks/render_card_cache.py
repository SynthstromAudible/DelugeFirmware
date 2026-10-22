"""Generated render cards, made once per generator and kept under the build tree.

tests/integration/golden/songs/make_render_song.py is deterministic: a card depends only on the song's name and the
generator's source. So each card is generated once into build/render-cards/<generator key>/<SONG>/ and every later run
plays that directory read-only. The samples every song shares are synthesised once per key, into <key>/samples/, and
hard-linked into each card.

Gates fill the cache concurrently: each directory and file is built under a temporary name and published with a
rename, and a rename that loses the race leaves the winner's in place.
"""

import hashlib
import importlib.util
import os
import shutil
import threading
from pathlib import Path

GENERATOR = Path("tests/integration/golden/songs/make_render_song.py")

_loaded: dict[str, object] = {}
_loading = threading.Lock()


def generator_key(root: Path) -> str:
    """The cache key for the generator under @p root: a prefix of its source's sha256 (it imports only the standard
    library, so its source is its whole input)."""
    return hashlib.sha256((root / GENERATOR).read_bytes()).hexdigest()[:16]


def _generator(root: Path):
    """The generator module under @p root, loaded once per key."""
    key = generator_key(root)
    with _loading:
        if key not in _loaded:
            spec = importlib.util.spec_from_file_location(
                f"make_render_song_{key}", root / GENERATOR
            )
            module = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(module)
            _loaded[key] = module
        return _loaded[key]


def _temporary(path: Path) -> Path:
    return path.with_name(f"{path.name}.tmp-{os.getpid()}-{threading.get_ident()}")


def _publish_dir(tmp: Path, final: Path) -> None:
    try:
        os.rename(tmp, final)
    except OSError:
        if not final.is_dir():
            raise
        shutil.rmtree(tmp)


def _sample(base: Path, generator, path: str) -> Path:
    """The shared sample at card path @p path, synthesised once per key."""
    sample = base / "samples" / path
    if not sample.is_file():
        sample.parent.mkdir(parents=True, exist_ok=True)
        tmp = _temporary(sample)
        tmp.write_bytes(generator.SAMPLE_FILES[path]())
        os.replace(tmp, sample)
    return sample


def _base(cache_root: Path, key: str) -> Path:
    """The key's directory, made on first use; making it removes every other key's."""
    base = cache_root / key
    if not base.is_dir():
        cache_root.mkdir(parents=True, exist_ok=True)
        base.mkdir(exist_ok=True)
        for stale in cache_root.iterdir():
            if stale.name != key and ".tmp-" not in stale.name:
                shutil.rmtree(stale, ignore_errors=True)
    return base


def card(root: Path, song: str, cache_root: Path | None = None) -> Path:
    """The card directory that plays generated song @p song: what `make_render_song.py <out> --song <song>` writes,
    byte for byte. Callers must not write into it.

    @p cache_root defaults to build/render-cards under @p root. Raises ValueError for a song the generator does not
    know."""
    generator = _generator(root)
    if song != "RS_STREAM" and song not in generator.SONGS:
        raise ValueError(f"unknown song {song}")
    base = _base(cache_root or root / "build" / "render-cards", generator_key(root))
    final = base / song
    if final.is_dir():
        return final

    tmp = _temporary(final)
    shutil.rmtree(tmp, ignore_errors=True)
    for path in generator.samples_for(song):
        (tmp / path).parent.mkdir(parents=True, exist_ok=True)
        os.link(_sample(base, generator, path), tmp / path)
    generator.write_song(tmp, song)
    _publish_dir(tmp, final)
    return final
