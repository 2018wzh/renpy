"""One-time import of beta.1/beta.2 adapter saves into native Ren'Py paths."""

import os
from pathlib import Path
import shutil


def legacy_save_root(game_root):
    # Match core.cpp's former path_key/game_hash (ASCII folding, UTF-8 FNV-1a).
    key = os.path.normpath(game_root).replace("\\", "/")
    key = key.translate(str.maketrans("ABCDEFGHIJKLMNOPQRSTUVWXYZ", "abcdefghijklmnopqrstuvwxyz"))
    digest = 14695981039346656037
    for byte in key.encode("utf-8"):
        digest = ((digest ^ byte) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    if os.name == "nt":
        base = os.environ.get("LOCALAPPDATA")
        if not base:
            return None
        base = Path(base)
    else:
        home = os.environ.get("HOME")
        if not home:
            return None
        base = Path(home) / ".local" / "share"
    return base / "ShinkiEMU" / "renpy" / f"{digest:016x}"


def _copy_missing(source, destinations):
    if source.is_symlink():
        return
    if source.is_dir():
        for child in source.iterdir():
            _copy_missing(child, [target / child.name for target in destinations])
        return
    # Existing saves in either native location win, regardless of timestamps.
    if any(target.exists() for target in destinations):
        return
    for target in destinations:
        target.parent.mkdir(parents=True, exist_ok=True)
        try:
            output = target.open("xb")
        except FileExistsError:
            continue
        try:
            with output, source.open("rb") as original:
                shutil.copyfileobj(original, output)
        except BaseException:
            target.unlink()
            raise


def migrate_legacy_saves(game_root, save_directories, tokens, backups):
    root = legacy_save_root(game_root)
    if root is None:
        return
    source = root / "saves"
    marker = root / ".native-save-migration-complete"
    if not source.is_dir() or marker.exists():
        return
    destinations = list(dict.fromkeys(Path(path) for path in save_directories if path))
    if not destinations:
        return
    for child in source.iterdir():
        if child.name == "tokens":
            if tokens:
                _copy_missing(child, [Path(tokens)])
        elif child.name == "backups":
            if backups:
                _copy_missing(child, [Path(backups)])
        else:
            _copy_missing(child, [path / child.name for path in destinations])
    try:
        marker.touch(exist_ok=False)
    except FileExistsError:
        pass
