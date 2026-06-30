#!/usr/bin/env python3
"""Convert HEIC photos in dataset/raw/ to JPEG for Roboflow upload."""

from __future__ import annotations

import argparse
from pathlib import Path

from PIL import Image
from pillow_heif import register_heif_opener

register_heif_opener()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--input",
        type=Path,
        default=Path("dataset/raw"),
        help="Folder with .heic/.HEIC files (default: dataset/raw)",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("dataset/jpeg"),
        help="Output folder for .jpg files (default: dataset/jpeg)",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    if not args.input.exists():
        raise SystemExit(f"Input folder not found: {args.input}")

    args.output.mkdir(parents=True, exist_ok=True)
    heic_files = sorted(
        list(args.input.glob("*.heic")) + list(args.input.glob("*.HEIC"))
    )
    if not heic_files:
        raise SystemExit(f"No HEIC files found in {args.input}")

    for path in heic_files:
        out_path = args.output / f"{path.stem}.jpg"
        with Image.open(path) as image:
            image.convert("RGB").save(out_path, "JPEG", quality=92)
        print(f"Converted {path.name} -> {out_path.name}")

    print(f"\nDone. {len(heic_files)} images in {args.output.resolve()}")
    print("Upload the JPEG folder to Roboflow for labeling.")


if __name__ == "__main__":
    main()
