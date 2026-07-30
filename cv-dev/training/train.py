#!/usr/bin/env python3
"""Fine-tune YOLOv8n on a teletubby YOLO dataset folder."""

from __future__ import annotations

import argparse
import re
from pathlib import Path

from ultralytics import YOLO


def default_run_name(dataset_dir: Path) -> str:
    """Map dataset folder name to a run/model tag, e.g. 320-dataset -> teletubby-320."""
    stem = dataset_dir.name
    stem = re.sub(r"-?dataset$", "", stem, flags=re.IGNORECASE)
    stem = stem.strip("-_") or "teletubby"
    if stem.lower() == "teletubby" or stem == ".":
        return "teletubby"
    if stem.lower().startswith("teletubby"):
        return stem
    return f"teletubby-{stem}"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--dataset",
        type=Path,
        default=None,
        help="Dataset folder containing data.yaml (e.g. 320-dataset). "
        "Overrides --data and sets a default --name tag from the folder.",
    )
    parser.add_argument(
        "--data",
        type=Path,
        default=Path("dataset/data.yaml"),
        help="YOLO dataset yaml (default: dataset/data.yaml)",
    )
    parser.add_argument(
        "--epochs",
        type=int,
        default=100,
        help="Training epochs (default: 100)",
    )
    parser.add_argument(
        "--imgsz",
        type=int,
        default=None,
        help="Training image size (default: 320 if dataset name contains 320, else 640)",
    )
    parser.add_argument(
        "--batch",
        type=int,
        default=16,
        help="Batch size (default: 16)",
    )
    parser.add_argument(
        "--name",
        type=str,
        default=None,
        help="Run name under runs/detect/ (default derived from --dataset)",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()

    dataset_dir: Path | None = None
    if args.dataset is not None:
        dataset_dir = args.dataset
        args.data = dataset_dir / "data.yaml"
        if args.name is None:
            args.name = default_run_name(dataset_dir)
    elif args.name is None:
        args.name = "teletubby"

    if args.imgsz is None:
        tag = (dataset_dir.name if dataset_dir is not None else args.name).lower()
        args.imgsz = 320 if "320" in tag else 640

    if not args.data.exists():
        raise SystemExit(
            f"Dataset config not found: {args.data}\n"
            "Export a YOLOv8 dataset from Roboflow into a folder (e.g. 320-dataset/) "
            "with data.yaml, or pass --data PATH."
        )

    print(f"Dataset: {args.data}")
    print(f"Run name: {args.name}")
    print(f"imgsz: {args.imgsz}  epochs: {args.epochs}  batch: {args.batch}")

    model = YOLO("yolov8n.pt")
    model.train(
        data=str(args.data.resolve()),
        epochs=args.epochs,
        imgsz=args.imgsz,
        batch=args.batch,
        name=args.name,
    )
    weights = Path("runs/detect") / args.name / "weights" / "best.pt"
    tag = args.name.removeprefix("teletubby-") if args.name.startswith("teletubby-") else args.name
    print(f"Training complete. Best weights: {weights}")
    print(
        "Export with:\n"
        f"  python export_onnx.py --tag {tag} --weights {weights.as_posix()} --imgsz {args.imgsz}"
    )


if __name__ == "__main__":
    main()
