"""Check local source integrity and codegen syntax; hardware validation is separate."""
import hashlib
import json
from pathlib import Path

folder = Path(__file__).resolve().parent
metadata = json.loads((folder / "provenance.json").read_text(encoding="utf-8"))
for name, expected in metadata["files"].items():
    assert hashlib.sha256((folder / name).read_bytes()).hexdigest() == expected["local_sha256"], name
compile((folder / "__init__.py").read_bytes(), str(folder / "__init__.py"), "exec")
print("OK: relay source hashes and Python syntax; C++ and physical dual-link tests remain required")
