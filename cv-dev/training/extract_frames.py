#!/usr/bin/env python3
"""Extract frames from a video for labeling."""

from __future__ import annotations

import argparse
from pathlib import Path

import cv2


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("video", type=Path, help="Input video file")
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("dataset/raw"),
        help="Directory for extracted JPEG frames",
    )
    parser.add_argument(
        "--every",
        type=int,
        default=10,
        help="Save one frame every N frames (default: 10)",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    args.output.mkdir(parents=True, exist_ok=True)

    capture = cv2.VideoCapture(str(args.video))
    if not capture.isOpened():
        raise SystemExit(f"Failed to open video: {args.video}")

    frame_index = 0
    saved = 0
    while True:
        ok, frame = capture.read()
        if not ok:
            break

        if frame_index % args.every == 0:
            output_path = args.output / f"{args.video.stem}_{frame_index:06d}.jpg"
            cv2.imwrite(str(output_path), frame)
            saved += 1

        frame_index += 1

    capture.release()
    print(f"Saved {saved} frames to {args.output}")


if __name__ == "__main__":
    main()
