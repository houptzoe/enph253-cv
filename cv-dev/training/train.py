#!/usr/bin/env python3
"""Fine-tune YOLOv8n on the teletubby dataset."""

from __future__ import annotations

import argparse
from pathlib import Path

from ultralytics import YOLO


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
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
        default=640,
        help="Training image size (default: 640)",
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
        default="teletubby",
        help="Run name under runs/detect/ (default: teletubby)",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    if not args.data.exists():
        raise SystemExit(
            f"Dataset config not found: {args.data}\n"
            "Export a YOLOv8 dataset from Roboflow and place data.yaml in dataset/."
        )

    model = YOLO("yolov8n.pt")
    model.train(
        data=str(args.data),
        epochs=args.epochs,
        imgsz=args.imgsz,
        batch=args.batch,
        name=args.name,
    )
    print(f"Training complete. Best weights: runs/detect/{args.name}/weights/best.pt")


if __name__ == "__main__":
    main()
