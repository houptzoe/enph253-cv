#!/usr/bin/env python3
"""Export trained YOLO weights to ONNX for mars-cv."""

from __future__ import annotations

import argparse
import shutil
from pathlib import Path

from ultralytics import YOLO


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--weights",
        type=Path,
        default=Path("runs/detect/teletubby/weights/best.pt"),
        help="Path to trained .pt weights",
    )
    parser.add_argument(
        "--imgsz",
        type=int,
        default=640,
        help="Model input size (default: 640)",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("../cv-testing/models/teletubby-yolov8n.onnx"),
        help="Destination ONNX path for mars-cv",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    if not args.weights.exists():
        raise SystemExit(f"Weights not found: {args.weights}")

    model = YOLO(str(args.weights))
    exported = model.export(format="onnx", imgsz=args.imgsz, simplify=True)

    exported_path = Path(exported)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(exported_path, args.output)
    print(f"Exported ONNX to {args.output.resolve()}")


if __name__ == "__main__":
    main()
