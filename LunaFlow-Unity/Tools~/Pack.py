"""Make a transfer ZIP without Unity caches, compiled output or parent-project files."""
import hashlib
import json
from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile

root = Path(__file__).resolve().parent.parent
version = json.loads((root / "package.json").read_text(encoding="utf-8"))["version"]
destination = root / "Dist~" / ("LunaFlow-Unity-" + version + ".zip")
destination.parent.mkdir(parents=True, exist_ok=True)
excluded = {"Dist~", ".test-project", ".git", ".vs", ".build", "bin", "obj", "Library", "Temp", "Logs", "UserSettings", "TestResults", "__pycache__"}
files = [p for p in sorted(root.rglob("*")) if p.is_file() and not excluded.intersection(p.relative_to(root).parts)]
manifest = {}
with ZipFile(destination, "w", ZIP_DEFLATED) as archive:
    for path in files:
        relative = path.relative_to(root).as_posix()
        data = path.read_bytes()
        manifest[relative] = hashlib.sha256(data).hexdigest()
        archive.writestr("LunaFlow-Unity/" + relative, data)
    archive.writestr("LunaFlow-Unity/TRANSFER-SHA256.json", json.dumps(manifest, indent=2) + "\n")
with ZipFile(destination) as archive:
    assert archive.testzip() is None
    for name, digest in manifest.items():
        assert hashlib.sha256(archive.read("LunaFlow-Unity/" + name)).hexdigest() == digest
print(str(destination))
print(str(len(files)) + " files; ZIP integrity and SHA-256 checked.")
