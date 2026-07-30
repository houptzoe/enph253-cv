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
        default=None,
        help="Path to trained .pt weights "
        "(default: runs/detect/teletubby[-TAG]/weights/best.pt)",
    )
    parser.add_argument(
        "--tag",
        type=str,
        default=None,
        help="Model tag for paths/filename, e.g. 320 -> teletubby-yolov8n-320.onnx",
    )
    parser.add_argument(
        "--imgsz",
        type=int,
        default=None,
        help="Model input size (default: 320 if tag/name contains 320, else 640)",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=None,
        help="Destination ONNX path for mars-cv",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()

    tag = args.tag
    run_name = f"teletubby-{tag}" if tag and tag != "teletubby" else (tag or "teletubby")
    if tag is None:
        run_name = "teletubby"

    if args.weights is None:
        args.weights = Path("runs/detect") / run_name / "weights" / "best.pt"

    if args.imgsz is None:
        hint = f"{tag or ''}{args.weights}".lower()
        args.imgsz = 320 if "320" in hint else 640

    if args.output is None:
        if tag and tag != "teletubby":
            filename = f"teletubby-yolov8n-{tag}.onnx"
        else:
            filename = "teletubby-yolov8n.onnx"
        args.output = Path("../cv-testing/models") / filename

    if not args.weights.exists():
        raise SystemExit(f"Weights not found: {args.weights}")

    print(f"Weights: {args.weights}")
    print(f"imgsz: {args.imgsz}")
    print(f"Output: {args.output}")

    model = YOLO(str(args.weights))
    exported = model.export(format="onnx", imgsz=args.imgsz, simplify=True)

    exported_path = Path(exported)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(exported_path, args.output)
    print(f"Exported ONNX to {args.output.resolve()}")


if __name__ == "__main__":
    main()
