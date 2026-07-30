#!/usr/bin/env python3
"""Download a Roboflow YOLOv8 export and unpack into dataset/."""

from __future__ import annotations

import argparse
import shutil
import tempfile
import urllib.request
import zipfile
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "url",
        help="Roboflow dataset download URL (YOLOv8 export link)",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("dataset"),
        help="Destination folder (default: dataset/; e.g. 320-dataset)",
    )
    return parser.parse_args()


def find_data_yaml(root: Path) -> Path | None:
    direct = root / "data.yaml"
    if direct.exists():
        return direct
    matches = list(root.rglob("data.yaml"))
    return matches[0] if matches else None


def merge_tree(src: Path, dst: Path) -> None:
    for item in src.iterdir():
        target = dst / item.name
        if item.is_dir():
            if target.exists():
                merge_tree(item, target)
            else:
                shutil.copytree(item, target)
        elif not target.exists():
            shutil.copy2(item, target)


def main() -> None:
    args = parse_args()
    args.output.mkdir(parents=True, exist_ok=True)

    print("Downloading from Roboflow...", flush=True)
    with tempfile.TemporaryDirectory() as tmp:
        zip_path = Path(tmp) / "roboflow.zip"
        with urllib.request.urlopen(args.url, timeout=120) as response:
            total = int(response.headers.get("Content-Length", 0))
            downloaded = 0
            chunk_size = 1024 * 1024
            with zip_path.open("wb") as out:
                while True:
                    chunk = response.read(chunk_size)
                    if not chunk:
                        break
                    out.write(chunk)
                    downloaded += len(chunk)
                    if total:
                        pct = downloaded * 100 // total
                        print(
                            f"\r  {downloaded / 1_000_000:.1f} / {total / 1_000_000:.1f} MB ({pct}%)",
                            end="",
                            flush=True,
                        )
        print(f"\nDownloaded {zip_path.stat().st_size / 1_000_000:.1f} MB", flush=True)

        extract_root = Path(tmp) / "extracted"
        with zipfile.ZipFile(zip_path) as archive:
            archive.extractall(extract_root)

        data_yaml = find_data_yaml(extract_root)
        if data_yaml is None:
            raise SystemExit("No data.yaml found in the Roboflow export.")

        export_root = data_yaml.parent
        print(f"Unpacking export from {export_root.name}/ into {args.output}/")
        merge_tree(export_root, args.output)

    final_yaml = args.output / "data.yaml"
    if not final_yaml.exists():
        shutil.copy2(data_yaml, final_yaml)

    # Roboflow often writes ../train/images; normalize to paths relative to this folder.
    text = final_yaml.read_text(encoding="utf-8")
    replacements = {
        "train: ../train/images": "train: train/images",
        "val: ../valid/images": "val: valid/images",
        "test: ../test/images": "test: test/images",
        "val: ../val/images": "val: val/images",
    }
    for old, new in replacements.items():
        text = text.replace(old, new)
    final_yaml.write_text(text, encoding="utf-8")

    train_images = list((args.output / "train" / "images").glob("*"))
    if not train_images:
        train_images = list((args.output / "images" / "train").glob("*"))
    val_images = list((args.output / "valid" / "images").glob("*"))
    if not val_images:
        val_images = list((args.output / "images" / "val").glob("*"))
    print(f"Ready: {len(train_images)} train images, {len(val_images)} val images")
    print(f"Train with: python train.py --dataset {args.output}")


if __name__ == "__main__":
    main()
