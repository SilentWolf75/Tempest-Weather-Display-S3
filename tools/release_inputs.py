"""Fingerprint firmware inputs so packaging cannot silently reuse an older build."""
import hashlib
from pathlib import Path

def source_digest(root):
    root = Path(root)
    files = [root / "platformio.ini"]
    for folder in ("src", "include", "boards"):
        files.extend(p for p in (root / folder).rglob("*") if p.is_file())
    digest = hashlib.sha256()
    for path in sorted(files, key=lambda p: p.relative_to(root).as_posix()):
        digest.update(path.relative_to(root).as_posix().encode())
        digest.update(b"\0")
        digest.update(path.read_bytes().replace(b"\r\n", b"\n"))
        digest.update(b"\0")
    return digest.hexdigest()
