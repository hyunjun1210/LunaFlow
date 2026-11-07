"""Create stable Unity GUIDs once; preserve existing .meta files on subsequent runs."""
from pathlib import Path
from uuid import UUID, uuid5

root = Path(__file__).resolve().parent.parent
namespace = UUID("a631938d-1e41-4637-a9fb-47ea4ab115a4")
for path in sorted(root.rglob("*")):
    relative = path.relative_to(root)
    if any(p.startswith(".") or p.endswith("~") for p in relative.parts):
        continue
    if path.suffix == ".meta" or path.name == "package.json":
        continue
    meta = Path(str(path) + ".meta")
    if meta.exists():
        continue
    header = "fileFormatVersion: 2\nguid: " + uuid5(namespace, relative.as_posix()).hex + "\n"
    if path.is_dir():
        content = "folderAsset: yes\nDefaultImporter:\n  externalObjects: {}\n  userData:\n  assetBundleName:\n  assetBundleVariant:\n"
    elif path.suffix == ".cs":
        content = "MonoImporter:\n  externalObjects: {}\n  serializedVersion: 2\n  defaultReferences: []\n  executionOrder: 0\n  icon: {instanceID: 0}\n  userData:\n  assetBundleName:\n  assetBundleVariant:\n"
    elif path.suffix == ".asmdef":
        content = "AssemblyDefinitionImporter:\n  externalObjects: {}\n  userData:\n  assetBundleName:\n  assetBundleVariant:\n"
    elif path.suffix == ".shader":
        content = "ShaderImporter:\n  externalObjects: {}\n  defaultTextures: []\n  nonModifiableTextures: []\n  userData:\n  assetBundleName:\n  assetBundleVariant:\n"
    else:
        content = "DefaultImporter:\n  externalObjects: {}\n  userData:\n  assetBundleName:\n  assetBundleVariant:\n"
    meta.write_text(header + content, encoding="utf-8")
    print(relative.as_posix())
