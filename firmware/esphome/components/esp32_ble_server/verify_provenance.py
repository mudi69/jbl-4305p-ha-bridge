"""Verify copied source integrity and Python syntax; this is not a C++ build."""

import hashlib
import json
from pathlib import Path


if __name__ == "__main__":
    folder = Path(__file__).resolve().parent
    metadata = json.loads((folder / "provenance.json").read_text(encoding="utf-8"))
    for name, expected in metadata["files"].items():
        assert hashlib.sha256((folder / name).read_bytes()).hexdigest() == expected["local_sha256"], name
    assert hashlib.sha256((folder / "LICENSE").read_bytes()).hexdigest() == metadata["license"]["sha256"]
    source = folder / "__init__.py"
    compile(source.read_bytes(), str(source), "exec")
    print(f"OK: {len(metadata['files'])} source hashes, license hash and Python syntax; C++ build still required")
